#ifndef XCODETEST_H
#define XCODETEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#include "XProcessTest.h"
#include "XConsoleShellTest.h"
#include "XConsoleShellBackendTest.h"
#if DEMOTEST
void XMenu_XCodeTest(XMenu* root);
void XMenu_XRcodeTest(XMenu* root);
void XMenu_XDebugTest(XMenu* root);
#if XTHREAD_ON
void XMenu_XThreadTest(XMenu* root);
#endif
#if XTHREADPOOL_ON
void XMenu_XThreadPoolTest(XMenu* root);
#endif
void XMenu_XStateMachineTest(XMenu* root);
void XMenu_XDateTimeTest(XMenu* root);
void XMenu_XCryptographicHashTest(XMenu* root);
void XMenu_XRandomGeneratorTest(XMenu* root);
void XMenu_XCoreApplicationTest(XMenu* root);
void XMenu_XCommandLineParserTest(XMenu* root);
/**
 * @brief 添加 XRegularExpression Qt 6.8 全量测试菜单。
 * @param root 测试根菜单对象；不能传入 NULL。
 */
void XMenu_XRegularExpressionTest(XMenu* root);
/** @brief 添加 XProcess 公共 API 全量测试菜单。 */
void XMenu_XProcessTest(XMenu* root);
/** @brief 添加 XConsoleShell 全量测试菜单。 */
void XMenu_XConsoleShellTest(XMenu* root);
/** @brief 添加 XConsoleShell TCP 后端端到端测试菜单。 */
void XMenu_XConsoleShellBackendTest(XMenu* root);
void XStateMachineEventTest();
void XStateMachineSignalTest();
void XHistoryState_Test();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
