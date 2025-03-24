#include"Test.h"
#if DemoTest
#include"XMap.h"
#include"XVector.h"
#include"XSort.h"
#include"XLess.h"
#include"XEquality.h"
#include<stdio.h>
#include<stdlib.h>
#include<time.h>
static void ForPrint(void* values, void* args)
{
	printf("%d\n", *(int*)values);
}
static void insertMap(void* values, void* args)
{
	XMap_insert(args, values, values);
}
void XMapAndXVectorFindTest()
{
	//创建乱序的数组
	XVector* VArray = XVector_new(sizeof(size_t));
	int count = 1000000;//测试数据量
	for (size_t i = 0; i < count; i++)
	{
		XVector_push_back(VArray,&i);
	}
	/*printf("打乱前\n");
	XVector_iterator_for_each(VArray, ForPrint, NULL);*/
	XDerangement(XVector_begin(VArray), count, sizeof(size_t));
	printf("打乱后\n");
	//XVector_iterator_for_each(VArray, ForPrint, NULL);
	/*printf("使用排序后\n");
	XVector_sort(VArray,XLess_int);
	XVector_iterator_for_each(VArray, ForPrint, NULL);*/

	XMap* map = XMap_new(sizeof(size_t), sizeof(size_t),XEquality_int,XLess_int);
	//map插入Vector的数据
	XVector_iterator_for_each(VArray, insertMap, map);

	//性能测试
	size_t findNum = count / 2;
	clock_t vector_start = clock();
	size_t* Vret=XVector_find(VArray, &findNum);
	clock_t vector_end = clock();
	printf("XVector查询数据:%d 用时%dms\n", *Vret, vector_end- vector_start);

	clock_t map_start = clock();
	size_t* mret = XMap_at(map,&findNum);
	clock_t map_end = clock();
	printf("XMap查询数据:%d 用时%dms\n", *mret, map_end - map_start);
}

#endif