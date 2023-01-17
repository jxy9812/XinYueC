#include"list.h"
#include"list_head.h"
#include"ContainerObject.h"
#include<string.h>
#include<stdlib.h>
#include"algorithm.h"
static bool isListNULL(const struct list* Object, const char* str)
{
	if (Object == NULL)
	{
		perror("%s成员函数调用的对象为NULL", str);
		return true;
	}
	return false;
}
//其他
bool List_empty(const list* this_list)
{
	LIST* list=(LIST*)this_list;
	if(!isListNULL(list,"empty"))
		return ContainerObject_empty(&list->object);
	return true;
}

size_t List_size(const list* this_list)
{
	LIST* list=(LIST*)this_list;
	if (!isListNULL(list, "empty"))
		return ContainerObject_size(&list->object);
	return 0;
}

void List_sort(list* this_list, bool(*Sort)(void* x, void* y))
{
	LIST* list=(LIST*)this_list;
	//char* p1=NULL;//指向第一个元素
	//char* p2=NULL;
	//size_t Pl = malloc(sizeof(void*) * list->_current);//创建临时数组保存数据的地址
	//Node* p = list->_data;
	//for (size_t i = 0; i < list->_current; i++)//按照顺序将链表的数据地址保存到数组
	//{
	//	memcpy(Pl + i * list->_type,&(p->date), sizeof(void*));
	//	p=p->next;
	//}
	////qsort(Pl, list->_current, list->_type, Sort);
	//for (size_t n1 = 0; n1 <list->size(li)-1; n1++)
	//{
	//	//p1 = list->at(li, n1);
	//	memcpy(&p1, Pl + n1 * list->_type, sizeof(void*));//将数组中的地址拿出来
	//	for (size_t n2 = n1+1; n2 <list->size(li); n2++)
	//	{
	//		//p2 = list->at(li, n2);
	//		memcpy(&p2, Pl + n2 * list->_type, sizeof(void*));//将数组中的地址拿出来
	//		if (!Sort(p1, p2))//排序比较函数，返回布尔值
	//		{
	//			swap(p1, p2, list->_type);//交换函数
	//		}
	//	}
	//}
	//free(Pl);
}

void List_swap(list* this_ListOne, list* this_ListTwo)
{
	LIST* list1=(LIST*)this_ListOne;
	LIST* list2=(LIST*)this_ListTwo;
	if (!(isListNULL(list1, "this_ListOne-swap")|| isListNULL(list2, "this_ListTwo-swap")))
		return ContainerObject_swap(&list1->object,&list2->object);
}

void List_free(list* this_list)
{
	List_clear(this_list);
	free(this_list);
}

