/**
 * @file       XScrollBar.h
 * @brief      XScrollBar 滚动条控件（对标 Qt 6.8 QScrollBar 全部公共 API）。
 * @details    功能范围：
 *             - 方向：Horizontal/Vertical（默认 Vertical，对标无方向构造）；
 *             - 范围/步进继承 XAbstractSlider：默认 0..99、singleStep 1、
 *               pageStep 10、value 0（对标 QScrollBar 文档默认值）；
 *             - 鼠标交互：点击轨道按 handle 位置执行 PageStepAdd/Sub，
 *               拖动滑块按像素映射 setValue（对标 pixelPosToRangeValue）；
 *             - 滚轮步进由 XAbstractSlider 基类统一处理；
 *             - 右键标准菜单（对标 QScrollBar::contextMenuEvent，依赖
 *               SH_ScrollBar_ContextMenu 风格提示的行为以本实现默认启用
 *               等价提供）：滚动到此处/上(左)缘/下(右)缘/向上(左)翻页/
 *               向下(右)翻页/向上(左)滚动/向下(右)滚动，条目文本随方向
 *               切换；菜单以 popup + DeleteOnClose 方式呈现；
 *             - sizeHint：对标 CT_ScrollBar 的正方形内容（15x15 存储
 *               位，随方向由布局使用）。
 * @note       模块总开关 XSCROLLBAR_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XABSTRACTSLIDER_ON。
 *             绘制为工程简化实现：槽体/滑块矩形着色，无箭头按钮。
 * @author     XinYueC 团队
 */
#ifndef XSCROLLBAR_H
#define XSCROLLBAR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XAbstractSlider.h"

#if XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON

XCLASS_DEFINE_BEGING(XScrollBar)
XCLASS_DEFINE_EXTEND_END(XScrollBar, XAbstractSlider)

/**
 * @brief      XScrollBar 控件对象；m_base 必须是第一个成员。
 */
typedef struct XScrollBar
{
    XAbstractSlider m_base;      /**< 基类成员；必须是第一个。 */
    bool m_dragging;             /**< 滑块拖动中。 */
    int  m_pressOffset;          /**< 按下点相对滑块原点的偏移像素。 */
} XScrollBar;

/* ==================== 生命周期 ==================== */

/**
 * @brief      初始化类虚函数表（对标 Qt 的 metaObject 构建过程）。
 */
XVtable* XScrollBar_class_init(void);
/**
 * @brief      初始化控件（对标构造函数）。
 */
void XScrollBar_init(XScrollBar* self, XWidget* parent, XWidgetFlags flags);
/** @brief 以指定方向初始化（对标 QScrollBar(Qt::Orientation, parent*)）。 */
/**
 * @brief      按方向初始化滚动条（对标带 orientation 的构造函数）。
 */
void XScrollBar_init_2(XScrollBar* self, int orientation,
                       XWidget* parent, XWidgetFlags flags);
#define XScrollBar_create(parent, flags) XScrollBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      按指定内存类型创建控件实例。
 */
XScrollBar* XScrollBar_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags);
#define XScrollBar_create_2(orientation, parent, flags) \
    XScrollBar_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, (orientation), (parent), (flags))
/**
 * @brief      按内存类型和方向创建滚动条实例。
 */
XScrollBar* XScrollBar_create_ex_2(XMemoryType memory, int orientation,
                                   XWidget* parent, XWidgetFlags flags);
#define XScrollBar_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XScrollBar_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 右键标准菜单（对标 contextMenuEvent 菜单条目） ==================== */

/** @brief 构造标准滚动条右键菜单（条目/顺序/随方向变化的文本完全对齐
 *         QScrollBar::contextMenuEvent；返回的菜单所有权转移给调用方）。
 *         供右键菜单实现与测试使用。 */
/**
 * @brief      创建标准右键上下文菜单（对标 QScrollBar::createStandardContextMenu）。
 */
XMenu* XScrollBar_createStandardContextMenu(XScrollBar* self);

/* ==================== 父类 API 宏转发（对齐库内 XDial 惯例） ==================== */

#define XScrollBar_minimum(self) XAbstractSlider_minimum((const XAbstractSlider*)(self))
#define XScrollBar_maximum(self) XAbstractSlider_maximum((const XAbstractSlider*)(self))
#define XScrollBar_setMinimum(self, min) XAbstractSlider_setMinimum((XAbstractSlider*)(self), (min))
#define XScrollBar_setMaximum(self, max) XAbstractSlider_setMaximum((XAbstractSlider*)(self), (max))
#define XScrollBar_setRange(self, min, max) XAbstractSlider_setRange((XAbstractSlider*)(self), (min), (max))
#define XScrollBar_value(self) XAbstractSlider_value((const XAbstractSlider*)(self))
#define XScrollBar_setValue(self, v) XAbstractSlider_setValue((XAbstractSlider*)(self), (v))
#define XScrollBar_singleStep(self) XAbstractSlider_singleStep((const XAbstractSlider*)(self))
#define XScrollBar_setSingleStep(self, s) XAbstractSlider_setSingleStep((XAbstractSlider*)(self), (s))
#define XScrollBar_pageStep(self) XAbstractSlider_pageStep((const XAbstractSlider*)(self))
#define XScrollBar_setPageStep(self, s) XAbstractSlider_setPageStep((XAbstractSlider*)(self), (s))
#define XScrollBar_orientation(self) XAbstractSlider_orientation((const XAbstractSlider*)(self))
#define XScrollBar_setOrientation(self, o) XAbstractSlider_setOrientation((XAbstractSlider*)(self), (o))
#define XScrollBar_sizeHint(self) XWidget_sizeHint((const XWidget*)(self))
#define XScrollBar_isSliderDown(self) XAbstractSlider_isSliderDown((const XAbstractSlider*)(self))
#define XScrollBar_setSliderDown(self, d) XAbstractSlider_setSliderDown((XAbstractSlider*)(self), (d))
#define XScrollBar_valueChanged_signal(self) XAbstractSlider_valueChanged_signal((XAbstractSlider*)(self))
#define XScrollBar_sliderMoved_signal(self) XAbstractSlider_sliderMoved_signal((XAbstractSlider*)(self))
#define XScrollBar_actionTriggered_signal(self) XAbstractSlider_actionTriggered_signal((XAbstractSlider*)(self))

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSCROLLBAR_H */
