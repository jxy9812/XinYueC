#include "XVector_iterator.h"
#if XVector_ON
#include"XVector.h"
#include<stdio.h>
XVector_iterator XVector_begin(XVector* this_vector)
{
	if (XVector_isEmpty_base(this_vector))
		return XVector_end(this_vector);
	XVector_iterator it = { 0 };
	if (ISNULL(this_vector, ""))
		return it;
	//printf("开始\n");
	it.data=XContainerDataPtr(this_vector);
	return it;
}

XVector_iterator XVector_end(XVector* this_vector)
{
	XVector_iterator it = { 0 };
	return it;
}

bool XVector_iterator_isEnd(const XVector_iterator* it)
{
	return it ? (it->data == NULL) : false;
}

void XVector_iterator_add(XVector* this_vector,XVector_iterator*it)
{
	if (ISNULL(this_vector, "") || ISNULL(it, ""))
		return ;
	void*  back= XVector_back_base(this_vector);
	if (it->data == back)//如果是最后一个元素则返回空表示遍历完成了
	{
		it->data = NULL;
		return;
	}
	it->data = ((char*)(it->data)) + ((XContainerObject*)this_vector)->m_typeSize;//指向下一个元素
}

bool XVector_iterator_equality(XVector_iterator* itFirst, XVector_iterator* itSecond)
{
	return itFirst->data == itSecond->data;
}

void XVector_iterator_for_each(XVector* this_vector, XFor_each ForFunction, void* args)
{
	if (this_vector == NULL || ForFunction == NULL)
		return;
	for_each_iterator(this_vector, XVector, it)
	{
		ForFunction(XVector_iterator_data(&it), args);
	}
}

void* XVector_iterator_data(XVector_iterator* it)
{
	if (it == NULL || it->data == NULL)
		return NULL;
	return it->data;
}


#endif