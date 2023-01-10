#include"vector.h"
#include"vector_head.h"
#include<stdlib.h>
#include<string.h>
void Vector_clear(struct VECTOR* vec)//清空vector的数组，释放内存
{
	if (vec->_date != NULL)
	{
		free(vec->_date);
		vec->_date = NULL;
		vec->_current = 0;
		vec->_size = 0;
	}
}
bool Vector_empty(const struct VECTOR* vec)//检测vector内是否为空，空为真 O(1)
{
	return !vec->_current;
}
int Vector_size(const struct VECTOR* vec)//返回vector内元素的个数 O(1)
{
	return vec->_current;
}
int  Vector_capacity(const struct VECTOR* vec)//返回当前向量所能容纳的最大元素值
{
	return vec->_size;
}

void  Vector_sort(struct VECTOR* vec, int (*Sort)(void* x, void* y))//排序
{
	qsort(vec->_date, vec->_current, vec->_type, Sort);
}
void Vector_swap(struct VECTOR* vec1, struct VECTOR* vec2)//交换两个同类型向量的数据
{
	swap(&vec1->_date, &vec2->_date, sizeof(void*));
	swap(&vec1->_current, &vec2->_current, sizeof(int));
	swap(&vec1->_size, &vec2->_size, sizeof(int));
}
void* Vector_find(const struct VECTOR* vec, const void* val, bool(*fi)(const void* val1, const void* val2))//查找数据，返回找到的指针，没有返回NULL
{
	for (int i = 0; i < vec->size(vec); i++)
	{
		//if (memcmp(vec->at(vec, i), val, vec->_type) == 0)
		if (fi(vec->at(vec, i), val))
		{
			return vec->at(vec, i);
		}
	}
	return NULL;
}
