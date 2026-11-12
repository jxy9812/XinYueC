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
 * @brief 查询样式提示（子类按类型名的虚函数分派入口别名）。
 *
 *        `*_base` 契约（条款 3）：本类不再保留同名真函数，对外唯一
 *        分派入口是 XStyle_styleHint_base（守卫 + 一次查虚表槽）；
 *        子类按自身类型名的调用经本宏展开到父类入口（范本
 *        XVector.h「复用基类接口的宏实现」）。首参按父类签名传
 *        (XStyle*)（XWindowsStyle 首成员即基类，直接转换安全）。
 * @param self 目标样式指针（XWindowsStyle*，传参时转 (XStyle*)）。
 * @param hint 提示枚举。
 * @param option 选项（可空）。
 * @param widget 关联控件（可空，借用）。
 * @return 提示值。
 */
#define XWindowsStyle_styleHint_base				XStyle_styleHint_base

#endif /* XSTYLE_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XWINDOWSSTYLE_H */
