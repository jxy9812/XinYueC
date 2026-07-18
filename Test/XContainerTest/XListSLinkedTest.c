#include"XDataStructTest.h"
#if DEMOTEST
#include"XListSLinked.h"
#include"XCompare.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XMemory.h"

static void XListSLinkedPrintInt(XListSLinked* li, const char* prefix)
{
	XPrintf("%s", prefix);
	for_each_iterator(li, XListSLinked, it)
	{
		XPrintf("%d ", XListSNode_Data(it.node, int));
	}
	XPrintf("\n");
}

static void XListSLinkedForEachInt(void* LPVal, void* args)
{
	(void)args;
	XPrintf("%d ", *(int*)LPVal);
}

static void XListSLinkedPushBackArr(XListSLinked* li, const int* arr, size_t n)
{
	for (size_t i = 0; i < n; i++)
		XListSLinked_push_back_base(li, (void*)(arr + i));
}

static XListSLinked* XListSLinkedMakeInt(const int* arr, size_t n)
{
	XListSLinked* li = XListSLinked_Create(int);
	XContainerSetCompare(li, int_compare);
	XListSLinkedPushBackArr(li, arr, n);
	return li;
}

static bool XListSLinkedRemoveEven(const void* val, void* args)
{
	(void)args;
	return (*(const int*)val) % 2 == 0;
}

// ======================== 1. 创建与初始化测试 ========================
static void XListSLinkedCreateTest(void)
{
	XPrintf("===== 创建与初始化测试 =====\n");
	{
		XListSLinked* li = XListSLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		XPrintf("XListSLinked_Create(int): size=%zu, capacity=%zu, isEmpty=%s\n",
			XListSLinked_size_base(li), XListSLinked_capacity_base(li),
			XListSLinked_isEmpty_base(li) ? "是" : "否");
		XPrintf("  typeSize=%zu\n", XListSLinked_typeSize_base(li));
		XListSLinked_delete_base(li);
	}
	{
		XListSLinked* li = XListSLinked_create_ex(sizeof(int), false);
		XContainerSetCompare(li, int_compare);
		XPrintf("create_ex(int,cow=false): size=%zu, typeSize=%zu\n",
			XListSLinked_size_base(li), XListSLinked_typeSize_base(li));
		XListSLinked_delete_base(li);
	}
	{
		XListSLinked li;
		XListSLinked_init(&li, sizeof(int), true);
		XContainerSetCompare(&li, int_compare);
		XPrintf("init: size=%zu, isEmpty=%s\n",
			XListSLinked_size_base(&li), XListSLinked_isEmpty_base(&li) ? "是" : "否");
		int val = 42;
		XListSLinked_push_back_base(&li, &val);
		XPrintf("  插入42后: size=%zu\n", XListSLinked_size_base(&li));
		XListSLinked_deinit_base(&li);
	}
	{
		int arr[] = { 1, 2, 3 };
		XListSLinked* src = XListSLinkedMakeInt(arr, 3);
		XListSLinked* copy = XListSLinked_create_ex(sizeof(int), true);
		XContainerSetCompare(copy, int_compare);
		XListSLinked_copy_base(copy, src);
		XPrintf("copy_base: copy.size=%zu, src.size=%zu\n",
			XListSLinked_size_base(copy), XListSLinked_size_base(src));
		XListSLinkedPrintInt(copy, "  copy: ");
		int val = 99;
		XListSLinked_push_back_base(src, &val);
		XPrintf("  修改src后: copy.size=%zu, src.size=%zu\n",
			XListSLinked_size_base(copy), XListSLinked_size_base(src));
		XListSLinked_delete_base(src);
		XListSLinked_delete_base(copy);
	}
	{
		int arr[] = { 10, 20, 30 };
		XListSLinked* src = XListSLinkedMakeInt(arr, 3);
		XListSLinked* moved = XListSLinked_Create(int);
		XContainerSetCompare(moved, int_compare);
		XListSLinked_move_base(moved, src);
		XPrintf("move_base: moved.size=%zu, src.isEmpty=%s\n",
			XListSLinked_size_base(moved),
			XListSLinked_isEmpty_base(src) ? "是" : "否");
		XListSLinkedPrintInt(moved, "  moved: ");
		XListSLinked_delete_base(moved);
		XListSLinked_delete_base(src);
	}
	{
		XPrintf("maxSize=%zu\n", XListSLinked_maxSize_base());
	}
	XPrintf("\n");
}

// ======================== 2. 插入操作测试 ========================
static void XListSLinkedInsertTest(void)
{
	XPrintf("===== 插入操作测试 =====\n");

	/* --- push_front / prepend --- */
	{
		XListSLinked* li = XListSLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 10, b = 20, c = 30;
		XListSLinked_push_front_base(li, &a);
		XListSLinked_push_front_base(li, &b);
		XListSLinked_Push_Front_Base(li, int, c);
		XListSLinkedPrintInt(li, "push_front 10,20,30: ");
		XPrintf("  size=%zu (期望:3)\n", XListSLinked_size_base(li));
		XListSLinked_delete_base(li);
	}
	{
		XListSLinked* li = XListSLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int* p = XMalloc_System(sizeof(int)); *p = 99;
		XListSLinked_push_front_move_base(li, p);
		XListSLinkedPrintInt(li, "push_front_move(99): ");
		XListSLinked_delete_base(li);
	}
	{
		XListSLinked* li = XListSLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int v = 5;
		XListSLinked_prepend_base(li, &v);
		int v2 = 3;
		XListSLinked_prepend_base(li, &v2);
		XListSLinkedPrintInt(li, "prepend 5,3: ");
		XListSLinked_delete_base(li);
	}

	/* --- push_back / append --- */
	{
		XListSLinked* li = XListSLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 1, b = 2, c = 3;
		XListSLinked_push_back_base(li, &a);
		XListSLinked_push_back_base(li, &b);
		XListSLinked_Push_Back_Base(li, int, c);
		XListSLinkedPrintInt(li, "push_back 1,2,3: ");
		XPrintf("  size=%zu (期望:3)\n", XListSLinked_size_base(li));
		XListSLinked_delete_base(li);
	}
	{
		XListSLinked* li = XListSLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int* p = XMalloc_System(sizeof(int)); *p = 88;
		XListSLinked_push_back_move_base(li, p);
		XListSLinkedPrintInt(li, "push_back_move(88): ");
		XListSLinked_delete_base(li);
	}
	{
		XListSLinked* li = XListSLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 100, b = 200;
		XListSLinked_append_base(li, &a);
		XListSLinked_append_base(li, &b);
		XListSLinkedPrintInt(li, "append 100,200: ");
		XListSLinked_delete_base(li);
	}

	/* --- insert_base --- */
	{
		int arr[] = { 1, 5, 9 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 3);
		XListSLinkedPrintInt(li, "初始: ");
		XListBase_iterator it;
		int findVal = 5;
		XListSLinked_find_base(li, &findVal, &it);
		int insertVal = 3;
		XListSLinked_insert_base(li, it.node, &insertVal);
		XListSLinkedPrintInt(li, "insert(3)在5之前: ");
		XListSLinked_delete_base(li);
	}
	{
		XListSLinked* li = XListSLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 10;
		XListSLinked_push_back_base(li, &a);
		int* p = XMalloc_System(sizeof(int)); *p = 999;
		XListBase_iterator it;
		int findVal = 10;
		XListSLinked_find_base(li, &findVal, &it);
		XListSLinked_insert_move_base(li, it.node, p);
		XListSLinkedPrintInt(li, "insert_move(999)在10之前: ");
		XListSLinked_delete_base(li);
	}

	/* --- insert_array --- */
	{
		int arr[] = { 10, 50 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 2);
		XListSLinkedPrintInt(li, "初始: ");
		int ins[] = { 20, 30, 40 };
		XListBase_iterator it;
		int findVal = 50;
		XListSLinked_find_base(li, &findVal, &it);
		size_t n = XListSLinked_insert_array_base(li, it.node, ins, 3);
		XPrintf("  insert_array插入%zu个\n", n);
		XListSLinkedPrintInt(li, "  结果: ");
		XListSLinked_delete_base(li);
	}

	/* --- push_front_node / push_back_node --- */
	{
		XListSLinked* li = XListSLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 1, b = 2;
		XListSLinked_push_back_base(li, &a);
		XListSLinked_push_back_base(li, &b);
		XListSNode* node = (XListSNode*)XListSNode_Create(XMalloc_System, int);
		XListSNode_Data(node, int) = 99;
		XListSLinked_push_front_node_base(li, (XListBaseNode*)node);
		XListSLinkedPrintInt(li, "push_front_node(99): ");
		XListSLinked_delete_base(li);
	}
	{
		XListSLinked* li = XListSLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 1;
		XListSLinked_push_back_base(li, &a);
		XListSNode* node = (XListSNode*)XListSNode_Create(XMalloc_System, int);
		XListSNode_Data(node, int) = 88;
		XListSLinked_push_back_node_base(li, (XListBaseNode*)node);
		XListSLinkedPrintInt(li, "push_back_node(88): ");
		XListSLinked_delete_base(li);
	}

	XPrintf("\n");
}


// ======================== 3. 删除操作测试 ========================
static void XListSLinkedRemoveTest(void)
{
	XPrintf("===== 删除操作测试 =====\n");
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 5);
		XListSLinkedPrintInt(li, "初始: ");
		XListSLinked_pop_front_base(li);
		XListSLinkedPrintInt(li, "pop_front后: ");
		XListSLinked_pop_back_base(li);
		XListSLinkedPrintInt(li, "pop_back后: ");
		XPrintf("  size=%zu (期望:3)\n", XListSLinked_size_base(li));
		XListSLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 5);
		XListSLinked_removeFirst_base(li);
		XListSLinkedPrintInt(li, "removeFirst: ");
		XListSLinked_removeLast_base(li);
		XListSLinkedPrintInt(li, "removeLast: ");
		XListSLinked_delete_base(li);
	}
	{
		int arr[] = { 10, 20, 30, 20, 40 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 5);
		int rm = 20;
		XPrintf("remove(20): %s\n",
			XListSLinked_remove_base(li, &rm) ? "是" : "否");
		XListSLinkedPrintInt(li, "  删除首个20后: ");
		XListSLinked_Remove_Base(li, int, 20);
		XListSLinkedPrintInt(li, "  Remove_Base(20)后: ");
		XListSLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 5);
		XListSLinkedPrintInt(li, "初始: ");
		XListBase_iterator it;
		int findVal = 3;
		XListSLinked_find_base(li, &findVal, &it);
		XListBase_iterator next;
		XListSLinked_erase_base(li, &it, &next);
		XListSLinkedPrintInt(li, "erase(3): ");
		XListSLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 5);
		XListSLinked_clear_base(li);
		XPrintf("clear: isEmpty=%s, size=%zu\n",
			XListSLinked_isEmpty_base(li) ? "是" : "否",
			XListSLinked_size_base(li));
		XListSLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 2, 4, 2, 5 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 7);
		XListSLinkedPrintInt(li, "初始: ");
		int rm = 2;
		size_t r = XListSLinked_removeAll_base(li, &rm);
		XPrintf("removeAll(2): 移除%zu个 (期望:3)\n", r);
		XListSLinkedPrintInt(li, "  结果: ");
		XListSLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 2, 4 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 5);
		int rm = 2;
		XPrintf("removeOne(2): %s (期望:是)\n",
			XListSLinked_removeOne_base(li, &rm) ? "是" : "否");
		XListSLinkedPrintInt(li, "  removeOne(2)后: ");
		rm = 999;
		XPrintf("removeOne(999): %s (期望:否)\n",
			XListSLinked_removeOne_base(li, &rm) ? "是" : "否");
		XListSLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 8);
		XListSLinkedPrintInt(li, "初始: ");
		size_t r = XListSLinked_removeIf_base(li, XListSLinkedRemoveEven, NULL);
		XPrintf("removeIf(偶数): 移除%zu个 (期望:4)\n", r);
		XListSLinkedPrintInt(li, "  结果: ");
		XListSLinked_delete_base(li);
	}
	XPrintf("\n");
}

// ======================== 4. 查找/访问测试 ========================
static void XListSLinkedAccessTest(void)
{
	XPrintf("===== 查找与访问测试 =====\n");
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 5);
		XPrintf("front=%d, back=%d\n",
			*(int*)XListSLinked_front_base(li),
			*(int*)XListSLinked_back_base(li));
		XPrintf("Front_Base=%d, Back_Base=%d\n",
			XListSLinked_Front_Base(li, int),
			XListSLinked_Back_Base(li, int));
		XListSLinked_delete_base(li);
	}
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 5);
		XListBase_iterator it;
		int f1 = 30, f2 = 999;
		XPrintf("find(30): %s\n",
			XListSLinked_find_base(li, &f1, &it) ? "是" : "否");
		if (XListSLinked_find_base(li, &f1, &it))
			XPrintf("  找到值: %d (期望:30)\n", XListSNode_Data(it.node, int));
		XPrintf("find(999): %s (期望:否)\n",
			XListSLinked_find_base(li, &f2, &it) ? "是" : "否");
		XListSLinked_delete_base(li);
	}
	{
        int __v20 = 20; int __v999 = 999;
		int arr[] = { 10, 20, 30, 20, 40, 20, 50 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 7);
		int f = 20, n = 999;
		XPrintf("Contains_Base(20): %s (期望:是)\n",
			XListSLinked_Contains_Base(li, &__v20) ? "是" : "否");
		XPrintf("Contains_Base(999): %s (期望:否)\n",
			XListSLinked_Contains_Base(li, &__v999) ? "是" : "否");
		XListBase_iterator it;
		XPrintf("indexOf(20,0): %s\n",
			XListSLinked_indexOf_base(li, &f, 0, &it) ? "是" : "否");
		if (XListSLinked_indexOf_base(li, &f, 0, &it))
			XPrintf("  位置: node.data=%d (期望:20)\n", XListSNode_Data(it.node, int));
		XPrintf("indexOf(20,3): %s\n",
			XListSLinked_indexOf_base(li, &f, 3, &it) ? "是" : "否");
		if (XListSLinked_indexOf_base(li, &f, 3, &it))
			XPrintf("  位置: node.data=%d (期望:20)\n", XListSNode_Data(it.node, int));
		XPrintf("indexOf(999,0): %s (期望:否)\n",
			XListSLinked_indexOf_base(li, &n, 0, &it) ? "是" : "否");
		XPrintf("lastIndexOf(20,6): %s\n",
			XListSLinked_lastIndexOf_base(li, &f, 6, &it) ? "是" : "否");
		if (XListSLinked_lastIndexOf_base(li, &f, 6, &it))
			XPrintf("  位置: node.data=%d (期望:20)\n", XListSNode_Data(it.node, int));
		XPrintf("lastIndexOf(999,6): %s (期望:否)\n",
			XListSLinked_lastIndexOf_base(li, &n, 6, &it) ? "是" : "否");
		XListSLinked_delete_base(li);
	}
	{
        int __v1 = 1; int __v5 = 5; int __v999 = 999;
		int arr[] = { 1, 2, 3, 4, 5 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 5);
		XPrintf("StartsWith_Base(1): %s (期望:是)\n",
			XListSLinked_StartsWith_Base(li, &__v1) ? "是" : "否");
		XPrintf("StartsWith_Base(999): %s (期望:否)\n",
			XListSLinked_StartsWith_Base(li, &__v999) ? "是" : "否");
		XPrintf("EndsWith_Base(5): %s (期望:是)\n",
			XListSLinked_EndsWith_Base(li, &__v5) ? "是" : "否");
		XPrintf("EndsWith_Base(999): %s (期望:否)\n",
			XListSLinked_EndsWith_Base(li, &__v999) ? "是" : "否");
		XListSLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 3);
		XPrintf("size=%zu, count=%zu, length=%zu\n",
			XListSLinked_size_base(li),
			XListSLinked_count_base(li),
			XListSLinked_length_base(li));
		XPrintf("capacity=%zu\n", XListSLinked_capacity_base(li));
		XListSLinked_delete_base(li);
	}
	{
		XListSLinked* empty = XListSLinked_Create(int);
		XContainerSetCompare(empty, int_compare);
		XPrintf("空链表: front=%s, back=%s\n",
			XListSLinked_front_base(empty) ? "非空" : "空",
			XListSLinked_back_base(empty) ? "非空" : "空");
		XListSLinked_delete_base(empty);
	}
	XPrintf("\n");
}


// ======================== 5. 取出元素测试 ========================
static void XListSLinkedTakeTest(void)
{
	XPrintf("===== 取出元素测试(takeFirst/takeLast) =====\n");
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 5);
		XListSLinkedPrintInt(li, "初始: ");
		int* p = (int*)XListSLinked_takeFirst_base(li);
		if (p) { XPrintf("takeFirst=%d (期望:10)\n", *p); XFree_System(p); }
		XListSLinkedPrintInt(li, "  takeFirst后: ");
		p = (int*)XListSLinked_takeLast_base(li);
		if (p) { XPrintf("takeLast=%d (期望:50)\n", *p); XFree_System(p); }
		XListSLinkedPrintInt(li, "  takeLast后: ");
		XListSLinked_delete_base(li);
	}
	{
		XListSLinked* empty = XListSLinked_Create(int);
		XContainerSetCompare(empty, int_compare);
		XPrintf("空: takeFirst=%s, takeLast=%s\n",
			XListSLinked_takeFirst_base(empty) ? "非空" : "空",
			XListSLinked_takeLast_base(empty) ? "非空" : "空");
		XListSLinked_delete_base(empty);
	}
	XPrintf("\n");
}

// ======================== 6. 比较/复制/交换/排序测试 ========================
static void XListSLinkedCompareTest(void)
{
	XPrintf("===== 比较/复制/交换/排序测试 =====\n");
	{
		int a1[] = { 1, 2, 3, 4, 5 };
		int a2[] = { 1, 2, 3, 4, 5 };
		int a3[] = { 1, 2, 3 };
		XListSLinked* v1 = XListSLinkedMakeInt(a1, 5);
		XListSLinked* v2 = XListSLinkedMakeInt(a2, 5);
		XListSLinked* v3 = XListSLinkedMakeInt(a3, 3);
		XPrintf("v1==v2(equals): %s (期望:是)\n",
			XListSLinked_equals_base(v1, v2) ? "是" : "否");
		XPrintf("v1==v3(equals): %s (期望:否)\n",
			XListSLinked_equals_base(v1, v3) ? "是" : "否");
		XListSLinked_delete_base(v1);
		XListSLinked_delete_base(v2);
		XListSLinked_delete_base(v3);
	}
	{
		int a1[] = { 3, 2, 1 };
		int a2[] = { 4, 5 };
		XListSLinked* v1 = XListSLinkedMakeInt(a1, 3);
		XListSLinked* v2 = XListSLinkedMakeInt(a2, 2);
		XListSLinked_swap_base(v1, v2);
		XListSLinkedPrintInt(v1, "swap后v1: ");
		XListSLinkedPrintInt(v2, "swap后v2: ");
		XPrintf("v1.size=%zu, v2.size=%zu\n",
			XListSLinked_size_base(v1), XListSLinked_size_base(v2));
		XListSLinked_delete_base(v1);
		XListSLinked_delete_base(v2);
	}
	{
		int arr[] = { 5, 2, 8, 1, 9, 3 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 6);
		XListSLinkedPrintInt(li, "排序前: ");
		XListSLinked_sort_base(li, XSORT_ASC);
		XListSLinkedPrintInt(li, "升序后: ");
		XListSLinked_sort_base(li, XSORT_DESC);
		XListSLinkedPrintInt(li, "降序后: ");
		XListSLinked_delete_base(li);
	}
	XPrintf("\n");
}

// ======================== 7. 迭代器测试 ========================
static void XListSLinkedIteratorTest(void)
{
	XPrintf("===== 迭代器测试 =====\n");
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 5);
		XPrintf("正向遍历(iterator_for_each): ");
		XListSLinked_iterator_for_each(li, XListSLinkedForEachInt, NULL);
		XPrintf("\n");
		XListSLinked_delete_base(li);
	}
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 5);
		XPrintf("for_each_iterator宏遍历: ");
		for_each_iterator(li, XListSLinked, it)
		{
			XPrintf("%d ", XListSNode_Data(it.node, int));
		}
		XPrintf("\n");
		XListSLinked_delete_base(li);
	}
	{
		XListSLinked* li = XListSLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 100;
		XListSLinked_push_back_base(li, &a);
		XListSLinked_iterator it = XListSLinked_begin(li);
		XListSLinked_iterator endIt = XListSLinked_end(li);
		XPrintf("begin: data=%d, isEnd=%s\n",
			XListSNode_Data(it.node, int),
			XListSLinked_iterator_isEnd(&it) ? "是" : "否");
		XPrintf("end: isEnd=%s\n",
			XListSLinked_iterator_isEnd(&endIt) ? "是" : "否");
		XListSLinked_iterator_add(li, &it);
		XPrintf("add后: isEnd=%s\n",
			XListSLinked_iterator_isEnd(&it) ? "是" : "否");
		XPrintf("iterator_data=%d\n", *(int*)XListSLinked_iterator_data(&it));
		XListSLinked_delete_base(li);
	}
	XPrintf("\n");
}


// ======================== 8. 边界与安全检查测试 ========================
static void XListSLinkedSafetyTest(void)
{
	XPrintf("===== 边界与安全检查测试 =====\n");
	{
		XListSLinked* li = XListSLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int val = 1;
		XPrintf("空链表: pop_front=%s, pop_back=%s\n",
			XListSLinked_pop_front_base(li) ? "是" : "否",
			XListSLinked_pop_back_base(li) ? "是" : "否");
		XPrintf("空链表: removeFirst=%s, removeLast=%s\n",
			XListSLinked_removeFirst_base(li) ? "是" : "否",
			XListSLinked_removeLast_base(li) ? "是" : "否");
		XPrintf("空链表: remove=%s, removeOne=%s, removeAll=%zu\n",
			XListSLinked_remove_base(li, &val) ? "是" : "否",
			XListSLinked_removeOne_base(li, &val) ? "是" : "否",
			XListSLinked_removeAll_base(li, &val));
		XPrintf("空链表: removeIf=%zu\n",
			XListSLinked_removeIf_base(li, XListSLinkedRemoveEven, NULL));
		XListSLinked_delete_base(li);
	}
	{
        int __v1 = 1;
		XPrintf("NULL: isEmpty=%s, size=%zu\n",
			XListSLinked_isEmpty_base(NULL) ? "是" : "否",
			XListSLinked_size_base(NULL));
		XPrintf("NULL: count=%zu, length=%zu\n",
			XListSLinked_count_base(NULL),
			XListSLinked_length_base(NULL));
		XPrintf("NULL: front=%s, back=%s\n",
			XListSLinked_front_base(NULL) ? "非空" : "空",
			XListSLinked_back_base(NULL) ? "非空" : "空");
		XPrintf("NULL: capacity=%zu\n", XListSLinked_capacity_base(NULL));
		XPrintf("NULL: typeSize=%zu\n", XListSLinked_typeSize_base(NULL));
		XPrintf("NULL: pop_front=%s, pop_back=%s\n",
			XListSLinked_pop_front_base(NULL) ? "是" : "否",
			XListSLinked_pop_back_base(NULL) ? "是" : "否");
		int val = 1;
		XPrintf("NULL: remove=%s, removeOne=%s, removeAll=%zu\n",
			XListSLinked_remove_base(NULL, &val) ? "是" : "否",
			XListSLinked_removeOne_base(NULL, &val) ? "是" : "否",
			XListSLinked_removeAll_base(NULL, &val));
		XPrintf("NULL: removeIf=%zu\n",
			XListSLinked_removeIf_base(NULL, XListSLinkedRemoveEven, NULL));
		XPrintf("NULL: takeFirst=%s, takeLast=%s\n",
			XListSLinked_takeFirst_base(NULL) ? "非空" : "空",
			XListSLinked_takeLast_base(NULL) ? "非空" : "空");
		XListBase_iterator it;
		XPrintf("NULL: find=%s, indexOf=%s, lastIndexOf=%s\n",
			XListSLinked_find_base(NULL, &val, &it) ? "是" : "否",
			XListSLinked_indexOf_base(NULL, &val, 0, &it) ? "是" : "否",
			XListSLinked_lastIndexOf_base(NULL, &val, 0, &it) ? "是" : "否");
		XPrintf("NULL: Contains=%s, StartsWith=%s, EndsWith=%s\n",
			XListSLinked_Contains_Base(NULL, &__v1) ? "是" : "否",
			XListSLinked_StartsWith_Base(NULL, &__v1) ? "是" : "否",
			XListSLinked_EndsWith_Base(NULL, &__v1) ? "是" : "否");
		XPrintf("NULL: removeFirst=%s, removeLast=%s\n",
			XListSLinked_removeFirst_base(NULL) ? "是" : "否",
			XListSLinked_removeLast_base(NULL) ? "是" : "否");
		XPrintf("NULL: equals(NULL,NULL)=%s\n",
			XListSLinked_equals_base(NULL, NULL) ? "是" : "否");
		XPrintf("NULL: maxSize=%zu\n", XListSLinked_maxSize_base());
	}
	{
		int arr[] = { 1, 2, 3 };
		XListSLinked* li = XListSLinkedMakeInt(arr, 3);
		XListSLinked* other = XListSLinked_create(sizeof(double));
		XContainerSetCompare(other, double_compare);
		double dv = 3.14;
		XListSLinked_push_back_base(other, &dv);
		XListSLinked_copy_base(li, other);
		XPrintf("类型不一致: copy后li.size未变=%s, push_back类型不一致=%s（均应否）\n",
			XListSLinked_size_base(li) == 3 ? "是" : "否",
			XListSLinked_push_back_base(li, &dv) ? "是" : "否");
		XListSLinked_delete_base(li);
		XListSLinked_delete_base(other);
	}
	XPrintf("\n");
}

// ======================== 全部测试汇总 ========================
static void XListSLinkedAllTest(void)
{
	XPrintf("========== XListSLinked 全部测试开始 ==========\n\n");
	XListSLinkedCreateTest();
	XListSLinkedInsertTest();
	XListSLinkedRemoveTest();
	XListSLinkedAccessTest();
	XListSLinkedTakeTest();
	XListSLinkedCompareTest();
	XListSLinkedIteratorTest();
	XListSLinkedSafetyTest();
	XPrintf("\n========== XListSLinked 全部测试结束 ==========\n");
	XCoreApplication_quit();
}

void XMenu_XListSLinkedTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XListSLinked(单向链表)");
	XMenu_addMenu(root, menu);
	{
		XAction* a = XMenu_addAction(menu, "【全部测试】");
		XAction_setAction(a, (Action)XListSLinkedAllTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "创建与初始化");
		XAction_setAction(a, (Action)XListSLinkedCreateTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "插入操作");
		XAction_setAction(a, (Action)XListSLinkedInsertTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "删除操作");
		XAction_setAction(a, (Action)XListSLinkedRemoveTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "查找与访问");
		XAction_setAction(a, (Action)XListSLinkedAccessTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "取出元素(take)");
		XAction_setAction(a, (Action)XListSLinkedTakeTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "比较/复制/交换/排序");
		XAction_setAction(a, (Action)XListSLinkedCompareTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "迭代器");
		XAction_setAction(a, (Action)XListSLinkedIteratorTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "边界与安全检查");
		XAction_setAction(a, (Action)XListSLinkedSafetyTest);
	}
}
#endif
