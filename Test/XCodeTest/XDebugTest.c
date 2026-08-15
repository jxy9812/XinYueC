#include"XCodeTest.h"
#include"XMemory.h"
#include"XMenu.h"
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

void XMenu_XDebugTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XDebug(调试器)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XDebugTest);
	}
}