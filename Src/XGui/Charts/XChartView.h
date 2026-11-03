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

#if XGUI_ON
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

/**
 * @brief      视口变更原因（viewportChanged 信号载荷）。
 * @details    用户侧交互导致的视口（X 域）变化类别；供应用层仲裁
 *             「自动跟随最新数据」与「用户接管视口」的归属
 *             （对标 Qt Charts scrollToDate 与用户手势的共存）。
 */
typedef enum XChartView_ViewportChange
{
    XChartView_ViewportChange_Zoom  = 0, /**< 缩放（滚轮/捏合/双击）。 */
    XChartView_ViewportChange_Pan   = 1, /**< 平移（鼠标/触摸拖拽）。 */
    XChartView_ViewportChange_Reset = 2  /**< 复位（Shift+双击 / zoomReset）。 */
} XChartView_ViewportChange;

/** @brief 图表视图控件（对标 QChartView）。 */
typedef struct XChartView
{
    XWidget m_base;           /**< 基类成员；必须是第一个。 */
    XChart* m_chart;          /**< 图表模型（内部拥有；setChart 转移）。 */
    /* init 预建的占位图表标记：true 表示 m_chart 仍是 init 自动创建、
     * 从未经 setChart 装入过的空图表。setChart 替换它时同步 delete——
     * 该对象无外部引用、不在信号发射帧内，无需延迟回收；若走
     * deleteLater，无事件循环场景（无头导出/探针/单测）永不兑现即泄漏
     * （实测 2990B/轮）。用户经 setChart 装入的图表仍走 deleteLater
     * （R-103：槽函数内换图表时发射帧在途）。 */
    bool m_chartPlaceholder;  /**< m_chart 是否为 init 预建的占位空图表。 */
    XChartView_RubberBands m_rubberBand; /**< 框选缩放模式（对标 rubberBand）。 */
    bool m_dragging;          /**< 框选拖拽进行中。 */
    XPoint m_dragStart;       /**< 拖拽起点（视图局部坐标）。 */
    XRect m_dragRect;         /**< 当前橡皮筋矩形（视图局部坐标）。 */
    XAbstractSeries* m_hoverSeries; /**< 悬停命中的序列（借用；无命中 NULL）。 */
    int m_hoverIndex;         /**< 悬停命中的点下标（无命中 -1）。 */
    bool m_hovering;          /**< 当前是否处于悬停命中状态。 */
    bool m_panEnabled;        /**< 拖拽平移（NoRubberBand 时左键拖拽=域滚动）。 */
    bool m_panning;           /**< 平移拖拽进行中（区别于框选 m_dragging）。 */
    XPoint m_panLast;         /**< 平移上一帧鼠标位（增量换算用）。 */
    bool m_wheelZoomXOnly;    /**< Ctrl+滚轮仅缩 X 轴（Y 域交给自动量程）。 */
    bool m_touchZoom;         /**< 双指捏合缩放 X + 双指平移开关（触屏）。 */
    bool m_pinching;          /**< 捏合手势进行中（Gesture Updated 帧间累积）。 */
    bool m_timeAxisMode;      /**< 时间轴模式（滚轮缩 X/拖拽平移 X/捏合缩 X）。 */
    bool m_panNotified;       /**< 本次平移是否已发过 viewportChanged（去重）。 */
#if XCHARTVIEW_STATIC_LAYER_ON
    /* 静态层缓存（§10.2 第一期 Phase B，对标 Qt QGraphicsItem::
     * DeviceCoordinateCache）：缓存背景/背景笔/标题/坐标轴网格五件套，
     * 序列/饼图/图例每帧现绘。XCHARTVIEW_STATIC_LAYER_ON=0 时三字段
     * 整体裁剪，内存与渲染路径与无层现状一致。 */
    XImage m_staticLayer;     /**< 静态层离屏画布；格式=目标表面格式，尺寸=控件尺寸。 */
    uint64_t m_staticFp;      /**< 静态层内容指纹（FNV-1a 64 位静态实现）。 */
    bool m_staticValid;       /**< 静态层是否与当前指纹一致（false 强制重建）。 */
    /* 图例保留瓦片（保留缓存一期；运行期环境开关 XGUI_CHART_STATIC_
     * CACHE，默认关，=1 才启用，沿 XGPU_QUAD_BATCH 先例）：把图例
     * （色块+序列名）渲进小离屏画布，后续帧一次 drawImage 贴回，替代
     * 逐项字形光栅化。z 序与直画图例相同（序列/饼图之上、橡皮筋之下），
     * source-over 结合律保证与直画逐位一致（同 m_staticLayer 论证）；
     * 逐位前提有两道 gate（t218c/夜一证据图修复）：瓦片顶边带 TOP_SLACK
     * 余量（drawText 基线锚使首行字形顶带上探图例区顶之上）、内容含
     * 半透明色即回退直画（预乘中间存储两次舍入可差 ±1/通道）。
     * 失效策略从粗（一期刻意收窄）：尺寸/格式失配、指纹变化、resize/
     * updateChart/setChart 整体失效重绘，不做细粒度失效；指纹含图例
     * 可见性与序列名/色，饼图切片标签不入指纹，靠 updateChart 粗失效
     * 兜底。开关关或旁路时两字段闲置，行为与既有路径逐位一致。 */
    XImage m_legendLayer;     /**< 图例瓦片离屏画布；格式=目标表面格式，尺寸=图例瓦片区。 */
    uint64_t m_legendFp;      /**< 图例瓦片内容指纹（与 m_staticFp 同算法同契约）。 */
    bool m_legendValid;       /**< 图例瓦片是否与当前指纹一致（false 强制重建）。 */
#endif
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

/** @brief 读取图表模型。 @param self 目标视图指针。 @return 图表指针（内部拥有）。 */
XChart* XChartView_chart(const XChartView* self);
/** @brief 请求重绘（模型变化后调用）。 @param self 目标视图指针。 @return 无返回值。 */
void XChartView_updateChart(XChartView* self);

#if XCHARTVIEW_STATIC_LAYER_ON
/**
 * @brief      设置静态层缓存运行期旁路（Phase C 位一致断言的 A/B 开关）。
 * @details    on=true 时渲染入口视层为永久失效（指纹命中判定被短路），
 *             全部内容走直画路径——与 XCHARTVIEW_STATIC_LAYER_ON=0 的
 *             编译期裁剪语义一致；on=false 恢复正常缓存。供回归测试在
 *             同一进程内做「层开/层关逐位 diff」（对标 Qt 渲染优化的
 *             A/B 验证纪律）；生产代码无需调用。
 * @param      self 目标视图指针。
 * @param      bypass true=绕过层直画；false=恢复正常缓存。
 */
void XChartView_setStaticLayerBypass(XChartView* self, bool bypass);
#endif /* XCHARTVIEW_STATIC_LAYER_ON */

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

/**
 * @brief      设置拖拽平移开关（对标 Qt Charts QChartView 拖拽滚动）。
 * @details    开启后：NoRubberBand（未框选）状态下按住左键拖拽 = 把
 *             绘图区沿数据域平移（拖多少移多少，X/Y 域同动）；框选
 *             模式开启时框选优先（两模式互斥，框选优先）。
 * @param      self 目标视图指针。
 * @param      enabled true=开启拖拽平移；false=关闭（缺省）。
 * @return     无返回值。
 */
void XChartView_setPanEnabled(XChartView* self, bool enabled);
/** @brief 查询拖拽平移开关。 @param self 目标视图指针。 @return true=已开启。 */
bool XChartView_panEnabled(const XChartView* self);
/**
 * @brief      设置滚轮缩放轴约束。
 * @details    true=Ctrl+滚轮仅缩放 X 轴（时间轴趋势图配纵轴自动量程
 *             的标准搭配，Y 域交给自动量程）；false=缺省双轴同缩。
 * @param      self 目标视图指针。
 * @param      xAxisOnly true=仅 X 轴。
 * @return     无返回值。
 */
void XChartView_setWheelZoomXOnly(XChartView* self, bool xAxisOnly);
/** @brief 查询滚轮缩放轴约束。 @param self 目标视图指针。 @return true=仅 X 轴。 */
bool XChartView_wheelZoomXOnly(const XChartView* self);
/**
 * @brief      设置触屏手势开关（双指捏合缩放 + 双指/单指平移）。
 * @details    开启后视图自动挂 Pinch/Pan 手势识别器（框架手势链，
 *             对标 Qt QWidget::grabGesture(GesturePinch|GesturePan)），
 *             移动端触屏交互与桌面鼠标行为同构：捏合=缩放 X 轴（受
 *             wheelZoomXOnly 约束），平移=域滚动。
 * @param      self 目标视图指针。
 * @param      enabled true=开启触屏手势。
 * @return     无返回值。
 */
void XChartView_setTouchGesturesEnabled(XChartView* self, bool enabled);
/** @brief 查询触屏手势开关。 @param self 目标视图指针。 @return true=已开启。 */
bool XChartView_touchGesturesEnabled(const XChartView* self);

/**
 * @brief      设置时间轴模式（趋势图口径：横轴＝时间，单轴交互）。
 * @details    开启后全部指针交互只作用于横轴（时间轴），纵轴交给应用侧
 *             自动量程：
 *             - 滚轮（含 Ctrl）＝**缩放横轴**，以光标处的时间值为锚
 *               （缩放围绕光标，不是域中心）；
 *             - 鼠标左键拖拽 ＝仅**左右平移横轴**（拖多远移多少）；
 *             - 双指捏合      ＝**缩放横轴**；
 *             - 单指拖拽      ＝**左右平移横轴**（与鼠标拖拽同构）；
 *             - 双击          ＝缩放横轴（Shift+双击 ＝ 复位）。
 *             关闭时行为不变（滚轮＝域滚动，Ctrl+滚轮＝双轴缩放，
 *             拖拽＝双轴平移）。
 * @param      self 目标视图指针。
 * @param      enabled true=开启时间轴模式。
 * @return     无返回值。
 */
void XChartView_setTimeAxisMode(XChartView* self, bool enabled);
/** @brief 查询时间轴模式。 @param self 目标视图指针。 @return true=已开启。 */
bool XChartView_timeAxisMode(const XChartView* self);

/**
 * @brief viewportChanged(int reason) 信号标识（用户交互改变视口时发射）。
 * @details 载荷＝XChartView_ViewportChange 枚举值（缩放/平移/复位）。
 *          供应用层仲裁「自动跟随最新数据」与「用户接管视口」：收到
 *          平移即把视口判给用户（应用侧停止改写轴域），收到复位则
 *          交还应用侧。信号在交互发生的同一事件帧内发射（不经事件
 *          循环排队），保证应用侧能在下一帧前接管。
 * @param  self 目标视图指针。
 * @param  reason 视口变更原因（XChartView_ViewportChange）。
 * @return 无返回值。
 */
void* XChartView_viewportChanged_signal(XChartView* self, int reason);

#endif /* XCHARTS_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XCHARTVIEW_H */
