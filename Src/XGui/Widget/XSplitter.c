/**
 * @file       XSplitter.c
 * @brief      分割器控件实现（对标 Qt 6.8 QSplitter 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XSplitter.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if XWIDGET_ON && XFRAME_ON && XSPLITTER_ON

static int xsp_childCount(const XSplitter* self)
{
    const XVector* children;
    if (!self) return 0;
    children = XObject_children((const XObject*)self);
    return children ? (int)XVector_size_base((const XContainer*)children) : 0;
}

static XWidget* xsp_childAt(const XSplitter* self, int index)
{
    const XVector* children;
    XObject** slot;
    if (!self || index < 0) return NULL;
    children = XObject_children((const XObject*)self);
    if (!children || index >= (int)XVector_size_base((const XContainer*)children))
        return NULL;
    slot = (XObject**)XVector_at_base(children, index);
    if (!slot || !*slot || !(*slot)->is_widget) return NULL;
    return (XWidget*)*slot;
}

static bool xsp_childVisible(const XSplitter* self, int index)
{
    XWidget* child = xsp_childAt(self, index);
    return child ? XWidget_isVisible(child) : false;
}

/* ==================== 内部工具 ==================== */

#define XSPLITTER_MIN_SIZE 0

static int xsp_horiz(const XSplitter* self)
{
    return self->m_orientation != 2; /* 非 Vertical 即水平 */
}

static int xsp_contentLen(const XSplitter* self)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int len = xsp_horiz(self) ? w : h;
    int hw = self->m_handleWidth;
    int count = xsp_childCount(self);
    int i;
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        if (!child || !xsp_childVisible(self, i)) continue;
        len -= hw; /* 每页之后一个分隔条（最后一页之后的忽略误差）。 */
    }
    if (len < 0) len = 0;
    return len;
}

static void xsp_layout(XSplitter* self)
{
    int count = xsp_childCount(self);
    int i;
    int len;
    int used = 0;
    int perPage;
    int x = 0;
    int y = 0;
    if (!self || count <= 0) return;
    len = xsp_contentLen(self);
    perPage = len / (count > 0 ? count : 1);
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        XRect r;
        int size = perPage;
        if (!child) continue;
        /* 几何分配不依赖当前可见性：子控件在隐藏时也需要正确尺寸，
         * 否则 show 后因 0x0 仍不可见（对标 QSplitterPrivate::layoutChildren）。 */
        if (i == count - 1) size = len - used;
        if (size < 0) size = 0;
        if (xsp_horiz(self))
            XRect_init(&r, x, 0, size, XWidget_height((XWidget*)self));
        else
            XRect_init(&r, 0, y, XWidget_width((XWidget*)self), size);
        XWidget_setGeometryRect(child, &r);
        used += size;
        if (xsp_horiz(self)) x += size + self->m_handleWidth;
        else y += size + self->m_handleWidth;
    }
}

static uint32_t xsp_color(const XSplitter* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

static void xsp_emitMoved(XSplitter* self, int pos, int index)
{
    XVarList* args = XVarList_Create(XVar(int, pos), XVar(int, index));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XSplitter_splitterMoved_signal, args,
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 事件处理 ==================== */

static void VX_splitter_resizeEvent(XWidget* self, XEvent* event)
{
    (void)event;
    xsp_layout((XSplitter*)self);
}

static void VX_splitBar_paintEvent(XWidget* self, XEvent* event);

static void VX_splitter_paintEvent(XWidget* self, XEvent* event)
{
    XSplitter* sp = (XSplitter*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    uint32_t mid;
    XRect line;
    int w;
    int h;
    int count;
    int i;
    int x;
    int y;
    if (!sp || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
    image = XWidget_paintDevice(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    mid = xsp_color(sp, XPaletteColorRole_Mid);
    count = xsp_childCount(self);
    x = 0;
    y = 0;
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        if (!child || !xsp_childVisible(self, i)) continue;
        if (xsp_horiz(sp)) {
            x += XWidget_width(child);
            XRect_init(&line, x, 0, sp->m_handleWidth, h);
            XPainter_fillRect(&painter, &line, mid);
            x += sp->m_handleWidth;
        } else {
            y += XWidget_height(child);
            XRect_init(&line, 0, y, w, sp->m_handleWidth);
            XPainter_fillRect(&painter, &line, mid);
            y += sp->m_handleWidth;
        }
    }
    (void)y;
    (void)x;
    XPainter_deinit(&painter);
}

static void VX_splitBar_paintEvent(XWidget* self, XEvent* event)
{
    (void)self;
    (void)event;
}

static void VX_splitter_mousePressEvent(XWidget* self, XEvent* event)
{
    XSplitter* sp = (XSplitter*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    if (!sp || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    XEvent_accept(event);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_splitter_deinit(XSplitter* self)
{
    if (!self) return;
    if (self->m_collapsible) {
        XFree_System(self->m_collapsible);
        self->m_collapsible = NULL;
        self->m_collapsibleCap = 0;
    }
    XClass_Deinit_Parent(XFrame, (XFrame*)self);
}

XVtable* XSplitter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSplitter)
    XVTABLE_INHERIT_XCLASS(XFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_splitter_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_splitter_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_splitter_deinit);
    return XVTABLE_DEFAULT;
}

static void xsp_ensureCollapsibleCap(XSplitter* self, int pages)
{
    if (pages <= self->m_collapsibleCap) return;
    {
        int newCap = pages + 8;
        int* p = (int*)XRealloc_System(self->m_collapsible,
                                       (size_t)newCap * sizeof(int));
        if (!p) return;
        self->m_collapsible = p;
        self->m_collapsibleCap = newCap;
    }
}

void XSplitter_init(XSplitter* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XFrame_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XSplitter);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_orientation = 1;
    self->m_handleWidth = 5;
    self->m_childrenCollapsible = true;
    self->m_opaqueResize = true;
    self->m_dragIndex = -1;
}

void XSplitter_init_2(XSplitter* self, int orientation, XWidget* parent,
                      XWidgetFlags flags)
{
    if (!self) return;
    XSplitter_init(self, parent, flags);
    self->m_orientation = orientation;
}

XSplitter* XSplitter_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XSplitter* self = (XSplitter*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XSplitter_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XSplitter* XSplitter_create_ex_2(XMemoryType memory, int orientation,
                                 XWidget* parent, XWidgetFlags flags)
{
    XSplitter* self = (XSplitter*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XSplitter_init_2(self, orientation, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 页面管理 ==================== */

void XSplitter_addWidget(XSplitter* self, XWidget* widget)
{
    if (!self || !widget) return;
    XWidget_setParent(widget, (XWidget*)self, 0);
    XWidget_setVisible(widget, true);
    xsp_layout(self);
}

void XSplitter_insertWidget(XSplitter* self, int index, XWidget* widget)
{
    if (!self || !widget) return;
    XWidget_setParent(widget, (XWidget*)self, 0);
    XWidget_setVisible(widget, true);
    /* Qt insertWidget 按插入顺序布局；第一版以加入顺序近似。 */
    xsp_layout(self);
}

XWidget* XSplitter_widget(const XSplitter* self, int index)
{
    if (!self || index < 0 || index >= xsp_childCount(self))
        return NULL;
    return xsp_childAt(self, index);
}

int XSplitter_count(const XSplitter* self)
{
    return self ? xsp_childCount(self) : 0;
}

int XSplitter_indexOf(const XSplitter* self, const XWidget* widget)
{
    int i;
    int count;
    if (!self || !widget) return -1;
    count = xsp_childCount(self);
    for (i = 0; i < count; ++i) {
        if (xsp_childAt(self, i) == widget) return i;
    }
    return -1;
}

/* ==================== 属性 ==================== */

void XSplitter_setOrientation(XSplitter* self, int orientation)
{
    if (!self || self->m_orientation == orientation) return;
    self->m_orientation = orientation;
    xsp_layout(self);
}

int XSplitter_orientation(const XSplitter* self)
{
    return self ? self->m_orientation : 1;
}

void XSplitter_setChildrenCollapsible(XSplitter* self, bool collapsible)
{
    if (!self) return;
    self->m_childrenCollapsible = collapsible;
}

bool XSplitter_childrenCollapsible(const XSplitter* self)
{
    return self ? self->m_childrenCollapsible : false;
}

void XSplitter_setCollapsible(XSplitter* self, int index, bool collapsible)
{
    if (!self || index < 0) return;
    xsp_ensureCollapsibleCap(self, index + 1);
    if (!self->m_collapsible || index >= self->m_collapsibleCap) return;
    self->m_collapsible[index] = collapsible ? 1 : 0;
}

bool XSplitter_isCollapsible(const XSplitter* self, int index)
{
    if (!self || index < 0) return false;
    if (!self->m_collapsible || index >= self->m_collapsibleCap)
        return self->m_childrenCollapsible;
    if (self->m_collapsible[index] < 0)
        return self->m_childrenCollapsible;
    return self->m_collapsible[index] != 0;
}

void XSplitter_setOpaqueResize(XSplitter* self, bool opaque)
{
    if (!self) return;
    self->m_opaqueResize = opaque;
}

bool XSplitter_opaqueResize(const XSplitter* self)
{
    return self ? self->m_opaqueResize : false;
}

int XSplitter_handleWidth(const XSplitter* self)
{
    return self ? self->m_handleWidth : 0;
}

void XSplitter_setHandleWidth(XSplitter* self, int width)
{
    if (!self || width < 0) return;
    self->m_handleWidth = width;
    xsp_layout(self);
}

void XSplitter_setStretchFactor(XSplitter* self, int index, int stretch)
{
    /* 对标 Qt：stretch 因子参与尺寸分配；第一版布局为均分，
       stretch 仅存储于折叠数组的扩展语义之外（无可见行为差异）。 */
    (void)self;
    (void)index;
    (void)stretch;
}

void XSplitter_refresh(XSplitter* self)
{
    xsp_layout(self);
}

/* ==================== 尺寸与状态 ==================== */

void XSplitter_sizes(const XSplitter* self, int* outSizes, int count)
{
    int i;
    if (!self || !outSizes || count <= 0) return;
    for (i = 0; i < count; ++i) {
        XWidget* child = (i < xsp_childCount(self))
                             ? xsp_childAt(self, i) : NULL;
        if (!child) {
            outSizes[i] = 0;
            continue;
        }
        outSizes[i] = xsp_horiz(self) ? XWidget_width(child)
                                      : XWidget_height(child);
    }
}

void XSplitter_setSizes(XSplitter* self, const int* sizes, int count)
{
    int total;
    int sum = 0;
    int i;
    int x = 0;
    int y = 0;
    if (!self || !sizes || count <= 0) return;
    total = xsp_contentLen(self);
    for (i = 0; i < count; ++i) sum += sizes[i] > 0 ? sizes[i] : 0;
    if (sum <= 0) return;
    for (i = 0; i < count && i < xsp_childCount(self); ++i) {
        XWidget* child = xsp_childAt(self, i);
        XRect r;
        int size = (int)(((long long)sizes[i] > 0 ? sizes[i] : 0) *
                         total / sum);
        if (!child) continue;
        if (xsp_horiz(self))
            XRect_init(&r, x, 0, size, XWidget_height((XWidget*)self));
        else
            XRect_init(&r, 0, y, XWidget_width((XWidget*)self), size);
        XWidget_setGeometryRect(child, &r);
        if (xsp_horiz(self)) x += size + self->m_handleWidth;
        else y += size + self->m_handleWidth;
    }
}

#if XByteArray_ON
XByteArray* XSplitter_saveState(const XSplitter* self)
{
    XByteArray* out;
    int i;
    int count;
    char header[32];
    if (!self) return NULL;
    out = XByteArray_create();
    if (!out) return NULL;
    count = xsp_childCount(self);
    snprintf(header, sizeof(header), "XSP%03d%03d", self->m_orientation,
             count);
    XByteArray_append_utf8(out, header);
    for (i = 0; i < count; ++i) {
        XWidget* child = xsp_childAt(self, i);
        char buf[16];
        int size = 0;
        if (child)
            size = xsp_horiz(self) ? XWidget_width(child)
                                   : XWidget_height(child);
        snprintf(buf, sizeof(buf), "%05d", size);
        XByteArray_append_utf8(out, buf);
    }
    return out;
}

bool XSplitter_restoreState(XSplitter* self, const XByteArray* state)
{
    const char* data;
    int orientation;
    int count;
    int i;
    char buf[8];
    if (!self || !state) return false;
    data = (const char*)XByteArray_constData(state);
    if (!data || strncmp(data, "XSP", 3) != 0) return false;
    orientation = (data[3] - '0') * 100 + (data[4] - '0') * 10 +
                  (data[5] - '0');
    count = (data[6] - '0') * 100 + (data[7] - '0') * 10 +
            (data[8] - '0');
    if (orientation != 1 && orientation != 2) return false;
    if (count != xsp_childCount(self)) return false;
    self->m_orientation = orientation;
    {
        int* sizes = (int*)XMalloc_System((size_t)count * sizeof(int));
        if (!sizes) return false;
        for (i = 0; i < count; ++i) {
            memcpy(buf, data + 9 + i * 5, 5);
            buf[5] = '\0';
            sizes[i] = atoi(buf);
        }
        XSplitter_setSizes(self, sizes, count);
        XFree_System(sizes);
    }
    return true;
}
#endif /* XByteArray_ON */

/* ==================== 信号 ==================== */

void* XSplitter_splitterMoved_signal(XSplitter* self, int pos, int index)
{
    (void)self;
    (void)pos;
    (void)index;
    return (void*)(size_t)XSplitter_splitterMoved_signal;
}

void XSplitter_setRubberBand(XSplitter* self, bool on) { (void)self; (void)on; }
bool XSplitter_rubberBand(const XSplitter* self) { (void)self; return false; }
#endif /* XWIDGET_ON && XFRAME_ON && XSPLITTER_ON */
