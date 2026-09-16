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

/**
 * @brief      框选缩放模式（对标 Qt Charts 6.8 QChartView::RubberBand，
 *             数值一致）。
 * @details    可按位组合；RectangleRubberBand = Vertical|Horizontal。
 */
typedef enum XChartView_RubberBand
{
    XChartView_RubberBand_NoRubberBand = 0x0,           /**< 关闭框选（对标 NoRubberBand）。 */
    XChartView_RubberBand_VerticalRubberBand = 0x1,     /**< 仅纵向框选（对标 VerticalRubberBand）。 */
    XChartView_RubberBand_HorizontalRubberBand = 0x2,   /**< 仅横向框选（对标 HorizontalRubberBand）。 */
    XChartView_RubberBand_RectangleRubberBand = 0x3,    /**< 矩形框选（对标 RectangleRubberBand）。 */
    XChartView_RubberBand_ClickThroughRubberBand = 0x80 /**< 点击穿透（对标 ClickThroughRubberBand）。 */
} XChartView_RubberBand;

/** @brief 框选缩放模式集合（可按位组合；对标 QChartView::RubberBands）。 */
typedef uint32_t XChartView_RubberBands;

/** @brief 图表视图控件（对标 QChartView）。 */
typedef struct XChartView
{
    XWidget m_base;           /**< 基类成员；必须是第一个。 */
    XChart* m_chart;          /**< 图表模型（内部拥有；setChart 转移）。 */
    XChartView_RubberBands m_rubberBand; /**< 框选缩放模式（对标 rubberBand）。 */
    bool m_dragging;          /**< 框选拖拽进行中。 */
    XPoint m_dragStart;       /**< 拖拽起点（视图局部坐标）。 */
    XRect m_dragRect;         /**< 当前橡皮筋矩形（视图局部坐标）。 */
    XAbstractSeries* m_hoverSeries; /**< 悬停命中的序列（借用；无命中 NULL）。 */
    int m_hoverIndex;         /**< 悬停命中的点下标（无命中 -1）。 */
    bool m_hovering;          /**< 当前是否处于悬停命中状态。 */
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

/** @brief 删除堆上图表视图（查表分派析构并释放内存）。 */
#define XChartView_delete_base(self) XClass_delete_base((XClass*)(self))

/** @brief 读取图表模型。 @param self 目标视图指针。 @return 图表指针（内部拥有）。 */
XChart* XChartView_chart(const XChartView* self);
/** @brief 请求重绘（模型变化后调用）。 @param self 目标视图指针。 @return 无返回值。 */
void XChartView_updateChart(XChartView* self);

/**
 * @brief 离屏渲染整张图表到目标图像（对标 QChartView 绘制的测试/导出路径）。
 *
 * @details 与 paintEvent 共用同一渲染管线（主题背景/轴/序列/图例/饼图）；
 *          便于无窗口环境的回归冒烟与像素断言。
 *
 * @param self  目标视图指针。
 * @param image 目标图像；宽高按视图当前尺寸。
 * @return 渲染成功返回 true；参数非法或绘制器绑定失败返回 false。
 */
bool XChartView_renderToImage(XChartView* self, XImage* image);

/**
 * @brief 替换图表模型（对标 QChartView::setChart）。
 *
 * @param self  目标视图指针。
 * @param chart 新图表指针；接管所有权并释放旧模型；NULL 仅清空。
 * @return 无返回值。
 */
void XChartView_setChart(XChartView* self, XChart* chart);

/** @brief 设置框选缩放模式。 @param self 目标视图指针。 @param rubberBands 模式位集合。 @return 无返回值。 */
void XChartView_setRubberBand(XChartView* self, XChartView_RubberBands rubberBands);
/** @brief 读取框选缩放模式。 @param self 目标视图指针。 @return 模式位集合。 */
XChartView_RubberBands XChartView_rubberBand(const XChartView* self);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XCHARTVIEW_H */