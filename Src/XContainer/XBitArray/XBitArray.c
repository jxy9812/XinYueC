#include "XBitArray.h"
#if XBitArray_ON
#include <stdlib.h>
#include <string.h>
#include "XVtable.h"

// 计算存储指定比特数所需的字节数（向上取整）
#define BYTE_COUNT(bitCount) ((bitCount + 7) / 8)

// COW分离：如果数据被共享，创建独立副本
static bool VXBitArrayDetachIfNeeded(XBitArray* array);
static void VXBitArrayDataDelete(void* data, XBitArray* array);

// 虚函数实现
static void VXBitArray_copy(XBitArray* dest, const XBitArray* src);
static void VXBitArray_move(XBitArray* dest, XBitArray* src);
static void VXBitArray_deinit(XBitArray* array);
static bool VXBitArray_clear(XBitArray* array);

XVtable* XBitArray_class_init() {
    XVTABLE_CREAT_DEFAULT

#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XBitArray))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif

        // 继承XContainer的虚函数表
        XVTABLE_INHERIT_XCLASS(XContainer);

    //// 注册新虚函数
    //void* table[] = {
    //    VXBitArray_set_bit,
    //    VXBitArray_get_bit,
    //    VXBitArray_flip_bit,
    //    VXBitArray_resize,
    //    VXBitArray_fill,
    //    VXBitArray_append,
    //    VXBitArray_clear_bits
    //};
    //XVTABLE_ADD_FUNC_LIST_DEFAULT(table);

    // 重载基础函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXBitArray_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXBitArray_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBitArray_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXBitArray_clear);

    return XVTABLE_DEFAULT;
}
// COW分离：如果数据被共享，创建独立副本
static bool VXBitArrayDetachIfNeeded(XBitArray* array)
{
    if (!XContainerSharedData(array) || !XSharedData_isShared(XContainerSharedData(array)))
        return true; // 不共享，无需分离
    size_t byteCount = BYTE_COUNT(XBitArray_count(array));
    // 分配新内存
    void* newData = XMalloc_System(byteCount > 0 ? byteCount : 1);
    if (!newData)
        return false;
    // 拷贝数据
    if (byteCount > 0 && XContainerDataPtr(array))
        memcpy(newData, XContainerDataPtr(array), byteCount);
    // 创建新的 XSharedData
    XSharedData* newShared = XSharedData_create(newData);
    if (!newShared)
    {
        XFree_System(newData);
        return false;
    }
    // 减少旧引用，设置新引用
    XSharedData_release(XContainerSharedData(array));
    XContainerSharedData(array) = newShared;
    return true;
}
// 删除BitArray数据
static void VXBitArrayDataDelete(void* data, XBitArray* array)
{
    if (data == NULL || array == NULL)
        return;
    XFree_System(data);
    XContainerSize(array) = 0;
    XContainerCapacity(array) = 0;
    XContainerSharedData(array) = NULL;
}
void VXBitArray_copy(XBitArray* dest, const XBitArray* src)
{
    if (!dest || !src) return;

    // 释放目标原有数据
    if (XContainerSharedData(dest))
    {
        XSharedData_release_with(XContainerSharedData(dest), VXBitArrayDataDelete, dest);
    }

    // 复制元数据
    dest->m_class.m_typeSize = src->m_class.m_typeSize;
    dest->m_class.m_dataCopyMethod = src->m_class.m_dataCopyMethod;
    dest->m_class.m_dataMoveMethod = src->m_class.m_dataMoveMethod;
    dest->m_class.m_dataDeinitMethod = src->m_class.m_dataDeinitMethod;
    dest->m_bitOrder = src->m_bitOrder;

    // 共享源数据的 XSharedData（COW 机制）
    XContainerSharedData(dest) = XContainerSharedData(src);
    if (XContainerSharedData(dest))
    {
        XSharedData_addRef(XContainerSharedData(dest));
    }

    dest->m_class.m_size = src->m_class.m_size;
    dest->m_class.m_capacity = src->m_class.m_capacity;
}

void VXBitArray_move(XBitArray* dest, XBitArray* src) {
    if (!dest || !src) return;

    // 释放目标原有数据
    if (XContainerSharedData(dest))
    {
        XSharedData_release_with(XContainerSharedData(dest), VXBitArrayDataDelete, dest);
    }

    // 转移所有权
    dest->m_class = src->m_class;
    dest->m_bitOrder = src->m_bitOrder;

    // 清空源对象
    XContainerSharedData(src) = NULL;
    src->m_class.m_size = 0;
    src->m_class.m_capacity = 0;
}

void VXBitArray_deinit(XBitArray* array) {
    if (!array) return;

    XSharedData_release_with(XContainerSharedData(array), VXBitArrayDataDelete, array);
    XContainerSize(array) = 0;
    XContainerCapacity(array) = 0;
    XContainerSharedData(array) = NULL;
}

/**
 * @brief 清空比特数组中所有比特位（将所有位设置为0），参考QBitArray的clear行为
 * @param array 目标比特数组对象
 * @return 操作成功返回true，数组为空指针时返回false
 */
bool VXBitArray_clear(XBitArray* array) {
    if (!array) {
        return false;
    }

    // 若数组已为空，直接返回成功
    if (XBitArray_isEmpty_base(array)) {
        return true;
    }

    // 如果数据被共享，减少引用并创建空数据
    if (XContainerSharedData(array) && XSharedData_isShared(XContainerSharedData(array)))
    {
        XSharedData_release(XContainerSharedData(array));

        // 初始容量至少为1字节
        size_t initialBytes = BYTE_COUNT(XContainerSize(array));
        void* data = XMalloc_System(initialBytes);
        XSharedData* sd = XSharedData_create(data);
        if (sd == NULL)
        {
            XFree_System(data);
            XContainerSharedData(array) = NULL;
            return;
        }
        XContainerSharedData(array) = sd;
    }

    // 不共享，直接清空数据
    XBitArray_fill(array, false);
    return true;
}


XBitArray* XBitArray_create(size_t initialBitCount) {
    XBitArray* array = XNew(XBitArray);
    if (array) {
        XBitArray_init(array, initialBitCount);
        Set_Class_MemoryFree(array, XFree_System);
    }
    return array;
}

XBitArray* XBitArray_create_copy(const XBitArray* other) {
    if (!other) return NULL;
    XBitArray* array = XBitArray_create(XBitArray_count(other));
    if (array) {
        XBitArray_copy_base(array, other);
    }
    return array;
}

XBitArray* XBitArray_create_move(XBitArray* other) {
    if (!other) return NULL;
    XBitArray* array = XBitArray_create(0);
    if (array) {
        XBitArray_move_base(array, other);
    }
    return array;
}

void XBitArray_init(XBitArray* array, size_t initialBitCount)
{
    if (!array) return;
    // 初始化基类（类型大小设为1，便于按字节管理）
    XContainer_init(array, 1);
    XClassGetVtable(array) = XBitArray_class_init();

    // 初始容量至少为1字节
    size_t initialBytes = BYTE_COUNT(initialBitCount);
    void* data = XMalloc_System(initialBytes);
    XSharedData* sd = XSharedData_create(data);
    if (sd == NULL)
    {
        XFree_System(data);
        XFree_System(array);
        return;
    }
    XContainerSharedData(array) = sd;

    if (XContainerDataPtr(array)) {
        memset(XContainerDataPtr(array), 0, initialBytes);
        XContainerCapacity(array) = initialBytes * 8; // 容量以比特为单位
        XContainerSize(array) = initialBitCount;      // 初始比特数
    }
    array->m_bitOrder = XBIT_ORDER_LSB_FIRST;
}

bool XBitArray_setBit(XBitArray* array, size_t index, bool value)
{
    if (!array || index >= XBitArray_count(array)) return false;

    // COW分离
    if (!VXBitArrayDetachIfNeeded(array)) return false;

    uint8_t* data = (uint8_t*)XContainerDataPtr(array);
    size_t byteIdx = index / 8;
    uint8_t bitMask = (array->m_bitOrder == XBIT_ORDER_MSB_FIRST) ? 1 << (7 - (index % 8)) : 1 << ((index % 8));//1 << (7-(index % 8))

    if (value) {
        data[byteIdx] |= bitMask;
    }
    else {
        data[byteIdx] &= ~bitMask;
    }
    return true;
}

bool XBitArray_getBit(const XBitArray* array, size_t index) 
{
    if (!array || index >= XBitArray_count(array)) return false;

    const uint8_t* data = (const uint8_t*)XContainerDataPtr(array);
    size_t byteIdx = index / 8;
    uint8_t bitMask = (array->m_bitOrder == XBIT_ORDER_MSB_FIRST) ? 1 << (7-(index % 8)): 1 << ((index % 8));//1 << (7-(index % 8))

    return (data[byteIdx] & bitMask) != 0;
}

bool XBitArray_toggleBit(XBitArray* array, size_t index) 
{
    if (!array || index >= XBitArray_count(array)) return false;

    // COW分离
    if (!VXBitArrayDetachIfNeeded(array)) return false;

    uint8_t* data = (uint8_t*)XContainerDataPtr(array);
    size_t byteIdx = index / 8;
    uint8_t bitMask = (array->m_bitOrder == XBIT_ORDER_MSB_FIRST) ? 1 << (7 - (index % 8)) : 1 << ((index % 8));

    data[byteIdx] ^= bitMask;
    return true;
}

bool XBitArray_resize(XBitArray* array, size_t newBitCount)
{
    if (!array) return false;

    // COW分离
    if (!VXBitArrayDetachIfNeeded(array)) return false;

    size_t newBytes = BYTE_COUNT(newBitCount);
    size_t oldBytes = BYTE_COUNT(XBitArray_capacity_base(array));

    // 容量不足时重新分配内存
    if (newBytes > oldBytes) {
        void* newData = XRealloc_System(XContainerDataPtr(array), newBytes);
        if (!newData) return false;
        XContainerDataPtr(array) = newData;
        XContainerCapacity(array) = newBytes * 8;
    }

    // 新大小小于原大小时无需修改内存，只需调整计数
    XContainerSize(array) = newBitCount;
    return true;
}

void XBitArray_fill(XBitArray* array, bool value) 
{
    if (!array || XBitArray_isEmpty_base(array)) return;

    // COW分离
    if (!VXBitArrayDetachIfNeeded(array)) return;

    size_t fullBytes = XBitArray_count(array) / 8;
    size_t remBits = XBitArray_count(array) % 8;
    uint8_t* data = (uint8_t*)XContainerDataPtr(array);

    // 填充完整字节
    memset(data, value ? 0xFF : 0x00, fullBytes);

    // 处理剩余比特
    if (remBits > 0) {
        uint8_t mask = (0xFF << (8 - remBits)) & 0xFF; // 仅保留前remBits位
        data[fullBytes] = value ? mask : 0x00;
    }
}

bool XBitArray_append(XBitArray* array, const XBitArray* other)
{
    if (!array || !other) return false;

    // COW分离
    if (!VXBitArrayDetachIfNeeded(array)) return false;

    size_t oldCount = XBitArray_count(array);
    size_t appendCount = XBitArray_count(other);
    if (!XBitArray_resize(array, oldCount + appendCount)) return false;

    // 拷贝比特数据
    for (size_t i = 0; i < appendCount; i++) {
        XBitArray_set(array, oldCount + i, XBitArray_test(other, i));
    }
    return true;
}

bool  XBitArray_clearBits(XBitArray* array, size_t index)
{
    // 参数有效性检查
    if (array == NULL || index >= XBitArray_size_base(array)) {
        return false;  // 索引超出当前比特数范围，无效
    }

    // COW分离
    if (!VXBitArrayDetachIfNeeded(array)) return false;

    // 计算该比特位所在的字节索引和在字节内的位置
    size_t byteIndex = index / 8;  // 每个字节包含8个比特
    uint8_t bitPos = index % 8;    // 比特在字节中的位置（0~7）

    // 获取存储比特数据的字节数组指针
    uint8_t* data = (uint8_t*)XContainerDataPtr(array);

    // 用位运算清除指定比特（与操作：保留其他位，当前位设为0）
    data[byteIndex] &= ~(1 << bitPos);

    return true;
}

bool XBitArray_writeBits(XBitArray* array, size_t startIndex, size_t bitCount, const uint8_t* src, size_t srcByteLen)
{
    // 参数有效性检查
    if (!array || !src || bitCount == 0 || srcByteLen == 0) {
        return false;
    }

    // COW分离
    if (!VXBitArrayDetachIfNeeded(array)) return false;
    // 源数据可提供的最大比特数检查
    size_t maxSrcBits = srcByteLen * 8;
    if (bitCount > maxSrcBits) {
        return false;
    }
    // 计算需要的总容量并扩容
    size_t requiredSize = startIndex + bitCount;
    if (requiredSize > XBitArray_capacity_base(array)) {
        if (!XBitArray_resize(array, requiredSize)) {
            return false;
        }
    }
    else if (requiredSize > XBitArray_count(array)) {
        XContainerSize(array) = requiredSize;
    }

    uint8_t* data = (uint8_t*)XContainerDataPtr(array);
    XBitOrder bitOrder = array->m_bitOrder;
    size_t startByte = startIndex / 8;
    size_t bitOffset; // 起始位在字节内的偏移（0-7，对应实际存储的位位置）

    // 根据比特序计算起始位在字节中的实际偏移
    if (bitOrder == XBIT_ORDER_MSB_FIRST) {
        bitOffset = 7 - (startIndex % 8); // MSB优先：字节高位对应低索引
    }
    else {
        bitOffset = startIndex % 8; // LSB优先：字节低位对应低索引
    }

    // 情况1：完全字节对齐（起始偏移为0且总比特数为8的倍数）
    if (bitOffset == 0 && (bitCount % 8) == 0) {
        size_t byteCount = bitCount / 8;
        memcpy(data + startByte, src, byteCount);
        return true;
    }

    // 情况2：部分对齐或完全不对齐，分三段处理
    size_t processed = 0;

    // 处理起始处非对齐的比特
    size_t startBits = 8 - bitOffset;
    if (startBits > bitCount) {
        startBits = bitCount; // 剩余比特不足一个完整字节
    }
    if (startBits > 0) {
        for (size_t i = 0; i < startBits; i++) {
            // 计算源比特位置
            size_t srcByteIdx = i / 8;
            uint8_t srcBitPos = (bitOrder == XBIT_ORDER_MSB_FIRST) ?
                (7 - (i % 8)) : (i % 8);
            bool bitVal = (src[srcByteIdx] >> srcBitPos) & 1;

            // 计算目标比特位置
            uint8_t targetBitPos = bitOffset + i;
            if (bitVal) {
                data[startByte] |= (1 << targetBitPos);
            }
            else {
                data[startByte] &= ~(1 << targetBitPos);
            }
        }
        processed += startBits;
        startByte++; // 起始字节已处理完，移至下一字节
    }

    // 批量处理中间对齐的完整字节
    size_t remaining = bitCount - processed;
    if (remaining >= 8) {
        size_t alignedBytes = remaining / 8;
        size_t srcStartByte = processed / 8;
        memcpy(data + startByte, src + srcStartByte, alignedBytes);
        processed += alignedBytes * 8;
        startByte += alignedBytes;
    }

    // 处理结束处非对齐的比特
    remaining = bitCount - processed;
    if (remaining > 0) {
        for (size_t i = 0; i < remaining; i++) {
            // 计算源比特位置
            size_t srcByteIdx = (processed + i) / 8;
            uint8_t srcBitPos = (bitOrder == XBIT_ORDER_MSB_FIRST) ?
                (7 - ((processed + i) % 8)) : ((processed + i) % 8);
            bool bitVal = (src[srcByteIdx] >> srcBitPos) & 1;

            // 计算目标比特位置（当前字节内从0开始）
            uint8_t targetBitPos = (bitOrder == XBIT_ORDER_MSB_FIRST) ?
                (7 - i) : i;
            if (bitVal) {
                data[startByte] |= (1 << targetBitPos);
            }
            else {
                data[startByte] &= ~(1 << targetBitPos);
            }
        }
    }

    return true;
}

bool XBitArray_readBits(const XBitArray* array, size_t startIndex, size_t bitCount, uint8_t* dest, size_t destByteLen) {
    // 参数有效性检查
    if (!array || !dest || bitCount == 0 || destByteLen == 0) {
        return false;
    }
    // 读取范围越界检查
    if (startIndex + bitCount > XBitArray_count(array)) {
        return false;
    }
    // 目标缓冲区空间检查
    size_t requiredBytes = (bitCount + 7) / 8;
    if (destByteLen < requiredBytes) {
        return false;
    }
    // 初始化目标缓冲区（清空有效字节）
    memset(dest, 0, requiredBytes);

    const uint8_t* data = (const uint8_t*)XContainerDataPtr(array);
    XBitOrder bitOrder = array->m_bitOrder;
    size_t startByte = startIndex / 8;
    size_t bitOffset; // 起始位在字节内的偏移（0-7，对应实际存储的位位置）

    // 根据比特序计算起始位在字节中的实际偏移
    if (bitOrder == XBIT_ORDER_MSB_FIRST) {
        bitOffset = 7 - (startIndex % 8);
    }
    else {
        bitOffset = startIndex % 8;
    }

    // 情况1：完全字节对齐（起始偏移为0且总比特数为8的倍数）
    if (bitOffset == 0 && (bitCount % 8) == 0) {
        size_t byteCount = bitCount / 8;
        memcpy(dest, data + startByte, byteCount);
        return true;
    }

    // 情况2：部分对齐或完全不对齐，分三段处理
    size_t processed = 0;

    // 处理起始处非对齐的比特
    size_t startBits = 8 - bitOffset;
    if (startBits > bitCount) {
        startBits = bitCount;
    }
    if (startBits > 0) {
        for (size_t i = 0; i < startBits; i++) {
            // 从源数据读取比特
            uint8_t srcBitPos = bitOffset + i;
            bool bitVal = (data[startByte] >> srcBitPos) & 1;

            // 写入目标缓冲区
            size_t destByteIdx = i / 8;
            uint8_t destBitPos = (bitOrder == XBIT_ORDER_MSB_FIRST) ?
                (7 - (i % 8)) : (i % 8);
            if (bitVal) {
                dest[destByteIdx] |= (1 << destBitPos);
            }
        }
        processed += startBits;
        startByte++;
    }

    // 批量处理中间对齐的完整字节
    size_t remaining = bitCount - processed;
    if (remaining >= 8) {
        size_t alignedBytes = remaining / 8;
        size_t destStartByte = processed / 8;
        memcpy(dest + destStartByte, data + startByte, alignedBytes);
        processed += alignedBytes * 8;
        startByte += alignedBytes;
    }

    // 处理结束处非对齐的比特
    remaining = bitCount - processed;
    if (remaining > 0) {
        for (size_t i = 0; i < remaining; i++) {
            // 从源数据读取比特
            uint8_t srcBitPos = (bitOrder == XBIT_ORDER_MSB_FIRST) ?
                (7 - i) : i;
            bool bitVal = (data[startByte] >> srcBitPos) & 1;

            // 写入目标缓冲区
            size_t destByteIdx = (processed + i) / 8;
            uint8_t destBitPos = (bitOrder == XBIT_ORDER_MSB_FIRST) ?
                (7 - ((processed + i) % 8)) : ((processed + i) % 8);
            if (bitVal) {
                dest[destByteIdx] |= (1 << destBitPos);
            }
        }
    }

    return true;
}
bool XBitArray_setBitOrder(XBitArray* array, XBitOrder bitOrder)
{
    if (!array) return false;
    // 检查比特序参数有效性
    if (bitOrder != XBIT_ORDER_MSB_FIRST && bitOrder != XBIT_ORDER_LSB_FIRST) {
        return false;
    }
    array->m_bitOrder = bitOrder;
    return true;
}

XBitOrder XBitArray_getBitOrder(const XBitArray* array)
{
    if (!array) return XBIT_ORDER_DEFAULT; // 空指针返回默认值
    return array->m_bitOrder;
}
const char* XBitArray_bits(const XBitArray* array)
{
    if (!array) {
        return NULL; // 空指针保护
    }

    // 获取内部存储的字节数组指针，转换为const char*类型返回
    // XContainerDataPtr返回void*，指向存储比特的uint8_t数组
    return (const char*)XContainerDataPtr(array);
}

void XBitArray_truncate(XBitArray* array, int64_t pos)
{
    // 空指针检查
    if (!array) {
        return;
    }

    // COW分离
    if (!VXBitArrayDetachIfNeeded(array)) return;

    // 获取当前比特数
    size_t currentCount = XBitArray_count(array);

    // 处理pos为负数的情况：截断到0（清空数组）
    if (pos < 0) {
        XBitArray_resize(array, 0);
        return;
    }

    // 转换pos为无符号类型（已确保pos非负）
    size_t newCount = pos;

    // 若新大小大于等于当前大小，无需截断
    if (newCount >= currentCount) {
        return;
    }

    // 调整数组大小以实现截断（保留前newCount个比特）
    XBitArray_resize(array, newCount);
}

#endif // XBitArray_ON