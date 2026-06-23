#include"XSort.h"
#include"XAlgorithm.h"
#include"XRandomGenerator.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>

//打乱数组元素-乱序
void XDerangement(void* LParray, const size_t nSize, const size_t TypeSize)
{
	for (size_t i = nSize; i > 1; i--)
	{
		size_t nSel = (size_t)XRandomGenerator_boundedU64(XRandomGenerator_global(), (uint64_t)i);
		XSwap((char*)LParray + nSel * TypeSize, (char*)LParray + (i - 1) * TypeSize, TypeSize);//交换数据
	}
}

////打乱数组元素-乱序
//void XDerangement(void* LParray/*数组首地址*/, const size_t nSize/*数组元素个数*/, const size_t TypeSize/*单元素大小-单位字节*/)
//{
//	for (size_t i = nSize; i > 1; i--)
//	{
//		size_t nSel = (size_t)XRandomGenerator_boundedU64(XRandomGenerator_global(), (uint64_t)i);
//		void* temp = XMalloc_System(TypeSize);
//		memcpy(temp,(char*)LParray + nSel * TypeSize, TypeSize);
//		memcpy((char*)LParray + nSel * TypeSize, (char*)LParray + (i - 1) * TypeSize, TypeSize);
//		memcpy((char*)LParray + (i - 1) * TypeSize, temp, TypeSize);
//		XFree_System(temp);
//	}
//}