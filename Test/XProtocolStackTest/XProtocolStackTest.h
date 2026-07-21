#ifndef XPROTOCOLSTACKTEST_H
#define XPROTOCOLSTACKTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#if DEMOTEST
	//协议栈
	void XMenu_XProtocolStackTest(XMenu* root);
	void XMenu_XDataFrameCommTest(XMenu* root);
	void XMenu_TJCHMICommTest(XMenu* root);
	void XMenu_XModbusTest(XMenu* root);
	void XMenu_XMqttTest(XMenu* root);
	void XMenu_XCanTest(XMenu* root);

	void XModbusRtuSerialClientTest();
	void XModbusTcpClientTest();
	void XModbusCommEventTest();
	void XModbusAduTest();
	void XDataFrameCommTest();
	void TJCHMICommTest();

	void XMqttTopicNameTest();
	void XMqttTopicFilterTest();
	void XMqttStringPairTest();
	void XMqttUserPropertiesTest();
	void XMqttMessageTest();
	void XMqttPublishPropertiesTest();
	void XMqttMessageStatusPropertiesTest();
	void XMqttConnectionPropertiesTest();
	void XMqttSubscriptionPropertiesTest();
	void XMqttAuthenticationPropertiesTest();
	void XMqttSubscriptionTest();
	void XMqttClientTest();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
