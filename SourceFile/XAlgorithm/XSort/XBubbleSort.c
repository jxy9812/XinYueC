#include"XAlgorithm.h"
void XBubbleSort(void* LArray, const size_t nSize, const size_t TypeSize, bool(*Sort)(const void* LPrevValue, const void* LNextValue))
{
	for (size_t i = 0; i < nSize - 1; i++)//n-1次冒泡
	{
		bool flag = true;//判断是否已经有序
		for (char* Lp = LArray; Lp < (char*)LArray + TypeSize * (nSize - 1 - i); Lp += TypeSize)//一趟冒泡
		{
			if (!Sort(Lp, Lp + TypeSize))
			{
				swap(Lp, Lp + TypeSize, TypeSize);//交换函数
				flag = false;
			}
		}
		if (flag)
			break;
	}

}