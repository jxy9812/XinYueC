#ifndef XBSON_H
#define XBSON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XTypes.h"
#include "XString.h"
#include "XByteArray.h"

// BSON 类型枚举
typedef enum {
    XBSON_TYPE_DOUBLE = 0x01,         // 双精度浮点数类型
    XBSON_TYPE_STRING = 0x02,         // 字符串类型
    XBSON_TYPE_DOCUMENT = 0x03,       // 文档类型
    XBSON_TYPE_ARRAY = 0x04,          // 数组类型
    XBSON_TYPE_BINARY = 0x05,         // 二进制数据类型
    XBSON_TYPE_OBJECT_ID = 0x07,      // 对象ID类型
    XBSON_TYPE_BOOL = 0x08,             // 布尔类型
    XBSON_TYPE_DATETIME = 0x09,       // 日期时间类型
    XBSON_TYPE_NULL = 0x0A,           // 空值类型
    XBSON_TYPE_REGEX = 0x0B,          // 正则表达式类型
    XBSON_TYPE_JAVASCRIPT = 0x0D,     // JavaScript代码类型
    XBSON_TYPE_JAVASCRIPT_SCOPE = 0x0F, // 带作用域的JavaScript代码类型
    XBSON_TYPE_INT32 = 0x10,          // 32位整数类型
    XBSON_TYPE_TIMESTAMP = 0x11,      // 时间戳类型
    XBSON_TYPE_INT64 = 0x12,          // 64位整数类型
    XBSON_TYPE_DECIMAL128 = 0x13,     // 128位十进制类型
    XBSON_TYPE_MIN_KEY = 0xFF,        // 最小键类型（用于比较）
    XBSON_TYPE_MAX_KEY = 0x7F         // 最大键类型（用于比较）
} XBsonType;

// BSON 二进制子类型
typedef enum {
    XBSON_BINARY_GENERIC = 0x00,      // 通用二进制数据
    XBSON_BINARY_FUNCTION = 0x01,     // 函数二进制数据
    XBSON_BINARY_UUID = 0x04,         // UUID（通用唯一识别码）二进制数据
    XBSON_BINARY_MD5 = 0x05,          // MD5哈希值二进制数据
    XBSON_BINARY_ENCRYPTED = 0x06,    // 加密数据二进制数据
    XBSON_BINARY_COLUMN = 0x07        // 列数据二进制数据
} XBsonBinarySubtype;

// 前向声明（用于后续结构体定义的引用）
typedef struct XBsonValue XBsonValue;
typedef struct XBsonArray XBsonArray;
typedef struct XBsonObject XBsonObject;
typedef struct XBsonDocument XBsonDocument;

#ifdef __cplusplus
}
#endif

#endif // XBSON_H