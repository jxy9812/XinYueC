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

#if XGUI_ON

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

/** @brief 视口建议尺寸的基类缺省实现（对标 QAbstractScrollArea::
 *         viewportSizeHint，qabstractscrollarea.cpp:1450-1462）。
 * @details 子类重载虚槽 EXAbstractScrollArea_ViewportSizeHint 后，需要
 *          基类口径时经本入口取（直调本类实现，不再经对象虚表）。
 * @param self 目标滚动区域；NULL 返回 (0,0)。
 * @return 视口建议尺寸。
 */
XSize XAbstractScrollArea_viewportSizeHint_base(
    const XAbstractScrollArea* self);

/** @brief 视口换装钩子的基类缺省实现（空操作，对标 setupViewport）。
 * @param self 目标滚动区域；NULL 无操作。
 * @param viewport 新视口（借用）。
 * @return 无返回值。
 */
void XAbstractScrollArea_setupViewport_base(XAbstractScrollArea* self,
                                           XWidget* viewport);

/** @brief 视口事件入口的基类缺省实现（对标 viewportEvent 缺省分支）。
 * @details 恒返回 false（不消费视口事件）——本框架绘制/事件模型下
 *          视口自身就是事件靶，与 Qt 的「重映射给滚动区处理器」不同
 *          （详见 XAbstractScrollArea_viewportEvent 的 @note）。子类重载
 *          虚槽 EXAbstractScrollArea_ViewportEvent 后需要基类口径时经本
 *          入口取（直调本类实现，不经对象虚表）。
 * @param self 目标滚动区域；NULL 返回 false。
 * @param event 视口事件（借用）。
 * @return 恒 false。
 */
bool XAbstractScrollArea_viewportEvent_base(XAbstractScrollArea* self,
                                            XEvent* event);

/** @brief 停止触摸跟手/惯性滚动（XGui 扩展；子类在自管输入路径——如
 *         滚轮覆写、编程滚动——应调用，避免惯性与社会滚动打架）。
 * @param self 目标滚动区域；NULL 无操作。 */
void XAbstractScrollArea_stopTouchScroll(XAbstractScrollArea* self);

#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XABSTRACTSCROLLAREA_PROTECTED_H */
