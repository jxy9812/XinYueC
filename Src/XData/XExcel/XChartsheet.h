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
    bool m_ownsChart;             /**< 是否拥有图表生命周期（加载文档时为 true） */
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
 * @brief 使用 UTF-8 名称创建图表工作表。
 * @param sheetName UTF-8 工作表名称。
 * @param sheetId   工作表 ID。
 * @param book      所属工作簿指针。
 * @param flag      创建标志。
 * @return 新图表工作表指针；失败返回 NULL，调用方负责释放。
 */
XChartsheet* XChartsheet_create_utf8(const char* sheetName, int sheetId, void* book, XAbstractOOXmlFile_CreateFlag flag);

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

/**
 * @brief 将 Chartsheet 序列化为 OOXML 数据。
 * @param self    Chartsheet 指针。
 * @param outData 输出数据地址；成功后调用方使用 XFree_System 释放。
 * @param outLen  输出数据长度地址。
 * @return 序列化成功返回 true，否则返回 false。
 */
bool XChartsheet_saveToXmlData(const XChartsheet* self, uint8_t** outData, size_t* outLen);

/**
 * @brief 从 OOXML 数据加载 Chartsheet 基本结构。
 * @param self Chartsheet 指针。
 * @param data XML 数据地址，数据无需 NUL 终止。
 * @param len  XML 数据长度（字节）。
 * @return 加载成功返回 true，否则返回 false。
 */
bool XChartsheet_loadFromXmlData(XChartsheet* self, const uint8_t* data, size_t len);
#ifdef __cplusplus
}
#endif
#endif
