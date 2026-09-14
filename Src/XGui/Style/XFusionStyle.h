#ifndef XFUSIONSTYLE_H
#define XFUSIONSTYLE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWindowsStyle.h"

#if XSTYLE_ON

XCLASS_DEFINE_BEGING(XFusionStyle)
XCLASS_DEFINE_EXTEND_END(XFusionStyle, XWindowsStyle)

/**
 * @brief Fusion 风格（对标 Qt 6.8 QFusionStyle）。
 *
 *        Fusion 主题：命令按钮圆角渐变面板、悬停高亮、按下凹陷、
 *        焦点环、复选/单选 Fusion 指示器、页签渐变等。
 */
typedef struct XFusionStyle
{
    XWindowsStyle m_base;  /**< 基类成员；必须是第一个。 */
} XFusionStyle;

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

/** @brief 析构入口（查表分派父类析构）。 */
#define XFusionStyle_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上样式（查表分派析构并释放内存）。 */
#define XFusionStyle_delete_base(self) XClass_delete_base((XClass*)(self))

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
#endif /* XFUSIONSTYLE_H */
