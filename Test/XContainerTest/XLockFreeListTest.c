#include"XDataStructTest.h"
#if DEMOTEST
#include"XLockFreeList.h"
#include"XCompare.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XMemory.h"
#include"XThread.h"
#include"XAtomic.h"
#include<time.h>
#include<stdlib.h>
#include<string.h>

/* =============================================================
 *  XLockFreeList 综合测试
 *  覆盖：创建/初始化、头/尾插入、指定位置插入、按值/按位置删除、
 *  Qt 6.8 对齐虚函数（removeAll/removeOne/removeIf/indexOf/lastIndexOf）、
 *  查找/访问、迭代器、原子 pop、清空/交换/拷贝/移动/排序、边界与安全、并发。
 * ============================================================= */

/* -------------------- 通用辅助 -------------------- */

static void XLockFreeListPrintInt(XLockFreeList* li, const char* prefix)
{
	XPrintf("%s", prefix);
	for_each_iterator(li, XLockFreeList, it)
	{
		XPrintf("%d ", XLockFreeListNode_Data(it.node, int));
	}
	XPrintf("(size=%zu)\n", XLockFreeList_size_base(li));
}

static void XLockFreeListForEachInt(void* LPVal, void* args)
{
	(void)args;
	XPrintf("%d ", *(int*)LPVal);
}

static void XLockFreeListPushBackArr(XLockFreeList* li, const int* arr, size_t n)
{
	for (size_t i = 0; i < n; i++)
		XLockFreeList_push_back_base(li, (void*)(arr + i));
}

static XLockFreeList* XLockFreeListMakeInt(const int* arr, size_t n)
{
	XLockFreeList* li = XLockFreeList_Create(int);
	XContainerSetCompare(li, int_compare);
	XLockFreeListPushBackArr(li, arr, n);
	return li;
}

static bool XLockFreeListIsEven(const void* val, void* args)
{
	(void)args;
	return (*(const int*)val) % 2 == 0;
}

static bool XLockFreeListGreaterThan(const void* val, void* userData)
{
	int thr = *(int*)userData;
	return *(const int*)val > thr;
}

/* ======================== 1. 创建与初始化 ======================== */
static void XLockFreeListCreateTest(void)
{
	XPrintf("===== 创建与初始化测试 =====\n");
	{
		XLockFreeList* li = XLockFreeList_Create(int);
		XContainerSetCompare(li, int_compare);
		XPrintf("XLockFreeList_Create(int): size=%zu, capacity=%zu, isEmpty=%s\n",
			XLockFreeList_size_base(li),
			XLockFreeList_capacity_base(li),
			XLockFreeList_isEmpty_base(li) ? "是" : "否");
		XPrintf("  typeSize=%zu (期望:%zu)\n",
			XLockFreeList_typeSize_base(li), sizeof(int));
		XLockFreeList_delete_base(li);
	}
	{
		XLockFreeList* li = XLockFreeList_create(sizeof(double));
		XContainerSetCompare(li, double_compare);
		XPrintf("XLockFreeList_create(sizeof(double)): typeSize=%zu (期望:%zu)\n",
			XLockFreeList_typeSize_base(li), sizeof(double));
		XLockFreeList_delete_base(li);
	}
	XPrintf("\n");
	//XCoreApplication_quit();
}

/* ======================== 2. 插入操作 ======================== */
static void XLockFreeListInsertTest(void)
{
	XPrintf("===== 插入操作测试 =====\n");
	{
		XLockFreeList* li = XLockFreeList_Create(int);
		XContainerSetCompare(li, int_compare);
		for (int i = 1; i <= 5; i++)
			XLockFreeList_push_front_base(li, &i);
		XLockFreeListPrintInt(li, "push_front(1..5): ");
		XPrintf("  期望: 5 4 3 2 1\n");
		XLockFreeList_delete_base(li);
	}
	{
		XLockFreeList* li = XLockFreeList_Create(int);
		XContainerSetCompare(li, int_compare);
		for (int i = 1; i <= 5; i++)
			XLockFreeList_push_back_base(li, &i);
		XLockFreeListPrintInt(li, "push_back(1..5): ");
		XPrintf("  期望: 1 2 3 4 5\n");
		XPrintf("  front=%d (期望:1), back=%d (期望:5)\n",
			XLockFreeList_Front_Base(li, int),
			XLockFreeList_Back_Base(li, int));
		XLockFreeList_delete_base(li);
	}
	{
		XLockFreeList* li = XLockFreeList_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 10, b = 20, c = 30;
		XLockFreeList_Push_Front_Base(li, int, 1);
		XLockFreeList_Push_Back_Base(li, int, 999);
		XLockFreeList_push_front_base(li, &a);
		XLockFreeList_push_back_base(li, &b);
		XLockFreeList_Push_Front_Base(li, int, c);
		XLockFreeListPrintInt(li, "混合插入: ");
		XPrintf("  期望顺序: 30 10 1 999 20\n");
		XLockFreeList_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 5);
		int findVal = 3;
		XLockFreeList_iterator it;
		if (XLockFreeList_find_base(li, &findVal, &it))
		{
			int x = 100;
			XLockFreeList_insert_base(li, it.node, &x);
			XLockFreeListPrintInt(li, "在3前insert(100): ");
			XPrintf("  期望包含: 1 2 100 3 4 5\n");
		}
		XLockFreeList_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 3);
		int addArr[] = { 100, 200, 300 };
		int findVal = 2;
		XLockFreeList_iterator it;
		if (XLockFreeList_find_base(li, &findVal, &it))
		{
			XLockFreeList_insert_array_base(li, it.node, addArr, 3);
			XLockFreeListPrintInt(li, "在2前insert_array: ");
			XPrintf("  期望包含: 1 100 200 300 2 3\n");
		}
		XLockFreeList_delete_base(li);
	}
	XPrintf("\n");
	//XCoreApplication_quit();
}

/* ======================== 3. 删除操作 ======================== */
static void XLockFreeListRemoveTest(void)
{
	XPrintf("===== 删除操作测试 =====\n");
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 5);
		XLockFreeListPrintInt(li, "初始: ");
		XLockFreeList_pop_front_base(li);
		XLockFreeListPrintInt(li, "pop_front: ");
		XLockFreeList_pop_back_base(li);
		XLockFreeListPrintInt(li, "pop_back: ");
		XPrintf("  期望剩: 2 3 4, size=3\n");
		XLockFreeList_delete_base(li);
	}
	{
		int arr[] = { 10, 20, 30, 20, 40 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 5);
		int rm = 20;
		XPrintf("remove(20)首个: %s (期望:是)\n",
			XLockFreeList_remove_base(li, &rm) ? "是" : "否");
		XLockFreeListPrintInt(li, "  删除首个20后: ");
		XLockFreeList_Remove_Base(li, int, 20);
		XLockFreeListPrintInt(li, "  Remove_Base(20)后: ");
		XPrintf("  期望: 10 30 40\n");
		XLockFreeList_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 5);
		XListBase_iterator it, next;
		int findVal = 3;
		if (XLockFreeList_find_base(li, &findVal, &it))
		{
			XLockFreeList_erase_base(li, &it, &next);
			XLockFreeListPrintInt(li, "erase(3): ");
			XPrintf("  期望: 1 2 4 5, next 应指向 4\n");
			if (next.node)
				XPrintf("  next.data=%d (期望:4)\n",
					XLockFreeListNode_Data(next.node, int));
		}
		XLockFreeList_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 5);
		XLockFreeList_clear_base(li);
		XPrintf("clear: isEmpty=%s (期望:是), size=%zu (期望:0)\n",
			XLockFreeList_isEmpty_base(li) ? "是" : "否",
			XLockFreeList_size_base(li));
		XLockFreeList_delete_base(li);
	}
	/* --- Qt 6.8 对齐：removeAll / removeOne / removeIf --- */
	{
		int arr[] = { 1, 2, 3, 2, 4, 2, 5 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 7);
		XLockFreeListPrintInt(li, "初始: ");
		int rm = 2;
		size_t r = XLockFreeList_removeAll_base(li, &rm);
		XPrintf("removeAll(2): 移除%zu个 (期望:3)\n", r);
		XLockFreeListPrintInt(li, "  结果: ");
		XPrintf("  期望: 1 3 4 5\n");
		XLockFreeList_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 2, 4 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 5);
		int rm = 2;
		XPrintf("removeOne(2): %s (期望:是)\n",
			XLockFreeList_removeOne_base(li, &rm) ? "是" : "否");
		XLockFreeListPrintInt(li, "  removeOne(2)后: ");
		XPrintf("  期望: 1 3 2 4\n");
		rm = 999;
		XPrintf("removeOne(999): %s (期望:否)\n",
			XLockFreeList_removeOne_base(li, &rm) ? "是" : "否");
		XLockFreeList_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 8);
		XLockFreeListPrintInt(li, "初始: ");
		size_t r = XLockFreeList_removeIf_base(li, XLockFreeListIsEven, NULL);
		XPrintf("removeIf(偶数): 移除%zu个 (期望:4)\n", r);
		XLockFreeListPrintInt(li, "  结果: ");
		XPrintf("  期望: 1 3 5 7\n");
		XLockFreeList_delete_base(li);
	}
	{
		int arr[] = { 5, 15, 25, 35, 45 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 5);
		int threshold = 20;
		size_t r = XLockFreeList_removeIf_base(li,
			XLockFreeListGreaterThan, &threshold);
		XPrintf("removeIf(>20): 移除%zu个 (期望:3)\n", r);
		XLockFreeListPrintInt(li, "  结果: ");
		XPrintf("  期望: 5 15\n");
		XLockFreeList_delete_base(li);
	}
	XPrintf("\n");
	//XCoreApplication_quit();
}

/* ======================== 4. 查找与访问 ======================== */
static void XLockFreeListAccessTest(void)
{
	XPrintf("===== 查找与访问测试 =====\n");
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 5);
		XPrintf("front=%d (期望:10), back=%d (期望:50)\n",
			*(int*)XLockFreeList_front_base(li),
			*(int*)XLockFreeList_back_base(li));
		XPrintf("Front_Base=%d, Back_Base=%d\n",
			XLockFreeList_Front_Base(li, int),
			XLockFreeList_Back_Base(li, int));
		XLockFreeList_delete_base(li);
	}
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 5);
		XListBase_iterator it;
		int f1 = 30, f2 = 999;
		XPrintf("find(30): %s\n",
			XLockFreeList_find_base(li, &f1, &it) ? "是" : "否");
		if (XLockFreeList_find_base(li, &f1, &it))
			XPrintf("  找到值: %d (期望:30)\n",
				XLockFreeListNode_Data(it.node, int));
		XPrintf("find(999): %s (期望:否)\n",
			XLockFreeList_find_base(li, &f2, &it) ? "是" : "否");
		XPrintf("contains(30): %s (期望:是)\n",
			XLockFreeList_contains(li, &f1) ? "是" : "否");
		XPrintf("contains(999): %s (期望:否)\n",
			XLockFreeList_contains(li, &f2) ? "是" : "否");
		XLockFreeList_delete_base(li);
	}
	/* --- Qt 6.8 对齐：indexOf / lastIndexOf --- */
	{
		int arr[] = { 10, 20, 30, 20, 40, 20, 50 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 7);
		int f = 20, n = 999;
		XListBase_iterator it;
		XPrintf("indexOf(20,from=0): %s\n",
			XLockFreeList_indexOf_base(li, &f, 0, &it) ? "是" : "否");
		if (XLockFreeList_indexOf_base(li, &f, 0, &it))
			XPrintf("  命中值=%d (期望:20，第一次)\n",
				XLockFreeListNode_Data(it.node, int));
		XPrintf("indexOf(20,from=3): %s\n",
			XLockFreeList_indexOf_base(li, &f, 3, &it) ? "是" : "否");
		if (XLockFreeList_indexOf_base(li, &f, 3, &it))
			XPrintf("  命中值=%d (期望:20，from>=3的首个)\n",
				XLockFreeListNode_Data(it.node, int));
		XPrintf("indexOf(999,0): %s (期望:否)\n",
			XLockFreeList_indexOf_base(li, &n, 0, &it) ? "是" : "否");
		XPrintf("lastIndexOf(20,from=(size_t)-1): %s\n",
			XLockFreeList_lastIndexOf_base(li, &f, (size_t)-1, &it) ? "是" : "否");
		if (XLockFreeList_lastIndexOf_base(li, &f, (size_t)-1, &it))
			XPrintf("  命中值=%d (期望:20，最后一个)\n",
				XLockFreeListNode_Data(it.node, int));
		XPrintf("lastIndexOf(20,from=3): %s\n",
			XLockFreeList_lastIndexOf_base(li, &f, 3, &it) ? "是" : "否");
		if (XLockFreeList_lastIndexOf_base(li, &f, 3, &it))
			XPrintf("  命中值=%d (期望:20，索引<=3的最后一个)\n",
				XLockFreeListNode_Data(it.node, int));
		XPrintf("lastIndexOf(999,6): %s (期望:否)\n",
			XLockFreeList_lastIndexOf_base(li, &n, 6, &it) ? "是" : "否");
		XLockFreeList_delete_base(li);
	}
	XPrintf("\n");
	//XCoreApplication_quit();
}

/* ======================== 5. 迭代器 ======================== */
static void XLockFreeListIteratorTest(void)
{
	XPrintf("===== 迭代器测试 =====\n");
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 5);
		XPrintf("iterator_for_each: ");
		XLockFreeList_iterator_for_each(li, XLockFreeListForEachInt, NULL);
		XPrintf("\n");
		XLockFreeList_delete_base(li);
	}
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 5);
		XPrintf("for_each_iterator宏遍历: ");
		for_each_iterator(li, XLockFreeList, it)
		{
			XPrintf("%d ", XLockFreeListNode_Data(it.node, int));
		}
		XPrintf("\n");
		XLockFreeList_delete_base(li);
	}
	{
		XLockFreeList* li = XLockFreeList_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 100;
		XLockFreeList_push_back_base(li, &a);
		XLockFreeList_iterator it  = XLockFreeList_begin(li);
		XLockFreeList_iterator eIt = XLockFreeList_end(li);
		XPrintf("begin: data=%d, isEnd=%s (期望:100/否)\n",
			XLockFreeListNode_Data(it.node, int),
			XLockFreeList_iterator_isEnd(&it) ? "是" : "否");
		XPrintf("end: isEnd=%s (期望:是)\n",
			XLockFreeList_iterator_isEnd(&eIt) ? "是" : "否");
		XPrintf("iterator_equality(begin,end): %s (期望:否)\n",
			XLockFreeList_iterator_equality(&it, &eIt) ? "是" : "否");
		XLockFreeList_iterator_add(li, &it);
		XPrintf("add后: isEnd=%s (期望:是)\n",
			XLockFreeList_iterator_isEnd(&it) ? "是" : "否");
		XLockFreeList_delete_base(li);
	}
	XPrintf("\n");
	//XCoreApplication_quit();
}

/* ======================== 6. 复制/移动/交换/排序 ======================== */
static void XLockFreeListCompareTest(void)
{
	XPrintf("===== 复制/移动/交换/排序测试 =====\n");
	{
		int a1[] = { 3, 2, 1 };
		int a2[] = { 4, 5 };
		XLockFreeList* v1 = XLockFreeListMakeInt(a1, 3);
		XLockFreeList* v2 = XLockFreeListMakeInt(a2, 2);
		XLockFreeListPrintInt(v1, "swap前 v1: ");
		XLockFreeListPrintInt(v2, "swap前 v2: ");
		XLockFreeList_swap_base(v1, v2);
		XLockFreeListPrintInt(v1, "swap后 v1: ");
		XLockFreeListPrintInt(v2, "swap后 v2: ");
		XPrintf("  期望 v1=4 5, v2=3 2 1\n");
		XLockFreeList_delete_base(v1);
		XLockFreeList_delete_base(v2);
	}
	{
		int src[] = { 7, 8, 9 };
		XLockFreeList* s = XLockFreeListMakeInt(src, 3);
		XLockFreeList* d = XLockFreeList_Create(int);
		XContainerSetCompare(d, int_compare);
		XCopy(d, s);
		XLockFreeListPrintInt(s, "copy后 src: ");
		XLockFreeListPrintInt(d, "copy后 dst: ");
		XPrintf("  期望 src/dst 均为 7 8 9\n");
		XLockFreeList_delete_base(s);
		XLockFreeList_delete_base(d);
	}
	{
		int src[] = { 11, 22, 33 };
		XLockFreeList* s = XLockFreeListMakeInt(src, 3);
		XLockFreeList* d = XLockFreeList_Create(int);
		XContainerSetCompare(d, int_compare);
		XMove(d, s);
		XLockFreeListPrintInt(d, "move后 dst: ");
		XPrintf("move后 src.isEmpty=%s (期望:是)\n",
			XLockFreeList_isEmpty_base(s) ? "是" : "否");
		XLockFreeList_delete_base(s);
		XLockFreeList_delete_base(d);
	}
	{
		int arr[] = { 5, 2, 8, 1, 9, 3 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 6);
		XLockFreeListPrintInt(li, "排序前: ");
		XLockFreeList_sort_base(li, XSORT_ASC);
		XLockFreeListPrintInt(li, "升序后: ");
		XLockFreeList_sort_base(li, XSORT_DESC);
		XLockFreeListPrintInt(li, "降序后: ");
		XLockFreeList_delete_base(li);
	}
	XPrintf("\n");
	//XCoreApplication_quit();
}

/* ======================== 7. 原子 pop 与随机压力 ======================== */
static void XLockFreeListPopAtomicTest(void)
{
	XPrintf("===== 原子 pop 测试 =====\n");
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 5);
		int out = 0;
		XPrintf("pop_and_copy_front: ");
		while (XLockFreeList_pop_and_copy_front(li, &out))
			XPrintf("%d ", out);
		XPrintf("(结束, size=%zu 期望:0)\n", XLockFreeList_size_base(li));
		XLockFreeList_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3 };
		XLockFreeList* li = XLockFreeListMakeInt(arr, 3);
		int out = 0;
		XPrintf("pop_and_move_front: ");
		while (XLockFreeList_pop_and_move_front(li, &out))
			XPrintf("%d ", out);
		XPrintf("(结束, size=%zu 期望:0)\n", XLockFreeList_size_base(li));
		XLockFreeList_delete_base(li);
	}
	{
		XLockFreeList* empty = XLockFreeList_Create(int);
		XContainerSetCompare(empty, int_compare);
		int out = 0;
		XPrintf("空表 pop_and_copy_front: %s (期望:否)\n",
			XLockFreeList_pop_and_copy_front(empty, &out) ? "是" : "否");
		XPrintf("空表 pop_and_move_front: %s (期望:否)\n",
			XLockFreeList_pop_and_move_front(empty, &out) ? "是" : "否");
		XLockFreeList_delete_base(empty);
	}
	XPrintf("\n");
	//XCoreApplication_quit();
}

/* ======================== 8. 边界与安全 ======================== */
static void XLockFreeListSafetyTest(void)
{
	XPrintf("===== 边界与安全检查测试 =====\n");
	{
		XLockFreeList* li = XLockFreeList_Create(int);
		XContainerSetCompare(li, int_compare);
		int val = 1;
		XPrintf("空表: pop_front=%s, pop_back=%s\n",
			XLockFreeList_pop_front_base(li) ? "是" : "否",
			XLockFreeList_pop_back_base(li) ? "是" : "否");
		XPrintf("空表: remove=%s, removeOne=%s, removeAll=%zu (期望全否/0)\n",
			XLockFreeList_remove_base(li, &val) ? "是" : "否",
			XLockFreeList_removeOne_base(li, &val) ? "是" : "否",
			XLockFreeList_removeAll_base(li, &val));
		XPrintf("空表: removeIf=%zu (期望:0)\n",
			XLockFreeList_removeIf_base(li, XLockFreeListIsEven, NULL));
		XPrintf("空表: front=%s, back=%s (期望:空/空)\n",
			XLockFreeList_front_base(li) ? "非空" : "空",
			XLockFreeList_back_base(li) ? "非空" : "空");
		XListBase_iterator it;
		XPrintf("空表: find=%s, indexOf=%s, lastIndexOf=%s (期望:否/否/否)\n",
			XLockFreeList_find_base(li, &val, &it) ? "是" : "否",
			XLockFreeList_indexOf_base(li, &val, 0, &it) ? "是" : "否",
			XLockFreeList_lastIndexOf_base(li, &val, 0, &it) ? "是" : "否");
		XLockFreeList_delete_base(li);
	}
	{
		int val = 1;
		XListBase_iterator it;
		XPrintf("NULL: isEmpty=%s, size=%zu\n",
			XLockFreeList_isEmpty_base(NULL) ? "是" : "否",
			XLockFreeList_size_base(NULL));
		XPrintf("NULL: capacity=%zu, typeSize=%zu\n",
			XLockFreeList_capacity_base(NULL),
			XLockFreeList_typeSize_base(NULL));
		XPrintf("NULL: pop_front=%s, pop_back=%s\n",
			XLockFreeList_pop_front_base(NULL) ? "是" : "否",
			XLockFreeList_pop_back_base(NULL) ? "是" : "否");
		XPrintf("NULL: remove=%s, removeOne=%s, removeAll=%zu\n",
			XLockFreeList_remove_base(NULL, &val) ? "是" : "否",
			XLockFreeList_removeOne_base(NULL, &val) ? "是" : "否",
			XLockFreeList_removeAll_base(NULL, &val));
		XPrintf("NULL: removeIf=%zu\n",
			XLockFreeList_removeIf_base(NULL, XLockFreeListIsEven, NULL));
		XPrintf("NULL: find=%s, indexOf=%s, lastIndexOf=%s\n",
			XLockFreeList_find_base(NULL, &val, &it) ? "是" : "否",
			XLockFreeList_indexOf_base(NULL, &val, 0, &it) ? "是" : "否",
			XLockFreeList_lastIndexOf_base(NULL, &val, 0, &it) ? "是" : "否");
		XPrintf("NULL: pop_and_copy_front=%s, pop_and_move_front=%s (期望全否)\n",
			XLockFreeList_pop_and_copy_front(NULL, &val) ? "是" : "否",
			XLockFreeList_pop_and_move_front(NULL, &val) ? "是" : "否");
	}
	{
		/* 单元素反复 push/pop */
		XLockFreeList* li = XLockFreeList_Create(int);
		XContainerSetCompare(li, int_compare);
		for (int i = 0; i < 5; i++) {
			int v = i * 10;
			XLockFreeList_push_back_base(li, &v);
			int out = 0;
			XLockFreeList_pop_and_copy_front(li, &out);
			XPrintf("push %d pop %d, size=%zu (期望:0)\n",
				v, out, XLockFreeList_size_base(li));
		}
		XLockFreeList_delete_base(li);
	}
	XPrintf("\n");
	//XCoreApplication_quit();
}

/* ======================== 9. 排序演示（原有排序压力） ======================== */
static void XLockFreeListSortDemo(void)
{
	XPrintf("===== 排序压力/演示 =====\n");
	XLockFreeList* li = XLockFreeList_Create(int);
	XContainerSetCompare(li, int_compare);
	int size = 20;
	srand((unsigned int)time(NULL));
	for (int i = 0; i < size; i++)
	{
		int num = rand() % 1000;
		XLockFreeList_push_back_base(li, &num);
	}
	XLockFreeListPrintInt(li, "排序前: ");
	clock_t t1 = clock();
	XLockFreeList_sort_base(li, XSORT_ASC);
	clock_t t2 = clock();
	XLockFreeListPrintInt(li, "排序后: ");
	XPrintf("耗时: %ld ticks\n", (long)(t2 - t1));
	XLockFreeList_delete_base(li);
	XPrintf("\n");
	//XCoreApplication_quit();
}

/* ======================== 10. 并发多生产/多消费 ======================== */
#define XLFL_TEST_TOTAL   20000   /* 待消费总元素数 */
#define XLFL_PRODUCERS    4
#define XLFL_CONSUMERS    8

typedef struct XLFLThreadCtx {
	XLockFreeList*   list;
	XAtomic_size_t*  produced;   /* 已生产总数 */
	XAtomic_size_t*  consumed;   /* 已消费总数 */
	XAtomic_size_t*  finished;   /* 生产者结束计数 */
	XAtomic_size_t*  threads_finished; /* 所有工作线程结束计数 */
	int              per_thread; /* 每个生产者的元素数 */
	int              is_producer;
	size_t           threads_total;
} XLFLThreadCtx;

#if XTHREAD_ON
static void XLFLProducer(XThread* thread, XVarList* varlist)
{
	XVarList_args_1(varlist, XLFLThreadCtx*, ctx);
	for (int i = 0; i < ctx->per_thread; i++) {
		int v = i;
		while (!XLockFreeList_push_back_base(ctx->list, &v)) { /* 重试 */ }
		XAtomic_fetch_add_size_t(ctx->produced, 1, XAtomic_MemoryOrder_Relaxed);
	}
	size_t done = XAtomic_fetch_add_size_t(ctx->finished, 1,
		XAtomic_MemoryOrder_Relaxed) + 1;
	XPrintf("[P] tid=%p 完成生产 %d 项 (完成生产者=%zu/%d)\n",
		XThread_currentThreadId(), ctx->per_thread, done, XLFL_PRODUCERS);
	size_t fin = XAtomic_fetch_add_size_t(ctx->threads_finished, 1,
		XAtomic_MemoryOrder_AcqRel) + 1;
	XThread_deleteLater(thread);
	/*if (fin == ctx->threads_total)
		XCoreApplication_quit();*/
}

static void XLFLConsumer(XThread* thread, XVarList* varlist)
{
	XVarList_args_1(varlist, XLFLThreadCtx*, ctx);
	int local = 0;
	int value = 0;
	int idle = 0;
	size_t target = (size_t)XLFL_PRODUCERS * (size_t)ctx->per_thread;
	for (;;) {
		if (XLockFreeList_pop_and_copy_front(ctx->list, &value)) {
			local++;
			idle = 0;
			size_t total = XAtomic_fetch_add_size_t(ctx->consumed, 1,
				XAtomic_MemoryOrder_Relaxed) + 1;
			if (total >= target) break;
		} else {
			/* 生产者全部结束、且消费计数已追上生产计数 -> 退出
			 * 用外部原子计数比 list 的 isEmpty/size 更可靠，
			 * 可绕过无锁链表内部 size 与真实链表长度短暂不一致的情况。 */
			size_t fin = XAtomic_load_size_t(ctx->finished,
				XAtomic_MemoryOrder_Relaxed);
			if (fin >= XLFL_PRODUCERS) {
				size_t prod = XAtomic_load_size_t(ctx->produced,
					XAtomic_MemoryOrder_Relaxed);
				size_t cons = XAtomic_load_size_t(ctx->consumed,
					XAtomic_MemoryOrder_Relaxed);
				if (cons >= prod)
					break;
				/* 空转过多 -> 认定链表算法已丢失残余节点，退出防止死循环 */
				if (++idle > 200000) {
					XPrintf("[C] tid=%p 检测到 %d 次空转 (cons=%zu/prod=%zu size=%zu)，退出\n",
						XThread_currentThreadId(), idle, cons, prod,
						XLockFreeList_size_base(ctx->list));
					break;
				}
			}
		}
	}
	XPrintf("[C] tid=%p 消费=%d 累计=%zu 剩余size=%zu\n",
		XThread_currentThreadId(), local,
		XAtomic_load_size_t(ctx->consumed, XAtomic_MemoryOrder_Relaxed),
		XLockFreeList_size_base(ctx->list));
	size_t fin = XAtomic_fetch_add_size_t(ctx->threads_finished, 1,
		XAtomic_MemoryOrder_AcqRel) + 1;
	XThread_deleteLater(thread);
	/*if (fin == ctx->threads_total)
		XCoreApplication_quit();*/
}

static void XLockFreeListConcurrentTest(void)
{
	XPrintf("===== 并发多生产/多消费测试 =====\n");
	XLockFreeList* list = XLockFreeList_Create(int);
	XContainerSetCompare(list, int_compare);

	int per = XLFL_TEST_TOTAL / XLFL_PRODUCERS;
	XAtomic_size_t produced = { 0 };
	XAtomic_size_t consumed = { 0 };
	XAtomic_size_t finished = { 0 };
	XAtomic_size_t threads_finished = { 0 };

	XLFLThreadCtx pctx = { list, &produced, &consumed, &finished,
		&threads_finished, per, 1,
		(size_t)(XLFL_PRODUCERS + XLFL_CONSUMERS) };
	XLFLThreadCtx cctx = { list, &produced, &consumed, &finished,
		&threads_finished, per, 0,
		(size_t)(XLFL_PRODUCERS + XLFL_CONSUMERS) };
	XLFLThreadCtx* pctxPtr = &pctx;
	XLFLThreadCtx* cctxPtr = &cctx;

	for (int i = 0; i < XLFL_PRODUCERS; i++) {
		XThread* t = XThread_create_func(XLFLProducer,
			XVarList_Create(XVar(XLFLThreadCtx*, pctxPtr)));
		XThread_start(t);
	}
	for (int i = 0; i < XLFL_CONSUMERS; i++) {
		XThread* t = XThread_create_func(XLFLConsumer,
			XVarList_Create(XVar(XLFLThreadCtx*, cctxPtr)));
		XThread_start(t);
	}
	XCoreApplication_exec();

	XPrintf("并发结束: produced=%zu consumed=%zu remain_size=%zu (期望:%d/%d/0)\n",
		XAtomic_load_size_t(&produced, XAtomic_MemoryOrder_Relaxed),
		XAtomic_load_size_t(&consumed, XAtomic_MemoryOrder_Relaxed),
		XLockFreeList_size_base(list),
		XLFL_PRODUCERS * per, XLFL_PRODUCERS * per);
	XLockFreeList_delete_base(list);
	XPrintf("\n");
	//XCoreApplication_quit();
}
#endif // XTHREAD_ON

/* ======================== 全部测试汇总 ======================== */
static void XLockFreeListAllTest(void)
{
	XPrintf("========== XLockFreeList 全部测试开始 ==========\n\n");
	XLockFreeListCreateTest();
	XLockFreeListInsertTest();
	XLockFreeListRemoveTest();
	XLockFreeListAccessTest();
	XLockFreeListIteratorTest();
	XLockFreeListCompareTest();
	XLockFreeListPopAtomicTest();
	XLockFreeListSafetyTest();
	XLockFreeListSortDemo();
	XPrintf("\n========== XLockFreeList 全部测试结束(不含并发) ==========\n");
	//XCoreApplication_quit();
}

void XTestMenu_XLockFreeListTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XLockFreeList(单向无锁链表)");
	XTestMenu_addMenu(root, menu);
	{
		XAction* a = XTestMenu_addAction(menu, "【全部测试(不含并发)】");
		XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLockFreeListAllTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "创建与初始化");
		XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLockFreeListCreateTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "插入操作");
		XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLockFreeListInsertTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "删除操作(含removeAll/One/If)");
		XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLockFreeListRemoveTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "查找与访问(含indexOf/lastIndexOf)");
		XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLockFreeListAccessTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "迭代器");
		XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLockFreeListIteratorTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "复制/移动/交换/排序");
		XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLockFreeListCompareTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "原子pop(pop_and_copy/move_front)");
		XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLockFreeListPopAtomicTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "边界与安全检查");
		XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLockFreeListSafetyTest);
	}
	{
		XAction* a = XTestMenu_addAction(menu, "排序压力/演示");
		XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLockFreeListSortDemo);
	}
	{
#if XTHREAD_ON
		XAction* a = XTestMenu_addAction(menu, "并发多生产/多消费");
		XTestMenu_setActionFunction(a, (XTestMenuActionFunc)XLockFreeListConcurrentTest);
#endif // XTHREAD_ON
	}
}
#endif
