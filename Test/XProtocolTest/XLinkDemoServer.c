/** @file       XLinkDemoServer.c
 * @brief      XLink 实战案例·服务端程序。
 * @details    服务端角色的全部代码，读这个文件就是"服务端怎么写"的答案：
 *               - 启用协议 4 行：XLinkServer_create + 设模板 + listen_base，
 *                 之后 accept/握手/心跳/判线/回收全部异步托管；
 *               - 登录认证（v1.3 剧情）：addUser 建账户 + setAuthEnabled
 *                 开认证 + 审计回调打日志——握手完成后会话进 Authenticating，
 *                 客户端凭据通过挑战应答才转 Ready；
 *               - 文件服务端（v1.3 剧情）：XLinkFileServer 挂接（沙箱根目录
 *                 xlink_demo_files/，启动时建、退出清理），策略开双向 + 覆盖
 *                 + 要求已登录；注意 attach 会用引擎自己的 tick 覆盖服务端
 *                 tick 槽（XLinkServer_setTickCallback 单槽、无链式），业务
 *                 tick 必须在 attach 之后再注册，并由它补偿引擎的节拍职责
 *                 （见 demo_server_file_tick）；
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

#if XLINK_FILE_ON
#include "XLinkFileServer.h"
#include "XDir.h"
#include "XString.h"
#endif

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
#if XLINK_FILE_ON
    /* 文件传输剧情：服务端对象与受理观察（终局由客户端逐字节校验兜底，
       这里的计数只用于日志呈现，不作为联调达标判据——小文件可能在两次
       tick 之间传完并被引擎就地回收，tick 侧观察存在先天竞态）。 */
    XLinkFileServer* fileSrv; /**< 文件传输服务端（对象拥有）。 */
    int fileUps;              /**< 已受理的上传传输数（日志观察）。 */
    int fileDowns;            /**< 已受理的下载传输数（日志观察）。 */
    uint32_t seenUpId;        /**< 上次打点的上传传输 id（去重）。 */
    uint32_t seenDownId;      /**< 上次打点的下载传输 id（去重）。 */
#endif
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

#if XLINK_AUTH_ON
/**
 * @brief 认证审计回调（setAuthAuditCallback 注册）：登录成功/失败/超时/
 *        匿名接入逐条打日志。
 * @note  审计只拿得到用户名与结果（拿不到口令，也禁止打印口令）——本案例
 *        按"日志即审计"演示最小接法；真实系统可在此接入限流/告警。
 *        当前引擎接线（XLinkServer.c XLinkServerPrivate_authChanged）经
 *        authChanged 信号桥接，user 恒为空串、session 为 NULL——角色是
 *        可用的，用户名待引擎后续补齐；本回调如实打印不做掩饰。
 */
static void demo_server_auth_audit(XLinkServer* self, XLinkSession* session,
                                   XLinkServerAuthEvent event,
                                   const char* user, uint8_t role)
{
    static const char* kNames[] = {
        "认证通过", "认证失败", "认证超时", "匿名接入"
    };
    (void)self;
    (void)session;
    DEMO_LOG("server", "审计：%s（user=%s role=%u）",
             (event >= XLinkServerAuthEvent_Success
              && event <= XLinkServerAuthEvent_Anonymous)
                 ? kNames[event] : "未知事件",
             (user && user[0]) ? user : "(空)",
             (unsigned)role);
}
#endif

#if XLINK_FILE_ON
/* ==================== 文件传输剧情（沙箱 + 节拍补偿） ==================== */

/**
 * @brief 创建文件沙箱根目录（cwd 下 xlink_demo_files/，已存在则幂等）。
 * @return 成功返回 true（setRootDir 要求目录已存在，故必须先建）。
 */
static bool demo_server_sandbox_prepare(void)
{
    XString* path = XString_create_utf8(XLINK_DEMO_FILE_DIR);
    XDir* dir;
    bool ok;

    if (!path)
        return false;
    dir = XDir_create_2(path);
    ok = dir && XDir_mkpath(dir, path);
    if (dir)
        XClassDelete((XClass*)dir);
    XClassDelete((XClass*)path);
    return ok;
}

/** @brief 退出清理：整体删除沙箱（连同其中上传落盘的演示文件）。 */
static void demo_server_sandbox_cleanup(void)
{
    XString* path = XString_create_utf8(XLINK_DEMO_FILE_DIR);
    XDir* dir;

    if (!path)
        return;
    dir = XDir_create_2(path);
    if (dir) {
        (void)XDir_removeRecursively(dir);
        XClassDelete((XClass*)dir);
    }
    XClassDelete((XClass*)path);
}

/**
 * @brief 文件引擎的节拍职责补偿：推进发送侧（下载方向服务端是发送方，
 *        数据块只能靠周期 pump 续发）并回收终局槽。
 * @note  为什么需要它：XLinkFileServer_attach 经 XLinkServer_setTickCallback
 *        安装引擎自己的 tick，而该接口是单槽覆盖、并无头文件所述的链式
 *        保留——本案例的业务 tick（状态推送/慢命令/超时兜底）必须在
 *        attach 之后重新注册回去，引擎这份节拍工作就由本函数以公开 API
 *        等价补上（遍历公开槽表 + XLinkFileTransfer_pump，释放语义与
 *        引擎实现一致：deinit 传输 + 清槽）。与引擎原 tick 相同的纪律：
 *        这里只推进传输，绝不 pump 会话（会话收发由 XLinkServer_pump
 *        负责，重入会搅乱收发游标）。
 */
static void demo_server_file_tick(DemoServer* ds)
{
    int i;
    uint32_t now = (uint32_t)XDateTime_currentMSecsSinceEpoch();

    if (!ds || !ds->fileSrv)
        return;
    for (i = 0; i < (int)XLINK_FILE_MAX_TRANSFERS; ++i) {
        XLinkFileServerSession* slot = &ds->fileSrv->m_slots[i];
        XLinkFileTransfer* transfer;

        if (!slot->m_used || !slot->m_session)
            continue;
        transfer = &slot->m_transfer;
        /* 受理观察：同一传输 id 只打一次点（上传/下载分开计数）。 */
        if (transfer->m_mode == XLinkFileMode_Upload) {
            if (transfer->m_transferId != ds->seenUpId) {
                ds->seenUpId = transfer->m_transferId;
                ds->fileUps++;
                DEMO_LOG("server", "受理上传传输 id=%u",
                         transfer->m_transferId);
            }
        } else if (transfer->m_transferId != ds->seenDownId) {
            ds->seenDownId = transfer->m_transferId;
            ds->fileDowns++;
            DEMO_LOG("server", "受理下载传输 id=%u",
                     transfer->m_transferId);
        }
        XLinkFileTransfer_pump(transfer, now);
        if (XLinkFileTransfer_isFinished(transfer)) {
            XLinkFileTransfer_deinit(transfer);
            slot->m_session = NULL;
            slot->m_used = false;
        }
    }
}

#endif /* XLINK_FILE_ON */

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

#if XLINK_FILE_ON
    /* 文件引擎节拍补偿（attach 覆盖 tick 槽后的等价补位，见函数注释）。 */
    demo_server_file_tick(ds);
#endif
}

/* ==================== 启停 ==================== */

/**
 * @brief 统一释放服务端角色（启动失败与正常停止共用，字段幂等清空）。
 * @note  顺序：文件服务端先摘除并反初始化（传输槽还借用着会话指针，必须
 *        赶在服务端删除会话之前清干净），再删服务端（连带回收监听器与
 *        会话），最后删沙箱并释放上下文。
 */
static void demo_server_release(DemoServer* ds)
{
    if (!ds)
        return;
#if XLINK_FILE_ON
    if (ds->fileSrv) {
        XLinkFileServer_detach(ds->fileSrv);
        XLinkFileServer_deinit(ds->fileSrv);
        XFree_System(ds->fileSrv);
        ds->fileSrv = NULL;
    }
#endif
    if (ds->srv) {
        XClassDelete((XClass*)ds->srv);
        ds->srv = NULL;
    }
#if XLINK_FILE_ON
    demo_server_sandbox_cleanup();
#endif
    if (g_demoServer == ds)
        g_demoServer = NULL;
    XFree_System(ds);
}

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

#if XLINK_AUTH_ON
    /* 剧情一：登录认证——先建账户再开认证（空库开认证 = 全员拒登），
       两步都在 listen 之前完成；审计回调让每次登录留下日志。 */
    if (!XLinkServer_addUser(ds->srv, XLINK_DEMO_USER, XLINK_DEMO_PASSWORD,
                             XLinkAuthRole_User)
        || !XLinkServer_setAuthEnabled(ds->srv, true)) {
        DEMO_LOG("server", "认证初始化失败（账户 %s 建户/启用未成功）",
                 XLINK_DEMO_USER);
        demo_server_release(ds);
        return NULL;
    }
    XLinkServer_setAuthAuditCallback(ds->srv, demo_server_auth_audit, NULL);
    DEMO_LOG("server", "登录认证已启用（账户 %s，口令见 XLinkDemo.h；"
                       "握手后须登录才 Ready）", XLINK_DEMO_USER);
#endif

#if XLINK_FILE_ON
    /* 剧情二：文件服务端——沙箱根目录先建（setRootDir 要求已存在），
       策略开双向 + 覆盖（REQUIRE_AUTH 缺省即开：文件通道要求已登录），
       最后 attach 挂接到服务端（接管 File 帧与判线续命钩子）。 */
    if (!demo_server_sandbox_prepare()) {
        DEMO_LOG("server", "文件沙箱 %s 创建失败", XLINK_DEMO_FILE_DIR);
        demo_server_release(ds);
        return NULL;
    }
    ds->fileSrv = (XLinkFileServer*)XMalloc_System(sizeof(XLinkFileServer));
    if (!ds->fileSrv) {
        DEMO_LOG0("server", "XLinkFileServer 分配失败");
        demo_server_release(ds);
        return NULL;
    }
    XLinkFileServer_init(ds->fileSrv);
    if (!XLinkFileServer_setRootDir(ds->fileSrv, XLINK_DEMO_FILE_DIR)
        || !XLinkFileServer_setFlags(
               ds->fileSrv, XLINK_FILESRV_ALLOW_UPLOAD
                                | XLINK_FILESRV_ALLOW_DOWNLOAD
                                | XLINK_FILESRV_ALLOW_OVERWRITE)
        || !XLinkFileServer_attach(ds->fileSrv, ds->srv)) {
        DEMO_LOG0("server", "文件服务端初始化/挂接失败");
        demo_server_release(ds);
        return NULL;
    }
    DEMO_LOG("server", "文件服务端已挂接（沙箱 %s/，双向+覆盖+要求已登录）",
             XLINK_DEMO_FILE_DIR);
#endif

    /* 周期业务注册到服务端内建节拍：状态推送/慢命令回执随 pump 自动触发。
       节拍直接设为状态推送周期 XLINK_DEMO_STATE_MS——tick 回调每拍推一次
       快照即可，应用侧不再自建推送间隔门限。
       顺序要点：必须在 XLinkFileServer_attach 之后注册——attach 会用引擎
       的 tick 覆盖本槽，后注册者生效（引擎职责由 demo_server_file_tick
       在业务 tick 内补偿）。 */
    XLinkServer_setTickCallback(ds->srv, demo_server_tick, ds);
    XLinkServer_setTickInterval(ds->srv, XLINK_DEMO_STATE_MS);
    ds->bindHost = XLinkDemo_envOr(XLINK_DEMO_BIND_ENV, "127.0.0.1");
    if (!XLinkServer_listen_base(ds->srv, ds->bindHost, port)) {
        DEMO_LOG("server", "监听 %s:%u 失败（端口被占？）",
                 ds->bindHost, (unsigned)port);
        demo_server_release(ds);
        return NULL;
    }
    return ds;
}

/** @brief 停止并销毁服务端角色（连带回收文件服务端、会话、监听器与沙箱）。 */
static void demo_server_stop(DemoServer* ds)
{
    demo_server_release(ds);
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

    DEMO_LOG("server", "启动：监听 %s:%u（认证+文件剧情，等"
             " --test xlink-demo-client）",
             XLinkDemo_envOr(XLINK_DEMO_BIND_ENV, "127.0.0.1"),
             (unsigned)XLINK_DEMO_PORT);
    ds = demo_server_start(XLINK_DEMO_PORT);
    if (!ds)
        return 1;
    g_serverStartTick = XDateTime_currentMSecsSinceEpoch();

    /* 主循环 = 框架标准入口。服务端有自己的内建节拍定时器
       （listen_base 时启动）：事件循环每圈派发定时器事件 →
       XLinkServer_pump → tick 回调（状态推送/慢命令回执/文件引擎推进/
       超时检查）+ 会话回收 + accept 兜底，全程无需应用再建定时器驱动。
       客户端离开（notify）或超时（tick 回调）经 quit 请求退出。 */
    XCoreApplication_exec();

    ok = ds->clientGone && ds->handled >= 3
         && ds->clientPubs >= 2 && ds->subCount >= 2 && ds->unsubCount >= 2;
#if XLINK_FILE_ON
    DEMO_LOG("server", "%s（handled=%d pubs=%d sub=%d unsub=%d"
             " 受理上传=%d 下载=%d）",
             ok ? "联调完成 exit 0" : "超时/交互不全 exit 1",
             ds->handled, ds->clientPubs, ds->subCount, ds->unsubCount,
             ds->fileUps, ds->fileDowns);
#else
    DEMO_LOG("server", "%s（handled=%d pubs=%d sub=%d unsub=%d）",
             ok ? "联调完成 exit 0" : "超时/交互不全 exit 1",
             ds->handled, ds->clientPubs, ds->subCount, ds->unsubCount);
#endif
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
