#include"XMapBase.h"
#if XMap_ON
#include"XVariant.h"
#include"XString.h"
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
	return XClassGetVirtualFunc(this_map, EXMapBase_Insert_Copy, bool(*)(XMapBase* ,const void*, const void*))(this_map, pvKey, pvValue);
}
bool XMapBase_insert_move_base(XMapBase* this_map, const void* pvKey, const void* pvValue)
{
	if (ISNULL(this_map, "") || ISNULL(pvKey, "") || ISNULL(pvValue, "") || ISNULL(XClassGetVtable(this_map), ""))
		return false;
	return XClassGetVirtualFunc(this_map, EXMapBase_Insert_Move, bool(*)(XMapBase*, const void*, const void*))(this_map, pvKey, pvValue);
}
void XMapBase_erase_base(XMapBase* this_map, const XMapBase_iterator* it, XMapBase_iterator* next)
{
	if (ISNULL(this_map, "") || ISNULL(it, "") || ISNULL(XClassGetVtable(this_map), ""))
		return;
	XClassGetVirtualFunc(this_map, EXMapBase_Erase, void(*)(XMapBase* , const XMapBase_iterator* ,XMapBase_iterator*))(this_map, it,next);
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

void XMapBase_KeyClassDeinitMethod(XPair* pair)
{
	XClass* object = (XClass*)XPair_first(pair);
	XClass_deinit_base(object);
}

void XMapBase_ValueClassDeinitMethod(XPair* pair)
{
	XClass* object = (XClass*)XPair_second(pair);
	XClass_deinit_base(object);
}

void XMapBase_ValueXVariantDeleteMethod(XPair* pair)
{
	XVariant* var = (XVariant*)XPair_second(pair);
	XVariant_deinit(var);
}

void XMapBase_ClassDeinitMethod(XPair* pair)
{
	XMapBase_KeyClassDeinitMethod(pair);
	XMapBase_ValueClassDeinitMethod(pair);
}

void XMapBase_XVariantMapCopyMethod(XPair* pair, const XPair* src)
{
	XString* pKey = XPair_first(src);
	XVariant* pVar = XPair_second(src);
	if (((XClass*)XPair_first(pair))->m_vtable == NULL)
	{//新插入
		XString_copy_base(XPair_first(pair), pKey);
		XVariant_copy(XPair_second(pair), pKey);
	}
	else
	{
		XVariant_copy(XPair_second(pair), pVar);
	}
}

void XMapBase_XVariantMapMoveMethod(XPair* pair, XPair* src)
{
	XString* pKey = XPair_first(src);
	XVariant* pVar = XPair_second(src);
	if (((XClass*)XPair_first(pair))->m_vtable == NULL)
	{//新插入
		XString_move_base(XPair_first(pair), pKey);
		XVariant_move(XPair_second(pair), pVar);
	}
	else
	{
		XVariant_move(XPair_second(pair), pVar);
	}
}

void XMapBase_XVariantMapDeinitMethod(XPair* pair)
{
	XMapBase_KeyClassDeinitMethod(pair);
	XMapBase_ValueXVariantDeleteMethod(pair);
}

#endif

