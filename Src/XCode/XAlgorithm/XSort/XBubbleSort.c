#include"XAlgorithm.h"
#include"XSort.h"
void XBubbleSort(void* LParray, const size_t nSize, const size_t TypeSize,XCompare compare, XSortOrder order)
{
	int32_t cmp;
	for (size_t i = 0; i < nSize - 1; i++)//n-1次冒泡
	{
		bool flag = true;//判断是否已经有序
		for (char* Lp = LParray; Lp < (char*)LParray + TypeSize * (nSize - 1 - i); Lp += TypeSize)//一趟冒泡
		{
			//if (!compare(Lp, Lp + TypeSize))
			cmp = compare(Lp, Lp + TypeSize);
			if (!((cmp == XCompare_Less) && (order == XSORT_ASC) || (cmp == XCompare_Equality) || (cmp == XCompare_Greater) && (order == XSORT_DESC)))
			{
				XSwap(Lp, Lp + TypeSize, TypeSize);//交换函数
				flag = false;
			}
		}
		if (flag)
			break;
	}

}