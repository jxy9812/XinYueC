#ifndef XBSONVALUE_H
#define XBSONVALUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XBson.h"
#include "XJsonValue.h"
#include "XStack.h"
/**
* @brief BSON值结构体，用于存储各种类型的BSON数据
*/
typedef struct XBsonValue
{
	XBsonType type;  ///< 存储当前BSON值的类型
	union {          ///< 联合体，根据类型存储对应的数据
		double dbl;               ///< 当类型为XBSON_TYPE_DOUBLE时使用，存储双精度浮点数
		struct {
		XString* str;             ///< 当类型为XBSON_TYPE_STRING或XBSON_TYPE_JAVASCRIPT时使用，存储字符串
		struct XBsonDocument* doc;///< 当类型为XBSON_TYPE_DOCUMENT或XBSON_TYPE_JAVASCRIPT_SCOPE时使用，存储文档
	};
	struct XBsonArray* arr;   ///< 当类型为XBSON_TYPE_ARRAY时使用，存储数组
	struct {                  ///< 当类型为XBSON_TYPE_BINARY时使用，存储二进制数据
		XBsonBinarySubtype subtype;  ///< 二进制子类型
		XByteArray* data;            ///< 二进制数据
	} binary;
	uint8_t oid[12];          ///< 当类型为XBSON_TYPE_OBJECT_ID时使用，存储12字节的对象ID
	bool boolean;             ///< 当类型为XBSON_TYPE_BOOLEAN时使用，存储布尔值
	int64_t datetime;         ///< 当类型为XBSON_TYPE_DATETIME时使用，存储日期时间（毫秒级时间戳）
	struct {                  ///< 当类型为XBSON_TYPE_REGEX时使用，存储正则表达式
		XString* pattern;     ///< 正则表达式模式
		XString* options;     ///< 正则表达式选项
	} regex;
	int32_t int32;            ///< 当类型为XBSON_TYPE_INT32时使用，存储32位整数
	struct {                  ///< 当类型为XBSON_TYPE_TIMESTAMP时使用，存储时间戳
		uint32_t increment;   ///< 自增计数器
		uint32_t timestamp;   ///< 时间戳
	} ts;
	int64_t int64;            ///< 当类型为XBSON_TYPE_INT64时使用，存储64位整数
	uint8_t decimal[16];      ///< 当类型为XBSON_TYPE_DECIMAL128时使用，存储128位十进制数
	// 注意：XBSON_TYPE_NULL、XBSON_TYPE_MIN_KEY、XBSON_TYPE_MAX_KEY这几种类型不需要额外数据
	} data;
} XBsonValue;
// 构造函数
/**
* @brief 创建一个指定类型的BSON值
* @param type 要创建的BSON值类型
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create(XBsonType type);
/**
* @brief 拷贝创建一个BSON值（深拷贝）
* @param other 被拷贝的BSON值
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_copy(const XBsonValue* other);
/**
* @brief 移动创建一个BSON值（转移内部资源所有权）
* @param other 被移动的BSON值
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_move(XBsonValue* other);
/**
* @brief 创建一个NULL类型的BSON值
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_null(void);
/**
* @brief 创建一个布尔类型的BSON值
* @param value 布尔值
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_bool(bool value);
/**
* @brief 创建一个双精度浮点类型的BSON值
* @param value 双精度浮点值
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_double(double value);
/**
* @brief 创建一个32位整数类型的BSON值
* @param value 32位整数值
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_int32(int32_t value);
/**
* @brief 创建一个64位整数类型的BSON值
* @param value 64位整数值
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_int64(int64_t value);
/**
* @brief 创建一个字符串类型的BSON值
* @param str 字符串数据（XString类型）
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_string(const XString* str);
/**
* @brief 创建一个文档类型的BSON值
* @param doc 文档数据（XBsonDocument类型）
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_document(const XBsonDocument* doc);
/**
* @brief 创建一个数组类型的BSON值
* @param arr 数组数据（XBsonArray类型）
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_array(const XBsonArray* arr);
/**
* @brief 创建一个二进制类型的BSON值
* @param subtype 二进制子类型
* @param data 二进制数据（XByteArray类型）
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_binary(XBsonBinarySubtype subtype, const XByteArray* data);
/**
* @brief 创建一个ObjectId类型的BSON值（复制12字节ID）
* @param oid 12字节的ObjectId数据
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_object_id(const uint8_t* oid);
/**
* @brief 创建一个datetime类型的BSON值（毫秒级时间戳）
* @param timestamp 毫秒级时间戳
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_datetime(int64_t timestamp);
/**
* @brief 创建一个正则表达式类型的BSON值（复制模式和选项）
* @param pattern 正则表达式模式
* @param options 正则表达式选项
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_regex(const XString* pattern, const XString* options);
/**
* @brief 创建一个JavaScript代码类型的BSON值（复制代码字符串）
* @param code JavaScript代码字符串
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_javascript(const XString* code);
/**
* @brief 创建一个带作用域的JavaScript类型的BSON值（复制代码和作用域文档）
* @param code JavaScript代码字符串
* @param scope 作用域文档
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_javascript_scope(const XString* code, const XBsonDocument* scope);
/**
* @brief 创建一个timestamp类型的BSON值（增量+时间戳）
* @param increment 自增计数器
* @param timestamp 时间戳
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_timestamp(uint32_t increment, uint32_t timestamp);
/**
* @brief 创建一个decimal128类型的BSON值（复制16字节十进制数据）
* @param decimal 16字节的十进制数据
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_decimal128(const uint8_t* decimal);
/**
* @brief 创建一个MinKey类型的BSON值
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_min_key(void);
/**
* @brief 创建一个MaxKey类型的BSON值
* @return 指向新创建的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_create_max_key(void);
// 初始化与宏定义
/**
* @brief 初始化一个BSON值
* @param value 要初始化的BSON值指针
* @param type BSON值的类型
*/
void XBsonValue_init(XBsonValue* value, XBsonType type);
/**
* @brief 宏定义：快速初始化一个BSON值变量
* @param var 变量名
* @param type BSON值类型
*/
#define XBsonValue_Init(var,type)  XBsonValue _##var,*var=&_##var;XBsonValue_init(var,type)
// 析构与清理函数
/**
* @brief 反初始化一个BSON值（释放内部资源，保留对象本身）
* @param value 要反初始化的BSON值指针
*/
void XBsonValue_deinit(XBsonValue* value);
/**
* @brief 销毁一个BSON值（释放所有资源）
* @param value 要销毁的BSON值指针
*/
void XBsonValue_delete(XBsonValue* value);
/**
* @brief 清空一个BSON值（释放内部资源，重置为初始状态）
* @param value 要清空的BSON值指针
*/
void XBsonValue_clear(XBsonValue* value);
// 设置值函数
/**
* @brief 将BSON值设置为NULL类型
* @param value BSON值指针
*/
void XBsonValue_setNull(XBsonValue* value);
/**
* @brief 设置BSON值为布尔类型并赋值
* @param value BSON值指针
* @param b 布尔值
*/
void XBsonValue_setBool(XBsonValue* value, bool b);
/**
* @brief 设置BSON值为双精度浮点类型并赋值
* @param value BSON值指针
* @param d 双精度浮点值
*/
void XBsonValue_setDouble(XBsonValue* value, double d);
/**
* @brief 设置BSON值为32位整数类型并赋值
* @param value BSON值指针
* @param i 32位整数值
*/
void XBsonValue_setInt32(XBsonValue* value, int32_t i);
/**
* @brief 设置BSON值为64位整数类型并赋值
* @param value BSON值指针
* @param i 64位整数值
*/
void XBsonValue_setInt64(XBsonValue* value, int64_t i);
/**
* @brief 设置BSON值为字符串类型并赋值（拷贝字符串）
* @param value BSON值指针
* @param str 字符串数据（XString类型）
*/
void XBsonValue_setString(XBsonValue* value, const XString* str);
/**
* @brief 设置BSON值为字符串类型并赋值（转移字符串所有权）
* @param value BSON值指针
* @param str 字符串数据（XString类型）
*/
void XBsonValue_setString_move(XBsonValue* value, XString* str);
/**
* @brief 设置BSON值为字符串类型并赋值（从UTF-8字符串）
* @param value BSON值指针
* @param utf8 UTF-8编码的字符串
*/
void XBsonValue_setString_utf8(XBsonValue* value, const char* utf8);
/**
* @brief 设置BSON值为文档类型并赋值（拷贝文档）
* @param value BSON值指针
* @param doc 文档数据（XBsonDocument类型）
*/
void XBsonValue_setDocument(XBsonValue* value, const XBsonDocument* doc);
/**
* @brief 设置BSON值为文档类型并赋值（转移文档所有权）
* @param value BSON值指针
* @param doc 文档数据（XBsonDocument类型）
*/
void XBsonValue_setDocument_move(XBsonValue* value, XBsonDocument* doc);
/**
* @brief 设置BSON值为数组类型并赋值（拷贝数组）
* @param value BSON值指针
* @param arr 数组数据（XBsonArray类型）
*/
void XBsonValue_setArray(XBsonValue* value, const XBsonArray* arr);
/**
* @brief 设置BSON值为数组类型并赋值（转移数组所有权）
* @param value BSON值指针
* @param arr 数组数据（XBsonArray类型）
*/
void XBsonValue_setArray_move(XBsonValue* value, XBsonArray* arr);
/**
* @brief 设置BSON值为二进制类型并赋值（拷贝二进制数据）
* @param value BSON值指针
* @param subtype 二进制子类型
* @param data 二进制数据（XByteArray类型）
*/
void XBsonValue_setBinary(XBsonValue* value, XBsonBinarySubtype subtype, const XByteArray* data);
/**
* @brief 设置BSON值为二进制类型并赋值（转移二进制数据所有权）
* @param value BSON值指针
* @param subtype 二进制子类型
* @param data 二进制数据（XByteArray类型）
*/
void XBsonValue_setBinary_move(XBsonValue* value, XBsonBinarySubtype subtype, XByteArray* data);
/**
* @brief 设置BSON值为ObjectId类型并赋值
* @param value BSON值指针
* @param oid 12字节的ObjectId数据
*/
void XBsonValue_setObjectId(XBsonValue* value, const uint8_t* oid);
/**
* @brief 设置BSON值为datetime类型并赋值（毫秒级时间戳）
* @param value BSON值指针
* @param timestamp 毫秒级时间戳
*/
void XBsonValue_setDatetime(XBsonValue* value, int64_t timestamp);
/**
* @brief 设置BSON值为正则表达式类型并赋值（拷贝模式和选项）
* @param value BSON值指针
* @param pattern 正则表达式模式
* @param options 正则表达式选项
*/
void XBsonValue_setRegex(XBsonValue* value, const XString* pattern, const XString* options);
/**
* @brief 设置BSON值为JavaScript代码类型并赋值（拷贝代码字符串）
* @param value BSON值指针
* @param code JavaScript代码字符串
*/
void XBsonValue_setJavascript(XBsonValue* value, const XString* code);
/**
* @brief 设置BSON值为带作用域的JavaScript类型并赋值（拷贝代码和作用域文档）
* @param value BSON值指针
* @param code JavaScript代码字符串
* @param scope 作用域文档
*/
void XBsonValue_setJavascript_scope(XBsonValue* value, const XString* code, const XBsonDocument* scope);
/**
* @brief 设置BSON值为timestamp类型并赋值（增量+时间戳）
* @param value BSON值指针
* @param increment 自增计数器
* @param timestamp 时间戳
*/
void XBsonValue_setTimestamp(XBsonValue* value, uint32_t increment, uint32_t timestamp);
/**
* @brief 设置BSON值为decimal128类型并赋值（拷贝16字节十进制数据）
* @param value BSON值指针
* @param decimal 16字节的十进制数据
*/
void XBsonValue_setDecimal128(XBsonValue* value, const uint8_t* decimal);
/**
* @brief 将BSON值设置为MinKey类型
* @param value BSON值指针
*/
void XBsonValue_setMin_key(XBsonValue* value);
/**
* @brief 将BSON值设置为MaxKey类型
* @param value BSON值指针
*/
void XBsonValue_setMax_key(XBsonValue* value);
// 获取值函数（to*系列）
/**
* @brief 将BSON值转换为布尔值（类型不匹配返回默认值）
* @param value BSON值指针
* @param defaultValue 类型不匹配时的默认返回值
* @return 转换后的布尔值
*/
bool XBsonValue_toBool(const XBsonValue* value, bool defaultValue);
/**
* @brief 将BSON值转换为双精度浮点数（类型不匹配返回默认值）
* @param value BSON值指针
* @param defaultValue 类型不匹配时的默认返回值
* @return 转换后的双精度浮点数
*/
double XBsonValue_toDouble(const XBsonValue* value, double defaultValue);
/**
* @brief 将BSON值转换为32位整数（类型不匹配返回默认值）
* @param value BSON值指针
* @param defaultValue 类型不匹配时的默认返回值
* @return 转换后的32位整数
*/
int32_t XBsonValue_toInt32(const XBsonValue* value, int32_t defaultValue);
/**
* @brief 将BSON值转换为64位整数（类型不匹配返回默认值）
* @param value BSON值指针
* @param defaultValue 类型不匹配时的默认返回值
* @return 转换后的64位整数
*/
int64_t XBsonValue_toInt64(const XBsonValue* value, int64_t defaultValue);
/**
* @brief 获取BSON值的字符串数据（类型不匹配返回NULL）
* @param value BSON值指针
* @return 字符串数据（XString类型），类型不匹配返回NULL
*/
const XString* XBsonValue_toString(const XBsonValue* value);
/**
* @brief 获取BSON值的文档数据（类型不匹配返回NULL）
* @param value BSON值指针
* @return 文档数据（XBsonDocument类型），类型不匹配返回NULL
*/
const XBsonDocument* XBsonValue_toDocument(const XBsonValue* value);
/**
* @brief 获取BSON值的数组数据（类型不匹配返回NULL）
* @param value BSON值指针
* @return 数组数据（XBsonArray类型），类型不匹配返回NULL
*/
const XBsonArray* XBsonValue_toArray(const XBsonValue* value);
/**
* @brief 获取BSON值的二进制数据（类型不匹配返回NULL）
* @param value BSON值指针
* @param outSubtype 输出二进制子类型
* @return 二进制数据（XByteArray类型），类型不匹配返回NULL
*/
const XByteArray* XBsonValue_toBinary(const XBsonValue* value, XBsonBinarySubtype* outSubtype);
/**
* @brief 获取BSON值的ObjectId数据（类型不匹配返回NULL）
* @param value BSON值指针
* @return 12字节的ObjectId数据，类型不匹配返回NULL
*/
const uint8_t* XBsonValue_toObjectId(const XBsonValue* value);
/**
* @brief 获取BSON值的datetime时间戳（类型不匹配返回默认值）
* @param value BSON值指针
* @param defaultValue 类型不匹配时的默认返回值
* @return 毫秒级时间戳
*/
int64_t XBsonValue_toDatetime(const XBsonValue* value, int64_t defaultValue);
/**
* @brief 获取BSON值的正则表达式模式（类型不匹配返回NULL）
* @param value BSON值指针
* @return 正则表达式模式（XString类型），类型不匹配返回NULL
*/
const XString* XBsonValue_toRegexPattern(const XBsonValue* value);
/**
* @brief 获取BSON值的正则表达式选项（类型不匹配返回NULL）
* @param value BSON值指针
* @return 正则表达式选项（XString类型），类型不匹配返回NULL
*/
const XString* XBsonValue_toRegexOptions(const XBsonValue* value);
// 类型判断函数（is*系列）
/**
* @brief 判断BSON值是否为NULL类型
* @param value BSON值指针
* @return 是NULL类型返回true，否则返回false
*/
bool XBsonValue_isNull(const XBsonValue* value);
/**
* @brief 判断BSON值是否为布尔类型
* @param value BSON值指针
* @return 是布尔类型返回true，否则返回false
*/
bool XBsonValue_isBool(const XBsonValue* value);
/**
* @brief 判断BSON值是否为双精度浮点类型
* @param value BSON值指针
* @return 是双精度浮点类型返回true，否则返回false
*/
bool XBsonValue_isDouble(const XBsonValue* value);
/**
* @brief 判断BSON值是否为32位整数类型
* @param value BSON值指针
* @return 是32位整数类型返回true，否则返回false
*/
bool XBsonValue_isInt32(const XBsonValue* value);
/**
* @brief 判断BSON值是否为64位整数类型
* @param value BSON值指针
* @return 是64位整数类型返回true，否则返回false
*/
bool XBsonValue_isInt64(const XBsonValue* value);
/**
* @brief 判断BSON值是否为字符串类型
* @param value BSON值指针
* @return 是字符串类型返回true，否则返回false
*/
bool XBsonValue_isString(const XBsonValue* value);
/**
* @brief 判断BSON值是否为文档类型
* @param value BSON值指针
* @return 是文档类型返回true，否则返回false
*/
bool XBsonValue_isDocument(const XBsonValue* value);
/**
* @brief 判断BSON值是否为数组类型
* @param value BSON值指针
* @return 是数组类型返回true，否则返回false
*/
bool XBsonValue_isArray(const XBsonValue* value);
/**
* @brief 判断BSON值是否为二进制类型
* @param value BSON值指针
* @return 是二进制类型返回true，否则返回false
*/
bool XBsonValue_isBinary(const XBsonValue* value);
/**
* @brief 判断BSON值是否为ObjectId类型
* @param value BSON值指针
* @return 是ObjectId类型返回true，否则返回false
*/
bool XBsonValue_isObjectId(const XBsonValue* value);
/**
* @brief 判断BSON值是否为datetime类型
* @param value BSON值指针
* @return 是datetime类型返回true，否则返回false
*/
bool XBsonValue_isDatetime(const XBsonValue* value);
/**
* @brief 判断BSON值是否为正则表达式类型
* @param value BSON值指针
* @return 是正则表达式类型返回true，否则返回false
*/
bool XBsonValue_isRegex(const XBsonValue* value);
/**
* @brief 判断BSON值是否为JavaScript代码类型
* @param value BSON值指针
* @return 是JavaScript代码类型返回true，否则返回false
*/
bool XBsonValue_isJavascript(const XBsonValue* value);
/**
* @brief 判断BSON值是否为带作用域的JavaScript类型
* @param value BSON值指针
* @return 是带作用域的JavaScript类型返回true，否则返回false
*/
bool XBsonValue_isJavascriptScope(const XBsonValue* value);
/**
* @brief 判断BSON值是否为timestamp类型
* @param value BSON值指针
* @return 是timestamp类型返回true，否则返回false
*/
bool XBsonValue_isTimestamp(const XBsonValue* value);
/**
* @brief 判断BSON值是否为decimal128类型
* @param value BSON值指针
* @return 是decimal128类型返回true，否则返回false
*/
bool XBsonValue_isDecimal128(const XBsonValue* value);
/**
* @brief 判断BSON值是否为MinKey类型
* @param value BSON值指针
* @return 是MinKey类型返回true，否则返回false
*/
bool XBsonValue_isMinKey(const XBsonValue* value);
/**
* @brief 判断BSON值是否为MaxKey类型
* @param value BSON值指针
* @return 是MaxKey类型返回true，否则返回false
*/
bool XBsonValue_isMaxKey(const XBsonValue* value);
// 拷贝与移动操作
/**
* @brief 将源BSON值复制到目标BSON值（深拷贝）
* @param dest 目标BSON值指针
* @param src 源BSON值指针
*/
void XBsonValue_copy(XBsonValue* dest, const XBsonValue* src);
/**
* @brief 将源BSON值移动到目标BSON值（转移内部资源所有权）
* @param dest 目标BSON值指针
* @param src 源BSON值指针
*/
void XBsonValue_move(XBsonValue* dest, XBsonValue* src);
// 类型检查函数
/**
* @brief 获取BSON值的类型
* @param value BSON值指针
* @return 该BSON值的类型（XBsonType）
*/
XBsonType XBsonValue_type(const XBsonValue* value);
/**
* @brief 检查BSON值是否为指定类型
* @param value BSON值指针
* @param type 要检查的类型
* @return 如果类型匹配则返回true，否则返回false
*/
bool XBsonValue_is_type(const XBsonValue* value, XBsonType type);
// 转换函数
/**
* @brief 将BSON值转换为JSON值
* @param bson_val 要转换的BSON值指针
* @return 指向转换后的XJsonValue的指针，失败时返回NULL
*/
XJsonValue* XBsonValue_to_json(const XBsonValue* bson_val);
/**
* @brief 将JSON值转换为BSON值
* @param json_val 要转换的JSON值指针
* @return 指向转换后的XBsonValue的指针，失败时返回NULL
*/
XBsonValue* XBsonValue_from_json(const XJsonValue* json_val);
// 序列化与反序列化函数
/**
* @brief 将BSON值序列化为字节数据
* @param value 要序列化的BSON值指针
*/
#ifdef __cplusplus
}
#endif
#endif // XBSONVALUE_H