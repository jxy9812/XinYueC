#include "XMainWindow.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XGuiConfig.h"
#include "XDockWidget_Protected.h"
#include "XStringUtils.h"

#include <string.h>

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#if XMENUBAR_ON
#include "XMenuBar.h"
#endif
#if XSTATUSBAR_ON
#include "XStatusBar.h"
#endif
#if XTOOLBAR_ON
#include "XToolBar.h"
#endif

#if XWIDGET_ON && XMAINWINDOW_ON

/* ==================== 内部布局 ==================== */

static int xmw_statusHeight(void) { return 24; }
static int xmw_menuHeight(void) { return 22; }

/**
 * @brief      在工具栏登记表中查找工具栏下标。
 * @param      self 目标主窗口；可为 NULL。
 * @param      toolbar 工具栏借用指针；可为 NULL。
 * @return     找到返回下标；未登记或参数无效返回 -1。
 */
static int64_t xmw_toolBarIndex(const XMainWindow* self,
                                const XWidget* toolbar)
{
    int64_t i;
    int64_t n;
    if (!self || !toolbar || !self->m_toolBars) return -1;
    n = XVector_size_base((const XContainer*)self->m_toolBars);
    for (i = 0; i < n; ++i) {
        XWidget* tb = XVector_At_Base(self->m_toolBars, i, XWidget*);
        if (tb == toolbar) return i;
    }
    return -1;
}

/**
 * @brief      判断工具栏登记表中的某项是否为断行哨兵条目。
 * @details    断行沿用 XMainWindow_addToolBarBreak 的既有表示：m_toolBars
 *             中一个 NULL 工具栏条目 + m_toolBarAreas 中对应的区域 0。
 * @param      self 目标主窗口；可为 NULL。
 * @param      index 登记表下标。
 * @return     该下标处是断行哨兵返回 true；越界或参数无效返回 false。
 */
static bool xmw_isBreakEntry(const XMainWindow* self, int64_t index)
{
    XWidget* tb;
    int* area;
    if (!self || !self->m_toolBars || !self->m_toolBarAreas) return false;
    if (index < 0 || index >= (int64_t)XVector_size_base(
                                 (const XContainer*)self->m_toolBars))
        return false;
    tb = XVector_At_Base(self->m_toolBars, index, XWidget*);
    area = (int*)XVector_at_base(self->m_toolBarAreas, index);
    return tb == NULL && area != NULL && *area == 0;
}

/**
 * @brief      判断工具栏登记项之前是否存在断行。
 * @details    对齐 Qt 的 QToolBarAreaLayout::toolBarBreak：工具栏位于首个
 *             断行段（登记表最前面的哨兵段）时不算有断行，因此要求哨兵
 *             条目之前还存在其它登记项。
 * @param      self 目标主窗口；可为 NULL。
 * @param      index 工具栏登记表下标。
 * @return     前一项为断行哨兵且该哨兵之前还有登记项时返回 true。
 */
static bool xmw_hasBreakBefore(const XMainWindow* self, int64_t index)
{
    return index > 1 && xmw_isBreakEntry(self, index - 1);
}

/**
 * @brief      判断工具栏登记项是否参与顶部布局。
 * @param      self 目标主窗口；可为 NULL。
 * @param      index 登记表下标。
 * @return     该项是 Top 区域的有效工具栏（断行哨兵除外）返回 true。
 */
static bool xmw_isTopToolBar(const XMainWindow* self, int64_t index)
{
    XWidget* tb;
    int* area;
    if (!self || !self->m_toolBars || !self->m_toolBarAreas) return false;
    if (index < 0 || index >= (int64_t)XVector_size_base(
                                 (const XContainer*)self->m_toolBars))
        return false;
    tb = XVector_At_Base(self->m_toolBars, index, XWidget*);
    area = (int*)XVector_at_base(self->m_toolBarAreas, index);
    return tb != NULL && area != NULL && *area == (int)XDockWidgetArea_Top;
}

/**
 * @brief      计算菜单栏与顶部工具栏占用的高度。
 * @param      self 目标主窗口；可为 NULL。
 * @return     顶部占用像素高度；参数无效返回 0。
 */
static int xmw_topUsed(const XMainWindow* self)
{
    int used = 0;
    int64_t i;
    int64_t n;
    if (!self) return 0;
    if (self->m_menuBar && XWidget_isVisible(self->m_menuBar))
        used += xmw_menuHeight();
    if (!self->m_toolBars) return used;
    n = XVector_size_base((const XContainer*)self->m_toolBars);
    for (i = 0; i < n; ++i)
        if (xmw_isTopToolBar(self, i)) used += 30;
    return used;
}

/**
 * @brief      计算状态栏占用的高度。
 * @param      self 目标主窗口；可为 NULL。
 * @return     状态栏占用像素高度；不可见或参数无效返回 0。
 */
static int xmw_bottomUsed(const XMainWindow* self)
{
    if (!self || !self->m_statusBar ||
        !XWidget_isVisible(self->m_statusBar))
        return 0;
    return XWidget_height(self->m_statusBar);
}

/**
 * @brief      判断某个停靠区域是否被占用。
 * @param      self 目标主窗口；可为 NULL。
 * @param      area 停靠区域码（XDockWidgetArea）。
 * @return     存在该区域的非浮动且未隐藏的停靠面板返回 true（区域内
 *             面板全部隐藏时区域不保留几何空间，对齐 Qt 行为）。
 */
static bool xmw_dockAreaUsed(const XMainWindow* self, int area)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_docks) return false;
    n = XVector_size_base((const XContainer*)self->m_docks);
    for (i = 0; i < n; ++i) {
        int* a = (int*)XVector_at_base(self->m_dockAreas, i);
        XDockWidget** d = (XDockWidget**)XVector_at_base(self->m_docks, i);
        if (a && d && *d && !(*d)->m_floating && *a == area &&
            !XWidget_isHidden((XWidget*)*d))
            return true;
    }
    return false;
}

/**
 * @brief      布局一条停靠列（左/右）内的可见面板行。
 * @details    参与布局的行 = 已登记、区域匹配、非浮动且显式显示的面板
 *             （标签组非活动成员已被隐藏，因此不占行）。行高优先取
 *             m_dockHeights 的覆盖值（对标 resizeDocks 的垂直尺寸），
 *             未覆盖的行均分剩余高度；覆盖值总和超过可用高度时按比例
 *             压缩。
 * @param      self 目标主窗口。
 * @param      area 列区域码（Left/Right）。
 * @param      x 列左边界。
 * @param      colW 列宽。
 * @param      top 列顶边界。
 * @param      bottom 列底边界。
 * @return     无返回值。
 */
static void xmw_layoutDockColumn(XMainWindow* self, int area, int x, int colW,
                                 int top, int bottom)
{
    XRect r;
    int64_t i;
    int64_t n;
    int64_t lastIndex = -1;
    int rows = 0;
    int fixed = 0;
    int fixedSum = 0;
    int avail;
    int autoHeight = 0;
    int y;
    if (!self || !self->m_docks || colW <= 0) return;
    if (bottom < top) bottom = top;
    avail = bottom - top;
    n = XVector_size_base((const XContainer*)self->m_docks);
    for (i = 0; i < n; ++i) {
        int* a = (int*)XVector_at_base(self->m_dockAreas, i);
        XDockWidget** d = (XDockWidget**)XVector_at_base(self->m_docks, i);
        int* oh = self->m_dockHeights
                      ? (int*)XVector_at_base(self->m_dockHeights, i) : NULL;
        if (!a || !d || !*d || *a != area || (*d)->m_floating) continue;
        if (XWidget_isHidden((XWidget*)*d)) continue;
        ++rows;
        lastIndex = i;
        if (oh && *oh > 0) {
            ++fixed;
            fixedSum += *oh;
        }
    }
    if (rows <= 0) return;
    {
        /* 指定值优先：无压缩时覆盖行高直接生效；总量超限按比例压缩；
         * 未覆盖的行均分剩余高度（最后一行吃掉整数均分的余量）。 */
        int compressed = fixedSum > avail;
        if (!compressed && rows > fixed)
            autoHeight = (avail - fixedSum) / (rows - fixed);
        y = top;
        for (i = 0; i < n; ++i) {
            int* a = (int*)XVector_at_base(self->m_dockAreas, i);
            XDockWidget** d =
                (XDockWidget**)XVector_at_base(self->m_docks, i);
            int* oh = self->m_dockHeights
                          ? (int*)XVector_at_base(self->m_dockHeights, i)
                          : NULL;
            int h;
            if (!a || !d || !*d || *a != area || (*d)->m_floating) continue;
            if (XWidget_isHidden((XWidget*)*d)) continue;
            if (oh && *oh > 0 && !compressed)
                h = *oh;
            else if (oh && *oh > 0)
                h = (int)((int64_t)*oh * avail / fixedSum);
            else if (i == lastIndex)
                h = bottom - y; /* 最后一行吃掉整数均分的余量 */
            else
                h = autoHeight;
            if (h < 0) h = 0;
            if (h > bottom - y) h = bottom - y;
            XRect_init(&r, x, y, colW, h);
            XWidget_setGeometryRect((XWidget*)*d, &r);
            y += h;
        }
    }
}

/**
 * @brief      布局一条停靠行（Top/Bottom）内的可见面板列。
 * @details    停靠列布局的转置模型（对标 Qt 停靠区行布局）：面板按登记
 *             顺序从左到右排列，宽度优先取 m_dockHeights 的覆盖值
 *             （resizeDocks Horizontal 对 Top/Bottom 面板的结果），未覆盖
 *             的均分剩余宽度；覆盖值总和超过可用宽度时按比例压缩。行高
 *             由调用方给定（m_topDockHeight/m_bottomDockHeight）。
 * @param      self 目标主窗口。
 * @param      area 行区域码（Top/Bottom）。
 * @param      y 行顶边界。
 * @param      rowH 行高。
 * @param      left 行左边界。
 * @param      right 行右边界。
 * @return     无返回值。
 */
static void xmw_layoutDockRow(XMainWindow* self, int area, int y, int rowH,
                              int left, int right)
{
    XRect r;
    int64_t i;
    int64_t n;
    int64_t lastIndex = -1;
    int cols = 0;
    int fixed = 0;
    int fixedSum = 0;
    int avail;
    int autoWidth = 0;
    int x;
    if (!self || !self->m_docks || rowH <= 0) return;
    if (right < left) right = left;
    avail = right - left;
    n = XVector_size_base((const XContainer*)self->m_docks);
    for (i = 0; i < n; ++i) {
        int* a = (int*)XVector_at_base(self->m_dockAreas, i);
        XDockWidget** d = (XDockWidget**)XVector_at_base(self->m_docks, i);
        int* ow = self->m_dockHeights
                      ? (int*)XVector_at_base(self->m_dockHeights, i) : NULL;
        if (!a || !d || !*d || *a != area || (*d)->m_floating) continue;
        if (XWidget_isHidden((XWidget*)*d)) continue;
        ++cols;
        lastIndex = i;
        if (ow && *ow > 0) {
            ++fixed;
            fixedSum += *ow;
        }
    }
    if (cols <= 0) return;
    {
        /* 指定值优先：无压缩时覆盖宽度直接生效；总量超限按比例压缩；
         * 未覆盖的列均分剩余宽度（最后一列吃掉整数均分的余量）。 */
        int compressed = fixedSum > avail;
        if (!compressed && cols > fixed)
            autoWidth = (avail - fixedSum) / (cols - fixed);
        x = left;
        for (i = 0; i < n; ++i) {
            int* a = (int*)XVector_at_base(self->m_dockAreas, i);
            XDockWidget** d =
                (XDockWidget**)XVector_at_base(self->m_docks, i);
            int* ow = self->m_dockHeights
                          ? (int*)XVector_at_base(self->m_dockHeights, i)
                          : NULL;
            int w;
            if (!a || !d || !*d || *a != area || (*d)->m_floating) continue;
            if (XWidget_isHidden((XWidget*)*d)) continue;
            if (ow && *ow > 0 && !compressed)
                w = *ow;
            else if (ow && *ow > 0)
                w = (int)((int64_t)*ow * avail / fixedSum);
            else if (i == lastIndex)
                w = right - x; /* 最后一列吃掉整数均分的余量 */
            else
                w = autoWidth;
            if (w < 0) w = 0;
            if (w > right - x) w = right - x;
            XRect_init(&r, x, y, w, rowH);
            XWidget_setGeometryRect((XWidget*)*d, &r);
            x += w;
        }
    }
}

static void xmw_layout(XMainWindow* self)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int top = 0;
    int bottom = 0;
    int leftW;
    int rightW;
    int topRowH;
    int bottomRowH;
    XRect r;
    int64_t i;
    int64_t n;
    if (!self) return;
    if (self->m_menuBar && XWidget_isVisible(self->m_menuBar)) {
        XRect_init(&r, 0, top, w, xmw_menuHeight());
        XWidget_setGeometryRect(self->m_menuBar, &r);
        top += xmw_menuHeight();
    }
    if (self->m_toolBars) {
        n = XVector_size_base((const XContainer*)self->m_toolBars);
        for (i = 0; i < n; ++i) {
            XToolBar** tb;
            if (!xmw_isTopToolBar(self, i)) continue;
            tb = (XToolBar**)XVector_at_base(self->m_toolBars, i);
            XRect_init(&r, 0, top, w, 30);
            XWidget_setGeometryRect((XWidget*)*tb, &r);
            top += 30;
        }
    }
    if (self->m_statusBar && XWidget_isVisible(self->m_statusBar)) {
        int sh = XWidget_height(self->m_statusBar);
        XRect_init(&r, 0, h - sh, w, sh);
        XWidget_setGeometryRect(self->m_statusBar, &r);
        bottom = sh;
    }
    /* Top/Bottom 停靠行（对标 QMainWindow 四区布局：Top 行位于工具栏
     * 之下、左右列与中央之上；Bottom 行位于状态栏之上、中央之下）。 */
    topRowH = xmw_dockAreaUsed(self, (int)XDockWidgetArea_Top)
                  ? self->m_topDockHeight : 0;
    bottomRowH = xmw_dockAreaUsed(self, (int)XDockWidgetArea_Bottom)
                     ? self->m_bottomDockHeight : 0;
    if (topRowH > h - bottom - top) topRowH = h - bottom - top > 0
                                                  ? h - bottom - top : 0;
    if (bottomRowH > h - bottom - top - topRowH)
        bottomRowH = h - bottom - top - topRowH > 0
                         ? h - bottom - top - topRowH : 0;
    xmw_layoutDockRow(self, (int)XDockWidgetArea_Top, top, topRowH, 0, w);
    top += topRowH;
    xmw_layoutDockRow(self, (int)XDockWidgetArea_Bottom,
                      h - bottom - bottomRowH, bottomRowH, 0, w);
    bottom += bottomRowH;
    /* 左/右停靠列：列宽可经 resizeDocks 覆盖，列内可见面板行堆叠。 */
    leftW = xmw_dockAreaUsed(self, (int)XDockWidgetArea_Left)
                ? self->m_leftDockWidth : 0;
    rightW = xmw_dockAreaUsed(self, (int)XDockWidgetArea_Right)
                 ? self->m_rightDockWidth : 0;
    xmw_layoutDockColumn(self, (int)XDockWidgetArea_Left, 0, leftW, top,
                         h - bottom);
    xmw_layoutDockColumn(self, (int)XDockWidgetArea_Right,
                         w - rightW > 0 ? w - rightW : 0, rightW, top,
                         h - bottom);
    if (self->m_central) {
        XRect_init(&r, leftW, top,
                   w - leftW - rightW > 0 ? w - leftW - rightW : 0,
                   h - top - bottom > 0 ? h - top - bottom : 0);
        XWidget_setGeometryRect(self->m_central, &r);
    }
}

/* ==================== 事件处理 ==================== */

static void VX_mainWindow_resizeEvent(XWidget* self, XEvent* event)
{
    (void)event;
    xmw_layout((XMainWindow*)self);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_mainWindow_deinit(XMainWindow* self)
{
    if (!self) return;
    if (self->m_menuBarOwned && self->m_menuBar) {
        XClass_delete_base((XClass*)self->m_menuBar);
    }
    if (self->m_statusBarOwned && self->m_statusBar) {
        XClass_delete_base((XClass*)self->m_statusBar);
    }
    if (self->m_toolBars) {
        XVector_delete_base(self->m_toolBars);
        self->m_toolBars = NULL;
    }
    if (self->m_toolBarAreas) {
        XVector_delete_base(self->m_toolBarAreas);
        self->m_toolBarAreas = NULL;
    }
    if (self->m_docks) {
        XVector_delete_base(self->m_docks);
        self->m_docks = NULL;
    }
    if (self->m_dockAreas) {
        XVector_delete_base(self->m_dockAreas);
        self->m_dockAreas = NULL;
    }
    if (self->m_dockHeights) {
        XVector_delete_base(self->m_dockHeights);
        self->m_dockHeights = NULL;
    }
    if (self->m_dockTabGroups) {
        int64_t i;
        int64_t n = XVector_size_base((const XContainer*)self->m_dockTabGroups);
        for (i = 0; i < n; ++i) {
            XVector** group =
                (XVector**)XVector_at_base(self->m_dockTabGroups, i);
            if (group && *group) XVector_delete_base(*group);
        }
        XVector_delete_base(self->m_dockTabGroups);
        self->m_dockTabGroups = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XMainWindow_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMainWindow)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VX_mainWindow_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_mainWindow_deinit);
    return XVTABLE_DEFAULT;
}

void XMainWindow_init(XMainWindow* self, XWidget* parent,
                      XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XMainWindow);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_toolBars = XVector_Create(XToolBar*);
    self->m_toolBarAreas = XVector_Create(int);
    self->m_docks = XVector_Create(XDockWidget*);
    self->m_dockAreas = XVector_Create(int);
    self->m_dockHeights = XVector_Create(int);
    self->m_dockTabGroups = XVector_Create(XVector*);
    self->m_leftDockWidth = 160;
    self->m_rightDockWidth = 160;
    self->m_topDockHeight = 100;
    self->m_bottomDockHeight = 100;
    self->m_dockOptions = (int)XMainWindowDockOption_AnimatedDocks;
    self->m_iconSize = 16;
    self->m_toolButtonStyle = (int)XToolButtonStyle_IconOnly;
    self->m_activeTabifiedDock = NULL;
    XWidget_resize(self, 600, 450);

    self->m_documentMode = false;
    self->m_animated = true;
    self->m_dockNestingEnabled = false;
    self->m_unifiedTitleAndToolBarOnMac = false;
    self->m_tabPosition = 0;
    self->m_tabShape = 0;
    self->m_separator = true;
    self->m_corner = 0;
}

XMainWindow* XMainWindow_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XMainWindow* self =
        (XMainWindow*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMainWindow_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 菜单栏与状态栏 ==================== */

XWidget* XMainWindow_menuBar(XMainWindow* self)
{
    if (!self) return NULL;
    if (!self->m_menuBar) {
#if XMENUBAR_ON
        self->m_menuBar = (XWidget*)XMenuBar_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, 0);
        self->m_menuBarOwned = true;
        if (self->m_menuBar) XWidget_show(self->m_menuBar);
#endif
    }
    return self->m_menuBar;
}

void XMainWindow_setMenuBar(XMainWindow* self, XWidget* menuBar)
{
    if (!self) return;
    if (self->m_menuBarOwned && self->m_menuBar)
        XClass_delete_base((XClass*)self->m_menuBar);
    self->m_menuBar = menuBar;
    self->m_menuBarOwned = false;
    if (menuBar) {
        XWidget_setParent(menuBar, (XWidget*)self, 0);
        XWidget_show(menuBar);
    }
    xmw_layout(self);
}

XWidget* XMainWindow_menuWidget(const XMainWindow* self)
{
    return self ? self->m_menuBar : NULL;
}

void XMainWindow_setMenuWidget(XMainWindow* self, XWidget* menuWidget)
{
    /* 与 setMenuBar 等价（Qt 中 setMenuBar 即转发 setMenuWidget）：
     * 内部创建的旧菜单栏随替换删除，外部控件始终为借用指针。 */
    XMainWindow_setMenuBar(self, menuWidget);
}

#if XMENU_ON
XMenu* XMainWindow_createPopupMenu(XMainWindow* self)
{
    if (!self) return NULL;
    /* 父对象设为主窗口：调用方未释放时随主窗口析构级联销毁；
     * 所有权按 Qt 约定转移给调用方。 */
    return XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, "");
}
#endif /* XMENU_ON */

XWidget* XMainWindow_statusBar(XMainWindow* self)
{
    if (!self) return NULL;
    if (!self->m_statusBar) {
#if XSTATUSBAR_ON
        self->m_statusBar = (XWidget*)XStatusBar_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, 0);
        self->m_statusBarOwned = true;
        if (self->m_statusBar) XWidget_show(self->m_statusBar);
#endif
    }
    return self->m_statusBar;
}

void XMainWindow_setStatusBar(XMainWindow* self, XWidget* statusBar)
{
    if (!self) return;
    if (self->m_statusBarOwned && self->m_statusBar)
        XClass_delete_base((XClass*)self->m_statusBar);
    self->m_statusBar = statusBar;
    self->m_statusBarOwned = false;
    if (statusBar) {
        XWidget_setParent(statusBar, (XWidget*)self, 0);
        XWidget_show(statusBar);
    }
    xmw_layout(self);
}

/* ==================== 中央控件与工具栏 ==================== */

void XMainWindow_setCentralWidget(XMainWindow* self, XWidget* widget)
{
    if (!self) return;
    if (self->m_central)
        XWidget_setVisible(self->m_central, false);
    self->m_central = widget;
    if (widget) {
        XWidget_setParent(widget, (XWidget*)self, 0);
        XWidget_show(widget);
    }
    xmw_layout(self);
}

XWidget* XMainWindow_centralWidget(const XMainWindow* self)
{
    return self ? self->m_central : NULL;
}

XWidget* XMainWindow_takeCentralWidget(XMainWindow* self)
{
    XWidget* widget = self ? self->m_central : NULL;
    if (widget) {
        XWidget_setParent(widget, NULL, 0);
        self->m_central = NULL;
    }
    return widget;
}

void XMainWindow_addToolBar(XMainWindow* self, int area, XWidget* toolbar)
{
    int areaVal = area;
    if (!self || !toolbar || !self->m_toolBars) return;
    XWidget_setParent(toolbar, (XWidget*)self, 0);
    XVector_push_back_1_base(self->m_toolBars, &toolbar);
    XVector_push_back_1_base(self->m_toolBarAreas, &areaVal);
    XWidget_show(toolbar);
    xmw_layout(self);
}

XWidget* XMainWindow_addToolBar_2(XMainWindow* self,
                                  const char* utf8Title)
{
    XWidget* toolbar = NULL;
    if (!self) return NULL;
#if XTOOLBAR_ON
    toolbar = (XWidget*)XToolBar_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)self, 0);
    if (toolbar) {
        XToolBar* tb = (XToolBar*)toolbar;
        XToolBar_setTitle(tb, utf8Title ? utf8Title : "");
        XMainWindow_addToolBar(self, (int)XDockWidgetArea_Top, toolbar);
    }
#endif
    return toolbar;
}

int XMainWindow_iconSize(const XMainWindow* self)
{
    return self ? self->m_iconSize : 16;
}

void XMainWindow_setIconSize(XMainWindow* self, int size)
{
    int64_t i;
    int64_t n;
    if (!self || size <= 0 || size == self->m_iconSize) return;
    self->m_iconSize = size;
    /* 对齐 Qt：主窗口 iconSizeChanged 连接到各工具栏的尺寸更新槽。 */
    if (self->m_toolBars) {
        n = XVector_size_base((const XContainer*)self->m_toolBars);
        for (i = 0; i < n; ++i) {
            XToolBar* tb =
                XVector_At_Base(self->m_toolBars, i, XToolBar*);
            if (tb) XToolBar_setIconSize(tb, size);
        }
    }
    XMainWindow_iconSizeChanged_signal(self, size, size);
}

/* ==================== 停靠面板 ==================== */

/**
 * @brief      在停靠面板登记表中查找面板下标。
 * @param      self 目标主窗口；可为 NULL。
 * @param      dock 停靠面板借用指针；可为 NULL。
 * @return     找到返回下标；未登记或参数无效返回 -1。
 */
static int64_t xmw_dockIndex(const XMainWindow* self, const XDockWidget* dock)
{
    int64_t i;
    int64_t n;
    if (!self || !dock || !self->m_docks) return -1;
    n = XVector_size_base((const XContainer*)self->m_docks);
    for (i = 0; i < n; ++i) {
        XDockWidget* d = XVector_At_Base(self->m_docks, i, XDockWidget*);
        if (d == dock) return i;
    }
    return -1;
}

/**
 * @brief      查询停靠面板所在的标签组。
 * @param      self 目标主窗口；可为 NULL。
 * @param      dock 停靠面板借用指针；可为 NULL。
 * @return     内部标签组数组借用指针（元素为 XDockWidget*）；未成组或
 *             参数无效返回 NULL。
 */
static XVector* xmw_dockGroupOf(const XMainWindow* self,
                                const XDockWidget* dock)
{
    int64_t g;
    int64_t gn;
    if (!self || !dock || !self->m_dockTabGroups) return NULL;
    gn = XVector_size_base((const XContainer*)self->m_dockTabGroups);
    for (g = 0; g < gn; ++g) {
        XVector* group =
            XVector_At_Base(self->m_dockTabGroups, g, XVector*);
        int64_t i;
        int64_t n;
        if (!group) continue;
        n = XVector_size_base((const XContainer*)group);
        for (i = 0; i < n; ++i) {
            XDockWidget* d = XVector_At_Base(group, i, XDockWidget*);
            if (d == dock) return group;
        }
    }
    return NULL;
}

/**
 * @brief      把停靠面板从所在标签组移除。
 * @details    组内成员不足两个时解散该组并释放组数组（对齐 Qt“独占
 *             标签条不算成组”的判定）。
 * @param      self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @param      dock 停靠面板借用指针；可为 NULL。
 * @return     无返回值。
 */
static void xmw_dockGroupDetach(XMainWindow* self, const XDockWidget* dock)
{
    int64_t g;
    int64_t gn;
    if (!self || !dock || !self->m_dockTabGroups) return;
    gn = XVector_size_base((const XContainer*)self->m_dockTabGroups);
    for (g = 0; g < gn; ++g) {
        XVector* group =
            XVector_At_Base(self->m_dockTabGroups, g, XVector*);
        int64_t idx;
        if (!group) continue;
        idx = XVector_indexOf(group, &dock, 0);
        if (idx < 0) continue;
        XVector_remove_base(group, idx, 1);
        if (XVector_size_base((const XContainer*)group) < 2) {
            XVector_delete_base(group);
            XVector_remove_base(self->m_dockTabGroups, g, 1);
        }
        return;
    }
}

/**
 * @brief      把停靠面板加入标签组（必要时以 first 新建组）。
 * @param      self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @param      first 基准停靠面板借用指针；未成组时作为新组首成员。
 * @param      second 待加入的停靠面板借用指针。
 * @return     加入后的标签组借用指针（内部数组）；参数无效或分配失败
 *             返回 NULL。
 */
static XVector* xmw_dockGroupAttach(XMainWindow* self, XDockWidget* first,
                                    XDockWidget* second)
{
    XVector* group;
    if (!self || !first || !second || first == second ||
        !self->m_dockTabGroups)
        return NULL;
    /* 先摘 second 再定位/建组：detach 在组内余员 <2 时解散并释放该
       组——second 与 first 同组时旧序（先取 group 后 detach）会拿着
       已释放的组指针继续 indexOf（use-after-free 段错误）。detach 后
       按 first 重新定位，同组情形自然落入新建组分支，终态
       [first, second] 不变。 */
    xmw_dockGroupDetach(self, second);
    group = xmw_dockGroupOf(self, first);
    if (!group) {
        group = XVector_Create(XDockWidget*);
        if (!group) return NULL;
        XVector_push_back_1_base(group, &first);
        XVector_push_back_1_base(self->m_dockTabGroups, &group);
    }
    if (XVector_indexOf(group, &second, 0) < 0)
        XVector_push_back_1_base(group, &second);
    return group;
}

/**
 * @brief      同步标签组可见性：每组只显示活动面板。
 * @details    活动面板取 m_activeTabifiedDock（在其组内时），否则取组内
 *             第一个成员；其余成员隐藏（对齐 Qt 只有当前标签可见）。
 * @param      self 目标主窗口；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
static void xmw_dockGroupSync(XMainWindow* self)
{
    int64_t g;
    int64_t gn;
    if (!self || !self->m_dockTabGroups) return;
    gn = XVector_size_base((const XContainer*)self->m_dockTabGroups);
    for (g = 0; g < gn; ++g) {
        XVector* group =
            XVector_At_Base(self->m_dockTabGroups, g, XVector*);
        XDockWidget* active = NULL;
        int64_t i;
        int64_t n;
        if (!group) continue;
        n = XVector_size_base((const XContainer*)group);
        if (self->m_activeTabifiedDock) {
            for (i = 0; i < n; ++i) {
                XDockWidget* d = XVector_At_Base(group, i, XDockWidget*);
                if (d == (XDockWidget*)self->m_activeTabifiedDock) {
                    active = d;
                    break;
                }
            }
        }
        if (!active && n > 0)
            active = XVector_At_Base(group, 0, XDockWidget*);
        for (i = 0; i < n; ++i) {
            XDockWidget* d = XVector_At_Base(group, i, XDockWidget*);
            if (!d) continue;
            XWidget_setVisible((XWidget*)d, d == active);
        }
    }
}

void XMainWindow_addDockWidget(XMainWindow* self, int area,
                               XWidget* dock)
{
    XDockWidget* d = (XDockWidget*)dock;
    int areaVal = area;
    int heightVal = 0;
    int64_t existing;
    if (!self || !dock || !self->m_docks) return;
    /* 登记宿主回链（浮动/显隐变化经此通知主窗口重排）。 */
    XDockWidget_setHost(d, (XWidget*)self);
    /* 对标 Qt：addDockWidget 会把浮动面板重新停靠。 */
    if (d->m_floating) XDockWidget_setFloating(d, false);
    existing = xmw_dockIndex(self, d);
    if (existing >= 0) {
        /* 对标 Qt：重复登记视为移动，仅更新区域（避免重复条目破坏
         * saveState/restoreState 的按下标映射）。 */
        *(int*)XVector_at_base(self->m_dockAreas, existing) = areaVal;
        XWidget_setParent(dock, (XWidget*)self, 0);
        XWidget_show(dock);
        xmw_layout(self);
        return;
    }
    XWidget_setParent(dock, (XWidget*)self, 0);
    XVector_push_back_1_base(self->m_docks, &dock);
    XVector_push_back_1_base(self->m_dockAreas, &areaVal);
    if (self->m_dockHeights)
        XVector_push_back_1_base(self->m_dockHeights, &heightVal);
    XWidget_show(dock);
    xmw_layout(self);
}

void XMainWindow_removeDockWidget(XMainWindow* self, XWidget* dock)
{
    int64_t i;
    int64_t n;
    if (!self || !dock || !self->m_docks) return;
    n = XVector_size_base((const XContainer*)self->m_docks);
    for (i = 0; i < n; ++i) {
        XDockWidget** item =
            (XDockWidget**)XVector_at_base(self->m_docks, i);
        if (item && (XWidget*)*item == dock) {
            /* 对标 Qt：移除后面板隐藏并脱离主窗口布局。 */
            XWidget_hide(dock);
            XDockWidget_setHost((XDockWidget*)dock, NULL);
            XVector_remove_base(self->m_docks, i, 1);
            XVector_remove_base(self->m_dockAreas, i, 1);
            if (self->m_dockHeights)
                XVector_remove_base(self->m_dockHeights, i, 1);
            xmw_dockGroupDetach(self, (XDockWidget*)dock);
            if (self->m_activeTabifiedDock == dock)
                self->m_activeTabifiedDock = NULL;
            xmw_dockGroupSync(self);
            XWidget_setParent(dock, NULL, 0);
            xmw_layout(self);
            return;
        }
    }
}

void XMainWindow_splitDockWidget(XMainWindow* self, XDockWidget* after,
                                 XDockWidget* dock, int orientation)
{
    int64_t ai;
    int64_t di;
    int afterArea;
    int dockArea;
    int heightVal = 0;
    if (!self || !after || !dock || after == dock || !self->m_docks)
        return;
    ai = xmw_dockIndex(self, after);
    if (ai < 0) return; /* 对齐 Qt：基准不在布局中即不动作 */
    /* 对齐 Qt：after 已在标签组中时，dock 作为新标签加入而非邻居。 */
    if (xmw_dockGroupOf(self, after)) {
        XMainWindow_tabifyDockWidget(self, after, dock);
        return;
    }
    di = xmw_dockIndex(self, dock);
    if (di < 0) {
        /* 未登记：先按 after 的区域接入主窗口（对齐 Qt 会加入该面板）。 */
        afterArea = XVector_At_Base(self->m_dockAreas, ai, int);
        XMainWindow_addDockWidget(self, afterArea, (XWidget*)dock);
        di = xmw_dockIndex(self, dock);
        if (di < 0) return;
    }
    afterArea = XVector_At_Base(self->m_dockAreas, ai, int);
    dockArea = afterArea;
    if (orientation == 1 /* Qt::Horizontal：放到相邻的左/右列 */) {
        if (afterArea == (int)XDockWidgetArea_Left)
            dockArea = (int)XDockWidgetArea_Right;
        else if (afterArea == (int)XDockWidgetArea_Right)
            dockArea = (int)XDockWidgetArea_Left;
    }
    xmw_dockGroupDetach(self, dock);
    if (self->m_dockHeights)
        heightVal = XVector_At_Base(self->m_dockHeights, di, int);
    *(int*)XVector_at_base(self->m_dockAreas, di) = dockArea;
    if (di != ai + 1) {
        /* 把 dock 的登记项移到 after 之后（顺序数组与行高数组同步）。 */
        int64_t insertAt = di < ai ? ai : ai + 1;
        XDockWidget* moved = dock;
        XVector_remove_base(self->m_docks, di, 1);
        XVector_remove_base(self->m_dockAreas, di, 1);
        if (self->m_dockHeights)
            XVector_remove_base(self->m_dockHeights, di, 1);
        XVector_insert_1_base(self->m_docks, insertAt, &moved, 1);
        XVector_insert_1_base(self->m_dockAreas, insertAt, &dockArea, 1);
        if (self->m_dockHeights)
            XVector_insert_1_base(self->m_dockHeights, insertAt, &heightVal,
                                  1);
    }
    xmw_dockGroupSync(self);
    XWidget_show((XWidget*)dock);
    xmw_layout(self);
}

void XMainWindow_tabifyDockWidget(XMainWindow* self, XDockWidget* first,
                                  XDockWidget* second)
{
    int64_t ai;
    int firstArea;
    if (!self || !first || !second || first == second || !self->m_docks)
        return;
    ai = xmw_dockIndex(self, first);
    if (ai < 0) return; /* 对齐 Qt：基准不在布局中即不动作 */
    if (xmw_dockIndex(self, second) < 0) {
        /* 未登记：先按 first 的区域接入主窗口。 */
        firstArea = XVector_At_Base(self->m_dockAreas, ai, int);
        XMainWindow_addDockWidget(self, firstArea, (XWidget*)second);
        if (xmw_dockIndex(self, second) < 0) return;
    }
    if (!xmw_dockGroupAttach(self, first, second)) return;
    /* 激活 second：记入 m_activeTabifiedDock 并真发射信号。 */
    XMainWindow_tabifiedDockWidgetActivated_signal(self, (XWidget*)second);
    xmw_dockGroupSync(self);
    xmw_layout(self);
}

const XVector* XMainWindow_tabifiedDockWidgets(const XMainWindow* self,
                                               const XDockWidget* dock)
{
    XVector* group;
    if (!self || !dock) return NULL;
    group = xmw_dockGroupOf(self, dock);
    if (!group) return NULL;
    if (XVector_size_base((const XContainer*)group) < 2) return NULL;
    return group;
}

bool XMainWindow_restoreDockWidget(XMainWindow* self, XDockWidget* dock)
{
    if (!self || !dock) return false;
    if (xmw_dockIndex(self, dock) < 0) return false;
    if (XDockWidget_isFloating(dock))
        XDockWidget_setFloating(dock, false);
    if (xmw_dockGroupOf(self, dock)) {
        /* 标签组成员：恢复为该组活动面板（对齐 Qt 恢复为当前标签）。 */
        XMainWindow_tabifiedDockWidgetActivated_signal(self, (XWidget*)dock);
        xmw_dockGroupSync(self);
    } else {
        XWidget_show((XWidget*)dock);
    }
    xmw_layout(self);
    return true;
}

void XMainWindow_resizeDocks(XMainWindow* self, XDockWidget** docks,
                             const int* sizes, int count, int orientation)
{
    int i;
    if (!self || !docks || !sizes || count <= 0 || !self->m_docks) return;
    for (i = 0; i < count; ++i) {
        XDockWidget* d = docks[i];
        int size = sizes[i];
        int64_t idx;
        int area;
        if (!d || size <= 0) continue;    /* 对齐 Qt：非正尺寸跳过 */
        idx = xmw_dockIndex(self, d);
        if (idx < 0) continue;            /* 未登记面板跳过 */
        if (XDockWidget_isFloating(d)) continue; /* 浮动面板不参与布局 */
        area = XVector_At_Base(self->m_dockAreas, idx, int);
        if (orientation == 1 /* Qt::Horizontal：调宽度 */) {
            if (area == (int)XDockWidgetArea_Left)
                self->m_leftDockWidth = size;
            else if (area == (int)XDockWidgetArea_Right)
                self->m_rightDockWidth = size;
            else if (area == (int)XDockWidgetArea_Top ||
                     area == (int)XDockWidgetArea_Bottom) {
                /* Top/Bottom 行面板的行内宽度覆盖（共用跨向覆盖槽位）。 */
                if (self->m_dockHeights)
                    *(int*)XVector_at_base(self->m_dockHeights, idx) = size;
            }
        } else if (orientation == 2 /* Qt::Vertical：调高度 */) {
            if (area == (int)XDockWidgetArea_Top)
                self->m_topDockHeight = size;
            else if (area == (int)XDockWidgetArea_Bottom)
                self->m_bottomDockHeight = size;
            else if (self->m_dockHeights) {
                /* 左/右列面板的行高覆盖。 */
                *(int*)XVector_at_base(self->m_dockHeights, idx) = size;
            }
        }
    }
    xmw_layout(self);
}

bool XMainWindow_isSeparator(const XMainWindow* self, const XPoint* pos)
{
    int w;
    int h;
    int top;
    int bottom;
    int lw;
    int rw;
    int topRowH;
    int bottomRowH;
    if (!self || !pos) return false;
    w = XWidget_width((const XWidget*)self);
    h = XWidget_height((const XWidget*)self);
    top = xmw_topUsed(self);
    bottom = xmw_bottomUsed(self);
    if (pos->y < top || pos->y >= h - bottom) return false;
    lw = self->m_leftDockWidth;
    rw = self->m_rightDockWidth;
    /* 左列与中央区域之间的竖直分隔带。 */
    if (xmw_dockAreaUsed(self, (int)XDockWidgetArea_Left) &&
        pos->x >= lw - 2 && pos->x <= lw + 2)
        return true;
    /* 右列与中央区域之间的竖直分隔带。 */
    if (xmw_dockAreaUsed(self, (int)XDockWidgetArea_Right) &&
        pos->x >= w - rw - 2 && pos->x <= w - rw + 2)
        return true;
    /* Top 行与中央区域之间的水平分隔带（与 xmw_layout 同口径）。 */
    topRowH = xmw_dockAreaUsed(self, (int)XDockWidgetArea_Top)
                  ? self->m_topDockHeight : 0;
    if (topRowH > 0 && pos->x >= lw && pos->x < w - rw &&
        pos->y >= top + topRowH - 2 && pos->y <= top + topRowH + 2)
        return true;
    /* Bottom 行与中央区域之间的水平分隔带。 */
    bottomRowH = xmw_dockAreaUsed(self, (int)XDockWidgetArea_Bottom)
                     ? self->m_bottomDockHeight : 0;
    if (bottomRowH > 0 && pos->x >= lw && pos->x < w - rw &&
        pos->y >= h - bottom - bottomRowH - 2 &&
        pos->y <= h - bottom - bottomRowH + 2)
        return true;
    return false;
}

void XMainWindow_setDockOptions(XMainWindow* self, int options)
{
    if (!self) return;
    self->m_dockOptions = options;
}

int XMainWindow_dockOptions(const XMainWindow* self)
{
    return self ? self->m_dockOptions : 0;
}

void XMainWindow_updateDockLayout(XMainWindow* self)
{
    /* 保护接口（见 XMainWindow_Protected.h）：停靠面板浮动/显隐变化时
     * 经宿主回链回触重排（对标 QDockWidget 触发
     * QMainWindowLayout::update 的私有路径）。重排幂等，重复调用安全。 */
    if (!self) return;
    xmw_layout(self);
}

























/**
 * @brief      向主窗口发射带 int 载荷的信号（无连接时释放参数）。
 * @param      self 目标主窗口；NULL 时不发射。
 * @param      signal 信号标识。
 * @param      value int 载荷。
 * @return     无返回值。
 */
static void xmw_emitInt(XMainWindow* self, size_t signal, int value)
{
    XVarList* args;

    if (!self) return;
    args = XVarList_Create(XVar(int, value));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/**
 * @brief      向主窗口发射 XWidget* 载荷的信号（无连接时释放参数）。
 * @param      self 目标主窗口；NULL 时不发射。
 * @param      signal 信号标识。
 * @param      widget 控件载荷；可为 NULL。
 * @return     无返回值。
 */
static void xmw_emitWidget(XMainWindow* self, size_t signal,
                           XWidget* widget)
{
    XVarList* args;

    if (!self) return;
    args = XVarList_Create(XVar(XWidget*, widget));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

void* XMainWindow_toolButtonStyleChanged_signal(XMainWindow* self,
                                                int toolButtonStyle)
{
    if (!self)
        return (void*)(size_t)XMainWindow_toolButtonStyleChanged_signal;
    xmw_emitInt(self, (size_t)XMainWindow_toolButtonStyleChanged_signal,
                toolButtonStyle);
    return (void*)(size_t)XMainWindow_toolButtonStyleChanged_signal;
}

void* XMainWindow_tabifiedDockWidgetActivated_signal(XMainWindow* self,
                                                     XWidget* dockWidget)
{
    if (self)
        self->m_activeTabifiedDock = dockWidget;
    if (!self)
        return (void*)(size_t)XMainWindow_tabifiedDockWidgetActivated_signal;
    xmw_emitWidget(self,
                   (size_t)XMainWindow_tabifiedDockWidgetActivated_signal,
                   dockWidget);
    return (void*)(size_t)XMainWindow_tabifiedDockWidgetActivated_signal;
}

void XMainWindow_setToolButtonStyle(XMainWindow* self, int toolButtonStyle)
{
    if (!self || self->m_toolButtonStyle == toolButtonStyle)
        return;
    self->m_toolButtonStyle = toolButtonStyle;
    xmw_emitInt(self, (size_t)XMainWindow_toolButtonStyleChanged_signal,
                toolButtonStyle);
}

int XMainWindow_toolButtonStyle(const XMainWindow* self)
{
    return self ? self->m_toolButtonStyle : (int)XToolButtonStyle_IconOnly;
}


















void* XMainWindow_iconSizeChanged_signal(XMainWindow* self, int width,
                                         int height)
{
    XVarList* args;

    if (!self) return (void*)(size_t)XMainWindow_iconSizeChanged_signal;
    args = XVarList_Create(XVar(int, width), XVar(int, height));
    if (args) {
        if (((XObject*)self)->m_signalSlot)
            XObject_emitSignal((XObject*)self,
                               (size_t)XMainWindow_iconSizeChanged_signal,
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        else
            XVarList_delete(args);
    }
    return (void*)(size_t)XMainWindow_iconSizeChanged_signal;
}

/* ==================== Task 2.4：布局 API ==================== */

int XMainWindow_toolBarArea(const XMainWindow* self, const XWidget* toolbar)
{
    size_t i;
    size_t n;
    if (!self || !toolbar || !self->m_toolBars) return 0;
    n = XVector_size_base((const XContainer*)self->m_toolBars);
    for (i = 0; i < n; ++i) {
        XWidget* tb = XVector_At_Base(self->m_toolBars, (int64_t)i, XWidget*);
        if (tb == toolbar) {
            return XVector_At_Base(self->m_toolBarAreas, (int64_t)i, int);
        }
    }
    return 0;
}

int XMainWindow_dockWidgetArea(const XMainWindow* self, const XWidget* dock)
{
    size_t i;
    size_t n;
    if (!self || !dock || !self->m_docks) return 0;
    n = XVector_size_base((const XContainer*)self->m_docks);
    for (i = 0; i < n; ++i) {
        XWidget* d = XVector_At_Base(self->m_docks, (int64_t)i, XWidget*);
        if (d == dock) {
            return XVector_At_Base(self->m_dockAreas, (int64_t)i, int);
        }
    }
    return 0;
}

void XMainWindow_setSeparator(XMainWindow* self, bool sep)
{ if (self) self->m_separator = sep; }

void XMainWindow_setDocumentMode(XMainWindow* self, bool enable)
{ if (self) self->m_documentMode = enable; }
bool XMainWindow_documentMode(const XMainWindow* self)
{ return self ? self->m_documentMode : false; }

void XMainWindow_setAnimated(XMainWindow* self, bool enable)
{ if (self) self->m_animated = enable; }
bool XMainWindow_isAnimated(const XMainWindow* self)
{ return self ? self->m_animated : true; }

void XMainWindow_setDockNestingEnabled(XMainWindow* self, bool enable)
{ if (self) self->m_dockNestingEnabled = enable; }
bool XMainWindow_isDockNestingEnabled(const XMainWindow* self)
{ return self ? self->m_dockNestingEnabled : false; }

void XMainWindow_setUnifiedTitleAndToolBarOnMac(XMainWindow* self,
                                                bool enable)
{ if (self) self->m_unifiedTitleAndToolBarOnMac = enable; }
bool XMainWindow_isUnifiedTitleAndToolBarOnMac(const XMainWindow* self)
{ return self ? self->m_unifiedTitleAndToolBarOnMac : false; }

void XMainWindow_setTabPosition(XMainWindow* self, int position)
{ if (self) self->m_tabPosition = position; }
int XMainWindow_tabPosition(const XMainWindow* self)
{ return self ? self->m_tabPosition : 0; }

void XMainWindow_setTabShape(XMainWindow* self, int shape)
{ if (self) self->m_tabShape = shape; }
int XMainWindow_tabShape(const XMainWindow* self)
{ return self ? self->m_tabShape : 0; }

void XMainWindow_setCorner(XMainWindow* self, int corner, int area)
{
    int shift;
    if (!self || corner < 0 || corner > 3) return;
    if (area < 0 || area > 0xF) return;
    /* 每角 4bit 承载停靠区域码（0..0xF，可完整往返 setCorner/corner；
     * 旧实现按 1bit 置位，corner() 恒返回 0/1，偏离头文件“返回区域
     * 码”契约——如 Bottom=8 无法表示）。 */
    shift = corner * 4;
    self->m_corner &= ~(0xF << shift);
    self->m_corner |= area << shift;
}

int XMainWindow_corner(const XMainWindow* self, int corner)
{
    if (!self || corner < 0 || corner > 3) return 0;
    return (self->m_corner >> (corner * 4)) & 0xF;
}

void XMainWindow_addToolBarBreak(XMainWindow* self)
{
    /* 布局中断：工具栏顺序数组中以 area=0 记录断点（内部标记）。 */
    if (self && self->m_toolBarAreas) {
        int v = 0;
        XWidget* dummy = NULL;
        if (self->m_toolBars)
            XVector_push_back_1_base(self->m_toolBars, &dummy);
        XVector_push_back_1_base(self->m_toolBarAreas, &v);
    }
}

void XMainWindow_insertToolBarBreak(XMainWindow* self, XWidget* before)
{
    int64_t i;
    XWidget* dummy = NULL;
    int v = 0;
    if (!self || !before || !self->m_toolBars || !self->m_toolBarAreas)
        return;
    i = xmw_toolBarIndex(self, before);
    if (i < 0) return;                      /* 对齐 Qt：找不到基准即返回 */
    if (xmw_hasBreakBefore(self, i)) return; /* 其前已有断行 */
    if (i == 0) return;                     /* 首个登记项没有可断的行 */
    XVector_insert_1_base(self->m_toolBars, i, &dummy, 1);
    XVector_insert_1_base(self->m_toolBarAreas, i, &v, 1);
    xmw_layout(self);
}

void XMainWindow_removeToolBarBreak(XMainWindow* self, XWidget* before)
{
    int64_t i;
    if (!self || !before || !self->m_toolBars || !self->m_toolBarAreas)
        return;
    i = xmw_toolBarIndex(self, before);
    if (i < 0) return;
    if (!xmw_hasBreakBefore(self, i)) return; /* 其前没有断行 */
    XVector_remove_base(self->m_toolBars, i - 1, 1);
    XVector_remove_base(self->m_toolBarAreas, i - 1, 1);
    xmw_layout(self);
}

bool XMainWindow_toolBarBreak(const XMainWindow* self, const XWidget* toolbar)
{
    int64_t i = xmw_toolBarIndex(self, toolbar);
    if (i < 0) return false;
    return xmw_hasBreakBefore(self, i);
}

void XMainWindow_insertToolBar(XMainWindow* self, XWidget* before,
                               XWidget* toolbar)
{
    size_t i;
    size_t j;
    size_t n;
    int areaVal;
    if (!self || !toolbar || !self->m_toolBars) return;
    /* Qt 语义：insertToolBar 是移动——先移除旧登记避免重复。 */
    n = XVector_size_base((const XContainer*)self->m_toolBars);
    for (j = 0; j < n; ++j) {
        XWidget* tb = XVector_At_Base(self->m_toolBars, (int64_t)j, XWidget*);
        if (tb == toolbar) {
            XVector_remove_base(self->m_toolBars, (int64_t)j, 1);
            XVector_remove_base(self->m_toolBarAreas, (int64_t)j, 1);
            break;
        }
    }
    n = XVector_size_base((const XContainer*)self->m_toolBars);
    for (i = 0; i < n; ++i) {
        XWidget* tb = XVector_At_Base(self->m_toolBars, (int64_t)i, XWidget*);
        if (tb == before) break;
    }
    XWidget_setParent(toolbar, (XWidget*)self, 0);
    if (i < n) {
        areaVal = XVector_At_Base(self->m_toolBarAreas, (int64_t)i, int);
        XVector_insert_1_base(self->m_toolBars, (int64_t)i, &toolbar, 1);
        XVector_insert_1_base(self->m_toolBarAreas, (int64_t)i, &areaVal, 1);
    } else {
        areaVal = 4; /* RightToolBarArea */
        XVector_push_back_1_base(self->m_toolBars, &toolbar);
        XVector_push_back_1_base(self->m_toolBarAreas, &areaVal);
    }
    XWidget_show(toolbar);
    xmw_layout(self);
}

void XMainWindow_removeToolBar(XMainWindow* self, XWidget* toolbar)
{
    size_t i;
    size_t n;
    if (!self || !toolbar || !self->m_toolBars) return;
    n = XVector_size_base((const XContainer*)self->m_toolBars);
    for (i = 0; i < n; ++i) {
        XWidget* tb = XVector_At_Base(self->m_toolBars, (int64_t)i, XWidget*);
        if (tb == toolbar) {
            XVector_remove_base(self->m_toolBars, (int64_t)i, 1);
            XVector_remove_base(self->m_toolBarAreas, (int64_t)i, 1);
            XWidget_hide(toolbar);
            xmw_layout(self);
            return;
        }
    }
}

/* ==================== 布局状态序列化（对标 saveState/restoreState） ==================== */

/** @brief 快照魔数前缀（对标 Qt QMainWindowLayout 的 magic 码）。 */
#define XMW_STATE_MAGIC "XMWSTATE:"
/** @brief 停靠面板条目分隔符内的字段个数：区域:可见:浮动:跨向覆盖。 */
#define XMW_STATE_VERSION 2

/**
 * @brief      从快照游标扫描一个十进制整数。
 * @param      cursor 游标指针（扫描后前移）；不可为 NULL。
 * @param      out 解析结果输出；不可为 NULL。
 * @return     至少消费一位数字返回 true；否则返回 false。
 */
static bool xmw_scanInt(const char** cursor, int* out)
{
    const char* p = *cursor;
    int value = 0;
    bool negative = false;
    bool any = false;
    if (*p == '-') {
        negative = true;
        ++p;
    }
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        ++p;
        any = true;
    }
    *cursor = p;
    *out = negative ? -value : value;
    return any;
}

/**
 * @brief      匹配游标处的指定字符并前移。
 * @param      cursor 游标指针；不可为 NULL。
 * @param      ch 期望字符。
 * @return     匹配返回 true；否则游标不动并返回 false。
 */
static bool xmw_scanChar(const char** cursor, char ch)
{
    if (**cursor == ch) {
        ++*cursor;
        return true;
    }
    return false;
}

XString* XMainWindow_saveState(const XMainWindow* self)
{
    XString* out;
    size_t i;
    size_t n;
    char buf[64];
    if (!self) return NULL;
    out = XString_create();
    if (!out) return NULL;
    XSnprintf(buf, sizeof(buf), "XMWSTATE:%d;", XMW_STATE_VERSION);
    XString_append_utf8(out, buf);
    /* 工具栏登记项：t<区域>;（断行哨兵区域为 0，按下标一一对应）。 */
    if (self->m_toolBars) {
        n = XVector_size_base((const XContainer*)self->m_toolBars);
        for (i = 0; i < n; ++i) {
            int area = XVector_At_Base(self->m_toolBarAreas, (int64_t)i, int);
            XSnprintf(buf, sizeof(buf), "t%d;", area);
            XString_append_utf8(out, buf);
        }
    }
    /* 四区几何：g<左列宽>,<右列宽>,<顶行高>,<底行高>; */
    XSnprintf(buf, sizeof(buf), "g%d,%d,%d,%d;",
              self->m_leftDockWidth, self->m_rightDockWidth,
              self->m_topDockHeight, self->m_bottomDockHeight);
    XString_append_utf8(out, buf);
    /* 停靠面板项（按登记顺序）：d<区域>:<可见>:<浮动>:<跨向覆盖>; */
    if (self->m_docks) {
        n = XVector_size_base((const XContainer*)self->m_docks);
        for (i = 0; i < n; ++i) {
            XDockWidget* d = XVector_At_Base(self->m_docks, (int64_t)i,
                                             XDockWidget*);
            int area = XVector_At_Base(self->m_dockAreas, (int64_t)i, int);
            int size = self->m_dockHeights
                           ? XVector_At_Base(self->m_dockHeights,
                                             (int64_t)i, int) : 0;
            XSnprintf(buf, sizeof(buf), "d%d:%d:%d:%d;",
                      area,
                      (d && !XWidget_isHidden((XWidget*)d)) ? 1 : 0,
                      (d && d->m_floating) ? 1 : 0,
                      size);
            XString_append_utf8(out, buf);
        }
    }
    /* 标签组条目（v2 追加段；对标 QMainWindow::saveState 持久化
     * tabified groups）：p<成员数>:<下标0>,<下标1>,...; 下标为停靠面板
     * 登记顺序（与 d 条目同口径，restoreState 按下标回映射）。旧快照
     * 缺本段时 restoreState 解析为无组，格式向后兼容。 */
    if (self->m_dockTabGroups) {
        n = XVector_size_base((const XContainer*)self->m_dockTabGroups);
        for (i = 0; i < n; ++i) {
            XVector* group =
                XVector_At_Base(self->m_dockTabGroups, (int64_t)i, XVector*);
            int64_t j;
            int64_t m;
            bool valid = true;
            if (!group) continue;
            m = XVector_size_base((const XContainer*)group);
            /* 预校验：成员必须全部已登记（独占成员不成组，与既有判定
             * 同口径），避免写出口径混乱的残缺条目。 */
            if (m < 2) continue;
            for (j = 0; j < m; ++j) {
                XDockWidget* d =
                    XVector_At_Base(group, j, XDockWidget*);
                if (!d || xmw_dockIndex(self, d) < 0) {
                    valid = false;
                    break;
                }
            }
            if (!valid) continue;
            XSnprintf(buf, sizeof(buf), "p%d:", (int)m);
            XString_append_utf8(out, buf);
            for (j = 0; j < m; ++j) {
                XDockWidget* d = XVector_At_Base(group, j, XDockWidget*);
                XSnprintf(buf, sizeof(buf), "%s%d",
                          j > 0 ? "," : "",
                          (int)xmw_dockIndex(self, d));
                XString_append_utf8(out, buf);
            }
            XString_append_utf8(out, ";");
        }
    }
    return out;
}

bool XMainWindow_restoreState(XMainWindow* self, const XString* state)
{
    const char* p;
    int version = 0;
    int toolBarIdx = 0;
    int dockIdx = 0;
    if (!state) {
        /* 对齐既有约定：NULL 视为重置请求；当前无可重置字段。 */
        return true;
    }
    if (!self) return false;
    p = XString_toUtf8(state);
    if (!p) return false;
    if (strncmp(p, XMW_STATE_MAGIC, sizeof(XMW_STATE_MAGIC) - 1) != 0)
        return false; /* 对齐 Qt：快照不识别返回 false */
    p += sizeof(XMW_STATE_MAGIC) - 1;
    if (!xmw_scanInt(&p, &version) || !xmw_scanChar(&p, ';'))
        return false;
    if (version < 1 || version > XMW_STATE_VERSION) return false;
    /* 对标 Qt restoreState 重建整个布局：编组先全部解散后按快照重建。
     * 旧快照（v1 或缺 p 段的 v2）没有组条目时即恢复为无组（任务裁定：
     * 旧快照缺段 = 无组），组结构只记录成组关系，不影响面板显隐。 */
    if (self->m_dockTabGroups) {
        int64_t g;
        int64_t gn =
            XVector_size_base((const XContainer*)self->m_dockTabGroups);
        for (g = 0; g < gn; ++g) {
            XVector* group =
                XVector_At_Base(self->m_dockTabGroups, g, XVector*);
            if (group) XVector_delete_base(group);
        }
        XVector_clear_base(self->m_dockTabGroups);
    }
    self->m_activeTabifiedDock = NULL;
    for (;;) {
        char tag = *p;
        if (tag == '\0') break;
        if (tag == 't') {
            /* 工具栏区域：按下标回放到登记表（含断行哨兵）。 */
            int area;
            ++p;
            if (!xmw_scanInt(&p, &area) || !xmw_scanChar(&p, ';'))
                return false;
            if (self->m_toolBars && self->m_toolBarAreas &&
                toolBarIdx < (int)XVector_size_base(
                                 (const XContainer*)self->m_toolBars)) {
                *(int*)XVector_at_base(self->m_toolBarAreas,
                                       (int64_t)toolBarIdx) = area;
            }
            ++toolBarIdx;
        } else if (tag == 'g' && version >= 2) {
            /* 四区几何尺寸。 */
            int l, r, t, b;
            ++p;
            if (!xmw_scanInt(&p, &l) || !xmw_scanChar(&p, ',') ||
                !xmw_scanInt(&p, &r) || !xmw_scanChar(&p, ',') ||
                !xmw_scanInt(&p, &t) || !xmw_scanChar(&p, ',') ||
                !xmw_scanInt(&p, &b) || !xmw_scanChar(&p, ';'))
                return false;
            if (l > 0) self->m_leftDockWidth = l;
            if (r > 0) self->m_rightDockWidth = r;
            if (t > 0) self->m_topDockHeight = t;
            if (b > 0) self->m_bottomDockHeight = b;
        } else if (tag == 'd' && version >= 2) {
            /* 停靠面板：区域/显隐/浮动/跨向覆盖，按登记顺序回放。 */
            int area, visible, floating, size;
            ++p;
            if (!xmw_scanInt(&p, &area) || !xmw_scanChar(&p, ':') ||
                !xmw_scanInt(&p, &visible) || !xmw_scanChar(&p, ':') ||
                !xmw_scanInt(&p, &floating) || !xmw_scanChar(&p, ':') ||
                !xmw_scanInt(&p, &size) || !xmw_scanChar(&p, ';'))
                return false;
            if (self->m_docks &&
                dockIdx < (int)XVector_size_base(
                              (const XContainer*)self->m_docks)) {
                XDockWidget* d = XVector_At_Base(self->m_docks,
                                                 (int64_t)dockIdx,
                                                 XDockWidget*);
                if (d) {
                    *(int*)XVector_at_base(self->m_dockAreas,
                                           (int64_t)dockIdx) = area;
                    if (self->m_dockHeights)
                        *(int*)XVector_at_base(self->m_dockHeights,
                                               (int64_t)dockIdx) = size;
                    if (d->m_floating != (floating != 0))
                        XDockWidget_setFloating(d, floating != 0);
                    XWidget_setVisible((XWidget*)d, visible != 0);
                }
            }
            ++dockIdx;
        } else if (tag == 'p' && version >= 2) {
            /* 标签组：p<成员数>:<下标0>,<下标1>,...;（v2 追加段；对标
             * restoreState 还原 tabified groups）。按下标取回面板后依次
             * 编组——首成员建组，其余成员逐个挂入（复用 tabify 的内部
             * 编组助手，不发射激活信号、不改显隐，显隐由 d 条目回放）。 */
            int count = 0;
            int k = 0;
            XDockWidget* first = NULL;
            ++p;
            if (!xmw_scanInt(&p, &count) || !xmw_scanChar(&p, ':') ||
                count < 0)
                return false;
            for (k = 0; k < count; ++k) {
                int idx = 0;
                XDockWidget* d = NULL;
                if (!xmw_scanInt(&p, &idx))
                    return false;
                if (k + 1 < count && !xmw_scanChar(&p, ','))
                    return false;
                if (self->m_docks && idx >= 0 &&
                    idx < (int)XVector_size_base(
                              (const XContainer*)self->m_docks)) {
                    d = XVector_At_Base(self->m_docks, (int64_t)idx,
                                        XDockWidget*);
                }
                if (!d) continue; /* 越界下标跳过（与 d 条目宽松口径一致） */
                if (!first)
                    first = d;
                else
                    xmw_dockGroupAttach(self, first, d);
            }
            if (!xmw_scanChar(&p, ';'))
                return false;
        } else {
            return false; /* 未知条目：快照损坏（对齐 Qt 返回 false） */
        }
    }
    /* 标签组活动面板回放：每组取快照显隐下首个未隐藏成员为活动标签
     * （d 条目已按"活动标签可见、其余隐藏"回放，此处据此还原各组的
     * 当前标签语义），并记入 m_activeTabifiedDock（最后一组胜出，与
     * tabifyDockWidget 的"最近激活"语义一致）。 */
    if (self->m_dockTabGroups) {
        int64_t g;
        int64_t gn =
            XVector_size_base((const XContainer*)self->m_dockTabGroups);
        for (g = 0; g < gn; ++g) {
            XVector* group =
                XVector_At_Base(self->m_dockTabGroups, g, XVector*);
            int64_t j;
            int64_t m;
            XDockWidget* active = NULL;
            if (!group) continue;
            m = XVector_size_base((const XContainer*)group);
            for (j = 0; j < m; ++j) {
                XDockWidget* d = XVector_At_Base(group, j, XDockWidget*);
                if (d && !XWidget_isHidden((XWidget*)d)) {
                    active = d;
                    break;
                }
            }
            if (!active && m > 0)
                active = XVector_At_Base(group, 0, XDockWidget*);
            self->m_activeTabifiedDock = (XWidget*)active;
        }
    }
    xmw_layout(self);
    return true;
}

#endif /* XWIDGET_ON && XMAINWINDOW_ON */