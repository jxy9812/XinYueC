#include"XCodeTest.h"
#include"XMemory.h"
#include"XTestMenu.h"
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

void XTestMenu_XRcodeTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XRcode(二维码)");
	XTestMenu_addMenu(root, menu);
	{
		XAction* action = XTestMenu_addAction(menu, "主测试");
		XTestMenu_setActionFunction(action, XRcodeTest);
	}
}