#include"vector.h"
#include"vector_head.h"
#include<stdlib.h>
#include<string.h>
void Vector_clear(struct vector* vec)//清空vector的数组，释放内存
{
	VECTOR* vector=(VECTOR*)vec;
	if (vector->_date != NULL)
	{
		free(vector->_date);
		vector->_date = NULL;
		vector->_current = 0;
		vector->_size = 0;
	}
}
bool Vector_empty(const struct vector* vec)//检测vector内是否为空，空为真 O(1)
{
	VECTOR* vector=(VECTOR*)vec;
	return !vector->_current;
}
int Vector_size(const struct vector* vec)//返回vector内元素的个数 O(1)
{
	VECTOR* vector=(VECTOR*)vec;
	return vector->_current;
}
int  Vector_capacity(const struct vector* vec)//返回当前向量所能容纳的最大元素值
{
	VECTOR* vector=(VECTOR*)vec;
	return vector->_size;
}

void  Vector_sort(struct vector* vec, int (*Sort)(void* x, void* y))//排序
{
	VECTOR* vector=(VECTOR*)vec;
	qsort(vector->_date, vector->_current, vector->_type, Sort);
}
void Vector_swap(struct vector* vec1, struct vector* vec2)//交换两个同类型向量的数据
{
	VECTOR* vector1=(VECTOR*)vec1;
	VECTOR* vector2=(VECTOR*)vec2;
	swap(&vector1->_date, &vector2->_date, sizeof(void*));
	swap(&vector1->_current, &vector2->_current, sizeof(int));
	swap(&vector1->_size, &vector2->_size, sizeof(int));
}
void* Vector_find(const struct vector* vec, const void* val, bool(*fi)(const void* val1, const void* val2))//查找数据，返回找到的指针，没有返回NULL
{
	VECTOR* vector=(VECTOR*)vec;
	for (int i = 0; i < vector->size(vec); i++)
	{
		//if (memcmp(vector->at(vec, i), val, vector->_type) == 0)
		if (fi(vector->at(vec, i), val))
		{
			return vector->at(vec, i);
		}
	}
	return NULL;
}
