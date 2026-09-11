/**
 * @file       XAbstractScrollArea.h
 * @brief      XAbstractScrollArea 滚动区域基类（对标 Qt 6.8
 *             QAbstractScrollArea 全部公共 API）。
 * @details    功能范围：
 *             - viewport 子控件（内容承载区，调用方在 viewport 内放置
 *               内容或派生类重用）；
 *             - 垂直/水平滚动条（内部 XScrollBar，随策略显示/隐藏）；
 *             - 滚动条策略：XScrollBarPolicy（AsNeeded/AlwaysOff/
 *               AlwaysOn，数值对齐 Qt::ScrollBarPolicy）；
 *             - cornerWidget（右下角控件，第一版仅存储/挂接）；
 *             - 滚动条 value 变化驱动 viewport 内容滚动
 *               （scrollContentsBy 虚槽，派生类可重载）。
 * @note       模块总开关 XABSTRACTSCROLLAREA_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XABSTRACTSCROLLAREA_H
#define XABSTRACTSCROLLAREA_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XFrame.h"
#if XSCROLLBAR_ON
#include "XScrollBar.h"
#endif

#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON

/** @brief 滚动条显示策略（对标 Qt::ScrollBarPolicy，数值一致）。 */
typedef enum XScrollBarPolicy
{
    XScrollBarPolicy_AsNeeded = 0,   /**< 按需显示。 */
    XScrollBarPolicy_AlwaysOff = 1,  /**< 永不显示。 */
    XScrollBarPolicy_AlwaysOn = 2    /**< 永远显示。 */
} XScrollBarPolicy;

XCLASS_DEFINE_BEGING(XAbstractScrollArea)
XCLASS_DEFINE_ENUM(XAbstractScrollArea, ScrollContentsBy) = XCLASS_VTABLE_GET_SIZE(XFrame),
XCLASS_DEFINE_END(XAbstractScrollArea)

/**
 * @brief      XAbstractScrollArea 控件对象；m_base 必须是第一个成员。
 */
typedef struct XAbstractScrollArea
{
    XFrame m_base;             /**< 基类成员；必须是第一个。 */
    XWidget* m_viewport;       /**< 内容承载区子控件（拥有）。 */
    XScrollBar* m_vScrollBar;  /**< 垂直滚动条（拥有）。 */
    XScrollBar* m_hScrollBar;  /**< 水平滚动条（拥有）。 */
    int m_vPolicy;             /**< 垂直滚动条策略（默认 AsNeeded）。 */
    int m_hPolicy;             /**< 水平滚动条策略（默认 AsNeeded）。 */
    XWidget* m_cornerWidget;   /**< 右下角控件（借用）。 */
    int m_contentWidth;        /**< 内容宽度（驱动水平滚动范围）。 */
    int m_contentHeight;       /**< 内容高度（驱动垂直滚动范围）。 */
} XAbstractScrollArea;

/* ==================== 生命周期 ==================== */

XVtable* XAbstractScrollArea_class_init(void);
void XAbstractScrollArea_init(XAbstractScrollArea* self, XWidget* parent,
                              XWidgetFlags flags);
#define XAbstractScrollArea_create(parent, flags) XAbstractScrollArea_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XAbstractScrollArea* XAbstractScrollArea_create_ex(
    XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XAbstractScrollArea_deinit_base(self) XFrame_deinit_base((XFrame*)(self))
#define XAbstractScrollArea_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 视口与滚动条（对标公共 API） ==================== */

/** @brief 返回内容承载视口子控件（对标 viewport()）。 */
/**
 * @brief      获取视口。
 */
XWidget* XAbstractScrollArea_viewport(const XAbstractScrollArea* self);
/** @brief 返回垂直滚动条（对标 verticalScrollBar()）。 */
/**
 * @brief      获取垂直滚动条。
 */
XScrollBar* XAbstractScrollArea_verticalScrollBar(
    const XAbstractScrollArea* self);
/** @brief 返回水平滚动条（对标 horizontalScrollBar()）。 */
/**
 * @brief      获取水平滚动条。
 */
XScrollBar* XAbstractScrollArea_horizontalScrollBar(
    const XAbstractScrollArea* self);
/** @brief 设置垂直滚动条策略（对标 setVerticalScrollBarPolicy）。 */
/**
 * @brief      设置垂直滚动条策略。
 */
void XAbstractScrollArea_setVerticalScrollBarPolicy(
    XAbstractScrollArea* self, XScrollBarPolicy policy);
/** @brief 查询垂直滚动条策略。 */
/**
 * @brief      获取垂直滚动条策略。
 */
XScrollBarPolicy XAbstractScrollArea_verticalScrollBarPolicy(
    const XAbstractScrollArea* self);
/** @brief 设置水平滚动条策略（对标 setHorizontalScrollBarPolicy）。 */
/**
 * @brief      设置水平滚动条策略。
 */
void XAbstractScrollArea_setHorizontalScrollBarPolicy(
    XAbstractScrollArea* self, XScrollBarPolicy policy);
/** @brief 查询水平滚动条策略。 */
/**
 * @brief      获取水平滚动条策略。
 */
XScrollBarPolicy XAbstractScrollArea_horizontalScrollBarPolicy(
    const XAbstractScrollArea* self);
/** @brief 设置右下角控件（对标 setCornerWidget；控件归调用方）。 */
void XAbstractScrollArea_setCornerWidget(XAbstractScrollArea* self,
                                         XWidget* widget);
/** @brief 查询右下角控件（对标 cornerWidget()）。 */
/**
 * @brief      获取角落控件。
 */
XWidget* XAbstractScrollArea_cornerWidget(const XAbstractScrollArea* self);
/** @brief 设置内容尺寸（驱动滚动范围；内容经 viewport 承载时由派生类
 *         或调用方维护）。 */
/**
 * @brief      设置内容尺寸。
 */
void XAbstractScrollArea_setContentSize(XAbstractScrollArea* self,
                                        int width, int height);

/* ==================== 保护槽入口（对标 protected scrollContentsBy） ==== */

/** @brief 内容滚动槽：滚动条变化后由基类调用（dx/dy 为增量）。 */
void XAbstractScrollArea_scrollContentsBy_base(XAbstractScrollArea* self,
                                               int dx, int dy);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON */

#ifdef __cplusplus
}
#endif
void XAbstractScrollArea_addScrollBarWidget(XAbstractScrollArea* self, XWidget* widget);
int XAbstractScrollArea_sizeAdjustPolicy(const XAbstractScrollArea* self);
void XAbstractScrollArea_setSizeAdjustPolicy(XAbstractScrollArea* self, int policy);
void XAbstractScrollArea_setCornerWidget(XAbstractScrollArea* self, XWidget* widget);
XWidget* XAbstractScrollArea_cornerWidget(const XAbstractScrollArea* self);
int XAbstractScrollArea_maximumViewportSize_height(const XAbstractScrollArea* self);
void XAbstractScrollArea_setViewport(XAbstractScrollArea* self, XWidget* widget);
#endif /* XABSTRACTSCROLLAREA_H */
