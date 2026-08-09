#include"XCodeTest.h"
#include"XMenu.h"
void XMenu_XCodeTest(XMenu* root)
{
	XMenu* menu = XMenu_create("代码");
	XMenu_addMenu(root, menu);
	XMenu_XRcodeTest(menu);
	XMenu_XDebugTest(menu);
	XMenu_XStateMachineTest(menu);
#if XTHREAD_ON
	XMenu_XThreadTest(menu);
#endif
#if XTHREADPOOL_ON
	XMenu_XThreadPoolTest(menu);
#endif
	XMenu_XDateTimeTest(menu);
	XMenu_XCryptographicHashTest(menu);
	XMenu_XRandomGeneratorTest(menu);
XMenu_XCoreApplicationTest(menu);
	XMenu_XCommandLineParserTest(menu);
	XMenu_XRegularExpressionTest(menu);
	XMenu_XProcessTest(menu);
	XMenu_XConsoleShellTest(menu);
	XMenu_XConsoleShellBackendTest(menu);
}
