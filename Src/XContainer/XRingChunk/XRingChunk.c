#include "XRingChunk.h"
#if XRingChunk_ON
#include "XMemory.h"
#include <string.h>

static void VXClass_copy(XRingChunk* object, const XRingChunk* src);
static void VXClass_move(XRingChunk* object, XRingChunk* src);
static void VXRingChunk_clear(XRingChunk* chunk);

// 辅助内联函数：获取物理容量 (逻辑容量 + 1)
static  size_t getPhysicalCapacity(const XRingChunk* chunk) {
    return XContainerCapacity(chunk) + 1;
}

// 辅助内联函数：检查是否为空
static  bool isBufferEmpty(const XRingChunk* chunk) {
    return chunk->m_readPos == chunk->m_writePos;
}

// 辅助内联函数：检查是否为满
static  bool isBufferFull(const XRingChunk* chunk) {
    return (chunk->m_writePos + 1) % getPhysicalCapacity(chunk) == chunk->m_readPos;
}

XVtable* XRingChunk_class_init()
{
    XVTABLE_CREAT_DEFAULT
        // 虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XRingChunk))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承类
        XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());

    //void* table[] = {
    //    VXRingChunk_write,
    //    VXRingChunk_read,
    //    VXRingChunk_peek,
    //    VXRingChunk_skip,
    //    VXRingChunk_reset,
    //    VXRingChunk_available
    //};

    // 追加虚函数
    //XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

    // 重写的函数
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXRingChunk_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);

#if SHOWCONTAINERSIZE
    printf("XRingChunk size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif

    return XVTABLE_DEFAULT;
}

void XRingChunk_init(XRingChunk* chunk, size_t logicalCapacity)
{
    if (ISNULL(chunk, "") || ISNULL(logicalCapacity, ""))
        return;

    XContainerObject_init(chunk, sizeof(uint8_t));
    XClassGetVtable(chunk) = XRingChunk_class_init();

    // --- 核心修正点 ---
    // 1. 对外报告的容量是用户请求的逻辑容量
    XContainerCapacity(chunk) = logicalCapacity;

    // 2. 分配物理容量 = 逻辑容量 + 1
    size_t physicalCapacity = getPhysicalCapacity(chunk);
    XContainerDataPtr(chunk) = XMemory_malloc(physicalCapacity);
    if (XContainerDataPtr(chunk) == NULL)
    {
        XContainerCapacity(chunk) = 0;
        XContainerSize(chunk) = 0;
        return;
    }
    // --- 修正点结束 ---

    XContainerSize(chunk) = 0;
    chunk->m_readPos = 0;
    chunk->m_writePos = 0;
    chunk->m_markPos = 0;
}

XRingChunk* XRingChunk_create(size_t capacity)
{
    if (ISNULL(capacity, ""))
        return NULL;

    XRingChunk* chunk = XNew(XRingChunk);
    if (chunk == NULL)
        return NULL;

    XRingChunk_init(chunk, capacity);
    Set_Class_MemoryFree(chunk,XFree);
    return chunk;
}

XRingChunk* XRingChunk_create_copy(XRingChunk* src)
{
    if (!src)return NULL;
    XRingChunk* chunk = XRingChunk_create(XRingChunk_capacity_base(src));
    XRingChunk_copy_base(chunk, src);
    return chunk;
}

size_t XRingChunk_write(XRingChunk* chunk, const void* data, size_t size)
{
    if (ISNULL(chunk, "") || ISNULL(data, "") || ISNULL(size, ""))
        return 0;

    if (size == 0)
        return 0;

    if (isBufferFull(chunk))
        return 0; // 缓冲区已满

    size_t availableSpace = XContainerCapacity(chunk) - XContainerSize(chunk);
    if (size > availableSpace)
        size = availableSpace;

    if (size == 0)
        return 0;

    // 计算写入区域，使用物理容量进行取模
    size_t physicalCap = getPhysicalCapacity(chunk);
    size_t firstPart = physicalCap - chunk->m_writePos;
    if (firstPart > size)
    {
        memcpy((uint8_t*)XContainerDataPtr(chunk) + chunk->m_writePos, data, size);
        chunk->m_writePos = (chunk->m_writePos + size) % physicalCap;
    }
    else
    {
        memcpy((uint8_t*)XContainerDataPtr(chunk) + chunk->m_writePos, data, firstPart);
        memcpy(XContainerDataPtr(chunk), (const uint8_t*)data + firstPart, size - firstPart);
        chunk->m_writePos = (size - firstPart) % physicalCap;
    }

    XContainerSize(chunk) += size;
    return size;
}

size_t XRingChunk_read(XRingChunk* chunk, void* buffer, size_t size)
{
    if (ISNULL(chunk, "") || ISNULL(buffer, "") || ISNULL(size, ""))
        return 0;

    if (size == 0)
        return 0;

    if (isBufferEmpty(chunk))
        return 0;

    size_t availableData = XContainerSize(chunk);
    if (size > availableData)
        size = availableData;

    if (size == 0)
        return 0;

    // 计算读取区域，使用物理容量进行取模
    size_t physicalCap = getPhysicalCapacity(chunk);
    size_t firstPart = physicalCap - chunk->m_readPos;
    if (firstPart > size)
    {
        memcpy(buffer, (uint8_t*)XContainerDataPtr(chunk) + chunk->m_readPos, size);
        chunk->m_readPos = (chunk->m_readPos + size) % physicalCap;
    }
    else
    {
        memcpy(buffer, (uint8_t*)XContainerDataPtr(chunk) + chunk->m_readPos, firstPart);
        memcpy((uint8_t*)buffer + firstPart, XContainerDataPtr(chunk), size - firstPart);
        chunk->m_readPos = (size - firstPart) % physicalCap;
    }

    XContainerSize(chunk) -= size;
    return size;
}

size_t XRingChunk_peek(XRingChunk* chunk, void* buffer, size_t size)
{
    if (ISNULL(chunk, "") || ISNULL(buffer, "") || ISNULL(size, ""))
        return 0;

    if (size == 0)
        return 0;

    if (isBufferEmpty(chunk))
        return 0;

    size_t availableData = XContainerSize(chunk);
    if (size > availableData)
        size = availableData;

    if (size == 0)
        return 0;

    // 计算读取区域（不移动读取指针），使用物理容量
    size_t physicalCap = getPhysicalCapacity(chunk);
    size_t firstPart = physicalCap - chunk->m_readPos;
    if (firstPart > size)
    {
        memcpy(buffer, (uint8_t*)XContainerDataPtr(chunk) + chunk->m_readPos, size);
    }
    else
    {
        memcpy(buffer, (uint8_t*)XContainerDataPtr(chunk) + chunk->m_readPos, firstPart);
        memcpy((uint8_t*)buffer + firstPart, XContainerDataPtr(chunk), size - firstPart);
    }

    return size;
}

void XRingChunk_skip(XRingChunk* chunk, size_t size)
{
    if (ISNULL(chunk, "") || ISNULL(size, ""))
        return;

    if (isBufferEmpty(chunk))
        return;

    size_t availableData = XContainerSize(chunk);
    if (size > availableData)
        size = availableData;

    if (size == 0)
        return;

    // 使用物理容量进行取模
    size_t physicalCap = getPhysicalCapacity(chunk);
    chunk->m_readPos = (chunk->m_readPos + size) % physicalCap;
    XContainerSize(chunk) -= size;
}

void XRingChunk_reset(XRingChunk* chunk)
{
    if (ISNULL(chunk, ""))
        return;

    chunk->m_readPos = 0;
    chunk->m_writePos = 0;
    XContainerSize(chunk) = 0;
}

size_t XRingChunk_available(const XRingChunk* chunk)
{
    if (ISNULL(chunk, ""))
        return 0;

    return XContainerSize(chunk);
}

void XRingChunk_mark(XRingChunk* chunk)
{
    if (ISNULL(chunk, "")) return;
    chunk->m_markPos = chunk->m_readPos;
}

void XRingChunk_resetToMark(XRingChunk* chunk)
{
    if (ISNULL(chunk, "")) return;
    if (chunk->m_markPos == chunk->m_readPos) return; // 无变化

    // 计算回滚的数据量
    size_t physicalCap = getPhysicalCapacity(chunk);
    size_t currentRead = chunk->m_readPos;
    size_t markedRead = chunk->m_markPos;

    // 由于是环形缓冲区，计算回滚字节数需要小心处理环绕
    size_t bytesToRollback;
    if (currentRead >= markedRead) {
        bytesToRollback = currentRead - markedRead;
    }
    else {
        bytesToRollback = (physicalCap - markedRead) + currentRead;
    }

    // 更新读指针和总大小
    chunk->m_readPos = chunk->m_markPos;
    XContainerSize(chunk) += bytesToRollback;
}

size_t XRingChunk_unget(XRingChunk* chunk, const void* data, size_t size)
{
    if (ISNULL(chunk, "") || ISNULL(data, "") || size == 0)
        return 0;

    size_t physicalCap = getPhysicalCapacity(chunk); // 逻辑容量 + 1
    size_t logicalCap = XContainerCapacity(chunk);
    size_t currentSize = XContainerSize(chunk);

    // 1. 检查是否有足够的空间放回数据
    if (currentSize + size > logicalCap) {
        return 0;
    }

    // 2. 检查是否可以回退（简化检查）
    if (chunk->m_readPos < size) {
        return 0;
    }

    // === 关键修复：计算正确的物理写入起始位置 ===
    size_t newLogicalReadPos = chunk->m_readPos - size;
    size_t physicalWritePos = newLogicalReadPos % physicalCap;
    // ===========================================

    uint8_t* buffer = (uint8_t*)XContainerDataPtr(chunk);

    // 3. 处理可能的环绕写入
    if (physicalWritePos + size <= physicalCap) {
        // 情况A: 写入区域是连续的
        memcpy(buffer + physicalWritePos, data, size);
    }
    else {
        // 情况B: 写入区域跨越了缓冲区末尾
        size_t firstPartLen = physicalCap - physicalWritePos;
        size_t secondPartLen = size - firstPartLen;
        memcpy(buffer + physicalWritePos, data, firstPartLen);
        memcpy(buffer, (const uint8_t*)data + firstPartLen, secondPartLen);
    }

    // 4. 更新状态
    chunk->m_readPos = newLogicalReadPos; // 保存逻辑位置
    XContainerSize(chunk) += size;
    return size;
}

void XRingChunk_resetReadPosOnly(XRingChunk* chunk)
{
    if (ISNULL(chunk, ""))
        return;

    // 只重置读取位置，保留写入位置和数据
    chunk->m_readPos = 0;
    // 总大小应等于已写入的数据量
    XContainerSize(chunk) = (chunk->m_writePos >= chunk->m_readPos) ?
        chunk->m_writePos :
        (getPhysicalCapacity(chunk) - chunk->m_readPos + chunk->m_writePos);
}

static void VXRingChunk_clear(XRingChunk* chunk)
{
    XRingChunk_reset(chunk);
}

static void VXClass_copy(XRingChunk* object, const XRingChunk* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XRingChunk_init(object, XContainerCapacity(src)); // 使用逻辑容量
    }
    else if (!XRingChunk_isEmpty_base(object))
    {
        XRingChunk_clear_base(object);
    }

    size_t srcPhysicalCap = getPhysicalCapacity(src);
    if (getPhysicalCapacity(object) < srcPhysicalCap)
    {
        // 需要重新分配内存
        if (XContainerDataPtr(object))
            XMemory_free(XContainerDataPtr(object));

        XContainerDataPtr(object) = XMemory_malloc(srcPhysicalCap);
        if (XContainerDataPtr(object) == NULL)
        {
            XContainerCapacity(object) = 0;
            XContainerSize(object) = 0;
            return;
        }
        // 对外容量保持不变
        XContainerCapacity(object) = XContainerCapacity(src);
    }

    // 复制整个物理内存块
    memcpy(XContainerDataPtr(object), XContainerDataPtr(src), srcPhysicalCap);

    // 复制状态
    XContainerSize(object) = XContainerSize(src);
    object->m_readPos = src->m_readPos;
    object->m_writePos = src->m_writePos;
}

static void VXClass_move(XRingChunk* object, XRingChunk* src)
{
    if (XContainerDataPtr(object))
        XMemory_free(XContainerDataPtr(object));

    memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XRingChunk) - sizeof(XClass));

    // 重置源对象
    XContainerDataPtr(src) = NULL;
    XContainerCapacity(src) = 0;
    XContainerSize(src) = 0;
    src->m_readPos = 0;
    src->m_writePos = 0;
}

#endif // XRingChunk_ON