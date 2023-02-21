#include"XAlgorithm.h"
#include"XSort.h"

//建堆
static void AdjustDwon(void* LParray, const size_t nSize, const size_t TypeSize, const size_t LpRoot,XCompare compare,bool NOT/*比较结果取反用在堆排序*/)
{
	size_t parent = LpRoot;//父亲节点
	//while (1)
	for (size_t child = parent * 2 + 1/*默认左孩子*/; child < nSize;)
	{
		char* LPparent = (char*)LParray + parent * TypeSize;//父亲当前指针
		char* LPLchild = (char*)LParray + child * TypeSize;//左孩子指针
		if (child + 1 < nSize)
		{
			char* LPRchild = LPLchild + TypeSize;//右孩子指针
			bool flag = !compare(LPLchild, LPRchild);
			if (NOT)
				flag = !flag;
			if (flag)//排序比较函数，选出大的那个
			{
				++child;//右孩子大默认孩子默认孩子指向右孩子
			}
		}

		char* LPchild = (char*)LParray + child * TypeSize;//选出的孩子指针
		bool flag = compare(LPchild, LPparent);
		if (NOT)
			flag = !flag;
		if (flag)//排序比较函数
		{
			swap(LPchild, LPparent, TypeSize);//交换函数
			parent = child;//父亲节点更新
			child = parent * 2 + 1;//默认孩子更新
		}
		else
		{
			break;
		}
	}
}
//创建堆(调整数组使其成为堆结构)
void XCreateHeap(void* LPArray, const size_t nSize, const size_t TypeSize, XCompare compare)
{
	//建堆
	size_t root = (nSize - 1 - 1) / 2;/*最后一个非叶子节点*/
	for (size_t i = 0; i <= root; i++)
	{
		AdjustDwon(LPArray, nSize, TypeSize, root - i, compare,false);
	}
}
void XHeapSort(void* LPArray, const size_t nSize, const size_t TypeSize,XCompare compare)
{
	//建堆
	size_t root = (nSize - 1 - 1) / 2;/*最后一个非叶子节点*/
	for (size_t i = 0; i <= root; i++)
	{
		AdjustDwon(LPArray, nSize, TypeSize, root - i, compare, true);
	}
	for (size_t i = 0; i < nSize - 1; i++)
	{
		char* p = (char*)LPArray + (nSize - 1 - i) * TypeSize;//从最后一位开始排序数据
		swap(LPArray, p, TypeSize);//交换函数
		AdjustDwon(LPArray, nSize - 1 - i, TypeSize, 0, compare, true);
	}
}