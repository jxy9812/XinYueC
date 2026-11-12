/**
 * @file       XToolBox_Protected.h
 * @brief      XToolBox 保护接口（仅供子类与内部实现使用）。
 * @details    本文件集中声明对标 Qt 6.8 QToolBox protected virtual
 *             （itemInserted/itemRemoved，qtoolbox.h:63-64）的虚槽唯一
 *             保护入口；`_base` 即分派入口（空/虚表守卫 + 一次查槽，
 *             2026-11-14 契约收口），缺省空操作在挂表的 static
 *             VX_toolBox_*；子类槽内回落基类口径用 XClass_Parent，不再
 *             有无后缀同名入口。普通应用代码不应直接包含本文件。
 * @note       本文件依赖 XToolBox.h；模块总开关关闭时以下接口全部裁剪。
 * @author     XinYueC 团队
 */
#ifndef XTOOLBOX_PROTECTED_H
#define XTOOLBOX_PROTECTED_H

#if XGUI_ON

#ifdef __cplusplus
extern "C" {
#endif

#include "XToolBox.h"

#if XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON

/** @brief itemInserted 虚槽唯一入口（对标 QToolBox::itemInserted；
 *         2026-11-14 契约收口：本入口即分派——空/虚表守卫 + 一次查槽，
 *         缺省空操作在本类挂表的 static VX_toolBox_itemInserted；子类
 *         槽内回落基类口径用 XClass_Parent(XToolBox,
 *         EXToolBox_ItemInserted, void(*)(XToolBox*, int))）。
 * @param self 目标工具箱；NULL 无操作。
 * @param index 新页索引。
 * @return 无返回值。
 */
void XToolBox_itemInserted_base(XToolBox* self, int index);

/** @brief itemRemoved 虚槽唯一入口（对标 QToolBox::itemRemoved；契约同
 *         itemInserted_base，回落用 XClass_Parent(XToolBox,
 *         EXToolBox_ItemRemoved, void(*)(XToolBox*, int))）。
 * @param self 目标工具箱；NULL 无操作。
 * @param index 被移除页的原索引。
 * @return 无返回值。
 */
void XToolBox_itemRemoved_base(XToolBox* self, int index);

#endif /* XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XTOOLBOX_PROTECTED_H */
