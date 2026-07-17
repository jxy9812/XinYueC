#include"XDataStructTest.h"
#if DEMOTEST
#include"XVector.h"
#include"XFunctionCallback.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XMemory.h"

static void XFor_each_int(void* LPVal, void* args)
{
	(void)args;
	XPrintf("%d ", *(int*)LPVal);
}

static void XVectorPrintInt(XVector* v, const char* prefix)
{
	XPrintf("%s", prefix);
	XVector_iterator_for_each(v, XFor_each_int, NULL);
	XPrintf("\n");
}

static XVector* XVectorMakeInt(const int* arr, size_t n)
{
	XVector* v = XVector_Create(int);
	XContainerSetCompare(v, int_compare);
	for (size_t i = 0; i < n; i++)
		XVector_Push_Back_Base(v, int, arr[i]);
	return v;
}

static const bool XVectorRemoveEven(const void* val, const void* args)
{
	(void)args;
	return (*(const int*)val) % 2 == 0;
}

static void XVectorCreateTest(void)
{
	XPrintf("===== 创建与初始化测试 =====\n");
	{
		XVector* v = XVector_Create(int);
		XPrintf("XVector_Create(int): size=%zu, capacity=%zu, isEmpty=%s\n",
			XVector_size_base(v), XVector_capacity_base(v),
			XVector_isEmpty_base(v) ? "是" : "否");
		XVector_delete_base(v);
	}
	{
		XVector* v = XVector_create_ex(sizeof(int), false);
		XPrintf("create_ex(int,cow=false): size=%zu, typeSize=%zu\n",
			XVector_size_base(v), XVector_typeSize_base(v));
		XVector_delete_base(v);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XVector* src = XVectorMakeInt(arr, 5);
		XVector* copy = XVector_create_copy(src);
		XPrintf("create_copy: equals=%s\n",
			XVector_equals(src, copy) ? "是" : "否");
		XVector_Push_Back_Base(src, int, 99);
		XPrintf("  修改源后: equals=%s (期望:否)\n",
			XVector_equals(src, copy) ? "是" : "否");
		XVector_delete_base(src);
		XVector_delete_base(copy);
	}
	{
		int arr[] = { 10, 20, 30 };
		XVector* src = XVectorMakeInt(arr, 3);
		XVector* moved = XVector_create_move(src);
		XPrintf("create_move: moved.size=%zu, src.isEmpty=%s\n",
			XVector_size_base(moved),
			XVector_isEmpty_base(src) ? "是" : "否");
		XVectorPrintInt(moved, "  moved: ");
		XVector_delete_base(moved);
		XVector_delete_base(src);
	}
	{
		XVector v;
		XVector_init(&v, sizeof(int), true);
		XPrintf("init: size=%zu, isEmpty=%s\n",
			XVector_size_base(&v), XVector_isEmpty_base(&v) ? "是" : "否");
		XVector_deinit_base(&v);
	}
	XPrintf("\n");
}

static void XVectorCapacityTest(void)
{
	XPrintf("===== 容量与大小测试 =====\n");
	XVector* v = XVector_Create(int);
	XContainerSetCompare(v, int_compare);
	XPrintf("初始: size=%zu, capacity=%zu\n",
		XVector_size_base(v), XVector_capacity_base(v));
	int arr[] = { 1, 2, 3, 4, 5 };
	for (size_t i = 0; i < 5; i++) XVector_Push_Back_Base(v, int, arr[i]);
	XPrintf("填充5个: size=%zu, capacity=%zu\n",
		XVector_size_base(v), XVector_capacity_base(v));
	XVector_reserve_base(v, 100);
	XPrintf("reserve(100): capacity=%zu (期望>=100)\n", XVector_capacity_base(v));
	XVector_squeeze_base(v);
	XPrintf("squeeze: capacity=%zu (期望==5)\n", XVector_capacity_base(v));
	XVector_resize_base(v, 8);
	XVectorPrintInt(v, "resize(8): ");
	XVector_resize_base(v, 3);
	XVectorPrintInt(v, "resize(3): ");
	int fill = 9;
	XVector_resize_2(v, 6, &fill);
	XVectorPrintInt(v, "resize_2(6,9): ");
	XVector_resizeForOverwrite(v, 10);
	XPrintf("resizeForOverwrite(10): size=%zu\n", XVector_size_base(v));
	XPrintf("max_size=%zu, maxSize(sizeof(int))=%zu\n",
		XVector_max_size(v), XVector_maxSize(sizeof(int)));
	XVector_shrink_to_fit(v);
	XPrintf("shrink_to_fit: capacity=%zu\n", XVector_capacity_base(v));
	XPrintf("别名: size=%zu, count_base=%zu, length_base=%zu\n",
		XVector_size(v), XVector_count_base(v), XVector_length_base(v));
	XVector_delete_base(v);
	XPrintf("\n");
}

static void XVectorAccessTest(void)
{
	XPrintf("===== 元素访问测试 =====\n");
	int arr[] = { 10, 20, 30, 40, 50 };
	XVector* v = XVectorMakeInt(arr, 5);
	XPrintf("at(0)=%d, at(2)=%d, at(4)=%d\n",
		*(int*)XVector_at_base(v, 0),
		*(int*)XVector_at_base(v, 2),
		*(int*)XVector_at_base(v, 4));
	XPrintf("operator[](2)=%d\n", XVector_At_Base(v, 2, int));
	XPrintf("front=%d, back=%d\n",
		*(int*)XVector_front_base(v),
		*(int*)XVector_back_base(v));
	XPrintf("Front_Base=%d, Back_Base=%d\n",
		XVector_Front_Base(v, int),
		XVector_Back_Base(v, int));
	XPrintf("constFirst=%d, constLast=%d\n",
		*(int*)XVector_constFirst(v),
		*(int*)XVector_constLast(v));
	XPrintf("front别名=%d, back别名=%d\n",
		*(int*)XVector_front(v),
		*(int*)XVector_back(v));
	int def = -1;
	XPrintf("value(2)=%d, value(99,默认-1)=%d\n",
		*(int*)XVector_value(v, 2, NULL),
		*(int*)XVector_value(v, 99, &def));
	{
		XVector* f = XVector_first(v, 3);
		XVectorPrintInt(f, "first(3): ");
		XVector_delete_base(f);
	}
	{
		XVector* l = XVector_last(v, 2);
		XVectorPrintInt(l, "last(2): ");
		XVector_delete_base(l);
	}
	int* d = (int*)XVector_data(v);
	const int* cd = (const int*)XVector_constData(v);
	XPrintf("data()[1]=%d, constData()[3]=%d\n", d[1], cd[3]);
	{
		XVector* empty = XVector_Create(int);
		XPrintf("空: front=%s, back=%s, at(0)=%s\n",
			XVector_front_base(empty) ? "非空" : "空",
			XVector_back_base(empty) ? "非空" : "空",
			XVector_at_base(empty, 0) ? "非空" : "空");
		XVector_delete_base(empty);
	}
	XVector_delete_base(v);
	XPrintf("\n");
}

static void XVectorFrontOpsTest(void)
{
	XPrintf("===== 头部操作测试 =====\n");
	int arr[] = { 1, 2, 3 };
	XVector* v = XVectorMakeInt(arr, 3);
	XVectorPrintInt(v, "初始: ");
	int val = 0;
	XVector_push_front_1_base(v, &val);
	XVectorPrintInt(v, "push_front_1(0): ");
	int* p = XMalloc_System(sizeof(int)); *p = 99;
	XVector_push_front_move_1_base(v, p);
	XVectorPrintInt(v, "push_front_move_1(99): ");
	{
		int a[] = { 7, 8 };
		XVector* v2 = XVectorMakeInt(a, 2);
		XVector_push_front_3(v, v2);
		XVectorPrintInt(v, "push_front_3({7,8}): ");
		XVector_delete_base(v2);
	}
	{
		int a[] = { 100 };
		XVector* v3 = XVectorMakeInt(a, 1);
		XVector_push_front_move_3(v, v3);
		XVectorPrintInt(v, "push_front_move_3({100}): ");
		XVector_delete_base(v3);
	}
	int prep[] = { 5, 6 };
	XVector_prepend_2(v, prep, 2);
	XVectorPrintInt(v, "prepend_2({5,6}): ");
	XVector_pop_front_base(v);
	XVectorPrintInt(v, "pop_front: ");
	XVector_delete_base(v);
	XPrintf("\n");
}

static void XVectorBackOpsTest(void)
{
	XPrintf("===== 尾部操作测试 =====\n");
	int arr[] = { 1, 2, 3 };
	XVector* v = XVectorMakeInt(arr, 3);
	XVectorPrintInt(v, "初始: ");
	int val = 4;
	XVector_push_back_1_base(v, &val);
	XVectorPrintInt(v, "push_back_1(4): ");
	int arr2[] = { 5, 6, 7 };
	XVector_push_back_2(v, arr2, 3);
	XVectorPrintInt(v, "push_back_2({5,6,7}): ");
	{
		int a[] = { 8, 9 };
		XVector* v3 = XVectorMakeInt(a, 2);
		XVector_push_back_3(v, v3);
		XVectorPrintInt(v, "push_back_3({8,9}): ");
		XVector_delete_base(v3);
	}
	int* p = XMalloc_System(sizeof(int)); *p = 10;
	XVector_push_back_move_1_base(v, p);
	XVectorPrintInt(v, "push_back_move_1(10): ");
	int* p2 = XMalloc_System(sizeof(int) * 2); p2[0] = 11; p2[1] = 12;
	XVector_push_back_move_2(v, p2, 2);
	XVectorPrintInt(v, "push_back_move_2({11,12}): ");
	int ap[] = { 14, 15 };
	XVector_append_2(v, ap, 2);
	XVectorPrintInt(v, "append_2({14,15}): ");
	XVector_pop_back_base(v);
	XVectorPrintInt(v, "pop_back: ");
	XVector_delete_base(v);
	XPrintf("\n");
}

static void XVectorInsertTest(void)
{
	XPrintf("===== 插入操作测试 =====\n");
	int arr[] = { 1, 5, 9 };
	XVector* v = XVectorMakeInt(arr, 3);
	XVectorPrintInt(v, "初始: ");
	int head[] = { 0 };
	XVector_insert_1_base(v, 0, head, 1);
	XVectorPrintInt(v, "insert_1(0,{0}): ");
	int mid[] = { 2, 3, 4 };
	XVector_insert_1_base(v, 2, mid, 3);
	XVectorPrintInt(v, "insert_1(2,{2,3,4}): ");
	int val = 99;
	XVector_insert_2(v, 4, &val);
	XVectorPrintInt(v, "insert_2(4,99): ");
	{
		int a[] = { 6, 7, 8 };
		XVector* v2 = XVectorMakeInt(a, 3);
		XVector_insert_3(v, 6, v2);
		XVectorPrintInt(v, "insert_3(6,{6,7,8}): ");
		XVector_delete_base(v2);
	}
	int* p = XMalloc_System(sizeof(int) * 2); p[0] = 100; p[1] = 101;
	XVector_insert_move_1_base(v, 0, p, 2);
	XVectorPrintInt(v, "insert_move_1(0,{100,101}): ");
	int* p2 = XMalloc_System(sizeof(int)); *p2 = 102;
	XVector_insert_move_2(v, 0, p2);
	XVectorPrintInt(v, "insert_move_2(0,102): ");
	XVector_delete_base(v);
	XPrintf("\n");
}

static void XVectorRemoveTest(void)
{
	XPrintf("===== 删除操作测试 =====\n");
	{
		int arr[] = { 1, 2, 3, 2, 4, 2, 5, 2 };
		XVector* v = XVectorMakeInt(arr, 8);
		XVectorPrintInt(v, "初始: ");
		int t = 2;
		size_t r = XVector_removeAll(v, &t);
		XPrintf("removeAll(2): 移除%zu个\n", r);
		XVectorPrintInt(v, "  结果: ");
		t = 4;
		XPrintf("removeOne(4): %s\n", XVector_removeOne(v, &t) ? "是" : "否");
		t = 999;
		XPrintf("removeOne(999): %s\n", XVector_removeOne(v, &t) ? "是" : "否");
		XVector_removeAt_base(v, 1);
		XVectorPrintInt(v, "removeAt(1): ");
		XVector_remove_base(v, 0, 2);
		XVectorPrintInt(v, "remove(0,2): ");
		XVector_pop_front_base(v);
		XVector_pop_back_base(v);
		XVectorPrintInt(v, "pop_front+pop_back: ");
		XVector_delete_base(v);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
		XVector* v = XVectorMakeInt(arr, 10);
		size_t r = XVector_removeIf(v, XVectorRemoveEven, NULL);
		XPrintf("removeIf(偶数): 移除%zu个\n", r);
		XVectorPrintInt(v, "  结果: ");
		XVector_delete_base(v);
	}
	{
		int arr[] = { 1, 2, 3, 2, 4, 2, 5 };
		XVector* v = XVectorMakeInt(arr, 7);
		for (XVector_iterator it = XVector_begin(v), endIt = XVector_end(v);
			!XVector_iterator_equality(&it, &endIt);)
		{
			if (*(int*)XVector_iterator_data(&it) == 2)
				XVector_erase_base(v, &it, &it);
			else
				XVector_iterator_add(v, &it);
		}
		XVectorPrintInt(v, "erase(所有2): ");
		XVector_delete_base(v);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XVector* v = XVectorMakeInt(arr, 5);
		XVector_clear_base(v);
		XPrintf("clear: isEmpty=%s, size=%zu\n",
			XVector_isEmpty_base(v) ? "是" : "否",
			XVector_size_base(v));
		XVector_delete_base(v);
	}
	XPrintf("\n");
}

static void XVectorTakeTest(void)
{
	XPrintf("===== 取出元素测试 =====\n");
	int arr[] = { 10, 20, 30, 40, 50 };
	XVector* v = XVectorMakeInt(arr, 5);
	XVectorPrintInt(v, "初始: ");
	{
		int* p = (int*)XVector_takeAt(v, 2);
		XPrintf("takeAt(2)=%d\n", *p); XFree_System(p);
	}
	XVectorPrintInt(v, "  takeAt后: ");
	{
		int* p = (int*)XVector_takeFirst(v);
		XPrintf("takeFirst=%d\n", *p); XFree_System(p);
	}
	XVectorPrintInt(v, "  takeFirst后: ");
	{
		int* p = (int*)XVector_takeLast(v);
		XPrintf("takeLast=%d\n", *p); XFree_System(p);
	}
	XVectorPrintInt(v, "  takeLast后: ");
	{
		XVector* empty = XVector_Create(int);
		XPrintf("空: takeAt=%s, takeFirst=%s, takeLast=%s\n",
			XVector_takeAt(empty, 0) ? "非空" : "空",
			XVector_takeFirst(empty) ? "非空" : "空",
			XVector_takeLast(empty) ? "非空" : "空");
		XVector_delete_base(empty);
	}
	XVector_delete_base(v);
	XPrintf("\n");
}

static void XVectorReplaceTest(void)
{
	XPrintf("===== 替换操作测试 =====\n");
	int arr[] = { 1, 2, 3, 4, 5 };
	XVector* v = XVectorMakeInt(arr, 5);
	XVectorPrintInt(v, "初始: ");
	int nv = 99;
	XVector_replace_1(v, 2, &nv);
	XVectorPrintInt(v, "replace_1(2,99): ");
	{
		int a[] = { 100, 200 };
		XVector* v2 = XVectorMakeInt(a, 2);
		XVector_replace_2(v, 0, v2);
		XVectorPrintInt(v, "replace_2(0,{100,200}): ");
		XVector_delete_base(v2);
	}
	int* p = XMalloc_System(sizeof(int)); *p = 300;
	XVector_replace_move_1(v, 3, p);
	XVectorPrintInt(v, "replace_move_1(3,300): ");
	int av = 888;
	XVector_replace(v, 1, &av);
	XVectorPrintInt(v, "XVector_replace(1,888): ");
	XVector_delete_base(v);
	XPrintf("\n");
}

static void XVectorSortFindTest(void)
{
	XPrintf("===== 排序与查找测试 =====\n");
	{
		int arr[] = { 5, 2, 8, 1, 9, 3, 7, 4, 6, 0 };
		XVector* v = XVectorMakeInt(arr, 10);
		XVector_sort_base(v, XSORT_ASC);
		XVectorPrintInt(v, "升序: ");
		XVector_sort_base(v, XSORT_DESC);
		XVectorPrintInt(v, "降序: ");
		XVector_delete_base(v);
	}
	{
		int arr[] = { 10, 20, 30, 20, 40, 20, 50 };
		XVector* v = XVectorMakeInt(arr, 7);
		int f = 20, n = 999;
		XPrintf("indexOf(20,0)=%lld, indexOf(20,3)=%lld\n",
			(long long)XVector_indexOf(v, &f, 0),
			(long long)XVector_indexOf(v, &f, 3));
		XPrintf("lastIndexOf(20,-1)=%lld, lastIndexOf(20,4)=%lld\n",
			(long long)XVector_lastIndexOf(v, &f, -1),
			(long long)XVector_lastIndexOf(v, &f, 4));
		XPrintf("indexOf(999)=%lld (期望-1)\n",
			(long long)XVector_indexOf(v, &n, 0));
		XPrintf("contains(20)=%s, contains(999)=%s\n",
			XVector_contains(v, &f) ? "是" : "否",
			XVector_contains(v, &n) ? "是" : "否");
		int fst = 10, lst = 50, wr = 99;
		XPrintf("startsWith(10)=%s, startsWith(99)=%s\n",
			XVector_startsWith(v, &fst) ? "是" : "否",
			XVector_startsWith(v, &wr) ? "是" : "否");
		XPrintf("endsWith(50)=%s, endsWith(99)=%s\n",
			XVector_endsWith(v, &lst) ? "是" : "否",
			XVector_endsWith(v, &wr) ? "是" : "否");
		XPrintf("count_value(20)=%zu, count_value(999)=%zu\n",
			XVector_count_value(v, &f), XVector_count_value(v, &n));
		XPrintf("XVector_count(20)=%zu\n", XVector_count(v, &f));
		XVector_delete_base(v);
	}
	XPrintf("\n");
}

static void XVectorCompareTest(void)
{
	XPrintf("===== 比较测试 =====\n");
	int a1[] = { 1, 2, 3, 4, 5 };
	int a2[] = { 1, 2, 3, 4, 5 };
	int a3[] = { 1, 2, 3, 4, 6 };
	int a4[] = { 1, 2, 3 };
	XVector* v1 = XVectorMakeInt(a1, 5);
	XVector* v2 = XVectorMakeInt(a2, 5);
	XVector* v3 = XVectorMakeInt(a3, 5);
	XVector* v4 = XVectorMakeInt(a4, 3);
	XPrintf("v1==v2: equals=%s, compare=%d\n",
		XVector_equals(v1, v2) ? "是" : "否", XVector_compare(v1, v2));
	XPrintf("v1<v3: lessThan=%s, compare=%d\n",
		XVector_lessThan(v1, v3) ? "是" : "否", XVector_compare(v1, v3));
	XPrintf("v1>v4: greaterThan=%s\n",
		XVector_greaterThan(v1, v4) ? "是" : "否");
	XPrintf("v1>=v2: greaterEqual=%s\n",
		XVector_greaterEqual(v1, v2) ? "是" : "否");
	XPrintf("v1<=v2: lessEqual=%s\n",
		XVector_lessEqual(v1, v2) ? "是" : "否");
	XVector_delete_base(v1);
	XVector_delete_base(v2);
	XVector_delete_base(v3);
	XVector_delete_base(v4);
	XPrintf("\n");
}

static void XVectorCowTest(void)
{
	XPrintf("===== COW与共享测试 =====\n");
	int arr[] = { 1, 2, 3, 4, 5 };
	XVector* v = XVectorMakeInt(arr, 5);
	XVector* copy = XVector_create_copy(v);
	XPrintf("拷贝后: isSharedWith=%s, isDetached=%s\n",
		XVector_isSharedWith(v, copy) ? "是" : "否",
		XVector_isDetached(v) ? "是" : "否");
	XVector_detach(v);
	XPrintf("detach后: isSharedWith=%s, isDetached=%s\n",
		XVector_isSharedWith(v, copy) ? "是" : "否",
		XVector_isDetached(v) ? "是" : "否");
	XVector_Push_Back_Base(copy, int, 99);
	XPrintf("修改copy后: isSharedWith=%s\n",
		XVector_isSharedWith(v, copy) ? "是" : "否");
	XPrintf("NULL: isSharedWith(v,NULL)=%s, isDetached(NULL)=%s\n",
		XVector_isSharedWith(v, NULL) ? "是" : "否",
		XVector_isDetached(NULL) ? "是" : "否");
	XVector_delete_base(v);
	XVector_delete_base(copy);
	XPrintf("\n");
}

static void XVectorSliceTest(void)
{
	XPrintf("===== 子向量测试 =====\n");
	int arr[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	XVector* v = XVectorMakeInt(arr, 10);
	{
		XVector* m = XVector_mid(v, 2, 4);
		XVectorPrintInt(m, "mid(2,4): "); XVector_delete_base(m);
	}
	{
		XVector* m = XVector_mid(v, 5, -1);
		XVectorPrintInt(m, "mid(5,-1): "); XVector_delete_base(m);
	}
	{
		XVector* f = XVector_first(v, 3);
		XVectorPrintInt(f, "first(3): "); XVector_delete_base(f);
	}
	{
		XVector* l = XVector_last(v, 3);
		XVectorPrintInt(l, "last(3): "); XVector_delete_base(l);
	}
	{
		XVector* s = XVector_sliced_1(v, 7);
		XVectorPrintInt(s, "sliced(7): "); XVector_delete_base(s);
	}
	{
		XVector* s = XVector_sliced_2(v, 3, 3);
		XVectorPrintInt(s, "sliced(3,3): "); XVector_delete_base(s);
	}
	XVector_delete_base(v);
	XPrintf("\n");
}

static void XVectorMoveSwapTest(void)
{
	XPrintf("===== 移动与交换测试 =====\n");
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XVector* v = XVectorMakeInt(arr, 5);
		XVector_move(v, 0, 3);
		XVectorPrintInt(v, "move(0,3): ");
		XVector_move(v, 4, 1);
		XVectorPrintInt(v, "move(4,1): ");
		XVector_delete_base(v);
	}
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XVector* v = XVectorMakeInt(arr, 5);
		XVector_swapItemsAt(v, 0, 4);
		XVectorPrintInt(v, "swapItemsAt(0,4): ");
		XVector_swapItemsAt(v, 1, 3);
		XVectorPrintInt(v, "swapItemsAt(1,3): ");
		XVector_delete_base(v);
	}
	{
		int a1[] = { 1, 2, 3 };
		int a2[] = { 4, 5, 6, 7 };
		XVector* v1 = XVectorMakeInt(a1, 3);
		XVector* v2 = XVectorMakeInt(a2, 4);
		XVector_swap_base(v1, v2);
		XVectorPrintInt(v1, "swap后v1: ");
		XVectorPrintInt(v2, "swap后v2: ");
		XPrintf("v1.size=%zu, v2.size=%zu\n",
			XVector_size_base(v1), XVector_size_base(v2));
		XVector_delete_base(v1);
		XVector_delete_base(v2);
	}
	XPrintf("\n");
}

static void XVectorFillTest(void)
{
	XPrintf("===== 填充与赋值测试 =====\n");
	XVector* v = XVector_Create(int);
	XContainerSetCompare(v, int_compare);
	int val = 42;
	XVector_fill(v, &val, 5);
	XVectorPrintInt(v, "fill(42,5): ");
	int val2 = 77;
	XVector_assign(v, &val2, 3);
	XVectorPrintInt(v, "assign(77,3): ");
	XVector_delete_base(v);
	XPrintf("\n");
}

static void XVectorSafetyTest(void)
{
	XPrintf("===== 安全与类型检查测试 =====\n");
	int val = 1;
	XPrintf("insert_2(NULL)=%s, push_front_2(NULL)=%s\n",
		XVector_insert_2(NULL, 0, &val) ? "是" : "否",
		XVector_push_front_2(NULL, &val, 1) ? "是" : "否");
	XPrintf("insert_move_2(NULL)=%s, contains(NULL)=%s\n",
		XVector_insert_move_2(NULL, 0, &val) ? "是" : "否",
		XVector_contains(NULL, &val) ? "是" : "否");
	XPrintf("first(NULL)=%s, mid(NULL)=%s\n",
		XVector_first(NULL, 3) ? "非空" : "空",
		XVector_mid(NULL, 0, 1) ? "非空" : "空");
	XPrintf("isEmpty(NULL)=%s, size(NULL)=%zu\n",
		XVector_isEmpty_base(NULL) ? "是" : "否",
		XVector_size_base(NULL));
	XPrintf("indexOf(NULL)=%lld, lastIndexOf(NULL)=%lld\n",
		(long long)XVector_indexOf(NULL, &val, 0),
		(long long)XVector_lastIndexOf(NULL, &val, -1));
	{
		int arr[] = { 1, 2, 3 };
		XVector* v = XVectorMakeInt(arr, 3);
		XVector* dbl = XVector_create(sizeof(double));
		XVector_Push_Back_Base(dbl, double, 1.5);
		XPrintf("insert_3(类型不一致)=%s, insert_move_3(类型不一致)=%s（均应否）\n",
			XVector_insert_3(v, 0, dbl) ? "是" : "否",
			XVector_insert_move_3(v, 0, dbl) ? "是" : "否");
		XVector_delete_base(v);
		XVector_delete_base(dbl);
	}
	{
		int arr[] = { 1, 2, 3 };
		XVector* v = XVectorMakeInt(arr, 3);
		XVector* dbl = XVector_create(sizeof(double));
		XVector_Push_Back_Base(dbl, double, 3.14);
		XPrintf("push_front_3(类型不一致)=%s, push_back_3(类型不一致)=%s（均应否）\n",
			XVector_push_front_3(v, dbl) ? "是" : "否",
			XVector_push_back_3(v, dbl) ? "是" : "否");
		XVector_delete_base(v);
		XVector_delete_base(dbl);
	}
	XPrintf("\n");
}

void XVectorAllTest(void)
{
	XPrintf("========== XVector 全部测试开始 ==========\n\n");
	XVectorCreateTest();
	XVectorCapacityTest();
	XVectorAccessTest();
	XVectorFrontOpsTest();
	XVectorBackOpsTest();
	XVectorInsertTest();
	XVectorRemoveTest();
	XVectorTakeTest();
	XVectorReplaceTest();
	XVectorSortFindTest();
	XVectorCompareTest();
	XVectorCowTest();
	XVectorSliceTest();
	XVectorMoveSwapTest();
	XVectorFillTest();
	XVectorSafetyTest();
	XPrintf("\n========== XVector 全部测试结束 ==========\n");
	XCoreApplication_quit();
}

void XMenu_XVectorTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XVector(数组)");
	XMenu_addMenu(root, menu);
	{
		XAction* a = XMenu_addAction(menu, "【全部测试】");
		XAction_setAction(a, XVectorAllTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "创建与初始化");
		XAction_setAction(a, XVectorCreateTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "容量与大小");
		XAction_setAction(a, XVectorCapacityTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "元素访问");
		XAction_setAction(a, XVectorAccessTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "头部操作");
		XAction_setAction(a, XVectorFrontOpsTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "尾部操作");
		XAction_setAction(a, XVectorBackOpsTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "插入操作");
		XAction_setAction(a, XVectorInsertTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "删除操作");
		XAction_setAction(a, XVectorRemoveTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "取出元素(take)");
		XAction_setAction(a, XVectorTakeTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "替换操作");
		XAction_setAction(a, XVectorReplaceTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "排序与查找");
		XAction_setAction(a, XVectorSortFindTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "比较");
		XAction_setAction(a, XVectorCompareTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "COW与共享");
		XAction_setAction(a, XVectorCowTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "子向量(slice/mid)");
		XAction_setAction(a, XVectorSliceTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "移动与交换");
		XAction_setAction(a, XVectorMoveSwapTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "填充与赋值");
		XAction_setAction(a, XVectorFillTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "安全与类型检查");
		XAction_setAction(a, XVectorSafetyTest);
	}
}
#endif
