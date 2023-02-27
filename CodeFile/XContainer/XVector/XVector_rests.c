#include"XVector_iterator.h"
#include"XSort.h"
#include"XFunctionCallback.h"
#include"XVector_func.h"
#include<stdlib.h>
#include<string.h>
struct XVector;
void XVector_clear(struct XVector* this_vector)//清空vector的数组，释放内存
{
	if (isNULL(isNULLInfo(this_vector, "")))
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
	if (isNULL(isNULLInfo(this_vector, "")))
		return;
	XVector_clear(this_vector);
	free(this_vector);
}
bool XVector_empty(const struct XVector* this_vector)//检测vector内是否为空，空为真 O(1)
{
	if (isNULL(isNULLInfo(this_vector, "")))
		return true;
	return XContainerObject_empty(XVector_object(this_vector));
}
int XVector_size(const struct XVector* this_vector)//返回vector内元素的个数 O(1)
{
	if (isNULL(isNULLInfo(this_vector, "")))
		return 0;
	return XContainerObject_size(XVector_object(this_vector));
}
int  XVector_capacity(const struct XVector* this_vector)//返回当前向量所能容纳的最大元素值
{
	if (isNULL(isNULLInfo(this_vector, "")))
		return 0;
	return XContainerObject_capacity(XVector_object(this_vector));
}

void XVector_swap(struct XVector* this_vectorOne, struct XVector* this_vectorTwo)//交换两个同类型向量的数据
{
	if (isNULL(isNULLInfo(this_vectorOne, "")) || isNULL(isNULLInfo(this_vectorTwo, "")))
		return;
	XContainerObject_swap(XVector_object(this_vectorOne), XVector_object(this_vectorTwo));
}
size_t XVector_TypeSize(struct XVector* this_vector)
{
	if (isNULL(isNULLInfo(this_vector, "")))
		return 0;
	return XVector_object(this_vector)->_type;
}

void  XVector_sort(struct XVector* this_vector, XCompare compare)//排序
{
	if (isNULL(isNULLInfo(this_vector, "")))
		return;
	XQuicPitSort_Stack(XVector_object(this_vector)->_data, XVector_size(this_vector),XVector_TypeSize(this_vector),compare);
}
void* XVector_find(const struct XVector* this_vector, XEquality equality, const void* val)//查找数据，返回找到的指针，没有返回NULL
{
	if (isNULL(isNULLInfo(this_vector, "")))
		return NULL;
	for (XVector_iterator* it = XVector_begin(this_vector); it != XVector_end(this_vector); it = XVector_iterator_add(this_vector, it))
	{
		if (equality(it, val))
			return it;
	}
	return NULL;
}
void XVector_resize(struct XVector* this_vector, const size_t size)
{
	size_t capacity = XVector_capacity(this_vector);//当前容器的最大数量
	size_t count= XVector_size(this_vector);//当前容器使用的数量
	size_t TypeSize = XVector_TypeSize(this_vector);//数据类型大小
	XContainerObject* object = XVector_object(this_vector);//数据父类
	if (size <= count)
	{
		for (size_t i = 0; i < count-size; i++)
		{
			XVector_pop_back(this_vector);
		}
		return;
	}

	if (size > capacity)//大于最大容量
	{
		object->_data = realloc(object->_data, size * TypeSize);
		if (object->_data == NULL)
		{
			perror("扩容失败vector");
			exit(-1);
		}
		object->_capacity = size;
	}

	char* LPstart = ((char*)(object->_data)) + count * TypeSize;//最后一个元素的下一个元素
	memset(LPstart, 0, (size - count) * TypeSize);
	object->_size = size;//设置当前容器元素数量
	return;
}
