#include"XFind.h"
#include"XVector.h"
#include"XLess.h"
#include"XEquality.h"
void XBinarySearchTest()
{
	//数组
	XVector* VArray = XVector_init(sizeof(size_t));
	int count = 10000000;//测试数据量
	for (size_t i = 0; i < count; i++)
	{
		XVector_push_back(VArray, &i);
	}
	int findVal = 9999999;
	int* ret=XBinarySearch(XVector_begin(VArray), count,sizeof(size_t),XLess_int,XEquality_int,&findVal);
	printf("二分查找到值:%d", *ret);
}