/******************************************************************************
 * @file       XPushButton.h
 * @brief      XPushButton 按钮控件（对标 Qt 6.8 QPushButton，继承 XAbstractButton）。
 * @details    XPushButton 继承 XAbstractButton（对齐 Qt 的
 *             QPushButton : QAbstractButton 继承关系）：
 *             - 文本/图标：text/setText（XString 与 UTF-8 C 字符串版本）、
 *               icon/setIcon、iconSize/setIconSize 由 XAbstractButton 提供，
 *               本头文件以宏别名保持 XPushButton_* 名称；
 *             - 按钮状态：checkable/checked/toggle、down/setDown、
 *               autoRepeat/autoRepeatDelay/autoRepeatInterval、
 *               autoExclusive/setAutoExclusive 继承 XAbstractButton；
 *             - 激活与命中：click/animateClick 继承 XAbstractButton；
 *               hitButton 重载基类保护槽，按 Qt 走
 *               style()->subElementRect(SE_PushButtonBevel)
 *               （qpushbutton.cpp:499-505）；
 *             - 信号：pressed/released/clicked(bool)/toggled(bool) 复用
 *               XAbstractButton 的信号槽机制，宏别名保持原名称；
 *             - 本类只实现 QPushButton 特有部分：autoDefault/default/flat
 *               三态与 setMenu/menu/showMenu、sizeHint/minimumSizeHint、
 *               drawContents 离屏绘制与 PaintEvent 重绘；
 *             - 事件：鼠标/键盘按下与释放、自动重复/动画定时器由
 *               XAbstractButton 基类统一处理；本类只补充键盘
 *               Return/Enter 在默认或 autoDefault 时触发点击；
 *             - 内容变更：本类重载 XAbstractButton 的 contentChanged
 *               保护槽，在文本/图标/图标尺寸变化后刷新自身 sizeHint
 *               存储位（XWidget 的 sizeHint 是存储位而非虚函数）。
 *             嵌入式裁剪由 XGuiConfig.h 的 XPUSHBUTTON_ON 控制，且依赖
 *             XABSTRACTBUTTON_ON（关闭基类时 XPushButton 一并裁剪）；
 *             绘制使用 XPainter 输出简单 raised/sunken/flat 外观，不依赖
 *             任何平台 API。
 * @note       近似边界：QAbstractButton 的显式 QButtonGroup 登记由
 *             XButtonGroup 承担（对 XPushButton 生效，基类据 group()
 *             承载位做独占反选保护；本类自身不再额外登记）；助记符
 *             '&' 的显示/度量与 Alt+字符 shortcut 文本已实现（激活受
 *             XShortcut 单键注册表限制，暂不登记裸键绑定），命中区已按
 *             Qt 走 style()->subElementRect(SE_PushButtonBevel)
 *             （qpushbutton.cpp:499-505；QSS margin 收窄待样式批次补
 *             XStyleSheetStyle::SubElementRect）；autoExclusive
 *             已按同一父控件的自动互斥按钮组实现；autoDefault 已实现父
 *             对话框链自动解析（对标 Qt 6.8 QPushButtonPrivate::dialogParent）
 *             与焦点进出迁移默认位（focusInEvent/focusOutEvent）；菜单关联
 *             （setMenu/menu/showMenu）已实现但真实平台弹层未接入；
 *             hitButton 走样式 SE_PushButtonBevel（默认 QCommonStyle
 *             返回控件矩形，qcommonstyle.cpp:2495-2499；QStyleSheetStyle
 *             的 margin 收窄待样式批次补 SubElementRect）。详细说明见
 *             XGui.md。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPUSHBUTTON_H
#define XPUSHBUTTON_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XAbstractButton.h"
#include "XPainter.h"
#include "XMenu.h"

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON

/* ==================== 虚函数表（继承 XAbstractButton 派生槽位） ==================== */

/**
 * @brief XPushButton 虚函数表枚举。
 * @details 槽位数量与 XAbstractButton 完全一致；XPushButton 不新增槽位，
 *          仅重载 XAbstractButton 的 ContentChanged 保护槽与 XWidget 的
 *          PaintEvent/KeyPressEvent，以及 XClass 的 Copy/Move/Deinit。
 */
XCLASS_DEFINE_BEGING(XPushButton)
XCLASS_DEFINE_EXTEND_END(XPushButton, XAbstractButton)

/* ==================== AutoDefault 内部三态（对标 QPushButtonPrivate） ==================== */

/** @brief 自动默认按钮内部三态（对标 Qt 6.8 QPushButtonPrivate::AutoDefault）。 */
typedef enum XPushButtonAutoDefault
{
    XPushButtonAutoDefault_Auto = 0, /**< 由父对话框是否默认按钮自动推断。 */
    XPushButtonAutoDefault_Off = 1,  /**< 不启用自动默认。 */
    XPushButtonAutoDefault_On = 2    /**< 启用自动默认。 */
} XPushButtonAutoDefault;

/* ==================== 控件对象（对标 QPushButton : QAbstractButton） ==================== */

/**
 * @brief XPushButton 按钮控件对象。
 * @details 首成员 m_base 必须是 XAbstractButton（继承）；QAbstractButton
 *          的文本/图标/状态位/定时器字段全部位于基类，本类只保留
 *          QPushButton 特有字段：扁平、默认按钮、自动默认三态与关联菜单。
 */
typedef struct XPushButton
{
    XAbstractButton      m_base;            /**< XAbstractButton 基类成员；必须是第一个。 */
    bool                 m_flat;            /**< 是否为扁平按钮。 */
    bool                 m_defaultButton;   /**< 是否为对话框默认按钮。 */
    XPushButtonAutoDefault m_autoDefault;   /**< 自动默认三态。 */
    XMenu*               m_menu;            /**< 关联弹出菜单（借用指针；不拥有）。 */
} XPushButton;

/* ==================== 生命周期（对标 QPushButton/QAbstractButton 构造析构） ==================== */

/** @brief XPushButton 类虚函数表初始化（重载 ContentChanged/Paint/KeyPress/Copy/Move/Deinit，并登记为 XAbstractButton 派生类）。 */
XVtable* XPushButton_class_init(void);

/**
 * @brief      初始化 XPushButton（对标 QPushButton(parent) 构造）。
 * @details    先初始化 XAbstractButton 基类（parent/flags 语义同
 *             XWidget_init，基类内部设置按钮默认值：文本空、图标空、
 *             checkable/checked/down/pressed=false、autoRepeat=false、
 *             autoExclusive=false、autoRepeatDelay=300、
 *             autoRepeatInterval=100、前景角色 ButtonText、背景角色
 *             Button、焦点策略 StrongFocus），再挂 XPushButton 虚表并
 *             设置 QPushButton 特有默认值：autoDefault=Auto、
 *             defaultButton=false、flat=false、尺寸策略
 *             Minimum/Fixed + PushButton。
 * @param      self   待初始化对象；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志（可传 0 表示 Widget 类型）。
 */
void XPushButton_init(XPushButton* self, XWidget* parent, XWidgetFlags flags);

/** @brief 使用默认内存类型创建按钮控件（语义同 XWidget_create）。 */
#define XPushButton_create(parent, flags) XPushButton_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建按钮控件。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志。
 * @return     新对象指针；失败返回 NULL。
 */
XPushButton* XPushButton_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);

/** @brief 通过 XClass 虚表释放 XPushButton 资源（栈/外部存储对象使用）。 */
/** @brief 删除堆上的 XPushButton 对象。 */

/* ==================== 继承 XAbstractButton 的公共 API（宏别名保持原名称） ==================== */

/** @brief 查询按钮文本（对标 QAbstractButton::text；返回借用指针，生命周期同对象）。 */
#define XPushButton_text(self)  XAbstractButton_text((const XAbstractButton*)(self))
/**
 * @brief      设置按钮文本（对标 QAbstractButton::setText）。
 * @details    与当前文本相同则为无操作；否则深拷贝新文本，经基类
 *             contentChanged 虚槽刷新本类 sizeHint 后更新几何与重绘。
 * @param      text 新文本；NULL 视为空。
 */
#define XPushButton_setText(self, text)  XAbstractButton_setText((XAbstractButton*)(self), (text))
/** @brief 设置按钮文本（UTF-8 C 字符串便利版本，含 '\0' 结尾）。 */
#define XPushButton_setText_2(self, utf8)  XAbstractButton_setText_2((XAbstractButton*)(self), (utf8))

/** @brief 返回按钮图标（对标 QAbstractButton::icon；按值返回共享数据）。 */
#define XPushButton_icon(self)  XAbstractButton_icon((const XAbstractButton*)(self))
/**
 * @brief      设置按钮图标（对标 QAbstractButton::setIcon）。
 * @details    复制图标值；经基类 contentChanged 虚槽刷新尺寸提示后更新
 *             几何与重绘。
 * @param      icon 源图标指针；NULL 或空图标会清空按钮图标。
 */
#define XPushButton_setIcon(self, icon)  XAbstractButton_setIcon((XAbstractButton*)(self), (icon))

/** @brief 查询按钮图标渲染尺寸（对标 QAbstractButton::iconSize）。 */
#define XPushButton_iconSize(self)  XAbstractButton_iconSize((const XAbstractButton*)(self))
/**
 * @brief      设置按钮图标渲染尺寸（对标 QAbstractButton::setIconSize）。
 * @param      size 目标尺寸；NULL 或宽高不大于 0 表示恢复默认。
 */
#define XPushButton_setIconSize(self, size)  XAbstractButton_setIconSize((XAbstractButton*)(self), (size))

/** @brief 查询按钮是否可选中（对标 QAbstractButton::isCheckable，默认 false）。 */
#define XPushButton_isCheckable(self)  XAbstractButton_isCheckable((const XAbstractButton*)(self))
/** @brief 设置按钮是否可选中（对标 QAbstractButton::setCheckable）。 */
#define XPushButton_setCheckable(self, checkable)  XAbstractButton_setCheckable((XAbstractButton*)(self), (checkable))
/** @brief 查询按钮是否选中（对标 QAbstractButton::isChecked，默认 false）。 */
#define XPushButton_isChecked(self)  XAbstractButton_isChecked((const XAbstractButton*)(self))
/** @brief 设置按钮选中状态（对标 QAbstractButton::setChecked）。 */
#define XPushButton_setChecked(self, checked)  XAbstractButton_setChecked((XAbstractButton*)(self), (checked))
/** @brief 切换选中状态并发射 toggled（对标 QAbstractButton::toggle）。 */
#define XPushButton_toggle(self)  XAbstractButton_toggle((XAbstractButton*)(self))

/** @brief 查询按下面板状态（对标 QAbstractButton::isDown，默认 false）。 */
#define XPushButton_isDown(self)  XAbstractButton_isDown((const XAbstractButton*)(self))
/** @brief 设置按下面板状态（对标 QAbstractButton::setDown）。 */
#define XPushButton_setDown(self, down)  XAbstractButton_setDown((XAbstractButton*)(self), (down))

/** @brief 查询是否允许按住自动重复（对标 QAbstractButton::autoRepeat，默认 false）。 */
#define XPushButton_autoRepeat(self)  XAbstractButton_autoRepeat((const XAbstractButton*)(self))
/** @brief 设置是否允许按住自动重复（对标 QAbstractButton::setAutoRepeat）。 */
#define XPushButton_setAutoRepeat(self, repeat)  XAbstractButton_setAutoRepeat((XAbstractButton*)(self), (repeat))
/** @brief 查询自动重复开始延迟（毫秒；默认 300）。 */
#define XPushButton_autoRepeatDelay(self)  XAbstractButton_autoRepeatDelay((const XAbstractButton*)(self))
/** @brief 设置自动重复开始延迟（毫秒；按 Qt 语义保存调用方整数）。 */
#define XPushButton_setAutoRepeatDelay(self, delay)  XAbstractButton_setAutoRepeatDelay((XAbstractButton*)(self), (delay))
/** @brief 查询自动重复间隔（毫秒；默认 100）。 */
#define XPushButton_autoRepeatInterval(self)  XAbstractButton_autoRepeatInterval((const XAbstractButton*)(self))
/** @brief 设置自动重复间隔（毫秒；按 Qt 语义保存调用方整数）。 */
#define XPushButton_setAutoRepeatInterval(self, interval)  XAbstractButton_setAutoRepeatInterval((XAbstractButton*)(self), (interval))
/** @brief 查询自动互斥标志（对标 QAbstractButton::autoExclusive；默认 false）。 */
#define XPushButton_autoExclusive(self)  XAbstractButton_autoExclusive((const XAbstractButton*)(self))
/** @brief 设置自动互斥标志（同一父控件下的自动互斥按钮按 Qt 规则联动）。 */
#define XPushButton_setAutoExclusive(self, exclusive)  XAbstractButton_setAutoExclusive((XAbstractButton*)(self), (exclusive))

/**
 * @brief      程序化点击按钮（对标 QAbstractButton::click）。
 * @details    禁用时直接返回；启用时按 pressed、down、nextCheckState、
 *             released、clicked 顺序执行，选中状态变化时发射 toggled。
 */
#define XPushButton_click(self)  XAbstractButton_click((XAbstractButton*)(self))
/**
 * @brief      动画点击（对标 QAbstractButton::animateClick）。
 * @details    立即进入按下状态并发射一次 pressed()，100ms 后由事件循环
 *             发射 released()/clicked()；重复调用会重置释放定时器。
 */
#define XPushButton_animateClick(self)  XAbstractButton_animateClick((XAbstractButton*)(self))

/* ==================== 信号（继承 XAbstractButton，宏别名保持原名称） ==================== */

/** @brief 按下信号（对标 QAbstractButton::pressed）。 */
#define XPushButton_pressed_signal(self)  XAbstractButton_pressed_signal((XAbstractButton*)(self))
/** @brief 释放信号（对标 QAbstractButton::released）。 */
#define XPushButton_released_signal(self)  XAbstractButton_released_signal((XAbstractButton*)(self))
/** @brief 点击信号（对标 QAbstractButton::clicked(bool)，参数为点击后的 checked 状态）。 */
#define XPushButton_clicked_signal(self, checked)  XAbstractButton_clicked_signal((XAbstractButton*)(self), (checked))
/** @brief 选中变化信号（对标 QAbstractButton::toggled(bool)，参数为变化后的 checked 状态）。 */
#define XPushButton_toggled_signal(self, checked)  XAbstractButton_toggled_signal((XAbstractButton*)(self), (checked))

/* ==================== 命中（QAbstractButton protected，经虚表分派） ==================== */

/**
 * @brief      判断局部坐标是否命中按钮（对标 QPushButton::hitButton）。
 * @details    对标 QPushButton::hitButton（qpushbutton.cpp:499-505）：
 *             `style()->subElementRect(SE_PushButtonBevel, &opt, this)
 *             .contains(pos)`。默认 QCommonStyle 的 bevel 等于控件矩形；
 *             QSS 规则带 margin/非 native 边框时按 borderRect 收窄
 *             （qstylesheetstyle.cpp:6147-6166，待样式批次实现）。
 *             事件路径经虚表覆写落到本函数（XAbstractButton 的
 *             mousePress/Move/Release 用 hitButton_base 分派）。
 * @param      self 按钮对象。
 * @param      pos 控件局部坐标；NULL 返回 false。
 * @return     命中返回 true。
 */
bool XPushButton_hitButton(const XPushButton* self, const XPoint* pos);

/* ==================== 默认/扁平（对标 QPushButton） ==================== */

/** @brief 查询自动默认按钮是否生效（对标 QPushButton::autoDefault）。 */
bool XPushButton_autoDefault(const XPushButton* self);
/**
 * @brief      设置自动默认按钮（对标 QPushButton::setAutoDefault）。
 * @param      enable true 设为 On，false 设为 Off。
 */
void XPushButton_setAutoDefault(XPushButton* self, bool enable);
/** @brief 查询是否为默认按钮（对标 QPushButton::isDefault，默认 false）。 */
bool XPushButton_isDefault(const XPushButton* self);
/**
 * @brief      设置是否为默认按钮（对标 QPushButton::setDefault）。
 * @details    只改 default 位并重绘，**不**改 autoDefault 三态（同 Qt
 *             qpushbutton.cpp:951-959）。置 true 时沿父链找所属对话框，
 *             清掉其子树内其它按钮的默认位（QDialogPrivate::setMainDefault
 *             的等价实现），保证对话框内至多一个默认按钮。
 *             autoDefault 生效的按钮在获得焦点时自动置默认位、失焦时
 *             交还（focusInEvent/focusOutEvent，qpushbutton.cpp:438-476）。
 */
void XPushButton_setDefault(XPushButton* self, bool enable);
/** @brief 查询是否为扁平按钮（对标 QPushButton::isFlat，默认 false）。 */
bool XPushButton_isFlat(const XPushButton* self);
/** @brief 设置是否为扁平按钮（对标 QPushButton::setFlat）；变化时重绘。 */
void XPushButton_setFlat(XPushButton* self, bool flat);

/* ==================== 菜单（对标 QPushButton setMenu/menu/showMenu） ==================== */

/**
 * @brief      关联弹出菜单（对标 QPushButton::setMenu，Qt 6.8 qpushbutton.h:43）。
 * @details    把按钮变为菜单按钮；菜单所有权不转移，按钮只借用指针。与
 *             当前菜单相同或 self/menu 为 NULL 时按 Qt 语义处理：
 *             - self 为 NULL 直接无操作；
 *             - menu 为 NULL 表示解除当前菜单关联；
 *             - 关联时按 UniqueConnection 语义建立 pressed→弹层桥
 *               （对标 qpushbutton.cpp:521-547 的 popupPressed 连接），
 *               按下即弹出菜单；aboutToHide 时恢复按钮按下态；
 *             - 替换后刷新几何与重绘，供绘制路径显示菜单箭头。
 * @param      self 按钮对象。
 * @param      menu 要关联的弹出菜单（借用指针）；NULL 解除关联。
 */
void XPushButton_setMenu(XPushButton* self, XMenu* menu);
/** @brief 返回当前关联的弹出菜单（对标 QPushButton::menu，qpushbutton.h:44）。 */
XMenu* XPushButton_menu(const XPushButton* self);
/**
 * @brief      显示关联菜单（对标 QPushButton::showMenu，qpushbutton.h:51）。
 * @details    对标 qpushbutton.cpp:560-568 showMenu→popupPressed：进入
 *             按下态后经 XMenu_popup 在按钮正下方弹出关联菜单，菜单收起
 *             （aboutToHide）时恢复按钮状态；无菜单或按钮禁用时无操作。
 * @param      self 按钮对象。
 */
void XPushButton_showMenu(XPushButton* self);

/* ==================== 尺寸（对标 QPushButton sizeHint/minimumSizeHint） ==================== */

/* sizeHint/minimumSizeHint 是 XWidget 虚函数（对标 QPushButton::
 * sizeHint 覆写；Qt 6.8 中 minimumSizeHint 直接返回 sizeHint()）：
 * 业务体在 XPushButton.c 的 static VXPushButton_sizeHint/minimumSizeHint
 * 槽内并经 class_init 注册虚表，对外统一走基类 `*_base` 入口虚分派，
 * 本类不再另设无后缀公开函数（《api命名风格》`*_base` 契约）。
 * 下列宏是按类型名调用的别名；实际是 XWidget 虚分派入口。 */
#define XPushButton_sizeHint_base(self) \
    XWidget_sizeHint_base((const XWidget*)(self))
#define XPushButton_minimumSizeHint_base(self) \
    XWidget_minimumSizeHint_base((const XWidget*)(self))

/* ==================== 离屏绘制入口（对标 QPushButton::paintEvent 内容） ==================== */

/**
 * @brief      把按钮内容绘制到给定绘制器。
 * @details    与重绘事件中的绘制路径一致：按尺寸/禁用/扁平/按下/选中
 *             状态绘制背景与立体边框，然后画图标和文本。供离屏渲染与
 *             回归测试直接调用；不设置任何平台资源。
 * @param      self 按钮对象；NULL 无操作。
 * @param      painter 目标绘制器；NULL 无操作。
 */
void XPushButton_drawContents(XPushButton* self, XPainter* painter);

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XPUSHBUTTON_H */
