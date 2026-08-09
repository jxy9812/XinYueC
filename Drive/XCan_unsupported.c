/**
 * @file XCan_unsupported.c
 * @brief 没有 CAN 驱动平台的安全存根。
 * @details
 * 该文件只提供 XCan 公共 API 的完整符号。没有实际控制器后，生命周期
 * 查询仍保持确定性，打开、启动、收发、过滤器和事件处理明确返回
 * XCanError_Unsupported，避免 Shell 在缺少硬件后端时产生链接错误或误报
 * 成功。产品后端定义 XCAN_PLATFORM_BACKEND 后应替换本文件。
 */
#include "XCan.h"

#if !(defined(XCAN_TEST_BACKEND) && XCAN_TEST_BACKEND) && \
    !(defined(XCONSOLE_SHELL_CAN_TEST_BACKEND) && XCONSOLE_SHELL_CAN_TEST_BACKEND) && \
    !(defined(XCAN_PLATFORM_BACKEND) && XCAN_PLATFORM_BACKEND)

#include "XMemory.h"
#include <string.h>

struct XCan {
    XCanConfig m_config;
    XCanError m_error;
    int32_t m_nativeError;
    bool m_open;
    bool m_started;
};

static void xcan_stub_error(XCan* can, XCanError error)
{
    if (can) {
        can->m_error = error;
        can->m_nativeError = 0;
    }
}

static bool xcan_stub_valid(const XCanConfig* config)
{
    return config && config->m_reserved[0] == 0u && config->m_reserved[1] == 0u &&
           config->m_nominalTiming.m_bitrate != 0u &&
           config->m_nominalTiming.m_samplePointPermille <= 1000u &&
           config->m_dataTiming.m_samplePointPermille <= 1000u;
}

XCan* XCan_create(const XCanConfig* config)
{
    XCan* can;
    if (!xcan_stub_valid(config)) return NULL;
    can = (XCan*)XCalloc_System(1u, sizeof(*can));
    if (!can) return NULL;
    can->m_config = *config;
    can->m_config.m_channel.m_name = NULL;
    can->m_error = XCanError_None;
    return can;
}

bool XCan_open(XCan* can)
{
    if (!can) return false;
    if (can->m_open) { xcan_stub_error(can, XCanError_AlreadyOpen); return false; }
    xcan_stub_error(can, XCanError_Unsupported);
    return false;
}

void XCan_close(XCan* can)
{
    if (!can) return;
    can->m_started = false;
    can->m_open = false;
    xcan_stub_error(can, XCanError_None);
}

void XCan_delete(XCan* can)
{
    if (!can) return;
    XCan_close(can);
    XFree_System(can);
}

bool XCan_isOpen(const XCan* can) { return can && can->m_open; }

bool XCan_start(XCan* can)
{
    if (!can) return false;
    if (!can->m_open) { xcan_stub_error(can, XCanError_NotOpen); return false; }
    xcan_stub_error(can, XCanError_Unsupported);
    return false;
}

void XCan_stop(XCan* can)
{
    if (can) can->m_started = false;
}

bool XCan_isStarted(const XCan* can) { return can && can->m_started; }

bool XCan_getConfig(const XCan* can, XCanConfig* config)
{
    if (!can || !config) return false;
    *config = can->m_config;
    return true;
}

bool XCan_configure(XCan* can, const XCanConfig* config)
{
    if (!can || !xcan_stub_valid(config)) {
        if (can) xcan_stub_error(can, XCanError_InvalidArgument);
        return false;
    }
    if (can->m_started) { xcan_stub_error(can, XCanError_Busy); return false; }
    can->m_config = *config;
    can->m_config.m_channel.m_name = NULL;
    xcan_stub_error(can, XCanError_None);
    return true;
}

XCanIoResult XCan_send(XCan* can, const XCanFrame* frame, int32_t timeoutMs)
{
    (void)frame;
    (void)timeoutMs;
    if (!can) return XCanIoResult_Error;
    if (!can->m_open) { xcan_stub_error(can, XCanError_NotOpen); return XCanIoResult_Error; }
    xcan_stub_error(can, XCanError_Unsupported);
    return XCanIoResult_Error;
}

XCanIoResult XCan_receive(XCan* can, XCanFrame* frame, int32_t timeoutMs)
{
    (void)frame;
    (void)timeoutMs;
    if (!can) return XCanIoResult_Error;
    if (!can->m_open) { xcan_stub_error(can, XCanError_NotOpen); return XCanIoResult_Error; }
    xcan_stub_error(can, XCanError_Unsupported);
    return XCanIoResult_Error;
}

bool XCan_clearTransmitQueue(XCan* can)
{
    if (!can) return false;
    xcan_stub_error(can, XCanError_Unsupported);
    return false;
}

bool XCan_clearReceiveQueue(XCan* can)
{
    if (!can) return false;
    xcan_stub_error(can, XCanError_Unsupported);
    return false;
}

bool XCan_addFilter(XCan* can, const XCanFilter* filter, uint32_t* filterId)
{
    (void)filter;
    (void)filterId;
    if (!can) return false;
    xcan_stub_error(can, XCanError_Unsupported);
    return false;
}

bool XCan_removeFilter(XCan* can, uint32_t filterId)
{
    (void)filterId;
    if (!can) return false;
    xcan_stub_error(can, XCanError_Unsupported);
    return false;
}

void XCan_clearFilters(XCan* can)
{
    if (can) xcan_stub_error(can, XCanError_Unsupported);
}

bool XCan_setEventCallback(XCan* can, XCanEventCallback callback, void* userData)
{
    (void)callback;
    (void)userData;
    if (!can) return false;
    xcan_stub_error(can, XCanError_Unsupported);
    return false;
}

XCanProcessResult XCan_processEvents(XCan* can, int32_t timeoutMs)
{
    (void)timeoutMs;
    if (!can) return XCanProcessResult_Error;
    xcan_stub_error(can, XCanError_Unsupported);
    return XCanProcessResult_Error;
}

bool XCan_getStatus(const XCan* can, XCanStatus* status)
{
    if (!can || !status) return false;
    memset(status, 0, sizeof(*status));
    status->m_state = can->m_open ? XCanBusState_Stopped : XCanBusState_Unknown;
    status->m_receiveErrorCount = UINT32_MAX;
    status->m_transmitErrorCount = UINT32_MAX;
    status->m_rxPending = UINT32_MAX;
    status->m_txPending = UINT32_MAX;
    return true;
}

bool XCan_recoverBus(XCan* can)
{
    if (!can) return false;
    xcan_stub_error(can, XCanError_Unsupported);
    return false;
}

XCanFeatures XCan_features(const XCan* can)
{
    (void)can;
    return XCanFeature_None;
}

bool XCan_hasFeature(const XCan* can, XCanFeature feature)
{
    (void)can;
    (void)feature;
    return false;
}

XCanNativeHandle XCan_handle(const XCan* can)
{
    (void)can;
    return XCAN_INVALID_NATIVE_HANDLE;
}

XCanError XCan_lastError(const XCan* can)
{
    return can ? can->m_error : XCanError_InvalidArgument;
}

int32_t XCan_nativeError(const XCan* can)
{
    return can ? can->m_nativeError : 0;
}

void XCan_clearError(XCan* can)
{
    xcan_stub_error(can, XCanError_None);
}

const char* XCan_errorString(XCanError error)
{
    switch (error) {
    case XCanError_None: return "none";
    case XCanError_InvalidArgument: return "invalid-argument";
    case XCanError_NotOpen: return "not-open";
    case XCanError_AlreadyOpen: return "already-open";
    case XCanError_NotStarted: return "not-started";
    case XCanError_Unsupported: return "unsupported";
    case XCanError_Busy: return "busy";
    case XCanError_Timeout: return "timeout";
    case XCanError_PermissionDenied: return "permission-denied";
    case XCanError_BusOff: return "bus-off";
    default: return "unknown";
    }
}

#endif /* XCan 测试或真实平台后端未替换存根 */
