/**
 * @file       XScrollBar.h
 * @brief      XScrollBar 滚动条控件（对标 Qt 6.8 QScrollBar 公共 API +
 *             X 扩展 setShowButtons/createStandardContextMenu）。
 * @details    功能范围：
 *             - 方向：Horizontal/Vertical（默认 Vertical，对标无方向构造）；
 *             - 范围/步进继承 XAbstractSlider：默认 0..99、singleStep 1、
 *               pageStep 10、value 0（对标 QScrollBar 文档默认值）；
 *             - 鼠标交互（对标 QScrollBar::mousePress/Move/ReleaseEvent）：
 *               点击两端步进按钮执行 SingleStepAdd/Sub、点击轨道按 handle
 *               位置执行 PageStepAdd/Sub，拖动滑块按像素映射 setValue
 *               （对标 pixelPosToRangeValue）；**中键绝对定位**按样式提示
 *               SH_ScrollBar_MiddleClickAbsolutePosition 走（B8 终裁
 *               2026-11-15）：缺省样式 XCommonStyle 返回 false ⇒ 中键
 *               点击被 QScrollBar::mousePressEvent 首段守卫整体忽略
 *               （qcommonstyle.cpp:5441；不翻页、不进拖动）；装返回 1 的
 *               样式（Fusion 口径）时中键点在轨道上滑块中心直接跳到
 *               点击处并进入拖动；
 *             - 滚轮（对标 QScrollBar::wheelEvent qscrollbar.cpp:475-497）：
 *               按 |dx|>|dy| 选轴；水平轨道上若同时带非零水平份量则不
 *               消费（避免触摸板混沌滚动）；水平份量经基类
 *               scrollByDelta 取负；
 *             - 右键标准菜单（对标 QScrollBar::contextMenuEvent 的条目
 *               全集与顺序；Qt 无同名 createStandardContextMenu 公开函数，
 *               该方法属 X 扩展）：滚动到此处/上(左)缘/下(右)缘/
 *               向上(左)翻页/向下(右)翻页/向上(左)滚动/向下(右)滚动，
 *               条目文本随方向切换；菜单以 popup + DeleteOnClose 方式呈现；
 *             - sizeHint：对标 QScrollBar::sizeHint（qscrollbar.cpp:
 *               411-425）= PM_ScrollBarExtent*2 + PM_ScrollBarSliderMin，
 *               横竖互换；minimumSizeHint **不覆写**（Qt 实测 (-1,-1)）。
 *             - 尺寸策略：对标 QScrollBarPrivate::init（qscrollbar.cpp:
 *               357-361）QSizePolicy(Minimum, Fixed, Slider)，垂直转置。
 *             - 子控件悬停追踪（对标 QScrollBar::event 的 HoverEnter/
 *               HoverLeave/HoverMove 分支，qscrollbar.cpp:441-446 +
 *               QScrollBarPrivate::updateHoverControl/newHoverControl
 *               qscrollbar.cpp:161-187）：收 XHoverEvent，用样式层
 *               hitTestComplexControl(CC_ScrollBar) 判定命中子控件，命中
 *               变化时按「旧矩形 ∪ 新矩形」标脏（Qt `q->update(lastHoverRect);
 *               q->update(hoverRect)`）；paint 时按 Qt paintEvent:511-517
 *               的规则填 activeSubControls＝按下控件（有则）否则悬停控件。
 *             - transient 收缩 flash（对标 QScrollBarPrivate::setTransient/
 *               flash 与 QScrollBar::event 的 Timer 分支，qscrollbar.cpp:
 *               189-219、453-464）：`flash()` 在 transient 样式下点亮
 *               `flashed` 并起 0ms 定时器，到期清位再标脏；wheelEvent 按
 *               Qt :494-497 用 ScrollBegin/ScrollEnd 切换 transient。
 * @note       模块总开关 XSCROLLBAR_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XABSTRACTSLIDER_ON。
 *             绘制：Fusion 非 transient（SH_ScrollBar_Transient=0）缺省
 *             **绘制两端步进按钮**；X 样式层未提供 transient 切换，故
 *             缺省按 Fusion 口径开按钮，并可用 X 扩展
 *             XScrollBar_setShowButtons 显式覆盖。
 * @author     XinYueC 团队
 */
#ifndef XSCROLLBAR_H
#define XSCROLLBAR_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XAbstractSlider.h"

#if XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON

XCLASS_DEFINE_BEGING(XScrollBar)
XCLASS_DEFINE_EXTEND_END(XScrollBar, XAbstractSlider)

/**
 * @brief      XScrollBar 控件对象；m_base 必须是第一个成员。
 */
typedef struct XScrollBar
{
    XAbstractSlider m_base;      /**< 基类成员；必须是第一个。 */
    int  m_pressOffset;          /**< 按下点相对滑块原点的偏移像素（保守保留 int）。 */
    /* [内存治理 2026-10-11] 标志位字：3 个 bool + AddLine/SubLine 指示
       （0/1）合并 1 字。 */
    uint32_t m_dragging : 1;      /**< 滑块拖动中。 */
    uint32_t m_showButtons : 1;   /**< 显示两端步进按钮（对标
                                      QScrollBar 的 SH_ScrollBar_Transient
                                      非 transient 形态；Qt Fusion 实测
                                      transient=0 ⇒ 缺省 **true** 画
                                      Add/SubLine 按钮，见
                                      XScrollBar_setShowButtons）。 */
    uint32_t m_showButtonsExplicit : 1; /**< 用户经 X 扩展 setShowButtons
                                      显式覆盖过（StyleChange 不再按
                                      transient 提示回写）。 */
    uint32_t m_activeSub : 1;     /**< 有按钮处于按下态（对标
                                      activeSubControls 非空）。 */
    uint32_t m_activeIsAdd : 1;   /**< 按下的是 AddLine（1）或 SubLine（0）：
                                      填充样式选项 m_scrollActiveDir
                                      （0 无 / 1 SubLine / 2 AddLine，
                                      P0-67），样式层只加深被按下的那一个
                                      按钮（Qt activeSubControls 位掩码口径）。 */
    uint32_t m_flashed : 1;       /**< transient 收缩 flash 已点亮（对标
                                      QScrollBarPrivate::flashed，
                                      qscrollbar.cpp:354/211/458）。 */
    uint32_t m_transient : 1;     /**< 当前 transient 态（对标
                                      QScrollBarPrivate::transient，
                                      qscrollbar.cpp:353；由 StyleChange 与
                                      wheelEvent 的 ScrollBegin/ScrollEnd
                                      改写，setTransient 只在真翻转时生效）。 */
    uint32_t m_pointerOutside : 1; /**< 按下控件指针已移出（对标
                                      QScrollBarPrivate::
                                      pointerOutsidePressedControl，
                                      qscrollbar.cpp:350/637/651）：移出即
                                      收连发、移回即恢复连发。 */
    uint32_t m_scrollBarFlagsUnused : 24; /**< 保留位。 */
    /* [2026-11-15 §B8] 尾部追加（指定语义，位置初始化器零依赖）：
     * 悬停子控件追踪 + transient flash 定时器。 */
    XTimerId m_flashTimerId;      /**< transient flash 定时器（对标
                                      QScrollBarPrivate::flashTimer；
                                      XTIMER_INVALID_ID=未布防）。 */
    XTimerId m_fadeTimerId;       /**< transient 淡出/淡入定时器（X 侧替代
                                      Qt 样式层的 QScrollbarStyleAnimation；
                                      XTIMER_INVALID_ID=未布防）。 */
    uint32_t m_hoverControl;      /**< 悬停命中的子控件（XStyleSubControl
                                      位；0=SC_None）。对标
                                      QScrollBarPrivate::hoverControl。 */
    XRect m_hoverRect;            /**< 上次悬停子控件矩形（对标
                                      QScrollBarPrivate::hoverRect；
                                      空矩形＝上次无命中）。 */
    int m_pressedControl;         /**< 按下命中的子控件（XStyleSubControl
                                      位；0=SC_None）。对标
                                      QScrollBarPrivate::pressedControl。 */
    int m_snapBackPosition;       /**< 按下时的句柄位置（对标
                                      QScrollBarPrivate::snapBackPosition，
                                      超出 PM_MaximumDragDistance 时回弹）。 */
    float m_transientOpacity;     /**< transient 当前不透明度（1.0=常态；
                                      transient 淡出时降到 0.0）。 */
    uint32_t m_fadeStep;          /**< 淡出进度（0..XSB_FADE_STEPS）。 */
    uint8_t  m_fadeMode;          /**< 淡出阶段（内部 XsbFadeMode：
                                      0 Idle/1 Activating/2 Delaying/
                                      3 Deactivating）。 */
} XScrollBar;

/* ==================== 生命周期 ==================== */

/**
 * @brief      初始化类虚函数表（对标 Qt 的 metaObject 构建过程）。
 */
XVtable* XScrollBar_class_init(void);
/**
 * @brief      初始化控件（对标构造函数）。
 */
void XScrollBar_init(XScrollBar* self, XWidget* parent, XWidgetFlags flags);
/** @brief 以指定方向初始化（对标 QScrollBar(Qt::Orientation, parent*)）。 */
/**
 * @brief      按方向初始化滚动条（对标带 orientation 的构造函数）。
 */
void XScrollBar_init_2(XScrollBar* self, int orientation,
                       XWidget* parent, XWidgetFlags flags);
#define XScrollBar_create(parent, flags) XScrollBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      按指定内存类型创建控件实例。
 */
XScrollBar* XScrollBar_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags);
#define XScrollBar_create_2(orientation, parent, flags) \
    XScrollBar_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, (orientation), (parent), (flags))
/**
 * @brief      按内存类型和方向创建滚动条实例。
 */
XScrollBar* XScrollBar_create_ex_2(XMemoryType memory, int orientation,
                                   XWidget* parent, XWidgetFlags flags);

/* ==================== 右键标准菜单（对标 contextMenuEvent 菜单条目） ==================== */

/** @brief 构造标准滚动条右键菜单（条目/顺序/随方向变化的文本完全对齐
 *         QScrollBar::contextMenuEvent；返回的菜单所有权转移给调用方）。
 *         供右键菜单实现与测试使用。 */
/**
 * @brief      创建标准右键上下文菜单（对标 QScrollBar::createStandardContextMenu）。
 */
XMenu* XScrollBar_createStandardContextMenu(XScrollBar* self);

/* ==================== 两端步进按钮（Qt 无同名 API） ==================== */

/**
 * @brief      设置是否绘制/命中两端步进按钮（Qt 无同名 API）。
 * @details    QScrollBar 无此公开 setter——Qt 侧由样式 transient 语义决定：
 *             `QScrollBarPrivate::init` 取
 *             `style()->styleHint(QStyle::SH_ScrollBar_Transient,...)`
 *             **原值**（无取反，qscrollbar.cpp:353），取反只出现在用法处
 *             `!d->transient`（qscrollbar.cpp:199、:294）。本框架的对应
 *             提示为 `XStyleSH_ScrollBar_Transient`（XStyleOption.h，枚举
 *             值 96 与 Qt 一致），XCommonStyle 返回 0＝非 transient，
 *             与 Qt Fusion 实测一致 ⇒ **缺省绘制两端按钮**（2026-11-11
 *             起对齐 Qt；此前本库缺省为不绘制）。
 *             本 setter 是 X 扩展，显式调用后 StyleChange 事件不再按
 *             transient 提示回写（m_showButtonsExplicit）。
 * @param      self 滚动条对象；可为 NULL。
 * @param      show true 显示两端步进按钮。
 * @return     无返回值。
 */
void XScrollBar_setShowButtons(XScrollBar* self, bool show);
/**
 * @brief      查询两端步进按钮显隐（缺省 true＝Fusion 非 transient 口径）。
 * @param      self 滚动条对象；可为 NULL。
 * @return     true 表示两端步进按钮可见；self 为 NULL 时返回 false。
 */
bool XScrollBar_showButtons(const XScrollBar* self);

/* ==================== 子控件悬停 / transient（对标 QScrollBarPrivate） ==================== */

/**
 * @brief      查询当前悬停命中的子控件（对标 QScrollBarPrivate::hoverControl）。
 * @details    由 HoverEnter/HoverLeave/HoverMove 事件经样式层
 *             `hitTestComplexControl(CC_ScrollBar)` 更新；绘制时若没有按下
 *             子控件，本值填入样式选项的 activeSubControls
 *             （对标 QScrollBar::paintEvent，qscrollbar.cpp:511-517）。
 * @param      self 滚动条对象；可为 NULL。
 * @return     XStyleSubControl 位标志；无命中或 self 为 NULL 时返回
 *             XStyleSC_None。
 */
int XScrollBar_hoverControl(const XScrollBar* self);

/**
 * @brief      按给定局部坐标重算悬停子控件（对标
 *             QScrollBarPrivate::updateHoverControl，qscrollbar.cpp:161-173）。
 * @details    命中与上次不同且控件声明了 XWidgetAttribute_Hover 时，按
 *             「旧矩形 ∪ 新矩形」两段标脏并返回 true；未声明 Hover 属性
 *             时不动状态、返回 true（Qt 的 `return !doesHover` 语义——
 *             调用方据此不把自己标脏）。
 *             本函数是公开入口，供悬停事件槽与测试直接驱动（Qt 侧为
 *             private 成员，X 侧公开是为了让无窗口系统的单元测试能验证
 *             子控件追踪）。
 * @param      self 滚动条对象；可为 NULL。
 * @param      x    控件局部坐标 X。
 * @param      y    控件局部坐标 Y。
 * @return     true 表示「命中已处理/无需再处理」；self 为 NULL 时返回
 *             false。
 */
bool XScrollBar_updateHoverControl(XScrollBar* self, int x, int y);

/**
 * @brief      点亮 transient 收缩 flash（对标 QScrollBarPrivate::flash，
 *             qscrollbar.cpp:205-219）。
 * @details    仅在样式提示 SH_ScrollBar_Transient 为真且尚未点亮时置
 *             `m_flashed`（不可见则先 show，可见则标脏），随后布防一次 0ms
 *             定时器（Qt `q->startTimer(0)`）；定时器到期清 `m_flashed`
 *             并再次标脏（QScrollBar::event 的 Timer 分支，
 *             qscrollbar.cpp:453-464）。非 transient 样式下本函数只布防
 *             定时器、不改视觉（与 Qt 同：`if (!flashed && styleHint(...))`）。
 * @param      self 滚动条对象；可为 NULL。
 * @return     无返回值。
 */
void XScrollBar_flash(XScrollBar* self);

/**
 * @brief      查询 transient flash 是否处于点亮态（对标 QScrollBarPrivate::flashed）。
 * @param      self 滚动条对象；可为 NULL。
 * @return     true 表示 flash 已点亮（样式选项 State_On 由它驱动）；self
 *             为 NULL 时返回 false。
 */
bool XScrollBar_flashed(const XScrollBar* self);

/**
 * @brief      查询/设置 transient 态（对标 QScrollBarPrivate::transient 与
 *             setTransient，qscrollbar.cpp:189-203）。
 * @details    setter 只在真翻转时生效：可见时按样式提示标脏；不可见且切到
 *             非 transient 时 show()（Qt `else if (!transient) q->show()`）。
 *             wheelEvent 按 Qt :494-497 以 ScrollBegin/ScrollEnd 调用它。
 * @param      self 滚动条对象；可为 NULL。
 * @return     XScrollBar_transient 返回当前态；self 为 NULL 时返回 false。
 */
bool XScrollBar_transient(const XScrollBar* self);
/** @brief 设置 transient 态（语义见 XScrollBar_transient）。 */
void XScrollBar_setTransient(XScrollBar* self, bool transient);

/**
 * @brief      查询 transient 当前不透明度（1.0=常态；淡出中趋 0）。
 * @details    X 侧替代 Qt 样式层的 QScrollbarStyleAnimation
 *             （qstyleanimation.cpp:291-305：Activating 200ms
 *             0→1，Deactivating 先延迟 450ms 再 200ms 1→0）。本框架不建
 *             动画对象，改用框架定时器（XObject_startTimer，铁律 4）按
 *             固定步长推进 m_fadeStep；绘制时按本值设 painter opacity。
 * @param      self 滚动条对象；可为 NULL。
 * @return     不透明度 0.0~1.0；self 为 NULL 时返回 1.0。
 */
float XScrollBar_transientOpacity(const XScrollBar* self);

/* ==================== 父类 API 宏转发（对齐库内 XDial 惯例） ==================== */

#define XScrollBar_minimum(self) XAbstractSlider_minimum((const XAbstractSlider*)(self))
#define XScrollBar_maximum(self) XAbstractSlider_maximum((const XAbstractSlider*)(self))
#define XScrollBar_setMinimum(self, min) XAbstractSlider_setMinimum((XAbstractSlider*)(self), (min))
#define XScrollBar_setMaximum(self, max) XAbstractSlider_setMaximum((XAbstractSlider*)(self), (max))
#define XScrollBar_setRange(self, min, max) XAbstractSlider_setRange((XAbstractSlider*)(self), (min), (max))
#define XScrollBar_value(self) XAbstractSlider_value((const XAbstractSlider*)(self))
#define XScrollBar_setValue(self, v) XAbstractSlider_setValue((XAbstractSlider*)(self), (v))
#define XScrollBar_singleStep(self) XAbstractSlider_singleStep((const XAbstractSlider*)(self))
#define XScrollBar_setSingleStep(self, s) XAbstractSlider_setSingleStep((XAbstractSlider*)(self), (s))
#define XScrollBar_pageStep(self) XAbstractSlider_pageStep((const XAbstractSlider*)(self))
#define XScrollBar_setPageStep(self, s) XAbstractSlider_setPageStep((XAbstractSlider*)(self), (s))
#define XScrollBar_orientation(self) XAbstractSlider_orientation((const XAbstractSlider*)(self))
#define XScrollBar_setOrientation(self, o) XAbstractSlider_setOrientation((XAbstractSlider*)(self), (o))
#define XScrollBar_sizeHint(self) XWidget_sizeHint_base((const XWidget*)(self))
#define XScrollBar_minimumSizeHint(self) XWidget_minimumSizeHint_base((const XWidget*)(self))
#define XScrollBar_isSliderDown(self) XAbstractSlider_isSliderDown((const XAbstractSlider*)(self))
#define XScrollBar_setSliderDown(self, d) XAbstractSlider_setSliderDown((XAbstractSlider*)(self), (d))
#define XScrollBar_valueChanged_signal(self, value) XAbstractSlider_valueChanged_signal((XAbstractSlider*)(self), (value))
#define XScrollBar_sliderMoved_signal(self, position) XAbstractSlider_sliderMoved_signal((XAbstractSlider*)(self), (position))
#define XScrollBar_actionTriggered_signal(self, action) XAbstractSlider_actionTriggered_signal((XAbstractSlider*)(self), (action))
/* QAbstractSlider 的其余公开成员（Qt 侧全部可直接调用，qabstractslider.h:51-101）；
 * 此前 XScrollBar 层缺这些转发宏，调用方只能显式转型到 XAbstractSlider。 */
#define XScrollBar_hasTracking(self) XAbstractSlider_hasTracking((const XAbstractSlider*)(self))
#define XScrollBar_setTracking(self, e) XAbstractSlider_setTracking((XAbstractSlider*)(self), (e))
#define XScrollBar_sliderPosition(self) XAbstractSlider_sliderPosition((const XAbstractSlider*)(self))
#define XScrollBar_setSliderPosition(self, p) XAbstractSlider_setSliderPosition((XAbstractSlider*)(self), (p))
#define XScrollBar_invertedAppearance(self) XAbstractSlider_invertedAppearance((const XAbstractSlider*)(self))
#define XScrollBar_setInvertedAppearance(self, i) XAbstractSlider_setInvertedAppearance((XAbstractSlider*)(self), (i))
#define XScrollBar_invertedControls(self) XAbstractSlider_invertedControls((const XAbstractSlider*)(self))
#define XScrollBar_setInvertedControls(self, i) XAbstractSlider_setInvertedControls((XAbstractSlider*)(self), (i))
#define XScrollBar_triggerAction(self, a) XAbstractSlider_triggerAction((XAbstractSlider*)(self), (a))
#define XScrollBar_setRepeatAction(self, a, t, r) XAbstractSlider_setRepeatAction((XAbstractSlider*)(self), (a), (t), (r))
#define XScrollBar_repeatAction(self) XAbstractSlider_repeatAction_base((const XAbstractSlider*)(self))
#define XScrollBar_sliderPressed_signal(self) XAbstractSlider_sliderPressed_signal((XAbstractSlider*)(self))
#define XScrollBar_sliderReleased_signal(self) XAbstractSlider_sliderReleased_signal((XAbstractSlider*)(self))
#define XScrollBar_rangeChanged_signal(self, min, max) XAbstractSlider_rangeChanged_signal((XAbstractSlider*)(self), (min), (max))
#define XScrollBar_stepBy_base(self, s) XAbstractSlider_stepBy_base((XAbstractSlider*)(self), (s))
#define XScrollBar_sliderChange_base(self, c) XAbstractSlider_sliderChange_base((XAbstractSlider*)(self), (c))
#define XScrollBar_stepEnabled_base(self) XAbstractSlider_stepEnabled_base((XAbstractSlider*)(self))

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XSCROLLBAR_H */
