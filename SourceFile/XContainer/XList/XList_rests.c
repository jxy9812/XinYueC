#include"XList.h"
#include"XList_head.h"
#include"XContainerObject.h"
#include<string.h>
#include<stdlib.h>
#include"XAlgorithm.h"
//其他
bool XList_empty(const XList* this_list)
{
	XLIST* list=(XLIST*)this_list;
	if(!isObjectNULL(list,"List_empty"))
		return XContainerObject_empty(&list->object);
	return true;
}

size_t XList_size(const XList* this_list)
{
	XLIST* list=(XLIST*)this_list;
	if (!isObjectNULL(list, "List_size"))
		return XContainerObject_size(&list->object);
	return 0;
}

void XList_swap(XList* this_ListOne, XList* this_ListTwo)
{
	XLIST* list1=(XLIST*)this_ListOne;
	XLIST* list2=(XLIST*)this_ListTwo;
	if (!(isObjectNULL(list1, "this_ListOne-List_swap")|| isObjectNULL(list2, "this_ListTwo-List_swap")))
		return XContainerObject_swap(&list1->object,&list2->object);
}

void XList_free(XList* this_list)
{
	if (isObjectNULL(this_list, "List_free"))
		return;
	XList_clear(this_list);
	free(this_list);
}

