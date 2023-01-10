#include"list.h"
#include"list_head.h"
#include<string.h>
#include"algorithm.h"
//其他
bool List_empty(const LIST* li)
{
	return !li->_current;
}

size_t List_size(const LIST* li)
{
	return li->_current;
}

void List_sort(LIST* li, bool(*Sort)(void* x, void* y))
{
	char* p1=NULL;//指向第一个元素
	char* p2=NULL;
	size_t Pl = malloc(sizeof(void*) * li->_current);//创建临时数组保存数据的地址
	Node* p = li->_date;
	for (size_t i = 0; i < li->_current; i++)//按照顺序将链表的数据地址保存到数组
	{
		memcpy(Pl + i * li->_type,&(p->date), sizeof(void*));
		p=p->next;
	}
	//qsort(Pl, li->_current, li->_type, Sort);
	for (size_t n1 = 0; n1 <li->size(li)-1; n1++)
	{
		//p1 = li->at(li, n1);
		memcpy(&p1, Pl + n1 * li->_type, sizeof(void*));//将数组中的地址拿出来
		for (size_t n2 = n1+1; n2 <li->size(li); n2++)
		{
			//p2 = li->at(li, n2);
			memcpy(&p2, Pl + n2 * li->_type, sizeof(void*));//将数组中的地址拿出来
			if (!Sort(p1, p2))//排序比较函数，返回布尔值
			{
				swap(p1, p2, li->_type);//交换函数
			}
		}
	}
	free(Pl);
}

void List_swap(LIST* li1, LIST* li2)
{
	swap(&li1->_date,&li2->_date, sizeof(Node*));
	swap(&li1->_current, &li2->_current, sizeof(size_t));
}

