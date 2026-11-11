/**
 * @file       XTabWidget_Protected.h
 * @brief      XTabWidget 保护接口（仅供子类与内部实现使用）。
 * @details    本文件集中声明对标 Qt 6.8 QTabWidget protected virtual
 *             （tabInserted/tabRemoved，qtabwidget.h:128-129）的基类入口；
 *             子类重载虚槽 EXTabWidget_TabInserted / EXTabWidget_TabRemoved
 *             后，需要基类口径时经本文件的 `_base` 入口取（直调本类实现，
 *             不经对象虚表，重载无递归）。普通应用代码不应直接包含本文件。
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

/** @brief tabInserted 的基类缺省实现（空操作）。
 * @param self 目标选项卡容器；NULL 无操作。
 * @param index 新页索引。
 * @return 无返回值。
 */
void XTabWidget_tabInserted_base(XTabWidget* self, int index);

/** @brief tabRemoved 的基类缺省实现（空操作）。
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
