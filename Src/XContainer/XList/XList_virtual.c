#include"XList_virtual.h"
#include"XList.h"
#include"stdlib.h"
#include<stdarg.h> 
XListNode* XVList_push_front(XList* this_list, void* LPValue)
{
	if (ISNULL(this_list, ""))
		return NULL;
	XList* list = this_list;
	XListNode* NewNode = XList_push_back(this_list, LPValue);
	if (list->object._size != 0)
	{
		list->object._data = NewNode;
	}
	return NewNode;
}

XListNode* XVList_push_back(XList* this_list, void* LPValue)
{
	if (ISNULL(this_list, ""))
		return NULL;
	XList* list = this_list;
	XListNode* NewNode = malloc(sizeof(XListNode));//新节点
	if (NewNode == NULL)
	{
		perror("开辟节点失败");
		exit(-1);
	}
	NewNode->date = malloc(list->object._type);//开辟节点内储存数据的空间
	memcpy(NewNode->date, LPValue, list->object._type);//拷贝数据
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

void XVList_insert_front_p(XList* this_list, XListNode* pval, ...)
{
	if (ISNULL(this_list, ""))
		return;
	XList* list = this_list;
	if (pval == NULL)
	{
		printf("节点指针不能为空\n");
		return;
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

void XVList_insert_front_int(XList* this_list, int i, ...)
{
	if (ISNULL(this_list, ""))
		return;
	XList* list = this_list;
	if ((i < 0) && (list->object._size <= i))
	{
		printf("输入的下标不在范围内\n");
		return;
	}
	va_list args;//接收可变参数，
	va_start(args, i);
	void* x = va_arg(args, void*);//依次访问参数，需指定按照什么类型读取数据  
	int n = va_arg(args, int);
	va_end(args);//参数使用结束  
	if (n > 1000 || n <= 0)//一次调用最多插入1000个
		n = 1;
	/*XListNode* p = list->at(this_list, i);
	list->insert_front_p(this_list, p, x, n);*/
}

void XVList_insert(XList* this_list, XListNode* pval, const void* p1, const void* p2)
{
	if (ISNULL(this_list, ""))
		return;
	XList* list = this_list;
	if (pval == NULL)
	{
		printf("节点指针不能为空\n");
		return;
	}
	for (size_t i = 0; i < ((char*)p2 - (char*)p1) / list->object._type + 1; i++)
	{
		XVList_insert_front_p(this_list, pval, (char*)p1 + i * list->object._type);
	}
}