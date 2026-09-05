#include"XDataStructTest.h"
#if DEMOTEST
#include"XListDLinked.h"
#include"XCompare.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XMemory.h"

static void XListDLinkedPrintInt(XListDLinked* li, const char* prefix)
{
	XPrintf("%s", prefix);
	for_each_iterator(li, XListDLinked, it)
	{
		XPrintf("%d ", XListDNode_Data(it.node, int));
	}
	XPrintf("\n");
}

static void XListDLinkedForEachInt(void* LPVal, void* args)
{
	(void)args;
	XPrintf("%d ", *(int*)LPVal);
}

static void XListDLinkedPushBackArr(XListDLinked* li, const int* arr, size_t n)
{
	for (size_t i = 0; i < n; i++)
		XListDLinked_push_back_base(li, (void*)(arr + i));
}

static XListDLinked* XListDLinkedMakeInt(const int* arr, size_t n)
{
	XListDLinked* li = XListDLinked_Create(int);
	XContainerSetCompare(li, int_compare);
	XListDLinkedPushBackArr(li, arr, n);
	return li;
}

static bool XListDLinkedRemoveEven(const void* val, void* args)
{
	(void)args;
	return (*(const int*)val) % 2 == 0;
}

// ======================== 1. 创建与初始化测试 ========================
static void XListDLinkedCreateTest(void)
{
	XPrintf("===== 创建与初始化测试 =====\n");
	{
		XListDLinked* li = XListDLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		XPrintf("XListDLinked_Create(int): size=%zu, capacity=%zu, isEmpty=%s\n",
			XListDLinked_size_base(li), XListDLinked_capacity_base(li),
			XListDLinked_isEmpty_base(li) ? "是" : "否");
		XPrintf("  typeSize=%zu\n", XListDLinked_typeSize_base(li));
		XListDLinked_delete_base(li);
	}
	{
		XListDLinked* li = XListDLinked_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(int), false);
		XContainerSetCompare(li, int_compare);
		XPrintf("create_ex(int,cow=false): size=%zu, typeSize=%zu\n",
			XListDLinked_size_base(li), XListDLinked_typeSize_base(li));
		XListDLinked_delete_base(li);
	}
	{
		XListDLinked li;
		XListDLinked_init(&li, sizeof(int), true);
		XContainerSetCompare(&li, int_compare);
		XPrintf("init: size=%zu, isEmpty=%s\n",
			XListDLinked_size_base(&li), XListDLinked_isEmpty_base(&li) ? "是" : "否");
		int val = 42;
		XListDLinked_push_back_base(&li, &val);
		XPrintf("  插入42后: size=%zu\n", XListDLinked_size_base(&li));
		XListDLinked_deinit_base(&li);
	}
	{
		int arr[] = { 1, 2, 3 };
		XListDLinked* src = XListDLinkedMakeInt(arr, 3);
		XListDLinked* copy = XListDLinked_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(int), true);
		XContainerSetCompare(copy, int_compare);
		XCopy(copy, src);
		XPrintf("copy_base: copy.size=%zu, src.size=%zu\n",
			XListDLinked_size_base(copy), XListDLinked_size_base(src));
		XListDLinkedPrintInt(copy, "  copy: ");
		int val = 99;
		XListDLinked_push_back_base(src, &val);
		XPrintf("  修改src后: copy.size=%zu, src.size=%zu\n",
			XListDLinked_size_base(copy), XListDLinked_size_base(src));
		XListDLinked_delete_base(src);
		XListDLinked_delete_base(copy);
	}
	{
		int arr[] = { 10, 20, 30 };
		XListDLinked* src = XListDLinkedMakeInt(arr, 3);
		XListDLinked* moved = XListDLinked_Create(int);
		XContainerSetCompare(moved, int_compare);
		XMove(moved, src);
		XPrintf("move_base: moved.size=%zu, src.isEmpty=%s\n",
			XListDLinked_size_base(moved),
			XListDLinked_isEmpty_base(src) ? "是" : "否");
		XListDLinkedPrintInt(moved, "  moved: ");
		XListDLinked_delete_base(moved);
		XListDLinked_delete_base(src);
	}
	{
		XPrintf("maxSize=%zu\n", XListDLinked_maxSize_base());
	}
	XPrintf("\n");
}

// ======================== 2. 插入操作测试 ========================
static void XListDLinkedInsertTest(void)
{
	XPrintf("===== 插入操作测试 =====\n");

	/* --- push_front / prepend --- */
	{
		XListDLinked* li = XListDLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 10, b = 20, c = 30;
		XListDLinked_push_front_base(li, &a);
		XListDLinked_push_front_base(li, &b);
		XListDLinked_Push_Front_Base(li, int, c);
		XListDLinkedPrintInt(li, "push_front 10,20,30: ");
		XPrintf("  size=%zu (期望:3)\n", XListDLinked_size_base(li));
		XListDLinked_delete_base(li);
	}
	{
		XListDLinked* li = XListDLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int* p = XMalloc_System(sizeof(int)); *p = 99;
		XListDLinked_push_front_move_base(li, p);
		XListDLinkedPrintInt(li, "push_front_move(99): ");
		XListDLinked_delete_base(li);
	}
	{
		XListDLinked* li = XListDLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int v = 5;
		XListDLinked_prepend_base(li, &v);
		int v2 = 3;
		XListDLinked_prepend_base(li, &v2);
		XListDLinkedPrintInt(li, "prepend 5,3: ");
		XListDLinked_delete_base(li);
	}

	/* --- push_back / append --- */
	{
		XListDLinked* li = XListDLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 1, b = 2, c = 3;
		XListDLinked_push_back_base(li, &a);
		XListDLinked_push_back_base(li, &b);
		XListDLinked_Push_Back_Base(li, int, c);
		XListDLinkedPrintInt(li, "push_back 1,2,3: ");
		XPrintf("  size=%zu (期望:3)\n", XListDLinked_size_base(li));
		XListDLinked_delete_base(li);
	}
	{
		XListDLinked* li = XListDLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int* p = XMalloc_System(sizeof(int)); *p = 88;
		XListDLinked_push_back_move_base(li, p);
		XListDLinkedPrintInt(li, "push_back_move(88): ");
		XListDLinked_delete_base(li);
	}
	{
		XListDLinked* li = XListDLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 100, b = 200;
		XListDLinked_append_base(li, &a);
		XListDLinked_append_base(li, &b);
		XListDLinkedPrintInt(li, "append 100,200: ");
		XListDLinked_delete_base(li);
	}

	/* --- insert_base --- */
	{
		int arr[] = { 1, 5, 9 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 3);
		XListDLinkedPrintInt(li, "初始: ");
		XListBase_iterator it;
		int findVal = 5;
		XListDLinked_find_base(li, &findVal, &it);
		int insertVal = 3;
		XListDLinked_insert_base(li, it.node, &insertVal);
		XListDLinkedPrintInt(li, "insert(3)在5之前: ");
		XListDLinked_delete_base(li);
	}
	{
		XListDLinked* li = XListDLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 10;
		XListDLinked_push_back_base(li, &a);
		int* p = XMalloc_System(sizeof(int)); *p = 999;
		XListBase_iterator it;
		int findVal = 10;
		XListDLinked_find_base(li, &findVal, &it);
		XListDLinked_insert_move_base(li, it.node, p);
		XListDLinkedPrintInt(li, "insert_move(999)在10之前: ");
		XListDLinked_delete_base(li);
	}

	/* --- insert_array --- */
	{
		int arr[] = { 10, 50 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 2);
		XListDLinkedPrintInt(li, "初始: ");
		int ins[] = { 20, 30, 40 };
		XListBase_iterator it;
		int findVal = 50;
		XListDLinked_find_base(li, &findVal, &it);
		size_t n = XListDLinked_insert_array_base(li, it.node, ins, 3);
		XPrintf("  insert_array插入%zu个\n", n);
		XListDLinkedPrintInt(li, "  结果: ");
		XListDLinked_delete_base(li);
	}

	

	XPrintf("\n");
}


// ======================== 3. 删除操作测试 ========================
static void XListDLinkedRemoveTest(void)
{
	XPrintf("===== 删除操作测试 =====\n");
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 5);
		XListDLinkedPrintInt(li, "初始: ");
		XListDLinked_pop_front_base(li);
		XListDLinkedPrintInt(li, "pop_front后: ");
		XListDLinked_pop_back_base(li);
		XListDLinkedPrintInt(li, "pop_back后: ");
		XPrintf("  size=%zu (期望:3)\n", XListDLinked_size_base(li));
		XListDLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 5);
		XListDLinked_removeFirst_base(li);
		XListDLinkedPrintInt(li, "removeFirst: ");
		XListDLinked_removeLast_base(li);
		XListDLinkedPrintInt(li, "removeLast: ");
		XListDLinked_delete_base(li);
	}
	{
		int arr[] = { 10, 20, 30, 20, 40 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 5);
		int rm = 20;
		XPrintf("remove(20): %s\n",
			XListDLinked_remove_base(li, &rm) ? "是" : "否");
		XListDLinkedPrintInt(li, "  删除首个20后: ");
		XListDLinked_Remove_Base(li, int, 20);
		XListDLinkedPrintInt(li, "  Remove_Base(20)后: ");
		XListDLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 5);
		XListDLinkedPrintInt(li, "初始: ");
		XListBase_iterator it;
		int findVal = 3;
		XListDLinked_find_base(li, &findVal, &it);
		XListBase_iterator next;
		XListDLinked_erase_base(li, &it, &next);
		XListDLinkedPrintInt(li, "erase(3): ");
		XListDLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 5);
		XListDLinked_clear_base(li);
		XPrintf("clear: isEmpty=%s, size=%zu\n",
			XListDLinked_isEmpty_base(li) ? "是" : "否",
			XListDLinked_size_base(li));
		XListDLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 2, 4, 2, 5 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 7);
		XListDLinkedPrintInt(li, "初始: ");
		int rm = 2;
		size_t r = XListDLinked_removeAll_base(li, &rm);
		XPrintf("removeAll(2): 移除%zu个 (期望:3)\n", r);
		XListDLinkedPrintInt(li, "  结果: ");
		XListDLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 2, 4 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 5);
		int rm = 2;
		XPrintf("removeOne(2): %s (期望:是)\n",
			XListDLinked_removeOne_base(li, &rm) ? "是" : "否");
		XListDLinkedPrintInt(li, "  removeOne(2)后: ");
		rm = 999;
		XPrintf("removeOne(999): %s (期望:否)\n",
			XListDLinked_removeOne_base(li, &rm) ? "是" : "否");
		XListDLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 8);
		XListDLinkedPrintInt(li, "初始: ");
		size_t r = XListDLinked_removeIf_base(li, XListDLinkedRemoveEven, NULL);
		XPrintf("removeIf(偶数): 移除%zu个 (期望:4)\n", r);
		XListDLinkedPrintInt(li, "  结果: ");
		XListDLinked_delete_base(li);
	}
	XPrintf("\n");
}

// ======================== 4. 查找/访问测试 ========================
static void XListDLinkedAccessTest(void)
{
	XPrintf("===== 查找与访问测试 =====\n");
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 5);
		XPrintf("front=%d, back=%d\n",
			*(int*)XListDLinked_front_base(li),
			*(int*)XListDLinked_back_base(li));
		XPrintf("Front_Base=%d, Back_Base=%d\n",
			XListDLinked_Front_Base(li, int),
			XListDLinked_Back_Base(li, int));
		XListDLinked_delete_base(li);
	}
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 5);
		XListBase_iterator it;
		int f1 = 30, f2 = 999;
		XPrintf("find(30): %s\n",
			XListDLinked_find_base(li, &f1, &it) ? "是" : "否");
		if (XListDLinked_find_base(li, &f1, &it))
			XPrintf("  找到值: %d (期望:30)\n", XListDNode_Data(it.node, int));
		XPrintf("find(999): %s (期望:否)\n",
			XListDLinked_find_base(li, &f2, &it) ? "是" : "否");
		XListDLinked_delete_base(li);
	}
	{
        int __v20 = 20; int __v999 = 999;
		int arr[] = { 10, 20, 30, 20, 40, 20, 50 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 7);
		int f = 20, n = 999;
		XPrintf("Contains_Base(20): %s (期望:是)\n",
			XListDLinked_Contains_Base(li, &__v20) ? "是" : "否");
		XPrintf("Contains_Base(999): %s (期望:否)\n",
			XListDLinked_Contains_Base(li, &__v999) ? "是" : "否");
		XListBase_iterator it;
		XPrintf("indexOf(20,0): %s\n",
			XListDLinked_indexOf_base(li, &f, 0, &it) ? "是" : "否");
		if (XListDLinked_indexOf_base(li, &f, 0, &it))
			XPrintf("  位置: node.data=%d (期望:20)\n", XListDNode_Data(it.node, int));
		XPrintf("indexOf(20,3): %s\n",
			XListDLinked_indexOf_base(li, &f, 3, &it) ? "是" : "否");
		if (XListDLinked_indexOf_base(li, &f, 3, &it))
			XPrintf("  位置: node.data=%d (期望:20)\n", XListDNode_Data(it.node, int));
		XPrintf("indexOf(999,0): %s (期望:否)\n",
			XListDLinked_indexOf_base(li, &n, 0, &it) ? "是" : "否");
		XPrintf("lastIndexOf(20,6): %s\n",
			XListDLinked_lastIndexOf_base(li, &f, 6, &it) ? "是" : "否");
		if (XListDLinked_lastIndexOf_base(li, &f, 6, &it))
			XPrintf("  位置: node.data=%d (期望:20)\n", XListDNode_Data(it.node, int));
		XPrintf("lastIndexOf(999,6): %s (期望:否)\n",
			XListDLinked_lastIndexOf_base(li, &n, 6, &it) ? "是" : "否");
		XListDLinked_delete_base(li);
	}
	{
        int __v1 = 1; int __v5 = 5; int __v999 = 999;
		int arr[] = { 1, 2, 3, 4, 5 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 5);
		XPrintf("StartsWith_Base(1): %s (期望:是)\n",
			XListDLinked_StartsWith_Base(li, &__v1) ? "是" : "否");
		XPrintf("StartsWith_Base(999): %s (期望:否)\n",
			XListDLinked_StartsWith_Base(li, &__v999) ? "是" : "否");
		XPrintf("EndsWith_Base(5): %s (期望:是)\n",
			XListDLinked_EndsWith_Base(li, &__v5) ? "是" : "否");
		XPrintf("EndsWith_Base(999): %s (期望:否)\n",
			XListDLinked_EndsWith_Base(li, &__v999) ? "是" : "否");
		XListDLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 3);
		XPrintf("size=%zu, count=%zu, length=%zu\n",
			XListDLinked_size_base(li),
			XListDLinked_count_base(li),
			XListDLinked_length_base(li));
		XPrintf("capacity=%zu\n", XListDLinked_capacity_base(li));
		XListDLinked_delete_base(li);
	}
	{
		XListDLinked* empty = XListDLinked_Create(int);
		XContainerSetCompare(empty, int_compare);
		XPrintf("空链表: front=%s, back=%s\n",
			XListDLinked_front_base(empty) ? "非空" : "空",
			XListDLinked_back_base(empty) ? "非空" : "空");
		XListDLinked_delete_base(empty);
	}
	XPrintf("\n");
}


// ======================== 5. 取出元素测试 ========================
static void XListDLinkedTakeTest(void)
{
	XPrintf("===== 取出元素测试(takeFirst/takeLast) =====\n");
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 5);
		XListDLinkedPrintInt(li, "初始: ");
		int* p = (int*)XListDLinked_takeFirst_base(li);
		if (p) { XPrintf("takeFirst=%d (期望:10)\n", *p); XFree_System(p); }
		XListDLinkedPrintInt(li, "  takeFirst后: ");
		p = (int*)XListDLinked_takeLast_base(li);
		if (p) { XPrintf("takeLast=%d (期望:50)\n", *p); XFree_System(p); }
		XListDLinkedPrintInt(li, "  takeLast后: ");
		XListDLinked_delete_base(li);
	}
	{
		XListDLinked* empty = XListDLinked_Create(int);
		XContainerSetCompare(empty, int_compare);
		XPrintf("空: takeFirst=%s, takeLast=%s\n",
			XListDLinked_takeFirst_base(empty) ? "非空" : "空",
			XListDLinked_takeLast_base(empty) ? "非空" : "空");
		XListDLinked_delete_base(empty);
	}
	XPrintf("\n");
}

// ======================== 6. 比较/复制/交换/排序测试 ========================
static void XListDLinkedCompareTest(void)
{
	XPrintf("===== 比较/复制/交换/排序测试 =====\n");
	{
		int a1[] = { 1, 2, 3, 4, 5 };
		int a2[] = { 1, 2, 3, 4, 5 };
		int a3[] = { 1, 2, 3 };
		XListDLinked* v1 = XListDLinkedMakeInt(a1, 5);
		XListDLinked* v2 = XListDLinkedMakeInt(a2, 5);
		XListDLinked* v3 = XListDLinkedMakeInt(a3, 3);
		XPrintf("v1==v2(equals): %s (期望:是)\n",
			XListDLinked_equals_base(v1, v2) ? "是" : "否");
		XPrintf("v1==v3(equals): %s (期望:否)\n",
			XListDLinked_equals_base(v1, v3) ? "是" : "否");
		XListDLinked_delete_base(v1);
		XListDLinked_delete_base(v2);
		XListDLinked_delete_base(v3);
	}
	{
		int a1[] = { 3, 2, 1 };
		int a2[] = { 4, 5 };
		XListDLinked* v1 = XListDLinkedMakeInt(a1, 3);
		XListDLinked* v2 = XListDLinkedMakeInt(a2, 2);
		XListDLinked_swap_base(v1, v2);
		XListDLinkedPrintInt(v1, "swap后v1: ");
		XListDLinkedPrintInt(v2, "swap后v2: ");
		XPrintf("v1.size=%zu, v2.size=%zu\n",
			XListDLinked_size_base(v1), XListDLinked_size_base(v2));
		XListDLinked_delete_base(v1);
		XListDLinked_delete_base(v2);
	}
	{
		int arr[] = { 5, 2, 8, 1, 9, 3 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 6);
		XListDLinkedPrintInt(li, "排序前: ");
		XListDLinked_sort_base(li, XSORT_ASC);
		XListDLinkedPrintInt(li, "升序后: ");
		XListDLinked_sort_base(li, XSORT_DESC);
		XListDLinkedPrintInt(li, "降序后: ");
		XListDLinked_delete_base(li);
	}
	XPrintf("\n");
}


// ======================== 7. 迭代器测试 ========================
static void XListDLinkedIteratorTest(void)
{
	XPrintf("===== 迭代器测试 =====\n");
	{
		int arr[] = { 1, 2, 3, 4, 5 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 5);
		XPrintf("正向遍历(iterator_for_each): ");
		XListDLinked_iterator_for_each(li, XListDLinkedForEachInt, NULL);
		XPrintf("\n");
		XListDLinked_delete_base(li);
	}
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 5);
		XPrintf("for_each_iterator宏遍历: ");
		for_each_iterator(li, XListDLinked, it)
		{
			XPrintf("%d ", XListDNode_Data(it.node, int));
		}
		XPrintf("\n");
		XListDLinked_delete_base(li);
	}
	{
		int arr[] = { 10, 20, 30, 40, 50 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 5);
		XPrintf("反向遍历(for_each_reverse_iterator): ");
		for_each_reverse_iterator(li, XListDLinked, it)
		{
			XPrintf("%d ", XListDNode_Data(it.node, int));
		}
		XPrintf("\n");
		XListDLinked_delete_base(li);
	}
	{
		XListDLinked* li = XListDLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int a = 100;
		XListDLinked_push_back_base(li, &a);
		XListDLinked_iterator it = XListDLinked_begin(li);
		XListDLinked_iterator endIt = XListDLinked_end(li);
		XPrintf("begin: data=%d, isEnd=%s\n",
			XListDNode_Data(it.node, int),
			XListDLinked_iterator_isEnd(&it) ? "是" : "否");
		XPrintf("end: isEnd=%s\n",
			XListDLinked_iterator_isEnd(&endIt) ? "是" : "否");
		XListDLinked_iterator_add(li, &it);
		XPrintf("add后: isEnd=%s\n",
			XListDLinked_iterator_isEnd(&it) ? "是" : "否");
		XPrintf("iterator_data=%s\n",
			XListDLinked_iterator_data(&it) ? "非空" : "空");
		XListDLinked_delete_base(li);
	}
	{
		int arr[] = { 1, 2, 3 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 3);
		XListDLinked_reverse_iterator rit = XListDLinked_rbegin(li);
		XListDLinked_reverse_iterator rendIt = XListDLinked_rend(li);
		XPrintf("rbegin: data=%d, isEnd=%s\n",
			XListDNode_Data(rit.node, int),
			XListDLinked_reverse_iterator_isEnd(&rit) ? "是" : "否");
		XPrintf("rend: isEnd=%s\n",
			XListDLinked_reverse_iterator_isEnd(&rendIt) ? "是" : "否");
		XPrintf("reverse_iterator_data=%d\n",
			*(int*)XListDLinked_reverse_iterator_data(&rit));
		XListDLinked_delete_base(li);
	}
	XPrintf("\n");
}

// ======================== 8. 边界与安全检查测试 ========================
static void XListDLinkedSafetyTest(void)
{
	XPrintf("===== 边界与安全检查测试 =====\n");
	{
		XListDLinked* li = XListDLinked_Create(int);
		XContainerSetCompare(li, int_compare);
		int val = 1;
		XPrintf("空链表: pop_front=%s, pop_back=%s\n",
			XListDLinked_pop_front_base(li) ? "是" : "否",
			XListDLinked_pop_back_base(li) ? "是" : "否");
		XPrintf("空链表: removeFirst=%s, removeLast=%s\n",
			XListDLinked_removeFirst_base(li) ? "是" : "否",
			XListDLinked_removeLast_base(li) ? "是" : "否");
		XPrintf("空链表: remove=%s, removeOne=%s, removeAll=%zu\n",
			XListDLinked_remove_base(li, &val) ? "是" : "否",
			XListDLinked_removeOne_base(li, &val) ? "是" : "否",
			XListDLinked_removeAll_base(li, &val));
		XPrintf("空链表: removeIf=%zu\n",
			XListDLinked_removeIf_base(li, XListDLinkedRemoveEven, NULL));
		XListDLinked_delete_base(li);
	}
	{
        int __v1 = 1;
		XPrintf("NULL: isEmpty=%s, size=%zu\n",
			XListDLinked_isEmpty_base(NULL) ? "是" : "否",
			XListDLinked_size_base(NULL));
		XPrintf("NULL: count=%zu, length=%zu\n",
			XListDLinked_count_base(NULL),
			XListDLinked_length_base(NULL));
		XPrintf("NULL: front=%s, back=%s\n",
			XListDLinked_front_base(NULL) ? "非空" : "空",
			XListDLinked_back_base(NULL) ? "非空" : "空");
		XPrintf("NULL: capacity=%zu\n", XListDLinked_capacity_base(NULL));
		XPrintf("NULL: typeSize=%zu\n", XListDLinked_typeSize_base(NULL));
		XPrintf("NULL: pop_front=%s, pop_back=%s\n",
			XListDLinked_pop_front_base(NULL) ? "是" : "否",
			XListDLinked_pop_back_base(NULL) ? "是" : "否");
		int val = 1;
		XPrintf("NULL: remove=%s, removeOne=%s, removeAll=%zu\n",
			XListDLinked_remove_base(NULL, &val) ? "是" : "否",
			XListDLinked_removeOne_base(NULL, &val) ? "是" : "否",
			XListDLinked_removeAll_base(NULL, &val));
		XPrintf("NULL: removeIf=%zu\n",
			XListDLinked_removeIf_base(NULL, XListDLinkedRemoveEven, NULL));
		XPrintf("NULL: takeFirst=%s, takeLast=%s\n",
			XListDLinked_takeFirst_base(NULL) ? "非空" : "空",
			XListDLinked_takeLast_base(NULL) ? "非空" : "空");
		XListBase_iterator it;
		XPrintf("NULL: find=%s, indexOf=%s, lastIndexOf=%s\n",
			XListDLinked_find_base(NULL, &val, &it) ? "是" : "否",
			XListDLinked_indexOf_base(NULL, &val, 0, &it) ? "是" : "否",
			XListDLinked_lastIndexOf_base(NULL, &val, 0, &it) ? "是" : "否");
		XPrintf("NULL: Contains=%s, StartsWith=%s, EndsWith=%s\n",
			XListDLinked_Contains_Base(NULL, &__v1) ? "是" : "否",
			XListDLinked_StartsWith_Base(NULL, &__v1) ? "是" : "否",
			XListDLinked_EndsWith_Base(NULL, &__v1) ? "是" : "否");
		XPrintf("NULL: removeFirst=%s, removeLast=%s\n",
			XListDLinked_removeFirst_base(NULL) ? "是" : "否",
			XListDLinked_removeLast_base(NULL) ? "是" : "否");
		XPrintf("NULL: equals(NULL,NULL)=%s\n",
			XListDLinked_equals_base(NULL, NULL) ? "是" : "否");
		XPrintf("NULL: maxSize=%zu\n", XListDLinked_maxSize_base());
	}
	{
		int arr[] = { 1, 2, 3 };
		XListDLinked* li = XListDLinkedMakeInt(arr, 3);
		XListDLinked* other = XListDLinked_create(sizeof(double));
		XContainerSetCompare(other, double_compare);
		double dv = 3.14;
		XListDLinked_push_back_base(other, &dv);
		XCopy(li, other);
		XPrintf("类型不一致: copy后li.size未变=%s, push_back类型不一致=%s（均应否）\n",
			XListDLinked_size_base(li) == 3 ? "是" : "否",
			XListDLinked_push_back_base(li, &dv) ? "是" : "否");
		XListDLinked_delete_base(li);
		XListDLinked_delete_base(other);
	}
	XPrintf("\n");
}

// ======================== 全部测试汇总 ========================
static void XListDLinkedAllTest(void)
{
	XPrintf("========== XListDLinked 全部测试开始 ==========\n\n");
	XListDLinkedCreateTest();
	XListDLinkedInsertTest();
	XListDLinkedRemoveTest();
	XListDLinkedAccessTest();
	XListDLinkedTakeTest();
	XListDLinkedCompareTest();
	XListDLinkedIteratorTest();
	XListDLinkedSafetyTest();
	XPrintf("\n========== XListDLinked 全部测试结束 ==========\n");
	//XCoreApplication_quit();
}

void XMenu_XListDLinkedTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XListDLinked(双向循环链表)");
	XMenu_addMenu(root, menu);
	{
		XAction* a = XMenu_addAction(menu, "【全部测试】");
		XAction_setAction(a, (Action)XListDLinkedAllTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "创建与初始化");
		XAction_setAction(a, (Action)XListDLinkedCreateTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "插入操作");
		XAction_setAction(a, (Action)XListDLinkedInsertTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "删除操作");
		XAction_setAction(a, (Action)XListDLinkedRemoveTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "查找与访问");
		XAction_setAction(a, (Action)XListDLinkedAccessTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "取出元素(take)");
		XAction_setAction(a, (Action)XListDLinkedTakeTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "比较/复制/交换/排序");
		XAction_setAction(a, (Action)XListDLinkedCompareTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "迭代器");
		XAction_setAction(a, (Action)XListDLinkedIteratorTest);
	}
	{
		XAction* a = XMenu_addAction(menu, "边界与安全检查");
		XAction_setAction(a, (Action)XListDLinkedSafetyTest);
	}
}
#endif
