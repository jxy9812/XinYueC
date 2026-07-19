#ifndef XPROTOCOLSTACKTEST_H
#define XPROTOCOLSTACKTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#if DEMOTEST
	//–≠“È’ª
	void XMenu_XProtocolStackTest(XMenu* root);
	void XMenu_XDataFrameCommTest(XMenu* root);
	void XMenu_TJCHMICommTest(XMenu* root);
	void XMenu_XModbusTest(XMenu* root);

	void XModbusRtuSerialClientTest();
	void XModbusTcpClientTest();
	void XModbusCommEventTest();
	void XDataFrameCommTest();
	void TJCHMICommTest();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif