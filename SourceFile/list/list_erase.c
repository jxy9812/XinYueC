#include"list.h"
#include"list_head.h"
//删除
void List_pop_front(LIST* li)
{
	if (li->_current == 1)
	{
		Node* head = li->_date;
		free(head->date);
		free(head);
		li->_date = NULL;
		li->_current--;
	}
	else if (li->_current > 1)
	{
		Node* pfront = li->_date;//原头节点
		Node* pback = pfront->prev;//原尾节点
		Node* pnfront = pfront->next;//新头节点
		pnfront->prev = pback;
		pback->next = pnfront;
		li->_date = pnfront;
		free(pfront->date);
		free(pfront);
		li->_current--;
	}
}

void List_pop_back(LIST* li)
{
	if (li->_current == 1)
	{
		Node* head = li->_date;
		free(head->date);
		free(head);
		li->_date = NULL;
		li->_current--;
	}
	else if (li->_current > 1)
	{
		Node* pfront = li->_date;//原头节点
		Node* pback = pfront->prev;//原尾节点
		Node* pnback = pback->prev;//新尾节点
		pnback->next = pfront;
		pfront->prev = pnback;
		free(pback->date);
		free(pback);//释放尾节点
		li->_current--;
	}
}

void List_erase_p(LIST* li, const Node* p1, const Node* p2)
{
	Node* pp = p1;
	Node* left = p1->prev;
	Node* right = p2->next;
	for (; pp != p2; )
	{
		pp = pp->next;
		free(pp->prev->date);
		free(pp->prev);
		li->_current--;
	}
	free(pp);//释放p2O
	li->_current--;
	left->next = right;
	right->prev = left;
	if (p1 == li->_date)
	{
		li->_date = right;
	}
}

void List_erase_int(LIST* li, const int left, const int right)
{
	if (right < li->_current && left <= right && left >= 0)
	{
		Node* p = List_at(li, left);//left的节点指针
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
		li->_current -= right - left + 1;
	}
}

void List_clear(LIST* li)
{
	Node* p = li->_date;
	Node* pnext = p->next;
	for (size_t i = 0; i < li->_current; i++)
	{
		pnext = p->next;
		free(p->date);
		free(p);
		p = pnext;
	}
	li->_current = 0;
	li->_date = NULL;
}