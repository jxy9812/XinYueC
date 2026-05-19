#include"XListDLinked.h"
#if XListDLinked_ON
#include"stdlib.h"

XListDLinked* XListDLinked_create_ex(size_t typeSize, bool useCow)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XListDLinked* this_list = XMalloc_System(sizeof(XListDLinked));
	XListDLinked_init(this_list, typeSize, useCow);
	Set_Class_MemoryFree(this_list, XFree_System);
	return this_list;
}
void XListDLinked_init(XListDLinked* this_list, size_t typeSize, bool useCow)
{
	if (ISNULL(this_list, "")||ISNULL(typeSize, ""))
		return;
	XListBase_init(this_list, typeSize, useCow);
	XClassGetVtable(this_list) = XListDLinked_class_init();
	XContainerDataPtr(this_list) = NULL;
}

#endif