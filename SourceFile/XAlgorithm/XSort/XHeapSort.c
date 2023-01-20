#include"XAlgorithm.h"
//建堆
static void AdjustDwon(void* LArray, const size_t nSize, const size_t TypeSize, const size_t LpRoot, bool(*Sort)(const void* LPrevValue, const void* LNextValue))
{
	size_t parent = LpRoot;//父亲节点
	//while (1)
	for (size_t child = parent * 2 + 1/*默认左孩子*/; child < nSize;)
	{
		char* p_parent = (char*)LArray + parent * TypeSize;//父亲当前指针
		char* p_left = (char*)LArray + child * TypeSize;//左孩子指针
		if (child + 1 < nSize)
		{
			char* p_right = p_left + TypeSize;//右孩子指针
			if (Sort(p_left, p_right))//排序比较函数，选出大的那个
			{
				++child;//右孩子大默认孩子+1
			}
		}

		char* p_child = (char*)LArray + child * TypeSize;//选出的默认孩子指针
		if (!Sort(p_child, p_parent))//排序比较函数
		{
			swap(p_child, p_parent, TypeSize);//交换函数
			parent = child;//父亲节点更新
			child = parent * 2 + 1;//默认孩子更新
		}
		else
		{
			break;
		}
	}
}
void XHeapSort(void* LArray, const size_t nSize, const size_t TypeSize, bool(*Sort)(const void* LPrevValue, const void* LNextValue))
{
	//建堆
	size_t root = (nSize - 1 - 1) / 2;/*最后一个非叶子节点*/
	for (size_t i = 0; i <= root; i++)
	{
		AdjustDwon(LArray, nSize, TypeSize, root - i, Sort);
	}
	for (size_t i = 0; i < nSize - 1; i++)
	{
		char* p = (char*)LArray + (nSize - 1 - i) * TypeSize;//从最后一位开始排序数据
		swap(LArray, p, TypeSize);//交换函数
		AdjustDwon(LArray, nSize - 1 - i, TypeSize, 0, Sort);
	}
}