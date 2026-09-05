#include "XMqtt_config.h"
#if XPROTOCOL_ON
#if XMQTT_ON
#if XMQTT_PROPERTIES_ON
#include "XMqttPublishProperties.h"
#include "XMemory.h"
#include <string.h>

// ==================== XMqttPublishProperties ====================

static void V_deinit(XMqttPublishProperties* prop);
static void V_copy(XMqttPublishProperties* dest, const XMqttPublishProperties* src);
static void V_move(XMqttPublishProperties* dest, XMqttPublishProperties* src);

XVtable* XMqttPublishProperties_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMqttPublishProperties)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, V_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, V_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, V_deinit);
    return XVTABLE_DEFAULT;
}

XMqttPublishProperties* XMqttPublishProperties_create_ex(XMemoryType memory)
{
    XMqttPublishProperties* p = (XMqttPublishProperties*)XMemory_malloc(sizeof(XMqttPublishProperties), memory);
    if (p) { XMqttPublishProperties_init(p); Set_Class_Memory(p, memory); Set_Class_IsHeap(p, true); }
    return p;
}

XMqttPublishProperties* XMqttPublishProperties_create_copy(const XMqttPublishProperties* other)
{
    if (!other) return NULL;
    XMqttPublishProperties* p = XMqttPublishProperties_create();
    if (p) XCopy(p, other);
    return p;
}

void XMqttPublishProperties_init(XMqttPublishProperties* prop)
{
    if (!prop) return;
    memset(prop, 0, sizeof(XMqttPublishProperties));
    XClass_init((XClass*)prop);
    XClassGetVtable(prop) = XMqttPublishProperties_class_init();
}

static void V_deinit(XMqttPublishProperties* prop)
{
    if (!prop) return;
    if (prop->m_responseTopic) { XString_delete_base(prop->m_responseTopic); prop->m_responseTopic = NULL; }
    if (prop->m_correlationData) { XByteArray_delete_base(prop->m_correlationData); prop->m_correlationData = NULL; }
    if (prop->m_userProperties) { XMqttUserProperties_delete_base(prop->m_userProperties); prop->m_userProperties = NULL; }
    if (prop->m_subscriptionIdentifiers) { XVector_delete_base(prop->m_subscriptionIdentifiers); prop->m_subscriptionIdentifiers = NULL; }
    if (prop->m_contentType) { XString_delete_base(prop->m_contentType); prop->m_contentType = NULL; }
    XClass_Deinit_Parent(XClass, prop);
}

static void V_copy(XMqttPublishProperties* dest, const XMqttPublishProperties* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttPublishProperties_init(dest);
    else {
        if (dest->m_responseTopic) XString_delete_base(dest->m_responseTopic);
        if (dest->m_correlationData) XByteArray_delete_base(dest->m_correlationData);
        if (dest->m_userProperties) XMqttUserProperties_delete_base(dest->m_userProperties);
        if (dest->m_subscriptionIdentifiers) XVector_delete_base(dest->m_subscriptionIdentifiers);
        if (dest->m_contentType) XString_delete_base(dest->m_contentType);
        dest->m_responseTopic = NULL; dest->m_correlationData = NULL;
        dest->m_userProperties = NULL; dest->m_subscriptionIdentifiers = NULL; dest->m_contentType = NULL;
    }
    dest->m_availableProperties = src->m_availableProperties;
    dest->m_payloadFormatIndicator = src->m_payloadFormatIndicator;
    dest->m_messageExpiryInterval = src->m_messageExpiryInterval;
    dest->m_topicAlias = src->m_topicAlias;
    if (src->m_responseTopic) dest->m_responseTopic = XString_create_copy(src->m_responseTopic);
    if (src->m_correlationData) dest->m_correlationData = XByteArray_create_copy(src->m_correlationData);
    if (src->m_userProperties) dest->m_userProperties = (XMqttUserProperties*)XVector_create_copy((XVector*)src->m_userProperties);
    if (src->m_subscriptionIdentifiers) dest->m_subscriptionIdentifiers = XVector_create_copy(src->m_subscriptionIdentifiers);
    if (src->m_contentType) dest->m_contentType = XString_create_copy(src->m_contentType);
}

static void V_move(XMqttPublishProperties* dest, XMqttPublishProperties* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttPublishProperties_init(dest);
    else {
        if (dest->m_responseTopic) XString_delete_base(dest->m_responseTopic);
        if (dest->m_correlationData) XByteArray_delete_base(dest->m_correlationData);
        if (dest->m_userProperties) XMqttUserProperties_delete_base(dest->m_userProperties);
        if (dest->m_subscriptionIdentifiers) XVector_delete_base(dest->m_subscriptionIdentifiers);
        if (dest->m_contentType) XString_delete_base(dest->m_contentType);
    }
    dest->m_availableProperties = src->m_availableProperties;
    dest->m_payloadFormatIndicator = src->m_payloadFormatIndicator;
    dest->m_messageExpiryInterval = src->m_messageExpiryInterval;
    dest->m_topicAlias = src->m_topicAlias;
    dest->m_responseTopic = src->m_responseTopic;
    dest->m_correlationData = src->m_correlationData;
    dest->m_userProperties = src->m_userProperties;
    dest->m_subscriptionIdentifiers = src->m_subscriptionIdentifiers;
    dest->m_contentType = src->m_contentType;
    src->m_availableProperties = 0;
    src->m_payloadFormatIndicator = 0;
    src->m_messageExpiryInterval = 0;
    src->m_topicAlias = 0;
    src->m_responseTopic = NULL;
    src->m_correlationData = NULL;
    src->m_userProperties = NULL;
    src->m_subscriptionIdentifiers = NULL;
    src->m_contentType = NULL;
}

uint32_t XMqttPublishProperties_availableProperties(const XMqttPublishProperties* prop) { return prop ? prop->m_availableProperties : 0; }
uint8_t XMqttPublishProperties_payloadFormatIndicator(const XMqttPublishProperties* prop) { return prop ? prop->m_payloadFormatIndicator : 0; }
void XMqttPublishProperties_setPayloadFormatIndicator(XMqttPublishProperties* prop, uint8_t indicator) { if (prop) { prop->m_payloadFormatIndicator = indicator; prop->m_availableProperties |= XMqttPublishProperties_PayloadFormatIndicator; } }
uint32_t XMqttPublishProperties_messageExpiryInterval(const XMqttPublishProperties* prop) { return prop ? prop->m_messageExpiryInterval : 0; }
void XMqttPublishProperties_setMessageExpiryInterval(XMqttPublishProperties* prop, uint32_t interval) { if (prop) { prop->m_messageExpiryInterval = interval; prop->m_availableProperties |= XMqttPublishProperties_MessageExpiryInterval; } }
uint16_t XMqttPublishProperties_topicAlias(const XMqttPublishProperties* prop) { return prop ? prop->m_topicAlias : 0; }
void XMqttPublishProperties_setTopicAlias(XMqttPublishProperties* prop, uint16_t alias) { if (prop && alias) { prop->m_topicAlias = alias; prop->m_availableProperties |= XMqttPublishProperties_TopicAlias; } }

const XString* XMqttPublishProperties_responseTopic_const(const XMqttPublishProperties* prop) { return prop ? prop->m_responseTopic : NULL; }
XString* XMqttPublishProperties_responseTopic(const XMqttPublishProperties* prop) { if (!prop || !prop->m_responseTopic) return NULL; return XString_create_copy(prop->m_responseTopic); }
void XMqttPublishProperties_setResponseTopic(XMqttPublishProperties* prop, const char* topic) { if (prop) { if (prop->m_responseTopic) { XString_delete_base(prop->m_responseTopic); } prop->m_responseTopic = topic ? XString_create_utf8(topic) : NULL; prop->m_availableProperties |= XMqttPublishProperties_ResponseTopic; } }

const XByteArray* XMqttPublishProperties_correlationData_const(const XMqttPublishProperties* prop) { return prop ? prop->m_correlationData : NULL; }
XByteArray* XMqttPublishProperties_correlationData(const XMqttPublishProperties* prop) { if (!prop || !prop->m_correlationData) return NULL; return XByteArray_create_copy(prop->m_correlationData); }
void XMqttPublishProperties_setCorrelationData(XMqttPublishProperties* prop, const uint8_t* data, size_t len) { if (prop) { if (prop->m_correlationData) { XByteArray_delete_base(prop->m_correlationData); } prop->m_correlationData = (data && len) ? XByteArray_create_with_data((const char*)data, len) : NULL; prop->m_availableProperties |= XMqttPublishProperties_CorrelationData; } }

const XMqttUserProperties* XMqttPublishProperties_userProperties_const(const XMqttPublishProperties* prop) { return prop ? prop->m_userProperties : NULL; }
XMqttUserProperties* XMqttPublishProperties_userProperties(const XMqttPublishProperties* prop) { if (!prop || !prop->m_userProperties) return NULL; return (XMqttUserProperties*)XVector_create_copy((XVector*)prop->m_userProperties); }
void XMqttPublishProperties_setUserProperties(XMqttPublishProperties* prop, const XMqttUserProperties* props) { if (prop) { if (prop->m_userProperties) { XMqttUserProperties_delete_base(prop->m_userProperties); } prop->m_userProperties = props ? (XMqttUserProperties*)XVector_create_copy((XVector*)props) : NULL; prop->m_availableProperties |= XMqttPublishProperties_UserProperty; } }

const XVector* XMqttPublishProperties_subscriptionIdentifiers_const(const XMqttPublishProperties* prop) { return prop ? prop->m_subscriptionIdentifiers : NULL; }
XVector* XMqttPublishProperties_subscriptionIdentifiers(const XMqttPublishProperties* prop) { if (!prop || !prop->m_subscriptionIdentifiers) return NULL; return XVector_create_copy(prop->m_subscriptionIdentifiers); }
void XMqttPublishProperties_setSubscriptionIdentifiers(XMqttPublishProperties* prop, const XVector* ids)
{
    if (!prop) return;
    if (ids) {
        for (size_t i = 0; i < XVector_size_base(ids); ++i) {
            const uint32_t* id = (const uint32_t*)XVector_at_base(ids, (int64_t)i);
            if (!id || *id == 0 || *id > 268435455U) return;
        }
    }
    if (prop->m_subscriptionIdentifiers) XVector_delete_base(prop->m_subscriptionIdentifiers);
    prop->m_subscriptionIdentifiers = ids ? XVector_create_copy(ids) : NULL;
    prop->m_availableProperties |= XMqttPublishProperties_SubscriptionIdentifier;
}

const XString* XMqttPublishProperties_contentType_const(const XMqttPublishProperties* prop) { return prop ? prop->m_contentType : NULL; }
XString* XMqttPublishProperties_contentType(const XMqttPublishProperties* prop) { if (!prop || !prop->m_contentType) return NULL; return XString_create_copy(prop->m_contentType); }
void XMqttPublishProperties_setContentType(XMqttPublishProperties* prop, const char* type) { if (prop) { if (prop->m_contentType) { XString_delete_base(prop->m_contentType); } prop->m_contentType = type ? XString_create_utf8(type) : NULL; prop->m_availableProperties |= XMqttPublishProperties_ContentType; } }

// ==================== XMqttMessageStatusProperties ====================

static void VMS_deinit(XMqttMessageStatusProperties* prop);
static void VMS_copy(XMqttMessageStatusProperties* dest, const XMqttMessageStatusProperties* src);
static void VMS_move(XMqttMessageStatusProperties* dest, XMqttMessageStatusProperties* src);

XVtable* XMqttMessageStatusProperties_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMqttMessageStatusProperties)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VMS_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VMS_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VMS_deinit);
    return XVTABLE_DEFAULT;
}

XMqttMessageStatusProperties* XMqttMessageStatusProperties_create_ex(XMemoryType memory)
{
    XMqttMessageStatusProperties* p = (XMqttMessageStatusProperties*)XMemory_malloc(sizeof(XMqttMessageStatusProperties), memory);
    if (p) { XMqttMessageStatusProperties_init(p); Set_Class_Memory(p, memory); Set_Class_IsHeap(p, true); }
    return p;
}

XMqttMessageStatusProperties* XMqttMessageStatusProperties_create_copy(const XMqttMessageStatusProperties* other)
{
    if (!other) return NULL;
    XMqttMessageStatusProperties* p = XMqttMessageStatusProperties_create();
    if (p) XCopy(p, other);
    return p;
}

void XMqttMessageStatusProperties_init(XMqttMessageStatusProperties* prop)
{
    if (!prop) return;
    memset(prop, 0, sizeof(XMqttMessageStatusProperties));
    XClass_init((XClass*)prop);
    XClassGetVtable(prop) = XMqttMessageStatusProperties_class_init();
}

static void VMS_deinit(XMqttMessageStatusProperties* prop)
{
    if (!prop) return;
    if (prop->m_reason) { XString_delete_base(prop->m_reason); prop->m_reason = NULL; }
    if (prop->m_userProperties) { XMqttUserProperties_delete_base(prop->m_userProperties); prop->m_userProperties = NULL; }
    XClass_Deinit_Parent(XClass, prop);
}

static void VMS_copy(XMqttMessageStatusProperties* dest, const XMqttMessageStatusProperties* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttMessageStatusProperties_init(dest);
    else {
        if (dest->m_reason) XString_delete_base(dest->m_reason);
        if (dest->m_userProperties) XMqttUserProperties_delete_base(dest->m_userProperties);
        dest->m_reason = NULL; dest->m_userProperties = NULL;
    }
    dest->m_reasonCode = src->m_reasonCode;
    if (src->m_reason) dest->m_reason = XString_create_copy(src->m_reason);
    if (src->m_userProperties) dest->m_userProperties = (XMqttUserProperties*)XVector_create_copy((XVector*)src->m_userProperties);
}

static void VMS_move(XMqttMessageStatusProperties* dest, XMqttMessageStatusProperties* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttMessageStatusProperties_init(dest);
    if (dest->m_reason) XString_delete_base(dest->m_reason);
    if (dest->m_userProperties) XMqttUserProperties_delete_base(dest->m_userProperties);
    dest->m_reasonCode = src->m_reasonCode;
    dest->m_reason = src->m_reason;
    dest->m_userProperties = src->m_userProperties;
    src->m_reasonCode = 0;
    src->m_reason = NULL;
    src->m_userProperties = NULL;
}

uint8_t XMqttMessageStatusProperties_reasonCode(const XMqttMessageStatusProperties* prop) { return prop ? prop->m_reasonCode : 0; }
const XString* XMqttMessageStatusProperties_reason_const(const XMqttMessageStatusProperties* prop) { return prop ? prop->m_reason : NULL; }
XString* XMqttMessageStatusProperties_reason(const XMqttMessageStatusProperties* prop) { if (!prop || !prop->m_reason) return NULL; return XString_create_copy(prop->m_reason); }
const XMqttUserProperties* XMqttMessageStatusProperties_userProperties_const(const XMqttMessageStatusProperties* prop) { return prop ? prop->m_userProperties : NULL; }
XMqttUserProperties* XMqttMessageStatusProperties_userProperties(const XMqttMessageStatusProperties* prop) { if (!prop || !prop->m_userProperties) return NULL; return (XMqttUserProperties*)XVector_create_copy((XVector*)prop->m_userProperties); }

#endif /* XMQTT_PROPERTIES_ON */
#endif /* XMQTT_ON */
#endif /* XPROTOCOL_ON */
