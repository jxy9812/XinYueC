#include"XHashMap.h"
#if XHashMap_ON
#include"XMemory.h"
#include"XString.h"
#include"XVariant.h"
#include<string.h>
XHashMap*XHashMap_create(const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XCompare compare)
{
	XHashMap*map = XMalloc_System(sizeof(XHashMap));
	XHashMap_init(map,keyTypeSize,valTypeSize,hash,compare);
	Set_Class_MemoryFree(map, XFree_System);
	return map;
}
XHashMap* XHashMap_create_copy(const XHashMap* other)
{
	if (other == NULL)
		return NULL;
	XHashMap* hash = XHashMap_create(((XMapBase*)other)->m_keyTypeSize, XContainerTypeSize(other), other->m_hash,XContainerCompare(other));
	if (hash == NULL)
		return NULL;
	XHashMap_copy_base(hash, other);
	return hash;
}
XHashMap* XHashMap_create_move(XHashMap* other)
{
	if (other == NULL)
		return NULL;
	XHashMap* hash = XHashMap_create(((XMapBase*)other)->m_keyTypeSize, XContainerTypeSize(other), other->m_hash,XContainerCompare(other));
	if (hash == NULL)
		return NULL;
	XHashMap_move_base(hash, other);
	return hash;
}
void XHashMap_init(XHashMap*this_map, const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XCompare compare)
{
	if (this_map == NULL)
		return;
	XMapBase_init(this_map, keyTypeSize, valTypeSize, compare,true);
	XClassGetVtable(this_map) = XHashMap_class_init();
	this_map->m_hash = hash;
	XContainerSharedData(this_map) = NULL;
}

XVariantHashMap* XHashMap_create_XVariantHashMap()
{
	XHashMap* hash = XHashMap_Create(XString, XVariant, XString_compare);
	if (hash == NULL)
		return NULL;
	/*XContainerSetDataCopyMethod(hash, XMapBase_XVariantMapCopyMethod);
	XContainerSetDataMoveMethod(hash, XMapBase_XVariantMapMoveMethod);
	XContainerSetDataDeinitMethod(hash, XMapBase_XVariantMapDeinitMethod);*/
	XMapBaseSetKeyCopyMethod(hash, XString_copy_base);
	XMapBaseSetKeyMoveMethod(hash, XString_move_base);
	XMapBaseSetKeyDeinitMethod(hash, XString_deinit_base);

	XContainerSetDataCopyMethod(hash, XVariant_copy_base);
	XContainerSetDataMoveMethod(hash, XVariant_move_base);
	XContainerSetDataDeinitMethod(hash, XVariant_deinit_base);
	return hash;
}

#endif
