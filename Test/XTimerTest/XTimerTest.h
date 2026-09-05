#ifndef XTIMERTEST_H
#define XTIMERTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include "XTestMenu.h"
#include"XClass.h"
#if DEMOTEST
	void XTestMenu_XTimerTest(XTestMenu* root);
	void XTestMenu_XTimerTimeWheelTest(XTestMenu* root);
	void XTestMenu_XHrTimerTest(XTestMenu* root);
	void XTimerTimeWheelTest();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif