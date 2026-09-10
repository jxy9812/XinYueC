/**
 * @file       XFocusFrame.h
 * @brief      XFocusFrame 焦点框控件（对标 Qt 6.8 QFocusFrame 全部
 *             公共 API）。
 * @details    环绕目标控件绘制焦点指示框；setWidget 建立关联（控件为
 *             借用，归调用方）。继承 XWidget。
 * @note       模块总开关 XFOCUSFRAME_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XFOCUSFRAME_H
#define XFOCUSFRAME_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XFOCUSFRAME_ON

XCLASS_DEFINE_BEGING(XFocusFrame)
XCLASS_DEFINE_EXTEND_END(XFocusFrame, XWidget)

typedef struct XFocusFrame
{
    XWidget m_base;         /**< 基类成员；必须是第一个。 */
    XWidget* m_widget;      /**< 焦点目标控件（借用）。 */
} XFocusFrame;

XVtable* XFocusFrame_class_init(void);
void XFocusFrame_init(XFocusFrame* self, XWidget* parent, XWidgetFlags flags);
#define XFocusFrame_create(parent, flags) XFocusFrame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XFocusFrame* XFocusFrame_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);
#define XFocusFrame_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XFocusFrame_delete_base(self) XClass_delete_base((XClass*)(self))

/** @brief 建立焦点框与目标控件的关联（对标 setWidget）。 */
/**
 * @brief      设置内容控件（对标 setWidget）。
 */
void XFocusFrame_setWidget(XFocusFrame* self, XWidget* widget);
/** @brief 查询关联的目标控件（对标 widget()）。 */
/**
 * @brief      获取内容控件（对标 Qt 同名方法）。
 */
XWidget* XFocusFrame_widget(const XFocusFrame* self);

#endif /* XWIDGET_ON && XFOCUSFRAME_ON */

#ifdef __cplusplus
}
#endif
#endif /* XFOCUSFRAME_H */
