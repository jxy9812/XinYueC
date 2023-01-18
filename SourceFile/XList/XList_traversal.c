#include"XList.h"
#include"XList_head.h"
#include<string.h>
//遍历
Node* List_at(const XList* this_list, int n)
{
	XLIST* list=(XLIST*)this_list;
	if (n >= 0 && n <= (list->object._size / 2))//向后找
	{
		Node* p = (Node*)list->object._data;
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
		Node* p = list->object._data;
		for (size_t i = 0; i < list->object._size - n; i++)
		{
			p = p->prev;
		}
		return p;
	}
	return NULL;
}

Node* List_front(XList* this_list)
{
	XLIST* list=(XLIST*)this_list;
	return list->object._data;
}

Node* List_back(XList* this_list)
{
	XLIST* list=(XLIST*)this_list;
	return ((Node*)(list->object._data))->prev;
}

Node* List_find(const XList* this_list, bool (*find)(const struct Node* node, const void* val), const void* findVal)
{
	XLIST* list=(XLIST*)this_list;
	Node* pNode = list->object._data;
	for (size_t i = 0; i < list->object._size; i++)
	{
		if (find(pNode, findVal))
		{
			return pNode;
		}
		pNode = pNode->next;
	}
	return NULL;
}
