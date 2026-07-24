/******************************************************************************
 * @file       XChart.h
 * @brief      XChart 图表类（对标 QXlsx::Chart）
 * @author     XinYueC 团队
 * @note       提供图表类型、系列、标题、图例、轴标题、网格线等功能。
 *             对齐 QXlsx::Chart 全部功能
 ******************************************************************************/
#ifndef XCHART_H
#define XCHART_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>

#include <stdbool.h>

#include <stddef.h>

#include "XString.h"
#include "XAbstractOOXmlFile.h"
#include "XCellRange.h"
typedef struct XAbstractSheet XAbstractSheet;

typedef enum XChart_ChartType {
    XChart_NoStatementChart = 0,
    XChart_AreaChart, XChart_Area3DChart, XChart_LineChart, XChart_Line3DChart,
    XChart_StockChart, XChart_RadarChart, XChart_ScatterChart,
    XChart_PieChart, XChart_Pie3DChart, XChart_DoughnutChart,
    XChart_BarChart, XChart_Bar3DChart, XChart_OfPieChart,
    XChart_SurfaceChart, XChart_Surface3DChart, XChart_BubbleChart
} XChart_ChartType;
typedef enum XChart_ChartAxisPos { XChart_AxisPosNone = -1, XChart_AxisPosLeft = 0, XChart_AxisPosRight, XChart_AxisPosTop, XChart_AxisPosBottom } XChart_ChartAxisPos;

typedef struct XChart_Series {
    XCellRange m_range;
    bool m_headerH; bool m_headerV; bool m_swapHeaders;
} XChart_Series;

typedef struct XChart {
    XAbstractOOXmlFile m_base;
    XChart_ChartType m_chartType;
    int m_chartStyle;
    XChart_ChartAxisPos m_legendPos;
    bool m_legendOverlay;
    XString* m_chartTitle;
    XString* m_axisTitleLeft;
    XString* m_axisTitleRight;
    XString* m_axisTitleTop;
    XString* m_axisTitleBottom;
    bool m_majorGridlinesEnable;
    bool m_minorGridlinesEnable;
    int m_row; int m_col; int m_width; int m_height;
    int m_colOffset; int m_rowOffset;
    XVector* m_series;
} XChart;

XChart* XChart_create(XAbstractSheet* parent, XAbstractOOXmlFile_CreateFlag flag);
void XChart_delete(XChart* self);
void XChart_addSeries(XChart* self, const XCellRange* range, bool headerH, bool headerV, bool swapHeaders);
void XChart_setChartType(XChart* self, XChart_ChartType type);
void XChart_setChartStyle(XChart* self, int id);
void XChart_setAxisTitle(XChart* self, XChart_ChartAxisPos pos, const char* axisTitle);
void XChart_setChartTitle(XChart* self, const char* title);
void XChart_setChartLegend(XChart* self, XChart_ChartAxisPos legendPos, bool overlap);
void XChart_setGridlinesEnable(XChart* self, bool majorEnable, bool minorEnable);
void XChart_setSize(XChart* self, int width, int height);
void XChart_setPosition(XChart* self, int row, int col, int rowOff, int colOff);
bool XChart_loadFromXmlFile(XChart* self, const char* filePath);
bool XChart_saveToXmlFile(XChart* self, const char* filePath);
#ifdef __cplusplus
}
#endif

/* ========== XML 序列化 ========== */
/**
 * @brief     保存图表 XML 到文件
 * @param self     XChart 指针
 * @param filePath 文件路径
 * @return    成功返回true
 */
bool XChart_saveToXmlFile(XChart* self, const char* filePath);
/**
 * @brief     从文件加载图表 XML
 * @param self     XChart 指针
 * @param filePath 文件路径
 * @return    成功返回true
 */
bool XChart_loadFromXmlFile(XChart* self, const char* filePath);

#endif

