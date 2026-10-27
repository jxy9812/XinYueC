/** @file       XLinkFileClient.c
 * @brief      XLink 文件传输客户端实现。
 * @details    只依赖 XinYueC 抽象层（XObject/XMemory/XDateTime），
 *             禁止平台 API 与 stdio。节拍经 XObject 内建定时器驱动，
 *             与 XLinkSession 的异步口径一致。
 * @author     XinYueC 团队
 */
#include "XLinkFileClient.h"

#if XLINK_ON && XLINK_FILE_ON && XLINK_CLIENT_ON

#include "XLinkFile.h"
#include "XString.h"
#include "XMemory.h"
#include "XDateTime.h"
#include "XPrintf.h"
#include <string.h>

/** @brief 本类使用的节拍定时器 id（每个对象一个）。 */
static uint32_t xfc_tickInterval(const XLinkFileClient* obj)
{
    return obj->m_tickIntervalMs ? obj->m_tickIntervalMs
                                 : XLINK_FILE_TICK_MS;
}

/* ========================================================================== */
/*                        内部：会话挂接与节拍                               */
/* ========================================================================== */

/* 前置声明：收帧槽与 pump 都会调用（定义见下）。 */
static void xfc_stopTimerIfIdle(XLinkFileClient* obj);

/** @brief 把 File 帧转给传输引擎。 */
static bool xfc_onFileFrame(XLinkSession* session, const XLinkFrameHeader* frame,
                            const uint8_t* payload, void* userData)
{
    XLinkFileClient* obj = (XLinkFileClient*)userData;
    (void)session;
    if (!obj || !frame)
        return true;
    if (!XLinkFileTransfer_isFinished(&obj->m_transfer))
        XLinkFileTransfer_onFrame(&obj->m_transfer, frame, payload,
                                  (uint32_t)XDateTime_currentMSecsSinceEpoch());
    /* 收帧后立即推进一次：发送侧在收到 ACK 后需要马上续发下一块，
       不能只等定时器（无事件分发器环境下定时器可能根本没起来）。 */
    if (!XLinkFileTransfer_isFinished(&obj->m_transfer))
        XLinkFileTransfer_pump(&obj->m_transfer,
                               (uint32_t)XDateTime_currentMSecsSinceEpoch());
    xfc_stopTimerIfIdle(obj);
    return true;
}

/** @brief 在活跃传输期间确保节拍定时器在跑。 */
static void xfc_ensureTimer(XLinkFileClient* obj)
{
    uint32_t interval;
    if (!obj || obj->m_timerId != XTIMER_INVALID_ID)
        return;
    interval = xfc_tickInterval(obj);
    if (interval == 0)
        return;
    obj->m_timerId = XObject_startTimer_ms((XObject*)obj, interval,
                                           XTimerType_CoarseTimer);
}

/** @brief 无活跃传输时停掉节拍（省电）。 */
static void xfc_stopTimerIfIdle(XLinkFileClient* obj)
{
    if (!obj || obj->m_timerId == XTIMER_INVALID_ID)
        return;
    if (XLinkFileTransfer_isFinished(&obj->m_transfer)) {
        XObject_killTimer((XObject*)obj, obj->m_timerId);
        obj->m_timerId = XTIMER_INVALID_ID;
    }
}

/* ========================================================================== */
/*                        回调转信号                                         */
/* ========================================================================== */

static void xfc_onProgress(void* userData, uint32_t transferId, uint64_t done,
                           uint64_t total)
{
    XLinkFileClient* obj = (XLinkFileClient*)userData;
    if (!obj)
        return;
    XLinkFileClient_progress_signal(obj, transferId, done, total);
}

static void xfc_onFinished(void* userData, uint32_t transferId, bool ok,
                           uint8_t status, const char* text)
{
    XLinkFileClient* obj = (XLinkFileClient*)userData;
    if (!obj)
        return;
    XLinkFileClient_finished_signal(obj, transferId, ok, status,
                                    text ? text : "");
    xfc_stopTimerIfIdle(obj);
}

/* ========================================================================== */
/*                        虚函数                                             */
/* ========================================================================== */

static void VXLinkFileClient_deinit(XClass* object)
{
    XLinkFileClient* obj;
    if (!object)
        return;
    obj = (XLinkFileClient*)object;
    if (obj->m_timerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)obj, obj->m_timerId);
        obj->m_timerId = XTIMER_INVALID_ID;
    }
    XLinkFileTransfer_deinit(&obj->m_transfer);
    obj->m_client = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)obj);
}

static void VXLinkFileClient_timerEvent(XObject* object, XTimerEvent* event)
{
    XLinkFileClient* obj = (XLinkFileClient*)object;
    if (!obj || !event)
        return;
    if (XTimerEvent_timerId(event) != obj->m_timerId) {
        XClass_Parent(XObject, EXObject_TimerEvent,
                      void (*)(XObject*, XTimerEvent*))((XObject*)object,
                                                        event);
        return;
    }
    XLinkFileTransfer_pump(&obj->m_transfer,
                           (uint32_t)XDateTime_currentMSecsSinceEpoch());
    xfc_stopTimerIfIdle(obj);
}

XVtable* XLinkFileClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLinkFileClient)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLinkFileClient_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXLinkFileClient_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXLinkFileClient_timerEvent,
                             VXLinkFileClient_timerEvent);
    return XVTABLE_DEFAULT;
}

/* ========================================================================== */
/*                        构造与生命周期                                     */
/* ========================================================================== */

void XLinkFileClient_init(XLinkFileClient* obj)
{
    if (!obj)
        return;
    memset(obj, 0, sizeof(*obj));
    XObject_init((XObject*)obj);
    XClassSetVtable(obj, XLinkFileClient);
    obj->m_client = NULL;
    XLinkFileTransfer_init(&obj->m_transfer);
    /* transferId 用奇数段（服务端用偶数段），避免两端撞号。 */
    obj->m_nextTransferId = 1;
    obj->m_tickIntervalMs = XLINK_FILE_TICK_MS;
    obj->m_timerId = XTIMER_INVALID_ID;
}

XLinkFileClient* XLinkFileClient_create(void)
{
    XMemoryType memory = XCLASS_DEFAULT_MEMORY_TYPE;
    XLinkFileClient* obj =
        (XLinkFileClient*)XMemory_malloc(sizeof(XLinkFileClient), memory);
    if (!obj)
        return NULL;
    XLinkFileClient_init(obj);
    Set_Class_Memory(obj, memory);
    Set_Class_IsHeap(obj, true);
    return obj;
}

bool XLinkFileClient_attach(XLinkFileClient* obj, XLinkClient* client)
{
    if (!obj || !client)
        return false;
    obj->m_client = client;
    XLinkFileTransfer_setCallbacks(&obj->m_transfer, xfc_onProgress,
                                   xfc_onFinished, obj);
    return true;
}

/* ========================================================================== */
/*                        发起与中止                                         */
/* ========================================================================== */

/** @brief 取当前会话并挂 File 回调（每次发起都挂，覆盖重连新建的会话）。
 *  @note  能力协商发生在握手期：客户端是否支持文件传输必须在连接**之前**
 *         通过 XLinkClient_setCapability 声明。本函数只负责挂回调；
 *         若对端未协商文件能力，后续 Open 会因对端不处理而无响应，
 *         由传输引擎的空闲超时兜底（或调用方先查 negotiatedCapability）。
 */
static XLinkSession* xfc_readySession(XLinkFileClient* obj)
{
    XLinkSession* session;
    if (!obj || !obj->m_client)
        return NULL;
    if (!XLinkClient_isReady(obj->m_client))
        return NULL;
    session = XLinkClient_session(obj->m_client);
    if (!session)
        return NULL;
    XLinkSession_setFileCallback(session, xfc_onFileFrame, obj);
    return session;
}

/** @brief 分配下一个 transferId（奇数段，跳过 0）。 */
static uint32_t xfc_nextId(XLinkFileClient* obj)
{
    uint32_t id = obj->m_nextTransferId;
    obj->m_nextTransferId += 2u;
    if (obj->m_nextTransferId == 0)
        obj->m_nextTransferId = 1;
    if (id == 0)
        id = 1;
    return id;
}

bool XLinkFileClient_upload(XLinkFileClient* obj, const char* localPath,
                            const char* remoteName, uint8_t flags,
                            uint32_t* outTransferId)
{
    XLinkSession* session;
    uint32_t id;

    if (!obj || XLinkFileClient_isBusy(obj))
        return false;
    session = xfc_readySession(obj);
    if (!session)
        return false;
    id = xfc_nextId(obj);
    /* 只设 id 与回调；生命周期由 beginUpload/beginDownload 内部重置
       （它们会保留 id 与回调，避免刚设好就被清掉）。 */
    obj->m_transfer.m_transferId = id;
    XLinkFileTransfer_setCallbacks(&obj->m_transfer, xfc_onProgress,
                                   xfc_onFinished, obj);
    if (!XLinkFileTransfer_beginUpload(&obj->m_transfer, session, localPath,
                                       remoteName, flags, NULL)) {
        return false;
    }
    xfc_ensureTimer(obj);
    if (outTransferId)
        *outTransferId = id;
    return true;
}

bool XLinkFileClient_download(XLinkFileClient* obj, const char* remoteName,
                              const char* localPath, uint8_t flags,
                              uint32_t* outTransferId)
{
    XLinkSession* session;
    uint32_t id;

    if (!obj || XLinkFileClient_isBusy(obj))
        return false;
    session = xfc_readySession(obj);
    if (!session)
        return false;
    id = xfc_nextId(obj);
    /* 只设 id 与回调；生命周期由 beginUpload/beginDownload 内部重置
       （它们会保留 id 与回调，避免刚设好就被清掉）。 */
    obj->m_transfer.m_transferId = id;
    XLinkFileTransfer_setCallbacks(&obj->m_transfer, xfc_onProgress,
                                   xfc_onFinished, obj);
    if (!XLinkFileTransfer_beginDownload(&obj->m_transfer, session, remoteName,
                                         localPath, flags, NULL)) {
        return false;
    }
    xfc_ensureTimer(obj);
    if (outTransferId)
        *outTransferId = id;
    return true;
}

void XLinkFileClient_pump(XLinkFileClient* obj, uint32_t nowMs)
{
    uint32_t now;
    if (!obj)
        return;
    now = nowMs ? nowMs : (uint32_t)XDateTime_currentMSecsSinceEpoch();
    /* 先驱动承载会话：排空控制帧（Open/ACK）发送队列并抽取收到的数据帧。
       手动驱动环境（无事件分发器）下这是唯一的推进通道。 */
    if (obj->m_transfer.m_session)
        XLinkSession_pump(obj->m_transfer.m_session, now);
    XLinkFileTransfer_pump(&obj->m_transfer, now);
    xfc_stopTimerIfIdle(obj);
}

void XLinkFileClient_abort(XLinkFileClient* obj, uint8_t status,
                           const char* text)
{
    if (!obj)
        return;
    XLinkFileTransfer_abort(&obj->m_transfer, status, text);
    xfc_stopTimerIfIdle(obj);
}

/* ========================================================================== */
/*                        查询                                               */
/* ========================================================================== */

bool XLinkFileClient_isBusy(const XLinkFileClient* obj)
{
    if (!obj)
        return false;
    return !XLinkFileTransfer_isFinished(&obj->m_transfer)
           && XLinkFileTransfer_state(&obj->m_transfer)
                  != XLinkFileTransferState_Idle;
}

XLinkFileTransferState XLinkFileClient_state(const XLinkFileClient* obj)
{
    return obj ? XLinkFileTransfer_state(&obj->m_transfer)
               : XLinkFileTransferState_Idle;
}

uint64_t XLinkFileClient_doneBytes(const XLinkFileClient* obj)
{
    return obj ? XLinkFileTransfer_doneBytes(&obj->m_transfer) : 0;
}

uint64_t XLinkFileClient_totalBytes(const XLinkFileClient* obj)
{
    return obj ? XLinkFileTransfer_totalBytes(&obj->m_transfer) : 0;
}

XLinkFileTransfer* XLinkFileClient_transfer(XLinkFileClient* obj)
{
    return obj ? &obj->m_transfer : NULL;
}

/* ========================================================================== */
/*                        信号                                               */
/* ========================================================================== */

void* XLinkFileClient_progress_signal(XLinkFileClient* self,
                                      uint32_t transferId, uint64_t done,
                                      uint64_t total)
{
    XEmitSignal(self, XLinkFileClient_progress_signal,
                XVarList_Create(XVar(uint32_t, transferId), XVar(uint64_t, done),
                                XVar(uint64_t, total)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XLinkFileClient_finished_signal(XLinkFileClient* self,
                                      uint32_t transferId, bool ok,
                                      uint8_t status, const char* text)
{
    XEmitSignal(self, XLinkFileClient_finished_signal,
                XVarList_Create(XVar(uint32_t, transferId), XVar(bool, ok),
                                XVar(uint8_t, status), XVar(const char*, text)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XLINK_ON && XLINK_FILE_ON && XLINK_CLIENT_ON */
