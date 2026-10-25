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
#include "XMemory.h"
#include "XDateTime.h"
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
    obj->m_rxFilled = 0;
    obj->m_rxExpected = 0;
    obj->m_dispatchCallback = NULL;
    obj->m_userData = NULL;
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
    obj->m_device = NULL;
    obj->m_timerId = XTIMER_INVALID_ID;
    obj->m_helloSent = false;
    obj->m_state = XLinkSessionState_Inactive;
    obj->m_nextRequestId = 1; /* 0 保留为"无效 id" */
    /* 请求自动超时/重试默认值：被动模式（0/0），行为与无自动重试时一致。 */
    obj->m_retryCount = XLINK_RETRY_MAX_DEFAULT;
    obj->m_requestTimeoutMs = XLINK_REQUEST_TIMEOUT_MS;
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
        if (!self->m_subscriptions[i].m_used) {
            self->m_subscriptions[i].m_used = true;
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
        if (self->m_subscriptions[i].m_used
            && self->m_subscriptions[i].m_topicId == topicId) {
            self->m_subscriptions[i].m_used = false;
            return true;
        }
    }
    return false;
}

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
                self->m_remoteCapability = (uint16_t)(
                    hello.m_capability & self->m_localCapability);
                XLinkSessionPrivate_setState(self, XLinkSessionState_Ready);
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
            }
            break;
        case XLinkSessionCode_Heartbeat:
            break; /* 心跳已在 pump 的静默判线中体现，无需额外处理 */
        case XLinkSessionCode_Goodbye:
            XLinkSession_reset(self);
            break;
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
 * @brief 带载荷前缀的单次原子写（内部复用）。
 * @param prefix      载荷前缀（topicId 的 4 字节小端），可为 NULL。
 * @param prefixLen   前缀长度（字节）。
 * @details Publish/Subscribe 类需在线上载荷首部补 4 字节 topicId，若先拼好
 *          再交给 send 会多一次整帧拷贝（热路径双重开销）。本函数把前缀与
 *          业务载荷直接写进常驻发送缓冲后一次写出，既保原子性又只拷一次。
 * @note  前置条件与 XLinkSession_send 相同。
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
    self->m_helloSent = false;

    if (ISNULL(device, "XLinkSession_setDevice device")) {
        self->m_device = NULL;
        XLinkSessionPrivate_setState(self, XLinkSessionState_Inactive);
        return true;
    }

    self->m_device = device;
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

    /* 自动握手：Connected 态首次驱动时补发 Hello（手动已发则跳过），
       异步与手动模式共用此入口——挂载传输后无需再手写握手代码。 */
    XLinkSessionPrivate_ensureHello(self);

    /* 收到任何字节即刷新静默计时。
       不以 bytesAvailable 作为读取前提：不同传输的「可读字节数」口径与
       XIODevice_read_1 的内部读缓冲层不一定一致（回环设备即如此：前者报
       底层环占用，后者走 XIODevicePrivate 读缓冲），据其判定会把可读帧
       误判为不可读。改为直接读，读到 0 即视为本轮无可读数据。 */
    {
        int64_t space = (int64_t)self->m_rxFilled + XLINK_MAX_PAYLOAD;
        uint32_t cap = (uint32_t)(XLINK_HEADER_SIZE + XLINK_MAX_PAYLOAD);
        if (space > cap)
            space = cap;

        available = XIODevice_read_1(self->m_device,
                                     (char*)&self->m_rxBuffer[self->m_rxFilled],
                                     space);
        if (available > 0) {
            self->m_rxFilled += (uint32_t)available;
            self->m_lastReceiveTick = nowMs;

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
    /* 常驻发送缓冲容量 = 帧头 + 最大载荷；带前缀时须确保拼接不越界。 */
    if (self->m_txBuffer
        && total > (int64_t)(XLINK_HEADER_SIZE + XLINK_MAX_PAYLOAD))
        return false;

    /* 单次原子写：帧头与载荷必须一次写出——部分传输（如 XGuiRemoteLoopback）
       的写入是「全有全无」，分两段写会只落帧头、对端拿到截断帧流。
       缓冲优先用会话常驻发送缓冲（与接收缓冲对称，init 时一次性申请），
       稳态零堆分配；仅当常驻缓冲不可用且帧超大时才退回一次性堆申请。 */
    if (self->m_txBuffer) {
        buf = self->m_txBuffer;
    } else if (total <= (int64_t)sizeof(stackBuf)) {
        buf = stackBuf;
    } else {
        buf = (uint8_t*)XMalloc_System((size_t)total);
        if (!buf)
            return false;
    }

    if (!XLinkCodec_encodeHeader(frame, hdrBuf)) {
        if (buf != stackBuf && buf != self->m_txBuffer)
            XFree_System(buf);
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
        if (self->m_subscriptions[i].m_used
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
