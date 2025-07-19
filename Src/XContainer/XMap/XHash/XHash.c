#include"XHash.h"
#if XHash_ON
#include"XMemory.h"
#include<string.h>
XHash*XHash_create(const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XEquality KeyEquality, XLess KeyLess)
{
	XHash*map = XMemory_malloc(sizeof(XHash));
	XHash_init(map,keyTypeSize,valTypeSize,hash,KeyEquality, KeyLess);
	return map;
}
void XHash_init(XHash*this_map, const size_t keyTypeSize, const size_t valTypeSize, XHashFunc hash, XEquality KeyEquality, XLess KeyLess)
{
	if (this_map == NULL)
		return;
	XMapBase_init(this_map, keyTypeSize, valTypeSize, KeyEquality, KeyLess);
	XClassGetVtable(this_map) = XHash_class_init();
	this_map->m_hash = hash;
	XContainerCapacity(this_map)= DEFAULT_CAPACITY;
	size_t size = sizeof(void*) * XContainerCapacity(this_map);
	XContainerDataPtr(this_map) = XMemory_malloc(size);
	if (XContainerDataPtr(this_map) == NULL)
		XMemory_free(this_map);
	if(XContainerDataPtr(this_map))
		memset(XContainerDataPtr(this_map),0,size);
}

XHash* XHash_create_XStringVariant()
{
	XHash* hash = XHash_Create(XString*, XVariant*,XHash_murmur3_32, XEquality_XString,XLess_XString);
	if (hash == NULL)
		return NULL;
	XContainerSetDataDeleteMethod(hash, XMapBase_KeyXStringValueXVariantDeleteMethod);
	return hash;
}

#endif
