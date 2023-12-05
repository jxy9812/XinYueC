#include"Test.h"
#include"XList.h"
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
	XList* li = XList_new(sizeof(int));
	int size = 10;
	srand((unsigned int)time(NULL));
	int* p1 = malloc(sizeof(int) * size);
	for (size_t i = 0; i < size; i++)
	{
		int num = rand() % 1000;
		XList_push_back(li,&num);//尾插
	}
	printf("排序前\n");
	XList_iterator_for_each(li, ListFor_each, NULL);printf("\n");

	clock_t  time_front = clock();
	XList_sort(li, XLess_int);
	clock_t time_after = clock();

	printf("排序后\n");
	XList_iterator_for_each(li, ListFor_each, NULL);printf("\n");

	printf("%d随机数，链表排序运行了%dms\n", size, time_after - time_front);
	XList_free(li);
}
void ListIterator()
{
	XList* li = XList_new(sizeof(int));
	int arr[] = { 123,12,1,4,9 };
	for (size_t i = 0; i < sizeof(arr) / sizeof(arr[0]); i++)
	{
		XList_push_back(li, arr + i);
	}
	printf("开始正向遍历\n");
	for (XList_iterator* it = XList_begin(li); it != XList_end(li); it = XList_iterator_add(li, it))
	{
		printf("%d\n", *(int*)((XListNode*)it)->date);
	}
	printf("开始反向遍历\n");
	for (XList_reverse_iterator* it = XList_rbegin(li); it != XList_rend(li); it = XList_reverse_iterator_add(li, it))
	{
		printf("%d\n", *(int*)((XListNode*)it)->date);
	}
	XList_free(li);
}

void ListTest()
{
	XList* list = XList_new(sizeof(int));
	list->equality = XEquality_int;
	printf("%s\n", XContainerObject_empty(list)?"empty":"");
	printf("%d\n", XContainerObject_size(list));

	

	int arr[] = { 123,12,1,4,9 };
	for (size_t i = 0; i < sizeof(arr)/sizeof(arr[0]); i++)
	{
		XList_push_back(list,arr+i);
	}
	int x = 100;
	int findValue = 123;
	//XList_insert(list, XList_at(list, &findValue), &x);
	
	printf("元素遍历\t");XList_iterator_for_each(list, ListFor_each,NULL);printf("\n");
	printf("头元素为：%d\n", XList_Front(list,int));
	printf("尾元素为：%d\n", XList_Back(list,int));

	XListNode*findNode=XList_find(list,arr +2);

	printf("找到的数字%d\n", *(int*)findNode->date);

	XList_pop_front(list);
	XList_pop_back(list);
	//XList_erase(list, XList_at(list, &findValue));
	int removeVlaue =4 ;
	XList_remove(list,&removeVlaue);
	//XList_clear(list);
	printf("删除元素后遍历\t");XList_iterator_for_each(list, ListFor_each, NULL);
	XList_free(list);
}

void ListSwapTest()//交换函数测试
{
	XList* li1 = XList_new(sizeof(int));
	int num;

	for (size_t i = 0; i < 10; i++)
	{
		num = i;
		XList_push_back(li1, &num);//尾插
	}
	printf("li1元素遍历\n");
	XList_iterator_for_each(li1, ListFor_each, NULL); printf("\n");

	XList* li2 = XList_new(sizeof(int));

	for (size_t i = 0; i < 20; i++)
	{
		num = 20 - i;
		XList_push_back(li2, &num);//尾插
	}
	printf("li2元素遍历\n");
	XList_iterator_for_each(li2, ListFor_each, NULL); printf("\n");

	XList_swap(li1, li2);

	printf("交换后li1元素遍历\n");
	XList_iterator_for_each(li1, ListFor_each, NULL); printf("\n");

	printf("交换后li2元素遍历\n");
	XList_iterator_for_each(li2, ListFor_each, NULL); printf("\n");
	XList_free(li1);
	XList_free(li2);
}