#ifndef XPROTOCOLTEST_H
#define XPROTOCOLTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include "XTestMenu.h"
#include"XClass.h"
#include "XPlc_config.h"
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

	//PLC/S7 协议（离线单测 + 集成测试，实现于 XS7Test.c；随 XPLC/XS7 裁剪联动）
#if XPROTOCOL_ON && XPLC_ON && XS7_ON
	void XS7TpktTest();
	void XS7CotpTest();
	void XS7PduTest_Read();
	void XS7PduTest_Write();
	void XS7PduTest_Setup();
	void XS7AddressTest();
	void XS7ValueTest();
	void XS7ControlTest();
	void XS7SessionTest();
	void XPlcReplyPublicApiTest();
	void XTestMenu_XS7Test(XTestMenu* root);
	/* 一键入口：离线单测全集（--test xs7-unit）；真机/模拟器集成测试（--test xs7-plc，需 XS7_PLC_IP 可达） */
	bool XS7Test_runAll(void);
	bool XS7Test_integration_run(void);
#endif /* XPROTOCOL_ON && XPLC_ON && XS7_ON */

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
