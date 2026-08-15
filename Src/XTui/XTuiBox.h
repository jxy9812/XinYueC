/**
 * @file       XTuiBox.h
 * @brief      XTui 带边框和标题的盒式控件公开 API。
 * @details    继承 XTuiWidget，重载 Render 绘制单线边框和可选标题。适合作为
 *             全屏应用的容器或面板。
 */

#ifndef XTUI_BOX_H
#define XTUI_BOX_H

#include "XTuiConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XTUI_ON && XTUI_WIDGET_ON

#include "XTuiWidget.h"
#include "XClass.h"
#include "XMemory.h"
#include <stdbool.h>

/**
 * @brief XTuiBox 类虚函数表枚举（仅继承 XTuiWidget，不新增虚函数）。
 */
XCLASS_DEFINE_BEGING(XTuiBox)
XCLASS_DEFINE_EXTEND_END(XTuiBox, XTuiWidget)

/**
 * @brief 盒式控件对象。
 * @details m_title 为动态分配字符串；m_borderFg/m_borderBg/m_contentFg 控制
 *          边框和内容颜色，均使用 XColor；无效颜色表示使用终端默认色。
 */
typedef struct XTuiBox
{
    XTuiWidget m_widget;    /**< 基类控件，第一个成员，由 XTuiWidget 管理。 */
    char*      m_title;     /**< 标题文本；动态分配，由本对象释放。 */
    XColor     m_borderFg;  /**< 边框前景色；无效颜色表示终端默认色。 */
    XColor     m_borderBg;  /**< 边框背景色；无效颜色表示终端默认色。 */
    XColor     m_contentFg; /**< 内容前景色；无效颜色表示终端默认色。 */
} XTuiBox;

/** @brief 初始化 XTuiBox 类虚函数表。 */
XVtable* XTuiBox_class_init(void);

/** @brief 在栈上初始化控件对象。 */
void XTuiBox_init(XTuiBox* box);

/** @brief 在堆上创建控件对象。 */
XTuiBox* XTuiBox_create_ex(XMemoryType memory);

#define XTuiBox_delete_base XClass_delete_base /**< 释放堆对象。 */
#define XTuiBox_deinit_base XClass_deinit_base /**< 反初始化栈对象。 */
#define XTuiBox_copy_base   XClass_copy_base   /**< 拷贝。 */
#define XTuiBox_move_base   XClass_move_base   /**< 移动。 */

/** @brief 设置标题文本（深拷贝）。 */
void XTuiBox_setTitle(XTuiBox* box, const char* title);

/** @brief 获取标题文本（借用指针）。 */
const char* XTuiBox_title(const XTuiBox* box);

/** @brief 设置边框/内容颜色；无效颜色（XTUI_COLOR_DEFAULT）表示终端默认色。 */
void XTuiBox_setColors(XTuiBox* box, XColor borderFg, XColor borderBg, XColor contentFg);

#endif /* XTUI_ON && XTUI_WIDGET_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XTuiBox_create
#define XTuiBox_create(...) XTuiBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif /* XTUI_BOX_H */
