#ifndef XPROTOCOLTEST_H
#define XPROTOCOLTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#if DEMOTEST
	//协议栈
	void XMenu_XProtocolTest(XMenu* root);
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
	bool XMqttDataLayoutTest_run(void);
	int XMqttPublicApiTest_run(void);
	void XMqttPublicApiTest(void);
	void XMqttTcpServerIntegrationTest(void);
	void XMqttTcpClientIntegrationTest(void);
	void XMqttMemoryLifecycleTest(void);
	bool XMqttServerUnitTest_run(void);
	bool XMqttTcpServerApiUnitTest_run(void);
	bool XMqttTcpServerProcess_run(void);
	bool XMqttTcpClientProcess_run(void);
	bool XMqttTcpInteropTest_run(void);
	bool XMqttTest_runAll(void);
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
