#include"list.h"
#include"list_head.h"
#include<stdlib.h>
#include <stdarg.h> 
//插入
Node* List_push_front(LIST* li, void* x)
{
	Node* p = List_push_back(li, x);
	if (li->_current != 0)
	{
		li->_date = p;
	}
	return p;
}

Node* List_push_back(LIST* li, void* x)
{
	Node* p = malloc(sizeof(Node));//开辟节点
	if (p == NULL)
	{
		perror("开辟节点失败");
		exit(-1);
	}
	p->date = malloc(li->_type);//开辟节点内储存数据的空间
	memcpy(p->date, x, li->_type);//拷贝数据
	if (li->_current == 0)
	{
		li->_date = p;
		p->next = p;
		p->prev = p;
	}
	else
	{
		Node* pfront = li->_date;//原头节点
		Node* pback = pfront->prev;//原尾节点
		p->next = pfront;
		p->prev = pback;
		pfront->prev = p;
		pback->next = p;
	}
	li->_current++;
	return p;
}

void List_insert_front_p(LIST* li, Node* pval, ...)
{
	if (pval == NULL)
	{
		printf("节点指针不能为空\n");
		return -1;
	}
	va_list args;//接收可变参数，
	va_start(args, pval);
	void* x = va_arg(args, void*);//依次访问参数，需指定按照什么类型读取数据  
	int n = va_arg(args, int);
	va_end(args);//参数使用结束  
	if (n > 1000 || n <= 0)//一次调用最多插入1000个
		n = 1;

	for (size_t i = 0; i < n; i++)
	{
		/*Node* pval = List_find(li, p);*/
		if (pval != NULL)
		{
			Node* left = pval->prev;
			//OneList* right = pval->next;

			Node* pk = malloc(sizeof(Node));//开辟节点
			if (pk == NULL)
			{
				perror("开辟节点失败");
				exit(-1);
			}
			pk->date = malloc(li->_type);//开辟节点内储存数据的空间
			memcpy(pk->date, x, li->_type);//拷贝数据

			pk->prev = left;
			pk->next = pval;
			left->next = pk;
			pval->prev = pk;

			if (pval == li->_date)
			{
				li->_date = pk;
			}
			li->_current++;
		}
		else
		{
			perror("插入的数找不到");
		}
	}
}

void List_insert_front_int(LIST* li, int i, ...)
{
	if ((i < 0) && (li->_current <= i))
	{
		printf("输入的下标不在范围内\n");
		return -1;
	}
	va_list args;//接收可变参数，
	va_start(args, i);
	void* x = va_arg(args, void*);//依次访问参数，需指定按照什么类型读取数据  
	int n = va_arg(args, int);
	va_end(args);//参数使用结束  
	if (n > 1000 || n <= 0)//一次调用最多插入1000个
		n = 1;
	Node* p = li->at(li, i);
	li->insert_front_p(li, p, x, n);
}

void List_insert(LIST* li, Node* pval, const void* p1, const void* p2)
{
	if (pval == NULL)
	{
		printf("节点指针不能为空\n");
		return -1;
	}
	for (size_t i = 0; i < ((char*)p2 - (char*)p1) / li->_type + 1; i++)
	{
		List_insert_front_p(li, pval, (char*)p1 + i * li->_type);
	}
}