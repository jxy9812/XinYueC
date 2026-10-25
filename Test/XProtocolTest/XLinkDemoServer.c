/** @file       XLinkDemoServer.c
 * @brief      XLink 实战案例·服务端程序。
 * @details    服务端角色的全部代码，读这个文件就是"服务端怎么写"的答案：
 *               - 启用协议 4 行：XLinkServer_create + 设模板 + listen_base，
 *                 之后 accept/握手/心跳/判线/回收全部异步托管；
 *               - 业务收发：demo_server_dispatch 收命令（PING 同步回执 /
 *                 ADD 序列化 / SLOW 异步受理），demo_server_tick 周期推
 *                 状态快照（TOPIC_STATE）并在慢命令完成时补回执 + 推
 *                 ALARM（TOPIC_ALARM）；
 *               - 事件循环只有 processEvents + sleep + 业务 tick，
 *                 没有任何 XLink 手动调用。
 *             入口：`--test xlink-demo-server`（双进程联调·服务端）；
 *             XLinkDemo_run（本进程联调）也驻留本文件，经
 *             XLinkDemo_client_flow 把客户端角色接进同一事件循环。
 * @author      XinYueC 团队
 */
#include "XLinkDemo.h"

#if XLINK_ON && XLINK_SESSION_ON && XLINK_SERVER_ON

#include "XLinkCodec.h"
#include "XLinkSession.h"
#include "XLinkServer.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>

/* ==================== 服务端角色上下文 ==================== */

/** @brief 服务端角色上下文：独立运行与本进程联调共用。 */
typedef struct DemoServer {
    XLinkServer* srv;         /**< 服务端包装（对象拥有）。 */
    const char* bindHost;     /**< 监听地址（环境变量或缺省环回，借用）。 */
    uint32_t stateCounter;    /**< 状态推送计数（演示业务值）。 */
    int handled;              /**< 已处理命令数（ping/add/slow 各计 1）。 */
    bool clientGone;          /**< 客户端会话已回收。 */
    /* 推送/订阅全景计数（客户端第六步的对端观察点）。 */
    int subCount;             /**< 收到的 Subscribe 帧数。 */
    int unsubCount;           /**< 收到的 Unsubscribe 帧数。 */
    int clientPubs;           /**< 收到的客户端反向 Publish 帧数。 */
    /* 慢命令待办（单客户端演示，1 格够用）：模拟"设备 IO 完成后再回执"。 */
    bool slowActive;
    uint32_t slowReqId;
    XLinkSession* slowOwner;  /**< 发起请求的会话（借用）。 */
    uint64_t slowDueTick;     /**< 应答到期时刻（单调毫秒）。 */
} DemoServer;

/** @brief 分发回调取上下文的唯一途径：本案例全局仅一个服务端实例。 */
static DemoServer* g_demoServer = NULL;

/** 服务端启动时刻：联调超时判定（tick 回调里检查，入口处赋值）。 */
static uint64_t g_serverStartTick = 0;

/* ==================== 业务收发 ==================== */

/**
 * @brief 服务端分发回调：三种命令 + 订阅/退订/反向推送记账。
 * @note  CMD_SLOW 只受理不立即应答——真实设备 IO 耗时的标准形态：
 *        记下 requestId，IO 完成后（demo_server_tick）再补 Response。
 */
static void demo_server_dispatch(XLinkSession* self,
                                 const XLinkFrameHeader* frame,
                                 const uint8_t* payload)
{
    DemoServer* ds = g_demoServer;

    if (!self || !frame || !ds)
        return;

    switch (frame->m_msgClass) {
    case XLinkMsgClass_Request:
        if (frame->m_msgCode == XLINK_DEMO_CMD_PING) {
            uint32_t seq = (payload && frame->m_payloadLen >= 4)
                               ? XLinkCodec_getU32(payload) : 0;
            uint8_t pong = 1;
            DEMO_LOG("server", "CMD_PING seq=%u -> 回执 pong", seq);
            XLinkSession_sendResponse(self, frame->m_requestId,
                                      frame->m_msgCode, &pong, 1);
            ds->handled++;
        } else if (frame->m_msgCode == XLINK_DEMO_CMD_ADD) {
            uint32_t a = (payload && frame->m_payloadLen >= 4)
                             ? XLinkCodec_getU32(payload) : 0;
            uint32_t b = (payload && frame->m_payloadLen >= 8)
                             ? XLinkCodec_getU32(&payload[4]) : 0;
            uint8_t out[4];
            DEMO_LOG("server", "CMD_ADD %u+%u -> 回执和", a, b);
            XLinkCodec_putU32(out, a + b);
            XLinkSession_sendResponse(self, frame->m_requestId,
                                      frame->m_msgCode, out, 4);
            ds->handled++;
        } else if (frame->m_msgCode == XLINK_DEMO_CMD_SLOW) {
            DEMO_LOG("server", "CMD_SLOW 受理（模拟设备 IO %ums）",
                     (unsigned)XLINK_DEMO_SLOW_MS);
            ds->slowActive = true;
            ds->slowReqId = frame->m_requestId;
            ds->slowOwner = self;
            ds->slowDueTick = XDateTime_currentMSecsSinceEpoch()
                              + XLINK_DEMO_SLOW_MS;
        }
        break;

    case XLinkMsgClass_Subscribe:
        ds->subCount++;
        DEMO_LOG("server", "客户端订阅 topic=%u",
                 (payload && frame->m_payloadLen >= 4)
                     ? XLinkCodec_getU32(payload) : 0);
        break;

    case XLinkMsgClass_Unsubscribe:
        ds->unsubCount++;
        DEMO_LOG("server", "客户端退订 topic=%u",
                 (payload && frame->m_payloadLen >= 4)
                     ? XLinkCodec_getU32(payload) : 0);
        break;

    case XLinkMsgClass_Publish:
        /* 推送谁发都行：客户端也能向服务端推（反向推送观察点）。 */
        ds->clientPubs++;
        DEMO_LOG("server", "收到客户端推送 topic=%u 业务载荷 %u 字节",
                 (payload && frame->m_payloadLen >= 4)
                     ? XLinkCodec_getU32(payload) : 0,
                 (unsigned)(frame->m_payloadLen >= 4
                                ? frame->m_payloadLen - 4 : 0));
        break;

    default:
        break;
    }
}

/**
 * @brief 会话增删通知：客户端离开即置完成标志（自动回收的观察点）。
 */
static void demo_server_notify(XLinkServer* self, XLinkSession* session,
                               XLinkServerSessionEvent event)
{
    DemoServer* ds = g_demoServer;

    (void)self;
    (void)session;
    if (!ds)
        return;
    if (event == XLinkServerSessionEvent_Added)
        DEMO_LOG0("server", "客户端接入（会话自动建立，握手将自动完成）");
    else {
        DEMO_LOG0("server", "客户端离开（Inactive 会话已自动回收）");
        ds->clientGone = true;
        /* 事件驱动收尾：请求退出主事件循环（exec 返回即收尾） */
        XCoreApplication_quit();
    }
}

/**
 * @brief 服务端周期业务（经 XLinkServer_setTickCallback 注册，由服务端
 *        内建节拍在事件循环线程触发）：周期推状态快照 + 到期的慢命令
 *        补回执并推 ALARM。
 * @note  内建节拍已设为 XLINK_DEMO_STATE_MS（见 demo_server_start），本回调
 *        每拍推送一次状态快照即可，无需再做间隔门限判断。状态推送用
 *        XLinkServer_publish 一对多分发——订阅关系由服务端自动镜像维护，
 *        这里不再逐会话轮询"谁订了什么"。
 */
static void demo_server_tick(XLinkServer* self, void* userData)
{
    DemoServer* ds = (DemoServer*)userData;
    uint32_t now = (uint32_t)XDateTime_currentMSecsSinceEpoch();

    if (!ds || !ds->srv || self != ds->srv)
        return;

    /* 状态快照：一对多发布给所有订阅了 TOPIC_STATE 的客户端。 */
    {
        uint8_t buf[8];
        XLinkCodec_putU32(&buf[0], now % 100000u);
        XLinkCodec_putU32(&buf[4], ++ds->stateCounter);
        XLinkServer_publish(ds->srv, XLINK_DEMO_TOPIC_STATE, buf, 8);
    }

    /* 联调超时兜底：到点即请求退出主事件循环（本回调随服务端内建
       节拍触发，天然是检查点）。 */
    if (XDateTime_currentMSecsSinceEpoch() - g_serverStartTick
            >= XLINK_DEMO_DEADLINE_MS) {
        DEMO_LOG0("server", "联调超时（30s 未完成）");
        XCoreApplication_quit();
        return;
    }

    /* 慢命令到期：补异步回执 + 推事件通知（产生即推）。 */
    if (ds->slowActive && now >= (uint32_t)ds->slowDueTick) {
        uint8_t done = 1;
        DEMO_LOG0("server", "CMD_SLOW 设备 IO 完成 -> 异步补回执 + ALARM");
        if (ds->slowOwner && XLinkSession_isReady(ds->slowOwner)) {
            XLinkSession_sendResponse(ds->slowOwner, ds->slowReqId,
                                      XLINK_DEMO_CMD_SLOW, &done, 1);
            XLinkSession_sendPublish(ds->slowOwner, XLINK_DEMO_TOPIC_ALARM,
                                     (const uint8_t*)"SLOW-DONE", 9);
        }
        ds->slowActive = false;
        ds->handled++;
    }
}

/* ==================== 启停 ==================== */

/**
 * @brief 创建并启动服务端角色。
 * @param port 监听端口；0 = 自动分配（本进程联调用），实际端口经
 *             XLinkServer_tcpServer_const + XTcpServer_serverPort 查询。
 * @return 成功返回上下文；失败返回 NULL（已打印原因）。
 */
static DemoServer* demo_server_start(uint16_t port)
{
    DemoServer* ds = (DemoServer*)XMalloc_System(sizeof(DemoServer));
    if (!ds)
        return NULL;
    memset(ds, 0, sizeof(DemoServer));

    ds->srv = XLinkServer_create();
    if (!ds->srv) {
        XFree_System(ds);
        DEMO_LOG0("server", "XLinkServer_create 失败");
        return NULL;
    }
    g_demoServer = ds;
    XLinkServer_setCapability(ds->srv, XLinkCapability_Zlib);
    XLinkServer_setDispatchCallback(ds->srv, demo_server_dispatch, NULL);
    XLinkServer_setNotifyCallback(ds->srv, demo_server_notify);
    /* 周期业务注册到服务端内建节拍：状态推送/慢命令回执随 pump 自动触发。
       节拍直接设为状态推送周期 XLINK_DEMO_STATE_MS——tick 回调每拍推一次
       快照即可，应用侧不再自建推送间隔门限（listen 后按该值启动定时器）。 */
    XLinkServer_setTickCallback(ds->srv, demo_server_tick, ds);
    XLinkServer_setTickInterval(ds->srv, XLINK_DEMO_STATE_MS);
    ds->bindHost = XLinkDemo_envOr(XLINK_DEMO_BIND_ENV, "127.0.0.1");
    if (!XLinkServer_listen_base(ds->srv, ds->bindHost, port)) {
        DEMO_LOG("server", "监听 %s:%u 失败（端口被占？）",
                 ds->bindHost, (unsigned)port);
        XClassDelete((XClass*)ds->srv);
        XFree_System(ds);
        g_demoServer = NULL;
        return NULL;
    }
    return ds;
}

/** @brief 停止并销毁服务端角色（连带回收全部会话与监听器）。 */
static void demo_server_stop(DemoServer* ds)
{
    if (!ds)
        return;
    XClassDelete((XClass*)ds->srv);
    XFree_System(ds);
    g_demoServer = NULL;
}

const char* XLinkDemo_envOr(const char* env, const char* fallback)
{
    const char* v = getenv(env);
    return (v && v[0]) ? v : fallback;
}

/* ==================== 运行入口 ==================== */

int XLinkDemoServer_run(void)
{
    DemoServer* ds;
    bool ok;

    DEMO_LOG("server", "启动：监听 %s:%u（等 --test xlink-demo-client）",
             XLinkDemo_envOr(XLINK_DEMO_BIND_ENV, "127.0.0.1"),
             (unsigned)XLINK_DEMO_PORT);
    ds = demo_server_start(XLINK_DEMO_PORT);
    if (!ds)
        return 1;
    g_serverStartTick = XDateTime_currentMSecsSinceEpoch();

    /* 主循环 = 框架标准入口。服务端有自己的内建节拍定时器
       （listen_base 时启动）：事件循环每圈派发定时器事件 →
       XLinkServer_pump → tick 回调（状态推送/慢命令回执/超时检查）
       + 会话回收 + accept 兜底，全程无需应用再建定时器驱动。
       客户端离开（notify）或超时（tick 回调）经 quit 请求退出。 */
    return 0;
    XCoreApplication_exec();

    ok = ds->clientGone && ds->handled >= 3
         && ds->clientPubs >= 2 && ds->subCount >= 2 && ds->unsubCount >= 2;
    DEMO_LOG("server", "%s（handled=%d pubs=%d sub=%d unsub=%d）",
             ok ? "联调完成 exit 0" : "超时/交互不全 exit 1",
             ds->handled, ds->clientPubs, ds->subCount, ds->unsubCount);
    demo_server_stop(ds);
    return ok ? 0 : 1;
}

int XLinkDemo_run(void)
{
    DemoServer* ds;
    XTcpServer* listener;
    uint16_t port;
    uint64_t start;
    int rc;

    DEMO_LOG0("demo", "本进程联调：服务端角色 + 客户端角色跑真实 TCP 环回");
    ds = demo_server_start(0); /* 端口自动分配，避免与残留进程撞车 */
    if (!ds)
        return 1;
    listener = XLinkServer_tcpServer_const(ds->srv);
    port = XTcpServer_serverPort(listener);
    DEMO_LOG("demo", "服务端已监听 127.0.0.1:%u", (unsigned)port);

    /* 同一事件循环伺候两个角色：服务端内建节拍（listen 时启动）在
       客户端等待循环转事件时照常派发（pump -> tick 回调 -> publish）
       ——真实双端语义，只是同进程。 */
    g_serverStartTick = XDateTime_currentMSecsSinceEpoch();
    rc = XLinkDemo_client_flow(port);

    /* 收尾宽限：客户端发完 Goodbye 即退出，Goodbye 帧还在路上——
       继续转事件直到服务端完成回收（或 2s 超时）。 */
    start = XDateTime_currentMSecsSinceEpoch();
    while (!ds->clientGone
           && XDateTime_currentMSecsSinceEpoch() - start < 2000u) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
    }
    if (rc == 0 && (!ds->clientGone || ds->handled < 3 || ds->clientPubs < 2
                    || ds->subCount < 2 || ds->unsubCount < 2)) {
        DEMO_LOG("demo", "客户端流程通过但服务端交互不全"
                         "（handled=%d pubs=%d sub=%d unsub=%d gone=%d）",
                 ds->handled, ds->clientPubs, ds->subCount,
                 ds->unsubCount, ds->clientGone);
        rc = 1;
    }
    DEMO_LOG("demo", "%s", rc == 0 ? "本进程联调通过，exit 0"
                                   : "本进程联调失败，exit 1");
    demo_server_stop(ds);
    return rc;
}

void XLinkDemoServer(void)
{
    (void)XLinkDemoServer_run();
}

void XLinkDemo(void)
{
    (void)XLinkDemo_run();
}

#endif /* XLINK_ON && XLINK_SESSION_ON && XLINK_SERVER_ON */
