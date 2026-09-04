/******************************************************************************
 * @file       XLayoutItem_Protected.h
 * @brief      XLayoutItem 基类保护接口（仅供子类与内部实现使用）。
 * @details    对标 Qt 6.8 QLayoutItem：widget()、layout()、controlTypes()、
 *             spacerItem() 在 qlayoutitem.h 中属于 protected 虚函数，
 *             故对应的 xxx_base 入口只暴露给子类与布局引擎内部使用，
 *             不进入公共头文件 XLayoutItem.h。
 ******************************************************************************/
#ifndef XLAYOUTITEM_PROTECTED_H
#define XLAYOUTITEM_PROTECTED_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XLayoutItem.h"

#if XLAYOUT_ON

/** @brief 返回条目承载的控件（对标 QLayoutItem::widget；protected）。 */
XWidget* XLayoutItem_widget_base(const XLayoutItem* self);

/** @brief 返回条目承载的子布局（对标 QLayoutItem::layout；protected）。 */
XLayout* XLayoutItem_layout_base(const XLayoutItem* self);

/** @brief 返回条目包含的控件类型位集（对标 QSizePolicy::controlTypes；protected）。 */
XWidgetSizePolicyControlTypes XLayoutItem_controlTypes_base(const XLayoutItem* self);

/** @brief 返回条目对应的空白条目（对标 QLayoutItem::spacerItem；protected）。 */
XSpacerItem* XLayoutItem_spacerItem_base(const XLayoutItem* self);

#ifdef __cplusplus
}
#endif
#endif /* XLAYOUT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XLAYOUTITEM_PROTECTED_H */
