/**
 * @file       XVirtualKeyboardSelectionListModel_Protected.h
 * @brief      XVirtualKeyboardSelectionListModel 保护头（类名级）：引
 *             擎装配入口与受保护槽。
 * @details    setDataSource 仅引擎 TU 调用（对标 Qt setDataSource 公共
 *             面但装配点在引擎）；selectionListChanged/
 *             selectionListActiveItemChanged 为数据源信号转发槽（Qt
 *             protected 槽）；dataSourceDestroyed 清数据源。创建入口
 *             create_engine 同样仅引擎 TU（Qt 私有构造口径）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDSELECTIONLISTMODEL_PROTECTED_H
#define XVIRTUALKEYBOARDSELECTIONLISTMODEL_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XVirtualKeyboardSelectionListModel.h"

#if XVIRTUALKEYBOARD_ON

/**
 * @brief      引擎 TU 专用创建入口（Qt 私有构造口径）。
 * @param      type 模型类型。
 * @return     新对象指针；失败返回 NULL。调用方用 *_delete_base 释放。
 */
XVirtualKeyboardSelectionListModel*
XVirtualKeyboardSelectionListModel_create_engine(
        XVirtualKeyboardSelectionListModelType type);

/**
 * @brief      初始化（栈对象路径；引擎创建入口内部使用）。
 * @param      self 待初始化对象。
 */
void XVirtualKeyboardSelectionListModel_init(
        XVirtualKeyboardSelectionListModel* self);

/**
 * @brief      装配数据源（仅引擎 TU 调用）。
 * @details    装配后立即同步行数缓存；置 NULL 解除并清零计数。
 * @param      self 模型对象；可为 NULL。
 * @param      dataSource 数据源输入法借用指针；可为 NULL。
 * @param      type 模型类型。
 */
void XVirtualKeyboardSelectionListModel_setDataSource(
        XVirtualKeyboardSelectionListModel* self,
        XVirtualKeyboardAbstractInputMethod* dataSource,
        XVirtualKeyboardSelectionListModelType type);

/**
 * @brief      数据源 selectionListChanged 转发槽（重新同步行数缓存，
 *             变化时发 countChanged；对标 protected 槽）。
 * @param      self 模型对象；可为 NULL。
 * @param      type 列表类型（非本模型类型时忽略）。
 */
void XVirtualKeyboardSelectionListModel_selectionListChanged(
        XVirtualKeyboardSelectionListModel* self, int type);

/**
 * @brief      数据源 selectionListActiveItemChanged 转发槽（更新高亮
 *             并发 activeItemChanged；对标 protected 槽）。
 */
void XVirtualKeyboardSelectionListModel_selectionListActiveItemChanged(
        XVirtualKeyboardSelectionListModel* self, int type, int index);

/**
 * @brief      数据源销毁通知槽（清数据源与计数；对标 protected 槽）。
 */
void XVirtualKeyboardSelectionListModel_dataSourceDestroyed(
        XVirtualKeyboardSelectionListModel* self);

/**
 * @brief      查询缓存的高亮候选下标（面板渲染消费；-1=无）。
 */
int XVirtualKeyboardSelectionListModel_activeItem(
        const XVirtualKeyboardSelectionListModel* self);

#endif /* XVIRTUALKEYBOARD_ON */
#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDSELECTIONLISTMODEL_PROTECTED_H */
