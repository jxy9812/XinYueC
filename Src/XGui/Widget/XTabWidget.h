/**
 * @file       XTabWidget.h
 * @brief      XTabWidget 选项卡容器控件（对标 Qt 6.8 QTabWidget）。
 * @details    XTabBar（顶部页签）+ 页容器（每页一个 XWidget 子容器）
 *             的组合控件：addTab/insertTab 接收页内容控件（借用），
 *             页容器几何由本控件在 resize 时分配；removeTab 移除页签
 *             （页控件不销毁，由调用方管理）。
 * @note       模块总开关 XTABWIDGET_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTABWIDGET_H
#define XTABWIDGET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XTabBar.h"

#if XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XTabWidget)
XCLASS_DEFINE_EXTEND_END(XTabWidget, XWidget)

/** @brief XTabWidget 选项卡容器对象。 */
typedef struct XTabWidget
{
    XWidget m_base;                  /**< 基类成员；必须是第一个。 */
    XTabBar m_tabBar;                /**< 顶部页签条（拥有）。 */
    XWidget** m_pages;               /**< 页容器数组（每页 XWidget 拥有）。 */
    XWidget** m_clients;             /**< 用户内容控件（借用指针记录）。 */
    int m_count;                     /**< 页数。 */
    int m_capacity;                  /**< 容量。 */
    int m_currentIndex;              /**< 当前页。 */
    int m_tabPosition;               /**< 页签位置（North=0，其余保留字段）。 */
    bool m_tabsClosable;             /**< 可关闭（转发页签条）。 */
    bool m_movable;                  /**< 可拖动（转发页签条）。 */
} XTabWidget;

/* ==================== 生命周期 ==================== */

XVtable* XTabWidget_class_init(void);
void XTabWidget_init(XTabWidget* self, XWidget* parent, XWidgetFlags flags);
#define XTabWidget_create(parent, flags) XTabWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XTabWidget* XTabWidget_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XTabWidget_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XTabWidget_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== API（对标 QTabWidget public API 子集） ==================== */

int XTabWidget_addTab(XTabWidget* self, XWidget* page, const char* label);
int XTabWidget_insertTab(XTabWidget* self, int index, XWidget* page,
                         const char* label);
void XTabWidget_removeTab(XTabWidget* self, int index);
int XTabWidget_count(const XTabWidget* self);
int XTabWidget_currentIndex(const XTabWidget* self);
void XTabWidget_setCurrentIndex(XTabWidget* self, int index);
XWidget* XTabWidget_currentWidget(const XTabWidget* self);
XWidget* XTabWidget_widget(const XTabWidget* self, int index);
int XTabWidget_indexOf(const XTabWidget* self, const XWidget* page);
const char* XTabWidget_tabText(const XTabWidget* self, int index);
void XTabWidget_setTabText(XTabWidget* self, int index, const char* text);
bool XTabWidget_isTabEnabled(const XTabWidget* self, int index);
void XTabWidget_setTabEnabled(XTabWidget* self, int index, bool enabled);
XTabBar* XTabWidget_tabBar(const XTabWidget* self);
int XTabWidget_tabPosition(const XTabWidget* self);
void XTabWidget_setTabPosition(XTabWidget* self, int position);
bool XTabWidget_tabsClosable(const XTabWidget* self);
void XTabWidget_setTabsClosable(XTabWidget* self, bool closable);
bool XTabWidget_isMovable(const XTabWidget* self);
void XTabWidget_setMovable(XTabWidget* self, bool movable);

/* ==================== 信号（转发页签条） ==================== */

void* XTabWidget_currentChanged_signal(XTabWidget* self);
void* XTabWidget_tabClicked_signal(XTabWidget* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTABWIDGET_H */
