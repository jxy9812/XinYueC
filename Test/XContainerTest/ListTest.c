#include"XDataStructTest.h"
#if DEMOTEST
#include"XListDLinked.h"
#include"XEquality.h"
#include"XCompare.h"
#include<time.h>
#include<stdio.h>
#include<stdlib.h>
void ListFor_each(void* LPVal, void* args)
{
	printf("%d ",*(int*)LPVal);
}
void ListSortTest()
{
#if XList_ON
	XListDLinked* li = XListDLinked_new(sizeof(int));
	int size = 10;
	srand((unsigned int)time(NULL));
	//int* p1 = XMemory_malloc(sizeof(int) * size);
	for (size_t i = 0; i < size; i++)
	{
		int num = rand() % 1000;
		XListBase_push_back_base(li,&num);//尾插
	}
	printf("排序前\n");
	XListDLinked_iterator_for_each(li, ListFor_each, NULL);printf("\n");

	clock_t  time_front = clock();
	XListDLinked_sort_base(li, XLess_int);
	clock_t time_after = clock();

	printf("排序后\n");
	XListDLinked_iterator_for_each(li, ListFor_each, NULL);printf("\n");

	printf("%d随机数，链表排序运行了%dms\n", size, time_after - time_front);
	XListBase_free_base(li);
#endif
}
void ListIterator()
{
#if XList_ON
	XListDLinked* li = XListDLinked_new(sizeof(int));
	int arr[] = { 123,12,1,4,9 };
	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		XListBase_push_back_base(li, arr + i);
	}
	printf("开始正向遍历\n");
	for (XListDLinked_iterator* it = XListDLinked_begin(li); it != XListDLinked_end(li); it = XListDLinked_iterator_add(li, it))
	{
		printf("%d\n", *(int*)((XListDNode*)it)->date);
	}
	printf("开始反向遍历\n");
	for (XListDLinked_reverse_iterator* it = XListDLinked_rbegin(li); it != XListDLinked_rend(li); it = XListDLinked_reverse_iterator_add(li, it))
	{
		printf("%d\n", *(int*)((XListDNode*)it)->date);
	}
	XListBase_free_base(li);
#endif
}

void ListTest()
{
#if XList_ON
	printf("XList 测试\n");
	XListDLinked* list = XListDLinked_new(sizeof(int));
	list->m_parent.m_equality = XEquality_int;
	//printf("%s\n", XContainerObject_empty(list)?"empty":"");
	//printf("%d\n", XContainerObject_getSize_base(list));

	

	int arr[] = { 123,12,1,4,9 };
	for (size_t i = 0; i < sizeof(arr)/sizeof(arr[0]); i++)
	{
		XListBase_push_back_base(list,arr+i);
	}
	int x = 100;
	int findValue = 123;
	//XList_insert_base(list, XList_at(list, &findValue), &x);
	
	printf("元素遍历\t"); XListDLinked_iterator_for_each(list, ListFor_each,NULL);printf("\n");
	printf("头元素为：%d\n", XListBase_Front_Base(list,int));
	printf("尾元素为：%d\n", XListBase_Back_Base(list,int));

	XListDNode*findNode=XListBase_find_base(list,arr +2);

	printf("找到的数字%d\n", *(int*)findNode->date);

	XListBase_pop_front_base(list);
	XListBase_pop_back_base(list);
	//XList_erase_base(list, findNode);
	int removeVlaue =4 ;
	XListBase_remove_base(list,&removeVlaue);
	//XList_clear_base(list);
	printf("删除元素后遍历\t"); XListDLinked_iterator_for_each(list, ListFor_each, NULL);
	XListBase_free_base(list);
#endif
}

void ListSwapTest()//交换函数测试
{
#if XList_ON
	XListDLinked* li1 = XListDLinked_new(sizeof(int));
	int num;

	for (size_t i = 0; i < 10; i++)
	{
		num = i;
		XListBase_push_back_base(li1, &num);//尾插
	}
	printf("li1元素遍历\n");
	XListDLinked_iterator_for_each(li1, ListFor_each, NULL); printf("\n");

	XListDLinked* li2 = XListDLinked_new(sizeof(int));

	for (size_t i = 0; i < 20; i++)
	{
		num = 20 - i;
		XListBase_push_back_base(li2, &num);//尾插
	}
	printf("li2元素遍历\n");
	XListDLinked_iterator_for_each(li2, ListFor_each, NULL); printf("\n");

	XListBase_swap_base(li1, li2);

	printf("交换后li1元素遍历\n");
	XListDLinked_iterator_for_each(li1, ListFor_each, NULL); printf("\n");

	printf("交换后li2元素遍历\n");
	XListDLinked_iterator_for_each(li2, ListFor_each, NULL); printf("\n");
	XListBase_free_base(li1);
	XListBase_free_base(li2);
#endif
}

#endif