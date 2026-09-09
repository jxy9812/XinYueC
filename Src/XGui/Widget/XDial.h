/**
 * @file       XDial.h
 * @brief      XDial 旋钮控件（对标 Qt 6.8 QDial）。
 * @details    圆形旋钮：继承 XAbstractSlider（范围/值/步进/翻转/
 *             wrapping 全部由基类提供），本类实现圆形绘制（凹槽圆弧
 *             + 指示针 + notches 刻度）与鼠标交互（角度拖动调值）。
 *             附加属性：notchTarget（刻度间距目标像素）、
 *             notchesVisible（是否绘制刻度）。
 * @note       模块总开关 XDIAL_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XABSTRACTSLIDER_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XDIAL_H
#define XDIAL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractSlider.h"

#if XWIDGET_ON && XABSTRACTSLIDER_ON && XDIAL_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XDial)
XCLASS_DEFINE_EXTEND_END(XDial, XAbstractSlider)

/**
 * @brief      XDial 旋钮控件对象；m_base 必须是第一个成员。
 * @details    字段含义：
 *             - m_notchTarget：相邻刻度的目标像素间距（默认 3.7，
 *               对标 QDial::notchTarget）；
 *             - m_notchesVisible：是否绘制刻度（默认 false）；
 *             - m_dragging：鼠标拖动中（内部）。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XDial
{
    XAbstractSlider m_base;        /**< 基类成员；必须是第一个。 */
    double  m_notchTarget;         /**< 刻度间距目标像素。 */
    bool    m_notchesVisible;      /**< 刻度可见。 */
    bool    m_wrapping;            /**< 环绕（对标 QDial::wrapping）。 */
    bool    m_dragging;            /**< 拖动中。 */
} XDial;

/* ==================== 生命周期 ==================== */

XVtable* XDial_class_init(void);
void XDial_init(XDial* self, XWidget* parent, XWidgetFlags flags);
#define XDial_create(parent, flags) XDial_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XDial* XDial_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XDial_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XDial_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== 专属属性（对标 QDial public API） ==================== */

/** @brief 查询刻度间距目标像素（默认 3.7）。 */
double XDial_notchTarget(const XDial* self);
/** @brief 设置刻度间距目标并重绘。 */
void XDial_setNotchTarget(XDial* self, double target);
/** @brief 查询刻度可见性。 */
bool XDial_notchesVisible(const XDial* self);
/** @brief 设置刻度可见并重绘。 */
void XDial_setNotchesVisible(XDial* self, bool visible);
/** @brief 查询环绕（继承自基类 wrapping 语义的便捷转发）。 */
bool XDial_wrapping(const XDial* self);
/** @brief 设置环绕。 */
void XDial_setWrapping(XDial* self, bool on);
/** @brief 计算当前实际刻度数（对标 QDial::notchSize 的刻度数量语义）。 */
int XDial_notchSize(const XDial* self);

/* ==================== 父类 API 宏转发（对齐库内 XRadioButton 惯例） ==================== */

#define XDial_minimum(self) XAbstractSlider_minimum((const XAbstractSlider*)(self))
#define XDial_maximum(self) XAbstractSlider_maximum((const XAbstractSlider*)(self))
#define XDial_setMinimum(self, min) XAbstractSlider_setMinimum((XAbstractSlider*)(self), (min))
#define XDial_setMaximum(self, max) XAbstractSlider_setMaximum((XAbstractSlider*)(self), (max))
#define XDial_setRange(self, min, max) XAbstractSlider_setRange((XAbstractSlider*)(self), (min), (max))
#define XDial_value(self) XAbstractSlider_value((const XAbstractSlider*)(self))
#define XDial_setValue(self, v) XAbstractSlider_setValue((XAbstractSlider*)(self), (v))
#define XDial_singleStep(self) XAbstractSlider_singleStep((const XAbstractSlider*)(self))
#define XDial_setSingleStep(self, s) XAbstractSlider_setSingleStep((XAbstractSlider*)(self), (s))
#define XDial_pageStep(self) XAbstractSlider_pageStep((const XAbstractSlider*)(self))
#define XDial_setPageStep(self, s) XAbstractSlider_setPageStep((XAbstractSlider*)(self), (s))
#define XDial_hasTracking(self) XAbstractSlider_hasTracking((const XAbstractSlider*)(self))
#define XDial_setTracking(self, e) XAbstractSlider_setTracking((XAbstractSlider*)(self), (e))
#define XDial_invertedAppearance(self) XAbstractSlider_invertedAppearance((const XAbstractSlider*)(self))
#define XDial_setInvertedAppearance(self, i) XAbstractSlider_setInvertedAppearance((XAbstractSlider*)(self), (i))
#define XDial_invertedControls(self) XAbstractSlider_invertedControls((const XAbstractSlider*)(self))
#define XDial_setInvertedControls(self, i) XAbstractSlider_setInvertedControls((XAbstractSlider*)(self), (i))
#define XDial_triggerAction(self, a) XAbstractSlider_triggerAction((XAbstractSlider*)(self), (a))
#define XDial_valueChanged_signal(self) XAbstractSlider_valueChanged_signal((XAbstractSlider*)(self))
#define XDial_sliderPressed_signal(self) XAbstractSlider_sliderPressed_signal((XAbstractSlider*)(self))
#define XDial_sliderMoved_signal(self) XAbstractSlider_sliderMoved_signal((XAbstractSlider*)(self))
#define XDial_sliderReleased_signal(self) XAbstractSlider_sliderReleased_signal((XAbstractSlider*)(self))
#define XDial_rangeChanged_signal(self) XAbstractSlider_rangeChanged_signal((XAbstractSlider*)(self))
#define XDial_actionTriggered_signal(self) XAbstractSlider_actionTriggered_signal((XAbstractSlider*)(self))

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON && XDIAL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDIAL_H */
