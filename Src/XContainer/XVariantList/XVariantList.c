#include "XVariantList.h"

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
//释放数据方法
static void DataDeleteMethod(void* args)
{
	XVariant* var = *((XVariant**)args);
	XVariant_delete(var);
}
void XVariantList_init(XVariantList* list)
{
	if (list == NULL)
		return;
	XVector_init(list, sizeof(XVariant*));
	XClassGetVtable(list) = XVariantList_class_init();
	XContainerSetDataDeleteMethod(list, DataDeleteMethod);
}

void XVariantList_push_front_base(XVariantList* list, XVariant* var)
{
	XVector_push_front_base(list, &var);
}

void XVariantList_push_back_base(XVariantList* list, XVariant* var)
{
	XVector_push_back_base(list, &var);
}

void XVariantList_insert_base(XVariantList* list, int64_t index, XVariant* var)
{
	XVector_insert_base(list, index, &var);
}

XVariant* XVariantList_at_base(const XVariantList* list, int64_t index)
{
	XVariant** p = XVector_at_base(list, index);
	if (p == NULL)
		return NULL;
	return *p;
}

XVariant* XVariantList_front_base(const XVariantList* list)
{
	XVariant** p = XVector_front_base(list);
	if (p == NULL)
		return NULL;
	return *p;
}

XVariant* XVariantList_back_base(const XVariantList* list)
{
	XVariant** p = XVector_back_base(list);
	if (p == NULL)
		return NULL;
	return *p;
}
