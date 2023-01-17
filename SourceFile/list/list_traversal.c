#include"list.h"
#include"list_head.h"
#include<string.h>
//遍历
Node* List_at(const list* this_list, int n)
{
	LIST* list=(LIST*)this_list;
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

Node* List_front(list* this_list)
{
	LIST* list=(LIST*)this_list;
	return list->object._data;
}

Node* List_back(list* this_list)
{
	LIST* list=(LIST*)this_list;
	return ((Node*)(list->object._data))->prev;
}

Node* List_find(const list* this_list, const void* val)
{
	LIST* list=(LIST*)this_list;
	Node* p = list->object._data;
	for (size_t i = 0; i < list->object._size; i++)
	{
		if (memcmp(p->date, val, list->object._type) == 0)
		{
			return p;
		}
		p = p->next;
	}
	return NULL;
}
