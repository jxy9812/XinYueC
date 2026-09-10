/**
 * @file       XStackedWidget.h
 * @brief      XStackedWidget 堆叠页面容器控件（对标 Qt 6.8
 *             QStackedWidget 全部公共 API）。
 * @details    功能范围：
 *             - 页面管理：addWidget/insertWidget/removeWidget/count；
 *             - 当前页：currentIndex/currentWidget/setCurrentIndex/
 *               setCurrentWidget/widget/indexOf；
 *             - 信号：currentChanged(int)/widgetRemoved(int)
 *               （转发内部 XStackedLayout 信号）；
 *             - 内部使用 XStackedLayout 承载页面（对标 QStackedWidget
 *               与 QStackedLayout 的组合关系）；
 *             - removeWidget 后页面控件归还调用方（布局仅放弃条目）。
 * @note       模块总开关 XSTACKEDWIDGET_ON 定义于 XGuiConfig.h；=0 时
 *             裁剪全部公共 API。依赖 XWIDGET_ON、XFRAME_ON、
*              XLAYOUT_ON、XLAYOUT_STACKED_ON。
 * @author     XinYueC 团队
 */
#ifndef XSTACKEDWIDGET_H
#define XSTACKEDWIDGET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XFrame.h"
#if XLAYOUT_ON && XLAYOUT_STACKED_ON
#include "XStackedLayout.h"
#endif

#if XWIDGET_ON && XFRAME_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON && XSTACKEDWIDGET_ON

XCLASS_DEFINE_BEGING(XStackedWidget)
XCLASS_DEFINE_EXTEND_END(XStackedWidget, XFrame)

/**
 * @brief      XStackedWidget 控件对象；m_base 必须是第一个成员。
 */
typedef struct XStackedWidget
{
    XFrame m_base;            /**< 基类成员；必须是第一个。 */
    XStackedLayout m_layout;  /**< 内部堆叠布局（拥有）。 */
} XStackedWidget;

/* ==================== 生命周期 ==================== */

XVtable* XStackedWidget_class_init(void);
void XStackedWidget_init(XStackedWidget* self, XWidget* parent,
                         XWidgetFlags flags);
#define XStackedWidget_create(parent, flags) XStackedWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XStackedWidget* XStackedWidget_create_ex(XMemoryType memory, XWidget* parent,
                                         XWidgetFlags flags);
#define XStackedWidget_deinit_base(self) XFrame_deinit_base((XFrame*)(self))
#define XStackedWidget_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 页面管理（对标 QStackedWidget public API） ==================== */

/** @brief 追加页面控件并返回其索引（对标 addWidget）。 */
/**
 * @brief      添加控件。
 */
int XStackedWidget_addWidget(XStackedWidget* self, XWidget* widget);
/** @brief 在 index 处插入页面控件并返回实际索引
 *         （对标 insertWidget；index 越界时等价追加）。 */
/**
 * @brief      插入控件。
 */
int XStackedWidget_insertWidget(XStackedWidget* self, int index,
                                XWidget* widget);
/** @brief 移除页面控件（控件归还调用方；对标 removeWidget）。 */
/**
 * @brief      移除控件。
 */
void XStackedWidget_removeWidget(XStackedWidget* self, XWidget* widget);
/** @brief 查询当前页索引（无页面时 -1）。 */
int XStackedWidget_currentIndex(const XStackedWidget* self);
/** @brief 查询当前页控件（无页面时 NULL）。 */
/**
 * @brief      获取当前控件。
 */
XWidget* XStackedWidget_currentWidget(const XStackedWidget* self);
/** @brief 查询页面控件的索引；未找到返回 -1。 */
int XStackedWidget_indexOf(const XStackedWidget* self, const XWidget* widget);
/** @brief 查询指定索引的页面控件；越界返回 NULL。 */
/**
 * @brief      获取内容控件（对标 Qt 同名方法）。
 */
XWidget* XStackedWidget_widget(const XStackedWidget* self, int index);
/** @brief 查询页面数量。 */
int XStackedWidget_count(const XStackedWidget* self);

/* ==================== 当前页槽 ==================== */

/** @brief 设置当前页索引（越界忽略；对标 setCurrentIndex）。 */
void XStackedWidget_setCurrentIndex(XStackedWidget* self, int index);
/** @brief 设置指定控件为当前页（非本容器页面忽略；对标
 *         setCurrentWidget）。 */
void XStackedWidget_setCurrentWidget(XStackedWidget* self, XWidget* widget);

/* ==================== 信号转发 ==================== */

/** @brief 当前页切换信号（对标 currentChanged(int)）。 */
/**
 * @brief      当前项变化信号（真发射）。
 */
void* XStackedWidget_currentChanged_signal(XStackedWidget* self, int index);
/** @brief 页面移除信号（对标 widgetRemoved(int)）。 */
void* XStackedWidget_widgetRemoved_signal(XStackedWidget* self, int index);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XFRAME_ON && XLAYOUT_ON && XLAYOUT_STACKED_ON && XSTACKEDWIDGET_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSTACKEDWIDGET_H */
