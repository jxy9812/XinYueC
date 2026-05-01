#include"XMapBase.h"
#if XMap_ON
#include"XVariant.h"
#include"XString.h"
XVtable* XMapBase_class_init()
{
	return XContainer_class_init();
}

void XMapBase_init(XMapBase* this_map, const size_t keyTypeSize, const size_t valTypeSize, XCompare compare)
{
	if (ISNULL(this_map, ""))
		return NULL;
	if (keyTypeSize == 0 || valTypeSize == 0)
	{
		printf("类型参数不能为0");
		return NULL;
	}
	if (ISNULL(this_map, ""))
		return NULL;
	XContainer_init(&this_map->m_class, valTypeSize);
	XClassGetVtable(this_map) = XMapBase_class_init();
	this_map->m_keyTypeSize = keyTypeSize;
	/*this_map->m_KeyEquality = KeyEquality;
	this_map->m_KeyLess = KeyLess;*/
	XContainerSetCompare(this_map,compare);
	this_map->m_keyCopyMethod = NULL;
	this_map->m_keyMoveMethod = NULL;
	this_map->m_keyDeinitMethod = NULL;
}
bool XMapBase_insert_base(XMapBase* this_map, const void* pvKey, const void* pvValue)
{
	if (ISNULL(this_map, "") || ISNULL(pvKey, "") || ISNULL(pvValue, "") || ISNULL(XClassGetVtable(this_map), ""))
		return false;
	return XClassGetVirtualFunc(this_map, EXMapBase_Insert, bool(*)(XMapBase* ,const void*, const void*, XCDataCreatMethod, XCDataCreatMethod))(this_map, pvKey, pvValue, XMapBaseKeyCopyMethod(this_map), XContainerDataCopyMethod(this_map));
}
bool XMapBase_insert_move_base(XMapBase* this_map,void* pvKey,void* pvValue)
{
	if (ISNULL(this_map, "") || ISNULL(pvKey, "") || ISNULL(pvValue, "") || ISNULL(XClassGetVtable(this_map), ""))
		return false;
	return XClassGetVirtualFunc(this_map, EXMapBase_Insert, bool(*)(XMapBase*, const void*, const void*, XCDataCreatMethod, XCDataCreatMethod))(this_map, pvKey, pvValue, XMapBaseKeyMoveMethod(this_map), XContainerDataMoveMethod(this_map));
}
bool XMapBase_insert_keyMove_base(XMapBase* this_map, void* pvKey, const void* pvValue)
{
	if (ISNULL(this_map, "") || ISNULL(pvKey, "") || ISNULL(pvValue, "") || ISNULL(XClassGetVtable(this_map), ""))
		return false;
	return XClassGetVirtualFunc(this_map, EXMapBase_Insert, bool(*)(XMapBase*, const void*, const void*, XCDataCreatMethod, XCDataCreatMethod))(this_map, pvKey, pvValue, XMapBaseKeyMoveMethod(this_map), XContainerDataCopyMethod(this_map));
}
bool XMapBase_insert_valueMove_base(XMapBase* this_map, const void* pvKey, void* pvValue)
{
	if (ISNULL(this_map, "") || ISNULL(pvKey, "") || ISNULL(pvValue, "") || ISNULL(XClassGetVtable(this_map), ""))
		return false;
	return XClassGetVirtualFunc(this_map, EXMapBase_Insert, bool(*)(XMapBase*, const void*, const void*, XCDataCreatMethod, XCDataCreatMethod))(this_map, pvKey, pvValue, XMapBaseKeyCopyMethod(this_map), XContainerDataMoveMethod(this_map));
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
bool XMapBase_find_base(const XMapBase* this_map, const void* pvKey, XMapBase_iterator* it)
{
	if (ISNULL(this_map, "") || ISNULL(pvKey, "") || ISNULL(XClassGetVtable(this_map), ""))
		return NULL;
	return XClassGetVirtualFunc(this_map, EXMapBase_Find, bool (*)(XMapBase*, const void*, XMapBase_iterator*))(this_map, pvKey,it);
}

bool XMapBase_contains(const XMapBase* this_map, const void* pvKey)
{
	return XMapBase_find_base(this_map,pvKey,NULL);
}

XVector* XMapBase_keys_base(const XMapBase* this_map)
{
	if (ISNULL(this_map, "")  || ISNULL(XClassGetVtable(this_map), ""))
		return NULL;
	return XClassGetVirtualFunc(this_map, EXMapBase_Keys, void* (*)(const XMapBase*))(this_map);
}

XVector* XMapBase_values_base(const XMapBase* this_map)
{
	if (ISNULL(this_map, "") || ISNULL(XClassGetVtable(this_map), ""))
		return NULL;
	return XClassGetVirtualFunc(this_map, EXMapBase_Values, void* (*)(const XMapBase*))(this_map);
}

void XMapBase_deleteNodeData(XPair** lpPair, XMapBase* this_map)
{
	if (!lpPair)return;
	XPair* pair = *lpPair;
	if (XMapBaseKeyDeinitMethod(this_map))
		XMapBaseKeyDeinitMethod(this_map)(XPair_first(pair));

	if (XContainerDataDeinitMethod(this_map))
		XContainerDataDeinitMethod(this_map)(XPair_second(pair));
	XPair_delete(pair);
}

#endif

