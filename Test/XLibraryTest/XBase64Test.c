#include"XLibraryTest.h"
#include"XString.h"
#include"XBase64.h"
#include"XByteArray.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
void XBase64Test()
{
	XPrintf_utf8("XBase64测试\n");
	char buff[] = "adss12313212345555555555456456";
	XByteArray* sour = XByteArray_create(0), *toBase=NULL,* fromBase64=NULL;
	XByteArray_append_array_base(sour,buff,sizeof(buff));
	if (sour)
	{
		toBase = XByteArray_toBase64(sour);
		if (toBase)
			XPrintf("转Base64:%s\n", XContainerDataPtr(toBase));
	}
	if (toBase)
	{
		fromBase64 = XByteArray_fromBase64(toBase);
		if (fromBase64)
			XPrintf("还原Base64:%s\n", XContainerDataPtr(fromBase64));
	}

	if (sour)
		XByteArray_delete_base(sour);
	if (toBase)
		XByteArray_delete_base(toBase);
	if (fromBase64)
		XByteArray_delete_base(fromBase64);
	XCoreApplication_quit();
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