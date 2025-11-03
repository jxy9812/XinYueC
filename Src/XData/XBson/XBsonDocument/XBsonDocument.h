#ifndef XBSONDOCUMENT_H
#define XBSONDOCUMENT_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XBson.h"
#include "XMap.h"
#include "XBsonValue.h"
/**
* @brief BSON文档结构体，用于存储键值对形式的BSON数据
* @details 内部通过XMap实现，键类型为XString，值类型为XBsonValue，支持多种BSON数据类型
*/
typedef struct XBsonDocument
{
	XMap members; // 键为XString，值为XBsonValue，存储文档中的键值对
} XBsonDocument;
// 构造与析构
/**
* @brief 创建一个空的XBsonDocument实例
* @return 成功返回XBsonDocument指针，失败返回NULL
*/
XBsonDocument* XBsonDocument_create();
/**
* @brief 从另一个XBsonDocument复制创建新实例（深拷贝）
* @param other 被复制的XBsonDocument实例
* @return 成功返回新的XBsonDocument指针，失败返回NULL
*/
XBsonDocument* XBsonDocument_create_copy(const XBsonDocument* other);
/**
* @brief 从另一个XBsonDocument移动创建新实例（转移资源所有权）
* @param other 被移动的XBsonDocument实例
* @return 成功返回新的XBsonDocument指针，失败返回NULL
*/
XBsonDocument* XBsonDocument_create_move(XBsonDocument* other);
/**
* @brief 初始化XBsonDocument实例
* @param doc 需要初始化的XBsonDocument指针
*/
void XBsonDocument_init(XBsonDocument* doc);
// 插入操作
/**
* @brief 向BSON文档中插入UTF-8字符串键和XBsonValue值（值为拷贝）
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @param value 要插入的XBsonValue值
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_value(XBsonDocument* doc, const char* key, XBsonValue* value);
/**
* @brief 向BSON文档中插入UTF-8字符串键和XBsonValue值（值为移动）
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @param value 要插入的XBsonValue值（所有权转移）
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_value_move(XBsonDocument* doc, const char* key, XBsonValue* value);
/**
* @brief 向BSON文档中插入UTF-8字符串键和double类型值
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @param d 要插入的double值
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_double(XBsonDocument* doc, const char* key, double d);
/**
* @brief 向BSON文档中插入UTF-8字符串键和int32_t类型值
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @param i 要插入的int32_t值
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_int32(XBsonDocument* doc, const char* key, int32_t i);
/**
* @brief 向BSON文档中插入UTF-8字符串键和int64_t类型值
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @param i 要插入的int64_t值
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_int64(XBsonDocument* doc, const char* key, int64_t i);
/**
* @brief 向BSON文档中插入UTF-8字符串键和XString类型值（值为拷贝）
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @param str 要插入的XString值
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_string(XBsonDocument* doc, const char* key, const XString* str);
/**
* @brief 向BSON文档中插入UTF-8字符串键和XString类型值（值为移动）
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @param str 要插入的XString值（所有权转移）
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_string_move(XBsonDocument* doc, const char* key, XString* str);
/**
* @brief 向BSON文档中插入UTF-8字符串键和UTF-8字符串值
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @param utf8 要插入的UTF-8字符串
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_utf8(XBsonDocument* doc, const char* key, const char* utf8);
/**
* @brief 向BSON文档中插入UTF-8字符串键和NULL值
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_null(XBsonDocument* doc, const char* key);
/**
* @brief 向BSON文档中插入UTF-8字符串键和bool类型值
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @param b 要插入的bool值
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_bool(XBsonDocument* doc, const char* key, bool b);
/**
* @brief 向BSON文档中插入UTF-8字符串键和XBsonArray类型值（值为拷贝）
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @param array 要插入的XBsonArray值
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_array(XBsonDocument* doc, const char* key, const XBsonArray* array);
/**
* @brief 向BSON文档中插入UTF-8字符串键和XBsonArray类型值（值为移动）
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @param array 要插入的XBsonArray值（所有权转移）
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_array_move(XBsonDocument* doc, const char* key, XBsonArray* array);
/**
* @brief 向BSON文档中插入UTF-8字符串键和XBsonDocument类型值（值为拷贝）
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @param newDoc 要插入的XBsonDocument值
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_document(XBsonDocument* doc, const char* key, const XBsonDocument* newDoc);
/**
* @brief 向BSON文档中插入UTF-8字符串键和XBsonDocument类型值（值为移动）
* @param doc 目标BSON文档
* @param key UTF-8格式的键字符串
* @param newDoc 要插入的XBsonDocument值（所有权转移）
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_keyUtf8_document_move(XBsonDocument* doc, const char* key, XBsonDocument* newDoc);
/**
* @brief 向BSON文档中插入XString键和XBsonValue值（值为移动）
* @param doc 目标BSON文档
* @param key XString类型的键
* @param value 要插入的XBsonValue值（所有权转移）
* @return 插入成功返回true，失败返回false
*/
bool XBsonDocument_insert_value_move(XBsonDocument* doc, const XString* key, XBsonValue* value);
// 删除操作
/**
* @brief 从BSON文档中删除指定UTF-8字符串键对应的键值对
* @param doc 目标BSON文档
* @param key 要删除的UTF-8格式键字符串
* @return 删除成功返回true，失败返回false
*/
bool XBsonDocument_remove_keyUtf8(XBsonDocument* doc, const char* key);
// 基础操作宏定义（映射到XMap操作）
/**
* @brief 基础插入操作，映射到XMap_insert_base（键为XString*，值为XBsonValue*）
*/
#define XBsonDocument_insert_base				    XMap_insert_base
/**
* @brief 基础移动插入操作，映射到XMap_insert_move_base（键为XString*，值为XBsonValue*）
*/
#define XBsonDocument_insert_move_base		        XMap_insert_move_base
/**
* @brief 基础插入操作（大写命名），映射到XMap_Insert_Base
*/
#define XBsonDocument_Insert_Base				    XMap_Insert_Base
/**
* @brief 基础擦除操作，映射到XMap_erase_base
*/
#define XBsonDocument_erase_base					XMap_erase_base
/**
* @brief 基础删除操作，映射到XMap_remove_base
*/
#define XBsonDocument_remove_base				    XMap_remove_base
/**
* @brief 基础删除操作（大写命名），映射到XMap_Remove_Base
*/
#define XBsonDocument_Remove_Base				    XMap_Remove_Base
/**
* @brief 基础值获取操作，映射到XMap_value_base
*/
#define XBsonDocument_value_base					XMap_value_base
/**
* @brief 基础值获取操作（大写命名），映射到XMap_Value_Base
*/
#define XBsonDocument_Value_Base					XMap_Value_Base
/**
* @brief 基础查找操作，映射到XMap_find_base
*/
#define XBsonDocument_find_base					    XMap_find_base
/**
* @brief 包含性检查操作，映射到XMap_contains
*/
#define XBsonDocument_contains					    XMap_contains
/**
* @brief 基础键集合获取操作，映射到XMap_keys_base
*/
#define XBsonDocument_keys_base					    XMap_keys_base
/**
* @brief 基础拷贝操作，映射到XMap_copy_base
*/
#define XBsonDocument_copy_base					    XMap_copy_base	
/**
* @brief 基础移动操作，映射到XMap_move_base
*/
#define XBsonDocument_move_base					    XMap_move_base	
/**
* @brief 基础反初始化操作，映射到XMap_deinit_base
*/
#define XBsonDocument_deinit_base				    XMap_deinit_base	
/**
* @brief 基础销毁操作，映射到XMap_delete_base
*/
#define XBsonDocument_delete_base				    XMap_delete_base	
/**
* @brief 基础清空操作，映射到XMap_clear_base
*/
#define XBsonDocument_clear_base					XMap_clear_base	
/**
* @brief 基础空检查操作，映射到XMap_isEmpty_base
*/
#define XBsonDocument_isEmpty_base				    XMap_isEmpty_base	
/**
* @brief 基础大小获取操作，映射到XMap_size_base
*/
#define XBsonDocument_size_base					    XMap_size_base	
/**
* @brief 基础容量获取操作，映射到XMap_capacity_base
*/
#define XBsonDocument_capacity_base				    XMap_capacity_base
/**
* @brief 基础交换操作，映射到XMap_swap_base
*/
#define XBsonDocument_swap_base					    XMap_swap_base	
/**
* @brief 基础类型大小获取操作，映射到XMap_typeSize_base
*/
#define XBsonDocument_typeSize_base				    XMap_typeSize_base
// 转换函数
/**
* @brief 将XBsonDocument转换为XJsonObject
* @param bson_obj 要转换的XBsonDocument实例
* @return 成功返回XJsonObject指针，失败返回NULL
*/
XJsonObject* XBsonDocument_toJsonObject(const XBsonDocument* bson_obj);
/**
* @brief 从XJsonObject创建XBsonDocument
* @param json_obj 要转换的XJsonObject实例
* @return 成功返回XBsonDocument指针，失败返回NULL
*/
XBsonDocument* XBsonDocument_fromJsonObject(const XJsonObject* json_obj);
/**
* @brief 将XBsonDocument转换为JSON格式的XByteArray（UTF-8编码）
* @param bson_doc 要转换的XBsonDocument实例
* @param format JSON文档的格式化方式
* @return 成功返回XByteArray指针，失败返回NULL
*/
XByteArray* XBsonDocument_toJson(const XBsonDocument* bson_doc, XJsonDocumentFormat format);
/**
* @brief 将XBsonDocument转换为JSON格式的XString（UTF-8编码）
* @param bson_doc 要转换的XBsonDocument实例
* @param format JSON文档的格式化方式
* @return 成功返回XString指针，失败返回NULL
*/
XString* XBsonDocument_toJson_string(const XBsonDocument* bson_doc, XJsonDocumentFormat format);
// 序列化与反序列化
/**
* @brief 将XBsonDocument序列化为BSON格式的XByteArray
* @param doc 要序列化的XBsonDocument实例
* @return 成功返回XByteArray指针，失败返回NULL
*/
XByteArray* XBsonDocument_toBson(const XBsonDocument* doc);
/**
* @brief 从BSON格式的XByteArray反序列化创建XBsonDocument
* @param data 包含BSON数据的XByteArray
* @return 成功返回XBsonDocument指针，失败返回NULL
*/
XBsonDocument* XBsonDocument_fromBson(XByteArray* data);
/**
* @brief 从字节数据初始化XBsonDocument
* @param doc 要初始化的XBsonDocument实例
* @param data 字节数据指针
* @param size 字节数据大小
* @return 初始化成功返回true，失败返回false
*/
bool XBsonDocument_from_bytes(XBsonDocument* doc, const uint8_t* data, size_t size);
// 与XVariant转换
/**
* @brief 将XBsonDocument转换为XVariantMap
* @param doc 要转换的XBsonDocument实例
* @return 成功返回XVariantMap指针，失败返回NULL
*/
XVariantMap* XBsonDocument_toVariantMap(const XBsonDocument* doc);
/**
* @brief 将XBsonDocument移动转换为XVariantMap（转移资源所有权）
* @param doc 要转换的XBsonDocument实例
* @return 成功返回XVariantMap指针，失败返回NULL
*/
XVariantMap* XBsonDocument_toVariantMap_move(XBsonDocument* doc);
/**
* @brief 将XBsonDocument转换为XVariant
* @param doc 要转换的XBsonDocument实例
* @return 成功返回XVariant指针，失败返回NULL
*/
XVariant* XBsonDocument_toVariant(const XBsonDocument* doc);
/**
* @brief 将XBsonDocument移动转换为XVariant（转移资源所有权）
* @param doc 要转换的XBsonDocument实例
* @return 成功返回XVariant指针，失败返回NULL
*/
XVariant* XBsonDocument_toVariant_move(XBsonDocument* doc);
/**
* @brief 将XBsonDocument转换为XVariant（引用形式）
* @param doc 要转换的XBsonDocument实例
* @return 成功返回XVariant指针，失败返回NULL
*/
XVariant* XBsonDocument_toVariant_ref(XBsonDocument* doc);
#ifdef __cplusplus
}
#endif
#endif // XBSONOBJECT_H