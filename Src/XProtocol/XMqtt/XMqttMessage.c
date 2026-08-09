#include "XMqtt_config.h"
#if XPROTOCOL_ON
#if XMQTT_ON
#if XMQTT_CLIENT_ON
#include "XMqttMessage.h"
#include "XMemory.h"
#include <string.h>

static void VMSG_deinit(XMqttMessage* msg);
static void VMSG_copy(XMqttMessage* dest, const XMqttMessage* src);
static void VMSG_move(XMqttMessage* dest, XMqttMessage* src);

XVtable* XMqttMessage_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMqttMessage)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VMSG_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VMSG_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VMSG_deinit);
    return XVTABLE_DEFAULT;
}

XMqttMessage* XMqttMessage_create(void)
{
    XMqttMessage* msg = (XMqttMessage*)XMalloc_System(sizeof(XMqttMessage));
    if (msg) { XMqttMessage_init(msg); Set_Class_MemoryFree(msg, XFree_System); }
    return msg;
}

XMqttMessage* XMqttMessage_create_full(const char* topic, const uint8_t* payload, size_t payloadLen,
                                        uint16_t id, uint8_t qos, bool dup, bool retain)
{
    XMqttMessage* msg = XMqttMessage_create();
    if (msg) XMqttMessage_init_full(msg, topic, payload, payloadLen, id, qos, dup, retain);
    return msg;
}

XMqttMessage* XMqttMessage_create_copy(const XMqttMessage* other)
{
    if (!other) return NULL;
    XMqttMessage* msg = XMqttMessage_create();
    if (msg) XMqttMessage_copy_base(msg, other);
    return msg;
}

void XMqttMessage_init(XMqttMessage* msg)
{
    if (!msg) return;
    memset(msg, 0, sizeof(XMqttMessage));
    XClass_init((XClass*)msg);
    XClassGetVtable(msg) = XMqttMessage_class_init();
}

void XMqttMessage_init_full(XMqttMessage* msg, const char* topic, const uint8_t* payload, size_t payloadLen,
                             uint16_t id, uint8_t qos, bool dup, bool retain)
{
    if (!msg) return;
    XMqttMessage_init(msg);
    if (topic) msg->m_topic = XMqttTopicName_create(topic);
    if (payload && payloadLen) msg->m_payload = XByteArray_create_with_data((const char*)payload, payloadLen);
    msg->m_id = id;
    msg->m_qos = qos;
    msg->m_duplicate = dup;
    msg->m_retain = retain;
}

static void VMSG_deinit(XMqttMessage* msg)
{
    if (!msg) return;
    if (msg->m_topic) { XMqttTopicName_delete_base(msg->m_topic); msg->m_topic = NULL; }
    if (msg->m_payload) { XByteArray_delete_base(msg->m_payload); msg->m_payload = NULL; }
    if (msg->m_publishProperties) { XMqttPublishProperties_delete_base(msg->m_publishProperties); msg->m_publishProperties = NULL; }
    XClass_Deinit_Parent(XClass, msg);
}

static void VMSG_copy(XMqttMessage* dest, const XMqttMessage* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttMessage_init(dest);
    else {
        if (dest->m_topic) XMqttTopicName_delete_base(dest->m_topic);
        if (dest->m_payload) XByteArray_delete_base(dest->m_payload);
        if (dest->m_publishProperties) XMqttPublishProperties_delete_base(dest->m_publishProperties);
        dest->m_topic = NULL; dest->m_payload = NULL; dest->m_publishProperties = NULL;
    }
    if (src->m_topic) dest->m_topic = XMqttTopicName_create_copy(src->m_topic);
    if (src->m_payload) dest->m_payload = XByteArray_create_copy(src->m_payload);
    dest->m_id = src->m_id;
    dest->m_qos = src->m_qos;
    dest->m_duplicate = src->m_duplicate;
    dest->m_retain = src->m_retain;
    if (src->m_publishProperties) dest->m_publishProperties = XMqttPublishProperties_create_copy(src->m_publishProperties);
}

static void VMSG_move(XMqttMessage* dest, XMqttMessage* src)
{
    if (!dest || !src) return;
    if (dest == src) return;
    if (XClassIsVtableNull(dest))
        XMqttMessage_init(dest);
    else {
        if (dest->m_topic) XMqttTopicName_delete_base(dest->m_topic);
        if (dest->m_payload) XByteArray_delete_base(dest->m_payload);
        if (dest->m_publishProperties) XMqttPublishProperties_delete_base(dest->m_publishProperties);
    }
    dest->m_topic = src->m_topic;
    dest->m_payload = src->m_payload;
    dest->m_id = src->m_id;
    dest->m_qos = src->m_qos;
    dest->m_duplicate = src->m_duplicate;
    dest->m_retain = src->m_retain;
    dest->m_publishProperties = src->m_publishProperties;
    src->m_topic = NULL;
    src->m_payload = NULL;
    src->m_publishProperties = NULL;
    src->m_id = 0;
    src->m_qos = 0;
    src->m_duplicate = false;
    src->m_retain = false;
}

const XByteArray* XMqttMessage_payload_const(const XMqttMessage* msg) { return msg ? msg->m_payload : NULL; }
XByteArray* XMqttMessage_payload(const XMqttMessage* msg) { if (!msg || !msg->m_payload) return NULL; return XByteArray_create_copy(msg->m_payload); }
uint8_t XMqttMessage_qos(const XMqttMessage* msg) { return msg ? msg->m_qos : 0; }
uint16_t XMqttMessage_id(const XMqttMessage* msg) { return msg ? msg->m_id : 0; }
XMqttTopicName* XMqttMessage_topic(const XMqttMessage* msg) { if (!msg || !msg->m_topic) return NULL; return XMqttTopicName_create_copy(msg->m_topic); }
const XMqttTopicName* XMqttMessage_topic_const(const XMqttMessage* msg) { return msg ? msg->m_topic : NULL; }
bool XMqttMessage_duplicate(const XMqttMessage* msg) { return msg ? msg->m_duplicate : false; }
bool XMqttMessage_retain(const XMqttMessage* msg) { return msg ? msg->m_retain : false; }
XMqttPublishProperties* XMqttMessage_publishProperties(const XMqttMessage* msg) { if (!msg || !msg->m_publishProperties) return NULL; return XMqttPublishProperties_create_copy(msg->m_publishProperties); }
const XMqttPublishProperties* XMqttMessage_publishProperties_const(const XMqttMessage* msg) { return msg ? msg->m_publishProperties : NULL; }

bool XMqttMessage_equal(const XMqttMessage* a, const XMqttMessage* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (!XMqttTopicName_equal(a->m_topic, b->m_topic)) return false;
    // 处理 NULL payload
    if (a->m_payload == NULL && b->m_payload == NULL) { /* 都为空，相等 */ }
    else if (a->m_payload == NULL || b->m_payload == NULL) return false;
    else if (XByteArray_compare(a->m_payload, b->m_payload) != 0) return false;
    return a->m_id == b->m_id && a->m_qos == b->m_qos &&
           a->m_duplicate == b->m_duplicate && a->m_retain == b->m_retain;
}

#endif /* XMQTT_CLIENT_ON */
#endif /* XMQTT_ON */
#endif /* XPROTOCOL_ON */
