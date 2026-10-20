/**
 * @file       XWindowsStyle.h
 * @brief      嵌入式 Windows 风格公开 API。
 * @details    对标 Qt 6.8 QWindowsStyle : QCommonStyle（继承
 *             XCommonStyle）：Qt 6 中该层较薄，主要是 polish 应用级钩子
 *             与少量度量/样式提示覆盖，绘制逻辑继承公共样式；
 *             XStyleSheetStyle 继承本类（1:1 对齐 QStyleSheetStyle :
 *             QWindowsStyle 链）。实现只依赖 XinYueC 抽象层，禁止调用
 *             Win32、POSIX、Qt 或其他平台 API。
 */
#ifndef XWINDOWSSTYLE_H
#define XWINDOWSSTYLE_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XCommonStyle.h"

#if XSTYLE_ON

XCLASS_DEFINE_BEGING(XWindowsStyle)
XCLASS_DEFINE_EXTEND_END(XWindowsStyle, XCommonStyle)

/**
 * @brief Windows 风格（对标 Qt 6.8 QWindowsStyle : QCommonStyle）。
 *
 *        Qt 6 中该层较薄：主要是 polish 应用级钩子与少量度量/样式提示
 *        覆盖，绘制逻辑继承 QCommonStyle；XStyleSheetStyle 继承本类
 *        （1:1 对齐 QStyleSheetStyle : QWindowsStyle 链）。
 */
typedef struct XWindowsStyle
{
    XCommonStyle m_base;  /**< 基类成员；必须是第一个。 */
} XWindowsStyle;

/**
 * @brief 初始化 XWindowsStyle 类的虚函数表。
 * @return 指向初始化完成的 XVtable 的指针。
 */
XVtable* XWindowsStyle_class_init(void);

/**
 * @brief 初始化嵌入式 Windows 风格。
 *
 * @param self 目标样式指针，不能为空。
 * @return 无返回值。
 */
void XWindowsStyle_init(XWindowsStyle* self);

/**
 * @brief 堆上创建 Windows 风格。
 *
 * @param memory 内存类型。
 * @return 样式指针；分配失败返回 NULL。
 */
XWindowsStyle* XWindowsStyle_create_ex(XMemoryType memory);
#define XWindowsStyle_create() XWindowsStyle_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief 查询样式提示（对标 QWindowsStyle::styleHint 子集）。
 * @param self 目标样式指针。
 * @param hint 提示枚举。
 * @param option 选项（可空）。
 * @param widget 关联控件（可空，借用）。
 * @return 提示值。
 */
int XWindowsStyle_styleHint(XWindowsStyle* self, int hint,
                            const XStyleOption* option,
                            const XWidget* widget);

#endif /* XSTYLE_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XWINDOWSSTYLE_H */
