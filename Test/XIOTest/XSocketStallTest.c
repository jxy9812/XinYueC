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
        XTcpSocket_deleteLater(s);
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

    XTcpSocket_deleteLater(cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
    XTcpSocket_deleteLater(srv);
    XTcpSocket_deleteLater(cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
    XTcpSocket_deleteLater(srv);
    XTcpSocket_deleteLater(cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
    XTcpSocket_deleteLater(srv);
    XTcpSocket_deleteLater(cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
    XTcpSocket_deleteLater(srv);
    XTcpSocket_deleteLater(cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
        XTcpSocket_deleteLater(srv);
        XTcpSocket_deleteLater(cli);
        sst_pump(60);
    }

    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
        XTcpSocket_deleteLater(srv);
        XTcpSocket_deleteLater(cli);
        sst_pump(60);
    }

    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
    XTcpSocket_deleteLater(quietSrv);
    XTcpSocket_deleteLater(busySrv);
    XTcpSocket_deleteLater(quietCli);
    XTcpSocket_deleteLater(busyCli);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
        XTcpSocket_deleteLater(srv[i]);
        XTcpSocket_deleteLater(cli[i]);
    }
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
    XTcpSocket_deleteLater(srv);
    XTcpSocket_deleteLater(cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
    XTcpSocket_deleteLater(srv);
    XTcpSocket_deleteLater(cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
        XTcpSocket_deleteLater(srv);
        XTcpSocket_deleteLater(cli);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }

    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
    XTcpSocket_deleteLater(srv);

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

    XTcpSocket_deleteLater(cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
    XTcpSocket_deleteLater(srv);
    XTcpSocket_deleteLater(cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
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
        SST_ASSERT(avail <= (int64_t)kTotal, "暂停期可用=%lld 异常",
                   (long long)avail);
    }

    /* 恢复期：应用持续消费，必须能收齐全部 8KB（恢复读通知生效）。 */
    SST_ASSERT(sst_read_exact((XIODevice*)cli, in, kTotal, 8000) == kTotal,
               "读缓冲暂停后未恢复（收不满 %d 字节）", kTotal);
    SST_ASSERT(memcmp(in, out, kTotal) == 0, "暂停恢复后内容不符");

    XIODevice_close_base((XIODevice*)srv);
    XAbstractSocket_disconnectFromHost_base((XAbstractSocket*)cli);
    XTcpSocket_deleteLater(srv);
    XTcpSocket_deleteLater(cli);
    XTcpServer_close(server);
    XTcpServer_deleteLater(server);
    sst_pump(50);
    SST_PASS("读缓冲暂停/恢复（8KB 全额到达）");
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
        { "读缓冲暂停/恢复",        sst_read_buffer_pause_resume }
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
