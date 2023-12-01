#include"XList_virtual.h"
#include"XList.h"
#include"XContainerObject_virtual.h"
#include<stdlib.h>
#include<string.h>
#include<stdarg.h> 
//虚函数表定义
void* XListVtable[] = {
	//继承的函数
	XVContainerObject_empty,XVContainerObject_size,XVContainerObject_capacity,XVContainerObject_type,XVContainerObject_swap,XVContainerObject_free,
	//插入
	XVList_push_front,XVList_push_back,XVList_inserts,XVList_insert,XVList_insertArray,
	//删除
	XVList_pop_front,XVList_pop_back,XVList_erase,XVList_remove,XVList_clear,
	//遍历
	XVList_at,XVList_front,XVList_back,XVList_find
};
XListNode* XVList_push_front(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, ""))
		return NULL;
	XList* list = this_list;
	XListNode* NewNode = XList_push_back(this_list, LpValue);
	if (list->object._size != 0)
	{
		list->object._data = NewNode;
	}
	return NewNode;
}

XListNode* XVList_push_back(XList* this_list, void* LpValue)
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
	memcpy(NewNode->date, LpValue, list->object._type);//拷贝数据
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

void XVList_inserts(XList* this_list, XListNode* curNode, void* LpValue, size_t n)
{
	XList* list = this_list;
	if (ISNULL(this_list, ""))
		return;
	for (size_t i = 0; i < n; i++)
	{
		/*Node* pval = List_find(li, p);*/
		if (curNode != NULL)
		{
			XListNode* left = curNode->prev;
			//OneList* right = pval->next;

			XListNode* newNode = malloc(sizeof(XListNode));//新节点
			if (newNode == NULL)
			{
				perror("开辟节点失败");
				exit(-1);
			}
			newNode->date = malloc(list->object._type);//开辟节点内储存数据的空间
			memcpy(newNode->date, LpValue, list->object._type);//拷贝数据

			newNode->prev = left;
			newNode->next = curNode;
			left->next = newNode;
			curNode->prev = newNode;

			if (curNode == list->object._data)
			{
				list->object._data = newNode;
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

void XVList_insert(XList* this_list, XListNode* curNode, void* LpValue)
{
	if (ISNULL(this_list, ""))
		return;
	XList* list = this_list;
	if (curNode == NULL)
	{
		printf("节点指针不能为空\n");
		return;
	}
	XVList_inserts(this_list, curNode, LpValue, 1);
}

void XVList_insertArray(XList* this_list, XListNode* curNode, const void* begin, size_t n)
{
	if (ISNULL(this_list, ""))
		return;
	XList* list = this_list;
	if (curNode == NULL)
	{
		printf("节点指针不能为空\n");
		return;
	}
	for (size_t i = 0; i < n; i++)
	{
		XVList_inserts(this_list, curNode, (char*)begin + i * list->object._type,1);
	}
}
//删除
void XVList_pop_front(XList* this_list)
{
	if (ISNULL(this_list, ""))
		return;
	XList* list = this_list;
	if (list->object._size == 1)
	{
		XListNode* head = list->object._data;
		free(head->date);
		free(head);
		list->object._data = NULL;
		list->object._size--;
		list->object._capacity--;
	}
	else if (list->object._size > 1)
	{
		XListNode* pfront = list->object._data;//原头节点
		XListNode* pback = pfront->prev;//原尾节点
		XListNode* pnfront = pfront->next;//新头节点
		pnfront->prev = pback;
		pback->next = pnfront;
		list->object._data = pnfront;
		free(pfront->date);
		free(pfront);
		list->object._size--;
		list->object._capacity--;
	}
}

void XVList_pop_back(XList* this_list)
{
	if (ISNULL(this_list, ""))
		return;
	XList* list = this_list;
	if (list->object._size == 1)
	{
		XList_pop_front(this_list);
	}
	else if (list->object._size > 1)
	{
		XListNode* pfront = list->object._data;//原头节点
		XListNode* pback = pfront->prev;//原尾节点
		XListNode* pnback = pback->prev;//新尾节点
		pnback->next = pfront;
		pfront->prev = pnback;
		free(pback->date);
		free(pback);//释放尾节点
		list->object._size--;
		list->object._capacity--;
	}
}

void XVList_erase(XList* this_list, XListNode* node)
{
	if (ISNULL(this_list, "")|| ISNULL(node, "")|| this_list->object._size<=0)
		return;
	XList* list = this_list;
	XListNode* nextNode = node->next;//下一个节点
	XListNode* prevNode = node->prev;//上一个节点
	if(node->date)
		free(node->date);//释放节点的数据
	free(node);//释放节点
	if (list->object._size == 1)
	{
		this_list->object._data = NULL;
	}
	else
	{
		nextNode->prev = prevNode;
		prevNode->next = nextNode;
		if (this_list->object._data == node)
			this_list->object._data = nextNode;//重新设置头节点
	}
	--this_list->object._capacity;
	--this_list->object._size;
}

void XVList_remove(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(LpValue, ""))
		return;
	XList* list = this_list;
	XVList_erase(this_list,XList_at(this_list,LpValue));
}

void XVList_clear(XList* this_list)
{
	if (ISNULL(this_list, ""))
		return;
	XList* list = this_list;
	XListNode* p = list->object._data;
	XListNode* pnext = p->next;
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
//遍历
XListNode* XVList_at(const XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, "")|| ISNULL(LpValue, ""))
		return NULL;
	for (XList_iterator* it = XList_begin(this_list); it != XList_end(this_list); it = XList_iterator_add(this_list, it))
	{
		if (this_list->equality(((XListNode*)it)->date, LpValue))
			return it;
	}
	return NULL;
}

XListNode* XVList_front(XList* this_list)
{
	if (isNULL(isNULLInfo(this_list, "")))
		return NULL;
	XList* list = this_list;
	return list->object._data;
}

XListNode* XVList_back(XList* this_list)
{
	if (isNULL(isNULLInfo(this_list, "")))
		return NULL;
	XList* list = this_list;
	return ((XListNode*)(list->object._data))->prev;
}

XListNode* XVList_find(const XList* this_list, void* LpValue)
{
	return XVList_at(this_list,LpValue);
}