#include "XMainWindow.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XGuiConfig.h"
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
#include <string.h>

#if XWIDGET_ON && XMAINWINDOW_ON

/* ==================== 内部布局 ==================== */

static int xmw_statusHeight(void) { return 24; }
static int xmw_menuHeight(void) { return 22; }

static void xmw_layout(XMainWindow* self)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int top = 0;
    int bottom = 0;
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
            int* area = (int*)XVector_at_base(self->m_toolBarAreas, i);
            XToolBar** tb =
                (XToolBar**)XVector_at_base(self->m_toolBars, i);
            if (!area || !tb || !*tb || *area != (int)XDockWidgetArea_Top)
                continue;
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
    if (self->m_docks) {
        n = XVector_size_base((const XContainer*)self->m_docks);
        for (i = 0; i < n; ++i) {
            int* area = (int*)XVector_at_base(self->m_dockAreas, i);
            XDockWidget** dock =
                (XDockWidget**)XVector_at_base(self->m_docks, i);
            if (!area || !dock || !*dock) continue;
            if (*area == (int)XDockWidgetArea_Left && !(*dock)->m_floating) {
                XRect_init(&r, 0, top, 160, h - top - bottom);
                XWidget_setGeometryRect((XWidget*)*dock, &r);
            } else if (*area == (int)XDockWidgetArea_Right &&
                       !(*dock)->m_floating) {
                XRect_init(&r, w > 160 ? w - 160 : 0, top, 160,
                           h - top - bottom);
                XWidget_setGeometryRect((XWidget*)*dock, &r);
            }
        }
    }
    {
        int leftW = 0;
        int rightW = 0;
        if (self->m_docks) {
            n = XVector_size_base((const XContainer*)self->m_docks);
            for (i = 0; i < n; ++i) {
                int* area =
                    (int*)XVector_at_base(self->m_dockAreas, i);
                XDockWidget** dock =
                    (XDockWidget**)XVector_at_base(self->m_docks, i);
                if (!area || !dock || !*dock || (*dock)->m_floating)
                    continue;
                if (*area == (int)XDockWidgetArea_Left) leftW = 160;
                if (*area == (int)XDockWidgetArea_Right) rightW = 160;
            }
        }
        if (self->m_central) {
            XRect_init(&r, leftW, top,
                       w - leftW - rightW > 0 ? w - leftW - rightW : 0,
                       h - top - bottom > 0 ? h - top - bottom : 0);
            XWidget_setGeometryRect(self->m_central, &r);
        }
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
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XMainWindow);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_toolBars = XVector_Create(XToolBar*);
    self->m_toolBarAreas = XVector_Create(int);
    self->m_docks = XVector_Create(XDockWidget*);
    self->m_dockAreas = XVector_Create(int);
    self->m_dockOptions = (int)XMainWindowDockOption_AnimatedDocks;
    self->m_iconSize = 16;
    XWidget_resize(self, 600, 450);
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
        strncpy(tb->m_title, utf8Title ? utf8Title : "",
                sizeof(tb->m_title) - 1);
        XMainWindow_addToolBar(self, (int)XDockWidgetArea_Top, toolbar);
    }
#endif
    return toolbar;
}

/* ==================== 停靠面板 ==================== */

void XMainWindow_addDockWidget(XMainWindow* self, int area,
                               XWidget* dock)
{
    int areaVal = area;
    if (!self || !dock || !self->m_docks) return;
    XWidget_setParent(dock, (XWidget*)self, 0);
    XVector_push_back_1_base(self->m_docks, &dock);
    XVector_push_back_1_base(self->m_dockAreas, &areaVal);
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
        if (item && *item == dock) {
            XVector_remove_base(self->m_docks, i, 1);
            XVector_remove_base(self->m_dockAreas, i, 1);
            XWidget_setParent(dock, NULL, 0);
            xmw_layout(self);
            return;
        }
    }
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

int XMainWindow_toolBarArea(const XMainWindow* self, XToolBar* toolbar) { (void)self; (void)toolbar; return 0; }
int XMainWindow_dockWidgetArea(const XMainWindow* self, XDockWidget* dock) { (void)self; (void)dock; return 0; }
void XMainWindow_addToolBarBreak(XMainWindow* self, int area) { (void)self; (void)area; }
void XMainWindow_setDocumentMode(XMainWindow* self, bool mode) { (void)self; (void)mode; }
bool XMainWindow_documentMode(const XMainWindow* self) { (void)self; return false; }
void XMainWindow_setIconSize(XMainWindow* self, int size) { if(self) self->m_iconSize = size; }
int XMainWindow_iconSize(const XMainWindow* self) { return self?self->m_iconSize:16; }
void XMainWindow_setCorner(XMainWindow* self, int corner, int area) { (void)self; (void)corner; (void)area; }
int XMainWindow_corner(const XMainWindow* self, int corner) { (void)self; (void)corner; return 0; }
void XMainWindow_setTabPosition(XMainWindow* self, int area, int position) { (void)self; (void)area; (void)position; }
int XMainWindow_tabPosition(const XMainWindow* self, int area) { (void)self; (void)area; return 0; }
void XMainWindow_setTabShape(XMainWindow* self, int shape) { (void)self; (void)shape; }
int XMainWindow_tabShape(const XMainWindow* self) { (void)self; return 0; }
void XMainWindow_setUnifiedTitleAndToolBarOnMac(XMainWindow* self, bool set) { (void)self; (void)set; }
bool XMainWindow_isUnifiedTitleAndToolBarOnMac(const XMainWindow* self) { (void)self; return false; }
void XMainWindow_setAnimated(XMainWindow* self, bool enabled) { (void)self; (void)enabled; }
bool XMainWindow_isAnimated(const XMainWindow* self) { (void)self; return false; }
void XMainWindow_setDockNestingEnabled(XMainWindow* self, bool enabled) { (void)self; (void)enabled; }
bool XMainWindow_isDockNestingEnabled(const XMainWindow* self) { (void)self; return false; }
void XMainWindow_setSeparator(XMainWindow* self, int area) { (void)self; (void)area; }
#endif /* XWIDGET_ON && XMAINWINDOW_ON */