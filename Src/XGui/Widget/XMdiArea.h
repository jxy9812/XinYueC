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
#if XMENU_ON
#include "XMenu.h"
#else
typedef struct XMenu XMenu; /* 裁剪场景（XMENU_ON=0）的类型占位。 */
#endif

#if XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON

/** @brief 子窗口排列模式（对标 QMdiArea::ViewMode，数值一致）。 */
typedef enum XMdiAreaViewMode
{
    XMdiAreaViewMode_SubWindowView = 0, /**< 子窗口平铺。 */
    XMdiAreaViewMode_TabbedView = 1     /**< 选项卡模式。 */
} XMdiAreaViewMode;

/** @brief 区域选项位（对标 QMdiArea::AreaOption，数值一致）。 */
typedef enum XMdiAreaAreaOption
{
    XMdiAreaAreaOption_AllowTabbedView = 0x1, /**< 允许切换到选项卡视图。 */
    XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation = 0x2 /**< 激活时不最大化子窗口。 */
} XMdiAreaAreaOption;

/** @brief 子窗口排列顺序（对标 QMdiArea::WindowOrder，数值一致）。 */
typedef enum XMdiAreaWindowOrder
{
    XMdiAreaWindowOrder_CreationOrder = 0,       /**< 按创建顺序。 */
    XMdiAreaWindowOrder_StackingOrder = 1,       /**< 按堆叠顺序。 */
    XMdiAreaWindowOrder_ActivationHistoryOrder = 2 /**< 按激活历史顺序。 */
} XMdiAreaWindowOrder;

/** @brief 子窗口选项位（对标 QMdiSubWindow::SubWindowOption，数值一致）。 */
typedef enum XMdiSubWindowOption
{
    XMdiSubWindowOption_AllowOutsideAreaHorizontally = 0x1, /**< 允许水平越出区域。 */
    XMdiSubWindowOption_AllowOutsideAreaVertically = 0x2,   /**< 允许垂直越出区域。 */
    XMdiSubWindowOption_RubberBandResize = 0x4,             /**< 橡皮筋缩放。 */
    XMdiSubWindowOption_RubberBandMove = 0x8                /**< 橡皮筋移动。 */
} XMdiSubWindowOption;

/** @brief 子窗口状态位（对标 Qt::WindowStates 中与本类相关的位；数值一致）。
 *  Qt::WindowShaded == 0x20。 */
#define XMdiSubWindowState_Shaded 0x20 /**< 折叠（仅标题条）。 */

/* 前向声明：XMdiSubWindow_mdiArea 返回 XMdiArea*，而 XMdiArea 定义在本
 * 文件的 XMdiSubWindow 段之后。 */
typedef struct XMdiArea XMdiArea;

/* ==================== XMdiSubWindow ==================== */

XCLASS_DEFINE_BEGING(XMdiSubWindow)
XCLASS_DEFINE_EXTEND_END(XMdiSubWindow, XWidget)

typedef struct XMdiSubWindow
{
    XWidget m_base;      /**< 基类成员；必须是第一个。 */
    XWidget* m_widget;   /**< 内容控件（借用，归 sub window）。 */
    XString* m_title;   /**< 标题条文本（对象拥有）。 */
    XMenu* m_systemMenu; /**< 系统菜单（对标 QMdiSubWindow::systemMenu；对象拥有）。 */
    int m_options;       /**< 选项位组合（XMdiSubWindowOption）。 */
    int m_keyboardSingleStep; /**< 键盘单步移动像素（对标 keyboardSingleStep）。 */
    int m_keyboardPageStep;   /**< 键盘页步移动像素（对标 keyboardPageStep）。 */
    bool m_shaded;       /**< 是否处于折叠（仅标题条）状态。 */
    int m_state;         /**< 当前窗口状态位（0=Normal；其余位后续扩展）。 */
} XMdiSubWindow;

/** @brief XMdi子Windowclassinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XMdiSubWindow_class_init(void);
void XMdiSubWindow_init(XMdiSubWindow* self, XWidget* parent,
                        XWidgetFlags flags);
#define XMdiSubWindow_create(parent, flags) XMdiSubWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/** @brief aboutToActivate() 信号（对标 QMdiSubWindow::aboutToActivate；
 *         在子窗口被激活之前发射）。 */
void* XMdiSubWindow_aboutToActivate_signal(XMdiSubWindow* self);
/** @brief windowStateChanged(int,int) 信号（对标
 *         QMdiSubWindow::windowStateChanged(oldState,newState)；
 *         载荷：旧状态位、新状态位）。 */
void* XMdiSubWindow_windowStateChanged_signal(XMdiSubWindow* self,
                                              int oldState, int newState);

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
/** @brief 设置子窗口选项位（对标 QMdiSubWindow::setOption）。
 * @param self 目标子窗口。
 * @param option 选项位（XMdiSubWindowOption）。
 * @param on true 置位，false 清位。
 * @return 无返回值。
 */
void XMdiSubWindow_setOption(XMdiSubWindow* self, int option, bool on);
/** @brief 查询子窗口选项位（对标 QMdiSubWindow::testOption）。
 * @param self 目标子窗口。
 * @param option 选项位（XMdiSubWindowOption）。
 * @return 置位返回 true。
 */
bool XMdiSubWindow_testOption(const XMdiSubWindow* self, int option);
/** @brief 设置键盘单步移动像素（对标 setKeyboardSingleStep）。
 * @param self 目标子窗口。
 * @param step 像素数。
 * @return 无返回值。
 */
void XMdiSubWindow_setKeyboardSingleStep(XMdiSubWindow* self, int step);
/** @brief 查询键盘单步移动像素（对标 keyboardSingleStep）。
 * @param self 目标子窗口。
 * @return 像素数。
 */
int XMdiSubWindow_keyboardSingleStep(const XMdiSubWindow* self);
/** @brief 设置键盘页步移动像素（对标 setKeyboardPageStep）。
 * @param self 目标子窗口。
 * @param step 像素数。
 * @return 无返回值。
 */
void XMdiSubWindow_setKeyboardPageStep(XMdiSubWindow* self, int step);
/** @brief 查询键盘页步移动像素（对标 keyboardPageStep）。
 * @param self 目标子窗口。
 * @return 像素数。
 */
int XMdiSubWindow_keyboardPageStep(const XMdiSubWindow* self);
/** @brief 查询是否处于折叠状态（对标 QMdiSubWindow::isShaded）。
 * @param self 目标子窗口。
 * @return 折叠返回 true。
 */
bool XMdiSubWindow_isShaded(const XMdiSubWindow* self);
/** @brief 折叠/还原子窗口为仅标题条（对标槽 showShaded）。
 * @param self 目标子窗口。
 * @return 无返回值。
 */
void XMdiSubWindow_showShaded(XMdiSubWindow* self);
/** @brief 在标题条位置弹出系统菜单（对标槽 showSystemMenu）。
 * @param self 目标子窗口。
 * @return 无返回值。
 */
void XMdiSubWindow_showSystemMenu(XMdiSubWindow* self);
/** @brief 设置系统菜单（对标 setSystemMenu；取得菜单所有权）。
 * @param self 目标子窗口。
 * @param systemMenu 系统菜单；NULL 表示无系统菜单。
 * @return 无返回值。
 */
void XMdiSubWindow_setSystemMenu(XMdiSubWindow* self, XMenu* systemMenu);
/** @brief 查询系统菜单（对标 systemMenu）。
 * @param self 目标子窗口。
 * @return 借用指针；未设置返回 NULL。
 */
XMenu* XMdiSubWindow_systemMenu(const XMdiSubWindow* self);
/** @brief 反查所属 MDI 区域（对标 QMdiSubWindow::mdiArea）。
 * @param self 目标子窗口。
 * @return 借用指针；非 MDI 子窗口返回 NULL。
 */
XMdiArea* XMdiSubWindow_mdiArea(const XMdiSubWindow* self);
/** @brief 尺寸提示（对标 QSize sizeHint）。
 * @param self 目标子窗口。
 * @return 建议尺寸。
 */
XSize XMdiSubWindow_sizeHint(const XMdiSubWindow* self);
/** @brief 最小尺寸提示（对标 minimumSizeHint）。
 * @param self 目标子窗口。
 * @return 建议最小尺寸。
 */
XSize XMdiSubWindow_minimumSizeHint(const XMdiSubWindow* self);
/** @brief 最大化时按钮控件（对标 QMdiSubWindow::maximizedButtonsWidget；
 *         该 API 在 Qt 中标记 internal）。
 * @param self 目标子窗口。
 * @return 本项目无独立按钮控件实例，恒返回 NULL。
 */
XWidget* XMdiSubWindow_maximizedButtonsWidget(const XMdiSubWindow* self);
/** @brief 最大化时系统菜单图标控件（对标
 *         QMdiSubWindow::maximizedSystemMenuIconWidget；Qt 中标记 internal）。
 * @param self 目标子窗口。
 * @return 本项目无独立按钮控件实例，恒返回 NULL。
 */
XWidget* XMdiSubWindow_maximizedSystemMenuIconWidget(const XMdiSubWindow* self);

/* ==================== XMdiArea ==================== */

XCLASS_DEFINE_BEGING(XMdiArea)
XCLASS_DEFINE_EXTEND_END(XMdiArea, XAbstractScrollArea)

typedef struct XMdiArea
{
    XAbstractScrollArea m_base; /**< 基类成员；必须是第一个。 */
    XVector* m_subWindows; /**< 子窗口数组（XMdiSubWindow*，拥有）。 */
    XMdiSubWindow* m_active; /**< 当前活动子窗口（借用）。 */
    int m_viewMode;      /**< 视图模式（XMdiAreaViewMode）。 */
    uint32_t m_background; /**< 背景画刷色（ARGB；0=默认）。 */
    int m_tabPosition;    /**< 页签位置（Tabbed 模式）。 */
    int m_tabShape;       /**< 页签形状（Tabbed 模式；0=Rounded，1=Triangular）。 */
    bool m_tabsMovable;   /**< 页签可拖（Tabbed 模式）。 */
    bool m_tabsClosable;  /**< 页签可关（Tabbed 模式）。 */
    int m_activationOrder; /**< 激活顺序（XMdiAreaWindowOrder）。 */
    int m_options;        /**< 选项位组合（XMdiAreaAreaOption）。 */
    bool m_documentMode;  /**< 文档模式（对标 documentMode）。 */
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
/** @brief 当前活动子窗口（对标 currentSubWindow）。
 * @param self 目标区域。
 * @return 借用指针；无活动返回 NULL。
 */
XMdiSubWindow* XMdiArea_currentSubWindow(const XMdiArea* self);
/** @brief 设置背景画刷色（对标 setBackground；ARGB）。
 * @param self 目标区域。
 * @param color ARGB；0=默认。
 * @return 无返回值。
 */
void XMdiArea_setBackground(XMdiArea* self, uint32_t color);
/** @brief 查询背景画刷色。 @param self 目标区域。 @return ARGB。 */
uint32_t XMdiArea_background(const XMdiArea* self);
/** @brief 设置页签位置（Tabbed 模式；对标 setTabPosition）。
 * @param self 目标区域。
 * @param position 位置码。
 * @return 无返回值。
 */
void XMdiArea_setTabPosition(XMdiArea* self, int position);
/** @brief 查询页签位置。 @param self 目标区域。 @return 位置码。 */
int XMdiArea_tabPosition(const XMdiArea* self);
/** @brief 设置页签形状（Tabbed 模式；对标 setTabShape）。
 * @param self 目标区域。
 * @param shape 形状码（0=Rounded，1=Triangular）。
 * @return 无返回值。
 */
void XMdiArea_setTabShape(XMdiArea* self, int shape);
/** @brief 查询页签形状。 @param self 目标区域。 @return 形状码。 */
int XMdiArea_tabShape(const XMdiArea* self);
/** @brief 设置页签可拖（对标 setTabsMovable）。
 * @param self 目标区域。
 * @param movable true 可拖。
 * @return 无返回值。
 */
void XMdiArea_setTabsMovable(XMdiArea* self, bool movable);
/** @brief 查询页签可拖。 @param self 目标区域。 @return 可拖返回 true。 */
bool XMdiArea_tabsMovable(const XMdiArea* self);
/** @brief 设置页签可关（对标 setTabsClosable）。
 * @param self 目标区域。
 * @param closable true 可关。
 * @return 无返回值。
 */
void XMdiArea_setTabsClosable(XMdiArea* self, bool closable);
/** @brief 查询页签可关。 @param self 目标区域。 @return 可关返回 true。 */
bool XMdiArea_tabsClosable(const XMdiArea* self);
/** @brief 设置激活顺序（对标 setActivationOrder）。
 * @param self 目标区域。
 * @param order 顺序码（XMdiAreaWindowOrder）。
 * @return 无返回值。
 */
void XMdiArea_setActivationOrder(XMdiArea* self, int order);
/** @brief 查询激活顺序。 @param self 目标区域。 @return 顺序码。 */
int XMdiArea_activationOrder(const XMdiArea* self);
/** @brief 设置区域选项位（对标 QMdiArea::setOption）。
 * @param self 目标区域。
 * @param option 选项位（XMdiAreaAreaOption）。
 * @param on true 置位，false 清位。
 * @return 无返回值。
 */
void XMdiArea_setOption(XMdiArea* self, int option, bool on);
/** @brief 查询区域选项位（对标 QMdiArea::testOption）。
 * @param self 目标区域。
 * @param option 选项位（XMdiAreaAreaOption）。
 * @return 置位返回 true。
 */
bool XMdiArea_testOption(const XMdiArea* self, int option);
/** @brief 设置文档模式（对标 QMdiArea::setDocumentMode）。
 * @param self 目标区域。
 * @param enabled true 启用。
 * @return 无返回值。
 */
void XMdiArea_setDocumentMode(XMdiArea* self, bool enabled);
/** @brief 查询文档模式（对标 QMdiArea::documentMode）。
 * @param self 目标区域。
 * @return 启用返回 true。
 */
bool XMdiArea_documentMode(const XMdiArea* self);
/**
 * @brief      获取活动子窗口。
 */
XMdiSubWindow* XMdiArea_activeSubWindow(const XMdiArea* self);
/**
 * @brief      设置活动子窗口。
 */
void XMdiArea_setActiveSubWindow(XMdiArea* self, XMdiSubWindow* window);
/**
 * @brief      关闭活动子窗口（对标槽 closeActiveSubWindow）。
 */
void XMdiArea_closeActiveSubWindow(XMdiArea* self);
/**
 * @brief      激活下一个子窗口（对标槽 activateNextSubWindow）。
 */
void XMdiArea_activateNextSubWindow(XMdiArea* self);
/**
 * @brief      激活上一个子窗口（对标槽 activatePreviousSubWindow）。
 */
void XMdiArea_activatePreviousSubWindow(XMdiArea* self);
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

#endif /* XMDIAREA_H */