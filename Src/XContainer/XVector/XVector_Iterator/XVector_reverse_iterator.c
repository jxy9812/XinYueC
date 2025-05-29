#include "XVector_reverse_iterator.h"
#if XVector_ON
#include"XVector.h"
#include<stdio.h>
XVector_reverse_iterator* XVector_rbegin(XVector* this_vector)
{
	if (ISNULL(this_vector, ""))
		return NULL;
	return XVector_back_base(this_vector);
}

XVector_reverse_iterator* XVector_rend(XVector* this_vector)
{
	return NULL;
}

XVector_reverse_iterator* XVector_reverse_iterator_add(XVector* this_vector,XVector_reverse_iterator* it)
{
	if (ISNULL(this_vector, "")|| ISNULL(it, ""))
		return NULL;
	XVector_reverse_iterator* back = XVector_front_base(this_vector);
	if (it == back)//如果是第一个元素则返回空表示遍历完成了
		return NULL;
	return (char*)it - this_vector->m_parent.m_typeSize;//指向上一个元素
}

void XVector_reverse_iterator_for_each(XVector* this_vector, XFor_each ForFunction, void* args)
{
	for (XVector_reverse_iterator* it = XVector_rbegin(this_vector); it != XVector_rend(this_vector); it = XVector_reverse_iterator_add(this_vector, it))
	{
		ForFunction(it, args);
	}
}

#endif