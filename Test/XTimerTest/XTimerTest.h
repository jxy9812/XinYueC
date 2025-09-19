#ifndef XTIMERTEST_H
#define XTIMERTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#if DEMOTEST
	void XMenu_XTimerTest(XMenu* root);
	void XMenu_XTimerWheelTest(XMenu* root);

	void XTimerWheelTest();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif