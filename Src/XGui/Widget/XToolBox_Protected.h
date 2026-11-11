/**
 * @file       XToolBox_Protected.h
 * @brief      XToolBox 保护接口（仅供子类与内部实现使用）。
 * @details    本文件集中声明对标 Qt 6.8 QToolBox protected virtual
 *             （itemInserted/itemRemoved，qtoolbox.h:63-64）的基类入口；
 *             子类重载虚槽 EXToolBox_ItemInserted / EXToolBox_ItemRemoved
 *             后，需要基类口径时经本文件的 `_base` 入口取（直调本类实现，
 *             不经对象虚表，重载无递归）。普通应用代码不应直接包含本文件。
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

/** @brief itemInserted 的基类缺省实现（空操作）。
 * @param self 目标工具箱；NULL 无操作。
 * @param index 新页索引。
 * @return 无返回值。
 */
void XToolBox_itemInserted_base(XToolBox* self, int index);

/** @brief itemRemoved 的基类缺省实现（空操作）。
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
