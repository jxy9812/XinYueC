#include "XListBase.h"
#if XList_ON
void XListBase_init(XListBase* this_list, size_t typeSize)
{
	if (ISNULL(this_list, "") || ISNULL(typeSize, ""))
		return;
	XContainerObject_init(this_list, typeSize);
	XClassGetVtable(this_list) = XContainerObject_class_init();
	this_list->m_equality = NULL;
}
XListBaseNode* XListBase_push_front_base(XListBase* this_list, void* pvData)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	return XClassGetVirtualFunc(this_list, EXListBase_Push_Front, XListBaseNode*(*)(XListBase*, void*))(this_list, pvData);
}
XListBaseNode* XListBase_push_back_base(XListBase* this_list, void* pvData)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	return XClassGetVirtualFunc(this_list, EXListBase_Push_Back, XListBaseNode* (*)(XListBase*, void*))(this_list, pvData);
}
void XListBase_insert_base(XListBase* this_list, XListBaseNode* curNode, void* pvData)
{
	if (ISNULL(this_list, "") || ISNULL(curNode, "") || ISNULL(pvData, "") || ISNULL(XClassGetVtable(this_list), ""))
		return ;
	XClassGetVirtualFunc(this_list, EXListBase_Insert, void(*)(XListBase*, XListBaseNode *, void*))(this_list, curNode,pvData);
}
void XListBase_insert_array_base(XListBase* this_list, XListBaseNode* curNode, const void* array, size_t size)
{
	if (ISNULL(this_list, "") || ISNULL(curNode, "") || ISNULL(array, "") || ISNULL(size, "") || ISNULL(XClassGetVtable(this_list), ""))
		return;
	XClassGetVirtualFunc(this_list, EXListBase_Insert_Array, void(*)(XListBase*, XListBaseNode*, const void*, size_t))(this_list, curNode, array,size);
}
void XListBase_pop_front_base(XListBase* this_list)
{
	if (ISNULL(this_list, "") ||ISNULL(XClassGetVtable(this_list), ""))
		return;
	XClassGetVirtualFunc(this_list, EXListBase_Pop_Front, void(*)(XListBase*))(this_list);
}
void XListBase_pop_back_base(XListBase* this_list)
{
	if (ISNULL(this_list, "") || ISNULL(XClassGetVtable(this_list), ""))
		return;
	XClassGetVirtualFunc(this_list, EXListBase_Pop_Back, void(*)(XListBase*))(this_list);
}
void XListBase_erase_base(XListBase* this_list, XListBaseNode* node)
{
	if (ISNULL(this_list, "") || ISNULL(node, "") || ISNULL(XClassGetVtable(this_list), ""))
		return;
	XClassGetVirtualFunc(this_list, EXListBase_Erase, void(*)(XListBase*, XListBaseNode*))(this_list,node);
}
void XListBase_remove_base(XListBase* this_list, void* pvData)
{
	if (ISNULL(this_list, "") || ISNULL(pvData, "") || ISNULL(XClassGetVtable(this_list), ""))
		return;
	XClassGetVirtualFunc(this_list, EXListBase_Erase, void(*)(XListBase*, void*))(this_list, pvData);
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
XListBaseNode* XListBase_find_base(const XListBase* this_list, const void* findVal)
{
	if (ISNULL(this_list, "") || ISNULL(findVal, "") || ISNULL(XClassGetVtable(this_list), ""))
		return NULL;
	return XClassGetVirtualFunc(this_list, EXListBase_Find, void* (*)(XListBase*, const void*))(this_list, findVal);
}
#endif