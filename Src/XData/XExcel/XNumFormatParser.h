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
bool XNumFormatParser_isDateTime(const char* formatCode);
#ifdef __cplusplus
}
#endif
#endif
