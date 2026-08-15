/**
 * @file       XRestAccessManager.c
 * @brief      REST 便捷管理器实现。
 */

#include "XRestAccessManager.h"

#include "XMemory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if XPROTOCOL_ON
#if XHTTP_ON

static void VXRestAccessManager_deinit(XRestAccessManager* self)
{
    if (!self) return;
    self->m_manager = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XRestAccessManager_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XRestAccessManager)
    //继承类
    XVTABLE_INHERIT_XCLASS(XObject);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXRestAccessManager_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XRestAccessManager);
    return XVTABLE_DEFAULT;
}

void XRestAccessManager_init(XRestAccessManager* self, XNetworkAccessManager* manager)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XRestAccessManager);
    self->m_manager = manager;
}

XRestAccessManager* XRestAccessManager_create_ex(XMemoryType memory, XNetworkAccessManager* manager)
{
    XRestAccessManager* self;
    if (!manager) return NULL;
    self = (XRestAccessManager*)XMemory_malloc(sizeof(XRestAccessManager), memory);
    if (!self) return NULL;
    XRestAccessManager_init(self, manager);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

XNetworkAccessManager* XRestAccessManager_networkAccessManager(const XRestAccessManager* self)
{ return self ? self->m_manager : NULL; }

XHttpReply* XRestAccessManager_deleteResource(XRestAccessManager* self, const XHttpRequest* request)
{ return self && self->m_manager ? XNetworkAccessManager_deleteResource(self->m_manager, request) : NULL; }
XHttpReply* XRestAccessManager_head(XRestAccessManager* self, const XHttpRequest* request)
{ return self && self->m_manager ? XNetworkAccessManager_head(self->m_manager, request) : NULL; }
XHttpReply* XRestAccessManager_get(XRestAccessManager* self, const XHttpRequest* request)
{ return self && self->m_manager ? XNetworkAccessManager_get(self->m_manager, request) : NULL; }
XHttpReply* XRestAccessManager_getWithData(XRestAccessManager* self, const XHttpRequest* request,
                                           const XByteArray* body)
{ return self && self->m_manager ? XNetworkAccessManager_sendRequest(self->m_manager,
    XNetworkAccessManager_GetOperation, request, body, NULL) : NULL; }
XHttpReply* XRestAccessManager_post(XRestAccessManager* self, const XHttpRequest* request,
                                    const XByteArray* body)
{ return self && self->m_manager ? XNetworkAccessManager_post(self->m_manager, request, body) : NULL; }
XHttpReply* XRestAccessManager_put(XRestAccessManager* self, const XHttpRequest* request,
                                   const XByteArray* body)
{ return self && self->m_manager ? XNetworkAccessManager_put(self->m_manager, request, body) : NULL; }
XHttpReply* XRestAccessManager_postMultipart(XRestAccessManager* self, const XHttpRequest* request,
                                             const XHttpMultiPart* multipart)
{ return self && self->m_manager ? XNetworkAccessManager_postMultipart(self->m_manager, request, multipart) : NULL; }

static XHttpReply* xrest_send_json(XRestAccessManager* self, const XHttpRequest* request,
                                   const XJsonDocument* json, XHttpRequest_Method method)
{
    XByteArray* body;
    XByteArray* contentType;
    XByteArray* customMethod = NULL;
    XHttpRequest* copy;
    XHttpReply* reply;
    if (!self || !self->m_manager || !request || !json)
        return NULL;
    body = XJsonDocument_toJson(json, XJsonDocument_Compact);
    contentType = XByteArray_create_utf8("application/json");
    copy = XHttpRequest_create_copy(request);
    if (method == XHttpRequest_Patch)
        customMethod = XByteArray_create_utf8("PATCH");
    if (!body || !contentType || !copy || (method == XHttpRequest_Patch && !customMethod)) goto failed;
    /* XJsonDocument_toJson 为字符串兼容会附加 NUL，但 HTTP body 的长度不能包含它。 */
    if (XByteArray_size_base(body) > 0 &&
        XByteArray_constData(body)[XByteArray_size_base(body) - 1] == 0)
        XByteArray_resize_base(body, XByteArray_size_base(body) - 1);
    if (!XHttpHeaders_containsKnown(XHttpRequest_headers_const(copy), XHttpHeaders_WellKnownHeader_ContentType) &&
        !XHttpHeaders_replaceOrAppendKnown(XHttpRequest_headers(copy),
             XHttpHeaders_WellKnownHeader_ContentType, contentType)) {
        goto failed;
    }
    XHttpRequest_setMethod(copy, method);
    reply = XNetworkAccessManager_sendRequest(self->m_manager,
        method == XHttpRequest_Post ? XNetworkAccessManager_PostOperation :
        method == XHttpRequest_Put ? XNetworkAccessManager_PutOperation :
        XNetworkAccessManager_CustomOperation, copy, body,
        customMethod);
    XClass_delete_base((XClass*)body);
    XClass_delete_base((XClass*)contentType);
    if (customMethod) XClass_delete_base((XClass*)customMethod);
    XClass_delete_base((XClass*)copy);
    return reply;
failed:
    if (body) XClass_delete_base((XClass*)body);
    if (contentType) XClass_delete_base((XClass*)contentType);
    if (customMethod) XClass_delete_base((XClass*)customMethod);
    if (copy) XClass_delete_base((XClass*)copy);
    return NULL;
}

XHttpReply* XRestAccessManager_postJson(XRestAccessManager* self, const XHttpRequest* request,
                                        const XJsonDocument* json)
{ return xrest_send_json(self, request, json, XHttpRequest_Post); }
XHttpReply* XRestAccessManager_putJson(XRestAccessManager* self, const XHttpRequest* request,
                                       const XJsonDocument* json)
{ return xrest_send_json(self, request, json, XHttpRequest_Put); }
XHttpReply* XRestAccessManager_patchJson(XRestAccessManager* self, const XHttpRequest* request,
                                         const XJsonDocument* json)
{ return xrest_send_json(self, request, json, XHttpRequest_Patch); }

XHttpReply* XRestAccessManager_patch(XRestAccessManager* self, const XHttpRequest* request,
                                     const XByteArray* body)
{
    XByteArray* method = XByteArray_create_utf8("PATCH");
    XHttpReply* reply = method && self && self->m_manager ?
        XNetworkAccessManager_sendCustomRequest(self->m_manager, request, method, body) : NULL;
    if (method) XClass_delete_base((XClass*)method);
    return reply;
}

XHttpReply* XRestAccessManager_sendCustomRequest(XRestAccessManager* self,
                                                 const XHttpRequest* request,
                                                 const XByteArray* method,
                                                 const XByteArray* body)
{ return self && self->m_manager ? XNetworkAccessManager_sendCustomRequest(self->m_manager, request, method, body) : NULL; }

XRestReply* XRestAccessManager_wrapReply(const XRestAccessManager* self, XHttpReply* reply)
{ return self && reply ? XRestReply_create(reply) : NULL; }
#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
