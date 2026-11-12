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
 *             点击动作触发顺序对齐 Qt：有默认动作时按钮自身不翻转，
 *             改由 nextCheckState 保护槽触发动作（动作承担状态翻转），
 *             checkStateSet 把程序化 setChecked 回写动作；因此按钮
 *             triggered(action) 落在 released/clicked 之前
 *             （Qt 6.8 qtoolbutton.cpp:971-990 + qabstractbutton.cpp:344-375，
 *             本机探针实测顺序 pressed → toggled → action.toggled →
 *             action.triggered → released → clicked）。
 * @note       模块总开关 XTOOLBUTTON_ON 定义于 XGuiConfig.h；关闭时裁剪
 *             本头文件全部公共声明。XToolButton 依赖 XABSTRACTBUTTON_ON
 *             与 XMENU_ON（菜单功能）。对象字段由 XClass 生命周期管理。
 * @note       按项目约束，QToolButton 的 QKeySequence 快捷键、样式表与
 *             tear-off 平台菜单不在本类提供，作为裁剪边界在 XGui.md 记录；
 *             菜单指示区宽度走 style()->pixelMetric(PM_MenuButtonIndicator)
 *             （qcommonstyle.cpp:4246-4260，本库缺省 12），sizeHint 只在
 *             **MenuButtonPopup 且有菜单**时预留（qtoolbutton.cpp:333-336；
 *             本机 Qt 探针：无菜单 27 / DelayedPopup 27 / MenuButtonPopup
 *             39；arrowType 不加宽）。“有菜单”按 Qt hasMenu 计
 *             （qtoolbutton.cpp:73-78）：setMenu 或 defaultAction->menu()
 *             任一存在；Qt 的 actions.size() 分支需要「动作列表 ⇒ 临时
 *             QMenu」的阻塞弹出生命周期，本库 XMenu_popup 为非阻塞，
 *             暂不登记（不造有箭头不弹层的假功能）。
 * @author     XinYueC 团队
 */
#ifndef XTOOLBUTTON_H
#define XTOOLBUTTON_H

#if XGUI_ON

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
 * @details    [P1-07 2026-11-13] 改名为 Qt 语义的裸 X 名 + 完整值名。
 *             命名规则（本批 P1-08/P1-09 同步确立）：Qt 的**全局**枚举
 *             （Qt::Orientation/Qt::Alignment/Qt::ArrowType）→ `X+枚举名`
 *             不带宿主（先例 XGuiOrientation.h→XGuiConfig.h、XAlignment.h）；
 *             宿主**类内**枚举（QToolButton::ToolButtonPopupMode）→
 *             `X+宿主+枚举`。故本条归 `XArrowType`，同文件
 *             XToolButtonStyle/XToolButtonPopupMode/XToolButtonTextPosition
 *             保持宿主前缀不变。
 */
typedef enum XArrowType
{
    XArrowType_NoArrow = 0,     /**< 无箭头（对标 Qt::NoArrow）。 */
    XArrowType_UpArrow = 1,     /**< 上箭头（对标 Qt::UpArrow）。 */
    XArrowType_DownArrow = 2,   /**< 下箭头（对标 Qt::DownArrow）。 */
    XArrowType_LeftArrow = 3,   /**< 左箭头（对标 Qt::LeftArrow）。 */
    XArrowType_RightArrow = 4   /**< 右箭头（对标 Qt::RightArrow）。 */
} XArrowType;

typedef XArrowType XArrowType;
#define XArrowType_NoArrow  XArrowType_NoArrow
#define XArrowType_Up       XArrowType_UpArrow
#define XArrowType_Down     XArrowType_DownArrow
#define XArrowType_Left     XArrowType_LeftArrow
#define XArrowType_Right    XArrowType_RightArrow

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
    uint8_t m_toolButtonStyle;   /**< 工具按钮样式。 */
    uint8_t m_arrowType;     /**< 箭头类型。 */
    uint8_t m_popupMode;     /**< 菜单弹出模式。 */
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
 *             对象由调用方拥有，必须使用 XClassDelete 释放。
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
 * @return     无返回值；堆对象必须使用 XClassDelete。
 */

/**
 * @brief      释放工具按钮对象资源并按对象所有权删除其存储空间。
 * @param      self 由 XToolButton_create 系列返回的堆对象；可为 NULL。
 * @return     无返回值；栈对象请使用 XClassDeinit。
 */

/* ==================== 默认动作（对标 QToolButton） ==================== */

/**
 * @brief      查询默认动作（对标 QToolButton::defaultAction）。
 * @param      self 按钮对象借用指针；可为 NULL。
 * @return     默认动作借用指针；未设置或 self 为 NULL 时返回 NULL。
 */
XAction* XToolButton_defaultAction(const XToolButton* self);

/**
 * @brief      设置默认动作（对标 QToolButton::setDefaultAction）。
 * @details    动作的 `iconText`（未设时回落 `text` 并把 '&' 双写）、图标、
 *             `toolTip`/`statusTip`/`whatsThis`、可选中/选中/启用全部
 *             镜像到按钮（qtoolbutton.cpp:922-955），按钮点击触发动作；
 *             动作变化（changed/toggled/enabledChanged）同步刷新按钮，
 *             动作销毁自动解绑。设置新动作会解除旧动作连接。
 *             动作**带菜单且按钮原先没有菜单**时自动切
 *             `popupMode = MenuButtonPopup`（qtoolbutton.cpp:941-947）。
 * @note       `font` 镜像不做（XAction 无字体 API）；文本源由 text 改为
 *             iconText 属可见变化：只设 iconText 的动作，按钮标题取
 *             iconText（对齐 Qt）。
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
 * @details    缺省值与 Qt 同（QToolButtonPrivate::init，qtoolbutton.cpp:171-178）：
 *             **构造时父控件是 XToolBar ⇒ true**（工具栏内按钮自动凸起），
 *             其它父控件（含 NULL）⇒ false。此后以 setAutoRaise 的显式
 *             设置为准。
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
 * @return     箭头类型枚举值（XArrowType）；self 为 NULL 时返回
 *             XArrowType_NoArrow。
 */
XArrowType XToolButton_arrowType(const XToolButton* self);

/**
 * @brief      设置箭头类型（对标 QToolButton::setArrowType）。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      type 目标箭头类型（XArrowType）。
 * @return     无返回值；变化时重绘。
 */
void XToolButton_setArrowType(XToolButton* self, XArrowType type);

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
 * @brief      显示关联弹出菜单并阻塞至其关闭（对标 QToolButton::showMenu）。
 * @details    仅当按钮启用且关联了菜单时执行：进入按下状态、在按钮正下方
 *             弹出菜单并**阻塞**——经 XMenu_exec_2 进入嵌套事件循环，
 *             菜单被用户关闭后才返回（qtoolbutton.cpp:659-661「This
 *             function does not return until the popup menu has been
 *             closed by the user」；P0-15，弹出位置为按钮左下方全局逻辑
 *             坐标，非旧注释所称「当前鼠标位置」）。菜单关闭
 *             （aboutToHide）时恢复按下状态。菜单已弹出（popupActive）
 *             期间的再入直接返回（对标 menuButtonDown 防再旋一层事件
 *             循环）。
 * @param      self 目标按钮对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
void XToolButton_showMenu(XToolButton* self);

/* ==================== 尺寸（对标 QToolButton） ==================== */

/* sizeHint/minimumSizeHint 是 XWidget 虚函数（对标 QToolButton::
 * sizeHint 覆写；minimumSizeHint 恒等 sizeHint）：业务体在
 * XToolButton.c 的 static VXToolButton_sizeHint/minimumSizeHint 槽内
 * 并经 class_init 注册虚表，对外统一走基类 `*_base` 入口虚分派，
 * 本类不再另设无后缀公开函数（《api命名风格》`*_base` 契约）。 */
#define XToolButton_sizeHint_base(self) \
    XWidget_sizeHint_base((const XWidget*)(self))
#define XToolButton_minimumSizeHint_base(self) \
    XWidget_minimumSizeHint_base((const XWidget*)(self))

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

#endif /* XGUI_ON */

#endif /* XTOOLBUTTON_H */
