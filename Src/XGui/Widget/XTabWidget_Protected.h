/**
 * @file       XTabWidget_Protected.h
 * @brief      XTabWidget 保护接口（仅供子类与内部实现使用）。
 * @details    本文件集中声明对标 Qt 6.8 QTabWidget protected virtual
 *             （tabInserted/tabRemoved，qtabwidget.h:128-129）的虚槽唯一
 *             公开/保护入口；`_base` 即分派入口（空/虚表守卫 + 一次查槽，
 *             2026-11-14 契约收口），缺省空操作在挂表的 static
 *             VXTabWidget_*；子类槽内回落基类口径用 XClass_Parent，不再
 *             有无后缀同名入口。普通应用代码不应直接包含本文件。
 * @note       本文件依赖 XTabWidget.h；模块总开关关闭时以下接口全部裁剪。
 * @author     XinYueC 团队
 */
#ifndef XTABWIDGET_PROTECTED_H
#define XTABWIDGET_PROTECTED_H

#if XGUI_ON

#ifdef __cplusplus
extern "C" {
#endif

#include "XTabWidget.h"

#if XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON

/** @brief tabInserted 虚槽唯一入口（对标 QTabWidget::tabInserted；
 *         2026-11-14 契约收口：本入口即分派——空/虚表守卫 + 一次查槽，
 *         缺省空操作在本类挂表的 static VXTabWidget_tabInserted；子类
 *         槽内回落基类口径用 XClass_Parent(XTabWidget,
 *         EXTabWidget_TabInserted, void(*)(XTabWidget*, int))）。
 * @param self 目标选项卡容器；NULL 无操作。
 * @param index 新页索引。
 * @return 无返回值。
 */
void XTabWidget_tabInserted_base(XTabWidget* self, int index);

/** @brief tabRemoved 虚槽唯一入口（对标 QTabWidget::tabRemoved；契约同
 *         tabInserted_base，回落用 XClass_Parent(XTabWidget,
 *         EXTabWidget_TabRemoved, void(*)(XTabWidget*, int))）。
 * @param self 目标选项卡容器；NULL 无操作。
 * @param index 被移除页的原索引。
 * @return 无返回值。
 */
void XTabWidget_tabRemoved_base(XTabWidget* self, int index);

#endif /* XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XTABWIDGET_PROTECTED_H */
