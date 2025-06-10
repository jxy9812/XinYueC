#include"XHashMap.h"
#if XHashMap_ON
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
//#define XHashMapNode_Next(map,node) *((XHashMapNode**)(((char*)node)+(sizeof(size_t) * 2 + ((XMapBase*)map)->m_keyTypeSize + XContainerTypeSize(map))))
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
			XHashMapNode* next =  current->next;
			size_t index = map->m_hash(XPair_first(current->pair), ((XMapBase*)map)->m_keyTypeSize) % new_capacity;
			XHashMapNode* new_current = newData[index];
			if (new_current != NULL)
			{
				while (new_current->next)
				{
					new_current = new_current->next;
				}
				new_current->next = current;
				
			}
			else
			{
				newData[index] = current;
			}
			current->next = NULL;
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
		//printf("XHashMap 扩容\n");
        size_t new_capacity = XContainerCapacity(this_map) * 2;
        if (!XHashMap_resize(this_map, new_capacity)) 
		{
			printf("XHashMap 扩容失败\n");
            return ;
        }
    }

    size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashMapNode* current = ((XHashMapNode**)XContainerDataPtr(this_map))[index];

    while (current) 
	{
        if (this_map->m_parent.m_KeyEquality(XPair_first(current->pair), pvKey)) 
		{
			XPair_insertSecond(current->pair, pvValue);
			//memcpy(XPair_second(current), pvValue, XContainerTypeSize(this_map));
            return ;
        }
        current = current->next;
    }
	XHashMapNode* new_node = (XHashMapNode*)XMemory_malloc(sizeof(XHashMapNode));
	if (new_node == NULL)
		return;

	XPair* pair = XPair_create(((XMapBase*)this_map)->m_keyTypeSize, XContainerTypeSize(this_map));
	XPair_insertFirst(pair,pvKey);
	XPair_insertSecond(pair, pvValue);
	new_node->pair = pair;

	new_node->next= ((XHashMapNode**)XContainerDataPtr(this_map))[index];
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
				prev->next = current->next;
			}
			else
			{
				((XHashMapNode**)XContainerDataPtr(this_map))[index] = current->next;
			}
			if (XContainerDataDeleteMethod(this_map) != NULL)
				XContainerDataDeleteMethod(this_map)(current->pair);
			XPair_delete(current->pair);
			XMemory_free(current);
			--XContainerSize(this_map);

			return;//释放成功
		}

		prev = current;
		current = current->next;
	}
}

void VXMap_remove(XHashMap* this_map, const void* pvKey)
{
	//printf("删除\n");
	size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashMapNode* current = ((XHashMapNode**)XContainerDataPtr(this_map))[index];
	XHashMapNode* prev = NULL;

	while (current) 
	{
		if (this_map->m_parent.m_KeyEquality(XPair_first(current->pair), pvKey)) 
		{
			if (prev) 
			{
				prev->next = current->next;
			}
			else 
			{
				((XHashMapNode**)XContainerDataPtr(this_map))[index] = current->next;
			}
			if (XContainerDataDeleteMethod(this_map) != NULL)
				XContainerDataDeleteMethod(this_map)(current->pair);
			XPair_delete(current->pair);
			XMemory_free(current);
			--XContainerSize(this_map);
			
			return ;//释放成功
		}

		prev = current;
		current = current->next;
	}

}

void* VXMap_value(XHashMap* this_map, const void* pvKey)
{
	size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashMapNode* current = ((XHashMapNode**)XContainerDataPtr(this_map))[index];

	while (current) 
	{
		if (((XMapBase*)this_map)->m_KeyEquality(XPair_first(current->pair), pvKey))
		{
			return XPair_second(current->pair);
		}
		current = current->next;
	}
	return NULL;
}

XPair* VXMap_find(XHashMap* this_map, const void* pvKey)
{
	size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashMapNode* current = ((XHashMapNode**)XContainerDataPtr(this_map))[index];

	while (current)
	{
		if (this_map->m_parent.m_KeyEquality(XPair_first(current->pair), pvKey))
		{
			return current->pair;
		}
		current = current->next;
	}
	return NULL;
}

void VXMap_clear(XHashMap* this_map)
{
	XHashMapNode* deleteNode = NULL;
	for (size_t i = 0; i < XContainerCapacity(this_map); i++)
	{
		XHashMapNode* current = ((XHashMapNode**)XContainerDataPtr(this_map))[i];

		while (current)
		{
			deleteNode = current;
			current = current->next;
			if (XContainerDataDeleteMethod(this_map) != NULL)
				XContainerDataDeleteMethod(this_map)(deleteNode->pair);
			XPair_delete(deleteNode->pair);
			XMemory_free(deleteNode);
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
	XSwap(&(this_mapOne->m_parent.m_KeyEquality), &(this_mapTwo->m_parent.m_KeyEquality), sizeof(XEquality));
	XSwap(&(this_mapOne->m_parent.m_keyTypeSize), &(this_mapTwo->m_parent.m_keyTypeSize), sizeof(size_t));
	XSwap(&(this_mapOne->m_hash), &(this_mapTwo->m_hash), sizeof(XHash));
}
#endif