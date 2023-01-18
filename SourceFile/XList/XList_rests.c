#include"XList.h"
#include"XList_head.h"
#include"XContainerObject.h"
#include<string.h>
#include<stdlib.h>
#include"XAlgorithm.h"
static bool isListNULL(const struct XList* Object, const char* str)
{
	if (Object == NULL)
	{
		perror("%s成员函数调用的对象为NULL", str);
		return true;
	}
	return false;
}
//其他
bool List_empty(const XList* this_list)
{
	XLIST* list=(XLIST*)this_list;
	if(!isListNULL(list,"List_empty"))
		return XContainerObject_empty(&list->object);
	return true;
}

size_t List_size(const XList* this_list)
{
	XLIST* list=(XLIST*)this_list;
	if (!isListNULL(list, "List_size"))
		return XContainerObject_size(&list->object);
	return 0;
}

void List_swap(XList* this_ListOne, XList* this_ListTwo)
{
	XLIST* list1=(XLIST*)this_ListOne;
	XLIST* list2=(XLIST*)this_ListTwo;
	if (!(isListNULL(list1, "this_ListOne-List_swap")|| isListNULL(list2, "this_ListTwo-List_swap")))
		return XContainerObject_swap(&list1->object,&list2->object);
}

void List_free(XList* this_list)
{
	if (isListNULL(this_list, "List_free"))
		return;
	List_clear(this_list);
	free(this_list);
}

