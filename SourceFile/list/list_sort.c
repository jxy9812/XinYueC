#include"list.h"
#include"list_head.h"
#include"ContainerObject.h"
#include<string.h>
#include<stdlib.h>
#include"algorithm.h"
//一次快排
static List_OneSort(struct Node* ListHead,struct Node* ListTail,const size_t type, bool(*Sort)(void* x, void* y))
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
}

void List_sort(struct list* this_list, bool(*Sort)(void* x, void* y))
{
	struct LIST* list = (LIST*)this_list;
	struct Node* ListHead = List_front(this_list);//链表第一个节点
	struct Node* ListTail = List_back(this_list);//链表最后一个节点
	List_OneSort(ListHead, ListTail, list->object._type,Sort);
}