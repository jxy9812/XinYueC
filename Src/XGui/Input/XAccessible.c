/****************************************************************************
 * @file       XAccessible.c
 * @brief      XAccessible 可访问节点实现；无平台 API。
 ****************************************************************************/
#if XGUI_ON

#include "XAccessible.h"
#include "XMemory.h"

#include "XAlgorithm.h"
#if XWINDOW_ON && XACCESSIBLE_ON
#include "XWindow.h"
#if XWIDGET_ON
#include "XWidget.h"
#if XLABEL_ON && XFRAME_ON
#include "XLabel.h"      /* buddy 回退：取以本控件为 buddy 的标签文本。 */
#endif
#if XGROUPBOX_ON
#include "XGroupBox.h"   /* buddy 回退：父控件是分组框时取其标题。 */
#endif
#endif
#if XWIDGET_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
#include "XApplication.h"
#endif
#include "XGuiApplication.h"
#include "XCoreApplication.h"

static void VXAccessible_deinit(XAccessible* self)
{
    if (!self) return;
    if (self->m_name) XClassDelete((XClass*)self->m_name);
    if (self->m_description) XClassDelete((XClass*)self->m_description);
    self->m_name = NULL;
    self->m_description = NULL;
    self->m_window = NULL;
    self->m_widget = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XAccessible_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAccessible)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAccessible_deinit);
    return XVTABLE_DEFAULT;
}

XAccessible* XAccessible_createForWindow_ex(XMemoryType memory, XWindow* window)
{
    XAccessible* self;
    if (!window) return NULL;
    self = (XAccessible*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XAccessible);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    self->m_window = window;
    self->m_role = XAccessibleRole_Window;
    return self;
}

XAccessible* XAccessible_createForWidget_ex(XMemoryType memory, XWidget* widget)
{
#if XWIDGET_ON
    XAccessible* self;
    if (!widget) return NULL;
    self = (XAccessible*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XAccessible);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    self->m_widget = widget;
    self->m_role = XWidget_isWindow(widget) ? XAccessibleRole_Window :
                   XAccessibleRole_Client;
    return self;
#else
    (void)memory;
    (void)widget;
    return NULL;
#endif
}

XAccessible* XAccessible_createApplication_ex(XMemoryType memory)
{
    XAccessible* self = (XAccessible*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XAccessible);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    self->m_role = XAccessibleRole_Application;
    self->m_applicationRoot = true;
    return self;
}

bool XAccessible_isValid(const XAccessible* self)
{ return self && (self->m_window || self->m_widget || self->m_applicationRoot); }
XAccessibleRole XAccessible_role(const XAccessible* self)
{ return XAccessible_isValid(self) ? self->m_role : XAccessibleRole_Unknown; }
XRect XAccessible_rect(const XAccessible* self)
{
    XRect rect = {0, 0, 0, 0};
    if (!XAccessible_isValid(self)) return rect;
#if XWIDGET_ON
    if (self->m_widget) {
        XPoint origin = {0, 0};
        origin = XWidget_mapToGlobal(self->m_widget, &origin);
        rect = XWidget_geometry(self->m_widget);
        rect.x = origin.x;
        rect.y = origin.y;
        return rect;
    }
#endif
    if (self->m_window) return XWindow_geometry(self->m_window);
    return rect;
}
bool XAccessible_isVisible(const XAccessible* self)
{
    if (!XAccessible_isValid(self)) return false;
    if (self->m_applicationRoot) return true;
#if XWIDGET_ON
    if (self->m_widget) return XWidget_isVisible(self->m_widget);
#endif
    return XWindow_isVisible(self->m_window);
}
XAccessibleState XAccessible_state(const XAccessible* self)
{
    XAccessibleState st = 0;
    if (!XAccessible_isValid(self)) return 0;
    if (self->m_applicationRoot) {
        /* 应用根：活跃 + 可见 + 可管理后代（AT-SPI Application 语义）。 */
        return XACCESSIBLE_STATE_ACTIVE | XACCESSIBLE_STATE_VISIBLE |
               XACCESSIBLE_STATE_SHOWING |
               XACCESSIBLE_STATE_MANAGES_DESCENDANTS;
    }
#if XWIDGET_ON
    if (self->m_widget) {
        XWidget* w = self->m_widget;
        if (XWidget_isVisible(w)) {
            st |= XACCESSIBLE_STATE_VISIBLE;
            /* showing=可见且父链可见（AT-SPI：showing 表示在屏上呈现）。 */
            st |= XACCESSIBLE_STATE_SHOWING;
        }
        /* enabled：控件自身及其所有祖先均启用（AT-SPI 语义：禁用则不置位，
           且不置 sensitive——禁用控件既不可用也不响应事件）。 */
        if (XWidget_isEnabled(w)) {
            st |= XACCESSIBLE_STATE_ENABLED;
            st |= XACCESSIBLE_STATE_SENSITIVE;
        }
        if (XWidget_hasFocus(w)) st |= XACCESSIBLE_STATE_FOCUSED;
        return st;
    }
#endif
    if (self->m_window) {
        if (XWindow_isVisible(self->m_window)) {
            st |= XACCESSIBLE_STATE_VISIBLE | XACCESSIBLE_STATE_SHOWING;
        }
        if (XWindow_isActive(self->m_window)) st |= XACCESSIBLE_STATE_ACTIVE;
        if (XWindow_isModal(self->m_window)) st |= XACCESSIBLE_STATE_MODAL;
        /* 窗口默认可获焦点、可调整大小（顶层窗口语义）。 */
        st |= XACCESSIBLE_STATE_FOCUSABLE | XACCESSIBLE_STATE_RESIZABLE;
        st |= XACCESSIBLE_STATE_SENSITIVE | XACCESSIBLE_STATE_SELECTABLE;
    }
    return st;
}

#if XWIDGET_ON
#if XLABEL_ON && XFRAME_ON
/**
 * @brief 伙伴（buddy）文本：与本控件配对的标签文本（对标 Qt
 *        qaccessiblewidget.cpp:39-66 的 buddyString）。
 * @details 对标 Qt 语义：遍历**父控件**的子控件，找一个以本控件为
 *          buddy 的 XLabel，取其文本；找不到则回退父控件是分组框时的
 *          分组框标题；都不是则返回 NULL（Qt 返回空串）。
 * @param      widget 目标控件；可为 NULL。
 * @return     UTF-8 借用视图（归标签/分组框所有，调用方不得释放或修改，
 *             且仅在本函数调用期间有效）；无伙伴文本时返回 NULL。
 */
static const char* xaccessible_buddyTextUtf8(const XWidget* widget)
{
    const XWidget* parent;
    const XVector* children;
    size_t n;
    size_t i;
    if (!widget) return NULL;
    parent = XWidget_parentWidget(widget);
    if (!parent) return NULL;
    children = XObject_children((const XObject*)parent);
    n = children ? XVector_size_base((const XContainer*)children) : 0;
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base(children, (int64_t)i);
        XLabel* label;
        if (!child || !child->m_is_widget) continue;
        /* 类型判定用虚表同一性（对标 Qt 的 qobject_cast<QLabel*>）：
         * 派生的 XLabel 子类虚表不同——本仓 XLabel 无类型注册表，故只认
         * XLabel 本体（已知派生类 XPerformanceOverlay 无 buddy 语义，
         * 不影响本回落链）。 */
        if (XClassGetVtable(child) != XLabel_class_init()) continue;
        label = (XLabel*)child;
        if (XLabel_buddy(label) != (XWidget*)widget) continue;
        {
            const XString* text = XLabel_text(label);
            if (!text) continue;
            return XString_toUtf8(text);
        }
    }
#if XGROUPBOX_ON
    /* Qt 口径：父控件是分组框时取其标题（buddyString 的第二段）。 */
    if (XClassGetVtable((const XObject*)parent) == XGroupBox_class_init())
        return XGroupBox_title((const XGroupBox*)parent);
#endif /* XGROUPBOX_ON */
    return NULL;
}
#endif /* XLABEL_ON && XFRAME_ON */
#endif /* XWIDGET_ON */

XString* XAccessible_name(const XAccessible* self)
{
    if (!XAccessible_isValid(self)) return XString_create_utf8("");
    if (self->m_applicationRoot) {
        const XString* name = XCoreApplication_applicationName();
        return name ? XString_create_copy(name) : XString_create_utf8("XinYueC");
    }
    if (self->m_name) return XString_create_copy(self->m_name);
#if XWIDGET_ON
    if (self->m_widget) {
        const XString* name = XWidget_accessibleName(self->m_widget);
        if (name && XString_toUtf8_length(name) > 0)
            return XString_create_copy(name);
        /* 对标 Qt 6.8（qaccessiblewidget.cpp:365-378）：Name 回落链
         * **区分窗口与子控件**——
         *   窗口节点：取窗口标题（最小化时取图标文本，Qt 同款）；
         *   子控件节点：取伙伴（buddy）标签文本 / 父分组框标题。
         * 本实现此前两类节点同走"窗口标题 → objectName → toolTip"，
         * 子控件节点永远拿不到伙伴标签文本（AT-SPI 朗读时读不出控件
         * 旁边的标签说明）。 */
        if (XWidget_isWindow(self->m_widget)) {
            const XString* title = XWidget_isMinimized(self->m_widget)
                ? XWidget_windowIconText(self->m_widget)
                : XWidget_windowTitle(self->m_widget);
            if (title && XString_toUtf8_length(title) > 0)
                return XString_create_copy(title);
        } else {
#if XLABEL_ON && XFRAME_ON
            const char* buddy = xaccessible_buddyTextUtf8(self->m_widget);
            if (buddy && buddy[0] != '\0')
                return XString_create_utf8(buddy);
#endif /* XLABEL_ON && XFRAME_ON */
        }
        name = XObject_objectName((const XObject*)self->m_widget);
        if (name && XString_toUtf8_length(name) > 0)
            return XString_create_copy(name);
        name = XWidget_toolTip(self->m_widget);
        return name ? XString_create_copy(name) : XString_create_utf8("");
    }
#endif
    return XWindow_title(self->m_window);
}
void XAccessible_setName(XAccessible* self, const XString* name)
{
    if (!self) return;
    if (self->m_name) XClassDelete((XClass*)self->m_name);
    self->m_name = name ? XString_create_copy(name) : NULL;
}
XString* XAccessible_description(const XAccessible* self)
{
    if (!XAccessible_isValid(self)) return XString_create_utf8("");
    if (self->m_description)
        return XString_create_copy(self->m_description);
#if XWIDGET_ON
    if (self->m_widget) {
        const XString* desc = XWidget_accessibleDescription(self->m_widget);
        if (desc && XString_toUtf8_length(desc) > 0)
            return XString_create_copy(desc);
        /* 对标 Qt 6.8（qaccessiblewidget.cpp:379-384）：描述为空时回落
         * 工具提示（toolTip）——AT-SPI 朗读的描述文本来源于提示气泡，
         * 此前本实现直接返回空串 → 有提示的控件在无障碍层读不到任何
         * 补充说明。 */
        desc = XWidget_toolTip(self->m_widget);
        if (desc && XString_toUtf8_length(desc) > 0)
            return XString_create_copy(desc);
    }
#endif
    return XString_create_utf8("");
}
XString* XAccessible_help(const XAccessible* self)
{
    if (!XAccessible_isValid(self)) return XString_create_utf8("");
#if XWIDGET_ON
    if (self->m_widget) {
        /* 对标 Qt 6.8（qaccessiblewidget.cpp:385-389）：Help 取控件的
         * What's This 文本。 */
        const XString* help = XWidget_whatsThis(self->m_widget);
        if (help && XString_toUtf8_length(help) > 0)
            return XString_create_copy(help);
    }
#endif
    return XString_create_utf8("");
}
void XAccessible_setDescription(XAccessible* self, const XString* description)
{
    if (!self) return;
    if (self->m_description) XClassDelete((XClass*)self->m_description);
    self->m_description = description ? XString_create_copy(description) : NULL;
}
XWindow* XAccessible_window(const XAccessible* self)
{
    if (!XAccessible_isValid(self)) return NULL;
#if XWIDGET_ON
    return self->m_widget ? XWidget_nativeWindow(self->m_widget) : self->m_window;
#else
    return self->m_window;
#endif
}

XWidget* XAccessible_widget(const XAccessible* self)
{ return XAccessible_isValid(self) ? self->m_widget : NULL; }

XAccessible* XAccessible_parent(const XAccessible* self)
{
#if XWIDGET_ON
    XObject* parent;
    if (!XAccessible_isValid(self) || !self->m_widget) return NULL;
    parent = XObject_parent((XObject*)self->m_widget);
    if (!parent || !XObject_isWidgetType(parent)) return NULL;
    return ((XWidget*)parent)->m_accessible;
#else
    (void)self;
    return NULL;
#endif
}

#if XWIDGET_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
static bool XAccessible_isWidgetWindow(const XWindow* window)
{
    XVector* widgets;
    size_t i;
    if (!window) return false;
    widgets = XApplication_topLevelWidgets();
    if (!widgets) return false;
    for (i = 0; i < XVector_size_base((const XContainer*)widgets); ++i) {
        XWidget* widget = XVector_At_Base(widgets, (int64_t)i, XWidget*);
        if (widget && XWidget_nativeWindow(widget) == window) {
            XClassDelete((XClass*)widgets);
            return true;
        }
    }
    XClassDelete((XClass*)widgets);
    return false;
}
#else
static bool XAccessible_isWidgetWindow(const XWindow* window)
{ (void)window; return false; }
#endif

size_t XAccessible_childCount(const XAccessible* self)
{
    XVector* windows;
    XVector* widgets;
    size_t count = 0;
    size_t i;
    if (!self) return 0;
#if XWIDGET_ON
    if (self->m_widget) {
        const XVector* children = XObject_children((const XObject*)self->m_widget);
        if (!children) return 0;
        for (i = 0; i < XVector_size_base((const XContainer*)children); ++i) {
            XObject* child = XVector_At_Base(children, (int64_t)i, XObject*);
            if (child && XObject_isWidgetType(child)) ++count;
        }
        return count;
    }
#endif
    if (!self->m_applicationRoot) return 0;
 #if XGUIAPPLICATION_ON
    windows = XGuiApplication_allWindows();
    if (windows) {
        for (i = 0; i < XVector_size_base((const XContainer*)windows); ++i) {
            XWindow* window = XVector_At_Base(windows, (int64_t)i, XWindow*);
            if (window && !XAccessible_isWidgetWindow(window)) ++count;
        }
        XClassDelete((XClass*)windows);
    }
 #else
    (void)windows;
 #endif
#if XWIDGET_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
    widgets = XApplication_topLevelWidgets();
    if (widgets) {
        count += XVector_size_base((const XContainer*)widgets);
        XClassDelete((XClass*)widgets);
    }
#else
    (void)widgets;
#endif
    return count;
}

XAccessible* XAccessible_childAtIndex(const XAccessible* self, size_t index)
{
    XVector* windows;
    XVector* widgets;
    XWindow* window;
    XAccessible* result = NULL;
    size_t i;
    if (!self) return NULL;
#if XWIDGET_ON
    if (self->m_widget) {
        const XVector* children = XObject_children((const XObject*)self->m_widget);
        size_t seen = 0;
        if (!children) return NULL;
        for (i = 0; i < XVector_size_base((const XContainer*)children); ++i) {
            XObject* child = XVector_At_Base(children, (int64_t)i, XObject*);
            if (child && XObject_isWidgetType(child)) {
                if (seen++ == index) return ((XWidget*)child)->m_accessible;
            }
        }
        return NULL;
    }
#endif
    if (!self->m_applicationRoot) return NULL;
 #if XGUIAPPLICATION_ON
    windows = XGuiApplication_allWindows();
    if (windows) {
        for (i = 0; i < XVector_size_base((const XContainer*)windows); ++i) {
            window = XVector_At_Base(windows, (int64_t)i, XWindow*);
            if (window && !XAccessible_isWidgetWindow(window)) {
                if (index-- == 0) {
                    result = (XAccessible*)XWindow_accessibleRoot(window);
                    XClassDelete((XClass*)windows);
                    return result;
                }
            }
        }
        XClassDelete((XClass*)windows);
    }
 #else
    (void)windows;
 #endif
#if XWIDGET_ON && XAPPLICATION_ON && XGUIAPPLICATION_ON
    widgets = XApplication_topLevelWidgets();
    if (!widgets) return NULL;
    if (index < XVector_size_base((const XContainer*)widgets)) {
        XWidget* widget = XVector_At_Base(widgets, (int64_t)index, XWidget*);
        result = widget ? widget->m_accessible : NULL;
    }
    XClassDelete((XClass*)widgets);
#else
    (void)widgets;
#endif
    return result;
}

#endif /* XWINDOW_ON && XACCESSIBLE_ON */

#endif /* XGUI_ON */
