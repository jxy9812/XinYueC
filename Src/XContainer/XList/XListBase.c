#include "XListBase.h"
#if XList_ON
void XListBase_init(XListBase* this_list, size_t typeSize)
{
	if (ISNULL(this_list, "") || ISNULL(typeSize, ""))
		return;
	XContainerObject_init(this_list, typeSize);
	XClassGetVtable(this_list) = XContainerObject_class_init();
	//this_list->m_equality = NULL;
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
bool XListBase_contains(const XListBase* this_list, const void* value)
{
	return XListBase_find_base(this_list,value,NULL);
}
void XListBase_sort_base(XListBase* this_list, XSortOrder order)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	return XClassGetVirtualFunc(this_list, EXListBase_Sort, void* (*)(XListBase*, XSortOrder))(this_list, order);
}
#endif