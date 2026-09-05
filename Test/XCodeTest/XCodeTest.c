#include"XCodeTest.h"
#include"XTestMenu.h"
void XTestMenu_XCodeTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("代码");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XRcodeTest(menu);
	XTestMenu_XDebugTest(menu);
	XTestMenu_XStateMachineTest(menu);
#if XTHREAD_ON
	XTestMenu_XThreadTest(menu);
#endif
#if XTHREADPOOL_ON
	XTestMenu_XThreadPoolTest(menu);
#endif
	XTestMenu_XDateTimeTest(menu);
	XTestMenu_XCryptographicHashTest(menu);
	XTestMenu_XCryptographicPrimitiveTest(menu);
	XTestMenu_XRandomGeneratorTest(menu);
	XTestMenu_XActionTest(menu);
	XTestMenu_XCoreApplicationTest(menu);
	XTestMenu_XCommandLineParserTest(menu);
	XTestMenu_XRegularExpressionTest(menu);
	XTestMenu_XProcessTest(menu);
	XTestMenu_XConsoleShellTest(menu);
	XTestMenu_XConsoleShellBackendTest(menu);
}
