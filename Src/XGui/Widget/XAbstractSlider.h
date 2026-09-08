/**
 * @file       XAbstractSlider.h
 * @brief      XAbstractSlider 滑块抽象基类（对齐 Qt 6.8 QAbstractSlider
 *             完整公共 API）。
 * @details    提供滑块控件的公共骨架（XSlider 继承本类，对标
 *             QSlider : QAbstractSlider）：
 *             - 范围与数值：minimum/maximum（setRange 单端联动收敛、
 *               min 超过 max 时 max 收敛为 min，对标 Qt qBound 语义）、
 *               value/setValue、sliderPosition/setSliderPosition（与 value
 *               分离的句柄位置：tracking=false 时拖动只动位置，释放时
 *               才提交值）；
 *             - 步进：singleStep/pageStep、stepBy 虚槽（基类默认钳位
 *               步进 + 发射 valueChanged/sliderMoved）、stepEnabled 虚槽
 *               （按当前值边界返回 StepEnabled 位组合）；
 *             - 行为：hasTracking/setTracking、setSliderDown/isSliderDown
 *               （切换发射 sliderPressed/sliderReleased）、
 *               invertedAppearance/invertedControls、orientation；
 *             - 动作：triggerAction（SingleStepAdd/Sub、PageStepAdd/Sub、
 *               ToMinimum/ToMaximum、Move，数值与 Qt 一致）、
 *               setRepeatAction/repeatAction（存字段；长按自动重复的
 *               timer 为后续扩展项）、sliderChange 虚槽（范围/步进/值
 *               变化通知，基类默认仅刷新显示）；
 *             - 信号：valueChanged/sliderPressed/sliderMoved/
 *               sliderReleased/rangeChanged/actionTriggered；
 *             - 事件：keyPressEvent（Up/Right 单步增、Down/Left 单步减、
 *               PageUp 页增、PageDown 页减、Home 到最小、End 到最大，
 *               invertedControls 翻转方向）、wheelEvent（滚轮 120 角度=
 *               1 单步，余数累积）、timerEvent/changeEvent 转发父类。
 * @note       模块总开关 XABSTRACTSLIDER_ON 定义于 XGuiConfig.h；=0 时
 *             裁剪全部公共 API。依赖 XWIDGET_ON、XWINDOWEVENT_ON（滚轮）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XABSTRACTSLIDER_H
#define XABSTRACTSLIDER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XABSTRACTSLIDER_ON

/* ==================== 枚举（对标 QAbstractSlider，数值与 Qt 一致） ==================== */

/** @brief 滑块方向（对标 Qt::Orientation；数值与 Qt 一致）。 */
typedef enum XAbstractSliderOrientation
{
    XAbstractSliderOrientation_Horizontal = 1, /**< 水平滑块（默认）。 */
    XAbstractSliderOrientation_Vertical = 2    /**< 垂直滑块。 */
} XAbstractSliderOrientation;

/** @brief 滑块动作（对标 QAbstractSlider::SliderAction；数值与 Qt 一致）。 */
typedef enum XAbstractSliderSliderAction
{
    XAbstractSliderSliderAction_NoAction = 0,      /**< 无动作。 */
    XAbstractSliderSliderAction_SingleStepAdd = 1, /**< 单步增。 */
    XAbstractSliderSliderAction_SingleStepSub = 2, /**< 单步减。 */
    XAbstractSliderSliderAction_PageStepAdd = 3,   /**< 翻页增。 */
    XAbstractSliderSliderAction_PageStepSub = 4,   /**< 翻页减。 */
    XAbstractSliderSliderAction_ToMinimum = 5,     /**< 跳到最小。 */
    XAbstractSliderSliderAction_ToMaximum = 6,     /**< 跳到最大。 */
    XAbstractSliderSliderAction_Move = 7           /**< 移动（提交位置到值）。 */
} XAbstractSliderSliderAction;

/** @brief 滑块变化类型（对标 QAbstractSlider::SliderChange；数值与 Qt 一致）。 */
typedef enum XAbstractSliderSliderChange
{
    XAbstractSliderSliderChange_RangeChange = 0,       /**< 范围变化。 */
    XAbstractSliderSliderChange_OrientationChange = 1, /**< 方向变化。 */
    XAbstractSliderSliderChange_StepsChange = 2,       /**< 步进变化。 */
    XAbstractSliderSliderChange_ValueChange = 3        /**< 值变化。 */
} XAbstractSliderSliderChange;

/** @brief 步进使能位（命名约定对标 XAbstractSpinBox::StepEnabledFlag；
 *         可按位组合）。 */
typedef enum XAbstractSliderStepEnabledFlag
{
    XAbstractSliderStepEnabledFlag_StepNone = 0x00,       /**< 不可步进。 */
    XAbstractSliderStepEnabledFlag_StepUpEnabled = 0x01,  /**< 可增步进（值 < 最大）。 */
    XAbstractSliderStepEnabledFlag_StepDownEnabled = 0x02 /**< 可减步进（值 > 最小）。 */
} XAbstractSliderStepEnabledFlag;

/** @brief 步进使能位组合类型。 */
typedef int XAbstractSliderStepEnabled;

/* ==================== 虚函数表（StepBy/SliderChange/StepEnabled 追加在 XWidget 槽位之后） ==================== */

/** @brief XAbstractSlider 虚函数表枚举：继承 XWidget 全部槽位后追加
 *  StepBy/SliderChange/StepEnabled 三个新槽（保持既有槽位数值不变）。 */
XCLASS_DEFINE_BEGING(XAbstractSlider)
XCLASS_DEFINE_ENUM(XAbstractSlider, StepBy) = XCLASS_VTABLE_GET_SIZE(XWidget),
XCLASS_DEFINE_ENUM(XAbstractSlider, SliderChange),
XCLASS_DEFINE_ENUM(XAbstractSlider, StepEnabled),
XCLASS_DEFINE_END(XAbstractSlider)

/**
 * @brief      XAbstractSlider 抽象基类对象；m_base 必须是第一个成员。
 * @details    字段含义（对标 QAbstractSlider 同名属性）：
 *             - m_min/m_max：范围（默认 0/99）；
 *             - m_value：当前值（默认 0）；
 *             - m_position：句柄位置（默认 0；tracking=false 时与 value
 *               分离，对标 Qt 私有 position）；
 *             - m_orientation：方向（默认水平）；
 *             - m_pageStep/m_singleStep：翻页/单步步进（默认 10/1）；
 *             - m_tracking：拖动实时发射 valueChanged（默认 true）；
 *             - m_sliderDown：handle 按下状态（默认 false）；
 *             - m_invertedAppearance/m_invertedControls：外观/控制翻转
 *               （默认 false）；
 *             - m_repeatAction/m_repeatActionThreshold/m_repeatActionTime：
 *               长按重复动作与阈值/间隔（存字段；timer 为后续扩展项）；
 *             - m_wheelDeltaRemainder/m_blockTracking：内部状态（滚轮
 *               余数累积 / triggerAction 期间禁止递归）。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XAbstractSlider
{
    XWidget m_base;                  /**< 基类成员；必须是第一个。 */
    int  m_min;                      /**< 范围下限。 */
    int  m_max;                      /**< 范围上限。 */
    int  m_value;                    /**< 当前值。 */
    int  m_position;                 /**< 句柄位置（与 m_value 分离）。 */
    int  m_orientation;              /**< 方向（XAbstractSliderOrientation）。 */
    int  m_pageStep;                 /**< 翻页步进。 */
    int  m_singleStep;               /**< 单步步进。 */
    bool m_tracking;                 /**< 拖动实时发射 valueChanged。 */
    bool m_sliderDown;               /**< handle 按下状态。 */
    bool m_invertedAppearance;       /**< 外观翻转。 */
    bool m_invertedControls;         /**< 控制翻转（键盘/滚轮方向）。 */
    int  m_repeatAction;             /**< 长按重复动作（默认 NoAction）。 */
    int  m_repeatActionThreshold;    /**< 长按重复初始延迟（毫秒；timer 后续扩展）。 */
    int  m_repeatActionTime;         /**< 长按重复间隔（毫秒；timer 后续扩展）。 */
    int  m_wheelDeltaRemainder;      /**< 内部：滚轮角度累积余数。 */
    bool m_blockTracking;            /**< 内部：triggerAction 期间禁止位置→值递归。 */
} XAbstractSlider;

/* ==================== 生命周期 ==================== */

/** @brief 初始化 XAbstractSlider 类虚函数表并返回共享表指针。 */
XVtable* XAbstractSlider_class_init(void);
/**
 * @brief      初始化 XAbstractSlider（对标 QAbstractSlider 构造）。
 * @param      self 待初始化对象；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志（可传 0 表示 Widget 类型）。
 */
void XAbstractSlider_init(XAbstractSlider* self, XWidget* parent,
                          XWidgetFlags flags);
/** @brief 使用默认内存类型创建滑块（headless 父对象语义同 init）。 */
#define XAbstractSlider_create(parent, flags) \
    XAbstractSlider_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建滑块。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志。
 * @return     新对象指针；失败返回 NULL。
 */
XAbstractSlider* XAbstractSlider_create_ex(XMemoryType memory,
                                           XWidget* parent,
                                           XWidgetFlags flags);
/** @brief 通过 XClass 虚表释放 XAbstractSlider 资源（栈/外部存储对象使用）。 */
#define XAbstractSlider_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
/** @brief 删除堆上的 XAbstractSlider 对象。 */
#define XAbstractSlider_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== 父类 XWidget API 宏转发（对齐库内 XRadioButton 惯例） ==================== */

#define XAbstractSlider_setEnabled(self, enabled) XWidget_setEnabled((XWidget*)(self), (enabled))
#define XAbstractSlider_isEnabled(self) XWidget_isEnabled((const XWidget*)(self))
#define XAbstractSlider_setVisible(self, visible) XWidget_setVisible((XWidget*)(self), (visible))
#define XAbstractSlider_isVisible(self) XWidget_isVisible((const XWidget*)(self))
#define XAbstractSlider_show(self) XWidget_show((XWidget*)(self))
#define XAbstractSlider_hide(self) XWidget_hide((XWidget*)(self))
#define XAbstractSlider_raise(self) XWidget_raise((XWidget*)(self))
#define XAbstractSlider_lower(self) XWidget_lower((XWidget*)(self))
#define XAbstractSlider_setGeometry(self, x, y, w, h) XWidget_setGeometry((XWidget*)(self), (x), (y), (w), (h))
#define XAbstractSlider_setGeometryRect(self, rect) XWidget_setGeometryRect((XWidget*)(self), (rect))
#define XAbstractSlider_x(self) XWidget_x((const XWidget*)(self))
#define XAbstractSlider_y(self) XWidget_y((const XWidget*)(self))
#define XAbstractSlider_width(self) XWidget_width((const XWidget*)(self))
#define XAbstractSlider_height(self) XWidget_height((const XWidget*)(self))
#define XAbstractSlider_resize(self, w, h) XWidget_resize((XWidget*)(self), (w), (h))
#define XAbstractSlider_move(self, x, y) XWidget_move((XWidget*)(self), (x), (y))
#define XAbstractSlider_update(self) XWidget_update((XWidget*)(self))
#define XAbstractSlider_updateRect(self, rect) XWidget_updateRect((XWidget*)(self), (rect))
#define XAbstractSlider_setParent(self, parent, flags) XWidget_setParent((XWidget*)(self), (parent), (flags))
#define XAbstractSlider_parentWidget(self) XWidget_parentWidget((const XWidget*)(self))
#define XAbstractSlider_setFocus(self) XWidget_setFocus((XWidget*)(self))
#define XAbstractSlider_clearFocus(self) XWidget_clearFocus((XWidget*)(self))
#define XAbstractSlider_hasFocus(self) XWidget_hasFocus((const XWidget*)(self))
#define XAbstractSlider_setFocusPolicy(self, policy) XWidget_setFocusPolicy((XWidget*)(self), (policy))
#define XAbstractSlider_setAttribute(self, attribute, on) XWidget_setAttribute((XWidget*)(self), (attribute), (on))
#define XAbstractSlider_testAttribute(self, attribute) XWidget_testAttribute((const XWidget*)(self), (attribute))
#define XAbstractSlider_setContentsMargins(self, l, t, r, b) XWidget_setContentsMargins((XWidget*)(self), (l), (t), (r), (b))
#define XAbstractSlider_contentsMargins(self) XWidget_contentsMargins((const XWidget*)(self))
#define XAbstractSlider_setWindowFlags(self, flags) XWidget_setWindowFlags((XWidget*)(self), (flags))
#define XAbstractSlider_updateGeometry(self) XWidget_updateGeometry((XWidget*)(self))


/* ==================== 属性（对标 QAbstractSlider public API） ==================== */

/** @brief 查询方向（对标 QAbstractSlider::orientation）。 */
int XAbstractSlider_orientation(const XAbstractSlider* self);
/** @brief 设置方向并重绘（对标 QAbstractSlider::setOrientation）。 */
void XAbstractSlider_setOrientation(XAbstractSlider* self, int orientation);
/** @brief 查询范围下限（对标 QAbstractSlider::minimum）。 */
int XAbstractSlider_minimum(const XAbstractSlider* self);
/** @brief 设置范围下限；下限超过当前上限时上限收敛为下限
 *         （对标 QAbstractSlider::setMinimum）。 */
void XAbstractSlider_setMinimum(XAbstractSlider* self, int min);
/** @brief 查询范围上限（对标 QAbstractSlider::maximum）。 */
int XAbstractSlider_maximum(const XAbstractSlider* self);
/** @brief 设置范围上限；上限低于当前下限时下限收敛为上限
 *         （对标 QAbstractSlider::setMaximum）。 */
void XAbstractSlider_setMaximum(XAbstractSlider* self, int max);
/** @brief 设置范围（min 超过 max 时 max 收敛为 min，对标
 *         QAbstractSlider::setRange 的 qMax 语义）。 */
void XAbstractSlider_setRange(XAbstractSlider* self, int min, int max);
/** @brief 查询单步步进（对标 QAbstractSlider::singleStep）。 */
int XAbstractSlider_singleStep(const XAbstractSlider* self);
/** @brief 设置单步步进并通知 SliderStepsChange（对标 setSingleStep）。 */
void XAbstractSlider_setSingleStep(XAbstractSlider* self, int step);
/** @brief 查询翻页步进（对标 QAbstractSlider::pageStep）。 */
int XAbstractSlider_pageStep(const XAbstractSlider* self);
/** @brief 设置翻页步进并通知 SliderStepsChange（对标 setPageStep）。 */
void XAbstractSlider_setPageStep(XAbstractSlider* self, int step);
/** @brief 查询拖动跟踪（对标 QAbstractSlider::hasTracking）。 */
bool XAbstractSlider_hasTracking(const XAbstractSlider* self);
/** @brief 设置拖动跟踪（对标 QAbstractSlider::setTracking）。 */
void XAbstractSlider_setTracking(XAbstractSlider* self, bool enable);
/** @brief 设置滑块按下状态；状态切换发射 sliderPressed/sliderReleased，
 *         抬起且位置≠值时提交位置到值（对标 QAbstractSlider::setSliderDown）。 */
void XAbstractSlider_setSliderDown(XAbstractSlider* self, bool down);
/** @brief 查询滑块按下状态（对标 QAbstractSlider::isSliderDown）。 */
bool XAbstractSlider_isSliderDown(const XAbstractSlider* self);
/** @brief 查询句柄位置（对标 QAbstractSlider::sliderPosition）。 */
int XAbstractSlider_sliderPosition(const XAbstractSlider* self);
/** @brief 设置句柄位置；tracking 时提交到值（经 SliderMove 动作），
 *         按下时发射 sliderMoved（对标 QAbstractSlider::setSliderPosition）。 */
void XAbstractSlider_setSliderPosition(XAbstractSlider* self, int position);
/** @brief 查询外观翻转（对标 QAbstractSlider::invertedAppearance）。 */
bool XAbstractSlider_invertedAppearance(const XAbstractSlider* self);
/** @brief 设置外观翻转并重绘（对标 setInvertedAppearance）。 */
void XAbstractSlider_setInvertedAppearance(XAbstractSlider* self, bool inverted);
/** @brief 查询控制翻转（对标 QAbstractSlider::invertedControls）。 */
bool XAbstractSlider_invertedControls(const XAbstractSlider* self);
/** @brief 设置控制翻转（翻转键盘/滚轮方向，对标 setInvertedControls）。 */
void XAbstractSlider_setInvertedControls(XAbstractSlider* self, bool inverted);
/** @brief 查询当前值（对标 QAbstractSlider::value）。 */
int XAbstractSlider_value(const XAbstractSlider* self);
/** @brief 设置当前值（钳位到范围并同步句柄位置；变化时发射
 *         valueChanged；对标 QAbstractSlider::setValue）。 */
void XAbstractSlider_setValue(XAbstractSlider* self, int value);

/* ==================== 动作（对标 QAbstractSlider） ==================== */

/**
 * @brief      触发滑块动作（对标 QAbstractSlider::triggerAction）。
 * @details    按动作调整句柄位置后发射 actionTriggered，再把位置提交
 *             到值（发射 valueChanged）；数值与 Qt 一致：SingleStepAdd/
 *             Sub 按 singleStep、PageStepAdd/Sub 按 pageStep、ToMinimum/
 *             ToMaximum 跳转边界、Move 直接提交位置。
 * @param      self 目标滑块；可为 NULL。
 * @param      action 要触发的动作（XAbstractSliderSliderAction）。
 */
void XAbstractSlider_triggerAction(XAbstractSlider* self, int action);
/**
 * @brief      设置长按自动重复动作（对标 QAbstractSlider::setRepeatAction）。
 * @details    本实现仅保存动作与阈值/间隔字段；长按自动重复的 timer
 *             为后续扩展项。action 为 SliderNoAction 时清空重复状态。
 * @param      self 目标滑块；可为 NULL。
 * @param      action 要重复触发的动作。
 * @param      thresholdTime 首次触发的初始延迟（毫秒；默认 500）。
 * @param      repeatTime 之后重复触发的间隔（毫秒；默认 50）。
 */
void XAbstractSlider_setRepeatAction(XAbstractSlider* self, int action,
                                     int thresholdTime, int repeatTime);
/** @brief 查询当前重复动作（对标 QAbstractSlider::repeatAction）。 */
int XAbstractSlider_repeatAction(const XAbstractSlider* self);

/* ==================== 虚槽（子类可重载；*_base 为多态调度入口） ==================== */

/**
 * @brief      步进虚槽（对标本项目约定的 QAbstractSlider 步进核心；
 *             Qt 侧对应触发动作的内部步进语义）。
 * @details    基类默认实现：把当前值按 steps（可正可负）钳位步进并同步
 *             句柄位置，按下时发射 sliderMoved、变化时发射 valueChanged。
 *             子类可重载自定义步进语义（经 EXAbstractSlider_StepBy 槽位）。
 * @param      self 滑块对象。
 * @param      steps 步数（正=增，负=减）。
 * @return     无返回值。
 */
void XAbstractSlider_stepBy_base(XAbstractSlider* self, int steps);
/**
 * @brief      滑块变化虚槽（对标 QAbstractSlider::sliderChange）。
 * @details    范围/步进/值变化时由对应 setter 通知；基类默认仅刷新显示
 *             （忽略 change 参数），子类可重载跟踪变化。
 * @param      self 滑块对象。
 * @param      change 变化类型（XAbstractSliderSliderChange）。
 * @return     无返回值。
 */
void XAbstractSlider_sliderChange_base(XAbstractSlider* self, int change);
/**
 * @brief      步进使能虚槽（对标 QAbstractSlider 子类常用 stepEnabled）。
 * @details    基类默认按当前值边界返回位组合：值 < 最大含
 *             StepUpEnabled、值 > 最小含 StepDownEnabled。
 * @param      self 滑块对象。
 * @return     步进使能位组合（XAbstractSliderStepEnabled）。
 */
int XAbstractSlider_stepEnabled_base(XAbstractSlider* self);

/* ==================== 信号 ==================== */

/** @brief valueChanged(int) 信号标识（值变化时发射）。 */
void* XAbstractSlider_valueChanged_signal(XAbstractSlider* self);
/** @brief sliderPressed() 信号标识（按下时发射）。 */
void* XAbstractSlider_sliderPressed_signal(XAbstractSlider* self);
/** @brief sliderMoved(int) 信号标识（按下期间位置变化时发射）。 */
void* XAbstractSlider_sliderMoved_signal(XAbstractSlider* self);
/** @brief sliderReleased() 信号标识（释放时发射）。 */
void* XAbstractSlider_sliderReleased_signal(XAbstractSlider* self);
/** @brief rangeChanged(int,int) 信号标识（范围变化时发射）。 */
void* XAbstractSlider_rangeChanged_signal(XAbstractSlider* self);
/** @brief actionTriggered(int) 信号标识（动作触发时发射）。 */
void* XAbstractSlider_actionTriggered_signal(XAbstractSlider* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON */

#ifdef __cplusplus
}
#endif
#endif /* XABSTRACTSLIDER_H */
