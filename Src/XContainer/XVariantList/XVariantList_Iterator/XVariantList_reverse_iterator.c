#include "XVariantList_reverse_iterator.h"
#if XVariantList_ON
#include"XVariantList.h"
#include<stdio.h>
XVariantList_reverse_iterator XVariantList_rbegin(XVariantList* list)
{
	XVariantList_reverse_iterator it = { 0 };
	if (ISNULL(list, ""))
		return it;
	it.data= XVariantList_back_base(list);
	return it;
}

XVariantList_reverse_iterator XVariantList_rend(XVariantList* list)
{
	XVariantList_reverse_iterator it = { 0 };
	return it;
}

void XVariantList_reverse_iterator_add(XVariantList* list,XVariantList_reverse_iterator* it)
{
	if (ISNULL(list, "")|| ISNULL(it, ""))
		return ;
	XVariantList_reverse_iterator* front = XVariantList_front_base(list);
	if (it->data == front)//如果是第一个元素则返回空表示遍历完成了
	{
		it->data = NULL;
		return;
	}
	it->data = ((char*)(it->data)) - ((XContainerObject*)list)->m_typeSize;//指向上一个元素

}

bool XVariantList_reverse_iterator_equality(XVariantList_reverse_iterator* itFirst, XVariantList_reverse_iterator* itSecond)
{
	return itFirst->data == itSecond->data;
}

void XVariantList_reverse_iterator_for_each(XVariantList* list, XFor_each ForFunction, void* args)
{
	if (list == NULL || ForFunction == NULL)
		return;
	for_each_reverse_iterator(list, XVariantList, it)
	{
		ForFunction(XVariantList_reverse_iterator_data(&it), args);
	}
}

XVariant* XVariantList_reverse_iterator_data(XVariantList_reverse_iterator* it)
{
	if (it == NULL || it->data == NULL)
		return NULL;
	return *((XVariant**)it->data);
}

#endif