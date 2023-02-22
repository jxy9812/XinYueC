#include"XSort.h"
#include"XAlgorithm.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<time.h>
//打乱数组元素-乱序
void XDerangement(void* LParray, const size_t nSize, const size_t TypeSize)
{
	srand((unsigned int)time(NULL));
	for (size_t i = nSize; i>1; i--)
	{
		size_t nSel = rand() % i;
		swap((char*)LParray+ nSel* TypeSize, (char*)LParray+(i-1)*TypeSize, TypeSize);//交换数据
	}
}

////打乱数组元素-乱序
//void XDerangement(void* LParray/*数组首地址*/, const size_t nSize/*数组元素个数*/, const size_t TypeSize/*单元素大小-单位字节*/)
//{
//	srand((unsigned int)time(NULL));
//	for (size_t i = nSize; i > 1; i--)
//	{
//		size_t nSel = rand() % i;
//		void* temp = malloc(TypeSize);
//		memcpy(temp,(char*)LParray + nSel * TypeSize, TypeSize);
//		memcpy((char*)LParray + nSel * TypeSize, (char*)LParray + (i - 1) * TypeSize, TypeSize);
//		memcpy((char*)LParray + (i - 1) * TypeSize, temp, TypeSize);
//		free(temp);
//	}
//}