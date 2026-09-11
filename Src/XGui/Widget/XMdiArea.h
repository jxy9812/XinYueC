/**
 * @file       XMdiArea.h
 * @brief      XMdiArea 多文档接口区域 + XMdiSubWindow 子窗口
 *             （对标 Qt 6.8 QMdiArea / QMdiSubWindow 核心公共 API）。
 * @details    功能范围：
 *             - XMdiSubWindow：setWidget/widget（内容归 sub window）、
 *               setWindowTitle（标题条文本）、mdiArea() 反查；
 *               绘制：高亮标题条 + 内容区域；
 *             - XMdiArea：addSubWindow/removeSubWindow/subWindowList/
 *               activeSubWindow/setActiveSubWindow/closeAllSubWindows/
 *               cascadeSubWindows/tileSubWindows/setViewMode/viewMode；
 *               信号 subWindowActivated(XMdiSubWindow*)。
 *             页面控件所有权：addSubWindow 后控件归 sub window，sub
 *             window 归 area；closeAllSubWindows 销毁全部子窗口。
 * @note       模块总开关 XMDIAREA_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XMDIAREA_H
#define XMDIAREA_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#if XABSTRACTSCROLLAREA_ON
#include "XAbstractScrollArea.h"
#endif

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON

/** @brief 子窗口排列模式（对标 QMdiArea::ViewMode，数值一致）。 */
typedef enum XMdiAreaViewMode
{
    XMdiAreaViewMode_SubWindowView = 0, /**< 子窗口平铺。 */
    XMdiAreaViewMode_TabbedView = 1     /**< 选项卡模式（第一版存储）。 */
} XMdiAreaViewMode;

/* ==================== XMdiSubWindow ==================== */

XCLASS_DEFINE_BEGING(XMdiSubWindow)
XCLASS_DEFINE_EXTEND_END(XMdiSubWindow, XWidget)

typedef struct XMdiSubWindow
{
    XWidget m_base;      /**< 基类成员；必须是第一个。 */
    XWidget* m_widget;   /**< 内容控件（借用，归 sub window）。 */
    char m_title[128];   /**< 标题条文本。 */
} XMdiSubWindow;

XVtable* XMdiSubWindow_class_init(void);
void XMdiSubWindow_init(XMdiSubWindow* self, XWidget* parent,
                        XWidgetFlags flags);
#define XMdiSubWindow_create(parent, flags) XMdiSubWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XMdiSubWindow* XMdiSubWindow_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags);
#define XMdiSubWindow_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XMdiSubWindow_delete_base(self) XClass_delete_base((XClass*)(self))

void XMdiSubWindow_setWidget(XMdiSubWindow* self, XWidget* widget);
/**
 * @brief      获取内容控件（对标 Qt 同名方法）。
 */
XWidget* XMdiSubWindow_widget(const XMdiSubWindow* self);
void XMdiSubWindow_setWindowTitle_2(XMdiSubWindow* self, const char* utf8);
const char* XMdiSubWindow_windowTitle_2(const XMdiSubWindow* self);

/* ==================== XMdiArea ==================== */

XCLASS_DEFINE_BEGING(XMdiArea)
XCLASS_DEFINE_EXTEND_END(XMdiArea, XAbstractScrollArea)

typedef struct XMdiArea
{
    XAbstractScrollArea m_base; /**< 基类成员；必须是第一个。 */
    XVector* m_subWindows; /**< 子窗口数组（XMdiSubWindow*，拥有）。 */
    XMdiSubWindow* m_active; /**< 当前活动子窗口（借用）。 */
    int m_viewMode;      /**< 视图模式（XMdiAreaViewMode）。 */
} XMdiArea;

/**
 * @brief      初始化类虚函数表（对标 Qt 的 metaObject 构建过程）。
 */
XVtable* XMdiArea_class_init(void);
/**
 * @brief      初始化控件（对标构造函数）。
 */
void XMdiArea_init(XMdiArea* self, XWidget* parent, XWidgetFlags flags);
#define XMdiArea_create(parent, flags) XMdiArea_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      按指定内存类型创建控件实例。
 */
XMdiArea* XMdiArea_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags);
#define XMdiArea_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XMdiArea_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief      添加子窗口。
 */
XMdiSubWindow* XMdiArea_addSubWindow(XMdiArea* self, XWidget* widget);
/**
 * @brief      移除子窗口（对标 removeSubWindow）。
 */
void XMdiArea_removeSubWindow(XMdiArea* self, XWidget* widget);
/**
 * @brief      获取子窗口列表。
 */
const XVector* XMdiArea_subWindowList(const XMdiArea* self);
/**
 * @brief      获取活动子窗口。
 */
XMdiSubWindow* XMdiArea_activeSubWindow(const XMdiArea* self);
/**
 * @brief      设置活动子窗口。
 */
void XMdiArea_setActiveSubWindow(XMdiArea* self, XMdiSubWindow* window);
/**
 * @brief      关闭全部子窗口。
 */
void XMdiArea_closeAllSubWindows(XMdiArea* self);
/**
 * @brief      层叠子窗口。
 */
void XMdiArea_cascadeSubWindows(XMdiArea* self);
/**
 * @brief      平铺子窗口。
 */
void XMdiArea_tileSubWindows(XMdiArea* self);
/**
 * @brief      设置视图模式。
 */
void XMdiArea_setViewMode(XMdiArea* self, XMdiAreaViewMode mode);
/**
 * @brief      获取视图模式。
 */
XMdiAreaViewMode XMdiArea_viewMode(const XMdiArea* self);
/**
 * @brief      获取子窗口数量。
 */
int XMdiArea_subWindowCount(const XMdiArea* self);

/* ==================== 信号 ==================== */

/**
 * @brief      子窗口激活信号（真发射）。
 */
void* XMdiArea_subWindowActivated_signal(XMdiArea* self,
                                         XMdiSubWindow* window);

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON */

#ifdef __cplusplus
}
#endif
void XMdiArea_activateNextSubWindow(XMdiArea* self);
void XMdiArea_activatePreviousSubWindow(XMdiArea* self);
void XMdiArea_closeActiveSubWindow(XMdiArea* self);
void XMdiArea_setActiveSubWindow_2(XMdiArea* self, XWidget* window);
void XMdiArea_setViewMode_2(XMdiArea* self, int mode);
void XMdiArea_cascadeSubWindows_2(XMdiArea* self);
void XMdiArea_tileSubWindows_2(XMdiArea* self);
void XMdiArea_closeAllSubWindows_2(XMdiArea* self);
void XMdiArea_removeSubWindow_2(XMdiArea* self, XWidget* widget);
int XMdiArea_subWindowCount_2(const XMdiArea* self);
void XMdiArea_setBackground(XMdiArea* self, uint32_t color);
uint32_t XMdiArea_background(const XMdiArea* self);
void XMdiArea_setDocumentMode_2(XMdiArea* self, bool mode);
bool XMdiArea_documentMode_2(const XMdiArea* self);
void XMdiArea_setTabPosition(XMdiArea* self, int position);
int XMdiArea_tabPosition(const XMdiArea* self);
void XMdiArea_setTabsClosable_2(XMdiArea* self, bool closable);
bool XMdiArea_isTabsClosable_2(const XMdiArea* self);
#endif /* XMDIAREA_H */