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
	typedef XListNode* (*funcPtr)(XList*, const void* );
	return ObjectVirtualFunc(this_list, At, funcPtr)(this_list, LpValue);
}

XListNode* XList_front(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return;
	typedef XListNode* (*funcPtr)(XList*);
	return ObjectVirtualFunc(this_list, Front, funcPtr)(this_list);
}

XListNode* XList_back(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return;
	typedef XListNode* (*funcPtr)(XList*);
	return ObjectVirtualFunc(this_list, Back, funcPtr)(this_list);
}

XListNode* XList_find(const XList* this_list, const void* findVal)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return;
	typedef XListNode* (*funcPtr)(XList*, const void*);
	return ObjectVirtualFunc(this_list, Find, funcPtr)(this_list, findVal);
}

bool XList_empty(const XList* this_list)
{
	return XContainerObject_empty(this_list);
}

size_t XList_size(const XList* this_list)
{
	return XContainerObject_size(this_list);
}

void XList_sort(XList* this_list, XCompare compare)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return;
	typedef void (*funcPtr)(XList*,XCompare);
	ObjectVirtualFunc(this_list, Sort, funcPtr)(this_list, compare);
}

void XList_swap(XList* this_listOne, XList* this_listTwo)
{
	return XContainerObject_swap(this_listOne, this_listTwo);
}

void XList_free(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(this_list->object.vtable, ""))
		return;
	typedef void (*funcPtr)(XList*);
	ObjectVirtualFunc(this_list, Free, funcPtr)(this_list);
}

XList* XList_init(int TypeSize, XEquality equality)
{
	if (ISNULL(TypeSize, "") )
		return;
	XList* this_list = malloc(sizeof(XList));
	XContainerObject_init(this_list, TypeSize);
	this_list->object.vtable = XListVtable;
	this_list->equality= equality;
	return this_list;
}

