#include"XVector_iterator.h"
#include"XSort.h"
#include"XFunctionCallback.h"
#include"XVector_func.h"
#include<stdlib.h>
#include<string.h>
struct XVector;
void XVector_clear(struct XVector* this_vector)//清空vector的数组，释放内存
{
	if (isObjectNULL(this_vector, "XVector_clear"))
		return;
	struct XContainerObject*  object = XVector_object(this_vector);
	if (object->_data != NULL)
	{
		free(object->_data);
		object->_data = NULL;
		object->_capacity = 0;
		object->_size = 0;
	}
}
void XVector_free(const struct XVector* this_vector)//释放内存
{
	if (isObjectNULL(this_vector, "XVector_free"))
		return;
	XVector_clear(this_vector);
	free(this_vector);
}
bool XVector_empty(const struct XVector* this_vector)//检测vector内是否为空，空为真 O(1)
{
	if (isObjectNULL(this_vector, "XVector_empty"))
		return true;
	return XContainerObject_empty(XVector_object(this_vector));
}
int XVector_size(const struct XVector* this_vector)//返回vector内元素的个数 O(1)
{
	if (isObjectNULL(this_vector, "XVector_size"))
		return 0;
	return XContainerObject_size(XVector_object(this_vector));
}
int  XVector_capacity(const struct XVector* this_vector)//返回当前向量所能容纳的最大元素值
{
	if (isObjectNULL(this_vector, "XVector_capacity"))
		return 0;
	return XContainerObject_capacity(XVector_object(this_vector));
}

void XVector_swap(struct XVector* this_vectorOne, struct XVector* this_vectorTwo)//交换两个同类型向量的数据
{
	if (isObjectNULL(this_vectorOne, "XVector_swap-this_vectorOne")|| isObjectNULL(this_vectorTwo, "XVector_swap-this_vectorTwo"))
		return;
	XContainerObject_swap(XVector_object(this_vectorOne), XVector_object(this_vectorTwo));
}
size_t XVector_TypeSize(struct XVector* this_vector)
{
	if (isObjectNULL(this_vector, "XVector_TypeSize"))
		return 0;
	return XVector_object(this_vector)->_type;
}

void  XVector_sort(struct XVector* this_vector, XCompare compare)//排序
{
	if (isObjectNULL(this_vector, "XVector_sort"))
		return;
	XQuicPitSort_Stack(XVector_object(this_vector)->_data, XVector_size(this_vector),XVector_TypeSize(this_vector),compare);
}
void* XVector_find(const struct XVector* this_vector, XEquality equality, const void* val)//查找数据，返回找到的指针，没有返回NULL
{
	if (isObjectNULL(this_vector, "XVector_find"))
		return NULL;
	for (XVector_iterator* it = XVector_begin(this_vector); it != XVector_end(this_vector); it = XVector_iterator_add(this_vector, it))
	{
		if (equality(it, val))
			return it;
	}
	return NULL;
}
