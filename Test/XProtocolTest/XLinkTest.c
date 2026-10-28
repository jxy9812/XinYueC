/** @file       XLinkTest.c
 * @brief      XLink 私有应用层协议全量单元测试 + 实战案例（接入 Test 菜单/命令行体系）。
 * @details    覆盖 XLink 公开 API 的每一条路径，读测试代码即可了解协议用途：
 *
 *   一、编解码层 XLinkCodec（小端序列化 / 帧头往返 / 载荷上限安全阀）
 *   二、会话生命周期 XLinkSession（init/create/create_ex/deinit 配对）
 *   三、传输挂载与状态机（Inactive -> Connected -> Ready）
 *   四、握手与能力位协商（自动 Hello/HelloAck 双向对称，能力按位与）
 *   五、请求-应答关联（requestId 双向独立、应答出表、异步应答支持）
 *   六、推送与 topicId 承载（u32 topicId 不被 u16 msgCode 截断）
 *   七、订阅/退订语义（订阅表登记、回滚、重复退订拒绝）
 *   八、待应答表与自动超时（严格大于阈值判定、终局上报与出表）
 *   九、心跳与静默判线
 *   十、防御性路径（NULL 防御、非法参数、协议违例、半包粘包）
 *   十一、服务端包装 XLinkServer（监听接入/模板下发/自动握手/会话回收，真实 TCP）
 *   （十二、实战案例已分立为 XLinkDemoServer.c / XLinkDemoClient.c /
 *      XLinkDemo.h 三个文件，本文件经 XLinkTest.h 间接引用其入口。）
 *   十三、信号与自动超时重试（stateChanged/requestFinished 信号、请求自动
 *      超时静默重发与终局上报、sendTick==0 起算回归、四条新 API 防御）
 *   十四、客户端包装 XLinkClient（NULL 防御、默认值与栈上生命周期、正常
 *      链路、踢线自动重连、重连关、手动断开、最大重连次数；原
 *      XLinkClientTest.c 已整体并入本分组）
 *   十五、能力位全矩阵（None/对称/不对称/全组合、HelloAck 附加快照容忍、
 *      RequestTimeout 位不干扰自动超时；negotiatedCapability(NULL) 已见
 *      分组十，不重复）
 *
 *   单元测试（一~十）的传输介质统一使用 XGuiRemoteLoopbackDevice（内存回环
 *   设备对）：两个会话经同一条管道互连，无需真实网卡即可跑通完整链路；其
 *   "全有全无"写入语义恰好能压出 XLink 的帧原子性缺陷。服务端测试（十一）、
 *   实战案例（十二）与客户端包装测试（十四）使用真实 127.0.0.1 环回 TCP。
 *
 *   运行入口：Test 菜单「协议栈 → XLink(...)」，或命令行
 *   `XinYueC -test xlink`（见 Test/XProtocolTest 的命令行分派）。
 *
 * @author      XinYueC 团队
 */
#include "XLinkTest.h"
#include "XLinkDemo.h"
#include "XLinkCodec.h"
#include "XLinkSession.h"
#include "XLinkServer.h"
#include "XLinkClient.h"
#if XLINK_AUTH_ON
#include "XLinkAuth.h"
#include "XLinkUserStore.h"
#endif
#if XLINK_FILE_ON
#include "XLinkFile.h"
#include "XLinkFileServer.h"
#include "XLinkFileClient.h"
#include "XFile.h"
#include "XDir.h"
#include "XDeviceFile.h"
#endif
#include "XGuiRemoteLoopback.h"
#include "XTcpSocket.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XThread.h"
#include "XTestMenu.h"
#include "XAction.h"
#include "XStringUtils.h"
#include "XPrintf.h"
#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
/*                        断言与统计                                         */
/* ========================================================================== */

/** @brief 通过计数。 */
static int g_passed = 0;
/** @brief 失败计数。 */
static int g_failed = 0;

/**
 * @brief 断言宏：条件为真记通过，否则打印用例名并累加失败计数。
 * @param cond 待验证条件。
 * @param desc 用例描述（失败时打印，便于定位）。
 */
#define XLINK_CHECK(cond, desc)                                              \
    do {                                                                     \
        if (cond) {                                                          \
            g_passed++;                                                      \
            XPrintf("  [PASS] %s\n", desc);                                 \
        } else {                                                             \
            g_failed++;                                                      \
            XPrintf("  [FAIL] %s\n", desc);                                 \
        }                                                                    \
    } while (0)

/** @brief 打印当前测试分组标题。 */
#define XLINK_SECTION(title) XPrintf("\n--- %s ---\n", title)

/* ========================================================================== */
/*                        传输介质：回环设备对                                 */
/* ========================================================================== */

/**
 * @brief 两个会话 + 一对回环设备的测试夹具。
 * @details 把「服务端会话 / 客户端会话 / 两端设备」打包管理，避免每个用例
 *          重复搭建与拆除代码。
 */
typedef struct XLinkTestFixture {
    XGuiRemoteLoopbackDevice* m_serverDevice; /**< 服务端侧传输设备（对象拥有）。 */
    XGuiRemoteLoopbackDevice* m_clientDevice; /**< 客户端侧传输设备（对象拥有）。 */
    XLinkSession* m_server;   /**< 服务端会话（被连接方，对象拥有）。 */
    XLinkSession* m_client;   /**< 客户端会话（主动连接方，对象拥有）。 */
    uint32_t m_now;           /**< 夹具内单调毫秒游标，由 step() 推进。 */
} XLinkTestFixture;

/**
 * @brief 搭建夹具：创建设备对并挂到两个会话上。
 * @return 成功返回夹具指针；任一分配失败返回 NULL。
 */
static XLinkTestFixture* xlink_fixture_create(void)
{
    XLinkTestFixture* fixture =
        (XLinkTestFixture*)XMalloc_System(sizeof(XLinkTestFixture));
    if (!fixture)
        return NULL;
    memset(fixture, 0, sizeof(XLinkTestFixture));

    fixture->m_serverDevice =
        XGuiRemoteLoopbackDevice_createPair(64 * 1024,
                                            &fixture->m_clientDevice);
    if (!fixture->m_serverDevice || !fixture->m_clientDevice) {
        if (fixture->m_serverDevice)
            XClassDelete((XClass*)fixture->m_serverDevice);
        if (fixture->m_clientDevice)
            XClassDelete((XClass*)fixture->m_clientDevice);
        XFree_System(fixture);
        return NULL;
    }

    fixture->m_server = XLinkSession_create_ex(
        (XIODevice*)fixture->m_serverDevice);
    fixture->m_client = XLinkSession_create_ex(
        (XIODevice*)fixture->m_clientDevice);
    if (!fixture->m_server || !fixture->m_client) {
        XLinkTestFixture* tmp = fixture;
        /* 拆除失败路径已创建的资源 */
        if (tmp->m_server) XClassDelete((XClass*)tmp->m_server);
        if (tmp->m_client) XClassDelete((XClass*)tmp->m_client);
        XClassDelete((XClass*)tmp->m_serverDevice);
        XClassDelete((XClass*)tmp->m_clientDevice);
        XFree_System(tmp);
        return NULL;
    }
    fixture->m_now = 1000;
    return fixture;
}

/**
 * @brief 拆除夹具：会话与设备全部释放（与 create 成对）。
 */
static void xlink_fixture_delete(XLinkTestFixture* fixture)
{
    if (!fixture)
        return;
    if (fixture->m_server) XClassDelete((XClass*)fixture->m_server);
    if (fixture->m_client) XClassDelete((XClass*)fixture->m_client);
    if (fixture->m_serverDevice)
        XClassDelete((XClass*)fixture->m_serverDevice);
    if (fixture->m_clientDevice)
        XClassDelete((XClass*)fixture->m_clientDevice);
    XFree_System(fixture);
}

/**
 * @brief 推进事件循环若干轮：双向 pump，让帧在两端之间流动。
 * @param fixture 夹具。
 * @param rounds 推进轮数（每轮 10ms）。
 */
static void xlink_fixture_step(XLinkTestFixture* fixture, int rounds)
{
    int i;
    if (!fixture)
        return;
    for (i = 0; i < rounds; i++) {
        fixture->m_now += 10;
        XLinkSession_pump(fixture->m_server, fixture->m_now);
        XLinkSession_pump(fixture->m_client, fixture->m_now);
    }
}

/* ========================================================================== */
/*                     测试用消息收发器与信号观测槽                            */
/* ========================================================================== */

/** 服务端收到的请求数。 */
static int g_serverRequestCount = 0;
/** 客户端收到的应答数。 */
static int g_clientResponseCount = 0;
/** 客户端收到的推送数。 */
static int g_clientPublishCount = 0;
/** 最近一次推送载荷解析出的 topicId。 */
static uint32_t g_lastPublishTopic = 0;
/** 最近一次推送的业务载荷首字节。 */
static uint8_t g_lastPublishByte = 0;
/** 服务端收到的订阅请求 topicId（从载荷首 4 字节解析）。 */
static uint32_t g_serverSawSubscribeTopic = 0;
/** 客户端收到的退订请求 topicId。 */
static uint32_t g_serverSawUnsubscribeTopic = 0;

/**
 * @brief 服务端侧分发回调：收请求即异步回应答，收订阅即推一条回执。
 * @note  业务载荷一律"原样回传"，用于验证线上字节序与边界无损。
 */
static void xlink_server_dispatch(XLinkSession* self,
                                  const XLinkFrameHeader* frame,
                                  const uint8_t* payload)
{
    if (!self || !frame)
        return;

    if (frame->m_msgClass == XLinkMsgClass_Request) {
        uint8_t echo = (payload && frame->m_payloadLen > 0) ? payload[0] : 0;
        g_serverRequestCount++;
        /* 模拟真实设备 IO 的异步应答：不在回调内立即返回，稍后显式回执。 */
        XLinkSession_sendResponse(self, frame->m_requestId, frame->m_msgCode,
                                  &echo, 1);
    } else if (frame->m_msgClass == XLinkMsgClass_Subscribe) {
        uint8_t ack = 0x77;
        /* topicId 在载荷首 4 字节（msgCode 只承载低 16 位） */
        if (payload && frame->m_payloadLen >= 4)
            g_serverSawSubscribeTopic = XLinkCodec_getU32(payload);
        /* 应答订阅：主动推送一条业务数据（sendPublish 会自行补 topicId） */
        XLinkSession_sendPublish(self, g_serverSawSubscribeTopic, &ack, 1);
    } else if (frame->m_msgClass == XLinkMsgClass_Unsubscribe) {
        if (payload && frame->m_payloadLen >= 4)
            g_serverSawUnsubscribeTopic = XLinkCodec_getU32(payload);
    }
}

/**
 * @brief 客户端侧分发回调：统计应答与推送并校验载荷。
 */
static void xlink_client_dispatch(XLinkSession* self,
                                  const XLinkFrameHeader* frame,
                                  const uint8_t* payload)
{
    if (!frame)
        return;
    if (frame->m_msgClass == XLinkMsgClass_Response) {
        g_clientResponseCount++;
    } else if (frame->m_msgClass == XLinkMsgClass_Publish) {
        g_clientPublishCount++;
        /* 推送载荷 = [u32 topicId][业务字节...] */
        if (payload && frame->m_payloadLen >= 4) {
            g_lastPublishTopic = XLinkCodec_getU32(payload);
            g_lastPublishByte = (frame->m_payloadLen >= 5) ? payload[4] : 0;
        }
    }
}

/** @brief 换设备测试的投递计数（xlink_switch_dispatch 落账，用例内清零）。 */
static int g_switchHits = 0;

/**
 * @brief 换设备测试的投递计数器：任何投递到业务层的帧计 1。
 */
static void xlink_switch_dispatch(XLinkSession* self,
                                  const XLinkFrameHeader* frame,
                                  const uint8_t* payload)
{
    (void)self;
    (void)frame;
    (void)payload;
    g_switchHits++;
}

/* ========================================================================== */
/*                        信号观测槽                                          */
/* ========================================================================== */

/** stateChanged 信号观测：累计发射次数。 */
static int g_stateChangedCount = 0;
/** stateChanged 信号观测：最近一次携带的新状态。 */
static XLinkSessionState g_lastStateChanged = XLinkSessionState_Inactive;
/** requestFinished 信号观测：累计发射次数。 */
static int g_reqFinishedCount = 0;
/** requestFinished 信号观测：最近一次携带的请求 id。 */
static uint32_t g_reqFinishedId = 0;
/** requestFinished 信号观测：最近一次携带的命令号。 */
static uint16_t g_reqFinishedCmd = 0;
/** requestFinished 信号观测：最近一次携带的终局结果。 */
static bool g_reqFinishedOk = false;
/** 只计数不应答的服务端回调：收到的请求总数。 */
static int g_silentRequestCount = 0;
/** 只计数不应答的服务端回调：最近一次请求的命令号。 */
static uint16_t g_silentLastCmd = 0;

/**
 * @brief stateChanged 信号槽：落账发射次数与最新状态。
 * @note  参数解包类型与 XLinkSession_stateChanged_signal 发射侧逐一对应
 *        （XVarList 按创建时的类型与大小连续存储）。
 */
static void xlink_state_changed_slot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XLinkSessionState, state);
    g_stateChangedCount++;
    g_lastStateChanged = state;
}

/**
 * @brief requestFinished 信号槽：落账终局三元组（id/命令/成败）。
 */
static void xlink_request_finished_slot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_3(args, uint32_t, requestId, uint16_t, command,
                    bool, succeeded);
    g_reqFinishedCount++;
    g_reqFinishedId = requestId;
    g_reqFinishedCmd = command;
    g_reqFinishedOk = succeeded;
}

/**
 * @brief 只计数不应答的服务端分发回调：构造"请求石沉大海"的稳定超时场景。
 * @note  不回任何应答，客户端的待应答槽只能靠自动超时路径终局。
 */
static void xlink_silent_dispatch(XLinkSession* self,
                                  const XLinkFrameHeader* frame,
                                  const uint8_t* payload)
{
    (void)self;
    (void)payload;
    if (frame && frame->m_msgClass == XLinkMsgClass_Request) {
        g_silentRequestCount++;
        g_silentLastCmd = frame->m_msgCode;
    }
}

/* ========================================================================== */
/*                        一、编解码层测试                                    */
/* ========================================================================== */

/**
 * @brief 覆盖 XLinkCodec 全部公开 API：小端原语、Hello 载荷、帧头编解码。
 */
static void xlink_test_codec(void)
{
    XLINK_SECTION("一、编解码层 XLinkCodec");

    /* --- 小端序列化原语：往返一致 + 已知字节序 --- */
    {
        uint8_t buf[4] = { 0 };
        XLINK_CHECK(XLinkCodec_getU16(NULL) == 0, "getU16 对 NULL 返回 0");
        XLINK_CHECK(XLinkCodec_getU32(NULL) == 0, "getU32 对 NULL 返回 0");
        XLinkCodec_putU16(NULL, 1); /* 不应崩溃 */
        XLinkCodec_putU32(NULL, 1); /* 不应崩溃 */
        XLINK_CHECK(1, "putU16/putU32 对 NULL 安全空操作");

        XLinkCodec_putU16(buf, 0x1234);
        XLINK_CHECK(buf[0] == 0x34 && buf[1] == 0x12,
                    "putU16 按小端写入（低字节在前）");
        XLINK_CHECK(XLinkCodec_getU16(buf) == 0x1234, "putU16/getU16 往返一致");

        XLinkCodec_putU32(buf, 0x89ABCDEFu);
        XLINK_CHECK(buf[0] == 0xEF && buf[3] == 0x89,
                    "putU32 按小端写入（低字节在前）");
        XLINK_CHECK(XLinkCodec_getU32(buf) == 0x89ABCDEFu,
                    "putU32/getU32 往返一致");
        /* topicId 高位不被截断的关键用例 */
        XLinkCodec_putU32(buf, XLINK_TOPIC_USER_BASE + 1u);
        XLINK_CHECK(XLinkCodec_getU32(buf) == 0x00010001u,
                    "u32 往返保持 topic 高位（0x10001 不被截成 1）");
    }

    /* --- Hello 载荷编解码 --- */
    {
        uint8_t buf[8] = { 0 };
        size_t written = 0;
        XLinkHelloPayload src;
        XLinkHelloPayload dst;
        memset(&dst, 0, sizeof(dst));

        src.m_version = XLINK_VERSION;
        src.m_capability = XLinkCapability_Zlib | XLinkCapability_Tls;

        XLINK_CHECK(XLinkCodec_encodeHello(NULL, buf, sizeof(buf), &written)
                        == false, "encodeHello 对 NULL 载荷返回 false");
        XLINK_CHECK(XLinkCodec_encodeHello(&src, NULL, sizeof(buf), &written)
                        == false, "encodeHello 对 NULL 缓冲返回 false");
        XLINK_CHECK(XLinkCodec_encodeHello(&src, buf, 2, &written) == false,
                    "encodeHello 容量不足返回 false");
        XLINK_CHECK(XLinkCodec_encodeHello(&src, buf, sizeof(buf), &written),
                    "encodeHello 正常编码成功");
        XLINK_CHECK(written == 4, "encodeHello 输出长度固定为 4 字节");
        XLINK_CHECK(XLinkCodec_decodeHello(NULL, 4, &dst) == false,
                    "decodeHello 对 NULL 缓冲返回 false");
        XLINK_CHECK(XLinkCodec_decodeHello(buf, 0, &dst) == false,
                    "decodeHello 长度不足返回 false");
        XLINK_CHECK(XLinkCodec_decodeHello(buf, 4, &dst), "decodeHello 正常解码");
        XLINK_CHECK(dst.m_version == src.m_version
                        && dst.m_capability == src.m_capability,
                    "Hello 载荷编解码往返一致");
    }

    /* --- 帧头编解码 --- */
    {
        uint8_t buf[XLINK_HEADER_SIZE] = { 0 };
        XLinkFrameHeader src;
        XLinkFrameHeader dst;
        memset(&dst, 0, sizeof(dst));

        src.m_payloadLen = 0x1234;
        src.m_msgClass = XLinkMsgClass_Publish;
        src.m_msgCode = 0x0001;
        src.m_requestId = 0xDEADBEEFu;

        XLINK_CHECK(XLinkCodec_encodeHeader(NULL, buf) == false,
                    "encodeHeader 对 NULL 帧头返回 false");
        XLINK_CHECK(XLinkCodec_encodeHeader(&src, NULL) == false,
                    "encodeHeader 对 NULL 缓冲返回 false");
        XLINK_CHECK(XLinkCodec_encodeHeader(&src, buf), "encodeHeader 正常编码");
        XLINK_CHECK(XLinkCodec_decodeHeader(NULL, XLINK_HEADER_SIZE, &dst)
                        == false, "decodeHeader 对 NULL 缓冲返回 false");
        XLINK_CHECK(XLinkCodec_decodeHeader(buf, XLINK_HEADER_SIZE - 1, &dst)
                        == false, "decodeHeader 长度不足 12 字节返回 false");
        XLINK_CHECK(XLinkCodec_decodeHeader(buf, XLINK_HEADER_SIZE, &dst),
                    "decodeHeader 正常解码");
        XLINK_CHECK(dst.m_payloadLen == src.m_payloadLen
                        && dst.m_msgClass == src.m_msgClass
                        && dst.m_msgCode == src.m_msgCode
                        && dst.m_requestId == src.m_requestId,
                    "帧头编解码往返一致（四个字段全等）");

        /* 载荷上限安全阀：越界帧头必须被拒，防畸形帧撑爆接收缓冲 */
        src.m_payloadLen = XLINK_MAX_PAYLOAD;
        XLINK_CHECK(XLinkCodec_encodeHeader(&src, buf)
                        && XLinkCodec_decodeHeader(buf, XLINK_HEADER_SIZE, &dst),
                    "载荷恰为上限 XLINK_MAX_PAYLOAD 时放行");
        src.m_payloadLen = XLINK_MAX_PAYLOAD + 1;
        XLINK_CHECK(XLinkCodec_encodeHeader(&src, buf)
                        && !XLinkCodec_decodeHeader(buf, XLINK_HEADER_SIZE, &dst),
                    "载荷超上限被判协议违例（安全阀生效）");
    }
}

/* ========================================================================== */
/*                        二、生命周期测试                                    */
/* ========================================================================== */

/**
 * @brief 覆盖会话对象的构造/析构与 init/deinit 成对语义。
 */
static void xlink_test_lifecycle(void)
{
    XLINK_SECTION("二、会话生命周期 XLinkSession");

    /* --- 堆对象 --- */
    {
        XLinkSession* session = XLinkSession_create();
        XLINK_CHECK(session != NULL, "create 成功返回堆对象");
        XLINK_CHECK(session && XLinkSession_state(session)
                        == XLinkSessionState_Inactive,
                    "新会话初始状态为 Inactive");
        XLINK_CHECK(session && !XLinkSession_isReady(session),
                    "新会话非 Ready（未握手）");
        XLINK_CHECK(session && XLinkSession_negotiatedCapability(session) == 0,
                    "未握手时能力交集为 0");
        XLINK_CHECK(session && XLinkSession_remoteVersion(session) == 0,
                    "未握手时对端版本为 0");
        if (session)
            XClassDelete((XClass*)session);
        XLINK_CHECK(1, "XClassDelete 释放堆会话（与 create 成对）");
    }

    /* --- 栈对象：init / XClassDeinit 成对 --- */
    {
        XLinkSession session;
        XLinkSession_init(&session);
        XLINK_CHECK(XLinkSession_state(&session)
                        == XLinkSessionState_Inactive,
                    "栈对象 init 后状态为 Inactive");
        XClassDeinit(&session);
        XLINK_CHECK(1, "栈对象经 XClassDeinit 反初始化");
    }

    /* --- create_ex：挂载传输后进入 Connected --- */
    {
        XGuiRemoteLoopbackDevice* devA = NULL;
        XGuiRemoteLoopbackDevice* devB = NULL;
        devA = XGuiRemoteLoopbackDevice_createPair(8 * 1024, &devB);
        XLINK_CHECK(devA && devB, "回环设备对创建成功");
        if (devA && devB) {
            XLinkSession* s = XLinkSession_create_ex((XIODevice*)devA);
            XLINK_CHECK(s != NULL, "create_ex 成功返回挂载好传输的会话");
            XLINK_CHECK(s && XLinkSession_state(s)
                            == XLinkSessionState_Connected,
                        "create_ex 挂载传输后状态为 Connected");
            if (s) XClassDelete((XClass*)s);
            /* create_ex(NULL) 也应合法，等价于未挂载 */
            {
                XLinkSession* empty = XLinkSession_create_ex(NULL);
                XLINK_CHECK(empty && XLinkSession_state(empty)
                                == XLinkSessionState_Inactive,
                            "create_ex(NULL) 保持 Inactive");
                if (empty) XClassDelete((XClass*)empty);
            }
        }
        if (devA) XClassDelete((XClass*)devA);
        if (devB) XClassDelete((XClass*)devB);
    }
}

/* ========================================================================== */
/*                        三、传输挂载与状态机                                */
/* ========================================================================== */

/**
 * @brief 覆盖 setDevice/reset 对状态机与内部表的影响。
 */
static void xlink_test_state_machine(void)
{
    XLINK_SECTION("三、传输挂载与状态机");

    XLINK_CHECK(XLinkSession_setDevice(NULL, NULL) == false,
                "setDevice 对 NULL 会话返回 false");

    {
        XLinkTestFixture* f = xlink_fixture_create();
        XLINK_CHECK(f != NULL, "夹具搭建成功");
        if (!f)
            return;

        XLINK_CHECK(XLinkSession_state(f->m_server)
                        == XLinkSessionState_Connected,
                    "夹具会话挂载后为 Connected");

        /* 摘除传输：回 Inactive 并清空内部表 */
        XLINK_CHECK(XLinkSession_setDevice(f->m_server, NULL),
                    "setDevice(NULL) 主动断开成功");
        XLINK_CHECK(XLinkSession_state(f->m_server)
                        == XLinkSessionState_Inactive,
                    "摘除传输后回 Inactive");

        /* 未挂载时发送一律失败（状态机保护） */
        {
            uint8_t arg = 0x01;
            uint32_t id = 0;
            XLINK_CHECK(!XLinkSession_sendRequest(f->m_server, 1, &arg, 1, &id),
                        "未挂载传输时发请求被拒");
            XLINK_CHECK(!XLinkSession_isPending(f->m_server, 1),
                        "发送失败的请求不入待应答表");
        }

        /* 重新挂载恢复 Connected */
        XLINK_CHECK(XLinkSession_setDevice(f->m_server,
                                           (XIODevice*)f->m_serverDevice),
                    "重新挂载传输成功");
        XLINK_CHECK(XLinkSession_state(f->m_server)
                        == XLinkSessionState_Connected,
                    "重新挂载后回 Connected");

        xlink_fixture_delete(f);
        XLINK_CHECK(1, "夹具销毁（会话与设备全部释放）");
    }

    /* 换设备接线：会话从设备对 A 摘下挂到设备对 B 并重新握手后，
       A 侧的收包不再投递（旧 readyRead 接线已摘），B 侧正常收发。
       先在 B 侧完成握手再验证 A 侧帧不达——把「旧接线已摘」与
       「握手未完成不投递」两个因素隔离开。 */
    {
        XGuiRemoteLoopbackDevice *aDev = NULL, *aPeer = NULL;
        XGuiRemoteLoopbackDevice *bDev = NULL, *bPeer = NULL;
        XLinkSession *session = NULL, *aOpponent = NULL, *bOpponent = NULL;
        g_switchHits = 0;

        aDev = XGuiRemoteLoopbackDevice_createPair(8 * 1024, &aPeer);
        bDev = XGuiRemoteLoopbackDevice_createPair(8 * 1024, &bPeer);
        session = XLinkSession_create_ex((XIODevice*)aDev);
        aOpponent = aPeer ? XLinkSession_create_ex((XIODevice*)aPeer) : NULL;
        bOpponent = bPeer ? XLinkSession_create_ex((XIODevice*)bPeer) : NULL;
        XLINK_CHECK(session && aOpponent && bOpponent,
                    "换设备测试夹具搭建成功");
        if (session && aOpponent && bOpponent) {
            XLinkSession_setDispatchCallback(session, xlink_switch_dispatch,
                                             NULL);

            /* A 对：双方首圈 pump 自动互发 Hello，两圈完成握手 */
            XLinkSession_pump(session, 1000);
            XLinkSession_pump(aOpponent, 1000);
            XLinkSession_pump(session, 1010);
            XLinkSession_pump(aOpponent, 1010);
            XLINK_CHECK(XLinkSession_isReady(session),
                        "换设备前 A 链路就绪");

            /* A 对端发一帧：会话（挂在 A 上）应收到 */
            XLinkSession_sendPublish(aOpponent, XLINK_TOPIC_USER_BASE + 3,
                                     (const uint8_t*)"A", 1);
            XLinkSession_pump(aOpponent, 1020);
            XLinkSession_pump(session, 1020);
            XLINK_CHECK(g_switchHits == 1, "挂载 A 时收到 A 侧推送");

            /* 换到设备 B 并与新对端握手 */
            XLINK_CHECK(XLinkSession_setDevice(session, (XIODevice*)bDev),
                        "换设备挂载成功");
            XLinkSession_pump(session, 1030);
            XLinkSession_pump(bOpponent, 1030);
            XLinkSession_pump(session, 1040);
            XLinkSession_pump(bOpponent, 1040);
            XLINK_CHECK(XLinkSession_isReady(session),
                        "换设备后 B 链路就绪");

            /* A 对端再发一帧：旧接线已摘，不得投递 */
            XLinkSession_sendPublish(aOpponent, XLINK_TOPIC_USER_BASE + 3,
                                     (const uint8_t*)"A2", 2);
            XLinkSession_pump(aOpponent, 1050);
            XLinkSession_pump(session, 1050);
            XLINK_CHECK(g_switchHits == 1,
                        "换设备后旧设备 A 的帧不再投递");

            /* B 对端发一帧：新接线正常投递 */
            XLinkSession_sendPublish(bOpponent, XLINK_TOPIC_USER_BASE + 3,
                                     (const uint8_t*)"B2", 2);
            XLinkSession_pump(bOpponent, 1060);
            XLinkSession_pump(session, 1060);
            XLINK_CHECK(g_switchHits == 2,
                        "换设备后新设备 B 的帧正常投递");
        }
        if (session) XClassDelete((XClass*)session);
        if (aOpponent) XClassDelete((XClass*)aOpponent);
        if (bOpponent) XClassDelete((XClass*)bOpponent);
        if (aDev) XClassDelete((XClass*)aDev);
        if (aPeer) XClassDelete((XClass*)aPeer);
        if (bDev) XClassDelete((XClass*)bDev);
        if (bPeer) XClassDelete((XClass*)bPeer);
    }

    /* 分发回调清空：置 NULL 后帧静默丢弃（只发不收的合法用法） */
    {
        XLinkTestFixture* f = xlink_fixture_create();
        if (!f) {
            XLINK_CHECK(false, "夹具搭建成功");
            return;
        }
        XLinkSession_setDispatchCallback(f->m_server, xlink_server_dispatch,
                                         NULL);
        XLinkSession_setDispatchCallback(f->m_client, xlink_client_dispatch,
                                         NULL);
        xlink_fixture_step(f, 3); /* 首圈自动互发 Hello，三圈完成握手 */

        XLinkSession_setDispatchCallback(f->m_client, NULL, NULL);
        {
            int before = g_clientPublishCount;
            XLinkSession_sendPublish(f->m_server, XLINK_TOPIC_USER_BASE + 1,
                                     NULL, 0);
            xlink_fixture_step(f, 3);
            XLINK_CHECK(g_clientPublishCount == before,
                        "回调清空后帧静默丢弃（不投递不崩溃）");
            XLinkSession_setDispatchCallback(f->m_client,
                                             xlink_client_dispatch, NULL);
            XLinkSession_sendPublish(f->m_server, XLINK_TOPIC_USER_BASE + 1,
                                     NULL, 0);
            xlink_fixture_step(f, 3);
            XLINK_CHECK(g_clientPublishCount == before + 1,
                        "重新注册回调后恢复投递");
        }
        xlink_fixture_delete(f);
    }
}

/* ========================================================================== */
/*                        四、握手与能力协商                                  */
/* ========================================================================== */

/**
 * @brief 覆盖双向对称握手：自动 Hello/HelloAck 与能力位按位与。
 */
static void xlink_test_handshake(void)
{
    XLINK_SECTION("四、握手与能力位协商");

    {
        XLinkTestFixture* f = xlink_fixture_create();
        if (!f) {
            XLINK_CHECK(false, "夹具搭建成功");
            return;
        }

        XLinkSession_setDispatchCallback(f->m_server, xlink_server_dispatch, NULL);
        XLinkSession_setDispatchCallback(f->m_client, xlink_client_dispatch, NULL);

        /* 两端各声明能力：服务端 Zlib|Tls，客户端仅 Zlib（随自动 Hello 上线） */
        XLinkSession_setCapability(f->m_server,
                                  XLinkCapability_Zlib | XLinkCapability_Tls);
        XLinkSession_setCapability(f->m_client, XLinkCapability_Zlib);

        XLINK_CHECK(XLinkSession_setCapability(NULL, 1) == false,
                    "setCapability 对 NULL 返回 false");

        /* 握手完成前对业务消息不可见：Publish 不应被投递（增量基线，
           计数器可能被前序用例累加过）。Publish 在驱动前入环，首圈
           pump 触发自动握手，投递与否取决于到达时对端状态。 */
        {
            uint8_t payload = 0x11;
            int before = g_clientPublishCount;
            XLinkSession_sendPublish(f->m_server, XLINK_TOPIC_USER_BASE + 5,
                                     &payload, 1);
            xlink_fixture_step(f, 2);
            XLINK_CHECK(g_clientPublishCount == before,
                        "握手完成前不投递业务消息（防止乱序帧喂给应用）");
        }

        /* 自动握手：双方首次驱动各自补发 Hello，收到对端 Hello 自动回
           HelloAck 并转 Ready（零样板，双向对称） */
        XLINK_CHECK(XLinkSession_isReady(f->m_client),
                    "客户端自动握手完成并转 Ready");
        XLINK_CHECK(XLinkSession_isReady(f->m_server),
                    "服务端自动握手完成并转 Ready（双向对称）");
        XLINK_CHECK(XLinkSession_remoteVersion(f->m_client) == XLINK_VERSION,
                    "客户端学到对端协议版本");
        XLINK_CHECK(XLinkSession_remoteVersion(f->m_server) == XLINK_VERSION,
                    "服务端学到对端协议版本");
        /* 能力按位与：客户端 Zlib ∩ 服务端(Zlib|Tls) = Zlib */
        XLINK_CHECK(XLinkSession_negotiatedCapability(f->m_client)
                        == XLinkCapability_Zlib,
                    "客户端能力交集=Zlib（与本端及对端声明一致）");
        XLINK_CHECK(XLinkSession_negotiatedCapability(f->m_server)
                        == XLinkCapability_Zlib,
                    "服务端能力交集=Zlib（双方共同支持项）");

        /* 心跳与主动断链 */
        XLINK_CHECK(XLinkSession_sendSession(f->m_client,
                                             XLinkSessionCode_Heartbeat,
                                             NULL, 0),
                    "客户端发出 Heartbeat");
        xlink_fixture_step(f, 2);
        XLINK_CHECK(XLinkSession_isReady(f->m_client),
                    "心跳后链路保持 Ready");

        XLINK_CHECK(XLinkSession_sendSession(f->m_server,
                                             XLinkSessionCode_Goodbye,
                                             NULL, 0),
                    "服务端发出 Goodbye 主动断链");
        xlink_fixture_step(f, 2);
        /* Goodbye 是发给对端的，发起方自身不置位；接收方才据此转 Inactive。 */
        XLINK_CHECK(XLinkSession_state(f->m_client)
                        == XLinkSessionState_Inactive,
                    "客户端收到 Goodbye 后转 Inactive（对端主动断链感知）");
        XLINK_CHECK(XLinkSession_state(f->m_server)
                        != XLinkSessionState_Inactive,
                    "发送方不因自己发出的 Goodbye 被动断链");

        xlink_fixture_delete(f);
    }
}

/* ========================================================================== */
/*                        五、请求-应答关联                                   */
/* ========================================================================== */

/**
 * @brief 覆盖 requestId 生成、应答关联、待应答进出表。
 */
static void xlink_test_request_response(void)
{
    XLINK_SECTION("五、请求-应答关联");

    {
        XLinkTestFixture* f = xlink_fixture_create();
        uint8_t arg = 0x5A;
        uint32_t id1 = 0;
        uint32_t id2 = 0;
        int reqBase = 0;
        int respBase = 0;
        if (!f) {
            XLINK_CHECK(false, "夹具搭建成功");
            return;
        }
        XLinkSession_setDispatchCallback(f->m_server, xlink_server_dispatch, NULL);
        XLinkSession_setDispatchCallback(f->m_client, xlink_client_dispatch, NULL);
        XLinkSession_setCapability(f->m_server, XLinkCapability_Zlib);
        XLinkSession_setCapability(f->m_client, XLinkCapability_Zlib);

        /* 参数防御 */
        XLINK_CHECK(XLinkSession_sendRequest(NULL, 1, &arg, 1, &id1) == false,
                    "sendRequest 对 NULL 会话返回 false");

        /* 自动握手：首次驱动补发 Hello，业务消息就绪后才可见 */
        xlink_fixture_step(f, 3);
        XLINK_CHECK(XLinkSession_isReady(f->m_client), "自动握手完成");
        /* 计数取增量基线：全局计数器可能被前序用例累加过 */
        reqBase = g_serverRequestCount;
        respBase = g_clientResponseCount;

        /* 请求 id 生成与待应答入表 */
        XLINK_CHECK(XLinkSession_sendRequest(f->m_client, 42, &arg, 1, &id1),
                    "客户端发出第一条请求");
        XLINK_CHECK(id1 > 0, "请求 id 有效（非 0）");
        XLINK_CHECK(XLinkSession_isPending(f->m_client, id1),
                    "请求进入待应答表");

        /* 零载荷请求（控制类常见形态） */
        XLINK_CHECK(XLinkSession_sendRequest(f->m_client, 43, NULL, 0, &id2),
                    "客户端发出零载荷请求");
        XLINK_CHECK(id2 != id1 && id2 > 0, "第二条请求 id 递增且互不相同");

        xlink_fixture_step(f, 3);

        XLINK_CHECK(g_serverRequestCount == reqBase + 2, "服务端收到两条请求");
        XLINK_CHECK(g_clientResponseCount == respBase + 2, "客户端收到两条应答");
        XLINK_CHECK(!XLinkSession_isPending(f->m_client, id1),
                    "应答到达后请求 1 出待应答表");
        XLINK_CHECK(!XLinkSession_isPending(f->m_client, id2),
                    "应答到达后请求 2 出待应答表");
        XLINK_CHECK(!XLinkSession_isPending(f->m_client, 99999),
                    "查询不存在的 id 返回 false（未误判）");

        /* 服务端主动回一个应答（反向：证明双向可发起） */
        XLINK_CHECK(XLinkSession_sendResponse(f->m_server, 777, 88, &arg, 1),
                    "服务端可主动发出应答（双向对称）");

        xlink_fixture_delete(f);
    }
}

/* ========================================================================== */
/*                        六、推送与 topicId 承载                             */
/* ========================================================================== */

/**
 * @brief 覆盖 Publish：u32 topicId 完整承载与业务载荷边界无损。
 */
static void xlink_test_publish(void)
{
    XLINK_SECTION("六、推送与 topicId 承载");

    {
        XLinkTestFixture* f = xlink_fixture_create();
        uint8_t payload = 0x99;
        if (!f) {
            XLINK_CHECK(false, "夹具搭建成功");
            return;
        }
        XLinkSession_setDispatchCallback(f->m_server, xlink_server_dispatch, NULL);
        XLinkSession_setDispatchCallback(f->m_client, xlink_client_dispatch, NULL);
        XLinkSession_setCapability(f->m_server, XLinkCapability_Zlib);
        XLinkSession_setCapability(f->m_client, XLinkCapability_Zlib);

        XLINK_CHECK(XLinkSession_sendPublish(NULL, XLINK_TOPIC_USER_BASE,
                                             &payload, 1) == false,
                    "sendPublish 对 NULL 会话返回 false");
        XLINK_CHECK(!XLinkSession_sendPublish(f->m_server, 5, &payload, 1),
                    "sendPublish 拒绝低于用户基址的 topicId");

        xlink_fixture_step(f, 3); /* 自动握手 */

        /* 零载荷推送（事件类"仅通知无数据"形态）。
           计数取增量基线：全局计数器可能被前序用例累加过。 */
        {
            int before = g_clientPublishCount;
        XLINK_CHECK(XLinkSession_sendPublish(f->m_server,
                                             XLINK_TOPIC_USER_BASE + 1,
                                             NULL, 0),
                    "零载荷推送发送成功");
        /* 带业务载荷推送 */
        XLINK_CHECK(XLinkSession_sendPublish(f->m_server,
                                             XLINK_TOPIC_USER_BASE + 1,
                                             &payload, 1),
                    "带载荷推送发送成功");
        xlink_fixture_step(f, 3);

        XLINK_CHECK(g_clientPublishCount == before + 2, "客户端收到两条推送");
        XLINK_CHECK(g_lastPublishTopic == XLINK_TOPIC_USER_BASE + 1,
                    "推送载荷携带完整 u32 topicId（0x10001 高位未丢失）");
        XLINK_CHECK(g_lastPublishByte == 0x99,
                    "业务载荷紧随 topicId 之后未错位");

        /* 业务载荷超上限（可用上限 = XLINK_MAX_PAYLOAD - 4）：
           拒绝、不截断、不部分发送，且链路不受影响。 */
        {
            static uint8_t bigBuf[XLINK_MAX_PAYLOAD - 3]; /* 比上限多 1 字节 */
            XLINK_CHECK(!XLinkSession_sendPublish(f->m_server,
                                                  XLINK_TOPIC_USER_BASE + 1,
                                                  bigBuf, sizeof(bigBuf)),
                        "业务载荷超上限被拒");
            XLINK_CHECK(XLinkSession_isReady(f->m_server),
                        "超限拒绝后链路仍健康");
            XLINK_CHECK(XLinkSession_sendPublish(f->m_server,
                                                 XLINK_TOPIC_USER_BASE + 1,
                                                 &payload, 1),
                        "超限拒绝后仍可正常推送");
            xlink_fixture_step(f, 3);
            XLINK_CHECK(g_clientPublishCount == before + 3,
                        "超限拒绝不影响后续推送送达");
            }
        }

        xlink_fixture_delete(f);
    }
}

/* ========================================================================== */
/*                        七、订阅与退订                                      */
/* ========================================================================== */

/**
 * @brief 覆盖 Subscribe/Unsubscribe 的本地表语义与失败回滚。
 */
static void xlink_test_subscribe(void)
{
    XLINK_SECTION("七、订阅与退订");

    /* 未挂载场景：验证参数校验与失败回滚 */
    {
        XLinkSession* s = XLinkSession_create();
        XLINK_CHECK(s != NULL, "会话创建成功");
        XLINK_CHECK(XLinkSession_subscribe(NULL, XLINK_TOPIC_USER_BASE)
                        == false, "subscribe 对 NULL 会话返回 false");
        XLINK_CHECK(XLinkSession_unsubscribe(NULL, XLINK_TOPIC_USER_BASE)
                        == false, "unsubscribe 对 NULL 会话返回 false");
        XLINK_CHECK(!XLinkSession_subscribe(s, 5),
                    "subscribe 拒绝低于用户基址的 topicId");
        XLINK_CHECK(!XLinkSession_isSubscribed(s, XLINK_TOPIC_USER_BASE),
                    "未订阅时 isSubscribed 返回 false");

        /* 未挂载传输：订阅写帧失败须回滚本地表 */
        XLINK_CHECK(!XLinkSession_subscribe(s, XLINK_TOPIC_USER_BASE + 9),
                    "未挂载传输时订阅发送失败");
        XLINK_CHECK(!XLinkSession_isSubscribed(s, XLINK_TOPIC_USER_BASE + 9),
                    "订阅发送失败后本地表已回滚（无脏订阅）");
        XLINK_CHECK(!XLinkSession_unsubscribe(s, XLINK_TOPIC_USER_BASE + 9),
                    "未订阅时退订被拒（不发无意义帧）");
        XLINK_CHECK(!XLinkSession_isSubscribed(NULL, 1),
                    "isSubscribed 对 NULL 返回 false");
        if (s) XClassDelete((XClass*)s);
    }

    /* 已握手场景：验证订阅驱动对端推送 */
    {
        XLinkTestFixture* f = xlink_fixture_create();
        uint32_t topic = XLINK_TOPIC_USER_BASE + 7;
        if (!f) {
            XLINK_CHECK(false, "夹具搭建成功");
            return;
        }
        XLinkSession_setDispatchCallback(f->m_server, xlink_server_dispatch, NULL);
        XLinkSession_setDispatchCallback(f->m_client, xlink_client_dispatch, NULL);
        XLinkSession_setCapability(f->m_server, XLinkCapability_Zlib);
        XLinkSession_setCapability(f->m_client, XLinkCapability_Zlib);

        xlink_fixture_step(f, 3); /* 自动握手 */

        /* 记录订阅前推送数，便于对比 */
        {
            int before = g_clientPublishCount;
            XLINK_CHECK(XLinkSession_subscribe(f->m_client, topic),
                        "客户端订阅 topic 成功");
            XLINK_CHECK(XLinkSession_isSubscribed(f->m_client, topic),
                        "订阅表登记成功");
            xlink_fixture_step(f, 3);
            XLINK_CHECK(g_serverSawSubscribeTopic == topic,
                        "服务端从载荷解析出完整 u32 topicId");
            XLINK_CHECK(g_clientPublishCount == before + 1,
                        "订阅后服务端主动推送被客户端接收");

            XLINK_CHECK(XLinkSession_unsubscribe(f->m_client, topic),
                        "客户端退订成功");
            XLINK_CHECK(!XLinkSession_isSubscribed(f->m_client, topic),
                        "退订后订阅表移除");
            xlink_fixture_step(f, 2);
            XLINK_CHECK(g_serverSawUnsubscribeTopic == topic,
                        "服务端从载荷解析出退订 topicId");
            XLINK_CHECK(!XLinkSession_unsubscribe(f->m_client, topic),
                        "重复退订被拒");

            /* 重复订阅幂等：多次订阅等价一次（不重复占槽），一次退订即退净。
               （若订阅重复占槽，此处一次退订后 isSubscribed 仍为 true） */
            XLINK_CHECK(XLinkSession_subscribe(f->m_client, topic),
                        "首次订阅成功");
            XLINK_CHECK(XLinkSession_subscribe(f->m_client, topic),
                        "重复订阅幂等返回 true");
            XLINK_CHECK(XLinkSession_subscribe(f->m_client, topic),
                        "多次订阅仍幂等");
            XLINK_CHECK(XLinkSession_unsubscribe(f->m_client, topic),
                        "幂等订阅一次退订即退净");
            XLINK_CHECK(!XLinkSession_isSubscribed(f->m_client, topic),
                        "幂等订阅退订后无残留槽位");
        }

        xlink_fixture_delete(f);
    }
}

/* ========================================================================== */
/*                        八、待应答表与自动超时                              */
/* ========================================================================== */

/**
 * @brief 覆盖自动超时判定：严格大于阈值才终局，终局出表且不重复上报。
 */
static void xlink_test_pending_timeout(void)
{
    XLINK_SECTION("八、待应答表与自动超时");

    XLINK_CHECK(XLinkSession_isPending(NULL, 1) == false,
                "isPending 对 NULL 返回 false");

    {
        XLinkTestFixture* f = xlink_fixture_create();
        uint8_t arg = 0x01;
        uint32_t id = 0;
        if (!f) {
            XLINK_CHECK(false, "夹具搭建成功");
            return;
        }
        /* 双端首次驱动自动握手；服务端不注册回调：请求发出后无人应答，
           构造稳定的超时终局场景。 */
        xlink_fixture_step(f, 3);
        XLINK_CHECK(XLinkSession_isReady(f->m_client), "自动握手完成");

        /* 自动模式：阈值 500ms、不重试（首次超时即终局） */
        XLINK_CHECK(XLinkSession_setRequestTimeout(f->m_client, 500),
                    "setRequestTimeout(500) 成功");
        XLINK_CHECK(XLinkSession_setNumberOfRetries(f->m_client, 0),
                    "setNumberOfRetries(0) 成功");
        g_reqFinishedCount = 0;
        g_reqFinishedId = 0;
        g_reqFinishedCmd = 0;
        g_reqFinishedOk = true;
        XObject_connect_2((XObject*)f->m_client,
                          XSignal(XLinkSession_requestFinished_signal),
                          xlink_request_finished_slot);

        XLINK_CHECK(XLinkSession_sendRequest(f->m_client, 99, &arg, 1, &id),
                    "发出不会被应答的请求");
        XLINK_CHECK(XLinkSession_isPending(f->m_client, id),
                    "请求在待应答表中");

        /* 恰好等于阈值不算超时：判定口径为严格大于 */
        XLinkSession_pump(f->m_client, f->m_now + 500);
        XLINK_CHECK(g_reqFinishedCount == 0,
                    "达到阈值但未超过不判超时（严格大于口径）");
        XLINK_CHECK(XLinkSession_isPending(f->m_client, id),
                    "阈值边缘请求仍在待应答表");

        /* 再过 1ms 越过阈值：终局上报并出表 */
        XLinkSession_pump(f->m_client, f->m_now + 501);
        XLINK_CHECK(g_reqFinishedCount == 1,
                    "越过阈值后发射 requestFinished(false)");
        XLINK_CHECK(g_reqFinishedId == id && g_reqFinishedCmd == 99
                        && g_reqFinishedOk == false,
                    "终局信号携带原请求 id、命令号与失败结果");
        XLINK_CHECK(!XLinkSession_isPending(f->m_client, id),
                    "超时终局后请求已出表");

        /* 出表后继续 pump：不重复上报（不刷屏） */
        XLinkSession_pump(f->m_client, f->m_now + 601);
        XLINK_CHECK(g_reqFinishedCount == 1,
                    "超时终局后不重复上报");

        xlink_fixture_delete(f);
    }

    /* 待应答表容量：超出槽位数应拒绝而非静默丢 */
    {
        XGuiRemoteLoopbackDevice* devA = NULL;
        XGuiRemoteLoopbackDevice* devB = NULL;
        devA = XGuiRemoteLoopbackDevice_createPair(64 * 1024, &devB);
        if (devA && devB) {
            XLinkSession* s = XLinkSession_create_ex((XIODevice*)devA);
            uint8_t arg = 1;
            uint32_t id = 0;
            int accepted = 0;
            int i;
            for (i = 0; i < XLINK_PENDING_SLOTS + 2; i++) {
                if (XLinkSession_sendRequest(s, (uint16_t)i, &arg, 1, &id))
                    accepted++;
                else
                    break;
            }
            XLINK_CHECK(accepted == XLINK_PENDING_SLOTS,
                        "待应答表装满后拒绝新请求（不静默丢失）");
            XLinkSession_setDevice(s, NULL);
            if (s) XClassDelete((XClass*)s);
        }
        if (devA) XClassDelete((XClass*)devA);
        if (devB) XClassDelete((XClass*)devB);
    }
}

/* ========================================================================== */
/*                        九、心跳与静默判线                                  */
/* ========================================================================== */

/**
 * @brief 覆盖静默超时判线：超时自动断链，有流量则续命。
 */
static void xlink_test_heartbeat_timeout(void)
{
    XLINK_SECTION("九、心跳与静默判线");

    {
        XLinkTestFixture* f = xlink_fixture_create();
        if (!f) {
            XLINK_CHECK(false, "夹具搭建成功");
            return;
        }
        XLinkSession_setDispatchCallback(f->m_server, xlink_server_dispatch, NULL);
        XLinkSession_setCapability(f->m_server, XLinkCapability_Zlib);

        xlink_fixture_step(f, 3); /* 自动握手 */
        XLINK_CHECK(XLinkSession_isReady(f->m_server), "握手完成进入 Ready");

        /* 静默推进超过阈值：pump 期间无任何收发，应判线 */
        f->m_now += XLINK_HEARTBEAT_TIMEOUT_MS + 1000;
        XLinkSession_pump(f->m_server, f->m_now);
        XLINK_CHECK(XLinkSession_state(f->m_server)
                        == XLinkSessionState_Inactive,
                    "静默超过阈值自动判线转 Inactive");

        xlink_fixture_delete(f);
    }
}

/* ========================================================================== */
/*                        十、防御性路径                                      */
/* ========================================================================== */

/**
 * @brief 覆盖 NULL 防御、畸形帧与半帧处理。
 */
static void xlink_test_defensive(void)
{
    XLINK_SECTION("十、防御性路径");

    /* NULL 防御：全部查询类 API 传 NULL 不得崩溃 */
    XLINK_CHECK(XLinkSession_state(NULL) == XLinkSessionState_Inactive,
                "state(NULL) 返回 Inactive");
    XLINK_CHECK(!XLinkSession_isReady(NULL), "isReady(NULL) 返回 false");
    XLINK_CHECK(XLinkSession_negotiatedCapability(NULL) == 0,
                "negotiatedCapability(NULL) 返回 0");
    XLINK_CHECK(XLinkSession_remoteVersion(NULL) == 0,
                "remoteVersion(NULL) 返回 0");
    XLINK_CHECK(!XLinkSession_isPending(NULL, 1), "isPending(NULL) 返回 false");
    XLINK_CHECK(XLinkSession_setDevice(NULL, NULL) == false,
                "setDevice(NULL) 返回 false");
    XLINK_CHECK(!XLinkSession_setDispatchCallback(NULL, NULL, NULL),
                "setDispatchCallback(NULL) 返回 false");
    XLINK_CHECK(XLinkSession_send(NULL, NULL, NULL) == false,
                "send(NULL, NULL) 返回 false");
    XLinkSession_reset(NULL);
    XLinkSession_pump(NULL, 0);
    XLINK_CHECK(1, "reset/pump 对 NULL 安全空操作不崩溃");

    /* 畸形帧：帧头声明超限载荷应被拒而非越界读 */
    {
        XLinkSession* s = XLinkSession_create();
        if (s) {
            XLinkFrameHeader bad;
            bad.m_payloadLen = XLINK_MAX_PAYLOAD + 100;
            bad.m_msgClass = XLinkMsgClass_Publish;
            bad.m_msgCode = 1;
            bad.m_requestId = 0;
            /* 未挂载传输，send 直接失败；这里验证参数校验分支 */
            XLINK_CHECK(!XLinkSession_send(s, &bad, NULL),
                        "未挂载传输时超限帧发送被拒");
            XLINK_CHECK(XLinkSession_send(s, &bad, NULL) == false,
                        "超限载荷的 send 返回 false");
            XClassDelete((XClass*)s);
        }
    }

    /* 半帧处理：分两次写入的帧应被正确拼接（验证跨 pump 的粘包/半包） */
    {
        XGuiRemoteLoopbackDevice* devA = NULL;
        XGuiRemoteLoopbackDevice* devB = NULL;
        devA = XGuiRemoteLoopbackDevice_createPair(64 * 1024, &devB);
        XLINK_CHECK(devA && devB, "回环设备对创建成功");
        if (devA && devB) {
            XLinkSession* a = XLinkSession_create_ex((XIODevice*)devA);
            XLinkSession* b = XLinkSession_create_ex((XIODevice*)devB);
            XLinkSession_setDispatchCallback(b, xlink_client_dispatch, NULL);
            if (a && b) {
                int before = g_clientPublishCount;
                uint8_t payload = 0x66;

                /* 先握手（各端首次 pump 自动补发 Hello） */
                XLinkSession_setCapability(a, XLinkCapability_Zlib);
                XLinkSession_setCapability(b, XLinkCapability_Zlib);
                XLinkSession_pump(a, 1000);
                XLinkSession_pump(b, 1000);
                XLinkSession_pump(a, 1000);
                XLINK_CHECK(XLinkSession_isReady(b), "半帧测试前置握手完成");

                /* 手动把一帧拆成两段写入对端，模拟半包到达。
                   线上格式 = [12B 帧头][4B topicId][1B 业务载荷]，共 17 字节。 */
                {
                    uint8_t frame[XLINK_HEADER_SIZE + 4 + 1];
                    XLinkFrameHeader h;
                    h.m_payloadLen = 5; /* topicId(4) + 业务字节(1) */
                    h.m_msgClass = XLinkMsgClass_Publish;
                    h.m_msgCode = (uint16_t)(XLINK_TOPIC_USER_BASE & 0xFFFF);
                    h.m_requestId = 0;
                    XLinkCodec_encodeHeader(&h, frame);
                    XLinkCodec_putU32(&frame[XLINK_HEADER_SIZE],
                                      XLINK_TOPIC_USER_BASE);
                    frame[XLINK_HEADER_SIZE + 4] = payload;
                    /* 先只写帧头（12 字节），载荷尚未到达。
                       写 devA（本端发送侧）：回环设备「本端写入 = 入对端接收
                       环」，故写 devA 才会送达 b 所挂的 devB。 */
                    XIODevice_write_1((XIODevice*)devA,
                                      (const char*)frame, XLINK_HEADER_SIZE);
                    XLinkSession_pump(b, 1010);
                    XLINK_CHECK(g_clientPublishCount == before,
                                "半帧（仅帧头到达）不被误投递");
                    /* 再补齐剩余 5 字节载荷，整帧到齐后应投递 */
                    XIODevice_write_1((XIODevice*)devA,
                                      (const char*)&frame[XLINK_HEADER_SIZE],
                                      5);
                    XLinkSession_pump(b, 1020);
                    XLINK_CHECK(g_clientPublishCount == before + 1,
                                "半帧补齐后整帧被正确投递");
                    XLINK_CHECK(g_lastPublishByte == payload,
                                "半帧拼接后业务载荷未错位");
                }
            }
            if (a) XClassDelete((XClass*)a);
            if (b) XClassDelete((XClass*)b);
        }
        if (devA) XClassDelete((XClass*)devA);
        if (devB) XClassDelete((XClass*)devB);
    }
}

/* ========================================================================== */
/*                        十一、服务端包装 XLinkServer                        */
/* ========================================================================== */

/** 服务端 notify：Added 事件计数。 */
static int g_srvAddedCount = 0;
/** 服务端 notify：Removed 事件计数。 */
static int g_srvRemovedCount = 0;
/** 服务端分发回调收到的请求数。 */
static int g_srvEchoRequests = 0;
/** 客户端收到 0x5A 回显应答数。 */
static int g_srvEchoResponses = 0;

/**
 * @brief 服务端会话增删通知回调：统计 Added/Removed。
 */
static void xlink_srv_notify(XLinkServer* self, XLinkSession* session,
                             XLinkServerSessionEvent event)
{
    (void)self;
    (void)session;
    if (event == XLinkServerSessionEvent_Added)
        g_srvAddedCount++;
    else
        g_srvRemovedCount++;
}

/**
 * @brief 服务端分发回调：请求即回 1 字节回显应答。
 */
static void xlink_srv_dispatch(XLinkSession* self,
                               const XLinkFrameHeader* frame,
                               const uint8_t* payload)
{
    if (!self || !frame)
        return;
    if (frame->m_msgClass == XLinkMsgClass_Request) {
        uint8_t echo = (payload && frame->m_payloadLen > 0) ? payload[0] : 0;
        g_srvEchoRequests++;
        XLinkSession_sendResponse(self, frame->m_requestId, frame->m_msgCode,
                                  &echo, 1);
    }
}

/**
 * @brief 客户端分发回调：统计 0x5A 回显应答。
 */
static void xlink_srv_client_dispatch(XLinkSession* self,
                                      const XLinkFrameHeader* frame,
                                      const uint8_t* payload)
{
    (void)self;
    if (frame && frame->m_msgClass == XLinkMsgClass_Response
        && payload && frame->m_payloadLen >= 1 && payload[0] == 0x5A)
        g_srvEchoResponses++;
}

/**
 * @brief 覆盖 XLinkServer：NULL 防御、栈上生命周期、真实 TCP 监听接入、
 *        模板下发、零样板自动握手、请求应答、Inactive 会话回收。
 * @note  真实链路经 127.0.0.1 环回 TCP，accept 依赖 processEvents 驱动
 *        监听器事件（与 XConsoleShellBackendTest 同口径）；pump 的 nowMs
 *        全程用实时单调毫秒——服务端会话可能被内建定时器/readyRead 以
 *        真实时钟异步驱动，混入假时钟会污染静默判线时间轴。
 */
static void xlink_test_server(void)
{
    XLINK_SECTION("十一、服务端包装 XLinkServer");

    /* --- NULL 防御 --- */
    XLinkServer_pump(NULL, 0);
    XLinkServer_close(NULL);
    XLINK_CHECK(XLinkServer_sessionCount(NULL) == 0,
                "sessionCount 对 NULL 返回 0");
    XLINK_CHECK(XLinkServer_sessionAt(NULL, 0) == NULL,
                "sessionAt 对 NULL 返回 NULL");
    XLINK_CHECK(XLinkServer_tcpServer_const(NULL) == NULL,
                "tcpServer_const 对 NULL 返回 NULL");
    XLINK_CHECK(!XLinkServer_setTickInterval(NULL, 100),
                "setTickInterval 对 NULL 拒绝");
    XLINK_CHECK(XLinkServer_tickInterval(NULL) == 0,
                "tickInterval 对 NULL 返回 0");
    XLINK_CHECK(!XLinkServer_listen_base(NULL, "127.0.0.1", 0),
                "listen 对 NULL 拒绝");
    XLINK_CHECK(1, "pump/close 对 NULL 安全空操作");

    /* --- 栈上生命周期：init/deinit 配对 --- */
    {
        XLinkServer stackSrv;
        XLinkServer_init(&stackSrv);
        XLINK_CHECK(XLinkServer_tcpServer_const(&stackSrv) != NULL,
                    "栈上 init 内嵌监听器就绪");
        XLINK_CHECK(XLinkServer_sessionCount(&stackSrv) == 0,
                    "初始托管会话数为 0");
        XClassDeinit(&stackSrv);
        XLINK_CHECK(1, "栈上 init/deinit 配对无崩溃");
    }

    /* --- 真实 TCP 全链路 --- */
    {
        XLinkServer* srv = XLinkServer_create();
        XTcpSocket* clientSock = NULL;
        XLinkSession* client = NULL;
        XLinkSession* srvSession = NULL;
        uint16_t port = 0;
        int i;
        uint32_t reqId = 0;
        uint8_t arg = 0x5A;

        g_srvAddedCount = 0;
        g_srvRemovedCount = 0;
        g_srvEchoRequests = 0;
        g_srvEchoResponses = 0;

        XLINK_CHECK(srv != NULL, "服务端堆创建成功");
        if (!srv)
            return;

        XLinkServer_setCapability(srv, XLinkCapability_Zlib);
        XLinkServer_setDispatchCallback(srv, xlink_srv_dispatch, NULL);
        XLinkServer_setNotifyCallback(srv, xlink_srv_notify);

        if (XLinkServer_listen_base(srv, "127.0.0.1", 0)) {
            XTcpServer* listener = XLinkServer_tcpServer_const(srv);
            port = XTcpServer_serverPort(listener);
            XLINK_CHECK(port != 0, "自动分配端口监听成功");
        } else {
            XLINK_CHECK(0, "自动分配端口监听成功");
        }

        /* 客户端连入（异步 connect + 事件驱动 accept） */
        clientSock = XTcpSocket_create();
        if (clientSock) {
            XTcpSocket_connectToHost_base((XAbstractSocket*)clientSock,
                                          "127.0.0.1", port,
                                          XIODevice_ReadWrite,
                                          XAbstractSocket_AnyIPProtocol);
            XLINK_CHECK(XTcpSocket_waitForConnected_base(
                            (XAbstractSocket*)clientSock, 3000),
                        "客户端连接环回端口成功");

            for (i = 0; i < 500 && g_srvAddedCount == 0; i++) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XLinkServer_pump(srv,
                                 (uint32_t)XDateTime_currentMSecsSinceEpoch());
            }
            XLINK_CHECK(g_srvAddedCount == 1, "newConnection 自动建会话（Added 通知）");
            XLINK_CHECK(XLinkServer_sessionCount(srv) == 1,
                        "托管会话数为 1");
            srvSession = XLinkServer_sessionAt(srv, 0);
            XLINK_CHECK(srvSession != NULL
                        && XLinkSession_state(srvSession)
                               == XLinkSessionState_Connected,
                        "新会话处于 Connected（模板能力位/回调已下发）");
        } else {
            XLINK_CHECK(0, "客户端连接环回端口成功");
        }

        /* 客户端侧会话 + 零样板握手：两侧均不手动发 Hello，
           各自首次 pump 自动补发，对端收到自动回 HelloAck。 */
        if (clientSock) {
            client = XLinkSession_create_ex((XIODevice*)clientSock);
            XLinkSession_setCapability(client, XLinkCapability_Zlib);
            XLinkSession_setDispatchCallback(client, xlink_srv_client_dispatch,
                                              NULL);
        }
        for (i = 0; i < 500
                    && !(client && XLinkSession_isReady(client)
                         && srvSession && XLinkSession_isReady(srvSession));
             i++) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XLinkServer_pump(srv,
                             (uint32_t)XDateTime_currentMSecsSinceEpoch());
            if (client)
                XLinkSession_pump(client,
                                  (uint32_t)XDateTime_currentMSecsSinceEpoch());
        }
        XLINK_CHECK(client != NULL && XLinkSession_isReady(client),
                    "客户端自动握手完成（零样板）");
        XLINK_CHECK(srvSession != NULL && XLinkSession_isReady(srvSession),
                    "服务端会话自动握手完成");
        XLINK_CHECK((client ? XLinkSession_negotiatedCapability(client) : 0)
                        & XLinkCapability_Zlib,
                    "能力位按位与协商生效（Zlib）");

        /* 请求-应答走真实 TCP */
        if (client && XLinkSession_isReady(client)) {
            XLinkSession_sendRequest(client, 0x1234, &arg, 1, &reqId);
            for (i = 0; i < 500 && g_srvEchoResponses == 0; i++) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XLinkServer_pump(srv,
                                 (uint32_t)XDateTime_currentMSecsSinceEpoch());
                XLinkSession_pump(client,
                                  (uint32_t)XDateTime_currentMSecsSinceEpoch());
            }
            XLINK_CHECK(g_srvEchoRequests == 1, "服务端模板回调收到请求");
            XLINK_CHECK(g_srvEchoResponses == 1, "客户端收到 0x5A 回显应答");
        }

        /* 回收：客户端 Goodbye -> 服务端会话 Inactive -> 自动出表删除 */
        if (client && XLinkSession_isReady(client)) {
            XLinkSession_sendSession(client, XLinkSessionCode_Goodbye,
                                     NULL, 0);
            for (i = 0; i < 500 && g_srvRemovedCount == 0; i++) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XLinkServer_pump(srv,
                                 (uint32_t)XDateTime_currentMSecsSinceEpoch());
            }
            XLINK_CHECK(g_srvRemovedCount == 1, "Inactive 会话自动回收（Removed 通知）");
            XLINK_CHECK(XLinkServer_sessionCount(srv) == 0,
                        "回收后托管会话数为 0");
        }

        if (client)
            XClassDelete((XClass*)client);
        if (clientSock)
            XClassDelete((XClass*)clientSock);
        XClassDelete((XClass*)srv);
        XLINK_CHECK(1, "服务端销毁连带回收全部资源无崩溃");
    }

    /* --- 复用与查询边界：sessionAt 越界 / close 重听 / 重复 listen --- */
    {
        XLinkServer* srv = XLinkServer_create();
        XTcpServer* listener;
        uint16_t port1 = 0;
        bool ok = false;

        XLINK_CHECK(srv != NULL, "复用测试服务端创建成功");
        if (!srv)
            return;

        /* 空表查询边界 */
        XLINK_CHECK(XLinkServer_sessionAt(srv, -1) == NULL,
                    "sessionAt 负下标返回 NULL");
        XLINK_CHECK(XLinkServer_sessionAt(srv, 0) == NULL,
                    "空表 sessionAt(0) 返回 NULL");
        XLINK_CHECK(XLinkServer_sessionAt(srv, 99) == NULL,
                    "sessionAt 越界下标返回 NULL");

        /* 首次监听 */
        ok = XLinkServer_listen_base(srv, "127.0.0.1", 0);
        XLINK_CHECK(ok, "复用测试首次监听成功");
        if (ok) {
            listener = XLinkServer_tcpServer_const(srv);
            port1 = XTcpServer_serverPort(listener);
            XLINK_CHECK(port1 != 0, "首次监听拿到端口");

            /* 重复 listen：语义为"关旧开新"，监听态保持 */
            XLINK_CHECK(XLinkServer_listen_base(srv, "127.0.0.1", 0),
                        "重复 listen 关旧开新成功");
            listener = XLinkServer_tcpServer_const(srv);
            XLINK_CHECK(XTcpServer_isListening(listener),
                        "重复 listen 后仍在监听");

            /* 内建节拍周期运行中可调：改值回读一致、监听不受影响、
               置 0 即停节拍（转手动 pump）、再恢复默认。 */
            XLINK_CHECK(XLinkServer_tickInterval(srv)
                            == XLINK_HEARTBEAT_TICK_MS,
                        "默认节拍为编译配置值");
            XLINK_CHECK(XLinkServer_setTickInterval(srv, 100),
                        "运行中调整节拍为 100ms");
            XLINK_CHECK(XLinkServer_tickInterval(srv) == 100,
                        "节拍回读一致");
            XLINK_CHECK(XTcpServer_isListening(
                            XLinkServer_tcpServer_const(srv)),
                        "调整节拍后监听不受影响");
            XLINK_CHECK(XLinkServer_setTickInterval(srv, 0),
                        "节拍置 0（停内建节拍）");
            XLINK_CHECK(XLinkServer_tickInterval(srv) == 0,
                        "节拍 0 回读一致");
            XLINK_CHECK(XLinkServer_setTickInterval(srv,
                                            XLINK_HEARTBEAT_TICK_MS),
                        "恢复默认节拍");
        }

        /* close 后对象可复用：再听一次 */
        XLinkServer_close(srv);
        XLINK_CHECK(XLinkServer_sessionCount(srv) == 0,
                    "close 后托管会话清空");
        XLINK_CHECK(XLinkServer_sessionAt(srv, 0) == NULL,
                    "close 后 sessionAt 返回 NULL");
        XLINK_CHECK(XLinkServer_listen_base(srv, "127.0.0.1", 0),
                    "close 后重新监听成功");
        listener = XLinkServer_tcpServer_const(srv);
        XLINK_CHECK(XTcpServer_isListening(listener),
                    "重听后监听态正常");

        XClassDelete((XClass*)srv);
        XLINK_CHECK(1, "复用测试服务端销毁无崩溃");
    }
}

/* ========================================================================== */
/*                        十三、信号与自动超时重试                            */
/* ========================================================================== */

/* 信号观测槽与只计数不应答的服务端回调统一放在文件头部
   「测试用消息收发器与信号观测」区（第八分组同样消费 requestFinished）。 */

/**
 * @brief 覆盖分组十三：stateChanged/requestFinished 两条信号与请求自动
 *        超时静默重试，及四条新 API 的 NULL 防御。
 * @note  信号经 XObject_connect_2 连接（发射同步直达槽），假时钟由夹具
 *        xlink_fixture_step 推进；自动超时的判定口径为严格大于阈值。
 */
static void xlink_test_signals_retry(void)
{
    XLINK_SECTION("十三、信号与自动超时重试");

    /* --- 四条新 API 的 NULL 防御 --- */
    XLINK_CHECK(XLinkSession_setNumberOfRetries(NULL, 3) == false,
                "setNumberOfRetries 对 NULL 返回 false");
    XLINK_CHECK(XLinkSession_numberOfRetries(NULL) == 0,
                "numberOfRetries 对 NULL 返回 0");
    XLINK_CHECK(XLinkSession_setRequestTimeout(NULL, 100) == false,
                "setRequestTimeout 对 NULL 返回 false");
    XLINK_CHECK(XLinkSession_requestTimeout(NULL) == 0,
                "requestTimeout 对 NULL 返回 0");

    /* --- 默认参数即被动模式：与既有行为一致（零回归红线） --- */
    {
        XLinkSession* s = XLinkSession_create();
        XLINK_CHECK(s && XLinkSession_numberOfRetries(s)
                        == XLINK_RETRY_MAX_DEFAULT,
                    "新会话重试次数默认 XLINK_RETRY_MAX_DEFAULT");
        XLINK_CHECK(s && XLinkSession_requestTimeout(s)
                        == XLINK_REQUEST_TIMEOUT_MS,
                    "新会话超时阈值默认 XLINK_REQUEST_TIMEOUT_MS（被动模式）");
        if (s) XClassDelete((XClass*)s);
    }

    /* --- stateChanged：建链/断链发射，同态不重发 --- */
    {
        XGuiRemoteLoopbackDevice* devA = NULL;
        XGuiRemoteLoopbackDevice* devB = NULL;
        XLinkSession* s = NULL;
        XLinkSession* peer = NULL;
        devA = XGuiRemoteLoopbackDevice_createPair(8 * 1024, &devB);
        s = devA ? XLinkSession_create() : NULL;
        XLINK_CHECK(s != NULL, "stateChanged 测试会话创建成功");
        if (s && devA && devB) {
            g_stateChangedCount = 0;
            g_lastStateChanged = XLinkSessionState_Inactive;
            XLINK_CHECK(XObject_connect_2(
                                (XObject*)s,
                                XSignal(XLinkSession_stateChanged_signal),
                                xlink_state_changed_slot) != NULL,
                        "stateChanged 信号连接成功");

            /* 建链：Inactive -> Connected（第 1 次发射） */
            XLINK_CHECK(XLinkSession_setDevice(s, (XIODevice*)devA),
                        "挂载传输建链成功");
            XLINK_CHECK(g_stateChangedCount == 1
                            && g_lastStateChanged
                                   == XLinkSessionState_Connected,
                        "挂载传输发射 stateChanged(Connected)");

            /* 握手转 Ready：对端自动应答 HelloAck，本端收到后转 Ready */
            peer = XLinkSession_create_ex((XIODevice*)devB);
            XLinkSession_pump(s, 1000);      /* s 自动补发 Hello */
            XLinkSession_pump(peer, 1000);   /* peer 收 Hello 回 HelloAck */
            XLinkSession_pump(s, 1010);      /* s 收 HelloAck 转 Ready */
            XLINK_CHECK(g_stateChangedCount == 2
                            && g_lastStateChanged == XLinkSessionState_Ready,
                        "握手完成发射 stateChanged(Ready)");

            /* 断链：reset 回 Inactive（第 3 次发射） */
            XLinkSession_reset(s);
            XLINK_CHECK(g_stateChangedCount == 3
                            && g_lastStateChanged
                                   == XLinkSessionState_Inactive,
                        "reset 断链发射 stateChanged(Inactive)");

            /* 同态不重发：Inactive 态重复 reset 不发射 */
            XLinkSession_reset(s);
            XLINK_CHECK(g_stateChangedCount == 3,
                        "同态（Inactive）重复 reset 不重发信号");

            /* 重复挂载同设备：Connected 态再挂载不发射 */
            XLINK_CHECK(XLinkSession_setDevice(s, (XIODevice*)devA),
                        "Connected 态重复挂载返回 true");
            XLINK_CHECK(g_stateChangedCount == 4,
                        "再次建链发射（第 4 次）");
            XLINK_CHECK(XLinkSession_setDevice(s, (XIODevice*)devA),
                        "同态重复挂载返回 true");
            XLINK_CHECK(g_stateChangedCount == 4,
                        "同态（Connected）重复挂载不重发信号");
        }
        if (s) XClassDelete((XClass*)s);
        if (peer) XClassDelete((XClass*)peer);
        if (devA) XClassDelete((XClass*)devA);
        if (devB) XClassDelete((XClass*)devB);
    }

    /* --- requestFinished：应答命中发射 true，携带正确 id 与命令 --- */
    {
        XLinkTestFixture* f = xlink_fixture_create();
        uint8_t arg = 0x5A;
        uint32_t id = 0;
        if (!f) {
            XLINK_CHECK(false, "requestFinished 测试夹具搭建成功");
            return;
        }
        XLinkSession_setDispatchCallback(f->m_server, xlink_server_dispatch,
                                         NULL);
        XLinkSession_setDispatchCallback(f->m_client, xlink_client_dispatch,
                                         NULL);
        xlink_fixture_step(f, 3); /* 自动握手 */
        XLINK_CHECK(XLinkSession_isReady(f->m_client),
                    "requestFinished 测试前置握手完成");

        g_reqFinishedCount = 0;
        g_reqFinishedId = 0;
        g_reqFinishedCmd = 0;
        g_reqFinishedOk = true;
        XObject_connect_2((XObject*)f->m_client,
                          XSignal(XLinkSession_requestFinished_signal),
                          xlink_request_finished_slot);

        XLINK_CHECK(XLinkSession_sendRequest(f->m_client, 42, &arg, 1, &id),
                    "发出待应答请求（命令 42）");
        xlink_fixture_step(f, 3);
        XLINK_CHECK(g_reqFinishedCount == 1,
                    "应答命中待应答表发射 requestFinished 一次");
        XLINK_CHECK(g_reqFinishedId == id,
                    "成功信号携带原请求 id");
        XLINK_CHECK(g_reqFinishedCmd == 42,
                    "成功信号携带命令号 42");
        XLINK_CHECK(g_reqFinishedOk,
                    "成功信号 succeeded=true");
        XLINK_CHECK(!XLinkSession_isPending(f->m_client, id),
                    "成功终局后槽已出表");

        /* 漂移应答（无对应待应答槽）不发射 */
        XLINK_CHECK(XLinkSession_sendResponse(f->m_server, 777, 88, &arg, 1),
                    "服务端发出漂移应答（id=777 无人等待）");
        xlink_fixture_step(f, 3);
        XLINK_CHECK(g_reqFinishedCount == 1,
                    "漂移应答不发射 requestFinished");

        /* 重复应答（槽已出表）不发射 */
        XLINK_CHECK(XLinkSession_sendResponse(f->m_server, id, 42, &arg, 1),
                    "服务端对已终局请求重复应答");
        xlink_fixture_step(f, 3);
        XLINK_CHECK(g_reqFinishedCount == 1,
                    "重复应答不发射 requestFinished（仅终局一次）");

        xlink_fixture_delete(f);
    }

    /* --- 自动超时静默重试：假时钟跨超时点，重发不换 id，终局发 false --- */
    {
        XLinkTestFixture* f = xlink_fixture_create();
        uint8_t arg = 0x5A;
        static uint8_t bigArg[XLINK_RETRY_PAYLOAD_MAX + 1]; /* 超长：129 字节 */
        uint32_t id42 = 0;
        uint32_t idBig = 0;
        uint32_t idEmpty = 0;
        if (!f) {
            XLINK_CHECK(false, "自动重试测试夹具搭建成功");
            return;
        }
        /* 服务端只计数不应答：请求石沉大海，超时路径稳定可断言 */
        XLinkSession_setDispatchCallback(f->m_server, xlink_silent_dispatch,
                                         NULL);
        xlink_fixture_step(f, 3); /* 自动握手 */
        XLINK_CHECK(XLinkSession_isReady(f->m_client),
                    "自动重试测试前置握手完成");

        /* 开启自动模式：超时 500ms，最多重发 1 次 */
        XLINK_CHECK(XLinkSession_setRequestTimeout(f->m_client, 500),
                    "setRequestTimeout(500) 成功");
        XLINK_CHECK(XLinkSession_requestTimeout(f->m_client) == 500,
                    "requestTimeout 回读一致");
        XLINK_CHECK(XLinkSession_setNumberOfRetries(f->m_client, 1),
                    "setNumberOfRetries(1) 成功");
        XLINK_CHECK(XLinkSession_numberOfRetries(f->m_client) == 1,
                    "numberOfRetries 回读一致");
        XObject_connect_2((XObject*)f->m_client,
                          XSignal(XLinkSession_requestFinished_signal),
                          xlink_request_finished_slot);

        /* 子场景一：小载荷请求跨超时点——静默重发同命令，额度用尽发 false */
        g_silentRequestCount = 0;
        g_silentLastCmd = 0;
        g_reqFinishedCount = 0;
        XLINK_CHECK(XLinkSession_sendRequest(f->m_client, 42, &arg, 1, &id42),
                    "发出 1 字节载荷请求（命令 42）");
        xlink_fixture_step(f, 60); /* 推进 600ms：跨过 500ms 超时点 */
        XLINK_CHECK(g_silentRequestCount == 2,
                    "超时后原 requestId 静默重发（服务端收到两次同命令请求）");
        XLINK_CHECK(g_silentLastCmd == 42,
                    "重发请求命令号与原请求一致");
        XLINK_CHECK(g_reqFinishedCount == 0,
                    "重发成功不发射终局信号");
        XLINK_CHECK(XLinkSession_isPending(f->m_client, id42),
                    "重发后请求仍在待应答表（不换 id 不出表）");

        xlink_fixture_step(f, 60); /* 再推进 600ms：重试额度（1 次）用尽 */
        XLINK_CHECK(g_reqFinishedCount == 1,
                    "第二次超时（额度用尽）发射 requestFinished(false)");
        XLINK_CHECK(g_reqFinishedId == id42,
                    "终局信号携带原请求 id");
        XLINK_CHECK(g_reqFinishedCmd == 42,
                    "终局信号携带命令号 42");
        XLINK_CHECK(g_reqFinishedOk == false,
                    "终局信号 succeeded=false");
        XLINK_CHECK(!XLinkSession_isPending(f->m_client, id42),
                    "终局后槽已出表");
        XLINK_CHECK(g_silentRequestCount == 2,
                    "额度用尽后不再重发（服务端仍只见过两次）");

        /* 子场景二：超长载荷（> XLINK_RETRY_PAYLOAD_MAX）不可重试，
           首次超时即终局，不发第二次请求 */
        g_silentRequestCount = 0;
        g_reqFinishedCount = 0;
        memset(bigArg, 0xA5, sizeof(bigArg));
        XLINK_CHECK(XLinkSession_sendRequest(f->m_client, 43, bigArg,
                                             sizeof(bigArg), &idBig),
                    "发出超长载荷请求（129 字节照常发送）");
        xlink_fixture_step(f, 60);
        XLINK_CHECK(g_silentRequestCount == 1,
                    "超长载荷超时不重发（服务端只见过一次）");
        XLINK_CHECK(g_reqFinishedCount == 1 && g_reqFinishedOk == false
                        && g_reqFinishedCmd == 43,
                    "超长载荷首次超时即终局上报 false");
        XLINK_CHECK(!XLinkSession_isPending(f->m_client, idBig),
                    "超长载荷终局后槽已出表");

        /* 子场景三：空载荷也算可重试（快照 len=0），超时后重发空请求 */
        g_silentRequestCount = 0;
        g_reqFinishedCount = 0;
        XLINK_CHECK(XLinkSession_sendRequest(f->m_client, 44, NULL, 0,
                                             &idEmpty),
                    "发出零载荷请求（命令 44）");
        xlink_fixture_step(f, 60);
        XLINK_CHECK(g_silentRequestCount == 2,
                    "空载荷请求超时后静默重发（服务端收到两次）");
        XLINK_CHECK(g_reqFinishedCount == 0
                        && XLinkSession_isPending(f->m_client, idEmpty),
                    "空载荷重发成功不发终局、槽保持待应答");

        xlink_fixture_delete(f);
    }

    /* --- 子场景四：未 pump 过发出的请求（sendTick==0）自首次 pump 起算，
       不被首拍误判超时（回归：判定式 nowMs >= 0 恒真曾致刚发出的请求
       在异步首拍被立即重发/终局） --- */
    {
        XGuiRemoteLoopbackDevice* devA = NULL;
        XGuiRemoteLoopbackDevice* devB = NULL;
        XLinkSession* client = NULL;
        XLinkSession* server = NULL;
        uint8_t helloBuf[4];
        XLinkHelloPayload hello;
        uint8_t arg = 0x5A;
        uint32_t id = 0;
        devA = XGuiRemoteLoopbackDevice_createPair(8 * 1024, &devB);
        client = devA ? XLinkSession_create_ex((XIODevice*)devA) : NULL;
        server = devB ? XLinkSession_create_ex((XIODevice*)devB) : NULL;
        XLINK_CHECK(client != NULL && server != NULL,
                    "起算回归测试双会话创建成功");
        if (client && server) {
            XLinkSession_setDispatchCallback(server, xlink_silent_dispatch,
                                             NULL);
            /* 客户端手动发 Hello（sendSession 不经 pump，客户端 m_nowTick
               保持 0），服务端 pump 后转 Ready——客户端全程未 pump。 */
            hello.m_version = XLINK_VERSION;
            hello.m_capability = XLinkCapability_Zlib;
            XLinkCodec_encodeHello(&hello, helloBuf, sizeof(helloBuf), NULL);
            XLinkSession_sendSession(client, XLinkSessionCode_Hello,
                                     helloBuf, 4);
            XLinkSession_pump(server, 1000);
            XLINK_CHECK(XLinkSession_isReady(server),
                        "服务端经手动 Hello 握手就绪（客户端仍未 pump）");

            XLinkSession_setRequestTimeout(client, 500);
            XLinkSession_setNumberOfRetries(client, 0);
            g_silentRequestCount = 0;
            g_reqFinishedCount = 0;
            g_reqFinishedOk = true;
            XObject_connect_2((XObject*)client,
                              XSignal(XLinkSession_requestFinished_signal),
                              xlink_request_finished_slot);
            /* 关键时序：m_nowTick==0 时发出请求（sendTick==0），下一拍
               直接跳到远超阈值的大时刻（模拟异步模式真实单调毫秒）。 */
            XLINK_CHECK(XLinkSession_sendRequest(client, 42, &arg, 1, &id),
                        "未 pump 过的会话发出请求（sendTick==0）");
            XLinkSession_pump(client, 5000);
            XLINK_CHECK(g_reqFinishedCount == 0,
                        "首拍不对 sendTick==0 的请求误判超时（不误发终局）");
            XLINK_CHECK(XLinkSession_isPending(client, id),
                        "首拍后请求保持待应答（不误出表）");

            XLinkSession_pump(server, 5010);
            XLINK_CHECK(g_silentRequestCount == 1,
                        "服务端只收到一次请求（无首拍误重发的重复帧）");

            /* 自首拍（5000）起算：+600ms 越过 500ms 阈值，正常终局 */
            XLinkSession_pump(client, 5600);
            XLINK_CHECK(g_reqFinishedCount == 1 && g_reqFinishedOk == false
                            && g_reqFinishedCmd == 42,
                        "起算后越过阈值正常终局上报 false");
            XLINK_CHECK(!XLinkSession_isPending(client, id),
                        "正常终局后槽已出表");
            XLINK_CHECK(g_silentRequestCount == 1,
                        "终局前未发生任何重发（服务端仍只见过一次）");
        }
        if (client) XClassDelete((XClass*)client);
        if (server) XClassDelete((XClass*)server);
        if (devA) XClassDelete((XClass*)devA);
        if (devB) XClassDelete((XClass*)devB);
    }
}

/* ========================================================================== */
/*                        十四、客户端包装 XLinkClient                        */
/* ========================================================================== */

/**
 * @brief 覆盖分组十四：客户端包装 XLinkClient（真实 127.0.0.1 TCP）。
 * @details 模式照第十一节 xlink_test_server：本地 XLinkServer 固定监听
 *          47321 端口，processEvents 轮询驱动（事件全量派发 + 服务端手动
 *          pump 兜底），子分组：
 *
 *   一、NULL 防御（全部公开 API 对 NULL 的行为）
 *   二、默认值与栈上生命周期（init/deinit 配对、重连配置默认值）
 *   三、正常链路（connectToServer -> isReady -> 经 session() 发 PING 收回执）
 *   四、自动重连（服务端踢线 -> Reconnecting -> 原端口重听 -> 再次 isReady，
 *       stateChanged 信号计数校验）
 *   五、重连关（踢线后停在 UnconnectedState）
 *   六、手动断开（Goodbye 排空收尾，服务端 notify 收到 Removed；开着重连
 *       断开也不触发 Reconnecting）
 *   七、最大重连次数（达到 maxReconnectAttempts 后收手转 UnconnectedState）
 *
 * @note  原 Test/XProtocolTest/XLinkClientTest.c 已整体并入本分组；由
 *        XLinkTest_run 全量回归与 XLinkClientTest_run 单独入口共用，运行
 *        入口 `--test xlink-client` / Test 命令 `Test xlink-client`。
 */

/** @brief 本分组专用固定端口（避免与 xlink-demo 的 45200 冲突）。 */
#define XCLIENT_TEST_PORT 47321u

/** @brief 测试业务命令号：PING（服务端同步回 1 字节 0x5A 回执）。 */
#define XCLIENT_CMD_PING 0x00A1u

/** @brief 当前被测客户端（等待条件闭包用；单流程，文件域即可）。 */
static XLinkClient* g_client = NULL;
/** @brief 客户端 stateChanged 信号发射次数（与第十三分组会话信号观测相区分）。 */
static int g_clientStateChangedCount = 0;
/** @brief 最近一次 stateChanged 携带的状态。 */
static XLinkClientState g_lastState = XLinkClient_UnconnectedState;
/** @brief 是否进入过 ReconnectingState。 */
static bool g_sawReconnecting = false;
/** @brief 客户端收到 0x5A 回执数。 */
static int g_pingResponses = 0;
/** @brief 服务端会话 Added/Removed 通知计数。 */
static int g_srvAdded = 0;
static int g_srvRemoved = 0;
/** @brief 服务端分发回调收到的请求数。 */
static int g_srvRequests = 0;

/**
 * @brief 客户端 stateChanged 信号槽：计数并落账最新状态。
 */
static void xlink_client_test_on_state(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver)
        return;
    g_clientStateChangedCount++;
    g_lastState = XLinkClient_state((XLinkClient*)receiver);
    if (g_lastState == XLinkClient_ReconnectingState)
        g_sawReconnecting = true;
}

/**
 * @brief 服务端会话增删通知：统计 Added/Removed。
 */
static void xlink_client_test_srv_notify(XLinkServer* self,
                                         XLinkSession* session,
                                         XLinkServerSessionEvent event)
{
    (void)self;
    (void)session;
    if (event == XLinkServerSessionEvent_Added)
        g_srvAdded++;
    else
        g_srvRemoved++;
}

/**
 * @brief 服务端分发回调：请求即回 1 字节 0x5A 回显应答。
 */
static void xlink_client_test_srv_dispatch(XLinkSession* self,
                                           const XLinkFrameHeader* frame,
                                           const uint8_t* payload)
{
    if (!self || !frame)
        return;
    if (frame->m_msgClass == XLinkMsgClass_Request) {
        uint8_t echo = (payload && frame->m_payloadLen > 0) ? payload[0] : 0;
        g_srvRequests++;
        XLinkSession_sendResponse(self, frame->m_requestId, frame->m_msgCode,
                                  &echo, 1);
    }
}

/**
 * @brief 客户端分发回调（会话模板）：统计 PING 0x5A 回执。
 */
static void xlink_client_test_cli_dispatch(XLinkSession* self,
                                           const XLinkFrameHeader* frame,
                                           const uint8_t* payload)
{
    (void)self;
    if (frame && frame->m_msgClass == XLinkMsgClass_Response
        && frame->m_msgCode == XCLIENT_CMD_PING
        && payload && frame->m_payloadLen >= 1 && payload[0] == 0x5A)
        g_pingResponses++;
}

/** @brief 等待条件：客户端已 Ready。 */
static bool xlink_client_test_cond_ready(void)
{
    return g_client && XLinkClient_isReady(g_client);
}

/** @brief 等待条件：客户端进入 ReconnectingState。 */
static bool xlink_client_test_cond_reconnecting(void)
{
    return g_client
           && XLinkClient_state(g_client) == XLinkClient_ReconnectingState;
}

/** @brief 等待条件：客户端回到 UnconnectedState。 */
static bool xlink_client_test_cond_unconnected(void)
{
    return g_client
           && XLinkClient_state(g_client) == XLinkClient_UnconnectedState;
}

/** @brief 等待条件：PING 回执已到达。 */
static bool xlink_client_test_cond_ping(void)
{
    return g_pingResponses >= 1;
}

/** @brief 等待条件：服务端已收到 Removed 通知。 */
static bool xlink_client_test_cond_removed(void)
{
    return g_srvRemoved >= 1;
}

/**
 * @brief 有界 processEvents 泵：条件成立或超时即返回。
 * @note  照 xlink_test_server 口径：事件全量派发（套接字/内建定时器驱动）+
 *        服务端手动 pump 兜底；pump 的 nowMs 全程用实时单调毫秒，不混入
 *        假时钟污染静默判线时间轴。
 */
static bool xlink_client_test_wait(bool (*cond)(void), uint32_t ms,
                                   XLinkServer* srv)
{
    uint64_t start = XDateTime_currentMSecsSinceEpoch();
    while (!cond()) {
        if (XDateTime_currentMSecsSinceEpoch() - start >= ms)
            return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (srv)
            XLinkServer_pump(srv,
                             (uint32_t)XDateTime_currentMSecsSinceEpoch());
        XThread_msleep(5);
    }
    return true;
}

/**
 * @brief 重置全部落账计数（沿用原 XLinkClientTest.c 的用例前清零口径）。
 */
static void xlink_client_test_reset(void)
{
    g_client = NULL;
    g_clientStateChangedCount = 0;
    g_lastState = XLinkClient_UnconnectedState;
    g_sawReconnecting = false;
    g_pingResponses = 0;
    g_srvAdded = 0;
    g_srvRemoved = 0;
    g_srvRequests = 0;
}

/**
 * @brief 创建已配好地址/模板/信号计数的被测客户端。
 * @param autoReconnect 自动重连开关。
 * @param intervalMs 重连间隔（毫秒）。
 * @param maxAttempts 最大重连次数。
 * @return 成功返回客户端指针；失败返回 NULL（调用方负责检查）。
 */
static XLinkClient* xlink_client_test_make_client(bool autoReconnect,
                                                  uint32_t intervalMs,
                                                  int16_t maxAttempts)
{
    XLinkClient* client = XLinkClient_create();
    if (!client)
        return NULL;
    XLinkClient_setHostPort(client, "127.0.0.1", XCLIENT_TEST_PORT);
    XLinkClient_setCapability(client, XLinkCapability_Zlib);
    XLinkClient_setDispatchCallback(client, xlink_client_test_cli_dispatch,
                                    NULL);
    XLinkClient_setAutoReconnect(client, autoReconnect);
    XLinkClient_setReconnectInterval(client, intervalMs);
    XLinkClient_setMaxReconnectAttempts(client, maxAttempts);
    XObject_connect_1((XObject*)client,
                      XSignal(XLinkClient_stateChanged_signal),
                      (XObject*)client, xlink_client_test_on_state,
                      XConnectionType_Auto);
    return client;
}

/**
 * @brief 创建监听 47321 的服务端（套回调模板）。
 * @return 成功返回服务端指针；监听失败返回 NULL。
 */
static XLinkServer* xlink_client_test_make_server(void)
{
    XLinkServer* srv = XLinkServer_create();
    if (!srv)
        return NULL;
    XLinkServer_setCapability(srv, XLinkCapability_Zlib);
    XLinkServer_setDispatchCallback(srv, xlink_client_test_srv_dispatch, NULL);
    XLinkServer_setNotifyCallback(srv, xlink_client_test_srv_notify);
    if (!XLinkServer_listen_base(srv, "127.0.0.1", XCLIENT_TEST_PORT)) {
        XClassDelete((XClass*)srv);
        return NULL;
    }
    return srv;
}

/**
 * @brief 子分组一：NULL 防御（全部公开 API 对 NULL 的行为）。
 */
static void xlink_client_test_null_defense(void)
{
    XLINK_SECTION("十四·一、客户端包装 NULL 防御");
    XLINK_CHECK(!XLinkClient_setHostPort(NULL, "127.0.0.1", 1),
                "setHostPort 对 NULL 拒绝");
    XLINK_CHECK(XLinkClient_host_const(NULL) == NULL,
                "host_const 对 NULL 返回 NULL");
    XLINK_CHECK(XLinkClient_port(NULL) == 0, "port 对 NULL 返回 0");
    XLINK_CHECK(!XLinkClient_setCapability(NULL, XLinkCapability_Zlib),
                "setCapability 对 NULL 拒绝");
    XLINK_CHECK(!XLinkClient_setDispatchCallback(NULL, NULL, NULL),
                "setDispatchCallback 对 NULL 拒绝");
    XLINK_CHECK(!XLinkClient_setAutoReconnect(NULL, true),
                "setAutoReconnect 对 NULL 拒绝");
    XLINK_CHECK(!XLinkClient_autoReconnect(NULL),
                "autoReconnect 对 NULL 返回 false");
    XLINK_CHECK(!XLinkClient_setReconnectInterval(NULL, 1),
                "setReconnectInterval 对 NULL 拒绝");
    XLINK_CHECK(XLinkClient_reconnectInterval(NULL) == 1000,
                "reconnectInterval 对 NULL 返回默认 1000");
    XLINK_CHECK(!XLinkClient_setMaxReconnectAttempts(NULL, 1),
                "setMaxReconnectAttempts 对 NULL 拒绝");
    XLINK_CHECK(XLinkClient_maxReconnectAttempts(NULL) == -1,
                "maxReconnectAttempts 对 NULL 返回默认 -1");
    XLINK_CHECK(XLinkClient_reconnectAttempts(NULL) == 0,
                "reconnectAttempts 对 NULL 返回 0");
    XLINK_CHECK(!XLinkClient_connectToServer(NULL),
                "connectToServer 对 NULL 拒绝");
    XLinkClient_disconnectFromServer(NULL);
    XLINK_CHECK(1, "disconnectFromServer 对 NULL 安全空操作");
    XLINK_CHECK(XLinkClient_session(NULL) == NULL,
                "session 对 NULL 返回 NULL");
    XLINK_CHECK(XLinkClient_state(NULL) == XLinkClient_UnconnectedState,
                "state 对 NULL 返回 Unconnected");
    XLINK_CHECK(!XLinkClient_isReady(NULL), "isReady 对 NULL 返回 false");
    (void)XLinkClient_stateChanged_signal(NULL, XLinkClient_UnconnectedState);
    XLINK_CHECK(1, "stateChanged_signal 对 NULL 安全不发射");
}

/**
 * @brief 子分组二：默认值与栈上生命周期（init/deinit 配对、重连配置默认值）。
 */
static void xlink_client_test_defaults(void)
{
    XLINK_SECTION("十四·二、客户端包装 默认值与栈上生命周期");
    {
        XLinkClient stack;
        XLinkClient_init(&stack);
        XLINK_CHECK(XLinkClient_state(&stack)
                        == XLinkClient_UnconnectedState,
                    "栈上 init 初始为 UnconnectedState");
        XLINK_CHECK(!XLinkClient_autoReconnect(&stack),
                    "autoReconnect 默认 false");
        XLINK_CHECK(XLinkClient_reconnectInterval(&stack) == 1000,
                    "reconnectInterval 默认 1000ms");
        XLINK_CHECK(XLinkClient_maxReconnectAttempts(&stack) == -1,
                    "maxReconnectAttempts 默认 -1（无限）");
        XLINK_CHECK(XLinkClient_reconnectAttempts(&stack) == 0,
                    "reconnectAttempts 初始为 0");
        XLINK_CHECK(XLinkClient_session(&stack) == NULL,
                    "初始无会话");
        XLINK_CHECK(!XLinkClient_isReady(&stack),
                    "初始 isReady 为 false");
        XLINK_CHECK(!XLinkClient_connectToServer(&stack),
                    "未设置主机名时连接拒绝");
        XLINK_CHECK(XLinkClient_setHostPort(&stack, "127.0.0.1",
                                            XCLIENT_TEST_PORT),
                    "setHostPort 成功");
        XLINK_CHECK(strcmp(XLinkClient_host_const(&stack),
                           "127.0.0.1") == 0,
                    "host 经 XString 深拷贝可读回");
        XLINK_CHECK(XLinkClient_port(&stack) == XCLIENT_TEST_PORT,
                    "port 回读一致");
        XLINK_CHECK(!XLinkClient_setHostPort(&stack, NULL, 1),
                    "host 为 NULL 时拒绝且不修改原配置");
        XLINK_CHECK(strcmp(XLinkClient_host_const(&stack),
                           "127.0.0.1") == 0,
                    "拒绝后原配置保持不变");
        XClassDeinit(&stack);
        XLINK_CHECK(1, "栈上 init/deinit 配对无崩溃");
    }
    {
        /* 堆对象生命周期：create + XClassDelete（未连接直接销毁）。 */
        XLinkClient* heap = XLinkClient_create();
        XLINK_CHECK(heap != NULL, "堆创建成功");
        XLINK_CHECK(XLinkClient_state(heap) == XLinkClient_UnconnectedState,
                    "堆对象初始为 UnconnectedState");
        XClassDelete((XClass*)heap);
        XLINK_CHECK(1, "堆对象销毁无崩溃");
    }
}

/**
 * @brief 子分组三：正常链路（连接/握手/PING 收回执）。
 */
static void xlink_client_test_normal_link(void)
{
    XLINK_SECTION("十四·三、客户端包装 正常链路（连接/握手/PING 收回执）");
    xlink_client_test_reset();
    {
        XLinkServer* srv = xlink_client_test_make_server();
        XLinkSession* session = NULL;
        uint32_t reqId = 0;
        uint8_t arg = 0x5A;

        XLINK_CHECK(srv != NULL, "服务端监听 127.0.0.1:47321 成功");
        if (!srv)
            return;

        g_client = xlink_client_test_make_client(false, 1000, -1);
        XLINK_CHECK(g_client != NULL, "客户端堆创建成功");
        if (!g_client) {
            XClassDelete((XClass*)srv);
            return;
        }

        XLINK_CHECK(XLinkClient_connectToServer(g_client),
                    "connectToServer 异步发起成功");
        XLINK_CHECK(XLinkClient_state(g_client)
                        == XLinkClient_ConnectingState,
                    "发起后进入 ConnectingState");
        XLINK_CHECK(xlink_client_test_wait(xlink_client_test_cond_ready,
                                           3000, srv),
                    "3s 内握手完成（isReady）");
        XLINK_CHECK(XLinkClient_state(g_client)
                        == XLinkClient_ConnectedState,
                    "Ready 即 ConnectedState");
        session = XLinkClient_session(g_client);
        XLINK_CHECK(session != NULL, "session() 借用指针非空");
        XLINK_CHECK(session
                        && (XLinkSession_negotiatedCapability(session)
                            & XLinkCapability_Zlib),
                    "能力位模板随自动 Hello 协商生效（Zlib）");

        /* 经 session() 发 PING，服务端模板回调同步回执 */
        if (session && XLinkSession_isReady(session)) {
            XLinkSession_sendRequest(session, XCLIENT_CMD_PING, &arg, 1,
                                     &reqId);
            XLINK_CHECK(xlink_client_test_wait(
                            xlink_client_test_cond_ping, 2000, srv),
                        "经 session() 发 PING 收到 0x5A 回执");
            XLINK_CHECK(g_srvRequests == 1, "服务端模板回调收到请求");
        }

        XLINK_CHECK(g_clientStateChangedCount >= 2,
                    "stateChanged 至少发射 Unconnected->Connecting->Connected");
        XLINK_CHECK(g_lastState == XLinkClient_ConnectedState,
                    "最新状态为 ConnectedState");

        /* 收尾：手动断开（本用例不校验服务端 Removed，留给子分组六） */
        XLinkClient_disconnectFromServer(g_client);
        XLINK_CHECK(XLinkClient_state(g_client)
                        == XLinkClient_UnconnectedState,
                    "断开后回到 UnconnectedState");
        XLINK_CHECK(XLinkClient_session(g_client) == NULL,
                    "断开后会话已拆除");
        XClassDelete((XClass*)g_client);
        g_client = NULL;
        XLinkServer_close(srv);
        XClassDelete((XClass*)srv);
        XLINK_CHECK(1, "子分组三资源收尾无崩溃");
    }
}

/**
 * @brief 子分组四：自动重连（踢线 -> Reconnecting -> 重听 -> 再 Ready）。
 */
static void xlink_client_test_auto_reconnect(void)
{
    XLINK_SECTION("十四·四、客户端包装 自动重连（踢线->Reconnecting->重听->再Ready）");
    xlink_client_test_reset();
    {
        XLinkServer* srv = xlink_client_test_make_server();
        uint32_t reqId = 0;
        uint8_t arg = 0x5A;
        int addedBefore = 0;

        XLINK_CHECK(srv != NULL, "服务端监听 127.0.0.1:47321 成功");
        if (!srv)
            return;

        g_client = xlink_client_test_make_client(true, 150, -1);
        if (!g_client) {
            XClassDelete((XClass*)srv);
            XLINK_CHECK(0, "客户端堆创建成功");
            return;
        }

        XLINK_CHECK(XLinkClient_connectToServer(g_client),
                    "connectToServer 发起成功");
        XLINK_CHECK(xlink_client_test_wait(xlink_client_test_cond_ready,
                                           3000, srv),
                    "首次连接握手完成");
        addedBefore = g_srvAdded;
        XLINK_CHECK(addedBefore == 1, "服务端收到首次 Added 通知");

        /* 服务端踢线：XTcpServer_close 只关监听 fd，已 accept 的连接套接字
           不被关闭、客户端收不到 FIN（XTcpServer.c close 语义），故照用例
           七口径 close 后连服务端对象一起删除，客户端才观测到链路断开。 */
        XLinkServer_close(srv);
        XClassDelete((XClass*)srv);
        srv = NULL;
        XLINK_CHECK(xlink_client_test_wait(
                        xlink_client_test_cond_reconnecting, 2000, NULL),
                    "踢线后进入 ReconnectingState");
        XLINK_CHECK(XLinkClient_session(g_client) == NULL,
                    "重连等待期旧会话已拆除");
        XLINK_CHECK(g_sawReconnecting, "stateChanged 信号报告过 Reconnecting");

        /* 原端口重听：旧服务端对象已删除，新建服务端占回 47321；
           客户端按 150ms 周期重试，服务端起来即恢复链路 */
        srv = xlink_client_test_make_server();
        XLINK_CHECK(srv != NULL, "原端口重新监听成功");
        XLINK_CHECK(xlink_client_test_wait(xlink_client_test_cond_ready,
                                           5000, srv),
                    "重连后再次握手完成（isReady）");
        XLINK_CHECK(g_srvAdded == addedBefore + 1,
                    "服务端收到重连 Added 通知");
        XLINK_CHECK(XLinkClient_reconnectAttempts(g_client) == 0,
                    "重连成功后重连计数清零");
        XLINK_CHECK(g_clientStateChangedCount >= 4,
                    "stateChanged 经历 Connecting/Connected/Reconnecting/Connected");
        XLINK_CHECK(g_lastState == XLinkClient_ConnectedState,
                    "最新状态回到 ConnectedState");

        /* 重连后的链路可正常收发：载荷带 0x5A，服务端模板回显后客户端
           分发回调才认账（服务端回显 payload[0]，空载荷回 0 不计入）。 */
        if (XLinkClient_isReady(g_client)) {
            g_pingResponses = 0;
            g_srvRequests = 0;
            XLinkSession_sendRequest(XLinkClient_session(g_client),
                                     XCLIENT_CMD_PING, &arg, 1, &reqId);
            XLINK_CHECK(xlink_client_test_wait(
                            xlink_client_test_cond_ping, 2000, srv),
                        "重连后链路 PING 收到回执");
        }

        XLinkClient_disconnectFromServer(g_client);
        XClassDelete((XClass*)g_client);
        g_client = NULL;
        if (srv)
            XClassDelete((XClass*)srv);
        XLINK_CHECK(1, "子分组四资源收尾无崩溃");
    }
}

/**
 * @brief 子分组五：重连关（踢线后停在 UnconnectedState）。
 */
static void xlink_client_test_no_reconnect(void)
{
    XLINK_SECTION("十四·五、客户端包装 重连关（踢线后停在 UnconnectedState）");
    xlink_client_test_reset();
    {
        XLinkServer* srv = xlink_client_test_make_server();

        XLINK_CHECK(srv != NULL, "服务端监听 127.0.0.1:47321 成功");
        if (!srv)
            return;

        /* 默认 autoReconnect=false */
        g_client = xlink_client_test_make_client(false, 150, -1);
        if (!g_client) {
            XClassDelete((XClass*)srv);
            XLINK_CHECK(0, "客户端堆创建成功");
            return;
        }
        XLINK_CHECK(!XLinkClient_autoReconnect(g_client),
                    "默认自动重连关闭");

        XLINK_CHECK(XLinkClient_connectToServer(g_client),
                    "connectToServer 发起成功");
        XLINK_CHECK(xlink_client_test_wait(xlink_client_test_cond_ready,
                                           3000, srv),
                    "首次连接握手完成");

        /* 踢线同子分组四：close 只停听，须删服务端对象才断开已 accept 连接 */
        XLinkServer_close(srv);
        XClassDelete((XClass*)srv);
        srv = NULL;
        XLINK_CHECK(xlink_client_test_wait(
                        xlink_client_test_cond_unconnected, 2000, NULL),
                    "踢线后回到 UnconnectedState");
        XLINK_CHECK(XLinkClient_session(g_client) == NULL,
                    "会话已拆除");
        XLINK_CHECK(!g_sawReconnecting, "全程未进入 ReconnectingState");
        XLINK_CHECK(g_lastState == XLinkClient_UnconnectedState,
                    "最新状态为 UnconnectedState");

        XClassDelete((XClass*)g_client);
        g_client = NULL;
        XLINK_CHECK(1, "子分组五资源收尾无崩溃");
    }
}

/**
 * @brief 子分组六：手动断开（Goodbye 收尾，服务端收到 Removed）。
 */
static void xlink_client_test_manual_disconnect(void)
{
    XLINK_SECTION("十四·六、客户端包装 手动断开（Goodbye 收尾，服务端收到 Removed）");
    xlink_client_test_reset();
    {
        XLinkServer* srv = xlink_client_test_make_server();

        XLINK_CHECK(srv != NULL, "服务端监听 127.0.0.1:47321 成功");
        if (!srv)
            return;

        /* 特意开自动重连：验证手动 disconnectFromServer 不触发重连 */
        g_client = xlink_client_test_make_client(true, 150, -1);
        if (!g_client) {
            XClassDelete((XClass*)srv);
            XLINK_CHECK(0, "客户端堆创建成功");
            return;
        }

        XLINK_CHECK(XLinkClient_connectToServer(g_client),
                    "connectToServer 发起成功");
        XLINK_CHECK(xlink_client_test_wait(xlink_client_test_cond_ready,
                                           3000, srv),
                    "连接握手完成");
        XLINK_CHECK(g_srvAdded == 1, "服务端收到 Added 通知");

        XLinkClient_disconnectFromServer(g_client);
        XLINK_CHECK(XLinkClient_state(g_client)
                        == XLinkClient_UnconnectedState,
                    "断开后立即回到 UnconnectedState");
        XLINK_CHECK(XLinkClient_session(g_client) == NULL,
                    "会话已拆除");
        XLINK_CHECK(xlink_client_test_wait(xlink_client_test_cond_removed,
                                           2000, srv),
                    "服务端 notify 收到 Removed（Goodbye 判活回收）");
        XLINK_CHECK(xlink_client_test_wait(
                        xlink_client_test_cond_unconnected, 500, NULL),
                    "等待期保持 UnconnectedState（手动断开不重连）");
        XLINK_CHECK(!g_sawReconnecting, "手动断开未进入 ReconnectingState");

        XClassDelete((XClass*)g_client);
        g_client = NULL;
        XLinkServer_close(srv);
        XClassDelete((XClass*)srv);
        XLINK_CHECK(1, "子分组六资源收尾无崩溃");
    }
}

/**
 * @brief 子分组七：最大重连次数（达到上限收手转 Unconnected）。
 */
static void xlink_client_test_max_attempts(void)
{
    XLINK_SECTION("十四·七、客户端包装 最大重连次数（达到上限收手转 Unconnected）");
    xlink_client_test_reset();
    {
        XLinkServer* srv = xlink_client_test_make_server();

        XLINK_CHECK(srv != NULL, "服务端监听 127.0.0.1:47321 成功");
        if (!srv)
            return;

        g_client = xlink_client_test_make_client(true, 120, 2);
        if (!g_client) {
            XClassDelete((XClass*)srv);
            XLINK_CHECK(0, "客户端堆创建成功");
            return;
        }

        XLINK_CHECK(XLinkClient_connectToServer(g_client),
                    "connectToServer 发起成功");
        XLINK_CHECK(xlink_client_test_wait(xlink_client_test_cond_ready,
                                           3000, srv),
                    "首次连接握手完成");

        /* 服务端永久下线：最多再试 2 次（约 2×120ms）即收手 */
        XLinkServer_close(srv);
        XClassDelete((XClass*)srv);
        srv = NULL;
        XLINK_CHECK(xlink_client_test_wait(
                        xlink_client_test_cond_reconnecting, 2000, NULL),
                    "踢线后进入 ReconnectingState");
        XLINK_CHECK(xlink_client_test_wait(
                        xlink_client_test_cond_unconnected, 5000, NULL),
                    "达到最大重连次数后转 UnconnectedState 收手");
        XLINK_CHECK(XLinkClient_session(g_client) == NULL,
                    "收手后会话与套接字已拆除");
        XLINK_CHECK(g_sawReconnecting, "重连流程确实经历过");

        XClassDelete((XClass*)g_client);
        g_client = NULL;
        XLINK_CHECK(1, "子分组七资源收尾无崩溃");
    }
}

/* ========================================================================== */
/*                        十五、能力位全矩阵                                   */
/* ========================================================================== */

/** @brief 分组十五参数化能力位表（与 xlink_cap_matrix_names 一一对应）。 */
static const uint16_t xlink_cap_matrix_bits[] = {
    XLinkCapability_Zlib,
    XLinkCapability_Tls,
    XLinkCapability_Snapshot,
    XLinkCapability_RequestTimeout
};

/** @brief 分组十五参数化能力位名（与 xlink_cap_matrix_bits 一一对应）。 */
static const char* const xlink_cap_matrix_names[] = {
    "Zlib", "Tls", "Snapshot", "RequestTimeout"
};

/** @brief 参数化能力位个数（bits/names 两表等长）。 */
static const int xlink_cap_matrix_count =
    (int)(sizeof(xlink_cap_matrix_bits) / sizeof(xlink_cap_matrix_bits[0]));

/**
 * @brief 覆盖分组十五：XLinkCapability 四个能力位（Zlib/Tls/Snapshot/
 *        RequestTimeout）的协商全矩阵——双侧不置位（None）、对称置位、
 *        单侧置位（两个方向）、全组合；HelloAck 附加快照（4+N 超长载荷）
 *        的容忍语义；RequestTimeout 位不干扰请求自动超时路径。
 * @note  能力协商是纯按位与（交集 = 对端声明 & 本端声明，见
 *        XLinkSession.c onFrame 的 Hello/HelloAck 分支），与传输介质无关，
 *        统一走回环夹具 + 假时钟推进（与分组四同口径）。
 *        negotiatedCapability(NULL) 的防御断言已见于分组十，此处不重复。
 */
static void xlink_test_capability_matrix(void)
{
    char desc[160];
    uint16_t full = (uint16_t)(XLinkCapability_Zlib | XLinkCapability_Tls
                               | XLinkCapability_Snapshot
                               | XLinkCapability_RequestTimeout);
    int i;
    int dir;

    XLINK_SECTION("十五、能力位全矩阵（None/对称/不对称/全组合/快照/超时语义）");

    /* --- 子用例一：None——双方均不置位，握手正常就绪且双侧交集为 0 --- */
    {
        XLinkTestFixture* f = xlink_fixture_create();
        if (!f) {
            XLINK_CHECK(false, "None 用例夹具搭建成功");
            return;
        }
        /* 服务端显式 setCapability(None)，客户端完全不调（init 清零默认
           为 0）：两种「不置位」写法在本用例中等价覆盖。 */
        XLINK_CHECK(XLinkSession_setCapability(f->m_server,
                                               XLinkCapability_None),
                    "setCapability(None) 显式声明无能力成功");
        xlink_fixture_step(f, 3); /* 自动握手 */
        XLINK_CHECK(XLinkSession_isReady(f->m_server)
                        && XLinkSession_isReady(f->m_client),
                    "双侧均不置位时自动握手正常就绪");
        XLINK_CHECK(XLinkSession_negotiatedCapability(f->m_server)
                        == XLinkCapability_None,
                    "双侧不置位：服务端能力交集为 0（None）");
        XLINK_CHECK(XLinkSession_negotiatedCapability(f->m_client)
                        == XLinkCapability_None,
                    "双侧不置位：客户端能力交集为 0（None）");
        xlink_fixture_delete(f);
    }

    /* --- 子用例二：对称——每个位单独双侧同置，交集恰为该位 --- */
    for (i = 0; i < xlink_cap_matrix_count; i++) {
        uint16_t bit = xlink_cap_matrix_bits[i];
        XLinkTestFixture* f = xlink_fixture_create();
        if (!f) {
            XLINK_CHECK(false, "对称用例夹具搭建成功");
            return;
        }
        XLinkSession_setCapability(f->m_server, bit);
        XLinkSession_setCapability(f->m_client, bit);
        xlink_fixture_step(f, 3); /* 自动握手 */
        XSnprintf(desc, sizeof(desc), "对称置位 %s：自动握手正常就绪",
                  xlink_cap_matrix_names[i]);
        XLINK_CHECK(XLinkSession_isReady(f->m_server)
                        && XLinkSession_isReady(f->m_client), desc);
        XSnprintf(desc, sizeof(desc), "对称置位 %s：服务端能力交集恰为 %s",
                  xlink_cap_matrix_names[i], xlink_cap_matrix_names[i]);
        XLINK_CHECK(XLinkSession_negotiatedCapability(f->m_server) == bit,
                    desc);
        XSnprintf(desc, sizeof(desc), "对称置位 %s：客户端能力交集恰为 %s",
                  xlink_cap_matrix_names[i], xlink_cap_matrix_names[i]);
        XLINK_CHECK(XLinkSession_negotiatedCapability(f->m_client) == bit,
                    desc);
        xlink_fixture_delete(f);
    }

    /* --- 子用例三：不对称——单侧置位，双侧交集均不含该位
           （按位与语义：单侧声明 & 对端 0 = 0，两个方向结论一致） --- */
    for (dir = 0; dir < 2; dir++) {
        const char* setterName = (dir == 0) ? "服务端" : "客户端";
        for (i = 0; i < xlink_cap_matrix_count; i++) {
            uint16_t bit = xlink_cap_matrix_bits[i];
            XLinkSession* setter;
            XLinkSession* other;
            XLinkTestFixture* f = xlink_fixture_create();
            if (!f) {
                XLINK_CHECK(false, "不对称用例夹具搭建成功");
                return;
            }
            setter = (dir == 0) ? f->m_server : f->m_client;
            other = (dir == 0) ? f->m_client : f->m_server;
            XLinkSession_setCapability(setter, bit);
            xlink_fixture_step(f, 3); /* 自动握手 */
            XSnprintf(desc, sizeof(desc),
                      "不对称（仅%s置位 %s）：握手正常就绪",
                      setterName, xlink_cap_matrix_names[i]);
            XLINK_CHECK(XLinkSession_isReady(f->m_server)
                            && XLinkSession_isReady(f->m_client), desc);
            XSnprintf(desc, sizeof(desc),
                      "不对称（仅%s置位 %s）：置位侧交集不含该位",
                      setterName, xlink_cap_matrix_names[i]);
            XLINK_CHECK(XLinkSession_negotiatedCapability(setter) == 0,
                        desc);
            XSnprintf(desc, sizeof(desc),
                      "不对称（仅%s置位 %s）：未置位侧交集同样不含该位",
                      setterName, xlink_cap_matrix_names[i]);
            XLINK_CHECK(XLinkSession_negotiatedCapability(other) == 0,
                        desc);
            xlink_fixture_delete(f);
        }
    }

    /* --- 子用例四：全组合——双侧同置全掩码，交集为全掩码 --- */
    {
        XLinkTestFixture* f = xlink_fixture_create();
        if (!f) {
            XLINK_CHECK(false, "全组合用例夹具搭建成功");
            return;
        }
        XLINK_CHECK(XLinkSession_setCapability(f->m_server, full),
                    "服务端声明全掩码（四能力位按位或）成功");
        XLINK_CHECK(XLinkSession_setCapability(f->m_client, full),
                    "客户端声明全掩码（四能力位按位或）成功");
        xlink_fixture_step(f, 3); /* 自动握手 */
        XLINK_CHECK(XLinkSession_isReady(f->m_server)
                        && XLinkSession_isReady(f->m_client),
                    "全掩码对称声明时自动握手正常就绪");
        XLINK_CHECK(XLinkSession_negotiatedCapability(f->m_server) == full,
                    "全组合：服务端能力交集为全掩码（四位全保留）");
        XLINK_CHECK(XLinkSession_negotiatedCapability(f->m_client) == full,
                    "全组合：客户端能力交集为全掩码（四位全保留）");
        xlink_fixture_delete(f);
    }

    /* --- 子用例五：Snapshot 深度语义——HelloAck 载荷 = hello(4B) +
           伪快照(N=64)。约定：HelloAck 可携带全量快照，长度由帧头
           payloadLen 推导，解析侧只读前 4 字节 hello 前缀。
           手动改道说明：自动 HelloAck 的触发条件是「本端 pump 消费到
           对端 Hello」即回（XLinkSession.c onFrame 的 Hello 分支），
           无从抑制；可行路径是让服务端在消费 Hello 之前先手动回
           HelloAck——客户端收到的手动 HelloAck（4+N）与服务端随后补发
           的自动 HelloAck（4B）并存无害，握手照常完成，按此路径验证。 */
    {
        /* Codec 层前置：decodeHello 对 4+N 超长载荷返回 true 且只读前 4 字节 */
        {
            uint8_t buf[4 + 64];
            XLinkHelloPayload src;
            XLinkHelloPayload dst;
            memset(&src, 0, sizeof(src));
            memset(&dst, 0, sizeof(dst));
            src.m_version = XLINK_VERSION;
            src.m_capability = (uint16_t)(XLinkCapability_Zlib
                                          | XLinkCapability_Snapshot);
            XLINK_CHECK(XLinkCodec_encodeHello(&src, buf, 4, NULL),
                        "encodeHello 编出 4 字节 hello 前缀");
            memset(&buf[4], 0xA5, 64); /* 伪快照字节 */
            XLINK_CHECK(XLinkCodec_decodeHello(buf, sizeof(buf), &dst),
                        "decodeHello 对 4+64 超长 Hello 载荷返回 true"
                        "（快照附加字节不破坏解析）");
            XLINK_CHECK(dst.m_version == XLINK_VERSION
                            && dst.m_capability == src.m_capability,
                        "超长载荷仅读前 4 字节：版本与能力位解析正确");
        }

        /* 会话层：服务端手动 HelloAck（4+64）应答客户端自动 Hello */
        {
            XLinkTestFixture* f = xlink_fixture_create();
            uint8_t snapBuf[4 + 64];
            XLinkHelloPayload hello;
            if (!f) {
                XLINK_CHECK(false, "快照语义用例夹具搭建成功");
                return;
            }
            XLinkSession_setDispatchCallback(f->m_server,
                                             xlink_server_dispatch, NULL);
            XLinkSession_setDispatchCallback(f->m_client,
                                             xlink_client_dispatch, NULL);
            XLinkSession_setCapability(f->m_server, full);
            XLinkSession_setCapability(f->m_client, full);

            /* 客户端先驱动：自动补发 Hello（帧落在服务端接收环，尚未被消费） */
            XLinkSession_pump(f->m_client, f->m_now);
            f->m_now += 10;

            /* 服务端此刻不 pump（不触发自动 HelloAck），手动改道：
               以 4+64 载荷回 HelloAck（hello 前缀 + 伪快照字节） */
            hello.m_version = XLINK_VERSION;
            hello.m_capability = full;
            XLINK_CHECK(XLinkCodec_encodeHello(&hello, snapBuf, 4, NULL),
                        "手动 HelloAck 的 hello 前缀编码成功");
            memset(&snapBuf[4], 0xA5, 64);
            XLINK_CHECK(XLinkSession_sendSession(f->m_server,
                                                 XLinkSessionCode_HelloAck,
                                                 snapBuf,
                                                 (uint32_t)sizeof(snapBuf)),
                        "服务端手动发出携带 4+64 伪快照的 HelloAck");

            /* 客户端消费手动 HelloAck：解析只读前 4 字节，握手照常完成 */
            XLinkSession_pump(f->m_client, f->m_now);
            f->m_now += 10;
            XLINK_CHECK(XLinkSession_isReady(f->m_client),
                        "客户端经手动 HelloAck（含伪快照）完成握手转 Ready");
            XLINK_CHECK((XLinkSession_negotiatedCapability(f->m_client)
                             & XLinkCapability_Snapshot) != 0,
                        "客户端能力交集含 Snapshot");

            /* 服务端补驱动：消费客户端 Hello 转 Ready，并补发一帧 4 字节
               自动 HelloAck（与手动 HelloAck 并存，客户端幂等消化） */
            XLinkSession_pump(f->m_server, f->m_now);
            f->m_now += 10;
            XLINK_CHECK(XLinkSession_isReady(f->m_server),
                        "服务端随后消费 Hello 完成握手转 Ready");
            XLINK_CHECK((XLinkSession_negotiatedCapability(f->m_server)
                             & XLinkCapability_Snapshot) != 0,
                        "服务端能力交集含 Snapshot");

            xlink_fixture_step(f, 4); /* 消化互发的自动 Hello/HelloAck */
            XLINK_CHECK(XLinkSession_isReady(f->m_server)
                            && XLinkSession_isReady(f->m_client),
                        "手动与自动握手帧并存收敛后链路保持 Ready");

            /* 后续业务帧收发不受快照附加字节影响：请求-应答 + 推送各验一条 */
            {
                uint8_t arg = 0x5A;
                uint32_t id = 0;
                int respBase = g_clientResponseCount;
                int pubBase = g_clientPublishCount;
                XLINK_CHECK(XLinkSession_sendRequest(f->m_client, 0x30,
                                                     &arg, 1, &id),
                            "快照握手后客户端发业务请求");
                XLINK_CHECK(XLinkSession_sendPublish(f->m_server,
                                                     XLINK_TOPIC_USER_BASE + 9,
                                                     &arg, 1),
                            "快照握手后服务端发业务推送");
                xlink_fixture_step(f, 3);
                XLINK_CHECK(g_clientResponseCount == respBase + 1,
                            "快照握手后请求-应答正常送达");
                XLINK_CHECK(g_clientPublishCount == pubBase + 1,
                            "快照握手后推送正常送达");
            }
            xlink_fixture_delete(f);
        }
    }

    /* --- 子用例六：RequestTimeout 语义——双侧置位 + 客户端自动超时配置，
           服务端不应答，超时终局照常上报（能力位不干扰自动超时路径） --- */
    {
        XLinkTestFixture* f = xlink_fixture_create();
        uint8_t arg = 0x42;
        uint32_t id = 0;
        if (!f) {
            XLINK_CHECK(false, "超时语义用例夹具搭建成功");
            return;
        }
        /* 服务端只计数不应答：请求石沉大海，构造稳定的超时终局场景 */
        XLinkSession_setDispatchCallback(f->m_server, xlink_silent_dispatch,
                                         NULL);
        XLinkSession_setCapability(f->m_server,
                                   XLinkCapability_RequestTimeout);
        XLinkSession_setCapability(f->m_client,
                                   XLinkCapability_RequestTimeout);
        xlink_fixture_step(f, 3); /* 自动握手 */
        XLINK_CHECK(XLinkSession_isReady(f->m_server)
                        && XLinkSession_isReady(f->m_client),
                    "RequestTimeout 双侧置位时自动握手正常就绪");
        XLINK_CHECK((XLinkSession_negotiatedCapability(f->m_server)
                         & XLinkCapability_RequestTimeout) != 0
                        && (XLinkSession_negotiatedCapability(f->m_client)
                            & XLinkCapability_RequestTimeout) != 0,
                    "双侧能力交集均含 RequestTimeout");
        XLINK_CHECK(XLinkSession_setRequestTimeout(f->m_client, 100),
                    "客户端 setRequestTimeout(100) 成功");
        XLINK_CHECK(XLinkSession_setNumberOfRetries(f->m_client, 0),
                    "客户端 setNumberOfRetries(0) 成功（首次超时即终局）");
        g_reqFinishedCount = 0;
        g_reqFinishedId = 0;
        g_reqFinishedCmd = 0;
        g_reqFinishedOk = true;
        XObject_connect_2((XObject*)f->m_client,
                          XSignal(XLinkSession_requestFinished_signal),
                          xlink_request_finished_slot);
        XLINK_CHECK(XLinkSession_sendRequest(f->m_client, 0x42, &arg, 1, &id),
                    "发出不会被应答的请求（命令 0x42）");
        xlink_fixture_step(f, 15); /* 推进 150ms，越过 100ms 阈值 */
        XLINK_CHECK(g_reqFinishedCount == 1 && g_reqFinishedOk == false,
                    "服务端不应答时客户端收到 requestFinished(false)"
                    "（能力位不干扰自动超时）");
        XLINK_CHECK(g_reqFinishedId == id && g_reqFinishedCmd == 0x42,
                    "终局信号携带原请求 id 与命令号 0x42");
        XLINK_CHECK(!XLinkSession_isPending(f->m_client, id),
                    "超时终局后请求已出待应答表");
        XLINK_CHECK(XLinkSession_isReady(f->m_server)
                        && XLinkSession_isReady(f->m_client),
                    "超时终局后链路保持 Ready（无副作用）");
        xlink_fixture_delete(f);
    }
}

/* ========================================================================== */
/*                        十六、登录认证（XLINK_AUTH_ON）                     */
/* ========================================================================== */

#if XLINK_AUTH_ON

/** @brief 登录测试专用端口（避免与其他分组冲突）。 */
#define XAUTH_TEST_PORT 47325u

/** @brief 登录测试口令（仅测试用；不出现在任何日志断言里）。 */
#define XAUTH_TEST_PASSWORD "xlink-test-pw-2026"
/** @brief 登录测试用户名。 */
#define XAUTH_TEST_USER "tester"
/** @brief 登录测试管理员名。 */
#define XAUTH_TEST_ADMIN "root"

/** @brief 认证测试全局观测量。 */
static int g_authChallengeCount = 0;
static int g_authChangedOk = 0;
static int g_authChangedFail = 0;
static uint8_t g_authLastRole = 0;
static int g_authClientFailed = 0;
static int g_authClientFailureReason = 0;
static int g_authSrvRequests = 0;
static int g_authClientResponses = 0;

/** @brief 凭据查询桩：任意用户返回一条固定凭据（4.x 用例用）。 */
static int xauth_dummy_lookup(void* userData, const char* user,
                              uint8_t saltOut[XLINK_AUTH_SALT_BYTES],
                              uint32_t* iterationsOut,
                              uint8_t hashOut[XLINK_AUTH_NONCE_BYTES],
                              uint8_t* roleOut)
{
    (void)userData;
    (void)user;
    memset(saltOut, 0x11, XLINK_AUTH_SALT_BYTES);
    memset(hashOut, 0x22, XLINK_AUTH_NONCE_BYTES);
    *iterationsOut = 1;
    *roleOut = XLinkAuthRole_User;
    return XLinkSessionAuthLookup_Ok;
}

/** @brief 应答回调桩：永不给出应答（让认证停在等待态，验证超时）。 */
static bool xauth_never_respond(void* userData,
                                const XLinkAuthChallengePayload* challenge,
                                uint8_t* methodOut,
                                uint8_t responseOut[XLINK_AUTH_NONCE_BYTES],
                                uint8_t hashOut[XLINK_AUTH_NONCE_BYTES])
{
    (void)userData;
    (void)challenge;
    (void)methodOut;
    (void)responseOut;
    (void)hashOut;
    return false;
}

/** @brief 认证挑战信号槽：计数。 */
static void xauth_on_challenge(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    g_authChallengeCount++;
}

/** @brief 会话 authChanged 槽：统计通过与失败。 */
static void xauth_on_auth_changed(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_3(args, bool, passed, uint8_t, status, uint8_t, role);
    (void)status;
    if (passed) {
        g_authChangedOk++;
        g_authLastRole = role;
    }
    else {
        g_authChangedFail++;
    }
}

/** @brief 客户端 authFailed 槽。 */
static void xauth_on_client_failed(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_2(args, int, reason, uint8_t, status);
    (void)status;
    g_authClientFailed++;
    g_authClientFailureReason = reason;
}

/** @brief 服务端分发回调：记录业务请求数（认证通过后才应递增）。 */
static void xauth_srv_dispatch(XLinkSession* self, const XLinkFrameHeader* frame,
                               const uint8_t* payload)
{
    if (!self || !frame)
        return;
    if (frame->m_msgClass == XLinkMsgClass_Request) {
        uint8_t echo = (payload && frame->m_payloadLen > 0) ? payload[0] : 0;
        g_authSrvRequests++;
        XLinkSession_sendResponse(self, frame->m_requestId, frame->m_msgCode,
                                  &echo, 1);
    }
}

/** @brief 客户端分发模板：统计应答数。 */
static void xauth_cli_dispatch(XLinkSession* self, const XLinkFrameHeader* frame,
                               const uint8_t* payload)
{
    (void)self;
    (void)payload;
    if (frame && frame->m_msgClass == XLinkMsgClass_Response)
        g_authClientResponses++;
}

/** @brief 等待条件：客户端已连接且已认证。 */
static bool xauth_cond_client_authed(void)
{
    return g_client && XLinkClient_isReady(g_client)
           && XLinkClient_isAuthenticated(g_client);
}

/** @brief 等待条件：客户端连接就绪（不论认证）。 */
static bool xauth_cond_client_ready(void)
{
    return g_client && XLinkClient_isReady(g_client);
}

/** @brief 等待条件：客户端收到认证失败。 */
static bool xauth_cond_client_auth_failed(void)
{
    return g_authClientFailed >= 1;
}

/** @brief 有界事件泵（复用客户端分组口径）。 */
static bool xauth_wait(bool (*cond)(void), uint32_t ms, XLinkServer* srv)
{
    uint64_t start = XDateTime_currentMSecsSinceEpoch();
    while (!cond()) {
        if (XDateTime_currentMSecsSinceEpoch() - start >= ms)
            return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (srv)
            XLinkServer_pump(srv,
                             (uint32_t)XDateTime_currentMSecsSinceEpoch());
        XThread_msleep(5);
    }
    return true;
}

/** @brief 清零认证测试观测量。 */
static void xauth_reset(void)
{
    g_client = NULL;
    g_authChallengeCount = 0;
    g_authChangedOk = 0;
    g_authChangedFail = 0;
    g_authLastRole = 0;
    g_authClientFailed = 0;
    g_authClientFailureReason = 0;
    g_authSrvRequests = 0;
    g_authClientResponses = 0;
}

/**
 * @brief 子分组一：认证编解码与口令派生的纯函数正确性。
 */
static void xauth_test_codec(void)
{
    XLINK_SECTION("十六·一、认证编解码与口令派生");

    /* AuthRequest 往返 */
    {
        XLinkAuthRequestPayload src;
        XLinkAuthRequestPayload dst;
        uint8_t buf[1 + XLINK_AUTH_NAME_BYTES];
        size_t len = 0;
        memset(&src, 0, sizeof(src));
        src.m_nameLen = (uint8_t)strlen(XAUTH_TEST_USER);
        memcpy(src.m_name, XAUTH_TEST_USER, src.m_nameLen);
        XLINK_CHECK(XLinkAuth_encodeRequest(&src, buf, sizeof(buf), &len),
                    "AuthRequest 编码成功");
        XLINK_CHECK(len == 1u + src.m_nameLen, "AuthRequest 长度 = 1 + nameLen");
        XLINK_CHECK(XLinkAuth_decodeRequest(buf, len, &dst),
                    "AuthRequest 解码成功");
        XLINK_CHECK(dst.m_nameLen == src.m_nameLen
                        && memcmp(dst.m_name, src.m_name, src.m_nameLen) == 0,
                    "AuthRequest 往返一致");
        XLINK_CHECK(!XLinkAuth_decodeRequest(buf, 1, &dst),
                    "AuthRequest 截断载荷被拒");
        XLINK_CHECK(!XLinkAuth_encodeRequest(&src, buf, 0, &len),
                    "AuthRequest 容量不足被拒（不半写）");
    }

    /* AuthChallenge 往返与封闭枚举校验 */
    {
        XLinkAuthChallengePayload src;
        XLinkAuthChallengePayload dst;
        uint8_t buf[8 + XLINK_AUTH_SALT_BYTES + XLINK_AUTH_NONCE_BYTES];
        size_t len = 0;
        memset(&src, 0, sizeof(src));
        src.m_method = (uint8_t)XLinkAuthMethod_IteratedHmac;
        src.m_iterations = 1234;
        memset(src.m_salt, 0x11, sizeof(src.m_salt));
        memset(src.m_nonce, 0x22, sizeof(src.m_nonce));
        XLINK_CHECK(XLinkAuth_encodeChallenge(&src, buf, sizeof(buf), &len),
                    "AuthChallenge 编码成功");
        XLINK_CHECK(XLinkAuth_decodeChallenge(buf, len, &dst),
                    "AuthChallenge 解码成功");
        XLINK_CHECK(dst.m_iterations == 1234
                        && dst.m_salt[0] == 0x11 && dst.m_nonce[0] == 0x22,
                    "AuthChallenge 往返一致");
        buf[0] = 0x7F;
        XLINK_CHECK(!XLinkAuth_decodeChallenge(buf, len, &dst),
                    "未知认证方法被拒（封闭枚举）");
        buf[0] = (uint8_t)XLinkAuthMethod_IteratedHmac;
        buf[4] = buf[5] = buf[6] = buf[7] = 0;
        XLINK_CHECK(!XLinkAuth_decodeChallenge(buf, len, &dst),
                    "迭代次数为 0 被拒（防退化）");
        XLINK_CHECK(XLinkAuth_encodeChallenge(&src, buf, sizeof(buf), &len),
                    "AuthChallenge 重新编码成功");
        buf[1] = 8;
        XLINK_CHECK(!XLinkAuth_decodeChallenge(buf, len, &dst),
                    "盐长度不符被拒");
        XLINK_CHECK(!XLinkAuth_decodeChallenge(buf, len - 1, &dst),
                    "负载截断被拒");
    }

    /* AuthResponse / AuthResult 往返 */
    {
        XLinkAuthResponsePayload resp;
        XLinkAuthResponsePayload respOut;
        XLinkAuthResultPayload result;
        XLinkAuthResultPayload resultOut;
        uint8_t buf[4 + XLINK_AUTH_TEXT_BYTES + XLINK_AUTH_NONCE_BYTES];
        size_t len = 0;

        memset(&resp, 0, sizeof(resp));
        resp.m_method = (uint8_t)XLinkAuthMethod_IteratedHmac;
        resp.m_len = XLINK_AUTH_NONCE_BYTES;
        memset(resp.m_response, 0x44, sizeof(resp.m_response));
        XLINK_CHECK(XLinkAuth_encodeResponse(&resp, buf, sizeof(buf), &len),
                    "AuthResponse 编码成功");
        XLINK_CHECK(XLinkAuth_decodeResponse(buf, len, &respOut)
                        && respOut.m_response[0] == 0x44,
                    "AuthResponse 往返一致");
        buf[2] = 16;
        XLINK_CHECK(!XLinkAuth_decodeResponse(buf, len, &respOut),
                    "AuthResponse 长度不符被拒");

        memset(&result, 0, sizeof(result));
        result.m_status = XLinkAuthStatus_Ok;
        result.m_role = XLinkAuthRole_Admin;
        result.m_textLen = 4;
        memcpy(result.m_text, "ok!!", 4);
        memset(result.m_serverProof, 0x33, sizeof(result.m_serverProof));
        XLINK_CHECK(XLinkAuth_encodeResult(&result, buf, sizeof(buf), &len),
                    "AuthResult 编码成功");
        XLINK_CHECK(XLinkAuth_decodeResult(buf, len, &resultOut),
                    "AuthResult 解码成功");
        XLINK_CHECK(resultOut.m_status == XLinkAuthStatus_Ok
                        && resultOut.m_role == XLinkAuthRole_Admin
                        && resultOut.m_serverProof[0] == 0x33,
                    "AuthResult 往返一致");
        XLINK_CHECK(!XLinkAuth_decodeResult(buf, 2, &resultOut),
                    "AuthResult 头部截断被拒");
    }

    /* 口令派生确定性 + 应答/证明域分离 */
    {
        uint8_t salt[XLINK_AUTH_SALT_BYTES];
        uint8_t nonce[XLINK_AUTH_NONCE_BYTES];
        uint8_t hash1[XLINK_AUTH_NONCE_BYTES];
        uint8_t hash2[XLINK_AUTH_NONCE_BYTES];
        uint8_t resp[XLINK_AUTH_NONCE_BYTES];
        uint8_t proof[XLINK_AUTH_NONCE_BYTES];
        memset(salt, 0x5A, sizeof(salt));
        memset(nonce, 0xA5, sizeof(nonce));
        XLINK_CHECK(XLinkAuth_deriveHash(XAUTH_TEST_PASSWORD, salt, 100, hash1),
                    "口令派生成功");
        XLINK_CHECK(XLinkAuth_deriveHash(XAUTH_TEST_PASSWORD, salt, 100, hash2),
                    "口令派生可重复");
        XLINK_CHECK(memcmp(hash1, hash2, sizeof(hash1)) == 0,
                    "同盐同迭代派生结果确定");
        XLINK_CHECK(XLinkAuth_computeResponse(hash1, nonce, resp),
                    "挑战应答计算成功");
        XLINK_CHECK(XLinkAuth_computeServerProof(hash1, nonce, proof),
                    "服务端证明计算成功");
        XLINK_CHECK(memcmp(resp, proof, sizeof(resp)) != 0,
                    "应答与服务端证明因域分离而不同（不可互相替代）");
        XLINK_CHECK(XLinkAuth_deriveHash(XAUTH_TEST_PASSWORD, salt, 101, hash2)
                        && memcmp(hash1, hash2, sizeof(hash1)) != 0,
                    "迭代次数参与派生（100 与 101 结果不同）");
        salt[0] ^= 0xFF;
        XLINK_CHECK(XLinkAuth_deriveHash(XAUTH_TEST_PASSWORD, salt, 100, hash2)
                        && memcmp(hash1, hash2, sizeof(hash1)) != 0,
                    "盐参与派生（换盐结果不同）");
        salt[0] ^= 0xFF;
        XLINK_CHECK(XLinkAuth_deriveHash("wrong-password", salt, 100, hash2)
                        && memcmp(hash1, hash2, sizeof(hash1)) != 0,
                    "错误口令派生结果不同");
        XLINK_CHECK(!XLinkAuth_deriveHash("", salt, 100, hash2),
                    "空口令被拒");
        XLINK_CHECK(!XLinkAuth_deriveHash(XAUTH_TEST_PASSWORD, salt, 0, hash2),
                    "零迭代被拒");
        XLINK_CHECK(XLinkAuth_constantTimeEqual(hash1, hash1, sizeof(hash1)),
                    "常量时间比较：相等返回 true");
        hash2[0] ^= 1;
        XLINK_CHECK(!XLinkAuth_constantTimeEqual(hash1, hash2, sizeof(hash1)),
                    "常量时间比较：单字节差异返回 false");
    }
}

/**
 * @brief 子分组二：账户库（内存态语义）。
 */
static void xauth_test_userstore(void)
{
    XLINK_SECTION("十六·二、账户库 XLinkUserStore");

    {
        XLinkUserStore store;
        const XLinkUserRecord* record;
        XLinkUserStore_init(&store);
        XLINK_CHECK(XLinkUserStore_count(&store) == 0, "新账户库为空");

        XLINK_CHECK(XLinkUserStore_upsert(&store, XAUTH_TEST_USER,
                                          XAUTH_TEST_PASSWORD,
                                          XLinkAuthRole_User),
                    "添加普通用户成功");
        XLINK_CHECK(XLinkUserStore_count(&store) == 1, "账户数为 1");
        XLINK_CHECK(XLinkUserStore_upsert(&store, XAUTH_TEST_ADMIN,
                                          "admin-pw", XLinkAuthRole_Admin),
                    "添加管理员成功");
        XLINK_CHECK(XLinkUserStore_count(&store) == 2, "账户数为 2");
        XLINK_CHECK(!XLinkUserStore_upsert(&store, "nopw", NULL,
                                           XLinkAuthRole_User),
                    "新建账户无口令被拒");
        XLINK_CHECK(XLinkUserStore_upsert(&store, XAUTH_TEST_USER,
                                          "new-password",
                                          XLinkAuthRole_Admin),
                    "同名账户更新口令与角色成功");
        XLINK_CHECK(XLinkUserStore_count(&store) == 2, "更新不增加账户数");
        record = XLinkUserStore_find(&store, XAUTH_TEST_USER);
        XLINK_CHECK(record && record->m_role == XLinkAuthRole_Admin,
                    "角色已更新为管理员");

        {
            uint8_t role = 0;
            XLINK_CHECK(XLinkUserStore_verify(&store, XAUTH_TEST_USER,
                                              "new-password", &role) == 0,
                        "正确口令校验通过");
            XLINK_CHECK(role == XLinkAuthRole_Admin,
                        "校验返回更新后的角色");
            XLINK_CHECK(XLinkUserStore_verify(&store, XAUTH_TEST_USER,
                                              "wrong", NULL) == 3,
                        "错误口令校验失败（码 3）");
            XLINK_CHECK(XLinkUserStore_verify(&store, "ghost", "x", NULL) == 1,
                        "不存在的用户校验失败（码 1）");
        }

        XLINK_CHECK(XLinkUserStore_setLocked(&store, XAUTH_TEST_USER, true),
                    "锁定账户成功");
        XLINK_CHECK(XLinkUserStore_verify(&store, XAUTH_TEST_USER,
                                          "new-password", NULL) == 2,
                    "锁定账户校验失败（码 2）");
        XLINK_CHECK(XLinkUserStore_setLocked(&store, XAUTH_TEST_USER, false),
                    "解锁账户成功");
        XLINK_CHECK(XLinkUserStore_verify(&store, XAUTH_TEST_USER,
                                          "new-password", NULL) == 0,
                    "解锁后校验恢复通过");
        XLINK_CHECK(XLinkUserStore_remove(&store, XAUTH_TEST_ADMIN),
                    "删除账户成功");
        XLINK_CHECK(XLinkUserStore_count(&store) == 1, "删除后账户数为 1");
        XLINK_CHECK(!XLinkUserStore_remove(&store, XAUTH_TEST_ADMIN),
                    "重复删除返回 false");

        XLinkUserStore_clear(&store);
        XLINK_CHECK(XLinkUserStore_count(&store) == 0, "clear 后账户库为空");
        XLINK_CHECK(!XLinkUserStore_upsert(NULL, "a", "b", 1),
                    "upsert 对 NULL 库拒绝");
        XLINK_CHECK(XLinkUserStore_find(NULL, "a") == NULL,
                    "find 对 NULL 库返回 NULL");
        XLINK_CHECK(XLinkUserStore_count(NULL) == 0,
                    "count 对 NULL 库返回 0");
    }
}

/**
 * @brief 子分组三：真实 TCP 链路端到端认证（成功/错误口令/无凭据）。
 */
static void xauth_test_end_to_end(void)
{
    XLINK_SECTION("十六·三、真实链路端到端认证");

    /* --- 3.1 正确口令：握手 -> 认证 -> Ready -> 业务可用 --- */
    {
        XLinkServer* srv;
        XLinkClient* cli;
        uint8_t arg = 0x5A;

        xauth_reset();
        srv = XLinkServer_create();
        XLINK_CHECK(srv != NULL, "服务端创建成功");
        if (!srv)
            return;
        XLinkServer_setCapability(srv, XLinkCapability_Zlib);
        XLinkServer_setDispatchCallback(srv, xauth_srv_dispatch, NULL);
        XLinkServer_addUser(srv, XAUTH_TEST_USER, XAUTH_TEST_PASSWORD,
                            XLinkAuthRole_User);
        XLINK_CHECK(XLinkServer_setAuthEnabled(srv, true),
                    "服务端启用登录认证");
        XLINK_CHECK(XLinkServer_authEnabled(srv), "authEnabled 回读为真");
        XLINK_CHECK(XLinkServer_userCount(srv) == 1, "服务端账户数为 1");

        if (!XLinkServer_listen_base(srv, "127.0.0.1", XAUTH_TEST_PORT)) {
            XLINK_CHECK(false, "服务端监听成功");
            XClassDelete((XClass*)srv);
            return;
        }

        cli = XLinkClient_create();
        XLINK_CHECK(cli != NULL, "客户端创建成功");
        if (cli) {
            XLinkClient_setHostPort(cli, "127.0.0.1", XAUTH_TEST_PORT);
            XLinkClient_setCapability(cli, XLinkCapability_Zlib);
            XLinkClient_setDispatchCallback(cli, xauth_cli_dispatch, NULL);
            XLINK_CHECK(XLinkClient_setCredential(cli, XAUTH_TEST_USER,
                                                  XAUTH_TEST_PASSWORD),
                        "客户端设置凭据成功");
            XLINK_CHECK(XLinkClient_hasCredential(cli), "hasCredential 为真");
            XLINK_CHECK(!XLinkClient_setCredential(cli, "", "x"),
                        "空用户名设置凭据被拒");
            XObject_connect_1((XObject*)cli,
                              XSignal(XLinkClient_authFailed_signal),
                              (XObject*)cli, xauth_on_client_failed,
                              XConnectionType_Auto);
            g_client = cli;
            XLINK_CHECK(XLinkClient_connectToServer(cli), "发起连接成功");
            XLINK_CHECK(xauth_wait(xauth_cond_client_authed, 8000, srv),
                        "正确口令：客户端连上并通过认证");
            XLINK_CHECK(XLinkClient_isAuthenticated(cli),
                        "客户端 isAuthenticated 为真");
            if (XLinkClient_session(cli)) {
                XLINK_CHECK(XLinkSession_isAuthenticated(
                                XLinkClient_session(cli)),
                            "会话层 isAuthenticated 为真");
                XLINK_CHECK(XLinkSession_authUser_const(
                                XLinkClient_session(cli)) != NULL
                                && strcmp(XLinkSession_authUser_const(
                                              XLinkClient_session(cli)),
                                          XAUTH_TEST_USER) == 0,
                            "会话记录登录用户名");
                XLINK_CHECK(XLinkSession_authRole(
                                XLinkClient_session(cli)) == XLinkAuthRole_User,
                            "会话记录用户角色");
            }
            else {
                XLINK_CHECK(false, "会话指针有效");
            }

            g_authSrvRequests = 0;
            XLINK_CHECK(XLinkSession_sendRequest(XLinkClient_session(cli),
                                                 0x31, &arg, 1, NULL),
                        "认证后发业务请求成功");
            /* 请求送达需要事件泵推进（回环/TCP 写是异步管线）。 */
            {
                uint64_t t0 = XDateTime_currentMSecsSinceEpoch();
                while (g_authSrvRequests < 1
                       && XDateTime_currentMSecsSinceEpoch() - t0 < 3000) {
                    XCoreApplication_processEvents(XEventLoop_AllEvents);
                    XLinkServer_pump(
                        srv, (uint32_t)XDateTime_currentMSecsSinceEpoch());
                    XThread_msleep(5);
                }
            }
            XLINK_CHECK(g_authSrvRequests >= 1,
                        "认证后业务请求送达服务端（认证前会被门控拦截）");
            XLinkClient_disconnectFromServer(cli);
            XClassDelete((XClass*)cli);
            g_client = NULL;
        }
        XLinkServer_close(srv);
        XClassDelete((XClass*)srv);
    }

    /* --- 3.2 错误口令：认证被拒，客户端不进入 Ready --- */
    {
        XLinkServer* srv;
        XLinkClient* cli;
        int i;

        xauth_reset();
        srv = XLinkServer_create();
        if (!srv) {
            XLINK_CHECK(false, "服务端创建成功（错误口令用例）");
            return;
        }
        XLinkServer_setCapability(srv, XLinkCapability_Zlib);
        XLinkServer_setDispatchCallback(srv, xauth_srv_dispatch, NULL);
        XLinkServer_addUser(srv, XAUTH_TEST_USER, XAUTH_TEST_PASSWORD,
                            XLinkAuthRole_User);
        XLinkServer_setAuthEnabled(srv, true);
        XLinkServer_setAuthFailureLimit(srv, 1);
        if (!XLinkServer_listen_base(srv, "127.0.0.1", XAUTH_TEST_PORT)) {
            XLINK_CHECK(false, "服务端监听成功（错误口令用例）");
            XClassDelete((XClass*)srv);
            return;
        }

        cli = XLinkClient_create();
        if (cli) {
            XLinkClient_setHostPort(cli, "127.0.0.1", XAUTH_TEST_PORT);
            XLinkClient_setDispatchCallback(cli, xauth_cli_dispatch, NULL);
            XLinkClient_setCredential(cli, XAUTH_TEST_USER, "definitely-wrong");
            XObject_connect_1((XObject*)cli,
                              XSignal(XLinkClient_authFailed_signal),
                              (XObject*)cli, xauth_on_client_failed,
                              XConnectionType_Auto);
            g_client = cli;
            XLinkClient_connectToServer(cli);
            XLINK_CHECK(xauth_wait(xauth_cond_client_auth_failed, 10000, srv),
                        "错误口令：客户端收到 authFailed");
            XLINK_CHECK(g_authClientFailureReason
                            == XLinkClientAuthFailure_BadPassword,
                        "authFailed 原因为口令错误");
            XLINK_CHECK(!XLinkClient_isReady(cli),
                        "错误口令：客户端未进入 Ready");
            XLINK_CHECK(XLinkClient_lastAuthFailure(cli)
                            == XLinkClientAuthFailure_BadPassword,
                        "lastAuthFailure 记录口令错误");
            for (i = 0; i < 200 && XLinkClient_state(cli)
                                            != XLinkClient_UnconnectedState;
                 ++i) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XLinkServer_pump(srv,
                                 (uint32_t)XDateTime_currentMSecsSinceEpoch());
                XThread_msleep(5);
            }
            XLINK_CHECK(XLinkClient_state(cli)
                            == XLinkClient_UnconnectedState,
                        "错误口令：状态回到未连接（不卡在连接中）");
            XClassDelete((XClass*)cli);
            g_client = NULL;
        }
        XLinkServer_close(srv);
        XClassDelete((XClass*)srv);
    }

    /* --- 3.3 无凭据：对端要求认证时不放行 --- */
    {
        XLinkServer* srv;
        XLinkClient* cli;

        xauth_reset();
        srv = XLinkServer_create();
        if (!srv) {
            XLINK_CHECK(false, "服务端创建成功（无凭据用例）");
            return;
        }
        XLinkServer_setDispatchCallback(srv, xauth_srv_dispatch, NULL);
        XLinkServer_addUser(srv, XAUTH_TEST_USER, XAUTH_TEST_PASSWORD,
                            XLinkAuthRole_User);
        XLinkServer_setAuthEnabled(srv, true);
        if (!XLinkServer_listen_base(srv, "127.0.0.1", XAUTH_TEST_PORT)) {
            XLINK_CHECK(false, "服务端监听成功（无凭据用例）");
            XClassDelete((XClass*)srv);
            return;
        }

        cli = XLinkClient_create();
        if (cli) {
            XLinkClient_setHostPort(cli, "127.0.0.1", XAUTH_TEST_PORT);
            XLinkClient_setDispatchCallback(cli, xauth_cli_dispatch, NULL);
            XObject_connect_1((XObject*)cli,
                              XSignal(XLinkClient_authFailed_signal),
                              (XObject*)cli, xauth_on_client_failed,
                              XConnectionType_Auto);
            g_client = cli;
            XLinkClient_connectToServer(cli);
            {
                int i;
                bool sawReady = false;
                for (i = 0; i < 400; ++i) {
                    XCoreApplication_processEvents(XEventLoop_AllEvents);
                    XLinkServer_pump(
                        srv, (uint32_t)XDateTime_currentMSecsSinceEpoch());
                    if (XLinkClient_isReady(cli))
                        sawReady = true;
                    XThread_msleep(5);
                }
                XLINK_CHECK(!sawReady,
                            "无凭据：客户端从未进入 Ready（认证门控生效）");
            }
            XLINK_CHECK(!XLinkClient_isAuthenticated(cli),
                        "无凭据：客户端未认证");
            XClassDelete((XClass*)cli);
            g_client = NULL;
        }
        XLinkServer_close(srv);
        XClassDelete((XClass*)srv);
    }
}

/**
 * @brief 子分组四：能力位降级不可绕过认证 + 认证超时 + 握手超时。
 */
static void xauth_test_hardening(void)
{
    XLINK_SECTION("十六·四、认证加固（降级/超时/半开连接）");

    /* --- 4.1 对端不宣告 Auth：默认拒绝（不因不声明能力位而放行） --- */
    {
        XGuiRemoteLoopbackDevice* devA = NULL;
        XGuiRemoteLoopbackDevice* devB = NULL;
        XLinkSession* server = NULL;
        XLinkSession* client = NULL;

        devA = XGuiRemoteLoopbackDevice_createPair(64 * 1024, &devB);
        if (!devA || !devB) {
            XLINK_CHECK(false, "回环设备对创建成功（降级用例）");
            return;
        }
        server = XLinkSession_create_ex((XIODevice*)devA);
        client = XLinkSession_create_ex((XIODevice*)devB);
        if (!server || !client) {
            XLINK_CHECK(false, "会话创建成功（降级用例）");
        }
        else {
            int i;
            XLinkSession_setAuthLookup(server, xauth_dummy_lookup, NULL);
            XLinkSession_setDispatchCallback(server, xauth_srv_dispatch, NULL);
            /* 客户端只宣告 Zlib（无 Auth），模拟老对端/能力位降级。 */
            XLinkSession_setCapability(client, XLinkCapability_Zlib);
            for (i = 0; i < 8; ++i) {
                XLinkSession_pump(server, 1000u + (uint32_t)i * 10u);
                XLinkSession_pump(client, 1000u + (uint32_t)i * 10u);
            }
            XLINK_CHECK(!XLinkSession_isReady(server),
                        "对端不宣告 Auth 时服务端不放行 Ready（默认拒绝）");
            XLINK_CHECK(XLinkSession_state(server)
                            == XLinkSessionState_Inactive,
                        "能力位降级被拒绝并断链");
            XLINK_CHECK(g_authSrvRequests == 0,
                        "降级连接未投递任何业务消息");
        }
        if (server) XClassDelete((XClass*)server);
        if (client) XClassDelete((XClass*)client);
        if (devA) XClassDelete((XClass*)devA);
        if (devB) XClassDelete((XClass*)devB);
    }

    /* --- 4.2 显式允许匿名时，老对端可接入（兼容模式） ---
       注意语义边界：allowAnonymous 是**服务端**决定"不宣告 Auth 的对端
       可以匿名接入"。客户端只要看到对端宣告了 Auth，就必须尝试登录
       （或快速失败），不能自行假定对方允许匿名——否则一个中间人只要
       谎称"允许匿名"就能绕过认证。因此本用例让服务端**不宣告** Auth
       （m_authRequired 为假），模拟真正的匿名服务端。 */
    {
        XGuiRemoteLoopbackDevice* devA = NULL;
        XGuiRemoteLoopbackDevice* devB = NULL;
        XLinkSession* server = NULL;
        XLinkSession* client = NULL;

        devA = XGuiRemoteLoopbackDevice_createPair(64 * 1024, &devB);
        if (!devA || !devB) {
            XLINK_CHECK(false, "回环设备对创建成功（匿名兼容用例）");
            return;
        }
        server = XLinkSession_create_ex((XIODevice*)devA);
        client = XLinkSession_create_ex((XIODevice*)devB);
        if (!server || !client) {
            XLINK_CHECK(false, "会话创建成功（匿名兼容用例）");
        }
        else {
            int i;
            XLinkSession_setCapability(server, XLinkCapability_Zlib);
            XLinkSession_setAuthAllowAnonymous(server, true);
            XLinkSession_setCapability(client, XLinkCapability_Zlib);
            for (i = 0; i < 8; ++i) {
                XLinkSession_pump(server, 1000u + (uint32_t)i * 10u);
                XLinkSession_pump(client, 1000u + (uint32_t)i * 10u);
            }
            XLINK_CHECK(XLinkSession_isReady(server),
                        "服务端未要求认证时匿名链路正常就绪");
            XLINK_CHECK(XLinkSession_isAuthenticated(server),
                        "匿名接入视为已认证（业务层判断口径一致）");
            XLINK_CHECK(XLinkSession_authRole(server) == XLinkAuthRole_Anonymous,
                        "匿名接入角色为 Anonymous");
        }
        if (server) XClassDelete((XClass*)server);
        if (client) XClassDelete((XClass*)client);
        if (devA) XClassDelete((XClass*)devA);
        if (devB) XClassDelete((XClass*)devB);
    }

    /* --- 4.3 认证超时：服务端等待应答超阈值即断链，不悬挂 --- */
    {
        XGuiRemoteLoopbackDevice* devA = NULL;
        XGuiRemoteLoopbackDevice* devB = NULL;
        XLinkSession* server = NULL;
        XLinkSession* client = NULL;
        uint32_t now = 1000;

        devA = XGuiRemoteLoopbackDevice_createPair(64 * 1024, &devB);
        if (!devA || !devB) {
            XLINK_CHECK(false, "回环设备对创建成功（认证超时用例）");
            return;
        }
        server = XLinkSession_create_ex((XIODevice*)devA);
        client = XLinkSession_create_ex((XIODevice*)devB);
        if (!server || !client) {
            XLINK_CHECK(false, "会话创建成功（认证超时用例）");
        }
        else {
            int i;
            XLinkSession_setAuthLookup(server, xauth_dummy_lookup, NULL);
            XLinkSession_setAuthTimeout(server, 300);
            XLinkSession_setCapability(client, XLinkCapability_Auth);
            /* 客户端配一个"永不回应"的应答回调：认证会停在服务端等待
               AuthResponse 处，从而能确定性地验证超时断链。 */
            XLinkSession_setAuthUser(client, "probe");
            XLinkSession_setAuthResponse(client, xauth_never_respond, NULL);
            for (i = 0; i < 6; ++i) {
                now += 10;
                XLinkSession_pump(server, now);
                XLinkSession_pump(client, now);
            }
            XLINK_CHECK(XLinkSession_state(server)
                            == XLinkSessionState_Authenticating,
                        "服务端进入认证等待态");
            now += 400;
            XLinkSession_pump(server, now);
            XLINK_CHECK(XLinkSession_state(server)
                            == XLinkSessionState_Inactive,
                        "认证超时后服务端断链（不悬挂）");
        }
        if (server) XClassDelete((XClass*)server);
        if (client) XClassDelete((XClass*)client);
        if (devA) XClassDelete((XClass*)devA);
        if (devB) XClassDelete((XClass*)devB);
    }

    /* --- 4.4 握手超时：对端设备从不回应，半开连接被回收 --- */
    {
        XGuiRemoteLoopbackDevice* dev = NULL;
        XGuiRemoteLoopbackDevice* peer = NULL;
        XLinkSession* session = NULL;
        uint32_t now = 1000;

        dev = XGuiRemoteLoopbackDevice_createPair(64 * 1024, &peer);
        if (!dev || !peer) {
            XLINK_CHECK(false, "回环设备对创建成功（握手超时用例）");
            return;
        }
        session = XLinkSession_create_ex((XIODevice*)dev);
        if (!session) {
            XLINK_CHECK(false, "会话创建成功（握手超时用例）");
        }
        else {
            XLinkSession_setHandshakeTimeout(session, 200);
            XLINK_CHECK(XLinkSession_handshakeTimeout(session) == 200,
                        "握手超时可运行时设置");
            /* 只 pump 本端：Hello 发进对端环，但没有人消费并回应。 */
            XLinkSession_pump(session, now);
            XLINK_CHECK(XLinkSession_state(session)
                            == XLinkSessionState_Connected,
                        "仅本端 pump：仍停留在握手中");
            now += 300;
            XLinkSession_pump(session, now);
            XLINK_CHECK(XLinkSession_state(session)
                            == XLinkSessionState_Inactive,
                        "握手超时后断链（半开连接被回收）");
            XLINK_CHECK(XLinkSession_stats(session, &(XLinkSessionStats){0}),
                        "stats 在断链后仍可读");
        }
        if (session) XClassDelete((XClass*)session);
        XClassDelete((XClass*)dev);
        XClassDelete((XClass*)peer);
    }
}

/**
 * @brief 子分组五：统计 API 与发送队列观测。
 */
static void xauth_test_stats(void)
{
    XLINK_SECTION("十六·五、统计与发送队列观测");

    {
        XLinkTestFixture* f = xlink_fixture_create();
        XLinkSessionStats stats;
        if (!f) {
            XLINK_CHECK(false, "夹具搭建成功（统计用例）");
            return;
        }
        memset(&stats, 0, sizeof(stats));
        XLINK_CHECK(!XLinkSession_stats(NULL, &stats),
                    "stats 对 NULL 会话返回 false");
        XLINK_CHECK(!XLinkSession_stats(f->m_server, NULL),
                    "stats 对 NULL 输出返回 false");

        XLinkSession_stats(f->m_server, &stats);
        {
            uint64_t framesBefore = stats.m_framesSent;
            uint64_t bytesBefore = stats.m_bytesSent;
            xlink_fixture_step(f, 3);
            XLinkSession_stats(f->m_server, &stats);
            XLINK_CHECK(stats.m_framesSent > framesBefore,
                        "握手后 framesSent 增长");
            XLINK_CHECK(stats.m_bytesSent > bytesBefore,
                        "握手后 bytesSent 增长");
            XLINK_CHECK(stats.m_framesReceived > 0,
                        "握手后 framesReceived 非零");
        }

        XLINK_CHECK(XLinkSession_pendingBytes(f->m_server)
                        <= XLINK_TX_QUEUE_BYTES,
                    "pendingBytes 在容量范围内");
        XLINK_CHECK(XLinkSession_pendingBytes(NULL) == 0,
                    "pendingBytes 对 NULL 返回 0");

        xlink_fixture_delete(f);
    }
}

/**
 * @brief 分组十六入口：登录认证与底层加固。
 */
static void xlink_test_auth(void)
{
    xauth_test_codec();
    xauth_test_userstore();
    xauth_test_end_to_end();
    xauth_test_hardening();
    xauth_test_stats();
}

#endif /* XLINK_AUTH_ON */

/* ========================================================================== */
/*                        十七、文件传输（XLINK_FILE_ON）                     */
/* ========================================================================== */

#if XLINK_FILE_ON

/** @brief 文件传输测试专用端口。 */
#define XFILE_TEST_PORT 47329u

/** @brief 测试文件名。 */
#define XFILE_TEST_NAME "xlink-test-file.bin"

/** @brief 构造并创建测试目录（相对当前工作目录），返回其路径。
 *  @param leaf 叶子名（拼成 xlink_ft_<leaf>）。
 *  @param out 输出缓冲（调用方提供，至少 256 字节）。
 *  @return 成功返回 out；失败返回 NULL（目录未建成）。
 *  @note  目录创建后立即可用于落盘；所有测试路径都相对当前工作目录，
 *         避免依赖平台临时目录 API（XDeviceFile_getSpecialPath 在部分
 *         后端不可用）。 */
static const char* xfile_test_makeDir(const char* leaf, char* out,
                                      size_t cap)
{
    XString* dirPath;
    bool created = false;

    if (!leaf || !out || cap == 0)
        return NULL;
    XSnprintf(out, cap, "xlink_ft_%s", leaf);

    dirPath = XString_create_utf8(out);
    if (!dirPath)
        return NULL;
    /* 直接建目录（XDir_mkdir 的 dirName 是相对 dir 的路径，语义不符）；
       目录已存在按成功处理。 */
    created = XDeviceFile_mkdir(dirPath, false);
    if (!created) {
        XFileStat stat;
        created = XDeviceFile_stat(dirPath, &stat) && stat.exists
                  && stat.isDir;
    }
    XClassDelete((XClass*)dirPath);
    return created ? out : NULL;
}

static void xfile_test_cleanup(const char* dir);

/** @brief 写一个指定大小/内容的测试文件（经 XFile；覆盖已存在文件）。
 *  @note  写前先清残留：老版本 cleanup 曾把目标chmod成不可写权限位
 *         又删不掉（见 xfile_test_cleanup 注），一次污染会让后续每轮
 *         在同一路径上打开即拒（沙箱外写/源文件准备首当其冲）。
 *         写前自愈让单个残留文件最多影响一轮。 */
static bool xfile_test_writeFile(const char* path, uint32_t size,
                                  uint8_t seed)
{
    XString* name = XString_create_utf8(path);
    XFile* file;
    uint8_t* buffer;
    uint32_t i;
    int64_t written;

    if (!name)
        return false;
    xfile_test_cleanup(path); /* 写前清残留（自愈，见函数注） */
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

/** @brief 读文件并返回大小（失败返回 -1）。 */
static int64_t xfile_test_readFile(const char* path, uint8_t** outData)
{
    XString* name;
    XFile* file;
    int64_t size;
    uint8_t* buffer;
    int64_t got;

    *outData = NULL;
    name = XString_create_utf8(path);
    if (!name)
        return -1;
    file = XFile_create_2(name);
    XClassDelete((XClass*)name);
    if (!file)
        return -1;
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0)) {
        XClassDelete((XClass*)file);
        return -1;
    }
    size = (int64_t)XFile_size_base(file);
    buffer = (uint8_t*)XMalloc_System((size_t)(size ? size : 1));
    if (!buffer) {
        XClassDelete((XClass*)file);
        return -1;
    }
    got = XIODevice_read_1((XIODevice*)file, (char*)buffer, size);
    XClassDelete((XClass*)file);
    if (got != size) {
        XFree_System(buffer);
        return -1;
    }
    *outData = buffer;
    return size;
}

/** @brief 删除测试文件/目录（尽力而为）。
 *  @note  先恢复可写权限：Windows 下只读文件无法删除，而部分打开路径会
 *         留下只读属性（曾致测试第二次运行失败）。
 *         文件与目录必须分流删除：removeRecursively 走 rmdir 语义，对
 *         普通文件恒失败——残留文件会让下一次运行的沙箱外写与下载本地
 *         落盘打开被拒，连跑必红；文件侧改走 XDir_removePath_static
 *         （对标 C remove 的类型分流）。
 *         权限旗标必须用 XFilePermission 枚举值：本层是 Qt 风格位域，
 *         直接传 0666 八进制会被逐位误读（0o666 十进制 = 0x1B6 恰好
 *         解码出 ExeUser|WriteGroup|…，chmod 出 0136 的不可写文件，
 *         「恢复可写」反成投毒，实测留下连跑必红的残留）。 */
static void xfile_test_cleanup(const char* dir)
{
    XString* path = XString_create_utf8(dir);
    XDir* handle;
    XFileStat stat;
    if (!path)
        return;
    if (XDeviceFile_stat(path, &stat) && stat.exists && stat.isFile) {
        (void)XDeviceFile_setPermissions(path,
                                         XFile_ReadOwner | XFile_WriteOwner);
        (void)XDir_removePath_static(path);
        XClassDelete((XClass*)path);
        return;
    }
    handle = XDir_create_2(path);
    if (handle) {
        (void)XDir_removeRecursively(handle);
        XClassDelete((XClass*)handle);
    }
    XClassDelete((XClass*)path);
}

/**
 * @brief 子分组一：文件协议编解码与文件名安全校验（纯函数）。
 */
static void xfile_test_codec(void)
{
    XLINK_SECTION("十七·一、文件协议编解码与文件名校验");

    /* Open 往返 */
    {
        XLinkFileOpenPayload src;
        XLinkFileOpenPayload dst;
        uint8_t buf[XLINK_FILE_OPEN_HEADER + XLINK_FILE_NAME_BYTES];
        size_t len = 0;
        memset(&src, 0, sizeof(src));
        src.m_mode = XLinkFileMode_Upload;
        src.m_flags = 1;
        src.m_nameLen = (uint16_t)strlen(XFILE_TEST_NAME);
        src.m_chunkBytes = 4096;
        src.m_windowBytes = 32768;
        src.m_totalSize = 123456;
        src.m_startOffset = 0;
        memcpy(src.m_name, XFILE_TEST_NAME, src.m_nameLen);

        XLINK_CHECK(XLinkFile_encodeOpen(&src, buf, sizeof(buf), &len),
                    "Open 编码成功");
        XLINK_CHECK(len == XLINK_FILE_OPEN_HEADER + src.m_nameLen,
                    "Open 长度 = 头部 + 文件名");
        XLINK_CHECK(XLinkFile_decodeOpen(buf, len, &dst), "Open 解码成功");
        XLINK_CHECK(dst.m_mode == src.m_mode && dst.m_totalSize == src.m_totalSize
                        && dst.m_chunkBytes == src.m_chunkBytes,
                    "Open 往返一致");
        XLINK_CHECK(strcmp(dst.m_name, XFILE_TEST_NAME) == 0,
                    "Open 文件名往返一致");
        XLINK_CHECK(!XLinkFile_decodeOpen(buf, len - 1u, &dst),
                    "Open 截断载荷被拒");
        /* 方向封闭枚举 */
        buf[0] = 9;
        XLINK_CHECK(!XLinkFile_decodeOpen(buf, len, &dst),
                    "Open 非法方向被拒（封闭枚举）");
        buf[0] = XLinkFileMode_Upload;
        /* 起始偏移 > 总大小 必须拒绝 */
        {
            uint8_t big[XLINK_FILE_OPEN_HEADER + XLINK_FILE_NAME_BYTES];
            size_t blen = 0;
            XLinkFileOpenPayload bad = src;
            bad.m_startOffset = bad.m_totalSize + 1;
            XLINK_CHECK(XLinkFile_encodeOpen(&bad, big, sizeof(big), &blen),
                        "越界起始偏移仍可编码（编码侧不校验语义）");
            XLINK_CHECK(!XLinkFile_decodeOpen(big, blen, &dst),
                        "起始偏移超过总大小被拒");
        }
    }

    /* Data 往返 */
    {
        uint8_t data[64];
        uint8_t buf[XLINK_FILE_DATA_OFFSET_BYTES + sizeof(data)];
        uint64_t offset = 0;
        const uint8_t* got = NULL;
        uint32_t gotLen = 0;
        size_t len = 0;
        int i;
        for (i = 0; i < (int)sizeof(data); ++i)
            data[i] = (uint8_t)i;
        XLINK_CHECK(XLinkFile_encodeData(0x1122334455667788ull, data,
                                         sizeof(data), buf, sizeof(buf), &len),
                    "Data 编码成功");
        XLINK_CHECK(XLinkFile_decodeData(buf, len, &offset, &got, &gotLen),
                    "Data 解码成功");
        XLINK_CHECK(offset == 0x1122334455667788ull,
                    "Data 偏移往返一致（u64 高位不丢）");
        XLINK_CHECK(gotLen == sizeof(data) && got && got[0] == 0
                        && got[63] == 63,
                    "Data 载荷往返一致");
        XLINK_CHECK(!XLinkFile_decodeData(buf, 4, &offset, &got, &gotLen),
                    "Data 头部截断被拒");
    }

    /* Ack / Finish / FinishAck / Abort */
    {
        XLinkFileAckPayload ack;
        XLinkFileAckPayload ackOut;
        XLinkFileFinishPayload fin;
        XLinkFileFinishPayload finOut;
        XLinkFileFinishAckPayload fa;
        XLinkFileFinishAckPayload faOut;
        XLinkFileAbortPayload ab;
        XLinkFileAbortPayload abOut;
        uint8_t buf[128];
        size_t len = 0;
        int i;

        ack.m_nextOffset = 987654321ull;
        ack.m_creditBytes = 65536;
        XLINK_CHECK(XLinkFile_encodeAck(&ack, buf, sizeof(buf), &len)
                        && XLinkFile_decodeAck(buf, len, &ackOut)
                        && ackOut.m_nextOffset == 987654321ull
                        && ackOut.m_creditBytes == 65536,
                    "Ack 往返一致");

        memset(&fin, 0, sizeof(fin));
        fin.m_totalSize = 42;
        for (i = 0; i < 32; ++i)
            fin.m_sha256[i] = (uint8_t)(i * 3);
        XLINK_CHECK(XLinkFile_encodeFinish(&fin, buf, sizeof(buf), &len)
                        && XLinkFile_decodeFinish(buf, len, &finOut)
                        && finOut.m_totalSize == 42
                        && memcmp(finOut.m_sha256, fin.m_sha256, 32) == 0,
                    "Finish（含 SHA-256）往返一致");

        memset(&fa, 0, sizeof(fa));
        fa.m_ok = 1;
        fa.m_status = XLinkFileStatus_Ok;
        fa.m_textLen = 2;
        memcpy(fa.m_text, "ok", 2);
        XLINK_CHECK(XLinkFile_encodeFinishAck(&fa, buf, sizeof(buf), &len)
                        && XLinkFile_decodeFinishAck(buf, len, &faOut)
                        && faOut.m_ok == 1 && faOut.m_status == 0,
                    "FinishAck 往返一致");

        memset(&ab, 0, sizeof(ab));
        ab.m_status = XLinkFileStatus_Checksum;
        ab.m_byPeer = 1;
        ab.m_textLen = 3;
        memcpy(ab.m_text, "bad", 3);
        XLINK_CHECK(XLinkFile_encodeAbort(&ab, buf, sizeof(buf), &len)
                        && XLinkFile_decodeAbort(buf, len, &abOut)
                        && abOut.m_status == XLinkFileStatus_Checksum
                        && abOut.m_byPeer == 1,
                    "Abort 往返一致");
    }

    /* 文件名安全校验 */
    {
        XLINK_CHECK(XLinkFile_isSafeName("a.txt", 0), "普通文件名通过");
        XLINK_CHECK(XLinkFile_isSafeName("dir/a.txt", 0),
                    "一级子目录通过");
        XLINK_CHECK(!XLinkFile_isSafeName("", 0), "空名被拒");
        XLINK_CHECK(!XLinkFile_isSafeName("/etc/passwd", 0),
                    "绝对路径被拒");
        XLINK_CHECK(!XLinkFile_isSafeName("C:\\Windows\\x", 0),
                    "盘符路径被拒");
        XLINK_CHECK(!XLinkFile_isSafeName("../secret", 0),
                    "上跳路径被拒");
        XLINK_CHECK(!XLinkFile_isSafeName("a/../../b", 0),
                    "中段上跳被拒");
        XLINK_CHECK(!XLinkFile_isSafeName("..", 0), "纯上跳被拒");
        XLINK_CHECK(!XLinkFile_isSafeName(".hidden", 0), "隐藏名被拒");
        /* 单点分量（"."）不构成上跳，属于安全名；靠后续规范化折叠。 */
        XLINK_CHECK(XLinkFile_isSafeName("a/./b", 0),
                    "单点分量不算路径穿越（规范化时折叠）");
        XLINK_CHECK(!XLinkFile_isSafeName("a\0b", 3), "内嵌 NUL 被拒");
        XLINK_CHECK(!XLinkFile_isSafeName("a\nb", 0), "控制字符被拒");
        XLINK_CHECK(XLinkFile_baseName_const("a/b/c.txt") != NULL
                        && strcmp(XLinkFile_baseName_const("a/b/c.txt"),
                                  "c.txt") == 0,
                    "baseName 取最后一段");
    }
}

/**
 * @brief 子分组二：服务端沙箱路径策略（不依赖网络）。
 */
static void xfile_test_sandbox(void)
{
    XLINK_SECTION("十七·二、服务端沙箱策略");

    char rootBuf[256];
    const char* root = xfile_test_makeDir("sandbox", rootBuf,
                                       sizeof(rootBuf));
    if (!root) {
        XLINK_CHECK(false, "测试目录创建成功");
        return;
    }
    XLINK_CHECK(xfile_test_writeFile("xlink_ft_sandbox/inside.txt", 16, 1),
                "沙箱内测试文件写入成功");
    XLINK_CHECK(xfile_test_writeFile("xlink_ft_outside.txt", 8, 2),
                "沙箱外测试文件写入成功");

    {
        XLinkFileServer fs;
        XLinkFileServer_init(&fs);
        XLINK_CHECK(XLinkFileServer_rootDir_const(&fs) == NULL,
                    "初始无根目录");
        XLINK_CHECK(XLinkFileServer_setRootDir(&fs, "no_such_dir_xyz") == false,
                    "不存在的根目录被拒");
        XLINK_CHECK(XLinkFileServer_setRootDir(&fs, root),
                    "设置沙箱根目录成功");
        XLINK_CHECK(XLinkFileServer_rootDir_const(&fs) != NULL,
                    "根目录可查询");

        /* 沙箱安全性由 xfs_resolve 承担，但它是 static；这里通过公开行为
           间接验证：isSafeName 拒绝穿越 + 根目录必须存在。 */
        XLINK_CHECK(!XLinkFile_isSafeName("../xlink_ft_outside.txt", 0),
                    "沙箱外文件名被 isSafeName 拒绝");

        XLINK_CHECK(XLinkFileServer_setFlags(
                        &fs, XLINK_FILESRV_ALLOW_UPLOAD
                                 | XLINK_FILESRV_ALLOW_DOWNLOAD),
                    "设置传输方向成功");
        XLINK_CHECK(XLinkFileServer_setMaxFileBytes(&fs, 1024),
                    "设置大小上限成功");
        XLINK_CHECK(XLinkFileServer_setMinRole(&fs, XLinkAuthRole_Admin),
                    "设置最低角色成功");
        XLINK_CHECK(!XLinkFileServer_setMinRole(&fs, 99),
                    "非法角色被拒");
        XLINK_CHECK(XLinkFileServer_setMaxFileBytes(&fs, 0),
                    "清零大小上限（不限制）成功");
        XLinkFileServer_deinit(&fs);
    }

    xfile_test_cleanup("xlink_ft_sandbox");
    xfile_test_cleanup("xlink_ft_outside.txt");
}

/**
 * @brief 子分组三：真实 TCP 端到端上传/下载（含校验与断点续传）。
 */
static void xfile_test_end_to_end(void)
{
    XLINK_SECTION("十七·三、真实链路端到端文件传输");

    char rootBuf[256];
    const char* root = xfile_test_makeDir("e2e", rootBuf,
                                       sizeof(rootBuf));
    if (!root) {
        XLINK_CHECK(false, "端到端测试目录创建成功");
        return;
    }
    /* 准备源文件（37KB，跨多个 4KB 块）。 */
    {
        const uint32_t size = 37u * 1024u + 123u;
        if (!xfile_test_writeFile("xlink_ft_e2e/src.bin", size, 0x5A)) {
            XLINK_CHECK(false, "源文件准备成功");
            xfile_test_cleanup("xlink_ft_e2e");
            return;
        }
        XLINK_CHECK(1, "源文件准备成功（37KB+123）");
    }

    {
        XLinkServer* srv = XLinkServer_create();
        XLinkClient* cli = XLinkClient_create();
        XLinkFileServer* fs = (XLinkFileServer*)XMalloc_System(
            sizeof(XLinkFileServer));
        XLinkFileClient* fc = XLinkFileClient_create();
        uint32_t transferId = 0;
        uint32_t waited;

        XLINK_CHECK(srv && cli && fs && fc,
                    "文件传输端到端对象创建成功");
        if (!srv || !cli || !fs || !fc) {
            if (srv) XClassDelete((XClass*)srv);
            if (cli) XClassDelete((XClass*)cli);
            if (fs) XFree_System(fs);
            if (fc) XClassDelete((XClass*)fc);
            xfile_test_cleanup("xlink_ft_e2e");
            return;
        }

        XLinkFileServer_init(fs);
        XLINK_CHECK(XLinkFileServer_setRootDir(fs, root),
                    "服务端沙箱根目录设置成功");
        XLINK_CHECK(XLinkFileServer_setFlags(
                        fs, XLINK_FILESRV_ALLOW_UPLOAD
                                | XLINK_FILESRV_ALLOW_DOWNLOAD
                                | XLINK_FILESRV_ALLOW_OVERWRITE),
                    "服务端允许双向传输与覆盖");
        XLinkFileServer_setMaxFileBytes(fs, 0);

        XLinkServer_setCapability(srv, XLinkCapability_Zlib);
        XLINK_CHECK(XLinkFileServer_attach(fs, srv),
                    "文件服务端挂接成功");
        if (!XLinkServer_listen_base(srv, "127.0.0.1", XFILE_TEST_PORT)) {
            XLINK_CHECK(false, "服务端监听成功（文件传输）");
            XLinkFileServer_deinit(fs);
            XFree_System(fs);
            XClassDelete((XClass*)fc);
            XClassDelete((XClass*)cli);
            XClassDelete((XClass*)srv);
            xfile_test_cleanup("xlink_ft_e2e");
            return;
        }

        XLinkClient_setHostPort(cli, "127.0.0.1", XFILE_TEST_PORT);
        /* 文件能力必须在连接前声明（握手期协商），否则双方交集不含
           FileTransfer，会话层会直接丢弃 File 帧。 */
        XLinkClient_setCapability(cli, XLinkCapability_Zlib
                                       | XLinkCapability_FileTransfer);
        g_client = cli;
        XLINK_CHECK(XLinkClient_connectToServer(cli), "客户端连接发起成功");
        XLINK_CHECK(xauth_wait(xauth_cond_client_ready, 8000, srv),
                    "文件传输前置：客户端 Ready");
        XLINK_CHECK(XLinkFileClient_attach(fc, cli),
                    "文件客户端挂接成功");
        XLINK_CHECK(!XLinkFileClient_isBusy(fc), "初始无活跃传输");

        /* --- 上传 --- */
        XLINK_CHECK(XLinkFileClient_upload(fc, "xlink_ft_e2e/src.bin",
                                           "uploaded.bin", 1, &transferId),
                    "上传发起成功");
        XLINK_CHECK(XLinkFileClient_isBusy(fc), "上传期间处于忙碌态");
        waited = 0;
        while (XLinkFileClient_isBusy(fc) && waited < 20000u) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XLinkServer_pump(srv,
                             (uint32_t)XDateTime_currentMSecsSinceEpoch());
            XLinkFileClient_pump(fc,
                                 (uint32_t)XDateTime_currentMSecsSinceEpoch());
            XThread_msleep(5);
            waited += 5;
        }
        XLINK_CHECK(XLinkFileClient_state(fc)
                        == XLinkFileTransferState_Done,
                    "上传完成（状态 Done）");
        {
            uint8_t* srcData = NULL;
            uint8_t* dstData = NULL;
            int64_t srcLen = xfile_test_readFile("xlink_ft_e2e/src.bin",
                                                 &srcData);
            int64_t dstLen = xfile_test_readFile("xlink_ft_e2e/uploaded.bin",
                                                 &dstData);
            XLINK_CHECK(srcLen > 0 && dstLen == srcLen,
                        "上传后目标文件大小一致");
            XLINK_CHECK(srcLen > 0 && dstLen == srcLen && srcData && dstData
                            && memcmp(srcData, dstData, (size_t)srcLen) == 0,
                        "上传后文件内容逐字节一致（SHA-256 校验通过）");
            if (srcData) XFree_System(srcData);
            if (dstData) XFree_System(dstData);
        }

        /* 服务端收尾：给几个 pump 轮次让上传槽位释放、会话回 Ready 稳态，
           再发起下载（避免与上传收尾竞态）。 */
        {
            int settle;
            for (settle = 0; settle < 20; ++settle) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XLinkServer_pump(srv,
                                 (uint32_t)XDateTime_currentMSecsSinceEpoch());
                XThread_msleep(5);
            }
        }

        /* --- 下载 --- */
        /* 下载目标由传输引擎打开（非 writeFile），同样先清旧残留。 */
        xfile_test_cleanup("xlink_ft_e2e_dl.bin");
        XLINK_CHECK(XLinkFileClient_download(fc, "uploaded.bin",
                                             "xlink_ft_e2e_dl.bin", 1,
                                             &transferId),
                    "下载发起成功");
        waited = 0;
        while (XLinkFileClient_isBusy(fc) && waited < 20000u) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XLinkServer_pump(srv,
                             (uint32_t)XDateTime_currentMSecsSinceEpoch());
            XLinkFileClient_pump(fc,
                                 (uint32_t)XDateTime_currentMSecsSinceEpoch());
            XThread_msleep(5);
            waited += 5;
        }
        XLINK_CHECK(XLinkFileClient_state(fc)
                        == XLinkFileTransferState_Done,
                    "下载完成（状态 Done）");
        {
            uint8_t* a = NULL;
            uint8_t* b = NULL;
            int64_t alen = xfile_test_readFile("xlink_ft_e2e/src.bin", &a);
            int64_t blen = xfile_test_readFile("xlink_ft_e2e_dl.bin", &b);
            XLINK_CHECK(alen > 0 && blen == alen && a && b
                            && memcmp(a, b, (size_t)alen) == 0,
                        "下载后文件与原文件逐字节一致");
            if (a) XFree_System(a);
            if (b) XFree_System(b);
        }

        /* --- 下载不存在的文件：应被沙箱/策略拒绝并终局失败 --- */
        XLINK_CHECK(XLinkFileClient_download(fc, "not-exists.bin",
                                             "xlink_ft_e2e_missing.bin", 1,
                                             &transferId),
                    "下载不存在文件的请求已发出");
        waited = 0;
        while (XLinkFileClient_isBusy(fc) && waited < 8000u) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            XLinkServer_pump(srv,
                             (uint32_t)XDateTime_currentMSecsSinceEpoch());
            XLinkFileClient_pump(fc,
                                 (uint32_t)XDateTime_currentMSecsSinceEpoch());
            XThread_msleep(5);
            waited += 5;
        }
        XLINK_CHECK(XLinkFileClient_state(fc)
                        == XLinkFileTransferState_Failed,
                    "下载不存在文件终局失败（沙箱拒绝）");

        /* --- 路径穿越：必须被拒绝 --- */
        XLINK_CHECK(!XLinkFileClient_upload(fc, "xlink_ft_e2e/src.bin",
                                            "../escape.bin", 1, &transferId),
                    "路径穿越的上传请求被本地拒绝");

        XLinkClient_disconnectFromServer(cli);
        XLinkFileClient_abort(fc, XLinkFileStatus_Aborted, "cleanup");
        XClassDelete((XClass*)fc);
        XLinkFileServer_deinit(fs);
        XFree_System(fs);
        XClassDelete((XClass*)cli);
        XLinkServer_close(srv);
        XClassDelete((XClass*)srv);
        g_client = NULL;
    }

    xfile_test_cleanup("xlink_ft_e2e_dl.bin");
    xfile_test_cleanup("xlink_ft_e2e_missing.bin");
    xfile_test_cleanup("xlink_ft_e2e");
}

/** @brief 激进双泵节奏测试端口（与十七·三错开，防监听互扰）。 */
#define XFILE_TEST_DP_PORT (XFILE_TEST_PORT + 1u)

/** @brief 激进双泵节奏单阶段等待上限（毫秒；每圈 1ms，高频短拍）。 */
#define XFILE_TEST_DP_BUDGET_MS 20000u

/**
 * @brief 子分组四：激进双泵节奏端到端（上传+下载连跑，驱动链回归锁）。
 * @details 每圈 processEvents（触发会话内建定时器/readyRead 异步驱动）与
 *          XLinkFileClient_pump 背靠背交替，节奏远快于会话 250ms 内建
 *          节拍。Auto 模式锁「单驱动门控下传输不被漏泵」；Force 模式锁
 *          「顺序双泵不搅会话、不丢帧」（XLink.md §14.3 怀疑 1 的回归
 *          保险）。防未来驱动链改动回退。
 */
static void xfile_test_double_pump(void)
{
    XLINK_SECTION("十七·四、激进双泵节奏（驱动链回归锁）");

    /* 驱动模式 API 自身的行为（不依赖链路）。 */
    {
        XLinkFileClient* fc = XLinkFileClient_create();
        XLINK_CHECK(fc != NULL, "驱动模式测试对象创建成功");
        if (!fc)
            return;
        XLINK_CHECK(XLinkFileClient_sessionDrive(fc)
                        == XLinkFileClientSessionDrive_Auto,
                    "会话驱动模式缺省 Auto");
        XLINK_CHECK(XLinkFileClient_setSessionDrive(
                        fc, XLinkFileClientSessionDrive_ForceSessionPump),
                    "切换 Force 模式成功");
        XLINK_CHECK(XLinkFileClient_sessionDrive(fc)
                        == XLinkFileClientSessionDrive_ForceSessionPump,
                    "Force 模式读回一致");
        XLINK_CHECK(XLinkFileClient_setSessionDrive(
                        fc, XLinkFileClientSessionDrive_Auto),
                    "切回 Auto 模式成功");
        XLINK_CHECK(!XLinkFileClient_setSessionDrive(
                        fc, (XLinkFileClientSessionDrive)9),
                    "非法驱动模式被拒");
        XClassDelete((XClass*)fc);
    }

    /* 真实链路：同一条连接上先上传后下载连跑，再以 Force 模式重传一次。 */
    {
        char rootBuf[256];
        const char* root = xfile_test_makeDir("dp", rootBuf,
                                              sizeof(rootBuf));
        XLinkServer* srv = NULL;
        XLinkClient* cli = NULL;
        XLinkFileServer* fs = NULL;
        XLinkFileClient* fc = NULL;
        uint32_t transferId = 0;

        XLINK_CHECK(root != NULL, "双泵测试目录创建成功");
        if (!root)
            return;
        if (!xfile_test_writeFile("xlink_ft_dp/src.bin",
                                  17u * 1024u + 9u, 0xA5)) {
            XLINK_CHECK(false, "双泵源文件准备成功");
            xfile_test_cleanup("xlink_ft_dp");
            return;
        }
        XLINK_CHECK(1, "双泵源文件准备成功（17KB+9）");

        srv = XLinkServer_create();
        cli = XLinkClient_create();
        fs = (XLinkFileServer*)XMalloc_System(sizeof(XLinkFileServer));
        fc = XLinkFileClient_create();
        XLINK_CHECK(srv && cli && fs && fc, "双泵节奏端到端对象创建成功");
        if (!srv || !cli || !fs || !fc) {
            if (srv) XClassDelete((XClass*)srv);
            if (cli) XClassDelete((XClass*)cli);
            if (fs) XFree_System(fs);
            if (fc) XClassDelete((XClass*)fc);
            xfile_test_cleanup("xlink_ft_dp");
            return;
        }

        XLinkFileServer_init(fs);
        XLINK_CHECK(XLinkFileServer_setRootDir(fs, root),
                    "双泵节奏沙箱根目录设置成功");
        XLINK_CHECK(XLinkFileServer_setFlags(
                        fs, XLINK_FILESRV_ALLOW_UPLOAD
                                | XLINK_FILESRV_ALLOW_DOWNLOAD
                                | XLINK_FILESRV_ALLOW_OVERWRITE),
                    "双泵节奏服务端允许双向传输与覆盖");
        XLinkFileServer_setMaxFileBytes(fs, 0);
        XLinkServer_setCapability(srv, XLinkCapability_Zlib);
        if (!XLinkFileServer_attach(fs, srv)
            || !XLinkServer_listen_base(srv, "127.0.0.1",
                                        XFILE_TEST_DP_PORT)) {
            XLINK_CHECK(false, "双泵节奏服务端监听成功");
            XLinkFileServer_deinit(fs);
            XFree_System(fs);
            XClassDelete((XClass*)fc);
            XClassDelete((XClass*)cli);
            XClassDelete((XClass*)srv);
            xfile_test_cleanup("xlink_ft_dp");
            return;
        }
        XLINK_CHECK(1, "双泵节奏服务端监听成功");

        XLinkClient_setHostPort(cli, "127.0.0.1", XFILE_TEST_DP_PORT);
        XLinkClient_setCapability(cli, XLinkCapability_Zlib
                                       | XLinkCapability_FileTransfer);
        g_client = cli;
        XLINK_CHECK(XLinkClient_connectToServer(cli), "双泵节奏连接发起成功");
        XLINK_CHECK(xauth_wait(xauth_cond_client_ready, 8000, srv),
                    "双泵节奏前置：客户端 Ready");
        XLINK_CHECK(XLinkFileClient_attach(fc, cli), "双泵节奏文件客户端挂接成功");

        /* --- 阶段一：Auto 模式上传（门控生效，会话单驱动） --- */
        XLINK_CHECK(XLinkFileClient_upload(fc, "xlink_ft_dp/src.bin",
                                           "dp_uploaded.bin", 1, &transferId),
                    "Auto 模式上传发起成功");
        {
            uint32_t waited = 0;
            while (XLinkFileClient_isBusy(fc)
                   && waited < XFILE_TEST_DP_BUDGET_MS) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XLinkServer_pump(srv,
                                 (uint32_t)XDateTime_currentMSecsSinceEpoch());
                XLinkFileClient_pump(fc, 0u);
                XThread_msleep(1);
                waited += 1;
            }
        }
        XLINK_CHECK(XLinkFileClient_state(fc) == XLinkFileTransferState_Done,
                    "Auto 激进双泵上传完成");
        XLINK_CHECK(XLinkClient_isReady(cli), "Auto 上传后会话仍 Ready（无误判回收）");
        {
            uint8_t* a = NULL;
            uint8_t* b = NULL;
            int64_t alen = xfile_test_readFile("xlink_ft_dp/src.bin", &a);
            int64_t blen = xfile_test_readFile("xlink_ft_dp/dp_uploaded.bin",
                                               &b);
            XLINK_CHECK(alen > 0 && alen == blen && a && b
                            && memcmp(a, b, (size_t)alen) == 0,
                        "Auto 上传文件逐字节一致");
            if (a) XFree_System(a);
            if (b) XFree_System(b);
        }

        /* --- 阶段二：Auto 模式下载（紧接上传，同链路同节奏） --- */
        xfile_test_cleanup("xlink_ft_dp_dp_dl.bin"); /* 下载目标先清残留 */
        XLINK_CHECK(XLinkFileClient_download(fc, "dp_uploaded.bin",
                                             "xlink_ft_dp_dp_dl.bin", 1,
                                             &transferId),
                    "Auto 模式下载发起成功（紧接上传）");
        {
            uint32_t waited = 0;
            while (XLinkFileClient_isBusy(fc)
                   && waited < XFILE_TEST_DP_BUDGET_MS) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XLinkServer_pump(srv,
                                 (uint32_t)XDateTime_currentMSecsSinceEpoch());
                XLinkFileClient_pump(fc, 0u);
                XThread_msleep(1);
                waited += 1;
            }
        }
        XLINK_CHECK(XLinkFileClient_state(fc) == XLinkFileTransferState_Done,
                    "Auto 激进双泵下载完成");
        XLINK_CHECK(XLinkClient_isReady(cli), "Auto 下载后会话仍 Ready（无误判回收）");
        {
            uint8_t* a = NULL;
            uint8_t* b = NULL;
            int64_t alen = xfile_test_readFile("xlink_ft_dp/src.bin", &a);
            int64_t blen = xfile_test_readFile("xlink_ft_dp_dp_dl.bin", &b);
            XLINK_CHECK(alen > 0 && alen == blen && a && b
                            && memcmp(a, b, (size_t)alen) == 0,
                        "Auto 下载文件与原文件逐字节一致");
            if (a) XFree_System(a);
            if (b) XFree_System(b);
        }

        /* --- 阶段三：Force 模式上传（旧版顺序双泵行为回归锁） --- */
        XLINK_CHECK(XLinkFileClient_setSessionDrive(
                        fc, XLinkFileClientSessionDrive_ForceSessionPump),
                    "切 Force 模式成功（顺序双泵回归锁）");
        XLINK_CHECK(XLinkFileClient_upload(fc, "xlink_ft_dp/src.bin",
                                           "dp_uploaded.bin", 1, &transferId),
                    "Force 模式上传发起成功");
        {
            uint32_t waited = 0;
            while (XLinkFileClient_isBusy(fc)
                   && waited < XFILE_TEST_DP_BUDGET_MS) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XLinkServer_pump(srv,
                                 (uint32_t)XDateTime_currentMSecsSinceEpoch());
                XLinkFileClient_pump(fc, 0u);
                XThread_msleep(1);
                waited += 1;
            }
        }
        XLINK_CHECK(XLinkFileClient_state(fc) == XLinkFileTransferState_Done,
                    "Force 顺序双泵上传完成（双泵不搅会话）");
        XLINK_CHECK(XLinkClient_isReady(cli), "Force 上传后会话仍 Ready");
        {
            uint8_t* a = NULL;
            uint8_t* b = NULL;
            int64_t alen = xfile_test_readFile("xlink_ft_dp/src.bin", &a);
            int64_t blen = xfile_test_readFile("xlink_ft_dp/dp_uploaded.bin",
                                               &b);
            XLINK_CHECK(alen > 0 && alen == blen && a && b
                            && memcmp(a, b, (size_t)alen) == 0,
                        "Force 上传文件逐字节一致（覆盖重传）");
            if (a) XFree_System(a);
            if (b) XFree_System(b);
        }
        XLINK_CHECK(XLinkFileClient_setSessionDrive(
                        fc, XLinkFileClientSessionDrive_Auto),
                    "恢复 Auto 模式成功");

        XLinkClient_disconnectFromServer(cli);
        XLinkFileClient_abort(fc, XLinkFileStatus_Aborted, "cleanup");
        XClassDelete((XClass*)fc);
        XLinkFileServer_deinit(fs);
        XFree_System(fs);
        XClassDelete((XClass*)cli);
        XLinkServer_close(srv);
        XClassDelete((XClass*)srv);
        g_client = NULL;
    }

    xfile_test_cleanup("xlink_ft_dp_dp_dl.bin");
    xfile_test_cleanup("xlink_ft_dp");
}

/** @brief 门控子句锁测试端口（与十七·三/四错开，防监听互扰）。 */
#define XFILE_TEST_GATE_PORT (XFILE_TEST_PORT + 2u)

/** @brief 无事件观察窗后「事件恢复」救援循环的完成预算（毫秒）。 */
#define XFILE_TEST_GATE_BUDGET_MS 20000u

/** @brief Auto 拒泵观察窗（毫秒）：localhost 上若代泵发生，一次小文件
 *  传输远快于此窗；窗内不完成即证「会话未被本函数驱动」。 */
#define XFILE_TEST_GATE_PROBE_MS 500u

/**
 * @brief 子分组五：单驱动门控子句锁（复核 medium·③）。
 * @details 十七·四的三个阶段每圈都转 processEvents，会话在 Auto/Force
 *          下都被异步驱动，门控子句本身（Auto+分发器在位 ⇒ pump 不代泵
 *          会话；Force ⇒ 代泵）没有可观察断言——把门控条件改反/改没，
 *          那组测试照样全绿。本分组两段锁：
 *            - 子句锁（willPumpSession 查询，与 pump 判定同源单一事实）：
 *              Auto ⇒ false、Force ⇒ true、切回 Auto ⇒ false。
 *            - 行为锁：观察窗内只泵服务端与文件客户端、**不转事件**，
 *              Auto 下传输停滞在 Opening；恢复转事件后同一笔传输照常
 *              推到 Done——证明停滞只来自无事件窗口，门控没有破坏传输、
 *              会话没有被误判回收。
 *          注：真实 TCP 的读写均为事件驱动（收包进环形缓冲、写残留推进
 *          都在分发器事件处理内，见 XAbstractSocket），无事件窗口里即便
 *          Force 代泵会话也搬不动数据，「Force 无事件救援」不可作为断言，
 *          Force 子句由查询 API 锁定。
 *          心跳拍编译为 0 时 Auto 恒代泵（会话契约），子句形态不同，
 *          该构建下跳过锁段。 */
static void xfile_test_drive_gate(void)
{
    XLINK_SECTION("十七·五、单驱动门控子句锁（Auto 拒泵/Force 代泵）");

    {
        char rootBuf[256];
        const char* root = xfile_test_makeDir("gate", rootBuf,
                                              sizeof(rootBuf));
        XLinkServer* srv = NULL;
        XLinkClient* cli = NULL;
        XLinkFileServer* fs = NULL;
        XLinkFileClient* fc = NULL;
        uint32_t transferId = 0;
        uint32_t waited;

        XLINK_CHECK(root != NULL, "门控锁测试目录创建成功");
        if (!root)
            return;
        if (!xfile_test_writeFile("xlink_ft_gate/src.bin",
                                  4u * 1024u + 7u, 0x3C)) {
            XLINK_CHECK(false, "门控锁源文件准备成功");
            xfile_test_cleanup("xlink_ft_gate");
            return;
        }
        XLINK_CHECK(1, "门控锁源文件准备成功（4KB+7）");

        srv = XLinkServer_create();
        cli = XLinkClient_create();
        fs = (XLinkFileServer*)XMalloc_System(sizeof(XLinkFileServer));
        fc = XLinkFileClient_create();
        XLINK_CHECK(srv && cli && fs && fc, "门控锁测试对象创建成功");
        if (!srv || !cli || !fs || !fc) {
            if (srv) XClassDelete((XClass*)srv);
            if (cli) XClassDelete((XClass*)cli);
            if (fs) XFree_System(fs);
            if (fc) XClassDelete((XClass*)fc);
            xfile_test_cleanup("xlink_ft_gate");
            return;
        }

        XLinkFileServer_init(fs);
        XLinkFileServer_setRootDir(fs, root);
        XLinkFileServer_setFlags(fs, XLINK_FILESRV_ALLOW_DOWNLOAD
                                     | XLINK_FILESRV_ALLOW_OVERWRITE);
        XLinkServer_setCapability(srv, XLinkCapability_Zlib);
        if (!XLinkFileServer_attach(fs, srv)
            || !XLinkServer_listen_base(srv, "127.0.0.1",
                                        XFILE_TEST_GATE_PORT)) {
            XLINK_CHECK(false, "门控锁服务端监听成功");
            XLinkFileServer_deinit(fs);
            XFree_System(fs);
            XClassDelete((XClass*)fc);
            XClassDelete((XClass*)cli);
            XClassDelete((XClass*)srv);
            xfile_test_cleanup("xlink_ft_gate");
            return;
        }
        XLINK_CHECK(1, "门控锁服务端监听成功");

        XLinkClient_setHostPort(cli, "127.0.0.1", XFILE_TEST_GATE_PORT);
        XLinkClient_setCapability(cli, XLinkCapability_Zlib
                                       | XLinkCapability_FileTransfer);
        g_client = cli;
        XLINK_CHECK(XLinkClient_connectToServer(cli), "门控锁连接发起成功");
        /* 前置就绪走正常异步驱动（转事件），与被锁子句无关。 */
        XLINK_CHECK(xauth_wait(xauth_cond_client_ready, 8000, srv),
                    "门控锁前置：客户端 Ready");
        XLINK_CHECK(XLinkFileClient_attach(fc, cli), "门控锁文件客户端挂接成功");

#if XLINK_HEARTBEAT_TICK_MS > 0
        /* 宿主前提：本线程分发器在位（测试宿主建了 XCoreApplication）。
           分发器缺席时 Auto 本就代泵，观察窗测不出子句。 */
        if (XObject_eventDispatcher((XObject*)fc)) {
            XLINK_CHECK(XLinkFileClient_sessionDrive(fc)
                            == XLinkFileClientSessionDrive_Auto,
                        "门控锁缺省 Auto 模式");
            xfile_test_cleanup("xlink_ft_gate_dl.bin");
            XLINK_CHECK(XLinkFileClient_download(fc, "src.bin",
                                                 "xlink_ft_gate_dl.bin", 1,
                                                 &transferId),
                        "门控锁下载发起成功");
            /* 观察窗：只泵服务端（OpenAck 会送到本机 socket）与文件
               客户端，**不转事件**——Auto 门控下会话无推进通道。 */
            waited = 0;
            while (XLinkFileClient_isBusy(fc)
                   && waited < XFILE_TEST_GATE_PROBE_MS) {
                XLinkServer_pump(srv,
                                 (uint32_t)XDateTime_currentMSecsSinceEpoch());
                XLinkFileClient_pump(fc, 0u);
                XThread_msleep(1);
                waited += 1;
            }
            XLINK_CHECK(XLinkFileClient_state(fc)
                            == XLinkFileTransferState_Opening,
                        "Auto+分发器在位：会话未被代泵（传输停在 Opening）");
            XLINK_CHECK(!XLinkFileTransfer_isFinished(
                            XLinkFileClient_transfer(fc)),
                        "Auto 观察窗内传输未终局（未被门控外通道推进）");

            /* 子句锁（willPumpSession 与 pump 判定同源）：Force ⇒ 代泵，
               切回 Auto ⇒ 恢复拒泵。真实 TCP 的读写均为事件驱动（见
               XAbstractSocket），无事件窗口里即便 Force 代泵会话也搬不动
               数据——「Force 无事件救援」不可作为断言，子句以查询锁定。 */
            XLINK_CHECK(XLinkFileClient_setSessionDrive(
                            fc, XLinkFileClientSessionDrive_ForceSessionPump),
                        "切 Force 模式成功（门控锁）");
            XLINK_CHECK(XLinkFileClient_willPumpSession(fc),
                        "Force 模式查询代泵（willPumpSession=true）");
            XLINK_CHECK(XLinkFileClient_setSessionDrive(
                            fc, XLinkFileClientSessionDrive_Auto),
                        "切回 Auto 模式成功（门控锁）");
            XLINK_CHECK(!XLinkFileClient_willPumpSession(fc),
                        "切回 Auto 查询恢复拒泵（willPumpSession=false）");

            /* 事件恢复出口：同一笔传输转事件后照常推进到 Done——证明
               停滞只来自无事件窗口（Auto 拒泵 + 传输层事件依赖），门控
               没有破坏传输、会话也没有被误判回收。 */
            waited = 0;
            while (XLinkFileClient_isBusy(fc)
                   && waited < XFILE_TEST_GATE_BUDGET_MS) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                XLinkServer_pump(srv,
                                 (uint32_t)XDateTime_currentMSecsSinceEpoch());
                XLinkFileClient_pump(fc, 0u);
                XThread_msleep(1);
                waited += 1;
            }
            XLINK_CHECK(XLinkFileClient_state(fc)
                            == XLinkFileTransferState_Done,
                        "事件恢复后同一传输走出停滞并完成");
            XLINK_CHECK(XLinkClient_isReady(cli),
                        "事件恢复后会话仍 Ready（无误判回收）");
            {
                uint8_t* a = NULL;
                uint8_t* b = NULL;
                int64_t alen = xfile_test_readFile("xlink_ft_gate/src.bin",
                                                   &a);
                int64_t blen = xfile_test_readFile("xlink_ft_gate_dl.bin",
                                                   &b);
                XLINK_CHECK(alen > 0 && alen == blen && a && b
                                && memcmp(a, b, (size_t)alen) == 0,
                            "事件恢复后下载文件逐字节一致");
                if (a) XFree_System(a);
                if (b) XFree_System(b);
            }
        } else {
            XLINK_CHECK(1, "宿主无事件分发器：Auto 本就代泵，子句锁跳过");
        }
#else
        XLINK_CHECK(1, "心跳拍为 0 的构建：Auto 恒代泵，子句锁跳过");
#endif /* XLINK_HEARTBEAT_TICK_MS > 0 */

        XLinkClient_disconnectFromServer(cli);
        XLinkFileClient_abort(fc, XLinkFileStatus_Aborted, "cleanup");
        XClassDelete((XClass*)fc);
        XLinkFileServer_deinit(fs);
        XFree_System(fs);
        XClassDelete((XClass*)cli);
        XLinkServer_close(srv);
        XClassDelete((XClass*)srv);
        g_client = NULL;
    }

    xfile_test_cleanup("xlink_ft_gate_dl.bin");
    xfile_test_cleanup("xlink_ft_gate");
}

/**
 * @brief 分组十七入口：文件传输。
 */
static void xlink_test_file(void)
{
    xfile_test_codec();
    xfile_test_sandbox();
    xfile_test_end_to_end();
    xfile_test_double_pump();
    xfile_test_drive_gate();
}

#endif /* XLINK_FILE_ON */

/* ========================================================================== */
/*                        入口                                               */
/* ========================================================================== */

/**
 * @brief 运行全部 XLink 测试分组。
 * @return 失败计数（0 表示全部通过）。
 */
int XLinkTest_run(void)
{
    XPrintf("========== XLink 私有协议单元测试开始 ==========\n");
    g_passed = 0;
    g_failed = 0;

    xlink_test_codec();
    xlink_test_lifecycle();
    xlink_test_state_machine();
    xlink_test_handshake();
    xlink_test_request_response();
    xlink_test_publish();
    xlink_test_subscribe();
    xlink_test_pending_timeout();
    xlink_test_heartbeat_timeout();
    xlink_test_defensive();
    xlink_test_server();
    xlink_test_signals_retry();
    xlink_client_test_null_defense();
    xlink_client_test_defaults();
    xlink_client_test_normal_link();
    xlink_client_test_auto_reconnect();
    xlink_client_test_no_reconnect();
    xlink_client_test_manual_disconnect();
    xlink_client_test_max_attempts();
    xlink_test_capability_matrix();
#if XLINK_AUTH_ON
    xlink_test_auth();
#endif
#if XLINK_FILE_ON
    xlink_test_file();
#endif

    XPrintf("\n========== XLink 测试完成: %d 通过, %d 失败 ==========\n",
            g_passed, g_failed);
    return g_failed;
}

/**
 * @brief XTestMenu 菜单入口：运行全部 XLink 测试。
 * @note  签名与 Test/XProtocolTest 同族测试（XMqttPublicApiTest 等）一致，
 *        由 XTestMenu_setActionFunction 绑定，无返回值。
 */
void XLinkTest(void)
{
    (void)XLinkTest_run();
}

/**
 * @brief 仅跑编解码层测试（细粒度菜单项，便于快速定位）。
 * @note  编解码是纯函数、无外部依赖，可脱离传输单独回归。
 */
void XLinkCodecTest(void)
{
    xlink_test_codec();
}

/**
 * @brief 仅跑会话层测试（生命周期/状态机/握手/请求应答/推送/订阅/超时）。
 */
void XLinkSessionTest(void)
{
    g_passed = 0;
    g_failed = 0;
    xlink_test_lifecycle();
    xlink_test_state_machine();
    xlink_test_handshake();
    xlink_test_request_response();
    xlink_test_publish();
    xlink_test_subscribe();
    xlink_test_pending_timeout();
    xlink_test_heartbeat_timeout();
    xlink_test_defensive();
    xlink_test_signals_retry();
    XPrintf("\n========== XLink 会话层测试完成: %d 通过, %d 失败 ==========\n",
            g_passed, g_failed);
}

/**
 * @brief 仅跑服务端包装测试（监听接入/模板下发/自动握手/会话回收）。
 * @note  依赖真实环回 TCP 与事件处理，比会话层测试重；单独成项便于
 *        网络环境受限时跳过。
 */
void XLinkServerTest(void)
{
    g_passed = 0;
    g_failed = 0;
    xlink_test_server();
    XPrintf("\n========== XLink 服务端测试完成: %d 通过, %d 失败 ==========\n",
            g_passed, g_failed);
}

/**
 * @brief 仅跑客户端包装测试（NULL 防御/默认值与生命周期/正常链路/踢线自动
 *        重连/重连关停/手动断开/最大重连次数）。
 * @note  即分组十四（原 XLinkClientTest.c 全量用例）；依赖真实环回 TCP
 *        （127.0.0.1:47321）与事件处理，比会话层测试重；单独成项便于
 *        网络环境受限时跳过。命令行入口 `--test xlink-client`。
 */
int XLinkClientTest_run(void)
{
    XPrintf("========== XLinkClient 客户端包装单元测试开始 ==========\n");
    g_passed = 0;
    g_failed = 0;

    xlink_client_test_null_defense();
    xlink_client_test_defaults();
    xlink_client_test_normal_link();
    xlink_client_test_auto_reconnect();
    xlink_client_test_no_reconnect();
    xlink_client_test_manual_disconnect();
    xlink_client_test_max_attempts();

    XPrintf("\n========== XLinkClient 测试完成: %d 通过, %d 失败 ==========\n",
            g_passed, g_failed);
    return g_failed;
}

/**
 * @brief XTestMenu 菜单入口：运行全部 XLinkClient 测试。
 * @note  签名与 Test/XProtocolTest 同族测试（XLinkTest 等）一致。
 */
void XLinkClientTest(void)
{
    (void)XLinkClientTest_run();
}


/* ======================================================================== *
 * 菜单注册：挂到 Test 菜单「协议栈 → XLink(xlink)」子菜单
 * ======================================================================== */

void XTestMenu_XLinkTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XLink(xlink)");
    {
        XAction* action = XTestMenu_addAction(menu, "全量回归（编解码+会话层）");
        XTestMenu_setActionFunction(action, XLinkTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "编解码层单元测试");
        XTestMenu_setActionFunction(action, XLinkCodecTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "服务端包装单元测试");
        XTestMenu_setActionFunction(action, XLinkServerTest);
    }
    {
        /* 客户端包装（分组十四，原 XLinkClientTest.c）：真实 TCP 自动重连 */
        XAction* action = XTestMenu_addAction(menu, "客户端包装单元测试");
        XTestMenu_setActionFunction(action, XLinkClientTest);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "实战案例（本进程联调）");
        XTestMenu_setActionFunction(action, XLinkDemo);
    }
    {
        /* 实战案例·服务端/客户端（声明在 XLinkDemo.h）：双进程联调两个角色 */
        XAction* action = XTestMenu_addAction(menu, "实战案例·服务端");
        XTestMenu_setActionFunction(action, XLinkDemoServer);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "实战案例·客户端");
        XTestMenu_setActionFunction(action, XLinkDemoClient);
    }
    {
        XAction* action = XTestMenu_addAction(menu, "会话层单元测试");
        XTestMenu_setActionFunction(action, XLinkSessionTest);
    }
    {
        /* 质量门禁：内存泄漏 + 性能基准 + 边界对抗 */
        XAction* action = XTestMenu_addAction(menu, "质量门禁（泄漏/性能/边界）");
        XTestMenu_setActionFunction(action, XLinkQualityTest);
    }
    XTestMenu_addMenu(root, menu);
}
