#include"list.h"
#include"list_head.h"
#include<stdlib.h>
#include<string.h>
#include <stdarg.h> 
//插入
Node* List_push_front(list* li, void* x)
{
	LIST* list=(LIST*)li;
	Node* p = List_push_back(li, x);
	if (list->_current != 0)
	{
		list->_date = p;
	}
	return p;
}

Node* List_push_back(list* li, void* x)
{
	LIST* list=(LIST*)li;
	Node* p = malloc(sizeof(Node));//开辟节点
	if (p == NULL)
	{
		perror("开辟节点失败");
		exit(-1);
	}
	p->date = malloc(list->_type);//开辟节点内储存数据的空间
	memcpy(p->date, x, list->_type);//拷贝数据
	if (list->_current == 0)
	{
		list->_date = p;
		p->next = p;
		p->prev = p;
	}
	else
	{
		Node* pfront = list->_date;//原头节点
		Node* pback = pfront->prev;//原尾节点
		p->next = pfront;
		p->prev = pback;
		pfront->prev = p;
		pback->next = p;
	}
	list->_current++;
	return p;
}

void List_insert_front_p(list* li, Node* pval, ...)
{
	LIST* list=(LIST*)li;
	if (pval == NULL)
	{
		printf("节点指针不能为空\n");
		return ;
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
			pk->date = malloc(list->_type);//开辟节点内储存数据的空间
			memcpy(pk->date, x, list->_type);//拷贝数据

			pk->prev = left;
			pk->next = pval;
			left->next = pk;
			pval->prev = pk;

			if (pval == list->_date)
			{
				list->_date = pk;
			}
			list->_current++;
		}
		else
		{
			perror("插入的数找不到");
		}
	}
}

void List_insert_front_int(list* li, int i, ...)
{
	LIST* list=(LIST*)li;
	if ((i < 0) && (list->_current <= i))
	{
		printf("输入的下标不在范围内\n");
		return ;
	}
	va_list args;//接收可变参数，
	va_start(args, i);
	void* x = va_arg(args, void*);//依次访问参数，需指定按照什么类型读取数据  
	int n = va_arg(args, int);
	va_end(args);//参数使用结束  
	if (n > 1000 || n <= 0)//一次调用最多插入1000个
		n = 1;
	Node* p = list->at(li, i);
	list->insert_front_p(li, p, x, n);
}

void List_insert(list* li, Node* pval, const void* p1, const void* p2)
{
	LIST* list=(LIST*)li;
	if (pval == NULL)
	{
		printf("节点指针不能为空\n");
		return ;
	}
	for (size_t i = 0; i < ((char*)p2 - (char*)p1) / list->_type + 1; i++)
	{
		List_insert_front_p(li, pval, (char*)p1 + i * list->_type);
	}
}