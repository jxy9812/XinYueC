#include"XList.h"
#if XList_ON
#include"XStack.h"
#include<stdlib.h>
#include<string.h>
//声明
//插入
static XListNode* VXList_push_front(XList* this_list, void* LpValue);
static XListNode* VXList_push_back(XList* this_list, void* LpValue);
static void VXList_inserts(XList* this_list, XListNode* curNode, void* LpValue, size_t n);
static void VXList_insert(XList* this_list, XListNode* curNode, void* LpValue);
static void VXList_insert_array(XList* this_list, XListNode* curNode, const void* begin, size_t n);
 //删除
static void VXList_pop_front(XList* this_list);
static void VXList_pop_back(XList* this_list);
static void VXList_erase(XList* this_list, XListNode* node);
static void VXList_remove(XList* this_list, void* LpValue);
static void VXList_clear(XList* this_list);
//遍历
static void* VXList_front(XList* this_list);
static void* VXList_back(XList* this_list);
static XListNode* VXList_find(const XList* this_list, void* LpValue);
//其他
static void VXList_sort(XList* this_list, XCompare compare);
static void VXList_free(XList* this_list);
//虚函数表定义
XVtable* XListVtable = NULL;
#if VTABLE_ISSTACK
	static XVtable vtable;//虚函数类
	static void* vtable_data[XLIST_VTABLE_SIZE];//虚函数数据
#endif
void XList_class_init()
{
	if (XListVtable)
		return;
	void* table[] = {
		//插入
		VXList_push_front,VXList_push_back,VXList_inserts,VXList_insert,VXList_insert_array,
		//删除
		VXList_pop_front,VXList_pop_back,VXList_erase,VXList_remove,
		//遍历
		VXList_front,VXList_back,VXList_find,
		//排序
		VXList_sort
	};
#if !VTABLE_ISSTACK
	XListVtable = XVtable_new();
#else
	XListVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	//继承的函数
	XVtable_append_vtable(XListVtable, XContainerObjectVtable);
	//追加函数
	XVtable_append_array(XListVtable, table, sizeof(table) / sizeof(table[0]));
	//重写的函数
	XVtable_At(XListVtable, EXClass_Free)= VXList_free;
	XVtable_At(XListVtable, EXContainerObject_Clear) = VXList_clear;

#if SHOWCONTAINERSIZE
	printf("XList size:%d\n", XVtable_size(XListVtable));
#endif
}

XListNode* VXList_push_front(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, ""))
		return NULL;
	XList* list = this_list;
	XListNode* NewNode = XList_push_back_base(this_list, LpValue);
	if (list->m_parent.m_size != 0)
	{
		list->m_parent.m_data = NewNode;
	}
	return NewNode;
}

XListNode* VXList_push_back(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, ""))
		return NULL;
	XList* list = this_list;
	XListNode* NewNode = XMemory_malloc(sizeof(XListNode));//新节点
	if (NewNode == NULL)
	{
		perror("开辟节点失败");
		exit(-1);
	}
	NewNode->date = XMemory_malloc(list->m_parent.m_typeSize);//开辟节点内储存数据的空间
	memcpy(NewNode->date, LpValue, list->m_parent.m_typeSize);//拷贝数据
	if (list->m_parent.m_size == 0)
	{
		list->m_parent.m_data = NewNode;
		NewNode->next = NewNode;
		NewNode->prev = NewNode;
	}
	else
	{
		XListNode* pfront = list->m_parent.m_data;//原头节点
		XListNode* pback = pfront->prev;//原尾节点
		NewNode->next = pfront;
		NewNode->prev = pback;
		pfront->prev = NewNode;
		pback->next = NewNode;
	}
	list->m_parent.m_size++;
	list->m_parent.m_capacity++;
	return NewNode;
}

void VXList_inserts(XList* this_list, XListNode* curNode, void* LpValue, size_t n)
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
			//XListNode* right = left->next;

			XListNode* newNode = XMemory_malloc(sizeof(XListNode));//新节点
			if (newNode == NULL)
			{
				perror("开辟节点失败");
				exit(-1);
			}
			newNode->date = XMemory_malloc(list->m_parent.m_typeSize);//开辟节点内储存数据的空间
			memcpy(newNode->date, LpValue, list->m_parent.m_typeSize);//拷贝数据

			newNode->prev = left;
			newNode->next = curNode;
			left->next = newNode;
			curNode->prev = newNode;

			if (curNode == list->m_parent.m_data)
			{
				list->m_parent.m_data = newNode;
			}
			list->m_parent.m_size++;
			list->m_parent.m_capacity++;
		}
		else
		{
			perror("插入的数找不到");
		}
	}
}

void VXList_insert(XList* this_list, XListNode* curNode, void* LpValue)
{
	if (ISNULL(this_list, ""))
		return;
	XList* list = this_list;
	if (curNode == NULL)
	{
		printf("节点指针不能为空\n");
		return;
	}
	VXList_inserts(this_list, curNode, LpValue, 1);
}

void VXList_insert_array(XList* this_list, XListNode* curNode, const void* begin, size_t n)
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
		VXList_inserts(this_list, curNode, (char*)begin + i * list->m_parent.m_typeSize,1);
	}
}
//删除
void VXList_pop_front(XList* this_list)
{
	if (ISNULL(this_list, "") || XContainerObject_isEmpty_base(this_list))
		return;
	VXList_erase(this_list,XList_begin(this_list));
}

void VXList_pop_back(XList* this_list)
{
	if (ISNULL(this_list, "") || XContainerObject_isEmpty_base(this_list))
		return;
	VXList_erase(this_list, XList_rbegin(this_list));
}

void VXList_erase(XList* this_list, XListNode* node)
{
	if (ISNULL(this_list, "")|| ISNULL(node, "")|| XContainerObject_isEmpty_base(this_list))
		return;
	XList* list = this_list;
	XListNode* nextNode = node->next;//下一个节点
	XListNode* prevNode = node->prev;//上一个节点
	if (node->date)
	{
		if (XContainerDataFreeMethod(this_list) != NULL)
			XContainerDataFreeMethod(this_list)(node->date);
		XMemory_free(node->date);//释放节点的数据
	}
	XMemory_free(node);//释放节点
	if (list->m_parent.m_size == 1)
	{
		this_list->m_parent.m_data = NULL;
	}
	else
	{
		nextNode->prev = prevNode;
		prevNode->next = nextNode;
		if (this_list->m_parent.m_data == node)
			this_list->m_parent.m_data = nextNode;//重新设置头节点
	}
	--this_list->m_parent.m_capacity;
	--this_list->m_parent.m_size;
}

void VXList_remove(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(LpValue, ""))
		return;
	XList* list = this_list;
	XListNode* node = VXList_find(this_list, LpValue);
	if(node)
		VXList_erase(this_list,node);
}

void VXList_clear(XList* this_list)
{
	if (XContainerObject_isEmpty_base(this_list))
		return;
	XList* list = this_list;
	XListNode* p = list->m_parent.m_data;
	XListNode* pnext = p->next;
	for (size_t i = 0; i < list->m_parent.m_size; i++)
	{
		if (XContainerDataFreeMethod(this_list) != NULL)
			XContainerDataFreeMethod(this_list)(p->date);
		pnext = p->next;
		XMemory_free(p->date);
		XMemory_free(p);
		p = pnext;
	}
	list->m_parent.m_size = 0;
	list->m_parent.m_capacity = 0;
	list->m_parent.m_data = NULL;
}

void* VXList_front(XList* this_list)
{
	if (ISNULL(this_list, ""))
		return NULL;
	XList* list = this_list;
	return ((XListNode*)(list->m_parent.m_data))->date;
}

void* VXList_back(XList* this_list)
{
	if (ISNULL(this_list, ""))
		return NULL;
	XList* list = this_list;
	return ((XListNode*)(list->m_parent.m_data))->prev->date;
}

XListNode* VXList_find(const XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->m_equality, "") || ISNULL(LpValue, ""))
		return NULL;
	for (XList_iterator* it = XList_begin(this_list); it != XList_end(this_list); it = XList_iterator_add(this_list, it))
	{
		if (this_list->m_equality(((XListNode*)it)->date, LpValue))
			return it;
	}
	return NULL;
}
//其他

void VXList_free(XList* this_list)
{
	if (ISNULL(this_list, ""))
		return;
	XList_clear_base(this_list);
	XMemory_free(this_list);
}
//排序
//一次快排
static struct XListNode* List_OneSort(XListNode* ListHead, XListNode* ListTail, const size_t type, bool(*Sort)(const void* LPrevValue, const void* LNextValue))
{

	char* compareVal = XMemory_malloc(type);
	if (compareVal == NULL)
		return;
	memcpy(compareVal, ListHead->date, type);
	while (ListHead != ListTail)
	{
		while (ListHead != ListTail)//右边开始往左边找
		{
			if (!Sort(ListTail->date, compareVal))
			{
				ListTail = ListTail->prev;
			}
			else
			{
				memcpy(ListHead->date, ListTail->date, type);
				break;
			}
		}
		while (ListHead != ListTail)//左边开始往右边找
		{
			if (Sort(ListHead->date, compareVal))
			{
				ListHead = ListHead->next;
			}
			else
			{
				memcpy(ListTail->date, ListHead->date, type);
				break;
			}
		}
	}
	memcpy(ListTail->date, compareVal, type);
	XMemory_free(compareVal);
	//单次结束，分割节点
	return ListHead;

}

void VXList_sort(XList* this_list, XCompare compare)
{
#if XStack_ON
	if (ISNULL(this_list, ""))
		return;
	XList* list = this_list;
	XListNode* ListHead = XList_begin(this_list);//链表第一个节点
	XListNode* ListTail = XList_rbegin(this_list);//链表最后一个节点
	XStack* stack = XStack_New(XListNode*);
	XStack_push_base(stack, &ListTail);
	XStack_push_base(stack, &ListHead);
	while (!XStack_isEmpty_base(stack))
	{
		//获取节点
		XListNode* ListHead = *((struct XListNode**)XStack_top_base(stack));
		XStack_pop_base(stack);
		XListNode* ListTail = *((struct XListNode**)XStack_top_base(stack));
		XStack_pop_base(stack);
		//单次排序
		XListNode* ListMiddle = List_OneSort(ListHead, ListTail, list->m_parent.m_typeSize, compare);
		//判断左区间是否存在
		if (ListHead != ListMiddle && ListHead->next != ListMiddle)
		{
			XStack_push_base(stack, &ListMiddle->prev);
			XStack_push_base(stack, &ListHead);
		}
		//判断右区间是否存在
		if (ListTail != ListMiddle && ListMiddle->next != ListTail)
		{
			XStack_push_base(stack, &ListTail);
			XStack_push_base(stack, &ListMiddle->next);
		}
	}
	XStack_free_base(stack);
#else
	IS_ON_DEBUG(XStack_ON);
#endif
}
#endif