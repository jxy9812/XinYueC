#include"XCodeTest.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XRcode.h"
void XRcodeTest()
{
	XRcode* code=XRcode_create();
	XByteArray* array= XByteArray_create_utf8("测试\n");
	XRcode_encode(code, array,20,0);
	XRcode_print_matrix(code);
	XByteArray_delete_base(array);
	XRcode_delete(code);
	XCoreApplication_quit();
}

void XMenu_XRcodeTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XRcode(二维码)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XRcodeTest);
	}
}