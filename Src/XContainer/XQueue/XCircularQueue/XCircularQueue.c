#include "XCircularQueue.h"
#if XCircularQueue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
static bool VXCircularQueue_isEmpty(const XCircularQueue* this_queue);
static bool VXCircularQueue_isFull(const XCircularQueue* this_queue);
static void VXCircularQueue_clear(XCircularQueue* this_queue);//清空
static size_t VXCircularQueue_size(const XCircularQueue* this_queue);
//插入到队列的队尾
static bool VXCircularQueue_push(XCircularQueue* this_queue, void* pvValue, XCDataCreatMethod dataCreatMethod);
//出队
static void VXCircularQueue_pop(XCircularQueue* this_queue);
// 返回队头元素
static void* VXCircularQueue_top(XCircularQueue* this_queue);
static bool VXCircularQueue_receive(XCircularQueue* this_queue, void* pvBuffer);

//COW分离（继承自XVector，使用XVector的分离函数）
static bool VXCircularQueueDetachIfNeeded(XCircularQueue* this_queue);
static void VXCircularQueueDataDelete(void* data, XCircularQueue* this_queue);
// 确保 XSharedData 存在（延迟创建）
static bool ensureSharedData(XCircularQueue* this_queue);

static void VXClass_copy(XCircularQueue* object, const XCircularQueue* src);
static void VXClass_move(XCircularQueue* object, XCircularQueue* src);
static void VXClass_deinit(XCircularQueue* this_queue);

XVtable* XCircularQueue_class_init()
{
	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCIRCULARQUEUE_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_XCLASS(XContainer);
	void* table[] = { VXCircularQueue_push,VXCircularQueue_pop,VXCircularQueue_top,VXCircularQueue_receive,VXCircularQueue_isFull };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainer_IsEmpty,VXCircularQueue_isEmpty);
	XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear,VXCircularQueue_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXContainer_Size,VXCircularQueue_size);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXClass_deinit);
#if SHOWCONTAINERSIZE
	printf("XCircularQueue size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
		return XVTABLE_DEFAULT;
}

// 确保 XSharedData 存在（延迟创建）
bool ensureSharedData(XCircularQueue* this_queue)
{
    if (!XContainerSharedData(this_queue))
    {
        // 创建默认容量的环形队列（容量+1，因为环形队列需要一个空位）
        size_t defaultCapacity = 16;
        XSharedData* sd = XSharedData_create(NULL, XContainerTypeSize(this_queue) * (defaultCapacity + 1));
        if (!sd)
            return false;
        XContainerSharedData(this_queue) = sd;
        XContainerSize(this_queue) = defaultCapacity + 1;
        XContainerCapacity(this_queue) = defaultCapacity + 1;
        this_queue->m_head = 0;
        this_queue->m_tail = 0;
    }
    return true;
}

// COW分离：如果数据被共享，创建独立副本
bool VXCircularQueueDetachIfNeeded(XCircularQueue* this_queue)
{
    if (!XContainerSharedData(this_queue) || !XSharedData_isShared(XContainerSharedData(this_queue)))
        return true; // 不共享，无需分离

    size_t typeSize = XContainerTypeSize(this_queue);
    size_t capacity = XContainerSize(this_queue); // 环形队列容量
    size_t count = VXCircularQueue_size(this_queue);

    // 创建新的 XSharedData
    XSharedData* newShared = XSharedData_create(NULL, typeSize * capacity);
    if (!newShared) return false;

    // 深拷贝元素
    uint8_t* oldData = (uint8_t*)XContainerDataPtr(this_queue);
    uint8_t* newData = (uint8_t*)newShared->data;
    size_t src_index = this_queue->m_head;
    for (size_t i = 0; i < count; i++)
    {
        if (XContainerDataCopyMethod(this_queue))
            XContainerDataCopyMethod(this_queue)(newData + i * typeSize, oldData + src_index * typeSize);
        else
            memcpy(newData + i * typeSize, oldData + src_index * typeSize, typeSize);
        src_index = (src_index + 1) % capacity;
    }

    // 减少旧引用，设置新引用
    XSharedData_release(XContainerSharedData(this_queue));
    XContainerSharedData(this_queue) = newShared;
    this_queue->m_head = 0;
    this_queue->m_tail = count;
    XContainerCapacity(this_queue) = capacity;
    return true;
}

// 删除数据（XSharedData释放回调）
void VXCircularQueueDataDelete(void* data, XCircularQueue* this_queue)
{
    if (this_queue == NULL) return;
    // 调用元素的析构函数
    if (XContainerDataDeinitMethod(this_queue))
    {
        size_t typeSize = XContainerTypeSize(this_queue);
        size_t capacity = XContainerSize(this_queue);
        uint8_t* buffer = (uint8_t*)XContainerDataPtr(this_queue);
        size_t current = this_queue->m_head;
        while (current != this_queue->m_tail)
        {
            XContainerDataDeinitMethod(this_queue)(buffer + current * typeSize);
            current = (current + 1) % capacity;
        }
    }
    this_queue->m_head = 0;
    this_queue->m_tail = 0;
    XContainerSize(this_queue) = 0;
    XContainerCapacity(this_queue) = 0;
    XContainerSharedData(this_queue) = NULL;
    this_queue->m_autoExpansion = false;
}

bool VXCircularQueue_isEmpty(const XCircularQueue* this_queue)
{
	/*if (this_queue == NULL)
		return true;*/
	return ((this_queue->m_head) == (this_queue->m_tail));//头指针等于尾指针时为空
}

bool VXCircularQueue_isFull(const XCircularQueue* this_queue)
{
	/*if (this_queue == NULL)
		return false;*/
	return ((this_queue->m_tail + 1) % XContainerSize(this_queue) == this_queue->m_head);//尾指针下一个位置等于头指针时为满

}

void VXCircularQueue_clear(XCircularQueue* this_queue)
{
	/*if (this_queue == NULL)
		return;*/
    // 如果数据被共享，减少引用并置空
    if (XContainerSharedData(this_queue) && XSharedData_isShared(XContainerSharedData(this_queue)))
    {
        XSharedData_release(XContainerSharedData(this_queue));
        XContainerSharedData(this_queue) = NULL;
        this_queue->m_head = 0;
        this_queue->m_tail = 0;
        XContainerSize(this_queue) = 0;
        XContainerCapacity(this_queue) = 0;
        return;
    }
    // 不共享，调用元素析构函数
    if (XContainerDataDeinitMethod(this_queue))
    {
        size_t typeSize = XContainerTypeSize(this_queue);
        size_t capacity = XContainerSize(this_queue);
        uint8_t* buffer = (uint8_t*)XContainerDataPtr(this_queue);
        size_t current = this_queue->m_head;
        while (current != this_queue->m_tail)
        {
            XContainerDataDeinitMethod(this_queue)(buffer + current * typeSize);
            current = (current + 1) % capacity;
        }
    }
	this_queue->m_head = this_queue->m_tail;
}

size_t VXCircularQueue_size(const XCircularQueue* this_queue)
{
	/*if (this_queue == NULL)
		return 0;*/
	if (this_queue->m_tail >= this_queue->m_head)
		return this_queue->m_tail - this_queue->m_head;
	else
		return this_queue->m_tail+XContainerSize(this_queue) - this_queue->m_head;

}

bool VXCircularQueue_push(XCircularQueue* this_queue, void* pvValue, XCDataCreatMethod dataCreatMethod)
{
    if (!ensureSharedData(this_queue) || !VXCircularQueueDetachIfNeeded(this_queue))
        return false;
	if (VXCircularQueue_isFull(this_queue))
	{
		if (!this_queue->m_autoExpansion)
			return false;//不开启自动扩容，返回失败
		// 准备扩容
		size_t old_capacity = XContainerSize(this_queue); // 原始分配容量
		size_t new_capacity = (size_t)(old_capacity * 1.5);
		if (new_capacity < old_capacity + 1) new_capacity = old_capacity + 1; // 至少增加1
		size_t elem_size = XContainerTypeSize(this_queue);

		// 创建新的 XSharedData
		XSharedData* newShared = XSharedData_create(NULL, elem_size * new_capacity);
		if (newShared == NULL)
			return false;

		// 复制元素到新数组（按元素大小复制，不是逐字节）
		size_t count = VXCircularQueue_size(this_queue);
		size_t src_index = this_queue->m_head;
		uint8_t* oldData = (uint8_t*)XContainerDataPtr(this_queue);
		uint8_t* newData = (uint8_t*)newShared->data;

		for (size_t i = 0; i < count; i++) {
			memcpy(newData + (i * elem_size),
				oldData + (src_index * elem_size),
				elem_size);
			src_index = (src_index + 1) % old_capacity;
		}

		// 设置新数组的头尾指针
		this_queue->m_head = 0;
		this_queue->m_tail = count;  // 新队列的长度等于原队列的元素个数

		// 释放原 XSharedData 并设置新的
		XSharedData_release(XContainerSharedData(this_queue));
		XContainerSharedData(this_queue) = newShared;
		XContainerSize(this_queue) = new_capacity;
		XContainerCapacity(this_queue) = new_capacity;
	}
	if (dataCreatMethod)
	{
		memset(((char*)XContainerDataPtr(this_queue)) + this_queue->m_tail * XContainerTypeSize(this_queue),0, XContainerTypeSize(this_queue));
		dataCreatMethod(((char*)XContainerDataPtr(this_queue)) + this_queue->m_tail * XContainerTypeSize(this_queue), pvValue);
	}
	else
	{
		memcpy(((char*)XContainerDataPtr(this_queue)) + this_queue->m_tail * XContainerTypeSize(this_queue), pvValue, XContainerTypeSize(this_queue));
	}
	this_queue->m_tail = (this_queue->m_tail + 1) % XContainerSize(this_queue);//指针后移取模实现环形
	return true;
}

void VXCircularQueue_pop(XCircularQueue* this_queue)
{
	if (VXCircularQueue_isEmpty(this_queue))
		return;
    if (!ensureSharedData(this_queue) || !VXCircularQueueDetachIfNeeded(this_queue))
        return;
	if (XContainerDataDeinitMethod(this_queue) != NULL)
		XContainerDataDeinitMethod(this_queue)(VXCircularQueue_top(this_queue));
	this_queue->m_head= (this_queue->m_head + 1) % XContainerSize(this_queue);//指针后移取模实现环形
}

void* VXCircularQueue_top(XCircularQueue* this_queue)
{
	if(VXCircularQueue_isEmpty(this_queue))
		return NULL;
	return ((char*)XContainerDataPtr(this_queue)) + (this_queue->m_head * XContainerTypeSize(this_queue));
}
bool VXCircularQueue_receive(XCircularQueue* this_queue, void* pvBuffer)
{
	if (VXCircularQueue_isEmpty(this_queue))
		return false;
    if (!ensureSharedData(this_queue) || !VXCircularQueueDetachIfNeeded(this_queue))
        return false;
	void* pvTop = ((char*)XContainerDataPtr(this_queue)) + (this_queue->m_head * XContainerTypeSize(this_queue));
	memcpy(pvBuffer, pvTop, XContainerTypeSize(this_queue));
	if (XContainerDataDeinitMethod(this_queue) != NULL)
		XContainerDataDeinitMethod(this_queue)(pvTop);
	this_queue->m_head = (this_queue->m_head + 1) % XContainerSize(this_queue);//指针后移取模实现环形
	return true;
}
void VXClass_copy(XCircularQueue* object, const XCircularQueue* src)
{
    // 如果目标还未初始化，先初始化
    if (((XClass*)object)->m_vtable == NULL)
    {
        XCircularQueue_init(object, XContainerTypeSize(src), XContainerCapacity(src) > 0 ? XContainerCapacity(src) - 1 : 0);
    }
    else if (XContainerSharedData(object))// 释放目标原有数据
    {
        XSharedData_release_with(XContainerSharedData(object), VXCircularQueueDataDelete, object);
    }

    // 复制回调函数
    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

    // 共享源数据的 XSharedData（COW 机制）
    XContainerSharedData(object) = XContainerSharedData(src);
    if (XContainerSharedData(object))
    {
        XSharedData_addRef(XContainerSharedData(object));
    }

    XContainerSize(object) = XContainerSize(src);
    XContainerCapacity(object) = XContainerCapacity(src);
    XContainerTypeSize(object) = XContainerTypeSize(src);
    object->m_autoExpansion = src->m_autoExpansion;
    object->m_head = src->m_head;
    object->m_tail = src->m_tail;
}

void VXClass_move(XCircularQueue* object, XCircularQueue* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XCircularQueue_init(object,XContainerTypeSize(src), XContainerCapacity(src)-1);
	}
	else if (XContainerSharedData(object))
	{
		XSharedData_release_with(XContainerSharedData(object), VXCircularQueueDataDelete, object);
	}

    // 转移所有权（指针拷贝）
    memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XCircularQueue) - sizeof(XClass));

    // 清空源对象
    XContainerSharedData(src) = NULL;
    XContainerCapacity(src) = 0;
    XContainerSize(src) = 0;
    src->m_head = 0;
    src->m_tail = 0;
    src->m_autoExpansion = false;
}

void VXClass_deinit(XCircularQueue* this_queue)
{
    if (XContainerSharedData(this_queue))
    {
        XSharedData_release_with(XContainerSharedData(this_queue), VXCircularQueueDataDelete, this_queue);
    }
    this_queue->m_autoExpansion = false;
    this_queue->m_head = 0;
    this_queue->m_tail = 0;
    XContainerSize(this_queue) = 0;
    XContainerCapacity(this_queue) = 0;
    XContainerSharedData(this_queue) = NULL;
}

void XCircularQueue_init(XCircularQueue* this_queue, size_t typeSize, size_t count)
{
    if (ISNULL(this_queue, "") || ISNULL(typeSize, "") || ISNULL(count, ""))
        return;
    XVector_init(this_queue, typeSize,false);
    XVector_resize_base(this_queue, count + 1);
    this_queue->m_autoExpansion = false;
    this_queue->m_head = 0;
    this_queue->m_tail = 0;
    XClassGetVtable(this_queue) = XCircularQueue_class_init();
}
XCircularQueue* XCircularQueue_create(size_t typeSize, size_t count)
{
    if (ISNULL(typeSize, "") || ISNULL(count, ""))
        return NULL;
    XCircularQueue* this_queue = XMalloc_System(sizeof(XCircularQueue));
    XCircularQueue_init(this_queue, typeSize, count);
    Set_Class_MemoryFree(this_queue, XFree_System);
    return this_queue;
}

size_t XCircularQueue_remove(XCircularQueue* this_queue, const void* value, size_t n)
{
    if (!this_queue || !value) {
        return 0;
    }
    // COW分离：如果数据被共享，创建独立副本
    if (!ensureSharedData(this_queue) || !VXCircularQueueDetachIfNeeded(this_queue)) {
        return 0;
    }
    XCompare compare = XContainerCompare(this_queue);
    if (!compare)return false;
    // 检查队列是否为空
    if (this_queue->m_head == this_queue->m_tail) {
        return 0;
    }

    uint8_t* buffer = (uint8_t*)XContainerDataPtr(this_queue);
    size_t elem_size = XContainerTypeSize(this_queue);
    size_t capacity = XContainerSize(this_queue); // 实际分配的容量

    // 计算当前元素数量
    size_t count;
    if (this_queue->m_tail >= this_queue->m_head) {
        count = this_queue->m_tail - this_queue->m_head;
    }
    else {
        count = this_queue->m_tail + capacity - this_queue->m_head;
    }

    if (count == 0) {
        return 0;
    }

    // 如果 n 为 0，表示删除所有匹配元素
    size_t max_to_remove = (n == 0) ? count : n;
    size_t removed_count = 0;

    // 使用数组记录要删除的索引，避免在遍历时修改队列结构
    size_t* indices_to_remove = XMalloc_System(count * sizeof(size_t));
    if (!indices_to_remove) {
        return 0; // 内存分配失败
    }

    size_t found_count = 0;
    size_t current = this_queue->m_head;

    // 第一次遍历：找到所有要删除的元素索引
    for (size_t i = 0; i < count && found_count < max_to_remove; i++) {
        void* current_element = buffer + (current * elem_size);
        if (compare(current_element, value) == 0) {
            indices_to_remove[found_count] = current;
            found_count++;
        }
        current = (current + 1) % capacity;
    }

    if (found_count == 0) {
        XFree_System(indices_to_remove);
        return 0; // 未找到任何匹配元素
    }

    // 按索引从大到小排序（从后往前删除，避免索引偏移问题）
    // 由于我们是从头到尾遍历的，所以索引已经是按顺序的
    // 但为了安全起见，我们需要从后往前处理

    // 重新计算当前的 count，因为可能有变化
    if (this_queue->m_tail >= this_queue->m_head) {
        count = this_queue->m_tail - this_queue->m_head;
    }
    else {
        count = this_queue->m_tail + capacity - this_queue->m_head;
    }

    // 从后往前删除元素（按索引降序）
    for (size_t i = found_count; i > 0; i--) {
        size_t target_index = indices_to_remove[i - 1];

        // 找到要删除的元素
        void* target_element = buffer + (target_index * elem_size);

        // 如果容器设置了数据释放方法，先释放该元素
        if (XContainerDataDeinitMethod(this_queue)) {
            XContainerDataDeinitMethod(this_queue)(target_element);
        }

        // 重新计算当前元素数量（因为之前的删除可能影响了队列状态）
        if (this_queue->m_tail >= this_queue->m_head) {
            count = this_queue->m_tail - this_queue->m_head;
        }
        else {
            count = this_queue->m_tail + capacity - this_queue->m_head;
        }

        if (count == 1) {
            // 只有一个元素，直接清空
            this_queue->m_head = 0;
            this_queue->m_tail = 0;
            buffer = (uint8_t*)XContainerDataPtr(this_queue); // 重新获取buffer
            capacity = XContainerSize(this_queue);
        }
        else if (target_index == this_queue->m_head) {
            // 删除队头元素
            this_queue->m_head = (this_queue->m_head + 1) % capacity;
        }
        else if ((this_queue->m_tail == 0 ? capacity : this_queue->m_tail) - 1 == target_index) {
            // 删除队尾元素（考虑环绕情况）
            this_queue->m_tail = target_index;
        }
        else {
            // 删除中间元素，需要批量移动

            // 确定数据布局：是否环绕
            bool is_wrapped = this_queue->m_tail < this_queue->m_head;

            if (!is_wrapped) {
                // 数据连续存储 [head, tail)
                size_t elements_after = this_queue->m_tail - target_index - 1;
                if (elements_after > 0) {
                    // 批量前移后面的元素
                    memmove(buffer + (target_index * elem_size),
                        buffer + ((target_index + 1) * elem_size),
                        elements_after * elem_size);
                }
                this_queue->m_tail--;
            }
            else {
                // 数据环绕存储 [head, capacity) + [0, tail)
                if (target_index >= this_queue->m_head) {
                    // 目标在前半段 [head, capacity)
                    size_t elements_after_in_first = capacity - target_index - 1;
                    if (elements_after_in_first > 0) {
                        memmove(buffer + (target_index * elem_size),
                            buffer + ((target_index + 1) * elem_size),
                            elements_after_in_first * elem_size);
                    }
                    // 处理环绕部分
                    if (this_queue->m_tail > 0) {
                        memcpy(buffer + ((capacity - 1) * elem_size),
                            buffer,
                            elem_size);
                        if (this_queue->m_tail > 1) {
                            memmove(buffer,
                                buffer + elem_size,
                                (this_queue->m_tail - 1) * elem_size);
                        }
                        this_queue->m_tail--;
                    }
                    else {
                        this_queue->m_tail = capacity - 1;
                    }
                }
                else {
                    // 目标在后半段 [0, tail)
                    size_t elements_after_in_second = this_queue->m_tail - target_index - 1;
                    if (elements_after_in_second > 0) {
                        memmove(buffer + (target_index * elem_size),
                            buffer + ((target_index + 1) * elem_size),
                            elements_after_in_second * elem_size);
                    }
                    this_queue->m_tail--;
                }
            }
        }

        removed_count++;
        buffer = (uint8_t*)XContainerDataPtr(this_queue); // 重新获取buffer，因为可能扩容/缩容
        capacity = XContainerSize(this_queue);
    }

    XFree_System(indices_to_remove);
    return removed_count;
}

void XCircularQueue_setAutoExpansion(XCircularQueue* this_queue, bool autoExpansion)
{
    if (this_queue)
    {
        this_queue->m_autoExpansion = autoExpansion;
    }
}

#endif