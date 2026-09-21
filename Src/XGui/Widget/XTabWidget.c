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

#include "XAlgorithm.h"
#if XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON

#include "XTabWidget.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"

#define XTABWIDGET_BAR_H 24

/* ==================== 前向声明 ==================== */
static void VXTabWidget_resizeEvent(XWidget* self, XEvent* event);
static void VXTabWidget_changeEvent(XWidget* self, XEvent* event);
static void VXTabWidget_copy(XTabWidget* self, const XTabWidget* other);
static void VXTabWidget_move(XTabWidget* self, XTabWidget* other);
static void xtabwidget_currentChangedForward(void* sender, XVarList* args);
static void xtabwidget_emitIntForward(XTabWidget* self);
static void xtabwidget_tabBarClickedForward(void* sender, XVarList* args);
static void xtabwidget_tabBarDoubleClickedForward(void* sender, XVarList* args);
static void xtabwidget_tabCloseRequestedForward(void* sender, XVarList* args);

/* ==================== 内部辅助 ==================== */

/** @brief 分配页容器与页签几何。 */
static void xtabwidget_layout(XTabWidget* self)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int barH;
    int pageH;
    int i;
    {   /* 页签条高度由 XTabBar 统一提供：溢出滚动模式恒单行(24)，
           否则按换行布局行数计（此前本地复算与滚动模式会漂移）。 */
        barH = XTabBar_barHeightHint(&self->m_tabBar, w);
        if (barH < 1) barH = 24; /* 24 = XTABBAR_TAB_H 兜底。 */
    }
    pageH = h - barH;
    if (pageH < 1) pageH = 1;
    XWidget_setGeometry((XWidget*)&self->m_tabBar, 0, 0, w, barH);
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_pages[i])
            XWidget_setGeometry(self->m_pages[i], 0, barH, w, pageH);
        /* 用户内容控件（client）必须铺满页容器，否则停留在默认几何
           (0,0,100,30)，子控件坐标越界导致 childAt 命中失败、鼠标
           事件永远到不了内容（对标 QStackedLayout 填满几何语义）。 */
        if (self->m_clients[i])
            XWidget_setGeometry(self->m_clients[i], 0, 0, w, pageH);
    }
    /* 角部件（对标 QTabWidget 角部件）：上两角置于页签条行内，按其
       当前尺寸放置（超出时截断），高度以页签条高为上限；Qt 以
       sizeHint 并在样式中为页签预留空间，此处未接入预留逻辑，页签
       可能与角部件重叠。下两角为预留位：仅承载，不参与布局。 */
    {
        XWidget* tl = self->m_cornerWidgets[XTABWIDGET_CORNER_TOPLEFT];
        XWidget* tr = self->m_cornerWidgets[XTABWIDGET_CORNER_TOPRIGHT];
        int cw;
        int ch;
        if (tl) {
            cw = XWidget_width(tl);
            ch = XWidget_height(tl);
            if (cw < 1) cw = 1;
            if (cw > w) cw = w;
            if (ch > barH) ch = barH;
            XWidget_setGeometry(tl, 0, 0, cw, ch);
        }
        if (tr) {
            cw = XWidget_width(tr);
            ch = XWidget_height(tr);
            if (cw < 1) cw = 1;
            if (cw > w) cw = w;
            if (ch > barH) ch = barH;
            XWidget_setGeometry(tr, w - cw, 0, cw, ch);
        }
    }
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
                           (size_t)XTabWidget_currentChanged_signal(
                               self, self->m_currentIndex),
                           arguments, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/**
 * @brief      从页签条上溯所属 XTabWidget。
 * @param      sender 页签条对象（connect_2 槽首参）。
 * @return     所属容器；上溯失败返回 NULL。
 */
static XTabWidget* xtabwidget_ownerOf(void* sender)
{
    XWidget* bar = (XWidget*)sender;
    XWidget* owner = bar ? XWidget_parentWidget(bar) : NULL;

    return owner ? (XTabWidget*)owner : NULL;
}

/** @brief 转发页签单击（tabClicked → tabBarClicked(int)）。 */
static void xtabwidget_tabBarClickedForward(void* sender, XVarList* args)
{
    XTabWidget* self = xtabwidget_ownerOf(sender);
    int idx;
    XVarList* arguments;

    if (!self) return;
    idx = 0;
    if (args) {
        XVarList_start(args);
        idx = XVarList_arg(args, int);
    }
    if (!((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(int, idx));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self,
                       (size_t)XTabWidget_tabBarClicked_signal,
                       arguments, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

/** @brief 转发页签双击（tabDoubleClicked → tabBarDoubleClicked(int)）。 */
static void xtabwidget_tabBarDoubleClickedForward(void* sender, XVarList* args)
{
    XTabWidget* self = xtabwidget_ownerOf(sender);
    int idx;
    XVarList* arguments;

    if (!self) return;
    idx = 0;
    if (args) {
        XVarList_start(args);
        idx = XVarList_arg(args, int);
    }
    if (!((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(int, idx));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self,
                       (size_t)XTabWidget_tabBarDoubleClicked_signal,
                       arguments, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

/** @brief 转发页签关闭请求（tabCloseRequested(int)）。
 *  @note  发射点为“tabsClosable 且点击页签关闭按钮”，位于 XTabBar 侧
 *         （对标 QTabBar::tabCloseRequested）。当前页签条关闭交互尚未
 *         实现（tabsClosable 仅存状态），本转发为预留接线：页签条侧
 *         一旦真发射，容器即同步通知订阅者。 */
static void xtabwidget_tabCloseRequestedForward(void* sender, XVarList* args)
{
    XTabWidget* self = xtabwidget_ownerOf(sender);
    int idx;
    XVarList* arguments;

    if (!self) return;
    idx = 0;
    if (args) {
        XVarList_start(args);
        idx = XVarList_arg(args, int);
    }
    if (!((XObject*)self)->m_signalSlot) return;
    arguments = XVarList_Create(XVar(int, idx));
    if (!arguments) return;
    XObject_emitSignal((XObject*)self,
                       (size_t)XTabWidget_tabCloseRequested_signal,
                       arguments, NULL, NULL, XEVENT_PRIORITY_NORMAL);
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
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTabWidget_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    /* 页与内容控件不可复制（Qt 同语义：仅结构属性拷贝）。 */
    self->m_tabPosition = other->m_tabPosition;
    self->m_tabsClosable = other->m_tabsClosable;
    self->m_movable = other->m_movable;
    /* 角部件为借用指针，不随 copy 转移：清空本侧登记，避免悬挂。 */
    for (i = 0; i < 4; ++i) self->m_cornerWidgets[i] = NULL;
}

static void VXTabWidget_move(XTabWidget* self, XTabWidget* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XTabWidget_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    self->m_tabPosition = other->m_tabPosition;
    self->m_tabsClosable = other->m_tabsClosable;
    self->m_movable = other->m_movable;
    /* 角部件借用指针随 move 整体转移，源侧清空防双持。 */
    for (i = 0; i < 4; ++i) {
        self->m_cornerWidgets[i] = other->m_cornerWidgets[i];
        other->m_cornerWidgets[i] = NULL;
    }
    other->m_tabPosition = 0;
    other->m_tabsClosable = false;
    other->m_movable = false;
}

/* ==================== 生命周期 ==================== */

static void VXTabWidget_deinit(XTabWidget* self)
{
    int i;
    if (!self) return;
    if (self->m_pages) {
        /* 页容器为 XWidget_init 的嵌入式堆对象（对齐 removeTab 惯例：
           deinit_base + XFree_System，而非 delete_base——delete_base
           仅对 IsHeap=true 的对象释放存储，测试的 XMemory_malloc+init
           页对象会泄漏本体）。 */
        for (i = 0; i < self->m_count; ++i) {
            if (self->m_pages[i]) {
                XWidget_deinit_base(self->m_pages[i]);
                XFree_System(self->m_pages[i]);
                self->m_pages[i] = NULL;
            }
        }
        XFree_System(self->m_pages);
        self->m_pages = NULL;
    }
    if (self->m_clients) {
        XFree_System(self->m_clients);
        self->m_clients = NULL;
    }
    /* 角部件为借用指针：仅解除登记，不销毁部件本体。 */
    {
        int c;
        for (c = 0; c < 4; ++c) self->m_cornerWidgets[c] = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XTabWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTabWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXTabWidget_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXTabWidget_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTabWidget_deinit);
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
    self->m_cornerWidgets[0] = NULL; /* TopLeft */
    self->m_cornerWidgets[1] = NULL; /* TopRight */
    self->m_cornerWidgets[2] = NULL; /* BottomLeft */
    self->m_cornerWidgets[3] = NULL; /* BottomRight */
    XTabBar_init(&self->m_tabBar, (XWidget*)self, 0);
    XWidget_show((XWidget*)&self->m_tabBar);
    XObject_connect_2((XObject*)&self->m_tabBar,
                      (size_t)XTabBar_currentChanged_signal(
                          &self->m_tabBar, 0),
                      xtabwidget_currentChangedForward);
    XObject_connect_2((XObject*)&self->m_tabBar,
                      (size_t)XTabBar_tabBarClicked_signal(
                          &self->m_tabBar, 0),
                      xtabwidget_tabBarClickedForward);
    XObject_connect_2((XObject*)&self->m_tabBar,
                      (size_t)XTabBar_tabBarDoubleClicked_signal(
                          &self->m_tabBar, 0),
                      xtabwidget_tabBarDoubleClickedForward);
    XObject_connect_2((XObject*)&self->m_tabBar,
                      (size_t)XTabBar_tabCloseRequested_signal(&self->m_tabBar),
                      xtabwidget_tabCloseRequestedForward);
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

int XTabWidget_addTab(XTabWidget* self, XWidget* page, const XString* label)
{
    return XTabWidget_insertTab(self, self ? self->m_count : 0, page, label);
}

int XTabWidget_addTab_2(XTabWidget* self, XWidget* page, const char* label)
{
    return XTabWidget_insertTab_2(self, self ? self->m_count : 0, page, label);
}

int XTabWidget_insertTab(XTabWidget* self, int index, XWidget* page,
                         const XString* label)
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
        XMemmove(&self->m_pages[index + 1], &self->m_pages[index],
                sizeof(XWidget*) * (size_t)(self->m_count - index));
        XMemmove(&self->m_clients[index + 1], &self->m_clients[index],
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

int XTabWidget_insertTab_2(XTabWidget* self, int index, XWidget* page,
                           const char* label)
{
    XString_Init_Utf8(tmp, label ? label : "");
    index = XTabWidget_insertTab(self, index, page, tmp);
    XString_deinit_base(tmp);
    return index;
}

void XTabWidget_removeTab(XTabWidget* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return;
    if (self->m_pages[index]) {
        XWidget_deinit_base(self->m_pages[index]);
        XFree_System(self->m_pages[index]);
    }
    XMemmove(&self->m_pages[index], &self->m_pages[index + 1],
            sizeof(XWidget*) * (size_t)(self->m_count - index - 1));
    XMemmove(&self->m_clients[index], &self->m_clients[index + 1],
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

XString* XTabWidget_tabText(const XTabWidget* self, int index)
{
    return self ? XTabBar_tabText(&self->m_tabBar, index) : NULL;
}

const char* XTabWidget_tabText_2(const XTabWidget* self, int index)
{
    return self ? XTabBar_tabText_2(&self->m_tabBar, index) : "";
}

void XTabWidget_setTabText(XTabWidget* self, int index, const XString* text)
{
    if (self) XTabBar_setTabText(&self->m_tabBar, index, text);
}

void XTabWidget_setTabText_2(XTabWidget* self, int index, const char* text)
{
    if (self) XTabBar_setTabText_2(&self->m_tabBar, index, text);
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
void XTabWidget_setCurrentWidget(XTabWidget* self, XWidget* page)
{
    int index;
    if (!self || !page) return;
    index = XTabWidget_indexOf(self, page);
    if (index >= 0) XTabWidget_setCurrentIndex(self, index);
}

void XTabWidget_setWidget(XTabWidget* self, XWidget* widget)
{
    int idx;
    int held;
    XWidget* old;
    if (!self || widget == (XWidget*)self) return;
    /* 替换以“存在页容器”为前提：尚无任何页时不动作（建页须页签
       文本，走 addTab/insertTab，setWidget 不代建）。 */
    idx = self->m_currentIndex;
    if (idx < 0 || idx >= self->m_count || !self->m_pages[idx]) return;
    old = self->m_clients[idx];
    /* 同指针幂等：重设现有内容为无操作（对标 Qt setWidget 家族）。 */
    if (old == widget) return;
    /* 新控件已登记在其它页：仅解除那条借用记录（控件本体随下方
       reparent 归入当前页容器，原页转为无内容——对标 Qt 同一子控件
       挂到新父即随迁）。 */
    if (widget) {
        held = XTabWidget_indexOf(self, widget);
        if (held >= 0 && held != idx) self->m_clients[held] = NULL;
    }
    /* 替换语义：旧控件摘除父链转独立顶层（XWidget_setParent(NULL)
       即窗口化并隐藏），不销毁——所有权转移调用方（对标 QDockWidget
       /QMdiSubWindow::setWidget 旧件不删）。 */
    if (old)
        XWidget_setParent(old, NULL, 0);
    /* 装入新控件并布局：reparent 到当前页容器，铺满页几何并按当前
       页同步显隐。 */
    self->m_clients[idx] = widget;
    if (widget)
        XWidget_setParent(widget, self->m_pages[idx], 0);
    xtabwidget_layout(self);
    xtabwidget_showCurrent(self);
}

void XTabWidget_setTabIcon(XTabWidget* self, int index, const XString* path)
{
    if (self) XTabBar_setTabIcon(&self->m_tabBar, index, path);
}
void XTabWidget_setTabIcon_2(XTabWidget* self, int index, const char* path)
{
    if (self) XTabBar_setTabIcon_2(&self->m_tabBar, index, path);
}
const XString* XTabWidget_tabIcon(const XTabWidget* self, int index)
{
    return self ? XTabBar_tabIcon(&self->m_tabBar, index) : NULL;
}
const char* XTabWidget_tabIcon_2(const XTabWidget* self, int index)
{
    return self ? XTabBar_tabIcon_2(&self->m_tabBar, index) : "";
}

void XTabWidget_setTabToolTip(XTabWidget* self, int index, const XString* tip)
{
    if (self) XTabBar_setTabToolTip(&self->m_tabBar, index, tip);
}
void XTabWidget_setTabToolTip_2(XTabWidget* self, int index, const char* tip)
{
    if (self) XTabBar_setTabToolTip_2(&self->m_tabBar, index, tip);
}

void XTabWidget_setTabWhatsThis(XTabWidget* self, int index,
                                const XString* text)
{
    /* 帮助文本无独立槽位：与提示共用存储（项目简化）。 */
    if (self) XTabBar_setTabToolTip(&self->m_tabBar, index, text);
}
void XTabWidget_setTabWhatsThis_2(XTabWidget* self, int index,
                                  const char* text)
{
    if (self) XTabBar_setTabToolTip_2(&self->m_tabBar, index, text);
}

void XTabWidget_setTabVisible(XTabWidget* self, int index, bool visible)
{
    if (self) XTabBar_setTabVisible(&self->m_tabBar, index, visible);
}
bool XTabWidget_isTabVisible(const XTabWidget* self, int index)
{
    return self ? XTabBar_isTabVisible(&self->m_tabBar, index) : true;
}

void XTabWidget_setTabBarAutoHide(XTabWidget* self, bool enable)
{
    if (self) {
        self->m_tabBar.m_autoHide = enable;
        XWidget_setVisible((XWidget*)&self->m_tabBar,
                           !enable || XTabBar_count(&self->m_tabBar) > 1);
    }
}
bool XTabWidget_tabBarAutoHide(const XTabWidget* self)
{
    return self ? self->m_tabBar.m_autoHide : false;
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

void XTabWidget_clear(XTabWidget* self)
{
    if (!self) return;
    while (self->m_count > 0) XTabWidget_removeTab(self, self->m_count - 1);
}

void XTabWidget_setDocumentMode(XTabWidget* self, bool enable)
{
    if (self) XTabBar_setDocumentMode(&self->m_tabBar, enable);
}
bool XTabWidget_documentMode(const XTabWidget* self)
{
    return self ? XTabBar_documentMode(&self->m_tabBar) : false;
}

void XTabWidget_setElideMode(XTabWidget* self, int mode)
{
    if (self) XTabBar_setElideMode(&self->m_tabBar, mode);
}
int XTabWidget_elideMode(const XTabWidget* self)
{
    return self ? XTabBar_elideMode(&self->m_tabBar) : 0;
}

void XTabWidget_setTabShape(XTabWidget* self, int shape)
{
    if (self) XTabBar_setShape(&self->m_tabBar, shape);
}
int XTabWidget_tabShape(const XTabWidget* self)
{
    return self ? XTabBar_shape(&self->m_tabBar) : 0;
}

void XTabWidget_setUsesScrollButtons(XTabWidget* self, bool enable)
{
    if (self) XTabBar_setUsesScrollButtons(&self->m_tabBar, enable);
}
bool XTabWidget_usesScrollButtons(const XTabWidget* self)
{
    return self ? XTabBar_usesScrollButtons(&self->m_tabBar) : false;
}

void XTabWidget_setIconSize(XTabWidget* self, int size)
{
    if (self) XTabBar_setIconSize(&self->m_tabBar, size);
}
int XTabWidget_iconSize(const XTabWidget* self)
{
    return self ? XTabBar_iconSize(&self->m_tabBar) : 0;
}

const XString* XTabWidget_tabToolTip(const XTabWidget* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return NULL;
    return XTabBar_tabToolTip(&self->m_tabBar, index);
}

const XString* XTabWidget_tabWhatsThis(const XTabWidget* self, int index)
{
    /* 帮助文本无独立槽位：与提示共用存储（项目简化）。 */
    return XTabWidget_tabToolTip(self, index);
}

XWidget* XTabWidget_cornerWidget(const XTabWidget* self, int corner)
{
    if (!self || corner < 0 || corner > 3) return NULL;
    return self->m_cornerWidgets[corner];
}

void XTabWidget_setCornerWidget(XTabWidget* self, XWidget* widget, int corner)
{
    XWidget* old;
    if (!self || corner < 0 || corner > 3) return;
    if (widget == (XWidget*)self) return;
    /* 借用挂载：Qt 同语义——角部件尚未挂到本控件时先 reparent。 */
    if (widget && XWidget_parentWidget(widget) != (XWidget*)self)
        XWidget_setParent(widget, (XWidget*)self, 0);
    old = self->m_cornerWidgets[corner];
    if (old && old != widget)
        XWidget_setVisible(old, false); /* 旧角部件隐藏（不销毁）。 */
    self->m_cornerWidgets[corner] = widget;
    xtabwidget_layout(self);
}

/* ==================== 信号 ==================== */

void* XTabWidget_currentChanged_signal(XTabWidget* self, int index)
{
    (void)index;
    return (void*)(size_t)XTabWidget_currentChanged_signal;
}
void* XTabWidget_tabClicked_signal(XTabWidget* self)
{
    return (void*)(size_t)XTabWidget_tabClicked_signal;
}

void* XTabWidget_tabCloseRequested_signal(XTabWidget* self)
{
    /* 仅返回信号标识；真发射经 init 中 connect_2 的
     * xtabwidget_tabCloseRequestedForward 转发（发射点在 XTabBar 侧，
     * 当前为预留，见头文件 @note）。 */
    (void)self;
    return (void*)(size_t)XTabWidget_tabCloseRequested_signal;
}




void* XTabWidget_tabBarClicked_signal(XTabWidget* self)
{
    XVarList* arguments;
    int index;

    if (!self)
        return (void*)(size_t)XTabWidget_tabBarClicked_signal;
    index = self->m_tabBar.m_currentIndex;
    arguments = XVarList_Create(XVar(int, index));
    if (arguments) {
        if (((XObject*)self)->m_signalSlot)
            XObject_emitSignal((XObject*)self,
                               (size_t)XTabWidget_tabBarClicked_signal,
                               arguments, NULL, NULL,
                               XEVENT_PRIORITY_NORMAL);
        else
            XVarList_delete(arguments);
    }
    return (void*)(size_t)XTabWidget_tabBarClicked_signal;
}

void* XTabWidget_tabBarDoubleClicked_signal(XTabWidget* self)
{
    XVarList* arguments;
    int index;

    if (!self)
        return (void*)(size_t)XTabWidget_tabBarDoubleClicked_signal;
    index = self->m_tabBar.m_currentIndex;
    arguments = XVarList_Create(XVar(int, index));
    if (arguments) {
        if (((XObject*)self)->m_signalSlot)
            XObject_emitSignal((XObject*)self,
                               (size_t)XTabWidget_tabBarDoubleClicked_signal,
                               arguments, NULL, NULL,
                               XEVENT_PRIORITY_NORMAL);
        else
            XVarList_delete(arguments);
    }
    return (void*)(size_t)XTabWidget_tabBarDoubleClicked_signal;
}


















#endif /* XWIDGET_ON && XTABBAR_ON && XTABWIDGET_ON */
