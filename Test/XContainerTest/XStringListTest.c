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
	printf("%s \n",XString_c_str(string) );
}

/**
 * @brief Qt对齐测试：sort/removeDuplicates/filter/replaceInStrings/contains/indexOf/lastIndexOf
 */
void XStringListQtAlignTest()
{
	XPrintf_3("========== XStringList Qt对齐测试 (sort/removeDuplicates/filter/replaceInStrings) ==========\n\n");

	// ---------------- sort 排序测试 ----------------
	XPrintf_3("--- sort 排序测试 ---\n");
	{
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "banana");
		XStringList_push_back_utf8(list, "Apple");
		XStringList_push_back_utf8(list, "apple");
		XStringList_push_back_utf8(list, "Cherry");
		XStringList_push_back_utf8(list, "banana");

		// 大小写敏感排序
		XStringList_sort(list, XChar_CaseSensitive);
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf_3("sort(大小写敏感) = "); XPrintf_2(joined); XPrintf_3("  (期望:Apple,Cherry,apple,banana,banana)\n");
		XString_delete_base(joined);

		// 大小写不敏感排序
		XStringList_sort(list, XChar_CaseInsensitive);
		joined = XStringList_join_utf8(list, ",");
		XPrintf_3("sort(大小写不敏感) = "); XPrintf_2(joined); XPrintf_3("  (期望:Apple,apple,banana,banana,Cherry)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}

	// ---------------- removeDuplicates 去重测试 ----------------
	XPrintf_3("\n--- removeDuplicates 去重测试 ---\n");
	{
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "a");
		XStringList_push_back_utf8(list, "b");
		XStringList_push_back_utf8(list, "a");
		XStringList_push_back_utf8(list, "c");
		XStringList_push_back_utf8(list, "b");
		XStringList_push_back_utf8(list, "a");

		size_t removed = XStringList_removeDuplicates(list);
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf("removeDuplicates: 移除%zu项, 结果=", removed);
		XPrintf_2(joined); XPrintf_3("  (期望:移除3项, 结果=a,b,c)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}

	// ---------------- filter 筛选测试 ----------------
	XPrintf_3("\n--- filter 筛选测试 ---\n");
	{
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "Hello");
		XStringList_push_back_utf8(list, "world");
		XStringList_push_back_utf8(list, "HELLO");
		XStringList_push_back_utf8(list, "test");

		// 大小写敏感筛选
		XString* needle = XString_create_utf8("Hello");
		XStringList* filtered = XStringList_filter(list, needle, XChar_CaseSensitive);
		XString* joined = XStringList_join_utf8(filtered, ",");
		XPrintf_3("filter(Hello,大小写敏感) = "); XPrintf_2(joined); XPrintf_3("  (期望:Hello)\n");
		XString_delete_base(joined);
		XStringList_delete_base(filtered);

		// 大小写不敏感筛选
		filtered = XStringList_filter(list, needle, XChar_CaseInsensitive);
		joined = XStringList_join_utf8(filtered, ",");
		XPrintf_3("filter(Hello,大小写不敏感) = "); XPrintf_2(joined); XPrintf_3("  (期望:Hello,HELLO)\n");
		XString_delete_base(joined);
		XStringList_delete_base(filtered);
		XString_delete_base(needle);

		// filter_utf8
		filtered = XStringList_filter_utf8(list, "test", XChar_CaseSensitive);
		joined = XStringList_join_utf8(filtered, ",");
		XPrintf_3("filter_utf8(test) = "); XPrintf_2(joined); XPrintf_3("  (期望:test)\n");
		XString_delete_base(joined);
		XStringList_delete_base(filtered);
		XStringList_delete_base(list);
	}

	// ---------------- replaceInStrings 替换测试 ----------------
	XPrintf_3("\n--- replaceInStrings 替换测试 ---\n");
	{
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "Hello World");
		XStringList_push_back_utf8(list, "hello world");
		XStringList_push_back_utf8(list, "HELLO WORLD");

		XString* before = XString_create_utf8("Hello");
		XString* after = XString_create_utf8("Hi");
		XStringList_replaceInStrings(list, before, after, XChar_CaseSensitive);
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf_3("replaceInStrings(Hello→Hi,大小写敏感) = "); XPrintf_2(joined); XPrintf_3("  (期望:Hi World,hello world,HELLO WORLD)\n");
		XString_delete_base(joined);
		XString_delete_base(before);
		XString_delete_base(after);

		// replaceInStrings_utf8
		XStringList_replaceInStrings_utf8(list, "world", "Earth", XChar_CaseInsensitive);
		joined = XStringList_join_utf8(list, ",");
		XPrintf_3("replaceInStrings_utf8(world→Earth,不敏感) = "); XPrintf_2(joined); XPrintf_3("  (期望:Hi Earth,hello Earth,HELLO Earth)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}

	// ---------------- contains 包含测试 ----------------
	XPrintf_3("\n--- contains 包含测试 ---\n");
	{
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "Apple");
		XStringList_push_back_utf8(list, "Banana");
		XStringList_push_back_utf8(list, "Cherry");

		XString* s = XString_create_utf8("apple");
		XPrintf("contains(apple,大小写敏感)=%s  (期望:否)\n", XStringList_contains(list, s, XChar_CaseSensitive) ? "是" : "否");
		XPrintf("contains(apple,大小写不敏感)=%s  (期望:是)\n", XStringList_contains(list, s, XChar_CaseInsensitive) ? "是" : "否");
		XString_delete_base(s);

		XPrintf("contains_utf8(Banana)=%s  (期望:是)\n", XStringList_contains_utf8(list, "Banana", XChar_CaseSensitive) ? "是" : "否");
		XPrintf("contains_utf8(NotExist)=%s  (期望:否)\n", XStringList_contains_utf8(list, "NotExist", XChar_CaseSensitive) ? "是" : "否");
		XStringList_delete_base(list);
	}

	// ---------------- indexOf / lastIndexOf 测试 ----------------
	XPrintf_3("\n--- indexOf / lastIndexOf 测试 ---\n");
	{
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "a");
		XStringList_push_back_utf8(list, "b");
		XStringList_push_back_utf8(list, "a");
		XStringList_push_back_utf8(list, "c");
		XStringList_push_back_utf8(list, "a");

		XString* s = XString_create_utf8("a");
		XPrintf("indexOf(a,from=0,大小写敏感)=%lld  (期望:0)\n", (long long)XStringList_indexOf(list, s, 0, XChar_CaseSensitive));
		XPrintf("indexOf(a,from=1,大小写敏感)=%lld  (期望:2)\n", (long long)XStringList_indexOf(list, s, 1, XChar_CaseSensitive));
		XPrintf("lastIndexOf(a,大小写敏感)=%lld  (期望:4)\n", (long long)XStringList_lastIndexOf(list, s, -1, XChar_CaseSensitive));
		XPrintf("lastIndexOf(a,from=3)=%lld  (期望:2)\n", (long long)XStringList_lastIndexOf(list, s, 3, XChar_CaseSensitive));
		XString_delete_base(s);

		XPrintf("indexOf_utf8(b)=%lld  (期望:1)\n", (long long)XStringList_indexOf_utf8(list, "b", 0, XChar_CaseSensitive));
		XPrintf("indexOf_utf8(NotExist)=%lld  (期望:-1)\n", (long long)XStringList_indexOf_utf8(list, "NotExist", 0, XChar_CaseSensitive));
		XStringList_delete_base(list);
	}

	// ---------------- NULL 安全性 ----------------
	XPrintf_3("\n--- NULL 安全性 ---\n");
	{
		XStringList_sort(NULL, XChar_CaseSensitive);
		XPrintf_3("sort(NULL)=无崩溃  (期望:无崩溃)\n");
		XPrintf("removeDuplicates(NULL)=%zu  (期望:0)\n", XStringList_removeDuplicates(NULL));
		XPrintf("filter(NULL)=%s  (期望:空)\n", XStringList_filter(NULL, NULL, XChar_CaseSensitive) ? "非空" : "空");
		XPrintf("contains(NULL)=%s  (期望:否)\n", XStringList_contains(NULL, NULL, XChar_CaseSensitive) ? "是" : "否");
		XPrintf("indexOf(NULL)=%lld  (期望:-1)\n", (long long)XStringList_indexOf(NULL, NULL, 0, XChar_CaseSensitive));
	}

	XPrintf_3("\n========== XStringList Qt对齐测试结束 ==========\n");
	XCoreApplication_quit();
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
	XCoreApplication_quit();
}
void XMenu_XStringListTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XStringList(字符串数组)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XStringListTest);
	{
		XAction* action = XMenu_addAction(menu, "Qt对齐(sort/filter/contains)");
		XAction_setAction(action, XStringListQtAlignTest);
	}
	}
}
#endif