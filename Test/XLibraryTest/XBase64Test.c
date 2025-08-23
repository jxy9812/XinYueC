#include"XLibraryTest.h"
#include"XString.h"
#include"XBase64.h"
#include"XVector.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
void XBase64Test()
{
	XPrint_utf8("XBase64测试\n");
	char buff[] = "adss12313212345555555555456456";
	XVector* sour = XVector_Create(uint8_t), *toBase=NULL,* fromBase64=NULL;
	XVector_append_array_base(sour,buff,sizeof(buff));
	if (sour)
	{
		toBase = XVector_toBase64(sour);
		if (toBase)
			XPrint_utf8_fmt("转Base64:%s\n", XContainerDataPtr(toBase));
	}
	if (toBase)
	{
		fromBase64 = XVector_fromBase64(toBase);
		if (fromBase64)
			XPrint_utf8_fmt("还原Base64:%s\n", XContainerDataPtr(fromBase64));
	}

	if (sour)
		XVector_delete_base(sour);
	if (toBase)
		XVector_delete_base(toBase);
	if (fromBase64)
		XVector_delete_base(fromBase64);
	XCoreApplication_requestQuit();
}
void XMenu_XBase64Test(XMenu* root)
{
	XMenu* menu = XMenu_create("XBase64(base64)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XBase64Test);
	}
}