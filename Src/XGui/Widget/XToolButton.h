/**
 * @file       XToolButton.h
 * @brief      XToolButton 工具按钮公开 API（对标 Qt 6.8 QToolButton）。
 * @details    XToolButton 继承 XAbstractButton，实现 QToolButton 的核心
 *             语义：
 *             - 默认动作：defaultAction/setDefaultAction，与 XAction
 *               双向联动（图标/文本/可选中/选中/启用镜像，点击触发动作，
 *               动作 triggered 转发为按钮 triggered(XAction*)）；
 *             - 外观：toolButtonStyle（IconOnly/TextOnly/TextBesideIcon/
 *               TextUnderIcon/FollowStyle）、autoRaise、arrowType；
 *             - 菜单：menu/setMenu、popupMode/showMenu（弹出 XMenu）；
 *             - 尺寸：sizeHint/minimumSizeHint 按样式与内容计算。
 *             点击动作触发顺序对齐 Qt：有默认动作时由动作承担状态翻转，
 *             按钮 triggered(action) 信号在动作 triggered 后转发。
 * @note       模块总开关 XTOOLBUTTON_ON 定义于 XGuiConfig.h；关闭时裁剪
 *             本头文件全部公共声明。XToolButton 依赖 XABSTRACTBUTTON_ON
 *             与 XMENU_ON（菜单功能）。对象字段由 XClass 生命周期管理。
 * @note       按项目约束，QToolButton 的 QKeySequence 快捷键、样式表与
 *             tear-off 平台菜单不在本类提供，作为裁剪边界在 XGui.md 记录。
 * @author     XinYueC 团队
 */
#ifndef XTOOLBUTTON_H
#define XTOOLBUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "XGuiConfig.h"
#include "XAbstractButton.h"
#include "XMenu.h"
#include "XString.h"
#include "XGeometry.h"

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON

/* ==================== 枚举（对标 Qt 6.8 QToolButton） ==================== */

/**
 * @brief      工具按钮样式（对标 Qt::ToolButtonStyle，取值一致）。
 */
typedef enum XToolButtonStyle
{
    XToolButtonStyle_IconOnly = 0,        /**< 只显示图标（对标 Qt::ToolButtonIconOnly）。 */
    XToolButtonStyle_TextOnly = 1,        /**< 只显示文本（对标 Qt::ToolButtonTextOnly）。 */
    XToolButtonStyle_TextBesideIcon = 2,  /**< 图标左侧、文本右侧（对标 Qt::ToolButtonTextBesideIcon）。 */
    XToolButtonStyle_TextUnderIcon = 3,   /**< 图标上方、文本下方（对标 Qt::ToolButtonTextUnderIcon）。 */
    XToolButtonStyle_FollowStyle = 4      /**< 跟随全局样式（本实现按 TextBesideIcon 处理，对标 Qt::ToolButtonFollowStyle）。 */
} XToolButtonStyle;

/**
 * @brief      箭头类型（对标 Qt::ArrowType，取值一致）。
 */
typedef enum XToolButtonArrowType
{
    XToolButtonArrowType_NoArrow = 0,     /**< 无箭头（对标 Qt::NoArrow）。 */
    XToolButtonArrowType_Up = 1,          /**< 上箭头（对标 Qt::UpArrow）。 */
    XToolButtonArrowType_Down = 2,        /**< 下箭头（对标 Qt::DownArrow）。 */
    XToolButtonArrowType_Left = 3,        /**< 左箭头（对标 Qt::LeftArrow）。 */
    XToolButtonArrowType_Right = 4        /**< 右箭头（对标 Qt::RightArrow）。 */
} XToolButtonArrowType;

/**
 * @brief      菜单弹出模式（对标 QToolButton::ToolButtonPopupMode，取值
 *             一致）。
 */
typedef enum XToolButtonPopupMode
{
    XToolButtonPopupMode_DelayedPopup = 0,   /**< 按住延迟弹出（对标 QToolButton::DelayedPopup）。 */
    XToolButtonPopupMode_MenuButtonPopup = 1,/**< 菜单按钮区弹出（对标 QToolButton::MenuButtonPopup）。 */
    XToolButtonPopupMode_InstantPopup = 2    /**< 立即弹出（对标 QToolButton::InstantPopup）。 */
} XToolButtonPopupMode;

/* ==================== 类虚函数表 ==================== */

/**
 * @brief      XToolButton 类虚函数表。
 * @details    XToolButton 继承 XAbstractButton 的全部事件槽位，并重载
 *             ContentChanged（刷新尺寸提示）与 XClass 的 Copy/Move/
 *             Deinit。
 */
XCLASS_DEFINE_BEGING(XToolButton)
XCLASS_DEFINE_EXTEND_END(XToolButton, XAbstractButton)

/* ==================== 工具按钮对象（对标 Qt 6.8 QToolButton） ==================== */

/**
 * @brief      XToolButton 工具按钮对象。
 * @details    m_base 是第一个成员，因此对象可向上转换为 XAbstractButton/
 *             XWidget/XObject；m_defaultAction 与 m_menu 均为借用指针，
 *             不取得所有权。所有成员均属于实现状态，调用方不得直接修改。
 */
typedef struct XToolButton
{
    XAbstractButton m_base;               /**< 基类成员；必须是第一个，由 XClass 管理，禁止手工修改。 */
    XAction*        m_defaultAction;      /**< 默认动作（对标 QToolButton::defaultAction）；借用指针。 */
    XMenu*          m_menu;               /**< 关联弹出菜单（对标 QToolButton::menu）；借用指针。 */
    XToolButtonStyle    m_toolButtonStyle;   /**< 工具按钮样式。 */
    XToolButtonArrowType m_arrowType;     /**< 箭头类型。 */
    XToolButtonPopupMode m_popupMode;     /**< 菜单弹出模式。 */
    bool            m_autoRaise;          /**< 是否自动凸起（对标 QToolButton::autoRaise）。 */
} XToolButton;

/* ==================== 类初始化与生命周期 ==================== */

/**
 * @brief      初始化并返回 XToolButton 类的共享虚函数表。
 * @return     类共享的 XVtable 指针；虚表创建或注册失败时返回 NULL。
 */
XVtable* XToolButton_class_init(void);

/**
 * @brief      初始化嵌入式 XToolButton 对象（对标 QToolButton 构造）。
 * @param      self 待初始化的可写对象存储；不可为 NULL，且必须尚未初始化。
 * @param      parent 父控件借用指针；可为 NULL，按钮不取得其所有权。
 * @param      flags 窗口标志；可传 0 表示普通 Widget 类型。
 * @return     无返回值；self 不满足初始化前提时调用方不得继续使用对象。
 */
void XToolButton_init(XToolButton* self, XWidget* parent,
                      XWidgetFlags flags);

/**
 * @brief      使用默认内存类型创建并初始化工具按钮对象。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志；可传 0。
 * @return     新建的已初始化对象指针；分配失败返回 NULL。成功返回的
 *             对象由调用方拥有，必须使用 XToolButton_delete_base 释放。
 */
#define XToolButton_create(parent, flags) \
    XToolButton_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))

/**
 * @brief      使用指定内存类型创建并初始化工具按钮对象。
 * @param      memory 对象分配所使用的 XMemoryType。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 窗口标志；可传 0。
 * @return     新建的已初始化对象指针；分配或初始化失败返回 NULL。
 */
XToolButton* XToolButton_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);

/**
 * @brief      通过当前 XClass 虚表释放工具按钮对象所拥有的资源。
 * @param      self 已初始化的栈对象或外部存储对象；可为 NULL。
 * @return     无返回值；堆对象必须使用 XToolButton_delete_base。
 */
#define XToolButton_deinit_base(self)  XClass_deinit_base((XClass*)(self))

/**
 * @brief      释放工具按钮对象资源并按对象所有权删除其存储空间。
 * @param      self 由 XToolButton_create 系列返回的堆对象；可为 NULL。
 * @return     无返回值；栈对象请使用 XToolButton_deinit_base。
 */
#define XToolButton_delete_base(self)  XClass_delete_base((XClass*)(self))

/* ==================== 默认动作（对标 QToolButton） ==================== */

/**
 * @brief      查询默认动作（对标 QToolButton::defaultAction）。
 * @param      self 按钮对象借用指针；可为 NULL。
 * @return     默认动作借用指针；未设置或 self 为 NULL 时返回 NULL。
 */
XAction* XToolButton_defaultAction(const XToolButton* self);

/**
 * @brief      设置默认动作（对标 QToolButton::setDefaultAction）。
 * @details    动作的文本/可选中/选中/启用会镜像到按钮，按钮点击触发
 *             动作；动作变化（changed/toggled/enabledChanged）同步刷新
 *             按钮，动作销毁自动解绑。设置新动作会解除旧动作连接。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      action 目标动作借用指针；可为 NULL 表示解除关联。
 * @return     无返回值。
 */
void XToolButton_setDefaultAction(XToolButton* self, XAction* action);

/* ==================== 外观（对标 QToolButton） ==================== */

/**
 * @brief      查询工具按钮样式（对标 QToolButton::toolButtonStyle）。
 * @param      self 按钮对象借用指针；可为 NULL。
 * @return     样式枚举值；self 为 NULL 时返回 XToolButtonStyle_IconOnly。
 */
XToolButtonStyle XToolButton_toolButtonStyle(const XToolButton* self);

/**
 * @brief      设置工具按钮样式（对标 QToolButton::setToolButtonStyle）。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      style 目标样式枚举值。
 * @return     无返回值；样式变化时刷新尺寸提示与重绘。
 */
void XToolButton_setToolButtonStyle(XToolButton* self,
                                    XToolButtonStyle style);

/**
 * @brief      查询是否自动凸起（对标 QToolButton::autoRaise）。
 * @param      self 按钮对象借用指针；可为 NULL。
 * @return     自动凸起返回 true；self 为 NULL 时返回 false。
 */
bool XToolButton_autoRaise(const XToolButton* self);

/**
 * @brief      设置是否自动凸起（对标 QToolButton::setAutoRaise）。
 * @details    自动凸起时仅按下/悬停状态绘制边框，否则始终绘制边框。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      enable true 自动凸起，false 始终绘制边框。
 * @return     无返回值；变化时重绘。
 */
void XToolButton_setAutoRaise(XToolButton* self, bool enable);

/**
 * @brief      查询箭头类型（对标 QToolButton::arrowType）。
 * @param      self 按钮对象借用指针；可为 NULL。
 * @return     箭头类型枚举值；self 为 NULL 时返回 XToolButtonArrowType_NoArrow。
 */
XToolButtonArrowType XToolButton_arrowType(const XToolButton* self);

/**
 * @brief      设置箭头类型（对标 QToolButton::setArrowType）。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      type 目标箭头类型。
 * @return     无返回值；变化时重绘。
 */
void XToolButton_setArrowType(XToolButton* self, XToolButtonArrowType type);

/* ==================== 菜单（对标 QToolButton） ==================== */

/**
 * @brief      查询关联弹出菜单（对标 QToolButton::menu）。
 * @param      self 按钮对象借用指针；可为 NULL。
 * @return     关联菜单借用指针；未设置或 self 为 NULL 时返回 NULL。
 */
XMenu* XToolButton_menu(const XToolButton* self);

/**
 * @brief      设置关联弹出菜单（对标 QToolButton::setMenu）。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      menu 目标菜单借用指针；可为 NULL 表示解除关联。
 * @return     无返回值；变化时刷新尺寸提示与重绘。
 */
void XToolButton_setMenu(XToolButton* self, XMenu* menu);

/**
 * @brief      查询菜单弹出模式（对标 QToolButton::popupMode）。
 * @param      self 按钮对象借用指针；可为 NULL。
 * @return     弹出模式枚举值；self 为 NULL 时返回
 *             XToolButtonPopupMode_DelayedPopup。
 */
XToolButtonPopupMode XToolButton_popupMode(const XToolButton* self);

/**
 * @brief      设置菜单弹出模式（对标 QToolButton::setPopupMode）。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      mode 目标弹出模式。
 * @return     无返回值。
 */
void XToolButton_setPopupMode(XToolButton* self, XToolButtonPopupMode mode);

/**
 * @brief      显示关联弹出菜单（对标 QToolButton::showMenu）。
 * @details    仅当按钮启用且设置了菜单时执行：进入按下状态、弹出菜单，
 *             菜单关闭（aboutToHide）时恢复按下状态。本实现使用当前
 *             鼠标位置作为弹出位置。
 * @param      self 目标按钮对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
void XToolButton_showMenu(XToolButton* self);

/* ==================== 尺寸（对标 QToolButton） ==================== */

/**
 * @brief      计算按钮建议尺寸。
 * @param      self 按钮对象借用指针；可为 NULL。
 * @return     建议尺寸；self 为 NULL 时返回 (0, 0)。
 */
XSize XToolButton_sizeHint(const XToolButton* self);

/**
 * @brief      计算按钮最小建议尺寸。
 * @param      self 按钮对象借用指针；可为 NULL。
 * @return     最小建议尺寸；self 为 NULL 时返回 (0, 0)。
 */
XSize XToolButton_minimumSizeHint(const XToolButton* self);

/* ==================== 信号（对标 QToolButton signals） ==================== */

/**
 * @brief      发射 triggered(XAction*) 信号（对标 QToolButton::triggered）。
 * @details    默认动作被触发（含按钮点击触发）时转发。
 * @param      self 发射信号的按钮对象；可为 NULL。
 * @param      action 被触发的动作借用指针。
 * @return     不透明的 triggered 信号标识。
 */
void* XToolButton_triggered_signal(XToolButton* self, XAction* action);

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON */

#ifdef __cplusplus
}
#endif

#endif /* XTOOLBUTTON_H */
