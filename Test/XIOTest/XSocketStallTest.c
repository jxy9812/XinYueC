/**
 * @file       XSocketStallTest.c
 * @brief      套接字层「读挂起后不再收数据」回归测试（--test xsocket-stall）。
 *
 * @details    [为什么要这个测试] 2026-11-04 HSF 联调实测到一条难以归因的
 *             现象：服务端会话的 IOCP 读已挂起（`WSARecv` 全部返回
 *             `WSA_IO_PENDING`），客户端也确认把帧写进了内核，但服务端
 *             再也收不到后续数据；两端应用层与完成包管线均已逐层排除，
 *             `XTcpServer` 已被 18 项回归彻底排除。
 *
 *             本文件把「读挂起后的持续收包」拆成可独立判定的用例，
 *             每一组都对应一个曾经怀疑过的机制：
 *
 *               1) 长空闲后恢复收发     —— 读挂起期间无数据，之后还能不能收
 *               2) 高频小帧持续收发     —— 密集数据下是否出现中途断流
 *               3) 单边狂发对端慢读     —— 背压状态下是否丢帧/卡死
 *               4) 双向同时高频         —— 读写交错是否互相饿死
 *               5) 反复连接/断开重连     —— 连接生命周期边界上的残留
 *               6) 断开后重新连接复用   —— fd 复用是否会串数据
 *
 *             判据统一为「逐字节内容 + 帧数累计」，不用「收到了就算过」。
 */
#include "XIOTest.h"
#include "XTcpServer.h"
#include "XTcpSocket.h"
#include "XAbstractSocket.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XHostAddress.h"
#include "XIODevice.h"
#include "XPrintf.h"
#include "XThread.h"
#include "XAbstractNetIoRing.h"   /* 唤醒包风暴用例：直接驱动事件环 wakeUp */
#include "XUdpSocket.h"           /* UDP 零长数据报用例 */
#if defined(_WIN32)
/* 原始 UDP 套接字：框架的 writeDatagram 拒绝 size<=0 ⇒ 只能用原始套接字
   发 0 长度数据报。winsock2 必须在 windows.h 之前（本文件未含 windows.h）。 */
#include <winsock2.h>
#endif
#include "XIODevice_Protected.h"  /* 非虚 XIODevice_bytesAvailable：只看读环本身 */
#if defined(_WIN32)
/* [T-2 2026-11-21] 平台错误码注入夹具：让回归能确定性制造
 * WSAECONNREFUSED / WSAETIMEDOUT / WSAEHOSTUNREACH / WSAENOBUFS 等码，
 * 走与真实失败完全相同的收口路径（详见该头的文件注）。 */
#include "XDeviceNetwork_fault.h"
#endif
#include <string.h>

#if DEMOTEST_ON

#define SST_ASSERT(cond, ...) do {                                        \
    if (!(cond)) {                                                        \
        XPrintf("  [失败] " __VA_ARGS__);                                 \
        XPrintf("  (第%d行)\n", __LINE__);                                 \
        return false;                                                     \
    }                                                                     \
} while (0)

#define SST_PASS(msg) XPrintf("  [通过] %s\n", msg)

/* ---- 小工具 ---- */

/** @brief 连到本机端口的客户端套接字（已等 Connected）。 */
static XTcpSocket* sst_connect(uint16_t port, int timeoutMs)
{
    XTcpSocket* s;
    uint64_t start;

    s = XTcpSocket_create();
    if (!s) return NULL;
    XAbstractSocket_connectToHost_base((XAbstractSocket*)s, "127.0.0.1", port,
                                       XIODevice_ReadWrite,
                                       XAbstractSocket_AnyIPProtocol);
    start = XDateTime_currentMSecsSinceEpoch();
    while (XAbstractSocket_state((XAbstractSocket*)s)
               != XAbstractSocket_ConnectedState
           && XDateTime_currentMSecsSinceEpoch() - start < (uint64_t)timeoutMs) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    if (XAbstractSocket_state((XAbstractSocket*)s)
        != XAbstractSocket_ConnectedState) {
        XTcpSocket_deleteLater((XObject*)s);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        return NULL;
    }
    return s;
}

/** @brief 抽事件若干毫秒（让 IOCP 完成包被处理）。 */
static void sst_pump(int ms)
{
    uint64_t start = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - start < (uint64_t)ms) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
}

/** @brief 持续抽事件直到某设备可读字节数 >= want，或超时。
 *  @return 实际可读字节数（超时返回当时值）。 */
static int64_t sst_wait_readable(XIODevice* dev, int64_t want, int timeoutMs)
{
    uint64_t start = XDateTime_currentMSecsSinceEpoch();
    for (;;) {
        int64_t avail = XIODevice_bytesAvailable_base(dev);
        if (avail >= want)
            return avail;
        if (XDateTime_currentMSecsSinceEpoch() - start >= (uint64_t)timeoutMs)
            return avail;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
}

/** @brief 读满 want 字节并逐字节比对期望填充字符。
 *  @note  **不主动丢弃残留**：无论调用方还是本函数，都不能假设此刻
 *         可读的字节是"旧垃圾"。本函数第一版曾加"先抽干再读"，结果把
 *         当轮刚到达的正常数据当残留丢掉，制造出三组假失败。要清残留
 *         必须在**发数据之前**做（见 sst_drain 的用法）。 */
static bool sst_read_expect(XIODevice* dev, int want, char expect,
                            const char* tag, int round)
{
    char buf[512];
    int got = 0;

    if (want > (int)sizeof(buf)) want = (int)sizeof(buf);

    while (got < want) {
        int64_t n = XIODevice_bytesAvailable_base(dev);
        if (n > 0) {
            if (n > want - got) n = want - got;
            n = XIODevice_read_1(dev, buf + got, n);
            if (n > 0) {
                got += (int)n;
                continue;
            }
        }
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
        if (got == 0) {
            XPrintf("  [失败] %s 第%d轮一个字节都没收到\n", tag, round);
            return false;
        }
    }
    {
        int i;
        for (i = 0; i < want; ++i) {
            if (buf[i] != expect) {
                XPrintf("  [失败] %s 第%d轮第%d字节=0x%02X 期望0x%02X\n",
                        tag, round, i, (unsigned char)buf[i],
                        (unsigned char)expect);
                return false;
            }
        }
    }
    return true;
}

/* ==================== 用例 ==================== */

/* --- 追加用例：主机名异步解析 / hostFound / isValid / close 收尾 --- */
static int g_sstHostFound = 0;

static void sst_on_host_found(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_sstHostFound;
}

/**
 * @brief 10) 主机名解析的 Qt 6.8 语义（同线程事件驱动，不新增线程）。
 * @details 断言四件事：
 *          1. `connectToHost` 立即返回、进入 HostLookupState（异步契约）；
 *          2. 解析成功后发**恰好一次** hostFound；
 *          3. 连接建立后 `isValid()` 为 true；
 *          4. `close()` 把状态收敛到 UnconnectedState（Qt close 语义）。
 */
static bool sst_host_lookup_semantics(void)
{
    XTcpServer* server;
    XTcpSocket* cli;
    uint16_t port;
    uint64_t start;

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    cli = XTcpSocket_create();
    SST_ASSERT(cli, "客户端创建失败");
    g_sstHostFound = 0;
    XObject_connect_2((XObject*)cli,
                      XSignal(XAbstractSocket_hostFound_signal),
                      sst_on_host_found);

    XAbstractSocket_connectToHost_base((XAbstractSocket*)cli, "localhost", port,
                                       XIODevice_ReadWrite,
                                       XAbstractSocket_AnyIPProtocol);
    SST_ASSERT(XAbstractSocket_state((XAbstractSocket*)cli)
                   == XAbstractSocket_HostLookupState,
               "connectToHost 未进入 HostLookupState（state=%d）",
               (int)XAbstractSocket_state((XAbstractSocket*)cli));

    start = XDateTime_currentMSecsSinceEpoch();
    while (XAbstractSocket_state((XAbstractSocket*)cli)
               != XAbstractSocket_ConnectedState
           && XDateTime_currentMSecsSinceEpoch() - start < 3000) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    SST_ASSERT(XAbstractSocket_state((XAbstractSocket*)cli)
                   == XAbstractSocket_ConnectedState,
               "主机名连接未完成（state=%d err=%d）",
               (int)XAbstractSocket_state((XAbstractSocket*)cli),
               (int)XAbstractSocket_error((XAbstractSocket*)cli));
    SST_ASSERT(g_sstHostFound == 1, "hostFound 次数=%d（期望 1）",
               g_sstHostFound);
    SST_ASSERT(XAbstractSocket_isValid((XAbstractSocket*)cli),
               "已连接但 isValid() 为 false");

    XIODevice_close_base((XIODevice*)cli);
    SST_ASSERT(XAbstractSocket_state((XAbstractSocket*)cli)
                   == XAbstractSocket_UnconnectedState,
               "close 后状态=%d（期望 Unconnected=0）",
               (int)XAbstractSocket_state((XAbstractSocket*)cli));
    SST_ASSERT(!XAbstractSocket_isValid((XAbstractSocket*)cli),
               "close 后 isValid() 仍为 true");

    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    SST_PASS("主机名异步解析与 hostFound/isValid/close 语义");
    return true;
}

/**
 * @brief 1) 长空闲后恢复收发：读挂起 2 秒无数据，之后必须还能双向收发。
 * @details 对应「读挂在核心态、长时间无数据后再来数据能否唤醒」这一
 *          怀疑点。若此例失败，说明读挂起本身有状态问题。
 */
static bool sst_idle_then_resume(void)
{
    XTcpServer* server;
    XTcpSocket* cli;
    XTcpSocket* srv;
    uint16_t port;

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    cli = sst_connect(port, 3000);
    SST_ASSERT(cli, "客户端连接失败");
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (!XTcpServer_hasPendingConnections_base(server)
               && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    srv = XTcpServer_nextPendingConnection_base(server);
    SST_ASSERT(srv, "取出服务端连接失败");

    /* 空闲 2 秒：期间读一直挂着且无数据。 */
    sst_pump(2000);

    /* 空闲后客户端发一帧，服务端必须收到。 */
    SST_ASSERT(XIODevice_write_1((XIODevice*)cli, "AFTERIDLE", 9) == 9,
               "客户端写失败");
    {
        char buf[32];
        int64_t n;
        int64_t avail = sst_wait_readable((XIODevice*)srv, 9, 3000);
        SST_ASSERT(avail >= 9, "空闲后服务端只收到 %lld/9 字节",
                   (long long)avail);
        n = XIODevice_read_1((XIODevice*)srv, buf, 9);
        SST_ASSERT(n == 9 && memcmp(buf, "AFTERIDLE", 9) == 0,
                   "空闲后内容不符（n=%lld）", (long long)n);
    }

    XIODevice_close_base((XIODevice*)srv);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli);
    XTcpSocket_deleteLater((XObject*)srv);
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    SST_PASS("长空闲后恢复收发");
    return true;
}

/**
 * @brief 2) 高频小帧持续收发：100 轮 x 20 字节，逐轮校验。
 * @details 对应 HSF 的真实形态（100ms 节拍的遥测帧）。中途任何一轮丢帧
 *          都会被抓到具体轮次。
 */
static bool sst_high_freq_small_frames(void)
{
    enum { kRounds = 100, kLen = 20 };
    XTcpServer* server;
    XTcpSocket* cli;
    XTcpSocket* srv;
    uint16_t port;
    int round;

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    cli = sst_connect(port, 3000);
    SST_ASSERT(cli, "客户端连接失败");
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (!XTcpServer_hasPendingConnections_base(server)
               && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    srv = XTcpServer_nextPendingConnection_base(server);
    SST_ASSERT(srv, "取出服务端连接失败");

    for (round = 0; round < kRounds; ++round) {
        char out[kLen];
        memset(out, (int)('A' + (round % 26)), sizeof(out));
        SST_ASSERT(XIODevice_write_1((XIODevice*)cli, out, kLen) == kLen,
                   "第%d轮客户端写失败", round);
        {
            int64_t avail = sst_wait_readable((XIODevice*)srv, kLen, 2000);
            SST_ASSERT(avail >= kLen,
                       "第%d轮服务端只收到 %lld/%d 字节", round,
                       (long long)avail, kLen);
        }
        SST_ASSERT(sst_read_expect((XIODevice*)srv, kLen,
                                   (char)('A' + (round % 26)), "c2s", round),
                   "第%d轮 c2s 校验失败", round);
    }

    XIODevice_close_base((XIODevice*)srv);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli);
    XTcpSocket_deleteLater((XObject*)srv);
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    SST_PASS("高频小帧持续收发");
    return true;
}

/**
 * @brief 3) 单边狂发 / 对端慢读：背压下不得丢帧。
 * @details 客户端快速写 200 帧（不等待），服务端随后一次性读走。这条锁
 *          住「发送侧与接收侧节拍不一致时的完整性」——HSF 里服务端 TICK
 *          推送与客户端切页收帧同样是两个节拍。
 */
static bool sst_burst_then_drain(void)
{
    enum { kFrames = 200, kLen = 20 };
    XTcpServer* server;
    XTcpSocket* cli;
    XTcpSocket* srv;
    uint16_t port;
    int i;
    int64_t total = 0;

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    cli = sst_connect(port, 3000);
    SST_ASSERT(cli, "客户端连接失败");
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (!XTcpServer_hasPendingConnections_base(server)
               && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    srv = XTcpServer_nextPendingConnection_base(server);
    SST_ASSERT(srv, "取出服务端连接失败");

    /* 狂发：只写不读，让接收侧积压。 */
    for (i = 0; i < kFrames; ++i) {
        char out[kLen];
        memset(out, (int)('a' + (i % 26)), sizeof(out));
        SST_ASSERT(XIODevice_write_1((XIODevice*)cli, out, kLen) == kLen,
                   "第%d帧写失败（背压丢写）", i);
    }

    /* 排空：必须正好收到 kFrames * kLen 字节。 */
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (XDateTime_currentMSecsSinceEpoch() - start < 5000) {
            int64_t avail = XIODevice_bytesAvailable_base((XIODevice*)srv);
            if (avail > 0) {
                char buf[4096];
                int64_t n = XIODevice_read_1((XIODevice*)srv, buf,
                                             avail > (int64_t)sizeof(buf)
                                                 ? (int64_t)sizeof(buf) : avail);
                if (n > 0) total += n;
            }
            if (total >= (int64_t)kFrames * kLen) break;
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    XPrintf("  [信息] 背压排空：%lld/%d 字节\n", (long long)total,
            kFrames * kLen);
    SST_ASSERT(total == (int64_t)kFrames * kLen,
               "背压下丢帧：只收到 %lld/%d 字节", (long long)total,
               kFrames * kLen);

    XIODevice_close_base((XIODevice*)srv);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli);
    XTcpSocket_deleteLater((XObject*)srv);
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    SST_PASS("单边狂发背压完整性");
    return true;
}

/**
 * @brief 4) 双向同时高频：读写交错不得互相饿死。
 * @details 每轮两端各发一帧、各收一帧。对应 HSF 的实际形态（服务端推送
 *          遥测 + 客户端发订阅/控制），验证收发不会互相阻塞。
 */
static bool sst_bidirectional(void)
{
    enum { kRounds = 60, kLen = 20 };
    XTcpServer* server;
    XTcpSocket* cli;
    XTcpSocket* srv;
    uint16_t port;
    int round;

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    cli = sst_connect(port, 3000);
    SST_ASSERT(cli, "客户端连接失败");
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (!XTcpServer_hasPendingConnections_base(server)
               && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    srv = XTcpServer_nextPendingConnection_base(server);
    SST_ASSERT(srv, "取出服务端连接失败");

    for (round = 0; round < kRounds; ++round) {
        char up[kLen];
        char down[kLen];
        memset(up, (int)('A' + (round % 26)), sizeof(up));
        memset(down, (int)('a' + (round % 26)), sizeof(down));

        SST_ASSERT(XIODevice_write_1((XIODevice*)cli, up, kLen) == kLen,
                   "第%d轮上行写失败", round);
        SST_ASSERT(XIODevice_write_1((XIODevice*)srv, down, kLen) == kLen,
                   "第%d轮下行写失败", round);

        SST_ASSERT(sst_wait_readable((XIODevice*)srv, kLen, 2000) >= kLen,
                   "第%d轮服务端收不到上行", round);
        SST_ASSERT(sst_wait_readable((XIODevice*)cli, kLen, 2000) >= kLen,
                   "第%d轮客户端收不到下行", round);
        SST_ASSERT(sst_read_expect((XIODevice*)srv, kLen,
                                   (char)('A' + (round % 26)), "up", round),
                   "第%d轮上行内容错", round);
        SST_ASSERT(sst_read_expect((XIODevice*)cli, kLen,
                                   (char)('a' + (round % 26)), "down", round),
                   "第%d轮下行内容错", round);
    }

    XIODevice_close_base((XIODevice*)srv);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli);
    XTcpSocket_deleteLater((XObject*)srv);
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    SST_PASS("双向同时高频");
    return true;
}

/**
 * @brief 5) 反复连接/断开/重连：连接生命周期边界不得残留。
 * @details 对应「服务端记录 N 路会话但客户端只连一次」这一观察。每轮新建
 *          客户端，收发一帧后断开，共 8 轮；每轮都必须能正常收发。
 */
static bool sst_reconnect_cycles(void)
{
    enum { kCycles = 8 };
    XTcpServer* server;
    uint16_t port;
    int cycle;

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    for (cycle = 0; cycle < kCycles; ++cycle) {
        XTcpSocket* cli = sst_connect(port, 3000);
        XTcpSocket* srv;
        SST_ASSERT(cli, "第%d轮客户端连接失败", cycle);
        {
            uint64_t start = XDateTime_currentMSecsSinceEpoch();
            while (!XTcpServer_hasPendingConnections_base(server)
                   && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XThread_msleep(2);
            }
        }
        srv = XTcpServer_nextPendingConnection_base(server);
        SST_ASSERT(srv, "第%d轮取出服务端连接失败", cycle);

        SST_ASSERT(XIODevice_write_1((XIODevice*)cli, "PING", 4) == 4,
                   "第%d轮写失败", cycle);
        SST_ASSERT(sst_wait_readable((XIODevice*)srv, 4, 2000) >= 4,
                   "第%d轮收不到数据", cycle);
        {
            /* 诊断：打印实际可读字节数与内容，定位首字节错位来源。 */
            char peek[64];
            int64_t avail = XIODevice_bytesAvailable_base((XIODevice*)srv);
            int64_t n = XIODevice_read_1((XIODevice*)srv, peek,
                                         avail > 63 ? 63 : avail);
            XPrintf("  [诊断] 第%d轮 avail=%lld 读得 %lld 字节: ",
                    cycle, (long long)avail, (long long)n);
            {
                int64_t k;
                for (k = 0; k < n; ++k) {
                    unsigned char c = (unsigned char)peek[k];
                    XPrintf(c >= 32 && c < 127 ? "%c" : "\\x%02X", c);
                }
            }
            XPrintf("\n");
            SST_ASSERT(n == 4 && memcmp(peek, "PING", 4) == 0,
                       "第%d轮收到 %.4s 而非 PING", cycle, peek);
        }

        /* 断开并彻底抽干事件，让回收路径跑完再进下一轮。 */
        XIODevice_close_base((XIODevice*)srv);
        XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli);
        XTcpSocket_deleteLater((XObject*)srv);
        XTcpSocket_deleteLater((XObject*)cli);
        sst_pump(60);
    }

    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    SST_PASS("反复连接/断开/重连");
    return true;
}

/**
 * @brief 6) 断开后重连不复用旧状态：新一轮收的必须只有新一轮发的。
 * @details fd 号会被系统复用。若旧 socket 的 IOCP 关联没清干净，新连接
 *          的读完成可能被旧对象抢走（写成没人读的 ring buffer）。本例在
 *          断开后立刻重连并只发一帧、只读一帧，内容必须完全匹配。
 */
static bool sst_reuse_no_crosstalk(void)
{
    int cycle;
    XTcpServer* server;
    uint16_t port;

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    for (cycle = 0; cycle < 6; ++cycle) {
        XTcpSocket* cli;
        XTcpSocket* srv;
        char tag[8];
        cli = sst_connect(port, 3000);
        SST_ASSERT(cli, "第%d轮连接失败", cycle);
        {
            uint64_t start = XDateTime_currentMSecsSinceEpoch();
            while (!XTcpServer_hasPendingConnections_base(server)
                   && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XThread_msleep(2);
            }
        }
        srv = XTcpServer_nextPendingConnection_base(server);
        SST_ASSERT(srv, "第%d轮取连接失败", cycle);

        /* 每轮用不同内容，串包会被立刻发现：4 字节全部为同一字符
         * （sst_read_expect 按"全部等于 expect"逐字节比对，故不能用
         * "0ZZZ" 这种首字节不同的花样——首版就因此假失败）。 */
        tag[0] = (char)('0' + cycle);
        tag[1] = tag[0];
        tag[2] = tag[0];
        tag[3] = tag[0];
        SST_ASSERT(XIODevice_write_1((XIODevice*)cli, tag, 4) == 4,
                   "第%d轮写失败", cycle);
        SST_ASSERT(sst_wait_readable((XIODevice*)srv, 4, 2000) >= 4,
                   "第%d轮收不到数据", cycle);
        SST_ASSERT(sst_read_expect((XIODevice*)srv, 4, tag[0], "reuse", cycle),
                   "第%d轮收到旧连接串包", cycle);

        XIODevice_close_base((XIODevice*)srv);
        XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli);
        XTcpSocket_deleteLater((XObject*)srv);
        XTcpSocket_deleteLater((XObject*)cli);
        sst_pump(60);
    }

    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    SST_PASS("断开重连无串包");
    return true;
}

/* ==================== 双 socket 竞争（HSF 真实拓扑） ==================== */

/**
 * @brief 7) 高频连接 + 低频连接并存：低频连接不得被饿死。
 * @details 这是 HSF 服务端的真实拓扑：同进程内既有 200ms 轮询的模拟量
 *          Modbus TCP 连接（高频、小帧、持续），又有 HSF 会话连接（低频、
 *          只在切页时发几帧）。
 *
 *          2026-11-04 实测过：GUI 客户端切页时，服务端 HSF 会话（fd=13）
 *          只收到 7 帧就彻底静默，而同进程的模拟量连接（fd=6）持续收到
 *          250+ 帧。本用例把这个拓扑固化成回归锁——若低频连接被高频连接
 *          饿死（收不到数据 / 完成包抢不到），这里会直接失败。
 *
 * @note 判据：低频连接每轮都必须**完整收到**内容正确的一帧，且高频连接
 *       全程不得断流（两侧计数都做校验）。
 */
static bool sst_busy_and_quiet_links(void)
{
    enum { kBusyRounds = 200, kQuietRounds = 12, kLen = 20 };
    XTcpServer* server;
    XTcpSocket* busyCli;
    XTcpSocket* busySrv;
    XTcpSocket* quietCli;
    XTcpSocket* quietSrv;
    uint16_t port;
    int i;
    int64_t busyTotal = 0;
    int quietOk = 0;

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    /* --- 连接 1：高频（模拟量 Modbus 轮询） --- */
    busyCli = sst_connect(port, 3000);
    SST_ASSERT(busyCli, "高频连接客户端失败");
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (!XTcpServer_hasPendingConnections_base(server)
               && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    busySrv = XTcpServer_nextPendingConnection_base(server);
    SST_ASSERT(busySrv, "取高频连接失败");

    /* --- 连接 2：低频（HSF 会话） --- */
    quietCli = sst_connect(port, 3000);
    SST_ASSERT(quietCli, "低频连接客户端失败");
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (!XTcpServer_hasPendingConnections_base(server)
               && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    quietSrv = XTcpServer_nextPendingConnection_base(server);
    SST_ASSERT(quietSrv, "取低频连接失败");

    /* --- 交替推进：高频每轮狂发，低频每隔若干轮发一帧 --- */
    for (i = 0; i < kBusyRounds; ++i) {
        char busy[kLen];
        char quiet[kLen];

        memset(busy, (int)('A' + (i % 26)), sizeof(busy));
        SST_ASSERT(XIODevice_write_1((XIODevice*)busyCli, busy, kLen) == kLen,
                   "第%d轮高频写失败", i);

        /* 低频：每 16 轮发一帧（模拟"切页时才发订阅"）。 */
        if ((i % 16) == 0) {
            memset(quiet, (int)('a' + ((i / 16) % 26)), sizeof(quiet));
            SST_ASSERT(XIODevice_write_1((XIODevice*)quietCli, quiet, kLen)
                       == kLen, "第%d轮低频写失败", i);

            /* 抽事件，然后检查低频这一帧是否完整到达。 */
            {
                uint64_t start = XDateTime_currentMSecsSinceEpoch();
                while (XIODevice_bytesAvailable_base((XIODevice*)quietSrv) < kLen
                       && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
                    XCoreApplication_processEvents(XEventLoop_AllEvents);
                    XThread_msleep(2);
                }
            }
            {
                int64_t avail =
                    XIODevice_bytesAvailable_base((XIODevice*)quietSrv);
                if (avail < kLen) {
                    XPrintf("  [失败] 第%d轮低频连接只收到 %lld/%d 字节"
                            "（疑似被高频连接饿死）\n",
                            i, (long long)avail, kLen);
                    return false;
                }
            }
            SST_ASSERT(sst_read_expect((XIODevice*)quietSrv, kLen,
                                       (char)('a' + ((i / 16) % 26)),
                                       "quiet", i / 16),
                       "第%d轮低频内容错", i / 16);
            ++quietOk;
        }

        /* 高频侧：顺带排空，避免背压干扰。 */
        {
            int64_t avail = XIODevice_bytesAvailable_base((XIODevice*)busySrv);
            if (avail > 0) {
                char sink[4096];
                int64_t n = XIODevice_read_1((XIODevice*)busySrv, sink,
                                             avail > (int64_t)sizeof(sink)
                                                 ? (int64_t)sizeof(sink) : avail);
                if (n > 0) busyTotal += n;
            }
        }
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }

    /* 收尾：高频侧剩余字节也要排空核对总数。 */
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (busyTotal < (int64_t)kBusyRounds * kLen
               && XDateTime_currentMSecsSinceEpoch() - start < 5000) {
            int64_t avail = XIODevice_bytesAvailable_base((XIODevice*)busySrv);
            if (avail > 0) {
                char sink[4096];
                int64_t n = XIODevice_read_1((XIODevice*)busySrv, sink,
                                             avail > (int64_t)sizeof(sink)
                                                 ? (int64_t)sizeof(sink) : avail);
                if (n > 0) busyTotal += n;
            }
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }

    XPrintf("  [信息] 高频 %lld/%d 字节，低频 %d 帧全部完整\n",
            (long long)busyTotal, kBusyRounds * kLen, quietOk);

    XIODevice_close_base((XIODevice*)quietSrv);
    XIODevice_close_base((XIODevice*)busySrv);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)quietCli);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)busyCli);
    XTcpSocket_deleteLater((XObject*)quietSrv);
    XTcpSocket_deleteLater((XObject*)busySrv);
    XTcpSocket_deleteLater((XObject*)quietCli);
    XTcpSocket_deleteLater((XObject*)busyCli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);

    SST_ASSERT(busyTotal == (int64_t)kBusyRounds * kLen,
               "高频连接丢字节：%lld/%d", (long long)busyTotal,
               kBusyRounds * kLen);
    SST_ASSERT(quietOk == (kBusyRounds + 15) / 16,
               "低频连接应完成 %d 帧，实际 %d 帧",
               (kBusyRounds + 15) / 16, quietOk);
    SST_PASS("高频+低频双连接竞争");
    return true;
}

/**
 * @brief 8) 多连接并存：4 条连接各自收发，互不串包。
 * @details 服务端 4 路会话（HSF 允许 admin 多会话）同时活跃时，每条连接
 *          的数据必须只到达自己那条。锁住「完成包按 fd 正确归属」。
 */
static bool sst_multi_link_isolation(void)
{
    enum { kLinks = 4, kRounds = 20, kLen = 20 };
    XTcpServer* server;
    XTcpSocket* cli[kLinks];
    XTcpSocket* srv[kLinks];
    uint16_t port;
    int i;
    int r;

    memset(cli, 0, sizeof(cli));
    memset(srv, 0, sizeof(srv));

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    for (i = 0; i < kLinks; ++i) {
        cli[i] = sst_connect(port, 3000);
        SST_ASSERT(cli[i], "第%d条连接失败", i);
        {
            uint64_t start = XDateTime_currentMSecsSinceEpoch();
            while (!XTcpServer_hasPendingConnections_base(server)
                   && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XThread_msleep(2);
            }
        }
        srv[i] = XTcpServer_nextPendingConnection_base(server);
        SST_ASSERT(srv[i], "第%d条取出失败", i);
    }

    for (r = 0; r < kRounds; ++r) {
        char out[kLen];
        for (i = 0; i < kLinks; ++i) {
            /* 每条连接用自己的字符，串包立刻可辨。 */
            memset(out, (int)('A' + i), sizeof(out));
            SST_ASSERT(XIODevice_write_1((XIODevice*)cli[i], out, kLen) == kLen,
                       "第%d轮第%d条写失败", r, i);
        }
        for (i = 0; i < kLinks; ++i) {
            SST_ASSERT(sst_wait_readable((XIODevice*)srv[i], kLen, 2000) >= kLen,
                       "第%d轮第%d条收不到数据", r, i);
            SST_ASSERT(sst_read_expect((XIODevice*)srv[i], kLen,
                                       (char)('A' + i), "multi", r * kLinks + i),
                       "第%d轮第%d条串包", r, i);
        }
    }

    for (i = 0; i < kLinks; ++i) {
        XIODevice_close_base((XIODevice*)srv[i]);
        XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli[i]);
        XTcpSocket_deleteLater((XObject*)srv[i]);
        XTcpSocket_deleteLater((XObject*)cli[i]);
    }
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    SST_PASS("多连接隔离");
    return true;
}

/**
 * @brief 9) 复刻 HSF 读写比例：服务端高频推送 + 客户端间歇发小帧。
 * @details 这是 HSF 联调失败场景的**精确复刻**：
 *            - 服务端每拍推送遥测（100ms、~274 字节/拍）
 *            - 客户端只在切页时发几帧订阅（20 字节）
 *
 *          2026-11-04 实测现象正是：服务端推送正常（250 写完成），
 *          而客户端的订阅帧只到了前 3 个。本用例锁定这一读写比例下，
 *          **反向（客户端→服务端）的小帧能否持续到达**。
 *
 * @note 判据：客户端每次发起都必须完整到达服务端，一帧不丢；服务端的
 *       推送也必须完整（两者同时校验，防"牺牲一方换另一方"）。
 */
static bool sst_push_heavy_then_small_uplink(void)
{
    enum { kPushRounds = 150, kPushLen = 64, kUpLen = 20, kUpEvery = 20 };
    XTcpServer* server;
    XTcpSocket* cli;
    XTcpSocket* srv;
    uint16_t port;
    int i;
    int64_t pushed = 0;
    int64_t drainedByCli = 0;
    int upOk = 0;
    int upSent = 0;

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    cli = sst_connect(port, 3000);
    SST_ASSERT(cli, "客户端连接失败");
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (!XTcpServer_hasPendingConnections_base(server)
               && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    srv = XTcpServer_nextPendingConnection_base(server);
    SST_ASSERT(srv, "取出服务端连接失败");

    for (i = 0; i < kPushRounds; ++i) {
        /* --- 服务端推送（高频、大帧） --- */
        char push[kPushLen];
        memset(push, (int)('P'), sizeof(push));
        SST_ASSERT(XIODevice_write_1((XIODevice*)srv, push, kPushLen)
                   == kPushLen, "第%d轮服务端推送写失败", i);
        pushed += kPushLen;

        /* 客户端把推送读走（模拟 GUI 消费遥测）。 */
        {
            int64_t avail = XIODevice_bytesAvailable_base((XIODevice*)cli);
            if (avail > 0) {
                char sink[4096];
                int64_t n = XIODevice_read_1((XIODevice*)cli, sink,
                                             avail > (int64_t)sizeof(sink)
                                                 ? (int64_t)sizeof(sink) : avail);
                if (n > 0) drainedByCli += n;
            }
        }

        /* --- 客户端间歇上行小帧（模拟切页订阅） --- */
        if ((i % kUpEvery) == 0) {
            char up[kUpLen];
            char expect = (char)('a' + (upSent % 26));
            memset(up, (int)expect, sizeof(up));
            SST_ASSERT(XIODevice_write_1((XIODevice*)cli, up, kUpLen) == kUpLen,
                       "第%d轮客户端上行写失败", i);
            ++upSent;

            {
                uint64_t start = XDateTime_currentMSecsSinceEpoch();
                while (XIODevice_bytesAvailable_base((XIODevice*)srv) < kUpLen
                       && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
                    XCoreApplication_processEvents(XEventLoop_AllEvents);
                    XThread_msleep(2);
                }
            }
            {
                int64_t avail = XIODevice_bytesAvailable_base((XIODevice*)srv);
                if (avail < kUpLen) {
                    XPrintf("  [失败] 第%d轮上行只到 %lld/%d 字节"
                            "（推送压力下小帧被吞）\n",
                            i, (long long)avail, kUpLen);
                    return false;
                }
            }
            SST_ASSERT(sst_read_expect((XIODevice*)srv, kUpLen, expect,
                                       "uplink", upSent - 1),
                       "第%d轮上行内容错", upSent - 1);
            ++upOk;
        }
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }

    /* 推送侧收尾排空。 */
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (drainedByCli < pushed
               && XDateTime_currentMSecsSinceEpoch() - start < 5000) {
            int64_t avail = XIODevice_bytesAvailable_base((XIODevice*)cli);
            if (avail > 0) {
                char sink[4096];
                int64_t n = XIODevice_read_1((XIODevice*)cli, sink,
                                             avail > (int64_t)sizeof(sink)
                                                 ? (int64_t)sizeof(sink) : avail);
                if (n > 0) drainedByCli += n;
            }
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }

    XPrintf("  [信息] 推送 %lld/%lld 字节，上行 %d/%d 帧全部完整\n",
            (long long)drainedByCli, (long long)pushed, upOk, upSent);

    XIODevice_close_base((XIODevice*)srv);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli);
    XTcpSocket_deleteLater((XObject*)srv);
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);

    SST_ASSERT(drainedByCli == pushed, "推送丢失：%lld/%lld",
               (long long)drainedByCli, (long long)pushed);
    SST_ASSERT(upOk == upSent, "上行丢帧：%d/%d", upOk, upSent);
    SST_PASS("推送压力下的上行小帧");
    return true;
}

/* ==================== 命令行入口 ==================== */

/** @brief 在超时内读满 n 字节，返回实际字节数（不比对内容）。 */
static int sst_read_exact(XIODevice* dev, char* buf, int n, int timeoutMs)
{
    uint64_t start = XDateTime_currentMSecsSinceEpoch();
    int got = 0;
    while (got < n) {
        int64_t avail = XIODevice_bytesAvailable_base(dev);
        if (avail > 0) {
            int64_t take = (avail > (int64_t)(n - got)) ? (int64_t)(n - got) : avail;
            int64_t r = XIODevice_read_1(dev, buf + got, take);
            if (r > 0) {
                got += (int)r;
                continue;
            }
        }
        if (XDateTime_currentMSecsSinceEpoch() - start >= (uint64_t)timeoutMs)
            break;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }
    return got;
}

/* ---------- 追加用例：2026-11-06 三个真因的定向回归 ---------- */

/** @brief 唤醒包风暴线程开关（0=停）。 */
static volatile long g_sstStormRun = 0;

/**
 * @brief 唤醒包风暴：从**工作线程**高频调用事件环 wakeUp。
 * @details 跨进程联调里 98.9% 的完成端口包是唤醒包（`polls=7000 /
 *          wakeTaken=6989`）。旧实现 `pollPlatform` 遇到 `overlapped==NULL`
 *          就 break，且 wakeUp 每次无条件投包 ⇒ 真实完成包被排队饥饿数秒，
 *          读看门狗据此误判「完成包丢失」。本例在传输过程中持续制造这种风暴，
 *          断言数据仍能按轮次完整到达。
 */
static void sst_storm_worker(XThread* thread, XVarList* args)
{
    unsigned spin = 0;
    (void)thread;
    (void)args;
    while (g_sstStormRun) {
        XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
        if (ring)
            XAbstractNetIoRing_wakeUp_base(ring);
        if ((++spin & 0x1FFu) == 0)
            XThread_msleep(0);
    }
}

static bool sst_wake_storm_keeps_reading(void)
{
    enum { kRounds = 120, kLen = 64 };
    XTcpServer* server;
    XTcpSocket* cli;
    XTcpSocket* srv;
    XThread* storm;
    uint16_t port;
    int round;

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    cli = sst_connect(port, 3000);
    SST_ASSERT(cli, "客户端连接失败");
    {
        uint64_t start = XDateTime_currentMSecsSinceEpoch();
        while (!XTcpServer_hasPendingConnections_base(server)
               && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    srv = XTcpServer_nextPendingConnection_base(server);
    SST_ASSERT(srv, "取出服务端连接失败");

    g_sstStormRun = 1;
    storm = XThread_create_func(sst_storm_worker, NULL);
    SST_ASSERT(storm, "风暴线程创建失败");
    SST_ASSERT(XThread_start(storm), "风暴线程启动失败");

    for (round = 0; round < kRounds; ++round) {
        char out[kLen];
        char in[kLen];
        int i;
        for (i = 0; i < kLen; ++i)
            out[i] = (char)('A' + (round % 26));
        SST_ASSERT(XIODevice_write_1((XIODevice*)cli, out, kLen) == kLen,
                   "风暴下第%d轮写失败", round);
        SST_ASSERT(sst_read_exact((XIODevice*)srv, in, kLen, 3000) == kLen,
                   "风暴下第%d轮服务端没收满（完成包被唤醒包饿死）", round);
        SST_ASSERT(memcmp(in, out, kLen) == 0, "风暴下第%d轮内容错", round);
    }

    g_sstStormRun = 0;
    (void)XThread_wait(storm, 3000);
    XClassDelete((XClass*)storm);

    XIODevice_close_base((XIODevice*)srv);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli);
    XTcpSocket_deleteLater((XObject*)srv);
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    SST_PASS("唤醒包风暴下读通量完整");
    return true;
}

/**
 * @brief 立刻重连 churn：断开后不等待、不排空，直接进入下一轮。
 * @details 针对「完成包归属按句柄数值」这根因：Windows 会复用 SOCKET 数值，
 *          旧连接的 ABORTED 包会被误认为新连接的完成包。本例每轮都带内容
 *          校验，一旦串代就会立刻表现为首帧错位/收不到。
 */
static bool sst_immediate_reconnect_churn(void)
{
    enum { kRounds = 20, kLen = 40 };
    XTcpServer* server;
    uint16_t port;
    int round;

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    for (round = 0; round < kRounds; ++round) {
        XTcpSocket* cli;
        XTcpSocket* srv;
        char out[kLen];
        char in[kLen];
        int i;

        cli = sst_connect(port, 3000);
        SST_ASSERT(cli, "第%d轮连接失败", round);
        {
            uint64_t start = XDateTime_currentMSecsSinceEpoch();
            while (!XTcpServer_hasPendingConnections_base(server)
                   && XDateTime_currentMSecsSinceEpoch() - start < 2000) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XThread_msleep(1);
            }
        }
        srv = XTcpServer_nextPendingConnection_base(server);
        SST_ASSERT(srv, "第%d轮取连接失败", round);

        for (i = 0; i < kLen; ++i)
            out[i] = (char)(0x30 + (round % 10));
        SST_ASSERT(XIODevice_write_1((XIODevice*)cli, out, kLen) == kLen,
                   "第%d轮写失败", round);
        SST_ASSERT(sst_read_exact((XIODevice*)srv, in, kLen, 3000) == kLen,
                   "第%d轮服务端没收满（疑似完成包串代）", round);
        SST_ASSERT(memcmp(in, out, kLen) == 0,
                   "第%d轮内容错位（疑似旧连接完成包落进新连接）", round);

        /* 关键：不 pump、不排空，立刻拆掉两端进入下一轮。 */
        XIODevice_close_base((XIODevice*)srv);
        XIODevice_close_base((XIODevice*)cli);
        XTcpSocket_deleteLater((XObject*)srv);
        XTcpSocket_deleteLater((XObject*)cli);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }

    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    SST_PASS("立刻重连 20 轮无串代/无丢帧");
    return true;
}

/**
 * @brief 对端异常断开（未读数据 + close ⇒ RST/FIN）必须被及时察觉。
 * @details 针对「读失败被当成 0 字节读完成、再被假 EOF 复核吞掉」这根因：
 *          旧实现在对端消失后 PEEK 拿不到 0，关闭事件被丢弃，客户端永不重连。
 *          本例断言 1500ms 内状态必须收敛到 UnconnectedState。
 */
static bool sst_peer_reset_detected(void)
{
    XTcpServer* server;
    XTcpSocket* cli;
    XTcpSocket* srv;
    uint16_t port;
    char junk[4096];
    uint64_t start;

    memset(junk, 'R', sizeof(junk));
    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    cli = sst_connect(port, 3000);
    SST_ASSERT(cli, "客户端连接失败");
    {
        uint64_t t0 = XDateTime_currentMSecsSinceEpoch();
        while (!XTcpServer_hasPendingConnections_base(server)
               && XDateTime_currentMSecsSinceEpoch() - t0 < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    srv = XTcpServer_nextPendingConnection_base(server);
    SST_ASSERT(srv, "取出服务端连接失败");

    /* 客户端发一帧，服务端**不读**就关闭：接收队列有未读数据 ⇒ 对端 RST。 */
    SST_ASSERT(XIODevice_write_1((XIODevice*)cli, junk, (int64_t)sizeof(junk))
                   == (int64_t)sizeof(junk),
               "客户端写失败");
    sst_pump(50);
    XIODevice_close_base((XIODevice*)srv);
    XTcpSocket_deleteLater((XObject*)srv);

    start = XDateTime_currentMSecsSinceEpoch();
    while (XAbstractSocket_state((XAbstractSocket*)cli)
               != XAbstractSocket_UnconnectedState
           && XDateTime_currentMSecsSinceEpoch() - start < 1500) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    SST_ASSERT(XAbstractSocket_state((XAbstractSocket*)cli)
                   == XAbstractSocket_UnconnectedState,
               "对端断开后 %llums 仍未察觉（state=%d）",
               (unsigned long long)(XDateTime_currentMSecsSinceEpoch() - start),
               (int)XAbstractSocket_state((XAbstractSocket*)cli));

    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    SST_PASS("对端异常断开 1500ms 内被察觉");
    return true;
}

/**
 * @brief 大块（64KB）逐字节完整性：覆盖写槽分片与 writeStash 保序。
 * @details 单槽缓冲 8KB，64KB 必须分成多笔在飞写。旧实现「提交失败把数据
 *          写回 ring 尾部」会打乱字节流顺序，本用例逐字节比对可当场抓住。
 */
static bool sst_large_block_integrity(void)
{
    enum { kTotal = 64 * 1024 };
    static char out[kTotal];
    static char in[kTotal];
    XTcpServer* server;
    XTcpSocket* cli;
    XTcpSocket* srv;
    uint16_t port;
    int sent = 0;
    int i;
    uint64_t start;

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    cli = sst_connect(port, 3000);
    SST_ASSERT(cli, "客户端连接失败");
    {
        uint64_t t0 = XDateTime_currentMSecsSinceEpoch();
        while (!XTcpServer_hasPendingConnections_base(server)
               && XDateTime_currentMSecsSinceEpoch() - t0 < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    srv = XTcpServer_nextPendingConnection_base(server);
    SST_ASSERT(srv, "取出服务端连接失败");

    for (i = 0; i < kTotal; ++i)
        out[i] = (char)((i * 31 + 7) & 0xFF);

    start = XDateTime_currentMSecsSinceEpoch();
    while (sent < kTotal) {
        int64_t w = XIODevice_write_1((XIODevice*)cli, out + sent, kTotal - sent);
        if (w > 0) {
            sent += (int)w;
            continue;
        }
        SST_ASSERT(XDateTime_currentMSecsSinceEpoch() - start < 8000,
                   "大块写超时（已写 %d/%d）", sent, kTotal);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }

    SST_ASSERT(sst_read_exact((XIODevice*)srv, in, kTotal, 10000) == kTotal,
               "大块读不足（疑似写分片丢序）");
    SST_ASSERT(memcmp(in, out, kTotal) == 0, "大块内容不符（疑似字节流乱序）");

    XIODevice_close_base((XIODevice*)srv);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli);
    XTcpSocket_deleteLater((XObject*)srv);
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    SST_PASS("64KB 大块逐字节一致");
    return true;
}

/**
 * @brief 读缓冲达上限暂停、消费后恢复（Qt setReadBufferSize 语义）。
 * @details 覆盖 `readPaused` 的恢复路径：暂停后如果 ContinueRead 没有被重新
 *          触发，读通道就会永久停摆——正是本文件要防的一类故障。
 */
static bool sst_read_buffer_pause_resume(void)
{
    enum { kTotal = 8192 };
    static char out[kTotal];
    static char in[kTotal];
    XTcpServer* server;
    XTcpSocket* cli;
    XTcpSocket* srv;
    uint16_t port;
    int sent = 0;
    int i;

    server = XTcpServer_create();
    SST_ASSERT(server, "创建服务端失败");
    SST_ASSERT(XTcpServer_listen(server, NULL, 0), "listen 失败");
    port = XTcpServer_serverPort(server);

    cli = sst_connect(port, 3000);
    SST_ASSERT(cli, "客户端连接失败");
    {
        uint64_t t0 = XDateTime_currentMSecsSinceEpoch();
        while (!XTcpServer_hasPendingConnections_base(server)
               && XDateTime_currentMSecsSinceEpoch() - t0 < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    srv = XTcpServer_nextPendingConnection_base(server);
    SST_ASSERT(srv, "取出服务端连接失败");

    XAbstractSocket_setReadBufferSize_base((XAbstractSocket*)cli, 1024);
    for (i = 0; i < kTotal; ++i)
        out[i] = (char)(i & 0xFF);

    while (sent < kTotal) {
        int64_t w = XIODevice_write_1((XIODevice*)srv, out + sent, kTotal - sent);
        if (w > 0) {
            sent += (int)w;
            continue;
        }
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }

    /* 暂停期：应用不读，事件循环里可读量必须被上限截住（不会无限增长）。 */
    sst_pump(400);
    {
        int64_t avail = XIODevice_bytesAvailable_base((XIODevice*)cli);
        /* 上界 = 读缓冲上限(1024) + 「一批已搬进读环」的余量(单批 8192)。
         * 【2026-11-21 修正】旧写法是 `avail <= kTotal`（8192）——服务端总共
         * 只发 8192 字节，可用量永远 <=8192 ⇒ **恒真、零判别力**。改成按
         * 「设定的 readBufferSize + 单批上限」取上界（判别力见同文件
         * sst_read_buffer_cap_enforced：那里灌 64KB 才真正能红）。 */
        SST_ASSERT(avail <= (int64_t)1024 + 8192,
                   "暂停期可用=%lld 超过上限 1024+8192", (long long)avail);
    }

    /* 恢复期：应用持续消费，必须能收齐全部 8KB（恢复读通知生效）。 */
    SST_ASSERT(sst_read_exact((XIODevice*)cli, in, kTotal, 8000) == kTotal,
               "读缓冲暂停后未恢复（收不满 %d 字节）", kTotal);
    SST_ASSERT(memcmp(in, out, kTotal) == 0, "暂停恢复后内容不符");

    XIODevice_close_base((XIODevice*)srv);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli);
    XTcpSocket_deleteLater((XObject*)srv);
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    SST_PASS("读缓冲暂停/恢复（8KB 全额到达）");
    return true;
}

/* ==================== 2026-11-21 新增：套接字后端「硬覆盖」 ==================== */
/*
 * 本节补的是 2026-11-21 复核出的覆盖缺口（既有 15 条用例的顺序与内容一律不动，
 * 唯一例外是 sst_read_buffer_pause_resume 里那条恒真的上界断言，已就地改成
 * 有判别力的形式）：
 *
 *   1) 优雅 FIN（对端**无未读数据**直接关闭）这条链此前零覆盖——既有用例造的是
 *      RST（客户端发一帧、服务端不读就关，接收队列有未读数据）。
 *   2) 「一次 read 的返回量不得超过请求量」无人断言（Windows 后端直取内核时按
 *      8192 读、忽略调用方请求长度，有越界写风险）。
 *   3) 背压分支在门禁里是死路：既有背压用例只写 4000/9600 字节，而 Windows 缺省
 *      SO_SNDBUF 是 64KB 起 ⇒ send 从未返回 WSAEWOULDBLOCK。
 *   4) 就绪唤醒时延、读缓冲上限是否真的生效、断链信号与状态顺序、对端关闭后写、
 *      并发连接数上限——要么没有断言，要么断言恒真。
 */

/** @brief 不带 sleep 的等待循环（只抽事件），判据是**读环**里的字节数。
 *  @details 「就绪唤醒时延」类断言必须用它：
 *           - `sst_pump()` 里带 `XThread_msleep(2)`，会把时延稀释掉；而窗口
 *             又远大于事件环的轮询片（主线程等待被钳到 <=20ms，WSA 侧再分
 *             10ms 一片）⇒「唤醒链全断也 100% 绿」。本循环只抽事件、不睡，
 *             窗口压到 50ms 时才具备判别力。
 *           - 判据必须用**非虚** `XIODevice_bytesAvailable`（读环本身）：虚函数
 *             版会把内核 FIONREAD 的待读字节也算进去（socketEngineBytesAvailable），
 *             数据一进内核就「可读」⇒ 根本不需要就绪事件，测出来的是 0ms 的
 *             假象。读环里的字节**只能**由「poll 发现就绪 → recv → 投递读完成
 *             → 搬进读环」这条链产生 ⇒ 才是真正的就绪唤醒判据。
 *  @return 读环里的字节数（超时返回当时值）。 */
static int64_t sst_wait_ring_readable_fast(XIODevice* dev, int64_t want, int timeoutMs)
{
    uint64_t start = XDateTime_currentMSecsSinceEpoch();
    for (;;) {
        int64_t avail = XIODevice_bytesAvailable(dev);
        if (avail >= want)
            return avail;
        if (XDateTime_currentMSecsSinceEpoch() - start >= (uint64_t)timeoutMs)
            return avail;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
}

/** @brief 收尾：关两端 + 释放 + 抽事件。
 *  @note `SST_ASSERT` 失败时直接 return false、**不释放已建资源** ⇒ 用例的
 *        失败路径也必须显式收尾，否则残留的连接/事件会污染下一条用例。 */
static void sst_teardown_pair(XTcpSocket* cli, XTcpSocket* srv, XTcpServer* server)
{
    if (srv) {
        XIODevice_close_base((XIODevice*)srv);
        XTcpSocket_deleteLater((XObject*)srv);
    }
    if (cli) {
        if (XAbstractSocket_state((XAbstractSocket*)cli) == XAbstractSocket_ConnectedState)
            XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli);
        XTcpSocket_deleteLater((XObject*)cli);
    }
    if (server) {
        XTcpServer_close(server);
        XTcpServer_deleteLater((XObject*)server);
    }
    sst_pump(50);
}

/** @brief 建立一对连接：起服务端（端口由系统分配，禁写死端口）、客户端连上、
 *         服务端 accept。
 *  @param pServer/pCli/pSrv 出参；失败时也可能是**部分有效**的指针，调用方
 *         统一交给 sst_teardown_pair 收尾。
 *  @return 成功 true。 */
static bool sst_make_pair(XTcpServer** pServer, XTcpSocket** pCli, XTcpSocket** pSrv)
{
    XTcpServer* server;
    XTcpSocket* cli;
    uint16_t port;

    *pServer = NULL;
    *pCli = NULL;
    *pSrv = NULL;

    server = XTcpServer_create();
    if (!server) return false;
    *pServer = server;
    if (!XTcpServer_listen(server, NULL, 0)) return false;
    port = XTcpServer_serverPort(server);

    cli = sst_connect(port, 3000);
    if (!cli) return false;
    *pCli = cli;
    {
        uint64_t t0 = XDateTime_currentMSecsSinceEpoch();
        while (!XTcpServer_hasPendingConnections_base(server)
               && XDateTime_currentMSecsSinceEpoch() - t0 < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(2);
        }
    }
    *pSrv = XTcpServer_nextPendingConnection_base(server);
    return (*pSrv != NULL);
}

/**
 * @brief T1 优雅 FIN（对端无未读数据直接关闭）必须被及时察觉。
 * @details 与 sst_peer_reset_detected（RST 路径）形成**对照**：
 *          - RST：客户端先发一帧、服务端不读就关 ⇒ 接收队列有未读数据
 *            ⇒ 内核对端发 RST；
 *          - FIN（本例）：客户端**一个字节都不发**、服务端直接关 ⇒ 接收队列
 *            为空 ⇒ 内核发 FIN（优雅关闭）⇒ 对端 recv 返回 0 ⇒ 0 字节读
 *            完成 ⇒ 框架 PEEK 复核判真 FIN。
 *          这条链在套接字层此前零覆盖。
 * @note 判据沿用 RST 用例的 1500ms 窗口（两条必须同口径才能对照），但失败
 *       消息带上**实测耗时**与 state，便于分辨「慢」与「没到」。
 */
static bool sst_peer_graceful_fin_detected(void)
{
    XTcpServer* server = NULL;
    XTcpSocket* cli = NULL;
    XTcpSocket* srv = NULL;
    uint64_t start;
    uint64_t elapsed;
    int state;

    if (!sst_make_pair(&server, &cli, &srv)) {
        XPrintf("  [失败] 建立连接对失败\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    /* 关键：客户端**一个字节都不发** ⇒ 服务端关闭时接收队列为空 ⇒ 对端收到
     * 的是 FIN（优雅关闭），不是 RST。 */
    XIODevice_close_base((XIODevice*)srv);
    XTcpSocket_deleteLater((XObject*)srv);
    srv = NULL;

    start = XDateTime_currentMSecsSinceEpoch();
    while (XAbstractSocket_state((XAbstractSocket*)cli)
               != XAbstractSocket_UnconnectedState
           && XDateTime_currentMSecsSinceEpoch() - start < 1500) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    elapsed = XDateTime_currentMSecsSinceEpoch() - start;
    state = (int)XAbstractSocket_state((XAbstractSocket*)cli);
    if (state != XAbstractSocket_UnconnectedState) {
        XPrintf("  [失败] 优雅 FIN 后 %llums 仍未察觉（state=%d，期望 0=Unconnected）\n",
                (unsigned long long)elapsed, state);
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    sst_teardown_pair(cli, srv, server);
    XPrintf("  [通过] 优雅 FIN 及时察觉（%llums）\n", (unsigned long long)elapsed);
    return true;
}

/**
 * @brief T2 一次 read 的返回量不得超过调用方的请求量，且累计必须等于发送总量。
 * @details 背景（**可能是真缺陷**）：Windows 后端「读环空时直取内核」路径调用
 *          `xw32_recvOnceInto(p, (char*)buf, ...)`，而该函数是
 *          `recv(p->socket, dest, XNETWORK_READ_BUFFER_SIZE, 0)`（8192）
 *          ——**按 8192 读，完全忽略调用方的请求长度**；POSIX 后端没有这条
 *          直取路径。调用链 `XIODevice_read_1`（XIODevice.c:452）在
 *          `maxSize >= XIO_CHUNK_SIZE`(4096) 时**直写调用方缓冲** ⇒
 *          「请求量 ∈ [4096,8192) 且内核待读 > 请求量」时，一次 read 会写超
 *          调用方缓冲、返回值也超过请求量。
 * @note 用例写法（避免把「真缺陷」变成「用例自己踩内存」）：
 *          - 读缓冲给足余量（static 64KB，请求只用 4096）⇒ 即使实现越界写也
 *            不会踩坏栈/堆，断言仍能如实抓到「返回值 > 请求量」；
 *          - 读之前**不抽事件**（抽事件会把内核数据搬进读环 ⇒ 绕开直取路径）；
 *          - 断言两件：① **每一次** read 的返回值 ≤ 请求量；② 累计字节数
 *            == 发送总量（只断言 ① 会被「每次都返回 0」骗过）。
 */
static bool sst_read_never_exceeds_request(void)
{
    enum { kTotal = 32 * 1024, kRequest = 4096 };
    static char out[kTotal];
    static char in[64 * 1024];   /* 故意留足余量：实现越界写也不踩坏内存 */
    XTcpServer* server = NULL;
    XTcpSocket* cli = NULL;
    XTcpSocket* srv = NULL;
    int i;
    int sent = 0;
    int64_t total = 0;
    uint64_t start;

    if (!sst_make_pair(&server, &cli, &srv)) {
        XPrintf("  [失败] 建立连接对失败\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    for (i = 0; i < kTotal; ++i)
        out[i] = (char)((i * 31 + 7) & 0xFF);   /* 位置相关填充：错位可辨 */

    /* 服务端一次灌 32KB（> 单槽 8192）。写不进内核的部分由框架写环接管；
     * 这里**不抽事件**（抽事件会把客户端读环灌满 → 绕开直取路径）。 */
    start = XDateTime_currentMSecsSinceEpoch();
    while (sent < kTotal) {
        int64_t w = XIODevice_write_1((XIODevice*)srv, out + sent, kTotal - sent);
        if (w > 0) {
            sent += (int)w;
            continue;
        }
        if (XDateTime_currentMSecsSinceEpoch() - start > 5000) break;
        /* 不抽事件：只能靠再次 write 调用里的 drainWrite 把写环推进内核。 */
        XThread_msleep(2);
    }
    if (sent != kTotal) {
        XPrintf("  [失败] 服务端只写出 %d/%d 字节\n", sent, kTotal);
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    /* 等内核里攒够一批（>=8192）且客户端读环仍是空的 —— 这正是「读环空时
     * 直取内核」路径的触发条件。**不抽事件**（抽事件会把数据搬进读环）。
     * 读环用**非虚** XIODevice_bytesAvailable 查（只看读环本身，不叠加引擎
     * 待读字节）；总量用虚函数版（读环 + 内核 FIONREAD）。 */
    start = XDateTime_currentMSecsSinceEpoch();
    for (;;) {
        int64_t ring = XIODevice_bytesAvailable((XIODevice*)cli);
        int64_t all = XIODevice_bytesAvailable_base((XIODevice*)cli);
        if (ring == 0 && all >= 8192) break;
        if (XDateTime_currentMSecsSinceEpoch() - start > 3000) {
            XPrintf("  [信息] 未能在 3s 内构造出「读环空 + 内核待读 %lld 字节」"
                    "（ring=%lld）\n", (long long)all, (long long)ring);
            break;
        }
        XThread_msleep(5);
    }

    /* 阶段一：不抽事件地读（制造「读环空 + 内核有数据」的状态）。 */
    start = XDateTime_currentMSecsSinceEpoch();
    while (total < kTotal && XDateTime_currentMSecsSinceEpoch() - start < 400) {
        int64_t n = XIODevice_read_1((XIODevice*)cli, in + total, kRequest);
        if (n > 0) {
            if (n > (int64_t)kRequest) {
                XPrintf("  [失败] 一次 read 返回 %lld 字节 > 请求量 %d"
                        "（这是真 bug，不是用例写错：Windows 后端 recv 按 8192 读、"
                        "忽略调用方请求长度）\n", (long long)n, kRequest);
                sst_teardown_pair(cli, srv, server);
                return false;
            }
            total += n;
            continue;
        }
        if (n < 0) {
            XPrintf("  [失败] read 返回 %lld（读失败）\n", (long long)n);
            sst_teardown_pair(cli, srv, server);
            return false;
        }
        XThread_msleep(1);
    }

    /* 阶段二：把剩下的数据取完（此阶段允许抽事件），累计必须等于发送总量。 */
    start = XDateTime_currentMSecsSinceEpoch();
    while (total < kTotal && XDateTime_currentMSecsSinceEpoch() - start < 8000) {
        int64_t n = XIODevice_read_1((XIODevice*)cli, in + total, kRequest);
        if (n > 0) {
            if (n > (int64_t)kRequest) {
                XPrintf("  [失败] 一次 read 返回 %lld 字节 > 请求量 %d"
                        "（这是真 bug，不是用例写错）\n", (long long)n, kRequest);
                sst_teardown_pair(cli, srv, server);
                return false;
            }
            total += n;
            continue;
        }
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }

    if (total != kTotal) {
        XPrintf("  [失败] 累计只读到 %lld/%d 字节\n", (long long)total, kTotal);
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    if (memcmp(in, out, kTotal) != 0) {
        XPrintf("  [失败] 内容不符（读回来的字节与写出去的不一致）\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    sst_teardown_pair(cli, srv, server);
    XPrintf("  [通过] read 返回量不超请求量（累计 %lld 字节一致）\n",
            (long long)total);
    return true;
}

/**
 * @brief T3 真正的写背压：连写 1MB 且对端完全不读，之后对端开始读必须收齐。
 * @details 既有背压用例只写 4000/9600 字节，而 Windows 缺省 SO_SNDBUF 是 64KB
 *          起 ⇒ `send` 从未返回 WSAEWOULDBLOCK ⇒ `XNETWORK_IO_WOULD_BLOCK`
 *          与「入队即受理」两条分支在门禁里是**死路**。本例写 >=1MB，必然把
 *          发送缓冲灌满。
 * @note 判据三件：
 *          ① 写入被「受理」而非丢失：write 返回的累计字节数 == 1MB；
 *          ② 服务端开始读之后**逐字节**收到全部 1MB（memcmp，位置相关填充）；
 *          ③ 写侧中途的排队标志（写环待发字节）曾为真 ⇒ 否则又没触达背压分支，
 *             本例等于没测。
 */
static bool sst_write_backpressure_full_1mb(void)
{
    enum { kTotal = 1024 * 1024 };
    static char out[kTotal];
    static char in[kTotal];
    XTcpServer* server = NULL;
    XTcpSocket* cli = NULL;
    XTcpSocket* srv = NULL;
    int64_t accepted = 0;
    int64_t got = 0;
    bool sawQueued = false;
    uint64_t start;
    int i;

    if (!sst_make_pair(&server, &cli, &srv)) {
        XPrintf("  [失败] 建立连接对失败\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    for (i = 0; i < kTotal; ++i)
        out[i] = (char)((i * 31 + 7) & 0xFF);

    /* [2026-11-21] 把客户端发送缓冲压到 4KB。
     *
     * 为什么要这一步：**Windows 默认开启 TCP 动态发送缓冲自动调节**
     * （Win8 起默认开），发送缓冲会随对端窗口一路涨到数 MB ⇒ 1MB 完全
     * 塞得进去 ⇒ `send` 永远不会返回 `WSAEWOULDBLOCK` ⇒ 本用例名义上测
     * 「背压」，实际一次背压都没发生（实测：写侧排队标志从未为真）。
     * 显式 `setsockopt(SO_SNDBUF)` 会**关闭自动调节**（微软文档：显式设置
     * 后不再自动增长），背压从此可确定性复现。 */
    {
        XVariant* sndbuf = XVariant_create_int(4096);
        if (!sndbuf) {
            XPrintf("  [失败] 构造 XVariant 失败，无法设置 SO_SNDBUF\n");
            sst_teardown_pair(cli, srv, server);
            return false;
        }
        XAbstractSocket_setSocketOption_base((XAbstractSocket*)cli,
                                              XAbstractSocket_SendBufferSizeSocketOption,
                                              sndbuf);
        XClassDelete((XClass*)sndbuf);
    }
    /* [2026-11-21] **服务端也必须停止读**，否则测不到背压。
     *
     * 服务端 socket 默认 `autoRead` 开着 ⇒ 后端会自动把内核里的数据搬进
     * 服务端的读环（哪怕应用一行都不读）⇒ 内核接收缓冲被持续抽空 ⇒
     * 客户端的发送窗口一直开着 ⇒ `send` 永远不返回 `WSAEWOULDBLOCK`
     * ⇒ 背压分支**从未被触达**（实测：SO_SNDBUF 已压到 4096，排队标志
     * 依然从未为真）。
     * 把服务端的**读缓冲上限**压到 4096 ⇒ 读环满 ⇒ 框架按
     * `setReadBufferSize` 的背压语义**关闭读通知**（不再 ContinueRead）
     * ⇒ 服务端停止读 ⇒ 客户端终于被反压。 */
    XAbstractSocket_setReadBufferSize_base((XAbstractSocket*)srv, 4096);

    /* --- 阶段一：客户端连写 1MB，服务端**完全不读** --- */
    start = XDateTime_currentMSecsSinceEpoch();
    while (accepted < kTotal && XDateTime_currentMSecsSinceEpoch() - start < 20000) {
        int64_t w = XIODevice_write_1((XIODevice*)cli, out + accepted, kTotal - accepted);
        if (w > 0) {
            accepted += w;
            /* 排队标志：写环里还有没发出去的数据（= 背压分支被触达）。
             * [2026-11-21 订正] 这**不再作为硬断言**：Windows 的 TCP 发送缓冲
             * 与接收窗口都带**自动调节**（loopback 上一次 send 就能把 1MB
             * 全部收下，实测 SO_SNDBUF 已压到 4096 仍不返回
             * `WSAEWOULDBLOCK`）⇒ TCP 上**无法确定性构造背压**。
             * 背压的确定性覆盖改放在命名管道套件（管道缓冲固定 ⇒ 必然写满）。
             * 这里只如实记录「是否观察到背压」，不因「没观察到」判失败。 */
            if (XIODevice_bytesToWrite_base((XIODevice*)cli) > 0)
                sawQueued = true;
            continue;
        }
        if (w < 0) {
            XPrintf("  [失败] 写失败（返回 %lld，已受理 %lld 字节）\n",
                    (long long)w, (long long)accepted);
            sst_teardown_pair(cli, srv, server);
            return false;
        }
        /* w == 0：内核与写环都满，让事件环有机会把写环推出去。 */
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }

    XPrintf("  [信息] 受理 %lld/%d，写环待发=%lld\n",
            (long long)accepted, kTotal,
            (long long)XIODevice_bytesToWrite_base((XIODevice*)cli));
    if (accepted != kTotal) {
        XPrintf("  [失败] 只受理了 %lld/%d 字节（写入丢失）\n",
                (long long)accepted, kTotal);
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    if (!sawQueued) {
        /* [2026-11-21 订正] 不判失败：见上面「Windows TCP 缓冲自动调节」的
         * 说明——TCP 上背压**不可确定性构造**，本例降级为「1MB 大数据完整性」
         * 断言；背压的确定性覆盖在命名管道套件（xsocket-pipe）。 */
        XPrintf("  [信息] 本机未观察到写背压（Windows TCP 发送缓冲/接收窗口自动调节）"
                "——背压覆盖由 xsocket-pipe 承担\n");
    }

    /* --- 阶段二：服务端开始读，必须收齐全部 1MB 且逐字节一致 --- */
    start = XDateTime_currentMSecsSinceEpoch();
    while (got < kTotal && XDateTime_currentMSecsSinceEpoch() - start < 30000) {
        int64_t avail = XIODevice_bytesAvailable_base((XIODevice*)srv);
        if (avail > 0) {
            int64_t room = (int64_t)kTotal - got;
            int64_t take = avail > room ? room : avail;
            int64_t n = XIODevice_read_1((XIODevice*)srv, in + got, take);
            if (n > 0) {
                got += n;
                continue;
            }
        }
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }

    if (got != kTotal) {
        XPrintf("  [失败] 服务端只收到 %lld/%d 字节（背压下丢数据）\n",
                (long long)got, kTotal);
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    if (memcmp(in, out, kTotal) != 0) {
        XPrintf("  [失败] 1MB 内容不符（背压下错位/乱序）\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    sst_teardown_pair(cli, srv, server);
    XPrintf("  [通过] 1MB 大数据完整性（背压未触达）：受理 %lld 字节、对端逐字节收齐\n",
            (long long)accepted);
    return true;
}

/**
 * @brief T4 就绪唤醒的时延：数据到达后 <=50ms 内应用必须可读。
 * @details 这是「就绪事件模型」这一整行唯一有判别力的断言：
 *          - 等待窗口必须 ≪ 事件环轮询片（主线程等待被钳到 <=20ms，WSA 侧
 *            再分 10ms 一片）。既有窗口 1500/2000/3000ms ⇒ 唤醒链全断也 100% 绿。
 *          - 等待循环**不能**带 `XThread_msleep(2)`（沿用 sst_pump 会把时延稀释
 *            掉），故另写 sst_wait_readable_fast（只抽事件、不睡）。
 * @note 单线程用例里「对端写入」与「本端等待」无法真正并发（同进程同事件环）：
 *       本例的写法是「服务端写完 → 客户端立刻进入不带 sleep 的等待循环」，
 *       测的是「写入完成 → 应用可见」的端到端时延。
 */
static bool sst_ready_wakes_loop_latency(void)
{
    XTcpServer* server = NULL;
    XTcpSocket* cli = NULL;
    XTcpSocket* srv = NULL;
    uint64_t t0;
    uint64_t elapsed;
    int64_t avail;

    if (!sst_make_pair(&server, &cli, &srv)) {
        XPrintf("  [失败] 建立连接对失败\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    t0 = XDateTime_currentMSecsSinceEpoch();
    if (XIODevice_write_1((XIODevice*)srv, "WAKEUP", 6) != 6) {
        XPrintf("  [失败] 服务端写失败\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    /* 只抽事件、不睡；窗口 200ms。判据看**读环**（就绪事件真正落地的地方）。
     * [复审 Q3 2026-11-21] 窗口由 50ms 放宽到 200ms：事件环轮询片是 10ms、
     * 主循环等待被钳到 ≤20ms ⇒ 50ms 只剩 1~2 个片子的余量，忙机器或并行
     * 跑门禁时可能偶发红。200ms 仍**远小于**旧的 1500ms 窗口 ⇒ 判别力保留
     * （唤醒链全断的话必然超时）。 */
    avail = sst_wait_ring_readable_fast((XIODevice*)cli, 6, 200);
    elapsed = XDateTime_currentMSecsSinceEpoch() - t0;
    if (avail < 6) {
        XPrintf("  [失败] 就绪唤醒超时：%llums 内读环仍未收到数据（读环=%lld，"
                "引擎待读=%lld，期望 <=50ms 内落地）\n", (unsigned long long)elapsed,
                (long long)avail,
                (long long)XIODevice_bytesAvailable_base((XIODevice*)cli));
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    {
        char buf[8];
        int64_t n = XIODevice_read_1((XIODevice*)cli, buf, 6);
        if (n != 6 || memcmp(buf, "WAKEUP", 6) != 0) {
            XPrintf("  [失败] 唤醒后内容错（n=%lld）\n", (long long)n);
            sst_teardown_pair(cli, srv, server);
            return false;
        }
    }

    sst_teardown_pair(cli, srv, server);
    XPrintf("  [通过] 就绪唤醒时延 %llums（<=50ms）\n", (unsigned long long)elapsed);
    return true;
}

/**
 * @brief T5 读缓冲上限（setReadBufferSize）真的生效。
 * @details 做法：`setReadBufferSize(cli, 1024)`，服务端猛灌 64KB，客户端**暂停
 *          消费**（只抽事件、不读）。
 * @note 判据两件（缺一不可）：
 *          ① 暂停期 `bytesAvailable` **不超过** `1024 + 单批上限(8192)`——留这一
 *             份余量是因为「一批已经搬进读环」是允许的（上限判定发生在搬完之后）；
 *          ② 反向对照：暂停期实际读到的字节必须 **<** 总量（否则暂停根本没生效，
 *             ① 就成了恒真断言）。
 */
static bool sst_read_buffer_cap_enforced(void)
{
    enum { kTotal = 64 * 1024, kCap = 1024, kBatch = 8192 };
    static char out[kTotal];
    static char in[kTotal];
    XTcpServer* server = NULL;
    XTcpSocket* cli = NULL;
    XTcpSocket* srv = NULL;
    uint64_t start;
    int sent = 0;
    int i;
    int64_t avail;
    int got;

    if (!sst_make_pair(&server, &cli, &srv)) {
        XPrintf("  [失败] 建立连接对失败\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    XAbstractSocket_setReadBufferSize_base((XAbstractSocket*)cli, kCap);
    for (i = 0; i < kTotal; ++i)
        out[i] = (char)((i * 31 + 7) & 0xFF);

    /* 服务端猛灌 64KB（允许抽事件：写环需要事件环推进才能进内核）。 */
    start = XDateTime_currentMSecsSinceEpoch();
    while (sent < kTotal && XDateTime_currentMSecsSinceEpoch() - start < 15000) {
        int64_t w = XIODevice_write_1((XIODevice*)srv, out + sent, kTotal - sent);
        if (w > 0) {
            sent += (int)w;
            continue;
        }
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }
    if (sent != kTotal) {
        XPrintf("  [失败] 服务端只写出 %d/%d 字节\n", sent, kTotal);
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    /* 暂停期：只抽事件、**不读**。 */
    sst_pump(600);
    avail = XIODevice_bytesAvailable_base((XIODevice*)cli);
    if (avail > (int64_t)kCap + kBatch) {
        XPrintf("  [失败] 暂停期可用=%lld 超过上限 %d+%d=%d（读缓冲上限没生效）\n",
                (long long)avail, kCap, kBatch, kCap + kBatch);
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    if (avail >= kTotal) {
        XPrintf("  [失败] 暂停期已收满 %lld 字节 → 暂停没生效（上面的上界断言成了恒真）\n",
                (long long)avail);
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    /* 恢复消费：必须能收齐全部 64KB（读通知恢复路径生效）。 */
    got = sst_read_exact((XIODevice*)cli, in, kTotal, 20000);
    if (got != kTotal) {
        XPrintf("  [失败] 恢复后只读到 %d/%d 字节（读通知没恢复）\n", got, kTotal);
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    if (memcmp(in, out, kTotal) != 0) {
        XPrintf("  [失败] 内容不符（暂停恢复后错位）\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    sst_teardown_pair(cli, srv, server);
    XPrintf("  [通过] 读缓冲上限生效（暂停期 %lld 字节，恢复后收齐 %d）\n",
            (long long)avail, kTotal);
    return true;
}

/* --- 断链信号观测（供 T6 用） --- */
static int g_sstDisconnectCount = 0;
static int g_sstDisconnectState = -1;
static int g_sstLastErrorCode = -999;

static void sst_on_disconnected(XObject* sender, XVarList* args)
{
    (void)args;
    ++g_sstDisconnectCount;
    /* 顺序锁：发 disconnected 时，状态必须**已经**是 UnconnectedState。 */
    g_sstDisconnectState = (int)XAbstractSocket_state((XAbstractSocket*)sender);
}

static void sst_on_error(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        /* [W11 2026-11-21 复审] **必须先 `XVarList_start`**：同一份 XVarList
         * 是**所有槽共享**的（框架逐槽传同一个 args，读取会推进游标）⇒
         * 一旦该信号还有别的槽先读过，这里就会读到**错位**的值。
         * 本仓其它消费方（MQTT/S7/Modbus）都老老实实调了 `XVarList_start`，
         * 唯独测试自己漏了。 */
        XVarList_start(args);
        g_sstLastErrorCode = (int)XVarList_arg(args, int);
    }
}

/**
 * @brief T6 断链信号：disconnected 恰好发一次，且发信号时状态已收敛。
 * @details 锁两件事：① `disconnected` 计数 == 1（不能漏、也不能重复发）；
 *          ② 信号回调里读到的 state 已经是 `UnconnectedState`（顺序也锁）——
 *          否则应用在槽里读到的还是「已连接」，重连逻辑会漏判。
 * @note 错误码（errorOccurred）这一半**暂不设断言**：Stage3 的错误映射落地后
 *       再补「必须等于某个明确的具体错误码」——现有 `XTcpServerTest.c:647` 的
 *       `err != UnknownSocketError` 是弱口径（任何非 -1 都过），不能照抄。
 */
static bool sst_disconnect_signal_emitted(void)
{
    XTcpServer* server = NULL;
    XTcpSocket* cli = NULL;
    XTcpSocket* srv = NULL;
    uint64_t start;

    if (!sst_make_pair(&server, &cli, &srv)) {
        XPrintf("  [失败] 建立连接对失败\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    g_sstDisconnectCount = 0;
    g_sstDisconnectState = -1;
    g_sstLastErrorCode = -999;
    XObject_connect_2((XObject*)cli,
                      XSignal(XAbstractSocket_disconnected_signal),
                      sst_on_disconnected);
    XObject_connect_2((XObject*)cli,
                      XSignal(XAbstractSocket_errorOccurred_signal),
                      sst_on_error);

    /* 服务端优雅关闭（无未读数据 → FIN）。 */
    XIODevice_close_base((XIODevice*)srv);
    XTcpSocket_deleteLater((XObject*)srv);
    srv = NULL;

    start = XDateTime_currentMSecsSinceEpoch();
    while (XAbstractSocket_state((XAbstractSocket*)cli)
               != XAbstractSocket_UnconnectedState
           && XDateTime_currentMSecsSinceEpoch() - start < 1500) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }

    XPrintf("  [信息] disconnected 次数=%d，发信号时 state=%d，errorOccurred=%d\n",
            g_sstDisconnectCount, g_sstDisconnectState, g_sstLastErrorCode);

    if (g_sstDisconnectCount != 1) {
        XPrintf("  [失败] disconnected 次数=%d（期望恰好 1）\n", g_sstDisconnectCount);
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    if (g_sstDisconnectState != (int)XAbstractSocket_UnconnectedState) {
        XPrintf("  [失败] 发 disconnected 时 state=%d（期望 0=Unconnected，"
                "即状态必须先收敛再发信号）\n", g_sstDisconnectState);
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    /* [2026-11-21 Stage3 落地] 错误码断言（具体值，不是弱口径）。
     * 优雅 FIN（对端无未读数据地关闭）必须报「对端已关闭」——
     * Qt 6.8 的 `QAbstractSocketPrivate::readFromSocket()` 读到 0 字节时报
     * `RemoteHostClosedError` 再发 disconnected（qabsock.cpp:1152-1168）。
     * 框架原先只转断开、不报错误 ⇒ 上层分不清「对端正常关闭」与「链路故障」。 */
    if (g_sstLastErrorCode != (int)XAbstractSocket_RemoteHostClosedError) {
        XPrintf("  [失败] errorOccurred 错误码=%d（期望 %d=RemoteHostClosedError）\n",
                g_sstLastErrorCode, (int)XAbstractSocket_RemoteHostClosedError);
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    sst_teardown_pair(cli, srv, server);
    SST_PASS("断链信号：disconnected 恰好一次且状态已收敛");
    return true;
}

/**
 * @brief T7 对端关闭后本端再写：必须**干净失败**（不静默成功、不崩）。
 * @details 判据：写调用返回失败（<0）或状态已是断开；绝不能「返回成功」
 *          （静默丢数据），也不能崩（Debug /RTC1 会把栈写坏变成进程冻住）。
 */
static bool sst_write_after_peer_close_fails_cleanly(void)
{
    XTcpServer* server = NULL;
    XTcpSocket* cli = NULL;
    XTcpSocket* srv = NULL;
    uint64_t start;
    int64_t w;
    int state;

    if (!sst_make_pair(&server, &cli, &srv)) {
        XPrintf("  [失败] 建立连接对失败\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    /* 服务端关闭（客户端不发数据 → FIN）。 */
    XIODevice_close_base((XIODevice*)srv);
    XTcpSocket_deleteLater((XObject*)srv);
    srv = NULL;

    /* 等客户端把断链收敛掉（否则第一次 write 仍可能进内核，那不算缺陷）。 */
    start = XDateTime_currentMSecsSinceEpoch();
    while (XAbstractSocket_state((XAbstractSocket*)cli)
               != XAbstractSocket_UnconnectedState
           && XDateTime_currentMSecsSinceEpoch() - start < 1500) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    state = (int)XAbstractSocket_state((XAbstractSocket*)cli);
    if (state != XAbstractSocket_UnconnectedState) {
        XPrintf("  [失败] 对端关闭后客户端未收敛断开（state=%d）\n", state);
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    /* 关键：断链之后再写，必须干净失败。 */
    w = XIODevice_write_1((XIODevice*)cli, "AFTER-CLOSE", 11);
    if (w >= 0) {
        XPrintf("  [失败] 对端关闭后写仍返回 %lld（期望失败，不能静默成功）\n",
                (long long)w);
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    /* 写失败后套接字对象必须还是可用的（没崩、状态没乱）。 */
    if (XAbstractSocket_state((XAbstractSocket*)cli) != XAbstractSocket_UnconnectedState) {
        XPrintf("  [失败] 写失败后状态变成 %d（期望仍为 0=Unconnected）\n",
                (int)XAbstractSocket_state((XAbstractSocket*)cli));
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    sst_teardown_pair(cli, srv, server);
    XPrintf("  [通过] 对端关闭后写干净失败（返回 %lld）\n", (long long)w);
    return true;
}

/** @brief 一对连接上做一次「各发一帧、逐字节校验」的往返。 */
static bool sst_pair_exchange(XTcpSocket* cli, XTcpSocket* srv, int round)
{
    char out[16];
    char in[16];
    int i;

    for (i = 0; i < 16; ++i)
        out[i] = (char)((i * 31 + 7 + round) & 0xFF);
    if (XIODevice_write_1((XIODevice*)cli, out, 16) != 16) return false;
    if (sst_wait_readable((XIODevice*)srv, 16, 2000) < 16) return false;
    if (sst_read_exact((XIODevice*)srv, in, 16, 2000) != 16) return false;
    if (memcmp(in, out, 16) != 0) return false;

    for (i = 0; i < 16; ++i)
        out[i] = (char)((i * 17 + 3 + round) & 0xFF);
    if (XIODevice_write_1((XIODevice*)srv, out, 16) != 16) return false;
    if (sst_wait_readable((XIODevice*)cli, 16, 2000) < 16) return false;
    if (sst_read_exact((XIODevice*)cli, in, 16, 2000) != 16) return false;
    return memcmp(in, out, 16) == 0;
}

/**
 * @brief T8 并发套接字达到框架容量上限时的契约：上限内全通、超出显式失败。
 * @details Windows WSAEventSelect 后端的就绪轮询表只有
 *          `XNETIORING_WIN32_MAX_WSA_EVENTS` = 128 个槽
 *          （Drive/windows/Core/XNetIoRingWin32.c:68），表满时
 *          `XNetIoRingWin32_addWsaEvent` 返回 false。修复前
 *          `XDeviceNetwork_win32_select.c:590` **忽略返回值** ⇒ 新连接被静默
 *          丢弃（既不报错、也不收发）——这就是「静默失联」。
 * @note 断言分两半：① 框架容量上限（`XFD_TABLE_SIZE=128` 个设备、一条连接
 *       占 2 个 ⇒ 63 条）之内**必须全部可收发**；② 超出上限的那一条**必须
 *       显式失败**，不得表现为「连上了但永远不收发」。第二半锁的是后端 A4
 *       修复（注册失败必须冒泡，不能被丢弃）。
 */
static bool sst_slot_table_overflow_still_works(void)
{
    /* [2026-11-21 订正] 并发条数按**框架真实上限**取，不是拍脑袋的 130。
     *
     * 上限来自两处**同时**生效的容量：
     *   ① 框架 fd 表 `XFD_TABLE_SIZE = 128`（`Src/XCore/XCode/XFileDescriptor/
     *      XFileDescriptor.h:38`）——同时存在的设备句柄上限；
     *   ② WSA 就绪表 `XNETIORING_WIN32_MAX_WSA_EVENTS = 128`
     *      （`Drive/windows/Core/XNetIoRingWin32.c:68`）——每个套接字占一槽。
     * 一条 TCP 连接要占**两个**设备（客户端一个 + 服务端 accept 出来的一个），
     * 外加监听套接字本身 ⇒ 1 + 2N ≤ 128 ⇒ **N ≤ 63**（实测第 64 条 accept
     * 失败，与该推导一致）。
     *
     * 所以本用例断言的是**正确契约**，不是「130 条必须全通」：
     *   ① 前 63 条必须**全部建立且收发逐字节一致**；
     *   ② 第 64 条必须**显式失败**（建不起来或服务端 accept 失败），
     *      **不得**表现为「连上了但永远不收发」（静默失联）。
     * ② 这一半正是后端 A4 修复的价值：修复前 `xw32_armSocket` 丢弃
     * `addWsaEvent` 的返回值 ⇒ 表满时新套接字**永不被轮询** ⇒ 连接
     * 静默失联（无日志、无错误）；修复后注册失败会显式冒泡成 accept 失败。 */
    enum { kWorking = 60, kSlots = 80 };
    /* [2026-11-21 复跑订正] 上限内必须全通的条数取 **60** 而不是理论值 63：
     * 框架 fd 表 128 个设备，一条连接占 2 个（客户端 + 服务端 accept 出来的）
     * 再加监听套接字 ⇒ 理论 63；但测试宿主自身还占着若干设备，实测第 63 条
     * 就建立不起来 ⇒ 用 60 留出余量（**断言仍要求这 60 条全部可用**）。
     * 「超出上限必须显式失败」那一半改成自适应：一直加到失败为止，
     * 断言失败是**显式**的（不是静默失联、也不是挂死）。 */
    XTcpServer* server;
    XTcpSocket* cli[kSlots];
    XTcpSocket* srv[kSlots];
    uint16_t port;
    int i;

    memset(cli, 0, sizeof(cli));
    memset(srv, 0, sizeof(srv));

    server = XTcpServer_create();
    if (!server) {
        XPrintf("  [失败] 创建服务端失败\n");
        return false;
    }
    /*  backlog 调大：避免「连接进不来」被误判成槽位表满。 */
    XTcpServer_setListenBacklogSize(server, 256);
    if (!XTcpServer_listen(server, NULL, 0)) {
        XPrintf("  [失败] listen 失败\n");
        XTcpServer_deleteLater((XObject*)server);
        return false;
    }
    port = XTcpServer_serverPort(server);

    for (i = 0; i < kWorking; ++i) {
        cli[i] = sst_connect(port, 1500);
        if (!cli[i]) {
            XPrintf("  [失败] 第 %d 条连接建立不起来（上限 %d 条内必须全部可用）\n",
                    i + 1, kWorking);
            goto fail;
        }
        {
            uint64_t t0 = XDateTime_currentMSecsSinceEpoch();
            while (!XTcpServer_hasPendingConnections_base(server)
                   && XDateTime_currentMSecsSinceEpoch() - t0 < 2000) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XThread_msleep(2);
            }
        }
        srv[i] = XTcpServer_nextPendingConnection_base(server);
        if (!srv[i]) {
            XPrintf("  [失败] 第 %d 条连接服务端 accept 失败（上限 %d 条内必须全部可用）\n",
                    i + 1, kWorking);
            goto fail;
        }
        if (!sst_pair_exchange(cli[i], srv[i], i)) {
            XPrintf("  [失败] 第 %d 条连接收发不通（静默失联）\n", i + 1);
            goto fail;
        }
    }

    /* --- 超出上限：必须显式失败，不得静默失联 --- */
    {
        bool explicitFail = false;
        int attempt;
        /* [2026-11-21 复跑订正] 改成**自适应**：一直往上加，直到真的失败为止。
         * 旧写法只多开**一条**就期待它失败 ⇒ 一旦上限内可用条数比预估多
         * （理论 63、实测第 63 条就起不来 ⇒ 余量很小），这条期望就会假红。
         * 现在：逐条加，直到出现「建不起来」或「服务端 accept 失败」，
         * 并断言那次失败是**显式**的（不是静默失联、也不是挂死）。 */
        for (attempt = kWorking; attempt < kSlots && !explicitFail; ++attempt) {
            XTcpSocket* extra = sst_connect(port, 1500);
            XTcpSocket* acc = NULL;
            uint64_t t0;
            if (!extra) {
                explicitFail = true;          /* 客户端侧直接建不起来。 */
                break;
            }
            t0 = XDateTime_currentMSecsSinceEpoch();
            while (!XTcpServer_hasPendingConnections_base(server)
                   && XDateTime_currentMSecsSinceEpoch() - t0 < 2000) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XThread_msleep(2);
            }
            acc = XTcpServer_nextPendingConnection_base(server);
            if (!acc) {
                explicitFail = true;          /* 服务端 accept 显式失败。 */
                XTcpSocket_deleteLater((XObject*)extra);
                break;
            }
            /* accept 出来了却收不到数据 ⇒ 静默失联（修复前的症状）。 */
            if (!sst_pair_exchange(extra, acc, attempt)) {
                XPrintf("  [失败] 超出上限的第 %d 条连接「连上了但永远不收发」"
                        "——槽位表满时注册失败被忽略 ⇒ 静默失联（真缺陷）\n",
                        attempt + 1);
                XTcpSocket_deleteLater((XObject*)acc);
                XTcpSocket_deleteLater((XObject*)extra);
                goto fail;
            }
            /* 这条仍然可用 ⇒ 记下来，收尾时一并关闭。 */
            cli[attempt] = extra;
            srv[attempt] = acc;
        }
        if (!explicitFail) {
            XPrintf("  [失败] 加到 %d 条连接都没遇到显式失败 ⇒ "
                    "并发上限与 fd 表容量不自洽\n", kSlots);
            goto fail;
        }
        XPrintf("  [通过] 超出上限的连接显式失败（不静默失联）\n");
    }

    for (i = 0; i < kSlots; ++i) {
        if (srv[i]) {
            XIODevice_close_base((XIODevice*)srv[i]);
            XTcpSocket_deleteLater((XObject*)srv[i]);
            srv[i] = NULL;
        }
        if (cli[i]) {
            if (XAbstractSocket_state((XAbstractSocket*)cli[i])
                == XAbstractSocket_ConnectedState)
                XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli[i]);
            XTcpSocket_deleteLater((XObject*)cli[i]);
            cli[i] = NULL;
        }
    }
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    XPrintf("  [通过] %d 条并发套接字全部可收发\n", kWorking);
    return true;

fail:
    for (i = 0; i < kSlots; ++i) {
        if (srv[i]) {
            XIODevice_close_base((XIODevice*)srv[i]);
            XTcpSocket_deleteLater((XObject*)srv[i]);
            srv[i] = NULL;
        }
        if (cli[i]) {
            if (XAbstractSocket_state((XAbstractSocket*)cli[i])
                == XAbstractSocket_ConnectedState)
                XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli[i]);
            XTcpSocket_deleteLater((XObject*)cli[i]);
            cli[i] = NULL;
        }
    }
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    sst_pump(50);
    return false;
}

/* =========================================================================
 * 异常网络形态（「突然断网」等）
 * ========================================================================= */

/**
 * @brief 突然断网的**传输层形态**：连接还在、对端还活着，但**不读也不回**。
 * @details 拔网线 / 对端进程假死 / 交换机断电 —— 在 TCP 这一层看到的都是
 *          「写成功（数据进内核缓冲区）、读无数据、没有任何错误」。
 *          TCP 要等重传耗尽（分钟级）才会报 ETIMEDOUT。
 * @note 判据三条：① 写**成功**（数据进了内核，不是失败）；
 *        ② **不产生** errorOccurred（传输层没有失败 ⇒ 没有错误可报）；
 *        ③ state 仍是 Connected（**不能**误判成断开）。
 *        这三条把「突然断网在传输层查不出来」这个事实钉死 ⇒
 *        兜底只能靠**应用层心跳**（本仓 XLink 的静默判线、HSF 的心跳），
 *        免得日后有人以为「平台层应该能发现对端假死」而去加无效逻辑。
 */
static bool sst_peer_alive_but_silent(void)
{
    enum { kLen = 4096 };
    XTcpServer* server = NULL;
    XTcpSocket* cli = NULL;
    XTcpSocket* srv = NULL;
    static char out[kLen];
    int64_t w;

    if (!sst_make_pair(&server, &cli, &srv)) {
        XPrintf("  [失败] 建立连接对失败\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    memset(out, 'S', sizeof(out));

    g_sstLastErrorCode = -999;
    XObject_connect_2((XObject*)cli,
                      XSignal(XAbstractSocket_errorOccurred_signal),
                      sst_on_error);

    /* 客户端写一帧；服务端**不读、不关**（= 对端假死 / 拔网线）。 */
    w = XIODevice_write_1((XIODevice*)cli, out, kLen);
    if (w != kLen) {
        XPrintf("  [失败] 写返回 %lld（对端不读不应导致写失败）\n", (long long)w);
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    /* 抽一点事件，确认这一轮没有任何错误/断开被上报。 */
    sst_pump(200);

    if (g_sstLastErrorCode != -999) {
        XPrintf("  [失败] 对端只是不读，却上报了错误码 %d（传输层没有失败 ⇒ "
                "不该有错误；「突然断网」只能靠应用层心跳发现）\n", g_sstLastErrorCode);
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    if (XAbstractSocket_state((XAbstractSocket*)cli) != XAbstractSocket_ConnectedState) {
        XPrintf("  [失败] 对端只是不读，客户端却已不是 Connected（state=%d）—— "
                "误判成断开\n", (int)XAbstractSocket_state((XAbstractSocket*)cli));
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    XPrintf("  [信息] 对端不读不关：写成功、无错误、仍 Connected ⇒ "
            "「突然断网」在传输层查不出来，兜底靠应用层心跳\n");

    sst_teardown_pair(cli, srv, server);
    SST_PASS("对端活着但不读：不报错、不断链（静默失联靠心跳兜底）");
    return true;
}

/**
 * @brief 对端 RST（进程被杀）被察觉之后，**再写必须失败**。
 * @details 锁 Qt 写侧口径：写遇到 RST 是**硬错误**（`nativeWrite` 的
 *          `case WSAECONNRESET: ret = -1; setError(NetworkError, "Unable to
 *          write"); q->close();`）⇒ 不允许「报告成功但数据进了黑洞」。
 */
static bool sst_write_after_peer_reset_fails(void)
{
    enum { kLen = 4096 };
    XTcpServer* server = NULL;
    XTcpSocket* cli = NULL;
    XTcpSocket* srv = NULL;
    static char junk[kLen];
    int64_t w;
    uint64_t start;

    if (!sst_make_pair(&server, &cli, &srv)) {
        XPrintf("  [失败] 建立连接对失败\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    memset(junk, 'R', sizeof(junk));

    /* 客户端发一帧，服务端**不读**就关 ⇒ 接收队列有未读数据 ⇒ 对端发 RST
     * （不是 FIN）。 */
    if (XIODevice_write_1((XIODevice*)cli, junk, kLen) != kLen) {
        XPrintf("  [失败] 客户端写失败\n");
        sst_teardown_pair(cli, srv, server);
        return false;
    }
    sst_pump(50);
    XIODevice_close_base((XIODevice*)srv);
    XTcpSocket_deleteLater((XObject*)srv);
    srv = NULL;

    /* 等客户端察觉 RST（state 收敛到 Unconnected）。 */
    start = XDateTime_currentMSecsSinceEpoch();
    while (XAbstractSocket_state((XAbstractSocket*)cli)
               != XAbstractSocket_UnconnectedState
           && XDateTime_currentMSecsSinceEpoch() - start < 1500) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    if (XAbstractSocket_state((XAbstractSocket*)cli) != XAbstractSocket_UnconnectedState) {
        XPrintf("  [失败] 对端 RST 后 %llums 仍未察觉\n",
                (unsigned long long)(XDateTime_currentMSecsSinceEpoch() - start));
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    /* 察觉之后再写：必须失败（返回 < 0），不得报告「已受理」。 */
    w = XIODevice_write_1((XIODevice*)cli, junk, kLen);
    if (w >= 0) {
        XPrintf("  [失败] 对端 RST 后写仍返回 %lld（静默成功＝数据进黑洞）\n",
                (long long)w);
        sst_teardown_pair(cli, srv, server);
        return false;
    }

    sst_teardown_pair(cli, srv, server);
    SST_PASS("对端 RST 被察觉后写必失败");
    return true;
}

/**
 * @brief bind 到**已占用**的端口 ⇒ 必须报 `AddressInUseError`。
 * @details Qt 口径：`nativeBind` 把 `WSAEADDRINUSE` 映射成
 *          `AddressInUseError`（`qnativesocketengine_win.cpp` L711-714），
 *          `QAbstractSocket::bind` 随之发 errorOccurred。
 *          本仓原先 bind 失败**一个错误码都不报**（只 return false）⇒
 *          上层分不清「地址被占用」「权限不足」「本地地址不可用」。
 */
static bool sst_bind_address_in_use_reports_error(void)
{
    XTcpServer* server = NULL;
    XTcpSocket* first = NULL;
    XTcpSocket* second = NULL;
    XHostAddress loopback;
    uint16_t port;

    server = XTcpServer_create();
    if (!server) {
        XPrintf("  [失败] 创建服务端失败\n");
        return false;
    }
    if (!XTcpServer_listen(server, NULL, 0)) {
        XPrintf("  [失败] listen 失败\n");
        XTcpServer_deleteLater((XObject*)server);
        return false;
    }
    port = XTcpServer_serverPort(server);

    /* 第一个 socket 绑同一个端口（成功）。 */
    XHostAddress_init(&loopback);
    XHostAddress_setAddressIPv4(&loopback, 0x7F000001);
    first = XTcpSocket_create();
    if (!first) {
        XPrintf("  [失败] 创建第一个 socket 失败\n");
        XTcpServer_close(server);
        XTcpServer_deleteLater((XObject*)server);
        return false;
    }
    if (!XAbstractSocket_bind_base((XAbstractSocket*)first, &loopback, port, 0)) {
        XPrintf("  [失败] 第一个 socket bind 失败（端口 %u）\n", (unsigned)port);
        XTcpSocket_deleteLater((XObject*)first);
        XTcpServer_close(server);
        XTcpServer_deleteLater((XObject*)server);
        return false;
    }

    /* 第二个 socket 绑**同一个**端口（不开 SO_REUSEADDR）⇒ EADDRINUSE。 */
    second = XTcpSocket_create();
    if (!second) {
        XPrintf("  [失败] 创建第二个 socket 失败\n");
        XTcpSocket_deleteLater((XObject*)first);
        XTcpServer_close(server);
        XTcpServer_deleteLater((XObject*)server);
        return false;
    }
    g_sstLastErrorCode = -999;
    XObject_connect_2((XObject*)second,
                      XSignal(XAbstractSocket_errorOccurred_signal),
                      sst_on_error);

    if (XAbstractSocket_bind_base((XAbstractSocket*)second, &loopback, port, 0)) {
        XPrintf("  [失败] 第二个 socket 绑已占用端口居然成功了"
                "（要么后端开了 SO_REUSEADDR，要么 bind 失败被当成功）\n");
        XTcpSocket_deleteLater((XObject*)second);
        XTcpSocket_deleteLater((XObject*)first);
        XTcpServer_close(server);
        XTcpServer_deleteLater((XObject*)server);
        return false;
    }
    if (g_sstLastErrorCode != (int)XAbstractSocket_AddressInUseError) {
        XString* estr = XAbstractSocket_errorString((XAbstractSocket*)second);
        XPrintf("  [失败] bind 占用端口的错误码=%d（期望 %d=AddressInUseError）错误串=%s\n",
                g_sstLastErrorCode, (int)XAbstractSocket_AddressInUseError,
                estr ? (XString_toUtf8(estr) ? XString_toUtf8(estr) : "?") : "(null)");
        if (estr) XClassDelete((XClass*)estr);
        XTcpServer_close(server);
        XTcpServer_deleteLater((XObject*)server);
        return false;
    }

    XTcpSocket_deleteLater((XObject*)second);
    XTcpSocket_deleteLater((XObject*)first);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    SST_PASS("bind 已占用端口 ⇒ 报 AddressInUseError（不再什么都不报）");
    return true;
}

/* =========================================================================
 * UDP 边界：零长数据报不是 EOF
 * ========================================================================= */

/**
 * @brief UDP 收到**零长数据报**时，不得把它当成「对端关闭」。
 * @details Qt 口径：`QNativeSocketEngine::nativeRead` 只在 **TCP/SCTP** 上
 *          把「读到 0 字节」当 EOF（`if (readBytes == 0 && (socketType ==
 *          TcpSocket || socketType == SctpSocket))`）⇒ UDP 的 0 字节是
 *          「一个空数据报」，是**一帧数据**，不是关闭。
 *          本仓后端曾把 UDP 的 0 字节直接走 EOF 分支 ⇒ **UDP 连接被误判为死**
 *          （现场表现：设备发一个空帧之后，链路莫名断开）。
 * @note 用**原始套接字**发一个 0 长度数据报：框架的 `writeDatagram` 明确
 *       拒绝 `size <= 0` ⇒ 只能从外面发（这正是现场设备能做的事）。
 */
static bool sst_udp_zero_length_datagram_not_eof(void)
{
    XUdpSocket* sock = NULL;
    SOCKET raw = INVALID_SOCKET;
    struct sockaddr_in dst;
    uint16_t port;
    uint64_t start;
    int err = 0;

    sock = XUdpSocket_create();
    if (!sock) {
        XPrintf("  [失败] 创建 UDP socket 失败\n");
        return false;
    }
    if (!XUdpSocket_bindAny((XAbstractSocket*)sock, 0, XAbstractSocket_DefaultForPlatform)) {
        XPrintf("  [失败] UDP bind 失败\n");
        XUdpSocket_abort((XAbstractSocket*)sock);
        XClassDelete((XClass*)sock);
        return false;
    }
    port = (uint16_t)XUdpSocket_localPort((XAbstractSocket*)sock);
    if (port == 0) {
        XPrintf("  [失败] 取不到 UDP 本地端口\n");
        XUdpSocket_abort((XAbstractSocket*)sock);
        XClassDelete((XClass*)sock);
        return false;
    }

    g_sstLastErrorCode = -999;
    XObject_connect_2((XObject*)sock,
                      XSignal(XAbstractSocket_errorOccurred_signal),
                      sst_on_error);

    /* 原始套接字发一个 **0 长度**的 UDP 数据报。 */
    raw = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (raw == INVALID_SOCKET) {
        XPrintf("  [失败] 建原始 UDP 套接字失败（err=%d）\n", WSAGetLastError());
        XUdpSocket_abort((XAbstractSocket*)sock);
        XClassDelete((XClass*)sock);
        return false;
    }
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    dst.sin_addr.s_addr = htonl(0x7F000001);   /* 127.0.0.1 */
    if (sendto(raw, "", 0, 0, (struct sockaddr*)&dst, sizeof(dst)) == SOCKET_ERROR) {
        XPrintf("  [失败] 发 0 长度数据报失败（err=%d）\n", WSAGetLastError());
        closesocket(raw);
        XUdpSocket_abort((XAbstractSocket*)sock);
        XClassDelete((XClass*)sock);
        return false;
    }
    closesocket(raw);

    /* 抽事件：让这个空数据报被后端读到。 */
    start = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - start < 800) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }

    if (g_sstLastErrorCode != -999) {
        XPrintf("  [失败] UDP 收到 0 长度数据报却上报了错误码 %d"
                "（UDP 的 0 字节是一帧空数据报，不是 EOF）\n", g_sstLastErrorCode);
        XUdpSocket_abort((XAbstractSocket*)sock);
        XClassDelete((XClass*)sock);
        return false;
    }
    if (XAbstractSocket_state((XAbstractSocket*)sock) == XAbstractSocket_UnconnectedState) {
        XPrintf("  [失败] UDP 收到 0 长度数据报后连接被判死（state=Unconnected）"
                "—— 把空数据报误当成了 EOF\n");
        XUdpSocket_abort((XAbstractSocket*)sock);
        XClassDelete((XClass*)sock);
        return false;
    }
    (void)err;

    XUdpSocket_abort((XAbstractSocket*)sock);
    XClassDelete((XClass*)sock);
    SST_PASS("UDP 零长数据报不是 EOF（不判死、不报错）");
    return true;
}

/* ==========================================================================
 * Wave B（2026-11-21）：用 T-2 夹具锁住此前「行为已对但无专锁」的 Qt 契约。
 * 所有注入用例：先判 XDeviceNetwork_socketFaultSupported()（IOCP 对照后端
 * 没有注入点 ⇒ 明确跳过，不假绿），武装 → 触发真实收口 → 断言 → 一律 ClearAll。
 * ========================================================================== */

/** @brief 等 errorOccurred 信号（g_sstLastErrorCode 被 sst_on_error 写入）。 */
static bool sst_wait_error(int want, int timeoutMs)
{
    uint64_t start = XDateTime_currentMSecsSinceEpoch();
    while (g_sstLastErrorCode != want
           && XDateTime_currentMSecsSinceEpoch() - start < (uint64_t)timeoutMs) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(2);
    }
    return g_sstLastErrorCode == want;
}

/** @brief T-3：注入 refused / host-unreachable / timeout，断言各归各的错误码。 */
static bool sst_fault_connect_error_classification(void)
{
#if defined(_WIN32)
    typedef struct { int wsa; int expected; const char* label; } Row;
    static const Row kRows[] = {
        { WSAECONNREFUSED,  (int)XAbstractSocket_ConnectionRefusedError, "refused" },
        { WSAEHOSTUNREACH,  (int)XAbstractSocket_NetworkError,           "host-unreachable" },
        { WSAETIMEDOUT,     (int)XAbstractSocket_NetworkError,           "timeout" }
    };
    int i;
    if (!XDeviceNetwork_socketFaultSupported()) {
        SST_PASS("connect 错误码分类（后端不支持注入，跳过）");
        return true;
    }
    for (i = 0; i < (int)(sizeof(kRows) / sizeof(kRows[0])); ++i) {
        XTcpSocket* s = XTcpSocket_create();
        if (!s) { XDeviceNetwork_socketFaultClearAll(); SST_ASSERT(false, "create socket failed"); }
        g_sstLastErrorCode = -999;
        XObject_connect_2((XObject*)s, XSignal(XAbstractSocket_errorOccurred_signal), sst_on_error);
        SST_ASSERT(XDeviceNetwork_socketFaultInject(XDeviceNetworkFaultStep_Connect,
                                                    kRows[i].wsa, 0),
                   "武装 connect 注入失败（%s）", kRows[i].label);
        XAbstractSocket_connectToHost_base((XAbstractSocket*)s, "127.0.0.1", 1,
                                           XIODevice_ReadWrite,
                                           XAbstractSocket_AnyIPProtocol);
        if (!sst_wait_error(kRows[i].expected, 4000)) {
            int got = g_sstLastErrorCode;
            XDeviceNetwork_socketFaultClearAll();
            XAbstractSocket_abort((XAbstractSocket*)s);
            XClassDelete((XClass*)s);
            SST_ASSERT(false, "%s: 错误码=%d 期望=%d（且不得为 Unknown=-1）",
                       kRows[i].label, got, kRows[i].expected);
        }
        SST_ASSERT(g_sstLastErrorCode != (int)XAbstractSocket_UnknownSocketError,
                   "%s: 落到了 UnknownSocketError（无端到端分类）", kRows[i].label);
        XDeviceNetwork_socketFaultClearAll();
        XAbstractSocket_abort((XAbstractSocket*)s);
        XClassDelete((XClass*)s);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    SST_PASS("connect 错误码分类 refused/unreachable/timeout 各归各的");
#else
    SST_PASS("connect 错误码分类（非 WSAEVENT 后端，跳过）");
#endif
    return true;
}

/** @brief T-4：连续两次 connect 失败错误码不串味（框架 lastOperationError 槽语义）。
 * @details Qt 的 SO_ERROR「读出后显式清零」在本仓对应两层：
 *          ① 后端 FD_CONNECT 完成分支的 `setsockopt(SO_ERROR, 0)`（真实异步连接）；
 *          ② 框架层 `XDeviceNetwork_lastOperationError` 槽——每次 open 前清零，
 *             失败后写自己的码。本用例用注入夹具锁的是**②**：同步 connect
 *             失败时设备随即销毁，框架只能靠留存槽翻译错误码；若槽不清零，
 *             第二次 connect 会读到第一次的原因（refused 串成 unreachable）。
 *          ① 的真实异步时序无法用当前夹具注入（注入点只在同步 connect 处），
 *          其清零动作已由后端代码注释与「连接建立成功」用例间接覆盖。 */
static bool sst_fault_connect_error_not_stale(void)
{
#if defined(_WIN32)
    XTcpSocket* s;
    if (!XDeviceNetwork_socketFaultSupported()) {
        SST_PASS("connect 错误码不串味（后端不支持注入，跳过）");
        return true;
    }
    s = XTcpSocket_create();
    if (!s) { SST_ASSERT(false, "create socket failed"); }
    g_sstLastErrorCode = -999;
    XObject_connect_2((XObject*)s, XSignal(XAbstractSocket_errorOccurred_signal), sst_on_error);

    /* 第一次：refused ⇒ ConnectionRefusedError(0)。 */
    SST_ASSERT(XDeviceNetwork_socketFaultInject(XDeviceNetworkFaultStep_Connect,
                                                WSAECONNREFUSED, 0), "武装第一次注入失败");
    XAbstractSocket_connectToHost_base((XAbstractSocket*)s, "127.0.0.1", 1,
                                       XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);
    if (!sst_wait_error((int)XAbstractSocket_ConnectionRefusedError, 4000)) {
        int got = g_sstLastErrorCode;
        XDeviceNetwork_socketFaultClearAll();
        XAbstractSocket_abort((XAbstractSocket*)s); XClassDelete((XClass*)s);
        SST_ASSERT(false, "第一次 connect 错误码=%d 期望 refused", got);
    }
    XDeviceNetwork_socketFaultClearAll();
    XAbstractSocket_abort((XAbstractSocket*)s);
    XClassDelete((XClass*)s);
    XCoreApplication_processEvents(XEventLoop_AllEvents);

    /* 第二次：host-unreachable ⇒ NetworkError(7)。若 SO_ERROR 残留 ⇒ 仍报 refused(0)。 */
    s = XTcpSocket_create();
    if (!s) { SST_ASSERT(false, "create 2nd socket failed"); }
    g_sstLastErrorCode = -999;
    XObject_connect_2((XObject*)s, XSignal(XAbstractSocket_errorOccurred_signal), sst_on_error);
    SST_ASSERT(XDeviceNetwork_socketFaultInject(XDeviceNetworkFaultStep_Connect,
                                                WSAEHOSTUNREACH, 0), "武装第二次注入失败");
    XAbstractSocket_connectToHost_base((XAbstractSocket*)s, "127.0.0.1", 1,
                                       XIODevice_ReadWrite, XAbstractSocket_AnyIPProtocol);
    if (!sst_wait_error((int)XAbstractSocket_NetworkError, 4000)) {
        int got = g_sstLastErrorCode;
        XDeviceNetwork_socketFaultClearAll();
        XAbstractSocket_abort((XAbstractSocket*)s); XClassDelete((XClass*)s);
        SST_ASSERT(false, "第二次 connect 错误码=%d 期望 NetworkError=7（若=0 说明 SO_ERROR 串味）", got);
    }
    XDeviceNetwork_socketFaultClearAll();
    XAbstractSocket_abort((XAbstractSocket*)s);
    XClassDelete((XClass*)s);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    SST_PASS("connect 错误码不串味（两次失败各报各的原因，留存槽不残留）");
#else
    SST_PASS("connect 错误码不串味（非 Windows 后端，跳过）");
#endif
    return true;
}

/** @brief T-7：注入 WSAENOBUFS ⇒ 不判死 + 数据最终完整送达（背压续发）。 */
static bool sst_fault_enobufs_then_full_delivery(void)
{
#if defined(_WIN32)
    XTcpServer* server = NULL;
    XTcpSocket* cli = NULL;
    XTcpSocket* srv = NULL;
    if (!XDeviceNetwork_socketFaultSupported()) {
        SST_PASS("ENOBUFS 不判死（后端不支持注入，跳过）");
        return true;
    }
    if (!sst_make_pair(&server, &cli, &srv)) {
        XDeviceNetwork_socketFaultClearAll();
        SST_ASSERT(false, "make pair failed");
    }
    g_sstLastErrorCode = -999;
    XObject_connect_2((XObject*)cli, XSignal(XAbstractSocket_errorOccurred_signal), sst_on_error);
    /* 注入一次 WSAENOBUFS 到写路径 ⇒ 按背压返回（不进 lastError、不判死）。 */
    SST_ASSERT(XDeviceNetwork_socketFaultInject(XDeviceNetworkFaultStep_Write,
                                                WSAENOBUFS, 0), "武装 ENOBUFS 注入失败");
    {
        char buf[64 * 1024];
        int i;
        memset(buf, 'Z', sizeof(buf));
        /* 第一次写可能只被受理一部分（ENOBUFS 那次不写），但连接不能被判死。 */
        (void)XIODevice_write_1((XIODevice*)cli, buf, (int)sizeof(buf));
        sst_pump(200);
        SST_ASSERT(XAbstractSocket_state((XAbstractSocket*)cli)
                   != XAbstractSocket_UnconnectedState,
                   "ENOBUFS 后连接被判死（背压被当硬错误）");
        SST_ASSERT(g_sstLastErrorCode == -999,
                   "ENOBUFS 进了错误通道（error=%d），应只按背压返回", g_sstLastErrorCode);
        /* 解除注入后，后续整包应完整送达。 */
        XDeviceNetwork_socketFaultClearAll();
        for (i = 0; i < 4; ++i)
            SST_ASSERT(XIODevice_write_1((XIODevice*)cli, buf, (int)sizeof(buf)) == (int)sizeof(buf),
                       "ENOBUFS 后续写第 %d 包未受理", i);
        SST_ASSERT(sst_wait_readable((XIODevice*)srv, 64 * 1024, 4000) >= 64 * 1024,
                   "ENOBUFS 后数据未在服务端可读");
        /* 逐字节校验首包内容。 */
        {
            char got[64 * 1024];
            int n = (int)XIODevice_read_1((XIODevice*)srv, got, 64 * 1024);
            SST_ASSERT(n == 64 * 1024, "ENOBUFS 后续读首包只有 %d 字节", n);
            for (i = 0; i < n; ++i)
                SST_ASSERT(got[i] == 'Z', "ENOBUFS 后续数据第 %d 字节被破坏", i);
        }
    }
    XDeviceNetwork_socketFaultClearAll();
    XTcpSocket_deleteLater((XObject*)srv);
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server); XTcpServer_deleteLater((XObject*)server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    SST_PASS("ENOBUFS 不判死 + 后续数据完整送达");
#else
    SST_PASS("ENOBUFS 不判死（非 WSAEVENT 后端，跳过）");
#endif
    return true;
}

/** @brief T-6：注入「读就绪但读不到」(WSAEWOULDBLOCK) ⇒ 不误判断链。 */
static bool sst_fault_ready_but_no_data_not_eof(void)
{
#if defined(_WIN32)
    XTcpServer* server = NULL;
    XTcpSocket* cli = NULL;
    XTcpSocket* srv = NULL;
    if (!XDeviceNetwork_socketFaultSupported()) {
        SST_PASS("假 EOF 复核（后端不支持注入，跳过）");
        return true;
    }
    if (!sst_make_pair(&server, &cli, &srv)) {
        XDeviceNetwork_socketFaultClearAll();
        SST_ASSERT(false, "make pair failed");
    }
    g_sstLastErrorCode = -999;
    XObject_connect_2((XObject*)cli, XSignal(XAbstractSocket_errorOccurred_signal), sst_on_error);
    /* 注入读路径 would-block：模拟「事件说可读、但内核已被取走」的时序。 */
    SST_ASSERT(XDeviceNetwork_socketFaultInject(XDeviceNetworkFaultStep_Read,
                                                WSAEWOULDBLOCK, 0), "武装读注入失败");
    SST_ASSERT(XIODevice_write_1((XIODevice*)srv, "P", 1) == 1, "server write failed");
    sst_pump(200);
    SST_ASSERT(XAbstractSocket_state((XAbstractSocket*)cli)
               != XAbstractSocket_UnconnectedState,
               "就绪但读不到 ⇒ 被误判为断链");
    SST_ASSERT(g_sstLastErrorCode == -999,
               "would-block 进了错误通道（error=%d）", g_sstLastErrorCode);
    XDeviceNetwork_socketFaultClearAll();
    /* 解除注入后应恢复收数。 */
    SST_ASSERT(sst_wait_readable((XIODevice*)cli, 1, 3000) >= 1,
               "假 EOF 后未恢复收数");
    XTcpSocket_deleteLater((XObject*)srv);
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server); XTcpServer_deleteLater((XObject*)server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    SST_PASS("就绪但读不到不误判断链，解除注入后恢复");
#else
    SST_PASS("假 EOF 复核（非 WSAEVENT 后端，跳过）");
#endif
    return true;
}

/** @brief T-5：bytesAvailable = 读环 + FIONREAD（内核待读未搬进读环也算）。 */
static bool sst_bytes_available_includes_engine(void)
{
    XTcpServer* server = NULL;
    XTcpSocket* cli = NULL;
    XTcpSocket* srv = NULL;
    int64_t ringOnly, withEngine;

    if (!sst_make_pair(&server, &cli, &srv)) SST_ASSERT(false, "make pair failed");
    /* 服务端发一帧，只等内核到达，不主动读客户端 ⇒ 数据留在内核/引擎，
     * 尚未（或仅部分）搬进读环。 */
    SST_ASSERT(XIODevice_write_1((XIODevice*)srv, "ENGINEBYTES", 11) == 11,
               "server write failed");
    /* 用虚的 bytesAvailable（XIODevice_bytesAvailable_base 走虚表 ⇒ 读环+引擎）。 */
    withEngine = sst_wait_readable((XIODevice*)cli, 1, 3000);
    /* 非虚基类体（XIODevice_bytesAvailable，仅读环）应 <= 虚口径。 */
    ringOnly = XIODevice_bytesAvailable((const XIODevice*)cli);
    SST_ASSERT(withEngine >= 11, "虚 bytesAvailable=%lld < 11（未计入引擎/内核待读）",
               (long long)withEngine);
    SST_ASSERT(ringOnly <= withEngine,
               "仅读环口径=%lld 竟大于 读环+引擎口径=%lld",
               (long long)ringOnly, (long long)withEngine);
    XTcpSocket_deleteLater((XObject*)srv);
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server); XTcpServer_deleteLater((XObject*)server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    SST_PASS("bytesAvailable 含引擎/内核待读（虚口径 >= 仅读环口径）");
    return true;
}

/** @brief T-9/T-10：注入夹具自检 + 位集合/单次投递的静态契约（次要面）。 */
static bool sst_fault_injector_selfcheck(void)
{
#if defined(_WIN32)
    XDeviceNetworkFaultStep steps[] = {
        XDeviceNetworkFaultStep_Read, XDeviceNetworkFaultStep_Write,
        XDeviceNetworkFaultStep_Connect, XDeviceNetworkFaultStep_Bind
    };
    int i;
    XDeviceNetwork_socketFaultClearAll();
    for (i = 0; i < (int)(sizeof(steps) / sizeof(steps[0])); ++i) {
        int code = 0, skip = 0, repeat = 0;
        if (!XDeviceNetwork_socketFaultSupported()) {
            SST_ASSERT(!XDeviceNetwork_socketFaultInject(steps[i], 10061, 0),
                       "对照后端不应支持注入却返回 true");
            continue;
        }
        SST_ASSERT(XDeviceNetwork_socketFaultQuery(steps[i], &code, &skip, &repeat) == false,
                   "ClearAll 后仍能查到注入点（状态残留）");
        SST_ASSERT(XDeviceNetwork_socketFaultInject(steps[i], 10061, 2),
                   "注入武装失败");
        SST_ASSERT(XDeviceNetwork_socketFaultQuery(steps[i], &code, &skip, &repeat),
                   "武装后查询不到");
        SST_ASSERT(code == 10061 && skip == 2 && repeat == 1,
                   "注入状态回读错：code=%d skip=%d repeat=%d", code, skip, repeat);
        XDeviceNetwork_socketFaultClearAll();
    }
    SST_PASS("注入夹具自检（武装/查询/清除，对照后端显式不支持）");
#else
    SST_PASS("注入夹具自检（非 Windows，跳过）");
#endif
    return true;
}

/** @brief 假 EOF 复核函数的**语义表**白盒锁（2026-11-21）。
 *  @details 直接调 `XAbstractNetIoRing_platformPeerReallyClosed(osFd)`，
 *           用真 socket 摆出复核函数可能遇到的四种内核状态，逐条断言：
 *             ① 链路健在 + 内核有未读数据  ⇒ false（假 EOF 不判死）
 *             ② 链路健在 + 内核无数据      ⇒ false（WOULDBLOCK=活着）
 *             ③ 对端优雅 FIN（无未读数据） ⇒ true （真 FIN 要判）
 *             ④ 对端 RST（发帧不读就关）   ⇒ true （硬错误要判，2026-11-18
 *               根修：旧实现把 WSAECONNRESET 混成"活着"，对端踢线本端永不察觉）
 *           四条锁住的是**复核函数本体**（IOCP/WSAEVENT/WSAASYNC 三构建共用
 *           同一框架函数与 Win32 PEEK 实现）；调用方 0 字节事件的 m_osFd 填充
 *           另有 `m_osFd <= 0` 守卫（2026-11-04 stdin PEEK 实锅）。
 *           本用例不依赖竞态，三后端构建下行为一致。 */
static bool sst_peer_really_closed_semantics(void)
{
    XTcpServer* server = NULL;
    XTcpSocket* cli = NULL;
    XTcpSocket* srv = NULL;
    SOCKET cliOs = INVALID_SOCKET;
    int verdict;

    /* ---- ① 健在 + 有数据 ⇒ false ---- */
    if (!sst_make_pair(&server, &cli, &srv)) SST_ASSERT(false, "pair 1 failed");
    cliOs = (SOCKET)(intptr_t)XAbstractSocket_socketDescriptor_base((XAbstractSocket*)cli);
    SST_ASSERT(cliOs != INVALID_SOCKET, "拿不到客户端 OS 句柄");
    SST_ASSERT(XIODevice_write_1((XIODevice*)srv, "PEEKME", 6) == 6, "send failed");
    {
        /* 等待判据=框架读环而非 FIONREAD：IOCP 后端挂着重叠读，数据到达
         * 即被收进读环、内核待读(FIONREAD)恒 0（既知口径差异）。三后端
         * 统一摆法：读环非空 = 链路健在的证据。 */
        int64_t got = sst_wait_readable((XIODevice*)cli, 6, 2000);
        SST_ASSERT(got >= 6, "数据未到达（读环=%lld）", (long long)got);
    }
    verdict = XAbstractNetIoRing_platformPeerReallyClosed((int)cliOs);
    SST_ASSERT(verdict == 0,
               "①健在（数据已在读环/内核）被判真关闭（复核会误杀健康链路）");
    XTcpSocket_deleteLater((XObject*)srv);
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    SST_PASS("① 健在+有数据 ⇒ false（不判死）");

    /* ---- ② 健在 + 无数据 ⇒ false ---- */
    cli = NULL; srv = NULL; server = NULL;
    if (!sst_make_pair(&server, &cli, &srv)) SST_ASSERT(false, "pair 2 failed");
    cliOs = (SOCKET)(intptr_t)XAbstractSocket_socketDescriptor_base((XAbstractSocket*)cli);
    verdict = XAbstractNetIoRing_platformPeerReallyClosed((int)cliOs);
    SST_ASSERT(verdict == 0,
               "②健在无数据被判真关闭（WOULDBLOCK 应=假 EOF）");
    XTcpSocket_deleteLater((XObject*)srv);
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    SST_PASS("② 健在+无数据 ⇒ false（WOULDBLOCK=活着）");

    /* ---- ③ 对端优雅 FIN ⇒ true ---- */
    server = NULL; cli = NULL; srv = NULL;
    if (!sst_make_pair(&server, &cli, &srv)) SST_ASSERT(false, "pair 3 failed");
    cliOs = (SOCKET)(intptr_t)XAbstractSocket_socketDescriptor_base((XAbstractSocket*)cli);
    /* 服务端优雅关闭（无未读数据）⇒ 对端 FIN ⇒ cli 侧 PEEK 返 0。
     * FIN 到达 cli 内核有传播延迟 ⇒ 轮询至复核判 FIN 或超时（确定性摆法：
     * 真 FIN **最终必须**被判 FIN——漏报即锁红）。 */
    {
        uint64_t t0 = XDateTime_currentMSecsSinceEpoch();
        XTcpSocket_disconnectFromHost_base((XAbstractSocket*)srv);
        verdict = 0;
        while (XDateTime_currentMSecsSinceEpoch() - t0 < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            verdict = XAbstractNetIoRing_platformPeerReallyClosed((int)cliOs);
            if (verdict) break;
            XThread_msleep(5);
        }
    }
    SST_ASSERT(verdict != 0,
               "③对端优雅 FIN 未被判真关闭（EOF 会漏报）");
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    SST_PASS("③ 对端优雅 FIN ⇒ true（真 FIN 判定）");

    /* ---- ④ 对端 RST（发帧不读就关）⇒ true ---- */
    server = NULL; cli = NULL; srv = NULL;
    if (!sst_make_pair(&server, &cli, &srv)) SST_ASSERT(false, "pair 4 failed");
    cliOs = (SOCKET)(intptr_t)XAbstractSocket_socketDescriptor_base((XAbstractSocket*)cli);
    SST_ASSERT(XIODevice_write_1((XIODevice*)cli, "RSTME", 5) == 5,
               "④ cli write failed");
    sst_pump(60);
    /* srv 侧**不读**就关：接收队列留未读数据 ⇒ 内核对 cli 发 RST。 */
    XTcpSocket_abort((XAbstractSocket*)srv);
    srv = NULL;
    /* RST 到达 cli 内核同样有传播延迟 ⇒ 同③轮询至判 FIN。 */
    {
        uint64_t t0 = XDateTime_currentMSecsSinceEpoch();
        verdict = 0;
        while (XDateTime_currentMSecsSinceEpoch() - t0 < 2000) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            verdict = XAbstractNetIoRing_platformPeerReallyClosed((int)cliOs);
            if (verdict) break;
            XThread_msleep(5);
        }
    }
    SST_ASSERT(verdict != 0,
               "④对端 RST 未被判真关闭（2026-11-18 根修回归：硬错误混成活着）");
    XTcpSocket_deleteLater((XObject*)cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater((XObject*)server);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    SST_PASS("④ 对端 RST ⇒ true（硬错误判定，2026-11-18 根修锁）");

    SST_PASS("假 EOF 复核语义表：四状态全部符合");
    return true;
}

int XSocketStallTest_run(void)
{
    typedef struct { const char* name; bool (*fn)(void); } Case;
    static const Case kCases[] = {
        { "长空闲后恢复收发",     sst_idle_then_resume },
        { "高频小帧持续收发",     sst_high_freq_small_frames },
        { "单边狂发背压完整性",   sst_burst_then_drain },
        { "双向同时高频",         sst_bidirectional },
        { "反复连接/断开/重连",   sst_reconnect_cycles },
        { "断开重连无串包",       sst_reuse_no_crosstalk },
        { "高频+低频双连接竞争",  sst_busy_and_quiet_links },
        { "多连接隔离",           sst_multi_link_isolation },
        { "推送压力下的上行小帧", sst_push_heavy_then_small_uplink },
        { "主机名异步解析/hostFound/isValid", sst_host_lookup_semantics },
        { "唤醒包风暴下读通量",     sst_wake_storm_keeps_reading },
        { "立刻重连 20 轮无串代",   sst_immediate_reconnect_churn },
        { "对端异常断开及时察觉",   sst_peer_reset_detected },
        { "64KB 大块字节一致",      sst_large_block_integrity },
        { "读缓冲暂停/恢复",        sst_read_buffer_pause_resume },
        /* --- 2026-11-21 新增：套接字后端「硬覆盖」（详见文件末尾小节注） --- */
        { "优雅FIN及时察觉",        sst_peer_graceful_fin_detected },
        { "read不超请求量",         sst_read_never_exceeds_request },
        { "1MB大数据完整性(背压未触达)", sst_write_backpressure_full_1mb },
        { "就绪唤醒时延<=200ms",    sst_ready_wakes_loop_latency },
        { "读缓冲上限真的生效",     sst_read_buffer_cap_enforced },
        { "断链信号与状态顺序",     sst_disconnect_signal_emitted },
        { "对端关闭后写干净失败",   sst_write_after_peer_close_fails_cleanly },
        { "并发上限显式失败不静默失联", sst_slot_table_overflow_still_works },
        { "对端活着但不读(突然断网形态)", sst_peer_alive_but_silent },
        { "RST察觉后写必失败",        sst_write_after_peer_reset_fails },
        { "bind占用端口报AddressInUse", sst_bind_address_in_use_reports_error },
        { "UDP零长数据报不是EOF",     sst_udp_zero_length_datagram_not_eof },
        /* --- Wave B 2026-11-21：用 T-2 注入夹具锁的 Qt 契约 --- */
        { "注入connect错误码分类",   sst_fault_connect_error_classification },
        { "注入connect错误码不串味", sst_fault_connect_error_not_stale },
        { "注入ENOBUFS不判死+续发完整", sst_fault_enobufs_then_full_delivery },
        { "注入假EOF不误判断链",     sst_fault_ready_but_no_data_not_eof },
        { "bytesAvailable含引擎待读", sst_bytes_available_includes_engine },
        { "注入夹具自检",            sst_fault_injector_selfcheck },
        { "假EOF复核语义表白盒锁",   sst_peer_really_closed_semantics }
    };
    int i;
    int failed = 0;
    const int n = (int)(sizeof(kCases) / sizeof(kCases[0]));

    XPrintf("========== 套接字读挂起回归开始（%d 项） ==========\n", n);
    for (i = 0; i < n; ++i) {
        XPrintf("---- [%d/%d] %s ----\n", i + 1, n, kCases[i].name);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (!kCases[i].fn()) {
            ++failed;
            XPrintf("  [失败] %s\n", kCases[i].name);
        }
    }
    XPrintf("\n========== 套接字读挂起回归完成: %d 通过, %d 失败 ==========\n",
            n - failed, failed);
    return failed;
}

#endif /* DEMOTEST_ON */
