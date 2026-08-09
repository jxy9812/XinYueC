/**
 * @file XCanShellTestBackend.c
 * @brief XCan Shell 测试用固定队列后端。
 * @details
 * 该文件只在显式定义 XCONSOLE_SHELL_CAN_TEST_BACKEND 时编译，用于验证
 * Shell 命令的参数、生命周期、发送和接收路径。它不模拟任何平台寄存器，
 * 也不应被产品固件链接；真实平台必须在 Drive 中实现同一公共接口。
 */
#include "XCan.h"

#if defined(XCONSOLE_SHELL_CAN_TEST_BACKEND) && XCONSOLE_SHELL_CAN_TEST_BACKEND

#include "XMemory.h"
#include <string.h>

#define XCAN_TEST_QUEUE_CAPACITY 16u

struct XCan {
    XCanConfig config;
    bool open;
    bool started;
    XCanError error;
    int32_t nativeError;
    XCanEventCallback callback;
    void* callbackUserData;
    XCanFrame queue[XCAN_TEST_QUEUE_CAPACITY];
    uint32_t queueHead;
    uint32_t queueTail;
    uint32_t queueCount;
    XCanFilter filter;
    bool hasFilter;
};

static void xcan_test_error(XCan* can, XCanError error)
{
    if (can) {
        can->error = error;
        can->nativeError = error == XCanError_None ? 0 : (int32_t)error;
    }
}

static bool xcan_test_valid_config(const XCanConfig* config)
{
    if (!config || config->m_reserved[0] != 0u || config->m_reserved[1] != 0u ||
        config->m_nominalTiming.m_bitrate == 0u)
        return false;
    if (config->m_frameFormat == XCanFrameFormat_Classical &&
        config->m_dataTiming.m_bitrate != 0u)
        return false;
    if (config->m_nominalTiming.m_samplePointPermille > 1000u ||
        config->m_dataTiming.m_samplePointPermille > 1000u)
        return false;
    return true;
}

XCan* XCan_create(const XCanConfig* config)
{
    XCan* can;
    if (!xcan_test_valid_config(config)) return NULL;
    can = (XCan*)XMalloc_System(sizeof(*can));
    if (!can) return NULL;
    memset(can, 0, sizeof(*can));
    can->config = *config;
    can->config.m_channel.m_name = NULL;
    can->error = XCanError_None;
    return can;
}

bool XCan_open(XCan* can)
{
    if (!can) return false;
    if (can->open) { xcan_test_error(can, XCanError_AlreadyOpen); return false; }
    can->open = true;
    can->started = false;
    xcan_test_error(can, XCanError_None);
    return true;
}

void XCan_close(XCan* can)
{
    if (!can) return;
    can->started = false;
    can->open = false;
    can->queueHead = can->queueTail = can->queueCount = 0u;
}

void XCan_delete(XCan* can)
{
    if (!can) return;
    XCan_close(can);
    XFree_System(can);
}

bool XCan_isOpen(const XCan* can) { return can && can->open; }

bool XCan_start(XCan* can)
{
    if (!can) return false;
    if (!can->open) { xcan_test_error(can, XCanError_NotOpen); return false; }
    if (can->started) { xcan_test_error(can, XCanError_AlreadyStarted); return false; }
    can->started = true;
    xcan_test_error(can, XCanError_None);
    return true;
}

void XCan_stop(XCan* can)
{
    if (!can) return;
    can->started = false;
}

bool XCan_isStarted(const XCan* can) { return can && can->started; }

bool XCan_getConfig(const XCan* can, XCanConfig* config)
{
    if (!can || !config) return false;
    *config = can->config;
    return true;
}

bool XCan_configure(XCan* can, const XCanConfig* config)
{
    if (!can || !config) return false;
    if (can->started) { xcan_test_error(can, XCanError_Busy); return false; }
    if (!xcan_test_valid_config(config)) {
        xcan_test_error(can, XCanError_InvalidArgument);
        return false;
    }
    can->config = *config;
    can->config.m_channel.m_name = NULL;
    xcan_test_error(can, XCanError_None);
    return true;
}

XCanIoResult XCan_send(XCan* can, const XCanFrame* frame, int32_t timeoutMs)
{
    (void)timeoutMs;
    if (!can || !frame) return XCanIoResult_Error;
    if (!can->open) { xcan_test_error(can, XCanError_NotOpen); return XCanIoResult_Error; }
    if (!can->started) { xcan_test_error(can, XCanError_NotStarted); return XCanIoResult_Error; }
    if (frame->m_length > (can->config.m_frameFormat == XCanFrameFormat_Classical ?
                          XCAN_CLASSIC_DATA_LENGTH : XCAN_FD_DATA_LENGTH)) {
        xcan_test_error(can, XCanError_InvalidArgument);
        return XCanIoResult_Error;
    }
    if (can->queueCount >= XCAN_TEST_QUEUE_CAPACITY) {
        xcan_test_error(can, XCanError_Overflow);
        return XCanIoResult_Error;
    }
    can->queue[can->queueTail] = *frame;
    can->queueTail = (can->queueTail + 1u) % XCAN_TEST_QUEUE_CAPACITY;
    ++can->queueCount;
    xcan_test_error(can, XCanError_None);
    return XCanIoResult_Success;
}

XCanIoResult XCan_receive(XCan* can, XCanFrame* frame, int32_t timeoutMs)
{
    (void)timeoutMs;
    if (!can || !frame) return XCanIoResult_Error;
    if (!can->open) { xcan_test_error(can, XCanError_NotOpen); return XCanIoResult_Error; }
    if (!can->started) { xcan_test_error(can, XCanError_NotStarted); return XCanIoResult_Error; }
    if (can->queueCount == 0u) {
        xcan_test_error(can, XCanError_Timeout);
        return timeoutMs == 0 ? XCanIoResult_WouldBlock : XCanIoResult_Timeout;
    }
    *frame = can->queue[can->queueHead];
    can->queueHead = (can->queueHead + 1u) % XCAN_TEST_QUEUE_CAPACITY;
    --can->queueCount;
    xcan_test_error(can, XCanError_None);
    return XCanIoResult_Success;
}

bool XCan_clearTransmitQueue(XCan* can)
{
    if (!can) return false;
    return true;
}

bool XCan_clearReceiveQueue(XCan* can)
{
    if (!can) return false;
    can->queueHead = can->queueTail = can->queueCount = 0u;
    return true;
}

bool XCan_addFilter(XCan* can, const XCanFilter* filter, uint32_t* filterId)
{
    if (!can || !filter) return false;
    can->filter = *filter;
    can->hasFilter = true;
    if (filterId) *filterId = 1u;
    return true;
}

bool XCan_removeFilter(XCan* can, uint32_t filterId)
{
    if (!can || filterId != 1u || !can->hasFilter) return false;
    can->hasFilter = false;
    return true;
}

void XCan_clearFilters(XCan* can)
{
    if (can) can->hasFilter = false;
}

bool XCan_setEventCallback(XCan* can, XCanEventCallback callback, void* userData)
{
    if (!can) return false;
    can->callback = callback;
    can->callbackUserData = userData;
    return true;
}

XCanProcessResult XCan_processEvents(XCan* can, int32_t timeoutMs)
{
    (void)timeoutMs;
    if (!can) return XCanProcessResult_Error;
    return XCanProcessResult_Timeout;
}

bool XCan_getStatus(const XCan* can, XCanStatus* status)
{
    if (!can || !status) return false;
    memset(status, 0, sizeof(*status));
    status->m_state = !can->open ? XCanBusState_Unknown :
                       (can->started ? XCanBusState_ErrorActive : XCanBusState_Stopped);
    status->m_receiveErrorCount = 0u;
    status->m_transmitErrorCount = 0u;
    status->m_rxPending = can->queueCount;
    status->m_txPending = 0u;
    return true;
}

bool XCan_recoverBus(XCan* can)
{
    if (!can || !can->open) return false;
    return true;
}

XCanFeatures XCan_features(const XCan* can)
{
    return can ? (XCanFeature_ClassicalCan | XCanFeature_CanFd |
                  XCanFeature_BitrateConfiguration | XCanFeature_Loopback |
                  XCanFeature_ListenOnly | XCanFeature_Filters |
                  XCanFeature_BusStatus | XCanFeature_ProcessEvents) :
                 XCanFeature_None;
}

bool XCan_hasFeature(const XCan* can, XCanFeature feature)
{
    return can && feature != XCanFeature_None &&
           (XCan_features(can) & feature) == feature;
}

XCanNativeHandle XCan_handle(const XCan* can)
{
    return can ? (XCanNativeHandle)(uintptr_t)can : XCAN_INVALID_NATIVE_HANDLE;
}

XCanError XCan_lastError(const XCan* can)
{
    return can ? can->error : XCanError_InvalidArgument;
}

int32_t XCan_nativeError(const XCan* can) { return can ? can->nativeError : 0; }

void XCan_clearError(XCan* can) { xcan_test_error(can, XCanError_None); }

const char* XCan_errorString(XCanError error)
{
    switch (error) {
    case XCanError_None: return "none";
    case XCanError_InvalidArgument: return "invalid-argument";
    case XCanError_NotOpen: return "not-open";
    case XCanError_AlreadyOpen: return "already-open";
    case XCanError_NotStarted: return "not-started";
    case XCanError_AlreadyStarted: return "already-started";
    case XCanError_Unsupported: return "unsupported";
    case XCanError_Busy: return "busy";
    case XCanError_Timeout: return "timeout";
    case XCanError_WouldBlock: return "would-block";
    case XCanError_Overflow: return "overflow";
    default: return "unknown";
    }
}

#endif /* XCONSOLE_SHELL_CAN_TEST_BACKEND */
