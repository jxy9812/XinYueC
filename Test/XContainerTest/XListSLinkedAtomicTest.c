#include"XDataStructTest.h"
#if DEMOTEST
#include"XListSLinkedAtomic.h"
#include"XEquality.h"
#include"XCompare.h"
#include<time.h>
#include<stdio.h>
#include<stdlib.h>
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
static void XListSLinkedAtomicSortTest();
static void XListSLinkedAtomicIterator();
static void XListSLinkedAtomicSwapTest();
static void XListSLinkedAtomicTest();
static void ListFor_each(void* LPVal, void* args)
{
	printf("%d ", *(int*)LPVal);
}
void XListSLinkedAtomicSortTest()
{
#if XList_ON
	XListSLinkedAtomic* li = XListSLinkedAtomic_create(sizeof(int));
	int size = 10;
	srand((unsigned int)time(NULL));
	//int* p1 = XMemory_malloc(sizeof(int) * size);
	for (size_t i = 0; i < size; i++)
	{
		int num = rand() % 1000;
		XListBase_push_back_base(li, &num);//尾插
	}
	printf("排序前\n");
	XListSLinkedAtomic_iterator_for_each(li, ListFor_each, NULL); printf("\n");

	clock_t  time_front = clock();
	XListSLinkedAtomic_sort_base(li, XLess_int);
	clock_t time_after = clock();

	printf("排序后\n");
	XListSLinkedAtomic_iterator_for_each(li, ListFor_each, NULL); printf("\n");

	printf("%d随机数，链表排序运行了%dms\n", size, time_after - time_front);
	XListBase_delete_base(li);
#endif
	XCoreApplication_requestQuit();
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
	printf("开始正向遍历\n");
	for_each_iterator(li, XListSLinkedAtomic, it)
	{
		printf("%d\n", XListSNodeAtomic_Data(it.node, int));
	}
	XListBase_delete_base(li);
#endif
	XCoreApplication_requestQuit();
}

void XListSLinkedAtomicTest()
{
#if XList_ON
	printf("XList 测试\n");
	XListSLinkedAtomic* list = XListSLinkedAtomic_create(sizeof(int));
	list->m_parent.m_equality = XEquality_int;
	printf("%s\n", XContainerObject_isEmpty_base(list) ? "empty" : "");
	printf("%d\n", XContainerObject_getSize_base(list));



	int arr[] = { 123,12,1,4,9 };
	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		XListBase_push_front_base(list, arr + i);
		//XListBase_push_back_base(list, arr + i);
	}
	int x = 100;
	int findValue = 123;
	//XList_insert_base(list, XList_at(list, &findValue), &x);

	printf("元素遍历\t"); XListSLinkedAtomic_iterator_for_each(list, ListFor_each, NULL); printf("\n");
	printf("头元素为：%d\n", XListBase_Front_Base(list, int));
	printf("尾元素为：%d\n", XListBase_Back_Base(list, int));

	XListSNodeAtomic* findNode = XListBase_find_base(list, arr + 2);
	XListBase_insert_array_base(list, findNode, arr, 5);
	printf("找到的数字%d\n", XListSNodeAtomic_Data(findNode, int));
	XListSLinkedAtomic_iterator_for_each(list, ListFor_each, NULL); printf("\n");
	XListBase_pop_front_base(list);
	XListBase_pop_back_base(list);
	//XListBase_erase_base(list, findNode);

	XListSLinkedAtomic_iterator_for_each(list, ListFor_each, NULL); printf("\n");
	//return;
	int removeVlaue = 4;
	XListBase_remove_base(list, &removeVlaue);
	//XList_clear_base(list);
	printf("删除元素后遍历\t"); XListSLinkedAtomic_iterator_for_each(list, ListFor_each, NULL);
	XListBase_delete_base(list);
#endif
	XCoreApplication_requestQuit();
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
	printf("li1元素遍历\n");
	XListSLinkedAtomic_iterator_for_each(li1, ListFor_each, NULL); printf("\n");

	XListSLinkedAtomic* li2 = XListSLinkedAtomic_create(sizeof(int));

	for (size_t i = 0; i < 20; i++)
	{
		num = 20 - i;
		XListBase_push_back_base(li2, &num);//尾插
	}
	printf("li2元素遍历\n");
	XListSLinkedAtomic_iterator_for_each(li2, ListFor_each, NULL); printf("\n");

	XListBase_swap_base(li1, li2);

	printf("交换后li1元素遍历\n");
	XListSLinkedAtomic_iterator_for_each(li1, ListFor_each, NULL); printf("\n");

	printf("交换后li2元素遍历\n");
	XListSLinkedAtomic_iterator_for_each(li2, ListFor_each, NULL); printf("\n");
	XListBase_delete_base(li1);
	XListBase_delete_base(li2);
#endif
	XCoreApplication_requestQuit();
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