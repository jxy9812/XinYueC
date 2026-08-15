/**
 * @file       XHttp2Connection.c
 * @brief      HTTP/2 多路复用连接状态机实现。
 */

#include "XHttp2Connection.h"

#include "XHttp2Frame.h"
#include "XMemory.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if XPROTOCOL_ON
#if XHTTP_ON

#define XHTTP2_CONNECTION_PROTOCOL_ERROR UINT32_C(0x1)
#define XHTTP2_CONNECTION_FLOW_CONTROL_ERROR UINT32_C(0x3)
#define XHTTP2_CONNECTION_STREAM_CLOSED UINT32_C(0x5)
#define XHTTP2_CONNECTION_FRAME_SIZE_ERROR UINT32_C(0x6)
#define XHTTP2_CONNECTION_REFUSE_STREAM UINT32_C(0x7)
#define XHTTP2_CONNECTION_COMPRESSION_ERROR UINT32_C(0x9)
#define XHTTP2_CONNECTION_ENHANCE_YOUR_CALM UINT32_C(0xb)

static bool xhttp2_connection_flush(XHttp2Connection* self);

static bool xhttp2_connection_append(XByteArray* target, const void* data, size_t size)
{
    return target && (!size || (data && XByteArray_push_back_2((XVector*)target, data, size)));
}

static void xhttp2_connection_stream_delete(XHttp2Stream* stream)
{
    if (!stream)
        return;
    if (stream->m_reply && stream->m_replyOwned) XClass_delete_base((XClass*)stream->m_reply);
    if (stream->m_wire) XClass_delete_base((XClass*)stream->m_wire);
    if (stream->m_headerBlock) XClass_delete_base((XClass*)stream->m_headerBlock);
    XFree_System(stream);
}

static void xhttp2_connection_clear_streams(XHttp2Connection* self)
{
    if (!self || !self->m_streams)
        return;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_streams); ++i) {
        XHttp2Stream** slot = (XHttp2Stream**)XVector_at_base(self->m_streams, (int64_t)i);
        if (slot) xhttp2_connection_stream_delete(*slot);
    }
    XVector_delete_base((XClass*)self->m_streams);
    self->m_streams = NULL;
}

static void xhttp2_connection_clear_pushed(XHttp2Connection* self)
{
    if (!self || !self->m_pushedReplies)
        return;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_pushedReplies); ++i) {
        XHttpReply** slot = (XHttpReply**)XVector_at_base(self->m_pushedReplies, (int64_t)i);
        if (slot && *slot) XClass_delete_base((XClass*)*slot);
    }
    XVector_delete_base((XClass*)self->m_pushedReplies);
    self->m_pushedReplies = NULL;
}

static XHttp2Stream* xhttp2_connection_find_stream(const XHttp2Connection* self, uint32_t id)
{
    if (!self || !id || !self->m_streams)
        return NULL;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_streams); ++i) {
        XHttp2Stream** slot = (XHttp2Stream**)XVector_at_base(self->m_streams, (int64_t)i);
        if (slot && *slot && (*slot)->m_id == id)
            return *slot;
    }
    return NULL;
}

/* 客户端流号为单调递增奇数；已分配但已回收的奇数流仍属于 closed，而不是 idle。 */
static bool xhttp2_connection_is_closed_local_stream(const XHttp2Connection* self, uint32_t id)
{
    uint32_t next;
    if (!self || !self->m_session || id == 0 || (id & 1u) == 0)
        return false;
    next = self->m_session->m_nextStreamId;
    return next == 0 || id < next;
}

static void xhttp2_connection_close_stream(XHttp2Connection* self, XHttp2Stream* stream)
{
    if (!self || !stream || stream->m_state == XHttp2Stream_Closed)
        return;
    stream->m_state = XHttp2Stream_Closed;
    if (!stream->m_pushed)
        XHttp2ClientSession_markStreamClosed(self->m_session);
}

/* 将本端发送 END_STREAM 后的状态转换与 RFC 7540 5.1.1 保持一致。 */
static bool xhttp2_connection_local_end_stream(XHttp2Connection* self, XHttp2Stream* stream)
{
    if (!self || !stream)
        return false;
    if (stream->m_state == XHttp2Stream_Open) {
        stream->m_state = XHttp2Stream_HalfClosedLocal;
        return true;
    }
    if (stream->m_state == XHttp2Stream_HalfClosedRemote) {
        xhttp2_connection_close_stream(self, stream);
        return true;
    }
    return false;
}

/* 将对端发送 END_STREAM 后的状态转换与 RFC 7540 5.1.1 保持一致。 */
static bool xhttp2_connection_remote_end_stream(XHttp2Connection* self, XHttp2Stream* stream)
{
    if (!self || !stream)
        return false;
    if (stream->m_state == XHttp2Stream_Open) {
        stream->m_state = XHttp2Stream_HalfClosedRemote;
        return true;
    }
    if (stream->m_state == XHttp2Stream_HalfClosedLocal ||
        stream->m_state == XHttp2Stream_ReservedRemote) {
        xhttp2_connection_close_stream(self, stream);
        return true;
    }
    return false;
}

static bool xhttp2_connection_append_frame(XHttp2Connection* self, uint8_t type,
                                           uint8_t flags, uint32_t streamId,
                                           const XByteArray* payload)
{
    XHttp2Frame* frame;
    XByteArray* wire;
    bool result;
    if (!self || !self->m_outgoing)
        return false;
    frame = XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, flags, streamId, payload);
    wire = frame ? XHttp2Frame_toByteArray(frame) : NULL;
    result = wire && xhttp2_connection_append(self->m_outgoing, XByteArray_constData(wire),
                                               XContainer_size_base((const XContainer*)wire));
    if (wire) XClass_delete_base((XClass*)wire);
    if (frame) XClass_delete_base((XClass*)frame);
    return result;
}

static bool xhttp2_connection_append_u32_frame(XHttp2Connection* self, uint8_t type,
                                               uint32_t streamId, uint32_t value)
{
    uint8_t bytes[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16),
                        (uint8_t)(value >> 8), (uint8_t)value};
    XByteArray* payload = XByteArray_create_with_data((const char*)bytes, sizeof(bytes));
    bool result = payload && xhttp2_connection_append_frame(self, type, 0, streamId, payload);
    if (payload) XClass_delete_base((XClass*)payload);
    return result;
}

/* 将只影响单个活动流的协议错误转换为 RST_STREAM，不能牵连同连接的其他请求流。 */
static bool xhttp2_connection_stream_error(XHttp2Connection* self, XHttp2Stream* stream,
                                           uint32_t error, const char* message)
{
    if (!self || !stream)
        return false;
    if (stream->m_state == XHttp2Stream_Closed)
        return true;
    if (!xhttp2_connection_append_u32_frame(self, XHttp2Frame_RstStream,
                                            stream->m_id, error))
        return false;
    if (stream->m_reply && !XHttpReply_isFinished(stream->m_reply))
        XHttpReply_setError(stream->m_reply, XHttpReply_ProtocolInvalidOperationError,
                            message ? message : "HTTP/2 流协议错误");
    xhttp2_connection_close_stream(self, stream);
    return true;
}

static bool xhttp2_connection_send_goaway(XHttp2Connection* self, uint32_t error)
{
    uint32_t last = self ? self->m_lastIncomingStreamId : 0;
    uint8_t bytes[8] = {(uint8_t)(last >> 24), (uint8_t)(last >> 16),
                        (uint8_t)(last >> 8), (uint8_t)last,
                        (uint8_t)(error >> 24), (uint8_t)(error >> 16),
                        (uint8_t)(error >> 8), (uint8_t)error};
    XByteArray* payload;
    bool result;
    if (!self || self->m_goAwaySent)
        return self != NULL;
    payload = XByteArray_create_with_data((const char*)bytes, sizeof(bytes));
    result = payload && xhttp2_connection_append_frame(self, XHttp2Frame_GoAway, 0, 0, payload);
    if (payload) XClass_delete_base((XClass*)payload);
    if (result) self->m_goAwaySent = true;
    self->m_goingAway = true;
    return result;
}

/* 对端 RST_STREAM 的错误码按 Qt 的 HTTP/2 到网络错误类别映射到响应对象。 */
static void xhttp2_connection_set_rst_error(XHttpReply* reply, uint32_t error)
{
    XHttpReply_NetworkError replyError = XHttpReply_ProtocolInvalidOperationError;
    const char* message = "HTTP/2 对端重置请求流";
    if (!reply)
        return;
    switch (error) {
    case 0:
        replyError = XHttpReply_RemoteHostClosedError;
        message = "HTTP/2 对端在响应完成前重置流";
        break;
    case 2:
        replyError = XHttpReply_InternalServerError;
        message = "HTTP/2 对端内部错误导致流重置";
        break;
    case 4:
        replyError = XHttpReply_TimeoutError;
        message = "HTTP/2 SETTINGS ACK 超时导致流重置";
        break;
    case 10:
        replyError = XHttpReply_UnknownNetworkError;
        message = "HTTP/2 CONNECT 隧道异常关闭";
        break;
    case 11:
        replyError = XHttpReply_UnknownServerError;
        message = "HTTP/2 对端拒绝当前流的行为或负载";
        break;
    case 12:
        replyError = XHttpReply_ContentAccessDenied;
        message = "HTTP/2 传输安全属性不足";
        break;
    default:
        break;
    }
    XHttpReply_setError(reply, replyError, message);
}

static bool xhttp2_connection_fail(XHttp2Connection* self, uint32_t error)
{
    if (!self)
        return false;
    self->m_failed = true;
    xhttp2_connection_send_goaway(self, error);
    for (size_t i = 0; self->m_streams && i < XContainer_size_base((const XContainer*)self->m_streams); ++i) {
        XHttp2Stream** slot = (XHttp2Stream**)XVector_at_base(self->m_streams, (int64_t)i);
        if (slot && *slot && (*slot)->m_reply && !XHttpReply_isFinished((*slot)->m_reply))
            XHttpReply_setError((*slot)->m_reply, XHttpReply_ProtocolInvalidOperationError,
                                "HTTP/2 连接协议错误");
    }
    return false;
}

/* 将 GOAWAY 的 HTTP/2 错误码映射为与 Qt QNetworkReply 相同类别的错误。 */
static void xhttp2_connection_set_goaway_error(XHttpReply* reply, uint32_t error)
{
    XHttpReply_NetworkError replyError = XHttpReply_ProtocolInvalidOperationError;
    const char* message = "HTTP/2 GOAWAY 协议错误";
    if (!reply)
        return;
    switch (error) {
    case 0:
        replyError = XHttpReply_ContentReSendError;
        message = "服务器在建立请求流前停止接收新流";
        break;
    case 2:
        replyError = XHttpReply_InternalServerError;
        message = "HTTP/2 对端内部错误";
        break;
    case 4:
        replyError = XHttpReply_TimeoutError;
        message = "HTTP/2 SETTINGS ACK 超时";
        break;
    case 10:
        replyError = XHttpReply_UnknownNetworkError;
        message = "HTTP/2 CONNECT 隧道异常关闭";
        break;
    case 11:
        replyError = XHttpReply_UnknownServerError;
        message = "HTTP/2 对端拒绝当前行为或负载";
        break;
    case 12:
        replyError = XHttpReply_ContentAccessDenied;
        message = "HTTP/2 传输安全属性不足";
        break;
    default:
        break;
    }
    XHttpReply_setError(reply, replyError, message);
}

static bool xhttp2_connection_payload(const XHttp2Frame* frame, size_t prefix,
                                      const uint8_t** data, size_t* size)
{
    const XByteArray* payload = XHttp2Frame_payload_const(frame);
    const uint8_t* bytes = payload ? XByteArray_constData((XByteArray*)payload) : NULL;
    size_t total = payload ? XContainer_size_base((const XContainer*)payload) : 0;
    size_t offset = 0;
    size_t padding = 0;
    if (!frame || !data || !size)
        return false;
    if (XHttp2Frame_flags(frame) & XHttp2Frame_Padded) {
        if (!total || !bytes) return false;
        padding = bytes[0];
        offset = 1;
    }
    if (prefix > total - offset || padding > total - offset - prefix)
        return false;
    offset += prefix;
    *data = bytes ? bytes + offset : NULL;
    *size = total - offset - padding;
    return *size == 0 || *data != NULL;
}

static bool xhttp2_connection_read_u32(const XByteArray* payload, size_t offset, uint32_t* value)
{
    const uint8_t* data;
    size_t size = payload ? XContainer_size_base((const XContainer*)payload) : 0;
    if (!payload || !value || offset > size || size - offset < 4 ||
        !(data = XByteArray_constData((XByteArray*)payload)))
        return false;
    *value = ((uint32_t)data[offset] << 24) | ((uint32_t)data[offset + 1] << 16) |
             ((uint32_t)data[offset + 2] << 8) | data[offset + 3];
    return true;
}

static bool xhttp2_connection_finish_headers(XHttp2Connection* self, XHttp2Stream* stream)
{
    XHttp2HeaderList* headers;
    bool result;
    bool headersAccepted;
    if (!self || !stream || !stream->m_headerBlock)
        return false;
    headers = XHttp2HeaderDecoder_decode(self->m_decoder,
        XByteArray_constData(stream->m_headerBlock),
        XContainer_size_base((const XContainer*)stream->m_headerBlock));
    if (!headers) {
        XByteArray_clear_base((XContainer*)stream->m_headerBlock);
        stream->m_headersActive = false;
        self->m_continuationStreamId = 0;
        self->m_continuationPushPromise = false;
        self->m_promisedStreamId = 0;
        return xhttp2_connection_fail(self, XHTTP2_CONNECTION_COMPRESSION_ERROR);
    }
    if (self->m_continuationPushPromise) {
        result = XHttp2Configuration_serverPushEnabled(self->m_configuration) ||
                 xhttp2_connection_stream_error(self, stream,
                                               XHTTP2_CONNECTION_REFUSE_STREAM,
                                               "HTTP/2 服务端推送已被客户端拒绝");
    } else {
        headersAccepted = stream->m_reply &&
            XHttpReply_feedHttp2Headers(stream->m_reply, headers, stream->m_headerEndStream);
        result = headersAccepted;
        if (!headersAccepted)
            result = xhttp2_connection_stream_error(self, stream,
                                                    XHTTP2_CONNECTION_PROTOCOL_ERROR,
                                                    "HTTP/2 响应头字段无效");
        if (headersAccepted && stream->m_headerEndStream) {
            if (stream->m_pushed && stream->m_reply && self->m_pushedReplies &&
                XVector_push_back_1_base(self->m_pushedReplies, &stream->m_reply))
                stream->m_reply = NULL;
            if (!xhttp2_connection_remote_end_stream(self, stream))
                result = xhttp2_connection_stream_error(self, stream,
                                                        XHTTP2_CONNECTION_PROTOCOL_ERROR,
                                                        "HTTP/2 HEADERS 的流状态无效");
        } else if (headersAccepted && stream->m_state == XHttp2Stream_ReservedRemote) {
            stream->m_state = XHttp2Stream_HalfClosedLocal;
        }
    }
    if (headers) XClass_delete_base((XClass*)headers);
    XByteArray_clear_base((XContainer*)stream->m_headerBlock);
    stream->m_headersActive = false;
    self->m_continuationStreamId = 0;
    self->m_continuationPushPromise = false;
    self->m_promisedStreamId = 0;
    return result;
}

static bool xhttp2_connection_process_settings(XHttp2Connection* self, const XByteArray* payload)
{
    const uint8_t* data = payload ? XByteArray_constData((XByteArray*)payload) : NULL;
    size_t size = payload ? XContainer_size_base((const XContainer*)payload) : 0;
    if ((size && !data) || size % 6)
        return false;
    for (size_t offset = 0; offset < size; offset += 6) {
        uint16_t id = (uint16_t)(((uint16_t)data[offset] << 8) | data[offset + 1]);
        uint32_t value = ((uint32_t)data[offset + 2] << 24) | ((uint32_t)data[offset + 3] << 16) |
                         ((uint32_t)data[offset + 4] << 8) | data[offset + 5];
        if (id == 1 && !XHttp2ClientSession_setPeerHeaderTableSize(self->m_session, value)) return false;
        /* 客户端接收的服务端 SETTINGS_ENABLE_PUSH 只能为 0。 */
        if (id == 2 && value != 0) return false;
        if (id == 3 && !XHttp2ClientSession_setPeerMaxConcurrentStreams(self->m_session, value)) return false;
        if (id == 4) {
            int64_t delta = (int64_t)value - (int64_t)self->m_peerInitialWindowSize;
            if (value > INT32_MAX) return false;
            for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_streams); ++i) {
                XHttp2Stream** slot = (XHttp2Stream**)XVector_at_base(self->m_streams, (int64_t)i);
                if (!slot || !*slot || (*slot)->m_state == XHttp2Stream_Closed)
                    continue;
                if ((*slot)->m_sendWindow + delta > INT32_MAX) {
                    /* Qt 对单流窗口溢出发送 RST_STREAM，而不是关闭整个连接。 */
                    if (!xhttp2_connection_stream_error(self, *slot,
                                                        XHTTP2_CONNECTION_PROTOCOL_ERROR,
                                                        "HTTP/2 SETTINGS 导致流窗口溢出"))
                        return false;
                    continue;
                }
                (*slot)->m_sendWindow += delta;
            }
            self->m_peerInitialWindowSize = value;
        }
        if (id == 5) {
            if (value < XHttp2Configuration_MinFrameSize || value > XHttp2Configuration_MaxFrameSize)
                return false;
            self->m_peerMaxFrameSize = value;
            if (!XHttp2ClientSession_setPeerMaxFrameSize(self->m_session, value)) return false;
        }
        if (id == 6 && !XHttp2ClientSession_setPeerMaxHeaderListSize(self->m_session, value)) return false;
    }
    return true;
}

/* 将各流的 HEADERS/DATA 按连接与流窗口交错写入输出队列。 */
static bool xhttp2_connection_flush(XHttp2Connection* self)
{
    if (!self || self->m_failed)
        return false;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_streams); ++i) {
        XHttp2Stream** slot = (XHttp2Stream**)XVector_at_base(self->m_streams, (int64_t)i);
        XHttp2Stream* stream = slot ? *slot : NULL;
        if (!stream || !stream->m_wire || stream->m_state == XHttp2Stream_Closed)
            continue;
        while (stream->m_writeOffset < XContainer_size_base((const XContainer*)stream->m_wire)) {
            const uint8_t* wire = XByteArray_constData(stream->m_wire);
            size_t total = XContainer_size_base((const XContainer*)stream->m_wire);
            size_t consumed = 0;
            XHttp2Frame* frame;
            if (!wire) return false;
            if (stream->m_writeOffset == 0 && total >= sizeof(XHttp2Frame_ClientPreface) - 1 &&
                XHttp2Frame_hasClientPreface(wire, total)) {
                if (!xhttp2_connection_append(self->m_outgoing, wire, sizeof(XHttp2Frame_ClientPreface) - 1))
                    return false;
                stream->m_writeOffset += sizeof(XHttp2Frame_ClientPreface) - 1;
                self->m_waitingForSettingsAck = true;
                continue;
            }
            frame = XHttp2Frame_fromBytes(wire + stream->m_writeOffset,
                                          total - stream->m_writeOffset, &consumed);
            if (!frame || !consumed) { if (frame) XClass_delete_base((XClass*)frame); return false; }
            if (XHttp2Frame_type(frame) != XHttp2Frame_Data) {
                XByteArray* encoded = XHttp2Frame_toByteArray(frame);
                bool ok = encoded && xhttp2_connection_append(self->m_outgoing,
                    XByteArray_constData(encoded), XContainer_size_base((const XContainer*)encoded));
                if (ok && XHttp2Frame_type(frame) == XHttp2Frame_Settings &&
                    !self->m_initialSessionWindowSent) {
                    uint32_t delta = self->m_sessionRecvTarget -
                                     (uint32_t)self->m_sessionRecvWindow;
                    self->m_initialSessionWindowSent = true;
                    if (delta != 0 && !xhttp2_connection_append_u32_frame(
                            self, XHttp2Frame_WindowUpdate, 0, delta))
                        ok = false;
                    else
                        self->m_sessionRecvWindow += delta;
                }
                if (XHttp2Frame_type(frame) == XHttp2Frame_Headers &&
                    (XHttp2Frame_flags(frame) & XHttp2Frame_EndStream) &&
                    !xhttp2_connection_local_end_stream(self, stream)) {
                    if (encoded) XClass_delete_base((XClass*)encoded);
                    XClass_delete_base((XClass*)frame);
                    return false;
                }
                if (encoded) XClass_delete_base((XClass*)encoded);
                XClass_delete_base((XClass*)frame);
                if (!ok) return false;
                stream->m_writeOffset += consumed;
                continue;
            }
            {
                const XByteArray* payload = XHttp2Frame_payload_const(frame);
                size_t payloadSize = payload ? XContainer_size_base((const XContainer*)payload) : 0;
                int64_t allowed = self->m_sessionSendWindow < stream->m_sendWindow ?
                                  self->m_sessionSendWindow : stream->m_sendWindow;
                size_t chunk;
                XByteArray* part;
                XHttp2Frame* out;
                XByteArray* encoded;
                uint8_t flags = 0;
                if (stream->m_dataOffset == payloadSize) {
                    stream->m_dataOffset = 0; stream->m_writeOffset += consumed;
                    XClass_delete_base((XClass*)frame); continue;
                }
                if (allowed <= 0) { XClass_delete_base((XClass*)frame); break; }
                chunk = payloadSize - stream->m_dataOffset;
                if ((int64_t)chunk > allowed) chunk = (size_t)allowed;
                if (chunk > self->m_peerMaxFrameSize) chunk = self->m_peerMaxFrameSize;
                part = XByteArray_create_with_data((const char*)XByteArray_constData((XByteArray*)payload) +
                                                    stream->m_dataOffset, chunk);
                if ((XHttp2Frame_flags(frame) & XHttp2Frame_EndStream) &&
                    stream->m_dataOffset + chunk == payloadSize) flags = XHttp2Frame_EndStream;
                out = part ? XHttp2Frame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHttp2Frame_Data, flags, stream->m_id, part) : NULL;
                encoded = out ? XHttp2Frame_toByteArray(out) : NULL;
                if (!part || !out || !encoded || !xhttp2_connection_append(self->m_outgoing,
                    XByteArray_constData(encoded), XContainer_size_base((const XContainer*)encoded))) {
                    if (encoded) XClass_delete_base((XClass*)encoded); if (out) XClass_delete_base((XClass*)out);
                    if (part) XClass_delete_base((XClass*)part); XClass_delete_base((XClass*)frame); return false;
                }
                self->m_sessionSendWindow -= (int64_t)chunk; stream->m_sendWindow -= (int64_t)chunk;
                stream->m_dataOffset += chunk;
                if ((flags & XHttp2Frame_EndStream) &&
                    !xhttp2_connection_local_end_stream(self, stream)) {
                    if (encoded) XClass_delete_base((XClass*)encoded);
                    if (out) XClass_delete_base((XClass*)out);
                    if (part) XClass_delete_base((XClass*)part);
                    XClass_delete_base((XClass*)frame);
                    return false;
                }
                if (encoded) XClass_delete_base((XClass*)encoded); if (out) XClass_delete_base((XClass*)out);
                if (part) XClass_delete_base((XClass*)part); XClass_delete_base((XClass*)frame);
            }
        }
    }
    return true;
}

static void VXHttp2Connection_deinit(XHttp2Connection* self)
{
    if (!self) return;
    xhttp2_connection_clear_streams(self);
    xhttp2_connection_clear_pushed(self);
    if (self->m_session) XClass_delete_base((XClass*)self->m_session);
    if (self->m_decoder) XClass_delete_base((XClass*)self->m_decoder);
    if (self->m_configuration) XClass_delete_base((XClass*)self->m_configuration);
    if (self->m_input) XClass_delete_base((XClass*)self->m_input);
    if (self->m_outgoing) XClass_delete_base((XClass*)self->m_outgoing);
    self->m_session = NULL; self->m_decoder = NULL; self->m_configuration = NULL;
    self->m_input = NULL; self->m_outgoing = NULL;
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

XVtable* XHttp2Connection_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHttp2Connection)
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttp2Connection_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XHttp2Connection);
    return XVTABLE_DEFAULT;
}

void XHttp2Connection_init(XHttp2Connection* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttp2Connection);
    self->m_configuration = XHttp2Configuration_create();
    self->m_session = XHttp2ClientSession_create();
    self->m_decoder = XHttp2HeaderDecoder_create();
    self->m_streams = XVector_create(sizeof(XHttp2Stream*));
    self->m_pushedReplies = XVector_create(sizeof(XHttpReply*));
    self->m_input = XByteArray_create();
    self->m_outgoing = XByteArray_create();
    self->m_sessionSendWindow = 65535;
    self->m_peerInitialWindowSize = 65535;
    self->m_peerMaxFrameSize = XHttp2Configuration_MinFrameSize;
    self->m_remoteLastStreamId = UINT32_C(0x7fffffff);
    self->m_sessionRecvTarget = self->m_configuration ?
        XHttp2Configuration_sessionReceiveWindowSize(self->m_configuration) : 65535;
    self->m_streamRecvTarget = self->m_configuration ?
        XHttp2Configuration_streamReceiveWindowSize(self->m_configuration) : 65535;
    if (self->m_sessionRecvTarget < 65535)
        self->m_sessionRecvTarget = 65535;
    self->m_sessionRecvWindow = 65535;
}

XHttp2Connection* XHttp2Connection_create_ex(XMemoryType memory,
                                              const XHttp2Configuration* configuration)
{
    XHttp2Connection* self = (XHttp2Connection*)XMemory_malloc(sizeof(XHttp2Connection), memory);
    XHttp2Configuration* copy;
    if (!self) return NULL;
    XHttp2Connection_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    if (!self->m_configuration || !self->m_session || !self->m_decoder || !self->m_streams ||
        !self->m_pushedReplies || !self->m_input || !self->m_outgoing) {
        XClass_delete_base((XClass*)self); return NULL;
    }
    if (!configuration) return self;
    copy = XHttp2Configuration_create_copy(configuration);
    if (!copy || !XHttp2ClientSession_setConfiguration(self->m_session, configuration)) {
        if (copy) XClass_delete_base((XClass*)copy); XClass_delete_base((XClass*)self); return NULL;
    }
    XClass_delete_base((XClass*)self->m_configuration);
    self->m_configuration = copy;
    self->m_sessionRecvTarget = XHttp2Configuration_sessionReceiveWindowSize(copy);
    self->m_streamRecvTarget = XHttp2Configuration_streamReceiveWindowSize(copy);
    if (self->m_sessionRecvTarget < 65535)
        self->m_sessionRecvTarget = 65535;
    self->m_sessionRecvWindow = 65535;
    return self;
}

static XHttpReply* xhttp2_connection_send_request(XHttp2Connection* self,
                                                   const XHttpRequest* request,
                                                   XHttpReply* reply,
                                                   bool replyOwned,
                                                   uint32_t* streamId)
{
    XHttp2Stream* stream;
    XByteArray* wire;
    uint32_t id = 0;
    if (!self || !request || !streamId || self->m_failed || self->m_goingAway)
        return NULL;
    wire = XHttp2ClientSession_encodeRequest(self->m_session, request, &id);
    if (!wire || !id) { if (wire) XClass_delete_base((XClass*)wire); return NULL; }
    stream = (XHttp2Stream*)XMalloc_System(sizeof(*stream));
    if (!stream) { XClass_delete_base((XClass*)wire); XHttp2ClientSession_markStreamClosed(self->m_session); return NULL; }
    memset(stream, 0, sizeof(*stream));
    stream->m_reply = reply ? reply : XHttpReply_create(request);
    stream->m_replyOwned = replyOwned;
    stream->m_headerBlock = XByteArray_create();
    stream->m_wire = wire;
    stream->m_id = id;
    stream->m_sendWindow = self->m_peerInitialWindowSize;
    stream->m_recvWindow = self->m_streamRecvTarget;
    stream->m_state = XHttp2Stream_Open;
    if (!stream->m_reply || !stream->m_headerBlock ||
        !XVector_push_back_1_base(self->m_streams, &stream)) {
        xhttp2_connection_stream_delete(stream); XHttp2ClientSession_markStreamClosed(self->m_session); return NULL;
    }
    *streamId = id;
    if (!xhttp2_connection_flush(self)) {
        XVector_remove_base(self->m_streams,
                            (int64_t)XContainer_size_base((const XContainer*)self->m_streams) - 1,
                            1);
        xhttp2_connection_stream_delete(stream);
        XHttp2ClientSession_markStreamClosed(self->m_session);
        return NULL;
    }
    return stream->m_reply;
}

XHttpReply* XHttp2Connection_sendRequest(XHttp2Connection* self,
                                         const XHttpRequest* request, uint32_t* streamId)
{
    return xhttp2_connection_send_request(self, request, NULL, true, streamId);
}

bool XHttp2Connection_sendRequestReply(XHttp2Connection* self,
                                       XHttpReply* reply, uint32_t* streamId)
{
    const XHttpRequest* request = reply ? XHttpReply_request_const(reply) : NULL;
    return xhttp2_connection_send_request(self, request, reply, false, streamId) == reply;
}

bool XHttp2Connection_adoptUpgradedRequest(XHttp2Connection* self,
                                           XHttpReply* reply)
{
    XHttp2Stream* stream;
    XByteArray* wire;
    if (!self || !reply || self->m_failed || self->m_goingAway ||
        XHttp2ClientSession_isStarted(self->m_session) ||
        XHttp2Connection_streamCount(self) != 0)
        return false;
    wire = XHttp2ClientSession_start(self->m_session);
    if (!wire || !XHttp2ClientSession_adoptUpgradedStream(self->m_session)) {
        if (wire) XClass_delete_base((XClass*)wire);
        return false;
    }
    stream = (XHttp2Stream*)XMalloc_System(sizeof(*stream));
    if (!stream) {
        XClass_delete_base((XClass*)wire);
        XHttp2ClientSession_markStreamClosed(self->m_session);
        return false;
    }
    memset(stream, 0, sizeof(*stream));
    stream->m_reply = reply;
    stream->m_wire = wire;
    stream->m_headerBlock = XByteArray_create();
    stream->m_id = 1;
    stream->m_sendWindow = self->m_peerInitialWindowSize;
    stream->m_recvWindow = self->m_streamRecvTarget;
    stream->m_state = XHttp2Stream_HalfClosedLocal;
    if (!stream->m_headerBlock || !XVector_push_back_1_base(self->m_streams, &stream)) {
        xhttp2_connection_stream_delete(stream);
        XHttp2ClientSession_markStreamClosed(self->m_session);
        return false;
    }
    if (!xhttp2_connection_flush(self)) {
        XVector_remove_base(self->m_streams, 0, 1);
        xhttp2_connection_stream_delete(stream);
        XHttp2ClientSession_markStreamClosed(self->m_session);
        return false;
    }
    return true;
}

static bool xhttp2_connection_process_frame(XHttp2Connection* self, const XHttp2Frame* frame)
{
    uint8_t type = XHttp2Frame_type(frame), flags = XHttp2Frame_flags(frame);
    uint32_t id = XHttp2Frame_streamId(frame);
    XHttp2Stream* stream;
    const XByteArray* payload = XHttp2Frame_payload_const(frame);
    const uint8_t* data;
    size_t size;
    if (!XHttp2Frame_validateHeader(frame) || !XHttp2Frame_validatePayload(frame))
        return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
    if (type == XHttp2Frame_Settings) {
        if (id != 0) return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        if (flags & XHttp2Frame_Ack) {
            if (!self->m_waitingForSettingsAck) return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
            self->m_waitingForSettingsAck = false; return true;
        }
        if (!xhttp2_connection_process_settings(self, payload) ||
            !xhttp2_connection_append_frame(self, XHttp2Frame_Settings, XHttp2Frame_Ack, 0, NULL))
            return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        return xhttp2_connection_flush(self);
    }
    if (type == XHttp2Frame_Ping) {
        if (id != 0 || !payload || XContainer_size_base((const XContainer*)payload) != 8)
            return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        /* Qt 对未发起探测的 ACK 只记录诊断状态，不因此断开可复用连接。 */
        if (flags & XHttp2Frame_Ack)
            return true;
        return xhttp2_connection_append_frame(self, XHttp2Frame_Ping, XHttp2Frame_Ack, 0, payload);
    }
    if (type == XHttp2Frame_WindowUpdate) {
        uint32_t increment;
        if (!xhttp2_connection_read_u32(payload, 0, &increment))
            return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        if (id == 0) {
            if (!increment || (increment & UINT32_C(0x80000000)))
                return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
            if (self->m_sessionSendWindow + increment > INT32_MAX)
                return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
            self->m_sessionSendWindow += increment;
        } else if ((stream = xhttp2_connection_find_stream(self, id))) {
            /* RFC 9113 6.9：closed stream 上的 WINDOW_UPDATE 可以忽略。 */
            if (stream->m_state != XHttp2Stream_Closed) {
                if (!increment || (increment & UINT32_C(0x80000000)) ||
                    stream->m_sendWindow + increment > INT32_MAX) {
                    if (!xhttp2_connection_stream_error(self, stream,
                                                        XHTTP2_CONNECTION_PROTOCOL_ERROR,
                                                        "HTTP/2 WINDOW_UPDATE 流窗口无效"))
                        return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
                } else {
                    stream->m_sendWindow += increment;
                }
            }
        }
        /* 对齐 Qt：无法关联到活动流的 WINDOW_UPDATE 按已关闭流忽略。 */
        return xhttp2_connection_flush(self);
    }
    if (type == XHttp2Frame_GoAway) {
        uint32_t error;
        uint32_t last;
        uint32_t next;
        if (id != 0 || !xhttp2_connection_read_u32(payload, 0, &last) ||
            !xhttp2_connection_read_u32(payload, 4, &error) ||
            (last & UINT32_C(0x80000000)))
            return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        self->m_remoteLastStreamId = last & UINT32_C(0x7fffffff);
        if (self->m_remoteLastStreamId != 0 && (self->m_remoteLastStreamId & 1u) == 0)
            return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        next = self->m_session ? self->m_session->m_nextStreamId : 0;
        if (next != 0 && self->m_remoteLastStreamId >= next &&
            (self->m_remoteLastStreamId != UINT32_C(0x7fffffff) || error != 0))
            return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        self->m_goingAway = true;
        XHttp2ClientSession_setGoingAway(self->m_session);
        for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_streams); ++i) {
            XHttp2Stream** slot = (XHttp2Stream**)XVector_at_base(self->m_streams, (int64_t)i);
            if (slot && *slot && (*slot)->m_id > self->m_remoteLastStreamId &&
                (*slot)->m_state != XHttp2Stream_Closed) {
                if ((*slot)->m_reply)
                    xhttp2_connection_set_goaway_error((*slot)->m_reply, error);
                xhttp2_connection_close_stream(self, *slot);
            }
        }
        return true;
    }
    if (self->m_continuationStreamId && (type != XHttp2Frame_Continuation || id != self->m_continuationStreamId)) return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
    if (type == XHttp2Frame_Continuation) {
        stream = self->m_continuationPushPromise ?
            xhttp2_connection_find_stream(self, self->m_promisedStreamId) :
            xhttp2_connection_find_stream(self, id);
        if (!stream || !stream->m_headersActive || !xhttp2_connection_append(stream->m_headerBlock,
            payload ? XByteArray_constData((XByteArray*)payload) : NULL, payload ? XContainer_size_base((const XContainer*)payload) : 0)) return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        return (flags & XHttp2Frame_EndHeaders) ? xhttp2_connection_finish_headers(self, stream) : true;
    }
    if (type == XHttp2Frame_Priority) {
        stream = xhttp2_connection_find_stream(self, id);
        if (!id)
            return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        if (!stream || stream->m_state == XHttp2Stream_Closed)
            return xhttp2_connection_fail(self, XHTTP2_CONNECTION_ENHANCE_YOUR_CALM);
        /* Qt 读取 PRIORITY 字段但暂不以依赖树调度上传流。 */
        return true;
    }
    if (type == XHttp2Frame_RstStream) {
        uint32_t resetError;
        stream = xhttp2_connection_find_stream(self, id);
        if (!id || ((id & 1u) && !stream &&
                    !xhttp2_connection_is_closed_local_stream(self, id)) ||
            ((id & 1u) == 0 && !stream && id > self->m_lastIncomingStreamId))
            return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        if (stream && stream->m_reply &&
            xhttp2_connection_read_u32(payload, 0, &resetError))
            xhttp2_connection_set_rst_error(stream->m_reply, resetError);
        if (stream) xhttp2_connection_close_stream(self, stream); return true;
    }
    if (type == XHttp2Frame_PushPromise) {
        uint32_t promised;
        size_t promisedOffset = (flags & XHttp2Frame_Padded) ? 1 : 0;
        XHttp2Stream* pushed;
        if (!XHttp2Configuration_serverPushEnabled(self->m_configuration) && !self->m_waitingForSettingsAck)
            return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        stream = xhttp2_connection_find_stream(self, id);
        if (!id)
            return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        if (!stream || (id & 1u) == 0 ||
            (stream->m_state != XHttp2Stream_Open &&
             stream->m_state != XHttp2Stream_HalfClosedLocal))
            return xhttp2_connection_fail(self, XHTTP2_CONNECTION_ENHANCE_YOUR_CALM);
        if (!xhttp2_connection_read_u32(payload, promisedOffset, &promised) ||
            (promised & UINT32_C(0x80000000))) return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        promised &= UINT32_C(0x7fffffff);
        if (!promised || (promised & 1u) || promised <= self->m_lastIncomingStreamId ||
            xhttp2_connection_find_stream(self, promised) ||
            !xhttp2_connection_payload(frame, 4, &data, &size)) return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        pushed = (XHttp2Stream*)XMalloc_System(sizeof(*pushed));
        if (!pushed) return false;
        memset(pushed, 0, sizeof(*pushed));
        pushed->m_id = promised; pushed->m_pushed = true; pushed->m_state = XHttp2Stream_ReservedRemote;
        pushed->m_reply = XHttpReply_create(NULL); pushed->m_replyOwned = true;
        pushed->m_headerBlock = XByteArray_create();
        pushed->m_sendWindow = self->m_peerInitialWindowSize; pushed->m_recvWindow = self->m_streamRecvTarget;
        if (!pushed->m_reply || !pushed->m_headerBlock || !xhttp2_connection_append(pushed->m_headerBlock, data, size) ||
            !XVector_push_back_1_base(self->m_streams, &pushed)) { xhttp2_connection_stream_delete(pushed); return false; }
        self->m_lastIncomingStreamId = promised;
        pushed->m_headersActive = (flags & XHttp2Frame_EndHeaders) == 0;
        self->m_continuationStreamId = pushed->m_headersActive ? id : 0;
        self->m_continuationPushPromise = true; self->m_promisedStreamId = promised;
        return pushed->m_headersActive || xhttp2_connection_finish_headers(self, pushed);
    }
    if (type > XHttp2Frame_Continuation) return true;
    stream = xhttp2_connection_find_stream(self, id);
    if (!stream) {
        if (type == XHttp2Frame_Data && xhttp2_connection_is_closed_local_stream(self, id))
            return xhttp2_connection_append_u32_frame(self, XHttp2Frame_RstStream, id,
                                                       XHTTP2_CONNECTION_STREAM_CLOSED);
        return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
    }
    if (type == XHttp2Frame_Headers) {
        if (stream->m_state == XHttp2Stream_HalfClosedRemote ||
            stream->m_state == XHttp2Stream_Closed)
            return xhttp2_connection_stream_error(self, stream,
                                                  XHTTP2_CONNECTION_PROTOCOL_ERROR,
                                                  "HTTP/2 已关闭接收方向仍收到 HEADERS");
        if (!xhttp2_connection_payload(frame, (flags & XHttp2Frame_PriorityFlag) ? 5 : 0, &data, &size) ||
            !xhttp2_connection_append(stream->m_headerBlock, data, size)) return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        stream->m_headerEndStream = (flags & XHttp2Frame_EndStream) != 0; stream->m_headersActive = (flags & XHttp2Frame_EndHeaders) == 0;
        self->m_continuationStreamId = stream->m_headersActive ? id : 0; self->m_continuationPushPromise = false;
        return stream->m_headersActive || xhttp2_connection_finish_headers(self, stream);
    }
    if (type == XHttp2Frame_Data) {
        size_t flow = payload ? XContainer_size_base((const XContainer*)payload) : 0;
        if (stream->m_state == XHttp2Stream_HalfClosedRemote)
            return xhttp2_connection_stream_error(self, stream,
                                                  XHTTP2_CONNECTION_STREAM_CLOSED,
                                                  "HTTP/2 已关闭接收方向仍收到 DATA");
        if (stream->m_state == XHttp2Stream_Closed)
            return xhttp2_connection_append_u32_frame(self, XHttp2Frame_RstStream, id,
                                                       XHTTP2_CONNECTION_STREAM_CLOSED);
        if ((int64_t)flow > self->m_sessionRecvWindow || (int64_t)flow > stream->m_recvWindow) return xhttp2_connection_fail(self, XHTTP2_CONNECTION_FLOW_CONTROL_ERROR);
        self->m_sessionRecvWindow -= (int64_t)flow; stream->m_recvWindow -= (int64_t)flow;
        if (!xhttp2_connection_payload(frame, 0, &data, &size) || !stream->m_reply ||
            !XHttpReply_feedHttp2Data(stream->m_reply, data, size, (flags & XHttp2Frame_EndStream) != 0)) return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        if (self->m_sessionRecvWindow < (int64_t)(self->m_sessionRecvTarget / 2)) {
            uint32_t delta = self->m_sessionRecvTarget - (uint32_t)self->m_sessionRecvWindow;
            if (!xhttp2_connection_append_u32_frame(self, XHttp2Frame_WindowUpdate, 0, delta)) return false;
            self->m_sessionRecvWindow += delta;
        }
        if (stream->m_recvWindow < (int64_t)(self->m_streamRecvTarget / 2)) {
            uint32_t delta = self->m_streamRecvTarget - (uint32_t)stream->m_recvWindow;
            if (!xhttp2_connection_append_u32_frame(self, XHttp2Frame_WindowUpdate, id, delta)) return false;
            stream->m_recvWindow += delta;
        }
        if (flags & XHttp2Frame_EndStream) {
            if (stream->m_pushed && stream->m_reply && self->m_pushedReplies &&
                XVector_push_back_1_base(self->m_pushedReplies, &stream->m_reply))
                stream->m_reply = NULL;
            if (!xhttp2_connection_remote_end_stream(self, stream))
                return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR);
        }
        return true;
    }
    return true;
}

bool XHttp2Connection_feed(XHttp2Connection* self, const void* data, size_t size)
{
    size_t offset = 0;
    if (!self || self->m_failed || (!data && size)) return false;
    if (size && !xhttp2_connection_append(self->m_input, data, size)) return false;
    while (XContainer_size_base((const XContainer*)self->m_input) - offset >= 9) {
        const uint8_t* bytes = XByteArray_constData(self->m_input);
        size_t total = XContainer_size_base((const XContainer*)self->m_input);
        size_t length = ((size_t)bytes[offset] << 16) | ((size_t)bytes[offset + 1] << 8) | bytes[offset + 2];
        size_t consumed = 0;
        XHttp2Frame* frame;
        /* SETTINGS_MAX_FRAME_SIZE 是本端接收承诺，完整载荷到达前就拒绝超限头。 */
        if (length > XHttp2Configuration_maxFrameSize(self->m_configuration))
            return xhttp2_connection_fail(self, XHTTP2_CONNECTION_FRAME_SIZE_ERROR);
        if (length > total - offset - 9) break;
        frame = XHttp2Frame_fromBytes(bytes + offset, total - offset, &consumed);
        if (!frame || !consumed) { if (frame) XClass_delete_base((XClass*)frame); return xhttp2_connection_fail(self, XHTTP2_CONNECTION_PROTOCOL_ERROR); }
        if (!xhttp2_connection_process_frame(self, frame)) { XClass_delete_base((XClass*)frame); return false; }
        XClass_delete_base((XClass*)frame); offset += consumed;
    }
    if (offset) XByteArray_remove_base((XVector*)self->m_input, 0, (int64_t)offset);
    return true;
}

XByteArray* XHttp2Connection_takeOutgoing(XHttp2Connection* self)
{
    XByteArray* result;
    if (!self || !self->m_outgoing) return NULL;
    result = XByteArray_create_copy(self->m_outgoing);
    if (result) XByteArray_clear_base((XContainer*)self->m_outgoing);
    return result;
}

XHttpReply* XHttp2Connection_replyForStream(XHttp2Connection* self, uint32_t streamId)
{
    XHttp2Stream* stream = xhttp2_connection_find_stream(self, streamId);
    return stream ? stream->m_reply : NULL;
}

bool XHttp2Connection_detachReply(XHttp2Connection* self, uint32_t streamId,
                                  const XHttpReply* reply)
{
    if (!self || !self->m_streams)
        return false;
    for (size_t index = 0;
         index < XContainer_size_base((const XContainer*)self->m_streams); ++index) {
        XHttp2Stream** slot = (XHttp2Stream**)XVector_at_base(self->m_streams,
                                                               (int64_t)index);
        XHttp2Stream* stream = slot ? *slot : NULL;
        if (!stream || stream->m_id != streamId)
            continue;
        if (stream->m_replyOwned || stream->m_reply != reply)
            return false;
        stream->m_reply = NULL;
        /* 管理器已经拥有完成响应；关闭流无需继续占用可复用连接的记录。 */
        if (stream->m_state == XHttp2Stream_Closed) {
            XVector_remove_base(self->m_streams, (int64_t)index, 1);
            xhttp2_connection_stream_delete(stream);
        }
        return true;
    }
    return false;
}

size_t XHttp2Connection_streamCount(const XHttp2Connection* self)
{
    return self && self->m_streams ? XContainer_size_base((const XContainer*)self->m_streams) : 0;
}

XHttpReply* XHttp2Connection_takePushedReply(XHttp2Connection* self)
{
    XHttpReply** reply;
    XHttpReply* result;
    if (!self || !self->m_pushedReplies || XContainer_size_base((const XContainer*)self->m_pushedReplies) == 0)
        return NULL;
    reply = (XHttpReply**)XVector_at_base(self->m_pushedReplies, 0);
    result = reply ? *reply : NULL;
    XVector_remove_base(self->m_pushedReplies, 0, 1);
    return result;
}

bool XHttp2Connection_isGoingAway(const XHttp2Connection* self)
{
    return self && (self->m_goingAway || self->m_failed);
}
#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
