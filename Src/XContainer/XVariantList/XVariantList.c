#include "XVariantList.h"
XVtable* XVariantList_class_init()
{
	return XVector_class_init();
}

XVariantList* XVariantList_create()
{
	XVariantList* vector = XMalloc_System(sizeof(XVariantList));
	XVariantList_init(vector);
	Set_Class_MemoryFree(vector, XFree_System);
	return vector;
}
XVariantList* XVariantList_create_copy(const XVariantList* other)
{
	if (other == NULL)
		return NULL;
	XVariantList* list = XVariantList_create();
	if (list == NULL)
		return NULL;
	XVariantList_copy_base(list, other);
	return list;
}
XVariantList* XVariantList_create_move(XVariantList* other)
{
	if (other == NULL)
		return NULL;
	XVariantList* list = XVariantList_create();
	if (list == NULL)
		return NULL;
	XVariantList_move_base(list, other);
	return list;
}
void XVariantList_init(XVariantList* list)
{
	if (list == NULL)
		return;
	XVector_init(list, sizeof(XVariant),true);
	XClassGetVtable(list) = XVariantList_class_init();
	XContainerSetDataCopyMethod(list, XVariant_copy_base);
	XContainerSetDataMoveMethod(list, XVariant_move_base);
	XContainerSetDataDeinitMethod(list, XVariant_deinit_base);
	XContainerSetCompare(list, uintptr_t_compare);
}