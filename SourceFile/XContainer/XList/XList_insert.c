#include"XList.h"
#include"XList_head.h"
#include<stdlib.h>
#include<string.h>
#include <stdarg.h> 
//插入
XListNode* XList_push_front(XList* this_list, void*LValue)
{
	if (isNULL(isNULLInfo(this_list, "")))
		return NULL;
	XLIST* list=(XLIST*)this_list;
	XListNode* NewNode = XList_push_back(this_list, LValue);
	if (list->object._size != 0)
	{
		list->object._data = NewNode;
	}
	return NewNode;
}

XListNode* XList_push_back (XList* this_list, void*LValue)
{
	if (isNULL(isNULLInfo(this_list, "")))
		return NULL;
	XLIST* list=(XLIST*)this_list;
	XListNode* NewNode = malloc(sizeof(XListNode));//新节点
	if (NewNode == NULL)
	{
		perror("开辟节点失败");
		exit(-1);
	}
	NewNode->date = malloc(list->object._type);//开辟节点内储存数据的空间
	memcpy(NewNode->date, LValue, list->object._type);//拷贝数据
	if (list->object._size == 0)
	{
		list->object._data = NewNode;
		NewNode->next = NewNode;
		NewNode->prev = NewNode;
	}
	else
	{
		XListNode* pfront = list->object._data;//原头节点
		XListNode* pback = pfront->prev;//原尾节点
		NewNode->next = pfront;
		NewNode->prev = pback;
		pfront->prev = NewNode;
		pback->next = NewNode;
	}
	list->object._size++;
	list->object._capacity++;
	return NewNode;
}

void XList_insert_front_p(XList* this_list, XListNode* pval, ...)
{
	if (isNULL(isNULLInfo(this_list, "")))
		return ;
	XLIST* list=(XLIST*)this_list;
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
			XListNode* left = pval->prev;
			//OneList* right = pval->next;

			XListNode* pk = malloc(sizeof(XListNode));//开辟节点
			if (pk == NULL)
			{
				perror("开辟节点失败");
				exit(-1);
			}
			pk->date = malloc(list->object._type);//开辟节点内储存数据的空间
			memcpy(pk->date, x, list->object._type);//拷贝数据

			pk->prev = left;
			pk->next = pval;
			left->next = pk;
			pval->prev = pk;

			if (pval == list->object._data)
			{
				list->object._data = pk;
			}
			list->object._size++;
			list->object._capacity++;
		}
		else
		{
			perror("插入的数找不到");
		}
	}
}

void XList_insert_front_int(XList* this_list, int i, ...)
{
	if (isNULL(isNULLInfo(this_list, "")))
		return;
	XLIST* list=(XLIST*)this_list;
	if ((i < 0) && (list->object._size <= i))
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
	XListNode* p = list->at(this_list, i);
	list->insert_front_p(this_list, p, x, n);
}

void XList_insert(XList* this_list, XListNode* pval, const void* p1, const void* p2)
{
	if (isNULL(isNULLInfo(this_list, "")))
		return;
	XLIST* list=(XLIST*)this_list;
	if (pval == NULL)
	{
		printf("节点指针不能为空\n");
		return ;
	}
	for (size_t i = 0; i < ((char*)p2 - (char*)p1) / list->object._type + 1; i++)
	{
		XList_insert_front_p(this_list, pval, (char*)p1 + i * list->object._type);
	}
}