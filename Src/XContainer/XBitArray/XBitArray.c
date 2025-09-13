#include "XBitArray.h"
#if XBitArray_ON
#include <stdlib.h>
#include <string.h>

// 计算存储bitCount所需的字节数
#define BYTE_COUNT(bitCount) ((bitCount + 7) / 8)

XBitArray* XBitArray_create(size_t initialBitCount) {
    XBitArray* array = XMemory_malloc(sizeof(XBitArray));
    if (array) {
        XBitArray_init(array, initialBitCount);
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

void XBitArray_init(XBitArray* array, size_t initialBitCount) {
    if (!array) return;
    // 初始化基类（类型大小设为1，便于按字节管理）
    XContainerObject_init(array, 1);
    XClassGetVtable(array) = XBitArray_class_init();

    // 初始容量至少为1字节
    size_t initialBytes = BYTE_COUNT(initialBitCount);
    XContainerDataPtr(array) = XMemory_malloc(initialBytes);
    if (XContainerDataPtr(array)) {
        memset(XContainerDataPtr(array), 0, initialBytes);
        XContainerCapacity(array) = initialBytes * 8; // 容量以比特为单位
        XContainerSize(array) = initialBitCount;      // 初始比特数
    }
}

bool XBitArray_setBit(XBitArray* array, size_t index, bool value)
{
    if (!array || index >= XBitArray_count(array)) return false;

    uint8_t* data = (uint8_t*)XContainerDataPtr(array);
    size_t byteIdx = index / 8;
    uint8_t bitMask = 1 << (7 - (index % 8)); // 高位在前存储

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
    uint8_t bitMask = 1 << (7 - (index % 8));

    return (data[byteIdx] & bitMask) != 0;
}

bool XBitArray_toggleBit(XBitArray* array, size_t index) 
{
    if (!array || index >= XBitArray_count(array)) return false;

    uint8_t* data = (uint8_t*)XContainerDataPtr(array);
    size_t byteIdx = index / 8;
    uint8_t bitMask = 1 << (7 - (index % 8));

    data[byteIdx] ^= bitMask;
    return true;
}

bool XBitArray_resize(XBitArray* array, size_t newBitCount)
{
    if (!array) return false;

    size_t newBytes = BYTE_COUNT(newBitCount);
    size_t oldBytes = BYTE_COUNT(XBitArray_capacity_base(array));

    // 容量不足时重新分配内存
    if (newBytes > oldBytes) {
        void* newData = XMemory_realloc(XContainerDataPtr(array), newBytes);
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

    // 计算该比特位所在的字节索引和在字节内的位置
    size_t byteIndex = index / 8;  // 每个字节包含8个比特
    uint8_t bitPos = index % 8;    // 比特在字节中的位置（0~7）

    // 获取存储比特数据的字节数组指针
    uint8_t* data = (uint8_t*)XContainerDataPtr(array);

    // 用位运算清除指定比特（与操作：保留其他位，当前位设为0）
    data[byteIndex] &= ~(1 << bitPos);

    return true;
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