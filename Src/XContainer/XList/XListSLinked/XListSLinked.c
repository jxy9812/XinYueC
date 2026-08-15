#include "XListSLinked.h"
#if XListSLinked_ON

XListSLinked* XListSLinked_create_ex(XMemoryType memory, size_t typeSize, bool useCow)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XListSLinked* this_list = XMemory_malloc(sizeof(XListSLinked), memory);
	XListSLinked_init(this_list, typeSize, useCow);
	Set_Class_Memory(this_list, memory); Set_Class_IsHeap(this_list, true);
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