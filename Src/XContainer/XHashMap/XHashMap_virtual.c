#include"XHashMap.h"
#include<string.h>
// 私有函数：扩容哈希表
static bool XHashMap_resize(XHashMap* map, size_t new_capacity);
XVtable* XHashMap_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XHASHMAP_VTABLE_SIZE)
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());
	//void* table[] = {
	//	VXMap_insert,VXMap_erase,VXMap_remove,VXMap_value,VXMap_find
	//};
	////追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	//XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXMap_clear);
	//XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXMap_free);
	//XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Swap, VXMap_swap);
#if SHOWCONTAINERSIZE
	printf("XHashMap size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}

// 私有函数：扩容哈希表
bool XHashMap_resize(XHashMap* map, size_t new_capacity)
{
	void* data = NULL;
	size_t new_size = new_capacity * sizeof(XHashMapNode*);
	if (XMemory_realloc_isNULL())
	{//先申请拷贝后在释放
		data = XMemory_malloc(new_size);
		if (data)
		{
			memcpy(data, XContainerDataPtr(map), XContainerCapacity(map)* sizeof(XHashMapNode*));
			XMemory_free(XContainerDataPtr(map));
		}
	}
	else
	{
		data = XMemory_realloc(XContainerDataPtr(map), new_size);
	}
	if (data == NULL)
		return;
	XContainerDataPtr(map)=data;
    /*for (size_t i = 0; i < XContainerCapacity(map); i++) 
	{
		XHashMapNode* current = ((XHashMapNode**)XContainerDataPtr(map))[i];
        while (current) 
		{
			XHashMapNode* next = current->next;
            size_t index = map->m_hash(current->key) % new_capacity;

            current->next = new_buckets[index];
            new_buckets[index] = current;

            current = next;
        }
    }

    free(map->buckets);
    map->buckets = new_buckets;
    map->capacity = new_capacity;*/
    return true;
}
