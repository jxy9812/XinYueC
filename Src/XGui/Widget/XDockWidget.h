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
    XWidget* m_titleBar;     /**< 自定义标题条（借用）。 */
    XString* m_title;       /**< 标题文本（对象拥有）。 */
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
 * @param self 目标控件指针。
 * @param widget 子控件指针。
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
/** @brief X停靠控件setFloating（对标 Qt 同名接口）。
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
/** @brief X停靠控件set标题条控件（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param widget 子控件指针。
 * @return 无返回值。
 */
void XDockWidget_setTitleBarWidget(XDockWidget* self, XWidget* widget);
/** @brief X停靠控件title条控件（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWidget* XDockWidget_titleBarWidget(const XDockWidget* self);
/** @brief X停靠控件toggleView动作（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
XAction* XDockWidget_toggleViewAction(const XDockWidget* self);

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
 * @param self 目标控件指针。
 * @param visible bool：true 可见。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XDockWidget_visibilityChanged_signal(XDockWidget* self, bool visible);

#endif /* XWIDGET_ON && XDOCKWIDGET_ON */

#ifdef __cplusplus
}
#endif

/** @brief X停靠控件dockLocation变更 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XDockWidget_dockLocationChanged_signal(XDockWidget* self);
/** @brief X停靠控件set标题条控件2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XDockWidget_setTitleBarWidget_2(XDockWidget* self);
/** @brief X停靠控件title条控件2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XDockWidget_titleBarWidget_2(XDockWidget* self);
/** @brief X停靠控件set控件2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XDockWidget_setWidget_2(XDockWidget* self);
/** @brief X停靠控件widget2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XDockWidget_widget_2(XDockWidget* self);
/** @brief X停靠控件setFeatures2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XDockWidget_setFeatures_2(XDockWidget* self);
/** @brief X停靠控件features2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XDockWidget_features_2(XDockWidget* self);
/** @brief X停靠控件setFloating2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XDockWidget_setFloating_2(XDockWidget* self);
/** @brief X停靠控件isFloating2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XDockWidget_isFloating_2(XDockWidget* self);
/** @brief X停靠控件setAllowedAreas2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XDockWidget_setAllowedAreas_2(XDockWidget* self);
/** @brief X停靠控件allowedAreas2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XDockWidget_allowedAreas_2(XDockWidget* self);
#endif /* XDOCKWIDGET_H */