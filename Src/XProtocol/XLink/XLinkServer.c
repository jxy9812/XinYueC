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
#include <string.h>

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
        if (self->m_capability)
            XLinkSession_setCapability(session, self->m_capability);
        XLinkSession_setDispatchCallback(session, self->m_dispatchCallback,
                                         self->m_userData);
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
            if (session)
                XClassDelete((XClass*)session);
        }
        XClassDelete((XClass*)obj->m_sessions);
        obj->m_sessions = NULL;
    }
    if (obj->m_listener) {
        XClassDelete((XClass*)obj->m_listener);
        obj->m_listener = NULL;
    }
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
    /* 新增槽位（定时器事件）：预留派生类扩展点。 */
    XVTABLE_OVERLOAD_DEFAULT(EXLinkServer_timerEvent, VXLinkServer_timerEvent);
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

bool XLinkServer_listen_base(XLinkServer* self, const char* host, uint16_t port)
{
    XHostAddress addr;
    bool ok;

    if (ISNULL(self, "XLinkServer_listen_base self")
        || ISNULL(self->m_listener, "XLinkServer_listen_base listener"))
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

bool XLinkServer_setTickInterval(XLinkServer* self, uint32_t intervalMs)
{
    if (ISNULL(self, "XLinkServer_setTickInterval self"))
        return false;

    self->m_tickIntervalMs = intervalMs;

    /* 正在监听（节拍在跑）：立即按新周期重启；置 0 即停节拍。
       未监听时仅记录，下次 listen_base 按新周期启动。 */
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
            if (slot && *slot)
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

#endif /* XLINK_ON && XLINK_SESSION_ON && XLINK_SERVER_ON */
