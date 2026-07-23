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
#include <stdint.h>#include <stdbool.h>
#include "XString.h"
#include "XByteArray.h"
#include "XDateTime.h"

double XUtility_dateTimeToExcelSerial(int64_t timestampMs, bool date1904);
int64_t XUtility_excelSerialToDateTime(double serial, bool date1904);
void XUtility_safeSheetName(const char* src, char* dst, size_t dstSize);
bool XUtility_isValidSheetName(const char* name);
XString* XUtility_createSheetName(const char* baseName, bool (*exists)(const char*));
double XUtility_epochToExcel(int year, int month, int day, int hour, int min, int sec);
void XUtility_excelToEpoch(double serial, int* year, int* month, int* day, int* hour, int* min, int* sec);

#ifdef __cplusplus
}
#endif
#endif
