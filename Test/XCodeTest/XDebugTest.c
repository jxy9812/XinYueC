#include"XCodeTest.h"
#include"XMemory.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XInfo.h"
void XDebugTest()
{
	XDebug* ctx = XDebug_create();
	XDebug_setAutoSpace(ctx,true);
	XDebug_setShowLocation(ctx, true);
	//XDebug_s(ctx, "Basic types: ");
	XDebug_int32(ctx, 123);
	//XDebug_space(ctx);
	XDebug_float(ctx, 3.14f);
	XDebug_ptr(ctx,ctx);
	XDebug_end(ctx);

	XDebug_start_stream;
	XDebug_sprintf("debug");
	XDebug_end_stream; // 释放

	XInfo_start_stream;
	XInfo_sprintf("Info");
	XInfo_sprintf("Info");
	XInfo_end_stream; // 释放
	//XCoreApplication_quit();
}

void XTestMenu_XDebugTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XDebug(调试器)");
	XTestMenu_addMenu(root, menu);
	{
		XAction* action = XTestMenu_addAction(menu, "主测试");
		XTestMenu_setActionFunction(action, XDebugTest);
	}
}