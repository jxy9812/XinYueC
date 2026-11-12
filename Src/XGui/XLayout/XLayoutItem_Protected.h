/******************************************************************************
 * @file       XLayoutItem_Protected.h
 * @brief      XLayoutItem 基类保护接口（仅供子类与内部实现使用）。
 * @details    对标 Qt 6.8 QLayoutItem：widget()、layout()、controlTypes()、
 *             spacerItem() 在 qlayoutitem.h 中均为 public 虚函数（此前
 *             注释误写为 protected）；这些公开查询接口按约定应声明于
 *             公共头文件 XLayoutItem.h，暂留本头供子类与布局引擎内部
 *             使用，待统一迁移。
 ******************************************************************************/
#ifndef XLAYOUTITEM_PROTECTED_H
#define XLAYOUTITEM_PROTECTED_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include "XLayoutItem.h"

#if XLAYOUT_ON

/**
 * @brief      返回条目承载的控件（对标 QLayoutItem::widget）。
 * @param      self 目标条目；可为 NULL。
 * @return     控件借用指针（不转移所有权，释放责任在控件属主，调用方
 *             不得释放）；非控件条目或失败返回 NULL。
 */
XWidget* XLayoutItem_widget_base(const XLayoutItem* self);

/**
 * @brief      返回条目承载的子布局（对标 QLayoutItem::layout）。
 * @param      self 目标条目；可为 NULL。
 * @return     子布局借用指针（布局条目返回其自身，不转移所有权，调用
 *             方不得释放）；非布局条目或失败返回 NULL。
 */
XLayout* XLayoutItem_layout_base(const XLayoutItem* self);

/* ==================== 缓存化尺寸分派（布局递归内部专用） ==================== */

/* [2026-11-13 _base 契约整改] 三级尺寸度量的 XLayout 缓存（2026-10-17
 * 设计）从 `*_base` 调度入口剥离：`*_base` 按《api命名风格》契约收敛为
 * 单行虚表转发；缓存化分派是**普通保护工具函数**（非虚函数入口），
 * 供布局递归内部（XLayout/XBoxLayout/XGridLayout 的 totalHint/Min/Max
 * 与子布局槽体）使用——resize 级联中同一布局被反复查询，缓存把每步
 * N 次整树遍历收敛为 1 次。外部单点查询直接走 `*_base`（Qt 语义：
 * QLayoutItem 层虚调用无缓存，缓存属于 QLayout::sizeHint() 覆写体内，
 * 本框架对应本组 cached 函数）。非布局条目（控件/空白）不缓存。 */

/**
 * @brief      首选尺寸的缓存化分派：先查所属布局缓存（-1 哨兵=未缓存），
 *             未命中经虚表分派并写回。失效清理由 XLayout_invalidate 承担。
 * @param      self 目标条目；可为 NULL。
 * @return     首选尺寸。
 */
XSize XLayoutItem_cachedSizeHint(const XLayoutItem* self);

/**
 * @brief      最小尺寸的缓存化分派（语义同上）。
 * @param      self 目标条目；可为 NULL。
 * @return     最小尺寸。
 */
XSize XLayoutItem_cachedMinimumSize(const XLayoutItem* self);

/**
 * @brief      最大尺寸的缓存化分派（语义同上）。
 * @param      self 目标条目；可为 NULL。
 * @return     最大尺寸。
 */
XSize XLayoutItem_cachedMaximumSize(const XLayoutItem* self);

/**
 * @brief      返回条目包含的控件类型位集（对标 QSizePolicy::controlTypes）。
 * @param      self 目标条目；可为 NULL。
 * @return     XWidgetSizePolicyControlTypes 位集；失败返回
 *             XWidgetSizePolicyControl_DefaultType。
 */
XWidgetSizePolicyControlTypes XLayoutItem_controlTypes_base(const XLayoutItem* self);

/**
 * @brief      返回条目对应的空白条目（对标 QLayoutItem::spacerItem）。
 * @param      self 目标条目；可为 NULL。
 * @return     空白条目借用指针（类型安全下转，不转移所有权，调用方不
 *             得释放）；非空白条目或失败返回 NULL。
 */
XSpacerItem* XLayoutItem_spacerItem_base(const XLayoutItem* self);

#endif /* XLAYOUT_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XLAYOUTITEM_PROTECTED_H */
