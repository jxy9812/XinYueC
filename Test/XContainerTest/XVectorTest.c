#include"XDataStructTest.h"
#if DEMOTEST
#include"XVector.h"
#include"XFunctionCallback.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XMemory.h"

//动态数组测试
static void XVectorTest();
static void XVectorCapacityTest();
static void XVectorAccessTest();
static void XVectorModifyTest();
static void XVectorLookupTest();
static void XVectorCowCompareTest();
static void XVectorMacroSafetyTest();

//打印单个整型元素
static void XFor_each_int(void* LPVal, void* args)
{
	(void)args;
	XPrintf("%d ", *(int*)LPVal);
}
//打印整型向量（带前缀）
static void XVectorPrintInt(XVector* v, const char* prefix)
{
	XPrintf("%s", prefix);
	XVector_iterator_for_each(v, XFor_each_int, NULL);
	XPrintf("\n");
}
//用一个整型数组构造并返回XVector（已设置int比较函数）
static XVector* XVectorMakeInt(const int* arr, size_t n)
{
	XVector* v = XVector_Create(int);
	XContainerSetCompare(v, int_compare);
	for (size_t i = 0; i < n; i++)
		XVector_Push_Back_Base(v, int, arr[i]);
	return v;
}
//removeIf谓词：删除偶数
static const bool XVectorRemoveEven(const void* val, const void* args)
{
	(void)args;
	return (*(const int*)val) % 2 == 0;
}

//主测试：头部插入、排序、查找、迭代器删除、拷贝
static void XVectorTest()
{
#if XVector_ON
	XPrintf("===== XVector 主测试 =====\n");
	XVector* v = XVector_Create(int);
	XContainerSetCompare(v, int_compare);
	int arr[] = { 100,123,456,4,8496,3,321,23,3,132,0 };
	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
		XVector_Push_Front_Base(v, int, arr[i]);
	XVector_Push_Front_Base(v, int, 9999);
	XVectorPrintInt(v, "头部插入后: ");
	XVector_sort_base(v, XSORT_ASC);
	XVectorPrintInt(v, "升序排序后: ");
	int findVal = 100;
	int64_t index = XVector_indexOf(v, &findVal, 0);
	XPrintf("查找100的索引: %lld\n", (long long)index);
	//用迭代器删除所有等于23的元素
	for (XVector_iterator it = XVector_begin(v), endIt = XVector_end(v); !XVector_iterator_equality(&it, &endIt);)
	{
		void* pValue = XVector_iterator_data(&it);
		if (*((int*)pValue) == 23)
			XVector_erase_base(v, &it, &it);
		else
			XVector_iterator_add(v, &it);
	}
	XVectorPrintInt(v, "删除23后: ");
	XVector* copy = XVector_create_copy(v);
	XVectorPrintInt(copy, "拷贝向量: ");
	XPrintf("两个向量是否相等: %s\n", XVector_equals(v, copy) ? "是" : "否");
	XVector_delete_base(v);
	XVector_delete_base(copy);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
	XCoreApplication_quit();
}

//容量与大小测试：resize/resizeForOverwrite/resize_2/reserve/squeeze/maxSize
static void XVectorCapacityTest()
{
#if XVector_ON
	XPrintf("===== 容量与大小测试 =====\n");
	XVector* v = XVector_Create(int);
	XContainerSetCompare(v, int_compare);
	int arr[] = { 1,2,3,4,5 };
	for (size_t i = 0; i < 5; i++)
		XVector_Push_Back_Base(v, int, arr[i]);
	XPrintf("初始 size=%zu capacity=%zu\n", XVector_size_base(v), XVector_capacity_base(v));
	XVector_reserve_base(v, 100);
	XPrintf("reserve(100)后 capacity=%zu\n", XVector_capacity_base(v));
	XVector_squeeze_base(v);
	XPrintf("squeeze后 capacity=%zu\n", XVector_capacity_base(v));
	XVector_resize_base(v, 8);
	XVectorPrintInt(v, "resize(8)后(新增置0): ");
	XVector_resizeForOverwrite(v, 10);
	XPrintf("resizeForOverwrite(10)后 size=%zu(新增元素未清零)\n", XVector_size_base(v));
	XVector_resize_base(v, 3);
	XVectorPrintInt(v, "resize(3)后: ");
	int fill = 9;
	XVector_resize_2(v, 6, &fill);
	XVectorPrintInt(v, "resize_2(6,9)后(新增填9): ");
	XPrintf("max_size=%zu  maxSize(sizeof(int))=%zu\n", XVector_max_size(v), XVector_maxSize(sizeof(int)));
	XPrintf("是否为空: %s\n", XVector_isEmpty_base(v) ? "是" : "否");
	XVector_delete_base(v);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
	XCoreApplication_quit();
}

//元素访问测试：at/operator[]/front/back/constFirst/constLast/first(n)/last(n)/value/data/constData
static void XVectorAccessTest()
{
#if XVector_ON
	XPrintf("===== 元素访问测试 =====\n");
	int arr[] = { 10,20,30,40,50 };
	XVector* v = XVectorMakeInt(arr, 5);
	XVectorPrintInt(v, "向量: ");
	XPrintf("at(2)=%d  operator[](2)=%d\n", *(int*)XVector_at_base(v, 2), XVector_At_Base(v, 2, int));
	XPrintf("front=%d  back=%d\n", *(int*)XVector_front_base(v), *(int*)XVector_back_base(v));
	XPrintf("constFirst=%d  constLast=%d\n", *(int*)XVector_constFirst(v), *(int*)XVector_constLast(v));
	int def = -1;
	XPrintf("value(2)=%d  value(99,默认-1)=%d\n", *(int*)XVector_value(v, 2, NULL), *(int*)XVector_value(v, 99, &def));
	XVector* first3 = XVector_first(v, 3);
	XVector* last2 = XVector_last(v, 2);
	XVectorPrintInt(first3, "first(3): ");
	XVectorPrintInt(last2, "last(2): ");
	int* d = (int*)XVector_data(v);
	const int* cd = (const int*)XVector_constData(v);
	XPrintf("data()[1]=%d  constData()[3]=%d\n", d[1], cd[3]);
	XVector_delete_base(first3);
	XVector_delete_base(last2);
	XVector_delete_base(v);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
	XCoreApplication_quit();
}

//增删改测试：insert/replace/fill/assign/move/swapItemsAt/takeAt/removeAll/removeOne/removeIf
static void XVectorModifyTest()
{
#if XVector_ON
	XPrintf("===== 增删改测试 =====\n");
	int arr[] = { 1,2,3,4,5 };
	XVector* v = XVectorMakeInt(arr, 5);
	XVectorPrintInt(v, "初始: ");
	int ins = 99;
	XVector_insert_2(v, 1, &ins);
	XVectorPrintInt(v, "insert(1,99): ");
	int rep = 100;
	XVector_replace_1(v, 0, &rep);
	XVectorPrintInt(v, "replace(0,100): ");
	XVector_move(v, 0, (int64_t)XVector_size_base(v) - 1);
	XVectorPrintInt(v, "move(0,末尾): ");
	XVector_swapItemsAt(v, 0, 1);
	XVectorPrintInt(v, "swapItemsAt(0,1): ");
	int f = 7;
	XVector_fill(v, &f, -1);
	XVectorPrintInt(v, "fill(7): ");
	int a = 8;
	XVector_assign(v, &a, 4);
	XVectorPrintInt(v, "assign(4,8): ");
	void* taken = XVector_takeAt(v, 1);
	XPrintf("takeAt(1)=%d\n", *(int*)taken);
	XFree_System(taken);
	XVectorPrintInt(v, "takeAt后: ");
	int rm = 8;
	size_t cnt = XVector_removeAll(v, &rm);
	XPrintf("removeAll(8)删除了%zu个\n", cnt);
	XVectorPrintInt(v, "removeAll后: ");
	//removeOne / removeIf
	int arr2[] = { 1,2,3,2,4 };
	XVector* v2 = XVectorMakeInt(arr2, 5);
	int one = 2;
	bool ok = XVector_removeOne(v2, &one);
	XPrintf("removeOne(2)=%s\n", ok ? "成功" : "失败");
	XVectorPrintInt(v2, "removeOne后: ");
	size_t removed = XVector_removeIf(v2, XVectorRemoveEven, NULL);
	XPrintf("removeIf(偶数)删除了%zu个\n", removed);
	XVectorPrintInt(v2, "removeIf后: ");
	XVector_delete_base(v);
	XVector_delete_base(v2);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
	XCoreApplication_quit();
}

//查找测试：indexOf/lastIndexOf/contains/count/startsWith/endsWith
static void XVectorLookupTest()
{
#if XVector_ON
	XPrintf("===== 查找测试 =====\n");
	int arr[] = { 5,3,8,3,9,3,1 };
	XVector* v = XVectorMakeInt(arr, 7);
	XVectorPrintInt(v, "向量: ");
	int key = 3;
	XPrintf("indexOf(3)=%lld  lastIndexOf(3)=%lld\n", (long long)XVector_indexOf(v, &key, 0), (long long)XVector_lastIndexOf(v, &key, -1));
	int k9 = 9, k100 = 100;
	XPrintf("contains(9)=%s  contains(100)=%s\n", XVector_contains(v, &k9) ? "是" : "否", XVector_contains(v, &k100) ? "是" : "否");
	XPrintf("count(3)=%zu\n", XVector_count_value(v, &key));
	int sf = 5, ef = 1, sf2 = 8;
	XPrintf("startsWith(5)=%s  startsWith(8)=%s  endsWith(1)=%s\n", XVector_startsWith(v, &sf) ? "是" : "否", XVector_startsWith(v, &sf2) ? "是" : "否", XVector_endsWith(v, &ef) ? "是" : "否");
	XVector_delete_base(v);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
	XCoreApplication_quit();
}

//COW共享与比较测试：isSharedWith/detach/isDetached/compare/关系运算/mid/sliced
static void XVectorCowCompareTest()
{
#if XVector_ON
	XPrintf("===== COW共享与比较测试 =====\n");
	int arr[] = { 1,2,3,4,5 };
	XVector* v = XVectorMakeInt(arr, 5);
	XVector* copy = XVector_create_copy(v);
	XPrintf("拷贝后 isSharedWith=%s  isDetached=%s\n", XVector_isSharedWith(v, copy) ? "是" : "否", XVector_isDetached(v) ? "是" : "否");
	XVector_detach(v);
	XPrintf("detach后 isSharedWith=%s  isDetached=%s\n", XVector_isSharedWith(v, copy) ? "是" : "否", XVector_isDetached(v) ? "是" : "否");
	int arr2[] = { 1,2,3,4,5 };
	int arr3[] = { 1,2,3,4,6 };
	int arr4[] = { 1,2,3 };
	XVector* v2 = XVectorMakeInt(arr2, 5);
	XVector* v3 = XVectorMakeInt(arr3, 5);
	XVector* v4 = XVectorMakeInt(arr4, 3);
	XPrintf("v2==v3 equals=%s\n", XVector_equals(v2, v3) ? "是" : "否");
	XPrintf("v2<v3 lessThan=%s  v2>v4 greaterThan=%s\n", XVector_lessThan(v2, v3) ? "是" : "否", XVector_greaterThan(v2, v4) ? "是" : "否");
	XPrintf("v2<=v4 lessEqual=%s  v2>=v4 greaterEqual=%s\n", XVector_lessEqual(v2, v4) ? "是" : "否", XVector_greaterEqual(v2, v4) ? "是" : "否");
	XPrintf("compare(v2,v3)=%d  compare(v2,v2)=%d\n", XVector_compare(v2, v3), XVector_compare(v2, v2));
	XVector* mid = XVector_mid(v, 1, 3);
	XVector* sl1 = XVector_sliced_1(v, 2);
	XVector* sl2 = XVector_sliced_2(v, 1, 2);
	XVectorPrintInt(mid, "mid(1,3): ");
	XVectorPrintInt(sl1, "sliced(2): ");
	XVectorPrintInt(sl2, "sliced(1,2): ");
	XVector_delete_base(mid);
	XVector_delete_base(sl1);
	XVector_delete_base(sl2);
	XVector_delete_base(v);
	XVector_delete_base(copy);
	XVector_delete_base(v2);
	XVector_delete_base(v3);
	XVector_delete_base(v4);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
	XCoreApplication_quit();
}

//宏安全与类型检查测试：6个转为宏的API的NULL安全 + insert_3/insert_move_3类型一致性
static void XVectorMacroSafetyTest()
{
#if XVector_ON
	XPrintf("===== 宏安全与类型检查测试 =====\n");
	//1. 6个转为宏的API对NULL入参应安全返回（不崩溃）
	int val = 1;
	XPrintf("insert_2(NULL)=%s  push_front_2(NULL)=%s  push_front_move_2(NULL)=%s\n",
		XVector_insert_2(NULL, 0, &val) ? "是" : "否",
		XVector_push_front_2(NULL, &val, 1) ? "是" : "否",
		XVector_push_front_move_2(NULL, &val, 1) ? "是" : "否");
	XPrintf("insert_move_2(NULL)=%s  contains(NULL)=%s  first(NULL)=%s\n",
		XVector_insert_move_2(NULL, 0, &val) ? "是" : "否",
		XVector_contains(NULL, &val) ? "是" : "否",
		XVector_first(NULL, 3) ? "非空" : "空");
	//2. 宏别名链式展开：prepend_2 -> push_front_2 -> insert_1_base
	int arr[] = { 1,2,3 };
	XVector* v = XVectorMakeInt(arr, 3);
	int head[] = { 7,8 };
	XVector_prepend_2(v, head, 2);
	XVectorPrintInt(v, "prepend_2({7,8}): ");
	//3. insert_3/insert_move_3 类型不一致应拒绝（与 push_front_3/push_back_3 行为一致）
	XVector* dbl = XVector_create(sizeof(double));
	XVector_Push_Back_Base(dbl, double, 1.5);
	bool rejected = XVector_insert_3(v, 0, dbl);
	bool rejMove = XVector_insert_move_3(v, 0, dbl);
	XPrintf("insert_3(类型不一致)=%s  insert_move_3(类型不一致)=%s（均应否）\n",
		rejected ? "是" : "否", rejMove ? "是" : "否");
	//4. insert_3 同类型应成功
	XVector* same = XVectorMakeInt(arr, 3);
	bool accepted = XVector_insert_3(v, 0, same);
	XPrintf("insert_3(同类型)=%s（应是）\n", accepted ? "是" : "否");
	XVectorPrintInt(v, "insert_3同类型后: ");
	XVector_delete_base(v);
	XVector_delete_base(dbl);
	XVector_delete_base(same);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
	XCoreApplication_quit();
}

void XMenu_XVectorTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XVector(数组)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XVectorTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "容量与大小测试");
		XAction_setAction(action, XVectorCapacityTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "元素访问测试");
		XAction_setAction(action, XVectorAccessTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "增删改测试");
		XAction_setAction(action, XVectorModifyTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "查找测试");
		XAction_setAction(action, XVectorLookupTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "COW共享与比较测试");
		XAction_setAction(action, XVectorCowCompareTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "宏安全与类型检查测试");
		XAction_setAction(action, XVectorMacroSafetyTest);
	}
}
#endif
