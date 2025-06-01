#include"XList.h"
#if XList_ON
#include"stdlib.h"

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
	XListBase_init(this_list, typeSize);
	XClassGetVtable(this_list) = XList_class_init();
}

#endif