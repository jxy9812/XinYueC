#include"list.h"
#include"list_head.h"
#include"ContainerObject.h"
#include"stack.h"
#include<string.h>
#include<stdlib.h>
#include"algorithm.h"
//一次快排
static struct Node* List_OneSort(struct Node* ListHead, struct Node* ListTail,const size_t type, bool(*Sort)(void* x, void* y))
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

void List_sort(struct list* this_list, bool(*Sort)(void* x, void* y))
{
	struct LIST* list = (LIST*)this_list;
	struct Node* ListHead = List_front(this_list);//链表第一个节点
	struct Node* ListTail = List_back(this_list);//链表最后一个节点
	struct stack* stack = Stack_init("struct Node*",sizeof(struct Node*));
	Stack_Push(stack, &ListTail);
	Stack_Push(stack, &ListHead);
	while (!Stack_empty(stack))
	{
		//获取节点
		struct Node* ListHead = *((struct Node**)Stack_top(stack));
		Stack_pop(stack);
		struct Node* ListTail = *((struct Node**)Stack_top(stack));
		Stack_pop(stack);
		//单次排序
		struct Node* ListMiddle=List_OneSort(ListHead, ListTail, list->object._type, Sort);
		//判断左区间是否存在
		if (ListHead != ListMiddle && ListHead->next != ListMiddle)
		{
			Stack_Push(stack, &ListMiddle->prev);
			Stack_Push(stack, &ListHead);
		}
		//判断右区间是否存在
		if (ListTail != ListMiddle && ListMiddle->next != ListTail)
		{
			Stack_Push(stack, &ListTail);
			Stack_Push(stack, &ListMiddle->next);
		}
	}
	Stact_free(stack);
}