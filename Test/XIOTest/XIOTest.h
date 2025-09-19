#ifndef XIOTEST_H
#define XIOTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#if DEMOTEST
	void XMenu_XIOTest(XMenu* root);
	void XMenu_XSerialPortTest(XMenu* root);
	void XMenu_XSocketTest(XMenu* root);



	void XPWMDeviceTest();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif