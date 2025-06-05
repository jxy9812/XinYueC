#include"XListDLinked.h"
#if XListDLinked_ON
#include"stdlib.h"

XListDLinked* XListDLinked_create(size_t typeSize)
{
	if (ISNULL(typeSize, ""))
		return NULL;
	XListDLinked* this_list = XMemory_malloc(sizeof(XListDLinked));
	XListDLinked_init(this_list, typeSize);
	return this_list;
}
void XListDLinked_init(XListDLinked* this_list, size_t typeSize)
{
	if (ISNULL(this_list, "")||ISNULL(typeSize, ""))
		return;
	XListBase_init(this_list, typeSize);
	XClassGetVtable(this_list) = XListDLinked_class_init();
}

#endif