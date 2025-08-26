#ifndef XPRINTF_H
#define XPRINTF_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XDataStructConfig.h"
#include"XTypes.h"
#include"XChar.h"
#include<stdio.h>
#include<stdint.h>
// -------------------------- 打印函数 --------------------------

/**
 * @brief 打印 XString 字符串（使用本地编码）
 * @param str XString 对象指针
 * @return 打印的字符数（参考 printf 返回值）
 */
int XPrintf_string(const XString* str);

/**
 * @brief 打印 UTF-8 编码字符串
 * @param utf8_str 待打印的 UTF-8 字符串
 * @return 打印的字符数（参考 printf 返回值）
 */
int XPrintf_utf8(const char* utf8_str);

/**
 * @brief 格式化打印 UTF-8 字符串
 * @param format 格式化字符串（UTF-8）
 * @param ... 可变参数列表
 * @return 打印的字符数（参考 printf 返回值）
 */
int XPrintf_utf8_fmt(const char* format, ...);

// 输出单个XChar字符
int XPrintf_char(XChar* ch);
#ifdef __cplusplus
}
#endif	
#endif