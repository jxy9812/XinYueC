#include "XVariantList.h"
//static bool compare(XVariant** var, XVariant** cmp)
//{
//	if (var == NULL || cmp == NULL)
//		return false;
//	return XVariant_equality(*var,*cmp);
//}
XVtable* XVariantList_class_init()
{
	return XVector_class_init();
}

XVariantList* XVariantList_create()
{
	XVariantList* vector = XMemory_malloc(sizeof(XVariantList));
	XVariantList_init(vector);
	
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
////释放数据方法
//static void DataDeleteMethod(XVariant* var)
//{
//	XVariant_deinit(var);
//}
void XVariantList_init(XVariantList* list)
{
	if (list == NULL)
		return;
	XVector_init(list, sizeof(XVariant));
	XClassGetVtable(list) = XVariantList_class_init();
	XContainerSetDataCopyMethod(list, XVariant_copy);
	XContainerSetDataMoveMethod(list, XVariant_move);
	XContainerSetDataDeinitMethod(list, XVariant_deinit);
	XContainerSetCompare(list, XCompare_ptr);
	//list->m_vector.m_equality = XVariant_equality;
}