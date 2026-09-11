/**
 * @file       XTabWidget.c
 * @brief      XTabWidget 选项卡容器实现（对标 Qt 6.8 QTabWidget 子集）。
 * @details    页签条固定在顶部（高 XTABBAR_TAB_H），页容器占其余区域；
 *             每页是一个内部 XWidget（页内容控件以它为 parent 放入，
 *             借用记录于 m_clients 便于 indexOf）。切页 = 显示当前页
 *             容器 + 隐藏其余（XWidget_setVisible）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#if XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON

#include "XTabWidget.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include <string.h>
#include <stdlib.h>

#define XTABWIDGET_BAR_H 24

/* ==================== 前向声明 ==================== */
static void VXTabWidget_resizeEvent(XWidget* self, XEvent* event);
static void VXTabWidget_changeEvent(XWidget* self, XEvent* event);
static void VXTabWidget_copy(XTabWidget* self, const XTabWidget* other);
static void VXTabWidget_move(XTabWidget* self, XTabWidget* other);
static void xtabwidget_currentChangedForward(void* sender, XVarList* args);
static void xtabwidget_emitIntForward(XTabWidget* self);

/* ==================== 内部辅助 ==================== */

/** @brief 分配页容器与页签几何。 */
static void xtabwidget_layout(XTabWidget* self)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int barH;
    int pageH;
    int i;
    {   /* 与 XTabBar 的 xtabbar_wrapLayout 一致：计算多行 tabBar 高度。 */
        int minW = 48;
        int cols;
        int rows;
        if (w < minW) w = minW;
        cols = w / minW;
        if (cols < 1) cols = 1;
        if (cols > self->m_count) cols = self->m_count;
        if (cols < 1) cols = 1; /* m_count=0 时防除零。 */
        rows = (self->m_count + cols - 1) / cols;
        if (rows < 1) rows = 1;
        barH = rows * 24; /* 24 = XTABBAR_TAB_H */
    }
    pageH = h - barH;
    if (pageH < 1) pageH = 1;
    XWidget_setGeometry((XWidget*)&self->m_tabBar, 0, 0, w, barH);
    for (i = 0; i < self->m_count; ++i)
        if (self->m_pages[i])
            XWidget_setGeometry(self->m_pages[i], 0, barH, w, pageH);
}

/** @brief 切页：仅显示当前页容器。 */
static void xtabwidget_showCurrent(XTabWidget* self)
{
    int i;
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_pages[i]) {
            bool show = (i == self->m_currentIndex);
            XWidget_setVisible(self->m_pages[i], show);
            /* 内容控件显式 show：reparent 后 m_explicitShow=0 不会随
             * 页容器可见而自动显示（对标 QWidget::setVisible 语义）。 */
            if (show && self->m_clients[i])
                XWidget_show(self->m_clients[i]);
        }
    }
    XWidget_update((XWidget*)self);
}

/** @brief 页签条 currentChanged 转发槽（同步 m_currentIndex 与显示页）。
 *  @note  connect_2 的槽首参是发送者（页签条本身）——沿 parentWidget
 *         上溯到所属 XTabWidget 后再同步。 */
static void xtabwidget_currentChangedForward(void* sender, XVarList* args)
{
    XWidget* bar = (XWidget*)sender;
    XWidget* owner = bar ? XWidget_parentWidget(bar) : NULL;
    XTabWidget* self = owner ? (XTabWidget*)owner : NULL;
    if (!self) return;
    self->m_currentIndex = self->m_tabBar.m_currentIndex;
    xtabwidget_showCurrent(self);
    if (((XObject*)self)->m_signalSlot)
        xtabwidget_emitIntForward(self);
}

/** @brief 转发 currentChanged 给本控件订阅者。 */
static void xtabwidget_emitIntForward(XTabWidget* self)
{
    XVarList* arguments = XVarList_Create(XVar(int, self->m_currentIndex));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XTabWidget_currentChanged_signal(self),
                           arguments, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/* ==================== 虚槽实现 ==================== */

static void VXTabWidget_resizeEvent(XWidget* self, XEvent* event)
{
    XTabWidget* tw = (XTabWidget*)self;
    if (!tw || !event || XEvent_type(event) != XEVENT_TYPE_RESIZE) return;
    xtabwidget_layout(tw);
    XEvent_ignore(event);
}

static void VXTabWidget_changeEvent(XWidget* self, XEvent* event)
{
    XEvent_ignore(event);
    (void)self;
}

static void VXTabWidget_copy(XTabWidget* self, const XTabWidget* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTabWidget_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    /* 页与内容控件不可复制（Qt 同语义：仅结构属性拷贝）。 */
    self->m_tabPosition = other->m_tabPosition;
    self->m_tabsClosable = other->m_tabsClosable;
    self->m_movable = other->m_movable;
}

static void VXTabWidget_move(XTabWidget* self, XTabWidget* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTabWidget_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    self->m_tabPosition = other->m_tabPosition;
    self->m_tabsClosable = other->m_tabsClosable;
    self->m_movable = other->m_movable;
    other->m_tabPosition = 0;
    other->m_tabsClosable = false;
    other->m_movable = false;
}

/* ==================== 生命周期 ==================== */

XVtable* XTabWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTabWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXTabWidget_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXTabWidget_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTabWidget_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTabWidget_move);

    return XVTABLE_DEFAULT;
}

void XTabWidget_init(XTabWidget* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XTabWidget);

    self->m_pages = NULL;
    self->m_clients = NULL;
    self->m_count = 0;
    self->m_capacity = 0;
    self->m_currentIndex = -1;
    self->m_tabPosition = 0; /* North */
    self->m_tabsClosable = false;
    self->m_movable = false;
    XTabBar_init(&self->m_tabBar, (XWidget*)self, 0);
    XWidget_show((XWidget*)&self->m_tabBar);
    XObject_connect_2((XObject*)&self->m_tabBar,
                      (size_t)XTabBar_currentChanged_signal(&self->m_tabBar),
                      xtabwidget_currentChangedForward);
}

XTabWidget* XTabWidget_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags)
{
    XTabWidget* self = (XTabWidget*)XMemory_malloc(sizeof(XTabWidget), memory);
    if (!self) return NULL;
    XTabWidget_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== API ==================== */

int XTabWidget_addTab(XTabWidget* self, XWidget* page, const char* label)
{
    return XTabWidget_insertTab(self, self ? self->m_count : 0, page, label);
}

int XTabWidget_insertTab(XTabWidget* self, int index, XWidget* page,
                         const char* label)
{
    if (!self || !page || !label) return -1;
    if (index < 0) index = 0;
    if (index > self->m_count) index = self->m_count;
    {
        /* 扩容。 */
        XWidget** pages;
        XWidget** clients;
        int cap = self->m_capacity > 0 ? self->m_capacity : 4;
        while (cap < self->m_count + 1) cap *= 2;
        if (self->m_count + 1 > self->m_capacity) {
            pages = (XWidget**)XRealloc_System(self->m_pages,
                                               sizeof(XWidget*) * (size_t)cap);
            if (!pages) return -1;
            self->m_pages = pages;
            clients = (XWidget**)XRealloc_System(self->m_clients,
                                                 sizeof(XWidget*) * (size_t)cap);
            if (!clients) return -1;
            self->m_clients = clients;
            self->m_capacity = cap;
        }
        memmove(&self->m_pages[index + 1], &self->m_pages[index],
                sizeof(XWidget*) * (size_t)(self->m_count - index));
        memmove(&self->m_clients[index + 1], &self->m_clients[index],
                sizeof(XWidget*) * (size_t)(self->m_count - index));
        /* 页容器（内部 XWidget，parent 为本控件）。 */
        self->m_pages[index] = (XWidget*)XMemory_malloc(sizeof(XWidget),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (!self->m_pages[index]) return -1;
        XWidget_init(self->m_pages[index], (XWidget*)self, 0);
        Set_Class_Memory(self->m_pages[index], XCLASS_DEFAULT_MEMORY_TYPE);
        Set_Class_IsHeap(self->m_pages[index], true);
        self->m_clients[index] = page;
        /* 内容控件挂到页容器。 */
        XWidget_setParent(page, self->m_pages[index], 0);

        ++self->m_count;
        (void)XTabBar_insertTab(&self->m_tabBar, index, label);
        xtabwidget_layout(self);
        /* TabBar 内部首项添加不发射 currentChanged——显式同步两侧。 */
        if (self->m_currentIndex < 0) {
            self->m_currentIndex = self->m_tabBar.m_currentIndex;
            xtabwidget_showCurrent(self);
        }
        else xtabwidget_showCurrent(self);
        return index;
    }
}

void XTabWidget_removeTab(XTabWidget* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return;
    if (self->m_pages[index]) {
        XWidget_deinit_base(self->m_pages[index]);
        XFree_System(self->m_pages[index]);
    }
    memmove(&self->m_pages[index], &self->m_pages[index + 1],
            sizeof(XWidget*) * (size_t)(self->m_count - index - 1));
    memmove(&self->m_clients[index], &self->m_clients[index + 1],
            sizeof(XWidget*) * (size_t)(self->m_count - index - 1));
    --self->m_count;
    if (self->m_currentIndex >= self->m_count)
        self->m_currentIndex = self->m_count - 1;
    XTabBar_removeTab(&self->m_tabBar, index);
    xtabwidget_layout(self);
    xtabwidget_showCurrent(self);
}

int XTabWidget_count(const XTabWidget* self) { return self ? self->m_count : 0; }
int XTabWidget_currentIndex(const XTabWidget* self)
{
    return self ? self->m_currentIndex : -1;
}

void XTabWidget_setCurrentIndex(XTabWidget* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return;
    XTabBar_setCurrentIndex(&self->m_tabBar, index); /* 转发槽同步显示。 */
}

XWidget* XTabWidget_currentWidget(const XTabWidget* self)
{
    if (self && self->m_currentIndex >= 0 && self->m_currentIndex < self->m_count)
        return self->m_clients[self->m_currentIndex];
    return NULL;
}

XWidget* XTabWidget_widget(const XTabWidget* self, int index)
{
    if (self && index >= 0 && index < self->m_count) return self->m_clients[index];
    return NULL;
}

int XTabWidget_indexOf(const XTabWidget* self, const XWidget* page)
{
    int i;
    if (!self || !page) return -1;
    for (i = 0; i < self->m_count; ++i)
        if (self->m_clients[i] == page) return i;
    return -1;
}

const char* XTabWidget_tabText(const XTabWidget* self, int index)
{
    return self ? XTabBar_tabText(&self->m_tabBar, index) : "";
}

void XTabWidget_setTabText(XTabWidget* self, int index, const char* text)
{
    if (self) XTabBar_setTabText(&self->m_tabBar, index, text);
}

bool XTabWidget_isTabEnabled(const XTabWidget* self, int index)
{
    return self ? XTabBar_isTabEnabled(&self->m_tabBar, index) : false;
}

void XTabWidget_setTabEnabled(XTabWidget* self, int index, bool enabled)
{
    if (self) {
        XTabBar_setTabEnabled(&self->m_tabBar, index, enabled);
        if (self->m_clients[index])
            XWidget_setEnabled(self->m_clients[index], enabled);
    }
}

XTabBar* XTabWidget_tabBar(const XTabWidget* self)
{
    return self ? (XTabBar*)&((XTabWidget*)self)->m_tabBar : NULL;
}

int XTabWidget_tabPosition(const XTabWidget* self)
{
    return self ? self->m_tabPosition : 0;
}

void XTabWidget_setTabPosition(XTabWidget* self, int position)
{
    if (self && position >= 0 && position <= 3) self->m_tabPosition = position;
}

bool XTabWidget_tabsClosable(const XTabWidget* self)
{
    return self ? self->m_tabsClosable : false;
}

void XTabWidget_setTabsClosable(XTabWidget* self, bool closable)
{
    if (self) {
        self->m_tabsClosable = closable;
        XTabBar_setTabsClosable(&self->m_tabBar, closable);
    }
}

bool XTabWidget_isMovable(const XTabWidget* self)
{
    return self ? self->m_movable : false;
}

void XTabWidget_setMovable(XTabWidget* self, bool movable)
{
    if (self) {
        self->m_movable = movable;
        XTabBar_setMovable(&self->m_tabBar, movable);
    }
}

/* ==================== 信号 ==================== */

void* XTabWidget_currentChanged_signal(XTabWidget* self)
{
    return (void*)(size_t)XTabWidget_currentChanged_signal;
}
void* XTabWidget_tabClicked_signal(XTabWidget* self)
{
    return (void*)(size_t)XTabWidget_tabClicked_signal;
}


void* XTabWidget_tabCloseRequested_signal(XTabWidget* self)
{
    (void)self;
    return (void*)(size_t)XTabWidget_tabCloseRequested_signal;
}

#endif /* XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON */
