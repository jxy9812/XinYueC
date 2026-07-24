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

double XUtility_dateTimeToExcelSerial(int64_t timestampMs, bool date1904);
int64_t XUtility_excelSerialToDateTime(double serial, bool date1904);
void XUtility_safeSheetName(const char* src, char* dst, size_t dstSize);
bool XUtility_isValidSheetName(const char* name);
XString* XUtility_createSheetName(const char* baseName, bool (*exists)(const char*));
double XUtility_epochToExcel(int year, int month, int day, int hour, int min, int sec);
void XUtility_excelToEpoch(double serial, int* year, int* month, int* day, int* hour, int* min, int* sec);

/* ========== XSD 布尔值解析 ========== */
bool XUtility_parseXsdBoolean(const char* value, bool defaultValue);
const char* XUtility_xsdBoolean(bool value);

/* ========== 路径工具 ========== */
void XUtility_splitPath(const char* path, char* dir, size_t dirSize, char* fileName, size_t nameSize);
XString XUtility_getRelFilePath(const char* filePath);

/* ========== 工作表名称工具 ========== */
XString XUtility_escapeSheetName(const char* sheetName);
XString XUtility_unescapeSheetName(const char* sheetName);
bool XUtility_isSpaceReserveNeeded(const char* str);

/* ========== 共享公式转换 ========== */
XString XUtility_convertSharedFormula(const char* rootFormula, const XCellReference* rootCell, const XCellReference* cell);

#ifdef __cplusplus
}
#endif
#endif
