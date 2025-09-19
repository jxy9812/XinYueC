#include "XBitArray.h"
#if XBitArray_ON
#include "XVtable.h"
#include <stdlib.h>
#include <string.h>
// 计算存储指定比特数所需的字节数（向上取整）
#define BYTE_COUNT(bitCount) ((bitCount + 7) / 8)
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

    // 继承XContainerObject的虚函数表
    XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());

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
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXBitArray_clear);

    return XVTABLE_DEFAULT;
}

void VXBitArray_copy(XBitArray* dest, const XBitArray* src) {
    if (!dest || !src) return;

    // 调整目标数组大小
    XBitArray_resize(dest, XBitArray_count(src));
    size_t byteCount = BYTE_COUNT(XBitArray_count(src));

    // 拷贝数据
    if (byteCount > 0) {
        memcpy(XContainerDataPtr(dest), XContainerDataPtr(src), byteCount);
    }

    // 复制基类元数据
    dest->m_class.m_size = src->m_class.m_size;
    dest->m_class.m_capacity = src->m_class.m_capacity;
    dest->m_class.m_typeSize = src->m_class.m_typeSize;
    dest->m_class.m_dataCopyMethod = src->m_class.m_dataCopyMethod;
    dest->m_class.m_dataMoveMethod = src->m_class.m_dataMoveMethod;
    dest->m_class.m_dataDeinitMethod = src->m_class.m_dataDeinitMethod;
}

void VXBitArray_move(XBitArray* dest, XBitArray* src) {
    if (!dest || !src) return;

    // 释放目标原有数据
    if (dest->m_class.m_data) {
        XMemory_free(dest->m_class.m_data);
    }

    // 移动数据
    dest->m_class = src->m_class;
    src->m_class.m_data = NULL;
    src->m_class.m_size = 0;
    src->m_class.m_capacity = 0;
}

void VXBitArray_deinit(XBitArray* array) {
    if (!array) return;

    if (XContainerDataPtr(array)) {
        XMemory_free(XContainerDataPtr(array));
        XContainerDataPtr(array) = NULL;
    }
    XContainerSize(array) = 0;
    XContainerCapacity(array) = 0;
    //array->m_class.m_typeSize = 0;
}

/**
 * @brief 清空比特数组中所有比特位（将所有位设置为0），参考QBitArray的clear行为
 * @param array 目标比特数组对象
 * @return 操作成功返回true，数组为空指针时返回false
 */
bool VXBitArray_clear(XBitArray* array) {
    if (!array) {
        return false; // 空指针检查，避免无效操作
    }

    // 若数组已为空，直接返回成功
    if (XBitArray_isEmpty_base(array)) {
        return true;
    }

    // 调用填充函数将所有比特位设置为0（QBitArray的clear本质是全0填充）
    XBitArray_fill(array, false);
    return true;
}

#endif // XBitArray_ON