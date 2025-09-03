#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XStringList.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
static void XFor_each_XString(XString* string, void* args)
{
	XPrintf("%s \n",XString_c_str(string) );
}

void XStringListTest()
{
#if XVector_ON
	while (true)
	{
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "你好");
		XStringList_push_back_utf8(list, "非常好");
		XStringList_push_back_utf8(list, "世界");
		XStringList_insert_utf8(list,0,"彩虹猫");
		XStringList_push_front_utf8(list, "星小白");
		XString* str = XStringList_join_utf8(list,"-");
		if (str)
		{
			XPrintf("连接:%s \n", XString_toUtf8(str));
			XString_delete_base(str);
		}
		XStringList_iterator_for_each(list, XFor_each_XString, NULL);
		XStringList_delete_base(list);
	}
#else
	IS_ON_DEBUG(XVector_ON);
#endif
	XCoreApplication_requestQuit();
}
void XMenu_XStringListTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XStringList(字符串数组)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XStringListTest);
	}
}
#endif