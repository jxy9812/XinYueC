#include"XList.h"
#include"XList_head.h"
#include<string.h>
//遍历
XListNode* XList_at(const XList* this_list, int n)
{
	if (isObjectNULL(this_list, "List_at"))
		return NULL;
	XLIST* list=(XLIST*)this_list;
	if (n >= 0 && n <= (list->object._size / 2))//向后找
	{
		XListNode* p = (XListNode*)list->object._data;
		if (n == 0)
		{
			return p;
		}
		for (size_t j = 0; j < n; j++)
		{
			p = p->next;
		}
		return p;
	}
	else if (n > (list->object._size / 2) && n < list->object._size)//向前找
	{
		XListNode* p = list->object._data;
		for (size_t i = 0; i < list->object._size - n; i++)
		{
			p = p->prev;
		}
		return p;
	}
	return NULL;
}

XListNode* XList_front(XList* this_list)
{
	if (isObjectNULL(this_list, "List_front"))
		return NULL;
	XLIST* list=(XLIST*)this_list;
	return list->object._data;
}

XListNode* XList_back(XList* this_list)
{
	if (isObjectNULL(this_list, "List_back"))
		return NULL;
	XLIST* list=(XLIST*)this_list;
	return ((XListNode*)(list->object._data))->prev;
}

XListNode* XList_find(const XList* this_list, XEquality equality, const void* findVal)
{
	if (isObjectNULL(this_list, "List_find"))
		return NULL;
	for (XList_iterator* it = XList_begin(this_list); it != XList_end(this_list); it = XList_iterator_add(this_list, it))
	{
		if (equality(((XListNode*)it)->date, findVal))
			return it;
	}
	return NULL;
}
