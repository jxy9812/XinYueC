/**
 * @file       XAbstractScrollArea_Protected.h
 * @brief      XAbstractScrollArea 保护接口（仅供子类与内部实现使用）。
 * @details    本文件集中声明对标 Qt 6.8 QAbstractScrollArea protected API
 *             的保护槽入口（scrollContentsBy/resizeEvent 的基类调用点）；
 *             普通应用代码不应直接包含或调用本文件中的接口；滚动区子类
 *             （XScrollArea/XPlainTextEdit/XTextEdit 等）与
 *             XAbstractScrollArea.c 必须显式包含本文件，不能依赖公共头
 *             文件的间接声明。虚函数表槽位枚举按项目惯例位于公共头文件
 *             XAbstractScrollArea.h。
 * @note       本文件依赖 XAbstractScrollArea.h；模块任一总开关关闭时，
 *             以下保护接口全部裁剪。
 * @author     XinYueC 团队
 */
#ifndef XABSTRACTSCROLLAREA_PROTECTED_H
#define XABSTRACTSCROLLAREA_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XAbstractScrollArea.h"

#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON

/* ==================== 保护槽入口（对标 protected scrollContentsBy/resizeEvent） ==== */

/** @brief 内容滚动槽：滚动条变化后由基类调用（dx/dy 为增量）。 */
void XAbstractScrollArea_scrollContentsBy_base(XAbstractScrollArea* self,
                                               int dx, int dy);

/** @brief 尺寸变化槽：供子类回调基类的视口/滚动条排布（跨 TU 入口）。
 *         直调父类槽位实现，不经对象虚表（子类重载无递归）；子类
 *         事件槽内部请用 XClass_Parent（代码风格约定）。 */
void XAbstractScrollArea_resizeEvent_base(XAbstractScrollArea* self);

#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON */

#ifdef __cplusplus
}
#endif
#endif /* XABSTRACTSCROLLAREA_PROTECTED_H */
