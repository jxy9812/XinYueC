#include "XListBase.h"
#if XList_ON
void XListBase_init(XListBase* this_list, size_t typeSize, bool useCow)
{
	if (ISNULL(this_list, "") || ISNULL(typeSize, ""))
		return;
	XContainer_init(this_list, typeSize, useCow);
	XClassGetVtable(this_list) = XContainer_class_init();
}
bool XListBase_push_front_node_base(XListBase* this_list, XListBaseNode* node)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return false;
	return XClassGetVirtualFunc(this_list, EXListBase_Push_Front_Node,bool(*)(XListBase*, XListBaseNode*))(this_list, node);
}
XListBaseNode* XListBase_push_front_base(XListBase* this_list, void* pvData)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	return XClassGetVirtualFunc(this_list, EXListBase_Push_Front, XListBaseNode*(*)(XListBase*, void*, XCDataCreatMethod))(this_list, pvData, XContainerDataCopyMethod(this_list));
}
XListBaseNode* XListBase_push_front_move_base(XListBase* this_list, void* pvData)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	return XClassGetVirtualFunc(this_list, EXListBase_Push_Front, XListBaseNode * (*)(XListBase*, void*, XCDataCreatMethod))(this_list, pvData,XContainerDataMoveMethod(this_list));
}
bool XListBase_push_back_node_base(XListBase* this_list, XListBaseNode* node)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return false;
	return XClassGetVirtualFunc(this_list, EXListBase_Push_Back_Node,bool(*)(XListBase*, XListBaseNode*))(this_list, node);
}
XListBaseNode* XListBase_push_back_base(XListBase* this_list, void* pvData)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	return XClassGetVirtualFunc(this_list, EXListBase_Push_Back, XListBaseNode* (*)(XListBase*, void*, XCDataCreatMethod))(this_list, pvData, XContainerDataCopyMethod(this_list));
}
XListBaseNode* XListBase_push_back_move_base(XListBase* this_list, void* pvData)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	return XClassGetVirtualFunc(this_list, EXListBase_Push_Back, XListBaseNode * (*)(XListBase*, void*, XCDataCreatMethod))(this_list, pvData, XContainerDataMoveMethod(this_list));
}
bool XListBase_insert_base(XListBase* this_list, XListBaseNode* curNode, void* pvData)
{
	if (ISNULL(this_list, "") || ISNULL(curNode, "") || ISNULL(pvData, "") || ISNULL(XClassGetVtable(this_list), ""))
		return false;
	return XClassGetVirtualFunc(this_list, EXListBase_Insert, bool(*)(XListBase*, XListBaseNode *, void*, XCDataCreatMethod))(this_list, curNode,pvData, XContainerDataCopyMethod(this_list));
}
bool XListBase_insert_move_base(XListBase* this_list, XListBaseNode* curNode, void* pvData)
{
	if (ISNULL(this_list, "") || ISNULL(curNode, "") || ISNULL(pvData, "") || ISNULL(XClassGetVtable(this_list), ""))
		return false;
	return XClassGetVirtualFunc(this_list, EXListBase_Insert, bool(*)(XListBase*, XListBaseNode*, void*, XCDataCreatMethod))(this_list, curNode, pvData, XContainerDataMoveMethod(this_list));
}
size_t XListBase_insert_array_base(XListBase* this_list, XListBaseNode* curNode,void* array, size_t count)
{
	if (ISNULL(this_list, "")  || ISNULL(array, "") || ISNULL(count, "") || ISNULL(XClassGetVtable(this_list), ""))
		return 0;
	return XClassGetVirtualFunc(this_list, EXListBase_Insert_Array, size_t(*)(XListBase*, XListBaseNode*,void*, size_t, XCDataCreatMethod))(this_list, curNode, array, count, XContainerDataCopyMethod(this_list));
}
size_t XListBase_insert_array_move_base(XListBase* this_list, XListBaseNode* curNode,void* array, size_t count)
{
	if (ISNULL(this_list, "") || ISNULL(array, "") || ISNULL(count, "") || ISNULL(XClassGetVtable(this_list), ""))
		return 0;
	return XClassGetVirtualFunc(this_list, EXListBase_Insert_Array, size_t(*)(XListBase*, XListBaseNode*,void*, size_t, XCDataCreatMethod))(this_list, curNode, array, count, XContainerDataMoveMethod(this_list));
}
bool XListBase_pop_front_base(XListBase* this_list)
{
	if (ISNULL(this_list, "") ||ISNULL(XClassGetVtable(this_list), ""))
		return false;
	return XClassGetVirtualFunc(this_list, EXListBase_Pop_Front, bool(*)(XListBase*))(this_list);
}
bool XListBase_pop_back_base(XListBase* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return false;
	XClassGetVirtualFunc(this_list, EXListBase_Pop_Back, bool(*)(XListBase*))(this_list);
}
void XListBase_erase_base(XListBase* this_list,const XListBase_iterator* it, XListBase_iterator* next)
{
	if (ISNULL(this_list, "") || ISNULL(it, "") || ISNULL(XClassGetVtable(this_list), ""))
		return;
	XClassGetVirtualFunc(this_list, EXListBase_Erase, void(*)(XListBase*, XListBase_iterator*, XListBase_iterator*))(this_list, it,next);
}
bool XListBase_remove_base(XListBase* this_list, void* pvData)
{
	if (ISNULL(this_list, "") || ISNULL(pvData, "") || ISNULL(XClassGetVtable(this_list), ""))
		return false;
	return XClassGetVirtualFunc(this_list, EXListBase_Remove, bool(*)(XListBase*, void*))(this_list, pvData);
}
void* XListBase_front_base(XListBase* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	return XClassGetVirtualFunc(this_list, EXListBase_Front, void* (*)(XListBase*))(this_list);
}
void* XListBase_back_base(XListBase* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	return XClassGetVirtualFunc(this_list, EXListBase_Back, void* (*)(XListBase*))(this_list);
}
bool XListBase_find_base(const XListBase* this_list, const void* findVal, XListBase_iterator* it)
{
	if (ISNULL(this_list, "") || ISNULL(findVal, "") || ISNULL(XClassGetVtable(this_list), ""))
		return false;
	return XClassGetVirtualFunc(this_list, EXListBase_Find, bool(*)(XListBase*, const void*, XListBase_iterator*))(this_list, findVal,it);
}
/*     Qt 6.8 对齐：takeFirst / takeLast（基层实现）+ count / removeAll / removeOne / equals（虚函数调度） */
/* ========================================================================== */

void* XListBase_takeFirst_base(XListBase* this_list)
{
	if (ISNULL(this_list, "") || XListBase_isEmpty_base(this_list))
		return NULL;
	void* frontData = XListBase_front_base(this_list);
	if (!frontData) return NULL;
	size_t typeSize = XListBase_typeSize_base(this_list);
	void* result = XMalloc_System(typeSize);
	if (!result) return NULL;
	memcpy(result, frontData, typeSize);
	XListBase_pop_front_base(this_list);
	return result;
}

void* XListBase_takeLast_base(XListBase* this_list)
{
	if (ISNULL(this_list, "") || XListBase_isEmpty_base(this_list))
		return NULL;
	void* backData = XListBase_back_base(this_list);
	if (!backData) return NULL;
	size_t typeSize = XListBase_typeSize_base(this_list);
	void* result = XMalloc_System(typeSize);
	if (!result) return NULL;
	memcpy(result, backData, typeSize);
	XListBase_pop_back_base(this_list);
	return result;
}


size_t XListBase_removeAll_base(XListBase* this_list, const void* pvData)
{
	if (ISNULL(this_list, "") || ISNULL(pvData, "") || ISNULL(XClassGetVtable(this_list), ""))
		return 0;
	return XClassGetVirtualFunc(this_list, EXListBase_Remove_All, size_t(*)(XListBase*, const void*))(this_list, pvData);
}

bool XListBase_removeOne_base(XListBase* this_list, const void* pvData)
{
	if (ISNULL(this_list, "") || ISNULL(pvData, "") || ISNULL(XClassGetVtable(this_list), ""))
		return false;
	return XClassGetVirtualFunc(this_list, EXListBase_Remove_One, bool(*)(XListBase*, const void*))(this_list, pvData);
}

#endif

bool XListBase_contains(const XListBase* this_list, const void* value)
{
	return XListBase_find_base(this_list, value, NULL);
}

void XListBase_sort_base(XListBase* this_list, XSortOrder order)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return;
	XClassGetVirtualFunc(this_list, EXListBase_Sort, void(*)(XListBase*, XSortOrder))(this_list, order);
}


/* ========================================================================== */
/*     Qt 6.8 对齐：indexOf / lastIndexOf / removeIf（虚函数调度）              */
/* ========================================================================== */

bool XListBase_indexOf_base(const XListBase* this_list, const void* findVal, size_t from, XListBase_iterator* it)
{
	if (ISNULL(this_list, "") || ISNULL(findVal, "") || ISNULL(XClassGetVtable(this_list), ""))
		return false;
	return XClassGetVirtualFunc(this_list, EXListBase_IndexOf, bool(*)(const XListBase*, const void*, size_t, XListBase_iterator*))(this_list, findVal, from, it);
}

bool XListBase_lastIndexOf_base(const XListBase* this_list, const void* findVal, size_t from, XListBase_iterator* it)
{
	if (ISNULL(this_list, "") || ISNULL(findVal, "") || ISNULL(XClassGetVtable(this_list), ""))
		return false;
	return XClassGetVirtualFunc(this_list, EXListBase_LastIndexOf, bool(*)(const XListBase*, const void*, size_t, XListBase_iterator*))(this_list, findVal, from, it);
}

size_t XListBase_removeIf_base(XListBase* this_list, bool (*predicate)(const void* elemData, void* userData), void* userData)
{
	if (ISNULL(this_list, "") || ISNULL(predicate, "") || ISNULL(XClassGetVtable(this_list), ""))
		return 0;
	return XClassGetVirtualFunc(this_list, EXListBase_RemoveIf, size_t(*)(XListBase*, bool (*)(const void*, void*), void*))(this_list, predicate, userData);
}
