#include "XStringVector_reverse_iterator.h"
#if XStringVector_ON
#include"XString.h"
#include<stdio.h>
XStringVector_reverse_iterator XStringVector_rbegin(XStringVector* this_XStringVector)
{
	return XVector_rbegin(this_XStringVector);
}

XStringVector_reverse_iterator XStringVector_rend(XStringVector* this_XStringVector)
{
	return XVector_rend(this_XStringVector);
}

void XStringVector_reverse_iterator_add(XStringVector* this_XStringVector, XStringVector_reverse_iterator* it)
{
	if (ISNULL(this_XStringVector, "") || ISNULL(it, ""))
		return;
	XVector_reverse_iterator* front = XVtableGetFunc(XVector_class_init(), EXVector_Front, void* (*)(XVector*))(this_XStringVector);
	if (it->data == front)//如果是第一个元素则返回空表示遍历完成了
	{
		it->data = NULL;
		return;
	}
	it->data = ((char*)(it->data)) - ((XContainerObject*)this_XStringVector)->m_typeSize;//指向上一个元素
}

bool XStringVector_reverse_iterator_equality(XStringVector_reverse_iterator* itFirst, XStringVector_reverse_iterator* itSecond)
{
	return XVector_reverse_iterator_equality(itFirst,itSecond);
}

void XStringVector_reverse_iterator_for_each(XStringVector* this_XStringVector, XFor_each ForFunction, void* args)
{
	if (this_XStringVector == NULL || ForFunction == NULL)
		return;
	for_each_reverse_iterator(this_XStringVector, XVector, it)
	{
		ForFunction(*((XString**)XVector_reverse_iterator_data(&it)), args);
	}
}

void* XStringVector_reverse_iterator_data(XStringVector_reverse_iterator* it)
{
	return XVector_reverse_iterator_data(it);
}

#endif

