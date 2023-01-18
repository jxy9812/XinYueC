#include"XVector_head.h"
#include<stdlib.h>
#include<string.h>
struct XVector;
void XVector_clear(struct XVector* this_vector)//清空vector的数组，释放内存
{
	XVECTOR* vector=(XVECTOR*)this_vector;
	if (vector->object._data != NULL)
	{
		free(vector->object._data);
		vector->object._data = NULL;
		vector->object._capacity = 0;
		vector->object._size = 0;
	}
}
void XVector_free(const struct XVector* this_vector)//释放内存
{
	XVECTOR* vector = (XVECTOR*)this_vector;
	XVector_clear(vector);
	free(vector);
}
bool XVector_empty(const struct XVector* this_vector)//检测vector内是否为空，空为真 O(1)
{
	XVECTOR* vector=(XVECTOR*)this_vector;
	return XContainerObject_empty(&vector->object);
}
int XVector_size(const struct XVector* vec)//返回vector内元素的个数 O(1)
{
	XVECTOR* vector=(XVECTOR*)vec;
	return XContainerObject_size(&vector->object);
}
int  XVector_capacity(const struct XVector* this_vector)//返回当前向量所能容纳的最大元素值
{
	XVECTOR* vector=(XVECTOR*)this_vector;
	return XContainerObject_capacity(&vector->object);
}

void XVector_swap(struct XVector* vec1, struct XVector* vec2)//交换两个同类型向量的数据
{
	XVECTOR* vector1=(XVECTOR*)vec1;
	XVECTOR* vector2=(XVECTOR*)vec2;
	XContainerObject_swap(&vector1->object, &vector2->object);
}
void  XVector_sort(struct XVector* this_vector, int (*Sort)(void* x, void* y))//排序
{
	XVECTOR* vector=(XVECTOR*)this_vector;
	qsort(vector->object._data, vector->object._size, vector->object._type, Sort);
}
void* XVector_find(const struct XVector* this_vector, const void* val, bool(*fi)(const void* val1, const void* val2))//查找数据，返回找到的指针，没有返回NULL
{
	XVECTOR* vector=(XVECTOR*)this_vector;
	for (int i = 0; i < vector->size(this_vector); i++)
	{
		//if (memcmp(vector->at(this_vector, i), val, vector->_type) == 0)
		if (fi(vector->at(this_vector, i), val))
		{
			return vector->at(this_vector, i);
		}
	}
	return NULL;
}
