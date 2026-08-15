#include"XListDLinked.h"
#if XListDLinked_ON
#include"stdlib.h"

XListDLinked* XListDLinked_create_ex(XMemoryType memory, size_t typeSize, bool useCow)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XListDLinked* this_list = XMemory_malloc(sizeof(XListDLinked), memory);
	XListDLinked_init(this_list, typeSize, useCow);
	Set_Class_Memory(this_list, memory); Set_Class_IsHeap(this_list, true);
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