#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XStringList.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
//static void XStringTest();
#if XString_ON
static void XFor_each_XString(XString* str, void* args)
{
	XPrintf_2(str);
	printf("\n");
}
static void XFor_each_XChar(XChar* ch, void* args)
{
	XPrintf_5(ch);
	XPrintf_3("\n");
}
//迭代器测试
void XStringIteratorTest()
{
	while (true)
	{
		XPrintf_3("XString 正向迭代器测试\n");
		XString* str = XString_create_utf8("正向迭代器");
		XString_iterator_for_each(str, XFor_each_XChar, NULL);
		XPrintf_3("XString 反向迭代器测试\n");
		XString_assign_utf8(str, "反向迭代器");
		XString_reverse_iterator_for_each(str, XFor_each_XChar, NULL);
		XString_delete_base(str);
	}

	XCoreApplication_quit();
}
//字符串 整数 测试
void XStringNumTest()
{
	//while (true)
	{
		XPrintf_3("XString 字符串转整数测试\n");
		XString* str = XString_create_utf8(NULL);
		XString_assign_utf8(str, "66666");
		XPrintf("整数:%d\n", XString_toLongLong(str, NULL, 10));
		XPrintf("整数:%.2lf\n", XString_toDouble(str, NULL));
		XPrintf_3("XString 整数转字符串测试\n");
		XString_setNum_int(str, -6666699, 2);
		XPrintf_2(str);
		XPrintf_3("\n");
		XString_setNum_double(str, 66666.153456, 'f', 2);
		XPrintf_2(str);

		XString_delete_base(str);
	}

	XCoreApplication_quit();
}
void XStringOperateTest()
{
	while (true)
	{
		XPrintf_3("XString 字符串操作测试\n");

		XString* str = XString_create_utf8("这是一个字符串操作例子");
		{
			XString* mid = XString_mid(str, 2, 4);
			if (mid)
			{
				XPrintf_2(mid);
				printf("\n");
			}
			XString_delete_base(mid);
		}
		{
			int64_t index = XString_index_of_utf8(str, "例子", 0, XChar_CaseInsensitive);
			if (index != -1)
				XPrintf("找到了,index:%d\n", index);
		}
		{
			if (XString_replace_utf8(str, "一", "1", XChar_CaseInsensitive))
			{
				XPrintf_2(str);
				printf("\n");
			}
		}
		XString_delete_base(str);
	}
	XCoreApplication_quit();
}
void XStringCopyTest()
{
	while(true)
	{
		XString* str = XString_create_utf8("这是一个字符串拷贝引用测试");
		XPrintf_2(str); printf("\n");
		XString* copy = XString_create_copy(str);
		//XPrintf_2(copy); printf("\n");

		XString_append_utf8(str,"追加测试");
		XString_assign_utf8(str, "写时才复制");
		XPrintf_2(str); printf("\n");
		XPrintf_2(copy); printf("\n");

		XString_delete_base(str);
		XString_delete_base(copy);
	}
	XCoreApplication_quit();
}
void XStringTest()
{
	//char* utf8 = "1234567890";
	//XChar buff [100];
	//XChar_fromUtf8Stream(utf8,0,buff,100);
	//for (size_t i = 0; i < 10; i++)
	//{
	//	printf("utf8:%d utf16:%d %d\n",utf8[i],buff[i].code, buff[i].code&(~0x80));//c & 0x80
	//}
	//
	//return;
	//while(true)
	{
#if XString_ON
	XPrintf_3("XString 测试\n");
	{
		XString* str = XString_create_utf8("你好-在吗");
		if (str)
		{
			XStringList* v = XString_split_utf8(str, "-",XChar_CaseInsensitive);
			if (v)
			{
				XStringList_iterator_for_each(v, XFor_each_XString, NULL);
				XStringList_delete_base(v);
			}
		}
		/*XString_append_utf8(str,"1223dasdas31d32as1d23sa1d32sa123d1sa23d132sad");
		XPrintf_2(str);
		printf("\n");*/
		XString_delete_base(str);
	}
	XCoreApplication_quit();
	return;
	
	XString* str = XString_create_utf8("你好");
	XPrintf_2(str);
	XString_append_utf8(str, "111");
	//XString_push_front_base(str, '#');
	//XString_push_back_base(str, '!');
	XString_insert_utf8(str,1,"12121ni_");
	XPrintf_2(str);
	XString_pop_front_base(str);
	XString_pop_back_base(str);
	XString_assign_utf8(str,"你好吗！");
	XString_clear_base(str);
	//XString_append_utf8(str, "  666\r\n");
	//printf("字符数量%d\n", XString_size(str));
	//XString_assign_utf8(str, "草泥马");
	XPrintf("字符数量%d\n", XString_size_base(str));
	XString_append_utf8(str, "你好呀");

	//XString_erase_base(str, 3, 3);
	XPrintf("字符数量%d\n", XString_size_base(str));
	//XString_erase_base(str, 0, 4);
	XPrintf_2(str);
	XString_delete_base(str);
#endif
	}
	XCoreApplication_quit();
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