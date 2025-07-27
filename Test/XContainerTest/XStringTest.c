#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XStringList.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
static void XStringTest();
static void XFor_each_XString(void* LPVal, void* args)
{
	XString* string = LPVal;
	//printf("测试\n");
	printf("%s \n", XString_c_str(string));
}
void XStringTest()
{
	//while(true)
	{
#if XString_ON
	printf("XString 测试\n");
	{
		XString* str = XString_create("你好-世界-？？？？");
		if (str)
		{
			XStringList* v = XString_split(str, "-");
			if (v)
			{
				XStringList_iterator_for_each(v, XFor_each_XString, NULL);
				XStringList_delete_base(v);
			}
			XString_delete_base(str);
		}
	}
	{
		XString* str = XString_create_fmt("你好%d %d\n",121,9);
		printf("%s", XString_data(str));
		XString_delete_base(str);
	}
	
	XString* str = XString_create("你好");
	XString_append_base(str, "111");
	//XString_push_front_base(str, '#');
	XString_push_back_base(str, '!');
	XString_insert_base(str,0,"12121ni_");
	printf("%s\t char:%c\n", XString_data(str),XString_at(str,0));
	XString_pop_front_base(str);
	XString_pop_back_base(str);
	XString_assign_base(str,"你好吗！");
	XString_clear_base(str);
	//XString_append_base(str, "  666\r\n");
	//printf("字符数量%d\n", XString_size(str));
	XString_assign_base(str, "草泥马");
	printf("字符数量%d\n", XString_getSize_base(str));
	XString_append_base(str, "你好呀");

	//XString_erase_base(str, 3, 3);
	printf("字符数量%d\n", XString_getSize_base(str));
	//XString_erase_base(str, 0, 4);
	printf("%s\n", XString_data(str));
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
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XStringTest);
	}
}
#endif