#ifndef XWINDOWSSTYLE_H
#define XWINDOWSSTYLE_H
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

/** @brief 析构入口（查表分派父类析构）。 */
#define XWindowsStyle_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上样式（查表分派析构并释放内存）。 */
#define XWindowsStyle_delete_base(self) XClass_delete_base((XClass*)(self))

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
#endif /* XWINDOWSSTYLE_H */
