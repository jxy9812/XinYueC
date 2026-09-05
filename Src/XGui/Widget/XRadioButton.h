/******************************************************************************
 * @file       XRadioButton.h
 * @brief      XRadioButton 单选按钮控件（对标 Qt 6.8 QRadioButton，继承 XAbstractButton）。
 * @details    XRadioButton 继承 XAbstractButton（对齐 Qt 的
 *             QRadioButton : QAbstractButton 继承关系）：
 *             - 文本/图标/选中/按下/自动重复/自动互斥/click/animateClick
 *               与 pressed/released/clicked/toggled 信号全部继承
 *               XAbstractButton，本头文件以宏别名保持 XRadioButton_* 名称；
 *             - 本类只实现 QRadioButton 特有部分：构造时默认
 *               checkable=true 且 autoExclusive=true（对标 Qt 6.8
 *               QRadioButtonPrivate::init，qradiobutton.cpp:35-36），
 *               圆形 indicator 绘制与命中、sizeHint/minimumSizeHint；
 *             - 互斥联动：同一父控件下的 XRadioButton 经基类 autoExclusive
 *               机制自动互斥（XAbstractButton_isInstance 派生登记表识别
 *               全部按钮派生类），无需额外实现；
 *             - 构造时前景角色设为 WindowText（对标 Qt 6.8
 *               qradiobutton.cpp:38 setForegroundRole(QPalette::WindowText)）。
 *             嵌入式裁剪由 XGuiConfig.h 的 XRADIOBUTTON_ON 控制，且依赖
 *             XABSTRACTBUTTON_ON（基类裁剪时本类一并裁剪）；绘制使用
 *             XPainter，不依赖任何平台 API。
 * @note       近似边界：快捷键、样式表 bevel、悬停效果未实现；hitButton
 *             按 indicator 外接矩形命中（对标 Qt SE_RadioButtonClickRect，
 *             无主题时近似 indicator 矩形）；显式 QButtonGroup 登记仍是
 *             裁剪项，跨父控件互斥需借助自动互斥组或后续 QButtonGroup。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XRADIOBUTTON_H
#define XRADIOBUTTON_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractButton.h"
#include "XPainter.h"

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON

/* ==================== 虚函数表（继承 XAbstractButton 派生槽位） ==================== */

/**
 * @brief XRadioButton 虚函数表枚举。
 * @details 槽位数量与 XAbstractButton 完全一致；XRadioButton 不新增槽位，
 *          仅重载 XAbstractButton 的 HitButton/ContentChanged 保护槽与
 *          XWidget 的 PaintEvent。
 */
XCLASS_DEFINE_BEGING(XRadioButton)
XCLASS_DEFINE_EXTEND_END(XRadioButton, XAbstractButton)

/* ==================== 控件对象（对标 QRadioButton : QAbstractButton） ==================== */

/**
 * @brief XRadioButton 单选按钮控件对象。
 * @details 首成员 m_base 必须是 XAbstractButton（继承）；文本/图标/状态位
 *          全部位于基类，本类不新增字段。互斥语义由基类 autoExclusive 提供。
 */
typedef struct XRadioButton
{
    XAbstractButton m_base; /**< XAbstractButton 基类成员；必须是第一个。 */
} XRadioButton;

/* ==================== 生命周期（对标 QRadioButton 构造/析构） ==================== */

/** @brief XRadioButton 类虚函数表初始化（重载 HitButton/ContentChanged/PaintEvent，并登记为 XAbstractButton 派生类）。 */
XVtable* XRadioButton_class_init(void);

/**
 * @brief      初始化 XRadioButton（对标 QRadioButton(parent) 构造）。
 * @details    先初始化 XAbstractButton 基类，再挂 XRadioButton 虚表并设置
 *             QRadioButton 默认值：checkable=true、autoExclusive=true、
 *             前景角色 WindowText。
 * @param      self   待初始化对象；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志（可传 0 表示 Widget 类型）。
 */
void XRadioButton_init(XRadioButton* self, XWidget* parent,
                       XWidgetFlags flags);

/** @brief 使用默认内存类型创建单选按钮控件（语义同 XWidget_create）。 */
#define XRadioButton_create(parent, flags) XRadioButton_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建单选按钮控件。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志。
 * @return     新对象指针；失败返回 NULL。
 */
XRadioButton* XRadioButton_create_ex(XMemoryType memory, XWidget* parent,
                                     XWidgetFlags flags);

/** @brief 通过 XClass 虚表释放 XRadioButton 资源（栈/外部存储对象使用）。 */
#define XRadioButton_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XRadioButton 对象。 */
#define XRadioButton_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 继承 XAbstractButton 的公共 API（宏别名保持原名称） ==================== */

/** @brief 查询按钮文本（对标 QAbstractButton::text）。 */
#define XRadioButton_text(self)  XAbstractButton_text((const XAbstractButton*)(self))
/** @brief 设置按钮文本（对标 QAbstractButton::setText）。 */
#define XRadioButton_setText(self, text)  XAbstractButton_setText((XAbstractButton*)(self), (text))
/** @brief 设置按钮文本（UTF-8 C 字符串便利版本）。 */
#define XRadioButton_setText_2(self, utf8)  XAbstractButton_setText_2((XAbstractButton*)(self), (utf8))

/** @brief 返回按钮图标（对标 QAbstractButton::icon）。 */
#define XRadioButton_icon(self)  XAbstractButton_icon((const XAbstractButton*)(self))
/** @brief 设置按钮图标（对标 QAbstractButton::setIcon）。 */
#define XRadioButton_setIcon(self, icon)  XAbstractButton_setIcon((XAbstractButton*)(self), (icon))
/** @brief 查询按钮图标渲染尺寸（对标 QAbstractButton::iconSize）。 */
#define XRadioButton_iconSize(self)  XAbstractButton_iconSize((const XAbstractButton*)(self))
/** @brief 设置按钮图标渲染尺寸（对标 QAbstractButton::setIconSize）。 */
#define XRadioButton_setIconSize(self, size)  XAbstractButton_setIconSize((XAbstractButton*)(self), (size))

/** @brief 查询是否可选中（对标 QAbstractButton::isCheckable；XRadioButton 默认 true）。 */
#define XRadioButton_isCheckable(self)  XAbstractButton_isCheckable((const XAbstractButton*)(self))
/** @brief 设置是否可选中（对标 QAbstractButton::setCheckable）。 */
#define XRadioButton_setCheckable(self, checkable)  XAbstractButton_setCheckable((XAbstractButton*)(self), (checkable))
/** @brief 查询是否选中（对标 QAbstractButton::isChecked）。 */
#define XRadioButton_isChecked(self)  XAbstractButton_isChecked((const XAbstractButton*)(self))
/** @brief 设置是否选中（对标 QAbstractButton::setChecked）。 */
#define XRadioButton_setChecked(self, checked)  XAbstractButton_setChecked((XAbstractButton*)(self), (checked))
/** @brief 切换选中状态（对标 QAbstractButton::toggle）。 */
#define XRadioButton_toggle(self)  XAbstractButton_toggle((XAbstractButton*)(self))

/** @brief 查询按下面板状态（对标 QAbstractButton::isDown）。 */
#define XRadioButton_isDown(self)  XAbstractButton_isDown((const XAbstractButton*)(self))
/** @brief 设置按下面板状态（对标 QAbstractButton::setDown）。 */
#define XRadioButton_setDown(self, down)  XAbstractButton_setDown((XAbstractButton*)(self), (down))

/** @brief 查询是否允许按住自动重复（对标 QAbstractButton::autoRepeat）。 */
#define XRadioButton_autoRepeat(self)  XAbstractButton_autoRepeat((const XAbstractButton*)(self))
/** @brief 设置是否允许按住自动重复（对标 QAbstractButton::setAutoRepeat）。 */
#define XRadioButton_setAutoRepeat(self, repeat)  XAbstractButton_setAutoRepeat((XAbstractButton*)(self), (repeat))
/** @brief 查询自动重复开始延迟（毫秒）。 */
#define XRadioButton_autoRepeatDelay(self)  XAbstractButton_autoRepeatDelay((const XAbstractButton*)(self))
/** @brief 设置自动重复开始延迟（毫秒）。 */
#define XRadioButton_setAutoRepeatDelay(self, delay)  XAbstractButton_setAutoRepeatDelay((XAbstractButton*)(self), (delay))
/** @brief 查询自动重复间隔（毫秒）。 */
#define XRadioButton_autoRepeatInterval(self)  XAbstractButton_autoRepeatInterval((const XAbstractButton*)(self))
/** @brief 设置自动重复间隔（毫秒）。 */
#define XRadioButton_setAutoRepeatInterval(self, interval)  XAbstractButton_setAutoRepeatInterval((XAbstractButton*)(self), (interval))
/** @brief 查询自动互斥标志（对标 QAbstractButton::autoExclusive；XRadioButton 默认 true）。 */
#define XRadioButton_autoExclusive(self)  XAbstractButton_autoExclusive((const XAbstractButton*)(self))
/** @brief 设置自动互斥标志（对标 QAbstractButton::setAutoExclusive）。 */
#define XRadioButton_setAutoExclusive(self, exclusive)  XAbstractButton_setAutoExclusive((XAbstractButton*)(self), (exclusive))

/** @brief 程序化点击按钮（对标 QAbstractButton::click）。 */
#define XRadioButton_click(self)  XAbstractButton_click((XAbstractButton*)(self))
/** @brief 动画点击（对标 QAbstractButton::animateClick）。 */
#define XRadioButton_animateClick(self)  XAbstractButton_animateClick((XAbstractButton*)(self))

/* ==================== 信号（继承 XAbstractButton，宏别名保持原名称） ==================== */

/** @brief 按下信号（对标 QAbstractButton::pressed）。 */
#define XRadioButton_pressed_signal(self)  XAbstractButton_pressed_signal((XAbstractButton*)(self))
/** @brief 释放信号（对标 QAbstractButton::released）。 */
#define XRadioButton_released_signal(self)  XAbstractButton_released_signal((XAbstractButton*)(self))
/** @brief 点击信号（对标 QAbstractButton::clicked(bool)）。 */
#define XRadioButton_clicked_signal(self, checked)  XAbstractButton_clicked_signal((XAbstractButton*)(self), (checked))
/** @brief 选中变化信号（对标 QAbstractButton::toggled(bool)）。 */
#define XRadioButton_toggled_signal(self, checked)  XAbstractButton_toggled_signal((XAbstractButton*)(self), (checked))

/* ==================== 命中（QAbstractButton protected，经虚表分派） ==================== */

/**
 * @brief      判断局部坐标是否命中单选按钮（对标 QRadioButton::hitButton）。
 * @details    Qt 的命中基准是 SE_RadioButtonClickRect（indicator 区域）；本实现
 *             经 XAbstractButton 保护虚表分派，XRadioButton 重载为 indicator
 *             外接矩形包含（无主题时近似）。
 * @param      self 单选按钮对象。
 * @param      pos 控件局部坐标；NULL 返回 false。
 * @return     命中返回 true。
 */
bool XRadioButton_hitButton(const XRadioButton* self, const XPoint* pos);

/* ==================== 尺寸（对标 QRadioButton sizeHint/minimumSizeHint） ==================== */

/**
 * @brief      查询单选按钮建议尺寸（对标 QRadioButton::sizeHint）。
 * @details    按 QCommonStyle 基础数值计算：indicator 13x13 加 6 像素
 *             标签间距，再加文本宽度与左右边距。
 * @param      self 单选按钮对象；NULL 返回无效尺寸 (-1,-1)。
 * @return     建议尺寸。
 */
XSize XRadioButton_sizeHint(const XRadioButton* self);
/** @brief 查询最小建议尺寸（对标 QRadioButton::minimumSizeHint；Qt 中直接返回 sizeHint()）。 */
XSize XRadioButton_minimumSizeHint(const XRadioButton* self);

/* ==================== 离屏绘制入口（对标 QRadioButton::paintEvent 内容） ==================== */

/**
 * @brief      把单选按钮内容绘制到给定绘制器。
 * @details    与重绘事件中的绘制路径一致：填充 Window 背景色、绘制圆形
 *             indicator 边框，选中时中心画实心圆点，再绘制文本与图标。
 *             供离屏渲染与回归测试直接调用；不设置任何平台资源。
 * @param      self 单选按钮对象；NULL 无操作。
 * @param      painter 目标绘制器；NULL 无操作。
 */
void XRadioButton_drawContents(XRadioButton* self, XPainter* painter);

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XRADIOBUTTON_ON */

#ifdef __cplusplus
}
#endif
#endif /* XRADIOBUTTON_H */
