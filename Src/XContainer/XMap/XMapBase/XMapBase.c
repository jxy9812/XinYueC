#include"XMapBase.h"
#include"XVariant.h"
#if XMap_ON
XVtable* XMapBase_class_init()
{
	return XContainerObject_class_init();
}

void XMapBase_init(XMapBase* this_map, const size_t keyTypeSize, const size_t valTypeSize, XEquality KeyEquality, XLess KeyLess)
{
	if (ISNULL(this_map, ""))
		return NULL;
	if (keyTypeSize == 0 || valTypeSize == 0)
	{
		printf("类型参数不能为0");
		return NULL;
	}
	if (KeyEquality == NULL )
	{
		printf("KeyEquality相等比较函数NULL");
		return NULL;
	}
	if (ISNULL(this_map, ""))
		return NULL;
	XContainerObject_init(&this_map->m_parent, valTypeSize);
	XClassGetVtable(this_map) = XMapBase_class_init();
	this_map->m_keyTypeSize = keyTypeSize;
	this_map->m_KeyEquality = KeyEquality;
	this_map->m_KeyLess = KeyLess;
}
bool XMapBase_insert_base(XMapBase* this_map, const void* pvKey, const void* pvValue)
{
	if (ISNULL(this_map, "") || ISNULL(pvKey, "") || ISNULL(pvValue, "") || ISNULL(XClassGetVtable(this_map), ""))
		return false;
	return XClassGetVirtualFunc(this_map, EXMapBase_Insert, bool(*)(XMapBase* ,const void*, const void*))(this_map, pvKey, pvValue);
}
void XMapBase_erase_base(XMapBase* this_map, const XPair* pPair)
{
	if (ISNULL(this_map, "") || ISNULL(pPair, "") || ISNULL(XClassGetVtable(this_map), ""))
		return;
	XClassGetVirtualFunc(this_map, EXMapBase_Erase, void(*)(XMapBase* ,const XPair*))(this_map, pPair);
}
bool XMapBase_remove_base(XMapBase* this_map, const void* pvKey)
{
	if (ISNULL(this_map, "") || ISNULL(pvKey, "") || ISNULL(XClassGetVtable(this_map), ""))
		return false;
	return XClassGetVirtualFunc(this_map, EXMapBase_Remove, bool(*)(XMapBase* , const void*))(this_map, pvKey);
}
void* XMapBase_value_base(XMapBase* this_map, const void* pvKey)
{
	if (ISNULL(this_map, "") || ISNULL(pvKey, "") || ISNULL(XClassGetVtable(this_map), ""))
		return NULL;
	return XClassGetVirtualFunc(this_map, EXMapBase_Value, void*(*)(XMapBase* ,const void*))(this_map, pvKey);
}
XPair* XMapBase_find_base(XMapBase* this_map, const void* pvKey)
{
	if (ISNULL(this_map, "") || ISNULL(pvKey, "") || ISNULL(XClassGetVtable(this_map), ""))
		return NULL;
	return XClassGetVirtualFunc(this_map, EXMapBase_Find, void* (*)(XMapBase*, const void*))(this_map, pvKey);
}

bool XMapBase_contains(XMapBase* this_map, const void* pvKey)
{
	return XMapBase_find_base(this_map,pvKey)!=NULL;
}

XVector* XMapBase_keys_base(const XMapBase* this_map)
{
	if (ISNULL(this_map, "")  || ISNULL(XClassGetVtable(this_map), ""))
		return NULL;
	return XClassGetVirtualFunc(this_map, EXMapBase_Keys, void* (*)(const XMapBase*))(this_map);
}

void XMapBase_KeyDeleteMethod(void* args)
{
	XPair* pair = (XPair*)args;
	XContainerObject* object = *((XContainerObject**)XPair_first(pair));
	XContainerObject_delete_base(object);
}

void XMapBase_XTreeNodeDataDeleteMethod(void* args)
{
	XPair* pair = (XPair*)args;
	XContainerObject* object = *((XContainerObject**)XPair_second(pair));
	XContainerObject_delete_base(object);
}

void XMapBase_ValueXVariantDeleteMethod(void* args)
{
	XPair* pair = (XPair*)args;
	XVariant* var = *((XVariant**)XPair_second(pair));
	XVariant_delete(var);
}

void XMapBase_KeyXTreeNodeDataDeleteMethod(void* args)
{
	XMapBase_KeyDeleteMethod(args);
	XMapBase_XTreeNodeDataDeleteMethod(args);
}

void XMapBase_KeyXStringValueXVariantDeleteMethod(void* args)
{
	XMapBase_KeyDeleteMethod(args);
	XMapBase_ValueXVariantDeleteMethod(args);
}

#endif

