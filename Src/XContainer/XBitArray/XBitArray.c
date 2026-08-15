#include "XBitArray.h"
#if XBitArray_ON
#include <stdlib.h>
#include <string.h>
#include "XVtable.h"

// 计算存储指定比特数所需的字节数（向上取整）
#define BYTE_COUNT(bitCount) ((bitCount + 7) / 8)

// 辅助函数：根据 COW 标志获取数据指针
static inline void* XBitArray_data(XBitArray* arr) {
    return XContainerIsCow(arr) ? XContainerSharedDataPtr(arr) : XContainerDataPtr(arr);
}
static inline const void* XBitArray_data_const(const XBitArray* arr) {
    return XContainerIsCow((XBitArray*)arr) ? XContainerSharedDataPtr((XBitArray*)arr) : XContainerDataPtr((XBitArray*)arr);
}

// COW分离：如果数据被共享，创建独立副本
static bool VXBitArrayDetachIfNeeded(XBitArray* array);
static void VXBitArrayDataDelete(void* data, XBitArray* array);

// 虚函数实现
static void VXBitArray_copy(XBitArray* dest, const XBitArray* src);
static void VXBitArray_move(XBitArray* dest, XBitArray* src);
static void VXBitArray_deinit(XBitArray* array);
static bool VXBitArray_clear(XBitArray* array);
static void XBitArray_init_with_memory(XBitArray* array, size_t initialBitCount,
    bool useCow, XMemoryType memory);

XVtable* XBitArray_class_init() {
    XVTABLE_INIT_DEFAULT(XBitArray)

        XVTABLE_INHERIT_XCLASS(XContainer);

    // 重载基础函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXBitArray_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXBitArray_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBitArray_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXBitArray_clear);

    return XVTABLE_DEFAULT;
}

// COW分离核心函数
static bool VXBitArrayDetachIfNeeded(XBitArray* array)
{
    // 非 COW 模式永远不需要分离
    if (!XContainerIsCow(array)) return true;

    XSharedData* sd = (XSharedData*)XContainerDataPtr(array);
    if (!sd || !XSharedData_isShared(sd))
        return true;

    size_t byteCount = BYTE_COUNT(XBitArray_count(array));
    if (byteCount == 0) byteCount = 1;

    XSharedData* newShared = XSharedData_create_ex(NULL, byteCount, XContainer_memory(array));
    if (!newShared) return false;

    // 拷贝数据
    const void* oldData = XContainerSharedDataPtr(array);
    if (oldData && byteCount > 0)
        memcpy(newShared->data, oldData, byteCount);

    XSharedData_release(sd, XContainer_memory(array));
    XContainerSetDataPtr(array, newShared);
    return true;
}

// 删除BitArray数据（XSharedData释放回调）
static void VXBitArrayDataDelete(void* data, XBitArray* array)
{
    if (array == NULL) return;
    // 注意：data 不需要单独释放，XSharedData 的柔性数组会一起释放
    XContainerSize(array) = 0;
    XContainerCapacity(array) = 0;
    XContainerSetDataPtr(array, NULL);
}

// 拷贝
void VXBitArray_copy(XBitArray* dest, const XBitArray* src)
{
    if (!dest || !src) return;
    bool target_uninitialized = XClassIsVtableNull(dest);
    if (target_uninitialized) {
        XBitArray_init(dest, 0, false);
        Class_Memory(dest) = Class_Memory(src);
    }
    else if (XContainerIsCow(src) &&
        XContainer_memory(dest) != XContainer_memory(src)) {
        return;
    }

    // 释放目标原有数据
    if (XContainerIsCow(dest)) {
        if ((XSharedData*)XContainerDataPtr(dest))
            XSharedData_release_with((XSharedData*)XContainerDataPtr(dest),
                VXBitArrayDataDelete, dest, XContainer_memory(dest));
    }
    else {
        if (XContainerDataPtr(dest))
            XContainer_free(dest, XContainerDataPtr(dest));
    }

    // 拷贝元数据（包括 m_useCow、容量、大小等）
    memcpy((XClass*)dest + 1, (XClass*)src + 1, sizeof(XBitArray) - sizeof(XClass));

    // 根据源模式拷贝数据
    if (XContainerIsCow(src)) {
        // COW 模式：共享 XSharedData，增加引用计数
        XContainerSetDataPtr(dest, (XSharedData*)XContainerDataPtr(src));
        if ((XSharedData*)XContainerDataPtr(dest))
            XSharedData_addRef((XSharedData*)XContainerDataPtr(dest));
    }
    else {
        // 非 COW 模式：深拷贝原始数据
        if (XContainerDataPtr(src) && XContainerSize(src) > 0) {
            size_t bytes = BYTE_COUNT(XContainerSize(src));
            void* newData = XContainer_malloc(dest, bytes);
            if (newData) {
                memcpy(newData, XContainerDataPtr(src), bytes);
                XContainerDataPtr(dest) = newData;
            }
            else {
                XContainerDataPtr(dest) = NULL;
            }
        }
        else {
            XContainerDataPtr(dest) = NULL;
        }
    }
    // 注意：XContainerCapacity 和 XContainerSize 已在 memcpy 中复制
}

// 移动
void VXBitArray_move(XBitArray* dest, XBitArray* src)
{
    if (!dest || !src) return;
    XMemory* source_memory = Class_Memory(src);
    bool target_uninitialized = XClassIsVtableNull(dest);
    if (target_uninitialized) {
        XBitArray_init(dest, 0, false);
    }
    else if (XContainerIsCow(dest)) {
        if (XContainerDataPtr(dest))
            XSharedData_release_with((XSharedData*)XContainerDataPtr(dest),
                VXBitArrayDataDelete, dest, XContainer_memory(dest));
    }
    else if (XContainerDataPtr(dest)) {
        XContainer_free(dest, XContainerDataPtr(dest));
    }

    // 转移所有权
    memcpy((XClass*)dest + 1, (XClass*)src + 1, sizeof(XBitArray) - sizeof(XClass));
    Class_Memory(dest) = source_memory;

    // 清空源对象
    if (XContainerIsCow(src)) {
        XContainerSetDataPtr(src, NULL);
    }
    else {
        XContainerDataPtr(src) = NULL;
    }
    XContainerSize(src) = 0;
    XContainerCapacity(src) = 0;
}

// 析构
void VXBitArray_deinit(XBitArray* array)
{
    if (!array) return;

    if (XContainerIsCow(array)) {
        if ((XSharedData*)XContainerDataPtr(array))
            XSharedData_release_with((XSharedData*)XContainerDataPtr(array),
                VXBitArrayDataDelete, array, XContainer_memory(array));
    }
    else {
        if (XContainerDataPtr(array))
            XContainer_free(array, XContainerDataPtr(array));
    }
    XContainerSize(array) = 0;
    XContainerCapacity(array) = 0;
    XContainerSetDataPtr(array, NULL);
}

// 清空
bool VXBitArray_clear(XBitArray* array)
{
    if (!array) return false;

    if (XBitArray_isEmpty_base(array)) return true;

    // COW 模式且共享：直接丢弃共享块，创建空数据
    if (XContainerIsCow(array) && (XSharedData*)XContainerDataPtr(array) && XSharedData_isShared((XSharedData*)XContainerDataPtr(array)))
    {
        XSharedData_release((XSharedData*)XContainerDataPtr(array), XContainer_memory(array));
        // 创建至少1字节的空数据
        XSharedData* sd = XSharedData_create_ex(NULL, 1, XContainer_memory(array));
        if (sd) {
            XContainerSetDataPtr(array, sd);
            XContainerCapacity(array) = 8;
        }
        else {
            XContainerSetDataPtr(array, NULL);
            XContainerCapacity(array) = 0;
        }
        XContainerSize(array) = 0;
        return true;
    }

    // 不共享或非 COW：直接清空数据（fill false）
    XBitArray_fill(array, false);
    return true;
}

// ======================== 公开 API ========================

XBitArray* XBitArray_create_ex(XMemoryType memory, size_t initialBitCount, bool useCow)
{
    XBitArray* array = XMemory_malloc(sizeof(XBitArray), memory);
    if (array) {
        XBitArray_init_with_memory(array, initialBitCount, useCow, memory);
        Set_Class_Memory(array, memory); Set_Class_IsHeap(array, true);
    }
    return array;
}

XBitArray* XBitArray_create_copy(const XBitArray* other) {
    if (!other) return NULL;
    XMemoryType memory = XContainerIsCow((XBitArray*)other) ?
        XContainer_memory_type((const XContainer*)other) : XCLASS_DEFAULT_MEMORY_TYPE;
    XBitArray* array = XBitArray_create_ex(memory, 0, XContainerIsCow((XBitArray*)other));
    if (array) {
        XBitArray_copy_base(array, other);
    }
    return array;
}

XBitArray* XBitArray_create_move(XBitArray* other) {
    if (!other) return NULL;
    XBitArray* array = XBitArray_create_ex(XContainer_memory_type((const XContainer*)other),
        0, XContainerIsCow(other));
    if (array) {
        XBitArray_move_base(array, other);
    }
    return array;
}

static void XBitArray_init_with_memory(XBitArray* array, size_t initialBitCount,
    bool useCow, XMemoryType memory)
{
    if (!array) return;
    // 初始化基类，类型大小设为1（字节）
    XContainer_init(&array->m_class, 1, useCow);
    XClassSetVtable(array, XBitArray);
    Set_Class_Memory(array, memory);

    size_t initialBytes = BYTE_COUNT(initialBitCount);
    if (initialBytes == 0) initialBytes = 1;

    if (XContainerIsCow(array)) {
        XSharedData* sd = XSharedData_create_ex(NULL, initialBytes, XContainer_memory(array));
        if (sd) {
            XContainerSetDataPtr(array, sd);
            memset(sd->data, 0, initialBytes);
        }
        else {
            XContainerSetDataPtr(array, NULL);
        }
    }
    else {
        void* raw = XContainer_malloc(array, initialBytes);
        if (raw) {
            XContainerDataPtr(array) = raw;
            memset(raw, 0, initialBytes);
        }
        else {
            XContainerDataPtr(array) = NULL;
        }
    }
    XContainerCapacity(array) = initialBytes * 8;
    XContainerSize(array) = initialBitCount;
    array->m_bitOrder = XBIT_ORDER_LSB_FIRST;
}

void XBitArray_init(XBitArray* array, size_t initialBitCount, bool useCow)
{
    XBitArray_init_with_memory(array, initialBitCount, useCow,
        XCLASS_DEFAULT_MEMORY_TYPE);
}

bool XBitArray_setBit(XBitArray* array, size_t index, bool value)
{
    if (!array || index >= XBitArray_count(array)) return false;

    if (!VXBitArrayDetachIfNeeded(array)) return false;

    uint8_t* data = (uint8_t*)XBitArray_data(array);
    size_t byteIdx = index / 8;
    uint8_t bitMask = (array->m_bitOrder == XBIT_ORDER_MSB_FIRST) ? (1 << (7 - (index % 8))) : (1 << (index % 8));

    if (value)
        data[byteIdx] |= bitMask;
    else
        data[byteIdx] &= ~bitMask;
    return true;
}

bool XBitArray_getBit(const XBitArray* array, size_t index)
{
    if (!array || index >= XBitArray_count(array)) return false;

    const uint8_t* data = (const uint8_t*)XBitArray_data_const(array);
    size_t byteIdx = index / 8;
    uint8_t bitMask = (array->m_bitOrder == XBIT_ORDER_MSB_FIRST) ? (1 << (7 - (index % 8))) : (1 << (index % 8));
    return (data[byteIdx] & bitMask) != 0;
}

bool XBitArray_toggleBit(XBitArray* array, size_t index)
{
    if (!array || index >= XBitArray_count(array)) return false;

    if (!VXBitArrayDetachIfNeeded(array)) return false;

    uint8_t* data = (uint8_t*)XBitArray_data(array);
    size_t byteIdx = index / 8;
    uint8_t bitMask = (array->m_bitOrder == XBIT_ORDER_MSB_FIRST) ? (1 << (7 - (index % 8))) : (1 << (index % 8));
    data[byteIdx] ^= bitMask;
    return true;
}

bool XBitArray_resize(XBitArray* array, size_t newBitCount)
{
    if (!array) return false;

    if (!VXBitArrayDetachIfNeeded(array)) return false;

    size_t newBytes = BYTE_COUNT(newBitCount);
    size_t oldBytes = BYTE_COUNT(XContainerCapacity(array)); // 当前容量（比特）

    if (newBytes > oldBytes) {
        // 需要扩容
        if (XContainerIsCow(array)) {
            XSharedData* newSd = XSharedData_create_ex(NULL, newBytes, XContainer_memory(array));
            if (!newSd) return false;
            // 拷贝旧数据
            void* oldData = XContainerSharedDataPtr(array);
            if (oldData && oldBytes > 0)
                memcpy(newSd->data, oldData, oldBytes);
            XSharedData_release((XSharedData*)XContainerDataPtr(array), XContainer_memory(array));
            XContainerSetDataPtr(array, newSd);
        }
        else {
            void* newRaw = XContainer_realloc(array, XContainerDataPtr(array), newBytes);
            if (!newRaw) return false;
            XContainerDataPtr(array) = newRaw;
        }
        XContainerCapacity(array) = newBytes * 8;
    }

    // 如果新大小小于当前大小，无需释放内存，只是逻辑截断
    XContainerSize(array) = newBitCount;
    return true;
}

void XBitArray_fill(XBitArray* array, bool value)
{
    if (!array || XBitArray_isEmpty_base(array)) return;

    if (!VXBitArrayDetachIfNeeded(array)) return;

    size_t fullBytes = XBitArray_count(array) / 8;
    size_t remBits = XBitArray_count(array) % 8;
    uint8_t* data = (uint8_t*)XBitArray_data(array);

    memset(data, value ? 0xFF : 0x00, fullBytes);
    if (remBits > 0) {
        uint8_t mask = (0xFF << (8 - remBits)) & 0xFF;
        data[fullBytes] = value ? mask : 0x00;
    }
}

bool XBitArray_append(XBitArray* array, const XBitArray* other)
{
    if (!array || !other) return false;

    if (!VXBitArrayDetachIfNeeded(array)) return false;

    size_t oldCount = XBitArray_count(array);
    size_t appendCount = XBitArray_count(other);
    if (!XBitArray_resize(array, oldCount + appendCount)) return false;

    // 逐位拷贝（性能非最优，但简单可靠）
    for (size_t i = 0; i < appendCount; i++) {
        XBitArray_setBit(array, oldCount + i, XBitArray_getBit(other, i));
    }
    return true;
}

bool XBitArray_clearBits(XBitArray* array, size_t index)
{
    if (!array || index >= XBitArray_size_base(array)) return false;

    if (!VXBitArrayDetachIfNeeded(array)) return false;

    uint8_t* data = (uint8_t*)XBitArray_data(array);
    size_t byteIdx = index / 8;
    uint8_t bitPos = index % 8;
    data[byteIdx] &= ~(1 << bitPos);
    return true;
}

bool XBitArray_writeBits(XBitArray* array, size_t startIndex, size_t bitCount, const uint8_t* src, size_t srcByteLen)
{
    if (!array || !src || bitCount == 0 || srcByteLen == 0) return false;
    if (!VXBitArrayDetachIfNeeded(array)) return false;

    size_t requiredSize = startIndex + bitCount;
    if (requiredSize > XBitArray_count(array)) {
        if (!XBitArray_resize(array, requiredSize)) return false;
    }

    uint8_t* data = (uint8_t*)XBitArray_data(array);
    XBitOrder bitOrder = array->m_bitOrder;
    // 为了简化，实现一个简单的按位拷贝（可优化但保证正确）
    for (size_t i = 0; i < bitCount; i++) {
        size_t srcByteIdx = (i) / 8;
        uint8_t srcBitPos = (bitOrder == XBIT_ORDER_MSB_FIRST) ? (7 - (i % 8)) : (i % 8);
        bool bitVal = (src[srcByteIdx] >> srcBitPos) & 1;
        size_t destBitIdx = startIndex + i;
        size_t destByteIdx = destBitIdx / 8;
        uint8_t destBitPos = (bitOrder == XBIT_ORDER_MSB_FIRST) ? (7 - (destBitIdx % 8)) : (destBitIdx % 8);
        if (bitVal)
            data[destByteIdx] |= (1 << destBitPos);
        else
            data[destByteIdx] &= ~(1 << destBitPos);
    }
    return true;
}

bool XBitArray_readBits(const XBitArray* array, size_t startIndex, size_t bitCount, uint8_t* dest, size_t destByteLen)
{
    if (!array || !dest || bitCount == 0 || destByteLen == 0) return false;
    if (startIndex + bitCount > XBitArray_count(array)) return false;
    size_t requiredBytes = (bitCount + 7) / 8;
    if (destByteLen < requiredBytes) return false;

    memset(dest, 0, requiredBytes);
    const uint8_t* data = (const uint8_t*)XBitArray_data_const(array);
    XBitOrder bitOrder = array->m_bitOrder;

    for (size_t i = 0; i < bitCount; i++) {
        size_t srcBitIdx = startIndex + i;
        size_t srcByteIdx = srcBitIdx / 8;
        uint8_t srcBitPos = (bitOrder == XBIT_ORDER_MSB_FIRST) ? (7 - (srcBitIdx % 8)) : (srcBitIdx % 8);
        bool bitVal = (data[srcByteIdx] >> srcBitPos) & 1;
        size_t destByteIdx = i / 8;
        uint8_t destBitPos = (bitOrder == XBIT_ORDER_MSB_FIRST) ? (7 - (i % 8)) : (i % 8);
        if (bitVal)
            dest[destByteIdx] |= (1 << destBitPos);
    }
    return true;
}

bool XBitArray_setBitOrder(XBitArray* array, XBitOrder bitOrder)
{
    if (!array) return false;
    if (bitOrder != XBIT_ORDER_MSB_FIRST && bitOrder != XBIT_ORDER_LSB_FIRST) return false;
    array->m_bitOrder = bitOrder;
    return true;
}

XBitOrder XBitArray_getBitOrder(const XBitArray* array)
{
    if (!array) return XBIT_ORDER_DEFAULT;
    return array->m_bitOrder;
}

const char* XBitArray_bits(const XBitArray* array)
{
    if (!array) return NULL;
    return (const char*)XBitArray_data_const(array);
}

void XBitArray_truncate(XBitArray* array, int64_t pos)
{
    if (!array) return;
    if (!VXBitArrayDetachIfNeeded(array)) return;
    if (pos < 0) pos = 0;
    size_t newCount = (size_t)pos;
    if (newCount >= XBitArray_count(array)) return;
    XBitArray_resize(array, newCount);
}



/* ============================== Qt 6.8 命名对齐 (QBitArray) 实现 ============================== */

bool XBitArray_setBit_one(XBitArray* array, size_t index)
{
    return XBitArray_setBit(array, index, true);
}

size_t XBitArray_countBits(const XBitArray* array, bool on)
{
    if (!array) return 0;
    size_t n = XBitArray_count((XBitArray*)array);
    size_t cnt = 0;
    for (size_t i = 0; i < n; ++i) {
        if (XBitArray_getBit(array, i)) ++cnt;
    }
    return on ? cnt : (n - cnt);
}

void XBitArray_fill_range(XBitArray* array, bool value, size_t first, size_t last)
{
    if (!array) return;
    size_t n = XBitArray_count(array);
    if (first > n) first = n;
    if (last  > n) last  = n;
    for (size_t i = first; i < last; ++i) {
        XBitArray_setBit(array, i, value);
    }
}

bool XBitArray_equals(const XBitArray* lhs, const XBitArray* rhs)
{
    if (lhs == rhs) return true;
    if (!lhs || !rhs) return false;
    size_t na = XBitArray_count((XBitArray*)lhs);
    size_t nb = XBitArray_count((XBitArray*)rhs);
    if (na != nb) return false;
    for (size_t i = 0; i < na; ++i) {
        if (XBitArray_getBit(lhs, i) != XBitArray_getBit(rhs, i)) return false;
    }
    return true;
}

XBitArray* XBitArray_and_inplace(XBitArray* lhs, const XBitArray* rhs)
{
    if (!lhs || !rhs) return lhs;
    size_t n = XBitArray_count(lhs);
    size_t m = XBitArray_count((XBitArray*)rhs);
    size_t k = n < m ? n : m;
    for (size_t i = 0; i < k; ++i) {
        bool a = XBitArray_getBit(lhs, i);
        bool b = XBitArray_getBit(rhs, i);
        XBitArray_setBit(lhs, i, a && b);
    }
    for (size_t i = k; i < n; ++i) XBitArray_setBit(lhs, i, false);
    return lhs;
}

XBitArray* XBitArray_or_inplace(XBitArray* lhs, const XBitArray* rhs)
{
    if (!lhs || !rhs) return lhs;
    size_t n = XBitArray_count(lhs);
    size_t m = XBitArray_count((XBitArray*)rhs);
    size_t k = n < m ? n : m;
    for (size_t i = 0; i < k; ++i) {
        bool a = XBitArray_getBit(lhs, i);
        bool b = XBitArray_getBit(rhs, i);
        XBitArray_setBit(lhs, i, a || b);
    }
    return lhs;
}

XBitArray* XBitArray_xor_inplace(XBitArray* lhs, const XBitArray* rhs)
{
    if (!lhs || !rhs) return lhs;
    size_t n = XBitArray_count(lhs);
    size_t m = XBitArray_count((XBitArray*)rhs);
    size_t k = n < m ? n : m;
    for (size_t i = 0; i < k; ++i) {
        bool a = XBitArray_getBit(lhs, i);
        bool b = XBitArray_getBit(rhs, i);
        XBitArray_setBit(lhs, i, a != b);
    }
    return lhs;
}

XBitArray* XBitArray_invert_inplace(XBitArray* array)
{
    if (!array) return NULL;
    size_t n = XBitArray_count(array);
    for (size_t i = 0; i < n; ++i) XBitArray_toggleBit(array, i);
    return array;
}

XBitArray* XBitArray_inverted(const XBitArray* array)
{
    if (!array) return NULL;
    XBitArray* out = XBitArray_create_copy(array);
    if (!out) return NULL;
    XBitArray_invert_inplace(out);
    return out;
}

#endif // XBitArray_ON
