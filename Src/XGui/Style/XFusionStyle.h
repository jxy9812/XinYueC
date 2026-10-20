/**
 * @file       XFusionStyle.h
 * @brief      嵌入式 Fusion 风格公开 API。
 * @details    对标 Qt 6.8 QFusionStyle（继承 XCommonStyle）：命令按钮
 *             圆角渐变面板、悬停高亮、按下凹陷、焦点环、复选/单选
 *             Fusion 指示器、页签渐变等。实现只依赖 XinYueC 抽象层，
 *             禁止调用 Win32、POSIX、Qt 或其他平台 API。
 */
#ifndef XFUSIONSTYLE_H
#define XFUSIONSTYLE_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XCommonStyle.h"

#if XSTYLE_ON

XCLASS_DEFINE_BEGING(XFusionStyle)
XCLASS_DEFINE_EXTEND_END(XFusionStyle, XCommonStyle)

/**
 * @brief Fusion 风格（对标 Qt 6.8 QFusionStyle）。
 *
 *        Fusion 主题：命令按钮圆角渐变面板、悬停高亮、按下凹陷、
 *        焦点环、复选/单选 Fusion 指示器、页签渐变等。
 */
typedef struct XFusionStyle
{
    XCommonStyle m_base;  /**< 基类成员；必须是第一个（Qt 6.8 QFusionStyle : QCommonStyle）。 */
} XFusionStyle;

/**
 * @brief 初始化 XFusionStyle 类的虚函数表。
 * @return 指向初始化完成的 XVtable 的指针。
 */
XVtable* XFusionStyle_class_init(void);

/**
 * @brief 初始化嵌入式 Fusion 样式。
 *
 * @param self 目标样式指针，不能为空。
 * @return 无返回值。
 */
void XFusionStyle_init(XFusionStyle* self);

/**
 * @brief 堆上创建 Fusion 样式。
 *
 * @param memory 内存类型。
 * @return 样式指针；分配失败返回 NULL。
 */
XFusionStyle* XFusionStyle_create_ex(XMemoryType memory);
#define XFusionStyle_create() XFusionStyle_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief 安装 Fusion 为全局默认样式（替换旧默认样式）。
 *
 * @return 无返回值。
 */
void XFusionStyle_installDefault(void);

#endif /* XSTYLE_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XFUSIONSTYLE_H */
