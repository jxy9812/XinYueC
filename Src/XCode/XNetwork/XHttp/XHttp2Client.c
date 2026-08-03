/**
 * @file       XHttp2Client.c
 * @brief      HTTP/2 客户端协议帧调度实现。
 */

#include "XHttp2Client.h"

#include "XMemory.h"
#include "XString.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void VXHttp2ClientSession_deinit(XHttp2ClientSession* self)
{
    if (!self) return;
    if (self->m_configuration)
        XClass_delete_base((XClass*)self->m_configuration);
    if (self->m_encoder)
        XClass_delete_base((XClass*)self->m_encoder);
    self->m_configuration = NULL;
    self->m_encoder = NULL;
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHttp2ClientSession_copy(XHttp2ClientSession* dest,
                                      const XHttp2ClientSession* src)
{
    XHttp2Configuration* configuration;
    XHttp2HeaderEncoder* encoder;
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XHttp2ClientSession_init(dest);
    configuration = XHttp2Configuration_create_copy(src->m_configuration);
    encoder = src->m_encoder ? (XHttp2HeaderEncoder*)XMalloc_System(sizeof(*encoder)) : NULL;
    if (!configuration || (src->m_encoder && !encoder)) {
        if (configuration) XClass_delete_base((XClass*)configuration);
        if (encoder) XFree_System(encoder);
        return;
    }
    if (encoder) {
        XHttp2HeaderEncoder_init(encoder);
        XClass_copy_base((XClass*)encoder, (const XClass*)src->m_encoder);
        Set_Class_MemoryFree(encoder, XFree_System);
    }
    if (dest->m_configuration)
        XClass_delete_base((XClass*)dest->m_configuration);
    if (dest->m_encoder)
        XClass_delete_base((XClass*)dest->m_encoder);
    dest->m_configuration = configuration;
    dest->m_encoder = encoder;
    dest->m_nextStreamId = src->m_nextStreamId;
    dest->m_peerMaxHeaderListSize = src->m_peerMaxHeaderListSize;
    dest->m_peerMaxConcurrentStreams = src->m_peerMaxConcurrentStreams;
    dest->m_peerMaxFrameSize = src->m_peerMaxFrameSize;
    dest->m_activeStreamCount = src->m_activeStreamCount;
    dest->m_started = src->m_started;
    dest->m_goingAway = src->m_goingAway;
}

static void VXHttp2ClientSession_move(XHttp2ClientSession* dest,
                                      XHttp2ClientSession* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XHttp2ClientSession_init(dest);
    if (dest->m_configuration)
        XClass_delete_base((XClass*)dest->m_configuration);
    if (dest->m_encoder)
        XClass_delete_base((XClass*)dest->m_encoder);
    dest->m_configuration = src->m_configuration;
    dest->m_encoder = src->m_encoder;
    dest->m_nextStreamId = src->m_nextStreamId;
    dest->m_peerMaxHeaderListSize = src->m_peerMaxHeaderListSize;
    dest->m_peerMaxConcurrentStreams = src->m_peerMaxConcurrentStreams;
    dest->m_peerMaxFrameSize = src->m_peerMaxFrameSize;
    dest->m_activeStreamCount = src->m_activeStreamCount;
    dest->m_started = src->m_started;
    dest->m_goingAway = src->m_goingAway;
    src->m_configuration = XHttp2Configuration_create();
    src->m_encoder = XHttp2HeaderEncoder_create();
    src->m_nextStreamId = 1;
    src->m_peerMaxHeaderListSize = SIZE_MAX;
    src->m_peerMaxConcurrentStreams = 100;
    src->m_peerMaxFrameSize = XHttp2Configuration_MinFrameSize;
    src->m_activeStreamCount = 0;
    src->m_started = false;
    src->m_goingAway = false;
}

XVtable* XHttp2ClientSession_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
    //虚函数表初始化
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XHttp2ClientSession)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttp2ClientSession_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttp2ClientSession_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHttp2ClientSession_move);
#if SHOWCONTAINERSIZE
    printf("XHttp2ClientSession size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

void XHttp2ClientSession_init(XHttp2ClientSession* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttp2ClientSession);
    self->m_configuration = XHttp2Configuration_create();
    self->m_encoder = XHttp2HeaderEncoder_create();
    self->m_nextStreamId = 1;
    self->m_peerMaxHeaderListSize = SIZE_MAX;
    self->m_peerMaxConcurrentStreams = 100;
    self->m_peerMaxFrameSize = XHttp2Configuration_MinFrameSize;
}

XHttp2ClientSession* XHttp2ClientSession_create(void)
{
    XHttp2ClientSession* self = (XHttp2ClientSession*)XMalloc_System(sizeof(*self));
    if (!self) return NULL;
    XHttp2ClientSession_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    if (!self->m_configuration || !self->m_encoder) {
        XClass_delete_base((XClass*)self);
        return NULL;
    }
    return self;
}

bool XHttp2ClientSession_setConfiguration(XHttp2ClientSession* self,
                                          const XHttp2Configuration* configuration)
{
    XHttp2Configuration* replacement;
    if (!self || !configuration) return false;
    replacement = XHttp2Configuration_create_copy(configuration);
    if (!replacement) return false;
    if (self->m_configuration)
        XClass_delete_base((XClass*)self->m_configuration);
    self->m_configuration = replacement;
    return true;
}

const XHttp2Configuration* XHttp2ClientSession_configuration_const(
    const XHttp2ClientSession* self)
{ return self ? self->m_configuration : NULL; }

bool XHttp2ClientSession_setPeerHeaderTableSize(XHttp2ClientSession* self, size_t size)
{
    return self && self->m_encoder &&
           XHttp2HeaderEncoder_setMaxDynamicTableSize(self->m_encoder, size);
}

bool XHttp2ClientSession_setPeerMaxHeaderListSize(XHttp2ClientSession* self, size_t size)
{
    if (!self)
        return false;
    self->m_peerMaxHeaderListSize = size;
    return true;
}

bool XHttp2ClientSession_setPeerMaxConcurrentStreams(XHttp2ClientSession* self,
                                                      uint32_t count)
{
    if (!self)
        return false;
    self->m_peerMaxConcurrentStreams = count;
    return true;
}

bool XHttp2ClientSession_setPeerMaxFrameSize(XHttp2ClientSession* self, uint32_t size)
{
    if (!self || size < XHttp2Configuration_MinFrameSize ||
        size > XHttp2Configuration_MaxFrameSize)
        return false;
    self->m_peerMaxFrameSize = size;
    return true;
}

size_t XHttp2ClientSession_activeStreamCount(const XHttp2ClientSession* self)
{
    return self ? self->m_activeStreamCount : 0;
}

bool XHttp2ClientSession_markStreamClosed(XHttp2ClientSession* self)
{
    if (!self || self->m_activeStreamCount == 0)
        return false;
    --self->m_activeStreamCount;
    return true;
}

void XHttp2ClientSession_setGoingAway(XHttp2ClientSession* self)
{
    if (self)
        self->m_goingAway = true;
}

bool XHttp2ClientSession_isGoingAway(const XHttp2ClientSession* self)
{
    return self && self->m_goingAway;
}

XByteArray* XHttp2ClientSession_start(XHttp2ClientSession* self)
{
    XHttp2Frame* settings;
    XByteArray* frameBytes;
    XByteArray* settingsPayload;
    XByteArray* result;
    uint8_t setting[6];
    if (!self) return NULL;
    result = XByteArray_create();
    if (!result) return NULL;
    if (self->m_started) return result;
    settingsPayload = XByteArray_create();
    if (settingsPayload && self->m_configuration) {
        uint32_t initialWindow = XHttp2Configuration_streamReceiveWindowSize(
            self->m_configuration);
        uint32_t maxFrame = XHttp2Configuration_maxFrameSize(self->m_configuration);
        setting[0] = 0;
        setting[1] = 2;
        setting[2] = 0;
        setting[3] = 0;
        setting[4] = 0;
        setting[5] = XHttp2Configuration_serverPushEnabled(self->m_configuration) ? 1 : 0;
        if (!XByteArray_push_back_2((XVector*)settingsPayload, setting, sizeof(setting)))
            goto fail;
        /* 对齐 Qt：http2 默认值不写入 SETTINGS，仅发送 ENABLE_PUSH。 */
        if (initialWindow != UINT32_C(65535)) {
            setting[0] = 0;
            setting[1] = 4;
            setting[2] = (uint8_t)(initialWindow >> 24);
            setting[3] = (uint8_t)(initialWindow >> 16);
            setting[4] = (uint8_t)(initialWindow >> 8);
            setting[5] = (uint8_t)initialWindow;
            if (!XByteArray_push_back_2((XVector*)settingsPayload, setting, sizeof(setting)))
                goto fail;
        }
        if (maxFrame != XHttp2Configuration_MinFrameSize) {
            setting[0] = 0;
            setting[1] = 5;
            setting[2] = (uint8_t)(maxFrame >> 24);
            setting[3] = (uint8_t)(maxFrame >> 16);
            setting[4] = (uint8_t)(maxFrame >> 8);
            setting[5] = (uint8_t)maxFrame;
            if (!XByteArray_push_back_2((XVector*)settingsPayload, setting, sizeof(setting)))
                goto fail;
        }
    }
    settings = XHttp2Frame_create_ex(XHttp2Frame_Settings, 0, 0, settingsPayload);
    frameBytes = settings ? XHttp2Frame_toByteArray(settings) : NULL;
    if (!settingsPayload || !settings || !frameBytes ||
        !XByteArray_push_back_2(result, XHttp2Frame_ClientPreface,
                                sizeof(XHttp2Frame_ClientPreface) - 1) ||
        !XByteArray_push_back_2(result, XByteArray_constData(frameBytes),
                                XByteArray_size_base(frameBytes))) {
        if (frameBytes) XClass_delete_base((XClass*)frameBytes);
        if (settings) XClass_delete_base((XClass*)settings);
        XClass_delete_base((XClass*)settingsPayload);
        XClass_delete_base((XClass*)result);
        return NULL;
    }
    self->m_started = true;
    XClass_delete_base((XClass*)frameBytes);
    XClass_delete_base((XClass*)settings);
    XClass_delete_base((XClass*)settingsPayload);
    return result;
fail:
    if (settingsPayload) XClass_delete_base((XClass*)settingsPayload);
    XClass_delete_base((XClass*)result);
    return NULL;
}

bool XHttp2ClientSession_isStarted(const XHttp2ClientSession* self)
{ return self && self->m_started; }

uint32_t XHttp2ClientSession_nextStreamId(XHttp2ClientSession* self)
{
    uint32_t result;
    if (!self || self->m_nextStreamId == 0 ||
        (self->m_nextStreamId & UINT32_C(0x80000000)) != 0 ||
        (self->m_nextStreamId & 1u) == 0)
        return 0;
    result = self->m_nextStreamId;
    /* RFC 9113 流标识最高位保留；分配 0x7fffffff 后永久耗尽本端奇数流号。 */
    if (self->m_nextStreamId >= UINT32_C(0x7fffffff))
        self->m_nextStreamId = 0;
    else
        self->m_nextStreamId += 2;
    return result;
}

bool XHttp2ClientSession_adoptUpgradedStream(XHttp2ClientSession* self)
{
    if (!self || self->m_goingAway || self->m_nextStreamId != 1 ||
        self->m_activeStreamCount >= self->m_peerMaxConcurrentStreams)
        return false;
    self->m_nextStreamId = 3;
    ++self->m_activeStreamCount;
    return true;
}

static const char* xhttp2_client_method(const XHttpRequest* request)
{
    const XByteArray* custom;
    if (!request) return NULL;
    switch (XHttpRequest_method(request)) {
    case XHttpRequest_Head: return "HEAD";
    case XHttpRequest_Get: return "GET";
    case XHttpRequest_Post: return "POST";
    case XHttpRequest_Put: return "PUT";
    case XHttpRequest_Delete: return "DELETE";
    case XHttpRequest_Patch: return "PATCH";
    case XHttpRequest_Custom:
        custom = XHttpRequest_customMethod_const(request);
        return custom ? (const char*)XByteArray_constData((XByteArray*)custom) : NULL;
    default: return NULL;
    }
}

static bool xhttp2_client_is_forbidden_header(const XByteArray* name)
{
    const char* text = name ? (const char*)XByteArray_constData((XByteArray*)name) : NULL;
    size_t size = name ? XByteArray_size_base(name) : 0;
    return (size == 10 && memcmp(text, "connection", 10) == 0) ||
           (size == 10 && memcmp(text, "keep-alive", 10) == 0) ||
           (size == 16 && memcmp(text, "proxy-connection", 16) == 0) ||
           (size == 17 && memcmp(text, "transfer-encoding", 17) == 0) ||
           (size == 7 && memcmp(text, "upgrade", 7) == 0) ||
           (size == 14 && memcmp(text, "http2-settings", 14) == 0);
}

static bool xhttp2_client_append_string(XHttp2HeaderList* headers,
                                        const char* name, const XString* value)
{
    XByteArray* byteName;
    XByteArray* byteValue;
    bool result;
    if (!headers || !name || !value) return false;
    byteName = XByteArray_create_utf8(name);
    byteValue = XByteArray_create_with_data(XString_toUtf8(value),
                                             XString_toUtf8_length(value));
    result = byteName && byteValue && XHttp2HeaderList_append(headers, byteName, byteValue);
    if (byteName) XClass_delete_base((XClass*)byteName);
    if (byteValue) XClass_delete_base((XClass*)byteValue);
    return result;
}

static bool xhttp2_client_headers_fit_peer_limit(const XHttp2ClientSession* self,
                                                 const XHttp2HeaderList* headers)
{
    size_t total = 0;
    if (!self || !headers)
        return false;
    for (size_t i = 0; i < XHttp2HeaderList_size(headers); ++i) {
        const XByteArray* name = XHttp2HeaderList_nameAt_const(headers, i);
        const XByteArray* value = XHttp2HeaderList_valueAt_const(headers, i);
        size_t nameSize = name ? XContainer_size_base((const XContainer*)name) : 0;
        size_t valueSize = value ? XContainer_size_base((const XContainer*)value) : 0;
        size_t fieldSize;
        if (!name || !value || nameSize > SIZE_MAX - valueSize - 32)
            return false;
        fieldSize = nameSize + valueSize + 32;
        if (fieldSize > self->m_peerMaxHeaderListSize ||
            total > self->m_peerMaxHeaderListSize - fieldSize)
            return false;
        total += fieldSize;
    }
    return true;
}

XByteArray* XHttp2ClientSession_encodeRequest(XHttp2ClientSession* self,
                                              const XHttpRequest* request,
                                              uint32_t* streamId)
{
    XByteArray* result = NULL;
    XByteArray* started = NULL;
    XByteArray* headerBlock = NULL;
    XHttp2HeaderList* headers = NULL;
    XHttp2Frame* frame = NULL;
    XByteArray* frameBytes = NULL;
    XString* authority;
    XString* path;
    XString* scheme;
    const XUrl* url;
    const XString* query;
    const XByteArray* body;
    uint32_t id;
    size_t i;
    if (!self || !request || !streamId || !(url = XHttpRequest_url_const(request)) ||
        !XUrl_isValid(url) || !xhttp2_client_method(request) || self->m_goingAway ||
        self->m_activeStreamCount >= self->m_peerMaxConcurrentStreams)
        return NULL;
    headers = XHttp2HeaderList_create();
    authority = XUrl_authority_const(url) ? XString_create_copy(XUrl_authority_const(url)) : NULL;
    path = XUrl_path_const(url) ? XString_create_copy(XUrl_path_const(url)) : XString_create_utf8("/");
    scheme = XUrl_scheme_const(url) ? XString_create_copy(XUrl_scheme_const(url)) : NULL;
    query = XUrl_query_const(url);
    if (!headers || !authority || !path || !scheme)
        goto fail;
    if (query && XString_toUtf8_length(query) &&
        (!XString_append_utf8(path, "?") || !XString_append(path, query)))
        goto fail;
    {
        XByteArray* method = XByteArray_create_utf8(xhttp2_client_method(request));
        XByteArray* methodName = XByteArray_create_utf8(":method");
        bool ok = method && methodName && XHttp2HeaderList_append(headers, methodName, method);
        if (method) XClass_delete_base((XClass*)method);
        if (methodName) XClass_delete_base((XClass*)methodName);
        if (!ok) goto fail;
    }
    if (!xhttp2_client_append_string(headers, ":scheme", scheme) ||
        !xhttp2_client_append_string(headers, ":authority", authority) ||
        !xhttp2_client_append_string(headers, ":path", path))
        goto fail;
    for (i = 0; i < XHttpHeaders_size(XHttpRequest_headers_const(request)); ++i) {
        const XByteArray* name = XHttpHeaders_nameAt_const(XHttpRequest_headers_const(request), i);
        const XByteArray* value = XHttpHeaders_valueAt_const(XHttpRequest_headers_const(request), i);
        if (name && value && !xhttp2_client_is_forbidden_header(name) &&
            !XHttp2HeaderList_append(headers, name, value)) goto fail;
    }
    if (!xhttp2_client_headers_fit_peer_limit(self, headers))
        goto fail;
    headerBlock = self->m_encoder ? XHttp2HeaderEncoder_encode(self->m_encoder, headers,
        self->m_configuration && XHttp2Configuration_huffmanCompressionEnabled(self->m_configuration)) : NULL;
    id = XHttp2ClientSession_nextStreamId(self);
    body = XHttpRequest_body_const(request);
    if (!headerBlock || id == 0)
        goto fail;
    /* 所有本地校验成功后再提交前言状态，失败请求不能吞掉客户端前言。 */
    started = self->m_started ? XByteArray_create() : XHttp2ClientSession_start(self);
    result = started;
    if (!result)
        goto fail;
    {
        size_t headerOffset = 0;
        size_t maxFrame = self->m_peerMaxFrameSize;
        size_t headerSize = XByteArray_size_base(headerBlock);
        while (headerOffset < headerSize || headerOffset == 0) {
            size_t chunk = headerSize - headerOffset;
            uint8_t flags = 0;
            uint8_t type = headerOffset == 0 ? XHttp2Frame_Headers : XHttp2Frame_Continuation;
            XByteArray* part;
            if (chunk > maxFrame) chunk = maxFrame;
            if (headerOffset + chunk == headerSize)
                flags |= XHttp2Frame_EndHeaders;
            if (type == XHttp2Frame_Headers &&
                (!body || XByteArray_size_base((XByteArray*)body) == 0))
                flags |= XHttp2Frame_EndStream;
            part = XByteArray_create_with_data(
                (const char*)XByteArray_constData(headerBlock) + headerOffset, chunk);
            frame = part ? XHttp2Frame_create_ex(type, flags, id, part) : NULL;
            frameBytes = frame ? XHttp2Frame_toByteArray(frame) : NULL;
            if (!part || !frame || !frameBytes ||
                !XByteArray_push_back_2(result, XByteArray_constData(frameBytes),
                                        XByteArray_size_base(frameBytes))) {
                if (frameBytes) XClass_delete_base((XClass*)frameBytes);
                if (frame) XClass_delete_base((XClass*)frame);
                if (part) XClass_delete_base((XClass*)part);
                goto fail;
            }
            XClass_delete_base((XClass*)frameBytes);
            XClass_delete_base((XClass*)frame);
            frameBytes = NULL;
            frame = NULL;
            XClass_delete_base((XClass*)part);
            headerOffset += chunk;
            if (headerSize == 0) break;
        }
    }
    if (body && XByteArray_size_base(body) > 0) {
        size_t offset = 0;
        size_t maxFrame = self->m_peerMaxFrameSize;
        while (offset < XByteArray_size_base(body)) {
            size_t chunk = XByteArray_size_base(body) - offset;
            uint8_t flags = 0;
            if (chunk > maxFrame) chunk = maxFrame;
            if (offset + chunk == XByteArray_size_base(body)) flags = XHttp2Frame_EndStream;
            {
                XByteArray* part = XByteArray_create_with_data(
                    (const char*)XByteArray_constData((XByteArray*)body) + offset, chunk);
                XHttp2Frame* dataFrame = part ? XHttp2Frame_create_ex(XHttp2Frame_Data, flags, id, part) : NULL;
                XByteArray* dataBytes = dataFrame ? XHttp2Frame_toByteArray(dataFrame) : NULL;
                if (!part || !dataFrame || !dataBytes ||
                    !XByteArray_push_back_2(result, XByteArray_constData(dataBytes), XByteArray_size_base(dataBytes))) {
                    if (dataBytes) XClass_delete_base((XClass*)dataBytes);
                    if (dataFrame) XClass_delete_base((XClass*)dataFrame);
                    if (part) XClass_delete_base((XClass*)part);
                    goto fail;
                }
                XClass_delete_base((XClass*)dataBytes);
                XClass_delete_base((XClass*)dataFrame);
                XClass_delete_base((XClass*)part);
            }
            offset += chunk;
        }
    }
    *streamId = id;
    ++self->m_activeStreamCount;
    XClass_delete_base((XClass*)headers);
    XClass_delete_base((XClass*)authority);
    XClass_delete_base((XClass*)path);
    XClass_delete_base((XClass*)scheme);
    if (headerBlock) XClass_delete_base((XClass*)headerBlock);
    if (frameBytes) XClass_delete_base((XClass*)frameBytes);
    if (frame) XClass_delete_base((XClass*)frame);
    return result;
fail:
    if (headers) XClass_delete_base((XClass*)headers);
    if (authority) XClass_delete_base((XClass*)authority);
    if (path) XClass_delete_base((XClass*)path);
    if (scheme) XClass_delete_base((XClass*)scheme);
    if (headerBlock) XClass_delete_base((XClass*)headerBlock);
    if (frameBytes) XClass_delete_base((XClass*)frameBytes);
    if (frame) XClass_delete_base((XClass*)frame);
    if (result) XClass_delete_base((XClass*)result);
    return NULL;
}
