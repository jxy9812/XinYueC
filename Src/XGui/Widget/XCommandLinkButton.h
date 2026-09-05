/******************************************************************************
 * @file       XCommandLinkButton.h
 * @brief      XCommandLinkButton 命令链接按钮（对标 Qt 6.8 QCommandLinkButton，继承 XPushButton）。
 * @details    XCommandLinkButton 继承 XPushButton（对齐 Qt 的
 *             QCommandLinkButton : QPushButton 继承关系）：
 *             - QAbstractButton/QPushButton 的文本/图标/选中/按下/自动重复/
 *               自动互斥/click/animateClick/信号，以及 autoDefault/default/
 *               flat/menu/sizeHint 等全部继承 XPushButton，本头文件以宏别名
 *               保持 XCommandLinkButton_* 名称；
 *             - 本类只实现 QCommandLinkButton 特有部分：description 描述文本
 *               属性、标题+描述双行绘制、左侧大图标（构造默认 iconSize
 *               20x20，对标 Qt 6.8 qcommandlinkbutton.cpp:189）与右侧箭头、
 *               尺寸策略 Preferred/Preferred（对标 qcommandlinkbutton.cpp:185）；
 *             - 继承 XPushButton 的 ContentChanged 机制：文本/图标/描述变化
 *               后刷新自身 sizeHint。
 *             嵌入式裁剪由 XGuiConfig.h 的 XCOMMANDLINKBUTTON_ON 控制，且
 *             依赖 XPUSHBUTTON_ON（父类裁剪时本类一并裁剪）；绘制使用
 *             XPainter，不依赖任何平台 API。
 * @note       近似边界：QCommandLinkButton 的 heightForWidth（描述按宽度
 *             换行计算高度）未实现，本类按固定双行高度返回 sizeHint；
 *             SP_CommandLink 标准图标未接入（无图标时只绘制文本）；
 *             快捷键与样式 bevel 未实现。详细说明见 XGui.md。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XCOMMANDLINKBUTTON_H
#define XCOMMANDLINKBUTTON_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XPushButton.h"
#include "XPainter.h"
#include "XString.h"

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON

/* ==================== 虚函数表（继承 XPushButton 派生槽位） ==================== */

/**
 * @brief XCommandLinkButton 虚函数表枚举。
 * @details 槽位数量与 XPushButton 完全一致；XCommandLinkButton 不新增槽位，
 *          仅重载 XAbstractButton 的 ContentChanged 保护槽、XWidget 的
 *          PaintEvent 与 XClass 的 Copy/Move/Deinit。
 */
XCLASS_DEFINE_BEGING(XCommandLinkButton)
XCLASS_DEFINE_EXTEND_END(XCommandLinkButton, XPushButton)

/* ==================== 控件对象（对标 QCommandLinkButton : QPushButton） ==================== */

/**
 * @brief XCommandLinkButton 命令链接按钮控件对象。
 * @details 首成员 m_base 必须是 XPushButton（继承）；QAbstractButton/
 *          QPushButton 字段全部位于基类，本类只新增拥有描述文本 m_description。
 */
typedef struct XCommandLinkButton
{
    XPushButton m_base;       /**< XPushButton 基类成员；必须是第一个。 */
    XString*    m_description; /**< 描述文本（拥有，按 XString UTF-16 保存）。 */
} XCommandLinkButton;

/* ==================== 生命周期（对标 QCommandLinkButton 构造/析构） ==================== */

/** @brief XCommandLinkButton 类虚函数表初始化（重载 ContentChanged/PaintEvent/Copy/Move/Deinit，并登记为 XAbstractButton 派生类）。 */
XVtable* XCommandLinkButton_class_init(void);

/**
 * @brief      初始化 XCommandLinkButton（对标 QCommandLinkButton(parent) 构造）。
 * @details    先初始化 XPushButton 基类（含 QAbstractButton/QPushButton 默认值），
 *             再挂 XCommandLinkButton 虚表并设置 QCommandLinkButton 默认值：
 *             描述为空、图标尺寸 20x20（对标 qcommandlinkbutton.cpp:189）、
 *             尺寸策略 Preferred/Preferred + PushButton（对标 :185）。
 * @param      self   待初始化对象；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志（可传 0 表示 Widget 类型）。
 */
void XCommandLinkButton_init(XCommandLinkButton* self, XWidget* parent,
                             XWidgetFlags flags);

/** @brief 使用默认内存类型创建命令链接按钮控件（语义同 XWidget_create）。 */
#define XCommandLinkButton_create(parent, flags)  XCommandLinkButton_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建命令链接按钮控件。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志。
 * @return     新对象指针；失败返回 NULL。
 */
XCommandLinkButton* XCommandLinkButton_create_ex(XMemoryType memory,
                                                 XWidget* parent,
                                                 XWidgetFlags flags);

/** @brief 通过 XClass 虚表释放 XCommandLinkButton 资源（栈/外部存储对象使用）。 */
#define XCommandLinkButton_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XCommandLinkButton 对象。 */
#define XCommandLinkButton_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 继承 XAbstractButton 的公共 API（宏别名保持原名称） ==================== */

/** @brief 查询按钮文本（对标 QAbstractButton::text）。 */
#define XCommandLinkButton_text(self)  XAbstractButton_text((const XAbstractButton*)(self))
/** @brief 设置按钮文本（标题，对标 QAbstractButton::setText）。 */
#define XCommandLinkButton_setText(self, text)  XAbstractButton_setText((XAbstractButton*)(self), (text))
/** @brief 设置按钮文本（UTF-8 C 字符串便利版本）。 */
#define XCommandLinkButton_setText_2(self, utf8)  XAbstractButton_setText_2((XAbstractButton*)(self), (utf8))

/** @brief 返回按钮图标（对标 QAbstractButton::icon）。 */
#define XCommandLinkButton_icon(self)  XAbstractButton_icon((const XAbstractButton*)(self))
/** @brief 设置按钮图标（对标 QAbstractButton::setIcon）。 */
#define XCommandLinkButton_setIcon(self, icon)  XAbstractButton_setIcon((XAbstractButton*)(self), (icon))
/** @brief 查询按钮图标渲染尺寸（对标 QAbstractButton::iconSize）。 */
#define XCommandLinkButton_iconSize(self)  XAbstractButton_iconSize((const XAbstractButton*)(self))
/** @brief 设置按钮图标渲染尺寸（对标 QAbstractButton::setIconSize）。 */
#define XCommandLinkButton_setIconSize(self, size)  XAbstractButton_setIconSize((XAbstractButton*)(self), (size))

/** @brief 查询是否可选中（对标 QAbstractButton::isCheckable）。 */
#define XCommandLinkButton_isCheckable(self)  XAbstractButton_isCheckable((const XAbstractButton*)(self))
/** @brief 设置是否可选中（对标 QAbstractButton::setCheckable）。 */
#define XCommandLinkButton_setCheckable(self, checkable)  XAbstractButton_setCheckable((XAbstractButton*)(self), (checkable))
/** @brief 查询是否选中（对标 QAbstractButton::isChecked）。 */
#define XCommandLinkButton_isChecked(self)  XAbstractButton_isChecked((const XAbstractButton*)(self))
/** @brief 设置是否选中（对标 QAbstractButton::setChecked）。 */
#define XCommandLinkButton_setChecked(self, checked)  XAbstractButton_setChecked((XAbstractButton*)(self), (checked))
/** @brief 切换选中状态（对标 QAbstractButton::toggle）。 */
#define XCommandLinkButton_toggle(self)  XAbstractButton_toggle((XAbstractButton*)(self))

/** @brief 查询按下面板状态（对标 QAbstractButton::isDown）。 */
#define XCommandLinkButton_isDown(self)  XAbstractButton_isDown((const XAbstractButton*)(self))
/** @brief 设置按下面板状态（对标 QAbstractButton::setDown）。 */
#define XCommandLinkButton_setDown(self, down)  XAbstractButton_setDown((XAbstractButton*)(self), (down))

/** @brief 查询是否允许按住自动重复（对标 QAbstractButton::autoRepeat）。 */
#define XCommandLinkButton_autoRepeat(self)  XAbstractButton_autoRepeat((const XAbstractButton*)(self))
/** @brief 设置是否允许按住自动重复（对标 QAbstractButton::setAutoRepeat）。 */
#define XCommandLinkButton_setAutoRepeat(self, repeat)  XAbstractButton_setAutoRepeat((XAbstractButton*)(self), (repeat))
/** @brief 查询自动重复开始延迟（毫秒）。 */
#define XCommandLinkButton_autoRepeatDelay(self)  XAbstractButton_autoRepeatDelay((const XAbstractButton*)(self))
/** @brief 设置自动重复开始延迟（毫秒）。 */
#define XCommandLinkButton_setAutoRepeatDelay(self, delay)  XAbstractButton_setAutoRepeatDelay((XAbstractButton*)(self), (delay))
/** @brief 查询自动重复间隔（毫秒）。 */
#define XCommandLinkButton_autoRepeatInterval(self)  XAbstractButton_autoRepeatInterval((const XAbstractButton*)(self))
/** @brief 设置自动重复间隔（毫秒）。 */
#define XCommandLinkButton_setAutoRepeatInterval(self, interval)  XAbstractButton_setAutoRepeatInterval((XAbstractButton*)(self), (interval))
/** @brief 查询自动互斥标志（对标 QAbstractButton::autoExclusive）。 */
#define XCommandLinkButton_autoExclusive(self)  XAbstractButton_autoExclusive((const XAbstractButton*)(self))
/** @brief 设置自动互斥标志（对标 QAbstractButton::setAutoExclusive）。 */
#define XCommandLinkButton_setAutoExclusive(self, exclusive)  XAbstractButton_setAutoExclusive((XAbstractButton*)(self), (exclusive))

/** @brief 程序化点击按钮（对标 QAbstractButton::click）。 */
#define XCommandLinkButton_click(self)  XAbstractButton_click((XAbstractButton*)(self))
/** @brief 动画点击（对标 QAbstractButton::animateClick）。 */
#define XCommandLinkButton_animateClick(self)  XAbstractButton_animateClick((XAbstractButton*)(self))

/* ==================== 信号（继承 XAbstractButton，宏别名保持原名称） ==================== */

/** @brief 按下信号（对标 QAbstractButton::pressed）。 */
#define XCommandLinkButton_pressed_signal(self)  XAbstractButton_pressed_signal((XAbstractButton*)(self))
/** @brief 释放信号（对标 QAbstractButton::released）。 */
#define XCommandLinkButton_released_signal(self)  XAbstractButton_released_signal((XAbstractButton*)(self))
/** @brief 点击信号（对标 QAbstractButton::clicked(bool)）。 */
#define XCommandLinkButton_clicked_signal(self, checked)  XAbstractButton_clicked_signal((XAbstractButton*)(self), (checked))
/** @brief 选中变化信号（对标 QAbstractButton::toggled(bool)）。 */
#define XCommandLinkButton_toggled_signal(self, checked)  XAbstractButton_toggled_signal((XAbstractButton*)(self), (checked))

/* ==================== 继承 XPushButton 特有 API（宏别名保持原名称） ==================== */

/** @brief 查询自动默认按钮是否生效（对标 QPushButton::autoDefault）。 */
#define XCommandLinkButton_autoDefault(self)  XPushButton_autoDefault((const XPushButton*)(self))
/** @brief 设置自动默认按钮（对标 QPushButton::setAutoDefault）。 */
#define XCommandLinkButton_setAutoDefault(self, enable)  XPushButton_setAutoDefault((XPushButton*)(self), (enable))
/** @brief 查询是否为默认按钮（对标 QPushButton::isDefault）。 */
#define XCommandLinkButton_isDefault(self)  XPushButton_isDefault((const XPushButton*)(self))
/** @brief 设置是否为默认按钮（对标 QPushButton::setDefault）。 */
#define XCommandLinkButton_setDefault(self, enable)  XPushButton_setDefault((XPushButton*)(self), (enable))
/** @brief 查询是否为扁平按钮（对标 QPushButton::isFlat）。 */
#define XCommandLinkButton_isFlat(self)  XPushButton_isFlat((const XPushButton*)(self))
/** @brief 设置是否为扁平按钮（对标 QPushButton::setFlat）。 */
#define XCommandLinkButton_setFlat(self, flat)  XPushButton_setFlat((XPushButton*)(self), (flat))
/** @brief 关联弹出菜单（对标 QPushButton::setMenu）。 */
#define XCommandLinkButton_setMenu(self, menu)  XPushButton_setMenu((XPushButton*)(self), (menu))
/** @brief 返回关联弹出菜单（对标 QPushButton::menu）。 */
#define XCommandLinkButton_menu(self)  XPushButton_menu((const XPushButton*)(self))
/** @brief 显示关联菜单（对标 QPushButton::showMenu）。 */
#define XCommandLinkButton_showMenu(self)  XPushButton_showMenu((XPushButton*)(self))
/** @brief 判断局部坐标是否命中按钮（对标 QPushButton::hitButton）。 */
#define XCommandLinkButton_hitButton(self, pos)  XPushButton_hitButton((const XPushButton*)(self), (pos))

/* ==================== 描述文本（对标 QCommandLinkButton description） ==================== */

/**
 * @brief      查询描述文本（对标 QCommandLinkButton::description）。
 * @param      self 命令链接按钮对象；可为 NULL。
 * @return     描述文本的借用指针；NULL 或无描述返回 NULL。返回指针不能
 *             释放、不能修改。
 */
const XString* XCommandLinkButton_description(const XCommandLinkButton* self);
/**
 * @brief      设置描述文本（对标 QCommandLinkButton::setDescription）。
 * @details    深拷贝 description；变化后刷新 sizeHint 并重绘。NULL 视为空。
 * @param      self 待修改的命令链接按钮对象；可为 NULL。
 * @param      description 源文本借用指针；可为 NULL。
 * @return     无返回值。
 */
void XCommandLinkButton_setDescription(XCommandLinkButton* self,
                                       const XString* description);
/** @brief 使用 UTF-8 C 字符串设置描述文本。 */
void XCommandLinkButton_setDescription_2(XCommandLinkButton* self,
                                         const char* utf8);

/* ==================== 尺寸（对标 QCommandLinkButton sizeHint/minimumSizeHint） ==================== */

/**
 * @brief      查询命令链接按钮建议尺寸（对标 QCommandLinkButton::sizeHint）。
 * @details    按标题+描述两行文本与左侧大图标计算：宽取标题/描述文本宽与
 *             图标宽的最大值加边距；高为图标高与两行文本高之和。无描述时
 *             高度只含标题行。
 * @param      self 命令链接按钮对象；NULL 返回无效尺寸 (-1,-1)。
 * @return     建议尺寸。
 */
XSize XCommandLinkButton_sizeHint(const XCommandLinkButton* self);
/** @brief 查询最小建议尺寸（对标 QCommandLinkButton::minimumSizeHint；Qt 中直接返回 sizeHint()）。 */
XSize XCommandLinkButton_minimumSizeHint(const XCommandLinkButton* self);

/* ==================== 离屏绘制入口（对标 QCommandLinkButton::paintEvent 内容） ==================== */

/**
 * @brief      把命令链接按钮内容绘制到给定绘制器。
 * @details    与重绘事件中的绘制路径一致：填充 Window 背景色、绘制左侧
 *             大图标、标题与描述双行文本（描述使用次要角色色）、右侧箭头。
 *             供离屏渲染与回归测试直接调用；不设置任何平台资源。
 * @param      self 命令链接按钮对象；NULL 无操作。
 * @param      painter 目标绘制器；NULL 无操作。
 */
void XCommandLinkButton_drawContents(XCommandLinkButton* self,
                                     XPainter* painter);

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XCOMMANDLINKBUTTON_ON */

#ifdef __cplusplus
}
#endif
#endif /* XCOMMANDLINKBUTTON_H */
