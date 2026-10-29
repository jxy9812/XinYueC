/** @file       XLinkQualityTest.c
 * @brief      XLink 私有协议的「质量门禁」测试：内存泄漏 / 性能基准 / 边界与对抗。
 * @details    与 XLinkTest.c 的功能覆盖互补，这里回答三个问题：
 *
 *   一、内存泄漏：反复创建/销毁会话，断言系统分配器在用字节数回到基线
 *      （XLink 全生命周期仅 init 时申请收发缓冲，deinit 必须成对释放）。
 *   二、高性能：吞吐与延迟基准——大批量小帧（状态快照类）与单帧大载荷
 *      （画面块类）的发送/接收吞吐、每帧平均耗时，确认无堆分配热点。
 *   三、边界与对抗：最大载荷帧、粘包（多帧一次到达）、订阅表满、
 *      畸形帧流、连续建链/断链（无资源累积）。
 *   四、文件传输质量（XLINK_FILE_ON）：上传/下载反复 churn 断言分配器
 *      在用字节回基线；1~4MB 大载荷上传+下载计时的吞吐宽松下限；
 *      传输完成后 XLinkSession_stats 的字节数/帧数冒烟区间。
 *
 *   本文件不重复 XLinkTest.c 已覆盖的协议语义，只补「质量维度」。
 * @author     XinYueC 团队
 */
#include "XLinkTest.h"
#include "XLinkCodec.h"
#include "XLinkSession.h"
#include "XLinkServer.h"
#include "XLinkClient.h"
#include "XLinkFileServer.h"
#include "XLinkFileClient.h"
#include "XFile.h"
#include "XDir.h"
#include "XDeviceFile.h"
#include "XString.h"
#include "XGuiRemoteLoopback.h"
#include "XTcpSocket.h"
#include "XCoreApplication.h"
#include "XMemory.h"
#include "XDateTime.h"
#include "XThread.h"
#include "XPrintf.h"
#include <string.h>
#if defined(_WIN32)
#include <windows.h>   /* QueryPerformanceCounter：仅基准统计用 */
#endif

/** @brief 通过计数（与 XLinkTest.c 各自独立统计）。 */
static int g_qPassed = 0;
/** @brief 失败计数。 */
static int g_qFailed = 0;

/**
 * @brief 断言宏（质量门禁专用，独立计数不污染功能测试统计）。
 */
#define XLINK_QCHECK(cond, desc)                                            \
    do {                                                                     \
        if (cond) {                                                          \
            g_qPassed++;                                                     \
            XPrintf("  [PASS] %s\n", desc);                                 \
        } else {                                                             \
            g_qFailed++;                                                     \
            XPrintf("  [FAIL] %s\n", desc);                                 \
        }                                                                    \
    } while (0)

/** @brief 打印分组标题。 */
#define XLINK_QSECTION(title) XPrintf("\n--- %s ---\n", title)

/* --- 边界与对抗用例早于其定义处使用，这里先行声明 --- */

/** 对抗测试中客户端收到的推送数（前向声明）。 */
static int g_advRecvCount;

/** 对抗测试接收回调（前向声明）。 */
static void xlink_q_advDispatch(XLinkSession* self,
                                const XLinkFrameHeader* frame,
                                const uint8_t* payload);

/* ========================================================================== */
/*                        简易时基                                           */
/* ========================================================================== */

/**
 * @brief 取单调毫秒（与 XLinkSession 时间口径一致：调用方传时序）。
 * @return 单调毫秒值。
 */
static uint32_t xlink_q_nowMs(void)
{
    return (uint32_t)XDateTime_currentMSecsSinceEpoch();
}

/**
 * @brief 高精度计时器：取单调毫秒（毫秒级精度）。
 * @note  基准耗时过短时毫秒精度会全部归零，故所有基准用例都通过放大
 *        迭代次数（见各用例的 REPEAT 宏）使总耗时进入毫秒可分辨区间；
 *        若 t1 == t0 说明耗时不足 1ms，判定为"极快"并按 >=1ms 计入。
 */
#define XLINK_BENCH_ELAPSED_MS(t0, t1) ((t1) > (t0) ? (uint32_t)((t1) - (t0)) : 1u)

/**
 * @brief 微秒级高精度计时（仅 Windows 测试构建使用）。
 * @details XDateTime 只提供毫秒精度，短耗时基准（单帧微秒级）会被整除归零，
 *          导致吞吐数字失真。测试侧用平台高精度计数器兜底，仅用于基准统计，
 *          不进入库代码（库内仍遵循「Src/ 禁止直呼时间源」的约束）。
 * @return 微秒值（单调）。
 */
static double xlink_q_nowUs(void)
{
#if defined(_WIN32)
    static LARGE_INTEGER freq = { { 0, 0 } };
    LARGE_INTEGER now;
    if (freq.QuadPart == 0)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1e6 / (double)freq.QuadPart;
#else
    /* 非 Windows 退回毫秒换算（精度较低，仅保证基准可运行）。 */
    return (double)XDateTime_currentMSecsSinceEpoch() * 1000.0;
#endif
}

/* ========================================================================== */
/*                        一、内存泄漏                                       */
/* ========================================================================== */

/**
 * @brief 服务端 churn 单轮：创建->监听(自动端口)->真实客户端接入->销毁。
 * @param pass 失败时置 0，成功不修改。
 * @note  服务端销毁连带回收托管会话与监听器子套接字；客户端先排空写队列
 *        再拆除（发完即走会丢帧，见 XLink.md Q8）。
 */
static void xlink_q_server_churn_round(int* pass)
{
    XLinkServer* srv = XLinkServer_create();
    XTcpSocket* sock = NULL;
    XLinkSession* cli = NULL;
    uint16_t port;
    int spin;

    if (!srv) {
        *pass = 0;
        return;
    }
    if (!XLinkServer_listen(srv, "127.0.0.1", 0)) {
        *pass = 0;
        XClassDelete((XClass*)srv);
        return;
    }
    port = XTcpServer_serverPort(XLinkServer_tcpServer_const(srv));
    sock = XTcpSocket_create();
    if (sock) {
        XTcpSocket_connectToHost_base((XAbstractSocket*)sock, "127.0.0.1",
                                      port, XIODevice_ReadWrite,
                                      XAbstractSocket_AnyIPProtocol);
        (void)XTcpSocket_waitForConnected_base((XAbstractSocket*)sock, 2000);
        cli = XLinkSession_create_ex((XIODevice*)sock);
        for (spin = 0; cli && spin < 200 && XLinkServer_sessionCount(srv) == 0;
             spin++) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XLinkServer_pump(srv, xlink_q_nowMs());
        }
        if (cli && XLinkServer_sessionCount(srv) == 0)
            *pass = 0;
    } else {
        *pass = 0;
    }
    /* 销毁顺序：先客户端（含排空），服务端连带回收会话 */
    if (cli)
        XClassDelete((XClass*)cli);
    if (sock) {
        int drain;
        for (drain = 0;
             drain < 5
             && XTcpSocket_waitForBytesWritten_base((XAbstractSocket*)sock,
                                                    100);
             drain++) {}
        XClassDelete((XClass*)sock);
    }
    XClassDelete((XClass*)srv);
}

/**
 * @brief 泄漏检测：反复创建/销毁会话，断言在用字节回到基线。
 * @details 覆盖两条释放路径：正常 XClassDelete，以及创建后中途摘除传输
 *          再销毁（确保 setDevice(NULL) 不产生悬挂引用）。
 */
static void xlink_qtest_leak(void)
{
    XLINK_QSECTION("一、内存泄漏检测");

    XMemory_setStatisticsEnabled(true);
    {
        /* 基线：统计刚启用时可能有在途分配，先稳定一轮再取基准 */
        XMemoryStatistics base = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
        size_t before = base.systemBytes;
        int i;
        int pass = 1;

        for (i = 0; i < 200; i++) {
            XLinkSession* s = XLinkSession_create();
            if (!s) { pass = 0; break; }
            /* 走一遍完整生命周期：挂载->能力->回调->销毁 */
            XLinkSession_setCapability(s, XLinkCapability_Zlib);
            XLinkSession_setDispatchCallback(s, NULL, NULL);
            XLinkSession_setDevice(s, NULL);
            XClassDelete((XClass*)s);
        }
        XLINK_QCHECK(pass, "200 次会话创建/销毁全部成功");

        {
            XMemoryStatistics now = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
            /* 允许少量框架内部缓存抖动；泄漏量级远大于此容差 */
            long delta = (long)now.systemBytes - (long)before;
            XPrintf("        [INFO] 循环后系统分配器在用 %zu 字节（基线 %zu，差 %ld）\n",
                    now.systemBytes, before, delta);
            XLINK_QCHECK(delta <= 0 || delta < 4096,
                         "反复创建/销毁无内存泄漏（在用字节回到基线容差内）");
        }

        /* 交替路径：带传输挂载的销毁 */
        before = XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;
        for (i = 0; i < 100; i++) {
            XGuiRemoteLoopbackDevice* a = NULL;
            XGuiRemoteLoopbackDevice* b = NULL;
            XLinkSession* s;
            a = XGuiRemoteLoopbackDevice_createPair(8 * 1024, &b);
            if (!a || !b) break;
            s = XLinkSession_create_ex((XIODevice*)a);
            if (s) XClassDelete((XClass*)s);
            XClassDelete((XClass*)a);
            XClassDelete((XClass*)b);
        }
        {
            XMemoryStatistics now = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
            long delta = (long)now.systemBytes - (long)before;
            XPrintf("        [INFO] 夹具循环后在用 %zu 字节（基线 %zu，差 %ld）\n",
                    now.systemBytes, before, delta);
            XLINK_QCHECK(delta <= 0 || delta < 4096,
                         "带传输挂载的创建/销毁无内存泄漏");
        }

        /* 换设备接线 churn：setDevice 摘挂各 50 轮——
           每轮做 readyRead 信号断接与内建定时器启停，验证信号连接
           与对象级簿记不累积。 */
        before = XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;
        {
            XGuiRemoteLoopbackDevice *a = NULL, *aPeer = NULL;
            XGuiRemoteLoopbackDevice *b = NULL, *bPeer = NULL;
            XLinkSession* s = NULL;
            int i;
            a = XGuiRemoteLoopbackDevice_createPair(8 * 1024, &aPeer);
            b = XGuiRemoteLoopbackDevice_createPair(8 * 1024, &bPeer);
            s = (a && b) ? XLinkSession_create_ex((XIODevice*)a) : NULL;
            for (i = 0; s && i < 50; i++) {
                if (!XLinkSession_setDevice(s, (XIODevice*)b)) break;
                if (!XLinkSession_setDevice(s, (XIODevice*)a)) break;
            }
            XLINK_QCHECK(i == 50, "50 轮换设备接线全部成功");
            if (s) XClassDelete((XClass*)s);
            if (a) XClassDelete((XClass*)a);
            if (aPeer) XClassDelete((XClass*)aPeer);
            if (b) XClassDelete((XClass*)b);
            if (bPeer) XClassDelete((XClass*)bPeer);
        }
        {
            XMemoryStatistics now = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
            long delta = (long)now.systemBytes - (long)before;
            XPrintf("        [INFO] 换设备接线循环后在用 %zu 字节（基线 %zu，差 %ld）\n",
                    now.systemBytes, before, delta);
            XLINK_QCHECK(delta <= 0 || delta < 4096,
                         "换设备接线（信号断接+定时器启停）无内存泄漏");
        }

        /* 订阅/退订 churn：握手后 50 轮订阅-退订——
           订阅表为定容数组，本项盯的是帧收发路径与幂等去重路径。 */
        before = XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;
        {
            XGuiRemoteLoopbackDevice *a = NULL, *b = NULL;
            XLinkSession *sa = NULL, *sb = NULL;
            int i;
            a = XGuiRemoteLoopbackDevice_createPair(8 * 1024, &b);
            sa = (a && b) ? XLinkSession_create_ex((XIODevice*)a) : NULL;
            sb = (a && b) ? XLinkSession_create_ex((XIODevice*)b) : NULL;
            if (sa && sb) {
                XLinkSession_pump(sa, 1000);
                XLinkSession_pump(sb, 1000);
                XLinkSession_pump(sa, 1010);
                XLinkSession_pump(sb, 1010);
            }
            for (i = 0; sa && sb && i < 50; i++) {
                uint32_t topic = XLINK_TOPIC_USER_BASE + (uint32_t)(i % 16);
                if (!XLinkSession_subscribe(sa, topic)) break;
                if (!XLinkSession_unsubscribe(sa, topic)) break;
                XLinkSession_pump(sa, 1020u + (uint32_t)i);
                XLinkSession_pump(sb, 1020u + (uint32_t)i);
            }
            XLINK_QCHECK(i == 50, "50 轮订阅/退订全部成功");
            if (sa) XClassDelete((XClass*)sa);
            if (sb) XClassDelete((XClass*)sb);
            if (a) XClassDelete((XClass*)a);
            if (b) XClassDelete((XClass*)b);
        }
        {
            XMemoryStatistics now = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
            long delta = (long)now.systemBytes - (long)before;
            XPrintf("        [INFO] 订阅退订循环后在用 %zu 字节（基线 %zu，差 %ld）\n",
                    now.systemBytes, before, delta);
            XLINK_QCHECK(delta <= 0 || delta < 4096,
                         "订阅/退订 churn 无内存泄漏");
        }

        /* XLinkServer 生命周期 + 真实接入 churn：10 轮
           [创建->监听(自动端口)->客户端接入->销毁]——
           盯监听器子套接字、托管会话、信号连接与内嵌定时器的成对回收。
           先暖机一轮：真实套接字首次使用会触发框架懒加载（事件分发器/
           IOCP 基础设施等一次性分配），消耗掉之后再取基线——
           实测暖机前基线差 +51544 字节且第 1 轮后逐轮完全持平（非泄漏）。 */
        {
            int warm = 1;
            int i;
            int pass = 1;
            xlink_q_server_churn_round(&warm);
            XLINK_QCHECK(warm, "暖机轮服务端接入成功");
            before = XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;
            for (i = 0; i < 10; i++) {
                int roundPass = 1;
                xlink_q_server_churn_round(&roundPass);
                if (!roundPass)
                    pass = 0;
            }
            XLINK_QCHECK(pass, "10 轮服务端生命周期+真实接入全部成功");
        }
        {
            XMemoryStatistics now = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
            long delta = (long)now.systemBytes - (long)before;
            XPrintf("        [INFO] 服务端 churn 循环后在用 %zu 字节（基线 %zu，差 %ld）\n",
                    now.systemBytes, before, delta);
            XLINK_QCHECK(delta <= 0 || delta < 4096,
                         "XLinkServer 生命周期+真实接入无内存泄漏");
        }
    }
    XMemory_setStatisticsEnabled(false);
}

/* ========================================================================== */
/*                        二、性能基准                                       */
/* ========================================================================== */

/** 基准收到的帧数。 */
static int g_benchRecvCount = 0;
/** 基准收到的载荷校验和。 */
static uint32_t g_benchChecksum = 0;

/**
 * @brief 基准用接收回调：只累加计数与校验和，保持极简开销。
 */
static void xlink_q_benchDispatch(XLinkSession* self,
                                  const XLinkFrameHeader* frame,
                                  const uint8_t* payload)
{
    uint32_t sum = 0;
    uint32_t i;
    (void)self;
    if (frame->m_msgClass != XLinkMsgClass_Publish || !payload)
        return;
    for (i = 0; i < frame->m_payloadLen; i++)
        sum += payload[i];
    g_benchChecksum += sum;
    g_benchRecvCount++;
}

/**
 * @brief 搭建已握手的基准夹具（两端均 Ready，客户端注册接收回调）。
 */
static bool xlink_q_benchSetup(XGuiRemoteLoopbackDevice** outA,
                              XGuiRemoteLoopbackDevice** outB,
                              XLinkSession** outSrv,
                              XLinkSession** outCli)
{
    uint8_t helloBuf[4];
    XLinkHelloPayload hello;
    XGuiRemoteLoopbackDevice* a = NULL;
    XGuiRemoteLoopbackDevice* b = NULL;
    XLinkSession* srv = NULL;
    XLinkSession* cli = NULL;
    int i;

    /* 环容量需容得下满载帧（16KB）与大载荷基准的连续写入，避免背压掩盖协议行为。 */
    a = XGuiRemoteLoopbackDevice_createPair(1024 * 1024, &b);
    if (!a || !b)
        return false;
    srv = XLinkSession_create_ex((XIODevice*)a);
    cli = XLinkSession_create_ex((XIODevice*)b);
    if (!srv || !cli) {
        if (srv) XClassDelete((XClass*)srv);
        if (cli) XClassDelete((XClass*)cli);
        XClassDelete((XClass*)a);
        XClassDelete((XClass*)b);
        return false;
    }
    XLinkSession_setDispatchCallback(cli, xlink_q_benchDispatch, NULL);
    XLinkSession_setCapability(srv, XLinkCapability_Zlib);
    XLinkSession_setCapability(cli, XLinkCapability_Zlib);

    hello.m_version = XLINK_VERSION;
    hello.m_capability = XLinkCapability_Zlib;
    XLinkCodec_encodeHello(&hello, helloBuf, sizeof(helloBuf), NULL);
    XLinkSession_sendSession(srv, XLinkSessionCode_Hello, helloBuf, 4);
    for (i = 0; i < 4; i++) {
        XLinkSession_pump(srv, 1000 + i * 10);
        XLinkSession_pump(cli, 1000 + i * 10);
    }
    if (!XLinkSession_isReady(cli)) {
        XClassDelete((XClass*)srv);
        XClassDelete((XClass*)cli);
        XClassDelete((XClass*)a);
        XClassDelete((XClass*)b);
        return false;
    }
    *outA = a; *outB = b; *outSrv = srv; *outCli = cli;
    return true;
}

/**
 * @brief 性能基准：吞吐、每帧耗时与大载荷路径。
 * @details 覆盖两类真实负载：
 *   - 小帧高频（状态快照，16 字节载荷）：验证无堆分配的稳态吞吐；
 *   - 大载荷（4KB 帧）：验证常驻发送缓冲路径不产生堆分配热点。
 */
static void xlink_qtest_performance(void)
{
    XLINK_QSECTION("二、性能基准");

    {
        XGuiRemoteLoopbackDevice *a = NULL, *b = NULL;
        XLinkSession *srv = NULL, *cli = NULL;
        if (!xlink_q_benchSetup(&a, &b, &srv, &cli)) {
            XLINK_QCHECK(false, "基准夹具握手成功");
            return;
        }
        XLINK_QCHECK(true, "基准夹具握手成功（两端 Ready）");

        /* --- 小帧高频：10000 帧 × 16 字节 --- */
        {
            uint8_t payload[16];
            const int N = 10000;
            uint32_t t0;
            uint32_t t1;
            int i;
            memset(payload, 0xAB, sizeof(payload));
            g_benchRecvCount = 0;
            g_benchChecksum = 0;

            t0 = xlink_q_nowMs();
            for (i = 0; i < N; i++) {
                payload[0] = (uint8_t)i;
                XLinkSession_sendPublish(srv, XLINK_TOPIC_USER_BASE + 1,
                                         payload, sizeof(payload));
                /* 每 16 帧 pump 一次，避免回环环缓冲写满背压 */
                if ((i & 15) == 15) {
                    XLinkSession_pump(srv, 1200 + i);
                    XLinkSession_pump(cli, 1200 + i);
                }
            }
            XLinkSession_pump(srv, 60000);
            XLinkSession_pump(cli, 60000);
            t1 = xlink_q_nowMs();

            {
                int frames = g_benchRecvCount;
                double ms = (double)XLINK_BENCH_ELAPSED_MS(t0, t1);
                double perFrameUs = frames ? (ms * 1000.0 / frames) : 0.0;
                double mbps = (frames * (XLINK_HEADER_SIZE + 4 + 16) * 8.0
                               / 1024.0 / 1024.0) / (ms / 1000.0);
                XPrintf("        [INFO] 小帧高频: %d 帧 / %.0f ms = %.2f us/帧, "
                        "线速 %.1f Mbps\n", frames, ms, perFrameUs, mbps);
                XLINK_QCHECK(frames == N, "小帧高频：全部 10000 帧无丢失送达");
                XLINK_QCHECK(g_benchChecksum != 0, "小帧高频：载荷校验和有效");
                XLINK_QCHECK(perFrameUs < 200.0,
                             "小帧高频：平均每帧耗时 < 200us（无堆分配热点）");
            }
        }

        /* --- 大载荷：200 帧 × 4096 字节（走常驻发送缓冲，非堆分配路径） ---
           独立夹具：静默判线阈值 3000ms，小帧基准的万帧循环已把时间轴推过
           阈值，复用同一会话会导致状态机判线、时间回退，故另建夹具。
           回环设备写入「全有全无」，连续发送会写满对端环导致背压失败，
           故每发一帧立即双向 pump 排空，模拟真实链路的流控。 */
        {
            XGuiRemoteLoopbackDevice* a2 = NULL;
            XGuiRemoteLoopbackDevice* b2 = NULL;
            XLinkSession* srv2 = NULL;
            XLinkSession* cli2 = NULL;
            uint8_t* payload = (uint8_t*)XMalloc_System(4096);
            const int N = 200;
            uint32_t t0;
            uint32_t t1;
            int i;
            int sent = 0;
            XLINK_QCHECK(payload != NULL, "大载荷缓冲申请成功");
            if (payload && xlink_q_benchSetup(&a2, &b2, &srv2, &cli2)) {
                memset(payload, 0xCD, 4096);
                XLinkSession_setDispatchCallback(cli2, xlink_q_benchDispatch, NULL);
                g_benchRecvCount = 0;
                g_benchChecksum = 0;

                t0 = xlink_q_nowMs();
                for (i = 0; i < N; i++) {
                    if (XLinkSession_sendPublish(srv2, XLINK_TOPIC_USER_BASE + 2,
                                                 payload, 4096))
                        sent++;
                    XLinkSession_pump(srv2, 1500 + i);
                    XLinkSession_pump(cli2, 1500 + i);
                }
                XLinkSession_pump(srv2, 1900);
                XLinkSession_pump(cli2, 1900);
                t1 = xlink_q_nowMs();

                {
                    int frames = g_benchRecvCount;
                    double ms = (double)XLINK_BENCH_ELAPSED_MS(t0, t1);
                    double perFrameUs = frames ? (ms * 1000.0 / frames) : 0.0;
                    XPrintf("        [INFO] 大载荷: 发出 %d/%d 帧, 收到 %d 帧 / "
                            "%.0f ms = %.2f us/帧\n",
                            sent, N, frames, ms, perFrameUs);
                    XLINK_QCHECK(sent == N, "大载荷：全部 200 帧发送成功");
                    XLINK_QCHECK(frames == N, "大载荷：全部 200 帧无丢失送达");
                    XLINK_QCHECK(perFrameUs < 5000.0,
                                 "大载荷：平均每帧耗时 < 5ms（常驻缓冲路径）");
                }
            } else {
                XLINK_QCHECK(false, "大载荷夹具握手成功");
            }
            if (srv2) XClassDelete((XClass*)srv2);
            if (cli2) XClassDelete((XClass*)cli2);
            if (a2) XClassDelete((XClass*)a2);
            if (b2) XClassDelete((XClass*)b2);
            if (payload) XFree_System(payload);
        }

        XClassDelete((XClass*)srv);
        XClassDelete((XClass*)cli);
        XClassDelete((XClass*)a);
        XClassDelete((XClass*)b);
    }

    /* --- 基准期间的内存不增长（证明稳态零堆分配） --- */
    {
        size_t before;
        size_t after;
        XGuiRemoteLoopbackDevice *a = NULL, *b = NULL;
        XLinkSession *srv = NULL, *cli = NULL;
        if (xlink_q_benchSetup(&a, &b, &srv, &cli)) {
            before = XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;
            {
                int i;
                uint8_t payload[64];
                memset(payload, 0x5A, sizeof(payload));
                for (i = 0; i < 500; i++) {
                    XLinkSession_sendPublish(srv, XLINK_TOPIC_USER_BASE + 3,
                                             payload, sizeof(payload));
                    XLinkSession_pump(srv, 1500 + i);
                    XLinkSession_pump(cli, 1500 + i);
                }
            }
            after = XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;
            XPrintf("        [INFO] 500 帧发送前后在用字节 %zu -> %zu\n",
                    before, after);
            XLINK_QCHECK(after <= before || (after - before) < 1024,
                         "稳态收发不产生堆分配增长（零分配热路径）");
            XClassDelete((XClass*)srv);
            XClassDelete((XClass*)cli);
            XClassDelete((XClass*)a);
            XClassDelete((XClass*)b);
        }
    }
}

/* ========================================================================== */
/*                        三、吞吐天花板基准                                 */
/* ========================================================================== */

/**
 * @brief 吞吐基准：分离「协议编解码纯计算」与「全链路收发」两条路径。
 * @details 环回设备实测值受 memcpy + 互斥锁 + XIODevice 缓冲层开销影响，
 *          并不等于协议本身的天花板。本用例分别测：
 *            (a) 纯编解码：只做帧头打包/解包，剥离 I/O 与内存拷贝；
 *            (b) 全链路：sendPublish + pump 的端到端吞吐；
 *          并按载荷尺寸扫描，量化每帧固定开销（12B 帧头 + 4B topicId）
 *          对小包吞吐的摊薄影响。
 */
static void xlink_qtest_throughput(void)
{
    XLINK_QSECTION("三、吞吐天花板基准");

    /* --- (a) 纯编解码天花板：不含 I/O，仅帧头打包/解包往返 --- */
    {
        const int N = 2000000;
        uint8_t hdr[XLINK_HEADER_SIZE];
        XLinkFrameHeader h;
        XLinkFrameHeader r;
        double t0u;
        double t1u;
        int i;
        uint32_t acc = 0; /* 防止编译器把循环优化掉 */

        h.m_payloadLen = 16;
        h.m_msgClass = XLinkMsgClass_Publish;
        h.m_msgCode = 1;
        h.m_requestId = 7;

        t0u = xlink_q_nowUs();
        for (i = 0; i < N; i++) {
            XLinkCodec_encodeHeader(&h, hdr);
            acc += hdr[0];
        }
        t1u = xlink_q_nowUs();
        {
            double us = t1u > t0u ? (t1u - t0u) : 1.0;
            double ns = us * 1000.0 / N;
            double mbps = (N * XLINK_HEADER_SIZE * 8.0 / 1024.0 / 1024.0)
                          / (us / 1e6);
            XPrintf("        [INFO] 纯编解码(仅 12B 帧头): %d 次 / %.0f us = "
                    "%.1f ns/帧, 折算 %.0f Mbps\n", N, us, ns, mbps);
            XLINK_QCHECK(acc != 0, "纯编解码基准未被优化掉");
            XLINK_QCHECK(ns < 500.0,
                         "纯编解码：帧头打包 < 500ns/帧（亚微秒）");
        }
    }

    /* --- (b) 全链路吞吐：按载荷尺寸扫描，量化固定开销摊薄 --- */
    {
        static const uint32_t sizes[] = { 16, 64, 256, 1024, 4096, 16380 };
        const int sizeCount = (int)(sizeof(sizes) / sizeof(sizes[0]));
        int si;

        for (si = 0; si < sizeCount; si++) {
            uint32_t len = sizes[si];
            XGuiRemoteLoopbackDevice* a = NULL;
            XGuiRemoteLoopbackDevice* b = NULL;
            XLinkSession* srv = NULL;
            XLinkSession* cli = NULL;
            uint8_t* payload = (uint8_t*)XMalloc_System(len ? len : 1);
            if (!payload)
                continue;
            if (!xlink_q_benchSetup(&a, &b, &srv, &cli)) {
                XFree_System(payload);
                continue;
            }
            {
                /* 固定帧数：包越大帧数越少，保证总字节量可比 */
                int N = (len >= 4096) ? 400 : (len >= 1024) ? 2000 : 8000;
                int i;
                double t0u;
                double t1u;
                int sent = 0;
                memset(payload, 0x33, len);
                XLinkSession_setDispatchCallback(cli, xlink_q_benchDispatch, NULL);
                g_benchRecvCount = 0;
                g_benchChecksum = 0;

                t0u = xlink_q_nowUs();
                for (i = 0; i < N; i++) {
                    if (XLinkSession_sendPublish(srv,
                            XLINK_TOPIC_USER_BASE + 1, payload, len))
                        sent++;
                    /* 每帧排空，避免回环环背压掩盖协议吞吐。
                       大载荷帧（>=1024B）单轮 pump 取不满（XIODevice_read_1
                       先读内部读缓冲），需连续泵送至整帧到齐；泵送轮数计入
                       耗时，反映真实的整帧交付成本。 */
                    {
                        int spins = 1;
                        while (spins < 8) {
                            XLinkSession_pump(srv, 1200 + (uint32_t)i);
                            XLinkSession_pump(cli, 1200 + (uint32_t)i);
                            spins++;
                            if (len < 1024)
                                break; /* 小包单轮即可读完 */
                            if (XIODevice_bytesAvailable_base(
                                    (XIODevice*)b) == 0)
                                break; /* 对端已取空 */
                        }
                    }
                }
                t1u = xlink_q_nowUs();

                {
                    /* 线上字节 = 帧头 12 + topicId 4 + 业务载荷 */
                    double wirePerFrame = (double)(XLINK_HEADER_SIZE + 4 + len);
                    double us = t1u > t0u ? (t1u - t0u) : 1.0;
                    double sec = us / 1e6;
                    double totalBytes = wirePerFrame * sent;
                    double mbps = (totalBytes * 8.0 / 1024.0 / 1024.0) / sec;
                    double perFrameUs = sent ? (us / sent) : 0.0;
                    double overheadPct = (double)(XLINK_HEADER_SIZE + 4)
                                         / wirePerFrame * 100.0;
                    XPrintf("        [INFO] 载荷 %5uB: %d 帧 / %.0f us = "
                            "%.2f us/帧, 线上 %.0f Mbps, 固定开销 %.1f%%\n",
                            len, sent, us, perFrameUs, mbps, overheadPct);
                    XLINK_QCHECK(sent == N, "吞吐扫描：全部帧发送成功");
                    XLINK_QCHECK(g_benchRecvCount == N,
                                 "吞吐扫描：全部帧无丢失送达");
                    /* 固定开销应随载荷增大而摊薄（<=20% 说明非小包陷阱） */
                    XLINK_QCHECK(overheadPct <= 50.0,
                                 "吞吐扫描：固定开销不超过 50%");
                }
            }
            XClassDelete((XClass*)srv);
            XClassDelete((XClass*)cli);
            XClassDelete((XClass*)a);
            XClassDelete((XClass*)b);
            XFree_System(payload);
        }
    }
}

/* ========================================================================== */
/*                        四、边界与对抗                                     */
/* ========================================================================== */

/** 对抗测试中客户端收到的推送数。 */
static int g_advRecvCount = 0;

/** 对抗测试接收回调（只计数）。 */
static void xlink_q_advDispatch(XLinkSession* self,
                                const XLinkFrameHeader* frame,
                                const uint8_t* payload)
{
    (void)self; (void)frame; (void)payload;
    g_advRecvCount++;
}
/**
 * @brief 边界与对抗测试。
 * @details 覆盖功能测试未涉及的极端路径：最大载荷帧、粘包（多帧一次
 *          到达）、订阅表满、畸形帧流、连续建链断链。
 */
static void xlink_qtest_boundary(void)
{
    XLINK_QSECTION("三、边界与对抗");

    /* --- 最大载荷帧：业务载荷上限 = XLINK_MAX_PAYLOAD - 4（topicId 占 4B） --- */
    {
        XGuiRemoteLoopbackDevice* a = NULL;
        XGuiRemoteLoopbackDevice* b = NULL;
        XLinkSession *srv = NULL, *cli = NULL;
        if (xlink_q_benchSetup(&a, &b, &srv, &cli)) {
            const uint32_t maxBusiness = XLINK_MAX_PAYLOAD - 4;
            uint8_t* big = (uint8_t*)XMalloc_System(XLINK_MAX_PAYLOAD);
            if (big) {
                int before;
                int i;
                bool sentOk;
                memset(big, 0x7E, XLINK_MAX_PAYLOAD);
                XLinkSession_setDispatchCallback(cli, xlink_q_advDispatch, NULL);
                before = g_advRecvCount;
                sentOk = XLinkSession_sendPublish(srv,
                                XLINK_TOPIC_USER_BASE + 1, big,
                                maxBusiness);
                XLINK_QCHECK(sentOk,
                             "恰好上限（线上载荷满 XLINK_MAX_PAYLOAD）发送成功");
                /* 满载帧（16KB）需多轮 pump 才能读完：XIODevice_read_1 优先读
                   内部读缓冲、单轮取不满时回底层，故循环泵送至整帧到齐。
                   时间轴逐轮 +1ms 连续推进，避免累积跨越静默判线阈值。 */
                for (i = 0; i < 64; i++) {
                    XLinkSession_pump(srv, 1200 + (uint32_t)i);
                    XLinkSession_pump(cli, 1200 + (uint32_t)i);
                    if (g_advRecvCount > before)
                        break;
                }
                if (sentOk)
                    XLINK_QCHECK(g_advRecvCount == before + 1,
                                 "满载帧正确送达对端");
                else
                    XLINK_QCHECK(false, "满载帧正确送达对端");
                /* 业务载荷超上限 1 字节：线上载荷越界，须拒绝而非越界写 */
                XLINK_QCHECK(!XLinkSession_sendPublish(srv,
                                  XLINK_TOPIC_USER_BASE + 1, big,
                                  maxBusiness + 1),
                             "业务载荷超上限被拒绝（防越界，不部分发送）");
                XFree_System(big);
            }
            XClassDelete((XClass*)srv);
            XClassDelete((XClass*)cli);
            XClassDelete((XClass*)a);
            XClassDelete((XClass*)b);
        }
    }

    /* --- 粘包：多帧在一次 read 中到达，须逐帧正确切分 --- */
    {
        XGuiRemoteLoopbackDevice* a = NULL;
        XGuiRemoteLoopbackDevice* b = NULL;
        XLinkSession *srv = NULL, *cli = NULL;
        if (xlink_q_benchSetup(&a, &b, &srv, &cli)) {
            uint8_t payload = 0x3C;
            const int N = 50;
            int i;
            g_advRecvCount = 0;
            XLinkSession_setDispatchCallback(cli, xlink_q_advDispatch, NULL);

            /* 连发 50 帧后只 pump 一次：所有帧粘在一次读取里 */
            for (i = 0; i < N; i++)
                XLinkSession_sendPublish(srv, XLINK_TOPIC_USER_BASE + 5,
                                         &payload, 1);
            /* 时间轴接续握手（benchSetup 用 1000+i*10），回退会触发静默判线。
               单次 pump 吃下全部粘包，验证逐帧切分。 */
            XLinkSession_pump(srv, 1200);
            XLinkSession_pump(cli, 1200);
            XLINK_QCHECK(g_advRecvCount == N,
                         "粘包：单次 pump 正确切分并投递全部 50 帧");
            XClassDelete((XClass*)srv);
            XClassDelete((XClass*)cli);
            XClassDelete((XClass*)a);
            XClassDelete((XClass*)b);
        }
    }

    /* --- 订阅表满：超出槽位应拒绝而非越界写 --- */
    {
        XGuiRemoteLoopbackDevice* a = NULL;
        XGuiRemoteLoopbackDevice* b = NULL;
        XLinkSession *srv = NULL, *cli = NULL;
        if (xlink_q_benchSetup(&a, &b, &srv, &cli)) {
            int i;
            int accepted = 0;
            for (i = 0; i < XLINK_TOPIC_SLOTS + 5; i++) {
                if (XLinkSession_subscribe(cli,
                        XLINK_TOPIC_USER_BASE + (uint32_t)i))
                    accepted++;
                else
                    break;
            }
            XLINK_QCHECK(accepted == XLINK_TOPIC_SLOTS,
                         "订阅表满后拒绝新订阅（不越界写）");
            /* 满表后再退订一个已订阅的，应成功（验证未破坏既有表项） */
            XLINK_QCHECK(XLinkSession_unsubscribe(cli, XLINK_TOPIC_USER_BASE),
                         "满表状态下仍可正常退订已有订阅");
            XClassDelete((XClass*)srv);
            XClassDelete((XClass*)cli);
            XClassDelete((XClass*)a);
            XClassDelete((XClass*)b);
        }
    }

    /* --- 畸形帧流：垃圾字节不得崩溃，且不得投递业务帧 --- */
    {
        XGuiRemoteLoopbackDevice* a = NULL;
        XGuiRemoteLoopbackDevice* b = NULL;
        XLinkSession *srv = NULL, *cli = NULL;
        if (xlink_q_benchSetup(&a, &b, &srv, &cli)) {
            uint8_t garbage[64];
            int i;
            g_advRecvCount = 0;
            XLinkSession_setDispatchCallback(cli, xlink_q_advDispatch, NULL);
            for (i = 0; i < 64; i++)
                garbage[i] = (uint8_t)(i * 7 + 3);
            /* 写入远超帧头的垃圾：payloadLen 会是天文数字 -> 判违例断链 */
            XIODevice_write_1((XIODevice*)a, (const char*)garbage, 64);
            XLinkSession_pump(cli, 1200);   /* 时间轴接续握手时刻 */
            XLINK_QCHECK(g_advRecvCount == 0,
                         "畸形帧：垃圾字节不产生业务投递");
            XLINK_QCHECK(!XLinkSession_isReady(cli),
                         "畸形帧：判协议违例后链路断开（状态回 Inactive）");
            XClassDelete((XClass*)srv);
            XClassDelete((XClass*)cli);
            XClassDelete((XClass*)a);
            XClassDelete((XClass*)b);
        }
    }

    /* --- 连续建链/断链：反复握手与 goodbye，不得资源累积 --- */
    {
        size_t before;
        size_t after;
        int round;
        XMemory_setStatisticsEnabled(true);
        before = XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;

        for (round = 0; round < 20; round++) {
            XGuiRemoteLoopbackDevice* a = NULL;
            XGuiRemoteLoopbackDevice* b = NULL;
            XLinkSession *srv = NULL, *cli = NULL;
            uint8_t helloBuf[4];
            XLinkHelloPayload hello;
            int i;
            a = XGuiRemoteLoopbackDevice_createPair(16 * 1024, &b);
            if (!a || !b) break;
            srv = XLinkSession_create_ex((XIODevice*)a);
            cli = XLinkSession_create_ex((XIODevice*)b);
            if (srv && cli) {
                XLinkSession_setCapability(srv, XLinkCapability_Zlib);
                XLinkSession_setCapability(cli, XLinkCapability_Zlib);
                hello.m_version = XLINK_VERSION;
                hello.m_capability = XLinkCapability_Zlib;
                XLinkCodec_encodeHello(&hello, helloBuf, sizeof(helloBuf), NULL);
                XLinkSession_sendSession(srv, XLinkSessionCode_Hello,
                                         helloBuf, 4);
                for (i = 0; i < 4; i++) {
                    XLinkSession_pump(srv, 1000 + i);
                    XLinkSession_pump(cli, 1000 + i);
                }
                /* 主动断链 */
                XLinkSession_sendSession(srv, XLinkSessionCode_Goodbye, NULL, 0);
                XLinkSession_pump(srv, 1100);
                XLinkSession_pump(cli, 1100);
            }
            if (srv) XClassDelete((XClass*)srv);
            if (cli) XClassDelete((XClass*)cli);
            XClassDelete((XClass*)a);
            XClassDelete((XClass*)b);
        }
        after = XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;
        XPrintf("        [INFO] 20 轮建链/断链后在用 %zu 字节（基线 %zu）\n",
                after, before);
        XLINK_QCHECK(after <= before || (after - before) < 4096,
                     "连续建链/断链无资源累积泄漏");
        XMemory_setStatisticsEnabled(false);
    }
}

/* ========================================================================== */
/*                        五、文件传输质量（XLINK_FILE_ON）                   */
/* ========================================================================== */

#if XLINK_FILE_ON && XLINK_CLIENT_ON

/** @brief 文件质量测试根目录（相对当前工作目录，测毕递归清理）。 */
#define XLINK_Q_FILE_DIR "xlink_ft_q"
/** @brief churn 轮数：每轮完整建拆一套传输对象（含会话）并传一个小文件。 */
#define XLINK_Q_FILE_CHURN_ROUNDS 10
/** @brief churn 单轮载荷（16KB：跨 4 个默认 4KB 块，覆盖分块与 ACK 路径）。 */
#define XLINK_Q_FILE_CHURN_BYTES (16u * 1024u)
/** @brief 吞吐基准载荷（4MB：取 1~4MB 口径的上限）。 */
#define XLINK_Q_FILE_BENCH_BYTES (4u * 1024u * 1024u)
/** @brief 单方向传输等待上限（毫秒）：挂死保护，非性能断言。 */
#define XLINK_Q_FILE_WAIT_MS 45000u
/** @brief 客户端握手等待上限（毫秒）。 */
#define XLINK_Q_FILE_READY_MS 8000u
/** @brief 组合吞吐宽松下限（Mbps，上传+下载合计）：只防数量级退化，不追极限。 */
#define XLINK_Q_FILE_MIN_MBPS 1.0

/**
 * @brief 确保测试目录存在（已存在按成功处理）。
 * @note  目录路径相对当前工作目录，避免依赖平台临时目录 API
 *        （口径同 XLinkTest.c 的 xfile_test_makeDir）。
 */
static bool xlink_q_file_ensureDir(void)
{
    XString* path = XString_create_utf8(XLINK_Q_FILE_DIR);
    XFileStat stat;
    bool ok;
    if (!path)
        return false;
    /* XDir_mkdir 的 dirName 语义不符，直接经设备层建目录（见 XLinkTest）。 */
    ok = XDeviceFile_mkdir(path, false);
    if (!ok)
        ok = XDeviceFile_stat(path, &stat) && stat.exists && stat.isDir;
    XClassDelete((XClass*)path);
    return ok;
}

/** @brief 写一个指定大小/内容的测试文件（覆盖写；口径同 XLinkTest.c）。 */
static bool xlink_q_file_writeFile(const char* path, uint32_t size,
                                   uint8_t seed)
{
    XString* name = XString_create_utf8(path);
    XFile* file;
    uint8_t* buffer;
    uint32_t i;
    int64_t written;

    if (!name)
        return false;
    file = XFile_create_2(name);
    XClassDelete((XClass*)name);
    if (!file)
        return false;
    if (!XFile_open_2(file, XIODevice_WriteOnly | XIODevice_Create
                             | XIODevice_Truncate, 0)) {
        XClassDelete((XClass*)file);
        return false;
    }
    buffer = (uint8_t*)XMalloc_System(size ? size : 1u);
    if (!buffer) {
        XClassDelete((XClass*)file);
        return false;
    }
    for (i = 0; i < size; ++i)
        buffer[i] = (uint8_t)(seed + i * 7u);
    written = XIODevice_write_1((XIODevice*)file, (const char*)buffer,
                                (int64_t)size);
    XFree_System(buffer);
    XFile_flush(file);
    XClassDelete((XClass*)file);
    return written == (int64_t)size;
}

/** @brief 删除测试目录（尽力而为；先恢复可写权限，见 XLinkTest.c 说明）。 */
static void xlink_q_file_cleanup(void)
{
    XString* path = XString_create_utf8(XLINK_Q_FILE_DIR);
    XDir* handle;
    XFileStat stat;
    if (!path)
        return;
    if (XDeviceFile_stat(path, &stat) && stat.exists && stat.isFile)
        (void)XDeviceFile_setPermissions(path, (XFilePermissions)0666);
    handle = XDir_create_2(path);
    if (handle) {
        (void)XDir_removeRecursively(handle);
        XClassDelete((XClass*)handle);
    }
    XClassDelete((XClass*)path);
}

/**
 * @brief 拆除一套传输四件套（NULL 安全；顺序沿 XLinkTest.c 端到端用例）。
 * @note  先断客户端链路，再拆文件客户端、文件服务端、客户端，最后关停服务端；
 *        对象建拆的内存对称性正是 churn 项要盯的。
 */
static void xlink_q_file_teardown(XLinkServer* srv, XLinkClient* cli,
                                  XLinkFileServer* fs, XLinkFileClient* fc)
{
    if (cli)
        XLinkClient_disconnectFromServer(cli);
    if (fc) {
        XLinkFileClient_abort(fc, XLinkFileStatus_Aborted, "cleanup");
        XClassDelete((XClass*)fc);
    }
    if (fs) {
        XLinkFileServer_deinit(fs);
        XFree_System(fs);
    }
    if (cli)
        XClassDelete((XClass*)cli);
    if (srv) {
        XLinkServer_close(srv);
        XClassDelete((XClass*)srv);
    }
}

/**
 * @brief 搭一套文件传输「四件套」并完成握手（服务端+文件服务端+客户端+文件客户端）。
 * @details 与 XLinkTest.c 端到端用例同口径：服务端自动端口（0）避免并发冲突；
 *          文件能力必须在连接前声明（握手期协商），否则 File 帧被会话层丢弃；
 *          服务端策略放开双方向与覆盖（测试沙箱内自覆盖写，磁盘占用恒定）。
 * @return 全部成功返回 true；失败时已回收全部半成品并输出 NULL。
 */
static bool xlink_q_file_setup(XLinkServer** outSrv, XLinkClient** outCli,
                               XLinkFileServer** outFs,
                               XLinkFileClient** outFc)
{
    XLinkServer* srv;
    XLinkClient* cli;
    XLinkFileServer* fs;
    XLinkFileClient* fc;
    bool ok;

    srv = XLinkServer_create();
    cli = XLinkClient_create();
    fs = (XLinkFileServer*)XMalloc_System(sizeof(XLinkFileServer));
    fc = XLinkFileClient_create();
    if (!srv || !cli || !fs || !fc) {
        /* 未 init 的 fs 不能 deinit，直接释放；其余走析构 */
        if (fc) XClassDelete((XClass*)fc);
        if (fs) XFree_System(fs);
        if (cli) XClassDelete((XClass*)cli);
        if (srv) XClassDelete((XClass*)srv);
        *outSrv = NULL; *outCli = NULL; *outFs = NULL; *outFc = NULL;
        return false;
    }
    XLinkFileServer_init(fs);
    ok = XLinkFileServer_setRootDir(fs, XLINK_Q_FILE_DIR)
         && XLinkFileServer_setFlags(fs, XLINK_FILESRV_ALLOW_UPLOAD
                                          | XLINK_FILESRV_ALLOW_DOWNLOAD
                                          | XLINK_FILESRV_ALLOW_OVERWRITE);
    if (ok) {
        XLinkFileServer_setMaxFileBytes(fs, 0); /* 沙箱内测试文件不限大小 */
        XLinkServer_setCapability(srv, XLinkCapability_Zlib);
        ok = XLinkFileServer_attach(fs, srv);
    }
    if (ok)
        ok = XLinkServer_listen(srv, "127.0.0.1", 0);
    if (ok) {
        uint16_t port = XTcpServer_serverPort(XLinkServer_tcpServer_const(srv));
        XLinkClient_setHostPort(cli, "127.0.0.1", port);
        XLinkClient_setCapability(cli, XLinkCapability_Zlib
                                            | XLinkCapability_FileTransfer);
        ok = XLinkClient_connectToServer(cli);
    }
    if (ok) {
        uint32_t waited = 0;
        while (!XLinkClient_isReady(cli) && waited < XLINK_Q_FILE_READY_MS) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XLinkServer_pump(srv, xlink_q_nowMs());
            XThread_msleep(2);
            waited += 2;
        }
        ok = XLinkClient_isReady(cli);
    }
    if (ok)
        ok = XLinkFileClient_attach(fc, cli);
    if (!ok) {
        xlink_q_file_teardown(srv, cli, fs, fc);
        *outSrv = NULL; *outCli = NULL; *outFs = NULL; *outFc = NULL;
        return false;
    }
    *outSrv = srv; *outCli = cli; *outFs = fs; *outFc = fc;
    return true;
}

/**
 * @brief 泵到当前传输终局（或超时）；驱动口径同 XLinkTest.c 端到端用例
 *        （事件循环 + 服务端 pump + 文件客户端 pump；环回 TCP 上客户端会话
 *        由 readyRead 信号驱动，文件引擎须按圈手动泵送）。
 * @return 返回时的传输状态；超时则停在最后一个状态（由调用方判 Done）。
 */
static XLinkFileTransferState xlink_q_file_runTransfer(XLinkServer* srv,
                                                       XLinkFileClient* fc,
                                                       uint32_t budgetMs)
{
    uint32_t waited = 0;
    while (XLinkFileClient_isBusy(fc) && waited < budgetMs) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XLinkServer_pump(srv, xlink_q_nowMs());
        XLinkFileClient_pump(fc, xlink_q_nowMs());
        XThread_msleep(2);
        waited += 2;
    }
    return XLinkFileClient_state(fc);
}

/**
 * @brief 文件 churn 单轮：建四件套 -> 上传小文件 -> 下载回来 -> 全部拆光。
 * @return 1 = 本轮全部成功；0 = 任一步失败。
 * @note  上传/下载均为覆盖写且路径固定（服务端 churn_up.bin / 本地 dl.bin），
 *        磁盘足迹恒定，不随轮数累积。
 */
static int xlink_q_file_churnRound(void)
{
    XLinkServer* srv = NULL;
    XLinkClient* cli = NULL;
    XLinkFileServer* fs = NULL;
    XLinkFileClient* fc = NULL;
    int pass = xlink_q_file_setup(&srv, &cli, &fs, &fc);

    if (pass)
        pass = XLinkFileClient_upload(fc, XLINK_Q_FILE_DIR "/src.bin",
                                      "churn_up.bin", 1, NULL)
               && xlink_q_file_runTransfer(srv, fc, XLINK_Q_FILE_WAIT_MS)
                      == XLinkFileTransferState_Done;
    if (pass)
        pass = XLinkFileClient_download(fc, "churn_up.bin",
                                        XLINK_Q_FILE_DIR "/dl.bin", 1, NULL)
               && xlink_q_file_runTransfer(srv, fc, XLINK_Q_FILE_WAIT_MS)
                      == XLinkFileTransferState_Done;
    xlink_q_file_teardown(srv, cli, fs, fc);
    return pass ? 1 : 0;
}

/**
 * @brief 文件传输质量门禁：churn 内存回基线 / 大载荷吞吐宽松下限 / 统计冒烟。
 * @details 走真实环回 TCP + 完整对象栈（XLinkServer/XLinkFileServer/
 *          XLinkClient/XLinkFileClient），与 XLinkTest.c 端到端用例同口径。
 */
static void xlink_qtest_file(void)
{
    XLINK_QSECTION("五、文件传输质量");

    /* 测试目录与 churn 源文件（一次写入多轮复用；必须在启用分配器统计
       之前准备，避免夹具自身的分配污染基线）。 */
    {
        bool dirOk = xlink_q_file_ensureDir();
        XLINK_QCHECK(dirOk, "文件质量测试目录创建成功");
        if (!dirOk)
            return;
    }
    {
        bool srcOk = xlink_q_file_writeFile(XLINK_Q_FILE_DIR "/src.bin",
                                            XLINK_Q_FILE_CHURN_BYTES, 0x3D);
        XLINK_QCHECK(srcOk, "churn 源文件准备成功（16KB）");
        if (!srcOk) {
            xlink_q_file_cleanup();
            return;
        }
    }

    /* --- 5.1 上传/下载反复 churn：对象与会话建拆多轮，在用字节回基线 --- */
    XMemory_setStatisticsEnabled(true);
    {
        int i;
        int pass = 1;
        size_t before;

        /* 暖机一轮：真实套接字与文件引擎首次使用会触发框架懒加载
           （事件分发器基础设施、根目录规范化等一次性分配），消耗掉之后
           再取基线——口径同泄漏分组的服务端 churn 暖机（该处实测暖机前
           基线差 +51544 字节且逐轮完全持平，非泄漏）。 */
        {
            int warm = xlink_q_file_churnRound();
            XLINK_QCHECK(warm, "文件 churn 暖机轮（上传+下载）成功");
        }
        before = XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;
        for (i = 0; i < XLINK_Q_FILE_CHURN_ROUNDS; i++) {
            if (!xlink_q_file_churnRound()) {
                pass = 0;
                break;
            }
        }
        XLINK_QCHECK(pass, "10 轮文件传输对象/会话建拆（上传+下载）全部成功");
        {
            XMemoryStatistics now = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
            long delta = (long)now.systemBytes - (long)before;
            XPrintf("        [INFO] 文件 churn 循环后在用 %zu 字节（基线 %zu，差 %ld）\n",
                    now.systemBytes, before, delta);
            XLINK_QCHECK(delta <= 0 || delta < 4096,
                         "文件传输对象/会话建拆 churn 无内存泄漏（在用字节回到基线容差内）");
        }
        XMemory_setStatisticsEnabled(false);
    }

    /* --- 5.2 吞吐基准：4MB 上传+下载计时，断言宽松下限 ---
       计时口径：上传发起到下载终局（含两次传输全程与中间收尾）；
       组合字节数 = 2 × 载荷（帧头/ACK 只增不减，对下限判定有利无害）。 */
    {
        XLinkServer* srv = NULL;
        XLinkClient* cli = NULL;
        XLinkFileServer* fs = NULL;
        XLinkFileClient* fc = NULL;
        bool benchOk = xlink_q_file_writeFile(XLINK_Q_FILE_DIR "/bench.bin",
                                              XLINK_Q_FILE_BENCH_BYTES, 0x5A);
        double t0u;
        double t1u;
        double ms;
        double mbps;
        XLinkFileTransferState state;

        XLINK_QCHECK(benchOk, "吞吐基准源文件准备成功（4MB）");
        if (benchOk)
            benchOk = xlink_q_file_setup(&srv, &cli, &fs, &fc);
        XLINK_QCHECK(benchOk, "吞吐基准夹具握手成功（客户端 Ready）");
        if (!benchOk) {
            xlink_q_file_cleanup();
            return;
        }

        t0u = xlink_q_nowUs();
        state = XLinkFileTransferState_Failed;
        if (XLinkFileClient_upload(fc, XLINK_Q_FILE_DIR "/bench.bin",
                                   "bench_up.bin", 1, NULL))
            state = xlink_q_file_runTransfer(srv, fc, XLINK_Q_FILE_WAIT_MS);
        XLINK_QCHECK(state == XLinkFileTransferState_Done,
                     "4MB 上传完成（状态 Done）");
        state = XLinkFileTransferState_Failed;
        if (XLinkFileClient_download(fc, "bench_up.bin",
                                     XLINK_Q_FILE_DIR "/bench_dl.bin", 1, NULL))
            state = xlink_q_file_runTransfer(srv, fc, XLINK_Q_FILE_WAIT_MS);
        t1u = xlink_q_nowUs();
        XLINK_QCHECK(state == XLinkFileTransferState_Done,
                     "4MB 下载完成（状态 Done）");

        ms = (t1u > t0u ? (t1u - t0u) : 1.0) / 1000.0;
        mbps = (2.0 * (double)XLINK_Q_FILE_BENCH_BYTES * 8.0 / 1024.0 / 1024.0)
               / (ms / 1000.0);
        XPrintf("        [INFO] 4MB 上传+下载: %.0f ms, 组合吞吐 %.1f Mbps"
                "（宽松下限 %.1f Mbps）\n",
                ms, mbps, XLINK_Q_FILE_MIN_MBPS);
        XLINK_QCHECK(mbps >= XLINK_Q_FILE_MIN_MBPS,
                     "4MB 上传+下载组合吞吐 >= 1 Mbps（宽松下限，防数量级退化）");

        /* --- 5.3 会话统计冒烟：传输完成后字节数/帧数落在合理区间 ---
           区间推演（P=4MB，C=协商块大小帧数 ceil(P/chunk)，默认 4096 块
           时 C=1024）：上行 DATA×C + Open/Finish/Hello，下行 ACK×C +
           Open/Finish——引擎当前为每块必回 ACK（见 XLinkFileTransfer.c
           xft_onData），故帧数约 2C；即使未来改为攒批 ACK 也不低于 C。
           字节数：载荷 P 是绝对下限；线上开销 16B 帧头/帧 × 2C 约 64KB，
           上限给 P + P/8 + 64KB 已足够宽。 */
        {
            XLinkSession* sess = cli ? XLinkClient_session(cli) : NULL;
            XLinkFileTransfer* t = fc ? XLinkFileClient_transfer(fc) : NULL;
            uint32_t chunk = t ? t->m_chunkBytes : XLINK_FILE_CHUNK_BYTES;
            uint64_t dataFrames = chunk
                ? ((uint64_t)XLINK_Q_FILE_BENCH_BYTES + chunk - 1) / chunk
                : 0;
            XLinkSessionStats st;
            bool statsOk;

            memset(&st, 0, sizeof(st));
            XLINK_QCHECK(sess != NULL, "统计冒烟：客户端会话可访问");
            statsOk = (sess != NULL) && XLinkSession_stats(sess, &st);
            XLINK_QCHECK(statsOk, "统计冒烟：XLinkSession_stats 读取成功");
            XLINK_QCHECK(st.m_bytesSent >= (uint64_t)XLINK_Q_FILE_BENCH_BYTES,
                         "统计冒烟：发送字节 >= 上传载荷");
            XLINK_QCHECK(st.m_bytesSent
                              <= (uint64_t)XLINK_Q_FILE_BENCH_BYTES
                                     + (uint64_t)XLINK_Q_FILE_BENCH_BYTES / 8
                                     + 65536,
                         "统计冒烟：发送字节在载荷+线头容差内");
            XLINK_QCHECK(st.m_bytesReceived
                              >= (uint64_t)XLINK_Q_FILE_BENCH_BYTES,
                         "统计冒烟：接收字节 >= 下载载荷");
            XLINK_QCHECK(st.m_bytesReceived
                              <= (uint64_t)XLINK_Q_FILE_BENCH_BYTES
                                     + (uint64_t)XLINK_Q_FILE_BENCH_BYTES / 8
                                     + 65536,
                         "统计冒烟：接收字节在载荷+线头容差内");
            XLINK_QCHECK(st.m_framesSent >= dataFrames + 16,
                         "统计冒烟：发送帧数 >= 数据块帧数+握手余量");
            XLINK_QCHECK(st.m_framesSent <= 2 * dataFrames + 256,
                         "统计冒烟：发送帧数 <= 2×数据块帧数+余量（无帧风暴）");
            XLINK_QCHECK(st.m_framesReceived >= dataFrames + 8,
                         "统计冒烟：接收帧数 >= 数据块帧数+握手余量");
            XLINK_QCHECK(st.m_framesReceived <= 2 * dataFrames + 256,
                         "统计冒烟：接收帧数 <= 2×数据块帧数+余量");
            XLINK_QCHECK(st.m_protocolErrors == 0,
                         "统计冒烟：全程无协议违例");
        }

        xlink_q_file_teardown(srv, cli, fs, fc);
    }

    xlink_q_file_cleanup();
}

#endif /* XLINK_FILE_ON && XLINK_CLIENT_ON */

/* ========================================================================== */
/*                        入口                                               */
/* ========================================================================== */

/**
 * @brief 运行全部 XLink 质量门禁（泄漏 / 性能 / 边界对抗 / 文件传输）。
 * @return 失败计数（0 表示全部通过）。
 */
int XLinkQualityTest_run(void)
{
    XPrintf("========== XLink 质量门禁（泄漏/性能/边界）开始 ==========\n");
    g_qPassed = 0;
    g_qFailed = 0;

    xlink_qtest_leak();
    xlink_qtest_performance();
    xlink_qtest_throughput();
    xlink_qtest_boundary();
#if XLINK_FILE_ON && XLINK_CLIENT_ON
    xlink_qtest_file();
#endif

    XPrintf("\n========== XLink 质量门禁完成: %d 通过, %d 失败 ==========\n",
            g_qPassed, g_qFailed);
    return g_qFailed;
}

/**
 * @brief 质量门禁菜单动作（Test 菜单入口）。
 */
void XLinkQualityTest(void)
{
    (void)XLinkQualityTest_run();
}
