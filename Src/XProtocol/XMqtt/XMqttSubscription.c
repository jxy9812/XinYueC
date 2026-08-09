#include "XMqtt_config.h"
#if XPROTOCOL_ON
#if XMQTT_ON
#if XMQTT_SUBSCRIPTION_ON
#include "XMqttSubscription.h"
#include "XMemory.h"
#include "XMqttClient.h"
#include <string.h>

static void VXMQ_deinit(XMqttSubscription* sub);
static void VXMQ_unsubscribe(XMqttSubscription* sub);

XVtable* XMqttSubscription_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMqttSubscription)
	XCLASS_SET_CLASS_NAME_DEFAULT("XMqttSubscription");
    XVTABLE_INHERIT_XCLASS(XObject);

    void* table[] = {
        VXMQ_unsubscribe
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXMQ_deinit);

    return XVTABLE_DEFAULT;
}

XMqttSubscription* XMqttSubscription_create(const XMqttTopicFilter* topic, uint8_t qos)
{
    XMqttSubscription* sub = (XMqttSubscription*)XMalloc_System(sizeof(XMqttSubscription));
    if (sub) {
        XMqttSubscription_init(sub, topic, qos);
        Set_Class_MemoryFree(sub, XFree_System);
    }
    return sub;
}

void XMqttSubscription_init(XMqttSubscription* sub, const XMqttTopicFilter* topic, uint8_t qos)
{
    if (!sub) return;
    memset(sub, 0, sizeof(XMqttSubscription));
    XObject_init((XObject*)sub);
    XClassGetVtable(sub) = XMqttSubscription_class_init();

    sub->m_state = XMqttSubscription_Unsubscribed;
    sub->m_qos = qos;
    if (topic) sub->m_topic = XMqttTopicFilter_create_copy(topic);
}

static void VXMQ_deinit(XMqttSubscription* sub)
{
    if (!sub) return;
    if (sub->m_topic) { XMqttTopicFilter_delete_base(sub->m_topic); sub->m_topic = NULL; }
    if (sub->m_reason) { XString_delete_base(sub->m_reason); sub->m_reason = NULL; }
    if (sub->m_sharedSubscriptionName) { XString_delete_base(sub->m_sharedSubscriptionName); sub->m_sharedSubscriptionName = NULL; }
    if (sub->m_userProperties) { XMqttUserProperties_delete_base(sub->m_userProperties); sub->m_userProperties = NULL; }
    sub->m_client = NULL;
    XClass_Deinit_Parent(XObject, sub);
}

XMqttSubscription_State XMqttSubscription_state(const XMqttSubscription* sub)
{
    return sub ? (XMqttSubscription_State)sub->m_state : XMqttSubscription_Unsubscribed;
}

XMqttTopicFilter* XMqttSubscription_topic(const XMqttSubscription* sub)
{
    if (!sub || !sub->m_topic) return NULL;
    return XMqttTopicFilter_create_copy(sub->m_topic);
}

const XMqttTopicFilter* XMqttSubscription_topic_const(const XMqttSubscription* sub)
{
    return sub ? sub->m_topic : NULL;
}

uint8_t XMqttSubscription_qos(const XMqttSubscription* sub) { return sub ? sub->m_qos : 0; }

XString* XMqttSubscription_reason(const XMqttSubscription* sub)
{
    if (!sub || !sub->m_reason) return NULL;
    return XString_create_copy(sub->m_reason);
}

const XString* XMqttSubscription_reason_const(const XMqttSubscription* sub) { return sub ? sub->m_reason : NULL; }

uint8_t XMqttSubscription_reasonCode(const XMqttSubscription* sub) { return sub ? sub->m_reasonCode : 0; }

XMqttUserProperties* XMqttSubscription_userProperties(const XMqttSubscription* sub)
{
    if (!sub || !sub->m_userProperties) return NULL;
    return (XMqttUserProperties*)XVector_create_copy((XVector*)sub->m_userProperties);
}

const XMqttUserProperties* XMqttSubscription_userProperties_const(const XMqttSubscription* sub)
{
    return sub ? sub->m_userProperties : NULL;
}

bool XMqttSubscription_isSharedSubscription(const XMqttSubscription* sub) { return sub ? sub->m_sharedSubscription : false; }

XString* XMqttSubscription_sharedSubscriptionName(const XMqttSubscription* sub)
{
    if (!sub || !sub->m_sharedSubscriptionName) return NULL;
    return XString_create_copy(sub->m_sharedSubscriptionName);
}

const XString* XMqttSubscription_sharedSubscriptionName_const(const XMqttSubscription* sub)
{
    return sub ? sub->m_sharedSubscriptionName : NULL;
}

void XMqttSubscription_unsubscribe_base(XMqttSubscription* sub)
{
    if (!sub) return;
    void (*func)(XMqttSubscription*) = XClassGetVirtualFunc(sub, EXMqttSubscription_Unsubscribe, void(*)(XMqttSubscription*));
    if (func) func(sub);
}

static void VXMQ_unsubscribe(XMqttSubscription* sub)
{
    if (!sub) return;
    if (sub->m_client && sub->m_topic)
        XMqttClient_unsubscribe((XMqttClient*)sub->m_client, sub->m_topic);
}

// =============== 受保护接口 ===============

void XMqttSubscription_setState(XMqttSubscription* sub, XMqttSubscription_State state)
{
    if (!sub || sub->m_state == (uint8_t)state) return;
    sub->m_state = (uint8_t)state;
    XMqttSubscription_stateChanged_signal(sub, state);
}

void XMqttSubscription_setQos(XMqttSubscription* sub, uint8_t qos)
{
    if (!sub || sub->m_qos == qos) return;
    sub->m_qos = qos;
    XMqttSubscription_qosChanged_signal(sub, qos);
}

void XMqttSubscription_setClient(XMqttSubscription* sub, void* client)
{
    if (!sub) return;
    sub->m_client = client;
}

// =============== 信号 ===============

void* XMqttSubscription_stateChanged_signal(XMqttSubscription* sub, XMqttSubscription_State state)
{
    XEmitSignal(sub, XMqttSubscription_stateChanged_signal,
        XVarList_Create(XVar(int, state)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XMqttSubscription_qosChanged_signal(XMqttSubscription* sub, uint8_t qos)
{
    XEmitSignal(sub, XMqttSubscription_qosChanged_signal,
        XVarList_Create(XVar(uint8_t, qos)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

static void XMqttSubscription_message_args_delete(XVarList* list)
{
    XVarList_args_1(list, XMqttMessage*, message);
    if (message) XMqttMessage_delete_base(message);
}

void* XMqttSubscription_messageReceived_signal(XMqttSubscription* sub, XMqttMessage* msg)
{
    if (!sub) return (void*)(size_t)XMqttSubscription_messageReceived_signal;
    XMqttMessage* copy = msg ? XMqttMessage_create_copy(msg) : NULL;
    XVarList* args = XVarList_Create(XVar(XMqttMessage*, copy));
    XObject_emitSignal((XObject*)sub, (size_t)XMqttSubscription_messageReceived_signal,
                       args, XMqttSubscription_message_args_delete,
                       NULL, XEVENT_PRIORITY_NORMAL);
    return (void*)(size_t)XMqttSubscription_messageReceived_signal;
}

#endif /* XMQTT_SUBSCRIPTION_ON */
#endif /* XMQTT_ON */
#endif /* XPROTOCOL_ON */
