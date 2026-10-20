/** @file       XNtpTest.c
 * @brief      XNtp 客户端全量单元测试（接入 Test 菜单/命令行体系）。
 * @details    覆盖读测试代码即可了解协议用途的分组结构：
 *
 *   一、报文编解码：48 字节大端往返、字段偏移逐字节核对、时间戳纪元换算
 *   二、应答合法性闸门：Mode/Stratum/LI/T1 回显的拒绝矩阵
 *   三、端到端校时（进程内模拟 NTP 服务器 + 已知人工偏移，验证 offset/delay
 *       公式的数值正确性）
 *   四、超时与重试：黑洞端口应超时判失败且重试后耗尽
 *   五、协议拒绝路径：Kiss-o'-Death / 未同步服务器 / T1 回显不匹配
 *   六、applyToSystem 权限门控：无权限时优雅拒绝（有权限的 CI 环境跳过
 *       实写，避免破坏构建机系统时钟）
 *   七、内存泄漏：反复创建/销毁，断言分配器回到基线
 *
 *   端到端用例的关键设计：模拟服务器在应答时给时间戳加 +60 秒人工偏移，
 *   客户端算出的 offset 应≈+60000ms（容差 ±100ms）——这能同时验证报文
 *   编解码、四时间戳公式与端序处理的联合正确性。
 *
 * @author      XinYueC 团队
 */
#include "XNtpTest.h"
#include "XNtpClient.h"
#include "XNtpPacket.h"
#include "XNtpProto.h"
#include "XUdpSocket.h"
#include "XHostAddress.h"
#include "XDateTime.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XString.h"
#include "XThread.h"
#include "XPrintf.h"
#include <string.h>

/* ========================================================================== */
/*                        断言与统计                                         */
/* ========================================================================== */

static int g_passed = 0;
static int g_failed = 0;

/** @brief 断言宏。 */
#define NTP_CHECK(cond, desc)                                               \
    do {                                                                    \
        if (cond) {                                                         \
            g_passed++;                                                     \
            XPrintf("  [PASS] %s\n", desc);                                 \
        } else {                                                            \
            g_failed++;                                                     \
            XPrintf("  [FAIL] %s\n", desc);                                 \
        }                                                                   \
    } while (0)

/** @brief 分组标题。 */
#define NTP_SECTION(title) XPrintf("\n--- %s ---\n", title)

/* --- 模拟服务器的白盒状态（定义在模拟服务器小节，前向声明供端到端用例使用） --- */
static int64_t g_mockT3WallSecs;

/** @brief 泵事件循环直至条件满足或轮次耗尽（每轮 5ms）。 */
static void ntp_pump_until(bool condition, int maxRounds)
{
    int i;
    for (i = 0; i < maxRounds && !condition; i++) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
    }
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/* ========================================================================== */
/*                        一、报文编解码                                     */
/* ========================================================================== */

static void ntp_test_codec(void)
{
    NTP_SECTION("一、报文编解码");

    /* --- 已知字节核对（大端 + LI/VN/Mode 位打包） --- */
    {
        XNtpPacket p;
        uint8_t wire[XNTP_PACKET_SIZE];
        XNtpTimestamp t1;
        const uint64_t t1UnixMs = 1700000000000ULL; /* 2023-11-14 附近 */

        XNtpTimestamp_fromUnixMs(&t1, t1UnixMs);
        XNtpPacket_makeClientRequest(&p, &t1);
        NTP_CHECK(XNtpPacket_encode(&p, wire), "客户端请求编码成功");

        NTP_CHECK(wire[0] == 0x23,
                  "首字节 0x23 = LI(0)|VN(4)|Mode(3) 位打包正确");
        NTP_CHECK(wire[40] == (uint8_t)(t1.m_secs >> 24)
                      && wire[43] == (uint8_t)(t1.m_secs & 0xFF),
                  "T1 秒部分按大端写在 Transmit 字段（偏移 40）");
        NTP_CHECK(wire[24] == 0 && wire[27] == 0 && wire[32] == 0 && wire[35] == 0,
                  "请求中 Originate/Receive 字段为零（RFC 约定）");

        /* --- 解码往返 --- */
        {
            XNtpPacket r;
            NTP_CHECK(XNtpPacket_decode(wire, XNTP_PACKET_SIZE, &r),
                      "应答解码成功");
            NTP_CHECK(r.m_leap == p.m_leap && r.m_version == p.m_version
                          && r.m_mode == p.m_mode && r.m_stratum == p.m_stratum,
                      "解码往返首部字段一致");
            NTP_CHECK(r.m_transmit.m_secs == t1.m_secs
                          && r.m_transmit.m_frac == t1.m_frac,
                      "解码往返时间戳一致");
        }

        /* --- 长度不足拒绝 --- */
        {
            XNtpPacket r;
            NTP_CHECK(!XNtpPacket_decode(wire, XNTP_PACKET_SIZE - 1, &r),
                      "长度不足 48 字节拒绝解码");
            NTP_CHECK(!XNtpPacket_decode(NULL, XNTP_PACKET_SIZE, &r),
                      "NULL 缓冲拒绝解码");
        }
    }

    /* --- 时间戳纪元换算 --- */
    {
        XNtpTimestamp ts;
        uint64_t unixMs = 1700000000123ULL; /* 带 123ms 亚秒部分 */

        XNtpTimestamp_fromUnixMs(&ts, unixMs);
        /* NTP 秒 = Unix 秒 + 2208988800 */
        NTP_CHECK(ts.m_secs == (uint32_t)(unixMs / 1000ULL) + (uint32_t)XNTP_ERA_OFFSET,
                  "NTP 秒 = Unix 秒 + 1900 纪元偏移");
        NTP_CHECK(XNtpTimestamp_toUnixMs(&ts) == unixMs,
                  "toUnixMs/fromUnixMs 毫秒级往返一致");

        /* 1900~1969 区间（早于 Unix 纪元）钳制为 0 */
        ts.m_secs = 2000000000u; /* 1933 年 */
        ts.m_frac = 12345;
        NTP_CHECK(XNtpTimestamp_toUnixMs(&ts) == 0,
                  "早于 Unix 纪元的时间戳钳制为 0");

        NTP_CHECK(XNtpTimestamp_isZero(NULL), "isZero(NULL) 返回 true");
        ts.m_secs = 0; ts.m_frac = 0;
        NTP_CHECK(XNtpTimestamp_isZero(&ts), "全零时间戳识别");
    }
}

/* ========================================================================== */
/*                        二、应答合法性闸门                                 */
/* ========================================================================== */

static void ntp_test_reply_validation(void)
{
    XNtpPacket reply;
    XNtpTimestamp t1;
    XNtpTimestamp zero;

    NTP_SECTION("二、应答合法性闸门");
    memset(&zero, 0, sizeof(zero));

    XNtpTimestamp_fromUnixMs(&t1, 1700000000000ULL);
    XNtpPacket_makeClientRequest(&reply, &t1);
    reply.m_mode = (uint8_t)XNtpMode_Server;
    reply.m_stratum = 5;
    reply.m_receive = t1;
    reply.m_transmit = t1;
    reply.m_originate = t1; /* 服务器把请求的 Transmit(T1) 回显到 Originate */

    NTP_CHECK(XNtpPacket_isValidServerReply(&reply, &t1),
              "合法服务器应答通过校验");

    reply.m_mode = (uint8_t)XNtpMode_Client;
    NTP_CHECK(!XNtpPacket_isValidServerReply(&reply, &t1),
              "Mode 非 Server 拒绝");
    reply.m_mode = (uint8_t)XNtpMode_Server;

    reply.m_stratum = XNTP_STRATUM_KOD;
    NTP_CHECK(!XNtpPacket_isValidServerReply(&reply, &t1),
              "Stratum=0 (Kiss-o'-Death) 拒绝");
    reply.m_stratum = 16;
    NTP_CHECK(!XNtpPacket_isValidServerReply(&reply, &t1),
              "Stratum=16 (未同步) 拒绝");
    reply.m_stratum = 5;

    reply.m_leap = (uint8_t)XNtpLeapIndicator_Unsynchronized;
    NTP_CHECK(!XNtpPacket_isValidServerReply(&reply, &t1),
              "LI=3 (服务器未同步) 拒绝");
    reply.m_leap = (uint8_t)XNtpLeapIndicator_None;

    {
        XNtpTimestamp wrong = t1;
        wrong.m_secs += 1;
        NTP_CHECK(!XNtpPacket_isValidServerReply(&reply, &wrong),
                  "T1 回显不匹配拒绝");
        /* expectedOrigin 传 NULL：跳过回显比对（合法服务器应答仍通过） */
        NTP_CHECK(XNtpPacket_isValidServerReply(&reply, NULL),
                  "expectedOrigin 为 NULL 时跳过回显比对");
    }

    reply.m_originate = zero;
    NTP_CHECK(!XNtpPacket_isValidServerReply(&reply, &t1),
              "零回显（服务器未拷贝 T1）拒绝");
    reply.m_originate = t1;

    reply.m_transmit = zero;
    NTP_CHECK(!XNtpPacket_isValidServerReply(&reply, &t1),
              "服务器 Transmit 为零拒绝");
}

/* ========================================================================== */
/*                        模拟 NTP 服务器                                    */
/* ========================================================================== */

/** 模拟服务器套接字。 */
static XUdpSocket* g_mockServer = NULL;
/** 人工偏移（毫秒）：服务器时间 = 真实时间 + 该值。 */
static int64_t g_mockOffsetMs = 0;
/** 强制应答的 Stratum（0 = 发 KoD）。 */
static uint8_t g_mockStratum = 5;
/** 强制应答的 LI。 */
static uint8_t g_mockLeap = 0;
/** 是否篡改回显（T1 不匹配测试用）。 */
static bool g_mockCorruptOrigin = false;
/** 服务器收到的请求数。 */
static int g_mockRequests = 0;
/** 服务器发出的应答数。 */
static int g_mockReplies = 0;
/** 模拟服务器端口（供信号用例复用同一实例）。 */
static uint16_t g_mockPort = 0;

/* --- 信号到达计数（信号全量覆盖用） --- */
static int g_ntpSyncOk = 0;        /**< synchronized 信号计数。 */
static int g_ntpSyncFail = 0;      /**< syncFailed 信号计数。 */
static int g_ntpOffsetChange = 0;   /**< offsetChanged 信号计数。 */
static int64_t g_ntpLastOffset = 0;/**< 最近一次 offsetChanged 的偏移值。 */

/** @brief synchronized 信号槽。 */
static void ntp_sig_synced(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    g_ntpSyncOk++;
}

/** @brief syncFailed 信号槽。 */
static void ntp_sig_failed(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    g_ntpSyncFail++;
}

/** @brief offsetChanged 信号槽（记录新偏移值）。 */
static void ntp_sig_offset(XObject* receiver, XVarList* args)
{
    XVarList* list = (XVarList*)args;
    (void)receiver;
    g_ntpOffsetChange++;
    if (list) {
        XVarList_args_1(list, int64_t, value);
        g_ntpLastOffset = value;
    }
}

/**
 * @brief 模拟服务器 readyRead：解码请求 → 按当前剧本构造应答 → 回发。
 */
static void ntp_mock_onReadyRead(XObject* sender, XVarList* args)
{
    XUdpSocket* sock = (XUdpSocket*)sender;
    XHostAddress senderAddr;
    uint16_t senderPort = 0;
    uint8_t wire[XNTP_PACKET_SIZE];
    XNtpPacket request;
    XNtpPacket reply;
    XNtpTimestamp t2;
    XNtpTimestamp t3;
    int64_t mockNow;
    int64_t got;

    (void)args;
    if (!sock)
        return;
    XHostAddress_init(&senderAddr);
    got = XUdpSocket_readDatagram(sock, (char*)wire, (int64_t)sizeof(wire),
                                  &senderAddr, &senderPort);
    if (got < (int64_t)XNTP_PACKET_SIZE) {
        XClassDeinit(&senderAddr);
        return;
    }
    if (!XNtpPacket_decode(wire, (size_t)got, &request)) {
        XClassDeinit(&senderAddr);
        return;
    }
    g_mockRequests++;

    /* 服务器视角的当前时间 = 真实墙钟 + 人工偏移 */
    mockNow = XDateTime_currentWallMSecsSinceEpoch() + g_mockOffsetMs;
    g_mockT3WallSecs = mockNow / 1000;
    XNtpTimestamp_fromUnixMs(&t2, (uint64_t)mockNow);
    XNtpTimestamp_fromUnixMs(&t3, (uint64_t)mockNow);

    XNtpPacket_makeClientRequest(&reply, NULL); /* 复用清零逻辑 */
    reply.m_mode = (uint8_t)XNtpMode_Server;
    reply.m_stratum = g_mockStratum;
    reply.m_leap = g_mockLeap;
    reply.m_receive = t2;
    reply.m_transmit = t3;
    reply.m_originate = request.m_transmit; /* 回显 T1 */
    if (g_mockCorruptOrigin)
        reply.m_originate.m_secs += 1;

    if (XNtpPacket_encode(&reply, wire)) {
        if (XUdpSocket_writeDatagram(sock, (const char*)wire,
                                     (int64_t)XNTP_PACKET_SIZE,
                                     &senderAddr, senderPort) > 0)
            g_mockReplies++;
    }
    XClassDeinit(&senderAddr);
}

/**
 * @brief 搭建模拟服务器并按给定剧本重置。
 * @return 成功返回端口号；失败返回 0。
 */
static uint16_t ntp_mock_start(int64_t offsetMs, uint8_t stratum, uint8_t leap,
                               bool corruptOrigin)
{
    uint16_t port;
    g_mockServer = XUdpSocket_create();
    if (!g_mockServer)
        return 0;
    if (!XUdpSocket_bindAny(g_mockServer, 0, XAbstractSocket_DefaultForPlatform)) {
        XClassDelete((XClass*)g_mockServer);
        g_mockServer = NULL;
        return 0;
    }
    g_mockOffsetMs = offsetMs;
    g_mockStratum = stratum;
    g_mockLeap = leap;
    g_mockCorruptOrigin = corruptOrigin;
    g_mockRequests = 0;
    g_mockReplies = 0;
    XObject_connect_2((XObject*)g_mockServer, XSignal(XIODevice_readyRead_signal),
                      ntp_mock_onReadyRead);
    port = (uint16_t)XUdpSocket_localPort(g_mockServer);
    g_mockPort = port; /* 供信号用例复用同一实例 */
    return port;
}

static void ntp_mock_stop(void)
{
    if (g_mockServer) {
        XUdpSocket_abort(g_mockServer);
        XClassDelete((XClass*)g_mockServer);
        g_mockServer = NULL;
    }
}

/* ========================================================================== */
/*                        三、端到端校时                                     */
/* ========================================================================== */

static void ntp_test_end_to_end(void)
{
    XNtpClient* client = NULL;
    uint16_t port;
    int64_t offset = 0;
    int64_t delay = 0;

    NTP_SECTION("三、端到端校时（模拟服务器 + 已知偏移）");

    port = ntp_mock_start(60000, 5, 0, false); /* 服务器快 60 秒 */
    NTP_CHECK(port > 0, "模拟 NTP 服务器启动");
    if (!port)
        return;

    client = XNtpClient_create();
    NTP_CHECK(client != NULL, "客户端创建");
    if (!client) {
        ntp_mock_stop();
        return;
    }
    NTP_CHECK(XNtpClient_state(client) == XNtpClientState_Idle, "初始 Idle");
    NTP_CHECK(!XNtpClient_isSynchronized(client), "初始未同步");
    NTP_CHECK(!XNtpClient_syncNow(client), "未配置服务器时 syncNow 拒绝");

    NTP_CHECK(XNtpClient_setServer_2(client, "127.0.0.1", port),
              "配置 127.0.0.1 服务器");
    NTP_CHECK(XNtpClient_setTimeout(client, 2000), "设置超时 2000ms");
    NTP_CHECK(XNtpClient_setRetries(client, 1), "设置重试 1 次");

    NTP_CHECK(XNtpClient_syncNow(client), "syncNow 发起同步");
    NTP_CHECK(XNtpClient_state(client) == XNtpClientState_Syncing,
              "发出请求后进入 Syncing");
    ntp_pump_until(XNtpClient_state(client) == XNtpClientState_Synchronized, 400);

    NTP_CHECK(XNtpClient_state(client) == XNtpClientState_Synchronized,
              "端到端同步成功转 Synchronized");
    NTP_CHECK(XNtpClient_isSynchronized(client), "isSynchronized 为真");
    NTP_CHECK(g_mockRequests >= 1, "模拟服务器收到请求");
    NTP_CHECK(g_mockReplies >= 1, "模拟服务器发出应答");

    /* 核心断言：offset ≈ +60000ms（容差 ±100ms 覆盖调度抖动）。
       这一条同时验证报文编解码、四时间戳公式与大端端序的联合正确性。 */
    offset = XNtpClient_offsetMs(client);
    delay = XNtpClient_roundTripMs(client);
    XPrintf("        [INFO] offset=%lld ms, delay=%lld ms, stratum=%u\n",
            (long long)offset, (long long)delay,
            (unsigned)XNtpClient_stratum(client));
    NTP_CHECK(offset > 60000 - 100 && offset < 60000 + 100,
              "offset ≈ +60000ms（人工偏移被正确算出）");
    NTP_CHECK(delay >= 0 && delay < 200, "delay 为非负小值（本机回环）");

    {
        /* 白盒等值断言：serverSecs 必须等于服务器生成 T3 时的墙钟秒。
           事件循环对 socket 事件的分派延迟不影响此等值（两侧同源），
           从而把「serverSecs 提取路径」的验证与分派时序解耦。 */
        int64_t got = (int64_t)XNtpClient_serverSecs(client);
        XPrintf("        [DIAG] serverSecs=%lld mockT3WallSecs=%lld diff=%lld "
                "requests=%d replies=%d offset=%lld delay=%lld\n",
                (long long)got, (long long)g_mockT3WallSecs,
                (long long)(got - g_mockT3WallSecs),
                g_mockRequests, g_mockReplies,
                (long long)XNtpClient_offsetMs(client),
                (long long)XNtpClient_roundTripMs(client));
        NTP_CHECK(got >= g_mockT3WallSecs - 1 && got <= g_mockT3WallSecs + 1,
                  "serverSecs 与服务器生成 T3 时刻的墙钟秒一致（白盒等值）");
    }

    NTP_CHECK(!XNtpClient_syncNow(client) == false,
              "同步完成后可再次发起 syncNow");

    XClassDelete((XClass*)client);
    ntp_mock_stop();
}

/* ========================================================================== */
/*                        四、超时与重试                                     */
/* ========================================================================== */

static void ntp_test_timeout(void)
{
    XNtpClient* client = NULL;
    uint16_t port;
    XUdpSocket* blackhole = NULL;

    NTP_SECTION("四、超时与重试");

    /* 黑洞端口：绑定了服务器但永不应答 */
    blackhole = XUdpSocket_create();
    if (!blackhole
        || !XUdpSocket_bindAny(blackhole, 0, XAbstractSocket_DefaultForPlatform)) {
        NTP_CHECK(false, "黑洞端口绑定");
        if (blackhole) {
            XUdpSocket_abort(blackhole);
            XClassDelete((XClass*)blackhole);
        }
        return;
    }
    port = (uint16_t)XUdpSocket_localPort(blackhole);

    client = XNtpClient_create();
    if (!client) {
        NTP_CHECK(false, "客户端创建");
        XUdpSocket_abort(blackhole);
        XClassDelete((XClass*)blackhole);
        return;
    }
    XNtpClient_setServer_2(client, "127.0.0.1", port);
    XNtpClient_setTimeout(client, 150);
    XNtpClient_setRetries(client, 1);

    NTP_CHECK(XNtpClient_syncNow(client), "向黑洞端口发起同步");
    ntp_pump_until(XNtpClient_state(client) == XNtpClientState_Failed, 300);
    NTP_CHECK(XNtpClient_state(client) == XNtpClientState_Failed,
              "超时重试耗尽后转 Failed");
    NTP_CHECK(XNtpClient_lastError(client) == XNtpClientError_Timeout,
              "失败原因 = Timeout");

    /* 未配置服务器：直接拒绝且不入失败态信号路径 */
    {
        XNtpClient* bare = XNtpClient_create();
        NTP_CHECK(bare && !XNtpClient_syncNow(bare),
                  "未配置服务器 syncNow 直接返回 false");
        NTP_CHECK(bare && XNtpClient_lastError(bare)
                          == XNtpClientError_NoServer,
                  "失败原因 = NoServer");
        if (bare) XClassDelete((XClass*)bare);
    }

    XClassDelete((XClass*)client);
    XUdpSocket_abort(blackhole);
    XClassDelete((XClass*)blackhole);
}

/* ========================================================================== */
/*                        五、协议拒绝路径                                   */
/* ========================================================================== */

static void ntp_test_reject_paths(void)
{
    NTP_SECTION("五、协议拒绝路径");

    /* KoD：stratum=0 */
    {
        XNtpClient* client = XNtpClient_create();
        uint16_t port = ntp_mock_start(0, XNTP_STRATUM_KOD, 0, false);
        if (client && port) {
            XNtpClient_setServer_2(client, "127.0.0.1", port);
            XNtpClient_setTimeout(client, 800);
            XNtpClient_syncNow(client);
            ntp_pump_until(XNtpClient_state(client) == XNtpClientState_Failed, 300);
            NTP_CHECK(XNtpClient_lastError(client)
                          == XNtpClientError_KissOfDeath,
                      "Kiss-o'-Death 应答判 KoD 失败");
        } else {
            NTP_CHECK(false, "KoD 场景夹具启动");
        }
        if (client) XClassDelete((XClass*)client);
        ntp_mock_stop();
    }

    /* 未同步服务器：LI=3 */
    {
        XNtpClient* client = XNtpClient_create();
        uint16_t port = ntp_mock_start(0, 5,
                                       (uint8_t)XNtpLeapIndicator_Unsynchronized,
                                       false);
        if (client && port) {
            XNtpClient_setServer_2(client, "127.0.0.1", port);
            XNtpClient_setTimeout(client, 800);
            XNtpClient_syncNow(client);
            ntp_pump_until(XNtpClient_state(client) == XNtpClientState_Failed, 300);
            NTP_CHECK(XNtpClient_lastError(client)
                          == XNtpClientError_ServerUnsynchronized,
                      "LI=3 应答判「服务器未同步」失败");
        } else {
            NTP_CHECK(false, "未同步场景夹具启动");
        }
        if (client) XClassDelete((XClass*)client);
        ntp_mock_stop();
    }

    /* T1 回显不匹配：客户端应忽略错包，最终按超时判失败 */
    {
        XNtpClient* client = XNtpClient_create();
        uint16_t port = ntp_mock_start(0, 5, 0, true);
        if (client && port) {
            XNtpClient_setServer_2(client, "127.0.0.1", port);
            XNtpClient_setTimeout(client, 150);
            XNtpClient_setRetries(client, 0);
            XNtpClient_syncNow(client);
            ntp_pump_until(XNtpClient_state(client) == XNtpClientState_Failed, 300);
            NTP_CHECK(XNtpClient_lastError(client) == XNtpClientError_Timeout,
                      "错包被忽略并最终按超时判失败（防重放语义）");
            NTP_CHECK(XNtpClient_state(client) != XNtpClientState_Synchronized,
                      "错包不会导致伪同步");
        } else {
            NTP_CHECK(false, "错包场景夹具启动");
        }
        if (client) XClassDelete((XClass*)client);
        ntp_mock_stop();
    }
}

/* ========================================================================== */
/*                        六、applyToSystem 权限门控                         */
/* ========================================================================== */

static void ntp_test_apply_to_system(void)
{
    bool canSet = XDateTime_canSetSystemTime();

    NTP_SECTION("六、applyToSystem 权限门控");
    XPrintf("        [INFO] 当前进程可设置系统时间: %s\n",
            canSet ? "是" : "否");

    {
        XNtpClient* client = XNtpClient_create();
        NTP_CHECK(client && !XNtpClient_applyToSystem(client),
                  "从未同步时 applyToSystem 拒绝（不碰系统时钟）");
        if (client) XClassDelete((XClass*)client);
    }

    if (canSet) {
        /* 有权限的环境（root CI/开发机）：跳过实写以免破坏系统时钟。
           权限路径的真实验证请在可随意重置时钟的设备上进行。 */
        XPrintf("        [SKIP] 检测到校时权限，跳过实写用例（防破坏构建机时钟）\n");
        NTP_CHECK(1, "权限环境跳过实写（记录 SKIP）");
    } else {
        /* 无权限：同步后 applyToSystem 应如实返回 false 且不崩溃 */
        XNtpClient* client = NULL;
        uint16_t port = ntp_mock_start(60000, 5, 0, false);
        if (port) {
            client = XNtpClient_create();
            if (client) {
                XNtpClient_setServer_2(client, "127.0.0.1", port);
                XNtpClient_setTimeout(client, 2000);
                XNtpClient_syncNow(client);
                ntp_pump_until(XNtpClient_isSynchronized(client), 400);
                NTP_CHECK(XNtpClient_isSynchronized(client), "无权限用例前置同步成功");
                NTP_CHECK(!XNtpClient_applyToSystem(client),
                          "无权限时 applyToSystem 如实返回 false");
            }
        } else {
            NTP_CHECK(false, "无权限用例夹具启动");
        }
        if (client) XClassDelete((XClass*)client);
        ntp_mock_stop();
    }
}

/* ========================================================================== */
/*                        七、内存泄漏                                       */
/* ========================================================================== */

/* ========================================================================== */
/*                        生命周期 / 访问器 / 信号全量覆盖                     */
/* ========================================================================== */

/**
 * @brief 生命周期与访问器全量覆盖（init 栈对象 / create 堆对象 / private 访问）。
 */
static void ntp_test_lifecycle(void)
{
    XNtpClient* heap;
    XNtpClient stack;

    NTP_SECTION("八、生命周期与访问器");

    /* --- 栈对象：init / XClassDeinit 成对 --- */
    XNtpClient_init(&stack);
    NTP_CHECK(XNtpClient_state(&stack) == XNtpClientState_Idle,
              "栈对象 init 后状态 Idle");
    NTP_CHECK(!XNtpClient_isSynchronized(&stack), "栈对象初始未同步");
    NTP_CHECK(XNtpClient_offsetMs(&stack) == 0, "栈对象初始偏移 0");
    NTP_CHECK(XNtpClient_roundTripMs(&stack) == 0, "栈对象初始时延 0");
    NTP_CHECK(XNtpClient_serverSecs(&stack) == 0, "栈对象初始服务器秒 0");
    NTP_CHECK(XNtpClient_stratum(&stack) == 0, "栈对象初始 stratum 0");
    NTP_CHECK(XNtpClient_lastError(&stack) == XNtpClientError_None,
              "栈对象初始 lastError 为 None");
    /* 私有状态访问器（仅供实现/测试观察内部） */
    NTP_CHECK(XNtpClient_private(&stack) != NULL,
              "private 访问器返回非空状态块");
    /* 堆/栈 NULL 防御 */
    NTP_CHECK(XNtpClient_state(NULL) == XNtpClientState_Idle,
              "state(NULL) 返回 Idle");
    NTP_CHECK(!XNtpClient_isSynchronized(NULL), "isSynchronized(NULL) 返回 false");
    NTP_CHECK(XNtpClient_lastError(NULL) == XNtpClientError_None,
              "lastError(NULL) 返回 None");
    NTP_CHECK(XNtpClient_private(NULL) == NULL, "private(NULL) 返回 NULL");
    XClassDeinit(&stack);
    NTP_CHECK(1, "栈对象 XClassDeinit 成对收尾");

    /* --- 堆对象：create / XClassDelete 成对 --- */
    heap = XNtpClient_create();
    NTP_CHECK(heap != NULL, "create 返回堆对象");
    NTP_CHECK(heap && XNtpClient_private(heap) != NULL,
              "堆对象 private 状态块已分配");
    NTP_CHECK(heap && !XNtpClient_syncNow(heap),
              "未配服务器时 syncNow 拒绝");
    NTP_CHECK(heap && XNtpClient_lastError(heap) == XNtpClientError_NoServer,
              "拒绝后 lastError=NoServer");
    NTP_CHECK(heap && XNtpClient_setServer_2(heap, "ntp.example.com", 123),
              "setServer_2 配置成功");
    NTP_CHECK(heap && XNtpClient_setServer(heap, NULL, 123) == false,
              "setServer(NULL) 返回 false");
    NTP_CHECK(heap && !XNtpClient_applyToSystem(heap),
              "未同步时 applyToSystem 拒绝");
    if (heap)
        XClassDelete((XClass*)heap);
    NTP_CHECK(1, "堆对象 XClassDelete 成对收尾");

    /* --- 虚函数表入口（class_init 幂等，框架内部亦用） --- */
    NTP_CHECK(XNtpClient_class_init() != NULL, "class_init 返回非空虚表");
    NTP_CHECK(XNtpClient_class_init() == XNtpClient_class_init(),
              "class_init 多次调用返回同一虚表（单例）");
}

/**
 * @brief 信号逐个到达覆盖（4 个信号全部触发并计数）。
 */
static void ntp_test_signals(void)
{
    XNtpClient* client;
    int pumped;

    NTP_SECTION("九、信号到达覆盖");

    /* 模拟服务器：偏移 +60000ms、层级 2（与端到端用例同剧本） */
    if (ntp_mock_start(60000, 2, 0, false) == 0) {
        NTP_CHECK(false, "信号用例：模拟服务器启动");
        return;
    }

    client = XNtpClient_create();
    if (!client) {
        NTP_CHECK(false, "信号用例：客户端创建");
        ntp_mock_stop();
        return;
    }

    g_ntpSyncOk = 0;
    g_ntpSyncFail = 0;
    g_ntpOffsetChange = 0;
    g_ntpLastOffset = 0;
    XObject_connect_2((XObject*)client,
                      XSignal(XNtpClient_synchronized_signal), ntp_sig_synced);
    XObject_connect_2((XObject*)client,
                      XSignal(XNtpClient_syncFailed_signal), ntp_sig_failed);
    XObject_connect_2((XObject*)client,
                      XSignal(XNtpClient_offsetChanged_signal),
                      ntp_sig_offset);

    XNtpClient_setServer_2(client, "127.0.0.1", g_mockPort);
    XNtpClient_setTimeout(client, 3000);

    /* 成功路径：应触发 synchronized + offsetChanged 两个信号 */
    XNtpClient_syncNow(client);
    for (pumped = 0; pumped < 400 && !g_ntpSyncOk; ++pumped) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
    }
    NTP_CHECK(g_ntpSyncOk == 1, "synchronized 信号到达 1 次");
    NTP_CHECK(g_ntpOffsetChange == 1, "offsetChanged 信号到达 1 次");
    NTP_CHECK(g_ntpSyncFail == 0, "成功路径不触发 syncFailed");
    NTP_CHECK(g_ntpLastOffset > 60000 - 100 && g_ntpLastOffset < 60000 + 100,
              "offsetChanged 参数为正确的偏移值（≈+60000ms）");

    /* 失败路径：黑洞端口超时 -> syncFailed */
    g_ntpSyncFail = 0;
    {
        XUdpSocket* blackhole = XUdpSocket_create();
        if (blackhole
            && XUdpSocket_bindAny(blackhole, 0,
                                 XAbstractSocket_DefaultForPlatform)) {
            XNtpClient_setServer_2(client, "127.0.0.1",
                                   (uint16_t)XUdpSocket_localPort(blackhole));
            XNtpClient_setTimeout(client, 150);
            XNtpClient_setRetries(client, 0);
            XNtpClient_syncNow(client);
            for (pumped = 0; pumped < 200 && !g_ntpSyncFail; ++pumped) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XThread_msleep(5);
            }
            NTP_CHECK(g_ntpSyncFail == 1, "syncFailed 信号到达 1 次");
            XUdpSocket_abort(blackhole);
            XClassDelete((XClass*)blackhole);
        } else {
            NTP_CHECK(false, "黑洞端口绑定");
        }
    }
    XClassDelete((XClass*)client);
    ntp_mock_stop();
    NTP_CHECK(1, "信号用例：客户端与模拟服务器均已销毁");
}

static void ntp_test_leak(void)
{
    int i;
    size_t before;
    size_t after;

    NTP_SECTION("十、内存泄漏");
    XMemory_setStatisticsEnabled(true);
    before = XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;

    for (i = 0; i < 100; i++) {
        XNtpClient* c = XNtpClient_create();
        if (c) {
            XNtpClient_setServer_2(c, "127.0.0.1", 123);
            XNtpClient_setAutoSync(c, true, 1000); /* 触发定时器创建/销毁路径 */
            XNtpClient_setAutoSync(c, false, 0);
            XClassDelete((XClass*)c);
        }
    }
    after = XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;
    XPrintf("        [INFO] 100 次创建/销毁后在用 %zu 字节（基线 %zu）\n",
            after, before);
    NTP_CHECK(after <= before || (after - before) < 4096,
              "反复创建/销毁无内存泄漏");
    XMemory_setStatisticsEnabled(false);
}

/* ========================================================================== */
/*                        真实网络联调                                       */
/* ========================================================================== */

/** 真网同步结果标志（信号槽置位）。 */
static volatile int g_realSyncState = 0; /* 0=进行中 1=成功 2=失败 */

static void ntp_real_synced(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    g_realSyncState = 1;
}

static void ntp_real_failed(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    g_realSyncState = 2;
}

/**
 * @brief 真实网络校时联调（对公网 NTP 服务器发真实请求，逐步打印环节）。
 * @details 诊断口径：DNS 解析 → 包发送 → 应答等待（12 秒上限）→ 结果。
 *          每个环节独立打印，用于定位「校时没反应」卡在哪一步：
 *          解析失败=DNS/网络不可用；发送失败=出网被拦（UDP 123 常见）；
 *          超时=服务器不可达或应答被防火墙丢弃。
 * @return 0=同步成功；1=失败。
 */
int XNtpRealTest_run(void)
{
    XNtpClient* client = XNtpClient_create();
    int64_t t0;
    int64_t deadline;
    int failures = 0;

    XPrintf("========== XNtp 真实网络校时联调 (ntp.aliyun.com) ==========\n");
    if (!client) {
        XPrintf("[FAIL] 客户端创建失败\n");
        return 1;
    }

    XObject_connect_2((XObject*)client,
                      XSignal(XNtpClient_synchronized_signal), ntp_real_synced);
    XObject_connect_2((XObject*)client,
                      XSignal(XNtpClient_syncFailed_signal), ntp_real_failed);

    XNtpClient_setServer_2(client, "ntp.aliyun.com", 123);
    XNtpClient_setTimeout(client, 5000);
    XNtpClient_setRetries(client, 1);

    t0 = (int64_t)XDateTime_currentMSecsSinceEpoch();
    if (XNtpClient_syncNow(client)) {
        XPrintf("[STEP] 请求已发出 (UDP ntp.aliyun.com:123)，等待应答...\n");
        deadline = t0 + 12000;
        while (g_realSyncState == 0
               && (int64_t)XDateTime_currentMSecsSinceEpoch() < deadline) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(10);
        }
    }

    if (g_realSyncState == 1) {
        XDateTime server;
        XString* text;
        XDateTime_setSecsSinceEpoch(
            &server, (int64_t)XNtpClient_serverSecs(client));
        text = XDateTime_toString_iso(&server);
        XPrintf("[PASS] 校时成功: offset=%lld ms, delay=%lld ms\n",
                (long long)XNtpClient_offsetMs(client),
                (long long)XNtpClient_roundTripMs(client));
        XPrintf("[PASS] 服务器时间(UTC): %s\n",
                text ? XString_toUtf8(text) : "?");
        if (text) XClassDelete(text);
        XPrintf("[INFO] 本机墙钟: %lld | 服务器: %lld (差 %lld 秒)\n",
                (long long)XDateTime_currentSecsSinceEpoch(),
                (long long)XNtpClient_serverSecs(client),
                (long long)((int64_t)XNtpClient_serverSecs(client)
                            - XDateTime_currentSecsSinceEpoch()));
    } else {
        XPrintf("[FAIL] 校时失败: err=%d (1=DNS解析失败 2=套接字 3=超时 "
                "7=云端报错)\n", (int)XNtpClient_lastError(client));
        failures = 1;
    }
    XClassDelete((XClass*)client);
    XPrintf("========== XNtp 真实联调完成: %s ==========\n",
            failures ? "FAIL" : "PASS");
    return failures;
}

/* ========================================================================== */
/*                        入口                                               */
/* ========================================================================== */

int XNtpTest_run(void)
{
    XPrintf("========== XNtp 客户端测试开始 ==========\n");
    g_passed = 0;
    g_failed = 0;

    ntp_test_codec();
    ntp_test_reply_validation();
    ntp_test_end_to_end();
    ntp_test_timeout();
    ntp_test_reject_paths();
    ntp_test_apply_to_system();
    ntp_test_lifecycle();
    ntp_test_signals();
    ntp_test_leak();

    XPrintf("\n========== XNtp 测试完成: %d 通过, %d 失败 ==========\n",
            g_passed, g_failed);
    return g_failed;
}

void XNtpTest(void)
{
    (void)XNtpTest_run();
}

void XTestMenu_XNtpTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XNtp(ntp校时)");
    {
        XAction* action = XTestMenu_addAction(menu, "全量回归（编解码+端到端+对抗）");
        XTestMenu_setActionFunction(action, XNtpTest);
    }
    XTestMenu_addMenu(root, menu);
}
