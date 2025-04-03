#include"XList.h"
#if XList_ON
#include"stdlib.h"
//插入
XListNode* XList_push_front(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return NULL;
	typedef XListNode* (*funcPtr)(XList* , void* );
	return ObjectVirtualFunc(this_list, EXList_Push_Front, funcPtr)(this_list, LpValue);
}

XListNode* XList_push_back(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return NULL;
	typedef XListNode* (*funcPtr)(XList*, void*);
	return ObjectVirtualFunc(this_list, EXList_Push_Back, funcPtr)(this_list, LpValue);
}

void XList_inserts(XList* this_list, XListNode* curNode, void* LpValue, size_t n)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return ;
	typedef void(*funcPtr)(XList*, XListNode*, void* , size_t );
	ObjectVirtualFunc(this_list, EXList_Inserts, funcPtr)(this_list, curNode, LpValue,n);
}

void XList_insert(XList* this_list, XListNode* curNode, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return ;
	typedef void(*funcPtr)(XList*, XListNode*, void*);
	ObjectVirtualFunc(this_list, EXList_Insert, funcPtr)(this_list, curNode, LpValue);
}

void XList_insert_array(XList* this_list, XListNode* curNode, const void* begin, size_t n)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*, XListNode*, const void* , const void* );
	ObjectVirtualFunc(this_list, EXList_Insert_Array, funcPtr)(this_list, curNode,begin,n);
}

void XList_pop_front(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*);
	ObjectVirtualFunc(this_list, EXList_Pop_Front, funcPtr)(this_list);
}

void XList_pop_back(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*);
	ObjectVirtualFunc(this_list, EXList_Pop_Back, funcPtr)(this_list);
}

void XList_erase(XList* this_list, XListNode* node)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*, XListNode*);
	ObjectVirtualFunc(this_list, EXList_Erase, funcPtr)(this_list,node);
}

void XList_remove(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*, void*);
	ObjectVirtualFunc(this_list, EXList_Remove, funcPtr)(this_list, LpValue);
}

void XList_clear(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*);
	ObjectVirtualFunc(this_list, EXList_Clear, funcPtr)(this_list);
}

//遍历
void* XList_front(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return NULL;
	typedef void* (*funcPtr)(XList*);
	return ObjectVirtualFunc(this_list, EXList_Front, funcPtr)(this_list);
}

void* XList_back(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return NULL;
	typedef void* (*funcPtr)(XList*);
	return ObjectVirtualFunc(this_list, EXList_Back, funcPtr)(this_list);
}

XListNode* XList_find(const XList* this_list, const void* findVal)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return NULL;
	typedef XListNode* (*funcPtr)(XList*, const void*);
	return ObjectVirtualFunc(this_list, EXList_Find, funcPtr)(this_list, findVal);
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
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*,XCompare);
	ObjectVirtualFunc(this_list, EXList_Sort, funcPtr)(this_list, compare);
}

void XList_swap(XList* this_listOne, XList* this_listTwo)
{
	return XContainerObject_swap(this_listOne, this_listTwo);
}

void XList_free(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(ObjectVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*);
	ObjectVirtualFunc(this_list, EXContainerObject_Free, funcPtr)(this_list);
}

XList* XList_new(size_t typeSize)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XList* this_list = XMemory_malloc(sizeof(XList));
	XList_init(this_list, typeSize);
	return this_list;
}
void XList_init(XList* this_list, size_t typeSize)
{
	if (ISNULL(this_list, "")||ISNULL(typeSize, ""))
		return;
	XContainerObject_init(this_list, typeSize);
	XList_class_init();
	ObjectVtable(this_list) = XListVtable;
	this_list->m_equality = NULL;
}

#endif