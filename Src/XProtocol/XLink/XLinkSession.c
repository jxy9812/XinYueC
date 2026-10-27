/** @file       XLinkSession.c
 * @brief      XLink 私有协议会话对象实现（收发/分发/订阅/待应答/心跳）。
 * @details    实现只依赖 XinYueC 抽象层（XIODevice/XObject/XMemory/XDateTime），
 *             禁止调用 Win32、POSIX 或其他平台 API。
 *
 *             双驱动口径：会话继承 XObject 后支持两种驱动方式，可并存——
 *               异步模式：setDevice 挂载传输时自动接线传输的 readyRead 信号
 *               （收包事件驱动）并启动内建定时器（心跳/判线节拍），时间由
 *               本类经 XDateTime_currentMSecsSinceEpoch()（单调毫秒）内部取；
 *               手动模式：调用方按圈调 XLinkSession_pump(nowMs) 注入时间
 *               （环回设备不发 readyRead、测试注入固定时钟均走此路径）。
 *               pump 幂等，两种驱动混用无副作用。
 * @author     XinYueC 团队
 */
#include "XLinkSession.h"

#if XLINK_ON && XLINK_SESSION_ON

#include "XLinkCodec.h"
#if XLINK_AUTH_ON
#include "XLinkAuth.h"
#endif
#include "XMemory.h"
#include "XDateTime.h"
#include "XPrintf.h"
#include <string.h>

/* ========================================================================== */
/*                  内部工具（异步驱动接线与自动握手）                         */
/* ========================================================================== */

/* 前置声明：attachAsync 接线与 deinit 摘线都引用 readyRead 槽
   （定义在下方静态虚函数段）。 */
static void VXLinkSession_onReadyRead(XObject* receiver, XVarList* args);

/**
 * @brief 状态迁移辅助：仅真变化时赋值并发射 stateChanged 信号。
 * @note  全类唯一的 m_state 写入口（init 置初值除外）：同态重复设置静默
 *        返回（不重发信号）；发射在赋值之后，槽内反查 state() 已是新值。
 *        两条信号函数的原型由 XLinkSession.h 提供（XEmitSignal 信号标识 =
 *        函数自身地址）。
 */
static void XLinkSessionPrivate_setState(XLinkSession* self,
                                         XLinkSessionState state)
{
    if (ISNULL(self, "XLinkSessionPrivate_setState self"))
        return;
    if (self->m_state == state)
        return;
    self->m_state = state;
    XLinkSession_stateChanged_signal(self, state);
}

/** @brief 异步驱动路径的当前时刻：单调毫秒。
 *  @note  异步路径（定时器事件/readyRead 槽）库内经 XDateTime 取时，与
 *         XAbstractSocket 等框架模块同口径（仓库时间源裁定：时长测量用
 *         XDateTime_currentMSecsSinceEpoch 单调毫秒）；手动 pump 模式仍由
 *         调用方注入时间，测试可注入固定时钟。 */
static uint32_t XLinkSessionPrivate_nowMs(void)
{
    return (uint32_t)XDateTime_currentMSecsSinceEpoch();
}

/** @brief 停止异步心跳定时器（未启动或无事件分发器时安全空操作）。 */
static void XLinkSessionPrivate_stopTimer(XLinkSession* self)
{
    if (self->m_timerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_timerId);
        self->m_timerId = XTIMER_INVALID_ID;
    }
}

/** @brief 启动异步驱动：内建定时器（心跳/判线节拍）+ 传输 readyRead 接线。
 *  @note  无事件分发器（裸测试环境）时 startTimer 返回无效 id，定时器自动
 *         缺席，会话退化为手动 pump 模式；readyRead 接线不受影响。
 *         环回设备等不发 readyRead 的传输同样退化为手动 pump。 */
static void XLinkSessionPrivate_attachAsync(XLinkSession* self,
                                            XIODevice* device)
{
    self->m_timerId = XTIMER_INVALID_ID;
#if XLINK_HEARTBEAT_TICK_MS > 0
    self->m_timerId = XObject_startTimer_ms((XObject*)self,
                                            XLINK_HEARTBEAT_TICK_MS,
                                            XTimerType_CoarseTimer);
#endif
    XObject_connect_1((XObject*)device, XSignal(XIODevice_readyRead_signal),
                      (XObject*)self, VXLinkSession_onReadyRead,
                      XConnectionType_Auto);
}

/** @brief 自动握手：Connected 态首次驱动时补发 Hello（每条链路仅一次）。
 *  @return 已发出或无需发送返回 true；编码/发送失败返回 false
 *          （不置 m_helloSent，由下一轮节拍/readyRead/pump 重试；
 *          成功后 m_helloSent 由 XLinkSession_sendSession(Hello) 置位）。 */
static bool XLinkSessionPrivate_ensureHello(XLinkSession* self)
{
    uint8_t helloBuf[4];
    if (self->m_helloSent || self->m_state != XLinkSessionState_Connected)
        return true;
    if (!XLinkCodec_encodeHello(&(XLinkHelloPayload){ XLINK_VERSION,
                                                      self->m_localCapability },
                                helloBuf, sizeof(helloBuf), NULL))
        return false;
    return XLinkSession_sendSession(self, XLinkSessionCode_Hello, helloBuf, 4);
}

/* ========================================================================== */
/*                        静态虚函数实现                                     */
/* ========================================================================== */

/**
 * @brief 反初始化：停定时器、释放收发缓冲并回链基类清理。
 * @param  object 基类指针（虚表槽统一签名），内部回转为 XLinkSession。
 * @note   末尾必须回链 XObject_deinit：定时器回调在计时线程异步触发，
 *         若对象先亡而分发器轮表残留，回调将向已释放内存 postEvent
 *         （实测堆 UAF）；XObject_deinit 负责对象级定时器注销与
 *         信号连接清理，禁止省略。
 */
static void VXLinkSession_deinit(XClass* object)
{
    XLinkSession* obj;
    if (!object)
        return;
    obj = (XLinkSession*)object;
    XLinkSessionPrivate_stopTimer(obj);
    /* 传输设备是借用对象，可能比本会话长寿：显式摘掉 readyRead 接线，
       防止设备后续收包回调到已释放的会话。 */
    if (obj->m_device) {
        XObject_disconnect_1((XObject*)obj->m_device,
                             XSignal(XIODevice_readyRead_signal),
                             (XObject*)obj, VXLinkSession_onReadyRead);
        obj->m_device = NULL;
    }
    if (obj->m_rxBuffer) {
        XFree_System(obj->m_rxBuffer);
        obj->m_rxBuffer = NULL;
    }
    if (obj->m_txBuffer) {
        XFree_System(obj->m_txBuffer);
        obj->m_txBuffer = NULL;
    }
    if (obj->m_txQueue) {
        XFree_System(obj->m_txQueue);
        obj->m_txQueue = NULL;
    }
    obj->m_rxFilled = 0;
    obj->m_rxExpected = 0;
    obj->m_txUsed = 0;
    obj->m_txHead = 0;
    obj->m_txTail = 0;
    obj->m_dispatchCallback = NULL;
    obj->m_userData = NULL;
    obj->m_authLookup = NULL;
    obj->m_authRespond = NULL;
    obj->m_fileCallback = NULL;
#if XLINK_AUTH_ON
    if (obj->m_authUser) {
        XClassDelete((XClass*)obj->m_authUser);
        obj->m_authUser = NULL;
    }
    if (obj->m_authPendingUser) {
        XClassDelete((XClass*)obj->m_authPendingUser);
        obj->m_authPendingUser = NULL;
    }
#endif
    XClass_Deinit_Parent(XObject, (XObject*)obj);
}

/**
 * @brief 默认消息分发实现：转调调用方注册的回调。
 * @note 未注册回调时静默丢弃（属正常用法：只发不收的单向用法）。
 */
static void VXLinkSession_dispatch(XLinkSession* self,
                                   const XLinkFrameHeader* frame,
                                   const uint8_t* payload)
{
    if (ISNULL(self, "VXLinkSession_dispatch self")
        || ISNULL(frame, "VXLinkSession_dispatch frame"))
        return;
    if (!self->m_dispatchCallback)
        return;
    self->m_dispatchCallback(self, frame, payload);
}

/** @brief readyRead 信号槽：事件驱动抽干传输收缓冲并逐帧解码分发。 */
static void VXLinkSession_onReadyRead(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver)
        return;
    XLinkSession_pump((XLinkSession*)receiver, XLinkSessionPrivate_nowMs());
}

/**
 * @brief 定时器事件：心跳发送与静默判线的异步节拍。
 * @note  只处理本会话启动的定时器（m_timerId 过滤，不吞用户自启的定时器）；
 *        其余 id 交回 XObject 默认实现（按风格指南经 XClass_Parent 调父类
 *        槽位，不得走对象自身 *_base 以免虚表递归）。
 */
static void VXLinkSession_timerEvent(XObject* object, XTimerEvent* event)
{
    XLinkSession* self = (XLinkSession*)object;
    if (!self || !event)
        return;
    if (XTimerEvent_timerId(event) != self->m_timerId) {
        XClass_Parent(XObject, EXObject_TimerEvent,
                      void(*)(XObject*, XTimerEvent*))((XObject*)self, event);
        return;
    }
    XLinkSession_pump(self, XLinkSessionPrivate_nowMs());
}

/* ========================================================================== */
/*                        虚函数表初始化                                     */
/* ========================================================================== */

XVtable* XLinkSession_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLinkSession)
    XVTABLE_INHERIT_XCLASS(XObject);
    /* 先继承后重载（顺序颠倒重载会被继承覆盖，见风格指南）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLinkSession_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXLinkSession_timerEvent);
    /* 新增槽位（消息分发）：派生类可重载以接管框架层之外的协议消息处理。 */
    XVTABLE_OVERLOAD_DEFAULT(EXLinkSession_dispatch, VXLinkSession_dispatch);
    return XVTABLE_DEFAULT;
}

/* ========================================================================== */
/*                        构造与析构                                         */
/* ========================================================================== */

void XLinkSession_init(XLinkSession* obj)
{
    if (!obj)
        return;
    memset(obj, 0, sizeof(XLinkSession));
    XObject_init((XObject*)obj);
    XClassSetVtable(obj, XLinkSession);
    /* 收发缓冲全生命周期一次性申请，稳态收发零堆分配。 */
    obj->m_rxBuffer = (uint8_t*)XMalloc_System(
        (size_t)(XLINK_HEADER_SIZE + XLINK_MAX_PAYLOAD));
    obj->m_txBuffer = (uint8_t*)XMalloc_System(
        (size_t)(XLINK_HEADER_SIZE + XLINK_MAX_PAYLOAD));
#if XLINK_TX_QUEUE_BYTES > 0
    obj->m_txQueue = (uint8_t*)XMalloc_System((size_t)XLINK_TX_QUEUE_BYTES);
    obj->m_txQueueCap = obj->m_txQueue ? (uint32_t)XLINK_TX_QUEUE_BYTES : 0;
#endif
    obj->m_txHead = 0;
    obj->m_txTail = 0;
    obj->m_txUsed = 0;
    obj->m_device = NULL;
    obj->m_timerId = XTIMER_INVALID_ID;
    obj->m_helloSent = false;
    obj->m_state = XLinkSessionState_Inactive;
    obj->m_nextRequestId = 1; /* 0 保留为"无效 id" */
    /* 请求自动超时/重试默认值：被动模式（0/0），行为与无自动重试时一致。 */
    obj->m_retryCount = XLINK_RETRY_MAX_DEFAULT;
    obj->m_requestTimeoutMs = XLINK_REQUEST_TIMEOUT_MS;
    /* 握手/认证超时默认值（编译期可裁剪）。 */
    obj->m_attachTick = 0;
    obj->m_authStartTick = 0;
    obj->m_handshakeTimeoutMs = XLINK_HANDSHAKE_TIMEOUT_MS;
    obj->m_authTimeoutMs = XLINK_AUTH_TIMEOUT_MS;
    obj->m_authFailureLimit = XLINK_AUTH_FAILURE_LIMIT;
    obj->m_authRole = XLinkAuthRole_Anonymous;
    obj->m_authFailures = 0;
    obj->m_authRequired = false;
    obj->m_authPassed = false;
    obj->m_authUser = XString_create_utf8("");
    obj->m_authPendingUser = XString_create_utf8("");
    memset(&obj->m_stats, 0, sizeof(obj->m_stats));
}

XLinkSession* XLinkSession_create(void)
{
    XMemoryType memory = XCLASS_DEFAULT_MEMORY_TYPE;
    XLinkSession* obj = (XLinkSession*)XMemory_malloc(sizeof(XLinkSession),
                                                      memory);
    if (!obj)
        return NULL;
    XLinkSession_init(obj);
    Set_Class_Memory(obj, memory);
    Set_Class_IsHeap(obj, true);
    return obj;
}

XLinkSession* XLinkSession_create_ex(XIODevice* device)
{
    XLinkSession* obj = XLinkSession_create();
    if (!obj)
        return NULL;
    XLinkSession_setDevice(obj, device);
    return obj;
}

/* ========================================================================== */
/*                        虚函数调度入口                                     */
/* ========================================================================== */

void XLinkSession_dispatch_base(XLinkSession* self,
                                const XLinkFrameHeader* frame,
                                const uint8_t* payload)
{
    if (ISNULL(self, "XLinkSession_dispatch_base self")
        || ISNULL(frame, "XLinkSession_dispatch_base frame")
        || ISNULL(XClassGetVtable(self), "Vtable"))
        return;
    XClassGetVirtualFunc(self, EXLinkSession_dispatch,
                         void(*)(XLinkSession*, const XLinkFrameHeader*,
                                  const uint8_t*))(self, frame, payload);
}

/* ========================================================================== */
/*                        内部工具                                           */
/* ========================================================================== */

/** @brief 分配下一个请求 id（0 跳过并回绕）。 */
static uint32_t XLinkSessionPrivate_nextRequestId(XLinkSession* self)
{
    uint32_t id = self->m_nextRequestId++;
    if (self->m_nextRequestId == 0)
        self->m_nextRequestId = 1;
    if (id == 0)
        id = self->m_nextRequestId++;
    return id;
}

/** @brief 清空待应答表与订阅表（换设备/断链时调用）。 */
static void XLinkSessionPrivate_clearTables(XLinkSession* self)
{
    memset(self->m_pending, 0, sizeof(self->m_pending));
    memset(self->m_subscriptions, 0, sizeof(self->m_subscriptions));
}

/**
 * @brief 登记一条待应答并快照载荷（自动重发用）；表满返回 false。
 * @note  载荷不超过 XLINK_RETRY_PAYLOAD_MAX 时快照进槽（空载荷也算可重试，
 *        m_payloadLen=0）；超长请求照常发送但 m_payloadLen 置 0 且
 *        m_retryCount 置 0xFF 哨兵，标记不可重试（避免定容表膨胀，
 *        见 XLink_config.h XLINK_RETRY_PAYLOAD_MAX）。
 */
static bool XLinkSessionPrivate_addPending(XLinkSession* self, uint32_t id,
                                           uint16_t command, uint32_t nowMs,
                                           const uint8_t* payload,
                                           uint32_t length)
{
    int i;
    for (i = 0; i < XLINK_PENDING_SLOTS; i++) {
        if (!self->m_pending[i].m_used) {
            self->m_pending[i].m_used = true;
            self->m_pending[i].m_requestId = id;
            self->m_pending[i].m_command = command;
            self->m_pending[i].m_sendTick = nowMs;
            if (length <= XLINK_RETRY_PAYLOAD_MAX) {
                self->m_pending[i].m_payloadLen = length;
                self->m_pending[i].m_retryCount = 0;
                /* payload 为 NULL 的非法组合由 send 校验拒绝并出表，
                   这里不再重复判错，仅防快照时解引用空指针。 */
                if (length > 0 && payload)
                    memcpy(self->m_pending[i].m_payload, payload, length);
            } else {
                /* 超长不可重试：不快照；0xFF 哨兵永不满足重试额度判定 */
                self->m_pending[i].m_payloadLen = 0;
                self->m_pending[i].m_retryCount = 0xFFu;
            }
            return true;
        }
    }
    return false; /* 表满 */
}

/** @brief 按 id 移除一条待应答；未命中返回 false。 */
static bool XLinkSessionPrivate_removePending(XLinkSession* self, uint32_t id)
{
    int i;
    for (i = 0; i < XLINK_PENDING_SLOTS; i++) {
        if (self->m_pending[i].m_used && self->m_pending[i].m_requestId == id) {
            self->m_pending[i].m_used = false;
            return true;
        }
    }
    return false;
}

/** @brief 追加订阅；表满返回 false。 */
static bool XLinkSessionPrivate_addSub(XLinkSession* self, uint32_t topicId)
{
    int i;
    for (i = 0; i < XLINK_TOPIC_SLOTS; i++) {
        /* 槽位占用位走会话级位图（表项退化为纯 u32，16 槽 128→64 字节）。 */
        if (!(self->m_subscriptionUsed & (1u << (unsigned)i))) {
            self->m_subscriptionUsed |= (1u << (unsigned)i);
            self->m_subscriptions[i].m_topicId = topicId;
            return true;
        }
    }
    return false;
}

/** @brief 移除订阅；未命中返回 false。 */
static bool XLinkSessionPrivate_removeSub(XLinkSession* self, uint32_t topicId)
{
    int i;
    for (i = 0; i < XLINK_TOPIC_SLOTS; i++) {
        if ((self->m_subscriptionUsed & (1u << (unsigned)i))
            && self->m_subscriptions[i].m_topicId == topicId) {
            self->m_subscriptionUsed &= ~(1u << (unsigned)i);
            return true;
        }
    }
    return false;
}

#if XLINK_AUTH_ON

/* 前置声明：认证内核函数在使用点之后定义（与 onFrame 相互引用）。 */
static void XLinkSessionPrivate_onAuthFrame(XLinkSession* self,
                                            const XLinkFrameHeader* frame,
                                            const uint8_t* payload);
static void XLinkSessionPrivate_authFail(XLinkSession* self, uint8_t status,
                                         bool notifyPeer);

/** @brief 发送认证会话帧（统一走 sendSession，自动处理状态前置校验）。
 *  @return 成功返回 true。 */
static bool XLinkSessionPrivate_sendAuthFrame(XLinkSession* self,
                                              XLinkSessionCode code,
                                              const uint8_t* buf, size_t len)
{
    return XLinkSession_sendSession(self, code, buf, (uint32_t)len);
}

/**
 * @brief 认证结果落地：通过则转 Ready，失败则计数并可能断链。
 * @param self 会话。
 * @param status 结果码。
 * @param notifyPeer true 时服务端向对端发送 AuthResult（失败路径）。
 * @param role 通过时的角色。
 * @param user 通过时的用户名（可为 NULL）。
 * @param proof 通过时的服务端证明（服务端视角；客户端为 NULL）。
 */
static void XLinkSessionPrivate_authConclude(XLinkSession* self,
                                             uint8_t status, bool notifyPeer,
                                             uint8_t role, const char* user,
                                             const uint8_t* proof)
{
    if (status == XLinkAuthStatus_Ok) {
        self->m_authPassed = true;
        self->m_authRole = role;
        self->m_authFailures = 0;
        if (self->m_authUser)
            XString_assign_utf8(self->m_authUser, user ? user : "");
        /* 认证结论先落状态再发信号：槽内反查已是新值。 */
        XLinkSessionPrivate_setState(self, XLinkSessionState_Ready);
        XLinkSession_authChanged_signal(self, true, status, role);
        return;
    }

    /* 失败：服务端按失败上限决定是否断链；客户端收到拒绝直接终局。 */
    if (self->m_authFailures < 0xFFu)
        self->m_authFailures++;

    if (notifyPeer) {
        XLinkAuthResultPayload result;
        uint8_t buf[4 + XLINK_AUTH_TEXT_BYTES + XLINK_AUTH_NONCE_BYTES];
        size_t len = 0;
        const char* text = status == XLinkAuthStatus_BadUser
                               ? "unknown user"
                               : (status == XLinkAuthStatus_Locked
                                      ? "account locked"
                                      : (status == XLinkAuthStatus_BadPassword
                                             ? "bad password"
                                             : "authentication rejected"));
        memset(&result, 0, sizeof(result));
        result.m_status = status;
        result.m_role = 0;
        result.m_textLen = (uint16_t)strlen(text);
        memcpy(result.m_text, text, result.m_textLen);
        /* 失败结果不带证明；编解码要求 32 字节字段，全 0 即可。 */
        if (XLinkAuth_encodeResult(&result, buf, sizeof(buf), &len))
            XLinkSessionPrivate_sendAuthFrame(self,
                                              XLinkSessionCode_AuthResult,
                                              buf, len);
    }

    XLinkSession_authChanged_signal(self, false, status, 0);

    /* 客户端：服务端拒绝即终局断链（口令错误属永久失败，不该重试）。
       服务端：达到失败上限才断链，未达上限允许对端重试（换用户/换口令）。 */
    if (!notifyPeer) {
        XLinkSessionPrivate_authFail(self, status, false);
        return;
    }
    if (self->m_authFailures >= self->m_authFailureLimit) {
        /* Goodbye 原因字节让对端区分「认证失败」与普通断开。 */
        uint8_t reason = XLinkGoodbyeReason_AuthFailed;
        XLinkSession_sendSession(self, XLinkSessionCode_Goodbye, &reason, 1);
        XLinkSession_reset(self);
    }
}

/**
 * @brief 认证失败收尾：清敏感态并按需断开。
 * @param notifyPeer true 表示本端是客户端，需要通知服务端原因。
 */
static void XLinkSessionPrivate_authFail(XLinkSession* self, uint8_t status,
                                         bool notifyPeer)
{
    (void)status;
    (void)notifyPeer;
    XLinkSession_authChanged_signal(self, false, status, 0);
    XLinkSession_reset(self);
}

/**
 * @brief 服务端处理 AuthRequest：查表下发挑战。
 */
static void XLinkSessionPrivate_handleAuthRequest(XLinkSession* self,
                                                  const uint8_t* payload,
                                                  uint32_t payloadLen)
{
    XLinkAuthRequestPayload request;
    XLinkAuthChallengePayload challenge;
    uint8_t buf[8 + XLINK_AUTH_SALT_BYTES + XLINK_AUTH_NONCE_BYTES];
    size_t len = 0;

    if (!XLinkAuth_decodeRequest(payload, payloadLen, &request)) {
        XLinkSession_authChallenge_signal(self, "", XLinkAuthStatus_Protocol);
        XLinkSessionPrivate_authConclude(self, XLinkAuthStatus_Protocol, true,
                                         0, NULL, NULL);
        return;
    }
    if (!self->m_authLookup) {
        /* 本端未要求认证却收到请求：按协议错误拒绝（不泄露账户信息）。 */
        XLinkSessionPrivate_authConclude(self, XLinkAuthStatus_Protocol, true,
                                         0, NULL, NULL);
        return;
    }

    /* 重置本连接的挑战上下文（同一连接重试时换新盐与新 nonce）。 */
    memset(self->m_authHash, 0, sizeof(self->m_authHash));
    memset(self->m_authNonce, 0, sizeof(self->m_authNonce));
    memset(self->m_authSalt, 0, sizeof(self->m_authSalt));
    self->m_authIterations = 0;
    self->m_authPendingRole = XLinkAuthRole_Anonymous;
    if (self->m_authPendingUser)
        XContainer_clear_base((XContainer*)self->m_authPendingUser);
    self->m_authChallengeSent = false;

    memset(&challenge, 0, sizeof(challenge));
    challenge.m_method = (uint8_t)XLinkAuthMethod_IteratedHmac;
    if (!XLinkAuth_randomFill(challenge.m_nonce, XLINK_AUTH_NONCE_BYTES)) {
        XLinkSessionPrivate_authConclude(self, XLinkAuthStatus_Rejected, true,
                                         0, NULL, NULL);
        return;
    }

    /* 查表：用户不存在/锁定时的处理口径是**不区分**（同回"挑战"），
       避免把用户存在性暴露给未认证连接；后续应答必然不匹配。 */
    {
        uint8_t lookupRole = XLinkAuthRole_Anonymous;
        uint32_t iterations = XLINK_AUTH_HASH_ITERATIONS;
        uint8_t lookupHash[XLINK_AUTH_NONCE_BYTES];
        uint8_t lookupSalt[XLINK_AUTH_SALT_BYTES];
        int lookup;

        memset(lookupHash, 0, sizeof(lookupHash));
        memset(lookupSalt, 0, sizeof(lookupSalt));
        lookup = self->m_authLookup(self->m_authLookupUserData,
                                    request.m_name, lookupSalt, &iterations,
                                    lookupHash, &lookupRole);
        if (lookup != XLinkSessionAuthLookup_Ok) {
            /* 用户不存在/锁定：下发随机盐 + 随机 nonce，但摘要保持全 0，
               应答永远不可能匹配（常量时间比较照样执行）。
               用随机盐而非账户盐，避免泄漏"该用户名是否存在"。 */
            if (!XLinkAuth_randomFill(challenge.m_salt,
                                      XLINK_AUTH_SALT_BYTES)) {
                XLinkSessionPrivate_authConclude(
                    self, XLinkAuthStatus_Rejected, true, 0, NULL, NULL);
                return;
            }
            challenge.m_iterations = XLINK_AUTH_HASH_ITERATIONS;
            memset(self->m_authHash, 0, sizeof(self->m_authHash));
            memcpy(self->m_authSalt, challenge.m_salt,
                   XLINK_AUTH_SALT_BYTES);
            memcpy(self->m_authNonce, challenge.m_nonce,
                   XLINK_AUTH_NONCE_BYTES);
            self->m_authIterations = challenge.m_iterations;
            if (self->m_authPendingUser)
                XString_assign_utf8(self->m_authPendingUser, request.m_name);
            self->m_authPendingRole = XLinkAuthRole_Anonymous;
            self->m_authChallengeSent = true;
            XLinkSessionPrivate_setState(self,
                                         XLinkSessionState_Authenticating);
            XLinkSession_authChallenge_signal(self, request.m_name, lookup);
            if (XLinkAuth_encodeChallenge(&challenge, buf, sizeof(buf),
                                          &len))
                XLinkSessionPrivate_sendAuthFrame(
                    self, XLinkSessionCode_AuthChallenge, buf, len);
            return;
        }

        /* 命中：挑战必须携带**账户存储的盐**与迭代次数——客户端据此
           派生 H，服务端才能用同一 H 复算应答。若这里换新盐，双方 H
           不同，应答必然不匹配。 */
        memcpy(challenge.m_salt, lookupSalt, XLINK_AUTH_SALT_BYTES);
        challenge.m_iterations = iterations;
        memcpy(self->m_authSalt, challenge.m_salt, XLINK_AUTH_SALT_BYTES);
        memcpy(self->m_authNonce, challenge.m_nonce, XLINK_AUTH_NONCE_BYTES);
        self->m_authIterations = iterations;
        memcpy(self->m_authHash, lookupHash, XLINK_AUTH_NONCE_BYTES);
        XLinkAuth_secureZero(lookupHash, sizeof(lookupHash));
        self->m_authPendingRole = lookupRole;
        if (self->m_authPendingUser)
            XString_assign_utf8(self->m_authPendingUser, request.m_name);
        self->m_authChallengeSent = true;

        XLinkSessionPrivate_setState(self, XLinkSessionState_Authenticating);
        XLinkSession_authChallenge_signal(self, request.m_name,
                                          XLinkSessionAuthLookup_Ok);
        if (XLinkAuth_encodeChallenge(&challenge, buf, sizeof(buf), &len))
            XLinkSessionPrivate_sendAuthFrame(
                self, XLinkSessionCode_AuthChallenge, buf, len);
    }
}

/**
 * @brief 服务端处理 AuthResponse：同式重算并比对。
 */
static void XLinkSessionPrivate_handleAuthResponse(XLinkSession* self,
                                                   const uint8_t* payload,
                                                   uint32_t payloadLen)
{
    XLinkAuthResponsePayload response;
    uint8_t expected[XLINK_AUTH_NONCE_BYTES];

    if (!self->m_authChallengeSent || self->m_authIterations == 0) {
        /* 未发挑战就收到应答：协议违例。 */
        XLinkSessionPrivate_authConclude(self, XLinkAuthStatus_Protocol, true,
                                         0, NULL, NULL);
        return;
    }
    if (!XLinkAuth_decodeResponse(payload, payloadLen, &response)
        || response.m_method != (uint8_t)XLinkAuthMethod_IteratedHmac) {
        XLinkSessionPrivate_authConclude(self, XLinkAuthStatus_Protocol, true,
                                         0, NULL, NULL);
        return;
    }

    memset(expected, 0, sizeof(expected));
    if (!XLinkAuth_computeResponse(self->m_authHash, self->m_authNonce,
                                   expected)) {
        XLinkAuth_secureZero(expected, sizeof(expected));
        XLinkSessionPrivate_authConclude(self, XLinkAuthStatus_Rejected, true,
                                         0, NULL, NULL);
        return;
    }

    if (!XLinkAuth_constantTimeEqual(expected, response.m_response,
                                     XLINK_AUTH_NONCE_BYTES)) {
        XLinkAuth_secureZero(expected, sizeof(expected));
        /* 用户不存在与口令错误对外同码（不暴露账户存在性）。 */
        XLinkSessionPrivate_authConclude(self, XLinkAuthStatus_BadPassword,
                                         true, 0, NULL, NULL);
        return;
    }

    /* 通过：下发带服务端证明的 AuthResult，然后转 Ready。
       先发结果帧再转状态——结果帧本身不受 Ready 门控。 */
    {
        XLinkAuthResultPayload result;
        uint8_t buf[4 + XLINK_AUTH_TEXT_BYTES + XLINK_AUTH_NONCE_BYTES];
        size_t len = 0;
        memset(&result, 0, sizeof(result));
        result.m_status = XLinkAuthStatus_Ok;
        result.m_role = self->m_authPendingRole;
        result.m_textLen = 0;
        if (!XLinkAuth_computeServerProof(self->m_authHash, self->m_authNonce,
                                          result.m_serverProof)
            || !XLinkAuth_encodeResult(&result, buf, sizeof(buf), &len)
            || !XLinkSessionPrivate_sendAuthFrame(
                self, XLinkSessionCode_AuthResult, buf, len)) {
            XLinkAuth_secureZero(expected, sizeof(expected));
            XLinkSessionPrivate_authConclude(self, XLinkAuthStatus_Rejected,
                                             true, 0, NULL, NULL);
            return;
        }
    }
    XLinkAuth_secureZero(expected, sizeof(expected));

    /* 服务端清理挑战临时态：只保留认证结果。 */
    XLinkAuth_secureZero(self->m_authHash, sizeof(self->m_authHash));
    XLinkAuth_secureZero(self->m_authNonce, sizeof(self->m_authNonce));
    XLinkAuth_secureZero(self->m_authSalt, sizeof(self->m_authSalt));
    self->m_authIterations = 0;
    self->m_authChallengeSent = false;
    XLinkSessionPrivate_authConclude(self, XLinkAuthStatus_Ok, false,
                                     self->m_authPendingRole,
                                     XString_toUtf8(self->m_authPendingUser),
                                     NULL);
}

/**
 * @brief 客户端处理 AuthChallenge：本地派生并回应答。
 */
static void XLinkSessionPrivate_handleAuthChallenge(XLinkSession* self,
                                                    const uint8_t* payload,
                                                    uint32_t payloadLen)
{
    XLinkAuthChallengePayload challenge;
    XLinkAuthResponsePayload response;
    uint8_t computed[XLINK_AUTH_NONCE_BYTES];
    uint8_t buf[4 + XLINK_AUTH_NONCE_BYTES];
    size_t len = 0;
    uint8_t method = (uint8_t)XLinkAuthMethod_IteratedHmac;

    if (!self->m_authRespond) {
        /* 对端要求认证但本端无凭据：按 AuthRequired 断链。 */
        uint8_t reason = XLinkGoodbyeReason_AuthRequired;
        XLinkSession_sendSession(self, XLinkSessionCode_Goodbye, &reason, 1);
        XLinkSession_reset(self);
        return;
    }
    if (!XLinkAuth_decodeChallenge(payload, payloadLen, &challenge)) {
        XLinkSessionPrivate_authFail(self, XLinkAuthStatus_Protocol, false);
        return;
    }

    /* 保存挑战上下文（用于校验服务端证明）。 */
    memcpy(self->m_authSalt, challenge.m_salt, XLINK_AUTH_SALT_BYTES);
    memcpy(self->m_authNonce, challenge.m_nonce, XLINK_AUTH_NONCE_BYTES);
    self->m_authIterations = challenge.m_iterations;

    memset(computed, 0, sizeof(computed));
    memset(self->m_authHash, 0, sizeof(self->m_authHash));
    if (!self->m_authRespond(self->m_authRespondUserData, &challenge, &method,
                             computed, self->m_authHash)) {
        XLinkAuth_secureZero(computed, sizeof(computed));
        XLinkSessionPrivate_authFail(self, XLinkAuthStatus_Rejected, false);
        return;
    }

    memset(&response, 0, sizeof(response));
    response.m_method = method;
    response.m_len = XLINK_AUTH_NONCE_BYTES;
    memcpy(response.m_response, computed, XLINK_AUTH_NONCE_BYTES);
    XLinkAuth_secureZero(computed, sizeof(computed));

    if (!XLinkAuth_encodeResponse(&response, buf, sizeof(buf), &len)
        || !XLinkSessionPrivate_sendAuthFrame(
            self, XLinkSessionCode_AuthResponse, buf, len)) {
        XLinkSessionPrivate_authFail(self, XLinkAuthStatus_Rejected, false);
        return;
    }

    if (self->m_state != XLinkSessionState_Authenticating) {
        self->m_authStartTick = 0;
        XLinkSessionPrivate_setState(self, XLinkSessionState_Authenticating);
    }
}

/**
 * @brief 客户端处理 AuthResult：校验结果与服务端证明。
 */
static void XLinkSessionPrivate_handleAuthResult(XLinkSession* self,
                                                 const uint8_t* payload,
                                                 uint32_t payloadLen)
{
    XLinkAuthResultPayload result;
    uint8_t expectedProof[XLINK_AUTH_NONCE_BYTES];

    if (!XLinkAuth_decodeResult(payload, payloadLen, &result)) {
        XLinkSessionPrivate_authFail(self, XLinkAuthStatus_Protocol, false);
        return;
    }
    if (result.m_status != XLinkAuthStatus_Ok) {
        XLinkSessionPrivate_authConclude(self, result.m_status, false, 0,
                                         NULL, NULL);
        return;
    }

    /* 校验服务端证明：只有持有同一 H 的一方才能算出正确 proof。
       若客户端尚未保存 H（应由应答回调侧填写），这里按"无法验证"处理。 */
    memset(expectedProof, 0, sizeof(expectedProof));
    if (self->m_authIterations == 0
        || !XLinkAuth_computeServerProof(self->m_authHash, self->m_authNonce,
                                         expectedProof)
        || !XLinkAuth_constantTimeEqual(expectedProof, result.m_serverProof,
                                        XLINK_AUTH_NONCE_BYTES)) {
        XLinkAuth_secureZero(expectedProof, sizeof(expectedProof));
        /* 证明不符：可能是中间人或配置错误，绝不放行。 */
        XLinkSessionPrivate_authFail(self, XLinkAuthStatus_Rejected, false);
        return;
    }
    XLinkAuth_secureZero(expectedProof, sizeof(expectedProof));

    XLinkAuth_secureZero(self->m_authHash, sizeof(self->m_authHash));
    XLinkAuth_secureZero(self->m_authNonce, sizeof(self->m_authNonce));
    self->m_authIterations = 0;
    /* 客户端同样记录用户名：服务端不回传用户名（不泄露账户存在性），
       本地登录名由客户端自己在发起时保存。 */
    XLinkSessionPrivate_authConclude(self, XLinkAuthStatus_Ok, false,
                                     result.m_role,
                                     XString_toUtf8(self->m_authPendingUser),
                                     NULL);
}

/**
 * @brief 认证帧入口：按本端角色（是否要求认证）分流。
 */
static void XLinkSessionPrivate_onAuthFrame(XLinkSession* self,
                                            const XLinkFrameHeader* frame,
                                            const uint8_t* payload)
{
    /* 握手未完成就收到认证帧：协议违例（认证必须在握手之后）。 */
    if (self->m_state != XLinkSessionState_Connected
        && self->m_state != XLinkSessionState_Authenticating) {
        /* Ready 后重复认证帧忽略（幂等，防重放干扰）。
           Inactive 已无意义。 */
        return;
    }

    switch ((XLinkSessionCode)frame->m_msgCode) {
    case XLinkSessionCode_AuthRequest:
        /* 只有服务端（要求认证）处理；客户端收到属协议违例，忽略。 */
        if (self->m_authRequired)
            XLinkSessionPrivate_handleAuthRequest(self, payload,
                                                  frame->m_payloadLen);
        break;
    case XLinkSessionCode_AuthChallenge:
        /* 只有客户端（配置了应答回调）处理。 */
        if (!self->m_authRequired && self->m_authRespond)
            XLinkSessionPrivate_handleAuthChallenge(self, payload,
                                                    frame->m_payloadLen);
        break;
    case XLinkSessionCode_AuthResponse:
        if (self->m_authRequired)
            XLinkSessionPrivate_handleAuthResponse(self, payload,
                                                   frame->m_payloadLen);
        break;
    case XLinkSessionCode_AuthResult:
        if (!self->m_authRequired)
            XLinkSessionPrivate_handleAuthResult(self, payload,
                                                 frame->m_payloadLen);
        break;
    default:
        break;
    }
}

#endif /* XLINK_AUTH_ON */

/**
 * @brief 会话层内部消息处理：维护握手状态机与待应答表，业务消息再交给分发。
 * @details 与 VXLinkSession_dispatch 分工：后者是调用方可见的分发槽位，
 *          本函数在框架层先行消化会话帧（Hello 记录能力交集、Response 出表），
 *          避免这些协议内务泄漏到应用回调。
 */
static void XLinkSessionPrivate_onFrame(XLinkSession* self,
                                        const XLinkFrameHeader* frame,
                                        const uint8_t* payload)
{
    uint8_t helloBuf[4];
    XLinkHelloPayload hello;

    switch (frame->m_msgClass) {
    case XLinkMsgClass_Session:
        switch ((XLinkSessionCode)frame->m_msgCode) {
        case XLinkSessionCode_Hello:
        case XLinkSessionCode_HelloAck:
            if (XLinkCodec_decodeHello(payload, frame->m_payloadLen, &hello)) {
                self->m_remoteVersion = hello.m_version;
                self->m_peerDeclaredCapability = hello.m_capability;
                self->m_remoteCapability = (uint16_t)(
                    hello.m_capability & self->m_localCapability);
                if (frame->m_msgCode == XLinkSessionCode_Hello) {
                    /* 对端先发 Hello：本端应答 HelloAck（对称，见头文件设计）。 */
                    if (XLinkCodec_encodeHello(
                            &(XLinkHelloPayload){ XLINK_VERSION,
                                                  self->m_localCapability },
                            helloBuf, sizeof(helloBuf), NULL)) {
                        XLinkFrameHeader ack;
                        ack.m_payloadLen = 4;
                        ack.m_msgClass = XLinkMsgClass_Session;
                        ack.m_msgCode = XLinkSessionCode_HelloAck;
                        ack.m_requestId = 0;
                        XLinkSession_send(self, &ack, helloBuf);
                    }
                }
#if XLINK_AUTH_ON
                /* 握手完成后的分流（顺序即优先级）：
                   1) 客户端配了凭据且对端要求认证 → 主动发 AuthRequest，
                      进入 Authenticating（不置 authPassed）；
                   2) 本端要求认证且对端支持 → 进入 Authenticating 等对端；
                   3) 本端要求认证但对端不支持 → 默认拒绝（防能力位降级绕过）；
                   4) 客户端无凭据但对端要求认证 → 快速失败（不发必错请求，
                      也不谎报 Ready；防止短暂 Ready 让业务误判可用）；
                   5) 其余 → 匿名链路，直接 Ready。
                   整段仅在**握手尚未定局**（Connected）时执行：自动握手会让
                   双方各收到 Hello 与 HelloAck 两帧，第二次进入时状态已是
                   Ready/Authenticating，必须整体跳过——否则认证态会被覆盖
                   成 Ready（认证门控失效）。 */
                if (self->m_state == XLinkSessionState_Connected) {
                    /* "对端要求认证"必须看对端**原始声明**（不是交集）：
                       客户端未宣告 Auth 时交集里没有该位，但服务端仍要求，
                       必须能识别出来并快速失败，而不是短暂 Ready。 */
                    bool peerNeedsAuth = !self->m_authRequired
                        && (self->m_peerDeclaredCapability
                            & XLinkCapability_Auth);
                    if (peerNeedsAuth && self->m_authRespond) {
                        XLinkAuthRequestPayload request;
                        uint8_t authBuf[1 + XLINK_AUTH_NAME_BYTES];
                        size_t authLen = 0;
                        const char* pendingUser =
                            XString_toUtf8(self->m_authPendingUser);
                        size_t nameLen = pendingUser ? strlen(pendingUser) : 0;
                        memset(&request, 0, sizeof(request));
                        if (nameLen == 0 || nameLen >= XLINK_AUTH_NAME_BYTES) {
                            /* 无用户名：无法登录，按 AuthRequired 断开。 */
                            uint8_t reason = XLinkGoodbyeReason_AuthRequired;
                            XLinkSession_sendSession(
                                self, XLinkSessionCode_Goodbye, &reason, 1);
                            XLinkSession_reset(self);
                            return;
                        }
                        request.m_nameLen = (uint8_t)nameLen;
                        memcpy(request.m_name, pendingUser, nameLen);
                        if (!XLinkAuth_encodeRequest(&request, authBuf,
                                                    sizeof(authBuf), &authLen)
                            || !XLinkSessionPrivate_sendAuthFrame(
                                self, XLinkSessionCode_AuthRequest, authBuf,
                                authLen)) {
                            XLinkSession_reset(self);
                            return;
                        }
                        self->m_authStartTick = 0;
                        XLinkSessionPrivate_setState(
                            self, XLinkSessionState_Authenticating);
                    }
                    else if (self->m_authRequired
                             && (self->m_remoteCapability
                                 & XLinkCapability_Auth)) {
                        /* 服务端视角：交集里含 Auth 说明对端确实支持登录，
                           进入等待态等它发 AuthRequest。 */
                        self->m_authStartTick = 0; /* 首拍起算，见 pump 超时 */
                        XLinkSessionPrivate_setState(
                            self, XLinkSessionState_Authenticating);
                    }
                    else if (self->m_authRequired
                             && !self->m_authAllowAnonymous) {
                        /* 本端要求认证但对端不宣告 Auth（老对端/恶意降级）：
                           默认拒绝。允许匿名时才回落 Ready（显式兼容）。 */
                        uint8_t reason = XLinkGoodbyeReason_AuthRequired;
                        XLinkSession_sendSession(self, XLinkSessionCode_Goodbye,
                                                 &reason, 1);
                        XLinkSession_reset(self);
                        return;
                    }
                    else if (peerNeedsAuth) {
                        /* 对端要求认证而本端没有任何凭据：快速失败（不发
                           必错的登录请求，也不谎报 Ready）。
                           注意：客户端不宣告 Auth 时，双方能力交集仍可能含
                           Auth（服务端强制置位），因此必须按"对端要求"而非
                           "本地能力"判断；否则会出现短暂 Ready 窗口。 */
                        uint8_t reason = XLinkGoodbyeReason_AuthRequired;
                        XLinkSession_sendSession(self, XLinkSessionCode_Goodbye,
                                                 &reason, 1);
                        XLinkSession_authChanged_signal(
                            self, false, XLinkAuthStatus_NoCredential, 0);
                        XLinkSession_reset(self);
                        return;
                    }
                    else {
                        self->m_authPassed = true; /* 匿名链路：可服务 */
                        XLinkSessionPrivate_setState(self,
                                                     XLinkSessionState_Ready);
                    }
                }
#else
                self->m_authPassed = true;
                XLinkSessionPrivate_setState(self, XLinkSessionState_Ready);
#endif
            }
            break;
        case XLinkSessionCode_Heartbeat:
            break; /* 心跳已在 pump 的静默判线中体现，无需额外处理 */
        case XLinkSessionCode_Goodbye:
            XLinkSession_reset(self);
            break;
#if XLINK_AUTH_ON
        case XLinkSessionCode_AuthRequest:
        case XLinkSessionCode_AuthChallenge:
        case XLinkSessionCode_AuthResponse:
        case XLinkSessionCode_AuthResult:
            /* 登录帧全部由认证内核消化，绝不外发到业务回调。 */
            XLinkSessionPrivate_onAuthFrame(self, frame, payload);
            return;
#endif
        default:
            break;
        }
        /* 会话帧不外发到业务回调。 */
        return;

    case XLinkMsgClass_Response:
        /* 应答命中待应答表才算请求终局：出表成功发射 requestFinished(true)；
           漂移应答（无对应槽）与重复应答（槽已出表）不发射。 */
        if (XLinkSessionPrivate_removePending(self, frame->m_requestId))
            XLinkSession_requestFinished_signal(self, frame->m_requestId,
                                                frame->m_msgCode, true);
        break; /* 应答出表后再外发，便于应用层在回调里同步查询状态 */

    case XLinkMsgClass_Request:
        /* 请求不自动应答：应答可能耗时（真实设备 IO），由应用层在完成时调用
           XLinkSession_sendResponse 显式回执（允许异步应答）。 */
        break;

    case XLinkMsgClass_Subscribe:
        /* 对端订阅登记镜像：收到客户端的 Subscribe 帧即登记到本端会话的
           订阅表——XLinkServer_publish 据此把消息一对多分发给订户，
           应用层无需自己维护"哪个客户端订了什么"。 */
        if (frame->m_payloadLen >= 4) {
            uint32_t topic = XLinkCodec_getU32(payload);
            if (topic >= XLINK_TOPIC_USER_BASE
                && !XLinkSession_isSubscribed(self, topic))
                XLinkSessionPrivate_addSub(self, topic);
        }
        break;

    case XLinkMsgClass_Unsubscribe:
        if (frame->m_payloadLen >= 4)
            XLinkSessionPrivate_removeSub(self, XLinkCodec_getU32(payload));
        break;

#if XLINK_FILE_ON
    case XLinkMsgClass_File:
        /* 文件传输帧只交给挂接的传输引擎；未协商能力或未注册回调时丢弃
           （永不落到业务分发回调，避免业务层误把文件块当业务消息）。
           认证未完成（非 Ready）时同样不处理。 */
        if (self->m_state != XLinkSessionState_Ready)
            return;
        if ((self->m_remoteCapability & XLinkCapability_FileTransfer)
            && self->m_fileCallback)
            self->m_fileCallback(self, frame, payload, self->m_fileUserData);
        return;
#endif

    default:
        break;
    }

    /* 握手未完成前不投递业务消息，避免把未建立状态下的乱序帧喂给应用。 */
    if (self->m_state != XLinkSessionState_Ready)
        return;

    XLinkSession_dispatch_base(self, frame, payload);
}

/* 前向声明：consumeFrames 在协议违例/Goodbye 路径需调用 reset，
   而 reset 的定义排在其后（随功能函数段统一编排）。 */
void XLinkSession_reset(XLinkSession* self);

/**
 * @brief 队列剩余空间（字节）。
 * @note  队列为环形：满/空用 m_txUsed 区分，读写游标只在 [0,cap) 内回绕。
 */
static uint32_t XLinkSessionPrivate_txSpace(const XLinkSession* self)
{
    if (!self->m_txQueue || self->m_txQueueCap == 0)
        return 0;
    return self->m_txQueueCap - self->m_txUsed;
}

/**
 * @brief 把一段字节追加进发送队列（调用方已确认空间足够）。
 * @note  允许跨环尾分两段拷贝；不推进设备写。
 */
static void XLinkSessionPrivate_txAppend(XLinkSession* self,
                                         const uint8_t* data, uint32_t length)
{
    uint32_t first;
    if (!self->m_txQueue || length == 0)
        return;
    first = self->m_txQueueCap - self->m_txTail;
    if (first > length)
        first = length;
    memcpy(&self->m_txQueue[self->m_txTail], data, first);
    if (length > first)
        memcpy(&self->m_txQueue[0], data + first, length - first);
    self->m_txTail = (self->m_txTail + length) % self->m_txQueueCap;
    self->m_txUsed += length;
}

/**
 * @brief 把发送队列中可写部分推给设备；返回本次写出的字节数。
 * @note  短写只推进读游标——这是「不丢半帧」的关键：剩余字节留在队列里，
 *        下一轮 pump 继续写。设备返回 <=0 视为本轮不可写（背压/暂满），
 *        不视为错误，不丢队列内容。
 */
static uint32_t XLinkSessionPrivate_txFlush(XLinkSession* self)
{
    uint32_t total = 0;
    while (self->m_txUsed > 0 && self->m_device) {
        uint32_t contiguous = self->m_txQueueCap - self->m_txHead;
        int64_t written;
        if (contiguous > self->m_txUsed)
            contiguous = self->m_txUsed;
        written = XIODevice_write_1(self->m_device,
                                    (const char*)&self->m_txQueue[self->m_txHead],
                                    (int64_t)contiguous);
        if (written <= 0)
            break; /* 背压：剩余字节留待下一轮，不丢数据 */
        self->m_txHead = (self->m_txHead + (uint32_t)written) % self->m_txQueueCap;
        self->m_txUsed -= (uint32_t)written;
        total += (uint32_t)written;
    }
    if (total > 0) {
        self->m_lastSendTick = self->m_nowTick;
        self->m_stats.m_bytesSent += total;
    }
    return total;
}

/**
 * @brief 统计"当前入队帧"的写出完成情况，用于更新帧计数。
 * @note  队列以字节为单位，帧边界不单独记账；帧计数在入队时按帧累加
 *        （m_framesSent 语义为「已受理入队的帧数」，实际落地由队列推进）。
 */
static bool XLinkSessionPrivate_sendWithPrefix(XLinkSession* self,
                                               const XLinkFrameHeader* frame,
                                               const uint8_t* prefix,
                                               uint32_t prefixLen,
                                               const uint8_t* payload,
                                               uint32_t length);

/**
 * @brief 处理缓冲区内已就绪的完整帧；返回消耗的字节数。
 * @note  onFrame 可能在处理 Goodbye/协议违例时调用 reset（清空 m_rxFilled），
 *        故每帧处理后必须复查会话是否已被复位——若已复位则立即停止本轮
 *        解析并返回已消耗字节数，否则会带着过期的 available 继续访问
 *        已被 reset 逻辑作废的缓冲区（越界访问）。
 */
static uint32_t XLinkSessionPrivate_consumeFrames(XLinkSession* self,
                                                  uint32_t available)
{
    uint8_t hdrBuf[XLINK_HEADER_SIZE];
    XLinkFrameHeader frame;
    uint32_t consumed = 0;

    while (available - consumed >= XLINK_HEADER_SIZE) {
        memcpy(hdrBuf, &self->m_rxBuffer[consumed], XLINK_HEADER_SIZE);
        if (!XLinkCodec_decodeHeader(hdrBuf, XLINK_HEADER_SIZE, &frame)) {
            /* 协议违例：直接断链并作废本轮缓冲，不让畸形帧进入业务层。 */
            self->m_stats.m_protocolErrors++;
            XLinkSession_reset(self);
            return XLINK_HEADER_SIZE;
        }

        uint32_t frameLen = XLINK_HEADER_SIZE + frame.m_payloadLen;
        if (available - consumed < frameLen)
            break; /* 半帧：留待下次 pump */

        XLinkSessionPrivate_onFrame(self, &frame,
                                    frame.m_payloadLen
                                        ? &self->m_rxBuffer[consumed + XLINK_HEADER_SIZE]
                                        : NULL);
        self->m_stats.m_framesReceived++;
        consumed += frameLen;

        /* onFrame 可能已复位会话（Goodbye 等）：缓冲基准已变，必须停止。 */
        if (self->m_rxFilled == 0)
            return consumed;
    }
    return consumed;
}

/* ========================================================================== */
/*                        传输挂载                                           */
/* ========================================================================== */

bool XLinkSession_setDevice(XLinkSession* self, XIODevice* device)
{
    if (ISNULL(self, "XLinkSession_setDevice self"))
        return false;

    /* 换设备前先摘掉旧设备的 readyRead 接线（旧设备归调用方所有且此时存活），
       防止旧链路的收包事件泵到新设备上。 */
    if (self->m_device) {
        XObject_disconnect_1((XObject*)self->m_device,
                             XSignal(XIODevice_readyRead_signal),
                             (XObject*)self, VXLinkSession_onReadyRead);
    }

    XLinkSessionPrivate_stopTimer(self);
    XLinkSessionPrivate_clearTables(self);
    self->m_rxFilled = 0;
    self->m_rxExpected = 0;
    self->m_remoteVersion = 0;
    self->m_remoteCapability = 0;
    self->m_peerDeclaredCapability = 0;
    self->m_helloSent = false;
    self->m_attachTick = 0;
    self->m_authStartTick = 0;
    self->m_authRequired = false;
    self->m_authPassed = false;
    self->m_authFailures = 0;
    self->m_authRole = XLinkAuthRole_Anonymous;
    if (self->m_authUser) XContainer_clear_base((XContainer*)self->m_authUser);
    /* 换设备/断开：丢弃尚未写出的残余帧（旧链路的数据不该泄漏到新链路）。 */
    self->m_txHead = 0;
    self->m_txTail = 0;
    self->m_txUsed = 0;

    if (ISNULL(device, "XLinkSession_setDevice device")) {
        self->m_device = NULL;
        XLinkSessionPrivate_setState(self, XLinkSessionState_Inactive);
        return true;
    }

    self->m_device = device;
    self->m_attachTick = self->m_nowTick;  /* 握手超时起算（首拍 nowTick 为 0 则下一拍补记） */
    XLinkSessionPrivate_setState(self, XLinkSessionState_Connected);
    XLinkSessionPrivate_attachAsync(self, device);
    return true;
}

void XLinkSession_reset(XLinkSession* self)
{
    if (ISNULL(self, "XLinkSession_reset self"))
        return;
    /* 定时器随链路一起停：Inactive 态下节拍只会空转。重连经 setDevice 重启。 */
    XLinkSessionPrivate_stopTimer(self);
    XLinkSessionPrivate_clearTables(self);
    self->m_rxFilled = 0;
    self->m_rxExpected = 0;
    self->m_helloSent = false;
    self->m_attachTick = 0;
    self->m_authStartTick = 0;
    self->m_authRequired = false;
    self->m_authPassed = false;
    self->m_authFailures = 0;
    self->m_authRole = XLinkAuthRole_Anonymous;
    if (self->m_authUser) XContainer_clear_base((XContainer*)self->m_authUser);
    /* 残帧随链路作废：未写出的队列内容不再属于任何连接。 */
    self->m_txHead = 0;
    self->m_txTail = 0;
    self->m_txUsed = 0;
    self->m_stats.m_resets++;
    XLinkSessionPrivate_setState(self, XLinkSessionState_Inactive);
}

/* ========================================================================== */
/*                        收发                                               */
/* ========================================================================== */

void XLinkSession_pump(XLinkSession* self, uint32_t nowMs)
{
    int64_t available;
    uint32_t consumed;

    if (ISNULL(self, "XLinkSession_pump self"))
        return;
    if (!self->m_device || self->m_state == XLinkSessionState_Inactive)
        return;

    /* 记录本轮时刻：供发请求打时间戳、心跳间隔判定使用（本类不自行取时）。 */
    self->m_nowTick = nowMs;

    /* 发送队列续写：上一轮（或入队时）未写完的字节在此推进。
       短写只推进游标不丢数据——非阻塞 TCP 的背压由此自然吸收。 */
#if XLINK_TX_QUEUE_BYTES > 0
    if (self->m_txUsed > 0)
        XLinkSessionPrivate_txFlush(self);
#endif

    /* 握手超时：挂载后长时间未握手完成（半开连接）即断开回收。
       已进入 Ready/Authenticating 的链路不再受此约束（认证另有超时）。
       m_attachTick 为 0 表示尚未取到过有效时刻（首拍前），以本拍补记起算。 */
    if (self->m_handshakeTimeoutMs > 0
        && self->m_state == XLinkSessionState_Connected) {
        if (self->m_attachTick == 0)
            self->m_attachTick = nowMs;
        else if (nowMs >= self->m_attachTick
                 && (nowMs - self->m_attachTick) > self->m_handshakeTimeoutMs) {
            XLinkSession_reset(self);
            return;
        }
    }

#if XLINK_AUTH_ON
    /* 认证超时：进入 Authenticating 后长期无结果即断开（服务端资源不被悬挂）。 */
    if (self->m_authTimeoutMs > 0
        && self->m_state == XLinkSessionState_Authenticating) {
        if (self->m_authStartTick == 0)
            self->m_authStartTick = nowMs;
        else if (nowMs >= self->m_authStartTick
                 && (nowMs - self->m_authStartTick) > self->m_authTimeoutMs) {
            XLinkSessionPrivate_authFail(self, XLinkAuthStatus_Timeout, true);
            return;
        }
    }
#endif

    /* 自动握手：Connected 态首次驱动时补发 Hello（手动已发则跳过），
       异步与手动模式共用此入口——挂载传输后无需再手写握手代码。 */
    XLinkSessionPrivate_ensureHello(self);

    /* 收到任何字节即刷新静默计时。
       不以 bytesAvailable 作为读取前提：不同传输的「可读字节数」口径与
       XIODevice_read_1 的内部读缓冲层不一定一致（回环设备即如此：前者报
       底层环占用，后者走 XIODevicePrivate 读缓冲），据其判定会把可读帧
       误判为不可读。改为直接读，读到 0 即视为本轮无可读数据。 */
    {
        /* 【越界根修】读取空间必须是**剩余容量**（cap - m_rxFilled），
           而非"已填充 + 最大载荷"再截到总容量：半帧残留（m_rxFilled > 0）
           时旧公式会向 rxBuffer 之外最多写出 m_rxFilled 字节（堆损坏，
           文件传输 4KB 分块高频出现半帧时必现）。 */
        uint32_t cap = (uint32_t)(XLINK_HEADER_SIZE + XLINK_MAX_PAYLOAD);
        int64_t space = (int64_t)(cap - self->m_rxFilled);

        available = XIODevice_read_1(self->m_device,
                                     (char*)&self->m_rxBuffer[self->m_rxFilled],
                                     space);
        if (available > 0) {
            self->m_rxFilled += (uint32_t)available;
            self->m_lastReceiveTick = nowMs;
            self->m_stats.m_bytesReceived += (uint64_t)available;

            /* 抽干积压后逐帧分发：完整帧优先，半帧留待下次。
               注意：consumeFrames 内部可能因 Goodbye/协议违例调用 reset
               把 m_rxFilled 清零，此时不得再按 consumed 做搬移。 */
            consumed = XLinkSessionPrivate_consumeFrames(self, self->m_rxFilled);
            if (consumed > 0 && consumed <= self->m_rxFilled) {
                uint32_t left = self->m_rxFilled - consumed;
                if (left > 0)
                    memmove(self->m_rxBuffer, &self->m_rxBuffer[consumed], left);
                self->m_rxFilled = left;
            }
        }
    }

    /* 静默超时判线。 */
    if (XLINK_HEARTBEAT_TIMEOUT_MS > 0 && self->m_lastReceiveTick != 0
        && nowMs >= self->m_lastReceiveTick
        && (nowMs - self->m_lastReceiveTick) > XLINK_HEARTBEAT_TIMEOUT_MS) {
        XLinkSession_reset(self);
        return;
    }

    /* 请求自动超时与重试（被动模式下 m_requestTimeoutMs=0 整段跳过，
       行为与无自动超时时完全一致）。
       判定口径：严格大于阈值才算超时。扫描在收包消化之后进行——本圈
       刚收到的应答已先出表，不会被误判超时。 */
    if (self->m_requestTimeoutMs > 0) {
        int i;
        for (i = 0; i < XLINK_PENDING_SLOTS; i++) {
            XLinkPendingRequest* slot = &self->m_pending[i];
            XLinkFrameHeader frame;
            if (!slot->m_used)
                continue;
            if (slot->m_sendTick == 0) {
                /* 从未 pump 过时发出的请求以 m_nowTick=0 打戳（见 sendRequest
                   注释）：本拍起算，绝不据此判超时——否则判定式 nowMs >= 0
                   恒真，异步模式首拍真实单调毫秒远超阈值，刚发出的请求会被
                   立即重发/终局（非幂等命令被执行两次的根源）。 */
                slot->m_sendTick = nowMs;
                continue;
            }
            if (!(nowMs >= slot->m_sendTick
                  && (nowMs - slot->m_sendTick) > self->m_requestTimeoutMs))
                continue;
            if (slot->m_retryCount < self->m_retryCount) {
                /* 原 requestId 静默重发：对端按同一关联 id 应答，不换 id。 */
                frame.m_payloadLen = slot->m_payloadLen;
                frame.m_msgClass = XLinkMsgClass_Request;
                frame.m_msgCode = slot->m_command;
                frame.m_requestId = slot->m_requestId;
                if (XLinkSession_send(self, &frame,
                                      slot->m_payloadLen ? slot->m_payload
                                                         : NULL)) {
                    slot->m_sendTick = nowMs; /* 重发起算，递增重试计数 */
                    slot->m_retryCount++;
                    continue;
                }
                /* 重发失败（链路已坏）：立即终局，不逐圈空转重试。 */
            }
            /* 额度用尽 / 不可重试 / 重发失败：终局上报并出表。
               先出表再发射——槽内回调若重入 sendRequest 可安全复用本槽。 */
            slot->m_used = false;
            XLinkSession_requestFinished_signal(self, slot->m_requestId,
                                                slot->m_command, false);
        }
    }

    /* 主动心跳：握手完成后按间隔发 Heartbeat，供对端刷新其静默计时。 */
    if (XLINK_HEARTBEAT_INTERVAL_MS > 0
        && self->m_state == XLinkSessionState_Ready) {
        uint32_t base = self->m_lastSendTick ? self->m_lastSendTick
                                            : self->m_lastReceiveTick;
        if (base == 0 || (nowMs >= base
            && (nowMs - base) >= XLINK_HEARTBEAT_INTERVAL_MS)) {
            XLinkSession_sendSession(self, XLinkSessionCode_Heartbeat, NULL, 0);
        }
    }
}

bool XLinkSession_send(XLinkSession* self, const XLinkFrameHeader* frame,
                       const uint8_t* payload)
{
    if (ISNULL(self, "XLinkSession_send self")
        || ISNULL(frame, "XLinkSession_send frame"))
        return false;
    if (!self->m_device || self->m_state == XLinkSessionState_Inactive)
        return false;
    if (frame->m_payloadLen > XLINK_MAX_PAYLOAD)
        return false;
    if (frame->m_payloadLen > 0 && ISNULL(payload, "XLinkSession_send payload"))
        return false;

    return XLinkSessionPrivate_sendWithPrefix(self, frame, NULL, 0,
                                              payload, frame->m_payloadLen);
}

static bool XLinkSessionPrivate_sendWithPrefix(XLinkSession* self,
                                               const XLinkFrameHeader* frame,
                                               const uint8_t* prefix,
                                               uint32_t prefixLen,
                                               const uint8_t* payload,
                                               uint32_t length)
{
    uint8_t hdrBuf[XLINK_HEADER_SIZE];
    uint8_t stackBuf[256];
    uint8_t* buf;
    int64_t total;
    int64_t written;

    if (ISNULL(self, "XLinkSessionPrivate_sendWithPrefix self")
        || ISNULL(frame, "XLinkSessionPrivate_sendWithPrefix frame"))
        return false;
    if (!self->m_device || self->m_state == XLinkSessionState_Inactive)
        return false;
    if (length > XLINK_MAX_PAYLOAD || prefixLen > XLINK_MAX_PAYLOAD)
        return false;
    if (length > 0 && ISNULL(payload, "XLinkSessionPrivate_sendWithPrefix payload"))
        return false;

    total = (int64_t)XLINK_HEADER_SIZE + prefixLen + length;
    if (!XLinkCodec_encodeHeader(frame, hdrBuf))
        return false;

#if XLINK_TX_QUEUE_BYTES > 0
    /* 有发送队列：整帧原子入队（空间不足则整帧拒绝，绝不半帧入队），
       随后尽力立即推进；短写只推进游标，剩余字节由后续 pump 续写。
       这使非阻塞 TCP 的短写不再丢帧，也让大块文件数据可以安全排队。 */
    if (self->m_txQueue) {
        if (XLinkSessionPrivate_txSpace(self) < (uint32_t)total)
            return false; /* 队列满：背压，调用方稍后重试 */
        XLinkSessionPrivate_txAppend(self, hdrBuf, XLINK_HEADER_SIZE);
        if (prefixLen > 0)
            XLinkSessionPrivate_txAppend(self, prefix, prefixLen);
        if (length > 0)
            XLinkSessionPrivate_txAppend(self, payload, length);
        self->m_stats.m_framesSent++;
        XLinkSessionPrivate_txFlush(self);
        return true;
    }
#endif

    /* 无发送队列（XLINK_TX_QUEUE_BYTES=0 或申请失败）：退回单次原子写。
       缓冲优先用会话常驻发送缓冲（与接收缓冲对称），稳态零堆分配；
       仅当常驻缓冲不可用且帧超大时才退回一次性堆申请。 */
    if (self->m_txBuffer) {
        buf = self->m_txBuffer;
    } else if (total <= (int64_t)sizeof(stackBuf)) {
        buf = stackBuf;
    } else {
        buf = (uint8_t*)XMalloc_System((size_t)total);
        if (!buf)
            return false;
    }

    memcpy(buf, hdrBuf, XLINK_HEADER_SIZE);
    if (prefixLen > 0)
        memcpy(&buf[XLINK_HEADER_SIZE], prefix, prefixLen);
    if (length > 0)
        memcpy(&buf[XLINK_HEADER_SIZE + prefixLen], payload, length);

    written = XIODevice_write_1(self->m_device, (const char*)buf, total);
    if (buf != stackBuf && buf != self->m_txBuffer)
        XFree_System(buf);

    if (written != total)
        return false;
    self->m_lastSendTick = self->m_nowTick;
    self->m_stats.m_bytesSent += (uint64_t)total;
    self->m_stats.m_framesSent++;
    return true;
}

bool XLinkSession_sendSession(XLinkSession* self, XLinkSessionCode code,
                              const uint8_t* payload, uint32_t length)
{
    XLinkFrameHeader frame;
    bool ok;
    frame.m_payloadLen = length;
    frame.m_msgClass = XLinkMsgClass_Session;
    frame.m_msgCode = (uint16_t)code;
    frame.m_requestId = 0;
    ok = XLinkSession_send(self, &frame, payload);
    if (ok && code == XLinkSessionCode_Hello)
        self->m_helloSent = true; /* 手动握手与自动握手互斥，防重复 Hello */
    return ok;
}

bool XLinkSession_sendRequest(XLinkSession* self, uint16_t command,
                              const uint8_t* payload, uint32_t length,
                              uint32_t* outRequestId)
{
    XLinkFrameHeader frame;
    uint32_t id;

    if (ISNULL(self, "XLinkSession_sendRequest self"))
        return false;

    id = XLinkSessionPrivate_nextRequestId(self);
    /* 发送时间戳取自最近一次 pump 传入的 nowTick（本类不自行取时，见头文件时间口径），
       未 pump 过时 m_nowTick 为 0，超时判定退化为"下次 pump 起算"，不误报。
       同时快照载荷进槽（不超过 XLINK_RETRY_PAYLOAD_MAX 才可自动重发）。 */
    if (!XLinkSessionPrivate_addPending(self, id, command, self->m_nowTick,
                                        payload, length))
        return false; /* 表满：拒绝发请求，避免对端应答无槽可落 */

    frame.m_payloadLen = length;
    frame.m_msgClass = XLinkMsgClass_Request;
    frame.m_msgCode = command;
    frame.m_requestId = id;
    if (!XLinkSession_send(self, &frame, payload)) {
        XLinkSessionPrivate_removePending(self, id);
        return false;
    }
    if (outRequestId)
        *outRequestId = id;
    return true;
}

bool XLinkSession_sendResponse(XLinkSession* self, uint32_t requestId,
                               uint16_t command, const uint8_t* payload,
                               uint32_t length)
{
    XLinkFrameHeader frame;
    frame.m_payloadLen = length;
    frame.m_msgClass = XLinkMsgClass_Response;
    frame.m_msgCode = command;
    frame.m_requestId = requestId;
    return XLinkSession_send(self, &frame, payload);
}

bool XLinkSession_sendPublish(XLinkSession* self, uint32_t topicId,
                              const uint8_t* payload, uint32_t length)
{
    XLinkFrameHeader frame;
    uint8_t topicBuf[4];

    if (ISNULL(self, "XLinkSession_sendPublish self"))
        return false;
    if (topicId < XLINK_TOPIC_USER_BASE)
        return false;

    /* topicId 完整 u32 置于载荷首 4 字节（msgCode 只承载低 16 位，见 XLinkProto.h
       topicId 承载规则）：前缀与业务载荷由 sendWithPrefix 一次拼入常驻发送
       缓冲后单次原子写出——既不拆帧，也不多一次整帧拷贝。 */
    XLinkCodec_putU32(&topicBuf[0], topicId);

    frame.m_msgClass = XLinkMsgClass_Publish;
    frame.m_msgCode = (uint16_t)(topicId & 0xFFFFu);
    frame.m_requestId = 0;

    if (length == 0 || ISNULL(payload, "XLinkSession_sendPublish payload"))
        length = 0;

    /* XLINK_MAX_PAYLOAD 约束的是「线上载荷总量」（含 topicId 前缀）：
       业务载荷可用上限为 XLINK_MAX_PAYLOAD - 4（u32 topicId）。 */
    if (length > XLINK_MAX_PAYLOAD - 4)
        return false;

    frame.m_payloadLen = 4 + length;
    return XLinkSessionPrivate_sendWithPrefix(self, &frame, topicBuf, 4,
                                              payload, length);
}

/* ========================================================================== */
/*                        订阅                                               */
/* ========================================================================== */

bool XLinkSession_subscribe(XLinkSession* self, uint32_t topicId)
{
    XLinkFrameHeader frame;
    uint8_t topicBuf[4];
    if (ISNULL(self, "XLinkSession_subscribe self"))
        return false;
    if (topicId < XLINK_TOPIC_USER_BASE)
        return false;
    /* 幂等：重复订阅等价一次——不重复占定容表槽位、不向对端重发帧。 */
    if (XLinkSession_isSubscribed(self, topicId))
        return true;
    if (!XLinkSessionPrivate_addSub(self, topicId))
        return false; /* 表满 */

    /* 完整 u32 topicId 走载荷首 4 字节（见 XLinkProto.h topicId 承载规则）。 */
    XLinkCodec_putU32(&topicBuf[0], topicId);
    frame.m_payloadLen = 4;
    frame.m_msgClass = XLinkMsgClass_Subscribe;
    frame.m_msgCode = (uint16_t)(topicId & 0xFFFFu);
    frame.m_requestId = 0;
    if (!XLinkSessionPrivate_sendWithPrefix(self, &frame, topicBuf, 4,
                                            NULL, 0)) {
        XLinkSessionPrivate_removeSub(self, topicId);
        return false;
    }
    return true;
}

bool XLinkSession_unsubscribe(XLinkSession* self, uint32_t topicId)
{
    XLinkFrameHeader frame;
    uint8_t topicBuf[4];
    if (ISNULL(self, "XLinkSession_unsubscribe self"))
        return false;
    if (topicId < XLINK_TOPIC_USER_BASE)
        return false;
    if (!XLinkSessionPrivate_removeSub(self, topicId))
        return false; /* 未订阅：不发无意义帧 */

    XLinkCodec_putU32(&topicBuf[0], topicId);
    frame.m_payloadLen = 4;
    frame.m_msgClass = XLinkMsgClass_Unsubscribe;
    frame.m_msgCode = (uint16_t)(topicId & 0xFFFFu);
    frame.m_requestId = 0;
    return XLinkSessionPrivate_sendWithPrefix(self, &frame, topicBuf, 4,
                                              NULL, 0);
}

bool XLinkSession_isSubscribed(const XLinkSession* self, uint32_t topicId)
{
    int i;
    if (ISNULL(self, "XLinkSession_isSubscribed self"))
        return false;
    for (i = 0; i < XLINK_TOPIC_SLOTS; i++) {
        if ((self->m_subscriptionUsed & (1u << (unsigned)i))
            && self->m_subscriptions[i].m_topicId == topicId)
            return true;
    }
    return false;
}

/* ========================================================================== */
/*                        待应答查询                                         */
/* ========================================================================== */

bool XLinkSession_isPending(const XLinkSession* self, uint32_t requestId)
{
    int i;
    if (ISNULL(self, "XLinkSession_isPending self"))
        return false;
    for (i = 0; i < XLINK_PENDING_SLOTS; i++) {
        if (self->m_pending[i].m_used && self->m_pending[i].m_requestId == requestId)
            return true;
    }
    return false;
}

/* ========================================================================== */
/*                        属性                                               */
/* ========================================================================== */

bool XLinkSession_setDispatchCallback(XLinkSession* self,
                                      XLinkSessionDispatchCallback callback,
                                      void* userData)
{
    if (ISNULL(self, "XLinkSession_setDispatchCallback self"))
        return false;
    self->m_dispatchCallback = callback;
    self->m_userData = userData;
    return true;
}

bool XLinkSession_setCapability(XLinkSession* self, uint16_t capability)
{
    if (ISNULL(self, "XLinkSession_setCapability self"))
        return false;
    self->m_localCapability = capability;
    return true;
}

uint16_t XLinkSession_negotiatedCapability(const XLinkSession* self)
{
    if (ISNULL(self, "XLinkSession_negotiatedCapability self"))
        return 0;
    return self->m_remoteCapability;
}

uint16_t XLinkSession_remoteVersion(const XLinkSession* self)
{
    if (ISNULL(self, "XLinkSession_remoteVersion self"))
        return 0;
    return self->m_remoteVersion;
}

XLinkSessionState XLinkSession_state(const XLinkSession* self)
{
    if (ISNULL(self, "XLinkSession_state self"))
        return XLinkSessionState_Inactive;
    return self->m_state;
}

bool XLinkSession_isReady(const XLinkSession* self)
{
    if (ISNULL(self, "XLinkSession_isReady self"))
        return false;
    return self->m_state == XLinkSessionState_Ready;
}

/* ========================================================================== */
/*                  请求自动超时与重试（自动模式配置）                         */
/* ========================================================================== */

bool XLinkSession_setNumberOfRetries(XLinkSession* self, uint8_t count)
{
    if (ISNULL(self, "XLinkSession_setNumberOfRetries self"))
        return false;
    self->m_retryCount = count;
    return true;
}

uint8_t XLinkSession_numberOfRetries(const XLinkSession* self)
{
    if (ISNULL(self, "XLinkSession_numberOfRetries self"))
        return 0;
    return self->m_retryCount;
}

bool XLinkSession_setRequestTimeout(XLinkSession* self, uint32_t ms)
{
    if (ISNULL(self, "XLinkSession_setRequestTimeout self"))
        return false;
    self->m_requestTimeoutMs = ms;
    return true;
}

uint32_t XLinkSession_requestTimeout(const XLinkSession* self)
{
    if (ISNULL(self, "XLinkSession_requestTimeout self"))
        return 0;
    return self->m_requestTimeoutMs;
}

/* ========================================================================== */
/*                        发送队列与统计                                     */
/* ========================================================================== */

uint32_t XLinkSession_pendingBytes(const XLinkSession* self)
{
    if (ISNULL(self, "XLinkSession_pendingBytes self"))
        return 0;
    return self->m_txUsed;
}

bool XLinkSession_stats(const XLinkSession* self, XLinkSessionStats* out)
{
    if (ISNULL(self, "XLinkSession_stats self") || !out)
        return false;
    *out = self->m_stats;
    return true;
}

bool XLinkSession_keepAlive(XLinkSession* self, uint32_t nowMs)
{
    if (ISNULL(self, "XLinkSession_keepAlive self"))
        return false;
    if (!self->m_device || self->m_state == XLinkSessionState_Inactive)
        return false;
    if (nowMs == 0)
        nowMs = XLinkSessionPrivate_nowMs();
    self->m_lastReceiveTick = nowMs;
    self->m_lastSendTick = nowMs;
    self->m_nowTick = nowMs;
    return true;
}

bool XLinkSession_setHandshakeTimeout(XLinkSession* self, uint32_t ms)
{
    if (ISNULL(self, "XLinkSession_setHandshakeTimeout self"))
        return false;
    self->m_handshakeTimeoutMs = ms;
    return true;
}

uint32_t XLinkSession_handshakeTimeout(const XLinkSession* self)
{
    if (ISNULL(self, "XLinkSession_handshakeTimeout self"))
        return 0;
    return self->m_handshakeTimeoutMs;
}

/* ========================================================================== */
/*                        登录认证公开 API                                   */
/* ========================================================================== */

#if XLINK_AUTH_ON

bool XLinkSession_setAuthLookup(XLinkSession* self,
                                XLinkSessionAuthLookupCallback callback,
                                void* userData)
{
    if (ISNULL(self, "XLinkSession_setAuthLookup self"))
        return false;
    self->m_authLookup = callback;
    self->m_authLookupUserData = userData;
    /* 设置查表回调即"本端要求认证"：握手后进入 Authenticating。
       同时声明 Auth 能力位（否则对端看不到本位，交集里永远没有它）。 */
    self->m_authRequired = (callback != NULL);
    if (self->m_authRequired)
        self->m_localCapability |= XLinkCapability_Auth;
    return true;
}

bool XLinkSession_setAuthResponse(XLinkSession* self,
                                  XLinkSessionAuthResponseCallback callback,
                                  void* userData)
{
    if (ISNULL(self, "XLinkSession_setAuthResponse self"))
        return false;
    self->m_authRespond = callback;
    self->m_authRespondUserData = userData;
    return true;
}

bool XLinkSession_setAuthAllowAnonymous(XLinkSession* self, bool allow)
{
    if (ISNULL(self, "XLinkSession_setAuthAllowAnonymous self"))
        return false;
    self->m_authAllowAnonymous = allow;
    return true;
}

bool XLinkSession_setAuthUser(XLinkSession* self, const char* user)
{
    if (ISNULL(self, "XLinkSession_setAuthUser self") || !user)
        return false;
    if (!self->m_authPendingUser)
        return false;
    return XString_assign_utf8(self->m_authPendingUser, user);
}

bool XLinkSession_isAuthenticated(const XLinkSession* self)
{
    if (ISNULL(self, "XLinkSession_isAuthenticated self"))
        return false;
    return self->m_authPassed;
}

const char* XLinkSession_authUser_const(const XLinkSession* self)
{
    if (ISNULL(self, "XLinkSession_authUser_const self"))
        return NULL;
    if (!self->m_authPassed || !self->m_authUser
        || XString_isEmpty_base(self->m_authUser))
        return NULL;
    return XString_toUtf8(self->m_authUser);
}

uint8_t XLinkSession_authRole(const XLinkSession* self)
{
    if (ISNULL(self, "XLinkSession_authRole self"))
        return XLinkAuthRole_Anonymous;
    return self->m_authRole;
}

bool XLinkSession_setAuthTimeout(XLinkSession* self, uint32_t ms)
{
    if (ISNULL(self, "XLinkSession_setAuthTimeout self"))
        return false;
    self->m_authTimeoutMs = ms;
    return true;
}

bool XLinkSession_setAuthFailureLimit(XLinkSession* self, uint8_t limit)
{
    if (ISNULL(self, "XLinkSession_setAuthFailureLimit self"))
        return false;
    self->m_authFailureLimit = limit ? limit : 1;
    return true;
}

void* XLinkSession_authChallenge_signal(XLinkSession* self, const char* user,
                                        int result)
{
    XEmitSignal(self, XLinkSession_authChallenge_signal,
        XVarList_Create(XVar(const char*, user), XVar(int, result)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XLinkSession_authChanged_signal(XLinkSession* self, bool passed,
                                      uint8_t status, uint8_t role)
{
    XEmitSignal(self, XLinkSession_authChanged_signal,
        XVarList_Create(XVar(bool, passed), XVar(uint8_t, status),
                        XVar(uint8_t, role)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XLINK_AUTH_ON */

/* ========================================================================== */
/*                        文件传输钩子                                       */
/* ========================================================================== */

#if XLINK_FILE_ON

bool XLinkSession_setFileCallback(XLinkSession* self,
                                  XLinkSessionFileCallback callback,
                                  void* userData)
{
    if (ISNULL(self, "XLinkSession_setFileCallback self"))
        return false;
    self->m_fileCallback = callback;
    self->m_fileUserData = userData;
    return true;
}

#endif /* XLINK_FILE_ON */

/* ========================================================================== */
/*                        信号                                               */
/* ========================================================================== */
/* 发射写法对齐 XModbusClient：XEmitSignal 宏 + XVarList_Create(XVar(...)) +
   XEVENT_PRIORITY_NORMAL；信号标识 = 信号函数自身地址（见 XObject.h XSignal）。 */

void* XLinkSession_stateChanged_signal(XLinkSession* self,
                                       XLinkSessionState state)
{
    XEmitSignal(self, XLinkSession_stateChanged_signal,
        XVarList_Create(XVar(XLinkSessionState, state)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XLinkSession_requestFinished_signal(XLinkSession* self, uint32_t requestId,
                                          uint16_t command, bool succeeded)
{
    XEmitSignal(self, XLinkSession_requestFinished_signal,
        XVarList_Create(XVar(uint32_t, requestId), XVar(uint16_t, command),
                        XVar(bool, succeeded)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XLINK_ON && XLINK_SESSION_ON */
