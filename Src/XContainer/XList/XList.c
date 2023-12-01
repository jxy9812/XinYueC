#include"XList.h"
#include"stdlib.h"
//插入
XListNode* XList_push_front(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return NULL;
	typedef XListNode* (*funcPtr)(XList* , void* );
	return ObjectVirtualFunc(this_list, Push_Front, funcPtr)(this_list, LpValue);
}

XListNode* XList_push_back(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return NULL;
	typedef XListNode* (*funcPtr)(XList*, void*);
	return ObjectVirtualFunc(this_list, Push_Back, funcPtr)(this_list, LpValue);
}

void XList_inserts(XList* this_list, XListNode* curNode, void* LpValue, size_t n)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return NULL;
	typedef void(*funcPtr)(XList*, XListNode*, void* , size_t );
	ObjectVirtualFunc(this_list, Inserts, funcPtr)(this_list, curNode, LpValue,n);
}

void XList_insert(XList* this_list, XListNode* curNode, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return NULL;
	typedef void(*funcPtr)(XList*, XListNode*, void*);
	ObjectVirtualFunc(this_list, Insert, funcPtr)(this_list, curNode, LpValue);
}

void XList_insertArray(XList* this_list, XListNode* curNode, const void* begin, size_t n)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return;
	typedef void (*funcPtr)(XList*, XListNode*, const void* , const void* );
	ObjectVirtualFunc(this_list, InsertArray, funcPtr)(this_list, curNode,begin,n);
}

void XList_pop_front(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return;
	typedef void (*funcPtr)(XList*);
	ObjectVirtualFunc(this_list, Pop_Front, funcPtr)(this_list);
}

void XList_pop_back(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return;
	typedef void (*funcPtr)(XList*);
	ObjectVirtualFunc(this_list, Pop_Back, funcPtr)(this_list);
}

void XList_erase(XList* this_list, XListNode* node)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return;
	typedef void (*funcPtr)(XList*, XListNode*);
	ObjectVirtualFunc(this_list, Erase, funcPtr)(this_list,node);
}

void XList_remove(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return;
	typedef void (*funcPtr)(XList*, void*);
	ObjectVirtualFunc(this_list, Remove, funcPtr)(this_list, LpValue);
}

void XList_clear(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return;
	typedef void (*funcPtr)(XList*);
	ObjectVirtualFunc(this_list, Clear, funcPtr)(this_list);
}

//遍历
XListNode* XList_at(const XList* this_list, const void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return;
	typedef void (*funcPtr)(XList*, const void* );
	ObjectVirtualFunc(this_list, At, funcPtr)(this_list, LpValue);
}


XList* XList_init(int TypeSize, XEquality equality, XLess less)
{
	if (ISNULL(TypeSize, "") )
		return;
	XList* this_list = malloc(sizeof(XList));
	XContainerObject_init(this_list, TypeSize);
	this_list->object.vtable = XListVtable;
	this_list->equality= equality;
	this_list->less= less;
	return this_list;
}

