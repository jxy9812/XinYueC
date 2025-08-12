#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XStringList.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
//static void XStringTest();
#if XString_ON
static void XFor_each_XString(XString* str, void* args)
{
	XPrint(str);
	printf("\n");
}
static void XFor_each_XChar(XChar* ch, void* args)
{
	XPrint_XChar(ch);
	XPrint_utf8("\n");
}
//迭代器测试
void XStringIteratorTest()
{
	while (true)
	{
		XPrint_utf8("XString 正向迭代器测试\n");
		XString* str = XString_create_utf8("正向迭代器");
		XString_iterator_for_each(str, XFor_each_XChar, NULL);
		XPrint_utf8("XString 反向迭代器测试\n");
		XString_assign_utf8(str, "反向迭代器");
		XString_reverse_iterator_for_each(str, XFor_each_XChar, NULL);
		XString_delete_base(str);
	}

	XCoreApplication_requestQuit();
}
//字符串 整数 测试
void XStringNumTest()
{
	while (true)
	{
		XPrint_utf8("XString 字符串转整数测试\n");
		XString* str = XString_create_utf8(NULL);
		XString_assign_utf8(str, "66666");
		XPrint_utf8_fmt("整数:%d\n", XString_toLongLong(str, NULL, 10));
		XPrint_utf8_fmt("整数:%.2lf\n", XString_toDouble(str, NULL));
		XPrint_utf8("XString 整数转字符串测试\n");
		XString_setNum_int(str, -6666699, 2);
		XPrint(str);
		XPrint_utf8("\n");
		XString_setNum_double(str, 66666.153456, 'f', 2);
		XPrint(str);

		XString_delete_base(str);
	}

	XCoreApplication_requestQuit();
}
void XStringOperateTest()
{
	while (true)
	{
		XPrint_utf8("XString 字符串操作测试\n");

		XString* str = XString_create_utf8("这是一个字符串操作例子");
		{
			XString* mid = XString_mid(str, 2, 4);
			if (mid)
			{
				XPrint(mid);
				printf("\n");
			}
			XString_delete_base(mid);
		}
		{
			int64_t index = XString_index_of_utf8(str, "例子", 0, XCharCaseInsensitive);
			if (index != -1)
				XPrint_utf8_fmt("找到了,index:%d\n", index);
		}
		{
			if (XString_replace_utf8(str, "一", "1", XCharCaseInsensitive))
			{
				XPrint(str);
				printf("\n");
			}
		}
		XString_delete_base(str);
	}
	XCoreApplication_requestQuit();
}
void XStringCopyTest()
{
	while(true)
	{
		XString* str = XString_create_utf8("这是一个字符串拷贝引用测试");
		XPrint(str); printf("\n");
		XString* copy = XString_create_copy(str);
		//XPrint(copy); printf("\n");

		XString_append_utf8(str,"追加测试");
		XString_assign_utf8(str, "写时才复制");
		XPrint(str); printf("\n");
		XPrint(copy); printf("\n");

		XString_delete_base(str);
		XString_delete_base(copy);
	}
	XCoreApplication_requestQuit();
}
void XStringTest()
{
	//char* utf8 = "1234567890";
	//XChar buff [100];
	//XChar_from_utf8_stream(utf8,0,buff,100);
	//for (size_t i = 0; i < 10; i++)
	//{
	//	printf("utf8:%d utf16:%d %d\n",utf8[i],buff[i].code, buff[i].code&(~0x80));//c & 0x80
	//}
	//
	//return;
	//while(true)
	{
#if XString_ON
	XPrint_utf8("XString 测试\n");
	{
		XString* str = XString_create_utf8("你好-在吗");
		if (str)
		{
			XStringList* v = XString_split_utf8(str, "-",XCharCaseInsensitive);
			if (v)
			{
				XStringList_iterator_for_each(v, XFor_each_XString, NULL);
				XStringList_delete_base(v);
			}
		}
		
		XString_delete_base(str);
	}
	XCoreApplication_requestQuit();
	return;
	
	XString* str = XString_create_utf8("你好");
	XPrint(str);
	XString_append_utf8(str, "111");
	//XString_push_front_base(str, '#');
	//XString_push_back_base(str, '!');
	XString_insert_utf8(str,1,"12121ni_");
	XPrint(str);
	XString_pop_front_base(str);
	XString_pop_back_base(str);
	XString_assign_utf8(str,"你好吗！");
	XString_clear_base(str);
	//XString_append_utf8(str, "  666\r\n");
	//printf("字符数量%d\n", XString_size(str));
	//XString_assign_utf8(str, "草泥马");
	XPrint_utf8_fmt("字符数量%d\n", XString_size_base(str));
	XString_append_utf8(str, "你好呀");

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
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XStringTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "迭代器测试");
		XAction_setAction(action, XStringIteratorTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "整数测试");
		XAction_setAction(action, XStringNumTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "操作");
		XAction_setAction(action, XStringOperateTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "拷贝");
		XAction_setAction(action, XStringCopyTest);
	}
}
#endif

#endif