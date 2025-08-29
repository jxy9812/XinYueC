#include"XDataStructTest.h"
#if DEMOTEST
#include"XListSLinked.h"
#include"XEquality.h"
#include"XCompare.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include<time.h>
#include<stdio.h>
#include<stdlib.h>
static void XListSLinkedTest();
static void XListSLinkedSwapTest();
static void XListSLinkedIterator();
static void XListSLinkedSortTest();

static void ListFor_each(void* LPVal, void* args)
{
	XPrintf("%d ", *(int*)LPVal);
}
void XListSLinkedSortTest()
{
#if XList_ON
	XListSLinked* li = XListSLinked_create(sizeof(int));
	int size = 10;
	srand((unsigned int)time(NULL));
	//int* p1 = XMemory_malloc(sizeof(int) * size);
	for (size_t i = 0; i < size; i++)
	{
		int num = rand() % 1000;
		XListBase_push_back_base(li, &num);//尾插
	}
	XPrintf("排序前\n");
	XListSLinked_iterator_for_each(li, ListFor_each, NULL); XPrintf("\n");

	clock_t  time_front = clock();
	XListSLinked_sort_base(li, XLess_int);
	clock_t time_after = clock();

	XPrintf("排序后\n");
	XListSLinked_iterator_for_each(li, ListFor_each, NULL); XPrintf("\n");

	XPrintf("%d随机数，链表排序运行了%dms\n", size, time_after - time_front);
	XListBase_delete_base(li);
#endif
	XCoreApplication_requestQuit();
}
void XListSLinkedIterator()
{
#if XList_ON
	XListSLinked* li = XListSLinked_create(sizeof(int));
	int arr[] = { 123,12,1,4,9 };
	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		XListBase_push_back_base(li, arr + i);
	}
	XPrintf("开始正向遍历\n");
	for_each_iterator(li, XListSLinked,it)
	{
		XPrintf("%d\n", XListSNode_Data(it.node, int));
	}
	XListBase_delete_base(li);
#endif
	XCoreApplication_requestQuit();
}

void XListSLinkedTest()
{
#if XList_ON
	XPrintf("XList 测试\n");
	XListSLinked* list = XListSLinked_create(sizeof(int));
	list->m_parent.m_equality = XEquality_int;
	XPrintf("%s\n", XContainerObject_isEmpty_base(list)?"empty":"");
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

	XPrintf("元素遍历\t"); XListSLinked_iterator_for_each(list, ListFor_each, NULL); XPrintf("\n");
	XPrintf("头元素为：%d\n", XListBase_Front_Base(list, int));
	XPrintf("尾元素为：%d\n", XListBase_Back_Base(list, int));
	
	/*XListSNode* findNode = XListBase_find_base(list, arr + 2);
	XListBase_insert_array_base(list, findNode, arr, 5);
	XPrintf("找到的数字%d\n", XListSNode_Data(findNode,int));*/
	XListSLinked_iterator_for_each(list, ListFor_each, NULL); XPrintf("\n");
	XListBase_pop_front_base(list);
	XListBase_pop_back_base(list);
	//XListBase_erase_base(list, findNode);

	XListSLinked_iterator_for_each(list, ListFor_each, NULL); XPrintf("\n");
	//return;
	int removeVlaue = 4;
	XListBase_remove_base(list, &removeVlaue);
	//XList_clear_base(list);
	XPrintf("删除元素后遍历\t"); XListSLinked_iterator_for_each(list, ListFor_each, NULL);
	XListBase_delete_base(list);
#endif
	XCoreApplication_requestQuit();
}

void XListSLinkedSwapTest()//交换函数测试
{
#if XList_ON
	XListSLinked* li1 = XListSLinked_create(sizeof(int));
	int num;

	for (size_t i = 0; i < 10; i++)
	{
		num = i;
		XListBase_push_back_base(li1, &num);//尾插
	}
	XPrintf("li1元素遍历\n");
	XListSLinked_iterator_for_each(li1, ListFor_each, NULL); XPrintf("\n");

	XListSLinked* li2 = XListSLinked_create(sizeof(int));

	for (size_t i = 0; i < 20; i++)
	{
		num = 20 - i;
		XListBase_push_back_base(li2, &num);//尾插
	}
	XPrintf("li2元素遍历\n");
	XListSLinked_iterator_for_each(li2, ListFor_each, NULL); XPrintf("\n");

	XListBase_swap_base(li1, li2);

	XPrintf("交换后li1元素遍历\n");
	XListSLinked_iterator_for_each(li1, ListFor_each, NULL); XPrintf("\n");

	XPrintf("交换后li2元素遍历\n");
	XListSLinked_iterator_for_each(li2, ListFor_each, NULL); XPrintf("\n");
	XListBase_delete_base(li1);
	XListBase_delete_base(li2);
#endif
	XCoreApplication_requestQuit();
}
void XMenu_XListSLinkedTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XListSLinked(单向链表)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XListSLinkedTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "排序测试");
		XAction_setAction(action, XListSLinkedSortTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "迭代器测试");
		XAction_setAction(action, XListSLinkedIterator);
	}
	{
		XAction* action = XMenu_addAction(menu, "交换测试");
		XAction_setAction(action, XListSLinkedSwapTest);
	}
}

#endif