/**
 * @file       XChart.h
 * @brief      XChart 图表模型（对标 Qt Charts 6.8 QChart）。
 * @details    管理：标题文本、图例可见性、X/Y 数值轴（XValueAxis）、
 *             折线序列（XLineSeries）与饼图序列（XPieSeries）列表；
 *             主题色板（对标 ChartThemeLight 子集）。渲染由 XChartView
 *             经 XChart 绘制接口完成；本类只持有数据与布局参数。
 * @note       模块总开关 XCHARTS_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCHART_H
#define XCHART_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XValueAxis.h"

#if XCHARTS_ON

typedef struct XValueAxis   XValueAxis;
typedef struct XLineSeries  XLineSeries;
typedef struct XBarSeries   XBarSeries;
typedef struct XScatterSeries XScatterSeries;
typedef struct XAreaSeries  XAreaSeries;
typedef struct XSplineSeries XSplineSeries;
typedef struct XPieSeries   XPieSeries;

XCLASS_DEFINE_BEGING(XChart)
XCLASS_DEFINE_EXTEND_END(XChart, XObject)

/** 
 * @brief 图表模型（对标 QChart）。
 *
 *        持有标题/图例/坐标轴与序列集合；序列按加入顺序渲染。
 */
typedef struct XChart
{
    char m_title[128];        /**< 图表标题（UTF-8）。 */
    bool m_legendVisible;     /**< 图例可见（默认 true）。 */
    bool m_titleVisible;      /**< 标题可见（默认 true）。 */
    XValueAxis* m_axisX;      /**< X 数值轴（内部拥有）。 */
    XValueAxis* m_axisY;      /**< Y 数值轴（内部拥有）。 */
    XLineSeries* m_lineSeries[8];   /**< 折线序列集合（内部拥有）。 */
    int m_lineCount;          /**< 折线序列数。 */
    XPieSeries* m_pieSeries;  /**< 饼图序列（内部拥有；单例）。 */
    XBarSeries* m_barSeries[4];     /**< 柱状序列集合（内部拥有）。 */
    int m_barCount;           /**< 柱状序列数。 */
    XScatterSeries* m_scatterSeries[4]; /**< 散点序列集合。 */
    int m_scatterCount;       /**< 散点序列数。 */
    XAreaSeries* m_areaSeries[4];   /**< 面积序列集合。 */
    int m_areaCount;          /**< 面积序列数。 */
    XSplineSeries* m_splineSeries[4]; /**< 样条序列集合。 */
    int m_splineCount;        /**< 样条序列数。 */
    uint32_t m_theme[8];      /**< 主题系列色板（ARGB）。 */
    int m_animationDuration;  /**< 动画时长（预留）。 */
} XChart;

XVtable* XChart_class_init(void);

/**
 * @brief 初始化图表模型（栈/嵌入使用）。
 *
 * @param self 目标图表指针，不能为空。
 * @return 无返回值。
 */
void XChart_init(XChart* self);

/**
 * @brief 堆上创建图表模型。
 *
 * @param memory 内存类型。
 * @return 图表指针；分配失败返回 NULL。
 */
XChart* XChart_create_ex(XMemoryType memory);
#define XChart_create() XChart_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构：释放轴与全部序列。 @param self 目标图表指针。 @return 无返回值。 */
void XChart_deinit(XChart* self);

/** @brief 设置标题文本。 @param self 目标图表指针。 @param title UTF-8 标题。 @return 无返回值。 */
void XChart_setTitle(XChart* self, const char* title);
/** @brief 读取标题文本。 @param self 目标图表指针。 @return 标题（UTF-8）。 */
const char* XChart_title(const XChart* self);
/** @brief 设置标题可见性。 @param self 目标图表指针。 @param visible true 显示。 @return 无返回值。 */
void XChart_setTitleVisible(XChart* self, bool visible);
/** @brief 查询标题可见性。 @param self 目标图表指针。 @return 可见返回 true。 */
bool XChart_isTitleVisible(const XChart* self);
/** @brief 设置图例可见性（对标 setLegendVisible）。 @param self 目标图表指针。 @param visible true 显示。 @return 无返回值。 */
void XChart_setLegendVisible(XChart* self, bool visible);
/** @brief 查询图例可见性。 @param self 目标图表指针。 @return 可见返回 true。 */
bool XChart_isLegendVisible(const XChart* self);

/**
 * @brief 添加折线序列并接管所有权（对标 addSeries）。
 *
 * @param self   目标图表指针。
 * @param series 折线序列指针；成功后由图表管理生命周期。
 * @return 无返回值。
 */
void XChart_addLineSeries(XChart* self, XLineSeries* series);

/**
 * @brief 设置饼图序列（单例；对标 addSeries(QPieSeries*)）。
 *
 * @param self   目标图表指针。
 * @param series 饼图序列指针；成功后由图表管理生命周期。
 * @return 无返回值。
 */
void XChart_setPieSeries(XChart* self, XPieSeries* series);

/** @brief 查询折线序列数。 @param self 目标图表指针。 @return 折线序列数。 */
int XChart_lineSeriesCount(const XChart* self);
/** @brief 按下标取折线序列。 @param self 目标图表指针。 @param index 下标。 @return 序列指针；越界返回 NULL。 */
XLineSeries* XChart_lineSeries(const XChart* self, int index);
/** @brief 查询饼图序列。 @param self 目标图表指针。 @return 饼图序列指针；未设置返回 NULL。 */
XPieSeries* XChart_pieSeries(const XChart* self);

/** @brief 添加柱状序列（接管所有权）。 @param self 目标图表指针。 @param series 柱状序列指针。 @return 无返回值。 */
void XChart_addBarSeries(XChart* self, XBarSeries* series);
/** @brief 添加散点序列（接管所有权）。 @param self 目标图表指针。 @param series 散点序列指针。 @return 无返回值。 */
void XChart_addScatterSeries(XChart* self, XScatterSeries* series);
/** @brief 添加面积序列（接管所有权）。 @param self 目标图表指针。 @param series 面积序列指针。 @return 无返回值。 */
void XChart_addAreaSeries(XChart* self, XAreaSeries* series);
/** @brief 添加样条序列（接管所有权）。 @param self 目标图表指针。 @param series 样条序列指针。 @return 无返回值。 */
void XChart_addSplineSeries(XChart* self, XSplineSeries* series);
/** @brief 查询柱状序列数。 @param self 目标图表指针。 @return 柱状序列数。 */
int XChart_barSeriesCount(const XChart* self);
/** @brief 按下标取柱状序列。 @param self 目标图表指针。 @param index 下标。 @return 序列指针；越界 NULL。 */
XBarSeries* XChart_barSeries(const XChart* self, int index);
/** @brief 查询散点序列数。 @param self 目标图表指针。 @return 散点序列数。 */
int XChart_scatterSeriesCount(const XChart* self);
/** @brief 按下标取散点序列。 @param self 目标图表指针。 @param index 下标。 @return 序列指针；越界 NULL。 */
XScatterSeries* XChart_scatterSeries(const XChart* self, int index);
/** @brief 查询面积序列数。 @param self 目标图表指针。 @return 面积序列数。 */
int XChart_areaSeriesCount(const XChart* self);
/** @brief 按下标取面积序列。 @param self 目标图表指针。 @param index 下标。 @return 序列指针；越界 NULL。 */
XAreaSeries* XChart_areaSeries(const XChart* self, int index);
/** @brief 查询样条序列数。 @param self 目标图表指针。 @return 样条序列数。 */
int XChart_splineSeriesCount(const XChart* self);
/** @brief 按下标取样条序列。 @param self 目标图表指针。 @param index 下标。 @return 序列指针；越界 NULL。 */
XSplineSeries* XChart_splineSeries(const XChart* self, int index);
/** @brief 取 X 数值轴。 @param self 目标图表指针。 @return X 轴指针（内部拥有）。 */
XValueAxis* XChart_axisX(const XChart* self);
/** @brief 取 Y 数值轴。 @param self 目标图表指针。 @return Y 轴指针（内部拥有）。 */
XValueAxis* XChart_axisY(const XChart* self);
/** @brief 取主题系列色（下标越界回环）。 @param self 目标图表指针。 @param index 序列下标。 @return ARGB 颜色。 */
uint32_t XChart_themeColor(const XChart* self, int index);

#endif /* XCHARTS_ON */

#ifdef __cplusplus
}
#endif
#endif /* XCHART_H */