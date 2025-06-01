#include"XListDLinked.h"
#if XListDLinked_ON
#include"XStack.h"
#include<stdlib.h>
#include<string.h>
//插入
static XListDNode* VXList_push_front(XListDLinked* this_list, void* LpValue);
static XListDNode* VXList_push_back(XListDLinked* this_list, void* LpValue);
static void VXList_insert(XListDLinked* this_list, XListDNode* curNode, void* LpValue);
static void VXList_insert_array(XListDLinked* this_list, XListDNode* curNode, const void* begin, size_t n);
 //删除
static void VXList_pop_front(XListDLinked* this_list);
static void VXList_pop_back(XListDLinked* this_list);
static void VXList_erase(XListDLinked* this_list, XListDNode* node);
static void VXList_remove(XListDLinked* this_list, void* LpValue);
static void VXList_clear(XListDLinked* this_list);
//遍历
static void* VXList_front(XListDLinked* this_list);
static void* VXList_back(XListDLinked* this_list);
static XListDNode* VXList_find(const XListDLinked* this_list, void* LpValue);
//其他
static void VXList_sort(XListDLinked* this_list, XCompare compare);
static void VXList_free(XListDLinked* this_list);

XVtable* XListDLinked_class_init()
{
	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XLISTDLINKED_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());

	void* table[] = {
		//插入
		VXList_push_front,VXList_push_back,VXList_insert,VXList_insert_array,
		//删除
		VXList_pop_front,VXList_pop_back,VXList_erase,VXList_remove,
		//遍历
		VXList_front,VXList_back,VXList_find,
		//排序
		VXList_sort
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Free,VXList_free);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear,VXList_clear);

#if SHOWCONTAINERSIZE
	printf("XListDLinked size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
return XVTABLE_DEFAULT;
}

XListDNode* VXList_push_front(XListDLinked* this_list, void* LpValue)
{
	if (ISNULL(this_list, ""))
		return NULL;
	XListBase* list = this_list;
	XListDNode* NewNode = XListDLinked_push_back_base(this_list, LpValue);
	if (list->m_parent.m_size != 0)
	{
		list->m_parent.m_data = NewNode;
	}
	return NewNode;
}

XListDNode* VXList_push_back(XListDLinked* this_list, void* LpValue)
{
	if (ISNULL(this_list, ""))
		return NULL;
	XListBase* list = this_list;
	XListDNode* NewNode = XMemory_malloc(sizeof(XListDNode));//新节点
	if (NewNode == NULL)
	{
		perror("开辟节点失败");
		return NULL;
	}
	NewNode->data = XMemory_malloc(list->m_parent.m_typeSize);//开辟节点内储存数据的空间
	memcpy(NewNode->data, LpValue, list->m_parent.m_typeSize);//拷贝数据
	if (list->m_parent.m_size == 0)
	{
		list->m_parent.m_data = NewNode;
		NewNode->next = NewNode;
		NewNode->prev = NewNode;
	}
	else
	{
		XListDNode* pfront = list->m_parent.m_data;//原头节点
		XListDNode* pback = pfront->prev;//原尾节点
		NewNode->next = pfront;
		NewNode->prev = pback;
		pfront->prev = NewNode;
		pback->next = NewNode;
	}
	//更新记录数量
	++XContainerSize(this_list);
	++XContainerCapacity(this_list);
	return NewNode;
}

void VXList_inserts(XListDLinked* this_list, XListDNode* curNode, void* LpValue, size_t n)
{
	XListBase* list = this_list;
	if (ISNULL(this_list, ""))
		return;
	for (size_t i = 0; i < n; i++)
	{
		/*Node* pval = List_find(li, p);*/
		if (curNode != NULL)
		{
			XListDNode* left = curNode->prev;
			//XListDNode* right = left->next;

			XListDNode* newNode = XMemory_malloc(sizeof(XListDNode));//新节点
			if (newNode == NULL)
			{
				perror("开辟节点失败");
				exit(-1);
			}
			newNode->data = XMemory_malloc(list->m_parent.m_typeSize);//开辟节点内储存数据的空间
			memcpy(newNode->data, LpValue, list->m_parent.m_typeSize);//拷贝数据

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

void VXList_insert(XListDLinked* this_list, XListDNode* curNode, void* LpValue)
{
	if (ISNULL(this_list, ""))
		return;
	XListDLinked* list = this_list;
	if (curNode == NULL)
	{
		printf("节点指针不能为空\n");
		return;
	}
	VXList_inserts(this_list, curNode, LpValue, 1);
}

void VXList_insert_array(XListDLinked* this_list, XListDNode* curNode, const void* begin, size_t n)
{
	if (ISNULL(this_list, ""))
		return;
	XListBase* list = this_list;
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
void VXList_pop_front(XListDLinked* this_list)
{
	if (ISNULL(this_list, "") || XContainerObject_isEmpty_base(this_list))
		return;
	VXList_erase(this_list,XListDLinked_begin(this_list));
}

void VXList_pop_back(XListDLinked* this_list)
{
	if (ISNULL(this_list, "") || XContainerObject_isEmpty_base(this_list))
		return;
	VXList_erase(this_list, XListDLinked_rbegin(this_list));
}

void VXList_erase(XListDLinked* this_list, XListDNode* node)
{
	if (ISNULL(this_list, "")|| ISNULL(node, "")|| XContainerObject_isEmpty_base(this_list))
		return;
	XListBase* list = this_list;
	XListDNode* nextNode = node->next;//下一个节点
	XListDNode* prevNode = node->prev;//上一个节点
	if (node->data)
	{
		if (XContainerDataFreeMethod(this_list) != NULL)
			XContainerDataFreeMethod(this_list)(node->data);
		XMemory_free(node->data);//释放节点的数据
	}
	XMemory_free(node);//释放节点
	if (list->m_parent.m_size == 1)
	{
		list->m_parent.m_data = NULL;
	}
	else
	{
		nextNode->prev = prevNode;
		prevNode->next = nextNode;
		if (list->m_parent.m_data == node)
			list->m_parent.m_data = nextNode;//重新设置头节点
	}
	--list->m_parent.m_capacity;
	--list->m_parent.m_size;
}

void VXList_remove(XListDLinked* this_list, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(LpValue, ""))
		return;
	XListDLinked* list = this_list;
	XListDNode* node = XListDLinked_find_base(this_list, LpValue);
	if(node)
		XListDLinked_erase_base(this_list,node);
}

void VXList_clear(XListDLinked* this_list)
{
	if (XContainerObject_isEmpty_base(this_list))
		return;
	XListBase* list = this_list;
	XListDNode* p = list->m_parent.m_data;
	XListDNode* pnext = p->next;
	for (size_t i = 0; i < list->m_parent.m_size; i++)
	{
		if (XContainerDataFreeMethod(this_list) != NULL)
			XContainerDataFreeMethod(this_list)(p->data);
		pnext = p->next;
		XMemory_free(p->data);
		XMemory_free(p);
		p = pnext;
	}
	list->m_parent.m_size = 0;
	list->m_parent.m_capacity = 0;
	list->m_parent.m_data = NULL;
}

void* VXList_front(XListDLinked* this_list)
{
	if (ISNULL(this_list, ""))
		return NULL;
	XListBase* list = this_list;
	return ((XListDNode*)(list->m_parent.m_data))->data;
}

void* VXList_back(XListDLinked* this_list)
{
	if (ISNULL(this_list, ""))
		return NULL;
	XListBase* list = this_list;
	return ((XListDNode*)(list->m_parent.m_data))->prev->data;
}

XListDNode* VXList_find(const XListDLinked* this_list, void* LpValue)
{
	XListBase* list = this_list;
	if (ISNULL(this_list, "") || ISNULL(list->m_equality, "") || ISNULL(LpValue, ""))
		return NULL;
	for (XListDLinked_iterator* it = XListDLinked_begin(this_list); it != XListDLinked_end(this_list); it = XListDLinked_iterator_add(this_list, it))
	{
		if (list->m_equality(((XListDNode*)it)->data, LpValue))
			return it;
	}
	return NULL;
}
//其他

void VXList_free(XListDLinked* this_list)
{
	if (ISNULL(this_list, ""))
		return;
	XListDLinked_clear_base(this_list);
	XMemory_free(this_list);
}
//排序
//一次快排
static struct XListDNode* List_OneSort(XListDNode* ListHead, XListDNode* ListTail, const size_t type, bool(*Sort)(const void* LPrevValue, const void* LNextValue))
{

	char* compareVal = XMemory_malloc(type);
	if (compareVal == NULL)
		return;
	memcpy(compareVal, ListHead->data, type);
	while (ListHead != ListTail)
	{
		while (ListHead != ListTail)//右边开始往左边找
		{
			if (!Sort(ListTail->data, compareVal))
			{
				ListTail = ListTail->prev;
			}
			else
			{
				memcpy(ListHead->data, ListTail->data, type);
				break;
			}
		}
		while (ListHead != ListTail)//左边开始往右边找
		{
			if (Sort(ListHead->data, compareVal))
			{
				ListHead = ListHead->next;
			}
			else
			{
				memcpy(ListTail->data, ListHead->data, type);
				break;
			}
		}
	}
	memcpy(ListTail->data, compareVal, type);
	XMemory_free(compareVal);
	//单次结束，分割节点
	return ListHead;

}

void VXList_sort(XListDLinked* this_list, XCompare compare)
{
#if XStack_ON
	if (ISNULL(this_list, ""))
		return;
	XListBase* list = this_list;
	XListDNode* ListHead = XListDLinked_begin(this_list);//链表第一个节点
	XListDNode* ListTail = XListDLinked_rbegin(this_list);//链表最后一个节点
	XStack* stack = XStack_New(XListDNode*);
	XStack_push_base(stack, &ListTail);
	XStack_push_base(stack, &ListHead);
	while (!XStack_isEmpty_base(stack))
	{
		//获取节点
		XListDNode* ListHead = *((struct XListDNode**)XStack_top_base(stack));
		XStack_pop_base(stack);
		XListDNode* ListTail = *((struct XListDNode**)XStack_top_base(stack));
		XStack_pop_base(stack);
		//单次排序
		XListDNode* ListMiddle = List_OneSort(ListHead, ListTail, list->m_parent.m_typeSize, compare);
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