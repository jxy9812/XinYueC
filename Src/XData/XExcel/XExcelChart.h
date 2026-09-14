/******************************************************************************
 * @file       XExcelChart.h
 * @brief      XExcelChart 图表类（对标 QXlsx::Chart）
 * @author     XinYueC 团队
 * @note       提供图表类型、系列、标题、图例、轴标题、网格线等功能。
 *             对齐 QXlsx::Chart 全部功能
 ******************************************************************************/
#ifndef XEXCELCHART_H
#define XEXCELCHART_H
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

/** @brief 图表类型枚举 */
typedef enum XExcelChart_ChartType {
    XExcelChart_NoStatementChart = 0,    /**< 无图表 */
    XExcelChart_AreaChart,               /**< 面积图 */
    XExcelChart_Area3DChart,             /**< 3D面积图 */
    XExcelChart_LineChart,               /**< 折线图 */
    XExcelChart_Line3DChart,             /**< 3D折线图 */
    XExcelChart_StockChart,              /**< 股价图 */
    XExcelChart_RadarChart,              /**< 雷达图 */
    XExcelChart_ScatterChart,            /**< 散点图 */
    XExcelChart_PieChart,                /**< 饼图 */
    XExcelChart_Pie3DChart,              /**< 3D饼图 */
    XExcelChart_DoughnutChart,           /**< 环形图 */
    XExcelChart_BarChart,                /**< 条形图 */
    XExcelChart_Bar3DChart,              /**< 3D条形图 */
    XExcelChart_OfPieChart,              /**< 复合饼图 */
    XExcelChart_SurfaceChart,            /**< 曲面图 */
    XExcelChart_Surface3DChart,          /**< 3D曲面图 */
    XExcelChart_BubbleChart              /**< 气泡图 */
} XExcelChart_ChartType;

/** @brief 图表轴位置枚举 */
typedef enum XExcelChart_ChartAxisPos { 
    XExcelChart_AxisPosNone = -1,   /**< 无 */
    XExcelChart_AxisPosLeft = 0,    /**< 左侧 */
    XExcelChart_AxisPosRight,       /**< 右侧 */
    XExcelChart_AxisPosTop,         /**< 顶部 */
    XExcelChart_AxisPosBottom       /**< 底部 */
} XExcelChart_ChartAxisPos;

/** @brief 图表系列结构体 */
typedef struct XExcelChart_Series {
    XCellRange m_range;        /**< 数据范围 */
    bool m_headerH;            /**< 水平方向是否有标题 */
    bool m_headerV;            /**< 垂直方向是否有标题 */
    bool m_swapHeaders;        /**< 是否交换行列标题 */
} XExcelChart_Series;

/** @brief XExcelChart 图表结构体 */
typedef struct XExcelChart {
    XAbstractOOXmlFile m_base;            /**< 基类 */
    XExcelChart_ChartType m_chartType;         /**< 图表类型 */
    int m_chartStyle;                      /**< 图表样式 */
    XExcelChart_ChartAxisPos m_legendPos;       /**< 图例位置 */
    bool m_legendOverlay;                  /**< 图例是否覆盖 */
    XString* m_chartTitle;                 /**< 图表标题 */
    XString* m_axisTitleLeft;               /**< 左侧轴标题 */
    XString* m_axisTitleRight;              /**< 右侧轴标题 */
    XString* m_axisTitleTop;                /**< 顶部轴标题 */
    XString* m_axisTitleBottom;             /**< 底部轴标题 */
    XString* m_dataSheetName;               /**< 图表数据所属工作表名称 */
    bool m_majorGridlinesEnable;            /**< 是否启用主网格线 */
    bool m_minorGridlinesEnable;           /**< 是否启用次网格线 */
    int m_row;                             /**< 所在行 */
    int m_col;                             /**< 所在列 */
    int m_width;                           /**< 宽度 */
    int m_height;                          /**< 高度 */
    int m_colOffset;                       /**< 列偏移 */
    int m_rowOffset;                       /**< 行偏移 */
    XVector* m_series;                     /**< 数据系列列表 */
} XExcelChart;

/**
 * @brief      创建图表对象
 * @param parent 父工作表
 * @param flag  创建标志
 * @return      新创建的图表对象指针
 */
XExcelChart* XExcelChart_create(XAbstractSheet* parent, XAbstractOOXmlFile_CreateFlag flag);

/**
 * @brief 创建图表的深拷贝。
 * @param source 源图表指针。
 * @param parent 副本所属的父工作表指针。
 * @return 新图表指针；失败返回 NULL，调用方负责释放。
 */
XExcelChart* XExcelChart_copy(const XExcelChart* source, XAbstractSheet* parent);

/**
 * @brief      销毁图表对象并释放资源
 * @param self 图表对象指针
 */
void XExcelChart_delete(XExcelChart* self);

/**
 * @brief      添加数据系列
 * @param self         图表对象指针
 * @param range        数据范围
 * @param headerH      水平方向是否有标题
 * @param headerV      垂直方向是否有标题
 * @param swapHeaders  是否交换行列标题
 */
void XExcelChart_addSeries(XExcelChart* self, const XCellRange* range, bool headerH, bool headerV, bool swapHeaders);

/**
 * @brief 设置图表数据所属的工作表名称。
 * @param self  图表指针
 * @param name  工作表名称；传入 NULL 清除显式设置
 */
void XExcelChart_setDataSheetName(XExcelChart* self, const XString* name);

/**
 * @brief 使用 UTF-8 文本设置图表数据所属的工作表名称。
 * @param self  图表指针
 * @param name  UTF-8 工作表名称；传入 NULL 清除显式设置
 */
void XExcelChart_setDataSheetName_utf8(XExcelChart* self, const char* name);

/**
 * @brief      设置图表类型
 * @param self 图表对象指针
 * @param type 图表类型
 */
void XExcelChart_setChartType(XExcelChart* self, XExcelChart_ChartType type);

/**
 * @brief      设置图表样式
 * @param self 图表对象指针
 * @param id   样式 ID
 */
void XExcelChart_setChartStyle(XExcelChart* self, int id);

/**
 * @brief      设置轴标题
 * @param self       图表对象指针
 * @param pos        轴位置
 * @param axisTitle  轴标题
 */
void XExcelChart_setAxisTitle(XExcelChart* self, XExcelChart_ChartAxisPos pos, const XString* axisTitle);

/**
 * @brief      设置图表标题
 * @param self  图表对象指针
 * @param title 标题
 */
void XExcelChart_setChartTitle(XExcelChart* self, const XString* title);
/**
 * @brief 使用 UTF-8 文本设置图表标题。
 * @param self  图表指针。
 * @param title UTF-8 标题文本；可为 NULL 清除标题。
 */
void XExcelChart_setChartTitle_utf8(XExcelChart* self, const char* title);

/**
 * @brief      设置图表图例
 * @param self      图表对象指针
 * @param legendPos 图例位置
 * @param overlap   是否覆盖
 */
void XExcelChart_setChartLegend(XExcelChart* self, XExcelChart_ChartAxisPos legendPos, bool overlap);

/**
 * @brief      设置网格线
 * @param self         图表对象指针
 * @param majorEnable  是否启用主网格线
 * @param minorEnable  是否启用次网格线
 */
void XExcelChart_setGridlinesEnable(XExcelChart* self, bool majorEnable, bool minorEnable);

/**
 * @brief      设置图表尺寸
 * @param self   图表对象指针
 * @param width  宽度
 * @param height 高度
 */
void XExcelChart_setSize(XExcelChart* self, int width, int height);

/**
 * @brief      设置图表位置
 * @param self   图表对象指针
 * @param row    行号
 * @param col    列号
 * @param rowOff 行偏移
 * @param colOff 列偏移
 */
void XExcelChart_setPosition(XExcelChart* self, int row, int col, int rowOff, int colOff);

/**
 * @brief      从文件加载图表 XML
 * @param self     图表对象指针
 * @param filePath 文件路径
 * @return         成功返回 true
 */
bool XExcelChart_loadFromXmlFile(XExcelChart* self, const XString* filePath);

/**
 * @brief 从内存中的图表 XML 加载图表。
 * @param self 图表对象指针。
 * @param data XML 数据地址，数据无需 NUL 终止。
 * @param len XML 数据长度（字节）。
 * @return 加载成功返回 true，否则返回 false。
 */
bool XExcelChart_loadFromXmlData(XExcelChart* self, const uint8_t* data, size_t len);

/**
 * @brief      保存图表 XML 到文件
 * @param self     图表对象指针
 * @param filePath 文件路径
 * @return         成功返回 true
 */
bool XExcelChart_saveToXmlFile(XExcelChart* self, const XString* filePath);

/**
 * @brief 将图表序列化为 XML 内存。
 * @param self    图表对象指针。
 * @param outData 输出数据地址；成功后调用方使用 XFree_System 释放。
 * @param outLen  输出数据长度地址。
 * @return 序列化成功返回 true，否则返回 false。
 */
bool XExcelChart_saveToXmlData(XExcelChart* self, uint8_t** outData, size_t* outLen);
#ifdef __cplusplus
}
#endif

#endif /* XEXCELCHART_H */
