//﻿/**
//* @file XByteArray.h
//* @brief 字节数组容器头文件
//* @details 定义了基于XVector的字节数组容器XByteArray，专门用于存储和操作uint8_t类型数据.
//* 提供字节级别的添加、删除、查找、编码转换（16进制、Base64）、压缩解压等功能,
//* 继承自XVector以复用动态数组的基础管理能力。
//*/
#include"CXinYueConfig.h"///< 项目配置文件，控制XByteArray模块是否启用
#if !defined(XBYTEARRAY_H)&& XByteArray_ON
#define XBYTEARRAY_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XVector.h"                  ///< 向量容器头文件，XByteArray继承自XVector
#include "XByteArray_iterator.h"      ///< XByteArray正向迭代器定义
#include "XByteArray_reverse_iterator.h"  ///< XByteArray反向迭代器定义
/**
* @brief 字节数组容器结构体
* @details 继承自XVector，专门用于存储uint8_t（字节）类型数据。
*          复用XVector的动态数组管理能力（数据存储、大小、容量等），
*          同时扩展字节特有的操作（如编码转换、压缩等）。
*/
typedef struct XByteArray
{
XVector m_class;  ///< 继承自XVector基类，包含数据存储、大小、容量等核心成员
} XByteArray;
//============================= 创建与初始化 =============================

/**
* @brief 创建指定初始大小的XByteArray实例
* @param size 初始字节数量（若为0则创建空数组）
* @return 成功返回XByteArray实例指针，失败返回NULL（内存分配失败）
*/
XByteArray* XByteArray_create(size_t size);

/**
* @brief 基于已有XByteArray创建深拷贝实例
* @param other 被复制的XByteArray实例指针（不可为NULL）
* @return 成功返回新的XByteArray实例指针，失败返回NULL（参数无效或内存分配失败）
*/
XByteArray* XByteArray_create_copy(const XByteArray* other);

/**
* @brief 转移已有XByteArray的资源（移动构造）
* @param other 被移动的XByteArray实例指针（不可为NULL）
* @return 成功返回新的XByteArray实例指针，失败返回NULL（参数无效或内存分配失败）
* @note 资源转移后，原实例other会被清空
*/
XByteArray* XByteArray_create_move(XByteArray* other);

/**
* @brief 初始化XByteArray实例
* @param array 待初始化的XByteArray实例指针（需提前分配内存，不可为NULL）
* @details 初始化内部继承的XVector结构，设置元素类型为uint8_t并配置比较函数
*/
void XByteArray_init(XByteArray* array);


//============================= 元素添加 =============================

/**
* @brief 在字节数组头部添加一个字节
* @param array 目标XByteArray实例指针（不可为NULL）
* @param byte 待添加的字节（uint8_t类型）
* @return 添加成功返回true，失败返回false（参数无效或扩容失败）
* @note 原数据会依次后移，可能触发容器扩容
*/
bool XByteArray_push_front_base(XByteArray* array, const uint8_t byte);

/**
* @brief 在字节数组尾部添加一个字节
* @param array 目标XByteArray实例指针（不可为NULL）
* @param byte 待添加的字节（uint8_t类型）
* @return 添加成功返回true，失败返回false（参数无效或扩容失败）
*/
bool XByteArray_push_back_base(XByteArray* array, const uint8_t byte);

/**
* @brief 在指定索引位置插入一个字节
* @param array 目标XByteArray实例指针（不可为NULL）
* @param index 插入位置索引（支持负数，-1表示末尾）
* @param byte 待插入的字节（uint8_t类型）
* @return 插入成功返回true，失败返回false（参数无效、索引越界或扩容失败）
* @note 原index及之后的数据会后移
*/
bool XByteArray_insert_base(XByteArray* array, int64_t index, const uint8_t byte);

/**
* @brief 在指定索引位置插入n个相同字节
* @param array 目标XByteArray实例指针（不可为NULL）
* @param index 插入位置索引（支持负数）
* @param byte 待插入的字节值
* @param n 插入的字节数量（需大于0）
* @return 插入成功返回true，失败返回false（参数无效、索引越界或扩容失败）
*/
bool XByteArray_inserts_base(XByteArray* array, int64_t index, uint8_t byte, size_t n);

/**
* @brief 追加UTF8字符串到字节数组（不含终止符'\0'）
* @param array 目标XByteArray实例指针（不可为NULL）
* @param utf8 待追加的UTF8字符串指针（不可为NULL且长度需大于0）
* @return 追加成功返回true，失败返回false（参数无效或字符串长度为0）
*/
bool XByteArray_append_utf8(XByteArray* array, const char* utf8);

/**
* @brief 复用XVector的接口，追加另一个数组的所有数据
* @details 将另一个字节数组的所有元素追加到当前数组尾部
*/
#define XByteArray_append_array_base				XVector_append_array_base

/**
* @brief 复用XVector的接口，追加单个元素
*/
#define XByteArray_append_base						XVector_append_base

/**
* @brief 复用XVector的接口，追加另一个数组的资源（移动语义）
*/
#define XByteArray_append_move_base					XVector_append_move_base

/**
* @brief 复用XVector的接口，在头部 prepend 数据
*/
#define XByteArray_prepend_base						XVector_prepend_base

/**
* @brief 复用XVector的接口，在头部 prepend 另一个数组的资源（移动语义）
*/
#define XByteArray_prepend_move_base				XVector_prepend_move_base


//============================= 元素删除 =============================

/**
* @brief 复用XVector的接口，删除头部第一个元素
*/
#define XByteArray_pop_front_base					XVector_pop_front_base

/**
* @brief 复用XVector的接口，删除尾部最后一个元素
*/
#define XByteArray_pop_back_base					XVector_pop_back_base

/**
* @brief 复用XVector的接口，删除指定指针位置的元素
* @note 指针需指向容器内的有效元素
*/
#define XByteArray_erase_base						XVector_erase_base

/**
* @brief 复用XVector的接口，删除指定范围的元素
* @param n 若n<0则删除index之后的所有元素，否则删除index开始的n个元素
*/
#define XByteArray_remove_base						XVector_remove_base

/**
* @brief 复用XVector的接口，清空数组所有元素（保留容量）
*/
#define XByteArray_clear_base						XVector_clear_base


//============================= 容器调整 =============================

/**
* @brief 复用XVector的接口，调整字节数组大小
* @details 若新大小大于当前容量则扩容，多余空间用默认值填充
*/
#define XByteArray_resize_base						XVector_resize_base

/**
* @brief 复用XVector的接口，在指定位置插入另一个数组的指定范围数据
*/
#define XByteArray_insert_array_base				XVector_insert_array_base


//============================= 元素访问与查询 =============================

/**
* @brief 复用XVector的接口，获取指定索引的元素指针
* @return 成功返回元素指针，失败返回NULL（索引越界）
*/
#define XByteArray_at_base							XVector_at_base

/**
* @brief 获取指定索引的元素值（宏封装）
* @param array 目标XByteArray实例指针
* @param index 元素索引
* @return 返回uint8_t类型的元素值
*/
#define XByteArray_At_Base(array,index)				XVector_At_Base(array,index,uint8_t)

/**
* @brief 复用XVector的接口，获取头部第一个元素的指针
* @return 成功返回指针，数组为空时返回NULL
*/
#define XByteArray_front_base						XVector_front_base

/**
* @brief 获取头部第一个元素的值（宏封装）
* @param array 目标XByteArray实例指针
* @return 返回uint8_t类型的元素值
*/
#define XByteArray_Front_Base(array)				XVector_Front_Base(array,uint8_t)

/**
* @brief 复用XVector的接口，获取尾部最后一个元素的指针
* @return 成功返回指针，数组为空时返回NULL
*/
#define XByteArray_back_base						XVector_back_base

/**
* @brief 获取尾部最后一个元素的值（宏封装）
* @param array 目标XByteArray实例指针
* @return 返回uint8_t类型的元素值
*/
#define XByteArray_Back_Base(array)					XVector_Back_Base(array,uint8_t)

/**
* @brief 在字节数组中查找指定字节
* @param array 目标XByteArray实例指针（不可为NULL）
* @param findVal 待查找的字节值
* @param it 输出参数，用于存储找到的位置的迭代器（未找到时内容不确定）
* @return 找到返回true，未找到或参数无效返回false
*/
bool XByteArray_find_base(const XByteArray* array, const uint8_t findVal, XByteArray_iterator* it);

/**
* @brief 复用XVector的接口，统计指定元素的出现次数
* @return 返回元素在数组中出现的次数
*/
#define XByteArray_count_base						XVector_count_base

/**
* @brief 复用XVector的接口，判断数组是否为空
* @return 为空返回true，否则返回false
*/
#define XByteArray_isEmpty_base						XVector_isEmpty_base

/**
* @brief 复用XVector的接口，获取数组当前元素数量（字节数）
* @return 返回数组中的字节总数
*/
#define XByteArray_size_base						XVector_size_base

/**
* @brief 复用XVector的接口，获取数组当前容量（可容纳的最大字节数）
* @return 返回数组的容量
*/
#define XByteArray_capacity_base					XVector_capacity_base


//============================= 容器操作 =============================

/**
* @brief 复用XVector的接口，对数组元素进行排序
*/
#define XByteArray_sort_base						XVector_sort_base

/**
* @brief 复用XVector的接口，复制另一个数组的内容到当前数组
* @param this_One 目标数组
* @param this_Two 源数组（被复制的数组）
*/
#define XByteArray_copy_base						XVector_copy_base

/**
* @brief 复用XVector的接口，逆序复制另一个数组的内容到当前数组
* @param this_One 目标数组
* @param this_Two 源数组（被复制的数组）
*/
#define XByteArray_rcopy_base						XVector_rcopy_base

/**
* @brief 复用XVector的接口，转移另一个数组的资源到当前数组
* @param this_One 目标数组
* @param this_Two 源数组（资源被转移后会被清空）
*/
#define XByteArray_move_base						XVector_move_base

/**
* @brief 复用XVector的接口，交换两个数组的内容
* @param a 第一个数组
* @param b 第二个数组
*/
#define XByteArray_swap_base						XVector_swap_base

/**
* @brief 复用XVector的接口，获取元素类型大小（uint8_t为1字节）
* @return 返回1（字节）
*/
#define XByteArray_typeSize_base					XVector_typeSize_base

/**
* @brief 比较两个字节数组
* @param lhs 左操作数数组
* @param rhs 右操作数数组
* @return 若lhs < rhs返回XCompare_Less；lhs > rhs返回XCompare_Greater；相等返回XCompare_Equality
*/
int32_t XByteArray_compare(const XByteArray* lhs, const XByteArray* rhs);


//============================= 编码与转换 =============================

/**
* @brief 将字节数组转换为16进制UTF8字符串（字节数组形式）
* @param array 目标XByteArray实例指针（不可为NULL且非空）
* @return 成功返回包含16进制字符串的XByteArray实例，失败返回NULL
*/
XByteArray* XByteArray_to16HexUtf8(XByteArray* array);

/**
* @brief 将字节数组转换为16进制字符串（XString形式）
* @param array 目标XByteArray实例指针（不可为NULL且非空）
* @return 成功返回包含16进制字符串的XString实例，失败返回NULL
*/
XString* XByteArray_to16HexString(XByteArray* array);

/**
* @brief 将字节数组转换为Base64编码的字节数组
* @param array 目标XByteArray实例指针（不可为NULL且非空）
* @return 成功返回Base64编码的XByteArray实例，失败返回NULL
*/
XByteArray* XByteArray_toBase64(XByteArray* array);

/**
* @brief 将Base64编码的字节数组解码为原始字节数组
* @param base64 包含Base64编码数据的XByteArray实例（不可为NULL且非空）
* @return 成功返回解码后的原始XByteArray实例，失败返回NULL
*/
XByteArray* XByteArray_fromBase64(XByteArray* base64);


//============================= 压缩与解压 =============================

/**
* @brief 对字节数组进行压缩
* @param sData 待压缩的字节数组（不可为NULL且非空）
* @return 成功返回压缩后的XByteArray实例，失败返回NULL
*/
XByteArray* XByteArray_toCompress(XByteArray* sData);

/**
* @brief 对字节数组进行解压
* @param sData 待解压的字节数组（不可为NULL且非空）
* @return 成功返回解压后的XByteArray实例，失败返回NULL
*/
XByteArray* XByteArray_toDecompress(XByteArray* sData);


//============================= 销毁与清理 =============================

/**
* @brief 复用XVector的接口，反初始化数组（释放内部资源，保留实例本身）
* @param array 目标XByteArray实例指针
*/
#define XByteArray_deinit_base						XVector_deinit_base

/**
* @brief 复用XVector的接口，销毁数组并释放所有内存
* @param array 目标XByteArray实例指针（可NULL，NULL时不操作）
*/
#define XByteArray_delete_base						XVector_delete_base


//============================= 迭代器 =============================

/**
* @brief 复用XVector的接口，获取正向迭代器的起始位置
* @return 返回指向第一个元素的正向迭代器
*/
#define XByteArray_begin						XVector_begin

/**
* @brief 复用XVector的接口，获取正向迭代器的结束位置
* @return 返回指向最后一个元素后一位的正向迭代器
*/
#define XByteArray_end						XVector_end

/**
* @brief 复用XVector的接口，移动正向迭代器
* @param it 正向迭代器
* @param n 移动的步数（可正可负）
* @return 返回移动后的正向迭代器
*/
#define XByteArray_iterator_add				XVector_iterator_add

/**
* @brief 复用XVector的接口，判断两个正向迭代器是否相等
* @return 相等返回true，否则返回false
*/
#define XByteArray_iterator_equality          XVector_iterator_equality

/**
* @brief 复用XVector的接口，遍历正向迭代器范围内的元素
* @param begin 起始迭代器
* @param end 结束迭代器
* @param func 遍历回调函数
* @param userData 用户自定义数据
*/
#define XByteArray_iterator_for_each			XVector_iterator_for_each

/**
* @brief 复用XVector的接口，获取正向迭代器指向的元素指针
* @return 返回元素指针，迭代器无效时返回NULL
*/
#define XByteArray_iterator_data				XVector_iterator_data

/**
* @brief 复用XVector的接口，获取反向迭代器的起始位置（即正向迭代器的末尾）
* @return 返回指向最后一个元素的反向迭代器
*/
#define XByteArray_rbegin								XVector_rbegin

/**
* @brief 复用XVector的接口，获取反向迭代器的结束位置（即正向迭代器的起始）
* @return 返回指向第一个元素前一位的反向迭代器
*/
#define XByteArray_rend									XVector_rend

/**
* @brief 复用XVector的接口，移动反向迭代器
* @param it 反向迭代器
* @param n 移动的步数（可正可负）
* @return 返回移动后的反向迭代器
*/
#define XByteArray_reverse_iterator_add					XVector_reverse_iterator_add

/**
* @brief 复用XVector的接口，判断两个反向迭代器是否相等
* @return 相等返回true，否则返回false
*/
#define XByteArray_reverse_iterator_equality			XVector_reverse_iterator_equality

/**
* @brief 复用XVector的接口，遍历反向迭代器范围内的元素
* @param begin 起始反向迭代器
* @param end 结束反向迭代器
* @param func 遍历回调函数
* @param userData 用户自定义数据
*/
#define XByteArray_reverse_iterator_for_each			XVector_reverse_iterator_for_each

/**
* @brief 复用XVector的接口，获取反向迭代器指向的元素指针
* @return 返回元素指针，迭代器无效时返回NULL
*/
#define XByteArray_reverse_iterator_data				XVector_reverse_iterator_data

#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H
