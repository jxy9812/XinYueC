#include "XListSLinked.h"
#if XListSLinked_ON

XListSLinked* XListSLinked_create_ex(size_t typeSize, bool useCow)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XListSLinked* this_list = XMalloc_System(sizeof(XListSLinked));
	XListSLinked_init(this_list, typeSize, useCow);
	Set_Class_MemoryFree(this_list, XFree_System);
	return this_list;
}
void XListSLinked_init(XListSLinked* this_list, size_t typeSize, bool useCow)
{
	if (ISNULL(this_list, "") || ISNULL(typeSize, ""))
		return;
	XListBase_init(this_list, typeSize, useCow);
	XClassSetVtable(this_list, XListSLinked);
	this_list->m_tail = NULL;
	XContainerDataPtr(this_list) = NULL;
}
#endif