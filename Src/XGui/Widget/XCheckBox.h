/******************************************************************************
 * @file       XCheckBox.h
 * @brief      XCheckBox 复选框控件（对标 Qt 6.8 QCheckBox，继承 XAbstractButton）。
 * @details    XCheckBox 继承 XAbstractButton（对齐 Qt 的
 *             QCheckBox : QAbstractButton 继承关系）：
 *             - 文本/图标/选中/按下/自动重复/自动互斥/click/animateClick
 *               与 pressed/released/clicked/toggled 信号全部继承
 *               XAbstractButton，本头文件以宏别名保持 XCheckBox_* 名称；
 *             - 本类只实现 QCheckBox 特有部分：三态（tristate）、
 *               checkState()/setCheckState() 与 checkStateChanged 信号、
 *               indicator 方块+勾/横线的绘制与命中、sizeHint/minimumSizeHint；
 *             - 状态模型对标 Qt 6.8 QCheckBoxPrivate：以基类 m_checked 为
 *               真值源，PartiallyChecked 通过 tristate+noChange 标志表达，
 *               checkState() 为派生计算；重载 nextCheckState（三态
 *               (state+1)%3 循环，非三态交基类反转）与 checkStateSet
 *               （基类 setChecked 后同步并发出 checkStateChanged），
 *               setCheckState 期间用 m_blockRefresh 抑制基类路径重复发信号；
 *             - 构造时设置 checkable=true（对标 Qt 6.8 QCheckBox init，
 *               qcheckbox.cpp:117）。
 *             嵌入式裁剪由 XGuiConfig.h 的 XCHECKBOX_ON 控制，且依赖
 *             XABSTRACTBUTTON_ON（基类裁剪时本类一并裁剪）；绘制使用
 *             XPainter，不依赖任何平台 API。
 * @note       近似边界：QCheckBox 的快捷键、样式表/主题 bevel 未实现；
 *             hitButton 按 indicator 矩形命中（对标 Qt
 *             SE_CheckBoxClickRect，无主题时即 indicator 矩形）；m_mouseTracking
 *             悬停效果未实现；Qt 6.9 起废弃的 stateChanged(int) 信号不再
 *             提供，只对齐 Qt 6.7+ 的 checkStateChanged(Qt::CheckState)。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCHECKBOX_H
#define XCHECKBOX_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractButton.h"
#include "XPainter.h"

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON

/* ==================== 虚函数表（继承 XAbstractButton 派生槽位） ==================== */

/**
 * @brief XCheckBox 虚函数表枚举。
 * @details 槽位数量与 XAbstractButton 完全一致；XCheckBox 不新增槽位，
 *          仅重载 XAbstractButton 的 CheckStateSet/NextCheckState/HitButton/
 *          ContentChanged 保护槽与 XWidget 的 PaintEvent，以及 XClass 的
 *          Copy/Move。
 */
XCLASS_DEFINE_BEGING(XCheckBox)
XCLASS_DEFINE_EXTEND_END(XCheckBox, XAbstractButton)

/* ==================== 选中状态枚举（对标 Qt::CheckState 数值） ==================== */

/** @brief 复选框选中状态（数值与 Qt 6.8 Qt::CheckState 一致）。 */
typedef enum XCheckState
{
    XCheckState_Unchecked = 0,        /**< 未选中。 */
    XCheckState_PartiallyChecked = 1, /**< 部分选中（三态中间态）。 */
    XCheckState_Checked = 2           /**< 选中。 */
} XCheckState;

/* ==================== 控件对象（对标 QCheckBox : QAbstractButton） ==================== */

/**
 * @brief XCheckBox 复选框控件对象。
 * @details 首成员 m_base 必须是 XAbstractButton（继承）；文本/图标/状态位
 *          位于基类，本类只保留三态相关标志。状态模型：m_checked（基类）
 *          是真值源，m_tristate 开启三态，m_noChange 表达 PartiallyChecked，
 *          m_publishedState 用于 checkStateChanged 信号去重，
 *          m_blockRefresh 在 setCheckState 期间抑制基类路径重复发信号。
 */
typedef struct XCheckBox
{
    XAbstractButton m_base;           /**< XAbstractButton 基类成员；必须是第一个。 */
    bool            m_tristate;       /**< 是否允许三态（部分选中）。 */
    bool            m_noChange;       /**< 三态中间态标志（对标 QCheckBoxPrivate::noChange）。 */
    bool            m_blockRefresh;   /**< setCheckState 期间抑制 checkStateSet 发信号。 */
    XCheckState     m_publishedState; /**< 已发布的选中状态（信号去重）。 */
} XCheckBox;

/* ==================== 生命周期（对标 QCheckBox 构造/析构） ==================== */

/** @brief XCheckBox 类虚函数表初始化（重载 CheckStateSet/NextCheckState/HitButton/ContentChanged/PaintEvent/Copy/Move，并登记为 XAbstractButton 派生类）。 */
XVtable* XCheckBox_class_init(void);

/**
 * @brief      初始化 XCheckBox（对标 QCheckBox(parent) 构造）。
 * @details    先初始化 XAbstractButton 基类，再挂 XCheckBox 虚表并设置
 *             QCheckBox 默认值：checkable=true、tristate=false、
 *             noChange=false、publishedState=Unchecked。
 * @param      self   待初始化对象；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志（可传 0 表示 Widget 类型）。
 */
void XCheckBox_init(XCheckBox* self, XWidget* parent, XWidgetFlags flags);

/** @brief 使用默认内存类型创建复选框控件（语义同 XWidget_create）。 */
#define XCheckBox_create(parent, flags) XCheckBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建复选框控件。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志。
 * @return     新对象指针；失败返回 NULL。
 */
XCheckBox* XCheckBox_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags);

/** @brief 通过 XClass 虚表释放 XCheckBox 资源（栈/外部存储对象使用）。 */
#define XCheckBox_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XCheckBox 对象。 */
#define XCheckBox_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 继承 XAbstractButton 的公共 API（宏别名保持原名称） ==================== */

/** @brief 查询按钮文本（对标 QAbstractButton::text）。 */
#define XCheckBox_text(self)  XAbstractButton_text((const XAbstractButton*)(self))
/** @brief 设置按钮文本（对标 QAbstractButton::setText）。 */
#define XCheckBox_setText(self, text)  XAbstractButton_setText((XAbstractButton*)(self), (text))
/** @brief 设置按钮文本（UTF-8 C 字符串便利版本）。 */
#define XCheckBox_setText_2(self, utf8)  XAbstractButton_setText_2((XAbstractButton*)(self), (utf8))

/** @brief 返回按钮图标（对标 QAbstractButton::icon）。 */
#define XCheckBox_icon(self)  XAbstractButton_icon((const XAbstractButton*)(self))
/** @brief 设置按钮图标（对标 QAbstractButton::setIcon）。 */
#define XCheckBox_setIcon(self, icon)  XAbstractButton_setIcon((XAbstractButton*)(self), (icon))
/** @brief 查询按钮图标渲染尺寸（对标 QAbstractButton::iconSize）。 */
#define XCheckBox_iconSize(self)  XAbstractButton_iconSize((const XAbstractButton*)(self))
/** @brief 设置按钮图标渲染尺寸（对标 QAbstractButton::setIconSize）。 */
#define XCheckBox_setIconSize(self, size)  XAbstractButton_setIconSize((XAbstractButton*)(self), (size))

/** @brief 查询是否可选中（对标 QAbstractButton::isCheckable；XCheckBox 默认 true）。 */
#define XCheckBox_isCheckable(self)  XAbstractButton_isCheckable((const XAbstractButton*)(self))
/** @brief 设置是否可选中（对标 QAbstractButton::setCheckable）。 */
#define XCheckBox_setCheckable(self, checkable)  XAbstractButton_setCheckable((XAbstractButton*)(self), (checkable))
/** @brief 查询是否选中（对标 QAbstractButton::isChecked；三态下 Checked 才为 true）。 */
#define XCheckBox_isChecked(self)  XAbstractButton_isChecked((const XAbstractButton*)(self))
/** @brief 设置是否选中（对标 QAbstractButton::setChecked；会经 checkStateSet 槽同步三态）。 */
#define XCheckBox_setChecked(self, checked)  XAbstractButton_setChecked((XAbstractButton*)(self), (checked))
/** @brief 切换选中状态（对标 QAbstractButton::toggle；经 nextCheckState 槽，三态时循环）。 */
#define XCheckBox_toggle(self)  XAbstractButton_toggle((XAbstractButton*)(self))

/** @brief 查询按下面板状态（对标 QAbstractButton::isDown）。 */
#define XCheckBox_isDown(self)  XAbstractButton_isDown((const XAbstractButton*)(self))
/** @brief 设置按下面板状态（对标 QAbstractButton::setDown）。 */
#define XCheckBox_setDown(self, down)  XAbstractButton_setDown((XAbstractButton*)(self), (down))

/** @brief 查询是否允许按住自动重复（对标 QAbstractButton::autoRepeat）。 */
#define XCheckBox_autoRepeat(self)  XAbstractButton_autoRepeat((const XAbstractButton*)(self))
/** @brief 设置是否允许按住自动重复（对标 QAbstractButton::setAutoRepeat）。 */
#define XCheckBox_setAutoRepeat(self, repeat)  XAbstractButton_setAutoRepeat((XAbstractButton*)(self), (repeat))
/** @brief 查询自动重复开始延迟（毫秒）。 */
#define XCheckBox_autoRepeatDelay(self)  XAbstractButton_autoRepeatDelay((const XAbstractButton*)(self))
/** @brief 设置自动重复开始延迟（毫秒）。 */
#define XCheckBox_setAutoRepeatDelay(self, delay)  XAbstractButton_setAutoRepeatDelay((XAbstractButton*)(self), (delay))
/** @brief 查询自动重复间隔（毫秒）。 */
#define XCheckBox_autoRepeatInterval(self)  XAbstractButton_autoRepeatInterval((const XAbstractButton*)(self))
/** @brief 设置自动重复间隔（毫秒）。 */
#define XCheckBox_setAutoRepeatInterval(self, interval)  XAbstractButton_setAutoRepeatInterval((XAbstractButton*)(self), (interval))
/** @brief 查询自动互斥标志（对标 QAbstractButton::autoExclusive）。 */
#define XCheckBox_autoExclusive(self)  XAbstractButton_autoExclusive((const XAbstractButton*)(self))
/** @brief 设置自动互斥标志（对标 QAbstractButton::setAutoExclusive）。 */
#define XCheckBox_setAutoExclusive(self, exclusive)  XAbstractButton_setAutoExclusive((XAbstractButton*)(self), (exclusive))

/** @brief 程序化点击按钮（对标 QAbstractButton::click）。 */
#define XCheckBox_click(self)  XAbstractButton_click((XAbstractButton*)(self))
/** @brief 动画点击（对标 QAbstractButton::animateClick）。 */
#define XCheckBox_animateClick(self)  XAbstractButton_animateClick((XAbstractButton*)(self))

/* ==================== 信号（继承 XAbstractButton，宏别名保持原名称） ==================== */

/** @brief 按下信号（对标 QAbstractButton::pressed）。 */
#define XCheckBox_pressed_signal(self)  XAbstractButton_pressed_signal((XAbstractButton*)(self))
/** @brief 释放信号（对标 QAbstractButton::released）。 */
#define XCheckBox_released_signal(self)  XAbstractButton_released_signal((XAbstractButton*)(self))
/** @brief 点击信号（对标 QAbstractButton::clicked(bool)）。 */
#define XCheckBox_clicked_signal(self, checked)  XAbstractButton_clicked_signal((XAbstractButton*)(self), (checked))
/** @brief 选中变化信号（对标 QAbstractButton::toggled(bool)）。 */
#define XCheckBox_toggled_signal(self, checked)  XAbstractButton_toggled_signal((XAbstractButton*)(self), (checked))

/* ==================== 命中（QAbstractButton protected，经虚表分派） ==================== */

/**
 * @brief      判断局部坐标是否命中复选框（对标 QCheckBox::hitButton）。
 * @details    Qt 的命中基准是 SE_CheckBoxClickRect（indicator 矩形）；本实现
 *             经 XAbstractButton 保护虚表分派，XCheckBox 重载为 indicator
 *             矩形包含（无主题时按 indicator 矩形近似）。
 * @param      self 复选框对象。
 * @param      pos 控件局部坐标；NULL 返回 false。
 * @return     命中返回 true。
 */
bool XCheckBox_hitButton(const XCheckBox* self, const XPoint* pos);

/* ==================== 三态（对标 QCheckBox tristate/checkState） ==================== */

/**
 * @brief      设置是否允许三态（对标 QCheckBox::setTristate）。
 * @param      self 待修改的复选框对象；可为 NULL。
 * @param      tristate true 开启三态（允许 PartiallyChecked），false 关闭。
 * @return     无返回值。
 */
void XCheckBox_setTristate(XCheckBox* self, bool tristate);
/** @brief 查询是否允许三态（对标 QCheckBox::isTristate）。 */
bool XCheckBox_isTristate(const XCheckBox* self);
/**
 * @brief      查询当前选中状态（对标 QCheckBox::checkState）。
 * @details    三态且 noChange 时返回 PartiallyChecked；否则按基类 checked
 *             返回 Checked/Unchecked。
 * @param      self 复选框对象；可为 NULL。
 * @return     当前选中状态；self 为 NULL 返回 Unchecked。
 */
XCheckState XCheckBox_checkState(const XCheckBox* self);
/**
 * @brief      设置选中状态（对标 QCheckBox::setCheckState）。
 * @details    传 PartiallyChecked 时自动开启 tristate 并置 noChange；其余
 *             状态清除 noChange。内部经基类 setChecked 写入真值，过程中用
 *             m_blockRefresh 抑制 checkStateSet 槽重复发信号，最后按
 *             publishedState 去重发出 checkStateChanged。
 * @param      self 待修改的复选框对象；可为 NULL。
 * @param      state 目标选中状态。
 * @return     无返回值。
 */
void XCheckBox_setCheckState(XCheckBox* self, XCheckState state);

/**
 * @brief      发射选中状态变化信号（对标 Qt 6.8 QCheckBox::checkStateChanged）。
 * @details    self 非 NULL 时同步通知已连接槽；self 为 NULL 时只返回信号
 *             标识，便于连接信号与槽，不发射任何通知。
 * @param      self 发射信号的复选框对象；可为 NULL。
 * @param      state 新的选中状态。
 * @return     不透明的 checkStateChanged 信号标识。
 */
void* XCheckBox_checkStateChanged_signal(XCheckBox* self, XCheckState state);

/* ==================== 尺寸（对标 QCheckBox sizeHint/minimumSizeHint） ==================== */

/**
 * @brief      查询复选框建议尺寸（对标 QCheckBox::sizeHint）。
 * @details    按 QCommonStyle 基础数值计算：indicator 13x13 加 6 像素
 *             标签间距，再加文本宽度与左右边距；有图标时图标参与宽度。
 * @param      self 复选框对象；NULL 返回无效尺寸 (-1,-1)。
 * @return     建议尺寸。
 */
XSize XCheckBox_sizeHint(const XCheckBox* self);
/** @brief 查询最小建议尺寸（对标 QCheckBox::minimumSizeHint；Qt 中直接返回 sizeHint()）。 */
XSize XCheckBox_minimumSizeHint(const XCheckBox* self);

/* ==================== 离屏绘制入口（对标 QCheckBox::paintEvent 内容） ==================== */

/**
 * @brief      把复选框内容绘制到给定绘制器。
 * @details    与重绘事件中的绘制路径一致：填充 Window 背景色、绘制
 *             indicator 方块边框（enabled/disabled 分组色），选中画勾、
 *             三态中间态画横线，再绘制文本与图标。供离屏渲染与回归测试
 *             直接调用；不设置任何平台资源。
 * @param      self 复选框对象；NULL 无操作。
 * @param      painter 目标绘制器；NULL 无操作。
 */
void XCheckBox_drawContents(XCheckBox* self, XPainter* painter);

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XCHECKBOX_ON */

#ifdef __cplusplus
}
#endif
#endif /* XCHECKBOX_H */
