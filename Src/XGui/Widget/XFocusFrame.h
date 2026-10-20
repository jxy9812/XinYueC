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

#if XGUI_ON
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
    /* [内存治理 2026-10-15] 几何跟随承载：轮询定时器 + 目标上次几何
       （文件头 @details：框架内部几何事件不经 notify 分发，Qt 的
       eventFilter 跟随以低频轮询轻量等价）。小 px 值收窄整型。 */
    XTimerId m_followTimer; /**< 几何跟随轮询定时器（XTIMER_INVALID_ID=无）。 */
    int16_t m_lastX;        /**< 目标上次几何 x（变化检测）。 */
    int16_t m_lastY;        /**< 目标上次几何 y。 */
    uint16_t m_lastW;       /**< 目标上次几何宽。 */
    uint16_t m_lastH;       /**< 目标上次几何高。 */
} XFocusFrame;

XVtable* XFocusFrame_class_init(void);
void XFocusFrame_init(XFocusFrame* self, XWidget* parent, XWidgetFlags flags);
#define XFocusFrame_create(parent, flags) XFocusFrame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XFocusFrame* XFocusFrame_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);

/** @brief 建立焦点框与目标控件的关联（对标 setWidget）。
 * @details 对标 Qt 几何跟踪（qfocusframe.cpp:149-205、243-300）：
 *          框体挂到目标的父控件上、贴合目标矩形四周外扩 2px 并浮于
 *          其上；随目标 Move/Resize 贴合、Show/Hide 同步框显隐（本框
 *          架以 30ms 轮询定时器轻量等价 Qt 的 eventFilter，见实现文
 *          件头 @details）。目标为顶层窗口时不跟踪（Qt 同：widget 置
 *          空并隐藏）；目标销毁时经 destroyed 连接自动解除。传 NULL
 *          解除关联并隐藏框体。
 */
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

#endif /* XGUI_ON */

#endif /* XFOCUSFRAME_H */
