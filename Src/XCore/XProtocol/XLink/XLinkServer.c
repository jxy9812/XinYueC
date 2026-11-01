/** @file       XLinkServer.c
 * @brief      XLink 服务端包装实现：监听 + 自动建会话 + 会话回收。
 * @details    实现只依赖 XinYueC 抽象层（XTcpServer/XLinkSession/XObject/
 *             XDateTime/XMemory），禁止调用 Win32、POSIX 或其他平台 API。
 *             双驱动口径与 XLinkSession 一致：异步模式经 newConnection 信号
 *             与内建定时器驱动（时间经 XDateTime_currentMSecsSinceEpoch 内部
 *             取）；手动模式按圈调 XLinkServer_pump 注入时间。pump 幂等，
 *             两种驱动混用无副作用。
 * @author     XinYueC 团队
 */
#include "XLinkServer.h"

#if XLINK_ON && XLINK_SESSION_ON && XLINK_SERVER_ON

#include "XMemory.h"
#include "XDateTime.h"
#include "XString.h"
#include <string.h>
#if XLINK_AUTH_ON
#include "XLinkAuth.h"
#endif

/* ========================================================================== */
/*                  内部工具（accept / 回收 / 定时器）                        */
/* ========================================================================== */

/* 前置声明：listen 与 timerEvent 都经 pump 驱动。 */
static void VXLinkServer_onNewConnection(XObject* receiver, XVarList* args);

/** @brief 异步驱动路径的当前时刻：单调毫秒（与会话异步口径一致）。 */
static uint32_t XLinkServerPrivate_nowMs(void)
{
    return (uint32_t)XDateTime_currentMSecsSinceEpoch();
}

/** @brief 通知会话增删（回调未注册时为空操作）。 */
static void XLinkServerPrivate_notify(XLinkServer* self, XLinkSession* session,
                                      XLinkServerSessionEvent event)
{
    if (self->m_notifyCallback)
        self->m_notifyCallback(self, session, event);
}

#if XLINK_AUTH_ON

/* 前置声明：认证回调与审计。 */
static int XLinkServerPrivate_authLookup(void* userData, const char* user,
                                         uint8_t saltOut[XLINK_AUTH_SALT_BYTES],
                                         uint32_t* iterationsOut,
                                         uint8_t hashOut[XLINK_AUTH_NONCE_BYTES],
                                         uint8_t* roleOut);
static void XLinkServerPrivate_authChanged(XObject* receiver, XVarList* args);

/**
 * @brief 凭据查询回调：从账户库按用户名取盐/迭代/摘要/角色。
 * @note  返回码与 XLinkSessionAuthLookupResult 数值对齐（0/1/2）。
 *        库为空时一律 BadUser（认证启用但没账户 = 谁也进不来）。
 */
static int XLinkServerPrivate_authLookup(void* userData, const char* user,
                                         uint8_t saltOut[XLINK_AUTH_SALT_BYTES],
                                         uint32_t* iterationsOut,
                                         uint8_t hashOut[XLINK_AUTH_NONCE_BYTES],
                                         uint8_t* roleOut)
{
    XLinkServer* self = (XLinkServer*)userData;
    const XLinkUserRecord* record;
    if (!self || !user || !saltOut || !iterationsOut || !hashOut || !roleOut)
        return XLinkSessionAuthLookup_BadUser;
    record = XLinkUserStore_find(&self->m_users, user);
    if (!record || !XLinkUserStore_hasPassword(record))
        return XLinkSessionAuthLookup_BadUser;
    if (XLinkUserStore_isLocked(record))
        return XLinkSessionAuthLookup_Locked;
    memcpy(saltOut, record->m_salt, XLINK_AUTH_SALT_BYTES);
    *iterationsOut = record->m_iterations;
    memcpy(hashOut, record->m_hash, XLINK_AUTH_NONCE_BYTES);
    *roleOut = record->m_role;
    return XLinkSessionAuthLookup_Ok;
}

/**
 * @brief 会话认证结果信号槽：转审计回调。
 * @note  receiver 是 XLinkServer，args 携带通过标志/结果码/角色；会话指针
 *        由注册时经 connect 的 userData 传不进来（XObject_connect_1 的槽
 *        只有 receiver/args），故审计里的 session 置 NULL，用用户名区分。
 */
static void XLinkServerPrivate_authChanged(XObject* receiver, XVarList* args)
{
    XLinkServer* self = (XLinkServer*)receiver;

    if (!self || !args)
        return;
    XVarList_args_3(args, bool, passed, uint8_t, status, uint8_t, role);
    if (!self->m_authAudit)
        return;
    if (passed)
        self->m_authAudit(self, NULL, XLinkServerAuthEvent_Success, "", role);
    else if (status == XLinkAuthStatus_Timeout)
        self->m_authAudit(self, NULL, XLinkServerAuthEvent_Timeout, "", 0);
    else
        self->m_authAudit(self, NULL, XLinkServerAuthEvent_Failure, "", 0);
}

#endif /* XLINK_AUTH_ON */

/** @brief 接受全部待处理连接并建会话（套能力位/回调模板）。
 *  @note  accept 与建会话在调用栈上同步完成；会话挂载传输后自动接线
 *         readyRead 并按模板握手，此处不再逐会话驱动。 */
static void XLinkServerPrivate_acceptPending(XLinkServer* self)
{
    for (;;) {
        XTcpSocket* conn = XTcpServer_nextPendingConnection_base(self->m_listener);
        XLinkSession* session;
        if (!conn)
            break;
        session = XLinkSession_create_ex((XIODevice*)conn);
        if (!session) {
            /* 会话对象创建失败：关闭连接防泄漏（套接字归监听器子对象管理）。 */
            continue;
        }
#if XLINK_AUTH_ON
        /* 认证启用时：能力位模板补 Auth 位（对端据此知道要登录），
           并挂凭据查表回调——握手后自动进入 Authenticating。 */
        if (self->m_authEnabled) {
            XLinkSession_setCapability(
                session, (uint16_t)(self->m_capability
                                    | XLinkCapability_Auth));
            XLinkSession_setAuthLookup(session, XLinkServerPrivate_authLookup,
                                       self);
            XLinkSession_setAuthFailureLimit(session,
                                             self->m_authFailureLimit);
            XLinkSession_setAuthAllowAnonymous(session,
                                               self->m_authAllowAnonymous);
            if (self->m_authTimeoutMs)
                XLinkSession_setAuthTimeout(session, self->m_authTimeoutMs);
            XObject_connect_1((XObject*)session,
                              XSignal(XLinkSession_authChanged_signal),
                              (XObject*)self,
                              XLinkServerPrivate_authChanged,
                              XConnectionType_Auto);
        }
        else
#endif
        if (self->m_capability) {
            XLinkSession_setCapability(session, self->m_capability);
        }
        XLinkSession_setDispatchCallback(session, self->m_dispatchCallback,
                                         self->m_userData);
#if XLINK_FILE_ON
        /* 文件传输：能力位补 FileTransfer（对端据此判断能否发 File 帧），
           并把 File 消息类转给引擎。注意必须在上面模板能力位设置之后做，
           否则会被 setCapability 覆盖。Auth 位只在认证扩展编译进（XLINK_AUTH_ON）
           且已启用认证时补——裁空构建里 m_authEnabled 成员不存在，且按约定
           "关闭时能力位 Auth 不置位"，与上方 accept 主路径的
           #if XLINK_AUTH_ON 分支同口径。 */
        if (self->m_fileHandler) {
            XLinkSession_setCapability(
                session, (uint16_t)(self->m_capability
#if XLINK_AUTH_ON
                                    | (self->m_authEnabled
                                           ? XLinkCapability_Auth : 0)
#endif
                                    | XLinkCapability_FileTransfer));
            XLinkSession_setFileCallback(session, self->m_fileHandler,
                                         self->m_fileHandlerUserData);
        }
#endif
        if (self->m_sessions)
            XVector_push_back_1_base(self->m_sessions, &session);
        XLinkServerPrivate_notify(self, session, XLinkServerSessionEvent_Added);
    }
}

/** @brief 回收 Inactive 会话：出表、通知（删除前，指针仍有效）、删除。
 *  @note  只允许在"任何会话调用栈均已退出"的上下文调用（最外层 pump 末尾
 *         或事件分派的 timerEvent）——同步删除在此前提下符合对象释放时机
 *         判据；deleteLater 依赖事件循环真正释放，无事件分发器的手动/测试
 *         环境会悬挂，故不用。 */
static void XLinkServerPrivate_reap(XLinkServer* self)
{
    int i;
    if (!self->m_sessions)
        return;
    for (i = (int)XContainerSize(self->m_sessions) - 1; i >= 0; i--) {
        XLinkSession** slot =
            (XLinkSession**)XVector_at_base(self->m_sessions, i);
        XLinkSession* session = slot ? *slot : NULL;
        if (!session)
            continue;
        if (XLinkSession_state(session) != XLinkSessionState_Inactive)
            continue; /* Connected（握手中）/Ready 会话保留 */
        XVector_remove_base(self->m_sessions, i, 1);
        XLinkServerPrivate_notify(self, session,
                                  XLinkServerSessionEvent_Removed);
#if XLINK_FILE_ON
        /* 扩展模块（文件传输）在会话销毁前清掉借用指针。 */
        if (self->m_sessionGone)
            self->m_sessionGone(self, session, self->m_sessionGoneUserData);
#endif
        XClassDelete((XClass*)session);
    }
}

/** @brief 停止内建节拍定时器（未启动或无事件分发器时安全空操作）。 */
static void XLinkServerPrivate_stopTimer(XLinkServer* self)
{
    if (self->m_timerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_timerId);
        self->m_timerId = XTIMER_INVALID_ID;
    }
}

/* ========================================================================== */
/*                        静态虚函数实现                                     */
/* ========================================================================== */

/** @brief readyRead 无关：本类只重载定时器事件（节拍驱动 pump 与回收）。 */
static void VXLinkServer_timerEvent(XObject* object, XTimerEvent* event)
{
    XLinkServer* self = (XLinkServer*)object;
    if (!self || !event)
        return;
    if (XTimerEvent_timerId(event) != self->m_timerId) {
        XClass_Parent(XObject, EXObject_TimerEvent,
                      void(*)(XObject*, XTimerEvent*))((XObject*)self, event);
        return;
    }
    XLinkServer_pump(self, XLinkServerPrivate_nowMs());
}

/** @brief newConnection 信号槽：有客户端连入即接受并建会话（零轮询）。 */
static void VXLinkServer_onNewConnection(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver)
        return;
    /* 接受连接是幂等操作；建会话后由会话自身异步驱动，无需在此 pump。
       回收交由内建节拍（或下一次 pump），不阻塞 accept 路径。 */
    XLinkServerPrivate_acceptPending((XLinkServer*)receiver);
}

/**
 * @brief 反初始化：停定时器、回收全部会话与内嵌监听器，回链基类清理。
 * @note  末尾回链 XObject_deinit：注销残留定时器、清理子对象与信号连接
 *        （定时器回调异步触发，轮表残留会向已释放内存 postEvent），禁止省略。
 */
static void VXLinkServer_deinit(XClass* object)
{
    XLinkServer* obj;
    if (!object)
        return;
    obj = (XLinkServer*)object;
    XLinkServerPrivate_stopTimer(obj);
    if (obj->m_sessions) {
        /* 同步删除仍托管的会话（对象销毁路径，非任何会话调用栈）。 */
        while (XContainerSize(obj->m_sessions) > 0) {
            XLinkSession** slot = (XLinkSession**)XVector_at_base(obj->m_sessions, 0);
            XLinkSession* session = slot ? *slot : NULL;
            XVector_remove_base(obj->m_sessions, 0, 1);
            if (session) {
                /* 对象销毁路径：不回调扩展钩子——扩展对象可能已先于服务端
                   销毁（钩子的 userData 会成为悬垂指针）。扩展应在其自身
                   deinit 里 detach。 */
                XClassDelete((XClass*)session);
            }
        }
        XClassDelete((XClass*)obj->m_sessions);
        obj->m_sessions = NULL;
    }
    if (obj->m_listener) {
        XClassDelete((XClass*)obj->m_listener);
        obj->m_listener = NULL;
    }
#if XLINK_AUTH_ON
    XLinkUserStore_clear(&obj->m_users);
    if (obj->m_userStorePath) {
        XFree_System(obj->m_userStorePath);
        obj->m_userStorePath = NULL;
    }
#endif
    XClass_Deinit_Parent(XObject, (XObject*)obj);
}

/* ========================================================================== */
/*                        虚函数表初始化                                     */
/* ========================================================================== */

XVtable* XLinkServer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLinkServer)
    XVTABLE_INHERIT_XCLASS(XObject);
    /* 先继承后重载（顺序颠倒重载会被继承覆盖，见风格指南）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLinkServer_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXLinkServer_timerEvent);
    return XVTABLE_DEFAULT;
}

/* ========================================================================== */
/*                        构造与析构                                         */
/* ========================================================================== */

void XLinkServer_init(XLinkServer* obj)
{
    if (!obj)
        return;
    memset(obj, 0, sizeof(XLinkServer));
    XObject_init((XObject*)obj);
    XClassSetVtable(obj, XLinkServer);
    obj->m_listener = XTcpServer_create();
    obj->m_sessions = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(XLinkSession*), false);
    obj->m_timerId = XTIMER_INVALID_ID;
    obj->m_tickIntervalMs = XLINK_HEARTBEAT_TICK_MS;
    obj->m_pumpDepth = 0;
#if XLINK_AUTH_ON
    XLinkUserStore_init(&obj->m_users);
    obj->m_userStorePath = NULL;
    obj->m_authEnabled = false;
    obj->m_authAllowAnonymous = false;
    obj->m_authFailureLimit = XLINK_AUTH_FAILURE_LIMIT;
    obj->m_authTimeoutMs = XLINK_AUTH_TIMEOUT_MS;
#endif
#if XLINK_FILE_ON
    obj->m_fileHandler = NULL;
    obj->m_fileHandlerUserData = NULL;
    obj->m_sessionGone = NULL;
    obj->m_sessionGoneUserData = NULL;
#endif
}

XLinkServer* XLinkServer_create(void)
{
    XMemoryType memory = XCLASS_DEFAULT_MEMORY_TYPE;
    XLinkServer* obj = (XLinkServer*)XMemory_malloc(sizeof(XLinkServer), memory);
    if (!obj)
        return NULL;
    XLinkServer_init(obj);
    Set_Class_Memory(obj, memory);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ========================================================================== */
/*                        监听与模板                                         */
/* ========================================================================== */

bool XLinkServer_listen(XLinkServer* self, const char* host, uint16_t port)
{
    XHostAddress addr;
    bool ok;

    if (ISNULL(self, "XLinkServer_listen self")
        || ISNULL(self->m_listener, "XLinkServer_listen listener"))
        return false;

    /* 重复 listen：先停旧监听（会话保留——换端口不应踢掉在线客户端）。 */
    if (XTcpServer_isListening(self->m_listener))
        XTcpServer_close(self->m_listener);

    XHostAddress_init(&addr);
    if (host && host[0])
        XHostAddress_setAddress(&addr, host);
    else
        XHostAddress_setAddressSpecial(&addr, XHostAddress_AnySpecial);
    ok = XTcpServer_listen(self->m_listener, &addr, port);
    XClassDeinit(&addr);
    if (!ok)
        return false;

    /* 异步驱动：内建节拍定时器（回收 + 手动模式兜底 pump）与
       newConnection 信号接线。无事件分发器时定时器自动缺席（无效 id），
       退化为手动 pump；信号接线不受影响。 */
    XLinkServerPrivate_stopTimer(self);
    if (self->m_tickIntervalMs > 0)
        self->m_timerId = XObject_startTimer_ms((XObject*)self,
                                                self->m_tickIntervalMs,
                                                XTimerType_CoarseTimer);
    XObject_connect_1((XObject*)self->m_listener,
                      XSignal(XTcpServer_newConnection_signal),
                      (XObject*)self, VXLinkServer_onNewConnection,
                      XConnectionType_Auto);
    return true;
}

void XLinkServer_close(XLinkServer* self)
{
    int i;
    if (ISNULL(self, "XLinkServer_close self"))
        return;
    XLinkServerPrivate_stopTimer(self);
    if (self->m_listener)
        XTcpServer_close(self->m_listener);
    if (self->m_sessions) {
        for (i = (int)XContainerSize(self->m_sessions) - 1; i >= 0; i--) {
            XLinkSession** slot = (XLinkSession**)XVector_at_base(self->m_sessions, i);
            XLinkSession* session = slot ? *slot : NULL;
            XVector_remove_base(self->m_sessions, i, 1);
            if (session) {
                XLinkServerPrivate_notify(self, session,
                                          XLinkServerSessionEvent_Removed);
#if XLINK_FILE_ON
                if (self->m_sessionGone)
                    self->m_sessionGone(self, session,
                                        self->m_sessionGoneUserData);
#endif
                XClassDelete((XClass*)session);
            }
        }
    }
}

bool XLinkServer_setCapability(XLinkServer* self, uint16_t capability)
{
    if (ISNULL(self, "XLinkServer_setCapability self"))
        return false;
    self->m_capability = capability;
    return true;
}

bool XLinkServer_setDispatchCallback(XLinkServer* self,
                                     XLinkSessionDispatchCallback callback,
                                     void* userData)
{
    if (ISNULL(self, "XLinkServer_setDispatchCallback self"))
        return false;
    self->m_dispatchCallback = callback;
    self->m_userData = userData;
    return true;
}

bool XLinkServer_setNotifyCallback(XLinkServer* self,
                                   XLinkServerNotifyCallback callback)
{
    if (ISNULL(self, "XLinkServer_setNotifyCallback self"))
        return false;
    self->m_notifyCallback = callback;
    return true;
}

bool XLinkServer_setTickCallback(XLinkServer* self,
                                 XLinkServerTickCallback callback,
                                 void* userData)
{
    if (ISNULL(self, "XLinkServer_setTickCallback self"))
        return false;
    self->m_tickCallback = callback;
    self->m_tickUserData = userData;
    return true;
}

void XLinkServer_tickCallback_const(XLinkServer* self,
                                    XLinkServerTickCallback* outCallback,
                                    void** outUserData)
{
    if (!self)
        return;
    if (outCallback)
        *outCallback = self->m_tickCallback;
    if (outUserData)
        *outUserData = self->m_tickUserData;
}

bool XLinkServer_setTickInterval(XLinkServer* self, uint32_t intervalMs)
{
    if (ISNULL(self, "XLinkServer_setTickInterval self"))
        return false;

    self->m_tickIntervalMs = intervalMs;

    /* 正在监听（节拍在跑）：立即按新周期重启；置 0 即停节拍。
       未监听时仅记录，下次 listen 按新周期启动。 */
    XLinkServerPrivate_stopTimer(self);
    if (intervalMs > 0 && self->m_listener
        && XTcpServer_isListening(self->m_listener))
        self->m_timerId = XObject_startTimer_ms((XObject*)self,
                                                intervalMs,
                                                XTimerType_CoarseTimer);
    return true;
}

uint32_t XLinkServer_tickInterval(const XLinkServer* self)
{
    if (ISNULL(self, "XLinkServer_tickInterval self"))
        return 0;
    return self->m_tickIntervalMs;
}

int XLinkServer_publish(XLinkServer* self, uint32_t topicId,
                        const uint8_t* payload, uint32_t length)
{
    int i, count;
    int sent = 0;

    if (ISNULL(self, "XLinkServer_publish self"))
        return 0;
    if (topicId < XLINK_TOPIC_USER_BASE || !self->m_sessions)
        return 0;

    count = (int)XContainerSize(self->m_sessions);
    for (i = 0; i < count; i++) {
        XLinkSession** slot =
            (XLinkSession**)XVector_at_base(self->m_sessions, i);
        XLinkSession* session = slot ? *slot : NULL;
        if (!session || !XLinkSession_isReady(session))
            continue;
        /* 一对多分发的核心：只推给登记过该 topic 的会话。
           订阅关系由会话层自动镜像（收到 Subscribe/Unsubscribe 帧时
           登记/移除），应用无需自己记录。 */
        if (!XLinkSession_isSubscribed(session, topicId))
            continue;
        if (XLinkSession_sendPublish(session, topicId, payload, length))
            sent++;
    }
    return sent;
}

int XLinkServer_broadcast(XLinkServer* self, uint32_t topicId,
                          const uint8_t* payload, uint32_t length)
{
    int i, count;
    int sent = 0;

    if (ISNULL(self, "XLinkServer_broadcast self"))
        return 0;
    if (topicId < XLINK_TOPIC_USER_BASE || !self->m_sessions)
        return 0;

    count = (int)XContainerSize(self->m_sessions);
    for (i = 0; i < count; i++) {
        XLinkSession** slot =
            (XLinkSession**)XVector_at_base(self->m_sessions, i);
        XLinkSession* session = slot ? *slot : NULL;
        if (!session || !XLinkSession_isReady(session))
            continue;
        if (XLinkSession_sendPublish(session, topicId, payload, length))
            sent++;
    }
    return sent;
}

/* ========================================================================== */
/*                        驱动与查询                                         */
/* ========================================================================== */

void XLinkServer_pump(XLinkServer* self, uint32_t nowMs)
{
    int i;

    if (ISNULL(self, "XLinkServer_pump self"))
        return;

    self->m_pumpDepth++;

    /* 1) 手动模式兜底 accept：无事件循环时 newConnection 不发射，
          轮询待处理队列补齐（有信号驱动时队列为空，空转零开销）。 */
    XLinkServerPrivate_acceptPending(self);

    /* 2) 逐会话驱动：异步会话幂等（内部 readyRead/定时器已在驱动），
          手动会话由此获得收发与心跳节拍。 */
    if (self->m_sessions) {
        int count = (int)XContainerSize(self->m_sessions);
        for (i = 0; i < count; i++) {
            XLinkSession** slot = (XLinkSession**)XVector_at_base(self->m_sessions, i);
            if (!slot || !*slot)
                continue;
#if XLINK_FILE_ON
            /* 扩展续命钩子：必须在会话 pump（含静默判线）之前调用。 */
            if (self->m_preSessionPump)
                self->m_preSessionPump(self, *slot,
                                       self->m_preSessionPumpUserData);
#endif
            XLinkSession_pump(*slot, nowMs);
        }
    }

    /* 3) 周期业务：应用注册的 tick 回调随内建节拍在这里触发——
          状态推送等周期业务无需应用自建定时器或手写主循环逻辑。 */
    if (self->m_tickCallback)
        self->m_tickCallback(self, self->m_tickUserData);

    self->m_pumpDepth--;

    /* 4) 最外层 pump 末尾回收：此刻（含重入在内的）所有会话调用栈均已
          退出，同步删除符合对象释放时机判据。 */
    if (self->m_pumpDepth == 0)
        XLinkServerPrivate_reap(self);
}

int XLinkServer_sessionCount(const XLinkServer* self)
{
    if (ISNULL(self, "XLinkServer_sessionCount self"))
        return 0;
    if (!self->m_sessions)
        return 0;
    return (int)XContainerSize(self->m_sessions);
}

XLinkSession* XLinkServer_sessionAt(const XLinkServer* self, int index)
{
    XLinkSession** slot;
    if (ISNULL(self, "XLinkServer_sessionAt self"))
        return NULL;
    if (!self->m_sessions || index < 0
        || index >= (int)XContainerSize(self->m_sessions))
        return NULL;
    slot = (XLinkSession**)XVector_at_base(self->m_sessions, index);
    return slot ? *slot : NULL;
}

XTcpServer* XLinkServer_tcpServer_const(const XLinkServer* self)
{
    if (ISNULL(self, "XLinkServer_tcpServer_const self"))
        return NULL;
    return self->m_listener;
}

#if XLINK_FILE_ON

bool XLinkServer_setFileCallback(XLinkServer* self,
                                 XLinkSessionFileCallback callback,
                                 void* userData)
{
    if (ISNULL(self, "XLinkServer_setFileCallback self"))
        return false;
    self->m_fileHandler = callback;
    self->m_fileHandlerUserData = userData;
    return true;
}

bool XLinkServer_setSessionGoneHook(XLinkServer* self,
                                    void (*callback)(XLinkServer* self,
                                                     XLinkSession* session,
                                                     void* userData),
                                    void* userData)
{
    if (ISNULL(self, "XLinkServer_setSessionGoneHook self"))
        return false;
    self->m_sessionGone = callback;
    self->m_sessionGoneUserData = userData;
    return true;
}

bool XLinkServer_setPreSessionPumpHook(XLinkServer* self,
                                       void (*callback)(XLinkServer* self,
                                                        XLinkSession* session,
                                                        void* userData),
                                       void* userData)
{
    if (ISNULL(self, "XLinkServer_setPreSessionPumpHook self"))
        return false;
    self->m_preSessionPump = callback;
    self->m_preSessionPumpUserData = userData;
    return true;
}

#endif /* XLINK_FILE_ON */

#if XLINK_AUTH_ON

/* ========================================================================== */
/*                        登录认证                                           */
/* ========================================================================== */

bool XLinkServer_setAuthEnabled(XLinkServer* self, bool enabled)
{
    if (ISNULL(self, "XLinkServer_setAuthEnabled self"))
        return false;
    /* 只影响之后建立的会话；在线会话保持原状（文档语义）。 */
    self->m_authEnabled = enabled;
    return true;
}

bool XLinkServer_authEnabled(const XLinkServer* self)
{
    if (ISNULL(self, "XLinkServer_authEnabled self"))
        return false;
    return self->m_authEnabled;
}

bool XLinkServer_setUserStorePath(XLinkServer* self, const char* path)
{
    char* copy = NULL;
    bool exists = false;

    if (ISNULL(self, "XLinkServer_setUserStorePath self"))
        return false;

    if (!path || !path[0]) {
        if (self->m_userStorePath) {
            XFree_System(self->m_userStorePath);
            self->m_userStorePath = NULL;
        }
        XLinkUserStore_clear(&self->m_users);
        return true;
    }

    copy = (char*)XMalloc_System(strlen(path) + 1u);
    if (!copy)
        return false;
    memcpy(copy, path, strlen(path) + 1u);

    if (!XLinkUserStore_load(&self->m_users, path, &exists)) {
        XFree_System(copy);
        return false;
    }
    if (self->m_userStorePath)
        XFree_System(self->m_userStorePath);
    self->m_userStorePath = copy;
    return true;
}

bool XLinkServer_saveUserStore(XLinkServer* self)
{
    if (ISNULL(self, "XLinkServer_saveUserStore self"))
        return false;
    if (!self->m_userStorePath)
        return false;
    return XLinkUserStore_save(&self->m_users, self->m_userStorePath);
}

bool XLinkServer_addUser(XLinkServer* self, const char* user,
                         const char* password, uint8_t role)
{
    if (ISNULL(self, "XLinkServer_addUser self"))
        return false;
    return XLinkUserStore_upsert(&self->m_users, user, password, role);
}

bool XLinkServer_removeUser(XLinkServer* self, const char* user)
{
    if (ISNULL(self, "XLinkServer_removeUser self"))
        return false;
    return XLinkUserStore_remove(&self->m_users, user);
}

bool XLinkServer_setUserLocked(XLinkServer* self, const char* user, bool locked)
{
    if (ISNULL(self, "XLinkServer_setUserLocked self"))
        return false;
    return XLinkUserStore_setLocked(&self->m_users, user, locked);
}

uint32_t XLinkServer_userCount(const XLinkServer* self)
{
    if (ISNULL(self, "XLinkServer_userCount self"))
        return 0;
    return XLinkUserStore_count(&self->m_users);
}

const XLinkUserRecord* XLinkServer_userAt(const XLinkServer* self,
                                          uint32_t index)
{
    if (ISNULL(self, "XLinkServer_userAt self"))
        return NULL;
    return XLinkUserStore_at(&self->m_users, index);
}

bool XLinkServer_setAuthAllowAnonymous(XLinkServer* self, bool allow)
{
    if (ISNULL(self, "XLinkServer_setAuthAllowAnonymous self"))
        return false;
    self->m_authAllowAnonymous = allow;
    return true;
}

bool XLinkServer_setAuthFailureLimit(XLinkServer* self, uint8_t limit)
{
    if (ISNULL(self, "XLinkServer_setAuthFailureLimit self"))
        return false;
    self->m_authFailureLimit = limit ? limit : 1;
    return true;
}

bool XLinkServer_setAuthTimeout(XLinkServer* self, uint32_t ms)
{
    if (ISNULL(self, "XLinkServer_setAuthTimeout self"))
        return false;
    self->m_authTimeoutMs = ms;
    return true;
}

bool XLinkServer_setAuthAuditCallback(XLinkServer* self,
                                      XLinkServerAuthAuditCallback callback,
                                      void* userData)
{
    if (ISNULL(self, "XLinkServer_setAuthAuditCallback self"))
        return false;
    self->m_authAudit = callback;
    self->m_authAuditUserData = userData;
    return true;
}

const char* XLinkServer_sessionUser_const(const XLinkServer* self,
                                          const XLinkSession* session)
{
    (void)self;
    if (!session)
        return NULL;
    return XLinkSession_authUser_const(session);
}

uint8_t XLinkServer_sessionRole(const XLinkServer* self,
                                const XLinkSession* session)
{
    (void)self;
    if (!session)
        return XLinkAuthRole_Anonymous;
    return XLinkSession_authRole(session);
}

#endif /* XLINK_AUTH_ON */

#endif /* XLINK_ON && XLINK_SESSION_ON && XLINK_SERVER_ON */
