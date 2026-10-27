/** @file       XLinkClient.c
 * @brief      XLink 客户端包装实现：内嵌 XTcpSocket + XLinkSession + 自动重连。
 * @details    实现只依赖 XinYueC 抽象层（XTcpSocket/XLinkSession/XObject/
 *             XString/XMemory），禁止调用 Win32、POSIX 或其他平台 API。
 *             异步驱动口径与会话/服务端一致：连接经 XTcpSocket 异步
 *             connectToHost，socket 的 connected/disconnected 信号与会话的
 *             stateChanged 信号驱动状态机（时间经 XDateTime 内部取），
 *             事件循环里无需任何手动调用。信号发射对齐
 *             XModbusClient_timeoutChanged_signal 的 XEmitSignal 模式。
 * @author     XinYueC 团队
 */
#include "XLinkClient.h"

#if XLINK_ON && XLINK_SESSION_ON && XLINK_CLIENT_ON

#include "XMemory.h"
#include "XString.h"
#include <string.h>
#if XLINK_AUTH_ON
#include "XLinkAuth.h"
#endif

/* 会话层 stateChanged 信号由并行任务补入 XLinkSession.h/.c：本头文件尚未
   声明时在此给出 extern 原型（签名对齐 XAbstractSocket_stateChanged_signal
   惯例），链接期由会话层实现补齐。 */
void* XLinkSession_stateChanged_signal(XLinkSession* self, XLinkSessionState state);

/** @brief 优雅断开时排空写队列的最大轮数（每轮 waitForBytesWritten 至少
 *         推进一个写完成；防御写侧异常导致的死循环）。 */
#define XLINK_CLIENT_DRAIN_MAX_ROUNDS 10

/** @brief 优雅断开时单轮排空等待的毫秒数（照 XLinkDemoClient.c 收尾口径）。 */
#define XLINK_CLIENT_DRAIN_WAIT_MS 200

/* ========================================================================== */
/*                  内部工具（状态迁移 / 拆除 / 定时器 / 连接）               */
/* ========================================================================== */

/* 前置声明：套接字与会的信号槽。 */
static void VXLinkClient_onSocketConnected(XObject* receiver, XVarList* args);
static void VXLinkClient_onSocketDisconnected(XObject* receiver, XVarList* args);
static void VXLinkClient_onSessionStateChanged(XObject* receiver, XVarList* args);
#if XLINK_AUTH_ON
static void VXLinkClient_onAuthChanged(XObject* receiver, XVarList* args);
#endif

/**
 * @brief 迁移连接状态并发射 stateChanged 信号（同值迁移不发射）。
 * @note  信号同步发射（XConnectionType_Auto 同线程直呼）；发射期间对象
 *        保持有效——本类所有状态迁移点都不在自身发射链上删除对象。
 */
static void XLinkClientPrivate_setState(XLinkClient* self, XLinkClientState state)
{
    if (!self || self->m_state == state)
        return;
    self->m_state = state;
    XLinkClient_stateChanged_signal(self, state);
}

/** @brief 停止重连定时器（未启动时安全空操作）。 */
static void XLinkClientPrivate_stopReconnectTimer(XLinkClient* self)
{
    if (self->m_reconnectTimerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_reconnectTimerId);
        self->m_reconnectTimerId = XTIMER_INVALID_ID;
    }
}

/**
 * @brief 拆除内嵌会话与套接字。
 * @param graceful true 时先发 Goodbye 并排空写队列（照 XLinkDemoClient.c
 *                 收尾：套接字写是异步管线，不排空就拆会丢未 flush 的帧）。
 * @param deferred true 时经 deleteLater 延迟释放——用于信号槽/定时器调用栈
 *                 上的拆除（对象发射链在途，同步删除会造成悬垂，见风格指南
 *                 「对象释放方式选择约束」）；false 时同步 XClassDelete，
 *                 仅用于 API/析构等"对象调用栈均已退出"的上下文。
 */
static void XLinkClientPrivate_teardown(XLinkClient* self, bool graceful,
                                        bool deferred)
{
    if (!self)
        return;
    if (graceful && self->m_session
        && XLinkSession_state(self->m_session) != XLinkSessionState_Inactive) {
        XLinkSession_sendSession(self->m_session, XLinkSessionCode_Goodbye,
                                 NULL, 0);
        if (self->m_socket) {
            int drain;
            for (drain = 0;
                 drain < XLINK_CLIENT_DRAIN_MAX_ROUNDS
                 && XTcpSocket_waitForBytesWritten_base(
                        (XIODevice*)self->m_socket,
                        XLINK_CLIENT_DRAIN_WAIT_MS);
                 drain++) {
            }
        }
    }
    if (self->m_session) {
        if (deferred)
            XObject_deleteLater((XObject*)self->m_session);
        else
            XClassDelete((XClass*)self->m_session);
        self->m_session = NULL;
    }
    if (self->m_socket) {
        if (deferred)
            XObject_deleteLater((XObject*)self->m_socket);
        else
            XClassDelete((XClass*)self->m_socket);
        self->m_socket = NULL;
    }
}

/**
 * @brief 确保内嵌套接字就绪并接线 connected/disconnected 信号（懒创建，
 *        套接字跨重连尝试复用——对齐 XModbusTcpClient 的套接字复用口径）。
 * @return 套接字指针；创建失败返回 NULL。
 */
static XTcpSocket* XLinkClientPrivate_ensureSocket(XLinkClient* self)
{
    if (!self)
        return NULL;
    if (self->m_socket)
        return self->m_socket;
    self->m_socket = XTcpSocket_create();
    if (!self->m_socket)
        return NULL;
    XObject_connect_1((XObject*)self->m_socket,
                      XSignal(XAbstractSocket_connected_signal),
                      (XObject*)self, VXLinkClient_onSocketConnected,
                      XConnectionType_Auto);
    XObject_connect_1((XObject*)self->m_socket,
                      XSignal(XAbstractSocket_disconnected_signal),
                      (XObject*)self, VXLinkClient_onSocketDisconnected,
                      XConnectionType_Auto);
    return self->m_socket;
}

/**
 * @brief 发起一次异步连接尝试（首次连接与重连尝试共用）。
 * @note  调用方已把状态迁到目标态（Connecting/保持 Reconnecting），本函数
 *        只负责建套接字并发起 connectToHost；连接结果由信号异步通知。
 */
static void XLinkClientPrivate_startConnect(XLinkClient* self)
{
    XTcpSocket* sock = XLinkClientPrivate_ensureSocket(self);
    if (!sock) {
        XLinkClientPrivate_stopReconnectTimer(self);
        XLinkClientPrivate_setState(self, XLinkClient_UnconnectedState);
        return;
    }
    XTcpSocket_connectToHost_base((XAbstractSocket*)sock,
                                  XString_toUtf8(self->m_host), self->m_port,
                                  XIODevice_ReadWrite,
                                  XAbstractSocket_AnyIPProtocol);
}

/**
 * @brief 链路断开处理（套接字 disconnected 与会话 Inactive 两条路径收敛于此）。
 * @note  幂等：重连流程已接手（Reconnecting 态）或手动断开进行中时直接返回。
 *        自动重连只对"曾达到 ConnectedState"的链路生效——首次连接失败
 *        （握手未完成）直接回 UnconnectedState，不进入重连。
 */
static void XLinkClientPrivate_handleLinkLost(XLinkClient* self)
{
    if (!self || self->m_userDisconnect)
        return;
    if (self->m_state == XLinkClient_ReconnectingState)
        return; /* 重连定时器已接手，勿重复启定时器 */

#if XLINK_AUTH_ON
    /* 认证失败是永久失败：口令/账户问题重试无意义，直接收手回未连接。
       （onAuthChanged 通常已先行处理；此处兜底会话复位引发的链路丢失。） */
    if (self->m_authFailed) {
        XLinkClientPrivate_stopReconnectTimer(self);
        XLinkClientPrivate_teardown(self, false, true);
        XLinkClientPrivate_setState(self, XLinkClient_UnconnectedState);
        return;
    }
#endif

    if (!self->m_everReady || !self->m_autoReconnect) {
        XLinkClientPrivate_stopReconnectTimer(self);
        XLinkClientPrivate_teardown(self, false, true);
        XLinkClientPrivate_setState(self, XLinkClient_UnconnectedState);
        return;
    }

    /* 曾连上且自动重连开：拆旧会话与套接字（延迟释放——本函数可能运行在
       socket/会话的信号发射栈上），转 Reconnecting 并启重连定时器。 */
    XLinkClientPrivate_teardown(self, false, true);
    XLinkClientPrivate_setState(self, XLinkClient_ReconnectingState);
    XLinkClientPrivate_stopReconnectTimer(self);
    self->m_reconnectTimerId =
        XObject_startTimer_ms((XObject*)self, self->m_reconnectInterval,
                              XTimerType_CoarseTimer);
}

#if XLINK_AUTH_ON

/**
 * @brief 会话挑战应答回调：本地派生 H 并计算 response。
 * @note  口令明文只在本函数栈上短暂使用；派生得到的 H 交给会话用于
 *        校验服务端证明，断链时由会话零化。
 */
static bool XLinkClientPrivate_authRespond(
    void* userData, const XLinkAuthChallengePayload* challenge,
    uint8_t* methodOut, uint8_t responseOut[XLINK_AUTH_NONCE_BYTES],
    uint8_t hashOut[XLINK_AUTH_NONCE_BYTES])
{
    XLinkClient* self = (XLinkClient*)userData;
    if (!self || !self->m_authPassword || !challenge || !methodOut
        || !responseOut || !hashOut)
        return false;
    if (!XLinkAuth_deriveHash(XString_toUtf8(self->m_authPassword),
                              challenge->m_salt,
                              challenge->m_iterations, hashOut))
        return false;
    if (!XLinkAuth_computeResponse(hashOut, challenge->m_nonce, responseOut)) {
        XLinkAuth_secureZero(hashOut, XLINK_AUTH_NONCE_BYTES);
        return false;
    }
    *methodOut = (uint8_t)XLinkAuthMethod_IteratedHmac;
    return true;
}

/**
 * @brief 会话认证结果槽：失败时阻断自动重连并发射 authFailed。
 */
static void VXLinkClient_onAuthChanged(XObject* receiver, XVarList* args)
{
    XLinkClient* self = (XLinkClient*)receiver;
    int reason;

    if (!self || !args)
        return;
    XVarList_args_3(args, bool, passed, uint8_t, status, uint8_t, role);
    (void)role;
    if (passed || self->m_userDisconnect)
        return;

    switch (status) {
    case XLinkAuthStatus_BadUser: reason = XLinkClientAuthFailure_BadUser; break;
    case XLinkAuthStatus_BadPassword:
        reason = XLinkClientAuthFailure_BadPassword; break;
    case XLinkAuthStatus_Locked: reason = XLinkClientAuthFailure_Locked; break;
    case XLinkAuthStatus_Timeout: reason = XLinkClientAuthFailure_Timeout; break;
    case XLinkAuthStatus_Protocol:
        reason = XLinkClientAuthFailure_Protocol; break;
    default: reason = XLinkClientAuthFailure_Rejected; break;
    }

    self->m_lastAuthFailure = reason;
    self->m_authFailed = true;
    /* 认证失败是永久失败：停重连，直接把状态归一为未连接。
       本槽运行在会话的信号发射栈上——会话拆除必须延迟（deleteLater）。 */
    XLinkClientPrivate_stopReconnectTimer(self);
    XLinkClientPrivate_teardown(self, false, true);
    XLinkClientPrivate_setState(self, XLinkClient_UnconnectedState);
    XLinkClient_authFailed_signal(self, reason, status);
}

/** @brief 释放凭据（口令先零化内容再释放 XString）。 */
static void XLinkClientPrivate_freeCredential(XLinkClient* self)
{
    if (!self)
        return;
    if (self->m_authPassword) {
        /* XString 内部缓冲先原地零化：XClassDelete 只释放不擦除。 */
        const char* text = XString_toUtf8(self->m_authPassword);
        size_t n = XString_toUtf8_length(self->m_authPassword);
        if (text && n)
            XLinkAuth_secureZero((void*)text, n);
        XClassDelete((XClass*)self->m_authPassword);
        self->m_authPassword = NULL;
    }
    if (self->m_authUser) {
        XClassDelete((XClass*)self->m_authUser);
        self->m_authUser = NULL;
    }
}

#endif /* XLINK_AUTH_ON */

/**
 * @brief 重连定时器节拍：每次触发尝试重连一次并计数，达到上限收手。
 */
static void XLinkClientPrivate_attemptReconnect(XLinkClient* self)
{
    if (!self)
        return;

    self->m_reconnectAttempts++;
    if (self->m_maxReconnectAttempts >= 0
        && self->m_reconnectAttempts > self->m_maxReconnectAttempts) {
        /* 达到最大重连次数（非 -1）：拆除残留并转 UnconnectedState 收手。 */
        XLinkClientPrivate_stopReconnectTimer(self);
        XLinkClientPrivate_teardown(self, false, true);
        XLinkClientPrivate_setState(self, XLinkClient_UnconnectedState);
        return;
    }
    /* 成对同步拆除上一轮尝试的残留（会话与套接字都要拆）：上一轮可能
       TCP 已连上、会话尚在 Connected 握手中——重连间隔小于会话 250ms
       内建节拍（XLINK_HEARTBEAT_TICK_MS）时必然撞上，只拆套接字会留下
       m_device 悬空且自带内部定时器仍在跑的 Connected 会话（pump 守卫
       只挡 NULL 设备/Inactive，不放行拦截），构成对已释放套接字解引用
       的 UAF 窗口，且残留会话会让服务端多挂一条连接。本定时器调用栈上
       无任何信号发射在途，成对同步释放符合对象释放时机判据。 */
    XLinkClientPrivate_teardown(self, false, false);
    /* 重连尝试期间保持 ReconnectingState（与首次连接的 Connecting 区分）；
       失败由下一节拍再试，成功经会话 Ready 转 Connected。 */
    XLinkClientPrivate_startConnect(self);
}

/* ========================================================================== */
/*                        静态虚函数实现                                     */
/* ========================================================================== */

/**
 * @brief 套接字 connected 槽：TCP 连上才建会话并套模板（会话挂载传输后
 *        自动握手，双方经自动 Hello/HelloAck 转 Ready）。
 */
static void VXLinkClient_onSocketConnected(XObject* receiver, XVarList* args)
{
    XLinkClient* self = (XLinkClient*)receiver;
    XLinkSession* session;

    (void)args;
    if (!self || !self->m_socket)
        return;
    /* 防御：不应有残留会话（会话只随套接字存活）；若有，延迟拆除防悬垂。 */
    if (self->m_session)
        XLinkClientPrivate_teardown(self, false, true);
    session = XLinkSession_create_ex((XIODevice*)self->m_socket);
    if (!session)
        return;
#if XLINK_AUTH_ON
    /* 有凭据：能力位补 Auth 并挂应答回调，握手后自动登录；无凭据：
       不宣告 Auth——服务端若强制认证，会在其侧按能力位缺失走拒绝路径。 */
    if (self->m_authUser && self->m_authPassword)
        XLinkSession_setCapability(
            session, (uint16_t)(self->m_capability | XLinkCapability_Auth));
    else if (self->m_capability)
        XLinkSession_setCapability(session, self->m_capability);
#else
    if (self->m_capability)
        XLinkSession_setCapability(session, self->m_capability);
#endif
#if XLINK_AUTH_ON
    if (self->m_authUser && self->m_authPassword) {
        XLinkSession_setAuthUser(session, XString_toUtf8(self->m_authUser));
        XLinkSession_setAuthResponse(session, XLinkClientPrivate_authRespond,
                                     self);
        XObject_connect_1((XObject*)session,
                          XSignal(XLinkSession_authChanged_signal),
                          (XObject*)self, VXLinkClient_onAuthChanged,
                          XConnectionType_Auto);
    }
#endif
    XLinkSession_setDispatchCallback(session, self->m_dispatchCallback,
                                     self->m_userData);
    self->m_session = session;
    /* 接会话 stateChanged：到 Ready 转 ConnectedState，到 Inactive 走断链处理。 */
    XObject_connect_1((XObject*)session,
                      XSignal(XLinkSession_stateChanged_signal),
                      (XObject*)self, VXLinkClient_onSessionStateChanged,
                      XConnectionType_Auto);
}

/**
 * @brief 套接字 disconnected 槽：传输层断开（对端关闭/网络故障）。
 */
static void VXLinkClient_onSocketDisconnected(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver)
        return;
    XLinkClientPrivate_handleLinkLost((XLinkClient*)receiver);
}

/**
 * @brief 会话 stateChanged 槽：Ready 转 ConnectedState 并清重连计数；
 *        Inactive 走断链处理（Goodbye/判线/违例均在此收敛）。
 */
static void VXLinkClient_onSessionStateChanged(XObject* receiver, XVarList* args)
{
    XLinkClient* self = (XLinkClient*)receiver;

    (void)args;
    if (!self || !self->m_session)
        return; /* 会话已拆除（延迟删除在途）的迟到信号，忽略 */
    if (XLinkSession_isReady(self->m_session)) {
        self->m_everReady = true;
        self->m_reconnectAttempts = 0;
        XLinkClientPrivate_stopReconnectTimer(self);
        XLinkClientPrivate_setState(self, XLinkClient_ConnectedState);
        return;
    }
    if (XLinkSession_state(self->m_session) == XLinkSessionState_Inactive)
        XLinkClientPrivate_handleLinkLost(self);
}

/**
 * @brief 定时器事件：只处理本类的重连定时器，非本类 id 经 XClass_Parent
 *        交回父类（照 VXLinkServer_timerEvent 写法）。
 */
static void VXLinkClient_timerEvent(XObject* object, XTimerEvent* event)
{
    XLinkClient* self = (XLinkClient*)object;
    if (!self || !event)
        return;
    if (XTimerEvent_timerId(event) != self->m_reconnectTimerId) {
        XClass_Parent(XObject, EXObject_TimerEvent,
                      void (*)(XObject*, XTimerEvent*))((XObject*)self, event);
        return;
    }
    XLinkClientPrivate_attemptReconnect(self);
}

/**
 * @brief 反初始化：停定时器、同步拆除会话与套接字、释放主机名，回链基类。
 * @note  末尾回链 XObject_deinit：注销残留定时器、清理子对象与信号连接，
 *        禁止省略。对象销毁路径不经任何信号发射栈，同步释放符合对象
 *        释放时机判据。
 */
static void VXLinkClient_deinit(XClass* object)
{
    XLinkClient* obj;
    if (!object)
        return;
    obj = (XLinkClient*)object;
    XLinkClientPrivate_stopReconnectTimer(obj);
    XLinkClientPrivate_teardown(obj, false, false);
    if (obj->m_host) {
        XClassDelete((XClass*)obj->m_host);
        obj->m_host = NULL;
    }
#if XLINK_AUTH_ON
    XLinkClientPrivate_freeCredential(obj);
#endif
    XClass_Deinit_Parent(XObject, (XObject*)obj);
}

/* ========================================================================== */
/*                        虚函数表初始化                                     */
/* ========================================================================== */

XVtable* XLinkClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLinkClient)
    XVTABLE_INHERIT_XCLASS(XObject);
    /* 先继承后重载（顺序颠倒重载会被继承覆盖，见风格指南）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLinkClient_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXLinkClient_timerEvent);
    /* 新增槽位（定时器事件）：预留派生类扩展点。 */
    XVTABLE_OVERLOAD_DEFAULT(EXLinkClient_timerEvent, VXLinkClient_timerEvent);
    return XVTABLE_DEFAULT;
}

/* ========================================================================== */
/*                        构造与析构                                         */
/* ========================================================================== */

void XLinkClient_init(XLinkClient* obj)
{
    if (!obj)
        return;
    memset(obj, 0, sizeof(XLinkClient));
    XObject_init((XObject*)obj);
    XClassSetVtable(obj, XLinkClient);
    obj->m_host = XString_create_utf8("");
    obj->m_port = 0;
    obj->m_capability = 0;
    obj->m_dispatchCallback = NULL;
    obj->m_userData = NULL;
    obj->m_autoReconnect = false;      /* 对齐 QModbusClient：默认关 */
    obj->m_reconnectInterval = 1000;   /* 默认 1 秒 */
    obj->m_maxReconnectAttempts = -1;  /* 默认无限 */
    obj->m_reconnectAttempts = 0;
    obj->m_socket = NULL;
    obj->m_session = NULL;
    obj->m_reconnectTimerId = XTIMER_INVALID_ID;
    obj->m_state = XLinkClient_UnconnectedState;
    obj->m_everReady = false;
    obj->m_userDisconnect = false;
#if XLINK_AUTH_ON
    obj->m_authUser = NULL;
    obj->m_authPassword = NULL;
    obj->m_lastAuthFailure = XLinkClientAuthFailure_None;
    obj->m_authFailed = false;
#endif
}

XLinkClient* XLinkClient_create(void)
{
    XMemoryType memory = XCLASS_DEFAULT_MEMORY_TYPE;
    XLinkClient* obj = (XLinkClient*)XMemory_malloc(sizeof(XLinkClient), memory);
    if (!obj)
        return NULL;
    XLinkClient_init(obj);
    Set_Class_Memory(obj, memory);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ========================================================================== */
/*                        连接参数与模板                                     */
/* ========================================================================== */

bool XLinkClient_setHostPort(XLinkClient* self, const char* host, uint16_t port)
{
    if (ISNULL(self, "XLinkClient_setHostPort self"))
        return false;
    if (!host || !host[0])
        return false;
    if (!self->m_host)
        return false;
    if (!XString_assign_utf8(self->m_host, host))
        return false;
    self->m_port = port;
    return true;
}

const char* XLinkClient_host_const(const XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_host_const self") || !self->m_host)
        return NULL;
    return XString_toUtf8(self->m_host);
}

uint16_t XLinkClient_port(const XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_port self"))
        return 0;
    return self->m_port;
}

bool XLinkClient_setCapability(XLinkClient* self, uint16_t capability)
{
    if (ISNULL(self, "XLinkClient_setCapability self"))
        return false;
    self->m_capability = capability;
    return true;
}

bool XLinkClient_setDispatchCallback(XLinkClient* self,
                                     XLinkSessionDispatchCallback callback,
                                     void* userData)
{
    if (ISNULL(self, "XLinkClient_setDispatchCallback self"))
        return false;
    self->m_dispatchCallback = callback;
    self->m_userData = userData;
    return true;
}

/* ========================================================================== */
/*                        自动重连配置                                       */
/* ========================================================================== */

bool XLinkClient_setAutoReconnect(XLinkClient* self, bool enabled)
{
    if (ISNULL(self, "XLinkClient_setAutoReconnect self"))
        return false;
    self->m_autoReconnect = enabled;
    /* 禁用时立即停掉进行中的重连定时器（下一次断线直接回 Unconnected）。 */
    if (!enabled)
        XLinkClientPrivate_stopReconnectTimer(self);
    return true;
}

bool XLinkClient_autoReconnect(const XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_autoReconnect self"))
        return false;
    return self->m_autoReconnect;
}

bool XLinkClient_setReconnectInterval(XLinkClient* self, uint32_t intervalMs)
{
    if (ISNULL(self, "XLinkClient_setReconnectInterval self"))
        return false;
    self->m_reconnectInterval = intervalMs;
    return true;
}

uint32_t XLinkClient_reconnectInterval(const XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_reconnectInterval self"))
        return 1000;
    return self->m_reconnectInterval;
}

bool XLinkClient_setMaxReconnectAttempts(XLinkClient* self, int16_t attempts)
{
    if (ISNULL(self, "XLinkClient_setMaxReconnectAttempts self"))
        return false;
    self->m_maxReconnectAttempts = attempts;
    return true;
}

int16_t XLinkClient_maxReconnectAttempts(const XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_maxReconnectAttempts self"))
        return -1;
    return self->m_maxReconnectAttempts;
}

int16_t XLinkClient_reconnectAttempts(const XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_reconnectAttempts self"))
        return 0;
    return self->m_reconnectAttempts;
}

/* ========================================================================== */
/*                        连接与断开                                         */
/* ========================================================================== */

bool XLinkClient_connectToServer(XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_connectToServer self"))
        return false;
    if (!self->m_host || XString_toUtf8_length(self->m_host) == 0)
        return false;

    /* 重连计数与守卫复位；已存在旧链路（含重连中）先同步拆除——本函数
       由调用方直接调用，不在任何信号发射栈上，同步释放符合判据。 */
    self->m_userDisconnect = false;
    self->m_everReady = false;
    self->m_reconnectAttempts = 0;
#if XLINK_AUTH_ON
    self->m_authFailed = false;
    self->m_lastAuthFailure = XLinkClientAuthFailure_None;
#endif
    XLinkClientPrivate_stopReconnectTimer(self);
    XLinkClientPrivate_teardown(self, false, false);

    XLinkClientPrivate_setState(self, XLinkClient_ConnectingState);
    XLinkClientPrivate_startConnect(self);
    return true;
}

void XLinkClient_disconnectFromServer(XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_disconnectFromServer self"))
        return;

    XLinkClientPrivate_stopReconnectTimer(self);
    /* 手动断开不触发重连：先立守卫再拆除，屏蔽拆除期间的迟到信号。 */
    self->m_userDisconnect = true;
    XLinkClientPrivate_teardown(self, true, false);
    self->m_reconnectAttempts = 0;
    self->m_everReady = false;
    self->m_userDisconnect = false;
    XLinkClientPrivate_setState(self, XLinkClient_UnconnectedState);
}

/* ========================================================================== */
/*                        查询                                               */
/* ========================================================================== */

XLinkSession* XLinkClient_session(const XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_session self"))
        return NULL;
    return self->m_session;
}

XLinkClientState XLinkClient_state(const XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_state self"))
        return XLinkClient_UnconnectedState;
    return self->m_state;
}

bool XLinkClient_isReady(const XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_isReady self"))
        return false;
    return self->m_state == XLinkClient_ConnectedState;
}

/* ========================================================================== */
/*                        信号                                               */
/* ========================================================================== */

void* XLinkClient_stateChanged_signal(XLinkClient* self, XLinkClientState state)
{
    XEmitSignal(self, XLinkClient_stateChanged_signal,
                XVarList_Create(XVar(XLinkClientState, state)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

#if XLINK_AUTH_ON

/* ========================================================================== */
/*                        登录凭据                                           */
/* ========================================================================== */

/** @brief 深拷贝一个 C 字符串到 XString（UTF-8）；空串/NULL 返回 NULL。 */
static XString* XLinkClientPrivate_dupString(const char* text)
{
    if (!text || !text[0])
        return NULL;
    return XString_create_utf8(text);
}

bool XLinkClient_setCredential(XLinkClient* self, const char* user,
                               const char* password)
{
    XString* userCopy;
    XString* passwordCopy;

    if (ISNULL(self, "XLinkClient_setCredential self"))
        return false;
    if (!user || !user[0] || !password || !password[0])
        return false;

    userCopy = XLinkClientPrivate_dupString(user);
    passwordCopy = XLinkClientPrivate_dupString(password);
    if (!userCopy || !passwordCopy) {
        if (userCopy) XClassDelete((XClass*)userCopy);
        if (passwordCopy) XClassDelete((XClass*)passwordCopy);
        return false;
    }
    XLinkClientPrivate_freeCredential(self);
    self->m_authUser = userCopy;
    self->m_authPassword = passwordCopy;
    self->m_authFailed = false;
    self->m_lastAuthFailure = XLinkClientAuthFailure_None;
    return true;
}

void XLinkClient_clearCredential(XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_clearCredential self"))
        return;
    XLinkClientPrivate_freeCredential(self);
    self->m_authFailed = false;
    self->m_lastAuthFailure = XLinkClientAuthFailure_None;
}

bool XLinkClient_hasCredential(const XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_hasCredential self"))
        return false;
    return self->m_authUser && self->m_authPassword;
}

const char* XLinkClient_user_const(const XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_user_const self") || !self->m_authUser)
        return NULL;
    return XString_toUtf8(self->m_authUser);
}

int XLinkClient_lastAuthFailure(const XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_lastAuthFailure self"))
        return 0;
    return self->m_lastAuthFailure;
}

bool XLinkClient_isAuthenticated(const XLinkClient* self)
{
    if (ISNULL(self, "XLinkClient_isAuthenticated self"))
        return false;
    if (!self->m_session)
        return false;
    return XLinkSession_isAuthenticated(self->m_session);
}

void* XLinkClient_authFailed_signal(XLinkClient* self, int reason,
                                     uint8_t status)
{
    XEmitSignal(self, XLinkClient_authFailed_signal,
                XVarList_Create(XVar(int, reason), XVar(uint8_t, status)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XLINK_AUTH_ON */

#endif /* XLINK_ON && XLINK_SESSION_ON && XLINK_CLIENT_ON */
