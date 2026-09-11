/**
 * @file       XScrollArea.h
 * @brief      XScrollArea 滚动区域控件（对标 Qt 6.8 QScrollArea 全部
 *             公共 API）。
 * @details    功能范围：
 *             - 内容管理：setWidget（接管所有权并 reparent 到视口）/t
 *               akeWidget（取回所有权）/widget；
 *             - widgetResizable：true 时内容随视口自动缩放，false 时
 *               使用内容自身尺寸；
 *             - alignment（XAlignment 位掩码，存储；第一版排布仅支持
 *               左上/居中水平）；
 *             - ensureVisible(x, y, xmargin, ymargin)/
 *               ensureWidgetVisible(childWidget, xmargin, ymargin)
 *               （滚动到指定位置/子控件）；
 *             - 继承 XAbstractScrollArea 的视口/滚动条/策略 API。
 * @note       模块总开关 XSCROLLAREA_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XABSTRACTSCROLLAREA_ON。
 * @author     XinYueC 团队
 */
#ifndef XSCROLLAREA_H
#define XSCROLLAREA_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#if XABSTRACTSCROLLAREA_ON
#include "XAbstractScrollArea.h"
#endif

#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && XSCROLLAREA_ON

XCLASS_DEFINE_BEGING(XScrollArea)
XCLASS_DEFINE_EXTEND_END(XScrollArea, XAbstractScrollArea)

/**
 * @brief      XScrollArea 控件对象；m_base 必须是第一个成员。
 */
typedef struct XScrollArea
{
    XAbstractScrollArea m_base; /**< 基类成员；必须是第一个。 */
    XWidget* m_widget;          /**< 内容控件（借用；所有权随接管的
                                     父子关系，takeWidget 可取回）。 */
    bool m_resizable;           /**< 内容随视口缩放（默认 false）。 */
    int m_alignment;            /**< 对齐（XAlignment 位掩码，默认 0）。 */
} XScrollArea;

/* ==================== 生命周期 ==================== */

XVtable* XScrollArea_class_init(void);
void XScrollArea_init(XScrollArea* self, XWidget* parent,
                      XWidgetFlags flags);
#define XScrollArea_create(parent, flags) XScrollArea_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XScrollArea* XScrollArea_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);
#define XScrollArea_deinit_base(self) XAbstractScrollArea_deinit_base((XAbstractScrollArea*)(self))
#define XScrollArea_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 内容管理（对标 QScrollArea public API） ==================== */

/** @brief 设置内容控件（接管所有权并 reparent 到视口；对标 setWidget）。 */
/**
 * @brief      设置内容控件（对标 setWidget）。
 */
void XScrollArea_setWidget(XScrollArea* self, XWidget* widget);
/** @brief 取回内容控件所有权（parent 置 NULL；对标 takeWidget）。 */
/**
 * @brief      取回控件。
 */
XWidget* XScrollArea_takeWidget(XScrollArea* self);
/** @brief 查询内容控件（对标 widget()）。 */
/**
 * @brief      获取内容控件（对标 Qt 同名方法）。
 */
XWidget* XScrollArea_widget(const XScrollArea* self);
/** @brief 查询内容随视口缩放开关（默认 false）。 */
/**
 * @brief      获取内容可调整。
 */
bool XScrollArea_widgetResizable(const XScrollArea* self);
/** @brief 设置内容随视口缩放（对标 setWidgetResizable）。 */
/**
 * @brief      设置内容可调整。
 */
void XScrollArea_setWidgetResizable(XScrollArea* self, bool resizable);
/** @brief 查询内容对齐（对标 alignment()）。 */
/**
 * @brief      获取对齐方式。
 */
int XScrollArea_alignment(const XScrollArea* self);
/** @brief 设置内容对齐（XAlignment 位掩码；对标 setAlignment）。 */
/**
 * @brief      设置对齐方式。
 */
void XScrollArea_setAlignment(XScrollArea* self, int alignment);
/** @brief 滚动到内容坐标 (x,y)，保持边距（对标 ensureVisible）。 */
/**
 * @brief      确保内容可见。
 */
void XScrollArea_ensureVisible(XScrollArea* self, int x, int y,
                               int xmargin, int ymargin);
/** @brief 滚动到子控件可见（对标 ensureWidgetVisible）。 */
void XScrollArea_ensureWidgetVisible(XScrollArea* self, XWidget* childWidget,
                                     int xmargin, int ymargin);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON && XSCROLLAREA_ON */

#ifdef __cplusplus
}
#endif
void XScrollArea_viewportSizeHint(XScrollArea* self, int* w, int* h);
#endif /* XSCROLLAREA_H */
