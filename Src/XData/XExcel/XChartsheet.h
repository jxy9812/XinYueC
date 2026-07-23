/******************************************************************************
 * @file       XChartsheet.h
 * @brief      XChartsheet 图表工作表类（对标 QXlsx::Chartsheet）
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCHARTSHEET_H
#define XCHARTSHEET_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>#include <stdbool.h>#include <stddef.h>
#include "XAbstractSheet.h"
#include "XChart.h"
typedef struct XChartsheet {
    XAbstractSheet m_base;
    XChart* m_chart;
} XChartsheet;
XChartsheet* XChartsheet_create(const char* sheetName, int sheetId, void* book, XAbstractOOXmlFile_CreateFlag flag);
void XChartsheet_delete(XChartsheet* self);
void XChartsheet_setChart(XChartsheet* self, XChart* chart);
XChart* XChartsheet_chart(const XChartsheet* self);
#ifdef __cplusplus
}
#endif
#endif
