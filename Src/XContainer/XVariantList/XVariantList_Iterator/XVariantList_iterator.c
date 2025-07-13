#include "XVariantList_iterator.h"
#if XVariantList_ON
#include"XVariantList.h"
#include<stdio.h>
XVariantList_iterator XVariantList_begin(XVariantList* list)
{
	if (XVariantList_isEmpty_base(list))
		return XVariantList_end(list);
	XVariantList_iterator it = { 0 };
	if (ISNULL(list, ""))
		return it;
	//printf("开始\n");
	it.data=XContainerDataPtr(list);
	return it;
}

XVariantList_iterator XVariantList_end(XVariantList* list)
{
	XVariantList_iterator it = { 0 };
	return it;
}

void XVariantList_iterator_add(XVariantList* list,XVariantList_iterator*it)
{
	if (ISNULL(list, "") || ISNULL(it, ""))
		return ;
	XVariant* back= XVariantList_back_base(list);
	if (it->data != NULL && *((XVariant**)it->data) == back)//如果是最后一个元素则返回空表示遍历完成了
	{
		it->data = NULL;
		return;
	}
	it->data = ((char*)(it->data)) + ((XContainerObject*)list)->m_typeSize;//指向下一个元素
}

bool XVariantList_iterator_equality(XVariantList_iterator* itFirst, XVariantList_iterator* itSecond)
{
	return itFirst->data == itSecond->data;
}

void XVariantList_iterator_for_each(XVariantList* list, XFor_each ForFunction, void* args)
{
	if (list == NULL || ForFunction == NULL)
		return;
	for_each_iterator(list, XVariantList, it)
	{
		ForFunction(XVariantList_iterator_data(&it), args);
	}
}

XVariant* XVariantList_iterator_data(XVariantList_iterator* it)
{
	if (it == NULL || it->data == NULL)
		return NULL;
	return *((XVariant**)it->data);
}


#endif