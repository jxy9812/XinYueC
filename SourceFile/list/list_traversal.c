#include"list.h"
#include"list_head.h"
#include<string.h>
//遍历
Node* List_at(const list* li, int n)
{
	LIST* list=(LIST*)li;
	if (n >= 0 && n <= (list->_current / 2))//向后找
	{
		Node* p = list->_date;
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
	else if (n > (list->_current / 2) && n < list->_current)//向前找
	{
		Node* p = list->_date;
		for (size_t i = 0; i < list->_current - n; i++)
		{
			p = p->prev;
		}
		return p;
	}
	return NULL;
}

Node* List_front(list* li)
{
	LIST* list=(LIST*)li;
	return list->_date;
}

Node* List_back(list* li)
{
	LIST* list=(LIST*)li;
	return list->_date->prev;
}

Node* List_find(const list* li, const void* val)
{
	LIST* list=(LIST*)li;
	Node* p = list->_date;
	for (size_t i = 0; i < list->_current; i++)
	{
		if (memcmp(p->date, val, list->_type) == 0)
		{
			return p;
		}
		p = p->next;
	}
	return NULL;
}
