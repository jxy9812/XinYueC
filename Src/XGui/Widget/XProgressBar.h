/**
 * @file       XProgressBar.h
 * @brief      XProgressBar 进度条控件（对标 Qt 6.8 QProgressBar）。
 * @details    提供水平/垂直进度条：范围与当前值（setValue 越界钳位并
 *             发射 valueChanged 信号；setMinimum/setMaximum 单独设置
 *             端点，对标 QProgressBar 同名槽）、外观翻转、百分比/数值
 *             文本格式串（%p 百分比、%v 当前值、%m 最大值；setFormat/
 *             resetFormat）、文本可见性与对齐，以及垂直文本方向
 *             （textDirection：TopToBottom/BottomToTop，垂直绘制时按
 *             方向旋转 90°）。绘制走 XPainter：凹陷凹槽（调色板 Dark/
 *             Light 描边 + Base 底）+ Highlight 进度块 + 居中文本，无
 *             裁剪能力构建时退化为单色描边。
 * @note       对标 QProgressBar::initStyleOption 的样式选项结构体
 *             （QStyleOptionProgressBar）本项目未提供，标注后续扩展；
 *             QProgressBar::event 中对动态属性的处理（动态属性机制未
 *             接入）亦为后续扩展，本类 event() 入口保留并调用父类。
 * @note       模块总开关 XPROGRESSBAR_ON 定义于 XGuiConfig.h；
 *             XPROGRESSBAR_ON=0 时裁剪整个 XProgressBar 公共 API。
 *             XProgressBar 依赖 XWIDGET_ON、XPALETTE_ON（关闭时绘制
 *             退化为无操作）与 XPAINTER_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPROGRESSBAR_H
#define XPROGRESSBAR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XPainter.h"
#include "XAlignment.h"

#if XWIDGET_ON && XPROGRESSBAR_ON

/* ==================== 方向（对标 Qt::Orientation 数值一致） ==================== */

/** @brief 进度条方向（数值与 Qt::Orientation 完全一致）。 */
typedef enum XProgressBarOrientation
{
    XProgressBarOrientation_Horizontal = 1, /**< 水平进度条（默认，左→右）。 */
    XProgressBarOrientation_Vertical = 2    /**< 垂直进度条（默认下→上）。 */
} XProgressBarOrientation;

/* ==================== 文本方向（对标 QProgressBar::Direction 数值一致） ==================== */

/** @brief 垂直进度条文本方向（数值与 QProgressBar::Direction 完全一致）。 */
typedef enum XProgressBarDirection
{
    XProgressBarDirection_TopToBottom = 0, /**< 文本自上而下排布（默认；垂直时顺时针旋转 90° 绘制）。 */
    XProgressBarDirection_BottomToTop = 1  /**< 文本自下而上排布（垂直时逆时针旋转 90° 绘制）。 */
} XProgressBarDirection;

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XProgressBar)
XCLASS_DEFINE_EXTEND_END(XProgressBar, XWidget)

/**
 * @brief      XProgressBar 进度条控件对象；m_base 必须是第一个成员
 *             （嵌 XWidget）。
 * @details    字段含义：
 *             - m_min/m_max：进度范围（默认 0/100，对标
 *               QProgressBar::minimum/maximum）；
 *             - m_value：当前进度（默认 0，越界写入时钳位）；
 *             - m_orientation：方向（默认水平，对标 orientation）；
 *             - m_invertedAppearance：外观翻转（默认 false，进度从
 *               另一端生长，对标 invertedAppearance）；
 *             - m_textVisible：是否绘制格式化文本（默认 true，对标
 *               textVisible）；
 *             - m_textDirection：垂直文本方向（默认 TopToBottom，对标
 *               QProgressBar::textDirection，仅垂直进度条生效）；
 *             - m_alignment：文本对齐（默认水平居中|垂直居中，对标
 *               alignment）；
 *             - m_format：文本格式串（默认 "%p%"；%p 百分比、%v 当前
 *               值、%m 最大值，对标 setFormat）。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XProgressBar
{
    XWidget m_base;                 /**< 基类成员；必须是第一个。 */
    int     m_min;                  /**< 进度范围下限。 */
    int     m_max;                  /**< 进度范围上限。 */
    int     m_value;                /**< 当前进度值。 */
    int     m_orientation;          /**< 方向（XProgressBarOrientation）。 */
    bool    m_invertedAppearance;   /**< 外观翻转。 */
    bool    m_textVisible;          /**< 是否绘制文本。 */
    int     m_textDirection;        /**< 垂直文本方向（XProgressBarDirection）。 */
    int     m_alignment;            /**< 文本对齐（XAlignment 组合）。 */
    char    m_format[32];           /**< 文本格式串（NUL 结尾）。 */
} XProgressBar;

/* ==================== 生命周期（对标 QProgressBar 构造/析构） ==================== */

/**
 * @brief      初始化 XProgressBar 类虚函数表并返回共享表指针。
 * @return     XProgressBar 类共享的虚函数表指针；初始化失败时返回 NULL。
 */
XVtable* XProgressBar_class_init(void);

/**
 * @brief      初始化 XProgressBar（对标 QProgressBar(parent) 构造）。
 * @details    先初始化 XWidget 基类，再挂 XProgressBar 虚表并设置默认
 *             值：范围 0..100、值 0、水平方向、文本可见、格式 "%p%"、
 *             对齐居中。
 * @param      self   待初始化对象；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志（可传 0 表示 Widget 类型）。
 * @return     无返回值。
 */
void XProgressBar_init(XProgressBar* self, XWidget* parent, XWidgetFlags flags);

/** @brief 使用默认内存类型创建进度条（语义同 XWidget_create）。 */
#define XProgressBar_create(parent, flags) XProgressBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建进度条。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志。
 * @return     新对象指针；失败返回 NULL。
 */
XProgressBar* XProgressBar_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);

/** @brief 反初始化（宏复用基类 XWidget 的 deinit 入口）。 */
#define XProgressBar_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
/** @brief 释放堆对象（宏复用基类 XWidget 的 delete 入口）。 */
#define XProgressBar_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== 父类 XWidget API 宏转发（对齐库内 XRadioButton 惯例） ==================== */

#define XProgressBar_setEnabled(self, enabled) XWidget_setEnabled((XWidget*)(self), (enabled))
#define XProgressBar_isEnabled(self) XWidget_isEnabled((const XWidget*)(self))
#define XProgressBar_setVisible(self, visible) XWidget_setVisible((XWidget*)(self), (visible))
#define XProgressBar_isVisible(self) XWidget_isVisible((const XWidget*)(self))
#define XProgressBar_show(self) XWidget_show((XWidget*)(self))
#define XProgressBar_hide(self) XWidget_hide((XWidget*)(self))
#define XProgressBar_raise(self) XWidget_raise((XWidget*)(self))
#define XProgressBar_lower(self) XWidget_lower((XWidget*)(self))
#define XProgressBar_setGeometry(self, x, y, w, h) XWidget_setGeometry((XWidget*)(self), (x), (y), (w), (h))
#define XProgressBar_setGeometryRect(self, rect) XWidget_setGeometryRect((XWidget*)(self), (rect))
#define XProgressBar_x(self) XWidget_x((const XWidget*)(self))
#define XProgressBar_y(self) XWidget_y((const XWidget*)(self))
#define XProgressBar_width(self) XWidget_width((const XWidget*)(self))
#define XProgressBar_height(self) XWidget_height((const XWidget*)(self))
#define XProgressBar_resize(self, w, h) XWidget_resize((XWidget*)(self), (w), (h))
#define XProgressBar_move(self, x, y) XWidget_move((XWidget*)(self), (x), (y))
#define XProgressBar_update(self) XWidget_update((XWidget*)(self))
#define XProgressBar_updateRect(self, rect) XWidget_updateRect((XWidget*)(self), (rect))
#define XProgressBar_setParent(self, parent, flags) XWidget_setParent((XWidget*)(self), (parent), (flags))
#define XProgressBar_parentWidget(self) XWidget_parentWidget((const XWidget*)(self))
#define XProgressBar_setFocus(self) XWidget_setFocus((XWidget*)(self))
#define XProgressBar_clearFocus(self) XWidget_clearFocus((XWidget*)(self))
#define XProgressBar_hasFocus(self) XWidget_hasFocus((const XWidget*)(self))
#define XProgressBar_setFocusPolicy(self, policy) XWidget_setFocusPolicy((XWidget*)(self), (policy))
#define XProgressBar_setAttribute(self, attribute, on) XWidget_setAttribute((XWidget*)(self), (attribute), (on))
#define XProgressBar_testAttribute(self, attribute) XWidget_testAttribute((const XWidget*)(self), (attribute))
#define XProgressBar_setContentsMargins(self, l, t, r, b) XWidget_setContentsMargins((XWidget*)(self), (l), (t), (r), (b))
#define XProgressBar_contentsMargins(self) XWidget_contentsMargins((const XWidget*)(self))
#define XProgressBar_setWindowFlags(self, flags) XWidget_setWindowFlags((XWidget*)(self), (flags))
#define XProgressBar_updateGeometry(self) XWidget_updateGeometry((XWidget*)(self))

/* 拷贝/移动统一使用 XCopy(dst, src) / XMove(dst, src)（XClass.h 定义，
 * 经 VXProgressBar_copy/move 虚槽多态执行；不再提供 *_copy_base/
 * *_move_base 别名宏）。 */

/* ==================== 范围与数值（对标 QProgressBar public API） ==================== */

/**
 * @brief      查询进度范围下限（对标 QProgressBar::minimum）。
 * @param      self 进度条对象；可为 NULL。
 * @return     当前下限；self 为 NULL 时返回 0。
 */
int XProgressBar_minimum(const XProgressBar* self);

/**
 * @brief      查询进度范围上限（对标 QProgressBar::maximum）。
 * @param      self 进度条对象；可为 NULL。
 * @return     当前上限；self 为 NULL 时返回 100。
 */
int XProgressBar_maximum(const XProgressBar* self);

/**
 * @brief      设置进度范围（对标 QProgressBar::setRange）。
 * @details    min > max 时自动交换；范围变化后当前值按新范围钳位并
 *             触发重绘（值实际变化时发射 valueChanged）。
 * @param      self 进度条对象；可为 NULL。
 * @param      min  新下限。
 * @param      max  新上限。
 * @return     无返回值。
 */
void XProgressBar_setRange(XProgressBar* self, int min, int max);

/**
 * @brief      单独设置进度范围下限（对标 QProgressBar::setMinimum 槽）。
 * @details    若新下限大于当前上限，则上限同步抬升到新下限（Qt 语义：
 *             newMax = qMax(max, minimum) 后 setRange）。
 * @param      self    进度条对象；可为 NULL。
 * @param      minimum 新下限。
 * @return     无返回值。
 */
void XProgressBar_setMinimum(XProgressBar* self, int minimum);

/**
 * @brief      单独设置进度范围上限（对标 QProgressBar::setMaximum 槽）。
 * @details    若新上限小于当前下限，则下限同步下压到新上限（Qt 语义：
 *             newMin = qMin(min, maximum) 后 setRange）。
 * @param      self    进度条对象；可为 NULL。
 * @param      maximum 新上限。
 * @return     无返回值。
 */
void XProgressBar_setMaximum(XProgressBar* self, int maximum);

/**
 * @brief      查询当前进度值（对标 QProgressBar::value）。
 * @param      self 进度条对象；可为 NULL。
 * @return     当前值（已钳位在范围内）；self 为 NULL 时返回 0。
 */
int XProgressBar_value(const XProgressBar* self);

/**
 * @brief      设置当前进度值（对标 QProgressBar::setValue）。
 * @details    越界值钳位到 [min, max]；值实际变化时发射
 *             valueChanged(int) 并触发重绘。
 * @param      self  进度条对象；可为 NULL。
 * @param      value 新进度值。
 * @return     无返回值。
 */
void XProgressBar_setValue(XProgressBar* self, int value);

/**
 * @brief      重置进度值到下限并重绘（对标 QProgressBar::reset）。
 * @details    同时清空格式化文本缓存；不发 valueChanged（对标 Qt：
 *             reset 后 value == minimum 但不发射信号）。
 * @param      self 进度条对象；可为 NULL。
 * @return     无返回值。
 */
void XProgressBar_reset(XProgressBar* self);

/* ==================== 外观属性 ==================== */

/**
 * @brief      查询方向（对标 QProgressBar::orientation）。
 * @param      self 进度条对象；可为 NULL。
 * @return     XProgressBarOrientation；self 为 NULL 时返回水平。
 */
int XProgressBar_orientation(const XProgressBar* self);

/**
 * @brief      设置方向并重绘（对标 QProgressBar::setOrientation）。
 * @param      self        进度条对象；可为 NULL。
 * @param      orientation 新方向（水平/垂直）。
 * @return     无返回值。
 */
void XProgressBar_setOrientation(XProgressBar* self, int orientation);

/**
 * @brief      查询外观翻转（对标 QProgressBar::invertedAppearance）。
 * @param      self 进度条对象；可为 NULL。
 * @return     true 表示进度从反方向生长；self 为 NULL 时返回 false。
 */
bool XProgressBar_invertedAppearance(const XProgressBar* self);

/**
 * @brief      设置外观翻转并重绘（对标 setInvertedAppearance）。
 * @param      self      进度条对象；可为 NULL。
 * @param      inverted  true 表示进度从反方向生长。
 * @return     无返回值。
 */
void XProgressBar_setInvertedAppearance(XProgressBar* self, bool inverted);

/**
 * @brief      查询文本可见性（对标 QProgressBar::isTextVisible）。
 * @param      self 进度条对象；可为 NULL。
 * @return     true 表示绘制格式化文本；self 为 NULL 时返回 true。
 */
bool XProgressBar_isTextVisible(const XProgressBar* self);

/**
 * @brief      设置文本可见性并重绘（对标 setTextVisible）。
 * @param      self   进度条对象；可为 NULL。
 * @param      visible true 绘制文本。
 * @return     无返回值。
 */
void XProgressBar_setTextVisible(XProgressBar* self, bool visible);

/**
 * @brief      查询垂直文本方向（对标 QProgressBar::textDirection）。
 * @param      self 进度条对象；可为 NULL。
 * @return     当前方向（XProgressBarDirection）；self 为 NULL 时返回
 *             TopToBottom。
 */
int XProgressBar_textDirection(const XProgressBar* self);

/**
 * @brief      设置垂直文本方向并重绘（对标 setTextDirection）。
 * @details    仅影响垂直进度条：TopToBottom 文本自上而下（顺时针旋转
 *             90°），BottomToTop 自下而上（逆时针旋转 90°）；水平进度
 *             条上无视觉效果（对标 Qt：该属性对水平进度条无效）。
 * @param      self          进度条对象；可为 NULL。
 * @param      textDirection 新方向（XProgressBarDirection）；非法值忽略。
 * @return     无返回值。
 */
void XProgressBar_setTextDirection(XProgressBar* self, int textDirection);

/**
 * @brief      查询文本对齐（XAlignment 组合；默认 HCenter|VCenter）。
 * @param      self 进度条对象；可为 NULL。
 * @return     当前对齐组合；self 为 NULL 时返回居中组合。
 */
int XProgressBar_alignment(const XProgressBar* self);

/**
 * @brief      设置文本对齐并重绘。
 * @param      self      进度条对象；可为 NULL。
 * @param      alignment XAlignment 组合。
 * @return     无返回值。
 */
void XProgressBar_setAlignment(XProgressBar* self, int alignment);

/**
 * @brief      查询文本格式串（内部缓冲借用指针；对标 QProgressBar::
 *             format）。
 * @param      self 进度条对象；不可为 NULL。
 * @return     格式串借用指针（"%p" 百分比 / "%v" 值 / "%m" 最大值）；
 *             self 为 NULL 时返回默认 "%p%"。
 */
const char* XProgressBar_format(const XProgressBar* self);

/**
 * @brief      设置文本格式串并重绘（对标 setFormat）。
 * @details    超长串截断到内部缓冲上限（31 字符 + NUL）。
 * @param      self   进度条对象；可为 NULL。
 * @param      format 格式串（UTF-8，NUL 结尾）；NULL 恢复默认 "%p%"。
 * @return     无返回值。
 */
void XProgressBar_setFormat(XProgressBar* self, const char* format);

/**
 * @brief      恢复默认文本格式串 "%p%" 并重绘（对标 QProgressBar::
 *             resetFormat）。
 * @param      self 进度条对象；可为 NULL。
 * @return     无返回值。
 */
void XProgressBar_resetFormat(XProgressBar* self);

/**
 * @brief      生成当前格式化文本（按 m_format 替换 %p/%v/%m）。
 * @param      self    进度条对象；可为 NULL。
 * @param      out     输出缓冲；不可为 NULL。
 * @param      outSize 输出缓冲大小（含 NUL）；不足时截断。
 * @return     无返回值。
 */
void XProgressBar_text(const XProgressBar* self, char* out, int outSize);

/* ==================== 信号 ==================== */

/**
 * @brief      valueChanged(int) 信号标识（对标 QProgressBar::
 *             valueChanged）。
 * @param      self 进度条对象；可为 NULL。
 * @return     不透明信号标识；返回值不指向可释放对象，也不得解引用。
 */
void* XProgressBar_valueChanged_signal(XProgressBar* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XPROGRESSBAR_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPROGRESSBAR_H */
