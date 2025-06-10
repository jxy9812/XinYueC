#include"XHashMap.h"
#include"XMemory.h"
#include<string.h>
XHashMap* XHashMap_create(const size_t keyTypeSize, const size_t valTypeSize, XHash hash, XEquality KeyEquality)
{
	XHashMap* map = XMemory_malloc(sizeof(XHashMap));
	XHashMap_init(map,keyTypeSize,valTypeSize,hash,KeyEquality);
	return map;
}
void XHashMap_init(XHashMap* this_map, const size_t keyTypeSize, const size_t valTypeSize, XHash hash, XEquality KeyEquality)
{
	if (this_map == NULL)
		return;
	XMapBase_init(this_map, keyTypeSize, valTypeSize, KeyEquality);
	XClassGetVtable(this_map) = XHashMap_class_init();
	this_map->m_hash = hash;
	XContainerCapacity(this_map)= DEFAULT_CAPACITY;
	size_t size = sizeof(XHashMapNode*) * XContainerCapacity(this_map);
	XContainerDataPtr(this_map) = XMemory_malloc(size);
	if (XContainerDataPtr(this_map) == NULL)
		XMemory_free(this_map);
	memset(XContainerDataPtr(this_map),0,size);
}
