#include "XVariantList.h"
//static bool equality(XVariant** var, XVariant** cmp)
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
	vector->m_vector.m_equality = XVariant_equality;
	return vector;
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
}