#ifndef XBSONVALUE_H
#define XBSONVALUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XBson.h"
#include "XJsonValue.h"

/**
 * @brief BSON 值。
 * @details 字符串、文档、数组和二进制成员由值对象拥有；移动后源值
 *          的 type 为 0，调用者仍可安全调用 XBsonValue_deinit。
 */
typedef struct XBsonValue
{
	XBsonType type;  ///< 存储当前 BSON 值的类型；0 表示已移动或未初始化值
	union
	{
		double dbl;              ///< 当类型为 XBSON_TYPE_DOUBLE 时使用
		struct {
			XString* str;             ///< string/javascript/symbol/code 字符串
			struct XBsonDocument* doc;///< document/javascript-scope 文档
		};
		struct XBsonArray* arr;   ///< 当类型为 XBSON_TYPE_ARRAY 时使用
		struct {
			XBsonBinarySubtype subtype;  ///< 二进制子类型
			XByteArray* data;            ///< 二进制数据
		} binary;
		uint8_t oid[12];          ///< ObjectId 使用的 12 字节数据
		bool boolean;             ///< 当类型为 XBSON_TYPE_BOOL 时使用
		int64_t datetime;         ///< 当类型为 XBSON_TYPE_DATETIME 时使用，单位毫秒
		struct {
			XString* pattern;     ///< 正则表达式模式
			XString* options;     ///< 正则表达式选项
		} regex;
		struct {
			XString* m_namespace; ///< DBPointer 命名空间
			uint8_t oid[12];      ///< DBPointer 的 12 字节对象 ID
		} dbpointer;
		int32_t int32;            ///< 当类型为 XBSON_TYPE_INT32 时使用
		struct {
			uint32_t increment;   ///< Timestamp 自增计数器
			uint32_t timestamp;   ///< Timestamp 时间戳
		} ts;
		int64_t int64;            ///< 当类型为 XBSON_TYPE_INT64 时使用
		uint8_t decimal[16];      ///< 当类型为 XBSON_TYPE_DECIMAL128 时使用
		/* NULL、Undefined、MinKey 和 MaxKey 类型不需要额外数据。 */
	} data;
} XBsonValue;

// 构造函数
/**
 * @brief 创建一个指定类型的 BSON 值。
 * @param type 要创建的 BSON 类型。
 * @return 成功返回新对象，类型无效或内存不足返回 NULL。
 */
XBsonValue* XBsonValue_create(XBsonType type);
/**
 * @brief 深拷贝创建 BSON 值。
 * @param other 源 BSON 值，只读借用。
 * @return 成功返回新对象，失败返回 NULL。
 */
XBsonValue* XBsonValue_create_copy(const XBsonValue* other);
/**
 * @brief 移动创建 BSON 值。
 * @param other 源 BSON 值，资源转移后 type 为 0。
 * @return 成功返回新对象，失败返回 NULL。
 */
XBsonValue* XBsonValue_create_move(XBsonValue* other);
/** @brief 创建 NULL 类型的 BSON 值。 */
XBsonValue* XBsonValue_create_null(void);
/** @brief 创建已废弃的 Undefined 类型 BSON 值。 */
XBsonValue* XBsonValue_create_undefined(void);
/** @brief 创建布尔类型 BSON 值。 */
XBsonValue* XBsonValue_create_bool(bool value);
/** @brief 创建双精度浮点 BSON 值。 */
XBsonValue* XBsonValue_create_double(double value);
/** @brief 创建 32 位整数 BSON 值。 */
XBsonValue* XBsonValue_create_int32(int32_t value);
/** @brief 创建 64 位整数 BSON 值。 */
XBsonValue* XBsonValue_create_int64(int64_t value);
/** @brief 创建字符串 BSON 值并深拷贝源字符串。 */
XBsonValue* XBsonValue_create_string(const XString* str);
/** @brief 创建已废弃的 Symbol BSON 值并深拷贝字符串。 */
XBsonValue* XBsonValue_create_symbol(const XString* str);
/** @brief 创建文档 BSON 值并深拷贝嵌套文档。 */
XBsonValue* XBsonValue_create_document(const XBsonDocument* doc);
/** @brief 创建数组 BSON 值并深拷贝嵌套数组。 */
XBsonValue* XBsonValue_create_array(const XBsonArray* arr);
/** @brief 创建二进制 BSON 值并深拷贝数据。 */
XBsonValue* XBsonValue_create_binary(XBsonBinarySubtype subtype,
                                     const XByteArray* data);
/** @brief 创建 ObjectId BSON 值并复制 12 字节对象 ID。 */
XBsonValue* XBsonValue_create_object_id(const uint8_t* oid);
/** @brief 创建毫秒时间戳 BSON 值。 */
XBsonValue* XBsonValue_create_datetime(int64_t timestamp);
/** @brief 创建正则表达式 BSON 值，选项序列化时必须按 BSON 规范排序。 */
XBsonValue* XBsonValue_create_regex(const XString* pattern,
                                    const XString* options);
/** @brief 创建已废弃的 DBPointer BSON 值。 */
XBsonValue* XBsonValue_create_dbpointer(const XString* namespace_name,
                                        const uint8_t* oid);
/** @brief 创建 JavaScript 代码 BSON 值。 */
XBsonValue* XBsonValue_create_javascript(const XString* code);
/** @brief 创建带作用域的 JavaScript BSON 值，并深拷贝代码和作用域。 */
XBsonValue* XBsonValue_create_javascript_scope(const XString* code,
                                               const XBsonDocument* scope);
/** @brief 创建 BSON Timestamp 值；字节顺序为 increment 后 timestamp。 */
XBsonValue* XBsonValue_create_timestamp(uint32_t increment,
                                        uint32_t timestamp);
/** @brief 创建 Decimal128 BSON 值并复制 16 字节原始数据。 */
XBsonValue* XBsonValue_create_decimal128(const uint8_t* decimal);
/** @brief 创建 MinKey BSON 值。 */
XBsonValue* XBsonValue_create_min_key(void);
/** @brief 创建 MaxKey BSON 值。 */
XBsonValue* XBsonValue_create_max_key(void);

// 初始化与宏定义

/**
 * @brief 初始化一个 BSON 值。
 * @param value 目标 BSON 值；可为栈对象。
 * @param type BSON 类型。
 */
void XBsonValue_init(XBsonValue* value, XBsonType type);
/** @brief 快速创建栈上 BSON 值并初始化指针变量。 */
#define XBsonValue_Init(var,type) \
	XBsonValue _##var,*var=&_##var; XBsonValue_init(var,type)

/** @brief 释放 BSON 值拥有的资源但保留对象本身。 */
void XBsonValue_deinit(XBsonValue* value);
/** @brief 释放 BSON 值资源并释放对象本身。 */
void XBsonValue_delete(XBsonValue* value);
/** @brief 清空值拥有的数据并保留当前 BSON 类型。 */
void XBsonValue_clear(XBsonValue* value);

// 设置值函数
/** @brief 将值设置为 NULL 类型。 */
void XBsonValue_setNull(XBsonValue* value);
/** @brief 将值设置为 Undefined 类型。 */
void XBsonValue_setUndefined(XBsonValue* value);
/** @brief 设置布尔值。 */
void XBsonValue_setBool(XBsonValue* value, bool b);
/** @brief 设置双精度浮点值。 */
void XBsonValue_setDouble(XBsonValue* value, double d);
/** @brief 设置 32 位整数值。 */
void XBsonValue_setInt32(XBsonValue* value, int32_t i);
/** @brief 设置 64 位整数值。 */
void XBsonValue_setInt64(XBsonValue* value, int64_t i);
/** @brief 深拷贝设置字符串值。 */
void XBsonValue_setString(XBsonValue* value, const XString* str);
/** @brief 移动设置字符串值，源字符串变为空。 */
void XBsonValue_setString_move(XBsonValue* value, XString* str);
/** @brief 从 UTF-8 字符串设置字符串值。 */
void XBsonValue_setString_utf8(XBsonValue* value, const char* utf8);
/** @brief 深拷贝设置 Symbol 值。 */
void XBsonValue_setSymbol(XBsonValue* value, const XString* str);
/** @brief 深拷贝设置嵌套文档值。 */
void XBsonValue_setDocument(XBsonValue* value, const XBsonDocument* doc);
/** @brief 移动设置嵌套文档值。 */
void XBsonValue_setDocument_move(XBsonValue* value, XBsonDocument* doc);
/** @brief 深拷贝设置嵌套数组值。 */
void XBsonValue_setArray(XBsonValue* value, const XBsonArray* arr);
/** @brief 移动设置嵌套数组值。 */
void XBsonValue_setArray_move(XBsonValue* value, XBsonArray* arr);
/** @brief 深拷贝设置二进制值。 */
void XBsonValue_setBinary(XBsonValue* value, XBsonBinarySubtype subtype,
                          const XByteArray* data);
/** @brief 移动设置二进制值。 */
void XBsonValue_setBinary_move(XBsonValue* value, XBsonBinarySubtype subtype,
                               XByteArray* data);
/** @brief 设置 12 字节 ObjectId。 */
void XBsonValue_setObjectId(XBsonValue* value, const uint8_t* oid);
/** @brief 设置毫秒级日期时间值。 */
void XBsonValue_setDatetime(XBsonValue* value, int64_t timestamp);
/** @brief 深拷贝设置正则模式和选项。 */
void XBsonValue_setRegex(XBsonValue* value, const XString* pattern,
                         const XString* options);
/** @brief 深拷贝设置 DBPointer 命名空间和对象 ID。 */
void XBsonValue_setDbpointer(XBsonValue* value, const XString* namespace_name,
                             const uint8_t* oid);
/** @brief 深拷贝设置 JavaScript 代码。 */
void XBsonValue_setJavascript(XBsonValue* value, const XString* code);
/** @brief 深拷贝设置 JavaScript 代码和作用域文档。 */
void XBsonValue_setJavascript_scope(XBsonValue* value, const XString* code,
                                    const XBsonDocument* scope);
/** @brief 设置 BSON Timestamp 的增量和时间戳。 */
void XBsonValue_setTimestamp(XBsonValue* value, uint32_t increment,
                             uint32_t timestamp);
/** @brief 复制 16 字节原始数据并设置 Decimal128。 */
void XBsonValue_setDecimal128(XBsonValue* value, const uint8_t* decimal);
/** @brief 将值设置为 MinKey。 */
void XBsonValue_setMin_key(XBsonValue* value);
/** @brief 将值设置为 MaxKey。 */
void XBsonValue_setMax_key(XBsonValue* value);

// 获取值函数（to* 系列）
/** @brief 获取布尔值；类型不匹配返回 defaultValue。 */
bool XBsonValue_toBool(const XBsonValue* value, bool defaultValue);
/** @brief 获取 double 值；类型不匹配返回 defaultValue。 */
double XBsonValue_toDouble(const XBsonValue* value, double defaultValue);
/** @brief 获取 int32 值；类型不匹配返回 defaultValue。 */
int32_t XBsonValue_toInt32(const XBsonValue* value, int32_t defaultValue);
/** @brief 获取 int64 值；类型不匹配返回 defaultValue。 */
int64_t XBsonValue_toInt64(const XBsonValue* value, int64_t defaultValue);
/** @brief 获取字符串、JavaScript 代码或 Symbol 的只读借用指针。 */
const XString* XBsonValue_toString(const XBsonValue* value);
/** @brief 获取文档或 JavaScript 作用域的只读借用指针。 */
const XBsonDocument* XBsonValue_toDocument(const XBsonValue* value);
/** @brief 获取嵌套数组的只读借用指针。 */
const XBsonArray* XBsonValue_toArray(const XBsonValue* value);
/** @brief 获取二进制数据和子类型的只读借用指针。 */
const XByteArray* XBsonValue_toBinary(const XBsonValue* value,
                                      XBsonBinarySubtype* outSubtype);
/** @brief 获取 12 字节 ObjectId 的只读借用指针。 */
const uint8_t* XBsonValue_toObjectId(const XBsonValue* value);
/** @brief 获取日期时间；类型不匹配返回 defaultValue。 */
int64_t XBsonValue_toDatetime(const XBsonValue* value, int64_t defaultValue);
/** @brief 获取正则表达式模式的只读借用指针。 */
const XString* XBsonValue_toRegexPattern(const XBsonValue* value);
/** @brief 获取正则表达式选项的只读借用指针。 */
const XString* XBsonValue_toRegexOptions(const XBsonValue* value);
/** @brief 获取 DBPointer 命名空间的只读借用指针。 */
const XString* XBsonValue_toDbpointerNamespace(const XBsonValue* value);
/** @brief 获取 DBPointer 对象 ID 的只读借用指针。 */
const uint8_t* XBsonValue_toDbpointerObjectId(const XBsonValue* value);
/** @brief 获取 JavaScript 作用域文档的只读借用指针。 */
const XBsonDocument* XBsonValue_toJavascriptScope(const XBsonValue* value);
/** @brief 获取 Decimal128 原始 16 字节数据的只读借用指针。 */
const uint8_t* XBsonValue_toDecimal128(const XBsonValue* value);
/** @brief 获取 Timestamp 的 increment 字段。 */
uint32_t XBsonValue_timestampIncrement(const XBsonValue* value);
/** @brief 获取 Timestamp 的 timestamp 字段。 */
uint32_t XBsonValue_timestamp(const XBsonValue* value);

// 类型判断函数（is* 系列）
/** @brief 判断是否为 NULL 类型。 */
bool XBsonValue_isNull(const XBsonValue* value);
/** @brief 判断是否为 Undefined 类型。 */
bool XBsonValue_isUndefined(const XBsonValue* value);
/** @brief 判断是否为 Bool 类型。 */
bool XBsonValue_isBool(const XBsonValue* value);
/** @brief 判断是否为 Double 类型。 */
bool XBsonValue_isDouble(const XBsonValue* value);
/** @brief 判断是否为 Int32 类型。 */
bool XBsonValue_isInt32(const XBsonValue* value);
/** @brief 判断是否为 Int64 类型。 */
bool XBsonValue_isInt64(const XBsonValue* value);
/** @brief 判断是否为 String 类型。 */
bool XBsonValue_isString(const XBsonValue* value);
/** @brief 判断是否为 Symbol 类型。 */
bool XBsonValue_isSymbol(const XBsonValue* value);
/** @brief 判断是否为 Document 类型。 */
bool XBsonValue_isDocument(const XBsonValue* value);
/** @brief 判断是否为 Array 类型。 */
bool XBsonValue_isArray(const XBsonValue* value);
/** @brief 判断是否为 Binary 类型。 */
bool XBsonValue_isBinary(const XBsonValue* value);
/** @brief 判断是否为 ObjectId 类型。 */
bool XBsonValue_isObjectId(const XBsonValue* value);
/** @brief 判断是否为 Datetime 类型。 */
bool XBsonValue_isDatetime(const XBsonValue* value);
/** @brief 判断是否为 Regex 类型。 */
bool XBsonValue_isRegex(const XBsonValue* value);
/** @brief 判断是否为 DBPointer 类型。 */
bool XBsonValue_isDbpointer(const XBsonValue* value);
/** @brief 判断是否为 JavaScript 类型。 */
bool XBsonValue_isJavascript(const XBsonValue* value);
/** @brief 判断是否为带作用域的 JavaScript 类型。 */
bool XBsonValue_isJavascriptScope(const XBsonValue* value);
/** @brief 判断是否为 Timestamp 类型。 */
bool XBsonValue_isTimestamp(const XBsonValue* value);
/** @brief 判断是否为 Decimal128 类型。 */
bool XBsonValue_isDecimal128(const XBsonValue* value);
/** @brief 判断是否为 MinKey 类型。 */
bool XBsonValue_isMinKey(const XBsonValue* value);
/** @brief 判断是否为 MaxKey 类型。 */
bool XBsonValue_isMaxKey(const XBsonValue* value);

// 拷贝与移动操作
/** @brief 深拷贝 BSON 值；目标原有资源会先释放。 */
void XBsonValue_copy(XBsonValue* dest, const XBsonValue* src);
/** @brief 移动 BSON 值；源值移动后 type 为 0。 */
void XBsonValue_move(XBsonValue* dest, XBsonValue* src);
/** @brief 获取当前 BSON 类型。 */
XBsonType XBsonValue_type(const XBsonValue* value);
/** @brief 判断 BSON 值是否为指定类型。 */
bool XBsonValue_is_type(const XBsonValue* value, XBsonType type);

// 与 XJson 转换
/**
 * @brief 将 BSON 值转换为 XJsonValue。
 * @details 特殊 BSON 类型使用带 $ 前缀的对象包装；Decimal128 使用原始
 *          16 字节十六进制表示，以保证转换可逆。
 */
XJsonValue* XBsonValue_to_json(const XBsonValue* bson_val);
/** @brief 从 XJsonValue 恢复 BSON 值，支持 XBsonValue_to_json 的包装格式。 */
XBsonValue* XBsonValue_from_json(const XJsonValue* json_val);

/** @brief 序列化一个 BSON 元素（包含 type、键名和值）。 */
bool XBsonValue_serialize(const XBsonValue* value, const char* key,
                          XByteArray* output);
/** @brief 反序列化一个 BSON 元素，并返回其新建键名。 */
XBsonValue* XBsonValue_deserialize(const uint8_t** ptr, const uint8_t* end,
                                   XString** key_out);

// 与 XVariant 转换
/** @brief 深拷贝转换为 XVariant。 */
XVariant* XBsonValue_toVariant(const XBsonValue* val);
/** @brief 移动转换为 XVariant，源值 type 变为 0。 */
XVariant* XBsonValue_toVariant_move(XBsonValue* val);
/**
 * @brief 创建引用型 XVariant。
 * @details 引用对象必须在 XVariant 存活期间保持有效。
 */
XVariant* XBsonValue_toVariant_ref(XBsonValue* val);
/** @brief 从同类型 XVariant 深复制取得 BSON 值。 */
XBsonValue* XBsonValue_fromVariant(const XVariant* variant);
/** @brief 从同类型 XVariant 借用取得 BSON 值。 */
XBsonValue* XBsonValue_fromVariant_ref(const XVariant* variant);
/** @brief 深复制设置 XVariant 的 BSON 值。 */
void XBsonValue_setVariant(XVariant* variant, const XBsonValue* value);
/** @brief 移动设置 XVariant 的 BSON 值。 */
void XBsonValue_setVariant_move(XVariant* variant, XBsonValue* value);
/** @brief 将已有 BSON 值直接交给 XVariant 管理。 */
void XBsonValue_setVariant_ref(XVariant* variant, XBsonValue* value);

/**
* @brief 兼容旧的 XVariant BSON 值扩展 API 名称。
* @details 以下宏仅保留源代码兼容性，实际实现均归属 XBsonValue。
*/
#define XVariant_create_BsonValue       XBsonValue_toVariant
#define XVariant_create_BsonValue_move  XBsonValue_toVariant_move
#define XVariant_create_BsonValue_ref   XBsonValue_toVariant_ref
#define XVariant_toBsonValue            XBsonValue_fromVariant
#define XVariant_toBsonValue_ref        XBsonValue_fromVariant_ref
#define XVariant_setValue_BsonValue     XBsonValue_setVariant
#define XVariant_setValue_BsonValue_move XBsonValue_setVariant_move
#define XVariant_setValue_BsonValue_ref XBsonValue_setVariant_ref

#ifdef __cplusplus
}
#endif

#endif /* XBSONVALUE_H */
