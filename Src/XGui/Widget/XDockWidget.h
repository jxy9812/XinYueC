/******************************************************************************
 * @file       XDockWidget.h
 * @brief      XDockWidget 停靠面板控件（对标 Qt 6.8 QDockWidget 全部
 *             公共 API）。
 * @details    功能范围：
 *             - setWidget/widget（内容控件，归 dock 所有）；
 *             - Features 位标志（Closable/Movable/Floatable/
 *               VerticalTitleBar，数值对齐）；
 *             - setFloating/isFloating（脱离宿主独立显示）；
 *             - setAllowedAreas/allowedAreas（XDockWidgetArea 位掩码）；
 *             - setTitleBarWidget/titleBarWidget（自定义标题条）；
 *             - toggleViewAction()（显示/隐藏切换动作）；
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
    XWidget* m_titleBar;     /**< 自定义标题条（借用）。 */
    char m_title[128];       /**< 标题文本。 */
} XDockWidget;

XVtable* XDockWidget_class_init(void);
void XDockWidget_init(XDockWidget* self, const char* utf8Title,
                      XWidget* parent, XWidgetFlags flags);
#define XDockWidget_create(title, parent, flags) XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (title), (parent), (flags))
XDockWidget* XDockWidget_create_ex(XMemoryType memory,
                                   const char* utf8Title,
                                   XWidget* parent, XWidgetFlags flags);
#define XDockWidget_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XDockWidget_delete_base(self) XClass_delete_base((XClass*)(self))

void XDockWidget_setWidget(XDockWidget* self, XWidget* widget);
XWidget* XDockWidget_widget(const XDockWidget* self);
void XDockWidget_setFeatures(XDockWidget* self, int features);
int XDockWidget_features(const XDockWidget* self);
void XDockWidget_setFloating(XDockWidget* self, bool floating);
bool XDockWidget_isFloating(const XDockWidget* self);
void XDockWidget_setAllowedAreas(XDockWidget* self, int areas);
int XDockWidget_allowedAreas(const XDockWidget* self);
void XDockWidget_setTitleBarWidget(XDockWidget* self, XWidget* widget);
XWidget* XDockWidget_titleBarWidget(const XDockWidget* self);
XAction* XDockWidget_toggleViewAction(const XDockWidget* self);

/* ==================== 信号 ==================== */

void* XDockWidget_featuresChanged_signal(XDockWidget* self, int features);
void* XDockWidget_topLevelChanged_signal(XDockWidget* self, bool topLevel);
void* XDockWidget_allowedAreasChanged_signal(XDockWidget* self, int areas);
void* XDockWidget_visibilityChanged_signal(XDockWidget* self, bool visible);

#endif /* XWIDGET_ON && XDOCKWIDGET_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDOCKWIDGET_H */