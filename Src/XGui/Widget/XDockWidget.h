/******************************************************************************
 * @file       XDockWidget.h
 * @brief      XDockWidget 停靠面板控件（对标 Qt 6.8 QDockWidget 全部
 *             公共 API）。
 * @details    功能范围：
 *             - setWidget/widget（内容控件，归 dock 所有）；
 *             - Features 位标志（Closable/Movable/Floatable/
 *               VerticalTitleBar，数值对齐）；
 *             - setFloating/isFloating（真实浮动：转独立顶层窗口，
 *               标题栏可拖动、可关闭；对标 QDockWidget 拖出主窗成
 *               独立顶层窗的行为）；
 *             - setAllowedAreas/allowedAreas（XDockWidgetArea 位掩码）；
 *             - setTitleBarWidget/titleBarWidget（自定义标题条；
 *               XGUI_CUSTOM_TITLEBAR_ON=1 时承载于父类控件级通用槽
 *               XWidget::m_titleBarWidget：设置=父类挂载语义+本类停靠
 *               钉位薄包装、查询宏映射 XWidget_titleBarWidget 纯读槽；
 *               =0 时保留本类自有成员与实现）；
 *             - toggleViewAction()（显示/隐藏切换动作，真实绑定
 *               visible 翻转并随显隐同步 checked）；
 *             - 信号：featuresChanged/topLevelChanged/
 *               allowedAreasChanged/visibilityChanged。
 * @note       模块总开关 XDOCKWIDGET_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XDOCKWIDGET_H
#define XDOCKWIDGET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XString.h"

#if XWIDGET_ON && XDOCKWIDGET_ON

/** @brief 停靠区域（对标 Qt::DockWidgetArea，数值一致）。 */
typedef enum XDockWidgetArea
{
    XDockWidgetArea_Left = 0x1,
    XDockWidgetArea_Right = 0x2,
    XDockWidgetArea_Top = 0x4,
    XDockWidgetArea_Bottom = 0x8,
    XDockWidgetArea_All = 0xf
} XDockWidgetArea;

XCLASS_DEFINE_BEGING(XDockWidget)
XCLASS_DEFINE_EXTEND_END(XDockWidget, XWidget)

typedef struct XDockWidget
{
    XWidget m_base;          /**< 基类成员；必须是第一个。 */
    XWidget* m_widget;       /**< 内容控件（借用，归 dock）。 */
    int m_features;          /**< 特性位标志（默认全开）。 */
    int m_allowedAreas;      /**< 允许停靠区域。 */
    bool m_floating;         /**< 浮动状态。 */
#if XGUI_CUSTOM_TITLEBAR_ON
    /* 自定义标题条由父类槽位承载：XWidget::m_titleBarWidget（借用，
     * 见 XWidget_setTitleBarWidget）；本类自有成员 m_titleBar 删除，
     * 内部消费点统一经 XWidget_titleBarWidget 读槽。 */
#else
    XWidget* m_titleBar;     /**< 自定义标题条（借用）。 */
#endif /* XGUI_CUSTOM_TITLEBAR_ON */
    XString* m_title;       /**< 标题文本（对象拥有）。 */
    XWidget* m_host;         /**< 宿主主窗口（借用；addDockWidget 登记，
                              *   对标 QDockWidget 回链 QMainWindowLayout）。 */
    XAction* m_toggleAction; /**< 显示/隐藏切换动作（拥有；toggleViewAction
                              *   惰性创建，对标 QDockWidget::toggleViewAction）。 */
    bool m_dragging;         /**< 浮动标题栏拖动进行中（内部状态）。 */
    XPoint m_dragOffset;     /**< 拖动抓取偏移（全局坐标 − 窗口左上角）。 */
    bool m_announcedVisible; /**< 上次广播 visibilityChanged 的值（去重）。 */
} XDockWidget;

/** @brief X停靠控件classinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XDockWidget_class_init(void);
void XDockWidget_init(XDockWidget* self, const char* utf8Title,
                      XWidget* parent, XWidgetFlags flags);
#define XDockWidget_create(title, parent, flags) XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (title), (parent), (flags))
XDockWidget* XDockWidget_create_ex(XMemoryType memory,
                                   const char* utf8Title,
                                   XWidget* parent, XWidgetFlags flags);
#define XDockWidget_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XDockWidget_delete_base(self) XClass_delete_base((XClass*)(self))

/** @brief X停靠控件set控件（对标 Qt 同名接口）。
 * @details 对标 Qt setWidget 的替换语义：首次设置装入内容并立即摆到
 *          标题条以下全部区域；已有内容时二次调用先摘除旧控件（脱离本
 *          面板父链、不删除，所有权转移给调用方，对标 Qt 旧 widget 由
 *          调用方管理），再装入新控件。内容矩形随后随面板停靠/浮动/
 *          缩放恒跟随（标题条以下全部区域，经 ResizeEvent 重摆）。
 * @param self 目标控件指针。
 * @param widget 子控件指针；传入与当前内容相同的指针时忽略（对齐 Qt
 *               setWidget 的同指针幂等）；可为 NULL 表示摘除当前内容。
 * @return 无返回值。
 */
void XDockWidget_setWidget(XDockWidget* self, XWidget* widget);
/** @brief X停靠控件widget（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWidget* XDockWidget_widget(const XDockWidget* self);
/** @brief X停靠控件setFeatures（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param features int 参数。
 * @return 无返回值。
 */
void XDockWidget_setFeatures(XDockWidget* self, int features);
/** @brief X停靠控件features（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XDockWidget_features(const XDockWidget* self);
/** @brief X停靠控件setFloating（对标 Qt QDockWidget::setFloating）。
 * @details floating=true 时面板脱离宿主主窗口布局，转成独立顶层窗口
 *          （对标 Qt：拖出主窗成独立顶层窗；保留当前尺寸并映射全局位置，
 *          顶层标题栏显示面板标题，可见时激活窗口）；floating=false 时
 *          若登记过宿主则挂回宿主，几何交还主窗口停靠布局。状态变化时
 *          真发射 topLevelChanged(bool)。
 * @param self 目标控件指针。
 * @param floating bool 参数。
 * @return 无返回值。
 */
void XDockWidget_setFloating(XDockWidget* self, bool floating);
/** @brief X停靠控件isFloating（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XDockWidget_isFloating(const XDockWidget* self);
/** @brief X停靠控件setAllowedAreas（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param areas int 参数。
 * @return 无返回值。
 */
void XDockWidget_setAllowedAreas(XDockWidget* self, int areas);
/** @brief X停靠控件allowedAreas（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XDockWidget_allowedAreas(const XDockWidget* self);
/** @brief 查询指定停靠区是否允许（对标 QDockWidget::isAreaAllowed）。
 * @param self 目标控件指针；传入 NULL 时返回 false。
 * @param area 停靠区位掩码（XDockWidgetArea 单个位）。
 * @return area 在 allowedAreas 位掩码内返回 true。
 */
bool XDockWidget_isAreaAllowed(const XDockWidget* self, int area);
#if XGUI_CUSTOM_TITLEBAR_ON
/* 设置为本类薄包装（父类挂载 + 停靠侧挂载即钉位，见下方函数注）；
 * 查询为纯读槽转发：继承父类已有 API 直接宏复用，不写 C 转发包装
 * （风格指南）。借用契约（借用不拥有、释放责任归创建方、旧条解挂不
 * 释放）由父类实现统一承载，消费分层见 XWidget_setTitleBarWidget：
 * 本面板即「XDockWidget=停靠标题条」层。 */
#define XDockWidget_titleBarWidget(self) XWidget_titleBarWidget((const XWidget*)(self))
#else
/* 宏关分支：本类自有成员 m_titleBar 与自有实现（行为与改造前逐行等价）。 */
#endif /* XGUI_CUSTOM_TITLEBAR_ON */
/** @brief X停靠控件set标题条控件（对标 Qt QDockWidget::setTitleBarWidget）。
 * @details XGUI_CUSTOM_TITLEBAR_ON=1 时为本类薄包装：挂载/解挂/借用语义
 *          统一交父类 XWidget_setTitleBarWidget（控件级通用槽），随后补
 *          停靠侧摆位——挂载即把条钉满标题条区（父类只挂载不摆位，摆位
 *          归消费方；不等待面板下一次 resizeEvent）；=0 时为自有实现。
 *          传 NULL 摘除自定义条恢复内置标题条。
 * @param self 目标控件指针。
 * @param widget 新标题条控件借用指针；不转移所有权，可为 NULL。
 * @return 无返回值。
 */
void XDockWidget_setTitleBarWidget(XDockWidget* self, XWidget* widget);
#if !XGUI_CUSTOM_TITLEBAR_ON
/** @brief X停靠控件title条控件（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWidget* XDockWidget_titleBarWidget(const XDockWidget* self);
#endif /* !XGUI_CUSTOM_TITLEBAR_ON */
/** @brief X停靠控件toggleView动作（对标 QDockWidget::toggleViewAction）。
 * @details 返回惰性创建的显示/隐藏切换动作：可选中，checked 随面板显隐
 *          同步（对标 Qt 的 syncViewAction），triggered 时翻转面板可见性；
 *          动作归面板所有（随面板析构释放）。面板显隐由 show/hide 事件
 *          驱动 visibilityChanged(bool) 真发射，并经宿主回链触发主窗口
 *          停靠区重排。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
XAction* XDockWidget_toggleViewAction(XDockWidget* self);

/* ==================== 信号 ==================== */

void* XDockWidget_featuresChanged_signal(XDockWidget* self, int features);
/** @brief X停靠控件topLevel变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @param topLevel bool 参数。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XDockWidget_topLevelChanged_signal(XDockWidget* self, bool topLevel);
/** @brief X停靠控件allowedAreas变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @param areas int 参数。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XDockWidget_allowedAreasChanged_signal(XDockWidget* self, int areas);
/** @brief X停靠控件visibility变更 信号地址（发射经 XObject_emitSignal）。
 * @details 显隐由 showEvent/hideEvent 驱动真发射（对标 QDockWidget::
 *          visibilityChanged 随真实可见状态变化），并同步
 *          toggleViewAction 的 checked 位。
 * @param self 目标控件指针。
 * @param visible bool：true 可见。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XDockWidget_visibilityChanged_signal(XDockWidget* self, bool visible);
/** @brief dockLocationChanged(int) 信号（对标 QDockWidget::dockLocationChanged；
 *         载荷：停靠区域码。TODO：拖拽重停靠落地后由主窗口布局在
 *         区域变化时真发射；当前仅保留信号标识）。 */
void* XDockWidget_dockLocationChanged_signal(XDockWidget* self, int area);

#endif /* XWIDGET_ON && XDOCKWIDGET_ON */

#endif /* XDOCKWIDGET_H */