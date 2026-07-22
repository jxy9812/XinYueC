#include"XCodeTest.h"
#include"XMenu.h"
void XMenu_XCodeTest(XMenu* root)
{
	XMenu* menu = XMenu_create("代码");
	XMenu_addMenu(root, menu);
	XMenu_XRcodeTest(menu);
	XMenu_XDebugTest(menu);
	XMenu_XStateMachineTest(menu);
	XMenu_XThreadTest(menu);
	XMenu_XThreadPoolTest(menu);
	XMenu_XDateTimeTest(menu);
	XMenu_XCryptographicHashTest(menu);
	XMenu_XRandomGeneratorTest(menu);
XMenu_XCoreApplicationTest(menu);
	XMenu_XCommandLineParserTest(menu);
}
