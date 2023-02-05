#include"XVector_head.h"
#include"XSort.h"
#include"XFunctionCallback.h"
#include<stdlib.h>
#include<string.h>
struct XVector;
void XVector_clear(struct XVector* this_vector)//清空vector的数组，释放内存
{
	if (isObjectNULL(this_vector, "XVector_clear"))
		return;
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
	if (isObjectNULL(this_vector, "XVector_free"))
		return;
	XVECTOR* vector = (XVECTOR*)this_vector;
	XVector_clear(vector);
	free(vector);
}
bool XVector_empty(const struct XVector* this_vector)//检测vector内是否为空，空为真 O(1)
{
	if (isObjectNULL(this_vector, "XVector_empty"))
		return true;
	XVECTOR* vector=(XVECTOR*)this_vector;
	return XContainerObject_empty(&vector->object);
}
int XVector_size(const struct XVector* this_vector)//返回vector内元素的个数 O(1)
{
	if (isObjectNULL(this_vector, "XVector_size"))
		return 0;
	XVECTOR* vector=(XVECTOR*)this_vector;
	return XContainerObject_size(&vector->object);
}
int  XVector_capacity(const struct XVector* this_vector)//返回当前向量所能容纳的最大元素值
{
	if (isObjectNULL(this_vector, "XVector_capacity"))
		return 0;
	XVECTOR* vector=(XVECTOR*)this_vector;
	return XContainerObject_capacity(&vector->object);
}

void XVector_swap(struct XVector* this_vectorOne, struct XVector* this_vectorTwo)//交换两个同类型向量的数据
{
	if (isObjectNULL(this_vectorOne, "XVector_swap-this_vectorOne")|| isObjectNULL(this_vectorTwo, "XVector_swap-this_vectorTwo"))
		return;
	XVECTOR* vector1=(XVECTOR*)this_vectorOne;
	XVECTOR* vector2=(XVECTOR*)this_vectorTwo;
	XContainerObject_swap(&vector1->object, &vector2->object);
}
size_t XVector_TypeSize(struct XVector* this_vector)
{
	if (isObjectNULL(this_vector, "XVector_TypeSize"))
		return 0;
	XVECTOR* vector = (XVECTOR*)this_vector;
	return vector->object._type;
}

void  XVector_sort(struct XVector* this_vector, XCompare compare)//排序
{
	if (isObjectNULL(this_vector, "XVector_sort"))
		return;
	XVECTOR* vector=(XVECTOR*)this_vector;
	XQuicPitSort_Stack(vector->object._data, vector->object._size, vector->object._type, compare);
}
void* XVector_find(const struct XVector* this_vector, XEquality equality, const void* val)//查找数据，返回找到的指针，没有返回NULL
{
	if (isObjectNULL(this_vector, "XVector_find"))
		return NULL;
	XVECTOR* vector=(XVECTOR*)this_vector;
	for (int i = 0; i < vector->size(this_vector); i++)
	{
		if (equality(vector->at(this_vector, i), val))
		{
			return vector->at(this_vector, i);
		}
	}
	return NULL;
}
