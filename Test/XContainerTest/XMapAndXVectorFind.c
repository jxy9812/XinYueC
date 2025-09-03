#include"XDataStructTest.h"
#if DEMOTEST
#include"XMap.h"
#include"XVector.h"
#include"XSort.h"
#include"XPrintf.h"
#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#if XMap_ON
static void ForPrint(void* value, void* args)
{
	XPrintf("%d\n", *(int*)value);
}
static void insertMap(void* value, void* args)
{
	XMap_insert_base(args, value, value);
}
#endif
void XMapAndXVectorFindTest()
{
#if XMap_ON&&XVector_ON
	//创建乱序的数组
	XVector* VArray = XVector_create(sizeof(size_t));
	XContainerSetCompare(VArray, XCompare_size_t);
	//VArray->m_equality = XEquality_size_t;
	int count = 1000000;//测试数据量
	for (size_t i = 0; i < count; i++)
	{
		XVector_push_back_base(VArray,&i);
	}
	XPrintf("打乱前\n");
	//XVector_iterator_for_each(VArray, ForPrint, NULL);
	XDerangement(XContainerDataPtr(VArray), count, sizeof(size_t));
	XPrintf("打乱后\n");
	//XVector_iterator_for_each(VArray, ForPrint, NULL);
	XPrintf("使用排序后\n");
	XVector_sort_base(VArray, XSORT_ASC);
	//XVector_iterator_for_each(VArray, ForPrint, NULL);

	XMap* map = XMap_create(sizeof(size_t), sizeof(size_t),XCompare_size_t);
	//map插入Vector的数据
	XVector_iterator_for_each(VArray, insertMap, map);

	//性能测试
	size_t findNum = count / 2;
	clock_t vector_start = clock();
	//size_t* Vret=XVector_find_base(VArray, &findNum);
	//clock_t vector_end = clock();
	//XPrintf("XVector查询数据:%d 用时%dms\n", *Vret, vector_end- vector_start);

	clock_t map_start = clock();
	size_t* mret = XMap_value_base(map,&findNum);
	clock_t map_end = clock();
	XPrintf("XMap查询数据:%d 用时%dms\n", *mret, map_end - map_start);
#endif
}

#endif