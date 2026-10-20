/** @file       XUdpDirectedTest.c
 * @brief      UDP 定向收发与源地址校验回归测试（命令行可运行）。
 * @details    针对 Linux io_uring 驱动的三处历史缺陷建立红线：
 *
 *   1. 目的地址被丢弃：startAsyncWrite 把 UDP 目标地址算出来后仍提交
 *      IORING_OP_SEND，导致 sendDatagram 无法定向发包（NTP 等一切
 *      「发给指定服务器」的用法都走不通）。
 *   2. UDP connect 未真正落 connect()：内核不路由报文、也不按对端过滤，
 *      收包来源无从校验。
 *   3. platformGetLastDatagramSender 返回未初始化内存：无条件 true 并把
 *      垃圾字节当作 IPv4 地址交给上层。
 *
 *   本测试覆盖两条路径：
 *      - 未连接 socket 的 writeDatagram(显式目标地址) → 验证 SENDTO 修复
 *      - 已连接 socket 的 connectToHost + 流式写 → 验证 connect 修复与
 *        源地址上报
 *
 * @note        退出码：0 全部通过；非 0 存在失败（供 CTest / CI 判定）。
 * @author      XinYueC 团队
 */
#include "XIOTest.h"
#include "XUdpSocket.h"
#include "XHostAddress.h"
#include "XNetworkDatagram.h"
#include "XByteArray.h"
#include "XCoreApplication.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>

/** @brief 通过计数。 */
static int g_passed = 0;
/** @brief 失败计数。 */
static int g_failed = 0;

/**
 * @brief 断言宏：条件为真记通过，否则打印用例名并累加失败。
 */
#define UDP_CHECK(cond, desc)                                               \
    do {                                                                    \
        if (cond) {                                                         \
            g_passed++;                                                     \
            XPrintf("  [PASS] %s\n", desc);                                 \
        } else {                                                            \
            g_failed++;                                                     \
            XPrintf("  [FAIL] %s\n", desc);                                 \
        }                                                                   \
    } while (0)

/** 服务端收到的数据。 */
static char g_recvBuffer[256];
/** 服务端收到的字节数。 */
static int g_recvSize = 0;
/** 服务端收到数据的次数。 */
static int g_recvCount = 0;

/**
 * @brief 服务端 readyRead 回调：抽干待收数据报。
 */
static void udp_server_readyRead(XObject* sender, XVarList* args)
{
    XUdpSocket* sock = (XUdpSocket*)sender;
    (void)args;
    while (XUdpSocket_hasPendingDatagrams(sock)) {
        int64_t size = XUdpSocket_pendingDatagramSize(sock);
        if (size <= 0) break;
        if (size > (int64_t)sizeof(g_recvBuffer)) size = (int64_t)sizeof(g_recvBuffer);
        int64_t got = XUdpSocket_readDatagram(sock, g_recvBuffer, size,
                                              NULL, NULL);
        if (got > 0) {
            g_recvSize = (int)got;
            g_recvCount++;
        }
    }
}

/**
 * @brief 驱动事件循环，直到超时或条件满足。
 * @param condition 终止条件（为 true 时提前退出）。
 * @param maxRounds 最大轮次（每轮 5ms）。
 */
static void pump_until(bool condition, int maxRounds)
{
    int i;
    for (i = 0; i < maxRounds && !condition; i++) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
    }
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/**
 * @brief 用例一：未连接 socket 的显式目标地址发送（验证 SENDTO 修复）。
 * @details 修复前该路径会丢弃目的地址、报文无法送达，本用例即为红线。
 */
static void test_udp_write_to_address(void)
{
    XUdpSocket* server = XUdpSocket_create();
    XUdpSocket* client = XUdpSocket_create();

    XPrintf("\n--- 用例一：writeDatagram 定向发送 ---\n");
    if (!server || !client) {
        UDP_CHECK(false, "创建 UDP socket 对");
        if (server) { XUdpSocket_abort(server); XClassDelete((XClass*)server); }
        if (client) { XUdpSocket_abort(client); XClassDelete((XClass*)client); }
        return;
    }

    g_recvSize = 0;
    g_recvCount = 0;

    if (!XUdpSocket_bindAny(server, 0, XAbstractSocket_DefaultForPlatform)) {
        UDP_CHECK(false, "服务端绑定任意端口");
        XUdpSocket_abort(server); XClassDelete((XClass*)server);
        XUdpSocket_abort(client); XClassDelete((XClass*)client);
        return;
    }
    uint16_t port = (uint16_t)XUdpSocket_localPort(server);
    UDP_CHECK(port > 0, "服务端取得实际绑定端口");

    XObject_connect_2((XObject*)server, XSignal(XIODevice_readyRead_signal),
                      udp_server_readyRead);

    /* 客户端未连接：显式指定目标地址发送，这正是 SENDTO 修复覆盖的路径。
       约定：XUdpSocket 在 bind 或 connect 之前不创建底层 fd，因此发送侧
       必须先 bind（否则没有可用的 socket，写返回 -1）。 */
    {
        XHostAddress dest;
        UDP_CHECK(XUdpSocket_bindAny(client, 0,
                                     XAbstractSocket_DefaultForPlatform),
                  "发送侧先绑定（bind/connect 之前无底层 fd）");

        XHostAddress_init(&dest);
        XHostAddress_setAddressSpecial(&dest, XHostAddress_LocalHostSpecial);
        int64_t sent = XUdpSocket_writeDatagram(client, "direct-send-probe", 17,
                                                &dest, port);
        UDP_CHECK(sent == 17, "writeDatagram 返回发送字节数 17");

        pump_until(g_recvCount > 0, 200);
        UDP_CHECK(g_recvCount == 1, "服务端收到定向数据报");
        UDP_CHECK(g_recvSize == 17
                      && memcmp(g_recvBuffer, "direct-send-probe", 17) == 0,
                  "数据报内容逐字节无损");
        XClassDeinit(&dest);
    }

    XUdpSocket_abort(server);
    XUdpSocket_abort(client);
    XClassDelete((XClass*)server);
    XClassDelete((XClass*)client);
    pump_until(false, 2);
}

/**
 * @brief 用例二：已连接 socket 的流式收发（验证 connect 修复）。
 */
static void test_udp_connected_stream(void)
{
    XUdpSocket* server = XUdpSocket_create();
    XUdpSocket* client = XUdpSocket_create();

    XPrintf("\n--- 用例二：connectToHost 后收发 ---\n");
    if (!server || !client) {
        UDP_CHECK(false, "创建 UDP socket 对");
        if (server) { XUdpSocket_abort(server); XClassDelete((XClass*)server); }
        if (client) { XUdpSocket_abort(client); XClassDelete((XClass*)client); }
        return;
    }

    g_recvSize = 0;
    g_recvCount = 0;

    if (!XUdpSocket_bindAny(server, 0, XAbstractSocket_DefaultForPlatform)) {
        UDP_CHECK(false, "服务端绑定任意端口");
        XUdpSocket_abort(server); XClassDelete((XClass*)server);
        XUdpSocket_abort(client); XClassDelete((XClass*)client);
        return;
    }
    uint16_t port = (uint16_t)XUdpSocket_localPort(server);

    XObject_connect_2((XObject*)server, XSignal(XIODevice_readyRead_signal),
                      udp_server_readyRead);

    /* 客户端真正 connect：内核负责路由到对端，并过滤非对端来源。
       连接建立是异步的（先 HostLookup 再 Connecting 再 Connected），
       必须泵送事件循环等到终态，不能只靠固定轮次。 */
    XAbstractSocket_connectToHost_base((XAbstractSocket*)client, "127.0.0.1",
                                       port, XIODevice_ReadWrite,
                                       XAbstractSocket_IPv4Protocol);
    pump_until(XUdpSocket_state(client) == XAbstractSocket_ConnectedState, 300);
    UDP_CHECK(XUdpSocket_state(client) == XAbstractSocket_ConnectedState,
              "UDP socket 异步连接抵达 Connected 终态");
    UDP_CHECK(XUdpSocket_peerPort(client) == port, "对端端口与服务器一致");

    {
        int64_t sent = XUdpSocket_write_1((XIODevice*)client, "connected-udp", 12);
        UDP_CHECK(sent == 12, "已连接 UDP 流式写返回 12 字节");

        pump_until(g_recvCount > 0, 300);
        UDP_CHECK(g_recvCount == 1, "服务端收到已连接 socket 的数据报");
        UDP_CHECK(g_recvSize == 12
                      && memcmp(g_recvBuffer, "connected-udp", 12) == 0,
                  "已连接 UDP 内容逐字节无损");
    }

    /* 源地址上报：已连接 socket 只能收到对端数据，故上报对端即为真值。 */
    /* 源地址上报：服务端回一条包，客户端收下后核验来源即对端。
       这是驱动「platformGetLastDatagramSender 不再返回未初始化内存」
       修复的可观测点：修复前此处读到的是垃圾字节。 */
    {
        XHostAddress toClient;
        XHostAddress_init(&toClient);
        XHostAddress_setAddressSpecial(&toClient, XHostAddress_LocalHostSpecial);
        XUdpSocket_writeDatagram(server, "server-reply", 12, &toClient,
                                 (uint16_t)XUdpSocket_localPort(client));
        XClassDeinit(&toClient);

        pump_until(XUdpSocket_bytesAvailable_base((XIODevice*)client) > 0, 300);
        UDP_CHECK(XUdpSocket_bytesAvailable_base((XIODevice*)client) > 0,
                  "客户端收到服务端回包");

        {
            XNetworkDatagram* dg = XUdpSocket_receiveDatagram(client, 256);
            UDP_CHECK(dg != NULL, "receiveDatagram 取回服务端数据报");
            if (dg) {
                UDP_CHECK(XNetworkDatagram_data(dg) != NULL
                              && XByteArray_size_base(XNetworkDatagram_data(dg)) == 12,
                          "回包载荷长度正确（12 字节）");
                /* 来源必须是已连接的对端 */
                UDP_CHECK(XNetworkDatagram_senderPort(dg) == port,
                          "数据报来源端口为已连接对端端口");
                XClassDelete((XClass*)dg);
            }
        }
    }

    XUdpSocket_abort(server);
    XUdpSocket_abort(client);
    XClassDelete((XClass*)server);
    XClassDelete((XClass*)client);
    pump_until(false, 2);
}

/**
 * @brief 运行全部 UDP 定向收发用例。
 * @return 失败计数（0 表示全部通过）。
 */
int XUdpDirectedTest_run(void)
{
    XPrintf("========== UDP 定向收发回归测试开始 ==========\n");
    g_passed = 0;
    g_failed = 0;

    test_udp_write_to_address();
    test_udp_connected_stream();

    XPrintf("\n========== UDP 定向收发测试完成: %d 通过, %d 失败 ==========\n",
            g_passed, g_failed);
    return g_failed;
}

/**
 * @brief Test 菜单动作入口。
 */
void XUdpDirectedTest(void)
{
    (void)XUdpDirectedTest_run();
}
