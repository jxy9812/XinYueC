#include "XProtocolTest.h"
#include "XMqttClient.h"
#include "XMqttTopicFilter.h"
#include "XMqttTopicName.h"
#include "XMqttMessage.h"
#include "XMqttPublishProperties.h"
#include "XMqttConnectionProperties.h"
#include "XMqttSubscriptionProperties.h"
#include "XMqttAuthenticationProperties.h"
#include "XClass.h"
#include "XPrintf.h"
#include <stdint.h>
#include <stdbool.h>

void XMqttMemoryLifecycleTest(void)
{
    static const uint8_t payload[] = "lifecycle-payload";
    const int rounds = 256;
    int completed = 0;
    int i;
    for (i = 0; i < rounds; ++i) {
        XMqttClient* client = XMqttClient_create();
        XMqttTopicFilter* filter = XMqttTopicFilter_create("lifecycle/#");
        XMqttTopicName* topic = XMqttTopicName_create("lifecycle/topic");
        XMqttMessage* message = XMqttMessage_create_full("lifecycle/topic", payload,
                                                          sizeof(payload) - 1, 7, 1, false, false);
        XMqttPublishProperties* publish = XMqttPublishProperties_create();
        XMqttConnectionProperties* connection = XMqttConnectionProperties_create();
        XMqttSubscriptionProperties* subscription = XMqttSubscriptionProperties_create();
        XMqttAuthenticationProperties* authentication = XMqttAuthenticationProperties_create();
        bool ok = client && filter && topic && message && publish && connection &&
                  subscription && authentication;
        if (client) {
            XMqttClient_setHostname(client, "127.0.0.1");
            XMqttClient_setWillTopic(client, "lifecycle/will");
            XMqttClient_setWillMessage(client, payload, sizeof(payload) - 1);
            XMqttClient_setWillQoS(client, 1);
        }
        if (message) XClassDelete(message);
        if (publish) XClassDelete(publish);
        if (connection) XClassDelete(connection);
        if (subscription) XClassDelete(subscription);
        if (authentication) XClassDelete(authentication);
        if (filter) XClassDelete(filter);
        if (topic) XClassDelete(topic);
        if (client) XClassDelete((XClass*)client);
        if (!ok) {
            XPrintf("[失败] MQTT 生命周期回归第 %d 轮创建对象失败\n", i + 1);
            return;
        }
        ++completed;
    }
    XPrintf("[通过] MQTT 生命周期创建/释放回归 %d/%d 轮；ASan/LSan 无报告即为无泄漏\n",
            completed, rounds);
}
