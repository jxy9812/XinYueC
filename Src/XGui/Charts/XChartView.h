/**
 * @file       XChartView.h
 * @brief      XChartView 图表视图控件（对标 Qt Charts 6.8 QChartView）。
 * @details    继承 XWidget，内嵌 XChart 模型并在 paintEvent 中完成
 *             标题/图例/网格/折线/饼图渲染（布局对标 Qt Charts 默认）。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCHARTVIEW_H
#define XCHARTVIEW_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XWidget.h"
#include "XChart.h"
#include "XGuiConfig.h"

#if XCHARTS_ON

XCLASS_DEFINE_BEGING(XChartView)
XCLASS_DEFINE_EXTEND_END(XChartView, XWidget)

/** @brief 图表视图控件（对标 QChartView）。 */
typedef struct XChartView
{
    XWidget m_base;           /**< 基类成员；必须是第一个。 */
    XChart* m_chart;          /**< 图表模型（内部拥有）。 */
    bool m_rubberBand;        /**< 框选缩放（预留）。 */
} XChartView;

XVtable* XChartView_class_init(void);

/**
 * @brief 初始化图表视图（栈/嵌入使用）。
 *
 * @param self   目标视图指针，不能为空。
 * @param parent 父控件；可为 NULL。
 * @param flags  窗口标志位组合。
 * @return 无返回值。
 */
void XChartView_init(XChartView* self, XWidget* parent, XWidgetFlags flags);

/**
 * @brief 堆上创建图表视图。
 *
 * @param memory 内存类型。
 * @param parent 父控件；可为 NULL。
 * @param flags  窗口标志位组合。
 * @return 视图指针；分配失败返回 NULL。
 */
XChartView* XChartView_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags);
#define XChartView_create(parent, flags) \
    XChartView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, parent, flags)

/** @brief 读取图表模型。 @param self 目标视图指针。 @return 图表指针（内部拥有）。 */
XChart* XChartView_chart(const XChartView* self);
/** @brief 请求重绘（模型变化后调用）。 @param self 目标视图指针。 @return 无返回值。 */
void XChartView_updateChart(XChartView* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XCHARTVIEW_H */