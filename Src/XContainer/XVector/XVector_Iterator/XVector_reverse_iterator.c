#include "XVector_reverse_iterator.h"
#if XVector_ON
#include"XVector.h"
#include<stdio.h>
XVector_reverse_iterator XVector_rbegin(XVector* this_vector)
{
	XVector_reverse_iterator it = { 0 };
	if (ISNULL(this_vector, ""))
		return it;
	it.data= XVector_back_base(this_vector);
	return it;
}

XVector_reverse_iterator XVector_rend(XVector* this_vector)
{
	XVector_reverse_iterator it = { 0 };
	return it;
}

bool XVector_reverse_iterator_isRend(const XVector_reverse_iterator* it)
{
	return it ? (it->data == NULL) : false;
}

void XVector_reverse_iterator_add(XVector* this_vector,XVector_reverse_iterator* it)
{
	if (ISNULL(this_vector, "")|| ISNULL(it, ""))
		return ;
	XVector_reverse_iterator* front = XVector_front_base(this_vector);
	if (it->data == front)//如果是第一个元素则返回空表示遍历完成了
	{
		it->data = NULL;
		return;
	}
	it->data = ((char*)(it->data)) - ((XContainerObject*)this_vector)->m_typeSize;//指向上一个元素

}

bool XVector_reverse_iterator_equality(XVector_reverse_iterator* itFirst, XVector_reverse_iterator* itSecond)
{
	return itFirst->data == itSecond->data;
}

void XVector_reverse_iterator_for_each(XVector* this_vector, XFor_each ForFunction, void* args)
{
	if (this_vector == NULL || ForFunction == NULL)
		return;
	for_each_reverse_iterator(this_vector, XVector, it)
	{
		ForFunction(XVector_reverse_iterator_data(&it), args);
	}
}

void* XVector_reverse_iterator_data(XVector_reverse_iterator* it)
{
	if (it == NULL || it->data == NULL)
		return NULL;
	return it->data;
}

#endif