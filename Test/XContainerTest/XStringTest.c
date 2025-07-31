#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XStringList.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
//static void XStringTest();
static void XFor_each_XString(void* LPVal, void* args)
{
	XString* str = LPVal;
	XPrint(str);
}
void XStringTest()
{
	//while(true)
	{
#if XString_ON
	XPrint_utf8("XString 测试\n");
	{
		XString* str = XString_create("你好-在吗");
		if (str)
		{
			XStringList* v = XString_split(str, "-",XCharCaseInsensitive);
			if (v)
			{
				XStringList_iterator_for_each(v, XFor_each_XString, NULL);
				XStringList_delete_base(v);
			}
			XString_delete_base(str);
		}
		//continue;
	}
	{
		XString* str = XString_create_fmt("你好%d %d\n",121,9);
		int64_t index= XString_index_of_utf8(str,"9",0,XCharCaseInsensitive);
		if(index!=-1)
			XPrint_utf8_fmt("找到了,index:%d\n",index);
		XPrint(str);
		XString_delete_base(str);
	}
	
	XString* str = XString_create("你好");
	XPrint(str);
	XString_append_base(str, "111");
	//XString_push_front_base(str, '#');
	//XString_push_back_base(str, '!');
	//XString_insert_base(str,0,"12121ni_");
	XPrint(str);
	XString_pop_front_base(str);
	XString_pop_back_base(str);
	XString_assign_base(str,"你好吗！");
	XString_clear_base(str);
	//XString_append_base(str, "  666\r\n");
	//printf("字符数量%d\n", XString_size(str));
	//XString_assign_base(str, "草泥马");
	XPrint_utf8_fmt("字符数量%d\n", XString_size_base(str));
	XString_append_base(str, "你好呀");

	//XString_erase_base(str, 3, 3);
	XPrint_utf8_fmt("字符数量%d\n", XString_size_base(str));
	//XString_erase_base(str, 0, 4);
	XPrint(str);
	XString_delete_base(str);
#endif
	}
	XCoreApplication_requestQuit();
}
void XMenu_XStringTest(XMenu* root)
{
	XMenu* menu = XMenu_create("字符串(XString)");
	XMenu_addMenu(root, menu);
	{
		const char* str = "主测试";
		XAction* action = XMenu_addAction(menu, str);
		XAction_setAction(action, XStringTest);
	}
}
#endif