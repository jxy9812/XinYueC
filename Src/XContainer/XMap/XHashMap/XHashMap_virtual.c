#include"XHashMap.h"
#include"XAlgorithm.h"
#include<string.h>
//Map插入数据
static void VXMap_insert(XHashMap* this_map, const void* pvKey, const void* pvValue);
static void VXMap_erase(XHashMap* this_map, const XPair* pPair);
//map删除数据
static void VXMap_remove(XHashMap* this_map, const void* pvKey);
//根据键值返回数据地址
static void* VXMap_value(XHashMap* this_map, const void* pvKey);
//查找数据，返回找到的XPair地址，没有返回NULL
static XPair* VXMap_find(XHashMap* this_map, const void* pvKey);
//清空Map，释放内存
static void VXMap_clear(XHashMap* this_map);
//释放内存
static void VXMap_delete(XHashMap* this_map);
static void VXMap_swap(XHashMap* this_mapOne, XHashMap* this_mapTwo);
// 私有函数：扩容哈希表
static bool XHashMap_resize(XHashMap* map, size_t new_capacity);
#define XHashMapNode_GetSize(map)  (sizeof(size_t) * 2 + ((XMapBase*)map)->m_keyTypeSize + XContainerTypeSize(map)+sizeof(XHashMapNode*))
#define XHashMapNode_Next(map,node) *((XHashMapNode**)(((char*)node)+(sizeof(size_t) * 2 + ((XMapBase*)map)->m_keyTypeSize + XContainerTypeSize(map))))
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
	void* table[] = {
		VXMap_insert,VXMap_erase,VXMap_remove,VXMap_value,VXMap_find
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXMap_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXMap_delete);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Swap, VXMap_swap);
#if SHOWCONTAINERSIZE
	printf("XHashMap size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}

// 私有函数：扩容哈希表
static bool XHashMap_resize(XHashMap* map, size_t new_capacity)
{
	//printf("进入扩容\n");
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
			XHashMapNode* next = XHashMapNode_Next(map, current) /* current->next*/;
			size_t index = map->m_hash(XPair_first(current), ((XMapBase*)map)->m_keyTypeSize) % new_capacity;

			XHashMapNode_Next(map, current) = newData[index];
			newData[index] = current;

            current = next;
        }
    }

    XMemory_free(XContainerDataPtr(map));
	XContainerDataPtr(map) = newData;
	XContainerCapacity(map) = new_capacity;
	return true;
}

void VXMap_insert(XHashMap* this_map, const void* pvKey, const void* pvValue)
{
	
    if ((double)XContainerSize(this_map) / XContainerCapacity(this_map) >= DEFAULT_LOAD_FACTOR)
	{
        size_t new_capacity = XContainerCapacity(this_map) * 2;
        if (!XHashMap_resize(this_map, new_capacity)) 
		{
			printf("XHashMap 扩容失败");
            return ;
        }
    }

    size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashMapNode* current = ((XHashMapNode**)XContainerDataPtr(this_map))[index];

    while (current) 
	{
        if (this_map->m_parent.m_KeyEquality(XPair_first(current), pvKey)) 
		{
			XPair_insertSecond(current, pvValue);
			//memcpy(XPair_second(current), pvValue, XContainerTypeSize(this_map));
            return ;
        }
        current = XHashMapNode_Next(this_map, current);
    }
	size_t nodeSize = XHashMapNode_GetSize(this_map);
	XHashMapNode* new_node = (XHashMapNode*)XMemory_malloc(nodeSize);
	{//pair派生后的初始化
		memset(new_node, 0, nodeSize);
		if (!new_node)
			return false;
		XPair* this_pair = new_node;
		this_pair->m_firstTypeSize = ((XMapBase*)this_map)->m_keyTypeSize;
		this_pair->m_secondTypeSize = XContainerTypeSize(this_map);
	}
	XPair_insertFirst(new_node,pvKey);
	XPair_insertSecond(new_node, pvValue);
	
	XHashMapNode_Next(this_map, new_node)= ((XHashMapNode**)XContainerDataPtr(this_map))[index];
	((XHashMapNode**)XContainerDataPtr(this_map))[index] = new_node;
	++XContainerSize(this_map);
}

void VXMap_erase(XHashMap* this_map, const XPair* pPair)
{
	size_t index = this_map->m_hash(XPair_first(pPair), ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashMapNode* current = ((XHashMapNode**)XContainerDataPtr(this_map))[index];
	XHashMapNode* prev = NULL;

	while (current)
	{
		if (current== pPair)
		{
			if (prev)
			{
				prev->next = XHashMapNode_Next(this_map, current);
			}
			else
			{
				((XHashMapNode**)XContainerDataPtr(this_map))[index] = XHashMapNode_Next(this_map, current);
			}
			if (XContainerDataDeleteMethod(this_map) != NULL)
				XContainerDataDeleteMethod(this_map)(current);
			XPair_delete(current);
			--XContainerSize(this_map);

			return;//释放成功
		}

		prev = current;
		current = XHashMapNode_Next(this_map, current);
	}
}

void VXMap_remove(XHashMap* this_map, const void* pvKey)
{
	size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashMapNode* current = ((XHashMapNode**)XContainerDataPtr(this_map))[index];
	XHashMapNode* prev = NULL;

	while (current) 
	{
		if (this_map->m_parent.m_KeyEquality(XPair_first(current), pvKey)) 
		{
			if (prev) 
			{
				prev->next = XHashMapNode_Next(this_map, current);
			}
			else 
			{
				((XHashMapNode**)XContainerDataPtr(this_map))[index] = XHashMapNode_Next(this_map, current);
			}
			if (XContainerDataDeleteMethod(this_map) != NULL)
				XContainerDataDeleteMethod(this_map)(current);
			XPair_delete(current);
			--XContainerSize(this_map);
			
			return ;//释放成功
		}

		prev = current;
		current = XHashMapNode_Next(this_map, current);
	}

}

void* VXMap_value(XHashMap* this_map, const void* pvKey)
{
	size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashMapNode* current = ((XHashMapNode**)XContainerDataPtr(this_map))[index];

	while (current) 
	{
		if (((XMapBase*)this_map)->m_KeyEquality(XPair_first(current), pvKey))
		{
			return XPair_second(current);
		}
		current = XHashMapNode_Next(this_map, current);
		//current = current->next;
	}
	return NULL;
}

XPair* VXMap_find(XHashMap* this_map, const void* pvKey)
{
	size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashMapNode* current = ((XHashMapNode**)XContainerDataPtr(this_map))[index];

	while (current)
	{
		if (this_map->m_parent.m_KeyEquality(XPair_first(current), pvKey))
		{
			return current;
		}
		current = XHashMapNode_Next(this_map, current);
	}
	return NULL;
}

void VXMap_clear(XHashMap* this_map)
{
	void* deleteNode = NULL;
	for (size_t i = 0; i < XContainerCapacity(this_map); i++)
	{
		XHashMapNode* current = ((XHashMapNode**)XContainerDataPtr(this_map))[i];

		while (current)
		{
			deleteNode = current;
			current = XHashMapNode_Next(this_map, current);
			if (XContainerDataDeleteMethod(this_map) != NULL)
				XContainerDataDeleteMethod(this_map)(deleteNode);
			XPair_delete(deleteNode);
		}
	}
	memset(XContainerDataPtr(this_map),0, sizeof(XHashMapNode*) * XContainerCapacity(this_map));
	XContainerSize(this_map)=0;
}

void VXMap_delete(XHashMap* this_map)
{
	XHashMap_clear_base(this_map);
	void* data = XContainerDataPtr(this_map);
	if (data)
		XMemory_free(data);
	XMemory_free(this_map);
}

void VXMap_swap(XHashMap* this_mapOne, XHashMap* this_mapTwo)
{
	//调用父类交换一部分
	XVtableGetFunc(XContainerObject_class_init(), EXContainerObject_Swap, void(*)(XHashMap*, XHashMap*))(this_mapOne, this_mapTwo);
	XSWAP(&(this_mapOne->m_parent.m_KeyEquality), &(this_mapTwo->m_parent.m_KeyEquality),XEquality);
	XSWAP(&(this_mapOne->m_parent.m_keyTypeSize), &(this_mapTwo->m_parent.m_keyTypeSize), size_t);
	XSWAP(&(this_mapOne->m_hash), &(this_mapTwo->m_hash), XHash);
}
