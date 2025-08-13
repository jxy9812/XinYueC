#ifndef XBSONVALUE_H
#define XBSONVALUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XBson.h"
#include "XJsonValue.h"
#include "XStack.h"

/**
 * BSON值结构体，用于存储各种类型的BSON数据
 */
typedef struct XBsonValue
{
    XBsonType type;  // 存储当前BSON值的类型
    union {          // 联合体，根据类型存储对应的数据
        double dbl;               // 当类型为XBSON_TYPE_DOUBLE时使用，存储双精度浮点数
        XString* str;             // 当类型为XBSON_TYPE_STRING或XBSON_TYPE_JAVASCRIPT时使用，存储字串
        struct XBsonDocument* doc;// 当类型为XBSON_TYPE_DOCUMENT或XBSON_TYPE_JAVASCRIPT_SCOPE时使用储档
        struct XBsonArray* arr;   // 当类型为XBSON_TYPE_ARRAY时使用，存储数组
        struct {                  // 当类型为XBSON_TYPE_BINARY时使用，存储二进制数据
            XBsonBinarySubtype subtype;  // 二进制子类型
            XByteArray* data;            // 二进制数据
        } binary;
        uint8_t oid[12];          // 当类型为XBSON_TYPE_OBJECT_ID时使用，存储12字节的对象ID
        bool boolean;             // 当类型为XBSON_TYPE_BOOLEAN时使用，存储布尔值
        int64_t datetime;         // 当类型为XBSON_TYPE_DATETIME时使用，存储日期时间（毫秒级时间戳）
        struct {                  // 当类型为XBSON_TYPE_REGEX时使用，存储正则表达式
            XString* pattern;     // 正则表达式模式
            XString* options;     // 正则表达式选项
        } regex;
        int32_t int32;            // 当类型为XBSON_TYPE_INT32时使用，存储32位整数
        struct {                  // 当类型为XBSON_TYPE_TIMESTAMP时使用，存储时间戳
            uint32_t increment;   // 自增计数器
            uint32_t timestamp;   // 时间戳
        } ts;
        int64_t int64;            // 当类型为XBSON_TYPE_INT64时使用，存储64位整数
        uint8_t decimal[16];      // 当类型为XBSON_TYPE_DECIMAL128时使用，存储128位十进制数
        // 注意：XBSON_TYPE_NULL、XBSON_TYPE_MIN_KEY、XBSON_TYPE_MAX_KEY这几种类型不需要额外数据
    } data;
}XBsonValue;

// 构造与析构函数
/**
 * 创建一个指定类型的BSON值
 * @param type 要创建的BSON值类型
 * @return 指向新创建的XBsonValue的指针，失败时返回NULL
 */
XBsonValue* XBsonValue_create(XBsonType type);
XBsonValue* XBsonValue_create_copy(const XBsonValue* other);
XBsonValue* XBsonValue_create_move(XBsonValue* other);
void XBsonValue_init(XBsonValue* value, XBsonType type);

/**
 * 反初始化一个BSON值（释放内部资源，保留对象本身）
 * @param value 要反初始化的BSON值指针
 */
void XBsonValue_deinit(XBsonValue* value);

/**
 * 销毁一个BSON值（释放所有资源）
 * @param value 要销毁的BSON值指针
 */
void XBsonValue_delete(XBsonValue* value);

// 拷贝与移动操作
/**
 * 将源BSON值复制到目标BSON值（深拷贝）
 * @param dest 目标BSON值指针
 * @param src 源BSON值指针
 */
void XBsonValue_copy(XBsonValue* dest, const XBsonValue* src);

/**
 * 将源BSON值移动到目标BSON值（转移内部资源所有权）
 * @param dest 目标BSON值指针
 * @param src 源BSON值指针
 */
void XBsonValue_move(XBsonValue* dest, XBsonValue* src);

// 类型检查函数
/**
 * 获取BSON值的类型
 * @param value BSON值指针
 * @return 该BSON值的类型（XBsonType）
 */
XBsonType XBsonValue_type(const XBsonValue* value);

/**
 * 检查BSON值是否为指定类型
 * @param value BSON值指针
 * @param type 要检查的类型
 * @return 如果类型匹配则返回true，否则返回false
 */
bool XBsonValue_is_type(const XBsonValue* value, XBsonType type);

// 转换函数
/**
 * 将BSON值转换为JSON值
 * @param bson_val 要转换的BSON值指针
 * @return 指向转换后的XJsonValue的指针，失败时返回NULL
 */
XJsonValue* XBsonValue_to_json(const XBsonValue* bson_val);

/**
 * 将JSON值转换为BSON值
 * @param json_val 要转换的JSON值指针
 * @return 指向转换后的XBsonValue的指针，失败时返回NULL
 */
XBsonValue* XBsonValue_from_json(const XJsonValue* json_val);

// 序列化与反序列化函数
/**
 * 将BSON值序列化为字节数据
 * @param value 要序列化的BSON值指针
 * @param key 该BSON值对应的键（字符串）
 * @param output 存储序列化结果的字节数组
 * @return 序列化成功返回true，失败返回false
 */
bool XBsonValue_serialize(const XBsonValue* value, const char* key, XByteArray* output);

/**
 * 从字节数据中反序列化出BSON值
 * @param ptr 指向输入字节数据的指针（会被更新为处理后的位置）
 * @param end 输入字节数据的结束位置指针
 * @param key_out 用于存储反序列化得到的键（输出参数）
 * @return 指向反序列化得到的XBsonValue的指针，失败时返回NULL
 */
XBsonValue* XBsonValue_deserialize(const uint8_t** ptr, const uint8_t* end, XString** key_out);

#ifdef __cplusplus
}
#endif

#endif // XBSONVALUE_H