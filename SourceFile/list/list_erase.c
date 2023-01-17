#include"list.h"
#include"list_head.h"
#include<stdlib.h>
//struct list;
//删除
void  List_pop_front(struct list* this_list)
{
	LIST* list=(LIST*)this_list;
	if (list->object._size == 1)
	{
		Node* head = list->object._data;
		free(head->date);
		free(head);
		list->object._data = NULL;
		list->object._size--;
		list->object._capacity--;
	}
	else if (list->object._size > 1)
	{
		Node* pfront = list->object._data;//原头节点
		Node* pback = pfront->prev;//原尾节点
		Node* pnfront = pfront->next;//新头节点
		pnfront->prev = pback;
		pback->next = pnfront;
		list->object._data = pnfront;
		free(pfront->date);
		free(pfront);
		list->object._size--;
		list->object._capacity--;
	}
}

void List_pop_back(list* this_list)
{
	LIST* list=(LIST*)this_list;
	if (list->object._size == 1)
	{
		List_pop_front(this_list);
	}
	else if (list->object._size > 1)
	{
		Node* pfront = list->object._data;//原头节点
		Node* pback = pfront->prev;//原尾节点
		Node* pnback = pback->prev;//新尾节点
		pnback->next = pfront;
		pfront->prev = pnback;
		free(pback->date);
		free(pback);//释放尾节点
		list->object._size--;
		list->object._capacity--;
	}
}

void List_erase_p(list* this_list, const Node* pNodeOne, const Node* NodeTwo)
{
	if (this_list==NULL||pNodeOne == NULL)
		return;
	Node* pNodeTwo = NodeTwo;
	if (pNodeTwo == NULL)
		pNodeTwo = pNodeOne;
	LIST* list=(LIST*)this_list;
	Node* pp = pNodeOne;
	Node* left = pNodeOne->prev;
	Node* right = pNodeTwo->next;
	for (pp = pp->next; pp != pNodeTwo; pp = pp->next )
	{
		free(pp->prev->date);
		free(pp->prev);
		list->object._size--;
		list->object._capacity--;
	}
	free(pNodeTwo->date);//
	free(pNodeTwo);//
	list->object._size--;
	list->object._capacity--;
	left->next = right;
	right->prev = left;
	if (pNodeOne == list->object._data)
	{
		list->object._data = right;
	}
}

void List_erase_int(list* this_list, const int left, const int right)
{
	LIST* list=(LIST*)this_list;
	if (right < list->object._size && left <= right && left >= 0)
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
		list->object._size -= right - left + 1;
		list->object._capacity = list->object._size;
	}
}

void List_clear(list* this_list)
{
	LIST* list=(LIST*)this_list;
	Node* p = list->object._data;
	Node* pnext = p->next;
	for (size_t i = 0; i < list->object._size; i++)
	{
		pnext = p->next;
		free(p->date);
		free(p);
		p = pnext;
	}
	list->object._size = 0;
	list->object._capacity = 0;
	list->object._data = NULL;
}