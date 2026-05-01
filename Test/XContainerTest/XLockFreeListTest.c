#include"XDataStructTest.h"
#if DEMOTEST
#include"XLockFreeList.h"
#include"XCompare.h"
#include<time.h>
#include<stdio.h>
#include<stdlib.h>
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include"XThread.h"
// static void XLockFreeListSortTest();
// static void XLockFreeListIterator();
// static void XLockFreeListSwapTest();
// static void XLockFreeListTest();
static void ListFor_each(void* LPVal, void* args)
{
	XPrintf("%d ", *(int*)LPVal);
}
void XLockFreeListSortTest()
{
#if XList_ON
	XLockFreeList* li = XLockFreeList_create(sizeof(int));
	XContainerSetCompare(li, int_compare);
	int size = 10;
	srand((unsigned int)time(NULL));
	//int* p1 = XMemory_malloc(sizeof(int) * size);
	for (size_t i = 0; i < size; i++)
	{
		int num = rand() % 1000;
		XListBase_push_back_base(li, &num);//尾插
	}
	XPrintf("排序前\n");
	XLockFreeList_iterator_for_each(li, ListFor_each, NULL); XPrintf("\n");

	clock_t  time_front = clock();
	XLockFreeList_sort_base(li, XSORT_ASC);
	clock_t time_after = clock();

	XPrintf("排序后\n");
	XLockFreeList_iterator_for_each(li, ListFor_each, NULL); XPrintf("\n");

	XPrintf("%d随机数，链表排序运行了%dms\n", size, time_after - time_front);
	XListBase_delete_base(li);
#endif
	XCoreApplication_quit();
}
void XLockFreeListIterator()
{
#if XList_ON
	XLockFreeList* li = XLockFreeList_create(sizeof(int));
	int arr[] = { 123,12,1,4,9 };
	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		XListBase_push_back_base(li, arr + i);
	}
	XPrintf("开始正向遍历\n");
	for_each_iterator(li, XLockFreeList, it)
	{
		XPrintf("%d\n", XLockFreeListNode_Data(it.node, int));
	}
	XListBase_delete_base(li);
#endif
	XCoreApplication_quit();
}

void XLockFreeListTest()
{
#if XList_ON
	XPrintf("XList 测试\n");
	XLockFreeList* list = XLockFreeList_create(sizeof(int));
	//list->m_class.m_equality = XEquality_int;
	XContainerSetCompare(list, int_compare);
	XPrintf("%s\n", XContainer_isEmpty_base(list) ? "empty" : "");
	XPrintf("%d\n", XContainer_size_base(list));



	int arr[] = { 123,12,1,4,9 };
	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		XListBase_push_front_base(list, arr + i);
		//XListBase_push_back_base(list, arr + i);
	}
	int x = 100;
	int findValue = 123;
	//XList_insert_base(list, XList_at(list, &findValue), &x);

	XPrintf("元素遍历\t"); XLockFreeList_iterator_for_each(list, ListFor_each, NULL); XPrintf("\n");
	XPrintf("头元素为：%d\n", XListBase_Front_Base(list, int));
	XPrintf("尾元素为：%d\n", XListBase_Back_Base(list, int));

	//XLockFreeListNode* findNode = XListBase_find_base(list, arr + 2);
	//XListBase_insert_array_base(list, findNode, arr, 5);
	//XPrintf("找到的数字%d\n", XLockFreeListNode_Data(findNode, int));
	XLockFreeList_iterator_for_each(list, ListFor_each, NULL); XPrintf("\n");
	XListBase_pop_front_base(list);
	XLockFreeList_iterator_for_each(list, ListFor_each, NULL); XPrintf("\n");
	XListBase_pop_back_base(list);
	//XListBase_erase_base(list, findNode);

	XLockFreeList_iterator_for_each(list, ListFor_each, NULL); XPrintf("\n");
	//return;
	int removeVlaue = 4;
	XListBase_remove_base(list, &removeVlaue);
	//XList_clear_base(list);
	XPrintf("删除元素后遍历\t"); XLockFreeList_iterator_for_each(list, ListFor_each, NULL);
	XListBase_delete_base(list);
#endif
	XCoreApplication_quit();
}

void XLockFreeListSwapTest()//交换函数测试
{
#if XList_ON
	XLockFreeList* li1 = XLockFreeList_create(sizeof(int));
	int num;

	for (size_t i = 0; i < 10; i++)
	{
		num = i;
		XListBase_push_back_base(li1, &num);//尾插
	}
	XPrintf("li1元素遍历\n");
	XLockFreeList_iterator_for_each(li1, ListFor_each, NULL); XPrintf("\n");

	XLockFreeList* li2 = XLockFreeList_create(sizeof(int));

	for (size_t i = 0; i < 20; i++)
	{
		num = 20 - i;
		XListBase_push_back_base(li2, &num);//尾插
	}
	XPrintf("li2元素遍历\n");
	XLockFreeList_iterator_for_each(li2, ListFor_each, NULL); XPrintf("\n");

	XListBase_swap_base(li1, li2);

	XPrintf("交换后li1元素遍历\n");
	XLockFreeList_iterator_for_each(li1, ListFor_each, NULL); XPrintf("\n");

	XPrintf("交换后li2元素遍历\n");
	XLockFreeList_iterator_for_each(li2, ListFor_each, NULL); XPrintf("\n");
	XListBase_delete_base(li1);
	XListBase_delete_base(li2);
#endif
	XCoreApplication_quit();
}
// 线程函数 1：输出 "Thread 1 is running"
static void ThreadReceive(XThread* thread, XVarList* varlist)
{
	//XPrintf("线程进入\n");
	XVarList_args_1(varlist, XLockFreeList*, list);
	//int arr[] = { 100,123,456,4,8496,3,321,23,3,132,0 };
	XHandle id= XThread_currentThreadId();
	int count = 0;
	int value;
	while (!XLockFreeList_isEmpty_base(list))
	{
		if(XLockFreeList_pop_and_move_front(list, &value))
			XPrintf("XThread:%p count:%d value:%d size:%d\n", id,count++, value, XLockFreeList_size_base(list));
	}
	XThread_deleteLater(thread);
	XCoreApplication_quit();
	return 0;
}
//线程测试
void XLockFreeListThreadTest()
{
#if XList_ON
	XPrintf("循环队列 测试\n");
	XLockFreeList* list = XLockFreeList_create(sizeof(int));
	//list->m_class.m_equality = XEquality_int;
	XContainerSetCompare(list, int_compare);
	
	//threadTest(queue);

	for (size_t i = 0; i < 10; i++)
	{
		int n = i;
		while (!XLockFreeList_push_front_base(list, &n));
		//Sleep(100);
	}
	for (size_t i = 0; i < 10; i++)
	{
		XThread* thread = XThread_create_func(ThreadReceive, XVarList_Create(XVar(XLockFreeList*, list)));
		XThread_start(thread);
	}
	
	XCoreApplication_exec();
	//XThread_wait(thread, UINT32_MAX);
	//XThread_deleteLater(thread);
	XPrintf("循环队列 空\n");
	XLockFreeList_delete_base(list);
#else
	IS_ON_DEBUG(XLockFreeList_ON);
#endif
	XCoreApplication_quit();
}
void XMenu_XLockFreeListTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XLockFreeList(单向无锁链表)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XLockFreeListTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "线程安全测试");
		XAction_setAction(action, XLockFreeListThreadTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "排序测试");
		XAction_setAction(action, XLockFreeListSortTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "迭代器测试");
		XAction_setAction(action, XLockFreeListIterator);
	}
	{
		XAction* action = XMenu_addAction(menu, "交换测试");
		XAction_setAction(action, XLockFreeListSwapTest);
	}
}
#endif