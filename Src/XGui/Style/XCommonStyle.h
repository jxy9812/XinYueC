#ifndef XCOMMONSTYLE_H
#define XCOMMONSTYLE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XStyle.h"

#if XSTYLE_ON

XCLASS_DEFINE_BEGING(XCommonStyle)
XCLASS_DEFINE_EXTEND_END(XCommonStyle, XStyle)

/**
 * @brief 公共样式（对标 Qt 6.8 QCommonStyle）。
 *
 *        提供 Qt 各平台共享的基元/控件绘制：边框、按钮面板、
 *        复选/单选指示器、箭头、进度条槽与块、页签形状等。
 *        XFusionStyle 继承本类并覆盖 Fusion 主题细节。
 */
typedef struct XCommonStyle
{
    XStyle m_base;   /**< 基类成员；必须是第一个。 */
} XCommonStyle;

XVtable* XCommonStyle_class_init(void);

/**
 * @brief 初始化嵌入式公共样式。
 *
 * @param self 目标样式指针，不能为空。
 * @return 无返回值。
 */
void XCommonStyle_init(XCommonStyle* self);

/**
 * @brief 堆上创建公共样式。
 *
 * @param memory 内存类型。
 * @return 样式指针；分配失败返回 NULL。
 */
XCommonStyle* XCommonStyle_create_ex(XMemoryType memory);
#define XCommonStyle_create() XCommonStyle_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */
#define XCommonStyle_deinit_base(self) XClass_deinit_base((XClass*)(self))

/** @brief 删除堆上样式（查表分派析构并释放内存）。 */
#define XCommonStyle_delete_base(self) XClass_delete_base((XClass*)(self))

#endif /* XSTYLE_ON */
#ifdef __cplusplus
}
#endif
#endif /* XCOMMONSTYLE_H */
