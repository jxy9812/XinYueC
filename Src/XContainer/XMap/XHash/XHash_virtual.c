#include"XHash.h"
#if XHash_ON
#include"XAlgorithm.h"
#include"XVector.h"
#include<string.h>
//Map插入数据
static bool VXMap_insert(XHash*this_map, const void* pvKey, const void* pvValue);
static void VXMap_erase(XHash*this_map, const XPair* pPair);
//map删除数据
static bool VXMap_remove(XHash*this_map, const void* pvKey);
//根据键值返回数据地址
static void* VXMap_value(XHash*this_map, const void* pvKey);
//查找数据，返回找到的XPair地址，没有返回NULL
static XPair* VXMap_find(XHash*this_map, const void* pvKey);
//返回key数组
static XVector* VXMapBase_keys(const XMapBase* this_map);
//清空Map，释放内存
static void VXMap_clear(XHash*this_map);
//释放内存
static void VXMap_delete(XHash*this_map);
static void VXMap_swap(XHash*this_mapOne, XHash*this_mapTwo);
// 私有函数：扩容哈希表
static bool XHash_resize(XHash*map, size_t new_capacity);
#define XHashNode_GetSize(map)  (sizeof(size_t) * 2 + ((XMapBase*)map)->m_keyTypeSize + XContainerTypeSize(map)+sizeof(XHashNode*))
//#define XHashNode_Next(map,node) *((XHashNode**)(((char*)node)+(sizeof(size_t) * 2 + ((XMapBase*)map)->m_keyTypeSize + XContainerTypeSize(map))))
XVtable* XHash_class_init()
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
		VXMap_insert,VXMap_erase,VXMap_remove,VXMap_value,VXMap_find,
		VXMapBase_keys
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXMap_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXMap_delete);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Swap, VXMap_swap);
#if SHOWCONTAINERSIZE
	printf("XHash size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}

// 私有函数：扩容哈希表
static bool XHash_resize(XHash*map, size_t new_capacity)
{
	//printf("进入扩容\n");
	size_t new_size = new_capacity * sizeof(XHashNode*);
	XHashNode** newData = XMemory_malloc(new_size);
	memset(newData, 0, new_size);
	if (newData == NULL)
		return false;

    for (size_t i = 0; i < XContainerCapacity(map); i++) 
	{
		XHashNode* current = ((XHashNode**)XContainerDataPtr(map))[i];
        while (current) 
		{
			XHashNode* next =  current->next;
			size_t index = map->m_hash(XPair_first(current->pair), ((XMapBase*)map)->m_keyTypeSize) % new_capacity;
			XHashNode* new_current = newData[index];
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

bool VXMap_insert(XHash*this_map, const void* pvKey, const void* pvValue)
{
	
    if ((double)XContainerSize(this_map) / XContainerCapacity(this_map) >= DEFAULT_LOAD_FACTOR)
	{
		//printf("XHash 扩容\n");
        size_t new_capacity = XContainerCapacity(this_map) * 2;
        if (!XHash_resize(this_map, new_capacity)) 
		{
			printf("XHash 扩容失败\n");
            return false ;
        }
    }

    size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashNode* current = ((XHashNode**)XContainerDataPtr(this_map))[index];

    while (current) 
	{
        if (this_map->m_parent.m_KeyEquality(XPair_first(current->pair), pvKey)) 
		{//有原先的键值
			if (XContainerDataDeleteMethod(this_map) != NULL)
				XContainerDataDeleteMethod(this_map)(current->pair);
			XPair_insertSecond(current->pair, pvValue);
			//拷贝键值
			memcpy(((uint8_t*)(&(current->pair->m_first))), pvKey, current->pair->m_firstTypeSize);
            return true;
        }
        current = current->next;
    }
	XHashNode* new_node = (XHashNode*)XMemory_malloc(sizeof(XHashNode));
	if (new_node == NULL)
		return false;

	XPair* pair = XPair_create(((XMapBase*)this_map)->m_keyTypeSize, XContainerTypeSize(this_map));
	XPair_insertFirst(pair,pvKey);
	XPair_insertSecond(pair, pvValue);
	new_node->pair = pair;

	new_node->next= ((XHashNode**)XContainerDataPtr(this_map))[index];
	((XHashNode**)XContainerDataPtr(this_map))[index] = new_node;
	++XContainerSize(this_map);
	return true;
}

void VXMap_erase(XHash*this_map, const XPair* pPair)
{
	if (XMapBase_isEmpty_base(this_map))
		return;
	size_t index = this_map->m_hash(XPair_first(pPair), ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashNode* current = ((XHashNode**)XContainerDataPtr(this_map))[index];
	XHashNode* prev = NULL;

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
				((XHashNode**)XContainerDataPtr(this_map))[index] = current->next;
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

bool VXMap_remove(XHash*this_map, const void* pvKey)
{
	if (XMapBase_isEmpty_base(this_map))
		return false;
	size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashNode* current = ((XHashNode**)XContainerDataPtr(this_map))[index];
	XHashNode* prev = NULL;

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
				((XHashNode**)XContainerDataPtr(this_map))[index] = current->next;
			}
			if (XContainerDataDeleteMethod(this_map) != NULL)
				XContainerDataDeleteMethod(this_map)(current->pair);
			XPair_delete(current->pair);
			XMemory_free(current);
			--XContainerSize(this_map);
			
			return true;//释放成功
		}

		prev = current;
		current = current->next;
	}
	return false;
}

void* VXMap_value(XHash*this_map, const void* pvKey)
{
	if (XMapBase_isEmpty_base(this_map))
		return NULL;
	size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashNode* current = ((XHashNode**)XContainerDataPtr(this_map))[index];

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

XPair* VXMap_find(XHash*this_map, const void* pvKey)
{
	if (XMapBase_isEmpty_base(this_map))
		return NULL;
	size_t index = this_map->m_hash(pvKey, ((XMapBase*)this_map)->m_keyTypeSize) % XContainerCapacity(this_map);
	XHashNode* current = ((XHashNode**)XContainerDataPtr(this_map))[index];

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

XVector* VXMapBase_keys(const XMapBase* this_map)
{
	
	XVector* v=XVector_create(this_map->m_keyTypeSize);
	for_each_iterator(this_map, XHash, it)
	{
		XVector_push_back_base(v,XPair_first(XHash_iterator_data(&it)));
	}
	return v;
}

void VXMap_clear(XHash*this_map)
{
	XHashNode* deleteNode = NULL;
	for (size_t i = 0; i < XContainerCapacity(this_map); i++)
	{
		XHashNode* current = ((XHashNode**)XContainerDataPtr(this_map))[i];

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
	memset(XContainerDataPtr(this_map),0, sizeof(XHashNode*) * XContainerCapacity(this_map));
	XContainerSize(this_map)=0;
}

void VXMap_delete(XHash*this_map)
{

	XHash_clear_base(this_map);
	void* data = XContainerDataPtr(this_map);
	if (data)
		XMemory_free(data);
	XMemory_free(this_map);
}

void VXMap_swap(XHash*this_mapOne, XHash*this_mapTwo)
{
	//调用父类交换一部分
	XVtableGetFunc(XContainerObject_class_init(), EXContainerObject_Swap, void(*)(XHash*, XHash*))(this_mapOne, this_mapTwo);
	XSwap(&(this_mapOne->m_parent.m_KeyEquality), &(this_mapTwo->m_parent.m_KeyEquality), sizeof(XEquality));
	XSwap(&(this_mapOne->m_parent.m_keyTypeSize), &(this_mapTwo->m_parent.m_keyTypeSize), sizeof(size_t));
	XSwap(&(this_mapOne->m_hash), &(this_mapTwo->m_hash), sizeof(XHashFunc));
}
#endif