#ifndef XCHAR_H
#define XCHAR_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief 字符大小写敏感性枚举
 * 用于指定字符串比较或查找时是否区分大小写
 */
typedef enum XCharCaseSensitivity
{
    XCharCaseInsensitive,  // 不区分大小写
    XCharCaseSensitive     // 区分大小写
} XCharCaseSensitivity;

/**
 * @brief XChar字符结构
 * 用于存储UTF-16编码的字符单元，可能是单个字符或代理对的一部分
 */
typedef struct XChar
{
    uint16_t code;  // 存储UTF-16编码值（可能是代理对的一部分）
} XChar;

// --------------------------
// XChar实例创建函数
// --------------------------

/**
 * @brief 从uint16_t创建XChar实例
 * @param code uint16_t类型的字符编码（通常为UTF-16）
 * @return 对应的XChar结构
 */
XChar XChar_from(uint16_t code);

/**
 * @brief 从Unicode码点创建XChar实例（处理基础平面和高代理）
 * @param unicode Unicode码点（0~0x10FFFF）
 * @return 成功返回对应的XChar结构（基础平面直接存储，补充平面返回高代理），无效码点返回空XC（code=0）
 */
XChar XChar_from_unicode(uint32_t unicode);

/**
 * @brief 创建补充平面字符的低代理
 * @param unicode Unicode码点（0x10000~0x10FFFF）
 * @return 对应的低代理XChar结构，无效码点返回空XChar（code=0）
 */
XChar XChar_from_unicode_low(uint32_t unicode);

// --------------------------
// Unicode码点操作函数
// --------------------------

/**
 * @brief 获取XChar的Unicode码点（单字符或高代理）
 * @param ch XChar指针（不可为NULL）
 * @return 对应的Unicode码点，ch为NULL时返回0
 */
uint32_t XChar_unicode(const XChar* ch);

// --------------------------
// 字符类型判断函数
// --------------------------

/**
 * @brief 判断XChar是否为字母（支持多语言）
 * @param ch XChar指针
 * @return 是字母返回true，否则返回false
 */
bool XChar_is_letter(const XChar* ch);

/**
 * @brief 判断XChar是否为数字字符
 * @param ch XChar指针
 * @return 是数字返回true，否则返回false
 */
bool XChar_is_digit(const XChar* ch);

/**
 * @brief 判断XChar是否为空白字符
 * @param ch XChar指针
 * @return 是空白字符返回true，否则返回false
 */
bool XChar_is_space(const XChar* ch);

/**
 * @brief 判断XChar是否为标点符号
 * @param ch XChar指针
 * @return 是标点符号返回true，否则返回false
 */
bool XChar_is_punct(const XChar* ch);

/**
 * @brief 判断XChar是否为大写字母
 * @param ch XChar指针
 * @return 是大写字母返回true，否则返回false
 */
bool XChar_is_upper(const XChar* ch);

/**
 * @brief 判断XChar是否为小写字母
 * @param ch XChar指针
 * @return 是小写字母返回true，否则返回false
 */
bool XChar_is_lower(const XChar* ch);

/**
 * @brief 判断XChar是否为控制字符
 * @param ch XChar指针
 * @return 是控制字符返回true，否则返回false
 */
bool XChar_is_control(const XChar* ch);

/**
 * @brief 判断XChar是否为符号字符
 * @param ch XChar指针
 * @return 是符号字符返回true，否则返回false
 */
bool XChar_is_symbol(const XChar* ch);

/**
 * @brief 判断XChar是否为表情符号
 * @param ch XChar指针
 * @return 是表情符号返回true，否则返回false
 */
bool XChar_is_emoji(const XChar* ch);

/**
 * @brief 判断XChar是否为全角字符
 * @param ch XChar指针
 * @return 是全角字符返回true，否则返回false
 */
bool XChar_is_fullwidth(const XChar* ch);

/**
 * @brief 判断XChar是否为半角字符
 * @param ch XChar指针
 * @return 是半角字符返回true，否则返回false
 */
bool XChar_is_halfwidth(const XChar* ch);

// --------------------------
// 字符转换函数
// --------------------------

/**
 * @brief 将XChar转换为大写形式
 * @param ch 源XChar指针
 * @return 转换后的大写XChar
 */
XChar XChar_to_upper(const XChar* ch);

/**
 * @brief 将XChar转换为小写形式
 * @param ch 源XChar指针
 * @return 转换后的小写XChar
 */
XChar XChar_to_lower(const XChar* ch);

/**
 * @brief 将半角字符转换为全角字符
 * @param ch 源XChar指针（半角字符）
 * @return 转换后的全角XChar
 */
XChar XChar_to_fullwidth(const XChar* ch);

/**
 * @brief 将全角字符转换为半角字符
 * @param ch 源XChar指针（全角字符）
 * @return 转换后的半角XChar
 */
XChar XChar_to_halfwidth(const XChar* ch);

// --------------------------
// 数字值转换函数
// --------------------------

/**
 * @brief 获取XChar对应的数字值
 * @param ch XChar指针（应为数字字符）
 * @return 对应的整数数值，非数字字符返回-1
 */
int XChar_digit_value(const XChar* ch);

// --------------------------
// 代理对相关函数
// --------------------------

/**
 * @brief 判断XChar是否为高代理
 * @param ch XChar指针
 * @return 是高代理返回true，否则返回false
 */
bool XChar_is_high_surrogate(const XChar* ch);

/**
 * @brief 判断XChar是否为低代理
 * @param ch XChar指针
 * @return 是低代理返回true，否则返回false
 */
bool XChar_is_low_surrogate(const XChar* ch);

/**
 * @brief 判断XChar是否为代理（高代理或低代理）
 * @param ch XChar指针
 * @return 是代理返回true，否则返回false
 */
bool XChar_is_surrogate(const XChar* ch);

/**
 * @brief 将高代理和低代理转换为Unicode码点
 * @param high 高代理XChar指针
 * @param low 低代理XChar指针
 * @return 对应的Unicode码点，无效代理对返回0
 */
uint32_t XChar_surrogate_to_unicode(const XChar* high, const XChar* low);

// --------------------------
// 字符比较函数
// --------------------------

/**
 * @brief 判断两个XChar是否相等（默认区分大小写）
 * @param a 第一个XChar指针
 * @param b 第二个XChar指针
 * @return 相等返回true，否则返回false
 */
bool XEquality_XChar(const XChar* a, const XChar* b);

/**
 * @brief 比较两个XChar是否相等（支持大小写敏感性）
 * @param a 第一个XChar指针
 * @param b 第二个XChar指针
 * @param cs 大小写敏感性（区分/不区分大小写）
 * @return 相等返回true，否则返回false
 */
bool XChar_equals(const XChar* a, const XChar* b, XCharCaseSensitivity cs);

/**
 * @brief 比较两个XChar的大小
 * @param a 第一个XChar指针
 * @param b 第二个XChar指针
 * @return a < b返回-1，a > b返回1，相等返回0
 */
int XChar_compare(const XChar* a, const XChar* b);

// --------------------------
// UTF-8编码转换函数
// --------------------------

/**
 * @brief 从UTF-8字节流解析出XChar（UTF-16）
 * @param utf8 输入的UTF-8字节流（以NULL结尾）
 * @param out 输出的XChar数组
 * @param max_out 输出数组的最大容量（含终止符）
 * @return 成功解析的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_from_utf8(const uint8_t* utf8, XChar* out, size_t max_out);

/**
 * @brief 将XChar（UTF-16）转换为UTF-8字节流
 * @param ch XChar数组（以code=0为终止符）
 * @param utf8 输出的UTF-8字节流
 * @param max_utf8 输出缓冲区的最大容量（含终止符）
 * @return 成功写入的字节数（不含终止符），失败返回-1
 */
int64_t XChar_to_utf8(const XChar* ch, uint8_t* utf8, size_t max_utf8);

// --------------------------
// UTF-16编码转换函数
// --------------------------

/**
 * @brief 从UTF-16编码字符串转换为XChar数组
 * @param utf16_str 待转换的UTF-16字符串（uint16_t类型，以L'\0'为终止符）
 * @param out_xchars 输出的XChar数组（以code=0为终止符）
 * @param max_count 输出数组的最大容量（含终止符）
 * @return 成功返回转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_from_utf16(const uint16_t* utf16_str, XChar* out_xchars, size_t max_count);

/**
 * @brief 将XChar数组转换为UTF-16编码字符串
 * @param xchars XChar数组（以code=0为终止符）
 * @param out_buf 输出的UTF-16字符串缓冲区（uint16_t类型）
 * @param buf_size 输出缓冲区的最大容量（含终止符）
 * @return 成功返回写入的uint16_t数量（不含终止符），失败返回-1
 */
int64_t XChar_to_utf16(const XChar* xchars, uint16_t* out_buf, size_t buf_size);

// --------------------------
// UTF-32编码转换函数
// --------------------------

/**
 * @brief 从UTF-32码点数组创建XChar数组（UTF-16）
 * @param utf32 输入的UTF-32码点数组（以0为终止符）
 * @param out 输出的XChar数组（以code=0为终止符）
 * @param max_out 输出数组的最大容量（含终止符）
 * @return 成功转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_from_utf32(const uint32_t* utf32, XChar* out, size_t max_out);

/**
 * @brief 将XChar数组（UTF-16）转换为UTF-32码点数组
 * @param ch XChar数组（以code=0为终止符）
 * @param utf32 输出的UTF-32码点数组（以0为终止符）
 * @param max_utf32 输出数组的最大容量（含终止符）
 * @return 成功转换的码点数量（不含终止符），失败返回-1
 */
int64_t XChar_to_utf32(const XChar* ch, uint32_t* utf32, size_t max_utf32);

// --------------------------
// Latin-1编码转换函数
// --------------------------

/**
 * @brief 从Latin1编码字符串转换为XChar数组
 * @param latin1 待转换的Latin1字符串（uint8_t类型，以'\0'为终止符）
 * @param out 输出的XChar数组（以code=0为终止符）
 * @param max_out 输出数组的最大容量（含终止符）
 * @return 成功返回转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_from_latin1(const uint8_t* latin1, XChar* out, size_t max_out);

/**
 * @brief 将XChar数组转换为Latin1编码字符串
 * @param ch XChar数组（以code=0为终止符）
 * @param latin1 输出的Latin1字符串缓冲区（uint8_t类型）
 * @param max_latin1 输出缓冲区的最大容量（含终止符）
 * @return 成功返回写入的字节数（不含终止符），码点超出Latin1范围或失败返回-1
 */
int64_t XChar_to_latin1(const XChar* ch, uint8_t* latin1, size_t max_latin1);

/**
 * @brief 将GBK编码字符串转换为XChar数组（Windows平台）
 * @param gbk GBK编码字符串（以NULL结尾）
 * @param out 输出的XChar数组（以code=0为终止符）
 * @param max_out 输出数组的最大容量（含终止符）
 * @return 成功转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_from_gbk(const char* gbk, XChar* out, size_t max_out);

/**
 * @brief 将XChar数组转换为GBK编码字符串（Windows平台）
 * @param ch XChar数组（以code=0为终止符）
 * @param gbk 输出的GBK编码字符串缓冲区
 * @param max_gbk 输出缓冲区的最大容量（含终止符）
 * @return 成功写入的字节数（不含终止符），失败返回-1
 */
int64_t XChar_to_gbk(const XChar* ch, char* gbk, size_t max_gbk);

// --------------------------
// 本地编码转换函数
// --------------------------

/**
 * @brief 将本地编码字符串转换为XChar数组
 * @param local_str 本地编码字符串（Windows为GBK，Linux为UTF-8，以'\0'为终止符）
 * @param out 输出的XChar数组（以code=0为终止符）
 * @param max_out 输出数组的最大容量（含终止符）
 * @return 成功返回转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_from_local(const char* local_str, XChar* out, size_t max_out);

/**
 * @brief 将XChar数组转换为本地编码字符串
 * @param ch XChar数组（以code=0为终止符）
 * @param local_str 输出的本地编码字符串缓冲区（Windows为GBK，Linux为UTF-8）
 * @param max_local 输出缓冲区的最大容量（含终止符）
 * @return 成功返回写入的字节数（不含终止符），失败返回-1
 */
int64_t XChar_to_local(const XChar* ch, char* local_str, size_t max_local);

// --------------------------
// UTF-8与GBK互转函数（跨平台）
// --------------------------

/**
 * @brief UTF-8转GBK编码（跨平台实现）
 * @param utf8_str 输入UTF-8字符串（以'\0'结尾）
 * @param gbk_buf 输出GBK缓冲区（NULL时仅计算所需大小）
 * @param max_len 输出缓冲区大小（含终止符）
 * @return 成功返回GBK字节数（不含终止符），失败返回-1
 */
int64_t XUTF8_to_gbk(const char* utf8_str, char* gbk_buf, size_t max_len);

/**
 * @brief GBK转UTF-8编码（跨平台实现）
 * @param gbk_str 输入GBK字符串（以'\0'结尾）
 * @param utf8_buf 输出UTF-8缓冲区（NULL时仅计算所需大小）
 * @param max_len 输出缓冲区大小（含终止符）
 * @return 成功返回UTF-8字节数（不含终止符），失败返回-1
 */
int64_t XGBK_to_utf8(const char* gbk_str, char* utf8_buf, size_t max_len);

#ifdef __cplusplus
}
#endif
#endif // XCHAR_H