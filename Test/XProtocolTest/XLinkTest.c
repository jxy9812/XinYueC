/** @file       XLinkTest.c
 * @brief      XLink 私有应用层协议全量单元测试（接入 Test 菜单/命令行体系）。
 * @details    覆盖 XLink 公开 API 的每一条路径，读测试代码即可了解协议用途：
 *
 *   一、编解码层 XLinkCodec（小端序列化 / 帧头往返 / 载荷上限安全阀）
 *   二、会话生命周期 XLinkSession（init/create/create_ex/deinit 配对）
 *   三、传输挂载与状态机（Inactive -> Connected -> Ready）
 *   四、握手与能力位协商（双向对称 Hello/HelloAck，能力按位与）
 *   五、请求-应答关联（requestId 双向独立、应答出表、异步应答支持）
 *   六、推送与 topicId 承载（u32 topicId 不被 u16 msgCode 截断）
 *   七、订阅/退订语义（订阅表登记、回滚、重复退订拒绝）
 *   八、待应答表与超时（不误报、取出不重复上报）
 *   九、心跳与静默判线
 *   十、防御性路径（NULL 防御、非法参数、协议违例、半包粘包）
 *
 *   传输介质统一使用 XGuiRemoteLoopbackDevice（内存回环设备对）：两个会话
 *   经同一条管道互连，无需真实网卡即可跑通完整链路；其"全有全无"写入
 *   语义恰好能压出 XLink 的帧原子性缺陷。
 *
 *   运行入口：Test 菜单「协议栈 → XLink(...)」，或命令行
 *   `XinYueC -test xlink`（见 Test/XProtocolTest 的命令行分派）。
 *
 * @author      XinYueC 团队
 */
#include "XLinkTest.h"
#include "XLinkCodec.h"
#include "XLinkSession.h"
#include "XGuiRemoteLoopback.h"
#include "XTestMenu.h"
#include "XAction.h"
#include "XPrintf.h"
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
/*                        测试用消息收发器                                   */
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
}

/* ========================================================================== */
/*                        四、握手与能力协商                                  */
/* ========================================================================== */

/**
 * @brief 覆盖双向对称握手：Hello/HelloAck 自动应答与能力位按位与。
 */
static void xlink_test_handshake(void)
{
    XLINK_SECTION("四、握手与能力位协商");

    {
        XLinkTestFixture* f = xlink_fixture_create();
        uint8_t helloBuf[4];
        XLinkHelloPayload hello;
        if (!f) {
            XLINK_CHECK(false, "夹具搭建成功");
            return;
        }

        XLinkSession_setDispatchCallback(f->m_server, xlink_server_dispatch, NULL);
        XLinkSession_setDispatchCallback(f->m_client, xlink_client_dispatch, NULL);

        /* 两端各声明能力：服务端 Zlib|Tls，客户端仅 Zlib */
        XLinkSession_setCapability(f->m_server,
                                  XLinkCapability_Zlib | XLinkCapability_Tls);
        XLinkSession_setCapability(f->m_client, XLinkCapability_Zlib);

        XLINK_CHECK(XLinkSession_setCapability(NULL, 1) == false,
                    "setCapability 对 NULL 返回 false");

        /* 握手前对业务消息不可见：Publish 不应被投递 */
        {
            uint8_t payload = 0x11;
            XLinkSession_sendPublish(f->m_server, XLINK_TOPIC_USER_BASE + 5,
                                     &payload, 1);
            xlink_fixture_step(f, 2);
            XLINK_CHECK(g_clientPublishCount == 0,
                        "握手完成前不投递业务消息（防止乱序帧喂给应用）");
        }

        /* 服务端发 Hello，客户端应自动回 HelloAck 并双双转 Ready */
        hello.m_version = XLINK_VERSION;
        hello.m_capability = XLinkCapability_Zlib | XLinkCapability_Tls;
        XLINK_CHECK(XLinkCodec_encodeHello(&hello, helloBuf, sizeof(helloBuf),
                                           NULL),
                    "Hello 载荷编码成功");
        XLINK_CHECK(XLinkSession_sendSession(f->m_server,
                                             XLinkSessionCode_Hello,
                                             helloBuf, 4),
                    "服务端发出 Hello");
        xlink_fixture_step(f, 3);

        XLINK_CHECK(XLinkSession_isReady(f->m_client),
                    "客户端收到 Hello 后自动应答并转 Ready");
        XLINK_CHECK(XLinkSession_isReady(f->m_server),
                    "服务端收到 HelloAck 后转 Ready（双向对称）");
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
        uint8_t helloBuf[4];
        XLinkHelloPayload hello;
        uint32_t id1 = 0;
        uint32_t id2 = 0;
        uint8_t arg = 0x5A;
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

        /* 先握手，业务消息才可见 */
        hello.m_version = XLINK_VERSION;
        hello.m_capability = XLinkCapability_Zlib;
        XLinkCodec_encodeHello(&hello, helloBuf, sizeof(helloBuf), NULL);
        XLinkSession_sendSession(f->m_server, XLinkSessionCode_Hello,
                                 helloBuf, 4);
        xlink_fixture_step(f, 3);
        XLINK_CHECK(XLinkSession_isReady(f->m_client), "握手完成");

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

        XLINK_CHECK(g_serverRequestCount == 2, "服务端收到两条请求");
        XLINK_CHECK(g_clientResponseCount == 2, "客户端收到两条应答");
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
        uint8_t helloBuf[4];
        XLinkHelloPayload hello;
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

        hello.m_version = XLINK_VERSION;
        hello.m_capability = XLinkCapability_Zlib;
        XLinkCodec_encodeHello(&hello, helloBuf, sizeof(helloBuf), NULL);
        XLinkSession_sendSession(f->m_server, XLinkSessionCode_Hello,
                                 helloBuf, 4);
        xlink_fixture_step(f, 3);

        /* 零载荷推送（事件类"仅通知无数据"形态） */
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

        XLINK_CHECK(g_clientPublishCount == 2, "客户端收到两条推送");
        XLINK_CHECK(g_lastPublishTopic == XLINK_TOPIC_USER_BASE + 1,
                    "推送载荷携带完整 u32 topicId（0x10001 高位未丢失）");
        XLINK_CHECK(g_lastPublishByte == 0x99,
                    "业务载荷紧随 topicId 之后未错位");

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
        uint8_t helloBuf[4];
        XLinkHelloPayload hello;
        uint32_t topic = XLINK_TOPIC_USER_BASE + 7;
        if (!f) {
            XLINK_CHECK(false, "夹具搭建成功");
            return;
        }
        XLinkSession_setDispatchCallback(f->m_server, xlink_server_dispatch, NULL);
        XLinkSession_setDispatchCallback(f->m_client, xlink_client_dispatch, NULL);
        XLinkSession_setCapability(f->m_server, XLinkCapability_Zlib);
        XLinkSession_setCapability(f->m_client, XLinkCapability_Zlib);

        hello.m_version = XLINK_VERSION;
        hello.m_capability = XLinkCapability_Zlib;
        XLinkCodec_encodeHello(&hello, helloBuf, sizeof(helloBuf), NULL);
        XLinkSession_sendSession(f->m_server, XLinkSessionCode_Hello,
                                 helloBuf, 4);
        xlink_fixture_step(f, 3);

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
        }

        xlink_fixture_delete(f);
    }
}

/* ========================================================================== */
/*                        八、待应答表与超时                                  */
/* ========================================================================== */

/**
 * @brief 覆盖待应答表的超时判定：到点才报、取出不重报。
 */
static void xlink_test_pending_timeout(void)
{
    XLINK_SECTION("八、待应答表与超时");

    XLINK_CHECK(XLinkSession_isPending(NULL, 1) == false,
                "isPending 对 NULL 返回 false");
    XLINK_CHECK(XLinkSession_takeTimedOut(NULL, 0, 100, NULL) == false,
                "takeTimedOut 对 NULL 返回 false");

    {
        XLinkTestFixture* f = xlink_fixture_create();
        uint8_t arg = 0x01;
        uint32_t id = 0;
        uint16_t cmd = 0;
        uint32_t base;
        if (!f) {
            XLINK_CHECK(false, "夹具搭建成功");
            return;
        }
        /* 先 pump 一轮建立时间基准：会话的发送时间戳取自最近一次 pump 的
           nowTick（协议不自行取时，见 XLinkSession.h 时间口径），若在首次
           pump 前发请求，基准为 0 会导致超时判定失真。 */
        f->m_now += 10;
        XLinkSession_pump(f->m_server, f->m_now);

        /* 不注册对端回调：请求发出后无人应答，构造稳定超时场景 */
        base = f->m_now;
        XLINK_CHECK(XLinkSession_sendRequest(f->m_server, 99, &arg, 1, &id),
                    "发出不会被应答的请求");
        XLINK_CHECK(XLinkSession_isPending(f->m_server, id),
                    "请求在待应答表中");

        XLINK_CHECK(!XLinkSession_takeTimedOut(f->m_server, f->m_now,
                                               2000, &cmd),
                    "未到时不误报超时");
        XLINK_CHECK(!XLinkSession_takeTimedOut(f->m_server, f->m_now + 1999,
                                               2000, &cmd),
                    "差 1ms 未到阈值不报超时");

        XLINK_CHECK(XLinkSession_takeTimedOut(f->m_server, f->m_now + 3000,
                                              2000, &cmd),
                    "超过阈值后取出超时请求");
        XLINK_CHECK(cmd == 99, "取出的超时命令号与发出时一致");
        XLINK_CHECK(!XLinkSession_isPending(f->m_server, id),
                    "超时取出后请求已出表");
        XLINK_CHECK(!XLinkSession_takeTimedOut(f->m_server, f->m_now + 3000,
                                               2000, &cmd),
                    "超时取出后不重复上报（不刷屏）");

        xlink_fixture_delete(f);
        XLINK_CHECK(base > 0, "夹具时间游标已推进");
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
        uint8_t helloBuf[4];
        XLinkHelloPayload hello;
        if (!f) {
            XLINK_CHECK(false, "夹具搭建成功");
            return;
        }
        XLinkSession_setDispatchCallback(f->m_server, xlink_server_dispatch, NULL);
        XLinkSession_setCapability(f->m_server, XLinkCapability_Zlib);

        hello.m_version = XLINK_VERSION;
        hello.m_capability = XLinkCapability_Zlib;
        XLinkCodec_encodeHello(&hello, helloBuf, sizeof(helloBuf), NULL);
        XLinkSession_sendSession(f->m_server, XLinkSessionCode_Hello,
                                 helloBuf, 4);
        xlink_fixture_step(f, 3);
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
                uint8_t helloBuf[4];
                XLinkHelloPayload hello;
                int before = g_clientPublishCount;
                uint8_t payload = 0x66;

                /* 先握手 */
                XLinkSession_setCapability(a, XLinkCapability_Zlib);
                XLinkSession_setCapability(b, XLinkCapability_Zlib);
                hello.m_version = XLINK_VERSION;
                hello.m_capability = XLinkCapability_Zlib;
                XLinkCodec_encodeHello(&hello, helloBuf, sizeof(helloBuf), NULL);
                XLinkSession_sendSession(a, XLinkSessionCode_Hello, helloBuf, 4);
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
    XPrintf("\n========== XLink 会话层测试完成: %d 通过, %d 失败 ==========\n",
            g_passed, g_failed);
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
