/** @file       XLinkDemoClient.c
 * @brief      XLink 实战案例·客户端程序。
 * @details    客户端角色的全部代码，读这个文件就是"客户端怎么写"的答案：
 *               - 连接：XLinkClient 包装（自持 XTcpSocket + XLinkSession），
 *                 setHostPort（XLINK_DEMO_HOST 可指对端 IP，缺省本机环回）
 *                 + setCapability/setDispatchCallback 会话模板 +
 *                 setCredential 登录凭据（v1.3 剧情：连接成功后自动登录，
 *                 握手 -> Authenticating -> Ready）+ connectToServer 异步
 *                 发起；首连失败不自动重连，包装器回 Unconnected 即再发起
 *                 （重试上限 10s，自动重连保持默认关——案例是单次流程）；
 *               - 握手零样板：TCP 连上包装器自动建会话并套模板，自动
 *                 Hello/HelloAck/认证挑战应答转 Ready，stateChanged 信号
 *                 打印每次迁移；
 *               - 等 Ready 后才能发业务帧（Hello/登录之前的帧会被对端丢弃）；
 *               - 完整故事：PING（请求-应答）-> ADD（序列化）-> SLOW
 *                 （异步应答）-> 状态推送 -> 文件上传/下载逐字节校验
 *                 （v1.3 剧情：XLinkFileClient 挂接后 256KB 模式化文件
 *                 上传再下载回来，进度/终局走信号，纯事件循环驱动）->
 *                 推送与订阅全景 -> Goodbye -> 排空写队列再拆除（收尾
 *                 统一由 XLinkClient_disconnectFromServer 承担，见
 *                 XLink.md Q8）。
 *             事件循环只有 processEvents + sleep——没有任何手动 pump。
 *             入口：`--test xlink-demo-client`（双进程联调·客户端）。
 * @author      XinYueC 团队
 */
#include "XLinkDemo.h"

#if XLINK_ON && XLINK_SESSION_ON && XLINK_SERVER_ON && XLINK_CLIENT_ON

#include "XLinkCodec.h"
#include "XLinkSession.h"
#include "XLinkClient.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>

#if XLINK_FILE_ON
#include "XLinkFileClient.h"
#include "XFile.h"
#include "XDir.h"
#include "XString.h"
#endif

/* ==================== 客户端落账 ==================== */

/** 最近一条应答的命令号与载荷（分发回调落账，主流程轮询比对）。 */
static uint16_t g_cliRespCmd;
static uint8_t g_cliRespBuf[8];
static uint32_t g_cliRespLen;
/** 收到的状态推送条数与最后 counter。 */
static int g_cliStateCount;
static uint32_t g_cliLastStateCounter;
/** 收到的事件通知条数。 */
static int g_cliAlarmCount;
/** 客户端包装（等待条件需要；单流程，文件域即可）。 */
static XLinkClient* g_cliClient = NULL;
#if XLINK_AUTH_ON
/** 认证失败信号落账（正常剧情不应触发，触发即主流程失败证据）。 */
static int g_cliAuthFailures;
#endif
#if XLINK_FILE_ON
/** 文件传输客户端（对象拥有）与终局信号落账。 */
static XLinkFileClient* g_cliFile = NULL;
static bool g_fileFinished;      /**< finished 信号已到。 */
static bool g_fileOk;            /**< 终局是否成功。 */
static uint32_t g_fileActiveId;  /**< 当前传输 id（进度节流按传输复位）。 */
static uint32_t g_fileLastPercent; /**< 已打印到的进度（每 25% 一条）。 */
#endif

static void demo_client_reset(void)
{
    g_cliRespCmd = 0;
    g_cliRespLen = 0;
    g_cliStateCount = 0;
    g_cliLastStateCounter = 0;
    g_cliAlarmCount = 0;
#if XLINK_AUTH_ON
    g_cliAuthFailures = 0;
#endif
#if XLINK_FILE_ON
    g_cliFile = NULL;
    g_fileFinished = false;
    g_fileOk = false;
    g_fileActiveId = 0;
    g_fileLastPercent = 0;
#endif
}

/**
 * @brief 客户端分发回调：应答/推送落账并打印。
 * @note  会话由 XLinkClient 内部创建，本回调经 setDispatchCallback 模板
 *        套用到每次新建的会话上，签名与 XLinkSession 直挂时完全一致。
 */
static void demo_client_dispatch(XLinkSession* self,
                                 const XLinkFrameHeader* frame,
                                 const uint8_t* payload)
{
    (void)self;
    if (!frame)
        return;

    if (frame->m_msgClass == XLinkMsgClass_Response) {
        g_cliRespCmd = frame->m_msgCode;
        g_cliRespLen = frame->m_payloadLen < sizeof(g_cliRespBuf)
                           ? frame->m_payloadLen : sizeof(g_cliRespBuf);
        if (payload && g_cliRespLen)
            memcpy(g_cliRespBuf, payload, g_cliRespLen);
    } else if (frame->m_msgClass == XLinkMsgClass_Publish) {
        uint32_t topic = 0;
        uint32_t tick = 0;
        if (payload && frame->m_payloadLen >= 4)
            XMemory_read_data(payload, XBYTE_ORDER_LITTLE_ENDIAN,
                              (uint8_t*)&topic, sizeof(topic));
        if (topic == XLINK_DEMO_TOPIC_STATE && frame->m_payloadLen >= 12) {
            g_cliStateCount++;
            XMemory_read_data(&payload[8], XBYTE_ORDER_LITTLE_ENDIAN,
                              (uint8_t*)&g_cliLastStateCounter,
                              sizeof(g_cliLastStateCounter));
            XMemory_read_data(&payload[4], XBYTE_ORDER_LITTLE_ENDIAN,
                              (uint8_t*)&tick, sizeof(tick));
            DEMO_LOG("client", "状态推送 #%d tick=%u counter=%u",
                     g_cliStateCount, tick, g_cliLastStateCounter);
        } else if (topic == XLINK_DEMO_TOPIC_ALARM) {
            g_cliAlarmCount++;
            DEMO_LOG("client", "事件通知：%.*s",
                     (int)(frame->m_payloadLen - 4),
                     (const char*)&payload[4]);
        }
    }
}

/** @brief 等待条件：应答已到达。 */
static bool demo_cond_resp(void) { return g_cliRespCmd != 0; }

/** @brief 等待条件：握手完成（isReady）。 */
static bool demo_cond_ready(void)
{
    return g_cliClient && XLinkClient_isReady(g_cliClient);
}

/** @brief 等待条件：TCP 已连上（包装器自动建会话）。 */
static bool demo_cond_session(void)
{
    return g_cliClient && XLinkClient_session(g_cliClient) != NULL;
}

/** @brief 等待条件：状态推送已收到 3 条。 */
static bool demo_cond_state(void) { return g_cliStateCount >= 3; }

/** @brief 等待条件：ALARM 事件已收到。 */
static bool demo_cond_alarm(void) { return g_cliAlarmCount >= 1; }

#if XLINK_FILE_ON
/** @brief 等待条件：文件传输终局信号已到。 */
static bool demo_cond_file_finished(void) { return g_fileFinished; }
#endif

/**
 * @brief 状态迁移槽：stateChanged 信号的发射式消费（对齐 XModbus 的
 *        信号面用法），打印每次连接状态迁移。
 * @note  参数解包类型与 XLinkClient_stateChanged_signal 发射侧逐一对应
 *        （XVarList 按创建时的类型与大小连续存储）。
 */
static void demo_client_on_state(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, XLinkClientState, state);
    switch (state) {
    case XLinkClient_UnconnectedState:
        DEMO_LOG0("client", "状态迁移 -> 未连接");
        break;
    case XLinkClient_ConnectingState:
        DEMO_LOG0("client", "状态迁移 -> 连接中");
        break;
    case XLinkClient_ConnectedState:
        DEMO_LOG0("client", "状态迁移 -> 已连接（会话就绪）");
        break;
    case XLinkClient_ReconnectingState:
        DEMO_LOG0("client", "状态迁移 -> 自动重连中");
        break;
    default:
        break;
    }
}

#if XLINK_AUTH_ON
/**
 * @brief 认证失败槽：authFailed 信号落账并打印。
 * @note  正常剧情（凭据正确）不会触发；触发即说明服务端账户/策略出了问题，
 *        主流程将因 Ready 等待超时而失败，这里负责留下第一现场。
 */
static void demo_client_on_auth_failed(XObject* sender, XVarList* args)
{
    XVarList_args_2(args, int, reason, uint8_t, status);
    (void)sender;
    g_cliAuthFailures++;
    DEMO_LOG("client", "认证失败（reason=%d status=%u）",
             reason, (unsigned)status);
}
#endif

#if XLINK_FILE_ON
/**
 * @brief 文件传输进度槽：progress 信号落账，每 25% 打一条日志。
 * @note  参数与 XLinkFileClient_progress_signal 发射侧逐一对应
 *        （transferId、done、total）。
 */
static void demo_client_on_file_progress(XObject* sender, XVarList* args)
{
    XVarList_args_3(args, uint32_t, transferId, uint64_t, done,
                    uint64_t, total);
    (void)sender;
    if (transferId != g_fileActiveId) {
        /* 新传输开跑：进度节流重新计。 */
        g_fileActiveId = transferId;
        g_fileLastPercent = 0;
    }
    if (total == 0)
        return;
    {
        uint32_t percent = (uint32_t)(done * 100u / total);
        if (percent - g_fileLastPercent >= 25u) {
            g_fileLastPercent = percent;
            DEMO_LOG("client", "文件传输进度 %u%%（%u/%u 字节，id=%u）",
                     percent, (unsigned)done, (unsigned)total, transferId);
        }
    }
}

/**
 * @brief 文件传输终局槽：finished 信号落账（成功/失败各一次）。
 * @note  参数与 XLinkFileClient_finished_signal 发射侧逐一对应
 *        （transferId、ok、status、text）。
 */
static void demo_client_on_file_finished(XObject* sender, XVarList* args)
{
    XVarList_args_4(args, uint32_t, transferId, bool, ok, uint8_t, status,
                    const char*, text);
    (void)sender;
    g_fileFinished = true;
    g_fileOk = ok;
    DEMO_LOG("client", "文件传输终局 id=%u %s（status=%u %s）",
             transferId, ok ? "成功" : "失败", (unsigned)status,
             (text && text[0]) ? text : "-");
}

/** @brief 文件辅助：删除本地临时文件（不存在为安全空操作）。 */
static void demo_client_remove_local(const char* path)
{
    XString* p = XString_create_utf8(path);
    if (!p)
        return;
    (void)XDir_removePath_static(p);
    XClassDelete((XClass*)p);
}

/** @brief 文件辅助：生成模式化测试文件（字节模式 i*31+7，确定可复现）。 */
static bool demo_client_make_test_file(const char* path)
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
    /* 写入要带 Create|Truncate：残留旧文件不能影响本次校验。 */
    if (!XFile_open_2(file, XIODevice_WriteOnly | XIODevice_Create
                             | XIODevice_Truncate, 0)) {
        XClassDelete((XClass*)file);
        return false;
    }
    buffer = (uint8_t*)XMalloc_System(XLINK_DEMO_FILE_BYTES);
    if (!buffer) {
        XClassDelete((XClass*)file);
        return false;
    }
    for (i = 0; i < XLINK_DEMO_FILE_BYTES; ++i)
        buffer[i] = (uint8_t)(i * 31u + 7u);
    written = XIODevice_write_1((XIODevice*)file, (const char*)buffer,
                                (int64_t)XLINK_DEMO_FILE_BYTES);
    XFree_System(buffer);
    XFile_flush(file);
    XClassDelete((XClass*)file);
    return written == (int64_t)XLINK_DEMO_FILE_BYTES;
}

/** @brief 文件辅助：整读文件进内存（调用方 XFree_System 释放）。 */
static uint8_t* demo_client_read_file(const char* path, int64_t* outLen)
{
    XString* name;
    XFile* file;
    int64_t size;
    uint8_t* buffer;
    int64_t got;

    *outLen = -1;
    name = XString_create_utf8(path);
    if (!name)
        return NULL;
    file = XFile_create_2(name);
    XClassDelete((XClass*)name);
    if (!file)
        return NULL;
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0)) {
        XClassDelete((XClass*)file);
        return NULL;
    }
    size = (int64_t)XFile_size_base(file);
    buffer = (uint8_t*)XMalloc_System((size_t)(size > 0 ? size : 1));
    if (!buffer) {
        XClassDelete((XClass*)file);
        return NULL;
    }
    got = XIODevice_read_1((XIODevice*)file, (char*)buffer, size);
    XClassDelete((XClass*)file);
    if (got != size) {
        XFree_System(buffer);
        return NULL;
    }
    *outLen = size;
    return buffer;
}
#endif /* XLINK_FILE_ON */

/**
 * @brief 客户端通用等待：有界 processEvents 泵，条件成立或超时即返回。
 * @note  exec 入口（XEventLoop_exec）在本框架当前版本存在定时器派发
 *        缺陷（Debug 下 XTimer 注册成功但 exec 内不触发，已带探针证据
 *        立项跟进），故等待采用有界 processEvents 泵——事件照常全量
 *        派发（XLinkClient 的套接字信号/会话内建定时器、服务端周期
 *        业务 tick 均经同一泵触发，本进程联调时两角色共用此泵）。
 */
static bool demo_client_wait(bool (*cond)(void), uint32_t ms)
{
    uint64_t start = XDateTime_currentMSecsSinceEpoch();
    while (!cond()) {
        if (XDateTime_currentMSecsSinceEpoch() - start >= ms)
            return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
    }
    return true;
}

/* ==================== 客户端完整故事 ==================== */

int XLinkDemo_client_flow(uint16_t port)
{
    XLinkSession* session = NULL;
    uint64_t start;
    uint64_t lastAttempt;
    uint32_t reqId = 0;
    uint32_t wire32 = 0;
    uint8_t out[8];
    const char* host = XLinkDemo_envOr(XLINK_DEMO_HOST_ENV, "127.0.0.1");

    demo_client_reset();
    DEMO_LOG("client", "连接 %s:%u（未起服务端则重试最多 10s）",
             host, (unsigned)port);

    /* 建客户端：连接参数 + 会话模板一次配好，连接/建会话/握手全部交给
       包装器（原 XTcpSocket 手动重试连接 + XLinkSession_create_ex +
       setCapability/setDispatchCallback 组装样板已由 XLinkClient 收编）。 */
    g_cliClient = XLinkClient_create();
    if (!g_cliClient) {
        DEMO_LOG0("client", "XLinkClient 创建失败");
        return 1;
    }
    XLinkClient_setHostPort(g_cliClient, host, port);
    /* 能力位：文件传输必须连接前声明（能力协商在握手期，事后声明无效——
       服务端挂了文件服务端会对 File 帧路由，但客户端不宣告该位则交集
       不含 FileTransfer，会话层会直接丢弃 File 帧）。 */
    XLinkClient_setCapability(g_cliClient, XLinkCapability_Zlib
#if XLINK_FILE_ON
                              | XLinkCapability_FileTransfer
#endif
    );
    XLinkClient_setDispatchCallback(g_cliClient, demo_client_dispatch, NULL);
#if XLINK_AUTH_ON
    /* 剧情：登录凭据连接前设好——每次连接/重连成功后自动登录（握手后进
       Authenticating，凭据通过服务端挑战应答才转 Ready）。 */
    if (!XLinkClient_setCredential(g_cliClient, XLINK_DEMO_USER,
                                   XLINK_DEMO_PASSWORD)) {
        DEMO_LOG0("client", "设置登录凭据失败");
        goto fail;
    }
    XObject_connect_2((XObject*)g_cliClient,
                      XSignal(XLinkClient_authFailed_signal),
                      demo_client_on_auth_failed);
    DEMO_LOG("client", "已设置登录凭据（user=%s，认证自动完成）",
             XLINK_DEMO_USER);
#endif
    /* 自动重连保持默认关：案例是单次流程，连接失败由下面的重试循环兜底。 */
    /* 发射式消费：接 stateChanged 信号，打印每次状态迁移。 */
    XObject_connect_2((XObject*)g_cliClient,
                      XSignal(XLinkClient_stateChanged_signal),
                      demo_client_on_state);

    /* 带重试的连接：客户端可能先于服务端启动。首连失败包装器不自动重连
       （回 UnconnectedState），限频 500ms 再发起；TCP 连上后包装器自动
       建会话，demo_cond_session 成立即出循环。 */
    start = XDateTime_currentMSecsSinceEpoch();
    lastAttempt = 0;
    while (!demo_cond_session()
           && XDateTime_currentMSecsSinceEpoch() - start
                  < XLINK_DEMO_CLIENT_WAIT_MS) {
        uint64_t now = XDateTime_currentMSecsSinceEpoch();
        if (XLinkClient_state(g_cliClient) == XLinkClient_UnconnectedState
            && now - lastAttempt >= 500) {
            XLinkClient_connectToServer(g_cliClient);
            lastAttempt = now;
        }
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
    }
    if (!demo_cond_session()) {
        DEMO_LOG0("client", "连接失败（服务端没起？先跑 --test xlink-demo-server）");
        goto fail;
    }
    session = XLinkClient_session(g_cliClient);
    DEMO_LOG0("client", "TCP 已连接，建会话（不手动发 Hello）");

    /* 等 Ready：会话挂载传输后自动补发 Hello，对端自动回 HelloAck。
       业务帧必须等握手完成再发——Hello 之前的帧会被对端丢弃。 */
    if (!demo_client_wait(demo_cond_ready, 3000)) {
        DEMO_LOG0("client", "3s 内未完成握手");
        goto fail;
    }
    DEMO_LOG("client", "握手完成（自动 Hello/HelloAck，negotiated=0x%04x）",
             XLinkSession_negotiatedCapability(session));
#if XLINK_AUTH_ON
    /* 认证剧情检查点：Ready 本身已含"认证通过"（Authenticating 不算
       Ready），这里显式确认一次并留日志，剧情闭环更直观。 */
    if (!XLinkClient_isAuthenticated(g_cliClient)) {
        DEMO_LOG0("client", "会话未通过登录认证（服务端认证剧情未达成）");
        goto fail;
    }
    DEMO_LOG("client", "已通过服务端登录认证（user=%s），进入授权通道",
             XLinkClient_user_const(g_cliClient));
#endif
    XLinkSession_subscribe(session, XLINK_DEMO_TOPIC_STATE);

    /* --- 1) PING：最小请求-应答 --- */
    g_cliRespCmd = 0;
    wire32 = 42u;
    XMemory_write_data(out, XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&wire32, sizeof(wire32));
    if (!XLinkSession_sendRequest(session, XLINK_DEMO_CMD_PING, out, 4,
                                  &reqId)) {
        DEMO_LOG0("client", "PING 下发失败");
        goto fail;
    }
    DEMO_LOG("client", "CMD_PING seq=42 已下发（reqId=%u）", reqId);
    if (!demo_client_wait(demo_cond_resp, 2000)
        || g_cliRespCmd != XLINK_DEMO_CMD_PING || g_cliRespLen < 1
        || g_cliRespBuf[0] != 1) {
        DEMO_LOG0("client", "PING 未收到 pong");
        goto fail;
    }
    DEMO_LOG0("client", "收到 pong —— 请求-应答成立");

    /* --- 2) ADD：载荷序列化往返 --- */
    g_cliRespCmd = 0;
    wire32 = 3u;
    XMemory_write_data(out, XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&wire32, sizeof(wire32));
    wire32 = 4u;
    XMemory_write_data(&out[4], XBYTE_ORDER_LITTLE_ENDIAN,
                       (const uint8_t*)&wire32, sizeof(wire32));
    XLinkSession_sendRequest(session, XLINK_DEMO_CMD_ADD, out, 8, &reqId);
    DEMO_LOG("client", "CMD_ADD 3+4 已下发（reqId=%u）", reqId);
    if (!demo_client_wait(demo_cond_resp, 2000)
        || g_cliRespCmd != XLINK_DEMO_CMD_ADD || g_cliRespLen < 4) {
        DEMO_LOG0("client", "ADD 未收到 3+4=7");
        goto fail;
    }
    wire32 = 0;
    XMemory_read_data(g_cliRespBuf, XBYTE_ORDER_LITTLE_ENDIAN,
                      (uint8_t*)&wire32, sizeof(wire32));
    if (wire32 != 7u) {
        DEMO_LOG0("client", "ADD 未收到 3+4=7");
        goto fail;
    }
    DEMO_LOG0("client", "收到 3+4=7 —— 序列化无损");

    /* --- 3) SLOW：异步应答（600ms 后补回执，期间状态推送照常到达） --- */
    g_cliRespCmd = 0;
    XLinkSession_sendRequest(session, XLINK_DEMO_CMD_SLOW, NULL, 0, &reqId);
    DEMO_LOG("client", "CMD_SLOW 已下发（reqId=%u）", reqId);
    if (!demo_client_wait(demo_cond_resp, 3000)
        || g_cliRespCmd != XLINK_DEMO_CMD_SLOW) {
        DEMO_LOG0("client", "SLOW 未收到异步回执");
        goto fail;
    }
    DEMO_LOG0("client", "收到异步回执 —— 异步应答模式成立");

    /* --- 4) 状态推送：订阅后应持续到达 --- */
    if (!demo_client_wait(demo_cond_state, 3000) || g_cliStateCount < 3) {
        DEMO_LOG("client", "状态推送不足（%d 条）", g_cliStateCount);
        goto fail;
    }
    DEMO_LOG("client", "状态推送持续到达（%d 条，最后 counter=%u）",
             g_cliStateCount, g_cliLastStateCounter);

    /* --- 5) ALARM 事件：SLOW 的伴生通知 --- */
    if (demo_client_wait(demo_cond_alarm, 1000))
        DEMO_LOG0("client", "ALARM 事件已收到");
    else
        DEMO_LOG0("client", "ALARM 事件未收到");

#if XLINK_FILE_ON
    /* --- 6) 文件上传/下载：授权通道上的 256KB 模式化文件往返 ---
       上传 -> 下载回来 -> 逐字节比对。驱动完全交给事件循环：传输引擎有
       内建 10ms 节拍（活跃期间自动启停），会话收发由 XLinkClient 的
       定时器/套接字信号驱动——不手动调 XLinkFileClient_pump（它会显式
       pump 会话，与包装器内建驱动叠加成双驱动，见 XLink.md §14.3）。 */
    {
        uint32_t transferId = 0;
        uint8_t* srcData = NULL;
        uint8_t* dstData = NULL;
        int64_t srcLen;
        int64_t dstLen;
        uint64_t off;
        int mismatch = 0;

        DEMO_LOG("client", "生成模式化测试文件 %s（%uKB）",
                 XLINK_DEMO_LOCAL_UPLOAD,
                 (unsigned)(XLINK_DEMO_FILE_BYTES / 1024u));
        if (!demo_client_make_test_file(XLINK_DEMO_LOCAL_UPLOAD)) {
            DEMO_LOG0("client", "测试文件生成失败");
            goto fail;
        }
        g_cliFile = XLinkFileClient_create();
        if (!g_cliFile || !XLinkFileClient_attach(g_cliFile, g_cliClient)) {
            DEMO_LOG0("client", "XLinkFileClient 创建/挂接失败");
            goto fail;
        }
        XObject_connect_2((XObject*)g_cliFile,
                          XSignal(XLinkFileClient_progress_signal),
                          demo_client_on_file_progress);
        XObject_connect_2((XObject*)g_cliFile,
                          XSignal(XLinkFileClient_finished_signal),
                          demo_client_on_file_finished);

        /* 6a 上传（flags bit0 = 允许覆盖远端，与服务端 ALLOW_OVERWRITE 对应） */
        g_fileFinished = false;
        g_fileOk = false;
        if (!XLinkFileClient_upload(g_cliFile, XLINK_DEMO_LOCAL_UPLOAD,
                                    XLINK_DEMO_FILE_NAME, 1u, &transferId)) {
            DEMO_LOG0("client", "上传发起失败");
            goto fail;
        }
        DEMO_LOG("client", "上传已发起（id=%u -> 远端 %s）",
                 transferId, XLINK_DEMO_FILE_NAME);
        if (!demo_client_wait(demo_cond_file_finished, XLINK_DEMO_FILE_WAIT_MS)
            || !g_fileOk
            || XLinkFileClient_state(g_cliFile)
                   != XLinkFileTransferState_Done) {
            DEMO_LOG0("client", "上传未在时限内完成");
            goto fail;
        }

        /* 6b 下载回来（flags bit0 = 允许覆盖本地旧文件） */
        g_fileFinished = false;
        g_fileOk = false;
        if (!XLinkFileClient_download(g_cliFile, XLINK_DEMO_FILE_NAME,
                                      XLINK_DEMO_LOCAL_DOWNLOAD, 1u,
                                      &transferId)) {
            DEMO_LOG0("client", "下载发起失败");
            goto fail;
        }
        DEMO_LOG("client", "下载已发起（id=%u <- 远端 %s）",
                 transferId, XLINK_DEMO_FILE_NAME);
        if (!demo_client_wait(demo_cond_file_finished, XLINK_DEMO_FILE_WAIT_MS)
            || !g_fileOk
            || XLinkFileClient_state(g_cliFile)
                   != XLinkFileTransferState_Done) {
            DEMO_LOG0("client", "下载未在时限内完成");
            goto fail;
        }

        /* 6c 逐字节校验：下载件与原文件长度一致且内容零差异。 */
        srcData = demo_client_read_file(XLINK_DEMO_LOCAL_UPLOAD, &srcLen);
        dstData = demo_client_read_file(XLINK_DEMO_LOCAL_DOWNLOAD, &dstLen);
        if (srcLen != (int64_t)XLINK_DEMO_FILE_BYTES || srcData == NULL
            || dstLen != srcLen || dstData == NULL) {
            DEMO_LOG("client", "文件大小不符（原 %d 下载 %d）",
                     (int)srcLen, (int)dstLen);
            if (srcData)
                XFree_System(srcData);
            if (dstData)
                XFree_System(dstData);
            goto fail;
        }
        for (off = 0; off < (uint64_t)srcLen; ++off) {
            if (srcData[off] != dstData[off]) {
                DEMO_LOG("client", "逐字节校验失败：偏移 %u 处不一致"
                         "（原 0x%02x 下载 0x%02x）",
                         (unsigned)off, srcData[off], dstData[off]);
                mismatch = 1;
                break;
            }
        }
        if (mismatch) {
            XFree_System(srcData);
            XFree_System(dstData);
            goto fail;
        }
        DEMO_LOG("client", "下载文件与原文件逐字节一致（%u 字节）"
                 "——文件通道闭环", (unsigned)srcLen);
        XFree_System(srcData);
        XFree_System(dstData);
        XClassDelete((XClass*)g_cliFile);
        g_cliFile = NULL;
    }
#endif

    /* --- 7) 推送与订阅全景：反向推送/空载荷/超限拒绝/幂等订阅/退订 --- */
    {
        static uint8_t bigBuf[XLINK_MAX_PAYLOAD - 3]; /* 比业务上限多 1 字节 */

        /* 6a 客户端 -> 服务端反向推送（Publish 谁发都行），带业务载荷 */
        if (!XLinkSession_sendPublish(session, XLINK_DEMO_TOPIC_STATE,
                                      (const uint8_t*)"HEARTBEAT", 9)) {
            DEMO_LOG0("client", "反向推送下发失败");
            goto fail;
        }
        /* 6b 空载荷推送（"仅通知无数据"事件形态，服务端应收到 0 字节业务载荷） */
        if (!XLinkSession_sendPublish(session, XLINK_DEMO_TOPIC_STATE,
                                      NULL, 0)) {
            DEMO_LOG0("client", "空载荷推送下发失败");
            goto fail;
        }
        /* 6c 业务载荷超上限：拒绝且链路不受影响 */
        if (XLinkSession_sendPublish(session, XLINK_DEMO_TOPIC_STATE,
                                     bigBuf, sizeof(bigBuf))) {
            DEMO_LOG0("client", "超限推送不应成功");
            goto fail;
        }
        if (!XLinkSession_isReady(session)) {
            DEMO_LOG0("client", "超限拒绝后链路应保持健康");
            goto fail;
        }
        DEMO_LOG0("client", "反向/空载荷推送已发，超限推送被拒且链路健康");

        /* 6d 订阅第二主题 + 重复订阅幂等（对端只应收到一次 Subscribe 帧） */
        if (!XLinkSession_subscribe(session, XLINK_DEMO_TOPIC_ALARM)
            || !XLinkSession_subscribe(session, XLINK_DEMO_TOPIC_ALARM)) {
            DEMO_LOG0("client", "重复订阅应幂等成功");
            goto fail;
        }
        /* 6e 退订：一次生效、二次被拒、订阅表移除（对端各计一次帧） */
        if (!XLinkSession_unsubscribe(session, XLINK_DEMO_TOPIC_ALARM)
            || XLinkSession_unsubscribe(session, XLINK_DEMO_TOPIC_ALARM)) {
            DEMO_LOG0("client", "退订语义不符（二次退订应被拒）");
            goto fail;
        }
        if (XLinkSession_isSubscribed(session, XLINK_DEMO_TOPIC_ALARM)) {
            DEMO_LOG0("client", "退订后订阅表应已移除");
            goto fail;
        }
        if (!XLinkSession_unsubscribe(session, XLINK_DEMO_TOPIC_STATE)) {
            DEMO_LOG0("client", "退订 STATE 失败");
            goto fail;
        }
        DEMO_LOG0("client", "推送与订阅全景通过（对端计数见服务端日志）");
    }

    /* --- 收尾：Goodbye + 排空写队列再拆除 --- */
    DEMO_LOG("client", "%s", g_cliAlarmCount >= 1
                                 ? "全部步骤完成，发送 Goodbye"
                                 : "主流程完成，发送 Goodbye");
    /* 收尾三件事（Goodbye + 排空写队列 + 拆除会话与套接字）已收编进
       disconnectFromServer：套接字的写是异步管线（write 返回 true 只表示
       已入队），不排空就拆会把未 flush 的帧连同对象一起丢掉——本案例曾
       因此丢过 Goodbye 前的一串帧（见 XLink.md Q8）。 */
    XLinkClient_disconnectFromServer(g_cliClient);
    XClassDelete((XClass*)g_cliClient);
    g_cliClient = NULL;
#if XLINK_FILE_ON
    /* 本地临时文件用完即删（上传源 + 下载件；远端沙箱由服务端退出清理）。 */
    demo_client_remove_local(XLINK_DEMO_LOCAL_UPLOAD);
    demo_client_remove_local(XLINK_DEMO_LOCAL_DOWNLOAD);
#endif
    DEMO_LOG0("client", "exit 0");
    return 0;

fail:
    DEMO_LOG0("client", "失败，exit 1");
#if XLINK_FILE_ON
    if (g_cliFile) {
        XLinkFileClient_abort(g_cliFile, XLinkFileStatus_Aborted, "cleanup");
        XClassDelete((XClass*)g_cliFile);
        g_cliFile = NULL;
    }
    demo_client_remove_local(XLINK_DEMO_LOCAL_UPLOAD);
    demo_client_remove_local(XLINK_DEMO_LOCAL_DOWNLOAD);
#endif
    if (g_cliClient) {
        XLinkClient_disconnectFromServer(g_cliClient);
        XClassDelete((XClass*)g_cliClient);
        g_cliClient = NULL;
    }
    return 1;
}

int XLinkDemoClient_run(void)
{
    return XLinkDemo_client_flow(XLINK_DEMO_PORT);
}

void XLinkDemoClient(void)
{
    (void)XLinkDemoClient_run();
}

#endif /* XLINK_ON && XLINK_SESSION_ON && XLINK_SERVER_ON && XLINK_CLIENT_ON */
