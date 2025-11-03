#ifndef XBSONARRAY_H
#define XBSONARRAY_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XBson.h"
#include "XVector.h"
#include "XBsonValue.h"
/**
* @brief BSON数组结构体，用于存储有序的BSON值集合
* @details 内部通过XVector实现，元素类型为XBsonValue，支持多种BSON数据类型的有序存储
*/
typedef struct XBsonArray
{
	XVector elements; // 存储XBsonValue类型的元素，构成BSON数组
} XBsonArray;
// 构造与析构
/**
* @brief 创建一个空的XBsonArray实例
* @return 成功返回XBsonArray指针，失败返回NULL
*/
XBsonArray* XBsonArray_create();
/**
* @brief 从另一个XBsonArray复制创建新实例（深拷贝）
* @param other 被复制的XBsonArray实例
* @return 成功返回新的XBsonArray指针，失败返回NULL
*/
XBsonArray* XBsonArray_create_copy(const XBsonArray* other);
/**
* @brief 从另一个XBsonArray移动创建新实例（转移资源所有权）
* @param other 被移动的XBsonArray实例
* @return 成功返回新的XBsonArray指针，失败返回NULL
*/
XBsonArray* XBsonArray_create_move(XBsonArray* other);
/**
* @brief 初始化XBsonArray实例
* @param array 需要初始化的XBsonArray指针
*/
void XBsonArray_init(XBsonArray* array);
// 基础操作宏定义（映射到XVector操作）
/**
* @brief 获取指定索引的元素，映射到XVector_at_base
* @param array XBsonArray实例指针
* @param index 元素索引
* @return 指向指定索引元素（XBsonValue）的指针
*/
#define XBsonArray_at_base							XVector_at_base
/**
* @brief 反向拷贝操作，映射到XVector_rcopy_base
*/
#define XBsonArray_rcopy_base						XVector_rcopy_base
/**
* @brief 拷贝操作，映射到XVector_copy_base
* @param dest 目标XBsonArray
* @param src 源XBsonArray
*/
#define XBsonArray_copy_base						XVector_copy_base	
/**
* @brief 移动操作，映射到XVector_move_base（转移资源所有权）
* @param dest 目标XBsonArray
* @param src 源XBsonArray
*/
#define XBsonArray_move_base						XVector_move_base	
/**
* @brief 反初始化操作，映射到XVector_deinit_base（释放内部资源，不释放实例本身）
* @param array XBsonArray实例指针
*/
#define XBsonArray_deinit_base						XVector_deinit_base	
/**
* @brief 销毁操作，映射到XVector_delete_base（释放内部资源及实例本身）
* @param array XBsonArray实例指针
*/
#define XBsonArray_delete_base						XVector_delete_base	
/**
* @brief 清空操作，映射到XVector_clear_base（移除所有元素，保留容量）
* @param array XBsonArray实例指针
*/
#define XBsonArray_clear_base						XVector_clear_base	
/**
* @brief 空检查操作，映射到XVector_isEmpty_base
* @param array XBsonArray实例指针
* @return 数组为空返回true，否则返回false
*/
#define XBsonArray_isEmpty_base						XVector_isEmpty_base	
/**
* @brief 获取元素数量，映射到XVector_size_base
* @param array XBsonArray实例指针
* @return 数组中元素的数量
*/
#define XBsonArray_size_base						XVector_size_base	
/**
* @brief 获取容量，映射到XVector_capacity_base
* @param array XBsonArray实例指针
* @return 数组当前的容量（可容纳的元素数量）
*/
#define XBsonArray_capacity_base					XVector_capacity_base
/**
* @brief 交换两个数组的内容，映射到XVector_swap_base
* @param a 第一个XBsonArray实例
* @param b 第二个XBsonArray实例
*/
#define XBsonArray_swap_base						XVector_swap_base	
/**
* @brief 获取元素类型大小，映射到XVector_typeSize_base
* @return 单个元素（XBsonValue）的大小（字节数）
*/
#define XBsonArray_typeSize_base					XVector_typeSize_base
/**
* @brief 计数操作，映射到XVector_count_base
* @param array XBsonArray实例指针
* @return 数组中元素的数量（同size_base）
*/
#define XBsonArray_count_base						XVector_count_base			
/**
* @brief 在数组末尾添加元素（拷贝），映射到XVector_append_base
* @param array XBsonArray实例指针
* @param value 要添加的XBsonValue元素（将被拷贝）
* @return 添加成功返回true，失败返回false
*/
#define XBsonArray_append_base						XVector_append_base			
/**
* @brief 在数组末尾添加元素（移动），映射到XVector_append_move_base
* @param array XBsonArray实例指针
* @param value 要添加的XBsonValue元素（所有权转移）
* @return 添加成功返回true，失败返回false
*/
#define XBsonArray_append_move_base					XVector_append_move_base
/**
* @brief 在数组开头添加元素（拷贝），映射到XVector_prepend_base
* @param array XBsonArray实例指针
* @param value 要添加的XBsonValue元素（将被拷贝）
* @return 添加成功返回true，失败返回false
*/
#define XBsonArray_prepend_base						XVector_prepend_base	
/**
* @brief 在数组开头添加元素（移动），映射到XVector_prepend_move_base
* @param array XBsonArray实例指针
* @param value 要添加的XBsonValue元素（所有权转移）
* @return 添加成功返回true，失败返回false
*/
#define XBsonArray_prepend_move_base				XVector_prepend_move_base
/**
* @brief 在指定索引插入元素（拷贝），映射到XVector_insert
* @param array XBsonArray实例指针
* @param index 插入位置索引
* @param value 要插入的XBsonValue元素（将被拷贝）
* @return 插入成功返回true，失败返回false
*/
#define XBsonArray_insert                           XVector_insert
/**
* @brief 在指定索引插入元素（移动），映射到XVector_insert_move
* @param array XBsonArray实例指针
* @param index 插入位置索引
* @param value 要插入的XBsonValue元素（所有权转移）
* @return 插入成功返回true，失败返回false
*/
#define XBsonArray_insert_move                      XVector_insert_move
/**
* @brief 移除指定索引的元素，映射到XVector_removeAt_base
* @param array XBsonArray实例指针
* @param index 要移除的元素索引
* @return 移除成功返回true，失败返回false
*/
#define XBsonArray_removeAt_base                    XVector_removeAt_base
/**
* @brief 替换指定索引的元素（拷贝），映射到XVector_replace
* @param array XBsonArray实例指针
* @param index 要替换的元素索引
* @param value 新的XBsonValue元素（将被拷贝）
* @return 替换成功返回true，失败返回false
*/
#define XBsonArray_replace                          XVector_replace
/**
* @brief 替换指定索引的元素（移动），映射到XVector_replace_move
* @param array XBsonArray实例指针
* @param index 要替换的元素索引
* @param value 新的XBsonValue元素（所有权转移）
* @return 替换成功返回true，失败返回false
*/
#define XBsonArray_replace_move                     XVector_replace_move 
// 转换函数
/**
* @brief 将XBsonArray转换为XJsonArray
* @param bson_arr 要转换的XBsonArray实例
* @return 成功返回XJsonArray指针，失败返回NULL
*/
XJsonArray* XBsonArray_toJsonArray(const XBsonArray* bson_arr);
/**
* @brief 从XJsonArray创建XBsonArray
* @param json_arr 要转换的XJsonArray实例
* @return 成功返回XBsonArray指针，失败返回NULL
*/
XBsonArray* XBsonArray_fromJsonArray(const XJsonArray* json_arr);
// 序列化与反序列化
/**
* @brief 将XBsonArray序列化为BSON格式的XByteArray
* @param array 要序列化的XBsonArray实例
* @return 成功返回XByteArray指针，失败返回NULL
*/
XByteArray* XBsonArray_toBson(const XBsonArray* array);
/**
* @brief 从BSON格式的XByteArray反序列化创建XBsonArray
* @param data 包含BSON数据的XByteArray
* @return 成功返回XBsonArray指针，失败返回NULL
*/
XBsonArray* XBsonArray_fromBson(XByteArray* data);
/**
* @brief 将XBsonArray转换为BSON格式的字节数组（XByteArray）
* @param array 要转换的XBsonArray实例
* @return 成功返回XByteArray指针，失败返回NULL
*/
XByteArray* XBsonArray_to_bytes(const XBsonArray* array);
/**
* @brief 从字节数据初始化XBsonArray
* @param array 要初始化的XBsonArray实例
* @param data 字节数据指针
* @param size 字节数据大小
* @return 初始化成功返回true，失败返回false
*/
bool XBsonArray_from_bytes(XBsonArray* array, const uint8_t* data, size_t size);
// 与XVariantList转换
/**
* @brief 将XBsonArray转换为XVariantList（深拷贝）
* @param array 要转换的XBsonArray实例
* @return 成功返回XVariantList指针，失败返回NULL
*/
XVariantList* XBsonArray_toVariantList(const XBsonArray* array);
/**
* @brief 将XBsonArray移动转换为XVariantList（转移资源所有权）
* @param array 要转换的XBsonArray实例
* @return 成功返回XVariantList指针，失败返回NULL
*/
XVariantList* XBsonArray_toVariantList_move(XBsonArray* array);
// 与XVariant转换
/**
* @brief 将XBsonArray转换为XVariant（深拷贝）
* @param array 要转换的XBsonArray实例
* @return 成功返回XVariant指针，失败返回NULL
*/
XVariant* XBsonArray_toVariant(const XBsonArray* array);
/**
* @brief 将XBsonArray移动转换为XVariant（转移资源所有权）
* @param array 要转换的XBsonArray实例
* @return 成功返回XVariant指针，失败返回NULL
*/
XVariant* XBsonArray_toVariant_move(XBsonArray* array);
/**
* @brief 将XBsonArray转换为XVariant（引用形式，不转移所有权）
* @param array 要转换的XBsonArray实例
* @return 成功返回XVariant指针，失败返回NULL
*/
XVariant* XBsonArray_toVariant_ref(XBsonArray* array);
#ifdef __cplusplus
}
#endif
#endif // XBSONARRAY_H