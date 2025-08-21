#include"XHashMap.h"
#if XHashMap_ON
#include"XMemory.h"
#include"XString.h"
#include"XVariant.h"
#include<string.h>
XHashMap*XHashMap_create(const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XEquality KeyEquality, XLess KeyLess)
{
	XHashMap*map = XMemory_malloc(sizeof(XHashMap));
	XHashMap_init(map,keyTypeSize,valTypeSize,hash,KeyEquality, KeyLess);
	return map;
}
XHashMap* XHashMap_create_copy(const XHashMap* other)
{
	if (other == NULL)
		return NULL;
	XHashMap* hash = XHashMap_create(((XMapBase*)other)->m_keyTypeSize, XContainerTypeSize(other), other->m_hash, ((XMapBase*)other)->m_KeyEquality, ((XMapBase*)other)->m_KeyLess);
	if (hash == NULL)
		return NULL;
	XHashMap_copy_base(hash, other);
	return hash;
}
XHashMap* XHashMap_create_move(XHashMap* other)
{
	if (other == NULL)
		return NULL;
	XHashMap* hash = XHashMap_create(((XMapBase*)other)->m_keyTypeSize, XContainerTypeSize(other), other->m_hash, ((XMapBase*)other)->m_KeyEquality, ((XMapBase*)other)->m_KeyLess);
	if (hash == NULL)
		return NULL;
	XHashMap_move_base(hash, other);
	return hash;
}
void XHashMap_init(XHashMap*this_map, const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XEquality KeyEquality, XLess KeyLess)
{
	if (this_map == NULL)
		return;
	XMapBase_init(this_map, keyTypeSize, valTypeSize, KeyEquality, KeyLess);
	XClassGetVtable(this_map) = XHashMap_class_init();
	this_map->m_hash = hash;
	XContainerCapacity(this_map)= DEFAULT_CAPACITY;
	size_t size = sizeof(void*) * XContainerCapacity(this_map);
	XContainerDataPtr(this_map) = XMemory_malloc(size);
	if (XContainerDataPtr(this_map) == NULL)
		XMemory_free(this_map);
	if(XContainerDataPtr(this_map))
		memset(XContainerDataPtr(this_map),0,size);
}

XVariantHashMap* XHashMap_create_XVariantHashMap()
{
	XHashMap* hash = XHashMap_Create(XString, XVariant, XEquality_XString,XLess_XString);
	if (hash == NULL)
		return NULL;
	/*XContainerSetDataCopyMethod(hash, XMapBase_XVariantMapCopyMethod);
	XContainerSetDataMoveMethod(hash, XMapBase_XVariantMapMoveMethod);
	XContainerSetDataDeinitMethod(hash, XMapBase_XVariantMapDeinitMethod);*/
	XMapBaseSetKeyCopyMethod(hash, XString_copy_base);
	XMapBaseSetKeyMoveMethod(hash, XString_move_base);
	XMapBaseSetKeyDeinitMethod(hash, XString_deinit_base);

	XContainerSetDataCopyMethod(hash, XVariant_copy);
	XContainerSetDataMoveMethod(hash, XVariant_move);
	XContainerSetDataDeinitMethod(hash, XVariant_deinit);
	return hash;
}

#endif
