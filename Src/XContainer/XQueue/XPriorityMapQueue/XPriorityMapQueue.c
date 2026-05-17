#include "XPriorityMapQueue.h"
#include "XPriorityQueue.h"
#include "XLockFreeQueue.h"
#include "XMap.h"
#include <string.h>
#define GetMap(queue)				(*(XMapBase**)XContainerSharedDataPtr(queue)) //获取map（解引用柔性数组中的指针）
#define GetPrioritySize(queue)		GetMap(queue)->m_keyTypeSize

// COW分离与数据删除
static bool VXPriorityMapQueueDetachIfNeeded(XPriorityMapQueue* this_queue);
static void VXPriorityMapQueueDataDelete(void* data, XPriorityMapQueue* this_queue);
// 确保 XSharedData 存在
static bool ensureSharedData(XPriorityMapQueue* this_queue);

//插入到队列的队尾
static bool VXPriorityQueue_push(XPriorityMapQueue* this_queue, void* pvPriority, void* pvValue, XCDataCreatMethod priorityCreatMethod, XCDataCreatMethod dataCreatMethod);
//出队
static void VXPriorityQueue_pop(XPriorityMapQueue* this_queue);
// 返回优先队列堆顶元素
static void* VXPriorityQueue_top(XPriorityMapQueue* this_queue);
static bool VXPriorityQueue_receive(XPriorityMapQueue* this_queue, void* pvBuffer);
static bool VXPriorityQueue_isFull(const XPriorityMapQueue* this_queue);
//static bool VXPriorityQueue_isEmpty(const XPriorityMapQueue* this_queue);
static void VXClass_deinit(XPriorityMapQueue* this_queue);
static void VXClass_copy(XPriorityMapQueue* object, const XPriorityMapQueue* src);
static void VXClass_move(XPriorityMapQueue* object, XPriorityMapQueue* src);
XVtable* XPriorityMapQueue_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XPRIORITYMAPQUEUE_VTABLE_SIZE)
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_XCLASS(XContainer);
	void* table[] = { VXPriorityQueue_push,VXPriorityQueue_pop,VXPriorityQueue_top,VXPriorityQueue_receive,VXPriorityQueue_isFull };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXClass_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
	//XVTABLE_OVERLOAD_DEFAULT(EXContainer_IsEmpty, VXPriorityQueue_isEmpty);
#if SHOWCONTAINERSIZE
	printf("XPriorityMapQueue size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}
void XPriorityMapQueue_init(XPriorityMapQueue* this_queue, size_t prioritySize, XCompare priorityCom, XSortOrder priorityOrder, size_t typeSize)
{
	if (ISNULL(this_queue, "") || ISNULL(typeSize, ""))
		return;
	XContainer_init(this_queue, typeSize);
	XClassGetVtable(this_queue) = XPriorityMapQueue_class_init();

	// 创建 XSharedData 存储 XMap 指针
	XContainerSharedData(this_queue) = XSharedData_create(NULL, sizeof(XMapBase*));
	if (XContainerSharedData(this_queue))
	{
		// 创建 XMap 并存储到 XSharedData 中
		XMapBase* map = XMap_create(prioritySize, sizeof(XCircularQueue), priorityCom);
		*(XMapBase**)XContainerSharedDataPtr(this_queue) = map;
	}

	XContainerSetDataMoveMethod(GetMap(this_queue), XCircularQueue_move_base);
	XContainerSetDataCopyMethod(GetMap(this_queue), XCircularQueue_copy_base);
	XContainerSetDataDeinitMethod(GetMap(this_queue), XCircularQueue_deinit_base);

	this_queue->m_priorityCopyMethod = NULL;
	this_queue->m_priorityMoveMethod = NULL;
	this_queue->m_priorityDeinitMethod = NULL;
	this_queue->low_freq_queue = XPriorityQueue_create(prioritySize+typeSize, priorityCom, priorityOrder);
	this_queue->mapPriority = NULL;
}

// 确保 XSharedData 存在
static bool ensureSharedData(XPriorityMapQueue* this_queue)
{
    if (!XContainerSharedData(this_queue))
    {
        XContainerSharedData(this_queue) = XSharedData_create(NULL, sizeof(XMapBase*));
        if (!XContainerSharedData(this_queue))
            return false;
        *(XMapBase**)XContainerSharedDataPtr(this_queue) = NULL;
    }
    return true;
}

// COW分离：如果数据被共享，创建独立副本
static bool VXPriorityMapQueueDetachIfNeeded(XPriorityMapQueue* this_queue)
{
    if (!XContainerSharedData(this_queue) || !XSharedData_isShared(XContainerSharedData(this_queue)))
        return true; // 不共享，无需分离

    // 获取原 XMap
    XMapBase* oldMap = GetMap(this_queue);
    if (!oldMap)
        return true;

    // 创建新的 XSharedData
    XSharedData* newShared = XSharedData_create(NULL, sizeof(XMapBase*));
    if (!newShared) return false;

    // 深拷贝 XMap
    XMapBase* newMap = XMap_create(XContainerTypeSize(oldMap), XContainerCapacity(oldMap), XContainerCompare(oldMap));
    if (!newMap)
    {
        XSharedData_release(newShared);
        return false;
    }

    // 拷贝 XMap 中的元素
    // 注意：这里需要深拷贝每个 XCircularQueue
    // 由于 XMap 的 COW 机制，这里简化处理
    // 实际上应该调用 XMap 的深拷贝
    XMap_copy_base(newMap, oldMap);

    *(XMapBase**)newShared->data = newMap;

    // 减少旧引用，设置新引用
    XSharedData_release(XContainerSharedData(this_queue));
    XContainerSharedData(this_queue) = newShared;

    // 更新回调函数
    XContainerSetDataMoveMethod(newMap, XCircularQueue_move_base);
    XContainerSetDataCopyMethod(newMap, XCircularQueue_copy_base);
    XContainerSetDataDeinitMethod(newMap, XCircularQueue_deinit_base);

    return true;
}

// 删除数据（XSharedData释放回调）
static void VXPriorityMapQueueDataDelete(void* data, XPriorityMapQueue* this_queue)
{
    if (this_queue == NULL) return;
    XMapBase* map = data ? *(XMapBase**)data : NULL;
    if (map)
    {
        // 清空并删除 XMap
        XMap_clear_base(map);
        XMap_delete_base(map);
    }
    if (this_queue->low_freq_queue)
    {
        XPriorityQueue_clear_base(this_queue->low_freq_queue);
        XPriorityQueue_delete_base(this_queue->low_freq_queue);
        this_queue->low_freq_queue = NULL;
    }
    if (this_queue->mapPriority)
    {
        XFree_System(this_queue->mapPriority);
        this_queue->mapPriority = NULL;
    }
    XContainerSharedData(this_queue) = NULL;
    XContainerSize(this_queue) = 0;
    XContainerCapacity(this_queue) = 0;
}

XPriorityMapQueue* XPriorityMapQueue_create(size_t prioritySize, XCompare priorityCom, XSortOrder priorityOrder, size_t typeSize)
{
	if (ISNULL(prioritySize, "")|| ISNULL(priorityCom, "") || ISNULL(typeSize, ""))
		return NULL;
	XPriorityMapQueue* this_queue = XMalloc_System(sizeof(XPriorityMapQueue));
	XPriorityMapQueue_init(this_queue, prioritySize, priorityCom, priorityOrder, typeSize);
	Set_Class_MemoryFree(this_queue, XFree_System);
	return this_queue;
}
//进行一次映射
static void mapPriority(XPriorityMapQueue* this_queue)
{
	if (this_queue->mapPriority)
		XFree_System(this_queue->mapPriority);
	if (XMap_isEmpty_base(GetMap(this_queue)))
	{
		this_queue->mapPriority = NULL;
		return;
	}
	this_queue->mapPriority = XMalloc_System(sizeof(XPair*)*XMapBase_size_base(GetMap(this_queue)));
	if (this_queue->mapPriority == NULL)
		return;
	size_t index = 0;
	if (this_queue->low_freq_queue->m_order == XSORT_ASC)
	{
		for_each_iterator(GetMap(this_queue), XMap, it)
		{
			((XPair**)this_queue->mapPriority)[index++] = XMap_iterator_data(&it);
		}
	}
	else
	{
		for_each_reverse_iterator(GetMap(this_queue), XMap, it)
		{
			((XPair**)this_queue->mapPriority)[index++] = XMap_reverse_iterator_data(&it);
		}
	}
}

bool XPriorityMapQueue_addFifoQueue(XPriorityMapQueue* this_queue, void* priority, size_t queueSize)
{
	if (this_queue == NULL || priority == NULL|| queueSize==0)
		return false;
    if (!ensureSharedData(this_queue) || !VXPriorityMapQueueDetachIfNeeded(this_queue))
        return false;
	if (XMapBase_value_base(GetMap(this_queue), priority))
		return false;//已经添加了
	XCircularQueue* queue = XLockFreeQueue_create(XContainerTypeSize(this_queue),queueSize);
	if (queue == NULL)
		return false;
	/*XContainerSetDataMoveMethod(queue, XContainerDataMoveMethod(this_queue));
	XContainerSetDataCopyMethod(queue, XContainerDataCopyMethod(this_queue));
	XContainerSetDataDeinitMethod(queue, XContainerDataDeinitMethod(this_queue));*/

	XMapBaseSetKeyCopyMethod(GetMap(this_queue),this_queue->m_priorityCopyMethod);
	//XMapBaseSetKeyMoveMethod(GetMap(this_queue), this_queue->m_priorityCopyMethod);
	bool is_ok=XMapBase_insert_valueMove_base(GetMap(this_queue), priority, queue);
	XCircularQueue_delete_base(queue);
	mapPriority(this_queue);
	return is_ok;
}

bool XPriorityMapQueue_removeFifoQueue(XPriorityMapQueue* this_queue, void* priority)
{
	if (this_queue == NULL || priority == NULL)
		return false;
    if (!ensureSharedData(this_queue) || !VXPriorityMapQueueDetachIfNeeded(this_queue))
        return false;
	XMap_iterator it;
	if (!XMapBase_find_base(GetMap(this_queue), priority,&it))
		return false;//要删除的不存在
	XMapBaseSetKeyDeinitMethod(GetMap(this_queue), this_queue->m_priorityCopyMethod);
	XCircularQueue* queue = XPair_second(XMap_iterator_data(&it));
	XContainerSetDataDeinitMethod(queue, XContainerDataDeinitMethod(this_queue));
	bool is_ok = XMapBase_remove_base(GetMap(this_queue), priority);//将自动释放fifo队列，但是保存的数据如果有申请动态内存将会有内存泄漏风险，需要提前释放掉
	mapPriority(this_queue);
	return is_ok;
}

bool XPriorityMapQueue_push_base(XPriorityMapQueue* this_queue, void* pvPriority, void* pvValue)
{
	if (ISNULL(this_queue, "") || ISNULL(pvPriority, "") || ISNULL(pvValue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return false;
	return XClassGetVirtualFunc(this_queue, EXQueueBase_Push, bool (*)(XQueueBase*, void*, void*, XCDataCreatMethod, XCDataCreatMethod))(this_queue, pvPriority, pvValue,this_queue->m_priorityCopyMethod, XContainerDataCopyMethod(this_queue));
}

bool XPriorityMapQueue_push_move_base(XPriorityMapQueue* this_queue, void* pvPriority, void* pvValue)
{
	if (ISNULL(this_queue, "") || ISNULL(pvPriority, "") || ISNULL(pvValue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return false;
	return XClassGetVirtualFunc(this_queue, EXQueueBase_Push, bool (*)(XQueueBase*, void*, void*, XCDataCreatMethod, XCDataCreatMethod))(this_queue, pvPriority, pvValue,this_queue->m_priorityMoveMethod, XContainerDataMoveMethod(this_queue));
}

bool XPriorityMapQueue_push_fifo(XPriorityMapQueue* this_queue, void* pvPriority, void* pvValue)
{
	if(!this_queue||!pvPriority||!pvValue|| !this_queue->mapPriority)
		return false;
	XQueueBase* queue = XMap_value_base(GetMap(this_queue), pvPriority);
	if (queue == NULL)
		return false;
	return XQueueBase_push_base(queue, pvValue);
}

bool XPriorityMapQueue_push_fifo_move(XPriorityMapQueue* this_queue, void* pvPriority, void* pvValue)
{
	if (!this_queue || !pvPriority || !pvValue || !this_queue->mapPriority)
		return false;
	XQueueBase* queue = XMap_value_base(GetMap(this_queue), pvPriority);
	if (queue == NULL)
		return false;
	return XQueueBase_push_move_base(queue, pvValue);
}

bool VXPriorityQueue_push(XPriorityMapQueue* this_queue, void* pvPriority, void* pvValue, XCDataCreatMethod priorityCreatMethod, XCDataCreatMethod dataCreatMethod)
{
    if (!ensureSharedData(this_queue) || !VXPriorityMapQueueDetachIfNeeded(this_queue))
        return false;
	if (!XMap_isEmpty_base(GetMap(this_queue)))
	{//存在高频队列
		XCircularQueue* queue=XMap_value_base(GetMap(this_queue), pvPriority);
		if (queue != NULL)
		{//此优先级数据有对应的高频队列插入
			++XContainerSize(this_queue);
			++XContainerCapacity(this_queue);
			return XClassGetVirtualFunc(queue, EXQueueBase_Push, bool (*)(XQueueBase*, void*, XCDataCreatMethod))(queue, pvValue, dataCreatMethod);
		}
	}
	//没有高频队列或没有对应的都插入到堆队列
	uint8_t* data = XMalloc_System(XContainerTypeSize(this_queue->low_freq_queue));
	//XMap* map = GetMap(this_queue);
	if (priorityCreatMethod)
		priorityCreatMethod(data, pvPriority);
	else
		memcpy(data, pvPriority, GetPrioritySize(this_queue));
	if (dataCreatMethod)
		dataCreatMethod(data + GetPrioritySize(this_queue), pvValue);
	else
		memcpy(data + GetPrioritySize(this_queue), pvValue, XContainerTypeSize((this_queue)));
	XPriorityQueue_push_base(this_queue->low_freq_queue, data);
	//更新修改的数据
	if (priorityCreatMethod&& dataCreatMethod)
	{
		memcpy(pvPriority, data, GetPrioritySize(this_queue));
		memcpy(pvValue, data + GetPrioritySize(this_queue), XContainerTypeSize((this_queue)));
	}
	XFree_System(data);
	++XContainerSize(this_queue);
	++XContainerCapacity(this_queue);
	return true;
}
//根据优先级获取映射队列  如果队头是映射队列返回真，不是返回假
static bool getMapQueue(XPriorityMapQueue* this_queue,XCircularQueue** getCq)
{
	if (this_queue->mapPriority)
	{
		void* priorityMap = NULL;//遍历找到最高或最低的优先级
		XCircularQueue* cq = NULL;//环形队列
		void* priority = XPriorityQueue_top_base(this_queue->low_freq_queue);//堆队列的优先级数据
		for (size_t i = 0; i < XContainerSize(GetMap(this_queue)); i++)
		{
			cq = XPair_second(((XPair**)this_queue->mapPriority)[i]);
			if (XContainer_isEmpty_base(cq))
				continue;//跳过当前
			priorityMap = XPair_first(((XPair**)this_queue->mapPriority)[i]);
			break;//找到了跳出循环
		}
		//两种队列都找到了数据
		if (priorityMap && priority)
		{
			//查看堆队列的数据
			int32_t com = XContainerCompare(this_queue->low_freq_queue)(priorityMap, priority);
			if ((com == XCompare_Less && this_queue->low_freq_queue->m_order == XSORT_ASC) || (com == XCompare_Greater && this_queue->low_freq_queue->m_order == XSORT_DESC))
			{//选择堆队列
				if (getCq)
					*getCq = cq;
				return true;
			}
			//else
			//{//选择映射的普通队列
			//	return false;
			//}


		}
		else if(priorityMap)
		{
			if (getCq)
				*getCq = cq;
			return true;
		}

	}
	if (getCq)
		*getCq = NULL;
	return false;
}
void VXPriorityQueue_pop(XPriorityMapQueue* this_queue)
{
    if (!ensureSharedData(this_queue) || !VXPriorityMapQueueDetachIfNeeded(this_queue))
        return;
	XCircularQueue* getCq=NULL;
	if (getMapQueue(this_queue, &getCq))
	{
		void* data= XQueueBase_top_base(getCq);
		if (data)
		{
			if (XContainerDataDeinitMethod(this_queue))
				XContainerDataDeinitMethod(this_queue)(data);
			XQueueBase_pop_base(getCq);
		}
	}
	else
	{
		uint8_t* data = XQueueBase_top_base(this_queue->low_freq_queue);
		if (data)
		{
			if (this_queue->m_priorityDeinitMethod)
				this_queue->m_priorityDeinitMethod(data);
			if (XContainerDataDeinitMethod(this_queue))
				XContainerDataDeinitMethod(this_queue)(data+GetPrioritySize(this_queue));
			XQueueBase_pop_base(this_queue->low_freq_queue);
		}

		//XQueueBase_pop_base(this_queue->low_freq_queue);
	}
	--XContainerSize(this_queue);
	--XContainerCapacity(this_queue);
	//if (this_queue->mapPriority)
	//{
	//	void* priorityMap = NULL;//遍历找到最高或最低的优先级
	//	XCircularQueue* cq = NULL;//环形队列
	//	void* priority = XPriorityQueue_top_base(this_queue->low_freq_queue);//堆队列的优先级数据
	//	for (size_t i = 0; i <XContainerSize(GetMap(this_queue)); i++)
	//	{
	//		cq = XPair_second(((XPair**)this_queue->mapPriority)[i]);
	//		if (XContainer_isEmpty_base(cq))
	//			continue;//跳过当前
	//		priorityMap = XPair_first(((XPair**)this_queue->mapPriority)[i]);
	//		break;//找到了跳出循环
	//	}
	//	//两种队列都找到了数据
	//	if(priorityMap&& priority)
	//	{
	//		//查看堆队列的数据
	//		int32_t com = XContainerCompare(this_queue->low_freq_queue)(priority, priorityMap);
	//		if ((com == XCompare_Less && this_queue->low_freq_queue->m_order == XSORT_ASC) || (com == XCompare_Greater && this_queue->low_freq_queue->m_order == XSORT_DESC))
	//		{//选择堆队列

	//		}
	//		else
	//		{//选择映射的普通队列

	//		}


	//	}

	//}
}

void* VXPriorityQueue_top(XPriorityMapQueue* this_queue)
{
	XCircularQueue* getCq = NULL;
	if (getMapQueue(this_queue, &getCq))
	{
		return XQueueBase_top_base(getCq);
	}
	else
	{
		uint8_t* data= XQueueBase_top_base(this_queue->low_freq_queue);
		if(data==NULL)
			return NULL;
		return data + GetPrioritySize(this_queue);
	}
}

bool VXPriorityQueue_receive(XPriorityMapQueue* this_queue, void* pvBuffer)
{
    if (!ensureSharedData(this_queue) || !VXPriorityMapQueueDetachIfNeeded(this_queue))
        return false;
	XCircularQueue* getCq = NULL;
	if (getMapQueue(this_queue, &getCq))
	{
		if(XQueueBase_receive_base(getCq, pvBuffer))
		{
			--XContainerSize(this_queue);
			--XContainerCapacity(this_queue);
			return true;
		}
		return false;
	}
	else
	{
		uint8_t* data = XQueueBase_top_base(this_queue->low_freq_queue);
		if (data == NULL)
			return false;
		if (pvBuffer)
		{
			memcpy(pvBuffer, data + GetPrioritySize(this_queue), XContainerTypeSize(this_queue));
			memset(data + GetPrioritySize(this_queue),0, XContainerTypeSize(this_queue));//防止被pop释放数据
		}
		if (this_queue->m_priorityDeinitMethod)
			this_queue->m_priorityDeinitMethod(data);
		XQueueBase_pop_base(this_queue->low_freq_queue);
		 //data + GetPrioritySize(this_queue);

		--XContainerSize(this_queue);
		--XContainerCapacity(this_queue);
		return true;
	}
}

bool VXPriorityQueue_isFull(const XPriorityMapQueue* this_queue)
{
	return false;
}

//bool VXPriorityQueue_isEmpty(const XPriorityMapQueue* this_queue)
//{
//	if (getMapQueue(this_queue, NULL))
//		return false;
//	return XQueueBase_isEmpty_base(this_queue->low_freq_queue);
//}

void VXClass_deinit(XPriorityMapQueue* this_queue)
{
    if (XContainerSharedData(this_queue))
    {
        XSharedData_release_with(XContainerSharedData(this_queue), VXPriorityMapQueueDataDelete, this_queue);
    }
    else
    {
        // 没有共享数据时，手动清理
        while (!XQueueBase_isEmpty_base(this_queue))
        {
            XQueueBase_pop_base(this_queue);
        }
        XMapBase* map = GetMap(this_queue);
        if (map)
        {
            XMap_delete_base(map);
        }
        if (this_queue->low_freq_queue)
        {
            XPriorityQueue_delete_base(this_queue->low_freq_queue);
            this_queue->low_freq_queue = NULL;
        }
                if (this_queue->mapPriority)
        {
            XFree_System(this_queue->mapPriority);
            this_queue->mapPriority = NULL;
        }
    }
    XContainerSharedData(this_queue) = NULL;
    XContainerSize(this_queue) = 0;
    XContainerCapacity(this_queue) = 0;
}

void VXClass_copy(XPriorityMapQueue* object, const XPriorityMapQueue* src)
{
    // 如果目标还未初始化，先初始化
    if (((XClass*)object)->m_vtable == NULL)
    {
        XPriorityMapQueue_init(object,
            XContainerTypeSize(src->low_freq_queue) - XContainerTypeSize(src),
            XContainerCompare(src->low_freq_queue),
            src->low_freq_queue->m_order,
            XContainerTypeSize(src));
    }
    else if (XContainerSharedData(object))
    {
        XSharedData_release_with(XContainerSharedData(object), VXPriorityMapQueueDataDelete, object);
    }

    // 复制回调函数
    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));
    object->m_priorityCopyMethod = src->m_priorityCopyMethod;
    object->m_priorityMoveMethod = src->m_priorityMoveMethod;
    object->m_priorityDeinitMethod = src->m_priorityDeinitMethod;

    // 共享源数据的 XSharedData（COW 机制）
    XContainerSharedData(object) = XContainerSharedData(src);
    if (XContainerSharedData(object))
    {
        XSharedData_addRef(XContainerSharedData(object));
    }

    // 共享 low_freq_queue（简化处理）
    object->low_freq_queue = src->low_freq_queue;

    XContainerSize(object) = XContainerSize(src);
    XContainerCapacity(object) = XContainerCapacity(src);
    XContainerTypeSize(object) = XContainerTypeSize(src);

    // 共享 mapPriority
    object->mapPriority = src->mapPriority;
}

void VXClass_move(XPriorityMapQueue* object, XPriorityMapQueue* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XPriorityMapQueue_init(object,
            XContainerTypeSize(src->low_freq_queue) - XContainerTypeSize(src),
            XContainerCompare(src->low_freq_queue),
            src->low_freq_queue->m_order,
            XContainerTypeSize(src));
    }
    else if (XContainerSharedData(object))
    {
        XSharedData_release_with(XContainerSharedData(object), VXPriorityMapQueueDataDelete, object);
    }

    // 转移所有权
    XContainerSharedData(object) = XContainerSharedData(src);
    object->low_freq_queue = src->low_freq_queue;
    object->mapPriority = src->mapPriority;
    object->m_priorityCopyMethod = src->m_priorityCopyMethod;
    object->m_priorityMoveMethod = src->m_priorityMoveMethod;
    object->m_priorityDeinitMethod = src->m_priorityDeinitMethod;
    XContainerSize(object) = XContainerSize(src);
    XContainerCapacity(object) = XContainerCapacity(src);
    XContainerTypeSize(object) = XContainerTypeSize(src);

    // 清空源对象
    XContainerSharedData(src) = NULL;
    src->low_freq_queue = NULL;
    src->mapPriority = NULL;
    XContainerSize(src) = 0;
    XContainerCapacity(src) = 0;
}
