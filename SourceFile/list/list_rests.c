#include"list.h"
#include"list_head.h"
#include<string.h>
#include<stdlib.h>
#include"algorithm.h"
//其他
bool List_empty(const list* li)
{
	LIST* list=(LIST*)li;
	return !list->_current;
}

size_t List_size(const list* li)
{
	LIST* list=(LIST*)li;
	return list->_current;
}

void List_sort(list* li, bool(*Sort)(void* x, void* y))
{
	LIST* list=(LIST*)li;
	char* p1=NULL;//指向第一个元素
	char* p2=NULL;
	size_t Pl = malloc(sizeof(void*) * list->_current);//创建临时数组保存数据的地址
	Node* p = list->_data;
	for (size_t i = 0; i < list->_current; i++)//按照顺序将链表的数据地址保存到数组
	{
		memcpy(Pl + i * list->_type,&(p->date), sizeof(void*));
		p=p->next;
	}
	//qsort(Pl, list->_current, list->_type, Sort);
	for (size_t n1 = 0; n1 <list->size(li)-1; n1++)
	{
		//p1 = list->at(li, n1);
		memcpy(&p1, Pl + n1 * list->_type, sizeof(void*));//将数组中的地址拿出来
		for (size_t n2 = n1+1; n2 <list->size(li); n2++)
		{
			//p2 = list->at(li, n2);
			memcpy(&p2, Pl + n2 * list->_type, sizeof(void*));//将数组中的地址拿出来
			if (!Sort(p1, p2))//排序比较函数，返回布尔值
			{
				swap(p1, p2, list->_type);//交换函数
			}
		}
	}
	free(Pl);
}

void List_swap(list* li1, list* li2)
{
	LIST* list1=(LIST*)li1;
	LIST* list2=(LIST*)li2;
	swap(&list1->_data,&list2->_data, sizeof(Node*));
	swap(&list1->_current, &list2->_current, sizeof(size_t));
}

