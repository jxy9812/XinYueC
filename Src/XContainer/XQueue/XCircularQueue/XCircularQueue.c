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
static bool VXCircularQueueDetachIfNeeded(XCircularQueue* this_queue);
static void VXCircularQueueDataDelete(void* data, XCircularQueue* this_queue);
static bool ensureSharedData(XCircularQueue* this_queue);
static bool enlargeCapacity(XCircularQueue* this_queue);

// =============== 虚函数表初始化 ===============
XVtable* XCircularQueue_class_init()
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCIRCULARQUEUE_VTABLE_SIZE)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XContainer);
    void* table[] = { VXCircularQueue_push, VXCircularQueue_pop, VXCircularQueue_top, VXCircularQueue_receive, VXCircularQueue_isFull };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_IsEmpty, VXCircularQueue_isEmpty);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXCircularQueue_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Size, VXCircularQueue_size);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXClass_deinit);
#if SHOWCONTAINERSIZE
    printf("XCircularQueue size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

// =============== 内部辅助函数实现 ===============

/**
 * @brief 获取环形队列的实际容量（用于取模计算）
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
    return (uint8_t*)XContainerDataAddr(this_queue);
}

/**
 * @brief 确保 XSharedData 存在（延迟创建）
 */
static bool ensureSharedData(XCircularQueue* this_queue)
{
    if (!XContainerIsCow(this_queue)) return true;
    
    if (!XContainerSharedData(this_queue))
    {
        size_t defaultCapacity = 16;
        size_t typeSize = XContainerTypeSize(this_queue);
        size_t bytes = typeSize * defaultCapacity;
        
        XSharedData* sd = XSharedData_create(NULL, bytes);
        if (!sd) return false;
        
        XContainerSharedData(this_queue) = sd;
        XContainerSize(this_queue) = defaultCapacity;
        XContainerCapacity(this_queue) = defaultCapacity;
        this_queue->m_head = 0;
        this_queue->m_tail = 0;
    }
    return true;
}

/**
 * @brief COW分离：如果数据被共享，创建独立副本
 */
static bool VXCircularQueueDetachIfNeeded(XCircularQueue* this_queue)
{
    if (!XContainerIsCow(this_queue)) return true;
    
    XSharedData* sd = XContainerSharedData(this_queue);
    if (!sd || !XSharedData_isShared(sd))
        return true;

    size_t typeSize = XContainerTypeSize(this_queue);
    size_t capacity = getCapacity(this_queue);
    size_t count = VXCircularQueue_size(this_queue);

    size_t bytes = typeSize * capacity;
    XSharedData* newShared = XSharedData_create(NULL, bytes);
    if (!newShared) return false;

    uint8_t* oldData = getBuffer(this_queue);
    uint8_t* newData = (uint8_t*)newShared->data;
    size_t src_index = this_queue->m_head;
    
    if (XContainerDataCopyMethod(this_queue))
    {
        for (size_t i = 0; i < count; i++)
        {
            XContainerDataCopyMethod(this_queue)(
                newData + i * typeSize,
                oldData + src_index * typeSize);
            src_index = (src_index + 1) % capacity;
        }
    }
    else
    {
        for (size_t i = 0; i < count; i++)
        {
            memcpy(newData + i * typeSize,
                   oldData + src_index * typeSize,
                   typeSize);
            src_index = (src_index + 1) % capacity;
        }
    }

    XSharedData_release(sd);
    XContainerSharedData(this_queue) = newShared;
    this_queue->m_head = 0;
    this_queue->m_tail = count;
    
    return true;
}

/**
 * @brief 扩容内部函数
 */
static bool enlargeCapacity(XCircularQueue* this_queue)
{
    size_t old_capacity = getCapacity(this_queue);
    size_t new_capacity = (size_t)(old_capacity * 1.5);
    if (new_capacity < old_capacity + 1) new_capacity = old_capacity + 1;
    
    size_t typeSize = XContainerTypeSize(this_queue);
    size_t count = VXCircularQueue_size(this_queue);

    uint8_t* newData;
    XSharedData* newShared = NULL;
    
    if (XContainerIsCow(this_queue))
    {
        newShared = XSharedData_create(NULL, typeSize * new_capacity);
        if (!newShared) return false;
        newData = (uint8_t*)newShared->data;
    }
    else
    {
        newData = (uint8_t*)XMalloc_System(typeSize * new_capacity);
        if (!newData) return false;
    }

    uint8_t* oldData = getBuffer(this_queue);
    size_t src_index = this_queue->m_head;

    for (size_t i = 0; i < count; i++)
    {
        memcpy(newData + i * typeSize,
               oldData + src_index * typeSize,
               typeSize);
        src_index = (src_index + 1) % old_capacity;
    }

    if (XContainerIsCow(this_queue))
    {
        XSharedData_release(XContainerSharedData(this_queue));
        XContainerSharedData(this_queue) = newShared;
    }
    else
    {
        XFree_System(XContainerDataPtr(this_queue));
        XContainerDataPtr(this_queue) = newData;
    }

    XContainerSize(this_queue) = new_capacity;
    XContainerCapacity(this_queue) = new_capacity;
    this_queue->m_head = 0;
    this_queue->m_tail = count;

    return true;
}

/**
 * @brief 删除数据回调
 */
static void VXCircularQueueDataDelete(void* data, XCircularQueue* this_queue)
{
    if (!this_queue) return;
    
    if (XContainerDataDeinitMethod(this_queue))
    {
        size_t typeSize = XContainerTypeSize(this_queue);
        size_t capacity = getCapacity(this_queue);
        uint8_t* buffer = getBuffer(this_queue);
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
    if (XContainerIsCow(this_queue))
    {
        XSharedData* sd = XContainerSharedData(this_queue);
        if (sd && XSharedData_isShared(sd))
        {
            XSharedData_release(sd);
            XContainerSharedData(this_queue) = NULL;
            this_queue->m_head = 0;
            this_queue->m_tail = 0;
            XContainerSize(this_queue) = 0;
            XContainerCapacity(this_queue) = 0;
            return;
        }
    }
    
    if (XContainerDataDeinitMethod(this_queue))
    {
        size_t typeSize = XContainerTypeSize(this_queue);
        size_t capacity = getCapacity(this_queue);
        uint8_t* buffer = getBuffer(this_queue);
        size_t current = this_queue->m_head;
        
        while (current != this_queue->m_tail)
        {
            XContainerDataDeinitMethod(this_queue)(buffer + current * typeSize);
            current = (current + 1) % capacity;
        }
    }
    
    this_queue->m_head = this_queue->m_tail = 0;
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
    if (!ensureSharedData(this_queue))
        return false;
        
    if (VXCircularQueue_isFull(this_queue))
    {
        if (!this_queue->m_autoExpansion)
            return false;
        
        if (!enlargeCapacity(this_queue))
            return false;
    }
    
    if (!VXCircularQueueDetachIfNeeded(this_queue))
        return false;

    size_t typeSize = XContainerTypeSize(this_queue);
    size_t capacity = getCapacity(this_queue);
    uint8_t* buffer = getBuffer(this_queue);
    
    if (dataCreatMethod)
    {
        memset(buffer + this_queue->m_tail * typeSize, 0, typeSize);
        dataCreatMethod(buffer + this_queue->m_tail * typeSize, pvValue);
    }
    else
    {
        memcpy(buffer + this_queue->m_tail * typeSize, pvValue, typeSize);
    }
    
    this_queue->m_tail = (this_queue->m_tail + 1) % capacity;
    return true;
}

static void VXCircularQueue_pop(XCircularQueue* this_queue)
{
    if (VXCircularQueue_isEmpty(this_queue))
        return;
        
    if (!ensureSharedData(this_queue) || !VXCircularQueueDetachIfNeeded(this_queue))
        return;

    if (XContainerDataDeinitMethod(this_queue))
    {
        size_t typeSize = XContainerTypeSize(this_queue);
        uint8_t* buffer = getBuffer(this_queue);
        XContainerDataDeinitMethod(this_queue)(buffer + this_queue->m_head * typeSize);
    }
    
    size_t capacity = getCapacity(this_queue);
    this_queue->m_head = (this_queue->m_head + 1) % capacity;
}

static void* VXCircularQueue_top(XCircularQueue* this_queue)
{
    if (VXCircularQueue_isEmpty(this_queue))
        return NULL;
    
    size_t typeSize = XContainerTypeSize(this_queue);
    uint8_t* buffer = getBuffer(this_queue);
    return buffer + this_queue->m_head * typeSize;
}

static bool VXCircularQueue_receive(XCircularQueue* this_queue, void* pvBuffer)
{
    if (VXCircularQueue_isEmpty(this_queue))
        return false;
        
    if (!ensureSharedData(this_queue) || !VXCircularQueueDetachIfNeeded(this_queue))
        return false;

    size_t typeSize = XContainerTypeSize(this_queue);
    size_t capacity = getCapacity(this_queue);
    uint8_t* buffer = getBuffer(this_queue);
    
    void* pvTop = buffer + this_queue->m_head * typeSize;
    memcpy(pvBuffer, pvTop, typeSize);
    
    if (XContainerDataDeinitMethod(this_queue))
        XContainerDataDeinitMethod(this_queue)(pvTop);
    
    this_queue->m_head = (this_queue->m_head + 1) % capacity;
    return true;
}

static void VXClass_copy(XCircularQueue* object, const XCircularQueue* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        size_t cap = XContainerSize(src);
        XCircularQueue_init(object, XContainerTypeSize(src), cap > 0 ? cap - 1 : 0);
    }
    else if (XContainerSharedData(object))
    {
        XSharedData_release_with(XContainerSharedData(object), 
            (void (*)(void*, void*))VXCircularQueueDataDelete, object);
    }

    XContainerSetDataCopyMethod(object, XContainerDataCopyMethod(src));
    XContainerSetDataMoveMethod(object, XContainerDataMoveMethod(src));
    XContainerSetDataDeinitMethod(object, XContainerDataDeinitMethod(src));

    if (XContainerIsCow(object))
    {
        XContainerSharedData(object) = XContainerSharedData(src);
        if (XContainerSharedData(object))
        {
            XSharedData_addRef(XContainerSharedData(object));
        }
    }
    else
    {
        size_t typeSize = XContainerTypeSize(src);
        size_t capacity = XContainerSize(src);
        size_t count = VXCircularQueue_size(src);
        
        void* newData = XMalloc_System(typeSize * capacity);
        if (newData)
        {
            XContainerDataPtr(object) = newData;
            
            uint8_t* srcBuffer = getBuffer((XCircularQueue*)src);
            uint8_t* dstBuffer = (uint8_t*)newData;
            size_t src_index = src->m_head;
            
            for (size_t i = 0; i < count; i++)
            {
                memcpy(dstBuffer + i * typeSize,
                       srcBuffer + src_index * typeSize,
                       typeSize);
                src_index = (src_index + 1) % capacity;
            }
        }
    }

    XContainerSize(object) = XContainerSize(src);
    XContainerCapacity(object) = XContainerCapacity(src);
    XContainerTypeSize(object) = XContainerTypeSize(src);
    object->m_autoExpansion = src->m_autoExpansion;
    object->m_head = src->m_head;
    object->m_tail = src->m_tail;
}

static void VXClass_move(XCircularQueue* object, XCircularQueue* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XCircularQueue_init(object, XContainerTypeSize(src), XContainerCapacity(src) > 0 ? XContainerCapacity(src) - 1 : 0);
    }
    else if (XContainerSharedData(object))
    {
        XSharedData_release_with(XContainerSharedData(object), VXCircularQueueDataDelete, object);
    }

    memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XCircularQueue) - sizeof(XClass));

    XContainerSharedData(src) = NULL;
    XContainerCapacity(src) = 0;
    XContainerSize(src) = 0;
    src->m_head = 0;
    src->m_tail = 0;
    src->m_autoExpansion = false;
}

static void VXClass_deinit(XCircularQueue* this_queue)
{
    if (XContainerSharedData(this_queue))
    {
        XSharedData_release_with(XContainerSharedData(this_queue), VXCircularQueueDataDelete, this_queue);
    }
    else if (!XContainerIsCow(this_queue) && XContainerDataPtr(this_queue))
    {
        // 非 COW 模式，手动释放
        if (XContainerDataDeinitMethod(this_queue))
        {
            size_t typeSize = XContainerTypeSize(this_queue);
            size_t capacity = getCapacity(this_queue);
            uint8_t* buffer = getBuffer(this_queue);
            size_t current = this_queue->m_head;
            
            while (current != this_queue->m_tail)
            {
                XContainerDataDeinitMethod(this_queue)(buffer + current * typeSize);
                current = (current + 1) % capacity;
            }
        }
        XFree_System(XContainerDataPtr(this_queue));
    }
    
    this_queue->m_autoExpansion = false;
    this_queue->m_head = 0;
    this_queue->m_tail = 0;
    XContainerSize(this_queue) = 0;
    XContainerCapacity(this_queue) = 0;
    XContainerSharedData(this_queue) = NULL;
}

// =============== 公开 API ===============

void XCircularQueue_init(XCircularQueue* this_queue, size_t typeSize, size_t count)
{
    if (ISNULL(this_queue, "") || ISNULL(typeSize, "") || ISNULL(count, ""))
        return;
    
    // 初始化容器基类（不使用 COW，环形队列自己管理）
    XContainer_init(this_queue, typeSize, false);
    XClassGetVtable(this_queue) = XCircularQueue_class_init();
    
    // 分配缓冲区（容量 + 1，因为环形队列需要一个空位判断满）
    size_t capacity = count + 1;
    size_t bytes = typeSize * capacity;
    
    void* buffer = XMalloc_System(bytes);
    if (!buffer) return;
    
    XContainerDataPtr(this_queue) = buffer;
    XContainerSize(this_queue) = capacity;
    XContainerCapacity(this_queue) = capacity;
    
    this_queue->m_autoExpansion = false;
    this_queue->m_head = 0;
    this_queue->m_tail = 0;
}

XCircularQueue* XCircularQueue_create(size_t typeSize, size_t count)
{
    if (ISNULL(typeSize, "") || ISNULL(count, ""))
        return NULL;

    XCircularQueue* this_queue = XMalloc_System(sizeof(XCircularQueue));
    if (!this_queue) return NULL;

    XCircularQueue_init(this_queue, typeSize, count);
    Set_Class_MemoryFree(this_queue, XFree_System);
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
    if (!this_queue || !value)
        return 0;

    if (!ensureSharedData(this_queue) || !VXCircularQueueDetachIfNeeded(this_queue))
        return 0;

    XCompare compare = XContainerCompare(this_queue);
    if (!compare) return 0;

    if (VXCircularQueue_isEmpty(this_queue))
        return 0;

    uint8_t* buffer = getBuffer(this_queue);
    size_t typeSize = XContainerTypeSize(this_queue);
    size_t capacity = getCapacity(this_queue);
    size_t count = VXCircularQueue_size(this_queue);

    if (count == 0) return 0;

    size_t max_to_remove = (n == 0) ? count : n;
    size_t removed_count = 0;

    // 记录要删除的索引
    size_t* indices_to_remove = XMalloc_System(count * sizeof(size_t));
    if (!indices_to_remove) return 0;

    size_t found_count = 0;
    size_t current = this_queue->m_head;

    // 第一次遍历：找到所有要删除的元素索引
    for (size_t i = 0; i < count && found_count < max_to_remove; i++)
    {
        void* current_element = buffer + (current * typeSize);
        if (compare(current_element, value) == 0)
        {
            indices_to_remove[found_count] = current;
            found_count++;
        }
        current = (current + 1) % capacity;
    }

    if (found_count == 0)
    {
        XFree_System(indices_to_remove);
        return 0;
    }

    // 从后往前删除元素（按索引降序处理）
    for (size_t i = found_count; i > 0; i--)
    {
        size_t target_index = indices_to_remove[i - 1];
        void* target_element = buffer + (target_index * typeSize);

        // 释放元素
        if (XContainerDataDeinitMethod(this_queue))
        {
            XContainerDataDeinitMethod(this_queue)(target_element);
        }

        // 重新获取当前状态
        count = VXCircularQueue_size(this_queue);
        capacity = getCapacity(this_queue);

        if (count == 1)
        {
            this_queue->m_head = 0;
            this_queue->m_tail = 0;
        }
        else if (target_index == this_queue->m_head)
        {
            // 删除队头
            this_queue->m_head = (this_queue->m_head + 1) % capacity;
        }
        else if ((this_queue->m_tail == 0 ? capacity : this_queue->m_tail) - 1 == target_index)
        {
            // 删除队尾
            this_queue->m_tail = target_index;
        }
        else
        {
            // 删除中间元素，需要移动数据
            bool is_wrapped = this_queue->m_tail < this_queue->m_head;

            if (!is_wrapped)
            {
                // 数据连续 [head, tail)
                size_t elements_after = this_queue->m_tail - target_index - 1;
                if (elements_after > 0)
                {
                    memmove(buffer + (target_index * typeSize),
                        buffer + ((target_index + 1) * typeSize),
                        elements_after * typeSize);
                }
                this_queue->m_tail--;
            }
            else
            {
                // 数据环绕 [head, capacity) + [0, tail)
                if (target_index >= this_queue->m_head)
                {
                    // 目标在前半段
                    size_t elements_after_in_first = capacity - target_index - 1;
                    if (elements_after_in_first > 0)
                    {
                        memmove(buffer + (target_index * typeSize),
                            buffer + ((target_index + 1) * typeSize),
                            elements_after_in_first * typeSize);
                    }

                    if (this_queue->m_tail > 0)
                    {
                        memcpy(buffer + ((capacity - 1) * typeSize), buffer, typeSize);
                        if (this_queue->m_tail > 1)
                        {
                            memmove(buffer, buffer + typeSize, (this_queue->m_tail - 1) * typeSize);
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
                    // 目标在后半段
                    size_t elements_after_in_second = this_queue->m_tail - target_index - 1;
                    if (elements_after_in_second > 0)
                    {
                        memmove(buffer + (target_index * typeSize),
                            buffer + ((target_index + 1) * typeSize),
                            elements_after_in_second * typeSize);
                    }
                    this_queue->m_tail--;
                }
            }
        }

        removed_count++;
        buffer = getBuffer(this_queue);
        capacity = getCapacity(this_queue);
    }

    XFree_System(indices_to_remove);
    return removed_count;
}

#endif