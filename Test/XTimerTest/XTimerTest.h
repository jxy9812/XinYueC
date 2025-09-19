#ifndef XTIMERTEST_H
#define XTIMERTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#if DEMOTEST
	void XMenu_XTimerTest(XMenu* root);
	void XMenu_XTimerTimeWheelTest(XMenu* root);

	void XTimerTimeWheelTest();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif