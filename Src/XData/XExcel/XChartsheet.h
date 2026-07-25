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
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "XAbstractSheet.h"
#include "XChart.h"
/** @brief XChartsheet 图表工作表结构体 */
typedef struct XChartsheet {
    XAbstractSheet m_base;         /**< 基类 */
    XChart* m_chart;              /**< 关联的图表 */
} XChartsheet;
/**
 * @brief  创建图表工作表
 * @param  sheetName  工作表名称
 * @param  sheetId    工作表 ID
 * @param  book       所属工作簿指针
 * @param  flag       创建标志
 * @return 成功返回 XChartsheet 指针，失败返回 NULL
 */
XChartsheet* XChartsheet_create(const XString* sheetName, int sheetId, void* book, XAbstractOOXmlFile_CreateFlag flag);

/**
 * @brief  销毁图表工作表并释放资源
 * @param  self  XChartsheet 指针
 */
void XChartsheet_delete(XChartsheet* self);

/**
 * @brief  设置图表工作表关联的图表对象
 * @param  self   XChartsheet 指针
 * @param  chart  图表对象（所有权不转移）
 */
void XChartsheet_setChart(XChartsheet* self, XChart* chart);

/**
 * @brief  获取图表工作表关联的图表对象
 * @param  self  XChartsheet 指针
 * @return 图表对象指针，未设置返回 NULL
 */
XChart* XChartsheet_chart(const XChartsheet* self);
#ifdef __cplusplus
}
#endif
#endif
