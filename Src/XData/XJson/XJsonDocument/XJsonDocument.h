#ifndef XJSONDOCUMENT_H
#define XJSONDOCUMENT_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XJson.h"
#include "XJsonValue.h"
#include "XJsonObject.h"
#include "XJsonArray.h"
#include "XString.h"
#include "XByteArray.h"
#include "XClass.h"
/**
* @brief JSON文档结构体
* @details 封装JSON文档的根节点（可为对象、数组或其他JSON值），提供JSON的解析与生成功能
*/
typedef struct XJsonDocument {
	XClass m_class; ///< 文档对象的内存方法和堆对象标志
	XJsonValue root; ///< 文档的根节点值（支持对象、数组、基本类型等）
} XJsonDocument;
// 构造与析构函数
/**
* @brief 创建一个空的XJsonDocument实例
* @return 成功返回XJsonDocument指针，失败返回NULL
*/
XJsonDocument* XJsonDocument_create_ex(XMemoryType memory);
/**
* @brief 通过深拷贝创建XJsonDocument实例
* @param copy 被拷贝的XJsonDocument实例
* @return 成功返回新的XJsonDocument指针，失败返回NULL
*/
XJsonDocument* XJsonDocument_create_copy(XJsonDocument* copy);
/**
* @brief 通过资源移动创建XJsonDocument实例（转移源实例的资源所有权）
* @param move 被移动的XJsonDocument实例
* @return 成功返回新的XJsonDocument指针，失败返回NULL
*/
XJsonDocument* XJsonDocument_create_move(XJsonDocument* move);
/**
* @brief 从XJsonObject创建XJsonDocument（根节点为对象）
* @param object 作为根节点的XJsonObject实例（会被拷贝）
* @return 成功返回XJsonDocument指针，失败返回NULL
*/
XJsonDocument* XJsonDocument_create_object(XJsonObject* object);
/**
* @brief 从XJsonObject移动创建XJsonDocument（根节点为对象，转移所有权）
* @param object 作为根节点的XJsonObject实例（所有权转移）
* @return 成功返回XJsonDocument指针，失败返回NULL
*/
XJsonDocument* XJsonDocument_create_object_move(XJsonObject* object);
/**
* @brief 从XJsonArray创建XJsonDocument（根节点为数组）
* @param array 作为根节点的XJsonArray实例（会被拷贝）
* @return 成功返回XJsonDocument指针，失败返回NULL
*/
XJsonDocument* XJsonDocument_create_array(XJsonArray* array);
/**
* @brief 从XJsonArray移动创建XJsonDocument（根节点为数组，转移所有权）
* @param array 作为根节点的XJsonArray实例（所有权转移）
* @return 成功返回XJsonDocument指针，失败返回NULL
*/
XJsonDocument* XJsonDocument_create_array_move(XJsonArray* array);
/**
* @brief 从 Variant 深拷贝创建 JSON 文档。
* @param variant 支持 Map、List、JSON 对象、数组、值和文档类型。
*/
XJsonDocument* XJsonDocument_fromVariant(const XVariant* variant);
/**
* @brief 从同类型 XVariant 深复制创建 JSON 文档。
* @param variant 源 XVariant；借用，类型不匹配返回 NULL。
* @return 新建的 XJsonDocument，失败返回 NULL。
*/
XJsonDocument* XJsonDocument_fromVariant_copy(const XVariant* variant);
/**
* @brief 从同类型 XVariant 借用取得 JSON 文档。
* @param variant 源 XVariant；借用。
* @return XVariant 内部文档指针；不得释放，类型不匹配返回 NULL。
*/
XJsonDocument* XJsonDocument_fromVariant_ref(const XVariant* variant);
/**
* @brief 深复制设置 XVariant 的 JSON 文档值。
* @param variant 目标 XVariant；不能为 NULL。
* @param document 源文档；借用，可为 NULL。
*/
void XJsonDocument_setVariant(XVariant* variant, const XJsonDocument* document);
/**
* @brief 移动设置 XVariant 的 JSON 文档值。
* @param variant 目标 XVariant；不能为 NULL。
* @param document 源文档；成功后由 XVariant 接管资源。
*/
void XJsonDocument_setVariant_move(XVariant* variant, XJsonDocument* document);
/**
* @brief 将已有 JSON 文档直接交给 XVariant 管理。
* @param variant 目标 XVariant；不能为 NULL。
* @param document 源文档；成功后由 XVariant 负责其生命周期。
*/
void XJsonDocument_setVariant_ref(XVariant* variant, XJsonDocument* document);
// 初始化与反初始化
/**
* @brief 初始化XJsonDocument实例
* @param document 需要初始化的XJsonDocument指针
* @details 将根节点初始化为Invalid类型，表示没有文档
*/
void XJsonDocument_init(XJsonDocument* document);
/**
* @brief 反初始化XJsonDocument实例
* @param document 需要反初始化的XJsonDocument指针
* @details 释放根节点资源，但不释放实例本身
*/
void XJsonDocument_deinit(XJsonDocument* document);
/**
* @brief 销毁XJsonDocument实例
* @param document 需要销毁的XJsonDocument指针
* @details 释放根节点资源及实例本身
*/
void XJsonDocument_delete(XJsonDocument* document);
/**
* @brief 清空XJsonDocument的内容
* @param document 目标XJsonDocument指针
* @details 将根节点重置为Invalid类型，释放原有资源
*/
void XJsonDocument_clear(XJsonDocument* document);
// 拷贝与移动
/**
* @brief 深拷贝XJsonDocument内容
* @param doc 目标XJsonDocument指针
* @param src 源XJsonDocument指针
* @details 将src的根节点深拷贝到doc
*/
void XJsonDocument_copy(XJsonDocument* doc, const XJsonDocument* src);
/**
* @brief 移动XJsonDocument资源
* @param doc 目标XJsonDocument指针
* @param src 源XJsonDocument指针
* @details 将src的根节点资源转移到doc，src变为空
*/
void XJsonDocument_move(XJsonDocument* doc, XJsonDocument* src);
// 根节点操作
/**
* @brief 获取文档的根节点（可修改）
* @param document 目标XJsonDocument指针
* @return 成功返回XJsonValue指针，失败返回NULL
*/
XJsonValue* XJsonDocument_root(XJsonDocument* document);
/**
* @brief 获取文档的根节点（不可修改）
* @param document 目标XJsonDocument指针
* @return 成功返回const XJsonValue指针，失败返回NULL
*/
const XJsonValue* XJsonDocument_root_const(const XJsonDocument* document);
/**
* @brief 设置文档的根节点（深拷贝）
* @param document 目标XJsonDocument指针
* @param root 要设置的根节点XJsonValue
*/
void XJsonDocument_setRoot(XJsonDocument* document, const XJsonValue* root);
/**
* @brief 设置文档的根节点（转移所有权）
* @param document 目标XJsonDocument指针
* @param root 要设置的根节点XJsonValue（所有权转移）
*/
void XJsonDocument_setRoot_move(XJsonDocument* document, XJsonValue* root);
// 类型检查
/**
* @brief 判断文档根节点是否为数组
* @param document 目标XJsonDocument指针
* @return 是数组返回true，否则返回false
*/
bool XJsonDocument_isArray(const XJsonDocument* document);
/**
* @brief 判断文档根节点是否为对象
* @param document 目标XJsonDocument指针
* @return 是对象返回true，否则返回false
*/
bool XJsonDocument_isObject(const XJsonDocument* document);
/**
* @brief 判断文档根节点是否为Null
* @param document 目标XJsonDocument指针
* @return 文档没有根节点时返回true，否则返回false
*/
bool XJsonDocument_isNull(const XJsonDocument* document);
/**
* @brief 判断文档是否为空（没有根节点）
* @param document 目标XJsonDocument指针
* @return 为空返回true，否则返回false
*/
bool XJsonDocument_isEmpty(const XJsonDocument* document);
// 数组与对象操作
/**
* @brief 获取文档根节点的XJsonObject（根节点必须为对象）
* @param document 目标XJsonDocument指针
* @return 成功返回XJsonObject指针，根节点非对象返回NULL
*/
XJsonObject* XJsonDocument_object(XJsonDocument* document);
/**
* @brief 获取文档根节点的XJsonArray（根节点必须为数组）
* @param document 目标XJsonDocument指针
* @return 成功返回XJsonArray指针，根节点非数组返回NULL
*/
XJsonArray* XJsonDocument_array(XJsonDocument* document);
/**
* @brief 设置文档根节点为指定XJsonArray（深拷贝）
* @param document 目标XJsonDocument指针
* @param array 要设置的XJsonArray
* @return 设置成功返回true，失败返回false
*/
bool XJsonDocument_setArray(XJsonDocument* document, const XJsonArray* array);
/**
* @brief 设置文档根节点为指定XJsonObject（深拷贝）
* @param document 目标XJsonDocument指针
* @param object 要设置的XJsonObject
* @return 设置成功返回true，失败返回false
*/
bool XJsonDocument_setObject(XJsonDocument* document, const XJsonObject* object);
/**
* @brief 设置文档根节点为指定XJsonArray（转移所有权）
* @param document 目标XJsonDocument指针
* @param array 要设置的XJsonArray（所有权转移）
* @return 设置成功返回true，失败返回false
*/
bool XJsonDocument_setArray_move(XJsonDocument* document, XJsonArray* array);
/**
* @brief 设置文档根节点为指定XJsonObject（转移所有权）
* @param document 目标XJsonDocument指针
* @param object 要设置的XJsonObject（所有权转移）
* @return 设置成功返回true，失败返回false
*/
bool XJsonDocument_setObject_move(XJsonDocument* document, XJsonObject* object);
// 序列化与反序列化
/**
* @brief 从XString解析JSON文档
* @param json 包含JSON字符串的XString
* @return 成功返回XJsonDocument指针，失败返回NULL
*/
XJsonDocument* XJsonDocument_fromString(const XString* json);
/**
* @brief 从 XString 解析 JSON，并返回详细错误位置。
*/
XJsonDocument* XJsonDocument_fromString_ex(const XString* json, XJsonParseError* error);
/**
* @brief 将JSON文档序列化为XString
* @param document 目标XJsonDocument指针
* @param format 序列化格式（缩进或紧凑）
* @return 成功返回序列化后的XString指针，失败返回NULL
*/
XString* XJsonDocument_toString(const XJsonDocument* document, XJsonDocumentFormat format);
/**
* @brief 从XByteArray解析JSON文档（UTF-8编码）
* @param json 包含JSON数据的XByteArray
* @return 成功返回XJsonDocument指针，失败返回NULL
*/
XJsonDocument* XJsonDocument_fromJson(const XByteArray* json);
/**
* @brief 从 XByteArray 解析 JSON，并返回详细错误位置。
*/
XJsonDocument* XJsonDocument_fromJson_ex(const XByteArray* json, XJsonParseError* error);
/**
* @brief 将JSON文档序列化为XByteArray（UTF-8编码，适合传输）
* @param document 目标XJsonDocument指针
* @param format 序列化格式（缩进或紧凑）
* @return 成功返回序列化后的XByteArray指针，失败返回NULL
*/
XByteArray* XJsonDocument_toJson(const XJsonDocument* document, XJsonDocumentFormat format);
/**
* @brief 将文档深拷贝转换为 Variant。
*/
XVariant* XJsonDocument_toVariant(const XJsonDocument* document);
/**
* @brief 将文档移动转换为 Variant，并清空源文档。
*/
XVariant* XJsonDocument_toVariant_move(XJsonDocument* document);
/**
* @brief 创建引用文档的 Variant，不转移文档所有权。
*/
XVariant* XJsonDocument_toVariant_ref(XJsonDocument* document);

/**
* @brief 兼容旧的 XVariant JSON 文档扩展 API 名称。
* @details 以下宏仅保留源代码兼容性，实际实现均归属 XJsonDocument。
*/
#define XVariant_create_JsonDocument       XJsonDocument_toVariant
#define XVariant_create_JsonDocument_move  XJsonDocument_toVariant_move
#define XVariant_create_JsonDocument_ref   XJsonDocument_toVariant_ref
#define XVariant_toJsonDocument            XJsonDocument_fromVariant_copy
#define XVariant_toJsonDocument_ref        XJsonDocument_fromVariant_ref
#define XVariant_setValue_JsonDocument     XJsonDocument_setVariant
#define XVariant_setValue_JsonDocument_move XJsonDocument_setVariant_move
#define XVariant_setValue_JsonDocument_ref XJsonDocument_setVariant_ref

/**
* @brief 从 BSON 文档字节流转换为 JSON 文档。
*/
XJsonDocument* XJsonDocument_fromBson_document(const XByteArray* bson);
/**
* @brief 从 BSON 数组字节流转换为 JSON 文档。
*/
XJsonDocument* XJsonDocument_fromBson_array(const XByteArray* bson);
/**
* @brief 将 JSON 文档序列化为 BSON 字节流。
*/
XByteArray* XJsonDocument_toBson(const XJsonDocument* document);
#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XJsonDocument_create
#define XJsonDocument_create() XJsonDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XJSONDOCUMENT_H
