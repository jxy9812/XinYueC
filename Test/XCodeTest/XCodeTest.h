#ifndef XCODETEST_H
#define XCODETEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include "XTestMenu.h"
#include"XClass.h"
#include "XProcessTest.h"
#include "XConsoleShellTest.h"
#include "XConsoleShellBackendTest.h"
#if DEMOTEST
void XTestMenu_XCodeTest(XTestMenu* root);
void XTestMenu_XRcodeTest(XTestMenu* root);
void XTestMenu_XDebugTest(XTestMenu* root);
#if XTHREAD_ON
void XTestMenu_XThreadTest(XTestMenu* root);
#endif
#if XTHREADPOOL_ON
void XTestMenu_XThreadPoolTest(XTestMenu* root);
#endif
void XTestMenu_XStateMachineTest(XTestMenu* root);
void XTestMenu_XDateTimeTest(XTestMenu* root);
void XTestMenu_XCryptographicHashTest(XTestMenu* root);
/** @brief 添加 XCryptographic 独立密码原语标准向量测试菜单。 */
void XTestMenu_XCryptographicPrimitiveTest(XTestMenu* root);
void XTestMenu_XRandomGeneratorTest(XTestMenu* root);
/** @brief 添加 XAction（对标 Qt 6.8 QAction）全量测试菜单。 */
void XTestMenu_XActionTest(XTestMenu* root);
void XTestMenu_XCoreApplicationTest(XTestMenu* root);
void XTestMenu_XCommandLineParserTest(XTestMenu* root);
/**
 * @brief 添加 XRegularExpression Qt 6.8 全量测试菜单。
 * @param root 测试根菜单对象；不能传入 NULL。
 */
void XTestMenu_XRegularExpressionTest(XTestMenu* root);
/** @brief 添加 XProcess 公共 API 全量测试菜单。 */
void XTestMenu_XProcessTest(XTestMenu* root);
/** @brief 添加 XConsoleShell 全量测试菜单。 */
void XTestMenu_XConsoleShellTest(XTestMenu* root);
/** @brief 添加 XConsoleShell TCP 后端端到端测试菜单。 */
void XTestMenu_XConsoleShellBackendTest(XTestMenu* root);
void XStateMachineEventTest();
void XStateMachineSignalTest();
void XHistoryState_Test();
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
