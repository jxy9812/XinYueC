/** @file       XUdpBindFamilyTest.c
 *  @brief      bindAny 地址族选择与 IPv4-only 回退回归（--test udp-bind-family）。
 *  @details    背景：2026-11-04「XAbstractSocket 全量对齐 Qt 6.8」把
 *              `XAbstractSocket_bindAny()` 改成恒绑双栈 AnyIPv6；而 Qt 的
 *              「Any = 双栈」是**编译期**能力（QT_CONFIG(ipv6) 关闭时
 *              QHostAddress::Any 落 IPv4-Any），运行期没有回退。全志 A33
 *              5.2.9 裁剪内核没有 IPv6 栈（无 /proc/net/if_inet6，
 *              socket(AF_INET6) 直接 EAFNOSUPPORT），于是屏上：
 *                · XGuiDemo NTP 校时恒报「套接字创建/发送失败」；
 *                · 远程服务 46300 恒 `remote-server listen FAILED`。
 *              修复后 bindAny 双栈失败即回退 0.0.0.0。
 *
 *              本用例锁两条红线：
 *                1. 双栈可用时 bindAny 仍优先落 IPv6（不推翻 11-04 的 IPv6
 *                   修复；IPv4-only 开发机则记录为「无 IPv6，已回退」）；
 *                2. `XNET_DISABLE_IPV6=1` 时必须落 IPv4-Any，且回退后的
 *                   套接字能真正收发（127.0.0.1 双向投递）。
 *  @note        返回值：失败用例数（0 = 全通过），供 --test 与 CI 判定。
 *  @author      XinYueC 团队
 */
#include "XIOTest.h"
#include "XUdpSocket.h"
#include "XAbstractSocket.h"
#include "XHostAddress.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <stdlib.h>
#include <string.h>

/** @brief 通过计数。 */
static int g_bindPassed = 0;
/** @brief 失败计数。 */
static int g_bindFailed = 0;

/**
 * @brief 断言宏：条件为真记通过，否则打印用例名并累加失败。
 */
#define BIND_CHECK(cond, desc)                                              \
    do {                                                                    \
        if (cond) {                                                         \
            g_bindPassed++;                                                 \
            XPrintf("  [PASS] %s\n", desc);                                 \
        } else {                                                            \
            g_bindFailed++;                                                 \
            XPrintf("  [FAIL] %s\n", desc);                                 \
        }                                                                   \
    } while (0)

/**
 * @brief 设置/清除 IPv6 强制关闭开关（与 bindAny 的 env 探针同名）。
 * @param disableIpv6 非 0 时置 "1"（强制 IPv4），为 0 时置 "0"（恢复双栈优先）。
 */
static void bind_family_env(int disableIpv6)
{
#ifdef _WIN32
    _putenv_s("XNET_DISABLE_IPV6", disableIpv6 ? "1" : "0");
#else
    setenv("XNET_DISABLE_IPV6", disableIpv6 ? "1" : "0", 1);
#endif
}

/** @brief 服务端最近一次收到的报文。 */
static char g_bindRx[64];
/** @brief 服务端收包次数。 */
static int g_bindRxCount = 0;

/** @brief 服务端 readyRead：抽干待收数据报。 */
static void bind_family_server_readyRead(XObject* sender, XVarList* args)
{
    XUdpSocket* sock = (XUdpSocket*)sender;
    (void)args;
    while (XUdpSocket_hasPendingDatagrams(sock)) {
        int64_t got = XUdpSocket_readDatagram(sock, g_bindRx,
                                             (int64_t)sizeof(g_bindRx) - 1,
                                             NULL, NULL);
        if (got <= 0) break;
        g_bindRx[got] = '\0';
        g_bindRxCount++;
    }
}

/** @brief 驱动事件循环直到收到报文或轮次用尽（每轮 5ms）。 */
static void bind_family_pump(int maxRounds)
{
    int i;
    for (i = 0; i < maxRounds && g_bindRxCount == 0; ++i) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
    }
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/** @brief 取本地地址族（绑定成功后回读，未知返回 Unknown）。 */
static XHostAddress_NetworkLayerProtocol bind_family_localProtocol(XUdpSocket* sock)
{
    const XHostAddress* local = XAbstractSocket_localAddress((XAbstractSocket*)sock);
    if (!local) return XHostAddress_UnknownNetworkLayerProtocol;
    return XHostAddress_protocol(local);
}

/**
 * @brief 用例一：默认（双栈优先）bindAny 必须成功，族别随环境自适应。
 * @details 双栈机器应落 IPv6；IPv4-only 机器由回退落 IPv4，两者都算通过，
 *          只有「绑定失败」才是红线。
 */
static void bind_family_test_default(void)
{
    XUdpSocket* sock;
    XHostAddress_NetworkLayerProtocol proto;

    XPrintf("\n--- 用例一：默认 bindAny（双栈优先）---\n");
    bind_family_env(0);
    sock = XUdpSocket_create();
    if (!sock) {
        BIND_CHECK(false, "创建 UDP socket");
        return;
    }
    BIND_CHECK(XUdpSocket_bindAny(sock, 0, XAbstractSocket_DefaultForPlatform),
               "默认 bindAny 绑定成功");
    proto = bind_family_localProtocol(sock);
    if (proto == XHostAddress_IPv6Protocol) {
        BIND_CHECK(true, "双栈可用：bindAny 落 IPv6（11-04 双栈语义保持）");
    } else if (proto == XHostAddress_IPv4Protocol) {
        BIND_CHECK(true, "本机无可用 IPv6：bindAny 回退 IPv4（IPv4-only 内核语义）");
    } else {
        BIND_CHECK(false, "绑定后本地地址族可读");
    }
    XUdpSocket_abort(sock);
    XClassDelete((XClass*)sock);
}

/**
 * @brief 用例二：XNET_DISABLE_IPV6=1 时必须落 IPv4-Any。
 * @details 用 env 覆盖把「IPv4-only 内核」在双栈开发机上确定性复现（内核
 *          缺 IPv6 时双栈 bind 的首绑必然失败，等价于本用例强制跳过首绑）。
 */
static void bind_family_test_forced_ipv4(void)
{
    XUdpSocket* sock;
    XHostAddress_NetworkLayerProtocol proto;

    XPrintf("\n--- 用例二：XNET_DISABLE_IPV6=1 强制回退 IPv4-Any ---\n");
    bind_family_env(1);
    sock = XUdpSocket_create();
    if (!sock) {
        BIND_CHECK(false, "创建 UDP socket");
        bind_family_env(0);
        return;
    }
    BIND_CHECK(XUdpSocket_bindAny(sock, 0, XAbstractSocket_DefaultForPlatform),
               "回退路径 bindAny 绑定成功");
    proto = bind_family_localProtocol(sock);
    BIND_CHECK(proto == XHostAddress_IPv4Protocol,
               "回退后本地地址族为 IPv4");
    BIND_CHECK(XUdpSocket_localPort(sock) > 0,
               "回退后取得实际绑定端口");
    XUdpSocket_abort(sock);
    XClassDelete((XClass*)sock);
}

/**
 * @brief 用例三：回退出来的 IPv4 套接字必须能真实收发。
 * @details 服务端显式绑 127.0.0.1（IPv4），客户端走「强制 IPv4」的
 *          bindAny，验证回退不是「绑上了但发不出去」的半残状态。
 */
static void bind_family_test_loopback_delivery(void)
{
    XUdpSocket* server = XUdpSocket_create();
    XUdpSocket* client = XUdpSocket_create();
    XHostAddress serverAddr;
    XHostAddress_NetworkLayerProtocol proto;
    uint16_t serverPort = 0;
    int64_t sent = -1;

    XPrintf("\n--- 用例三：回退后的 IPv4 套接字双向投递 ---\n");
    bind_family_env(1);
    if (!server || !client) {
        BIND_CHECK(false, "创建 UDP socket 对");
        goto cleanup;
    }
    XHostAddress_init(&serverAddr);
    XHostAddress_setAddress(&serverAddr, "127.0.0.1");
    if (!XAbstractSocket_bind_base((XAbstractSocket*)server, &serverAddr, 0,
                                   XAbstractSocket_DefaultForPlatform)) {
        BIND_CHECK(false, "服务端绑 127.0.0.1");
        goto cleanup;
    }
    serverPort = (uint16_t)XUdpSocket_localPort(server);
    BIND_CHECK(serverPort > 0, "服务端取得回环端口");
    XObject_connect_2(server, XSignal(XIODevice_readyRead_signal),
                      bind_family_server_readyRead);

    BIND_CHECK(XUdpSocket_bindAny(client, 0, XAbstractSocket_DefaultForPlatform),
               "客户端走回退路径绑定成功");
    proto = bind_family_localProtocol(client);
    BIND_CHECK(proto == XHostAddress_IPv4Protocol,
               "客户端本地地址族为 IPv4");

    g_bindRxCount = 0;
    g_bindRx[0] = '\0';
    sent = XUdpSocket_writeDatagram(client, "ping", 4, &serverAddr, serverPort);
    BIND_CHECK(sent == 4, "回退套接字定向发送 4 字节");
    bind_family_pump(200);
    BIND_CHECK(g_bindRxCount > 0 && strcmp(g_bindRx, "ping") == 0,
               "服务端收到回环报文 ping");

cleanup:
    bind_family_env(0);
    XClassDeinit(&serverAddr);
    if (server) { XUdpSocket_abort(server); XClassDelete((XClass*)server); }
    if (client) { XUdpSocket_abort(client); XClassDelete((XClass*)client); }
}

/**
 * @brief 运行全部 bindAny 地址族用例。
 * @return 失败用例数（0 表示全部通过）。
 */
int XUdpBindFamilyTest_run(void)
{
    XPrintf("========== UDP bindAny 地址族 / IPv4 回退回归开始 ==========\n");
    g_bindPassed = 0;
    g_bindFailed = 0;
    bind_family_env(0);

    bind_family_test_default();
    bind_family_test_forced_ipv4();
    bind_family_test_loopback_delivery();

    XPrintf("========== 结果：%d 通过 / %d 失败 ==========\n",
            g_bindPassed, g_bindFailed);
    return g_bindFailed;
}

/** @brief Test 菜单动作入口。 */
void XUdpBindFamilyTest(void)
{
    (void)XUdpBindFamilyTest_run();
}
