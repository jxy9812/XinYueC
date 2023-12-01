#include"XList.h"
#include"XContainerObject.h"
#include<string.h>
#include<stdlib.h>
#include"XAlgorithm.h"
//其他
bool XList_empty(const XList* this_list)
{
	XList* list = this_list;
	if (isNULL(isNULLInfo(this_list, "")))
		return true;
	return XContainerObject_empty(&list->object);
}

size_t XList_size(const XList* this_list)
{
	XList* list = this_list;
	if (isNULL(isNULLInfo(this_list, "")))
		return 0;
	return XContainerObject_size(&list->object);

}

void XList_swap(XList* this_ListOne, XList* this_ListTwo)
{
	if (!(isNULL(isNULLInfo(this_ListOne, ""))|| isNULL(isNULLInfo(this_ListTwo, ""))))
		return XContainerObject_swap(&this_ListOne->object,&this_ListTwo->object);
}

void XList_free(XList* this_list)
{
	if (isNULL(isNULLInfo(this_list, "")))
		return;
	XList_clear(this_list);
	free(this_list);
}

