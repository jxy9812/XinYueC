#include"XAlgorithm.h"
#include"XSort.h"
#include"XStack.h"
#include"XClass.h"
//三数取中
static void* GetMidIndex(void const* LpLeft, void const* LpRight, const size_t TypeSize,XCompare compare )
{
	size_t nSize = ((char*)LpRight - (char*)LpLeft) / TypeSize + 1;
	char* mid = (char*)LpLeft + (nSize >> 1) * TypeSize;
	if (compare(LpLeft, mid) < 0)
		mid = LpLeft;
	if (compare(LpRight, mid) < 0)
		mid = LpRight;
	return mid;
}
//快排挖坑排序单次
static void* QuicPitSort_One(void const* LpLeft, void const* LpRight, const size_t TypeSize,XCompare compare )
{
	char* begin = LpLeft;//头指针
	char* end = LpRight;//尾指针
	char* index = GetMidIndex(LpLeft, LpRight, TypeSize, compare);//三数取中
	XSwap(index, LpLeft, TypeSize);//交换
	char* pivit = LpLeft;//坑位置
	while (begin < end)
	{
		//flag = true;
		//右边找放左边
		while (begin < end && !compare(end, pivit))
		{
			end -= TypeSize;
		}
		if (begin < end)
		{
			XSwap(pivit, end, TypeSize);//交换函数,入坑
			pivit = end;//新的坑位
		}
		//左边找放右边
		while (begin < end && compare(begin, pivit) )
		{
			begin += TypeSize;
		}
		if (begin < end)
		{
			XSwap(pivit, begin, TypeSize);//交换函数,入坑
			pivit = begin;//新的坑位
		}
	}
	return pivit;
}

//挖坑法栈模拟递归
void XQuicPitSort_Stack(void* LParray, const size_t nSize, const size_t TypeSize,XCompare compare )
{
#if XStack_ON
	if (LParray == NULL || nSize == 0 || TypeSize == 0)
		return;
	XStack* st=XStack_New(char*);
	char* begin = LParray;//移动头指针，开始指向头元素
	char* end = begin + TypeSize * (nSize - 1);//移动尾指针，开始指向尾元素
	char* temp = NULL;//入栈边界临时指针
	XStack_push_base(st,&end);//入右边界
	XStack_push_base(st, &begin);//入左边界
	//st->push(st, end);//入右边界
	//st->push(st, begin);//入左边界
	char* left = NULL;//区间头指针
	char* right = NULL;//区间尾指针
	char* pivit = NULL;//单次排序返回的排好元素指针
	while (!XStack_isEmpty_base(st))
	{
		left = XStack_Top_Base(st,char*);//头指针
		XStack_pop_base(st);
		right = XStack_Top_Base(st,char*);//尾指针 
		XStack_pop_base(st);
		pivit = QuicPitSort_One(left, right, TypeSize, compare);
		temp = pivit + TypeSize;
		if (temp < right)//右区间存在
		{
			XStack_push_base(st, &right);
			XStack_push_base(st, &temp);
		}
		temp = pivit - TypeSize;
		if (left < temp)//左区间存在
		{
			XStack_push_base(st, &temp);
			XStack_push_base(st, &left);
		}
	}
	XStack_free_base(st);
#else
	IS_ON_DEBUG(XStack_ON);
#endif
}

//挖坑法递归调用函数
static void QuicPitSort_Recur(void* LParray, void const* LpLeft, void const* LpRight, const size_t TypeSize,XCompare compare )
{

	if (LpLeft >= LpRight)//区间不存在或只有一个数
	{
		return;
	}
	size_t nSize = ((char*)LpRight - (char*)LpLeft) / TypeSize + 1;
	if (nSize <= 13 && nSize > 1)//优化最后几层的递归次数，调用直接插入排序
	{
		XInsertSort(LpLeft, nSize, TypeSize, compare);
		return;
	}
	char* pivit = QuicPitSort_One(LpLeft, LpRight, TypeSize, compare);
	QuicPitSort_Recur(LParray, LpLeft, pivit - TypeSize, TypeSize, compare);//左区间排序
	QuicPitSort_Recur(LParray, pivit + TypeSize, LpRight, TypeSize, compare);//右区间排序
}

void XQuickSort(void* LParray, const size_t nSize, const size_t TypeSize,XCompare compare )
{
	char* begin = LParray;//头指针
	char* end = begin + TypeSize * (nSize - 1);//尾指针
	//递归
	QuicPitSort_Recur(LParray, begin, end, TypeSize, compare);//挖坑法
}