#include "XRingBuffer.h"
#if XRingBuffer_ON
#include "XVector.h"
#include "XMemory.h"
#include "XRingChunk.h"
#include <string.h>

// 声明需要重写的父类虚函数
static void VXClass_copy(XRingBuffer* object, const XRingBuffer* src);
static void VXClass_move(XRingBuffer* object, XRingBuffer* src);
static void VXRingBuffer_clear(XRingBuffer* buffer);
static void VXRingBuffer_deinit(XRingBuffer* buffer);
XVtable* XRingBuffer_class_init()
{
    XVTABLE_CREAT_DEFAULT
        // 虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XRingBuffer))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承类
        XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());

    // --- 关键修改: 不再添加 XRingBuffer 自己的 Write/Read 等虚函数 ---

    // 重写父类的虚函数
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXRingBuffer_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXRingBuffer_deinit);

#if SHOWCONTAINERSIZE
    printf("XRingBuffer size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif

    return XVTABLE_DEFAULT;
}
// --- 新增: XVector中存储的XRingChunk*的析构函数 ---
static void VXRingBuffer_chunkDeleter(void* data)
{
    if (data == NULL)
        return;
    XRingChunk** chunkPtr = (XRingChunk**)data;
    if (*chunkPtr != NULL)
    {
        XRingChunk_delete_base(*chunkPtr); // 调用基类删除宏，确保正确析构
        *chunkPtr = NULL; // 防止悬空指针（虽非必需，但良好习惯）
    }
}
void XRingBuffer_init(XRingBuffer* buffer, size_t chunkSize)
{
    if (ISNULL(buffer, "") || ISNULL(chunkSize, ""))
        return;

    XContainerObject_init(buffer, sizeof(uint8_t));
    XClassGetVtable(buffer) = XRingBuffer_class_init();

    // 创建chunks向量
    buffer->m_chunks = XVector_Create(XRingChunk*);
    if (buffer->m_chunks == NULL)
    {
        XContainerCapacity(buffer) = 0;
        XContainerSize(buffer) = 0;
        buffer->m_currentReadChunk = 0;
        buffer->m_currentWriteChunk = 0;
        return;
    }
    XContainerSetDataDeinitMethod(buffer->m_chunks, VXRingBuffer_chunkDeleter);
    // 添加第一个chunk
    XRingChunk* firstChunk = XRingChunk_create(chunkSize);
    if (firstChunk == NULL)
    {
        XVector_delete_base(buffer->m_chunks); // 现在delete会自动清理（虽然此时为空）
        buffer->m_chunks = NULL;
        XContainerCapacity(buffer) = 0;
        XContainerSize(buffer) = 0;
        buffer->m_currentReadChunk = 0;
        buffer->m_currentWriteChunk = 0;
        // --- 初始化标记成员 ---
        buffer->m_markedReadChunk = 0;
        buffer->m_markedReadChunkIndex = 0;
        buffer->m_markedReadPosInChunk = 0;
        buffer->m_markedTotalSize = 0;
        buffer->m_hasMark = false;
        // -------------------------
        return;
    }

    XVector_push_back_base(buffer->m_chunks, &firstChunk);

    // --- 关键修正: 容量语义 ---
   // XRingBuffer是动态扩容的，没有固定总容量，故设为0。
    XContainerCapacity(buffer) = 0;
    XContainerSize(buffer) = 0;
    buffer->m_currentReadChunk = 0;
    buffer->m_currentWriteChunk = 0;
    // --- 初始化标记成员 ---
    buffer->m_markedReadChunk = 0;
    buffer->m_markedReadChunkIndex = 0;
    buffer->m_markedReadPosInChunk = 0;
    buffer->m_markedTotalSize = 0;
    buffer->m_hasMark = false;
}

XRingBuffer* XRingBuffer_create(size_t chunkSize)
{
    if (ISNULL(chunkSize, ""))
        return NULL;

    XRingBuffer* buffer = XMemory_malloc(sizeof(XRingBuffer));
    if (buffer == NULL)
        return NULL;

    XRingBuffer_init(buffer, chunkSize);
    Set_Class_MemoryFree(buffer, XFree);
    return buffer;
}

// --- 以下是直接函数调用，不再通过虚函数表 ---

size_t XRingBuffer_write(XRingBuffer* buffer, const void* data, size_t size)
{
    if (ISNULL(buffer, "") || ISNULL(data, "") || ISNULL(size, ""))
        return 0;

    if (size == 0)
        return 0;

    const uint8_t* src = (const uint8_t*)data;
    size_t remaining = size;
    size_t written = 0;

    while (remaining > 0 && written < size)
    {
        // 确保有可用的写入chunk
        if (buffer->m_currentWriteChunk >= XVector_size_base(buffer->m_chunks))
        {
            // 尝试添加新chunk
            XRingChunk** firstChunkPtr = (XRingChunk**)XVector_at_base(buffer->m_chunks, 0);
            size_t newChunkSize = (firstChunkPtr && *firstChunkPtr) ? XRingChunk_capacity_base(*firstChunkPtr) : 1024;
            if (!XRingBuffer_addChunk(buffer, newChunkSize))
                break;
        }

        XRingChunk** chunkPtr = (XRingChunk**)XVector_at_base(buffer->m_chunks, buffer->m_currentWriteChunk);
        if (chunkPtr == NULL || *chunkPtr == NULL)
            break;

        XRingChunk* chunk = *chunkPtr;

        // === 关键修正点 ===
        // 1. 获取chunk的总容量（逻辑容量）
        size_t chunkCapacity = XRingChunk_capacity_base(chunk);
        // 2. 获取chunk中已用空间
        size_t chunkUsed = XRingChunk_available(chunk);
        // 3. 计算真正的剩余可写空间
        size_t chunkFreeSpace = (chunkUsed <= chunkCapacity) ? (chunkCapacity - chunkUsed) : 0;
        // ===================

        if (chunkFreeSpace == 0)
        {
            // 当前chunk确实已满，移动到下一个
            buffer->m_currentWriteChunk++;
            continue;
        }

        // 写入尽可能多的数据，但不超过剩余数据和chunk的空闲空间
        size_t toWrite = (remaining < chunkFreeSpace) ? remaining : chunkFreeSpace;
        size_t actuallyWritten = XRingChunk_write(chunk, src + written, toWrite);

        // 即使XRingChunk_write理论上不应该失败（因为我们已经检查了空间），
        // 但为了健壮性，还是检查一下。
        if (actuallyWritten == 0) {
            break; // 写入失败，跳出循环
        }

        written += actuallyWritten;
        remaining -= actuallyWritten;
        XContainerSize(buffer) += actuallyWritten;

        // 如果这次写入填满了chunk，可以在这里选择立即递增m_currentWriteChunk，
        // 或者等到下次写入时再判断。两种方式都可以，这里选择后者（更简单）。
    }

     
    return written;
}

size_t XRingBuffer_read(XRingBuffer* buffer, void* buffer_out, size_t size)
{
    if (ISNULL(buffer, "") || ISNULL(buffer_out, "") || ISNULL(size, ""))
        return 0;

    if (size == 0)
        return 0;

    if (XContainerSize(buffer) == 0)
        return 0;

    uint8_t* dest = (uint8_t*)buffer_out;
    size_t remaining = size;
    size_t read = 0;

    while (remaining > 0 && read < size && XContainerSize(buffer) > 0)
    {
        if (buffer->m_currentReadChunk >= XVector_size_base(buffer->m_chunks))
            break;

        XRingChunk** chunkPtr = (XRingChunk**)XVector_at_base(buffer->m_chunks, buffer->m_currentReadChunk);
        if (chunkPtr == NULL || *chunkPtr == NULL)
            break;

        XRingChunk* chunk = *chunkPtr;
        size_t chunkAvailable = XRingChunk_available(chunk);
        size_t toRead = (remaining < chunkAvailable) ? remaining : chunkAvailable;

        if (toRead == 0)
        {
            // 当前chunk已空，移动到下一个
            buffer->m_currentReadChunk++;
            continue;
        }

        size_t actuallyRead = XRingChunk_read(chunk, dest + read, toRead);
        read += actuallyRead;
        remaining -= actuallyRead;
        XContainerSize(buffer) -= actuallyRead;

        if (actuallyRead < toRead)
            break; // 读取失败
    }

     
    return read;
}

size_t XRingBuffer_peek(XRingBuffer* buffer, void* buffer_out, size_t size)
{
    if (ISNULL(buffer, "") || ISNULL(buffer_out, "") || ISNULL(size, ""))
        return 0;

    if (size == 0)
        return 0;

    if (XContainerSize(buffer) == 0)
        return 0;

    uint8_t* dest = (uint8_t*)buffer_out;
    size_t remaining = size;
    size_t peeked = 0;
    size_t tempReadChunk = buffer->m_currentReadChunk;

    while (remaining > 0 && peeked < size)
    {
        if (tempReadChunk >= XVector_size_base(buffer->m_chunks))
            break;

        XRingChunk** chunkPtr = (XRingChunk**)XVector_at_base(buffer->m_chunks, tempReadChunk);
        if (chunkPtr == NULL || *chunkPtr == NULL)
            break;

        XRingChunk* chunk = *chunkPtr;
        size_t chunkAvailable = XRingChunk_available(chunk);
        size_t toPeek = (remaining < chunkAvailable) ? remaining : chunkAvailable;

        if (toPeek == 0)
        {
            tempReadChunk++;
            continue;
        }

        size_t actuallyPeeked = XRingChunk_peek(chunk, dest + peeked, toPeek);
        peeked += actuallyPeeked;
        remaining -= actuallyPeeked;

        if (actuallyPeeked < toPeek)
            break;
    }

    return peeked;
}

void XRingBuffer_skip(XRingBuffer* buffer, size_t size)
{
    if (ISNULL(buffer, "") || ISNULL(size, ""))
        return;

    if (size == 0)
        return;

    if (size >= XContainerSize(buffer))
    {
        XRingBuffer_reset(buffer);
        return;
    }

    size_t remaining = size;

    while (remaining > 0 && XContainerSize(buffer) > 0)
    {
        if (buffer->m_currentReadChunk >= XVector_size_base(buffer->m_chunks))
            break;

        XRingChunk** chunkPtr = (XRingChunk**)XVector_at_base(buffer->m_chunks, buffer->m_currentReadChunk);
        if (chunkPtr == NULL || *chunkPtr == NULL)
            break;

        XRingChunk* chunk = *chunkPtr;
        size_t chunkAvailable = XRingChunk_available(chunk);
        size_t toSkip = (remaining < chunkAvailable) ? remaining : chunkAvailable;

        if (toSkip == 0)
        {
            buffer->m_currentReadChunk++;
            continue;
        }

        XRingChunk_skip(chunk, toSkip);
        remaining -= toSkip;
        XContainerSize(buffer) -= toSkip;
    }

     
}

void XRingBuffer_reset(XRingBuffer* buffer)
{
    if (ISNULL(buffer, ""))
        return;

    // 重置所有chunks的状态（而非删除它们）
    for_each_iterator(buffer->m_chunks, XVector, it)
    {
        XRingChunk** chunkPtr = (XRingChunk**)XVector_iterator_data(&it);
        if (chunkPtr && *chunkPtr)
        {
            XRingChunk_reset(*chunkPtr);
        }
    }

    XContainerSize(buffer) = 0;
    buffer->m_currentReadChunk = 0;
    buffer->m_currentWriteChunk = 0;
    XContainerSize(buffer) = 0;
}

size_t XRingBuffer_available(const XRingBuffer* buffer)
{
    if (ISNULL(buffer, ""))
        return 0;

    return XContainerSize(buffer);
}

bool XRingBuffer_addChunk(XRingBuffer* buffer, size_t chunkSize)
{
    if (ISNULL(buffer, "") || ISNULL(chunkSize, ""))
        return false;

    XRingChunk* newChunk = XRingChunk_create(chunkSize);
    if (newChunk == NULL)
        return false;

    if (!XVector_push_back_base(buffer->m_chunks, &newChunk))
    {
        XRingChunk_delete_base(newChunk);
        return false;
    }

    return true;
}

void XRingBuffer_mark(XRingBuffer* buffer)
{
    if (buffer == NULL) return;

    // 1. 保存当前读取 chunk 的索引
    buffer->m_markedReadChunkIndex = buffer->m_currentReadChunk;

    // 2. 保存当前读取 chunk 内的读取位置
    if (buffer->m_currentReadChunk < XVector_size_base(buffer->m_chunks)) {
        XRingChunk** chunkPtr = (XRingChunk**)XVector_at_base(buffer->m_chunks, buffer->m_currentReadChunk);
        if (chunkPtr && *chunkPtr) {
            buffer->m_markedReadPosInChunk = (*chunkPtr)->m_readPos;
        }
        else {
            // 如果指针无效，标记为无效
            buffer->m_hasMark = false;
            return;
        }
    }

    // 3. 保存当前总大小
    buffer->m_markedTotalSize = XContainerSize(buffer);

    // 4. 标记为有效
    buffer->m_hasMark = true;
}

void XRingBuffer_resetToMark(XRingBuffer* buffer)
{
    if (buffer == NULL || !buffer->m_hasMark) return;

    // === 1. 恢复总大小 ===
    XContainerSize(buffer) = buffer->m_markedTotalSize;
     

    // === 2. 恢复读取 chunk 索引 ===
    buffer->m_currentReadChunk = buffer->m_markedReadChunkIndex;

    // === 3. 恢复被标记 chunk 的读取位置 ===
    if (buffer->m_markedReadChunkIndex < XVector_size_base(buffer->m_chunks)) {
        XRingChunk** markedChunkPtr = (XRingChunk**)XVector_at_base(buffer->m_chunks, buffer->m_markedReadChunkIndex);
        if (markedChunkPtr && *markedChunkPtr) {
            (*markedChunkPtr)->m_readPos = buffer->m_markedReadPosInChunk;
            // 修正被标记chunk的大小
            XRingChunk* markedChunk = *markedChunkPtr;
            size_t physicalCap = XContainerCapacity(markedChunk) + 1;
            markedChunk->m_class.m_size = (markedChunk->m_writePos >= markedChunk->m_readPos) ?
                (markedChunk->m_writePos - markedChunk->m_readPos) :
                (physicalCap - markedChunk->m_readPos + markedChunk->m_writePos);
        }
    }

    // === 4. 关键修正: 仅重置后续 chunk 的读取位置，保留其数据 ===
    for (size_t i = buffer->m_markedReadChunkIndex + 1; i < XVector_size_base(buffer->m_chunks); ++i) {
        XRingChunk** chunkPtr = (XRingChunk**)XVector_at_base(buffer->m_chunks, i);
        if (chunkPtr && *chunkPtr) {
            // 使用新函数，只重置读取状态
            XRingChunk_resetReadPosOnly(*chunkPtr);
        }
    }
    // ========================================
}

size_t XRingBuffer_writeable(const XRingBuffer* buffer)
{
    if (ISNULL(buffer, "")) return 0;
    if (buffer->m_currentWriteChunk >= XVector_size_base(buffer->m_chunks)) return 0;

    XRingChunk** chunkPtr = (XRingChunk**)XVector_at_base(buffer->m_chunks, buffer->m_currentWriteChunk);
    if (!chunkPtr || !*chunkPtr) return 0;

    // XRingChunk_capacity_base 返回逻辑容量，available 返回已用空间
    size_t capacity = XRingChunk_capacity_base(*chunkPtr);
    size_t used = XRingChunk_available(*chunkPtr);
    return (used <= capacity) ? (capacity - used) : 0;
}

const void* XRingBuffer_peekReadPtr(XRingBuffer* buffer, size_t* size)
{
    if (ISNULL(buffer, "") || ISNULL(size, "")) return NULL;
    if (XContainerSize(buffer) == 0 || *size == 0) {
        *size = 0;
        return NULL;
    }

    if (buffer->m_currentReadChunk >= XVector_size_base(buffer->m_chunks)) {
        *size = 0;
        return NULL;
    }

    XRingChunk** chunkPtr = (XRingChunk**)XVector_at_base(buffer->m_chunks, buffer->m_currentReadChunk);
    if (!chunkPtr || !*chunkPtr) {
        *size = 0;
        return NULL;
    }

    XRingChunk* chunk = *chunkPtr;
    size_t availableInChunk = XRingChunk_available(chunk);
    if (availableInChunk == 0) {
        *size = 0;
        return NULL;
    }

    // 计算物理容量以确定连续区域
    size_t physicalCap = XContainerCapacity(chunk) + 1; // getPhysicalCapacity
    size_t firstPart = physicalCap - chunk->m_readPos;
    size_t contiguousSize = (firstPart <= availableInChunk) ? firstPart : availableInChunk;

    // 不要超过请求的大小
    if (contiguousSize > *size) {
        contiguousSize = *size;
    }

    *size = contiguousSize;
    return (uint8_t*)XContainerDataPtr(chunk) + chunk->m_readPos;
}

// --- 以下是重写的父类虚函数 ---

static void VXRingBuffer_clear(XRingBuffer* buffer)
{
    XRingBuffer_reset(buffer);
}

void VXRingBuffer_deinit(XRingBuffer* buffer)
{
    if (ISNULL(buffer, ""))
        return;

    // 只需删除m_chunks向量，其析构方法会自动处理所有XRingChunk的释放
    if (buffer->m_chunks != NULL)
    {
        XVector_delete_base(buffer->m_chunks);
        buffer->m_chunks = NULL;
    }

    // 重置成员变量
    XContainerSize(buffer) = 0;
    buffer->m_currentReadChunk = 0;
    buffer->m_currentWriteChunk = 0;
}

static void VXClass_copy(XRingBuffer* object, const XRingBuffer* src)
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        // 如果目标对象未初始化，先用源对象的第一个chunk大小来初始化它
        XRingChunk** firstChunkPtr = (XRingChunk**)XVector_at_base(src->m_chunks, 0);
        size_t initSize = (firstChunkPtr && *firstChunkPtr) ? XRingChunk_capacity_base(*firstChunkPtr) : 1024;
        XRingBuffer_init(object, initSize);
    }
    else if (!XRingBuffer_isEmpty_base(object))
    {
        XRingBuffer_clear_base(object);
    }

    // 复制chunks
    XVector_clear_base(object->m_chunks);
    for_each_iterator(src->m_chunks, XVector, it)
    {
        XRingChunk** srcChunkPtr = (XRingChunk**)XVector_iterator_data(&it);
        if (srcChunkPtr && *srcChunkPtr)
        {
            XRingChunk* newChunk = XRingChunk_create_copy(*srcChunkPtr);
            if (newChunk)
            {
                XVector_push_back_base(object->m_chunks, &newChunk);
            }
        }
    }

    // 复制状态
    object->m_currentReadChunk = src->m_currentReadChunk;
    object->m_currentWriteChunk = src->m_currentWriteChunk;
    XContainerSize(object) = XContainerSize(src);
    // XContainerCapacity 保持为0
}

static void VXClass_move(XRingBuffer* object, XRingBuffer* src)
{
    if (object->m_chunks)
        XVector_delete_base(object->m_chunks);

    memcpy((XClass*)object + 1, (XClass*)src + 1, sizeof(XRingBuffer) - sizeof(XClass));

    // 重置源对象
    src->m_chunks = NULL;
    src->m_currentReadChunk = 0;
    src->m_currentWriteChunk = 0;
    XContainerCapacity(src) = 0;
    XContainerSize(src) = 0;
}

#endif // XRingBuffer_ON