#include "XVector_iterator.h"
#if XVector_ON
#include"XVector.h"
#include<stdio.h>
XVector_iterator* XVector_begin(XVector* this_vector)
{
	if (ISNULL(this_vector, ""))
		return NULL;
	return XVector_front_base(this_vector);
}

XVector_iterator* XVector_end(XVector* this_vector)
{
	return NULL;
}

XVector_iterator* XVector_iterator_add(XVector* this_vector,XVector_iterator*it)
{
	if (ISNULL(this_vector, "") || ISNULL(it, ""))
		return NULL;
	XVector_iterator*  back= XVector_back_base(this_vector);
	if(it== back)//如果是最后一个元素则返回空表示遍历完成了
		return NULL;
	return (char*)it + this_vector->m_parent.m_typeSize;//指向下一个元素
}

void XVector_iterator_for_each(XVector* this_vector, XFor_each ForFunction, void* args)
{
	for_each_iterator(this_vector, XVector, it)
	{
		ForFunction(it, args);
	}
}


#endif