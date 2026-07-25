/******************************************************************************
 * @file       XNumFormatParser.h
 * @brief      XNumFormatParser 数字格式解析器（对标 QXlsx::NumFormatParser）
 * @author     XinYueC 团队
 * @note       解析 Excel 数字格式字符串，判断是否为日期时间格式。
 *             对齐 QXlsx::NumFormatParser 全部功能
 ******************************************************************************/
#ifndef XNUMFORMATPARSER_H
#define XNUMFORMATPARSER_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>

#include "XString.h"
/**
 * @brief  判断数字格式代码是否为日期时间格式
 * @param  formatCode  数字格式代码字符串（如 "yyyy-mm-dd"、"hh:mm:ss"）
 * @return 是日期时间格式返回 true，否则返回 false
 */
bool XNumFormatParser_isDateTime(const XString* formatCode);
#ifdef __cplusplus
}
#endif
#endif
