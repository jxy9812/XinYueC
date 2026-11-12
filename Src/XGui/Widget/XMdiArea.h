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
 *             window 归 area；内部创建的子窗带 DeleteOnClose 属性，
 *             closeAllSubWindows 按关闭事件语义回收（接管来的子窗只隐藏、
 *             不释放——见各接口 @note）。
 * @note       子窗标题带的系统菜单图标/最小化/关闭按钮已接真实交互：
 *             按钮命中矩形与绘制同源（关闭 → close 链，最小化 →
 *             showMinimized 呈现），显隐受 windowFlags 提示位约束；
 *             折叠/最小化的恢复经基类 WindowStateChange 联动
 *             （showNormal / setWindowState(NoState)）。
 * @note       模块总开关 XMDIAREA_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XMDIAREA_H
#define XMDIAREA_H

#if XGUI_ON
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
#include "XBrush.h" /* 背景画刷值句柄（setBackground_2；P3-54）。 */
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

/** @brief 区域选项位（对标 QMdiArea::AreaOption，数值一致）。
 *  @note   Qt 6.8（qmdiarea.h:36-38）只有
 *          DontMaximizeSubWindowOnActivation = 0x1 一个选项；页签视图是
 *          ViewMode（setViewMode），不是 Option。X 独有的
 *          AllowTabbedView(0x1) 已删除——其旧位值 0x1/0x2 与 Qt 冲突，
 *          按 Qt 位值传 0x1 的调用会静默落错位（P0-51）。 */
typedef enum XMdiAreaAreaOption
{
    XMdiAreaAreaOption_DontMaximizeSubWindowOnActivation = 0x1 /**< 激活时不最大化子窗口。 */
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
    /* ---- 指针组（8 字节对齐，置顶） ---- */
    XWidget* m_widget;   /**< 内容控件（借用，归 sub window）。 */
    /* 标题条文本**不另设字段**：对标 QMdiSubWindow 继承 QWidget 的
     * windowTitle 属性（qmdisubwindow.cpp:994 的标题同步链与
     * paintEvent 均读 windowTitle），统一由基类
     * XWidget_windowTitle / XWidget_setWindowTitle 承载。 */
    XMenu* m_systemMenu; /**< 系统菜单（对标 QMdiSubWindow::systemMenu；
                          *   对象拥有；构造即建标准菜单，见
                          *   XMdiSubWindow_systemMenu 头注）。 */
    XString* m_lastChildTitle; /**< 内容件上一次的 windowTitle（拥有；对标
                                *   QMdiSubWindowPrivate::lastChildWindowTitle，
                                *   qmdisubwindow.cpp:1852-1854 的跟随判定：
                                *   子窗标题仍等于该值时继续跟随内容件标题
                                *   变化，否则视为用户自定义标题不再覆盖）。 */
    /* ---- 4 字节组（有符号：Qt 允许 0=不动、负值=反向，
     *      见 qmdisubwindow.cpp:2402-2407 的显式注释） ---- */
    int32_t m_keyboardSingleStep; /**< 键盘单步移动像素（对标 keyboardSingleStep；初值 5）。 */
    int32_t m_keyboardPageStep;   /**< 键盘页步移动像素（对标 keyboardPageStep；初值 20）。 */
    XRect m_restoreGeometry;      /**< 折叠/最小化前的几何（对标
                                   *   QMdiSubWindowPrivate::restoreSize +
                                   *   oldGeometry：showShaded/setMinimizeMode
                                   *   时记录，setNormalMode 恢复，
                                   *   qmdisubwindow.cpp:2586-2625）。 */
    /* ---- 1 字节组 ---- */
    uint8_t m_options;       /**< 选项位组合（XMdiSubWindowOption，最大 0x8；
                                *   setOption 只按位或/与单选项位）。 */
    uint8_t m_state;         /**< 当前窗口状态位（0=Normal；其余位后续扩展）。 */
    uint8_t m_shaded : 1;    /**< 是否处于折叠（仅标题条）状态。 */
    uint8_t m_minimized : 1; /**< 是否处于最小化（图标态）呈现。 */
    uint8_t m_contentHiddenByUs : 1; /**< 内容件是否被折叠/最小化流程隐藏
                                      *   （对标 isWidgetHiddenByUs，
                                      *   qmdisubwindow.cpp:2612-2616）。 */
    uint8_t m_restoreValid : 1;      /**< m_restoreGeometry 是否有效。 */
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
/** @brief 设置子窗口标题条文本（对标 QWidget::setWindowTitle 在
 *         QMdiSubWindow 上的用法）。
 * @details Qt 的 QMdiSubWindow **没有**独立标题字段：标题条绘制与
 *          windowTitle() 同源（qmdisubwindow.cpp:994、3086-3138）。
 *          本接口即基类 XWidget_setWindowTitle 的 UTF-8 便捷重载，
 *          写入后经 XWidget_windowTitle 读回，二者双向可见。
 * @param self 目标控件指针。
 * @param utf8 UTF-8 文本；NULL 按空串处理。
 * @return 无返回值。
 */
void XMdiSubWindow_setWindowTitle_2(XMdiSubWindow* self, const char* utf8);
/** @brief 读取子窗口标题条文本（对标 QWidget::windowTitle）。
 * @param self 目标控件指针。
 * @return 基类 windowTitle 的 UTF-8 借用视图；未设置返回空串。
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
 * @details 对标 Qt 原样存储**任意**整数（qmdisubwindow.cpp:2402-2407：
 *          "Haven't done any boundary check here since negative step only
 *          means inverted behavior... A step equal to zero means do
 *          nothing"）：0 与负值不做钳制，读回同值。
 * @param self 目标子窗口。
 * @param step 像素数（可 0/负）。
 * @return 无返回值。
 */
void XMdiSubWindow_setKeyboardSingleStep(XMdiSubWindow* self, int step);
/** @brief 查询键盘单步移动像素（对标 keyboardSingleStep）。
 * @param self 目标子窗口。
 * @return 像素数。
 */
int XMdiSubWindow_keyboardSingleStep(const XMdiSubWindow* self);
/** @brief 设置键盘页步移动像素（对标 setKeyboardPageStep）。
 * @details 同 setKeyboardSingleStep：0/负值原样存储（Qt 同）。
 * @param self 目标子窗口。
 * @param step 像素数（可 0/负）。
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
/** @brief 折叠子窗口为仅标题条（对标槽 showShaded）。
 * @details Qt 6.8 语义（qmdisubwindow.cpp:2553-2646）：
 *          ① **无父窗口直接返回**（`if (!parent()) return;`）；
 *          ② **已是折叠态再调也返回**（`if (... && isShaded()) return;`）
 *             ——不是开关；
 *          ③ 进入折叠：记录恢复几何、把基类窗口状态置为
 *             WindowMinimized（Qt 用 ensureWindowState），隐藏内容件，
 *             高度缩到标题带、宽度保持。
 *          取消折叠的路径是 `XWidget_showNormal` /
 *          `XWidget_setWindowState(sw, XWindowState_NoState)`（Qt 的
 *          setWindowState(NoState) → setNormalMode，
 *          qmdisubwindow.cpp:1246-1288）：本类经 changeEvent 监听
 *          WindowStateChange，Minimized 位清除时按记录的几何恢复。
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
 * @details 构造即建标准系统菜单（对标 QMdiSubWindow 构造里的
 *          createSystemMenu，qmdisubwindow.cpp:2202-2210 /
 *          2321-2350），条目：Restore / Minimize / Maximize /
 *          Stay on &Top（可选中）/ 分隔线 / &Close，已接到真实动作
 *          （showNormal/showMinimized/showMaximized/WindowStaysOnTopHint
 *          翻转/close）。Qt 的 &Move / &Size 需要交互式移动/缩放模式，
 *          本框架未建该模式（标题带拖动即移动），故不列出（不留无效项）。
 * @param self 目标子窗口。
 * @param systemMenu 系统菜单；NULL 表示删除系统菜单（Qt 同）。
 * @return 无返回值。
 */
void XMdiSubWindow_setSystemMenu(XMdiSubWindow* self, XMenu* systemMenu);
/** @brief 查询系统菜单（对标 systemMenu）。
 * @param self 目标子窗口。
 * @return 借用指针；XMENU_ON=0 或无菜单时为 NULL。
 */
XMenu* XMdiSubWindow_systemMenu(const XMdiSubWindow* self);
/** @brief 反查所属 MDI 区域（对标 QMdiSubWindow::mdiArea）。
 * @param self 目标子窗口。
 * @return 借用指针；非 MDI 子窗口返回 NULL。
 */
XMdiArea* XMdiSubWindow_mdiArea(const XMdiSubWindow* self);
/* ==================== 父类 API 宏转发（对齐库内 XDial 惯例） ==================== */

/* 按类型名调用的别名；实际是 XWidget 虚分派入口（《api命名风格》*_base
 * 契约：业务在 VXMdiSubWindow_sizeHint/VXMdiSubWindow_minimumSizeHint
 * 虚表默认槽——Qt 6.8 口径见 qmdisubwindow.cpp:3451-3462/:3465-3510，
 * 对外唯一入口是分派 `XWidget_sizeHint_base`/`XWidget_minimumSizeHint_base`）。 */
#define XMdiSubWindow_sizeHint_base(self) XWidget_sizeHint_base((const XWidget*)(self))
#define XMdiSubWindow_minimumSizeHint_base(self) XWidget_minimumSizeHint_base((const XWidget*)(self))
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
    /* ---- 指针组（8 字节对齐，置顶） ---- */
    XVector* m_subWindows; /**< 子窗口数组（XMdiSubWindow*，拥有）。 */
    XVector* m_activationHistory; /**< 激活历史（XMdiSubWindow*，借用；
                                   *   最近激活在前，对标
                                   *   QMdiAreaPrivate::indicesToActivatedChildren
                                   *   的 prepend/move-to-front 维护，
                                   *   qmdiarea.cpp:812、1018-1024、
                                   *   1105-1113；subWindowList 的
                                   *   ActivationHistoryOrder 即其逆序）。 */
    XMdiSubWindow* m_active; /**< 当前活动子窗口（借用）。 */
    XWidget* m_tabBar;       /**< 区域页签条（XTabBar*，区域子控件、随区域
                              *   级联销毁；Tabbed 视图的唯一一条，页签=
                              *   子窗列表，currentChanged →
                              *   setActiveSubWindow、tabCloseRequested →
                              *   关闭对应子窗、tabMoved → 子窗表平移；
                              *   P3-53，对标 Qt 页签视图的内部 QTabBar）。 */
    XVector* m_tabbedRestore; /**< 进 Tabbed 视图时的子窗几何快照（元素
                               *   为实现文件内部的 {子窗指针, XRect} 对，
                               *   进 Tabbed 捕获、切回 SubWindowView 回放
                               *   后清空；对标 Qt 切回子窗视图对最大化
                               *   子窗 showNormal 的还原语义，P3-53）。 */
    /* ---- 8 字节组：背景画刷值句柄 ---- */
    XBrush m_backgroundBrush; /**< 背景画刷（值句柄；NoBrush=未设，
                               *   绘制期优先于 m_background 便捷色；
                               *   对标 QBrush background，P3-54）。 */
    /* ---- 4 字节组 ---- */
    uint32_t m_background; /**< 背景画刷色（ARGB；0=默认；纯色画刷视图，
                            *   见 setBackground/setBackground_2）。 */
    /* ---- 1 字节组 ---- */
    uint8_t m_viewMode;      /**< 视图模式（XMdiAreaViewMode，0=SubWindow/1=Tabbed）。 */
    uint8_t m_tabPosition;    /**< 页签位置（Tabbed 模式；setter 原样存取外部
                                  *   位置码，按对齐保持 uint8_t 承载）。 */
    uint8_t m_tabShape;       /**< 页签形状（Tabbed 模式；0=Rounded，1=Triangular）。 */
    uint8_t m_activationOrder; /**< 激活顺序（XMdiAreaWindowOrder，0..2）。 */
    uint8_t m_options;        /**< 选项位组合（XMdiAreaAreaOption，最大 0x1；
                                  *   setOption 只按位或/与单选项位）。 */
    /* ---- 位域组（4 个布尔打包 1 字节） ---- */
    uint8_t m_tabsMovable : 1;   /**< 页签可拖（Tabbed 模式，下发区域页签条）。 */
    uint8_t m_tabsClosable : 1;  /**< 页签可关（Tabbed 模式，下发区域页签条）。 */
    uint8_t m_documentMode : 1;  /**< 文档模式（Tabbed 模式，下发区域页签条）。 */
    uint8_t m_inTabBarSync : 1;  /**< 页签条同步重入保护（回填 current 下标/
                                  *   重建页签时置位，抑制 currentChanged
                                  *   槽递归；P3-53）。 */
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

/**
 * @brief      添加子窗口（对标 QMdiArea::addSubWindow）。
 * @details    两种入口与 Qt 同构（qmdiarea.cpp:1936-1975）：
 *             ① widget 是普通控件 ⇒ 新建 XMdiSubWindow 外壳、把 widget
 *                作为内容装入，并给外壳置 XWidgetAttribute_DeleteOnClose
 *                （对标 `child->setAttribute(Qt::WA_DeleteOnClose)`）——
 *                关闭即回收，closeAllSubWindows/closeActiveSubWindow 据此
 *                判定是否从列表摘除；控件随外壳级联释放；
 *             ② widget **本身已是 XMdiSubWindow** ⇒ 直接接管（父链挂到
 *                视口并入列），不新建外壳、不动其内容件、不置
 *                DeleteOnClose；已在区域内时原样返回（Qt 同）。
 *             新子窗按级联步进摆放并取得激活。
 * @param self 目标区域；NULL 或 widget 为 NULL 时返回 NULL。
 * @param widget 内容控件或子窗口借用指针（所有权转交区域）。
 * @return 子窗口指针（区域拥有）；失败返回 NULL。
 */
XMdiSubWindow* XMdiArea_addSubWindow(XMdiArea* self, XWidget* widget);
/** @brief 添加子窗口（带窗口标志重载，对标 QMdiArea::addSubWindow 的
 *         windowFlags 形参；C 无默认参数，本重载即 Qt 缺省实参形态的
 *         显式版）。
 * @details 与 XMdiArea_addSubWindow 的两种入口同构，差异只在窗口标志：
 *          ① widget 是普通控件 ⇒ 新建 XMdiSubWindow 外壳时以 `flags`
 *          为窗口标志（对标 Qt 把调用方 windowFlags 传给
 *          QMdiSubWindow(viewport(), windowFlags)），0 = 既有行为；
 *          ② widget **本身已是 XMdiSubWindow** ⇒ 以
 *          `flags ? flags : widget 自身 windowFlags` 为基做既有类型位
 *          规范化（Qt 同式 `windowFlags ? windowFlags :
 *          child->windowFlags()`）。原入口等价
 *          `XMdiArea_addSubWindow_2(self, widget, 0)`。
 * @param self 目标区域；NULL 或 widget 为 NULL 时返回 NULL。
 * @param widget 内容控件或子窗口借用指针（所有权转交区域）。
 * @param flags 新建外壳的窗口标志（XWidgetFlags 位组合；0=缺省）。
 * @return 子窗口指针（区域拥有）；失败返回 NULL。
 */
XMdiSubWindow* XMdiArea_addSubWindow_2(XMdiArea* self, XWidget* widget,
                                       XWidgetFlags flags);
/**
 * @brief      移除子窗口（对标 QMdiArea::removeSubWindow）。
 * @details    与 Qt 6.8 文档/实现同构（qmdiarea.cpp:1970-2016）：
 *             ① 传入**子窗口本身** ⇒ 摘父链（setParent(NULL)）并移出
 *                列表，**不删除对象**（Qt：「widget is never actually
 *                deleted by QMdiArea」）；活动窗被移出后按
 *                updateActiveWindow 口径改激活下一个可见子窗，无候选则
 *                发 subWindowActivated(NULL)；
 *             ② 传入的是某子窗的**内容控件** ⇒ 只把该内容件从子窗摘除
 *                （`child->setWidget(nullptr)`），**子窗口保留**在
 *                subWindowList 中（Qt 官方文档明写 "the QMdiSubWindow is
 *                *not* removed"）；内容件摘链后归调用方，不删除。
 * @param self 目标区域。
 * @param widget 子窗口或内容控件借用指针。
 * @return 无返回值。
 */
void XMdiArea_removeSubWindow(XMdiArea* self, XWidget* widget);
/** @brief 获取子窗口列表（按创建顺序；借用指针）。
 * @details 对标 Qt `subWindowList(WindowOrder order = CreationOrder)` 的
 *          **缺省实参形态**：只按创建顺序返回内部数组借用指针。需要
 *          堆叠顺序/激活历史顺序用 XMdiArea_subWindowList_2。
 * @param self 目标区域。
 * @return 内部 XVector<XMdiSubWindow*> 借用指针；修改登记后失效。
 */
const XVector* XMdiArea_subWindowList(const XMdiArea* self);
/** @brief 获取指定顺序的子窗口列表（新建数组，调用方拥有）。
 * @details 对标 QMdiArea::subWindowList(WindowOrder)（qmdiarea.h:62、
 *          qmdiarea.cpp:1344-1391）：
 *          - CreationOrder：登记（创建）顺序；
 *          - StackingOrder：视口子控件顺序（叠加次序），即
 *            viewport->children() 顺序的 X 等价物——本框架的
 *            XWidget_raise/lower/stackUnder 正是在父控件子数组中移动，
 *            顺序与 Qt 的 children 顺序同构（本机 Qt 6.8.0 探针：
 *            add a,b,c 后 [a,b,c]；activate(a) 后 [b,c,a]；raise(a) 后
 *            [c,b,a]）；
 *          - ActivationHistoryOrder：激活历史由旧到新（qmdiarea.cpp:
 *            1372-1385 按 indicesToActivatedChildren 逆序输出；探针：
 *            add a,b,c → [a,b,c]，激活 a → [b,c,a]，再激活 b →
 *            [c,a,b]）。
 * @param self 目标区域；可为 NULL（返回空数组）。
 * @param order 顺序码（XMdiAreaWindowOrder）。
 * @return 新建 XVector<XMdiSubWindow*>，调用方须 XClassDelete；分配
 *         失败返回 NULL。
 */
XVector* XMdiArea_subWindowList_2(const XMdiArea* self, int order);
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
/** @brief 按画刷设置背景（对标 setBackground(const QBrush&)）。
 * @details 画刷非 NoBrush 时绘制期按该刷填脏区（纯色/渐变经
 *          XBrush_applyToPainter）；纯色刷同时把便捷色视图
 *          （XMdiArea_background）同步为刷色，非纯色刷（渐变/纹理）
 *          便捷色视图归 0。NoBrush 等价 XMdiArea_setBackground(0)
 *          （回退调色板 Base）。
 * @param self 目标区域；可为 NULL。
 * @param brush 画刷值借用指针；NULL 等价清除（NoBrush）。
 * @return 无返回值。
 */
void XMdiArea_setBackground_2(XMdiArea* self, const XBrush* brush);
/** @brief 查询背景画刷（对标 background() 返回 QBrush 的形态）。
 * @param self 目标区域；可为 NULL。
 * @return 内部画刷借用指针（值句柄，随 setBackground(_2) 变化）；self
 *         为 NULL 返回 NULL。未设置时样式为 NoBrush。
 */
const XBrush* XMdiArea_background_2(const XMdiArea* self);
/** @brief 设置页签位置（Tabbed 模式；对标 setTabPosition）。
 * @param self 目标区域。
 * @param position 位置码。
 * @return 无返回值。
 */
void XMdiArea_setTabPosition(XMdiArea* self, int position);
/** @brief 查询页签位置。 @param self 目标区域。 @return 位置码。 */
int XMdiArea_tabPosition(const XMdiArea* self);
/** @brief 设置页签形状（Tabbed 模式；对标 setTabShape）。
 * @details Tabbed 视图下同步下发区域页签条（XTabBar_setShape，形状
 *          绘制消费由页签条承载；取值域 Qt::TabShape 0..7）。
 * @param self 目标区域。
 * @param shape 形状码（0=Rounded，1=Triangular）。
 * @return 无返回值。
 */
void XMdiArea_setTabShape(XMdiArea* self, int shape);
/** @brief 查询页签形状。 @param self 目标区域。 @return 形状码。 */
int XMdiArea_tabShape(const XMdiArea* self);
/** @brief 设置页签可拖（对标 setTabsMovable）。
 * @details Tabbed 视图下同步下发区域页签条（XTabBar_setMovable，拖拽
 *          交换由页签条承载）。
 * @param self 目标区域。
 * @param movable true 可拖。
 * @return 无返回值。
 */
void XMdiArea_setTabsMovable(XMdiArea* self, bool movable);
/** @brief 查询页签可拖。 @param self 目标区域。 @return 可拖返回 true。 */
bool XMdiArea_tabsMovable(const XMdiArea* self);
/** @brief 设置页签可关（对标 setTabsClosable）。
 * @details Tabbed 视图下同步下发区域页签条（XTabBar_setTabsClosable，
 *          页签关闭按钮绘制/命中由页签条承载）。
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
 * @details Tabbed 视图下同步下发区域页签条（XTabBar_setDocumentMode，
 *          扁平/无基底绘制消费由页签条承载）。
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
 * @details    等价 `active->close()`（qmdiarea.cpp:1824-1828）：走关闭事件，
 *             **被拒绝时保持激活**；接受后内部创建的子窗（带
 *             DeleteOnClose）移出列表并延迟释放，接管来的子窗仅隐藏，
 *             随后按 updateActiveWindow 激活下一个可见子窗（无则发
 *             subWindowActivated(NULL)）。
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
 * @brief      关闭全部子窗口（对标槽 closeAllSubWindows）。
 * @details    逐窗发送关闭事件（qmdiarea.cpp:1857-1873）：**忽略关闭事件的
 *             子窗口保持打开**（Qt 文档明写）；接受关闭后，内部创建的子窗
 *             （带 DeleteOnClose 属性）移出列表并延迟释放，接管来的子窗
 *             仅隐藏且仍在 subWindowList 中（本机 Qt 6.8.0 实测同）。
 *             活动窗被关闭后激活下一个可见子窗，无剩余则发
 *             subWindowActivated(NULL)。
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
 * @brief      设置视图模式（对标 QMdiArea::setViewMode）。
 * @details    TabbedView：装配单一区域页签条（页签=子窗创建序列表），
 *             currentChanged → setActiveSubWindow、tabCloseRequested →
 *             关闭对应子窗，仅活动子窗可见、其余隐藏；SubWindowView：
 *             隐藏页签条、恢复全部子窗平铺（Qt 切回子窗视图语义）。
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
/* ==================== 父类 API 宏转发（对齐库内 XDial 惯例） ==================== */

/* 按类型名调用的别名；实际是 XWidget 虚分派入口（《api命名风格》*_base
 * 契约：业务在 VXmdiArea_sizeHint/VXmdiArea_minimumSizeHint 虚表默认槽
 * ——Qt 6.8 算法 qmdiarea.cpp:1710-1734/:1736-1750，对外唯一入口是分派
 * `XWidget_sizeHint_base`/`XWidget_minimumSizeHint_base`）。 */
#define XMdiArea_sizeHint_base(self) XWidget_sizeHint_base((const XWidget*)(self))
#define XMdiArea_minimumSizeHint_base(self) XWidget_minimumSizeHint_base((const XWidget*)(self))

/* ==================== 信号 ==================== */

/**
 * @brief      子窗口激活信号（真发射）。
 */
void* XMdiArea_subWindowActivated_signal(XMdiArea* self,
                                         XMdiSubWindow* window);

#endif /* XWIDGET_ON && XABSTRACTSCROLLAREA_ON && XMDIAREA_ON */

#endif /* XGUI_ON */

#endif /* XMDIAREA_H */
