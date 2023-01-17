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
	if(!isListNULL(list,"List_empty"))
		return ContainerObject_empty(&list->object);
	return true;
}

size_t List_size(const list* this_list)
{
	LIST* list=(LIST*)this_list;
	if (!isListNULL(list, "List_size"))
		return ContainerObject_size(&list->object);
	return 0;
}

void List_swap(list* this_ListOne, list* this_ListTwo)
{
	LIST* list1=(LIST*)this_ListOne;
	LIST* list2=(LIST*)this_ListTwo;
	if (!(isListNULL(list1, "this_ListOne-List_swap")|| isListNULL(list2, "this_ListTwo-List_swap")))
		return ContainerObject_swap(&list1->object,&list2->object);
}

void List_free(list* this_list)
{
	if (isListNULL(this_list, "List_free"))
		return;
	List_clear(this_list);
	free(this_list);
}

