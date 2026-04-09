#include"XDataStructTest.h"
#if DEMOTEST
#include"XListSLinkedAtomic.h"
#include"XCompare.h"
#include<time.h>
#include<stdio.h>
#include<stdlib.h>
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
// static void XListSLinkedAtomicSortTest();
// static void XListSLinkedAtomicIterator();
// static void XListSLinkedAtomicSwapTest();
// static void XListSLinkedAtomicTest();
static void ListFor_each(void* LPVal, void* args)
{
	XPrintf("%d ", *(int*)LPVal);
}
void XListSLinkedAtomicSortTest()
{
#if XList_ON
	XListSLinkedAtomic* li = XListSLinkedAtomic_create(sizeof(int));
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
	XListSLinkedAtomic_iterator_for_each(li, ListFor_each, NULL); XPrintf("\n");

	clock_t  time_front = clock();
	XListSLinkedAtomic_sort_base(li, XSORT_ASC);
	clock_t time_after = clock();

	XPrintf("排序后\n");
	XListSLinkedAtomic_iterator_for_each(li, ListFor_each, NULL); XPrintf("\n");

	XPrintf("%d随机数，链表排序运行了%dms\n", size, time_after - time_front);
	XListBase_delete_base(li);
#endif
	XCoreApplication_quit();
}
void XListSLinkedAtomicIterator()
{
#if XList_ON
	XListSLinkedAtomic* li = XListSLinkedAtomic_create(sizeof(int));
	int arr[] = { 123,12,1,4,9 };
	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		XListBase_push_back_base(li, arr + i);
	}
	XPrintf("开始正向遍历\n");
	for_each_iterator(li, XListSLinkedAtomic, it)
	{
		XPrintf("%d\n", XListSNodeAtomic_Data(it.node, int));
	}
	XListBase_delete_base(li);
#endif
	XCoreApplication_quit();
}

void XListSLinkedAtomicTest()
{
#if XList_ON
	XPrintf("XList 测试\n");
	XListSLinkedAtomic* list = XListSLinkedAtomic_create(sizeof(int));
	//list->m_class.m_equality = XEquality_int;
	XContainerSetCompare(list, int_compare);
	XPrintf("%s\n", XContainerObject_isEmpty_base(list) ? "empty" : "");
	XPrintf("%d\n", XContainerObject_size_base(list));



	int arr[] = { 123,12,1,4,9 };
	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		XListBase_push_front_base(list, arr + i);
		//XListBase_push_back_base(list, arr + i);
	}
	int x = 100;
	int findValue = 123;
	//XList_insert_base(list, XList_at(list, &findValue), &x);

	XPrintf("元素遍历\t"); XListSLinkedAtomic_iterator_for_each(list, ListFor_each, NULL); XPrintf("\n");
	XPrintf("头元素为：%d\n", XListBase_Front_Base(list, int));
	XPrintf("尾元素为：%d\n", XListBase_Back_Base(list, int));

	//XListSNodeAtomic* findNode = XListBase_find_base(list, arr + 2);
	//XListBase_insert_array_base(list, findNode, arr, 5);
	//XPrintf("找到的数字%d\n", XListSNodeAtomic_Data(findNode, int));
	XListSLinkedAtomic_iterator_for_each(list, ListFor_each, NULL); XPrintf("\n");
	XListBase_pop_front_base(list);
	XListBase_pop_back_base(list);
	//XListBase_erase_base(list, findNode);

	XListSLinkedAtomic_iterator_for_each(list, ListFor_each, NULL); XPrintf("\n");
	//return;
	int removeVlaue = 4;
	XListBase_remove_base(list, &removeVlaue);
	//XList_clear_base(list);
	XPrintf("删除元素后遍历\t"); XListSLinkedAtomic_iterator_for_each(list, ListFor_each, NULL);
	XListBase_delete_base(list);
#endif
	XCoreApplication_quit();
}

void XListSLinkedAtomicSwapTest()//交换函数测试
{
#if XList_ON
	XListSLinkedAtomic* li1 = XListSLinkedAtomic_create(sizeof(int));
	int num;

	for (size_t i = 0; i < 10; i++)
	{
		num = i;
		XListBase_push_back_base(li1, &num);//尾插
	}
	XPrintf("li1元素遍历\n");
	XListSLinkedAtomic_iterator_for_each(li1, ListFor_each, NULL); XPrintf("\n");

	XListSLinkedAtomic* li2 = XListSLinkedAtomic_create(sizeof(int));

	for (size_t i = 0; i < 20; i++)
	{
		num = 20 - i;
		XListBase_push_back_base(li2, &num);//尾插
	}
	XPrintf("li2元素遍历\n");
	XListSLinkedAtomic_iterator_for_each(li2, ListFor_each, NULL); XPrintf("\n");

	XListBase_swap_base(li1, li2);

	XPrintf("交换后li1元素遍历\n");
	XListSLinkedAtomic_iterator_for_each(li1, ListFor_each, NULL); XPrintf("\n");

	XPrintf("交换后li2元素遍历\n");
	XListSLinkedAtomic_iterator_for_each(li2, ListFor_each, NULL); XPrintf("\n");
	XListBase_delete_base(li1);
	XListBase_delete_base(li2);
#endif
	XCoreApplication_quit();
}
void XMenu_XListSLinkedAtomicTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XListSLinkedAtomic(单向无锁链表)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XListSLinkedAtomicTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "排序测试");
		XAction_setAction(action, XListSLinkedAtomicSortTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "迭代器测试");
		XAction_setAction(action, XListSLinkedAtomicIterator);
	}
	{
		XAction* action = XMenu_addAction(menu, "交换测试");
		XAction_setAction(action, XListSLinkedAtomicSwapTest);
	}
}
#endif