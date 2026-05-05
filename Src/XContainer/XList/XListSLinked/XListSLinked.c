#include "XListSLinked.h"
#if XListSLinked_ON
XListSLinked* XListSLinked_create(size_t typeSize)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XListSLinked* this_list = XMalloc_System(sizeof(XListSLinked));
	XListSLinked_init(this_list, typeSize);
	Set_Class_MemoryFree(this_list, XFree_System);
	return this_list;
}
void XListSLinked_init(XListSLinked* this_list, size_t typeSize)
{
	if (ISNULL(this_list, "") || ISNULL(typeSize, ""))
		return;
	XListBase_init(this_list, typeSize);
	XClassGetVtable(this_list) = XListSLinked_class_init();
	this_list->m_tail = NULL;
}
#endif