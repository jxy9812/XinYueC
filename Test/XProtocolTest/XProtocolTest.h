#ifndef XPROTOCOLTEST_H
#define XPROTOCOLTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include "XTestMenu.h"
#include"XClass.h"
#if DEMOTEST
	//协议栈
	void XTestMenu_XProtocolTest(XTestMenu* root);
	void XTestMenu_XDataFrameCommTest(XTestMenu* root);
	void XTestMenu_TJCHMICommTest(XTestMenu* root);
	void XTestMenu_XModbusTest(XTestMenu* root);
	void XTestMenu_XMqttTest(XTestMenu* root);
	void XTestMenu_XCanTest(XTestMenu* root);

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
