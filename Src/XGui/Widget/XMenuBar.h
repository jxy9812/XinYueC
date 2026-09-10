/**
 * @file       XMenuBar.h
 * @brief      XMenuBar 菜单栏控件（对标 Qt 6.8 QMenuBar 全部公共 API）。
 * @details    功能范围：
 *             - addMenu(XMenu*)/addMenu(title)：注册菜单并创建关联动作
 *               （动作文本=菜单标题；动作触发时弹出对应菜单）；
 *             - addSeparator/insertSeparator/insertMenu/clear；
 *             - activeAction/setActiveAction、setDefaultUp/isDefaultUp；
 *             - 信号 triggered(action)/hovered(action)（转发出组内动作）；
 *             - 绘制：横排菜单标题文本（简化实现，无快捷键下划线）；
 *             - cornerWidget/left/right 角落控件占位 API（第一版仅存储）。
 *             菜单栏为顶层窗口/主窗口的组成部分，不拥有 XMenu 对象
 *             （菜单归调用方，与 Qt 相同）。
 * @note       模块总开关 XMENUBAR_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XMENU_ON、XACTION_ON。
 * @author     XinYueC 团队
 */
#ifndef XMENUBAR_H
#define XMENUBAR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#if XMENU_ON
#include "XMenu.h"
#endif

#if XWIDGET_ON && XMENU_ON && XMENUBAR_ON

XCLASS_DEFINE_BEGING(XMenuBar)
XCLASS_DEFINE_EXTEND_END(XMenuBar, XWidget)

/**
 * @brief      XMenuBar 控件对象；m_base 必须是第一个成员。
 */
typedef struct XMenuBar
{
    XWidget m_base;         /**< 基类成员；必须是第一个。 */
    XVector* m_actions;     /**< 菜单栏动作数组（XAction*，拥有）。 */
    XVector* m_menus;       /**< 与动作顺序关联的菜单（XMenu*，借用）。 */
    XAction* m_activeAction;/**< 当前激活动作（借用）。 */
    bool m_defaultUp;       /**< 弹出菜单默认向上（默认 false）。 */
    XSize m_cachedHint;     /**< 尺寸提示缓存。 */
} XMenuBar;

/* ==================== 生命周期 ==================== */

XVtable* XMenuBar_class_init(void);
void XMenuBar_init(XMenuBar* self, XWidget* parent, XWidgetFlags flags);
#define XMenuBar_create(parent, flags) XMenuBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XMenuBar* XMenuBar_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags);
#define XMenuBar_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XMenuBar_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 菜单管理（对标 QMenuBar public API） ==================== */

/** @brief 注册菜单并创建关联动作（对标 addMenu(QMenu*)；返回动作归
 *         菜单栏所有）。 */
XAction* XMenuBar_addMenu(XMenuBar* self, XMenu* menu);
/** @brief 以标题创建空菜单并注册（对标 addMenu(const QString&)；返回
 *         的菜单归调用方所有）。 */
XMenu* XMenuBar_addMenu_2(XMenuBar* self, const char* utf8Title);
/** @brief 追加分隔条动作（对标 addSeparator）。 */
XAction* XMenuBar_addSeparator(XMenuBar* self);
/** @brief 在 before 动作之前插入菜单（对标 insertMenu；before 为 NULL
 *         时等价追加）。 */
XAction* XMenuBar_insertMenu(XMenuBar* self, XAction* before, XMenu* menu);
/** @brief 在 before 动作之前插入分隔条（对标 insertSeparator）。 */
XAction* XMenuBar_insertSeparator(XMenuBar* self, XAction* before);
/** @brief 清空全部动作（对标 clear；不销毁外部菜单对象）。 */
/**
 * @brief      清空内容（对标 Qt 同名槽）。
 */
void XMenuBar_clear(XMenuBar* self);
/** @brief 查询激活动作。 */
XAction* XMenuBar_activeAction(const XMenuBar* self);
/** @brief 设置激活动作。 */
void XMenuBar_setActiveAction(XMenuBar* self, XAction* action);
/** @brief 查询弹出方向默认向上（默认 false）。 */
bool XMenuBar_isDefaultUp(const XMenuBar* self);
/** @brief 设置弹出方向默认向上。 */
void XMenuBar_setDefaultUp(XMenuBar* self, bool up);
/** @brief 查询动作总数（动作+分隔条）。 */
int XMenuBar_actionCount(const XMenuBar* self);

/* ==================== 信号 ==================== */

void* XMenuBar_triggered_signal(XMenuBar* self, XAction* action);
void* XMenuBar_hovered_signal(XMenuBar* self, XAction* action);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XMENU_ON && XMENUBAR_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMENUBAR_H */
