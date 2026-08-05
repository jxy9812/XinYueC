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
	void XModbusPublicApiTest(XVariant* data);
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
	int XMqttPublicApiTest_run(void);
	void XMqttPublicApiTest(void);
	void XMqttTcpServerIntegrationTest(void);
	void XMqttTcpClientIntegrationTest(void);
	void XMqttMemoryLifecycleTest(void);
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
