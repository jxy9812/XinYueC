/**
 * @file       XSlider.h
 * @brief      XSlider 滑块控件（对齐 Qt 6.8 QSlider；继承 XAbstractSlider）。
 * @details    水平/垂直滑块，基于 XAbstractSlider 提供绘制与鼠标交互：
 *             - TickPosition 刻度位置与 tickInterval 刻度间隔（字段 +
 *               绘制；interval=0 时自动回退 pageStep/singleStep 间隔）；
 *             - paintEvent：凹陷凹槽 + Raised 立体 handle + 刻度短线
 *               （水平在上/下、垂直在左/右）；
 *             - mousePressEvent：点击 handle 外凹槽 → handle 跳到点击处
 *               （Qt 绝对定位语义）；点击 handle → 进入拖动 +
 *               sliderPressed；mouseMoveEvent：拖动 →
 *               setSliderPosition（内部发射 sliderMoved，tracking 时
 *               提交值）；mouseReleaseEvent：sliderReleased。
 *             范围/数值/步进/行为/信号全部继承自 XAbstractSlider。
 * @note       模块总开关 XSLIDER_ON 定义于 XGuiConfig.h；=0 时裁剪整个
 *             XSlider 公共 API。依赖 XWIDGET_ON、XABSTRACTSLIDER_ON、
 *             XPALETTE_ON、XPAINTER_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XSLIDER_H
#define XSLIDER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractSlider.h"

#if XWIDGET_ON && XABSTRACTSLIDER_ON && XSLIDER_ON

/* ==================== 刻度位置（数值对齐 Qt） ==================== */

/* ==================== 父类 API 宏转发（对齐库内 XRadioButton 惯例） ==================== */

#define XSlider_orientation(self) XAbstractSlider_orientation((const XAbstractSlider*)(self))
#define XSlider_setOrientation(self, o) XAbstractSlider_setOrientation((XAbstractSlider*)(self), (o))
#define XSlider_minimum(self) XAbstractSlider_minimum((const XAbstractSlider*)(self))
#define XSlider_setMinimum(self, min) XAbstractSlider_setMinimum((XAbstractSlider*)(self), (min))
#define XSlider_maximum(self) XAbstractSlider_maximum((const XAbstractSlider*)(self))
#define XSlider_setMaximum(self, max) XAbstractSlider_setMaximum((XAbstractSlider*)(self), (max))
#define XSlider_setRange(self, min, max) XAbstractSlider_setRange((XAbstractSlider*)(self), (min), (max))
#define XSlider_singleStep(self) XAbstractSlider_singleStep((const XAbstractSlider*)(self))
#define XSlider_setSingleStep(self, s) XAbstractSlider_setSingleStep((XAbstractSlider*)(self), (s))
#define XSlider_pageStep(self) XAbstractSlider_pageStep((const XAbstractSlider*)(self))
#define XSlider_setPageStep(self, s) XAbstractSlider_setPageStep((XAbstractSlider*)(self), (s))
#define XSlider_hasTracking(self) XAbstractSlider_hasTracking((const XAbstractSlider*)(self))
#define XSlider_setTracking(self, e) XAbstractSlider_setTracking((XAbstractSlider*)(self), (e))
#define XSlider_setSliderDown(self, d) XAbstractSlider_setSliderDown((XAbstractSlider*)(self), (d))
#define XSlider_isSliderDown(self) XAbstractSlider_isSliderDown((const XAbstractSlider*)(self))
#define XSlider_sliderPosition(self) XAbstractSlider_sliderPosition((const XAbstractSlider*)(self))
#define XSlider_setSliderPosition(self, p) XAbstractSlider_setSliderPosition((XAbstractSlider*)(self), (p))
#define XSlider_invertedAppearance(self) XAbstractSlider_invertedAppearance((const XAbstractSlider*)(self))
#define XSlider_setInvertedAppearance(self, i) XAbstractSlider_setInvertedAppearance((XAbstractSlider*)(self), (i))
#define XSlider_invertedControls(self) XAbstractSlider_invertedControls((const XAbstractSlider*)(self))
#define XSlider_setInvertedControls(self, i) XAbstractSlider_setInvertedControls((XAbstractSlider*)(self), (i))
#define XSlider_value(self) XAbstractSlider_value((const XAbstractSlider*)(self))
#define XSlider_setValue(self, v) XAbstractSlider_setValue((XAbstractSlider*)(self), (v))
#define XSlider_triggerAction(self, a) XAbstractSlider_triggerAction((XAbstractSlider*)(self), (a))
#define XSlider_setRepeatAction(self, a, t, r) XAbstractSlider_setRepeatAction((XAbstractSlider*)(self), (a), (t), (r))
#define XSlider_repeatAction(self) XAbstractSlider_repeatAction((const XAbstractSlider*)(self))
#define XSlider_stepBy_base(self, s) XAbstractSlider_stepBy_base((XAbstractSlider*)(self), (s))
#define XSlider_sliderChange_base(self, c) XAbstractSlider_sliderChange_base((XAbstractSlider*)(self), (c))
#define XSlider_stepEnabled_base(self) XAbstractSlider_stepEnabled_base((XAbstractSlider*)(self))
#define XSlider_valueChanged_signal(self) XAbstractSlider_valueChanged_signal((XAbstractSlider*)(self))
#define XSlider_sliderPressed_signal(self) XAbstractSlider_sliderPressed_signal((XAbstractSlider*)(self))
#define XSlider_sliderMoved_signal(self) XAbstractSlider_sliderMoved_signal((XAbstractSlider*)(self))
#define XSlider_sliderReleased_signal(self) XAbstractSlider_sliderReleased_signal((XAbstractSlider*)(self))
#define XSlider_rangeChanged_signal(self) XAbstractSlider_rangeChanged_signal((XAbstractSlider*)(self))
#define XSlider_actionTriggered_signal(self) XAbstractSlider_actionTriggered_signal((XAbstractSlider*)(self))

/** @brief 刻度位置（对标 QSlider::TickPosition，数值一致）。 */
typedef enum XSliderTickPosition
{
    XSliderTickPosition_NoTicks = 0,          /**< 不绘制刻度（默认）。 */
    XSliderTickPosition_TicksAbove = 1,       /**< 上方/左侧刻度。 */
    XSliderTickPosition_TicksBelow = 2,       /**< 下方/右侧刻度。 */
    XSliderTickPosition_TicksBothSides = 3    /**< 两侧刻度。 */
} XSliderTickPosition;

/* ==================== 类定义 ==================== */

/** @brief XSlider 虚函数表枚举：继承 XAbstractSlider 全部槽位（不新增）。 */
XCLASS_DEFINE_BEGING(XSlider)
XCLASS_DEFINE_EXTEND_END(XSlider, XAbstractSlider)

/**
 * @brief      XSlider 滑块控件对象；m_base 必须是第一个成员。
 * @details    字段含义（对标 QSlider 同名属性）：
 *             - m_tickPosition：刻度位置（默认 NoTicks）；
 *             - m_tickInterval：刻度值间隔（默认 0=自动）；
 *             - m_dragOffset：拖动偏移（按下点相对 handle 中心的偏移）。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XSlider
{
    XAbstractSlider m_base;        /**< 基类成员；必须是第一个。 */
    int  m_tickPosition;           /**< 刻度位置（XSliderTickPosition）。 */
    int  m_tickInterval;           /**< 刻度间隔（值间隔；0=自动）。 */
    int  m_dragOffset;             /**< 拖动偏移（按下点相对 handle 中心）。 */
} XSlider;

/* ==================== 生命周期 ==================== */

/** @brief 初始化 XSlider 类虚函数表并返回共享表指针。 */
XVtable* XSlider_class_init(void);
/**
 * @brief      初始化 XSlider（对标 QSlider 构造；默认水平方向）。
 * @param      self 待初始化对象；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志（可传 0 表示 Widget 类型）。
 */
void XSlider_init(XSlider* self, XWidget* parent, XWidgetFlags flags);
/** @brief 使用默认内存类型创建滑块（headless 父对象语义同 init）。 */
#define XSlider_create(parent, flags) XSlider_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建滑块。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志。
 * @return     新对象指针；失败返回 NULL。
 */
XSlider* XSlider_create_ex(XMemoryType memory, XWidget* parent,
                           XWidgetFlags flags);
/** @brief 通过 XClass 虚表释放 XSlider 资源（栈/外部存储对象使用）。 */
#define XSlider_deinit_base(self) XAbstractSlider_deinit_base((XAbstractSlider*)(self))
/** @brief 删除堆上的 XSlider 对象。 */
#define XSlider_delete_base(self) XAbstractSlider_delete_base((XAbstractSlider*)(self))

/* ==================== 刻度（对标 QSlider） ==================== */

/** @brief 查询刻度位置（对标 QSlider::tickPosition）。 */
int XSlider_tickPosition(const XSlider* self);
/** @brief 设置刻度位置并重绘（对标 QSlider::setTickPosition）。 */
void XSlider_setTickPosition(XSlider* self, int position);
/** @brief 查询刻度间隔（对标 QSlider::tickInterval；0=自动）。 */
int XSlider_tickInterval(const XSlider* self);
/** @brief 设置刻度值间隔并重绘（负值按 0 处理；0=自动，对标
 *         QSlider::setTickInterval）。 */
void XSlider_setTickInterval(XSlider* self, int interval);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON && XSLIDER_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSLIDER_H */
