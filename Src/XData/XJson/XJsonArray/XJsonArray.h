#ifndef XJSONARRAY_H
#define XJSONARRAY_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XJson.h"
#include "XVector.h"
#if !XVector_ON
#error "XJsonArray requires XVector to be enabled in CXinYueConfig.h"
#endif
/**
* @brief JSON数组结构体
* @details 封装JSON数组功能，内部通过XVector存储XJsonValue元素，支持动态扩容和各类数组操作
*/
typedef struct XJsonArray
{
	XVector elements; ///< 存储XJsonValue类型元素的向量容器
} XJsonArray;
// 构造与析构函数
/**
* @brief 创建一个空的XJsonArray实例
* @return 成功返回XJsonArray指针，失败返回NULL
*/
XJsonArray* XJsonArray_create();
/**
* @brief 通过深拷贝创建XJsonArray实例
* @param copy 被拷贝的XJsonArray实例
* @return 成功返回新的XJsonArray指针，失败返回NULL
*/
XJsonArray* XJsonArray_create_copy(XJsonArray* copy);
/**
* @brief 通过资源移动创建XJsonArray实例（转移源实例的资源所有权）
* @param move 被移动的XJsonArray实例
* @return 成功返回新的XJsonArray指针，失败返回NULL
*/
XJsonArray* XJsonArray_create_move(XJsonArray* move);
/**
* @brief 初始化XJsonArray实例
* @param array 需要初始化的XJsonArray指针
* @details 初始化内部向量容器，设置数据操作回调（拷贝、移动、反初始化）
*/
void XJsonArray_init(XJsonArray* array);
// 基础操作宏（基于XVector）
/**
* @brief 基于XVector的反向拷贝基础操作
* @details 调用XVector的反向拷贝接口实现数组元素的反向拷贝
*/
#define XJsonArray_rcopy_base						XVector_rcopy_base
/**
* @brief 基于XVector的拷贝基础操作
* @details 调用XVector的拷贝接口实现数组元素的深拷贝
*/
#define XJsonArray_copy_base						XVector_copy_base	
/**
* @brief 基于XVector的移动基础操作
* @details 调用XVector的移动接口实现数组资源的所有权转移
*/
#define XJsonArray_move_base						XVector_move_base	
/**
* @brief 基于XVector的反初始化基础操作
* @details 调用XVector的反初始化接口，释放内部元素资源但保留数组实例
*/
#define XJsonArray_deinit_base						XVector_deinit_base	
/**
* @brief 基于XVector的销毁基础操作
* @details 调用XVector的销毁接口，释放内部元素资源及数组实例本身
*/
#define XJsonArray_delete_base						XVector_delete_base	
/**
* @brief 基于XVector的清空基础操作
* @details 调用XVector的清空接口，移除所有元素并释放其资源，保留数组容量
*/
#define XJsonArray_clear_base						XVector_clear_base	
/**
* @brief 基于XVector的判空基础操作
* @details 调用XVector的判空接口，判断数组是否为空（元素数量为0）
* @return 为空返回true，否则返回false
*/
#define XJsonArray_isEmpty_base						XVector_isEmpty_base	
/**
* @brief 基于XVector的大小基础操作
* @details 调用XVector的大小接口，获取数组当前元素数量
* @return 返回元素数量
*/
#define XJsonArray_size_base						XVector_size_base	
/**
* @brief 基于XVector的容量基础操作
* @details 调用XVector的容量接口，获取数组当前可容纳的最大元素数量
* @return 返回容量大小
*/
#define XJsonArray_capacity_base					XVector_capacity_base
/**
* @brief 基于XVector的交换基础操作
* @details 调用XVector的交换接口，交换两个数组的元素资源
*/
#define XJsonArray_swap_base						XVector_swap_base	
/**
* @brief 基于XVector的类型大小基础操作
* @details 调用XVector的类型大小接口，获取数组中单个元素的类型大小（XJsonValue的大小）
* @return 返回单个元素的字节大小
*/
#define XJsonArray_typeSize_base					XVector_typeSize_base
/**
* @brief 基于XVector的计数基础操作
* @details 调用XVector的计数接口，获取数组元素数量（同size_base）
* @return 返回元素数量
*/
#define XJsonArray_count_base						XVector_count_base			
/**
* @brief 基于XVector的尾部添加基础操作（深拷贝）
* @details 调用XVector的尾部添加接口，深拷贝元素到数组末尾
*/
#define XJsonArray_append_base						XVector_append_1_base
/**
* @brief 基于XVector的尾部添加基础操作（转移所有权）
* @details 调用XVector的尾部添加接口，将元素所有权转移到数组末尾
*/
#define XJsonArray_append_move_base					XVector_append_move_1
/**
* @brief 基于XVector的头部添加基础操作（深拷贝）
* @details 调用XVector的头部添加接口，深拷贝元素到数组开头
*/
#define XJsonArray_prepend_base						XVector_prepend_1_base	
/**
* @brief 基于XVector的头部添加基础操作（转移所有权）
* @details 调用XVector的头部添加接口，将元素所有权转移到数组开头
*/
#define XJsonArray_prepend_move_base				XVector_prepend_move_1_base
/**
* @brief 基于XVector的插入操作（深拷贝）
* @details 调用XVector的插入接口，在指定索引位置深拷贝插入元素
* @param array 目标XJsonArray实例
* @param index 插入位置索引
* @param value 待插入的XJsonValue元素（将被拷贝）
*/
#define XJsonArray_insert                           XVector_insert_2
/**
* @brief 基于XVector的插入操作（转移所有权）
* @details 调用XVector的插入接口，在指定索引位置转移插入元素所有权
* @param array 目标XJsonArray实例
* @param index 插入位置索引
* @param value 待插入的XJsonValue元素（所有权将被转移）
*/
#define XJsonArray_insert_move                      XVector_insert_move_2
/**
* @brief 基于XVector的指定位置移除基础操作
* @details 调用XVector的移除接口，移除指定索引位置的元素并释放其资源
* @param array 目标XJsonArray实例
* @param index 待移除元素的索引
*/
#define XJsonArray_removeAt_base                    XVector_removeAt_base
/**
* @brief 基于XVector的替换操作（深拷贝）
* @details 调用XVector的替换接口，用新元素深拷贝替换指定索引位置的元素
* @param array 目标XJsonArray实例
* @param index 待替换元素的索引
* @param value 替换用的XJsonValue元素（将被拷贝）
*/
#define XJsonArray_replace                          XVector_replace_1
/**
* @brief 基于XVector的替换操作（转移所有权）
* @details 调用XVector的替换接口，用新元素转移所有权替换指定索引位置的元素
* @param array 目标XJsonArray实例
* @param index 待替换元素的索引
* @param value 替换用的XJsonValue元素（所有权将被转移）
*/
#define XJsonArray_replace_move                     XVector_replace_move_1 
// 元素访问函数
/**
* @brief 获取指定索引位置的XJsonValue元素（可修改）
* @param array 目标XJsonArray实例
* @param index 元素索引（支持负索引，-1表示最后一个元素）
* @return 成功返回XJsonValue指针，索引无效返回NULL
*/
XJsonValue* XJsonArray_at(XJsonArray* array, int64_t index);
/**
* @brief 获取指定索引位置的XJsonValue元素（不可修改）
* @param array 目标XJsonArray实例
* @param index 元素索引（支持负索引，-1表示最后一个元素）
* @return 成功返回const XJsonValue指针，索引无效返回NULL
*/
const XJsonValue* XJsonArray_at_const(const XJsonArray* array, int64_t index);
/**
* @brief 获取数组第一个元素。
* @return 非空数组返回元素指针；空数组或参数无效时返回未定义值。
*/
XJsonValue* XJsonArray_first(const XJsonArray* array);
/**
* @brief 获取数组最后一个元素。
* @return 非空数组返回元素指针；空数组或参数无效时返回未定义值。
*/
XJsonValue* XJsonArray_last(const XJsonArray* array);
/**
* @brief 移除并返回指定位置元素的所有权。
* @param array 目标数组；@param index 元素索引，支持负索引。
*/
XJsonValue* XJsonArray_takeAt(XJsonArray* array, int64_t index);
/**
* @brief 判断数组是否包含与 value 相等的元素。
*/
bool XJsonArray_contains(const XJsonArray* array, const XJsonValue* value);
/**
* @brief 比较两个数组的元素序列是否相等。
*/
bool XJsonArray_equals(const XJsonArray* left, const XJsonArray* right);
/**
* @brief 从字符串列表深拷贝创建 JSON 字符串数组。
*/
XJsonArray* XJsonArray_fromStringList(const XStringList* list);
/**
* @brief 从 VariantList 深拷贝创建 JSON 数组。
*/
XJsonArray* XJsonArray_fromVariantList(const XVariantList* list);
// 转换函数
/**
* @brief 将XJsonArray序列化为XString
* @param array 目标XJsonArray实例
* @param format 序列化格式（缩进或紧凑）
* @return 成功返回序列化后的XString指针，失败返回NULL
*/
XString* XJsonArray_toString(const XJsonArray* array, XJsonDocumentFormat format);
/**
* @brief 将XJsonArray转换为XVariantList（深拷贝）
* @param array 目标XJsonArray实例
* @return 成功返回XVariantList指针，失败返回NULL
*/
XVariantList* XJsonArray_toVariantList(const XJsonArray* array);
/**
* @brief 将XJsonArray转换为XVariantList（转移所有权）
* @param array 目标XJsonArray实例
* @return 成功返回XVariantList指针，失败返回NULL
*/
XVariantList* XJsonArray_toVariantList_move(XJsonArray* array);
/**
* @brief 将XJsonArray转换为XVariant（深拷贝）
* @param array 目标XJsonArray实例
* @return 成功返回XVariant指针（类型为XVariantType_JsonArray），失败返回NULL
*/
XVariant* XJsonArray_toVariant(const XJsonArray* array);
/**
* @brief 将XJsonArray转换为XVariant（转移所有权）
* @param array 目标XJsonArray实例
* @return 成功返回XVariant指针（类型为XVariantType_JsonArray），失败返回NULL
*/
XVariant* XJsonArray_toVariant_move(XJsonArray* array);
/**
* @brief 将XJsonArray转换为XVariant（引用类型）
* @param array 目标XJsonArray实例
* @return 成功返回XVariant指针（引用原数组，不转移所有权），失败返回NULL
*/
XVariant* XJsonArray_toVariant_ref(XJsonArray* array);
#ifdef __cplusplus
}
#endif
#endif // XJSONARRAY_H
