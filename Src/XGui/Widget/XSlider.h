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

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractSlider.h"
#include "XStyleOption.h"

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
#define XSlider_valueChanged_signal(self, value) XAbstractSlider_valueChanged_signal((XAbstractSlider*)(self), (value))
#define XSlider_sliderPressed_signal(self) XAbstractSlider_sliderPressed_signal((XAbstractSlider*)(self))
#define XSlider_sliderMoved_signal(self, position) XAbstractSlider_sliderMoved_signal((XAbstractSlider*)(self), (position))
#define XSlider_sliderReleased_signal(self) XAbstractSlider_sliderReleased_signal((XAbstractSlider*)(self))
#define XSlider_rangeChanged_signal(self, min, max) XAbstractSlider_rangeChanged_signal((XAbstractSlider*)(self), (min), (max))
#define XSlider_actionTriggered_signal(self, action) XAbstractSlider_actionTriggered_signal((XAbstractSlider*)(self), (action))
/* Qt 的 QSlider 公开 sizeHint()/minimumSizeHint()（qslider.h:39-40 覆写）；
 * 本框架经 XWidget 同名接口取值，这里补两条转发宏与 XScrollBar 惯例一致。 */
#define XSlider_sizeHint(self) XWidget_sizeHint((const XWidget*)(self))
#define XSlider_minimumSizeHint(self) XWidget_minimumSizeHint((const XWidget*)(self))

/** @brief 刻度位置（对标 QSlider::TickPosition，数值一致）。
 *  @note  Qt 把 TicksLeft/TicksRight 定义成 TicksAbove/TicksBelow 的
 *         同值别名（qslider.h:27-28），C 侧同样提供同名枚举值，便于
 *         按 Qt 原文书写纵向滑块的刻度位置。 */
typedef enum XSliderTickPosition
{
    XSliderTickPosition_NoTicks = 0,          /**< 不绘制刻度（默认）。 */
    XSliderTickPosition_TicksAbove = 1,       /**< 上方/左侧刻度。 */
    XSliderTickPosition_TicksLeft = 1,        /**< TicksAbove 的别名（Qt 同值）。 */
    XSliderTickPosition_TicksBelow = 2,       /**< 下方/右侧刻度。 */
    XSliderTickPosition_TicksRight = 2,       /**< TicksBelow 的别名（Qt 同值）。 */
    XSliderTickPosition_TicksBothSides = 3    /**< 两侧刻度。 */
} XSliderTickPosition;

/* ==================== 类定义 ==================== */

/** @brief XSlider 虚函数表枚举：继承 XAbstractSlider 全部槽位（不新增）。 */
XCLASS_DEFINE_BEGING(XSlider)
XCLASS_DEFINE_EXTEND_END(XSlider, XAbstractSlider)

/**
 * @brief      XSlider 滑块控件对象；m_class 必须是第一个成员。
 * @details    字段含义（对标 QSlider 同名属性）：
 *             - m_tickPosition：刻度位置（默认 NoTicks）；
 *             - m_tickInterval：刻度值间隔（默认 0=自动）；
 *             - m_dragOffset：拖动偏移（按下点相对 handle 中心的偏移）。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XSlider
{
    XAbstractSlider m_class;       /**< 基类成员；必须是第一个，由 XClass 管理，禁止手工修改。 */
    int  m_dragOffset;             /**< 拖动偏移（按下点相对 handle 中心；保守保留 int）。 */
    int  m_tickInterval;           /**< 刻度间隔（值间隔；0=自动）。全 int——
                                        Qt 的 QSliderPrivate::tickInterval 也是
                                        int，无 16 位上界（qslider.cpp
                                        setTickInterval 不校验）；此前收在
                                        16 位位域里会把 >65535 静默截断。 */
    int  m_tickPosition;           /**< 刻度位置（XSliderTickPosition）。
                                        **全 int 原样保存**：Qt 的
                                        QSlider::setTickPosition 不做范围校验
                                        （qslider.cpp:472-479，实测
                                        setTickPosition(9) 读回 9），绘制/
                                        尺寸提示按位判定（&TicksAbove/
                                        &TicksBelow）而非等值匹配。 */
} XSlider;

/* ==================== 生命周期 ==================== */

/** @brief 初始化 XSlider 类虚函数表并返回共享表指针。 */
XVtable* XSlider_class_init(void);
/**
 * @brief      初始化 XSlider（对标 QSlider 构造；**缺省垂直方向**）。
 * @details    Qt 6.8 的 `QSlider(QWidget*)` 委托
 *             `QSlider(Qt::Vertical, parent)`（qslider.cpp:253-255，本机
 *             实测方向=2）。本函数自 2026-11-11 起按 Qt 缺口径改为
 *             **Vertical**；既有依赖水平缺省的调用点已改为显式
 *             `XSlider_init_2(Horizontal, ...)`（HsfMonitorVfd 频率给定），
 *             Test/XGuiDemo 的初始化点按下标迁移（见 gaps/迁移清单）。
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
/**
 * @brief      按方向初始化滑块（对标 QSlider(Qt::Orientation, QWidget*)，
 *             qslider.h:36）。
 * @param      self 待初始化对象；不可为 NULL。
 * @param      orientation 方向（XAbstractSliderOrientation 1=水平/2=垂直；
 *                         非法值回落水平）。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志。
 */
void XSlider_init_2(XSlider* self, int orientation, XWidget* parent,
                    XWidgetFlags flags);
/** @brief 按方向创建滑块（默认内存类型；对标 QSlider(Qt::Orientation, parent)）。 */
#define XSlider_create_2(orientation, parent, flags) \
    XSlider_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, (orientation), (parent), (flags))
/** @brief 按方向 + 指定内存类型创建滑块。 */
XSlider* XSlider_create_ex_2(XMemoryType memory, int orientation,
                             XWidget* parent, XWidgetFlags flags);
/* 反初始化/释放：统一用 XClassDeinit/XClassDelete（派生类不再声明
 * deinit/delete 入口，见约束文档「类的创建」迁移说明）。 */

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

#if XSTYLE_ON
/**
 * @brief      填充滑块样式选项（对标 QSlider::initStyleOption，
 *             qslider.cpp:416-446）。
 * @details    字段与 Qt 逐项对应：rect/state 取自控件、m_horizontal、
 *             m_sliderMin/Max/Value/SingleStep/PageStep、
 *             m_sliderTickPosition（**原样**，越界值也保留）、
 *             m_sliderTickInterval。绘制路径共用本函数，子类/样式可
 *             按 Qt 契约取完整选项（X 无 C++ protected virtual，沿用
 *             XFrame_initStyleOption/XLineEdit_initStyleOption 的
 *             「具名填充函数」先例）。
 * @param      self   滑块对象；可为 NULL（无操作）。
 * @param      option 输出选项（调用方栈上分配）；NULL 无操作。
 * @return     无返回值。
 */
void XSlider_initStyleOption(const XSlider* self, XStyleOption* option);
#endif /* XSTYLE_ON */

#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON && XSLIDER_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XSLIDER_H */
