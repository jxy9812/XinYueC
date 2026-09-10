/**
 * @file       XSizeGrip.h
 * @brief      XSizeGrip 尺寸手柄控件（对标 Qt 6.8 QSizeGrip 全部公共 API）。
 * @details    放置于窗口右下角，拖动时调整其顶层窗口的尺寸；绘制右下
 *             角斜纹三角。继承 XWidget。
 * @note       模块总开关 XSIZEGRIP_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XSIZEGRIP_H
#define XSIZEGRIP_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XSIZEGRIP_ON

XCLASS_DEFINE_BEGING(XSizeGrip)
XCLASS_DEFINE_EXTEND_END(XSizeGrip, XWidget)

typedef struct XSizeGrip
{
    XWidget m_base;   /**< 基类成员；必须是第一个。 */
} XSizeGrip;

/**
 * @brief      初始化类虚函数表（对标 Qt 的 metaObject 构建过程）。
 */
XVtable* XSizeGrip_class_init(void);
/**
 * @brief      初始化控件（对标构造函数）。
 */
void XSizeGrip_init(XSizeGrip* self, XWidget* parent);
#define XSizeGrip_create(parent) XSizeGrip_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent))
/**
 * @brief      按指定内存类型创建控件实例。
 */
XSizeGrip* XSizeGrip_create_ex(XMemoryType memory, XWidget* parent);
#define XSizeGrip_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XSizeGrip_delete_base(self) XClass_delete_base((XClass*)(self))

#endif /* XWIDGET_ON && XSIZEGRIP_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSIZEGRIP_H */
