#include"list.h"
#include"list_head.h"
//遍历
Node* List_at(const LIST* li, int n)
{
	if (n >= 0 && n <= (li->_current / 2))//向后找
	{
		Node* p = li->_date;
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
	else if (n > (li->_current / 2) && n < li->_current)//向前找
	{
		Node* p = li->_date;
		for (size_t i = 0; i < li->_current - n; i++)
		{
			p = p->prev;
		}
		return p;
	}
	return NULL;
}

Node* List_front(LIST* li)
{
	return li->_date;
}

Node* List_back(LIST* li)
{
	return li->_date->prev;
}

Node* List_find(const LIST* li, const void* val)
{
	Node* p = li->_date;
	for (size_t i = 0; i < li->_current; i++)
	{
		if (memcmp(p->date, val, li->_type) == 0)
		{
			return p;
		}
		p = p->next;
	}
	return NULL;
}
