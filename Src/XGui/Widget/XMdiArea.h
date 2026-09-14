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
    XString* m_title;   /**< 标题条文本（对象拥有）。 */
} XMdiSubWindow;

/** @brief XMdi子Windowclassinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XMdiSubWindow_class_init(void);
void XMdiSubWindow_init(XMdiSubWindow* self, XWidget* parent,
                        XWidgetFlags flags);
#define XMdiSubWindow_create(parent, flags) XMdiSubWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XMdiSubWindow* XMdiSubWindow_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags);
#define XMdiSubWindow_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XMdiSubWindow_delete_base(self) XClass_delete_base((XClass*)(self))

/** @brief XMdi子Windowset控件（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param widget 子控件指针。
 * @return 无返回值。
 */
void XMdiSubWindow_setWidget(XMdiSubWindow* self, XWidget* widget);
/**
 * @brief      获取内容控件（对标 Qt 同名方法）。
 */
XWidget* XMdiSubWindow_widget(const XMdiSubWindow* self);
/** @brief XMdi子WindowsetWindow标题2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param utf8 UTF-8 文本。
 * @return 无返回值。
 */
void XMdiSubWindow_setWindowTitle_2(XMdiSubWindow* self, const char* utf8);
/** @brief XMdi子Windowwindow标题2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
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
/** @brief XMdi区域activateNext子Window（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_activateNextSubWindow(XMdiArea* self);
/** @brief XMdi区域activatePrevious子Window（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_activatePreviousSubWindow(XMdiArea* self);
/** @brief XMdi区域close活动子Window（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_closeActiveSubWindow(XMdiArea* self);
/** @brief XMdi区域set活动子Window2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param window XWidget 参数。
 * @return 无返回值。
 */
void XMdiArea_setActiveSubWindow_2(XMdiArea* self, XWidget* window);
/** @brief XMdi区域setView模式2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param mode bool 模式开关。
 * @return 无返回值。
 */
void XMdiArea_setViewMode_2(XMdiArea* self, int mode);
/** @brief XMdi区域cascade子Windows2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_cascadeSubWindows_2(XMdiArea* self);
/** @brief XMdi区域tile子Windows2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_tileSubWindows_2(XMdiArea* self);
/** @brief XMdi区域closeAll子Windows2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_closeAllSubWindows_2(XMdiArea* self);
/** @brief XMdi区域remove子Window2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param widget 子控件指针。
 * @return 无返回值。
 */
void XMdiArea_removeSubWindow_2(XMdiArea* self, XWidget* widget);
/** @brief XMdi区域subWindow数量2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XMdiArea_subWindowCount_2(const XMdiArea* self);
/** @brief XMdi区域set背景（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param color ARGB 颜色值。
 * @return 无返回值。
 */
void XMdiArea_setBackground(XMdiArea* self, uint32_t color);
/** @brief XMdi区域background（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应值。
 */
uint32_t XMdiArea_background(const XMdiArea* self);
/** @brief XMdi区域set文档模式2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param mode bool 模式开关。
 * @return 无返回值。
 */
void XMdiArea_setDocumentMode_2(XMdiArea* self, bool mode);
/** @brief XMdi区域document模式2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XMdiArea_documentMode_2(const XMdiArea* self);
/** @brief XMdi区域set页签位置（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param position int 参数。
 * @return 无返回值。
 */
void XMdiArea_setTabPosition(XMdiArea* self, int position);
/** @brief XMdi区域tab位置（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XMdiArea_tabPosition(const XMdiArea* self);
/** @brief XMdi区域setTabs可关闭2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param closable bool 参数。
 * @return 无返回值。
 */
void XMdiArea_setTabsClosable_2(XMdiArea* self, bool closable);
/** @brief XMdi区域isTabs可关闭2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XMdiArea_isTabsClosable_2(const XMdiArea* self);
/** @brief XMdi区域setActivation顺序（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_setActivationOrder(XMdiArea* self);
/** @brief XMdi区域activation顺序（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_activationOrder(XMdiArea* self);
/** @brief XMdi区域set选项2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_setOption_2(XMdiArea* self);
/** @brief XMdi区域test选项2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_testOption_2(XMdiArea* self);
/** @brief XMdi区域scroll内容By2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_scrollContentsBy_2(XMdiArea* self);
/** @brief XMdi区域setHorizontal滚动条策略2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_setHorizontalScrollBarPolicy_2(XMdiArea* self);
/** @brief XMdi区域setVertical滚动条策略2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_setVerticalScrollBarPolicy_2(XMdiArea* self);
/** @brief XMdi区域set页签形状2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_setTabShape_2(XMdiArea* self);
/** @brief XMdi区域tab形状2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_tabShape_2(XMdiArea* self);
/** @brief XMdi区域set页签Tabs可关闭2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_setTabTabsClosable_2(XMdiArea* self);
/** @brief XMdi区域tabTabs可关闭2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_tabTabsClosable_2(XMdiArea* self);
/** @brief XMdi区域set页签Tabs可移动2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_setTabTabsMovable_2(XMdiArea* self);
/** @brief XMdi区域tabTabs可移动2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_tabTabsMovable_2(XMdiArea* self);
/** @brief XMdi区域set页签Tabs自动Hide2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_setTabTabsAutoHide_2(XMdiArea* self);
/** @brief XMdi区域tabTabs自动Hide2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_tabTabsAutoHide_2(XMdiArea* self);
/** @brief XMdi区域sizeHint2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_sizeHint_2(XMdiArea* self);
/** @brief XMdi区域subWindow列表count（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XMdiArea_subWindowList_count(XMdiArea* self);
#endif /* XMDIAREA_H */