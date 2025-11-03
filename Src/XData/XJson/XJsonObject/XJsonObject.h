#ifndef XJSONOBJECT_H
#define XJSONOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XJson.h"
#include "XJsonValue.h"
#include "XMap.h"
/**
* @brief 检查XMap模块是否启用，未启用则报错
* @details XJsonObject依赖XMap实现键值对存储，需在CXinYueConfig.h中开启XMap_ON
*/
#if !XMap_ON
#error "XJsonObject requires XMap to be enabled in CXinYueConfig.h"
#endif
/**
* @brief JSON对象结构体
* @details 内部使用XMap存储键值对，键为XString类型，值为XJsonValue类型
*/
typedef struct XJsonObject
{
	XMap members; ///< 存储JSON对象的键值对集合（键：XString，值：XJsonValue）
} XJsonObject;
// 构造与析构函数
/**
* @brief 创建一个空的XJsonObject实例
* @return 成功返回XJsonObject指针，失败返回NULL
*/
XJsonObject* XJsonObject_create(void);
/**
* @brief 通过深拷贝创建XJsonObject实例
* @param copy 被拷贝的XJsonObject实例
* @return 成功返回新的XJsonObject指针，失败返回NULL
*/
XJsonObject* XJsonObject_create_copy(XJsonObject* copy);
/**
* @brief 通过资源移动创建XJsonObject实例（转移源实例的资源所有权）
* @param move 被移动的XJsonObject实例
* @return 成功返回新的XJsonObject指针，失败返回NULL
*/
XJsonObject* XJsonObject_create_move(XJsonObject* move);
/**
* @brief 初始化XJsonObject实例
* @param object 需要初始化的XJsonObject指针
* @details 初始化内部XMap结构，设置键值对的拷贝、移动和销毁方法
*/
void XJsonObject_init(XJsonObject* object);
// 插入操作
/**
* @brief 向JSON对象插入UTF-8键和XJsonValue值（深拷贝值）
* @param object 目标XJsonObject指针
* @param key UTF-8编码的键字符串
* @param value 要插入的XJsonValue值（会被深拷贝）
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_keyUtf8_value(XJsonObject* object, const char* key, XJsonValue* value);
/**
* @brief 向JSON对象插入UTF-8键和XJsonValue值（移动值的所有权）
* @param object 目标XJsonObject指针
* @param key UTF-8编码的键字符串
* @param value 要插入的XJsonValue值（所有权转移给对象）
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_keyUtf8_value_move(XJsonObject* object, const char* key, XJsonValue* value);
/**
* @brief 向JSON对象插入UTF-8键和double值
* @param object 目标XJsonObject指针
* @param key UTF-8编码的键字符串
* @param d 要插入的double值
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_keyUtf8_double(XJsonObject* object, const char* key, double d);
/**
* @brief 向JSON对象插入UTF-8键和int64_t值
* @param object 目标XJsonObject指针
* @param key UTF-8编码的键字符串
* @param i 要插入的int64_t值
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_keyUtf8_int(XJsonObject* object, const char* key, int64_t i);
/**
* @brief 向JSON对象插入UTF-8键和XString值（深拷贝字符串）
* @param object 目标XJsonObject指针
* @param key UTF-8编码的键字符串
* @param str 要插入的XString值（会被深拷贝）
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_keyUtf8_string(XJsonObject* object, const char* key, const XString* str);
/**
* @brief 向JSON对象插入UTF-8键和XString值（移动字符串所有权）
* @param object 目标XJsonObject指针
* @param key UTF-8编码的键字符串
* @param str 要插入的XString值（所有权转移给对象）
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_keyUtf8_string_move(XJsonObject* object, const char* key, XString* str);
/**
* @brief 向JSON对象插入UTF-8键和UTF-8字符串值
* @param object 目标XJsonObject指针
* @param key UTF-8编码的键字符串
* @param utf8 要插入的UTF-8字符串（会被转换为XString存储）
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_keyUtf8_utf8(XJsonObject* object, const char* key, const char* utf8);
/**
* @brief 向JSON对象插入UTF-8键和Null值
* @param object 目标XJsonObject指针
* @param key UTF-8编码的键字符串
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_keyUtf8_null(XJsonObject* object, const char* key);
/**
* @brief 向JSON对象插入UTF-8键和bool值
* @param object 目标XJsonObject指针
* @param key UTF-8编码的键字符串
* @param b 要插入的bool值
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_keyUtf8_bool(XJsonObject* object, const char* key, bool b);
/**
* @brief 向JSON对象插入UTF-8键和XJsonArray值（深拷贝数组）
* @param object 目标XJsonObject指针
* @param key UTF-8编码的键字符串
* @param array 要插入的XJsonArray值（会被深拷贝）
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_keyUtf8_array(XJsonObject* object, const char* key, const XJsonArray* array);
/**
* @brief 向JSON对象插入UTF-8键和XJsonArray值（移动数组所有权）
* @param object 目标XJsonObject指针
* @param key UTF-8编码的键字符串
* @param array 要插入的XJsonArray值（所有权转移给对象）
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_keyUtf8_array_move(XJsonObject* object, const char* key, XJsonArray* array);
/**
* @brief 向JSON对象插入UTF-8键和XJsonObject值（深拷贝对象）
* @param object 目标XJsonObject指针
* @param key UTF-8编码的键字符串
* @param value 要插入的XJsonObject值（会被深拷贝）
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_keyUtf8_object(XJsonObject* object, const char* key, const XJsonObject* value);
/**
* @brief 向JSON对象插入UTF-8键和XJsonObject值（移动对象所有权）
* @param object 目标XJsonObject指针
* @param key UTF-8编码的键字符串
* @param value 要插入的XJsonObject值（所有权转移给对象）
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_keyUtf8_object_move(XJsonObject* object, const char* key, XJsonObject* value);
/**
* @brief 向JSON对象插入XString键和XJsonValue值（移动值的所有权）
* @param object 目标XJsonObject指针
* @param key XString类型的键（会被拷贝）
* @param value 要插入的XJsonValue值（所有权转移给对象）
* @return 插入成功返回true，失败返回false（参数无效时）
*/
bool XJsonObject_insert_value_move(XJsonObject* object, const XString* key, XJsonValue* value);
// 删除操作
/**
* @brief 从JSON对象中删除指定UTF-8键对应的键值对
* @param object 目标XJsonObject指针
* @param key 要删除的UTF-8编码的键字符串
* @return 删除成功返回true，失败返回false（键不存在或参数无效时）
*/
bool XJsonObject_remove_keyUtf8(XJsonObject* object, const char* key);
// 基础操作宏定义（封装XMap的接口）
/**
* @brief 插入键值对（基础接口，键为XString*，值为XJsonValue*，深拷贝）
* @details 直接调用XMap_insert_base，用于底层键值对插入
*/
#define XJsonObject_insert_base				    XMap_insert_base
/**
* @brief 插入键值对（基础接口，键为XString*，值为XJsonValue*，移动语义）
* @details 直接调用XMap_insert_move_base，转移值的所有权
*/
#define XJsonObject_insert_move_base		    XMap_insert_move_base
/**
* @brief 插入键值对（基础接口，大写命名风格，同XJsonObject_insert_base）
*/
#define XJsonObject_Insert_Base				    XMap_Insert_Base
/**
* @brief 擦除键值对（基础接口，通过迭代器）
* @details 直接调用XMap_erase_base，用于通过迭代器删除键值对
*/
#define XJsonObject_erase_base					XMap_erase_base
/**
* @brief 删除指定键的键值对（基础接口，键为XString*）
* @details 直接调用XMap_remove_base，删除指定键对应的键值对
*/
#define XJsonObject_remove_base				    XMap_remove_base
/**
* @brief 删除指定键的键值对（基础接口，大写命名风格，同XJsonObject_remove_base）
*/
#define XJsonObject_Remove_Base				    XMap_Remove_Base
/**
* @brief 获取指定键的值（基础接口，键为XString*）
* @details 直接调用XMap_value_base，返回键对应的XJsonValue*
*/
#define XJsonObject_value_base					XMap_value_base
/**
* @brief 获取指定键的值（基础接口，大写命名风格，同XJsonObject_value_base）
*/
#define XJsonObject_Value_Base					XMap_Value_Base
/**
* @brief 查找指定键的迭代器（基础接口，键为XString*）
* @details 直接调用XMap_find_base，返回键对应的迭代器
*/
#define XJsonObject_find_base					XMap_find_base
/**
* @brief 检查是否包含指定键（键为XString*）
* @details 直接调用XMap_contains，返回键是否存在
*/
#define XJsonObject_contains					XMap_contains
/**
* @brief 获取所有键的集合（基础接口）
* @details 直接调用XMap_keys_base，返回包含所有键的集合
*/
#define XJsonObject_keys_base					XMap_keys_base
/**
* @brief 深拷贝XJsonObject（基础接口）
* @details 直接调用XMap_copy_base，拷贝整个键值对集合
*/
#define XJsonObject_copy_base					XMap_copy_base	
/**
* @brief 移动XJsonObject资源（基础接口）
* @details 直接调用XMap_move_base，转移资源所有权
*/
#define XJsonObject_move_base					XMap_move_base	
/**
* @brief 反初始化XJsonObject（基础接口，释放内部资源，不释放实例本身）
* @details 直接调用XMap_deinit_base
*/
#define XJsonObject_deinit_base				    XMap_deinit_base	
/**
* @brief 销毁XJsonObject（基础接口，释放内部资源及实例本身）
* @details 直接调用XMap_delete_base
*/
#define XJsonObject_delete_base				    XMap_delete_base	
/**
* @brief 清空XJsonObject的所有键值对（保留实例）
* @details 直接调用XMap_clear_base
*/
#define XJsonObject_clear_base					XMap_clear_base	
/**
* @brief 检查XJsonObject是否为空（无键值对）
* @details 直接调用XMap_isEmpty_base，返回是否为空
*/
#define XJsonObject_isEmpty_base				XMap_isEmpty_base	
/**
* @brief 获取XJsonObject中键值对的数量
* @details 直接调用XMap_size_base，返回键值对数量
*/
#define XJsonObject_size_base					XMap_size_base	
/**
* @brief 获取XJsonObject的容量（预分配的存储空间）
* @details 直接调用XMap_capacity_base，返回容量大小
*/
#define XJsonObject_capacity_base				XMap_capacity_base
/**
* @brief 交换两个XJsonObject的内容
* @details 直接调用XMap_swap_base，交换内部键值对集合
*/
#define XJsonObject_swap_base					XMap_swap_base	
/**
* @brief 获取XJsonObject中元素的类型大小
* @details 直接调用XMap_typeSize_base，返回元素类型大小
*/
#define XJsonObject_typeSize_base				XMap_typeSize_base
// 转换函数
/**
* @brief 将XJsonObject序列化为XString
* @param object 要序列化的XJsonObject指针
* @param format 序列化格式（缩进或紧凑）
* @return 成功返回序列化后的XString指针，失败返回NULL
*/
XString* XJsonObject_toString(const XJsonObject* object, XJsonDocumentFormat format);
/**
* @brief 将XJsonObject序列化为UTF-8编码的XByteArray（适合网络传输）
* @param object 要序列化的XJsonObject指针
* @param format 序列化格式（缩进或紧凑）
* @return 成功返回序列化后的XByteArray指针，失败返回NULL
*/
XByteArray* XJsonObject_toJson(const XJsonObject* object, XJsonDocumentFormat format);
/**
* @brief 将XJsonObject转换为XVariantMap（深拷贝）
* @param object 要转换的XJsonObject指针
* @return 成功返回XVariantMap指针，失败返回NULL
*/
XVariantMap* XJsonObject_toVariantMap(const XJsonObject* object);
/**
* @brief 将XJsonObject转换为XVariantMap（移动资源所有权）
* @param object 要转换的XJsonObject指针（所有权转移）
* @return 成功返回XVariantMap指针，失败返回NULL
*/
XVariantMap* XJsonObject_toVariantMap_move(XJsonObject* object);
// 与XVariant转换
/**
* @brief 将XJsonObject转换为XVariant（深拷贝）
* @param object 要转换的XJsonObject指针
* @return 成功返回XVariant指针，失败返回NULL
*/
XVariant* XJsonObject_toVariant(const XJsonObject* object);
/**
* @brief 将XJsonObject转换为XVariant（移动资源所有权）
* @param object 要转换的XJsonObject指针（所有权转移）
* @return 成功返回XVariant指针，失败返回NULL
*/
XVariant* XJsonObject_toVariant_move(XJsonObject* object);
/**
* @brief 将XJsonObject转换为XVariant（引用形式，不转移所有权）
* @param object 要转换的XJsonObject指针
* @return 成功返回XVariant指针，失败返回NULL
*/
XVariant* XJsonObject_toVariant_ref(XJsonObject* object);
#ifdef __cplusplus
}
#endif
#endif // XJSONOBJECT_H