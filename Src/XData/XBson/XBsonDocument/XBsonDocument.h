#ifndef XBSONDOCUMENT_H
#define XBSONDOCUMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XBson.h"
#include "XVector.h"
#include "XBsonValue.h"

/**
 * @brief BSON 文档中的一个有序键值元素。
 * @details BSON 文档允许保存相同键名的多个元素；元素顺序就是 BSON
 *          字节流中的顺序。结构体由 XBsonDocument 拥有，调用者不能直接
 *          释放其成员。
 */
typedef struct XBsonElement
{
	XString m_key;       /**< UTF-8 BSON 键名；由元素拥有，键名不能包含 NUL。 */
	XBsonValue m_value;  /**< BSON 值；由元素拥有。 */
} XBsonElement;

/**
 * @brief BSON 文档容器。
 * @details 内部使用 XVector 保存 XBsonElement，保留元素顺序并允许重复键。
 *          文档的第一个成员是 XVector，可使用 XBsonDocument_*_base 容器
 *          生命周期和容量接口。
 */
typedef struct XBsonDocument
{
	XVector m_elements; /**< 有序 BSON 元素序列；仅通过公开 API 修改。 */
} XBsonDocument;

/** @brief 初始化 BSON 元素。 */
void XBsonElement_init(XBsonElement* element);
/** @brief 深拷贝 BSON 元素。 */
void XBsonElement_copy(XBsonElement* dest, const XBsonElement* src);
/** @brief 移动 BSON 元素所有权。 */
void XBsonElement_move(XBsonElement* dest, XBsonElement* src);
/** @brief 释放 BSON 元素内部资源但保留元素对象。 */
void XBsonElement_deinit(XBsonElement* element);

/** @brief 获取元素键名的只读借用指针。 */
const XString* XBsonElement_key_const(const XBsonElement* element);
/** @brief 获取元素值的只读借用指针。 */
const XBsonValue* XBsonElement_value_const(const XBsonElement* element);
/** @brief 获取元素值的可写借用指针。 */
XBsonValue* XBsonElement_value(XBsonElement* element);

/**
 * @brief 创建空 BSON 文档。
 * @return 新对象，失败返回 NULL；调用者使用 XBsonDocument_delete_base 释放。
 */
XBsonDocument* XBsonDocument_create(void);
/**
 * @brief 深拷贝创建 BSON 文档。
 * @param other 源文档，只读借用。
 * @return 新对象，失败返回 NULL。
 */
XBsonDocument* XBsonDocument_create_copy(const XBsonDocument* other);
/**
 * @brief 移动创建 BSON 文档。
 * @param other 源文档，资源转移后变为空文档。
 * @return 新对象，失败返回 NULL。
 */
XBsonDocument* XBsonDocument_create_move(XBsonDocument* other);
/** @brief 初始化栈上 BSON 文档。 */
void XBsonDocument_init(XBsonDocument* doc);

/**
 * @brief 在文档末尾追加元素并深拷贝键和值。
 * @param doc 目标文档。
 * @param key 键名，只读借用，不能为 NULL 或包含 NUL。
 * @param value BSON 值，只读借用。
 * @return 成功返回 true；参数、键名或内存分配失败返回 false，文档不变。
 */
bool XBsonDocument_append(XBsonDocument* doc, const XString* key,
                          const XBsonValue* value);
/**
 * @brief 在文档末尾追加元素并移动键和值。
 * @param doc 目标文档。
 * @param key 键名，资源转移后可安全 deinit。
 * @param value 值，资源转移后类型变为 0，可安全 deinit。
 * @return 成功返回 true；失败时文档不变，源参数可能已被清空。
 */
bool XBsonDocument_append_move(XBsonDocument* doc, XString* key,
                               XBsonValue* value);

/* 兼容原有基础接口：insert 在标准 BSON 文档中表示追加，不覆盖同名键。 */
bool XBsonDocument_insert_base(XBsonDocument* doc, const XString* key,
                               const XBsonValue* value);
bool XBsonDocument_insert_move_base(XBsonDocument* doc, XString* key,
                                    XBsonValue* value);
#define XBsonDocument_Insert_Base(doc,keyType,key,valType,value) \
	do { keyType _xbson_key = (key); valType _xbson_value = (value); \
	XBsonDocument_insert_base((doc), &_xbson_key, &_xbson_value); } while (0)

/** @brief 追加 UTF-8 键和 BSON 值的深拷贝。 */
bool XBsonDocument_insert_keyUtf8_value(XBsonDocument* doc, const char* key,
                                        XBsonValue* value);
/** @brief 追加 UTF-8 键并移动 BSON 值。 */
bool XBsonDocument_insert_keyUtf8_value_move(XBsonDocument* doc,
                                             const char* key,
                                             XBsonValue* value);
/** @brief 追加 double 元素。 */
bool XBsonDocument_insert_keyUtf8_double(XBsonDocument* doc, const char* key,
                                         double d);
/** @brief 追加 int32 元素。 */
bool XBsonDocument_insert_keyUtf8_int32(XBsonDocument* doc, const char* key,
                                        int32_t i);
/** @brief 追加 int64 元素。 */
bool XBsonDocument_insert_keyUtf8_int64(XBsonDocument* doc, const char* key,
                                        int64_t i);
/** @brief 追加字符串元素的深拷贝。 */
bool XBsonDocument_insert_keyUtf8_string(XBsonDocument* doc, const char* key,
                                         const XString* strValue);
/** @brief 追加字符串元素并移动源字符串。 */
bool XBsonDocument_insert_keyUtf8_string_move(XBsonDocument* doc,
                                              const char* key,
                                              XString* strValue);
/** @brief 追加 UTF-8 字符串元素。 */
bool XBsonDocument_insert_keyUtf8_utf8(XBsonDocument* doc, const char* key,
                                       const char* utf8);
/** @brief 追加 null 元素。 */
bool XBsonDocument_insert_keyUtf8_null(XBsonDocument* doc, const char* key);
/** @brief 追加 bool 元素。 */
bool XBsonDocument_insert_keyUtf8_bool(XBsonDocument* doc, const char* key,
                                       bool b);
/** @brief 追加数组元素的深拷贝。 */
bool XBsonDocument_insert_keyUtf8_array(XBsonDocument* doc, const char* key,
                                        const XBsonArray* array);
/** @brief 追加数组元素并移动源数组。 */
bool XBsonDocument_insert_keyUtf8_array_move(XBsonDocument* doc,
                                             const char* key,
                                             XBsonArray* array);
/** @brief 追加嵌套文档元素的深拷贝。 */
bool XBsonDocument_insert_keyUtf8_document(XBsonDocument* doc, const char* key,
                                           const XBsonDocument* newDoc);
/** @brief 追加嵌套文档元素并移动源文档。 */
bool XBsonDocument_insert_keyUtf8_document_move(XBsonDocument* doc,
                                                const char* key,
                                                XBsonDocument* newDoc);
/** @brief 追加 XString 键并移动 BSON 值，键本身会被拷贝。 */
bool XBsonDocument_insert_value_move(XBsonDocument* doc, const XString* key,
                                     XBsonValue* value);

/** @brief 获取指定索引的可写元素；越界返回 NULL。 */
XBsonElement* XBsonDocument_at_base(XBsonDocument* doc, int64_t index);
/** @brief 获取指定索引的只读元素；越界返回 NULL。 */
const XBsonElement* XBsonDocument_at_const(const XBsonDocument* doc, int64_t index);
/** @brief 获取第一个同名键的值借用指针；不存在返回 NULL。 */
XBsonValue* XBsonDocument_value_base(XBsonDocument* doc, const XString* key);
/** @brief 获取首个 UTF-8 同名键的值借用指针；不存在返回 NULL。 */
XBsonValue* XBsonDocument_value_keyUtf8(XBsonDocument* doc, const char* key);
/** @brief 返回第一个同名键的索引，不存在返回 -1。 */
int64_t XBsonDocument_indexOf(const XBsonDocument* doc, const XString* key);
/** @brief 返回 UTF-8 同名键的数量。 */
size_t XBsonDocument_count_keyUtf8(const XBsonDocument* doc, const char* key);
/** @brief 判断文档中是否存在指定 UTF-8 键。 */
bool XBsonDocument_contains_keyUtf8(const XBsonDocument* doc, const char* key);
/** @brief 删除第一个指定 UTF-8 键；不存在返回 false。 */
bool XBsonDocument_remove_keyUtf8(XBsonDocument* doc, const char* key);
/** @brief 删除指定 UTF-8 键的全部元素并返回删除数量。 */
size_t XBsonDocument_removeAll_keyUtf8(XBsonDocument* doc, const char* key);
/** @brief 删除指定索引元素。 */
bool XBsonDocument_removeAt(XBsonDocument* doc, int64_t index);
/** @brief 返回所有键的有序深拷贝向量，调用者使用 XVector_delete_base 释放。 */
XVector* XBsonDocument_keys_base(const XBsonDocument* doc);

/* 旧接口兼容别名。按键查找返回第一个元素索引，按索引擦除。 */
#define XBsonDocument_find_base XBsonDocument_indexOf
#define XBsonDocument_erase_base(doc,index) XBsonDocument_removeAt((doc),(index))
#define XBsonDocument_copy_base XVector_copy_base
#define XBsonDocument_move_base XVector_move_base
#define XBsonDocument_deinit_base XVector_deinit_base
#define XBsonDocument_delete_base XVector_delete_base
#define XBsonDocument_clear_base XVector_clear_base
#define XBsonDocument_isEmpty_base XVector_isEmpty_base
#define XBsonDocument_size_base XVector_size_base
#define XBsonDocument_capacity_base XVector_capacity_base
#define XBsonDocument_swap_base XVector_swap_base
#define XBsonDocument_typeSize_base XVector_typeSize_base

/** @brief 将 BSON 文档转换为 JSON 对象；重复键按最后一个值覆盖。 */
XJsonObject* XBsonDocument_toJsonObject(const XBsonDocument* bson_obj);
/** @brief 从 JSON 对象创建 BSON 文档；JSON 对象本身不包含重复键。 */
XBsonDocument* XBsonDocument_fromJsonObject(const XJsonObject* json_obj);
/** @brief 将 BSON 文档转换为 UTF-8 JSON 字节数组。 */
XByteArray* XBsonDocument_toJson(const XBsonDocument* bson_doc,
                                 XJsonDocumentFormat format);
/** @brief 将 BSON 文档转换为 UTF-8 JSON 字符串。 */
XString* XBsonDocument_toJson_string(const XBsonDocument* bson_doc,
                                     XJsonDocumentFormat format);

/** @brief 按 BSON 1.1 文档格式序列化；空文档也返回 5 字节文档。 */
XByteArray* XBsonDocument_toBson(const XBsonDocument* doc);
/** @brief 从完整 BSON 文档字节数组创建文档。 */
XBsonDocument* XBsonDocument_fromBson(XByteArray* data);
/**
 * @brief 从指定字节范围解析一个完整 BSON 文档。
 * @param doc 目标文档，成功前会先清空已有元素。
 * @param data BSON 起始地址。
 * @param size 字节数，必须等于 BSON 头部声明长度。
 * @return 成功返回 true；失败时目标文档保持为空。
 */
bool XBsonDocument_from_bytes(XBsonDocument* doc, const uint8_t* data,
                              size_t size);

/** @brief 转换为 XVariantMap；重复键按最后一个值保留。 */
XVariantMap* XBsonDocument_toVariantMap(const XBsonDocument* doc);
/** @brief 移动转换为 XVariantMap；重复键按最后一个值保留。 */
XVariantMap* XBsonDocument_toVariantMap_move(XBsonDocument* doc);
/** @brief 深拷贝转换为 XVariant。 */
XVariant* XBsonDocument_toVariant(const XBsonDocument* doc);
/** @brief 移动转换为 XVariant。 */
XVariant* XBsonDocument_toVariant_move(XBsonDocument* doc);
/** @brief 创建引用型 XVariant；引用对象必须在 Variant 存活期间保持有效。 */
XVariant* XBsonDocument_toVariant_ref(XBsonDocument* doc);

#ifdef __cplusplus
}
#endif

#endif /* XBSONDOCUMENT_H */
