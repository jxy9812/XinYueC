#include"XAlgorithm.h"
void XSelectSort(void* LArray, const size_t nSize, const size_t TypeSize, bool(*Sort)(const void* LPrevValue, const void* LNextValue))
{//四指针头尾同时遍历直接选择
	char* p_left = LArray;//指向第一个元素
	char* p_right = p_left + TypeSize * (nSize - 1);//指向最后一个元素
	char* p_move_left;//遍历左指针
	char* p_move_right;//遍历右指针
	char* p_val_left;//选出最值左元素
	char* p_val_right;//选出最值右元素
	for (; p_left < p_right; p_left += TypeSize, p_right -= TypeSize)
	{
		if (!Sort(p_left, p_right))//排序比较左右是否需要交换
		{
			swap(p_left, p_right, TypeSize);//交换函数
		}
		p_val_left = p_left;
		p_val_right = p_right;
		for (p_move_left = p_left + TypeSize, p_move_right = p_right - TypeSize; p_move_left < p_move_right; p_move_left += TypeSize, p_move_right -= TypeSize)
		{
			if (!Sort(p_val_left, p_move_left))//排序比较函数
			{
				p_val_left = p_move_left;//更新左边最小值
			}
			if (Sort(p_val_right, p_move_left))//排序比较函数
			{
				p_val_right = p_move_left;//更新右边最大值
			}
			if (!Sort(p_val_left, p_move_right))//排序比较函数
			{
				p_val_left = p_move_right;//更新左边最小值
			}
			if (Sort(p_val_right, p_move_right))//排序比较函数
			{
				p_val_right = p_move_right;//更新右边最大值
			}
		}//找出最大和最小的数
		swap(p_val_left, p_left, TypeSize);//交换函数
		swap(p_val_right, p_right, TypeSize);//交换函数
	}
}