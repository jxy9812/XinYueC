#include"list.h"
#include"list_head.h"
#include<stdlib.h>
//struct list;
//删除
void  List_pop_front(struct list* li)
{
	LIST* list=(LIST*)li;
	if (list->_current == 1)
	{
		Node* head = list->_data;
		free(head->date);
		free(head);
		list->_data = NULL;
		list->_current--;
	}
	else if (list->_current > 1)
	{
		Node* pfront = list->_data;//原头节点
		Node* pback = pfront->prev;//原尾节点
		Node* pnfront = pfront->next;//新头节点
		pnfront->prev = pback;
		pback->next = pnfront;
		list->_data = pnfront;
		free(pfront->date);
		free(pfront);
		list->_current--;
	}
}

void List_pop_back(list* li)
{
	LIST* list=(LIST*)li;
	if (list->_current == 1)
	{
		Node* head = list->_data;
		free(head->date);
		free(head);
		list->_data = NULL;
		list->_current--;
	}
	else if (list->_current > 1)
	{
		Node* pfront = list->_data;//原头节点
		Node* pback = pfront->prev;//原尾节点
		Node* pnback = pback->prev;//新尾节点
		pnback->next = pfront;
		pfront->prev = pnback;
		free(pback->date);
		free(pback);//释放尾节点
		list->_current--;
	}
}

void List_erase_p(list* li, const Node* p1, const Node* p2)
{
	LIST* list=(LIST*)li;
	Node* pp = p1;
	Node* left = p1->prev;
	Node* right = p2->next;
	for (; pp != p2; )
	{
		pp = pp->next;
		free(pp->prev->date);
		free(pp->prev);
		list->_current--;
	}
	free(pp);//释放p2O
	list->_current--;
	left->next = right;
	right->prev = left;
	if (p1 == list->_data)
	{
		list->_data = right;
	}
}

void List_erase_int(list* li, const int left, const int right)
{
	LIST* list=(LIST*)li;
	if (right < list->_current && left <= right && left >= 0)
	{
		Node* p = List_at(list, left);//left的节点指针
		Node* prev = p->prev;//上一个节点
		Node* pleft = p;
		for (size_t i = 0; i <= right - left; i++)
		{
			p = p->next;
			free(pleft->date);
			free(pleft);
			pleft = p;
		}
		Node* pnext = p;//right的下一个节点
		prev->next = pnext;
		pnext->prev = prev;
		list->_current -= right - left + 1;
	}
}

void List_clear(list* li)
{
	LIST* list=(LIST*)li;
	Node* p = list->_data;
	Node* pnext = p->next;
	for (size_t i = 0; i < list->_current; i++)
	{
		pnext = p->next;
		free(p->date);
		free(p);
		p = pnext;
	}
	list->_current = 0;
	list->_data = NULL;
}