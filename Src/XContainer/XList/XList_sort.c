#include"XList.h"
#include"XContainerObject.h"
#include"XStack.h"
#include<string.h>
#include<stdlib.h>
#include"XAlgorithm.h"
//一次快排
static struct XListNode* List_OneSort(XListNode* ListHead, XListNode* ListTail,const size_t type, bool(*Sort)(const void* LPrevValue, const void* LNextValue))
{
	
	char* compareVal = malloc(type);
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
	free(compareVal);
	//单次结束，分割节点
	return ListHead;

}

void XList_sort(struct XList* this_list, XCompare compare)
{
	if (isNULL(isNULLInfo(this_list, "")))
		return ;
	XList* list = this_list;
	XListNode* ListHead = XList_front(this_list);//链表第一个节点
	XListNode* ListTail = XList_back(this_list);//链表最后一个节点
	struct XStack* stack = XStack_init("struct Node*",sizeof(struct XListNode*));
	XStack_Push(stack, &ListTail);
	XStack_Push(stack, &ListHead);
	while (!XStack_empty(stack))
	{
		//获取节点
		XListNode* ListHead = *((struct XListNode**)XStack_top(stack));
		XStack_pop(stack);
		 XListNode* ListTail = *((struct XListNode**)XStack_top(stack));
		XStack_pop(stack);
		//单次排序
	 XListNode* ListMiddle=List_OneSort(ListHead, ListTail, list->object._type, compare);
		//判断左区间是否存在
		if (ListHead != ListMiddle && ListHead->next != ListMiddle)
		{
			XStack_Push(stack, &ListMiddle->prev);
			XStack_Push(stack, &ListHead);
		}
		//判断右区间是否存在
		if (ListTail != ListMiddle && ListMiddle->next != ListTail)
		{
			XStack_Push(stack, &ListTail);
			XStack_Push(stack, &ListMiddle->next);
		}
	}
	XStack_free(stack);
}