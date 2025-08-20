#include"XAlgorithm.h"
#include"XSort.h"
//直接插入排序gap变量控制一次间隔
void InsertSort_gap(void* LParray, const size_t  nSize, const size_t TypeSize, const size_t gap, XCompare compare )
{
	for (size_t end = 0; end < nSize - gap; end++)
	{
		char* LpTwo = (char*)LParray + (end + gap) * TypeSize;
		for (size_t i = 0; i <= end; i += gap)
		{
			char* LpOne = (char*)LParray + (end - i) * TypeSize;
			if (compare(LpOne, LpTwo))//排序比较函数
			{
				break;
			}
			else
			{
				XSwap(LpOne, LpTwo, TypeSize);//交换函数
				LpTwo = LpOne;
			}
		}
	}
}
void XInsertSort(void* LParray, const size_t nSize, const size_t TypeSize, XCompare compare )
{
	InsertSort_gap(LParray, nSize, TypeSize, 1, compare);
}