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
