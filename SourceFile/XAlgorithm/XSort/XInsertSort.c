#include"XAlgorithm.h"
//直接插入排序gap变量控制一次间隔
void InsertSort_gap(void* LArray, const size_t  nSize, const size_t TypeSize, const size_t gap, bool(*Sort)(const void* LPrevValue, const void* LNextValue))
{
	for (size_t end = 0; end < nSize - gap; end++)
	{
		char* LpTwo = (char*)LArray + (end + gap) * TypeSize;
		for (size_t i = 0; i <= end; i += gap)
		{
			char* LpOne = (char*)LArray + (end - i) * TypeSize;
			if (Sort(LpOne, LpTwo) <= 0)//排序比较函数
			{
				break;
			}
			else
			{
				swap(LpOne, LpTwo, TypeSize);//交换函数
				LpTwo = LpOne;
			}
		}
	}
}
void XInsertSort(void* LArray, const size_t nSize, const size_t TypeSize, bool(*Sort)(const void* LPrevValue, const void* LNextValue))
{
	InsertSort_gap(LArray, nSize, TypeSize, 1, Sort);
}