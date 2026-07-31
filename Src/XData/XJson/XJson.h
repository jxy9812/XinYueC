#ifndef XJSON_H
#define XJSON_H

#ifdef __cplusplus
extern "C" {
#endif
#include "CXinYueConfig.h"
#include "XTypes.h"
#include <stdint.h>
typedef enum XJsonDocumentFormat
{
/*     {
         "Array": [
             true,
             999,
             "string"
         ],
         "Key": "Value",
         "null": null
     }
*/
    XJsonDocument_Indented,
// { "Array": [true,999,"string"] ,"Key" : "Value","null" : null }
    XJsonDocument_Compact
} XJsonDocumentFormat;

/**
 * @brief JSON 解析错误码，语义对应 Qt 的 QJsonParseError::ParseError。
 */
typedef enum XJsonParseErrorCode
{
    XJsonParseError_NoError = 0,                 ///< 没有发生错误。
    XJsonParseError_UnterminatedObject,          ///< 对象缺少结束符 `}`。
    XJsonParseError_MissingNameSeparator,        ///< 对象成员名后缺少名称分隔符 `:`。
    XJsonParseError_UnterminatedArray,           ///< 数组缺少结束符 `]`。
    XJsonParseError_MissingValueSeparator,       ///< 数组或对象值之间缺少分隔符 `,`。
    XJsonParseError_IllegalValue,                ///< 出现不合法的 JSON 值或关键字。
    XJsonParseError_TerminationByNumber,         ///< 数字后出现非法终止字符。
    XJsonParseError_IllegalNumber,               ///< 数字格式不符合 JSON 数字语法。
    XJsonParseError_IllegalEscapeSequence,       ///< 字符串转义序列无效。
    XJsonParseError_IllegalUtf8String,           ///< 输入字符串不是合法 UTF-8。
    XJsonParseError_UnterminatedString,          ///< 字符串缺少结束引号 `"`。
    XJsonParseError_MissingObject,               ///< 需要对象的位置没有提供对象。
    XJsonParseError_DeepNesting,                 ///< 嵌套深度超过解析器限制。
    XJsonParseError_DocumentTooLarge,            ///< 文档大小超过解析器限制。
    XJsonParseError_GarbageAtEnd                ///< 根值后仍存在非空白字符。
} XJsonParseErrorCode;

/**
 * @brief JSON 解析错误结果。
 * @details offset 是输入中的字节偏移；无错误时为 -1。
 */
typedef struct XJsonParseError
{
    int64_t offset;
    XJsonParseErrorCode error;
} XJsonParseError;

/**
 * @brief 初始化解析错误结果，允许传入 NULL。
 */
void XJsonParseError_init(XJsonParseError* error);
/**
 * @brief 获取错误码的静态英文描述，不需要释放返回值。
 */
const char* XJsonParseError_errorString(const XJsonParseError* error);
#ifdef __cplusplus
}
#endif

#endif // XJSONARRAY_H
