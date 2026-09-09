/**
 * @file       XTabBar.h
 * @brief      XTabBar 选项卡条控件（对标 Qt 6.8 QTabBar）。
 * @details    水平选项卡条：addTab/insertTab/removeTab/setTabText/
 *             setTabEnabled/setCurrentIndex + currentIndexBar 显示与
 *             点击切换；tabsClosable/movable 为字段保留项。
 * @note       模块总开关 XTABBAR_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTABBAR_H
#define XTABBAR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XTABBAR_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XTabBar)
XCLASS_DEFINE_EXTEND_END(XTabBar, XWidget)

/** @brief XTabBar 选项卡条对象。 */
typedef struct XTabBar
{
    XWidget m_base;                  /**< 基类成员；必须是第一个。 */
    char**  m_titles;                /**< 项标题数组（拥有）。 */
    int     m_count;                 /**< 项数。 */
    int     m_capacity;              /**< 容量。 */
    int     m_currentIndex;          /**< 当前项。 */
    bool*   m_enabled;               /**< 各项启用状态。 */
    bool    m_tabsClosable;          /**< 可关闭（字段保留）。 */
    bool    m_movable;               /**< 可拖动（字段保留）。 */
} XTabBar;

/* ==================== 生命周期 ==================== */

XVtable* XTabBar_class_init(void);
void XTabBar_init(XTabBar* self, XWidget* parent, XWidgetFlags flags);
#define XTabBar_create(parent, flags) XTabBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XTabBar* XTabBar_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XTabBar_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XTabBar_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== API（对标 QTabBar public API 子集） ==================== */

int XTabBar_addTab(XTabBar* self, const char* text);
int XTabBar_insertTab(XTabBar* self, int index, const char* text);
void XTabBar_removeTab(XTabBar* self, int index);
int XTabBar_count(const XTabBar* self);
int XTabBar_currentIndex(const XTabBar* self);
void XTabBar_setCurrentIndex(XTabBar* self, int index);
const char* XTabBar_tabText(const XTabBar* self, int index);
void XTabBar_setTabText(XTabBar* self, int index, const char* text);
bool XTabBar_isTabEnabled(const XTabBar* self, int index);
void XTabBar_setTabEnabled(XTabBar* self, int index, bool enabled);
bool XTabBar_tabsClosable(const XTabBar* self);
void XTabBar_setTabsClosable(XTabBar* self, bool closable);
bool XTabBar_isMovable(const XTabBar* self);
void XTabBar_setMovable(XTabBar* self, bool movable);

/* ==================== 信号 ==================== */

void* XTabBar_currentChanged_signal(XTabBar* self);
void* XTabBar_tabClicked_signal(XTabBar* self);
void* XTabBar_tabCloseRequested_signal(XTabBar* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABBAR_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTABBAR_H */
