#include"XDataTest.h"
#include"XCharTest.h"
#include"XXmlStreamWriterTest.h"
#include"XXmlStreamReaderTest.h"
#include"XExcelTest.h"
#include"XDomTest.h"
#include"XSqlTest.h"
#include"XExcelExtendedTest.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XPrintf.h"

bool XDataTest_runAll(void)
{
    bool json = XJsonQtAlignmentTest() == 0;
    bool sql = XSqlTest_run() == 0;
    bool dom = XDomTest_runAll();
    bool reader = XXmlStreamReaderTest_runAll();
    bool writer = XXmlStreamWriterTest_runAll();
    bool excel = XExcelExtendedTest_runAll();
    bool result = json && sql && dom && reader && writer && excel;
    XPrintf("XData 全量自动化测试: JSON=%s SQL=%s DOM=%s Reader=%s Writer=%s Excel=%s => %s\n",
            json ? "通过" : "失败", sql ? "通过" : "失败",
            dom ? "通过" : "失败", reader ? "通过" : "失败",
            writer ? "通过" : "失败", excel ? "通过" : "失败",
            result ? "通过" : "失败");
    return result;
}

#if DEMOTEST
static void xsql_run_wrapper(XVariant* data)
{
	(void)data;
	XSqlTest_run();
}

void XTestMenu_XSqlTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XSqlTest");
	XTestMenu_addMenu(root, menu);
	XAction* action = XTestMenu_addAction(menu, "抽象驱动和查询测试");
	XTestMenu_setActionFunction(action, xsql_run_wrapper);
}

static void xjson_run_all_wrapper(XVariant* data)
{
	(void)data;
	XJsonQtAlignmentTest();
}

void XTestMenu_XJsonQtAlignmentTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XJsonQtAlignmentTest");
	XTestMenu_addMenu(root, menu);
	XAction* action = XTestMenu_addAction(menu, "全部 API 测试");
	XTestMenu_setActionFunction(action, xjson_run_all_wrapper);
}
#endif

void XTestMenu_XDataTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XDataTest");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XCharTest(menu);
	XTestMenu_XJsonQtAlignmentTest(menu);
	XTestMenu_XXmlStreamWriterTest(menu);
	XTestMenu_XXmlStreamReaderTest(menu);
	XTestMenu_XExcelTest(menu);
	XTestMenu_XDomTest(menu);
	XTestMenu_XSqlTest(menu);
}
