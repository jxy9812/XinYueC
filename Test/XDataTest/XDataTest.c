#include"XDataTest.h"
#include"XCharTest.h"
#include"XXmlStreamWriterTest.h"
#include"XXmlStreamReaderTest.h"
#include"XExcelTest.h"
#include"XDomTest.h"
#include"XMenu.h"
#include"XAction.h"

#if DEMOTEST
static void xjson_run_all_wrapper(XVariant* data)
{
	(void)data;
	XJsonQtAlignmentTest();
}

void XMenu_XJsonQtAlignmentTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XJsonQtAlignmentTest");
	XMenu_addMenu(root, menu);
	XAction* action = XMenu_addAction(menu, "全部 API 测试");
	XAction_setAction(action, xjson_run_all_wrapper);
}
#endif

void XMenu_XDataTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XDataTest");
	XMenu_addMenu(root, menu);
	XMenu_XCharTest(menu);
	XMenu_XJsonQtAlignmentTest(menu);
	XMenu_XXmlStreamWriterTest(menu);
	XMenu_XXmlStreamReaderTest(menu);
	XMenu_XExcelTest(menu);
	XMenu_XDomTest(menu);
}
