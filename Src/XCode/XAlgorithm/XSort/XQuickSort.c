#include"XAlgorithm.h"
#include"XSort.h"
#include"XStack.h"
#include"XClass.h"
// 三数取中：返回左、中、右三个位置中值大小居中的元素索引
static void* GetMidIndex(void const* LpLeft, void const* LpRight, const size_t TypeSize, XCompare compare) 
{
	// 计算中间元素的地址（避免整数溢出的写法）
	size_t count = ((char*)LpRight - (char*)LpLeft) / TypeSize;  // 元素个数-1（因为右减左是总字节数）
	char* mid = (char*)LpLeft + (count / 2) * TypeSize;  // 中间位置索引

	// 步骤1：确保 left <= mid（如果不是，则交换两者的索引）
	if (compare(LpLeft, mid) > 0) {  // 左 > 中，需要交换
		mid = (char*)LpLeft;  // 此时mid临时指向左，原mid作为新的左
		LpLeft = mid + (count / 2) * TypeSize;  // 恢复原mid的位置作为新左
	}

	// 步骤2：确保 mid <= right（如果不是，则交换两者的索引）
	if (compare(mid, LpRight) > 0) {  // 中 > 右，需要交换
		char* temp = (char*)LpRight;
		LpRight = mid;
		mid = temp;
	}

	// 步骤3：再次确保 left <= mid（经过前两步，此时mid一定是中间值）
	if (compare(LpLeft, mid) > 0) {  // 左 > 中，最终交换
		mid = (char*)LpLeft;
	}

	return mid;  // 返回中间值的索引
}

//快排挖坑排序单次
static void* QuicPitSort_One(void const* LpLeft, void const* LpRight, const size_t TypeSize,XCompare compare, XSortOrder order)
{
	char* begin = LpLeft;//头指针
	char* end = LpRight;//尾指针
	char* index = GetMidIndex(LpLeft, LpRight, TypeSize, compare);//三数取中
	XSwap(index, LpLeft, TypeSize);//交换
	char* pivit = LpLeft;//坑位置
	int32_t cmp;
	while (begin < end)
	{
		// 2. 右边找符合条件的元素（放左边）
				// 每次移动end后，重新计算当前end元素与基准的比较结果
		while (begin < end)
		{
			int32_t cmp = compare(end, pivit);  // 实时更新cmp
			// 明确优先级：升序时找比基准小的，降序时找比基准大的，等于也包含
			bool shouldSwap = ((cmp == XCompare_Less && order == XSORT_ASC) ||
				(cmp == XCompare_Equality) ||
				(cmp == XCompare_Greater && order == XSORT_DESC));
			if (shouldSwap) break;  // 找到符合条件的元素，退出循环
			end -= TypeSize;        // 否则继续左移
		}
		if (begin < end)
		{
			XSwap(pivit, end, TypeSize);  // 填入坑中
			pivit = end;                  // 更新坑位
		}

		// 3. 左边找符合条件的元素（放右边）
		// 每次移动begin后，重新计算当前begin元素与基准的比较结果
		while (begin < end)
		{
			int32_t cmp = compare(begin, pivit);  // 实时更新cmp
			// 升序时找比基准大的，降序时找比基准小的，等于则跳过
			bool shouldContinue = ((cmp == XCompare_Less && order == XSORT_ASC) ||
				(cmp == XCompare_Equality) ||
				(cmp == XCompare_Greater && order == XSORT_DESC));
			if (shouldContinue)
				begin += TypeSize;  // 符合条件，继续右移
			else
				break;  // 找到符合条件的元素，退出循环
		}
		if (begin < end)
		{
			XSwap(pivit, begin, TypeSize);  // 填入坑中
			pivit = begin;                  // 更新坑位
		}
	}
	return pivit;
}

//挖坑法栈模拟递归
void XQuicPitSort_Stack(void* LParray, const size_t nSize, const size_t TypeSize,XCompare compare, XSortOrder order)
{
#if XStack_ON
	if (LParray == NULL || nSize == 0 || TypeSize == 0)
		return;
	XStack* st=XStack_Create(char*);
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
		pivit = QuicPitSort_One(left, right, TypeSize, compare, order);
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
	XStack_delete_base(st);
#else
	IS_ON_DEBUG(XStack_ON);
#endif
}

//挖坑法递归调用函数
static void QuicPitSort_Recur(void* LParray, void const* LpLeft, void const* LpRight, const size_t TypeSize,XCompare compare, XSortOrder order)
{

	if (LpLeft >= LpRight)//区间不存在或只有一个数
	{
		return;
	}
	size_t nSize = ((char*)LpRight - (char*)LpLeft) / TypeSize + 1;
	if (nSize <= 13 && nSize > 1)//优化最后几层的递归次数，调用直接插入排序
	{
		XInsertSort(LpLeft, nSize, TypeSize, compare, order);
		return;
	}
	char* pivit = QuicPitSort_One(LpLeft, LpRight, TypeSize, compare, order);
	QuicPitSort_Recur(LParray, LpLeft, pivit - TypeSize, TypeSize, compare, order);//左区间排序
	QuicPitSort_Recur(LParray, pivit + TypeSize, LpRight, TypeSize, compare, order);//右区间排序
}

void XQuickSort(void* LParray, const size_t nSize, const size_t TypeSize,XCompare compare, XSortOrder order)
{
	char* begin = LParray;//头指针
	char* end = begin + TypeSize * (nSize - 1);//尾指针
	//递归
	QuicPitSort_Recur(LParray, begin, end, TypeSize, compare, order);//挖坑法
}