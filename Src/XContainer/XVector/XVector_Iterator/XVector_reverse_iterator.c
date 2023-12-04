#include "XVector_reverse_iterator.h"
#include"XVector.h"
#include<stdio.h>
struct XVector_reverse_iterator* XVector_rbegin(struct XVector* this_vector)
{
	if (ISNULL(this_vector, ""))
		return NULL;
	return XVector_back(this_vector);
}

struct XVector_reverse_iterator* XVector_rend(struct XVector* this_vector)
{
	return NULL;
}

struct XVector_reverse_iterator* XVector_reverse_iterator_add(struct XVector* this_vector, struct XVector_reverse_iterator* it)
{
	if (ISNULL(this_vector, "")|| ISNULL(it, ""))
		return NULL;
	XVector_reverse_iterator* back = XVector_front(this_vector);
	if (it == back)//如果是第一个元素则返回空表示遍历完成了
		return NULL;
	return (char*)it - this_vector->object._typeSize;//指向上一个元素
}

void XVector_reverse_iterator_for_each(struct XVector* this_vector, XFor_each ForFunction, void* args)
{
	for (XVector_reverse_iterator* it = XVector_rbegin(this_vector); it != XVector_rend(this_vector); it = XVector_reverse_iterator_add(this_vector, it))
	{
		ForFunction(it, args);
	}
}