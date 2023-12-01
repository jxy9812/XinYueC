#include"XList.h"
#include<string.h>
//遍历
XListNode* XList_front(XList* this_list)
{
	if (isNULL(isNULLInfo(this_list, "")))
		return NULL;
	XList* list = this_list;
	return list->object._data;
}

XListNode* XList_back(XList* this_list)
{
	if (isNULL(isNULLInfo(this_list, "")))
		return NULL;
	XList* list = this_list;
	return ((XListNode*)(list->object._data))->prev;
}

XListNode* XList_find(const XList* this_list, XEquality equality, const void* findVal)
{
	if (isNULL(isNULLInfo(this_list, "")))
		return NULL;
	for (XList_iterator* it = XList_begin(this_list); it != XList_end(this_list); it = XList_iterator_add(this_list, it))
	{
		if (equality(((XListNode*)it)->date, findVal))
			return it;
	}
	return NULL;
}
