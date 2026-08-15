#include "XCircularQueue.h"
#if XCircularQueue_ON
#include "XAlgorithm.h"
#include <string.h>
#include <stdlib.h>

// =============== 虚函数前置声明 ===============
static bool VXCircularQueue_isEmpty(const XCircularQueue* this_queue);
static bool VXCircularQueue_isFull(const XCircularQueue* this_queue);
static void VXCircularQueue_clear(XCircularQueue* this_queue);
static size_t VXCircularQueue_size(const XCircularQueue* this_queue);
static bool VXCircularQueue_push(XCircularQueue* this_queue, void* pvValue, XCDataCreatMethod dataCreatMethod);
static void VXCircularQueue_pop(XCircularQueue* this_queue);
static void* VXCircularQueue_top(XCircularQueue* this_queue);
static bool VXCircularQueue_receive(XCircularQueue* this_queue, void* pvBuffer);
static void VXClass_copy(XCircularQueue* object, const XCircularQueue* src);
static void VXClass_move(XCircularQueue* object, XCircularQueue* src);
static void VXClass_deinit(XCircularQueue* this_queue);

// =============== 内部辅助函数 ===============
static bool enlargeCapacity(XCircularQueue* this_queue);
static void XCircularQueue_init_with_memory(XCircularQueue* this_queue,
    size_t typeSize, size_t count, XMemoryType memory);

// =============== 虚函数表初始化 ===============
XVtable* XCircularQueue_class_init()
{
    XVTABLE_INIT_DEFAULT_SIZE(XCIRCULARQUEUE_VTABLE_SIZE)
	XCLASS_SET_CLASS_NAME_DEFAULT("XCircularQueue");
        XVTABLE_INHERIT_XCLASS(XContainer);
    void* table[] = {
        VXCircularQueue_push,
        VXCircularQueue_pop,
        VXCircularQueue_top,
        VXCircularQueue_receive,
        VXCircularQueue_isFull
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_IsEmpty, VXCircularQueue_isEmpty);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXCircularQueue_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Size, VXCircularQueue_size);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXClass_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XCircularQueue);
    return XVTABLE_DEFAULT;
}

// =============== 内联辅助函数 ===============

/**
 * @brief 获取环形队列容量
 */
static inline size_t getCapacity(const XCircularQueue* this_queue)
{
    return XContainerSize(this_queue);
}

/**
 * @brief 获取数据缓冲区指针
 */
static inline uint8_t* getBuffer(XCircularQueue* this_queue)
{
    return (uint8_t*)XContainerDataPtr(this_queue);
}

/**
 * @brief 扩容
 */
static bool enlargeCapacity(XCircularQueue* this_queue)
{
    size_t old_capacity = getCapacity(this_queue);
    size_t new_capacity = (size_t)(old_capacity * 1.5);
    if (new_capacity < old_capacity + 1)
        new_capacity = old_capacity + 1;

    size_t typeSize = XContainerTypeSize(this_queue);
    size_t count = VXCircularQueue_size(this_queue);

    // 分配新缓冲区
    uint8_t* newData = (uint8_t*)XContainer_malloc(this_queue, typeSize * new_capacity);
    if (!newData) return false;

    // 复制元素到新数组（线性化）
    uint8_t* oldData = getBuffer(this_queue);
    size_t src_index = this_queue->m_head;

    for (size_t i = 0; i < count; i++)
    {
        memcpy(newData + i * typeSize,
            oldData + src_index * typeSize,
            typeSize);
        src_index = (src_index + 1) % old_capacity;
    }

    // 释放旧缓冲区
    XContainer_free(this_queue, oldData);

    // 更新状态
    XContainerDataPtr(this_queue) = newData;
    XContainerSize(this_queue) = new_capacity;
    XContainerCapacity(this_queue) = new_capacity;
    this_queue->m_head = 0;
    this_queue->m_tail = count;

    return true;
}

// =============== 虚函数实现 ===============

static bool VXCircularQueue_isEmpty(const XCircularQueue* this_queue)
{
    return (this_queue->m_head == this_queue->m_tail);
}

static bool VXCircularQueue_isFull(const XCircularQueue* this_queue)
{
    size_t capacity = getCapacity(this_queue);
    return ((this_queue->m_tail + 1) % capacity == this_queue->m_head);
}

static void VXCircularQueue_clear(XCircularQueue* this_queue)
{
    if (!this_queue) return;

    uint8_t* buffer = getBuffer(this_queue);
    if (!buffer) return;

    // 调用元素析构函数
    if (XContainerDataDeinitMethod(this_queue))
    {
        size_t typeSize = XContainerTypeSize(this_queue);
        size_t capacity = getCapacity(this_queue);
        size_t current = this_queue->m_head;

        while (current != this_queue->m_tail)
        {
            XContainerDataDeinitMethod(this_queue)(buffer + current * typeSize);
            current = (current + 1) % capacity;
        }
    }

    this_queue->m_head = 0;
    this_queue->m_tail = 0;
}

static size_t VXCircularQueue_size(const XCircularQueue* this_queue)
{
    size_t capacity = getCapacity(this_queue);
    if (capacity == 0) return 0;

    if (this_queue->m_tail >= this_queue->m_head)
        return this_queue->m_tail - this_queue->m_head;
    else
        return this_queue->m_tail + capacity - this_queue->m_head;
}

static bool VXCircularQueue_push(XCircularQueue* this_queue, void* pvValue, XCDataCreatMethod dataCreatMethod)
{
    if (!this_queue || !pvValue) return false;

    // 检查是否已满
    if (VXCircularQueue_isFull(this_queue))
    {
        if (!this_queue->m_autoExpansion)
            return false;

        if (!enlargeCapacity(this_queue))
            return false;
    }

    size_t typeSize = XContainerTypeSize(this_queue);
    size_t capacity = getCapacity(this_queue);
    uint8_t* buffer = getBuffer(this_queue);

    // 写入数据
    if (dataCreatMethod)
    {
        memset(buffer + this_queue->m_tail * typeSize, 0, typeSize);
        dataCreatMethod(buffer + this_queue->m_tail * typeSize, pvValue);
    }
    else
    {
        memcpy(buffer + this_queue->m_tail * typeSize, pvValue, typeSize);
    }

    // 移动尾指针
    this_queue->m_tail = (this_queue->m_tail + 1) % capacity;
    return true;
}

static void VXCircularQueue_pop(XCircularQueue* this_queue)
{
    if (!this_queue || VXCircularQueue_isEmpty(this_queue))
        return;

    uint8_t* buffer = getBuffer(this_queue);
    size_t typeSize = XContainerTypeSize(this_queue);
    size_t capacity = getCapacity(this_queue);

    // 调用析构函数
    if (XContainerDataDeinitMethod(this_queue))
    {
        XContainerDataDeinitMethod(this_queue)(buffer + this_queue->m_head * typeSize);
    }

    // 移动头指针
    this_queue->m_head = (this_queue->m_head + 1) % capacity;
}

static void* VXCircularQueue_top(XCircularQueue* this_queue)
{
    if (!this_queue || VXCircularQueue_isEmpty(this_queue))
        return NULL;

    size_t typeSize = XContainerTypeSize(this_queue);
    uint8_t* buffer = getBuffer(this_queue);
    return buffer + this_queue->m_head * typeSize;
}

static bool VXCircularQueue_receive(XCircularQueue* this_queue, void* pvBuffer)
{
    if (!this_queue || !pvBuffer || VXCircularQueue_isEmpty(this_queue))
        return false;

    size_t typeSize = XContainerTypeSize(this_queue);
    size_t capacity = getCapacity(this_queue);
    uint8_t* buffer = getBuffer(this_queue);

    // 复制数据到缓冲区
    void* pvTop = buffer + this_queue->m_head * typeSize;
    memcpy(pvBuffer, pvTop, typeSize);

    // 调用析构函数
    if (XContainerDataDeinitMethod(this_queue))
        XContainerDataDeinitMethod(this_queue)(pvTop);

    // 移动头指针
    this_queue->m_head = (this_queue->m_head + 1) % capacity;
    return true;
}

static void VXClass_copy(XCircularQueue* object, const XCircularQueue* src)
{
    if (!object || !src) return;

    size_t typeSize = XContainerTypeSize(src);
    size_t capacity = getCapacity(src);
    size_t count = VXCircularQueue_size(src);
    bool target_uninitialized = XClassIsVtableNull(object);
    XMemory* target_memory = target_uninitialized ? NULL : XContainer_memory(object);

    // 如果目标未初始化
    if (target_uninitialized)
    {
        XCircularQueue_init_with_memory(object, typeSize,
            capacity > 0 ? capacity - 1 : 0, XContainer_memory_type(src));
    }
    else
    {
        // 清理目标原有数据
        VXClass_deinit(object);
        XContainer_init(object, typeSize, false);
        XClassGetVtable(object) = XCircularQueue_class_init();
        Class_Memory(object) = target_memory;

        // 分配缓冲区
        uint8_t* buffer = (uint8_t*)XContainer_malloc(object, typeSize * capacity);
        if (!buffer) return;

        XContainerDataPtr(object) = buffer;
        XContainerSize(object) = capacity;
        XContainerCapacity(object) = capacity;
    }

    // 复制回调函数
    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

    // 深拷贝数据
    uint8_t* srcBuffer = getBuffer((XCircularQueue*)src);
    uint8_t* dstBuffer = getBuffer(object);
    size_t src_index = src->m_head;

    for (size_t i = 0; i < count; i++)
    {
        memcpy(dstBuffer + i * typeSize,
            srcBuffer + src_index * typeSize,
            typeSize);
        src_index = (src_index + 1) % capacity;
    }

    // 设置指针
    object->m_autoExpansion = src->m_autoExpansion;
    object->m_head = 0;
    object->m_tail = count;
}

static void VXClass_move(XCircularQueue* object, XCircularQueue* src)
{
    if (!object || !src) return;
    XMemory* source_memory = Class_Memory(src);

    // 如果目标未初始化
    if (XClassIsVtableNull(object))
    {
        XContainer_init(object, XContainerTypeSize(src), false);
        XClassGetVtable(object) = XCircularQueue_class_init();
    }
    else
    {
        VXClass_deinit(object);
    }

    // 转移所有权（直接复制所有成员）
    memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XCircularQueue) - sizeof(XClass));
    Class_Memory(object) = source_memory;

    // 清空源对象
    XContainerDataPtr(src) = NULL;
    XContainerCapacity(src) = 0;
    XContainerSize(src) = 0;
    src->m_head = 0;
    src->m_tail = 0;
    src->m_autoExpansion = false;
}

static void VXClass_deinit(XCircularQueue* this_queue)
{
    if (!this_queue) return;

    uint8_t* buffer = getBuffer(this_queue);

    // 调用元素析构函数
    if (buffer && XContainerDataDeinitMethod(this_queue))
    {
        size_t typeSize = XContainerTypeSize(this_queue);
        size_t capacity = getCapacity(this_queue);
        size_t current = this_queue->m_head;

        while (current != this_queue->m_tail)
        {
            XContainerDataDeinitMethod(this_queue)(buffer + current * typeSize);
            current = (current + 1) % capacity;
        }
    }

    // 释放缓冲区
    if (buffer)
    {
        XContainer_free(this_queue, buffer);
        XContainerDataPtr(this_queue) = NULL;
    }

    // 重置状态
    this_queue->m_autoExpansion = false;
    this_queue->m_head = 0;
    this_queue->m_tail = 0;
    XContainerSize(this_queue) = 0;
    XContainerCapacity(this_queue) = 0;
}

// =============== 公开 API ===============

static void XCircularQueue_init_with_memory(XCircularQueue* this_queue,
    size_t typeSize, size_t count, XMemoryType memory)
{
    if (ISNULL(this_queue, "") || ISNULL(typeSize, "") || ISNULL(count, ""))
        return;

    // 初始化容器基类（不使用 COW）
    XContainer_init(this_queue, typeSize, false);
    XClassGetVtable(this_queue) = XCircularQueue_class_init();
    Set_Class_Memory(this_queue, memory);

    // 分配缓冲区（容量 + 1，环形队列需要一个空位判断满）
    size_t capacity = count + 1;
    size_t bytes = typeSize * capacity;

    void* buffer = XContainer_malloc(this_queue, bytes);
    if (!buffer) return;

    XContainerDataPtr(this_queue) = buffer;
    XContainerSize(this_queue) = capacity;
    XContainerCapacity(this_queue) = capacity;

    this_queue->m_autoExpansion = false;
    this_queue->m_head = 0;
    this_queue->m_tail = 0;
}

void XCircularQueue_init(XCircularQueue* this_queue, size_t typeSize, size_t count)
{
    XCircularQueue_init_with_memory(this_queue, typeSize, count,
        XCLASS_DEFAULT_MEMORY_TYPE);
}

XCircularQueue* XCircularQueue_create_ex(XMemoryType memory, size_t typeSize, size_t count)
{
    if (ISNULL(typeSize, "") || ISNULL(count, ""))
        return NULL;

    XCircularQueue* this_queue = XMemory_malloc(sizeof(XCircularQueue), memory);
    if (!this_queue) return NULL;

    XCircularQueue_init_with_memory(this_queue, typeSize, count, memory);
    Set_Class_Memory(this_queue, memory); Set_Class_IsHeap(this_queue, true);
    return this_queue;
}

void XCircularQueue_setAutoExpansion(XCircularQueue* this_queue, bool autoExpansion)
{
    if (this_queue)
    {
        this_queue->m_autoExpansion = autoExpansion;
    }
}

size_t XCircularQueue_remove(XCircularQueue* this_queue, const void* value, size_t n)
{
    if (!this_queue || !value) return 0;
    if (VXCircularQueue_isEmpty(this_queue)) return 0;

    XCompare compare = XContainerCompare(this_queue);
    if (!compare) return 0;

    uint8_t* buffer = getBuffer(this_queue);
    size_t typeSize = XContainerTypeSize(this_queue);
    size_t capacity = getCapacity(this_queue);
    size_t count = VXCircularQueue_size(this_queue);

    if (count == 0) return 0;

    size_t max_to_remove = (n == 0) ? count : n;
    size_t removed_count = 0;

    // 记录要删除的索引
    size_t* indices = XContainer_malloc(this_queue, count * sizeof(size_t));
    if (!indices) return 0;

    size_t found_count = 0;
    size_t current = this_queue->m_head;

    // 查找匹配元素
    for (size_t i = 0; i < count && found_count < max_to_remove; i++)
    {
        void* elem = buffer + (current * typeSize);
        if (compare(elem, value) == 0)
        {
            indices[found_count++] = current;
        }
        current = (current + 1) % capacity;
    }

    if (found_count == 0)
    {
        XContainer_free(this_queue, indices);
        return 0;
    }

    // 从后往前删除
    for (size_t i = found_count; i > 0; i--)
    {
        size_t idx = indices[i - 1];
        void* elem = buffer + (idx * typeSize);

        if (XContainerDataDeinitMethod(this_queue))
            XContainerDataDeinitMethod(this_queue)(elem);

        count = VXCircularQueue_size(this_queue);
        capacity = getCapacity(this_queue);

        if (count == 1)
        {
            this_queue->m_head = 0;
            this_queue->m_tail = 0;
        }
        else if (idx == this_queue->m_head)
        {
            this_queue->m_head = (this_queue->m_head + 1) % capacity;
        }
        else if ((this_queue->m_tail == 0 ? capacity : this_queue->m_tail) - 1 == idx)
        {
            this_queue->m_tail = idx;
        }
        else
        {
            // 中间元素，需要移动
            bool wrapped = this_queue->m_tail < this_queue->m_head;

            if (!wrapped)
            {
                size_t after = this_queue->m_tail - idx - 1;
                if (after > 0)
                {
                    memmove(buffer + idx * typeSize,
                        buffer + (idx + 1) * typeSize,
                        after * typeSize);
                }
                this_queue->m_tail--;
            }
            else
            {
                if (idx >= this_queue->m_head)
                {
                    size_t after_first = capacity - idx - 1;
                    if (after_first > 0)
                    {
                        memmove(buffer + idx * typeSize,
                            buffer + (idx + 1) * typeSize,
                            after_first * typeSize);
                    }
                    if (this_queue->m_tail > 0)
                    {
                        memcpy(buffer + (capacity - 1) * typeSize, buffer, typeSize);
                        if (this_queue->m_tail > 1)
                        {
                            memmove(buffer, buffer + typeSize,
                                (this_queue->m_tail - 1) * typeSize);
                        }
                        this_queue->m_tail--;
                    }
                    else
                    {
                        this_queue->m_tail = capacity - 1;
                    }
                }
                else
                {
                    size_t after_second = this_queue->m_tail - idx - 1;
                    if (after_second > 0)
                    {
                        memmove(buffer + idx * typeSize,
                            buffer + (idx + 1) * typeSize,
                            after_second * typeSize);
                    }
                    this_queue->m_tail--;
                }
            }
        }

        removed_count++;
        buffer = getBuffer(this_queue);
        capacity = getCapacity(this_queue);
    }

    XContainer_free(this_queue, indices);
    return removed_count;
}

#endif
