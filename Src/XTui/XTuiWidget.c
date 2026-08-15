/**
 * @file       XTuiWidget.c
 * @brief      XTui 控件基类实现。
 */

#include "XTuiWidget.h"

#if XTUI_ON && XTUI_WIDGET_ON
#include <string.h>

/* ==================== 默认虚函数实现 ==================== */

static bool VXTuiWidget_render(XTuiWidget* self, XTuiScreen* screen)
{
    (void)self;
    (void)screen;
    return false;
}

static bool VXTuiWidget_keyPress(XTuiWidget* self, const XTuiKeyEvent* event)
{
    (void)self;
    (void)event;
    return false;
}

static void VXTuiWidget_resize(XTuiWidget* self, int width, int height)
{
    if (!self)
        return;
    if (width > 0)
        self->m_rect.width = width;
    if (height > 0)
        self->m_rect.height = height;
}

static void VXTuiWidget_focusIn(XTuiWidget* self)
{
    if (self)
        self->m_focused = true;
}

static void VXTuiWidget_focusOut(XTuiWidget* self)
{
    if (self)
        self->m_focused = false;
}

static void VXTuiWidget_deinit(XTuiWidget* self)
{
    if (!self)
        return;
    if (self->m_name) {
        XFree_System(self->m_name);
        self->m_name = NULL;
    }
    self->m_parent = NULL;
    self->m_visible = false;
    self->m_focused = false;
    self->m_enabled = false;
}

static bool copyName(XTuiWidget* dest, const XTuiWidget* src)
{
    if (!src->m_name)
        return true;
    size_t len = strlen(src->m_name);
    char* name = (char*)XMalloc_System(len + 1);
    if (!name)
        return false;
    memcpy(name, src->m_name, len + 1);
    if (dest->m_name)
        XFree_System(dest->m_name);
    dest->m_name = name;
    return true;
}

static void VXTuiWidget_copy(XTuiWidget* dest, const XTuiWidget* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTuiWidget_init(dest);
    dest->m_rect = src->m_rect;
    dest->m_parent = src->m_parent;
    dest->m_visible = src->m_visible;
    dest->m_focused = src->m_focused;
    dest->m_enabled = src->m_enabled;
    copyName(dest, src);
}

static void VXTuiWidget_move(XTuiWidget* dest, XTuiWidget* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTuiWidget_init(dest);
    dest->m_rect = src->m_rect;
    dest->m_parent = src->m_parent;
    dest->m_visible = src->m_visible;
    dest->m_focused = src->m_focused;
    dest->m_enabled = src->m_enabled;
    if (dest->m_name)
        XFree_System(dest->m_name);
    dest->m_name = src->m_name;
    src->m_name = NULL;
}

/* ==================== 虚函数表 ==================== */

XVtable* XTuiWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTuiWidget)
    XVTABLE_INHERIT_XCLASS(XClass);
    void* table[] = {
        VXTuiWidget_render,
        VXTuiWidget_keyPress,
        VXTuiWidget_resize,
        VXTuiWidget_focusIn,
        VXTuiWidget_focusOut
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTuiWidget_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTuiWidget_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTuiWidget_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XTuiWidget);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造与析构 ==================== */

void XTuiWidget_init(XTuiWidget* widget)
{
    if (!widget)
        return;
    memset(((XClass*)widget) + 1, 0, sizeof(XTuiWidget) - sizeof(XClass));
    XClass_init(widget);
    XClassGetVtable(widget) = XTuiWidget_class_init();
    widget->m_rect = (XRect){0, 0, 1, 1};
    widget->m_visible = true;
    widget->m_enabled = true;
}

XTuiWidget* XTuiWidget_create_ex(XMemoryType memory)
{
    XTuiWidget* widget = (XTuiWidget*)XMemory_malloc(sizeof(XTuiWidget), memory);
    if (!widget)
        return NULL;
    XTuiWidget_init(widget);
    Set_Class_Memory(widget, memory); Set_Class_IsHeap(widget, true);
    return widget;
}

/* ==================== 虚函数调度 ==================== */

bool XTuiWidget_render_base(XTuiWidget* self, XTuiScreen* screen)
{
    if (ISNULL(self, "XTuiWidget") || ISNULL(XClassGetVtable(self), "Vtable") || !screen)
        return false;
    if (!self->m_visible)
        return false;
    return XClassGetVirtualFunc(self, EXTuiWidget_Render, bool(*)(XTuiWidget*, XTuiScreen*))(self, screen);
}

bool XTuiWidget_keyPress_base(XTuiWidget* self, const XTuiKeyEvent* event)
{
    if (ISNULL(self, "XTuiWidget") || ISNULL(XClassGetVtable(self), "Vtable") || !event)
        return false;
    if (!self->m_enabled || !self->m_visible)
        return false;
    return XClassGetVirtualFunc(self, EXTuiWidget_KeyPress, bool(*)(XTuiWidget*, const XTuiKeyEvent*))(self, event);
}

void XTuiWidget_resize_base(XTuiWidget* self, int width, int height)
{
    if (ISNULL(self, "XTuiWidget") || ISNULL(XClassGetVtable(self), "Vtable"))
        return;
    XClassGetVirtualFunc(self, EXTuiWidget_Resize, void(*)(XTuiWidget*, int, int))(self, width, height);
}

void XTuiWidget_focusIn_base(XTuiWidget* self)
{
    if (ISNULL(self, "XTuiWidget") || ISNULL(XClassGetVtable(self), "Vtable"))
        return;
    XClassGetVirtualFunc(self, EXTuiWidget_FocusIn, void(*)(XTuiWidget*))(self);
}

void XTuiWidget_focusOut_base(XTuiWidget* self)
{
    if (ISNULL(self, "XTuiWidget") || ISNULL(XClassGetVtable(self), "Vtable"))
        return;
    XClassGetVirtualFunc(self, EXTuiWidget_FocusOut, void(*)(XTuiWidget*))(self);
}

/* ==================== 属性 ==================== */

void XTuiWidget_setRect(XTuiWidget* self, const XRect* rect)
{
    if (!self || !rect)
        return;
    self->m_rect = *rect;
    if (self->m_rect.width < 1)
        self->m_rect.width = 1;
    if (self->m_rect.height < 1)
        self->m_rect.height = 1;
}

XRect XTuiWidget_rect(const XTuiWidget* self)
{
    XRect r = (XRect){0, 0, 1, 1};
    if (self)
        r = self->m_rect;
    return r;
}

void XTuiWidget_setName(XTuiWidget* self, const char* name)
{
    if (!self)
        return;
    if (!name) {
        if (self->m_name) {
            XFree_System(self->m_name);
            self->m_name = NULL;
        }
        return;
    }
    size_t len = strlen(name);
    char* copy = (char*)XMalloc_System(len + 1);
    if (!copy)
        return;
    memcpy(copy, name, len + 1);
    if (self->m_name)
        XFree_System(self->m_name);
    self->m_name = copy;
}

const char* XTuiWidget_name(const XTuiWidget* self)
{
    return self ? (const char*)self->m_name : NULL;
}

void XTuiWidget_setParent(XTuiWidget* self, XTuiWidget* parent)
{
    if (self)
        self->m_parent = parent;
}

XTuiWidget* XTuiWidget_parent(const XTuiWidget* self)
{
    return self ? self->m_parent : NULL;
}

void XTuiWidget_setVisible(XTuiWidget* self, bool visible)
{
    if (self)
        self->m_visible = visible;
}

bool XTuiWidget_isVisible(const XTuiWidget* self)
{
    return self ? self->m_visible : false;
}

void XTuiWidget_setEnabled(XTuiWidget* self, bool enabled)
{
    if (self)
        self->m_enabled = enabled;
}

bool XTuiWidget_isEnabled(const XTuiWidget* self)
{
    return self ? self->m_enabled : false;
}

#endif /* XTUI_ON && XTUI_WIDGET_ON */
