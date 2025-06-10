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
static bool XHashMap_resize(XHashMap* map, size_t new_capacity)
{
	
	size_t new_size = new_capacity * sizeof(XHashMapNode*);
	XHashMapNode** newData = XMemory_malloc(new_size);
	memset(newData, 0, new_size);
	if (newData == NULL)
		return false;

    for (size_t i = 0; i < XContainerCapacity(map); i++) 
	{
		XHashMapNode* current = ((XHashMapNode**)XContainerDataPtr(map))[i];
        while (current) 
		{
			XHashMapNode* next = current->next;
            size_t index = map->m_hash(XPair_first(current)) % new_capacity;

            current->next = newData[index];
			newData[index] = current;

            current = next;
        }
    }

    XMemory_free(XContainerDataPtr(map));
	XContainerDataPtr(map) = newData;
	XContainerCapacity(map) = new_capacity;
	return true;
}
