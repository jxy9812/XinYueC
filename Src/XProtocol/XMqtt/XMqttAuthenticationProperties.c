#include "XMqtt_config.h"
#if XPROTOCOL_ON
#if XMQTT_ON
#if XMQTT_PROPERTIES_ON
#include "XMqttAuthenticationProperties.h"
#include "XMemory.h"
#include <string.h>

static void VAP_deinit(XMqttAuthenticationProperties* prop);
static void VAP_copy(XMqttAuthenticationProperties* dest, const XMqttAuthenticationProperties* src);
static void VAP_move(XMqttAuthenticationProperties* dest, XMqttAuthenticationProperties* src);

XVtable* XMqttAuthenticationProperties_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMqttAuthenticationProperties)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VAP_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VAP_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VAP_deinit);
    return XVTABLE_DEFAULT;
}

XMqttAuthenticationProperties* XMqttAuthenticationProperties_create_ex(XMemoryType memory)
{
    XMqttAuthenticationProperties* p = (XMqttAuthenticationProperties*)XMemory_malloc(sizeof(XMqttAuthenticationProperties), memory);
    if (p) { XMqttAuthenticationProperties_init(p); Set_Class_Memory(p, memory); Set_Class_IsHeap(p, true); }
    return p;
}

XMqttAuthenticationProperties* XMqttAuthenticationProperties_create_copy(const XMqttAuthenticationProperties* other)
{
    if (!other) return NULL;
    XMqttAuthenticationProperties* p = XMqttAuthenticationProperties_create();
    if (p) XCopy(p, other);
    return p;
}

void XMqttAuthenticationProperties_init(XMqttAuthenticationProperties* prop)
{
    if (!prop) return;
    memset(prop, 0, sizeof(XMqttAuthenticationProperties));
    XClass_init((XClass*)prop);
    XClassGetVtable(prop) = XMqttAuthenticationProperties_class_init();
}

static void VAP_deinit(XMqttAuthenticationProperties* prop)
{
    if (!prop) return;
    if (prop->m_authenticationMethod) { XString_delete_base(prop->m_authenticationMethod); prop->m_authenticationMethod = NULL; }
    if (prop->m_authenticationData) { XByteArray_delete_base(prop->m_authenticationData); prop->m_authenticationData = NULL; }
    if (prop->m_reason) { XString_delete_base(prop->m_reason); prop->m_reason = NULL; }
    if (prop->m_userProperties) { XMqttUserProperties_delete_base(prop->m_userProperties); prop->m_userProperties = NULL; }
    XClass_Deinit_Parent(XClass, prop);
}

static void VAP_copy(XMqttAuthenticationProperties* dest, const XMqttAuthenticationProperties* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttAuthenticationProperties_init(dest);
    else {
        if (dest->m_authenticationMethod) XString_delete_base(dest->m_authenticationMethod);
        if (dest->m_authenticationData) XByteArray_delete_base(dest->m_authenticationData);
        if (dest->m_reason) XString_delete_base(dest->m_reason);
        if (dest->m_userProperties) XMqttUserProperties_delete_base(dest->m_userProperties);
        dest->m_authenticationMethod = NULL; dest->m_authenticationData = NULL;
        dest->m_reason = NULL; dest->m_userProperties = NULL;
    }
    if (src->m_authenticationMethod) dest->m_authenticationMethod = XString_create_copy(src->m_authenticationMethod);
    if (src->m_authenticationData) dest->m_authenticationData = XByteArray_create_copy(src->m_authenticationData);
    if (src->m_reason) dest->m_reason = XString_create_copy(src->m_reason);
    if (src->m_userProperties) dest->m_userProperties = (XMqttUserProperties*)XVector_create_copy((XVector*)src->m_userProperties);
}

static void VAP_move(XMqttAuthenticationProperties* dest, XMqttAuthenticationProperties* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttAuthenticationProperties_init(dest);
    if (dest->m_authenticationMethod) XString_delete_base(dest->m_authenticationMethod);
    if (dest->m_authenticationData) XByteArray_delete_base(dest->m_authenticationData);
    if (dest->m_reason) XString_delete_base(dest->m_reason);
    if (dest->m_userProperties) XMqttUserProperties_delete_base(dest->m_userProperties);
    dest->m_authenticationMethod = src->m_authenticationMethod;
    dest->m_authenticationData = src->m_authenticationData;
    dest->m_reason = src->m_reason;
    dest->m_userProperties = src->m_userProperties;
    src->m_authenticationMethod = NULL; src->m_authenticationData = NULL;
    src->m_reason = NULL; src->m_userProperties = NULL;
}

const XString* XMqttAuthenticationProperties_authenticationMethod_const(const XMqttAuthenticationProperties* prop) { return prop ? prop->m_authenticationMethod : NULL; }
XString* XMqttAuthenticationProperties_authenticationMethod(const XMqttAuthenticationProperties* prop) { if (!prop || !prop->m_authenticationMethod) return NULL; return XString_create_copy(prop->m_authenticationMethod); }
void XMqttAuthenticationProperties_setAuthenticationMethod(XMqttAuthenticationProperties* prop, const char* method) { if (prop) { if (prop->m_authenticationMethod) { XString_delete_base(prop->m_authenticationMethod); } prop->m_authenticationMethod = method ? XString_create_utf8(method) : NULL; } }
const XByteArray* XMqttAuthenticationProperties_authenticationData_const(const XMqttAuthenticationProperties* prop) { return prop ? prop->m_authenticationData : NULL; }
XByteArray* XMqttAuthenticationProperties_authenticationData(const XMqttAuthenticationProperties* prop) { if (!prop || !prop->m_authenticationData) return NULL; return XByteArray_create_copy(prop->m_authenticationData); }
void XMqttAuthenticationProperties_setAuthenticationData(XMqttAuthenticationProperties* prop, const uint8_t* data, size_t len) { if (prop) { if (prop->m_authenticationData) { XByteArray_delete_base(prop->m_authenticationData); } prop->m_authenticationData = (data && len) ? XByteArray_create_with_data((const char*)data, len) : NULL; } }
const XString* XMqttAuthenticationProperties_reason_const(const XMqttAuthenticationProperties* prop) { return prop ? prop->m_reason : NULL; }
XString* XMqttAuthenticationProperties_reason(const XMqttAuthenticationProperties* prop) { if (!prop || !prop->m_reason) return NULL; return XString_create_copy(prop->m_reason); }
void XMqttAuthenticationProperties_setReason(XMqttAuthenticationProperties* prop, const char* r) { if (prop) { if (prop->m_reason) { XString_delete_base(prop->m_reason); } prop->m_reason = r ? XString_create_utf8(r) : NULL; } }
const XMqttUserProperties* XMqttAuthenticationProperties_userProperties_const(const XMqttAuthenticationProperties* prop) { return prop ? prop->m_userProperties : NULL; }
XMqttUserProperties* XMqttAuthenticationProperties_userProperties(const XMqttAuthenticationProperties* prop) { if (!prop || !prop->m_userProperties) return NULL; return (XMqttUserProperties*)XVector_create_copy((XVector*)prop->m_userProperties); }
void XMqttAuthenticationProperties_setUserProperties(XMqttAuthenticationProperties* prop, const XMqttUserProperties* user) { if (prop) { if (prop->m_userProperties) { XMqttUserProperties_delete_base(prop->m_userProperties); } prop->m_userProperties = user ? (XMqttUserProperties*)XVector_create_copy((XVector*)user) : NULL; } }

#endif /* XMQTT_PROPERTIES_ON */
#endif /* XMQTT_ON */
#endif /* XPROTOCOL_ON */
