#include "XCanBusFrame.h"
#include "XMemory.h"
#include <string.h>

// =============== 创建与初始化 ===============

XCanBusFrame* XCanBusFrame_create(XCanBusFrame_FrameType type)
{
    XCanBusFrame* frame = (XCanBusFrame*)XMalloc_System(sizeof(XCanBusFrame));
    if (frame) {
        XCanBusFrame_init(frame, type);
    }
    return frame;
}

XCanBusFrame* XCanBusFrame_create_with_data(uint32_t identifier, const uint8_t* data, size_t size)
{
    XCanBusFrame* frame = (XCanBusFrame*)XMalloc_System(sizeof(XCanBusFrame));
    if (frame) {
        XCanBusFrame_init(frame, XCanBusFrame_DataFrame);
        XCanBusFrame_setFrameId(frame, identifier);
        XCanBusFrame_setPayload(frame, data, size);
    }
    return frame;
}

XCanBusFrame* XCanBusFrame_create_copy(const XCanBusFrame* other)
{
    if (!other) return NULL;
    XCanBusFrame* frame = (XCanBusFrame*)XMalloc_System(sizeof(XCanBusFrame));
    if (frame) {
        memcpy(frame, other, sizeof(XCanBusFrame));
        // 深拷贝负载数据
        if (other->m_load) {
            frame->m_load = XByteArray_create_copy(other->m_load);
        }
    }
    return frame;
}

void XCanBusFrame_init(XCanBusFrame* frame, XCanBusFrame_FrameType type)
{
    if (!frame) return;
    memset(frame, 0, sizeof(XCanBusFrame));
    frame->m_isValidFrameId = 1;
    frame->m_version = 2; // Qt_5_10
    XCanBusFrame_setFrameType(frame, type);
    frame->m_load = NULL;
    frame->m_stamp.m_secs = 0;
    frame->m_stamp.m_usecs = 0;
}

void XCanBusFrame_deinit(XCanBusFrame* frame)
{
    if (!frame) return;
    if (frame->m_load) {
        XByteArray_delete_base(frame->m_load);
        frame->m_load = NULL;
    }
}

void XCanBusFrame_delete(XCanBusFrame* frame)
{
    if (!frame) return;
    XCanBusFrame_deinit(frame);
    XFree_System(frame);
}

// =============== 帧属性访问 ===============

bool XCanBusFrame_isValid(const XCanBusFrame* frame)
{
    if (!frame) return false;

    if (frame->m_format == XCanBusFrame_InvalidFrame)
        return false;

    // 非扩展帧但 ID 超过 11 位
    if (!frame->m_isExtendedFrame && (frame->m_canId & 0x1FFFF800U))
        return false;

    if (!frame->m_isValidFrameId)
        return false;

    // 检查负载长度
    size_t length = frame->m_load ? XByteArray_size_base(frame->m_load) : 0;
    if (frame->m_isFlexibleDataRate) {
        if (frame->m_format == XCanBusFrame_RemoteRequestFrame)
            return false;
        return length <= 8 || length == 12 || length == 16 || length == 20
                || length == 24 || length == 32 || length == 48 || length == 64;
    }

    return length <= 8;
}

XCanBusFrame_FrameType XCanBusFrame_frameType(const XCanBusFrame* frame)
{
    if (!frame) return XCanBusFrame_InvalidFrame;
    switch (frame->m_format) {
    case 0x1: return XCanBusFrame_DataFrame;
    case 0x2: return XCanBusFrame_ErrorFrame;
    case 0x3: return XCanBusFrame_RemoteRequestFrame;
    case 0x4: return XCanBusFrame_InvalidFrame;
    default:  return XCanBusFrame_UnknownFrame;
    }
}

void XCanBusFrame_setFrameType(XCanBusFrame* frame, XCanBusFrame_FrameType type)
{
    if (!frame) return;
    switch (type) {
    case XCanBusFrame_DataFrame:          frame->m_format = 0x1; return;
    case XCanBusFrame_ErrorFrame:         frame->m_format = 0x2; return;
    case XCanBusFrame_RemoteRequestFrame: frame->m_format = 0x3; return;
    case XCanBusFrame_UnknownFrame:       frame->m_format = 0x0; return;
    case XCanBusFrame_InvalidFrame:       frame->m_format = 0x4; return;
    }
}

bool XCanBusFrame_hasExtendedFrameFormat(const XCanBusFrame* frame)
{
    return frame ? (frame->m_isExtendedFrame & 0x1) : false;
}

void XCanBusFrame_setExtendedFrameFormat(XCanBusFrame* frame, bool isExtended)
{
    if (!frame) return;
    frame->m_isExtendedFrame = (isExtended & 0x1);
}

uint32_t XCanBusFrame_frameId(const XCanBusFrame* frame)
{
    if (!frame) return 0;
    if (frame->m_format == XCanBusFrame_ErrorFrame)
        return 0;
    return (frame->m_canId & 0x1FFFFFFFU);
}

void XCanBusFrame_setFrameId(XCanBusFrame* frame, uint32_t newFrameId)
{
    if (!frame) return;
    if (newFrameId < 0x20000000U) {
        frame->m_isValidFrameId = 1;
        frame->m_canId = newFrameId;
        // 如果 ID 超过 11 位，自动设置扩展帧格式
        frame->m_isExtendedFrame = frame->m_isExtendedFrame || ((newFrameId & 0x1FFFF800U) != 0);
    } else {
        frame->m_isValidFrameId = 0;
        frame->m_canId = 0;
    }
}

XByteArray* XCanBusFrame_payload(const XCanBusFrame* frame)
{
    if (!frame) return NULL;
    if (frame->m_load) {
        return XByteArray_create_copy(frame->m_load);
    }
    return XByteArray_create();
}

const XByteArray* XCanBusFrame_payload_const(const XCanBusFrame* frame)
{
    return frame ? frame->m_load : NULL;
}

void XCanBusFrame_setPayload(XCanBusFrame* frame, const uint8_t* data, size_t size)
{
    if (!frame) return;

    if (frame->m_load) {
        XByteArray_delete_base(frame->m_load);
        frame->m_load = NULL;
    }

    if (data && size > 0) {
        frame->m_load = XByteArray_create();
        XByteArray_append_2(frame->m_load, data, size);
        if (size > 8)
            frame->m_isFlexibleDataRate = 0x1;
    }
}

void XCanBusFrame_setPayload_from_array(XCanBusFrame* frame, const XByteArray* data)
{
    if (!frame) return;

    if (frame->m_load) {
        XByteArray_delete_base(frame->m_load);
        frame->m_load = NULL;
    }

    if (data) {
        frame->m_load = XByteArray_create_copy(data);
        if (XByteArray_size_base(data) > 8)
            frame->m_isFlexibleDataRate = 0x1;
    }
}

XCanBusFrame_TimeStamp XCanBusFrame_timeStamp(const XCanBusFrame* frame)
{
    XCanBusFrame_TimeStamp ts = {0, 0};
    if (frame) {
        ts = frame->m_stamp;
    }
    return ts;
}

void XCanBusFrame_setTimeStamp(XCanBusFrame* frame, XCanBusFrame_TimeStamp ts)
{
    if (!frame) return;
    frame->m_stamp = ts;
}

// =============== 错误帧相关 ===============

uint32_t XCanBusFrame_error(const XCanBusFrame* frame)
{
    if (!frame) return XCanBusFrame_NoError;
    if (frame->m_format != XCanBusFrame_ErrorFrame)
        return XCanBusFrame_NoError;
    return (frame->m_canId & 0x1FFFFFFFU);
}

void XCanBusFrame_setError(XCanBusFrame* frame, uint32_t err)
{
    if (!frame) return;
    if (frame->m_format != XCanBusFrame_ErrorFrame)
        return;
    frame->m_canId = (err & XCanBusFrame_AnyError);
}

// =============== CAN FD 相关 ===============

bool XCanBusFrame_hasFlexibleDataRateFormat(const XCanBusFrame* frame)
{
    return frame ? (frame->m_isFlexibleDataRate & 0x1) : false;
}

void XCanBusFrame_setFlexibleDataRateFormat(XCanBusFrame* frame, bool isFlexibleData)
{
    if (!frame) return;
    frame->m_isFlexibleDataRate = (isFlexibleData & 0x1);
    if (!isFlexibleData) {
        frame->m_isBitrateSwitch = 0x0;
        frame->m_isErrorStateIndicator = 0x0;
    }
}

bool XCanBusFrame_hasBitrateSwitch(const XCanBusFrame* frame)
{
    return frame ? (frame->m_isBitrateSwitch & 0x1) : false;
}

void XCanBusFrame_setBitrateSwitch(XCanBusFrame* frame, bool bitrateSwitch)
{
    if (!frame) return;
    frame->m_isBitrateSwitch = (bitrateSwitch & 0x1);
    if (bitrateSwitch)
        frame->m_isFlexibleDataRate = 0x1;
}

bool XCanBusFrame_hasErrorStateIndicator(const XCanBusFrame* frame)
{
    return frame ? (frame->m_isErrorStateIndicator & 0x1) : false;
}

void XCanBusFrame_setErrorStateIndicator(XCanBusFrame* frame, bool errorStateIndicator)
{
    if (!frame) return;
    frame->m_isErrorStateIndicator = (errorStateIndicator & 0x1);
    if (errorStateIndicator)
        frame->m_isFlexibleDataRate = 0x1;
}

bool XCanBusFrame_hasLocalEcho(const XCanBusFrame* frame)
{
    return frame ? (frame->m_isLocalEcho & 0x1) : false;
}

void XCanBusFrame_setLocalEcho(XCanBusFrame* frame, bool localEcho)
{
    if (!frame) return;
    frame->m_isLocalEcho = (localEcho & 0x1);
}

// =============== 字符串表示 ===============

XString* XCanBusFrame_toString(const XCanBusFrame* frame)
{
    if (!frame) return XString_create_utf8("(NULL)");

    XCanBusFrame_FrameType type = XCanBusFrame_frameType(frame);

    switch (type) {
    case XCanBusFrame_InvalidFrame:
        return XString_create_utf8("(Invalid)");
    case XCanBusFrame_ErrorFrame:
        return XString_create_utf8("(Error)");
    case XCanBusFrame_UnknownFrame:
        return XString_create_utf8("(Unknown)");
    default:
        break;
    }

    // 构建字符串
    XString result;
    XString_init(&result);

    // 帧 ID 部分
    uint32_t id = XCanBusFrame_frameId(frame);
    bool ext = XCanBusFrame_hasExtendedFrameFormat(frame);

    /* 使用 XString_create_fmt_utf8 创建临时格式化字符串并追加 */
    if (!ext) {
        XString_append_utf8(&result, "     ");
    }
    {
        XString* tmp = XString_create_fmt_utf8(ext ? "%08X" : "%03X", id);
        if (tmp) {
            XString_append(&result, tmp);
            XString_delete_base(tmp);
        }
    }

    // 长度部分
    size_t payloadSize = frame->m_load ? XByteArray_size_base(frame->m_load) : 0;
    bool fd = XCanBusFrame_hasFlexibleDataRateFormat(frame);
    if (fd) {
        XString_append_utf8(&result, "  ");
    } else {
        XString_append_utf8(&result, "   ");
    }
    {
        XString* tmp = XString_create_fmt_utf8("[%zu]", payloadSize);
        if (tmp) {
            XString_append(&result, tmp);
            XString_delete_base(tmp);
        }
    }

    // 数据部分
    if (type == XCanBusFrame_RemoteRequestFrame) {
        XString_append_utf8(&result, "  Remote Request");
    } else if (frame->m_load && payloadSize > 0) {
        XString_append_utf8(&result, "  ");
        const uint8_t* data = XByteArray_data(frame->m_load);
        for (size_t i = 0; i < payloadSize; i++) {
            if (i > 0) XString_append_utf8(&result, " ");
            XString* tmp = XString_create_fmt_utf8("%02X", data[i]);
            if (tmp) {
                XString_append(&result, tmp);
                XString_delete_base(tmp);
            }
        }
    }

    XString* ret = XString_create_move(&result);
    XString_deinit_base(&result);
    return ret;
}

// =============== 时间戳工具函数 ===============

XCanBusFrame_TimeStamp XCanBusFrame_TimeStamp_fromMicroSeconds(int64_t usec)
{
    XCanBusFrame_TimeStamp ts;
    ts.m_secs = usec / 1000000;
    ts.m_usecs = usec % 1000000;
    return ts;
}

int64_t XCanBusFrame_TimeStamp_seconds(XCanBusFrame_TimeStamp ts)
{
    return ts.m_secs;
}

int64_t XCanBusFrame_TimeStamp_microSeconds(XCanBusFrame_TimeStamp ts)
{
    return ts.m_usecs;
}
