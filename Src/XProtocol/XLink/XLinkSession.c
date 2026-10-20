/** @file       XLinkSession.c
 * @brief      XLink 私有协议会话对象实现（收发/分发/订阅/待应答/心跳）。
 * @details    实现只依赖 XinYueC 抽象层（XIODevice/XMemory/XClass），
 *             禁止调用 Win32、POSIX 或其他平台 API；时间由调用方以单调毫秒传入
 *             （见 XLinkSession.h 文件头时间口径）。
 * @author     XinYueC 团队
 */
#include "XLinkSession.h"

#if XLINK_ON && XLINK_SESSION_ON

#include "XLinkCodec.h"
#include "XMemory.h"
#include <string.h>

/* ========================================================================== */
/*                        静态虚函数实现                                     */
/* ========================================================================== */

/**
 * @brief 反初始化：释放接收缓冲并清空可再生的运行时状态。
 * @param  object 基类指针（虚表槽统一签名），内部回转为 XLinkSession。
 */
static void VXLinkSession_deinit(XClass* object)
{
    XLinkSession* obj;
    if (!object)
        return;
    obj = (XLinkSession*)object;
    if (obj->m_rxBuffer) {
        XFree_System(obj->m_rxBuffer);
        obj->m_rxBuffer = NULL;
    }
    if (obj->m_txBuffer) {
        XFree_System(obj->m_txBuffer);
        obj->m_txBuffer = NULL;
    }
    obj->m_device = NULL;
    obj->m_rxFilled = 0;
    obj->m_rxExpected = 0;
    obj->m_dispatchCallback = NULL;
    obj->m_userData = NULL;
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

/* ========================================================================== */
/*                        虚函数表初始化                                     */
/* ========================================================================== */

XVtable* XLinkSession_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLinkSession)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLinkSession_deinit);
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
    XClass_init(obj);
    XClassSetVtable(obj, XLinkSession);
    /* 收发缓冲全生命周期一次性申请，稳态收发零堆分配。 */
    obj->m_rxBuffer = (uint8_t*)XMalloc_System(
        (size_t)(XLINK_HEADER_SIZE + XLINK_MAX_PAYLOAD));
    obj->m_txBuffer = (uint8_t*)XMalloc_System(
        (size_t)(XLINK_HEADER_SIZE + XLINK_MAX_PAYLOAD));
    obj->m_device = NULL;
    obj->m_state = XLinkSessionState_Inactive;
    obj->m_nextRequestId = 1; /* 0 保留为"无效 id" */
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

/** @brief 登记一条待应答；表满返回 false。 */
static bool XLinkSessionPrivate_addPending(XLinkSession* self, uint32_t id,
                                           uint16_t command, uint32_t nowMs)
{
    int i;
    for (i = 0; i < XLINK_PENDING_SLOTS; i++) {
        if (!self->m_pending[i].m_used) {
            self->m_pending[i].m_used = true;
            self->m_pending[i].m_requestId = id;
            self->m_pending[i].m_command = command;
            self->m_pending[i].m_sendTick = nowMs;
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
                self->m_state = XLinkSessionState_Ready;
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
        XLinkSessionPrivate_removePending(self, frame->m_requestId);
        break; /* 应答出表后再外发，便于应用层在回调里同步查询状态 */

    case XLinkMsgClass_Request:
        /* 请求不自动应答：应答可能耗时（真实设备 IO），由应用层在完成时调用
           XLinkSession_sendResponse 显式回执（允许异步应答）。 */
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

    XLinkSessionPrivate_clearTables(self);
    self->m_rxFilled = 0;
    self->m_rxExpected = 0;
    self->m_remoteVersion = 0;
    self->m_remoteCapability = 0;

    if (ISNULL(device, "XLinkSession_setDevice device")) {
        self->m_device = NULL;
        self->m_state = XLinkSessionState_Inactive;
        return true;
    }

    self->m_device = device;
    self->m_state = XLinkSessionState_Connected;
    return true;
}

void XLinkSession_reset(XLinkSession* self)
{
    if (ISNULL(self, "XLinkSession_reset self"))
        return;
    XLinkSessionPrivate_clearTables(self);
    self->m_rxFilled = 0;
    self->m_rxExpected = 0;
    self->m_state = XLinkSessionState_Inactive;
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
    frame.m_payloadLen = length;
    frame.m_msgClass = XLinkMsgClass_Session;
    frame.m_msgCode = (uint16_t)code;
    frame.m_requestId = 0;
    return XLinkSession_send(self, &frame, payload);
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
       未 pump 过时 m_nowTick 为 0，超时判定退化为"下次 pump 起算"，不误报。 */
    if (!XLinkSessionPrivate_addPending(self, id, command, self->m_nowTick))
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

bool XLinkSession_takeTimedOut(XLinkSession* self, uint32_t nowMs,
                               uint32_t timeoutMs, uint16_t* outCommand)
{
    int i;
    if (ISNULL(self, "XLinkSession_takeTimedOut self"))
        return false;
    for (i = 0; i < XLINK_PENDING_SLOTS; i++) {
        if (self->m_pending[i].m_used
            && nowMs >= self->m_pending[i].m_sendTick
            && (nowMs - self->m_pending[i].m_sendTick) > timeoutMs) {
            if (outCommand)
                *outCommand = self->m_pending[i].m_command;
            self->m_pending[i].m_used = false; /* 出表，不重复上报 */
            return true;
        }
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

#endif /* XLINK_ON && XLINK_SESSION_ON */
