#ifndef XCHAR_H
#define XCHAR_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct XChar
{
    uint16_t code; // 存储UTF-16编码值（可能是代理对的一部分）
} XChar;

// 创建XChar实例
XChar XChar_from_unicode(uint32_t unicode);
XChar XChar_from_unicode_low(uint32_t unicode);

// 获取Unicode码点（注意：补充平面字符需结合代理对）
uint32_t XChar_unicode(const XChar* ch);

// 字符类型判断
bool XChar_is_letter(const XChar* ch);
bool XChar_is_digit(const XChar* ch);
bool XChar_is_space(const XChar* ch);
bool XChar_is_punct(const XChar* ch);
bool XChar_is_upper(const XChar* ch);
bool XChar_is_lower(const XChar* ch);
bool XChar_is_control(const XChar* ch);       // 新增：控制字符判断
bool XChar_is_symbol(const XChar* ch);        // 新增：符号字符判断
bool XChar_is_emoji(const XChar* ch);         // 新增：表情符号判断
bool XChar_is_fullwidth(const XChar* ch);     // 新增：全角字符判断
bool XChar_is_halfwidth(const XChar* ch);     // 新增：半角字符判断

// 字符转换
XChar XChar_to_upper(const XChar* ch);
XChar XChar_to_lower(const XChar* ch);
XChar XChar_to_fullwidth(const XChar* ch);    // 新增：半角转全角
XChar XChar_to_halfwidth(const XChar* ch);    // 新增：全角转半角

// 数字值转换
int XChar_digit_value(const XChar* ch);

// 代理对相关
bool XChar_is_high_surrogate(const XChar* ch);
bool XChar_is_low_surrogate(const XChar* ch);
bool XChar_is_surrogate(const XChar* ch);
uint32_t XChar_surrogate_to_unicode(const XChar* high, const XChar* low);

// 字符比较
bool XEquality_XChar(const XChar* a, const XChar* b);
int XChar_compare(const XChar* a, const XChar* b);


/**
 * 从UTF-8字节流解析出XChar（UTF-16）
 * @param utf8 输入的UTF-8字节流（以NULL结尾）
 * @param out 输出的XChar数组
 * @param max_out 输出数组的最大容量
 * @return 成功解析的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_from_utf8(const uint8_t* utf8, XChar* out, size_t max_out);

/**
 * 将XChar（UTF-16）转换为UTF-8字节流
 * @param ch XChar数组（以code=0为终止符）
 * @param utf8 输出的UTF-8字节流
 * @param max_utf8 输出缓冲区的最大容量
 * @return 成功写入的字节数（不含终止符），失败返回-1
 */
int64_t XChar_to_utf8(const XChar* ch, uint8_t* utf8, size_t max_utf8);

// --------------------------
// 新增：UTF-32 转换接口
// --------------------------
/**
 * 从UTF-32码点数组创建XChar数组（UTF-16）
 * @param utf32 输入的UTF-32码点数组（以0为终止符）
 * @param out 输出的XChar数组
 * @param max_out 输出数组的最大容量
 * @return 成功转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_from_utf32(const uint32_t* utf32, XChar* out, size_t max_out);

/**
 * 将XChar数组（UTF-16）转换为UTF-32码点数组
 * @param ch XChar数组（以code=0为终止符）
 * @param utf32 输出的UTF-32码点数组
 * @param max_utf32 输出数组的最大容量
 * @return 成功转换的码点数量（不含终止符），失败返回-1
 */
int64_t XChar_to_utf32(const XChar* ch, uint32_t* utf32, size_t max_utf32);

// Latin-1转XChar数组（返回转换的字符数，-1表示失败）
int64_t XChar_from_latin1(const uint8_t* latin1, XChar* out, size_t max_out);
// XChar数组转Latin-1（返回字节数，-1表示包含超出范围的字符）
int64_t XChar_to_latin1(const XChar* ch, uint8_t* latin1, size_t max_latin1);
// --------------------------
// 新增：本地编码转换接口（依赖平台/外部库）
// --------------------------
#ifdef _WIN32
/**
 * 将GBK编码字符串转换为XChar数组（Windows平台）
 * @param gbk GBK编码字符串（以NULL结尾）
 * @param out 输出的XChar数组
 * @param max_out 输出数组的最大容量
 * @return 成功转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_from_gbk(const char* gbk, XChar* out, size_t max_out);

/**
 * 将XChar数组转换为GBK编码字符串（Windows平台）
 * @param ch XChar数组（以code=0为终止符）
 * @param gbk 输出的GBK编码字符串
 * @param max_gbk 输出缓冲区的最大容量
 * @return 成功写入的字节数（不含终止符），失败返回-1
 */
int64_t XChar_to_gbk(const XChar* ch, char* gbk, size_t max_gbk);
#endif

#ifdef __linux__
/**
 * 将Shift-JIS编码字符串转换为XChar数组（Linux平台，依赖iconv）
 * @param sjis Shift-JIS编码字符串（以NULL结尾）
 * @param out 输出的XChar数组
 * @param max_out 输出数组的最大容量
 * @return 成功转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_from_shiftjis(const char* sjis, XChar* out, size_t max_out);

/**
 * 将XChar数组转换为Shift-JIS编码字符串（Linux平台，依赖iconv）
 * @param ch XChar数组（以code=0为终止符）
 * @param sjis 输出的Shift-JIS编码字符串
 * @param max_sjis 输出缓冲区的最大容量
 * @return 成功写入的字节数（不含终止符），失败返回-1
 */
int64_t XChar_to_shiftjis(const XChar* ch, char* sjis, size_t max_sjis);
#endif
/**
 * 将本地编码字符串转换为XChar数组
 * @param local_str 本地编码字符串
 * @param out 输出的XChar数组
 * @param max_out 输出数组的最大容量
 * @return 成功转换的XChar数量（不含终止符），失败返回-1
 */
int64_t XChar_from_local(const char* local_str, XChar* out, size_t max_out);
/**
 * 将XChar数组转换为本地编码字符串
 * @param ch XChar数组（以code=0为终止符）
 * @param local_str 本地编码字符串
 * @param max_local 输出缓冲区的最大容量
 * @return 成功写入的字节数（不含终止符），失败返回-1
 */
int64_t XChar_to_local(const XChar* ch, char* local_str, size_t max_local);



//其他

/*
*  UTF-8 转 GBK 编码（跨平台实现）
* @param   utf8_str - 输入UTF-8字符串（以'\0'结尾）
* @param   gbk_buf  - 输出GBK缓冲区（NULL时仅计算所需大小）
* @param   max_len  - 输出缓冲区大小（含终止符）
* @return 成功返回GBK字节数（不含终止符），失败返回-1
*/
int64_t XUTF8_to_gbk(const char* utf8_str, char* gbk_buf, size_t max_len);
#ifdef __cplusplus
}
#endif
#endif // !XCHAR_H  // 修正宏定义错误