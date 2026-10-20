/** @file       XNtpClient.c
 * @brief      XNtp 客户端实现（SNTP 口径异步校时：请求-应答-偏移计算）。
 * @details    事件驱动：syncNow 发出 48 字节请求后立即返回，应答经
 *             readyRead 信号进入校验与 offset/delay 计算；超时经 XObject
 *             定时器触发重试或判失败。时间口径：协议时间戳（T1/T4）用
 *             XDateTime_currentWallMSecsSinceEpoch（真实 UTC + 亚秒精度），
 *             定时器由 XObject 框架驱动（单调钟，校时免疫）。
 * @author     XinYueC 团队
 */
#include "XNtpClient.h"

#if XNTP_ON && XNTP_CLIENT_ON

#include "XNtpClient_Protected.h"
#include "XNtpPacket.h"
#include "XUdpSocket.h"
#include "XHostInfo.h"
#include "XHostAddress.h"
#include "XDateTime.h"
#include "XVarList.h"
#include "XMemory.h"
#include <string.h>

/* ========================================================================== */
/*                        静态虚函数前置声明                                 */
/* ========================================================================== */

static void VXNtpClient_deinit(XClass* object);
static void VXNtpClient_timerEvent(XObject* obj, XTimerEvent* event);

/* ========================================================================== */
/*                        内部辅助前置声明                                   */
/* ========================================================================== */

static void xntp_onReadyRead(XObject* receiver, XVarList* args);
static bool xntp_resolveServer(XNtpClient* self);
static bool xntp_sendRequest(XNtpClientPrivate* d);
static void xntp_armTimeout(XNtpClientPrivate* d, XNtpClient* self);
static void xntp_disarmTimeout(XNtpClientPrivate* d, XNtpClient* self);
static void xntp_fail(XNtpClient* self, XNtpClientError error);

/* ========================================================================== */
/*                        静态虚函数实现                                     */
/* ========================================================================== */

static void VXNtpClient_deinit(XClass* object)
{
    XNtpClient* self;
    XNtpClientPrivate* d;
    if (!object)
        return;
    self = (XNtpClient*)object;
    d = self->m_d;
    if (d) {
        if (d->m_timeoutTimer) {
            XObject_killTimer((XObject*)self, d->m_timeoutTimer);
            d->m_timeoutTimer = 0;
        }
        if (d->m_syncTimer) {
            XObject_killTimer((XObject*)self, d->m_syncTimer);
            d->m_syncTimer = 0;
        }
        if (d->m_socket) {
            XUdpSocket_abort(d->m_socket);
            XClassDelete((XClass*)d->m_socket);
            d->m_socket = NULL;
        }
        if (d->m_serverName) {
            XClassDelete(d->m_serverName);
            d->m_serverName = NULL;
        }
        XClassDeinit(&d->m_serverAddr);
        XFree_System(d);
        self->m_d = NULL;
    }
}

/**
 * @brief 定时器事件分派：超时定时器驱动重试/判失败，同步定时器驱动周期校时。
 */
static void VXNtpClient_timerEvent(XObject* obj, XTimerEvent* event)
{
    XNtpClient* self;
    XNtpClientPrivate* d;
    XTimerId timerId;

    if (!obj || !event)
        return;
    XEvent_accept(event);
    self = (XNtpClient*)obj;
    d = self->m_d;
    if (!d)
        goto parent_call;

    timerId = XTimerEvent_timerId(event);
    if (timerId == d->m_timeoutTimer) {
        /* 应答超时：还有重试额度则重发（新 T1），否则判失败。 */
        xntp_disarmTimeout(d, self);
        if (d->m_retriesLeft > 0 && xntp_sendRequest(d)) {
            d->m_retriesLeft--;
            xntp_armTimeout(d, self);
        } else {
            xntp_fail(self, XNtpClientError_Timeout);
        }
        return;
    }
    if (timerId == d->m_syncTimer) {
        XNtpClient_syncNow(self);
        return;
    }

parent_call:
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))((XObject*)self, event);
}

/* ========================================================================== */
/*                        虚函数表初始化                                     */
/* ========================================================================== */

XVtable* XNtpClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XNtpClient)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNtpClient_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXNtpClient_timerEvent);
    return XVTABLE_DEFAULT;
}

/* ========================================================================== */
/*                        构造                                               */
/* ========================================================================== */

void XNtpClient_init(XNtpClient* obj)
{
    if (!obj)
        return;
    memset(obj, 0, sizeof(XNtpClient));
    XObject_init((XObject*)obj);
    XClassSetVtable(obj, XNtpClient);
    obj->m_d = (XNtpClientPrivate*)XCalloc_System(1, sizeof(XNtpClientPrivate));
    if (obj->m_d) {
        XNtpClientPrivate* d = obj->m_d;
        XHostAddress_init(&d->m_serverAddr);
        d->m_serverPort = (uint16_t)XNTP_DEFAULT_PORT;
        d->m_timeoutMs = XNTP_DEFAULT_TIMEOUT_MS;
        d->m_syncIntervalMs = XNTP_DEFAULT_SYNC_INTERVAL_MS;
        d->m_maxRetries = XNTP_DEFAULT_RETRIES;
        d->m_state = (uint32_t)XNtpClientState_Idle;
    }
}

XNtpClient* XNtpClient_create(void)
{
    XMemoryType memory = XCLASS_DEFAULT_MEMORY_TYPE;
    XNtpClient* obj = (XNtpClient*)XMemory_malloc(sizeof(XNtpClient), memory);
    if (!obj)
        return NULL;
    XNtpClient_init(obj);
    Set_Class_Memory(obj, memory);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ========================================================================== */
/*                        内部辅助                                           */
/* ========================================================================== */

XNtpClientPrivate* XNtpClient_private(const XNtpClient* client)
{
    return client ? client->m_d : NULL;
}

/**
 * @brief 解析服务器地址（阻塞式 DNS，setServer 后首次 sync 时执行一次）。
 */
static bool xntp_resolveServer(XNtpClient* self)
{
    XNtpClientPrivate* d = self ? self->m_d : NULL;
    XHostInfo* info;
    const XVector* addresses;

    if (!d)
        return false;
    if (d->m_addrResolved)
        return true;
    if (!d->m_serverName)
        return false;

    info = XHostInfo_fromName(d->m_serverName);
    if (!info)
        return false;
    if (XHostInfo_error(info) != XHostInfo_NoError) {
        XClassDelete(info);
        return false;
    }
    addresses = XHostInfo_addresses_const(info);
    if (!addresses || XVector_size_base(addresses) == 0) {
        XClassDelete(info);
        return false;
    }
    {
        XVector_iterator it = XVector_begin((XVector*)addresses);
        XHostAddress* first = (XHostAddress*)XVector_iterator_data(&it);
        XClassCopy(&d->m_serverAddr, first);
    }
    XClassDelete(info);
    d->m_addrResolved = true;
    return true;
}

/**
 * @brief 构造并发送一次请求（刷新 T1；供首次发送与超时重试共用）。
 */
static bool xntp_sendRequest(XNtpClientPrivate* d)
{
    XNtpPacket request;
    XNtpTimestamp t1;
    uint8_t wire[XNTP_PACKET_SIZE];
    int64_t nowWallMs;
    int64_t sent;

    if (!d || !d->m_socket)
        return false;

    /* T1 用墙钟毫秒（真实 UTC + 亚秒精度）：offset/delay 公式要求 T1..T4
     * 同一时刻基。秒级截断的 currentSecsSinceEpoch 不够精度，单调钟会随
     * 校时跳变，均不可用（见 XDateTime_currentWallMSecsSinceEpoch）。 */
    nowWallMs = XDateTime_currentWallMSecsSinceEpoch();
    XNtpTimestamp_fromUnixMs(&t1, (uint64_t)nowWallMs);

    XNtpPacket_makeClientRequest(&request, &t1);
    if (!XNtpPacket_encode(&request, wire))
        return false;

    sent = XUdpSocket_writeDatagram(d->m_socket, (const char*)wire,
                                    (int64_t)sizeof(wire),
                                    &d->m_serverAddr, d->m_serverPort);
    if (sent != (int64_t)sizeof(wire))
        return false;

    d->m_t1 = t1;
    d->m_t1WallMs = nowWallMs;
    return true;
}

/**
 * @brief 启动应答超时定时器（单发语义：触发即先自行摘除）。
 */
static void xntp_armTimeout(XNtpClientPrivate* d, XNtpClient* self)
{
    if (!d || !self)
        return;
    if (d->m_timeoutTimer)
        XObject_killTimer((XObject*)self, d->m_timeoutTimer);
    d->m_timeoutTimer = XObject_startTimer_ms((XObject*)self, d->m_timeoutMs,
                                              XTimerType_CoarseTimer);
}

static void xntp_disarmTimeout(XNtpClientPrivate* d, XNtpClient* self)
{
    if (!d || !self)
        return;
    if (d->m_timeoutTimer) {
        XObject_killTimer((XObject*)self, d->m_timeoutTimer);
        d->m_timeoutTimer = 0;
    }
}

/**
 * @brief 统一失败出口：置错、转 Failed 态并发失败信号。
 */
static void xntp_fail(XNtpClient* self, XNtpClientError error)
{
    XNtpClientPrivate* d = self ? self->m_d : NULL;
    if (!d)
        return;
    xntp_disarmTimeout(d, self);
    d->m_lastError = (uint32_t)error;
    d->m_state = (uint32_t)XNtpClientState_Failed;
    XNtpClient_syncFailed_signal(self);
}

/**
 * @brief readyRead 槽：读应答 → 校验 → 计算 offset/delay → 发信号。
 * @note  收到非法报文（错包/杂散 UDP）时保持 Syncing 继续等待，让超时
 *        机制兜底；只有协议层明确拒绝（KoD/未同步）才立即判失败。
 */
static void xntp_onReadyRead(XObject* receiver, XVarList* args)
{
    XNtpClient* self = (XNtpClient*)receiver;
    XNtpClientPrivate* d;
    XUdpSocket* sock;
    uint8_t wire[XNTP_PACKET_SIZE];
    XNtpPacket reply;
    int64_t t4WallMs;
    int64_t got;
    int64_t t1Ms;
    int64_t t2Ms;
    int64_t t3Ms;
    int64_t offsetMs;
    int64_t delayMs;

    (void)args;
    if (!self || !(d = self->m_d) || !d->m_socket)
        return;
    if (d->m_state != (uint32_t)XNtpClientState_Syncing)
        return; /* 非同步中的报文一律忽略 */

    sock = d->m_socket;
    if (!XUdpSocket_hasPendingDatagrams(sock))
        return;

    got = XUdpSocket_readDatagram(sock, (char*)wire, (int64_t)sizeof(wire),
                                  NULL, NULL);
    if (got < (int64_t)XNTP_PACKET_SIZE)
        return; /* 过短报文：等待真正应答，超时兜底 */
    t4WallMs = XDateTime_currentWallMSecsSinceEpoch();

    if (!XNtpPacket_decode(wire, (size_t)got, &reply))
        return;
    if (reply.m_mode == (uint8_t)XNtpMode_Server
        && reply.m_stratum == XNTP_STRATUM_KOD) {
        xntp_fail(self, XNtpClientError_KissOfDeath);
        return;
    }
    if (reply.m_leap == (uint8_t)XNtpLeapIndicator_Unsynchronized
        || reply.m_stratum > XNTP_STRATUM_MAX) {
        xntp_fail(self, XNtpClientError_ServerUnsynchronized);
        return;
    }
    if (!XNtpPacket_isValidServerReply(&reply, &d->m_t1))
        return; /* 错包/T1 不匹配：继续等待正确应答 */

    t1Ms = d->m_t1WallMs;
    t2Ms = (int64_t)XNtpTimestamp_toUnixMs(&reply.m_receive);
    t3Ms = (int64_t)XNtpTimestamp_toUnixMs(&reply.m_transmit);
    if (t2Ms <= 0 || t3Ms <= 0)
        return;

    /* RFC 5905 标准式：offset = ((T2-T1)+(T3-T4))/2；delay = (T4-T1)-(T3-T2) */
    offsetMs = ((t2Ms - t1Ms) + (t3Ms - t4WallMs)) / 2;
    delayMs = (t4WallMs - t1Ms) - (t3Ms - t2Ms);
    if (delayMs < 0)
        delayMs = 0; /* 时钟跳变等异常下不为负 */

    xntp_disarmTimeout(d, self);
    d->m_offsetMs = offsetMs;
    d->m_delayMs = delayMs;
    d->m_serverSecs = (uint64_t)t3Ms / 1000ULL;
    d->m_stratum = reply.m_stratum;
    d->m_lastError = (uint32_t)XNtpClientError_None;
    d->m_state = (uint32_t)XNtpClientState_Synchronized;
    d->m_synchronized = 1;

    XNtpClient_synchronized_signal(self);
    {
        int64_t offsetArg = offsetMs;
        XEmitSignal(self, XNtpClient_offsetChanged_signal,
                    XVarList_Create(XVar(int64_t, offsetArg)),
                    NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
}

/* ========================================================================== */
/*                        配置                                               */
/* ========================================================================== */

bool XNtpClient_setServer(XNtpClient* self, const XString* name, uint16_t port)
{
    XNtpClientPrivate* d = self ? self->m_d : NULL;
    if (!d || !name)
        return false;

    /* 换服务器即重置同步状态：旧偏移对新服务器无意义。 */
    if (d->m_timeoutTimer) {
        XObject_killTimer((XObject*)self, d->m_timeoutTimer);
        d->m_timeoutTimer = 0;
    }
    if (d->m_syncTimer) {
        XObject_killTimer((XObject*)self, d->m_syncTimer);
        d->m_syncTimer = 0;
        if (d->m_autoSync) {
            d->m_syncTimer = XObject_startTimer_ms((XObject*)self,
                                                   d->m_syncIntervalMs,
                                                   XTimerType_CoarseTimer);
        }
    }
    if (d->m_serverName)
        XClassDelete(d->m_serverName);
    d->m_serverName = XString_create_copy(name);
    d->m_serverPort = port ? port : (uint16_t)XNTP_DEFAULT_PORT;
    d->m_addrResolved = false;
    d->m_state = (uint32_t)XNtpClientState_Idle;
    d->m_synchronized = 0;
    return d->m_serverName != NULL;
}

bool XNtpClient_setServer_2(XNtpClient* self, const char* nameUtf8, uint16_t port)
{
    XString* name;
    bool ok;
    if (!self || !nameUtf8)
        return false;
    name = XString_create_utf8(nameUtf8);
    if (!name)
        return false;
    ok = XNtpClient_setServer(self, name, port);
    XClassDelete(name);
    return ok;
}

bool XNtpClient_setTimeout(XNtpClient* self, uint32_t timeoutMs)
{
    XNtpClientPrivate* d = self ? self->m_d : NULL;
    if (!d)
        return false;
    d->m_timeoutMs = timeoutMs ? timeoutMs : (uint32_t)XNTP_DEFAULT_TIMEOUT_MS;
    return true;
}

bool XNtpClient_setAutoSync(XNtpClient* self, bool on, uint32_t intervalMs)
{
    XNtpClientPrivate* d = self ? self->m_d : NULL;
    if (!d)
        return false;
    d->m_autoSync = on ? 1u : 0u;
    d->m_syncIntervalMs = intervalMs ? intervalMs
                                     : (uint32_t)XNTP_DEFAULT_SYNC_INTERVAL_MS;
    if (d->m_syncTimer) {
        XObject_killTimer((XObject*)self, d->m_syncTimer);
        d->m_syncTimer = 0;
    }
    if (on) {
        d->m_syncTimer = XObject_startTimer_ms((XObject*)self,
                                               d->m_syncIntervalMs,
                                               XTimerType_CoarseTimer);
    }
    return true;
}

bool XNtpClient_setRetries(XNtpClient* self, int retries)
{
    XNtpClientPrivate* d = self ? self->m_d : NULL;
    if (!d)
        return false;
    d->m_maxRetries = retries > 0 ? retries : 0;
    return true;
}

/* ========================================================================== */
/*                        同步                                               */
/* ========================================================================== */

bool XNtpClient_syncNow(XNtpClient* self)
{
    XNtpClientPrivate* d = self ? self->m_d : NULL;
    if (!d)
        return false;
    if (d->m_state == (uint32_t)XNtpClientState_Syncing)
        return false; /* 同步进行中，拒绝并发 */
    if (!d->m_serverName) {
        d->m_lastError = (uint32_t)XNtpClientError_NoServer;
        return false;
    }

    /* 懒创建套接字；绑定任意端口后由 writeDatagram 定向发送。 */
    if (!d->m_socket) {
        d->m_socket = XUdpSocket_create();
        if (!d->m_socket
            || !XUdpSocket_bindAny(d->m_socket, 0,
                                   XAbstractSocket_DefaultForPlatform)) {
            if (d->m_socket) {
                XClassDelete((XClass*)d->m_socket);
                d->m_socket = NULL;
            }
            xntp_fail(self, XNtpClientError_Socket);
            return true; /* 已按异步失败路径处理 */
        }
        XObject_connect_1((XObject*)d->m_socket,
                          XSignal(XIODevice_readyRead_signal),
                          (XObject*)self, xntp_onReadyRead,
                          XConnectionType_Auto);
    }

    if (!xntp_resolveServer(self)) {
        xntp_fail(self, XNtpClientError_NoServer);
        return true;
    }

    d->m_retriesLeft = d->m_maxRetries;
    if (!xntp_sendRequest(d)) {
        xntp_fail(self, XNtpClientError_Socket);
        return true;
    }
    d->m_state = (uint32_t)XNtpClientState_Syncing;
    xntp_armTimeout(d, self);
    return true;
}

/* ========================================================================== */
/*                        查询                                               */
/* ========================================================================== */

XNtpClientState XNtpClient_state(const XNtpClient* self)
{
    if (!self || !self->m_d)
        return XNtpClientState_Idle;
    return (XNtpClientState)self->m_d->m_state;
}

XNtpClientError XNtpClient_lastError(const XNtpClient* self)
{
    if (!self || !self->m_d)
        return XNtpClientError_None;
    return (XNtpClientError)self->m_d->m_lastError;
}

bool XNtpClient_isSynchronized(const XNtpClient* self)
{
    if (!self || !self->m_d)
        return false;
    return self->m_d->m_synchronized != 0u;
}

int64_t XNtpClient_offsetMs(const XNtpClient* self)
{
    if (!self || !self->m_d)
        return 0;
    return self->m_d->m_offsetMs;
}

int64_t XNtpClient_roundTripMs(const XNtpClient* self)
{
    if (!self || !self->m_d)
        return 0;
    return self->m_d->m_delayMs;
}

uint64_t XNtpClient_serverSecs(const XNtpClient* self)
{
    if (!self || !self->m_d)
        return 0;
    return self->m_d->m_serverSecs;
}

uint8_t XNtpClient_stratum(const XNtpClient* self)
{
    if (!self || !self->m_d)
        return 0;
    return self->m_d->m_stratum;
}

/* ========================================================================== */
/*                        校时落地                                           */
/* ========================================================================== */

bool XNtpClient_applyToSystem(XNtpClient* self)
{
    XNtpClientPrivate* d = self ? self->m_d : NULL;
    int64_t trueWallMs;
    int64_t secs;

    if (!d || d->m_synchronized == 0u)
        return false;
    /* 估算当前真实时刻 = 本地墙钟 + 偏移（写入粒度为秒，亚秒舍入）。 */
    trueWallMs = XDateTime_currentWallMSecsSinceEpoch() + d->m_offsetMs;
    secs = (trueWallMs + 500) / 1000;
    return XDateTime_setSystemSecsSinceEpoch(secs);
}

/* ========================================================================== */
/*                        信号                                               */
/* ========================================================================== */

#define XNTP_SIMPLE_SIGNAL(object, function)                                 \
    XEmitSignal(object, function, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL)

void* XNtpClient_synchronized_signal(XNtpClient* client)
{
    XNTP_SIMPLE_SIGNAL(client, XNtpClient_synchronized_signal);
}

void* XNtpClient_syncFailed_signal(XNtpClient* client)
{
    XNTP_SIMPLE_SIGNAL(client, XNtpClient_syncFailed_signal);
}

void* XNtpClient_offsetChanged_signal(XNtpClient* client, int64_t offsetMs)
{
    (void)offsetMs;
    XNTP_SIMPLE_SIGNAL(client, XNtpClient_offsetChanged_signal);
}

#endif /* XNTP_ON && XNTP_CLIENT_ON */
