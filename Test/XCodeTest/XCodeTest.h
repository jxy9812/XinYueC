#ifndef XCODETEST_H
#define XCODETEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#if DEMOTEST
void XMenu_XCodeTest(XMenu* root);
void XMenu_XDebugTest(XMenu* root);
void XMenu_XStateMachineTest(XMenu* root);
void XStateMachineEventTest();
void XStateMachineSignalTest();
void XHistoryState_Test();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif