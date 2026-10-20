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

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XSIZEGRIP_ON

/** @brief XSizeGrip 虚函数表枚举：继承 XWidget 全部槽位（不新增）。 */
XCLASS_DEFINE_BEGING(XSizeGrip)
XCLASS_DEFINE_EXTEND_END(XSizeGrip, XWidget)

/**
 * @brief      XSizeGrip 尺寸手柄控件对象。
 * @details    基类成员必须是第一个成员，保证对象可向上转换为
 *             XWidget/XObject；本类无自有堆成员，拖拽与绘制状态由
 *             实现维护，调用方不得直接修改成员，一律走公开 API。
 */
typedef struct XSizeGrip
{
    XWidget m_base;   /**< 基类成员；必须是第一个，由 XClass 管理，禁止手工修改。 */
} XSizeGrip;

/**
 * @brief      初始化类虚函数表（对标 Qt 的 metaObject 构建过程）。
 * @return     类共享的 XVtable 指针；失败返回 NULL。
 */
XVtable* XSizeGrip_class_init(void);
/**
 * @brief      初始化控件（对标构造函数）。
 * @param      self 待初始化对象；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @return     无返回值。
 */
void XSizeGrip_init(XSizeGrip* self, XWidget* parent);
/** @brief 使用默认内存类型创建尺寸手柄（默认构造；语义同 create_ex）。 */
#define XSizeGrip_create(parent) XSizeGrip_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent))
/**
 * @brief      按指定内存类型创建控件实例。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @return     新建的已初始化对象指针；失败返回 NULL。成功后必须
 *             XClassDelete 释放。
 */
XSizeGrip* XSizeGrip_create_ex(XMemoryType memory, XWidget* parent);

#endif /* XWIDGET_ON && XSIZEGRIP_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XSIZEGRIP_H */
