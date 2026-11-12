/**
 * @file       XProgressBar.h
 * @brief      XProgressBar 进度条控件（对标 Qt 6.8 QProgressBar）。
 * @details    提供水平/垂直进度条：范围与当前值（**构造值 = minimum-1
 *             的复位态**，setValue 对越界值**忽略**——均对齐
 *             QProgressBar，qprogressbar.cpp:48/277-291）、
 *             setMinimum/setMaximum 单独设置端点（对标 QProgressBar
 *             同名槽）、外观翻转、百分比/数值文本格式串（%p 百分比、
 *             %v 当前值、%m 总步数 maximum-minimum；setFormat/
 *             resetFormat）、文本可见性与对齐，以及垂直文本方向
 *             （textDirection：TopToBottom/BottomToTop，垂直绘制时按
 *             方向旋转 90°）。绘制走 XPainter：凹陷凹槽（调色板 Dark/
 *             Light 描边 + Base 底）+ Highlight 进度块 + 按 alignment
 *             绘制的文本（缺省 XAlignment_Left，垂直居中由绘制路径统一
 *             保证），无裁剪能力构建时退化为单色描边。
 * @note       尺寸策略对齐 QProgressBarPrivate::init（qprogressbar.cpp:
 *             60-70）：QSizePolicy(Expanding, Fixed)，垂直转置；
 *             setOrientation 在未显式覆盖时转置策略。
 * @note       对标 QProgressBar::initStyleOption 的填充函数
 *             XProgressBar_initStyleOption 已提供（绘制路径共用）。
 * @note       模块总开关 XPROGRESSBAR_ON 定义于 XGuiConfig.h；
 *             XPROGRESSBAR_ON=0 时裁剪整个 XProgressBar 公共 API。
 *             XProgressBar 依赖 XWIDGET_ON、XPALETTE_ON（关闭时绘制
 *             退化为无操作）与 XPAINTER_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPROGRESSBAR_H
#define XPROGRESSBAR_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XString.h"
#include "XPainter.h"
#include "XAlignment.h"
#include "XStyleOption.h"

#if XWIDGET_ON && XPROGRESSBAR_ON

/* ==================== 方向（对标 Qt::Orientation 数值一致） ==================== */

/* [P1-09 2026-11-13] 方向类型统一到 XGuiOrientation（XGuiConfig.h，对标
 * Qt::Orientation；数值一致 1=水平/2=垂直）。与 P1-08 滑块族同批，
 * 避免同族残留两套方向类型。 */

/** @brief 进度条方向 = 公共类型 XGuiOrientation（对标 Qt::Orientation）。 */
typedef XGuiOrientation XGuiOrientation;
#define XGuiOrientation_Horizontal XGuiOrientation_Horizontal
#define XGuiOrientation_Vertical   XGuiOrientation_Vertical

/* ==================== 文本方向（对标 QProgressBar::Direction 数值一致） ==================== */

/** @brief 垂直进度条文本方向（数值与 QProgressBar::Direction 完全一致）。 */
typedef enum XProgressBarDirection
{
    XProgressBarDirection_TopToBottom = 0, /**< 文本自上而下排布（默认；垂直时顺时针旋转 90° 绘制）。 */
    XProgressBarDirection_BottomToTop = 1  /**< 文本自下而上排布（垂直时逆时针旋转 90° 绘制）。 */
} XProgressBarDirection;

/* ==================== 类定义 ==================== */
/* [P3-03] 追加虚槽 Text（对标 QProgressBar::text 的虚派发）：
 * 槽位编号接在 XWidget 全部槽位之后（XCLASS_VTABLE_GET_SIZE(XWidget)），
 * 保证既有槽位数值不变；XVTABLE_INIT_DEFAULT(XProgressBar) 的静态表容量
 * 由 XCLASS_DEFINE_END 生成的 _END_SIZE（= XWidget 槽数 + 1）给出，
 * 本类没有 ADD_FUNC_LIST，容量与槽位一一对应。 */
XCLASS_DEFINE_BEGING(XProgressBar)
XCLASS_DEFINE_ENUM(XProgressBar, Text) = XCLASS_VTABLE_GET_SIZE(XWidget),
XCLASS_DEFINE_END(XProgressBar)

/**
 * @brief      XProgressBar 进度条控件对象；m_base 必须是第一个成员
 *             （嵌 XWidget）。
 * @details    字段含义：
 *             - m_min/m_max：进度范围（默认 0/100，对标
 *               QProgressBar::minimum/maximum）；
 *             - m_value：当前进度（默认 **minimum-1 = -1** 的复位态，
 *               对标 qprogressbar.cpp:48 `value(-1)`；text() 返回空串）；
 *             - m_orientation：方向（默认水平，对标 orientation）；
 *             - m_invertedAppearance：外观翻转（默认 false，进度从
 *               另一端生长，对标 invertedAppearance）；
 *             - m_textVisible：是否绘制格式化文本（默认 true，对标
 *               textVisible）；
 *             - m_textDirection：垂直文本方向（默认 TopToBottom，对标
 *               QProgressBar::textDirection，仅垂直进度条生效）；
 *             - m_alignment：文本对齐（默认 Left，对标
 *               QProgressBarPrivate 构造的 alignment(Qt::AlignLeft)，
 *               qprogressbar.cpp:48）；
 *             - m_format：文本格式串（默认 "%p%"；%p 百分比、%v 当前
 *               值、%m 总步数 maximum-minimum，对标 setFormat）。
 *             - 复位态：value 落到 minimum 之下（reset 置 minimum-1，
 *               INT_MIN 下限时置 INT_MIN），text() 返回空串，对标 Qt
 *               的“未开始”哨兵。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XProgressBar
{
    XWidget m_base;                 /**< 基类成员；必须是第一个。 */
    XString* m_format;             /**< 字符串字段（对象拥有）。 */
    int     m_min;                  /**< 进度范围下限（数值域全 int，保留）。 */
    int     m_max;                  /**< 进度范围上限（数值域全 int，保留）。 */
    int     m_value;                /**< 当前进度值（数值域全 int，保留）。 */
    int     m_alignment;            /**< 文本对齐（XAlignment 组合；setter
                                       往返任意组合，保留 int）。 */
    /* [内存治理 2026-10-11] 标志位字：2 个 bool + 方向 1..2 + 文本方向
       0..1（XProgressBarDirection）合并 1 字。 */
    uint32_t m_invertedAppearance : 1; /**< 外观翻转。 */
    uint32_t m_textVisible : 1;     /**< 是否绘制文本。 */
    uint32_t m_orientation : 2;     /**< 方向（XGuiOrientation_Horizontal/Vertical）。 */
    uint32_t m_textDirection : 2;   /**< 垂直文本方向（XProgressBarDirection 0..1）。 */
    uint32_t m_progressBarFlagsUnused : 26; /**< 保留位。 */
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
 *             值：范围 0..100、值 **minimum-1 = -1**（复位态，对标
 *             QProgressBarPrivate 构造 `value(-1)`，qprogressbar.cpp:48）、
 *             水平方向、文本可见、格式 "%p%"、对齐 XAlignment_Left
 *             （对标 Qt 的 AlignLeft 缺省）。
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

/* 反初始化/释放统一走 XClassDeinit / XClassDelete（XProgressBar 已重载
 * EXClass_Deinit 释放 m_format，派生类不再另行声明入口）。 */

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
/* Qt 的 QProgressBar 公开 sizeHint()/minimumSizeHint()（qprogressbar.h:50-51）；
 * 本框架经 XWidget 同名接口取值。 */
#define XProgressBar_sizeHint(self) XWidget_sizeHint_base((const XWidget*)(self))
#define XProgressBar_minimumSizeHint(self) XWidget_minimumSizeHint_base((const XWidget*)(self))

/* 拷贝/移动统一使用 XClassCopy(dst, src) / XClassMove(dst, src)（XClass.h 定义，
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
 * @details    min > max 时 maximum 收敛为 min（qMax 语义）；范围变化时
 *             当前值落到新范围外（value < min-1 或 value > max）则走
 *             reset()（置 minimum-1、不发 valueChanged），否则仅重绘。
 *             setRange(0, 0) 进入不确定进度（busy）模式。
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
 * @details    复位态（reset 之后）返回 minimum-1，可用
 *             "value < minimum" 判定“尚未开始”，与 Qt 一致。
 * @param      self 进度条对象；可为 NULL。
 * @return     当前值；self 为 NULL 时返回 0。
 */
int XProgressBar_value(const XProgressBar* self);

/**
 * @brief      设置当前进度值（对标 QProgressBar::setValue）。
 * @details    与 Qt 6.8 qprogressbar.cpp:277-291 逐行同构：
 *             - 同值直接返回；
 *             - 非 busy（setRange(0,0) 之外）时越界值（> max 或 < min）
 *               **直接忽略、不改变当前值、不发信号**（**不钳位**）；
 *             - busy 模式（min==max==0）接受任意值；
 *             - 值实际变化时发射 valueChanged(int) 并重绘。
 * @param      self  进度条对象；可为 NULL。
 * @param      value 新进度值。
 * @return     无返回值。
 */
void XProgressBar_setValue(XProgressBar* self, int value);

/**
 * @brief      重置进度（对标 QProgressBar::reset）。
 * @details    值置为 minimum-1（minimum==INT_MIN 时置 INT_MIN），表示
 *             “未开始”的复位态——text() 据此返回空串，value() 可经
 *             "value < minimum" 判定复位；不发射 valueChanged。
 * @param      self 进度条对象；可为 NULL。
 * @return     无返回值。
 */
void XProgressBar_reset(XProgressBar* self);

/* ==================== 外观属性 ==================== */

/**
 * @brief      查询方向（对标 QProgressBar::orientation）。
 * @param      self 进度条对象；可为 NULL。
 * @return     XGuiOrientation（XGuiOrientation_Horizontal/Vertical）；self 为 NULL
 *             时返回水平。
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
 * @brief      查询文本对齐（XAlignment 组合；默认 XAlignment_Left）。
 * @param      self 进度条对象；可为 NULL。
 * @return     当前对齐组合；self 为 NULL 时返回 XAlignment_Left（与构造
 *             缺省一致，对标 qprogressbar.cpp:48）。
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
 * @details    返回内部 XString 的 UTF-8 借用指针，**原样**返回存储的
 *             格式串：`setFormat("")` 之后返回空串（Qt 的 format() 同样
 *             原样返回空 QString，qprogressbar.cpp:589-593）；只有
 *             未建立缓冲（防御分支）才回落 "%p%"。
 * @param      self 进度条对象；可为 NULL。
 * @return     格式串借用指针（"%p" 百分比 / "%v" 值 / "%m" 总步数
 *             maximum-minimum；空串=空文本模板）；self 为 NULL 时返回
 *             默认 "%p%"。
 */
const char* XProgressBar_format(const XProgressBar* self);

/**
 * @brief      设置文本格式串并重绘（对标 setFormat）。
 * @details    格式串由内部 XString 全量保存，**无长度上限**（与 Qt 的
 *             QString 同）；同值调用早退（qprogressbar.cpp:565-573）。
 *             **NULL 与空串语义不同**（2026-11-11 P0-66 对齐 Qt）：
 *             - `format == NULL`：C 侧便利语义「恢复缺省」，等价
 *               resetFormat()，格式串变回 "%p%"（Qt 无 NULL 形态，
 *               该约定见保留表 K-24）；
 *             - `format == ""`：**空文本模板**——text() 返回空串（Qt
 *               `setFormat("")` 后的 text() 就是空串，X 旧实现把空串
 *               静默当 "%p%" ⇒ 空格式不可表达）。
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
 * @details    对标 QProgressBar::text：复位态（value < minimum）与
 *             不确定进度（setRange(0,0)）返回空串；空格式串
 *             （setFormat("")）同样返回空串（P0-66）。
 *             **多态入口**（P3-03）：本函数就是 Qt 的虚 `text()`，
 *             经 `XProgressBar_text_base` 派发到 EXProgressBar_Text 槽，
 *             默认实现为内部的 `VXProgressBar_text`。子类要像 Qt 那样
 *             覆写文本生成，可在自己的 class_init 里
 *             `XVTABLE_OVERLOAD_DEFAULT(EXProgressBar_Text, MyText)`
 *             （签名 `void (*)(const XProgressBar*, char*, int)`）。
 * @param      self    进度条对象；可为 NULL。
 * @param      out     输出缓冲；不可为 NULL。
 * @param      outSize 输出缓冲大小（含 NUL）；不足时截断。
 * @return     无返回值。
 */
void XProgressBar_text(const XProgressBar* self, char* out, int outSize);

/**
 * @brief      text 虚槽的多态调度入口（对标 QProgressBar::text 的虚派发，
 *             P3-03）。
 * @details    `XProgressBar_text` 与绘制路径都经本函数，子类覆写
 *             EXProgressBar_Text 后对两者同时生效。
 * @param      self    进度条对象；可为 NULL（无操作）。
 * @param      out     输出缓冲；可为 NULL（无操作）。
 * @param      outSize 输出缓冲大小（含 NUL）。
 * @return     无返回值。
 */
void XProgressBar_text_base(const XProgressBar* self, char* out,
                            int outSize);

#if XSTYLE_ON
/**
 * @brief      填充进度条样式选项（对标 QProgressBar::initStyleOption，
 *             qprogressbar.cpp:78-100）。
 * @details    字段与 Qt 对应：state/rect 取自控件（水平补
 *             State_Horizontal）、m_progressMin/Max/Value、
 *             m_progressTextVisible、m_progressInverted、对齐、文本、
 *             方向。绘制路径共用本函数；样式/子类可按 Qt 契约取完整
 *             选项（X 无 C++ protected virtual，沿用
 *             XFrame_initStyleOption/XLineEdit_initStyleOption 先例）。
 * @param      self   进度条对象；可为 NULL（无操作）。
 * @param      option 输出选项（调用方栈上分配）；NULL 无操作。
 * @return     无返回值。
 */
void XProgressBar_initStyleOption(const XProgressBar* self,
                                  XStyleOption* option);
#endif /* XSTYLE_ON */

/* ==================== 信号 ==================== */

/**
 * @brief      valueChanged(int) 信号标识（对标 QProgressBar::
 *             valueChanged）。
 * @note       只返回标识、**不发射**（与全库其余 `*_signal` 同契约）；
 *             连接用 `XObject_connect_2(obj, XSignal(名), slot)`。
 * @param      self 进度条对象；可为 NULL。
 * @return     不透明信号标识；返回值不指向可释放对象，也不得解引用。
 */
void* XProgressBar_valueChanged_signal(XProgressBar* self, int value);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XPROGRESSBAR_ON */

#endif /* XGUI_ON */

#endif /* XPROGRESSBAR_H */
