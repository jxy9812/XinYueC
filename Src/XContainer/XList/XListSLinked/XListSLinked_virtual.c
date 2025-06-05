#include "XListSLinked.h"
#if XListSLinked_ON
#include"XStack.h"
#include<stdlib.h>
#include<string.h>
//插入
static XListSNode* VXList_push_front(XListSLinked* this_list, void* pvData);
static XListSNode* VXList_push_back(XListSLinked* this_list, void* pvData);
static void VXList_insert(XListSLinked* this_list, XListSNode* curNode, void* pvData);
static void VXList_insert_array(XListSLinked* this_list, XListSNode* curNode, const void* array, size_t count);
//删除
static void VXList_pop_front(XListSLinked* this_list);
static void VXList_pop_back(XListSLinked* this_list);
static void* VXList_erase(XListSLinked* this_list, XListSNode* node);
static void VXList_remove(XListSLinked* this_list, void* pvData);
static void VXList_clear(XListSLinked* this_list);
//遍历
static void* VXList_front(XListSLinked* this_list);
static void* VXList_back(XListSLinked* this_list);
static XListSNode* VXList_find(const XListSLinked* this_list, void* pvData);
//其他
static void VXList_sort(XListSLinked* this_list, XCompare compare);
static void VXList_free(XListSLinked* this_list);
XVtable* XListSLinked_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XLISTSLINKED_VTABLE_SIZE)
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
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Free, VXList_free);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXList_clear);

#if SHOWCONTAINERSIZE
	printf("XListSLinked size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

#endif
#define CreatNode(this_list)   (XMemory_malloc(sizeof(XListSNode*)+XContainerTypeSize(this_list)))
XListSNode* VXList_push_front(XListSLinked* this_list, void* pvData)
{
	XListBase* list = this_list;
	XListSNode* NewNode = CreatNode(this_list);//新节点
	if (NewNode == NULL)
	{
		perror("开辟节点失败");
		return NULL;
	}
	memcpy(&(NewNode->data), pvData, XContainerTypeSize(this_list));//拷贝数据
	XListSNode* head = XContainerDataPtr(this_list);//获取头指针
	XContainerDataPtr(this_list) = NewNode;//新节点成为新的头
	NewNode->next = head;//修改指向下一个节点为原先的头
	if (XListBase_isEmpty_base(this_list))
		this_list->m_tail = NewNode;
	//更新记录数量
	++XContainerSize(this_list);
	++XContainerCapacity(this_list);
	return NewNode;
}

XListSNode* VXList_push_back(XListSLinked* this_list, void* pvData)
{
	XListBase* list = this_list;
	XListSNode* NewNode = CreatNode(this_list);//新节点
	if (NewNode == NULL)
	{
		perror("开辟节点失败");
		return NULL;
	}
	memcpy(&(NewNode->data), pvData, XContainerTypeSize(this_list));//拷贝数据
	if(this_list->m_tail)
		this_list->m_tail->next = NewNode;//尾指针指向新节点
	NewNode->next = NULL;//新节点指向NULL
	this_list->m_tail = NewNode;//更新记录的尾节点
	if (XListBase_isEmpty_base(this_list))
		XContainerDataPtr(this_list) = NewNode;
	//更新记录数量
	++XContainerSize(this_list);
	++XContainerCapacity(this_list);
	return NewNode;
}

void VXList_insert(XListSLinked* this_list, XListSNode* curNode, void* pvData)
{
	XListBase_insert_array_base(this_list,curNode,pvData,1);
	return;
	if (curNode == NULL)
	{
		XListBase_push_back_base(this_list,pvData);
		return;
	}
	//遍历节点
	XListSNode* prev = NULL;//前一个节点
	XListSNode* node = XContainerDataPtr(this_list);//当前节点
	while (node)
	{
		if (node == curNode)
			break;//找到节点后跳出循环
		//没找到看下一个
		prev = node;
		node = node->next;
	}
	if (node==NULL)
		return;//没找到插入失败
	//创建一个新的节点
	XListSNode* NewNode = CreatNode(this_list);//新节点
	if (NewNode == NULL)
	{
		perror("开辟节点失败");
		return NULL;
	}
	memcpy(&(NewNode->data), pvData, XContainerTypeSize(this_list));//拷贝数据
	NewNode->next = node;//链接节点
	//更新节点信息
	if (prev == NULL)
	{
		XContainerDataPtr(this_list) = NewNode;//更新头节点
	}
	else
	{
		prev->next = NewNode;//
	}
	//更新记录数量
	++XContainerSize(this_list);
	++XContainerCapacity(this_list);
}

void VXList_insert_array(XListSLinked* this_list, XListSNode* curNode, const void* array, size_t count)
{
	//遍历节点
	XListSNode* prev = NULL;//前一个节点
	XListSNode* node = XContainerDataPtr(this_list);//当前节点
	if (curNode != NULL)
	{
		while (node)
		{
			if (node == curNode)
				break;//找到节点后跳出循环
			//没找到看下一个
			prev = node;
			node = node->next;
		}
		if (node == NULL)
			return;//没找到插入失败
	}
	
	//开始将数组数据构建成链表
	XListSNode* NewListHead = NULL;
	XListSNode* NewListNode = NULL;
	XListSNode* NewListTail = NULL;
	for (size_t i = 0; i < count; i++)
	{
		//创建一个新的节点
		NewListNode = CreatNode(this_list);//新节点
		if (NewListNode == NULL)
		{
			perror("开辟节点失败");
			return NULL;
		}
		memcpy(&(NewListNode->data), ((char*)array)+i* XContainerTypeSize(this_list), XContainerTypeSize(this_list));//拷贝数据
		if (NewListTail == NULL)
		{//第一个节点
			NewListTail = NewListNode;
			NewListHead = NewListNode;
		}
		else
		{
			NewListTail->next = NewListNode;
			NewListTail = NewListNode;
		}
	}
	if (XContainerDataPtr(this_list)==NULL)
	{//链表是空的情况
		XContainerDataPtr(this_list) = NewListHead;//更新头节点
		this_list->m_tail = NewListTail;//更新尾节点
		NewListTail->next = NULL;
		//更新数量
		XContainerSize(this_list) += count;
		XContainerCapacity(this_list) += count;
		return;
	}

	//开始将两个链表合并起来
	if (curNode == NULL)
	{//要插入到链表尾部	
		this_list->m_tail->next = NewListHead;//链接头尾
		this_list->m_tail = NewListTail;//更新尾节点
		NewListTail->next = NULL;
	}
	else if(curNode== XContainerDataPtr(this_list))
	{//插入到链表头
		NewListTail->next = XContainerDataPtr(this_list);
		XContainerDataPtr(this_list) = NewListHead;
	}
	else
	{//插入到原先链表中间
		prev->next = NewListHead;
		NewListTail->next = node;
	}
	//更新数量
	XContainerSize(this_list)+=count;
	XContainerCapacity(this_list) += count;
}

void VXList_pop_front(XListSLinked* this_list)
{
	XListSNode* head= XContainerDataPtr(this_list);
	if (head == NULL)
		return;//链表是空的
	if (XListBase_getSize_base(this_list) == 1)
	{
		XContainerDataPtr(this_list) = NULL;
		this_list->m_tail = NULL;//更新指向的尾节点
	}
	else
	{
		XContainerDataPtr(this_list) = head->next;
	}
	if (XContainerDataFreeMethod(this_list) != NULL)
		XContainerDataFreeMethod(this_list)(&(head->data));
	//释放节点
	XMemory_free(head);
	//更新数量
	--XContainerSize(this_list) ;
	--XContainerCapacity(this_list);
}

void VXList_pop_back(XListSLinked* this_list)
{
	XListSNode* tail = this_list->m_tail;
	if (tail == NULL)
		return;
	//printf("尾删中\n");
	if (XListBase_getSize_base(this_list) == 1)
	{
		XContainerDataPtr(this_list) = NULL;
		this_list->m_tail = NULL;//更新指向的尾节点
	}
	else
	{
		XListSNode* node = XContainerDataPtr(this_list);//当前节点
		while (node)
		{
			if (node->next == tail)
			{
				node->next = NULL;
				this_list->m_tail = node;//更新指向的尾节点
			}
			node=node->next;
		}
	}
	if (XContainerDataFreeMethod(this_list) != NULL)
		XContainerDataFreeMethod(this_list)(&(tail->data));
	XMemory_free(tail);
	//更新数量
	--XContainerSize(this_list);
	--XContainerCapacity(this_list);
}
//删除一个节点
static removeNode(XListSLinked* this_list, XListSNode* prev, XListSNode* removeNode)
{
	if (prev == NULL)
	{//删除的是头节点
		XContainerDataPtr(this_list) = removeNode->next;
	}
	else
	{
		prev->next = removeNode->next;
	}
	if (removeNode->next == NULL)
	{//删除的是尾节点
		this_list->m_tail = prev;
		if (prev)
			prev->next = NULL;
	}
	if (XContainerDataFreeMethod(this_list) != NULL)
		XContainerDataFreeMethod(this_list)(&(removeNode->data));
	XMemory_free(removeNode);
	//更新数量
	--XContainerSize(this_list);
	--XContainerCapacity(this_list);
}

void* VXList_erase(XListSLinked* this_list, XListSNode* node)
{
	if (XListBase_isEmpty_base(this_list)||node==NULL)
		return NULL;
	XListSNode* prev = NULL;//前一个节点
	XListSNode* curNode = XContainerDataPtr(this_list);//当前节点
	while (node)
	{
		if (curNode == node)
		{//找到要删除的节点了
			XListSNode* next = curNode->next;
			removeNode(this_list,prev,curNode);
			return next;
		}
		prev = curNode;
		curNode = curNode->next;
	}
	return NULL;
}

void VXList_remove(XListSLinked* this_list, void* pvData)
{
	if (XListBase_isEmpty_base(this_list))
		return;
	if (((XListBase*)this_list)->m_equality == NULL)
		return ;
	XListSNode* prev = NULL;//前一个节点
	XListSNode* curNode = XContainerDataPtr(this_list);//当前节点
	while (curNode)
	{
		if (((XListBase*)this_list)->m_equality(&(curNode->data), pvData))
		{//找到了
			removeNode(this_list,prev,curNode);
			return;
		}
		prev = curNode;
		curNode = curNode->next;
	}
	return ;
}

void VXList_clear(XListSLinked* this_list)
{
	if (XListBase_isEmpty_base(this_list))
		return;
	XListSNode* prev = NULL;//前一个节点
	XListSNode* node = XContainerDataPtr(this_list);//当前节点
	while (node)
	{
		prev = node;
		node = node->next;
		if (XContainerDataFreeMethod(this_list) != NULL)
			XContainerDataFreeMethod(this_list)(&(prev->data));
		XMemory_free(prev);
	}
	this_list->m_tail = NULL;
	XContainerDataPtr(this_list) = NULL;
	XContainerSize(this_list)=0;
	XContainerCapacity(this_list)=0;
}

void* VXList_front(XListSLinked* this_list)
{
	if(XContainerDataPtr(this_list))
		return XListSNode_DataPtr(XContainerDataPtr(this_list));
	return NULL;
}

void* VXList_back(XListSLinked* this_list)
{
	if (this_list->m_tail)
		return &(this_list->m_tail->data);
	return NULL;
}

XListSNode* VXList_find(const XListSLinked* this_list, void* pvData)
{
	if (XListBase_isEmpty_base(this_list))
		return NULL;
	if(((XListBase*)this_list)->m_equality==NULL)
		return NULL;
	XListSNode* node = XContainerDataPtr(this_list);//当前节点
	while (node)
	{
		if (((XListBase*)this_list)->m_equality(&(node->data), pvData))
		{//找到了
			return node;
		}
		node = node->next;
	}
	return NULL;
}
// 找到链表尾部节点
static XListSNode* findTail(XListSNode* head) {
	if (head == NULL) return NULL;
	while (head->next != NULL)
		head = head->next;
	return head;
}
// 单向链表的一次快排（分区函数）
static XListSNode* List_OneSort(XListSNode* left, XListSNode* right, size_t typeSize, XCompare compare) {
	if (left == NULL || right == NULL || left == right)
		return left;
	
	void* pivot = XMemory_malloc(typeSize);
	if (pivot == NULL) return NULL;
	memcpy(pivot, &(left->data), typeSize);

	XListSNode* i = left;    // 分区点
	XListSNode* j = left->next;

	while (j != NULL) {
		if (compare(&(j->data), pivot)) {
			i = i->next;
			// 交换i和j的数据

			void* temp = XMemory_malloc(typeSize);
			memcpy(temp, &(i->data), typeSize);
			memcpy(&(i->data), &(j->data), typeSize);
			memcpy(&(j->data), temp, typeSize);
			XMemory_free(temp);
		}
		if (j == right) break;  // 到达右边界
		j = j->next;
	}

	// 将pivot放到正确位置
	void* temp = XMemory_malloc(typeSize);
	memcpy(temp, &(i->data), typeSize);
	memcpy(&(i->data), &(left->data), typeSize);
	memcpy(&(left->data), temp, typeSize);
	XMemory_free(temp);
	XMemory_free(pivot);

	return i;  // 返回分区点
}
void VXList_sort(XListSLinked* this_list, XCompare compare)
{
#if XStack_ON
	if (XListBase_isEmpty_base(this_list))
		return ;
	//printf("进入排序\n");
	XListSNode* head = XContainerDataPtr(this_list);
	XListSNode* tail = findTail(head);

	// 使用现有的XStack
	XStack* stack = XStack_Create(XListSNode*);
	if (stack == NULL) 
		return;

	// 初始化栈
	if (head != NULL) {
		XStack_push_base(stack, &tail);
		XStack_push_base(stack, &head);
	}

	while (!XStack_isEmpty_base(stack)) {
		// 弹出区间
		XListSNode* h = *((XListSNode**)XStack_top_base(stack));
		XStack_pop_base(stack);
		XListSNode* t = *((XListSNode**)XStack_top_base(stack));
		XStack_pop_base(stack);

		if (h == NULL || t == NULL || h == t)
			continue;

		// 执行一次快排
		XListSNode* pivot = List_OneSort(h, t, XContainerTypeSize(this_list), compare);

		// 处理左子区间
		if (h != pivot) {
			XListSNode* leftTail = findTail(h);
			if (leftTail != NULL && h != leftTail) {
				XStack_push_base(stack, &leftTail);
				XStack_push_base(stack, &h);
			}
		}

		// 处理右子区间
		if (pivot != NULL && pivot->next != NULL) {
			XListSNode* rightHead = pivot->next;
			XListSNode* rightTail = findTail(rightHead);
			if (rightTail != NULL && rightHead != rightTail) {
				XStack_push_base(stack, &rightTail);
				XStack_push_base(stack, &rightHead);
			}
		}
	}

	XStack_free_base(stack);
#else
	IS_ON_DEBUG(XStack_ON);
#endif
}

void VXList_free(XListSLinked* this_list)
{
	XListBase_clear_base(this_list);
	XMemory_free(this_list);
}
