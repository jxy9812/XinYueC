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
	XList* li = XList_init(sizeof(int));
	int size = 1000000;
	srand((unsigned int)time(NULL));
	int* p1 = malloc(sizeof(int) * size);
	for (size_t i = 0; i < size; i++)
	{
		int num = rand() % 1000;
		li->push_back(li,&num);//尾插
	}
	clock_t  time_front = clock();
	XList_sort(li, XLess_int);
	clock_t time_after = clock();
	printf("%d随机数，链表排序运行了%dms\n", size, time_after - time_front);
	li->free(li);
}
void ListIterator()
{
	XList* li = XList_init(sizeof(int));
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
	XList* list = XList_init(sizeof(int));
	
	int arr[] = { 123,12,1,4,9 };
	for (size_t i = 0; i < sizeof(arr)/sizeof(arr[0]); i++)
	{
		XList_push_back(list,arr+i);
	}
	int x = 100;
	//li->insert_front_int(li, 0, &x, 10);
	printf("元素遍历\n");

	XList_iterator_for_each(list, ListFor_each,NULL);
	printf("\n");
	printf("头元素为：%d\n", *(int*)list->front(list)->date);
	printf("尾元素为：%d\n", *(int*)list->back(list)->date);

	XListNode*findNode=XList_find(list, XEquality_int, arr +2);
	printf("找到的数字%d\n", *(int*)findNode->date);


	/*li->pop_back(li);
	li->pop_back(li);
	li->pop_front(li);
	li->pop_front(li);*/
	//li->erase_p(li,li->find(li,num+1), li->find(li,num+5));
	list->erase_int(list,1, 8);
	printf("删除元素后遍历\n");
	for (size_t i = 0; i < list->size(list); i++)
	{
		printf("%d\n", *(int*)list->at(list, i)->date);
	}
	list->free(list);
}

void ListSwapTest()//交换函数测试
{
	XList* li1 = XList_init(sizeof(int));
	int num;

	for (size_t i = 0; i < 10; i++)
	{
		num = i;
		li1->push_front(li1, &num);
	}
	printf("li1元素遍历\n");
	for (size_t i = 0; i < li1->size(li1); i++)
	{
		printf("%d\n", *(int*)li1->at(li1, i));
	}

	XList* li2 = XList_init(sizeof(int));

	for (size_t i = 0; i < 20; i++)
	{
		num = 20 - i;
		li1->push_front(li2, &num);
	}
	printf("li2元素遍历\n");
	for (size_t i = 0; i < li1->size(li2); i++)
	{
		printf("%d\n", *(int*)li1->at(li2, i));
	}

	li1->swap(li1, li2);

	printf("交换后li1元素遍历\n");
	for (size_t i = 0; i < li1->size(li1); i++)
	{
		printf("%d\n", *(int*)li1->at(li1, i));
	}

	printf("交换后li2元素遍历\n");
	for (size_t i = 0; i < li1->size(li2); i++)
	{
		printf("%d\n", *(int*)li1->at(li2, i));
	}
	li1->clear(li1);
	li1->clear(li2);
}