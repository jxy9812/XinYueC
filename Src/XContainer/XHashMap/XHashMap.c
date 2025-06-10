#include"XHashMap.h"
#include"XMemory.h"
// 默认初始容量
#define DEFAULT_CAPACITY 16
// 默认负载因子阈值
#define DEFAULT_LOAD_FACTOR 0.75f
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
	XContainerObject_init(this_map,valTypeSize);
	XClassGetVtable(this_map) = XHashMap_class_init();
	this_map->m_hash = hash;
	this_map->m_KeyEquality = KeyEquality;
	this_map->m_keyTypeSize = keyTypeSize;
	XContainerCapacity(this_map)= DEFAULT_CAPACITY;
	XContainerDataPtr(this_map) = XMemory_malloc(sizeof(XHashMapNode*)* XContainerCapacity(this_map));
	if (XContainerDataPtr(this_map) == NULL)
		XMemory_free(this_map);

}

void XHashMap_insert_base(XHashMap* this_map, const void* key, const void* pvValue)
{
	if (ISNULL(this_map, "") || ISNULL(XClassGetVtable(this_map), ""))
		return;
	XClassGetVirtualFunc(this_map, EXHashMap_Insert,void(*)(XHashMap*,const void* ,const void*))(this_map, key, pvValue);
}
