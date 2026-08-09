#include "XMqtt_config.h"
#if XPROTOCOL_ON
#if XMQTT_ON
#if XMQTT_PROPERTIES_ON
#include "XMqttConnectionProperties.h"
#include "XMemory.h"
#include <string.h>

// ==================== XMqttLastWillProperties ====================

static void VLW_deinit(XMqttLastWillProperties* prop);
static void VLW_copy(XMqttLastWillProperties* dest, const XMqttLastWillProperties* src);
static void VLW_move(XMqttLastWillProperties* dest, XMqttLastWillProperties* src);

XVtable* XMqttLastWillProperties_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XClass)
	XCLASS_SET_CLASS_NAME_DEFAULT("XMqttLastWillProperties");
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VLW_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VLW_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VLW_deinit);
    return XVTABLE_DEFAULT;
}

XMqttLastWillProperties* XMqttLastWillProperties_create(void)
{
    XMqttLastWillProperties* p = (XMqttLastWillProperties*)XMalloc_System(sizeof(XMqttLastWillProperties));
    if (p) { XMqttLastWillProperties_init(p); Set_Class_MemoryFree(p, XFree_System); }
    return p;
}

XMqttLastWillProperties* XMqttLastWillProperties_create_copy(const XMqttLastWillProperties* other)
{
    if (!other) return NULL;
    XMqttLastWillProperties* p = XMqttLastWillProperties_create();
    if (p) XMqttLastWillProperties_copy_base(p, other);
    return p;
}

void XMqttLastWillProperties_init(XMqttLastWillProperties* prop)
{
    if (!prop) return;
    memset(prop, 0, sizeof(XMqttLastWillProperties));
    XClass_init((XClass*)prop);
    XClassGetVtable(prop) = XMqttLastWillProperties_class_init();
}

static void VLW_deinit(XMqttLastWillProperties* prop)
{
    if (!prop) return;
    if (prop->m_contentType) { XString_delete_base(prop->m_contentType); prop->m_contentType = NULL; }
    if (prop->m_responseTopic) { XString_delete_base(prop->m_responseTopic); prop->m_responseTopic = NULL; }
    if (prop->m_correlationData) { XByteArray_delete_base(prop->m_correlationData); prop->m_correlationData = NULL; }
    if (prop->m_userProperties) { XMqttUserProperties_delete_base(prop->m_userProperties); prop->m_userProperties = NULL; }
    XClass_Deinit_Parent(XClass, prop);
}

static void VLW_copy(XMqttLastWillProperties* dest, const XMqttLastWillProperties* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttLastWillProperties_init(dest);
    else {
        if (dest->m_contentType) XString_delete_base(dest->m_contentType);
        if (dest->m_responseTopic) XString_delete_base(dest->m_responseTopic);
        if (dest->m_correlationData) XByteArray_delete_base(dest->m_correlationData);
        if (dest->m_userProperties) XMqttUserProperties_delete_base(dest->m_userProperties);
        dest->m_contentType = NULL; dest->m_responseTopic = NULL;
        dest->m_correlationData = NULL; dest->m_userProperties = NULL;
    }
    dest->m_willDelayInterval = src->m_willDelayInterval;
    dest->m_payloadFormatIndicator = src->m_payloadFormatIndicator;
    dest->m_messageExpiryInterval = src->m_messageExpiryInterval;
    if (src->m_contentType) dest->m_contentType = XString_create_copy(src->m_contentType);
    if (src->m_responseTopic) dest->m_responseTopic = XString_create_copy(src->m_responseTopic);
    if (src->m_correlationData) dest->m_correlationData = XByteArray_create_copy(src->m_correlationData);
    if (src->m_userProperties) dest->m_userProperties = (XMqttUserProperties*)XVector_create_copy((XVector*)src->m_userProperties);
}

static void VLW_move(XMqttLastWillProperties* dest, XMqttLastWillProperties* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttLastWillProperties_init(dest);
    if (dest->m_contentType) XString_delete_base(dest->m_contentType);
    if (dest->m_responseTopic) XString_delete_base(dest->m_responseTopic);
    if (dest->m_correlationData) XByteArray_delete_base(dest->m_correlationData);
    if (dest->m_userProperties) XMqttUserProperties_delete_base(dest->m_userProperties);
    dest->m_willDelayInterval = src->m_willDelayInterval;
    dest->m_payloadFormatIndicator = src->m_payloadFormatIndicator;
    dest->m_messageExpiryInterval = src->m_messageExpiryInterval;
    dest->m_contentType = src->m_contentType; dest->m_responseTopic = src->m_responseTopic;
    dest->m_correlationData = src->m_correlationData; dest->m_userProperties = src->m_userProperties;
    src->m_willDelayInterval = 0; src->m_payloadFormatIndicator = 0; src->m_messageExpiryInterval = 0;
    src->m_contentType = NULL; src->m_responseTopic = NULL; src->m_correlationData = NULL; src->m_userProperties = NULL;
}

uint32_t XMqttLastWillProperties_willDelayInterval(const XMqttLastWillProperties* prop) { return prop ? prop->m_willDelayInterval : 0; }
void XMqttLastWillProperties_setWillDelayInterval(XMqttLastWillProperties* prop, uint32_t delay) { if (prop) prop->m_willDelayInterval = delay; }
uint8_t XMqttLastWillProperties_payloadFormatIndicator(const XMqttLastWillProperties* prop) { return prop ? prop->m_payloadFormatIndicator : 0; }
void XMqttLastWillProperties_setPayloadFormatIndicator(XMqttLastWillProperties* prop, uint8_t p) { if (prop) prop->m_payloadFormatIndicator = p; }
uint32_t XMqttLastWillProperties_messageExpiryInterval(const XMqttLastWillProperties* prop) { return prop ? prop->m_messageExpiryInterval : 0; }
void XMqttLastWillProperties_setMessageExpiryInterval(XMqttLastWillProperties* prop, uint32_t expiry) { if (prop) prop->m_messageExpiryInterval = expiry; }
const XString* XMqttLastWillProperties_contentType_const(const XMqttLastWillProperties* prop) { return prop ? prop->m_contentType : NULL; }
XString* XMqttLastWillProperties_contentType(const XMqttLastWillProperties* prop) { if (!prop || !prop->m_contentType) return NULL; return XString_create_copy(prop->m_contentType); }
void XMqttLastWillProperties_setContentType(XMqttLastWillProperties* prop, const char* content) { if (prop) { if (prop->m_contentType) { XString_delete_base(prop->m_contentType); } prop->m_contentType = content ? XString_create_utf8(content) : NULL; } }
const XString* XMqttLastWillProperties_responseTopic_const(const XMqttLastWillProperties* prop) { return prop ? prop->m_responseTopic : NULL; }
XString* XMqttLastWillProperties_responseTopic(const XMqttLastWillProperties* prop) { if (!prop || !prop->m_responseTopic) return NULL; return XString_create_copy(prop->m_responseTopic); }
void XMqttLastWillProperties_setResponseTopic(XMqttLastWillProperties* prop, const char* response) { if (prop) { if (prop->m_responseTopic) { XString_delete_base(prop->m_responseTopic); } prop->m_responseTopic = response ? XString_create_utf8(response) : NULL; } }
const XByteArray* XMqttLastWillProperties_correlationData_const(const XMqttLastWillProperties* prop) { return prop ? prop->m_correlationData : NULL; }
XByteArray* XMqttLastWillProperties_correlationData(const XMqttLastWillProperties* prop) { if (!prop || !prop->m_correlationData) return NULL; return XByteArray_create_copy(prop->m_correlationData); }
void XMqttLastWillProperties_setCorrelationData(XMqttLastWillProperties* prop, const uint8_t* data, size_t len) { if (prop) { if (prop->m_correlationData) { XByteArray_delete_base(prop->m_correlationData); } prop->m_correlationData = (data && len) ? XByteArray_create_with_data((const char*)data, len) : NULL; } }
const XMqttUserProperties* XMqttLastWillProperties_userProperties_const(const XMqttLastWillProperties* prop) { return prop ? prop->m_userProperties : NULL; }
XMqttUserProperties* XMqttLastWillProperties_userProperties(const XMqttLastWillProperties* prop) { if (!prop || !prop->m_userProperties) return NULL; return (XMqttUserProperties*)XVector_create_copy((XVector*)prop->m_userProperties); }
void XMqttLastWillProperties_setUserProperties(XMqttLastWillProperties* prop, const XMqttUserProperties* props) { if (prop) { if (prop->m_userProperties) { XMqttUserProperties_delete_base(prop->m_userProperties); } prop->m_userProperties = props ? (XMqttUserProperties*)XVector_create_copy((XVector*)props) : NULL; } }

// ==================== XMqttConnectionProperties ====================

static void VCP_deinit(XMqttConnectionProperties* prop);
static void VCP_copy(XMqttConnectionProperties* dest, const XMqttConnectionProperties* src);
static void VCP_move(XMqttConnectionProperties* dest, XMqttConnectionProperties* src);

XVtable* XMqttConnectionProperties_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XClass)
	XCLASS_SET_CLASS_NAME_DEFAULT("XMqttConnectionProperties");
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VCP_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VCP_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VCP_deinit);
    return XVTABLE_DEFAULT;
}

XMqttConnectionProperties* XMqttConnectionProperties_create(void)
{
    XMqttConnectionProperties* p = (XMqttConnectionProperties*)XMalloc_System(sizeof(XMqttConnectionProperties));
    if (p) { XMqttConnectionProperties_init(p); Set_Class_MemoryFree(p, XFree_System); }
    return p;
}

XMqttConnectionProperties* XMqttConnectionProperties_create_copy(const XMqttConnectionProperties* other)
{
    if (!other) return NULL;
    XMqttConnectionProperties* p = XMqttConnectionProperties_create();
    if (p) XMqttConnectionProperties_copy_base(p, other);
    return p;
}

void XMqttConnectionProperties_init(XMqttConnectionProperties* prop)
{
    if (!prop) return;
    memset(prop, 0, sizeof(XMqttConnectionProperties));
    XClass_init((XClass*)prop);
    XClassGetVtable(prop) = XMqttConnectionProperties_class_init();
    prop->m_maximumReceive = UINT16_MAX;
    prop->m_maximumPacketSize = UINT32_MAX;
    prop->m_requestProblemInformation = true;
}

static void VCP_deinit(XMqttConnectionProperties* prop)
{
    if (!prop) return;
    if (prop->m_userProperties) { XMqttUserProperties_delete_base(prop->m_userProperties); prop->m_userProperties = NULL; }
    if (prop->m_authenticationMethod) { XString_delete_base(prop->m_authenticationMethod); prop->m_authenticationMethod = NULL; }
    if (prop->m_authenticationData) { XByteArray_delete_base(prop->m_authenticationData); prop->m_authenticationData = NULL; }
    XClass_Deinit_Parent(XClass, prop);
}

static void VCP_copy(XMqttConnectionProperties* dest, const XMqttConnectionProperties* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttConnectionProperties_init(dest);
    else {
        if (dest->m_userProperties) XMqttUserProperties_delete_base(dest->m_userProperties);
        if (dest->m_authenticationMethod) XString_delete_base(dest->m_authenticationMethod);
        if (dest->m_authenticationData) XByteArray_delete_base(dest->m_authenticationData);
        dest->m_userProperties = NULL; dest->m_authenticationMethod = NULL; dest->m_authenticationData = NULL;
    }
    dest->m_sessionExpiryInterval = src->m_sessionExpiryInterval;
    dest->m_maximumReceive = src->m_maximumReceive;
    dest->m_maximumPacketSize = src->m_maximumPacketSize;
    dest->m_maximumTopicAlias = src->m_maximumTopicAlias;
    dest->m_requestResponseInformation = src->m_requestResponseInformation;
    dest->m_requestProblemInformation = src->m_requestProblemInformation;
    if (src->m_userProperties) dest->m_userProperties = (XMqttUserProperties*)XVector_create_copy((XVector*)src->m_userProperties);
    if (src->m_authenticationMethod) dest->m_authenticationMethod = XString_create_copy(src->m_authenticationMethod);
    if (src->m_authenticationData) dest->m_authenticationData = XByteArray_create_copy(src->m_authenticationData);
}

static void VCP_move(XMqttConnectionProperties* dest, XMqttConnectionProperties* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttConnectionProperties_init(dest);
    if (dest->m_userProperties) XMqttUserProperties_delete_base(dest->m_userProperties);
    if (dest->m_authenticationMethod) XString_delete_base(dest->m_authenticationMethod);
    if (dest->m_authenticationData) XByteArray_delete_base(dest->m_authenticationData);
    dest->m_sessionExpiryInterval = src->m_sessionExpiryInterval;
    dest->m_maximumReceive = src->m_maximumReceive;
    dest->m_maximumPacketSize = src->m_maximumPacketSize;
    dest->m_maximumTopicAlias = src->m_maximumTopicAlias;
    dest->m_requestResponseInformation = src->m_requestResponseInformation;
    dest->m_requestProblemInformation = src->m_requestProblemInformation;
    dest->m_userProperties = src->m_userProperties;
    dest->m_authenticationMethod = src->m_authenticationMethod;
    dest->m_authenticationData = src->m_authenticationData;
    src->m_sessionExpiryInterval = 0; src->m_maximumReceive = 0; src->m_maximumPacketSize = 0;
    src->m_maximumTopicAlias = 0; src->m_requestResponseInformation = false; src->m_requestProblemInformation = false;
    src->m_userProperties = NULL; src->m_authenticationMethod = NULL; src->m_authenticationData = NULL;
}

uint32_t XMqttConnectionProperties_sessionExpiryInterval(const XMqttConnectionProperties* prop) { return prop ? prop->m_sessionExpiryInterval : 0; }
void XMqttConnectionProperties_setSessionExpiryInterval(XMqttConnectionProperties* prop, uint32_t expiry) { if (prop) prop->m_sessionExpiryInterval = expiry; }
uint16_t XMqttConnectionProperties_maximumReceive(const XMqttConnectionProperties* prop) { return prop ? prop->m_maximumReceive : 0; }
void XMqttConnectionProperties_setMaximumReceive(XMqttConnectionProperties* prop, uint16_t maxRecv) { if (prop && maxRecv) prop->m_maximumReceive = maxRecv; }
uint32_t XMqttConnectionProperties_maximumPacketSize(const XMqttConnectionProperties* prop) { return prop ? prop->m_maximumPacketSize : 0; }
void XMqttConnectionProperties_setMaximumPacketSize(XMqttConnectionProperties* prop, uint32_t packetSize) { if (prop && packetSize) prop->m_maximumPacketSize = packetSize; }
uint16_t XMqttConnectionProperties_maximumTopicAlias(const XMqttConnectionProperties* prop) { return prop ? prop->m_maximumTopicAlias : 0; }
void XMqttConnectionProperties_setMaximumTopicAlias(XMqttConnectionProperties* prop, uint16_t alias) { if (prop) prop->m_maximumTopicAlias = alias; }
bool XMqttConnectionProperties_requestResponseInformation(const XMqttConnectionProperties* prop) { return prop ? prop->m_requestResponseInformation : false; }
void XMqttConnectionProperties_setRequestResponseInformation(XMqttConnectionProperties* prop, bool response) { if (prop) prop->m_requestResponseInformation = response; }
bool XMqttConnectionProperties_requestProblemInformation(const XMqttConnectionProperties* prop) { return prop ? prop->m_requestProblemInformation : false; }
void XMqttConnectionProperties_setRequestProblemInformation(XMqttConnectionProperties* prop, bool problem) { if (prop) prop->m_requestProblemInformation = problem; }
const XMqttUserProperties* XMqttConnectionProperties_userProperties_const(const XMqttConnectionProperties* prop) { return prop ? prop->m_userProperties : NULL; }
XMqttUserProperties* XMqttConnectionProperties_userProperties(const XMqttConnectionProperties* prop) { if (!prop || !prop->m_userProperties) return NULL; return (XMqttUserProperties*)XVector_create_copy((XVector*)prop->m_userProperties); }
void XMqttConnectionProperties_setUserProperties(XMqttConnectionProperties* prop, const XMqttUserProperties* props) { if (prop) { if (prop->m_userProperties) { XMqttUserProperties_delete_base(prop->m_userProperties); } prop->m_userProperties = props ? (XMqttUserProperties*)XVector_create_copy((XVector*)props) : NULL; } }
const XString* XMqttConnectionProperties_authenticationMethod_const(const XMqttConnectionProperties* prop) { return prop ? prop->m_authenticationMethod : NULL; }
XString* XMqttConnectionProperties_authenticationMethod(const XMqttConnectionProperties* prop) { if (!prop || !prop->m_authenticationMethod) return NULL; return XString_create_copy(prop->m_authenticationMethod); }
void XMqttConnectionProperties_setAuthenticationMethod(XMqttConnectionProperties* prop, const char* authMethod) { if (prop) { if (prop->m_authenticationMethod) { XString_delete_base(prop->m_authenticationMethod); } prop->m_authenticationMethod = authMethod ? XString_create_utf8(authMethod) : NULL; } }
const XByteArray* XMqttConnectionProperties_authenticationData_const(const XMqttConnectionProperties* prop) { return prop ? prop->m_authenticationData : NULL; }
XByteArray* XMqttConnectionProperties_authenticationData(const XMqttConnectionProperties* prop) { if (!prop || !prop->m_authenticationData) return NULL; return XByteArray_create_copy(prop->m_authenticationData); }
void XMqttConnectionProperties_setAuthenticationData(XMqttConnectionProperties* prop, const uint8_t* data, size_t len) { if (prop) { if (prop->m_authenticationData) { XByteArray_delete_base(prop->m_authenticationData); } prop->m_authenticationData = (data && len) ? XByteArray_create_with_data((const char*)data, len) : NULL; } }

// ==================== XMqttServerConnectionProperties ====================

static void VSCP_deinit(XMqttServerConnectionProperties* prop);
static void VSCP_copy(XMqttServerConnectionProperties* dest, const XMqttServerConnectionProperties* src);
static void VSCP_move(XMqttServerConnectionProperties* dest, XMqttServerConnectionProperties* src);

XVtable* XMqttServerConnectionProperties_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMqttConnectionProperties)
	XCLASS_SET_CLASS_NAME_DEFAULT("XMqttServerConnectionProperties");
    XVTABLE_INHERIT_XCLASS(XMqttConnectionProperties);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VSCP_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VSCP_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VSCP_deinit);
    return XVTABLE_DEFAULT;
}

XMqttServerConnectionProperties* XMqttServerConnectionProperties_create(void)
{
    XMqttServerConnectionProperties* p = (XMqttServerConnectionProperties*)XMalloc_System(sizeof(XMqttServerConnectionProperties));
    if (p) { XMqttServerConnectionProperties_init(p); Set_Class_MemoryFree(p, XFree_System); }
    return p;
}

XMqttServerConnectionProperties* XMqttServerConnectionProperties_create_copy(const XMqttServerConnectionProperties* other)
{
    if (!other) return NULL;
    XMqttServerConnectionProperties* p = XMqttServerConnectionProperties_create();
    if (p) XMqttServerConnectionProperties_copy_base(p, other);
    return p;
}

void XMqttServerConnectionProperties_init(XMqttServerConnectionProperties* prop)
{
    if (!prop) return;
    memset(prop, 0, sizeof(XMqttServerConnectionProperties));
    XMqttConnectionProperties_init((XMqttConnectionProperties*)prop);
    XClassGetVtable(prop) = XMqttServerConnectionProperties_class_init();
    prop->m_maximumQoS = 2;
    prop->m_retainAvailable = true;
    prop->m_wildcardSupported = true;
    prop->m_subscriptionIdentifierSupported = true;
    prop->m_sharedSubscriptionSupported = true;
}

static void VSCP_deinit(XMqttServerConnectionProperties* prop)
{
    if (!prop) return;
    if (prop->m_reason) { XString_delete_base(prop->m_reason); prop->m_reason = NULL; }
    if (prop->m_responseInformation) { XString_delete_base(prop->m_responseInformation); prop->m_responseInformation = NULL; }
    if (prop->m_serverReference) { XString_delete_base(prop->m_serverReference); prop->m_serverReference = NULL; }
    XClass_Deinit_Parent(XMqttConnectionProperties, prop);
}

static void VSCP_copy(XMqttServerConnectionProperties* dest, const XMqttServerConnectionProperties* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttServerConnectionProperties_init(dest);
    else {
        if (dest->m_reason) XString_delete_base(dest->m_reason);
        if (dest->m_responseInformation) XString_delete_base(dest->m_responseInformation);
        if (dest->m_serverReference) XString_delete_base(dest->m_serverReference);
        dest->m_reason = NULL; dest->m_responseInformation = NULL; dest->m_serverReference = NULL;
    }
    XMqttConnectionProperties_copy_base((XMqttConnectionProperties*)dest, (const XMqttConnectionProperties*)src);
    dest->m_valid = src->m_valid;
    dest->m_availableProperties = src->m_availableProperties;
    dest->m_maximumQoS = src->m_maximumQoS;
    dest->m_retainAvailable = src->m_retainAvailable;
    dest->m_clientIdAssigned = src->m_clientIdAssigned;
    dest->m_reasonCode = src->m_reasonCode;
    dest->m_wildcardSupported = src->m_wildcardSupported;
    dest->m_subscriptionIdentifierSupported = src->m_subscriptionIdentifierSupported;
    dest->m_sharedSubscriptionSupported = src->m_sharedSubscriptionSupported;
    dest->m_serverKeepAlive = src->m_serverKeepAlive;
    if (src->m_reason) dest->m_reason = XString_create_copy(src->m_reason);
    if (src->m_responseInformation) dest->m_responseInformation = XString_create_copy(src->m_responseInformation);
    if (src->m_serverReference) dest->m_serverReference = XString_create_copy(src->m_serverReference);
}

static void VSCP_move(XMqttServerConnectionProperties* dest, XMqttServerConnectionProperties* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttServerConnectionProperties_init(dest);
    if (dest->m_reason) XString_delete_base(dest->m_reason);
    if (dest->m_responseInformation) XString_delete_base(dest->m_responseInformation);
    if (dest->m_serverReference) XString_delete_base(dest->m_serverReference);
    XMqttConnectionProperties_move_base(&dest->m_base, &src->m_base);
    dest->m_valid = src->m_valid; dest->m_availableProperties = src->m_availableProperties;
    dest->m_maximumQoS = src->m_maximumQoS; dest->m_retainAvailable = src->m_retainAvailable;
    dest->m_clientIdAssigned = src->m_clientIdAssigned; dest->m_reason = src->m_reason;
    dest->m_reasonCode = src->m_reasonCode; dest->m_wildcardSupported = src->m_wildcardSupported;
    dest->m_subscriptionIdentifierSupported = src->m_subscriptionIdentifierSupported;
    dest->m_sharedSubscriptionSupported = src->m_sharedSubscriptionSupported;
    dest->m_serverKeepAlive = src->m_serverKeepAlive;
    dest->m_responseInformation = src->m_responseInformation; dest->m_serverReference = src->m_serverReference;
    src->m_valid = false; src->m_availableProperties = 0; src->m_maximumQoS = 0;
    src->m_retainAvailable = false; src->m_clientIdAssigned = false; src->m_reason = NULL;
    src->m_reasonCode = 0; src->m_wildcardSupported = false; src->m_subscriptionIdentifierSupported = false;
    src->m_sharedSubscriptionSupported = false; src->m_serverKeepAlive = 0;
    src->m_responseInformation = NULL; src->m_serverReference = NULL;
}

uint32_t XMqttServerConnectionProperties_availableProperties(const XMqttServerConnectionProperties* prop) { return prop ? prop->m_availableProperties : 0; }
bool XMqttServerConnectionProperties_isValid(const XMqttServerConnectionProperties* prop) { return prop && prop->m_valid; }
uint8_t XMqttServerConnectionProperties_maximumQoS(const XMqttServerConnectionProperties* prop) { return prop ? prop->m_maximumQoS : 0; }
bool XMqttServerConnectionProperties_retainAvailable(const XMqttServerConnectionProperties* prop) { return prop ? prop->m_retainAvailable : false; }
bool XMqttServerConnectionProperties_clientIdAssigned(const XMqttServerConnectionProperties* prop) { return prop ? prop->m_clientIdAssigned : false; }
const XString* XMqttServerConnectionProperties_reason_const(const XMqttServerConnectionProperties* prop) { return prop ? prop->m_reason : NULL; }
XString* XMqttServerConnectionProperties_reason(const XMqttServerConnectionProperties* prop) { if (!prop || !prop->m_reason) return NULL; return XString_create_copy(prop->m_reason); }
uint8_t XMqttServerConnectionProperties_reasonCode(const XMqttServerConnectionProperties* prop) { return prop ? prop->m_reasonCode : 0; }
bool XMqttServerConnectionProperties_wildcardSupported(const XMqttServerConnectionProperties* prop) { return prop ? prop->m_wildcardSupported : false; }
bool XMqttServerConnectionProperties_subscriptionIdentifierSupported(const XMqttServerConnectionProperties* prop) { return prop ? prop->m_subscriptionIdentifierSupported : false; }
bool XMqttServerConnectionProperties_sharedSubscriptionSupported(const XMqttServerConnectionProperties* prop) { return prop ? prop->m_sharedSubscriptionSupported : false; }
uint16_t XMqttServerConnectionProperties_serverKeepAlive(const XMqttServerConnectionProperties* prop) { return prop ? prop->m_serverKeepAlive : 0; }
const XString* XMqttServerConnectionProperties_responseInformation_const(const XMqttServerConnectionProperties* prop) { return prop ? prop->m_responseInformation : NULL; }
XString* XMqttServerConnectionProperties_responseInformation(const XMqttServerConnectionProperties* prop) { if (!prop || !prop->m_responseInformation) return NULL; return XString_create_copy(prop->m_responseInformation); }
const XString* XMqttServerConnectionProperties_serverReference_const(const XMqttServerConnectionProperties* prop) { return prop ? prop->m_serverReference : NULL; }
XString* XMqttServerConnectionProperties_serverReference(const XMqttServerConnectionProperties* prop) { if (!prop || !prop->m_serverReference) return NULL; return XString_create_copy(prop->m_serverReference); }

#endif /* XMQTT_PROPERTIES_ON */
#endif /* XMQTT_ON */
#endif /* XPROTOCOL_ON */
