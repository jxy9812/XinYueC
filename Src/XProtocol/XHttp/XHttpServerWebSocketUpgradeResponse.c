/**
 * @file       XHttpServerWebSocketUpgradeResponse.c
 * @brief      WebSocket 升级判定值对象实现。
 */

#include "XHttpServerWebSocketUpgradeResponse.h"

#include "XMemory.h"
#include <stdio.h>
#include <string.h>
#if XPROTOCOL_ON
#if XHTTP_ON

static void VXHttpServerWebSocketUpgradeResponse_deinit(
    XHttpServerWebSocketUpgradeResponse* self)
{
    if (!self)
        return;
    if (self->m_denyMessage)
        XClass_delete_base((XClass*)self->m_denyMessage);
    self->m_denyMessage = NULL;
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXHttpServerWebSocketUpgradeResponse_copy(
    XHttpServerWebSocketUpgradeResponse* dest,
    const XHttpServerWebSocketUpgradeResponse* src)
{
    XByteArray* message;
    if (!dest || !src || dest == src)
        return;
    message = src->m_denyMessage ? XByteArray_create_copy(src->m_denyMessage) : XByteArray_create();
    if (!message)
        return;
    if (XClassIsVtableNull(dest)) {
        XClass_init((XClass*)dest);
        XClassSetVtable(dest, XHttpServerWebSocketUpgradeResponse);
    }
    if (dest->m_denyMessage)
        XClass_delete_base((XClass*)dest->m_denyMessage);
    dest->m_type = src->m_type;
    dest->m_denyStatus = src->m_denyStatus;
    dest->m_denyMessage = message;
}

XVtable* XHttpServerWebSocketUpgradeResponse_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHttpServerWebSocketUpgradeResponse)
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHttpServerWebSocketUpgradeResponse_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHttpServerWebSocketUpgradeResponse_copy);
    XCLASS_SHOW_SIZE_DEFAULT(XHttpServerWebSocketUpgradeResponse);
    return XVTABLE_DEFAULT;
}

static XHttpServerWebSocketUpgradeResponse*
xhttp_server_websocket_upgrade_response_create(
    XHttpServerWebSocketUpgradeResponse_Type type,
    int status,
    const XByteArray* message)
{
    XHttpServerWebSocketUpgradeResponse* self =
        (XHttpServerWebSocketUpgradeResponse*)XMalloc_System(sizeof(*self));
    if (!self)
        return NULL;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XHttpServerWebSocketUpgradeResponse);
    self->m_type = type;
    self->m_denyStatus = (status >= 100 && status <= 599) ? status : 403;
    self->m_denyMessage = message ? XByteArray_create_copy(message) : XByteArray_create();
    Set_Class_MemoryFree(self, XFree_System);
    if (!self->m_denyMessage) {
        XClass_delete_base((XClass*)self);
        return NULL;
    }
    return self;
}

XHttpServerWebSocketUpgradeResponse* XHttpServerWebSocketUpgradeResponse_accept(void)
{
    return xhttp_server_websocket_upgrade_response_create(
        XHttpServerWebSocketUpgradeResponse_Accept, 403, NULL);
}

XHttpServerWebSocketUpgradeResponse* XHttpServerWebSocketUpgradeResponse_deny(void)
{
    return xhttp_server_websocket_upgrade_response_create(
        XHttpServerWebSocketUpgradeResponse_Deny, 403, NULL);
}

XHttpServerWebSocketUpgradeResponse* XHttpServerWebSocketUpgradeResponse_denyWith(
    int status, const XByteArray* message)
{
    return xhttp_server_websocket_upgrade_response_create(
        XHttpServerWebSocketUpgradeResponse_Deny, status, message);
}

XHttpServerWebSocketUpgradeResponse* XHttpServerWebSocketUpgradeResponse_passToNext(void)
{
    return xhttp_server_websocket_upgrade_response_create(
        XHttpServerWebSocketUpgradeResponse_PassToNext, 403, NULL);
}

XHttpServerWebSocketUpgradeResponse* XHttpServerWebSocketUpgradeResponse_create_copy(
    const XHttpServerWebSocketUpgradeResponse* other)
{
    XHttpServerWebSocketUpgradeResponse* self;
    if (!other)
        return NULL;
    self = xhttp_server_websocket_upgrade_response_create(other->m_type,
                                                          other->m_denyStatus,
                                                          other->m_denyMessage);
    return self;
}

XHttpServerWebSocketUpgradeResponse_Type XHttpServerWebSocketUpgradeResponse_type(
    const XHttpServerWebSocketUpgradeResponse* self)
{
    return self ? self->m_type : XHttpServerWebSocketUpgradeResponse_Deny;
}

int XHttpServerWebSocketUpgradeResponse_denyStatus(
    const XHttpServerWebSocketUpgradeResponse* self)
{
    return self ? self->m_denyStatus : 403;
}

const XByteArray* XHttpServerWebSocketUpgradeResponse_denyMessage_const(
    const XHttpServerWebSocketUpgradeResponse* self)
{
    return self ? self->m_denyMessage : NULL;
}

void XHttpServerWebSocketUpgradeResponse_swap(
    XHttpServerWebSocketUpgradeResponse* self,
    XHttpServerWebSocketUpgradeResponse* other)
{
    XHttpServerWebSocketUpgradeResponse_Type type;
    int status;
    XByteArray* message;
    if (!self || !other || self == other)
        return;
    type = self->m_type;
    status = self->m_denyStatus;
    message = self->m_denyMessage;
    self->m_type = other->m_type;
    self->m_denyStatus = other->m_denyStatus;
    self->m_denyMessage = other->m_denyMessage;
    other->m_type = type;
    other->m_denyStatus = status;
    other->m_denyMessage = message;
}
#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
