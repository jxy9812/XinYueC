#include"XList.h"
#include"stdlib.h"
#include"XContainerObject_virtual.h"
#include"XList_virtual.h"
//虚函数表
void* XListVtable[] = { 
	XVContainerObject_empty,XVContainerObject_size,XVContainerObject_capacity,XVContainerObject_type,XVContainerObject_swap,XVContainerObject_free,
	XVList_push_front,XVList_push_back,XVList_insert
};
//插入
XListNode* XList_push_front(XList* this_list, void* LPValue)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return NULL;
	typedef XListNode* (*funcPtr)(XList* , void* );
	return ObjectVirtualFunc(this_list, Push_Front, funcPtr)(this_list, LPValue);
}

XListNode* XList_push_back(XList* this_list, void* LPValue)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return NULL;
	typedef XListNode* (*funcPtr)(XList*, void*);
	return ObjectVirtualFunc(this_list, Push_Back, funcPtr)(this_list, LPValue);
}

void XList_insert(XList* this_list, XListNode* curNode, const void* begin, const void* end)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return;
	typedef void (*funcPtr)(XList*, XListNode*, const void* , const void* );
	ObjectVirtualFunc(this_list, Insert, funcPtr)(this_list, curNode,begin,end);
}

XList* XList_init(int TypeSize)
{
	XList* this_list = malloc(sizeof(XList));
	XContainerObject_init(this_list, TypeSize);
	this_list->object.vtable = XListVtable;
	return this_list;
}