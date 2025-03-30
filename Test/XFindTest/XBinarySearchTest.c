#include"XDataStructTest.h"
#if DEMOTEST
#include"XFind.h"
#include"XVector.h"
#include"XLess.h"
#include"XEquality.h"
void XBinarySearchTest()
{
#if(XVector_ON)
	//数组
	XVector* VArray = XVector_New(size_t);
	int count = 10000000;//测试数据量
	for (size_t i = 0; i < count; i++)
	{
		XVector_push_back(VArray, &i);
	}
	int findVal = 9999999;
	int* ret=XBinarySearch(XVector_begin(VArray), count,sizeof(size_t),XLess_int,XEquality_int,&findVal);
	printf("二分查找到值:%d", *ret);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
#endif