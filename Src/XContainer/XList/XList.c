#include"XList.h"
#if XList_ON
#include"stdlib.h"
//插入
XListNode* XList_push_front_base(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	typedef XListNode* (*funcPtr)(XList* , void* );
	return XClassGetVirtualFunc(this_list, EXList_Push_Front, funcPtr)(this_list, LpValue);
}

XListNode* XList_push_back_base(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	typedef XListNode* (*funcPtr)(XList*, void*);
	return XClassGetVirtualFunc(this_list, EXList_Push_Back, funcPtr)(this_list, LpValue);
}

void XList_inserts_base(XList* this_list, XListNode* curNode, void* LpValue, size_t n)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return ;
	typedef void(*funcPtr)(XList*, XListNode*, void* , size_t );
	XClassGetVirtualFunc(this_list, EXList_Inserts, funcPtr)(this_list, curNode, LpValue,n);
}

void XList_insert_base(XList* this_list, XListNode* curNode, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return ;
	typedef void(*funcPtr)(XList*, XListNode*, void*);
	XClassGetVirtualFunc(this_list, EXList_Insert, funcPtr)(this_list, curNode, LpValue);
}

void XList_insert_array_base(XList* this_list, XListNode* curNode, const void* begin, size_t n)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*, XListNode*, const void* , const void* );
	XClassGetVirtualFunc(this_list, EXList_Insert_Array, funcPtr)(this_list, curNode,begin,n);
}

void XList_pop_front_base(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*);
	XClassGetVirtualFunc(this_list, EXList_Pop_Front, funcPtr)(this_list);
}

void XList_pop_back_base(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*);
	XClassGetVirtualFunc(this_list, EXList_Pop_Back, funcPtr)(this_list);
}

void XList_erase_base(XList* this_list, XListNode* node)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*, XListNode*);
	XClassGetVirtualFunc(this_list, EXList_Erase, funcPtr)(this_list,node);
}

void XList_remove_base(XList* this_list, void* LpValue)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*, void*);
	XClassGetVirtualFunc(this_list, EXList_Remove, funcPtr)(this_list, LpValue);
}

//遍历
void* XList_front_base(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	typedef void* (*funcPtr)(XList*);
	return XClassGetVirtualFunc(this_list, EXList_Front, funcPtr)(this_list);
}

void* XList_back_base(XList* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	typedef void* (*funcPtr)(XList*);
	return XClassGetVirtualFunc(this_list, EXList_Back, funcPtr)(this_list);
}

XListNode* XList_find_base(const XList* this_list, const void* findVal)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	typedef XListNode* (*funcPtr)(XList*, const void*);
	return XClassGetVirtualFunc(this_list, EXList_Find, funcPtr)(this_list, findVal);
}

void XList_sort_base(XList* this_list, XCompare compare)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return;
	typedef void (*funcPtr)(XList*,XCompare);
	XClassGetVirtualFunc(this_list, EXList_Sort, funcPtr)(this_list, compare);
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
	XClassGetVtable(this_list) = XList_class_init();
	this_list->m_equality = NULL;
}

#endif