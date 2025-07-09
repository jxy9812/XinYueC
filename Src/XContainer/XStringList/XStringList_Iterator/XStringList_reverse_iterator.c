#include "XStringList_reverse_iterator.h"
#if XStringList_ON
#include"XString.h"
#include<stdio.h>
XStringList_reverse_iterator XStringList_rbegin(XStringList* this_XStringList)
{
	return XVector_rbegin(this_XStringList);
}

XStringList_reverse_iterator XStringList_rend(XStringList* this_XStringList)
{
	return XVector_rend(this_XStringList);
}

void XStringList_reverse_iterator_add(XStringList* this_XStringList, XStringList_reverse_iterator* it)
{
	if (ISNULL(this_XStringList, "") || ISNULL(it, ""))
		return;
	XVector_reverse_iterator* front = XVtableGetFunc(XVector_class_init(), EXVector_Front, void* (*)(XVector*))(this_XStringList);
	if (it->data == front)//如果是第一个元素则返回空表示遍历完成了
	{
		it->data = NULL;
		return;
	}
	it->data = ((char*)(it->data)) - ((XContainerObject*)this_XStringList)->m_typeSize;//指向上一个元素
}

bool XStringList_reverse_iterator_equality(XStringList_reverse_iterator* itFirst, XStringList_reverse_iterator* itSecond)
{
	return XVector_reverse_iterator_equality(itFirst,itSecond);
}

void XStringList_reverse_iterator_for_each(XStringList* this_XStringList, XFor_each ForFunction, void* args)
{
	if (this_XStringList == NULL || ForFunction == NULL)
		return;
	for_each_reverse_iterator(this_XStringList, XVector, it)
	{
		ForFunction(*((XString**)XVector_reverse_iterator_data(&it)), args);
	}
}

void* XStringList_reverse_iterator_data(XStringList_reverse_iterator* it)
{
	return XVector_reverse_iterator_data(it);
}

#endif

