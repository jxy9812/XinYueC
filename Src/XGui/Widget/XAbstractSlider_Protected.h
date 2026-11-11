/**
 * @file       XAbstractSlider_Protected.h
 * @brief      XAbstractSlider 内部控制面（仅供 XSlider/XScrollBar/XDial
 *             等派生类实现使用）。
 * @details    本头暴露 Qt 中受保护/私有的滚动核心
 *             `QAbstractSliderPrivate::scrollByDelta`（qabstractslider.cpp:
 *             668-729）——它按**滚动方向**决定水平份量取负
 *             （「in Qt scrolling to the right gives negative values」，
 *             :671-674），是 QScrollBar::wheelEvent（qscrollbar.cpp:
 *             475-497）按 |dx|>|dy| 择轴后复用的同一函数。C 侧没有
 *             派生类默认可见的 protected 段，故单独放在本头，业务/上层
 *             代码不要直接调用。
 * @note       模块总开关 XABSTRACTSLIDER_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XABSTRACTSLIDER_PROTECTED_H
#define XABSTRACTSLIDER_PROTECTED_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"
#include "XAbstractSlider.h"

#if XWIDGET_ON && XABSTRACTSLIDER_ON

/**
 * @brief      按滚动方向折算滚轮增量并移动滑块（对标
 *             QAbstractSliderPrivate::scrollByDelta）。
 * @details    语义与 Qt 6.8 逐分支一致：
 *             - orientation == Horizontal 时先取负（向右滚=值减小）；
 *             - Ctrl/Shift：按整格 offset 折算一页并 qBound 到 ±pageStep，
 *               累积器清零；
 *             - 普通：一格 = wheelScrollLines × singleStep，单次不超过
 *               一页，方向反转清零累积器；
 *             - invertedControls 翻转方向；
 *             - 末尾 position = bound(overflowSafeAdd(steps)) 后
 *               triggerAction(SliderMove) 提交，返回「值是否变化」。
 *             **本库已知偏差**：累积器为 int 整步口径，|delta|<120 的
 *             增量不入账（Qt 的 offset_accumulated 为 qreal）——见
 *             XAbstractSlider.c 中该函数的注（现场滚动时序依赖）。
 * @param      self 目标滑块；可为 NULL（返回 false）。
 * @param      orientation 滚动方向（XAbstractSliderOrientation 1/2）。
 * @param      delta 滚轮角度增量（1/8 度；一格 = 120）。
 * @param      modifiers 键盘修饰键（Ctrl/Shift 走整页分支）。
 * @return     true=已消费（值发生变化），false=忽略。
 */
bool XAbstractSlider_scrollByDelta(XAbstractSlider* self, int orientation,
                                   int delta,
                                   XKeyboardModifiers modifiers);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON */

#endif /* XGUI_ON */

#endif /* XABSTRACTSLIDER_PROTECTED_H */
