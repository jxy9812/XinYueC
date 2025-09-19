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

	void XModbusTest();
	void XDataFrameCommTest();
	void TJCHMICommTest();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif