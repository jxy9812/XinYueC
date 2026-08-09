/******************************************************************************
 * @file       XUtility.h
 * @brief      XUtility 工具函数（对标 QXlsx::Utility）
 * @author     XinYueC 团队
 * @note       提供日期时间转换、文件路径处理、工作表名称生成等工具函数。
 *             对齐 QXlsx::Utility 全部功能
 ******************************************************************************/
#ifndef XUTILITY_H
#define XUTILITY_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>

#include "XString.h"
#include "XByteArray.h"
#include "XDateTime.h"
#include "XCellReference.h"

/* ========== 日期时间转换 ========== */

/**
 * @brief  将 Unix 时间戳（毫秒）转换为 Excel 序列号
 * @param  timestampMs  Unix 时间戳（毫秒）
 * @param  date1904     是否使用 1904 日期系统
 * @return Excel 日期序列号（浮点数，整数部分为天数，小数部分为时间）
 */
double XUtility_dateTimeToExcelSerial(int64_t timestampMs, bool date1904);

/**
 * @brief  将 Excel 序列号转换为 Unix 时间戳（毫秒）
 * @param  serial    Excel 日期序列号
 * @param  date1904  是否使用 1904 日期系统
 * @return Unix 时间戳（毫秒）
 */
int64_t XUtility_excelSerialToDateTime(double serial, bool date1904);

/**
 * @brief  将年月日时分秒转换为 Excel 序列号
 * @param  year   年
 * @param  month  月（1-12）
 * @param  day    日（1-31）
 * @param  hour   时（0-23）
 * @param  min    分（0-59）
 * @param  sec    秒（0-59）
 * @return Excel 日期序列号
 */
double XUtility_epochToExcel(int year, int month, int day, int hour, int min, int sec);

/**
 * @brief  将 Excel 序列号转换为年月日时分秒
 * @param  serial  Excel 日期序列号
 * @param  year    [out] 年
 * @param  month   [out] 月
 * @param  day     [out] 日
 * @param  hour    [out] 时
 * @param  min     [out] 分
 * @param  sec     [out] 秒
 */
void XUtility_excelToEpoch(double serial, int* year, int* month, int* day, int* hour, int* min, int* sec);

/* ========== 工作表名称工具 ========== */

/**
 * @brief  将字符串转换为安全的工作表名称（替换非法字符为空格，截断到31字符）
 * @param  src  原始名称字符串
 * @return 安全的工作表名称（栈上 XString，调用者需调用 XString_deinit_base 释放）
 */
XString XUtility_safeSheetName(const XString* src);

/**
 * @brief  判断名称是否为合法的工作表名称
 * @param  name  待检查的名称
 * @return 合法返回 true，否则返回 false
 */
bool XUtility_isValidSheetName(const XString* name);

/**
 * @brief  创建唯一的工作表名称
 * @param  baseName  基础名称（为 NULL 或空时使用 "Sheet"）
 * @param  exists    判断名称是否已存在的回调函数（可为 NULL）
 * @return 新创建的 XString*（调用者负责释放），失败返回 NULL
 */
XString* XUtility_createSheetName(const XString* baseName, bool (*exists)(const XString*));

/* ========== XSD 布尔值解析 ========== */

/**
 * @brief  解析 XSD 布尔值字符串（"true"/"false"/"1"/"0"）
 * @param  value         待解析的字符串
 * @param  defaultValue  无法解析时的默认值
 * @return 解析后的布尔值
 */
bool XUtility_parseXsdBoolean(const XString* value, bool defaultValue);

/**
 * @brief  将布尔值转换为 XSD 布尔字符串（"true" 或 "false"）
 * @param  value  布尔值
 * @return 静态 XString 指针（无需释放）
 */
const XString* XUtility_xsdBoolean(bool value);

/* ========== 路径工具 ========== */

/**
 * @brief  将路径拆分为目录部分和文件名部分
 * @param  path      完整路径
 * @param  dir       [out] 目录部分（可为 NULL）
 * @param  fileName  [out] 文件名部分（可为 NULL）
 */
void XUtility_splitPath(const XString* path, XString* dir, XString* fileName);

/**
 * @brief  获取路径中的文件名部分（去掉目录前缀）
 * @param  filePath  完整文件路径
 * @return 文件名（栈上 XString，调用者需调用 XString_deinit_base 释放）
 */
XString XUtility_getRelFilePath(const XString* filePath);

/* ========== 工作表名称转义 ========== */

/**
 * @brief  对工作表名称进行转义（包含特殊字符时用单引号包裹）
 * @param  sheetName  原始工作表名称
 * @return 转义后的名称（栈上 XString，调用者需调用 XString_deinit_base 释放）
 */
XString XUtility_escapeSheetName(const XString* sheetName);

/**
 * @brief  对工作表名称进行反转义（去掉首尾单引号，还原内部 '' 为 '）
 * @param  sheetName  转义后的工作表名称
 * @return 反转义后的名称（栈上 XString，调用者需调用 XString_deinit_base 释放）
 */
XString XUtility_unescapeSheetName(const XString* sheetName);

/**
 * @brief  判断字符串首尾是否包含空格（需要保留空格标记）
 * @param  str  待检查的字符串
 * @return 首尾有空格返回 true
 */
bool XUtility_isSpaceReserveNeeded(const XString* str);

/* ========== 共享公式转换 ========== */

/**
 * @brief  将共享公式从根单元格转换到目标单元格（自动偏移相对引用）
 * @param  rootFormula  根公式文本
 * @param  rootCell     根单元格引用
 * @param  cell         目标单元格引用
 * @return 转换后的公式（栈上 XString，调用者需调用 XString_deinit_base 释放）
 */
XString XUtility_convertSharedFormula(const XString* rootFormula, const XCellReference* rootCell, const XCellReference* cell);

#ifdef __cplusplus
}
#endif
#endif
