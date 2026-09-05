#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XStringList.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"

static void XFor_each_XString(XString* string, void* args)
{
	XPrintf("%s ", XString_c_str(string));
}

/**
 * @brief Qt对齐测试：sort/removeDuplicates/filter/replaceInStrings/contains/indexOf/lastIndexOf
 */
void XStringListQtAlignTest()
{
	XPrintf_3("========== XStringList Qt对齐测试 ==========\n\n");

	// ================================================================
	// sort 排序测试
	// ================================================================
	XPrintf_3("--- sort 排序测试 ---\n");
	{
		// 基本排序：大小写敏感
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "banana");
		XStringList_push_back_utf8(list, "Apple");
		XStringList_push_back_utf8(list, "apple");
		XStringList_push_back_utf8(list, "Cherry");
		XStringList_push_back_utf8(list, "banana");
		XStringList_sort(list, XChar_CaseSensitive);
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf_3("sort(大小写敏感,5项) = "); XPrintf_2(joined);
		XPrintf_3("  (期望:Apple,Cherry,apple,banana,banana)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}
	{
		// 大小写不敏感排序
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "banana");
		XStringList_push_back_utf8(list, "Apple");
		XStringList_push_back_utf8(list, "apple");
		XStringList_push_back_utf8(list, "Cherry");
		XStringList_push_back_utf8(list, "banana");
		XStringList_sort(list, XChar_CaseInsensitive);
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf_3("sort(大小写不敏感,5项) = "); XPrintf_2(joined);
		XPrintf_3("  (期望:Apple,apple,banana,banana,Cherry)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}
	{
		// 空列表排序（不崩溃）
		XStringList* list = XStringList_create();
		XStringList_sort(list, XChar_CaseSensitive);
		XPrintf("sort(空列表)=%s  (期望:无崩溃)\n",
			XStringList_isEmpty_base(list) ? "空,无崩溃" : "异常");
		XStringList_delete_base(list);
	}
	{
		// 单元素排序
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "Only");
		XStringList_sort(list, XChar_CaseSensitive);
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf_3("sort(单元素) = "); XPrintf_2(joined);
		XPrintf_3("  (期望:Only)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}
	{
		// 中文排序
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "张三");
		XStringList_push_back_utf8(list, "李四");
		XStringList_push_back_utf8(list, "王五");
		XStringList_push_back_utf8(list, "阿明");
		XStringList_sort(list, XChar_CaseSensitive);
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf_3("sort(中文) = "); XPrintf_2(joined);
		XPrintf_3("\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}
	{
		// 数字字符串排序
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "10");
		XStringList_push_back_utf8(list, "2");
		XStringList_push_back_utf8(list, "1");
		XStringList_push_back_utf8(list, "20");
		XStringList_sort(list, XChar_CaseSensitive);
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf_3("sort(数字字符串) = "); XPrintf_2(joined);
		XPrintf_3("  (期望:1,10,2,20)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}

	// ================================================================
	// removeDuplicates 去重测试
	// ================================================================
	XPrintf_3("\n--- removeDuplicates 去重测试 ---\n");
	{
		// 基本去重
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
		XPrintf_2(joined); XPrintf_3("  (期望:移除3项, a,b,c)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}
	{
		// 无重复
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "a");
		XStringList_push_back_utf8(list, "b");
		XStringList_push_back_utf8(list, "c");
		size_t removed = XStringList_removeDuplicates(list);
		XPrintf("removeDuplicates(无重复): 移除%zu项  (期望:0)\n", removed);
		XStringList_delete_base(list);
	}
	{
		// 全部相同
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "x");
		XStringList_push_back_utf8(list, "x");
		XStringList_push_back_utf8(list, "x");
		size_t removed = XStringList_removeDuplicates(list);
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf("removeDuplicates(全相同): 移除%zu项, 结果=", removed);
		XPrintf_2(joined); XPrintf_3("  (期望:移除2项, x)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}
	{
		// 空列表
		XPrintf("removeDuplicates(空列表)=%zu  (期望:0)\n",
			XStringList_removeDuplicates(NULL));
	}
	{
		// 单元素
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "one");
		size_t removed = XStringList_removeDuplicates(list);
		XPrintf("removeDuplicates(单元素): 移除%zu项  (期望:0)\n", removed);
		XStringList_delete_base(list);
	}

	// ================================================================
	// filter 筛选测试
	// ================================================================
	XPrintf_3("\n--- filter 筛选测试 ---\n");
	{
		// 大小写敏感筛选
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "Hello");
		XStringList_push_back_utf8(list, "world");
		XStringList_push_back_utf8(list, "HELLO");
		XStringList_push_back_utf8(list, "test");
		XString* needle = XString_create_utf8("Hello");
		XStringList* filtered = XStringList_filter(list, needle, XChar_CaseSensitive);
		XString* joined = XStringList_join_utf8(filtered, ",");
		XPrintf_3("filter(Hello,大小写敏感) = "); XPrintf_2(joined);
		XPrintf_3("  (期望:Hello)\n");
		XString_delete_base(joined);
		XStringList_delete_base(filtered);
		XString_delete_base(needle);
		XStringList_delete_base(list);
	}
	{
		// 大小写不敏感筛选
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "Hello");
		XStringList_push_back_utf8(list, "world");
		XStringList_push_back_utf8(list, "HELLO");
		XStringList_push_back_utf8(list, "test");
		XString* needle = XString_create_utf8("Hello");
		XStringList* filtered = XStringList_filter(list, needle, XChar_CaseInsensitive);
		XString* joined = XStringList_join_utf8(filtered, ",");
		XPrintf_3("filter(Hello,大小写不敏感) = "); XPrintf_2(joined);
		XPrintf_3("  (期望:Hello,HELLO)\n");
		XString_delete_base(joined);
		XStringList_delete_base(filtered);
		XString_delete_base(needle);
		XStringList_delete_base(list);
	}
	{
		// filter_utf8
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "Hello");
		XStringList_push_back_utf8(list, "world");
		XStringList_push_back_utf8(list, "HELLO");
		XStringList_push_back_utf8(list, "test");
		XStringList* filtered = XStringList_filter_utf8(list, "test", XChar_CaseSensitive);
		XString* joined = XStringList_join_utf8(filtered, ",");
		XPrintf_3("filter_utf8(test) = "); XPrintf_2(joined);
		XPrintf_3("  (期望:test)\n");
		XString_delete_base(joined);
		XStringList_delete_base(filtered);
		XStringList_delete_base(list);
	}
	{
		// 无匹配
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "a");
		XStringList_push_back_utf8(list, "b");
		XStringList* filtered = XStringList_filter_utf8(list, "z", XChar_CaseSensitive);
		XPrintf("filter(无匹配): 结果%s  (期望:空列表)\n",
			XStringList_isEmpty_base(filtered) ? "为空" : "非空");
		XStringList_delete_base(filtered);
		XStringList_delete_base(list);
	}
	{
		// 全部匹配
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "abc");
		XStringList_push_back_utf8(list, "xabcx");
		XStringList_push_back_utf8(list, "abc123");
		XStringList* filtered = XStringList_filter_utf8(list, "abc", XChar_CaseSensitive);
		XPrintf("filter(全部匹配): 结果%zu项  (期望:3项)\n",
			XStringList_size_base(filtered));
		XStringList_delete_base(filtered);
		XStringList_delete_base(list);
	}
	{
		// 空列表筛选
		XStringList* list = XStringList_create();
		XStringList* filtered = XStringList_filter_utf8(list, "x", XChar_CaseSensitive);
		XPrintf("filter(空列表): 结果%s  (期望:空列表)\n",
			XStringList_isEmpty_base(filtered) ? "为空" : "非空");
		XStringList_delete_base(filtered);
		XStringList_delete_base(list);
	}

	// ================================================================
	// replaceInStrings 替换测试
	// ================================================================
	XPrintf_3("\n--- replaceInStrings 替换测试 ---\n");
	{
		// 大小写敏感替换
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "Hello World");
		XStringList_push_back_utf8(list, "hello world");
		XStringList_push_back_utf8(list, "HELLO WORLD");
		XString* before = XString_create_utf8("Hello");
		XString* after = XString_create_utf8("Hi");
		XStringList_replaceInStrings(list, before, after, XChar_CaseSensitive);
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf_3("replaceInStrings(Hello→Hi,敏感) = "); XPrintf_2(joined);
		XPrintf_3("  (期望:Hi World,hello world,HELLO WORLD)\n");
		XString_delete_base(joined);
		XString_delete_base(before);
		XString_delete_base(after);
		XStringList_delete_base(list);
	}
	{
		// 大小写不敏感替换（utf8）
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "Hello World");
		XStringList_push_back_utf8(list, "hello world");
		XStringList_push_back_utf8(list, "HELLO WORLD");
		XStringList_replaceInStrings_utf8(list, "world", "Earth", XChar_CaseInsensitive);
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf_3("replaceInStrings_utf8(world→Earth,不敏感) = "); XPrintf_2(joined);
		XPrintf_3("  (期望:Hello Earth,hello Earth,HELLO Earth)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}
	{
		// 无匹配项
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "abc");
		XStringList_push_back_utf8(list, "def");
		XStringList_replaceInStrings_utf8(list, "xyz", "yyy", XChar_CaseSensitive);
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf_3("replaceInStrings(无匹配) = "); XPrintf_2(joined);
		XPrintf_3("  (期望:abc,def)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}
	{
		// 多次出现
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "a-b-c");
		XStringList_push_back_utf8(list, "d-e-f");
		XStringList_replaceInStrings_utf8(list, "-", "_", XChar_CaseSensitive);
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf_3("replaceInStrings(-→_) = "); XPrintf_2(joined);
		XPrintf_3("  (期望:a_b_c,d_e_f)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}

	// ================================================================
	// contains 包含测试
	// ================================================================
	XPrintf_3("\n--- contains 包含测试 ---\n");
	{
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "Apple");
		XStringList_push_back_utf8(list, "Banana");
		XStringList_push_back_utf8(list, "Cherry");
		// 大小写敏感
		XString* s = XString_create_utf8("apple");
		XPrintf("contains(apple,敏感)=%s  (期望:否)\n",
			XStringList_contains(list, s, XChar_CaseSensitive) ? "是" : "否");
		XPrintf("contains(apple,不敏感)=%s  (期望:是)\n",
			XStringList_contains(list, s, XChar_CaseInsensitive) ? "是" : "否");
		XString_delete_base(s);
		// utf8
		XPrintf("contains_utf8(Banana)=%s  (期望:是)\n",
			XStringList_contains_utf8(list, "Banana", XChar_CaseSensitive) ? "是" : "否");
		XPrintf("contains_utf8(NotExist)=%s  (期望:否)\n",
			XStringList_contains_utf8(list, "NotExist", XChar_CaseSensitive) ? "是" : "否");
		// 空列表
		XPrintf("contains_utf8(空列表)=%s  (期望:否)\n",
			XStringList_contains_utf8(NULL, "x", XChar_CaseSensitive) ? "是" : "否");
		XStringList_delete_base(list);
	}

	// ================================================================
	// indexOf / lastIndexOf 测试
	// ================================================================
	XPrintf_3("\n--- indexOf / lastIndexOf 测试 ---\n");
	{
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "a");
		XStringList_push_back_utf8(list, "b");
		XStringList_push_back_utf8(list, "a");
		XStringList_push_back_utf8(list, "c");
		XStringList_push_back_utf8(list, "a");
		XString* s = XString_create_utf8("a");
		// 正向查找
		XPrintf("indexOf(a,from=0)=%lld  (期望:0)\n",
			(long long)XStringList_indexOf(list, s, 0, XChar_CaseSensitive));
		XPrintf("indexOf(a,from=1)=%lld  (期望:2)\n",
			(long long)XStringList_indexOf(list, s, 1, XChar_CaseSensitive));
		XPrintf("indexOf(a,from=5)=%lld  (期望:-1)\n",
			(long long)XStringList_indexOf(list, s, 5, XChar_CaseSensitive));
		// 反向查找
		XPrintf("lastIndexOf(a,-1)=%lld  (期望:4)\n",
			(long long)XStringList_lastIndexOf(list, s, -1, XChar_CaseSensitive));
		XPrintf("lastIndexOf(a,from=3)=%lld  (期望:2)\n",
			(long long)XStringList_lastIndexOf(list, s, 3, XChar_CaseSensitive));
		XPrintf("lastIndexOf(a,from=0)=%lld  (期望:0)\n",
			(long long)XStringList_lastIndexOf(list, s, 0, XChar_CaseSensitive));
		// 大小写不敏感
		XString* sA = XString_create_utf8("A");
		XPrintf("indexOf(A,不敏感)=%lld  (期望:0)\n",
			(long long)XStringList_indexOf(list, sA, 0, XChar_CaseInsensitive));
		XString_delete_base(sA);
		XString_delete_base(s);
		// utf8
		XPrintf("indexOf_utf8(b)=%lld  (期望:1)\n",
			(long long)XStringList_indexOf_utf8(list, "b", 0, XChar_CaseSensitive));
		XPrintf("indexOf_utf8(NotExist)=%lld  (期望:-1)\n",
			(long long)XStringList_indexOf_utf8(list, "NotExist", 0, XChar_CaseSensitive));
		XPrintf("lastIndexOf_utf8(b)=%lld  (期望:1)\n",
			(long long)XStringList_lastIndexOf_utf8(list, "b", -1, XChar_CaseSensitive));
		XStringList_delete_base(list);
	}

	// ================================================================
	// join 测试
	// ================================================================
	XPrintf_3("\n--- join 连接测试 ---\n");
	{
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "Hello");
		XStringList_push_back_utf8(list, "World");
		XStringList_push_back_utf8(list, "Test");
		XString* joined = XStringList_join_utf8(list, ", ");
		XPrintf_3("join(\", \") = "); XPrintf_2(joined);
		XPrintf_3("  (期望:Hello, World, Test)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}
	{
		// 单分隔符
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "A");
		XStringList_push_back_utf8(list, "B");
		XStringList_push_back_utf8(list, "C");
		XString* joined = XStringList_join_utf8(list, "-");
		XPrintf_3("join(\"-\") = "); XPrintf_2(joined);
		XPrintf_3("  (期望:A-B-C)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}

	// ================================================================
	// 基础操作测试
	// ================================================================
	XPrintf_3("\n--- 基础操作测试 ---\n");
	{
		XStringList* list = XStringList_create();
		// push_back / push_front / insert
		XStringList_push_back_utf8(list, "B");
		XStringList_push_front_utf8(list, "A");
		XStringList_insert_utf8(list, 1, "C");
		XString* joined = XStringList_join_utf8(list, ",");
		XPrintf_3("push+insert = "); XPrintf_2(joined);
		XPrintf_3("  (期望:A,C,B)\n");
		XString_delete_base(joined);
		// size / isEmpty
		XPrintf("size=%zu isEmpty=%s  (期望:3,否)\n",
			XStringList_size_base(list),
			XStringList_isEmpty_base(list) ? "是" : "否");
		// at / front / back
		XString* s = XStringList_at_base(list, 1);
		XPrintf("at(1)=%s  (期望:C)\n", XString_toUtf8(s));
		s = XStringList_front_base(list);
		XPrintf("front()=%s  (期望:A)\n", XString_toUtf8(s));
		s = XStringList_back_base(list);
		XPrintf("back()=%s  (期望:B)\n", XString_toUtf8(s));
		// pop
		XStringList_pop_back_base(list);
		XStringList_pop_front_base(list);
		joined = XStringList_join_utf8(list, ",");
		XPrintf_3("pop_back+pop_front = "); XPrintf_2(joined);
		XPrintf_3("  (期望:C)\n");
		XString_delete_base(joined);
		XStringList_delete_base(list);
	}
	{
		// create_copy
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "X");
		XStringList_push_back_utf8(list, "Y");
		XStringList* copy = XStringList_create_copy(list);
		XString* joined = XStringList_join_utf8(copy, ",");
		XPrintf_3("create_copy = "); XPrintf_2(joined);
		XPrintf_3("  (期望:X,Y)\n");
		XString_delete_base(joined);
		XStringList_delete_base(copy);
		XStringList_delete_base(list);
	}
	{
		// clear
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "A");
		XStringList_push_back_utf8(list, "B");
		XStringList_clear_base(list);
		XPrintf("clear后 isEmpty=%s  (期望:是)\n",
			XStringList_isEmpty_base(list) ? "是" : "否");
		XStringList_delete_base(list);
	}

	// ================================================================
	// NULL 安全性
	// ================================================================
	XPrintf_3("\n--- NULL 安全性 ---\n");
	{
		XStringList_sort(NULL, XChar_CaseSensitive);
		XPrintf_3("sort(NULL)=无崩溃\n");
		XPrintf("removeDuplicates(NULL)=%zu  (期望:0)\n",
			XStringList_removeDuplicates(NULL));
		XPrintf("filter(NULL)=%s  (期望:空)\n",
			XStringList_filter(NULL, NULL, XChar_CaseSensitive) ? "非空" : "空");
		XStringList_replaceInStrings(NULL, NULL, NULL, XChar_CaseSensitive);
		XPrintf_3("replaceInStrings(NULL)=无崩溃\n");
		XPrintf("contains(NULL)=%s  (期望:否)\n",
			XStringList_contains(NULL, NULL, XChar_CaseSensitive) ? "是" : "否");
		XPrintf("indexOf(NULL)=%lld  (期望:-1)\n",
			(long long)XStringList_indexOf(NULL, NULL, 0, XChar_CaseSensitive));
		XPrintf("lastIndexOf(NULL)=%lld  (期望:-1)\n",
			(long long)XStringList_lastIndexOf(NULL, NULL, -1, XChar_CaseSensitive));
	}

	XPrintf_3("\n========== XStringList Qt对齐测试结束 ==========\n");
	//XCoreApplication_quit();
}

/**
 * @brief 原始基础测试（重写为非循环版本）
 */
void XStringListTest()
{
#if XVector_ON
	XPrintf_3("========== XStringList 基础测试 ==========\n\n");
	{
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "你好");
		XStringList_push_back_utf8(list, "非常好");
		XStringList_push_back_utf8(list, "世界");
		XStringList_insert_utf8(list, 0, "彩虹猫");
		XStringList_push_front_utf8(list, "星小白");
		XString* str = XStringList_join_utf8(list, "-");
		if (str) {
			XPrintf("join结果: %s\n", XString_toUtf8(str));
			XString_delete_base(str);
		}
		XPrintf("遍历: ");
		XStringList_iterator_for_each(list, XFor_each_XString, NULL);
		XPrintf("\n");
		XPrintf("size=%zu\n", XStringList_size_base(list));
		XStringList_delete_base(list);
	}
#else
	IS_ON_DEBUG(XVector_ON);
#endif
	//XCoreApplication_quit();
}

void XTestMenu_XStringListTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XStringList(字符串数组)");
	XTestMenu_addMenu(root, menu);
	{
		XAction* action = XTestMenu_addAction(menu, "主测试");
		XTestMenu_setActionFunction(action, XStringListTest);
	}
	{
		XAction* action = XTestMenu_addAction(menu, "Qt对齐(sort/filter/contains)");
		XTestMenu_setActionFunction(action, XStringListQtAlignTest);
	}
}
#endif
