#include"XAlgorithm.h"
#include"XSort.h"
#include"XMemory.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
void XMergeSort(void* LParray, const size_t nSize, const size_t TypeSize, XCompare compare, XSortOrder order)
{
	char* temp = XMemory_malloc(nSize * TypeSize);//开辟临时数组

	if (temp == NULL)
	{
		perror("临时数组失败\n");
	}
	//memset(tmp, 0, n * size);
	size_t gap = 1;
	char* begin = LParray;//数组头指针，开始指向头元素
	char* end = begin + TypeSize * (nSize - 1);//数组尾指针，指向最后一个元素
	char* LpOne = NULL;//第一组遍历指针指向头
	char* LpTwo = NULL;//第二组遍历指针指向头
	int32_t cmp;
	while (gap < nSize)
	{
		char* begtmp = temp;
		char* p = begin;//移动的指针
		for (p = begin; p < end; p += (TypeSize * gap * 2))
		{
			char* begina = p;//第一组比较的头元素
			char* righta = begina + gap * TypeSize - TypeSize;//第一组比较的尾元素
			char* beginb = righta + TypeSize;//第二组比较的头元素
			char* rightb = beginb + gap * TypeSize - TypeSize;//第二组比较的尾元素


			if (righta > end)//第一组右边界超出，第二组不存在
			{
				righta = end;//修正尾元素尾数组最后一个元素
				beginb = rightb + TypeSize;//使b数组不在进去循环判断
			}
			else if (beginb > end)//第一组存在，第二组不存在
			{
				beginb = rightb + TypeSize;//使b数组不在进去循环判断
			}
			else if (rightb > end)//第二组的尾元素超出边界
			{
				rightb = end;//修正尾元素尾数组最后一个元素
			}

			LpOne = begina;//第一组遍历指针指向头
			LpTwo = beginb;//第二组遍历指针指向头
			while (LpOne <= righta || LpTwo <= rightb)
			{
				if (LpOne <= righta && LpTwo <= rightb)//两个数组均有数的时候
				{
					//if (compare(LpOne, LpTwo))//比较的数符合排序标准
					cmp = compare(LpOne, LpTwo);
					if (((cmp == XCompare_Less) && (order == XSORT_ASC) || (cmp == XCompare_Equality) || (cmp == XCompare_Greater) && (order == XSORT_DESC)))//排序比较函数
					{
						XSwap(LpOne, begtmp, TypeSize);
						begtmp += TypeSize;//指向tmp数组的下一个元素
						if (LpOne <= righta)
							LpOne += TypeSize;
					}
					else
					{
						XSwap(LpTwo, begtmp, TypeSize);
						begtmp += TypeSize;//指向tmp数组的下一个元素
						if (LpTwo <= rightb)
							LpTwo += TypeSize;
					}
				}

				if (LpOne > righta)//a数组放完了
				{
					for (; LpTwo <= rightb; LpTwo += TypeSize)
					{
						XSwap(LpTwo, begtmp, TypeSize);
						begtmp += TypeSize;//指向tmp数组的下一个元素
					}
				}
				else if (LpTwo > rightb)//b数组放完了
				{
					for (; LpOne <= righta; LpOne += TypeSize)
					{
						XSwap(LpOne, begtmp, TypeSize);
						begtmp += TypeSize;//指向tmp数组的下一个元素
					}
				}
			}
		}
		if (LpTwo <= end)//b数组还剩下元素没复制完
		{
			for (; LpTwo <= end; LpTwo += TypeSize)
			{
				XSwap(LpTwo, begtmp, TypeSize);
				begtmp += TypeSize;//指向tmp数组的下一个元素
			}
		}
		memcpy(LParray, temp, nSize * TypeSize);//将临时数组保存的数据考回原数组
		//memset(tmp, 0, n * size);
		gap *= 2;
	}
	XMemory_free(temp);//释放临时数组
}
