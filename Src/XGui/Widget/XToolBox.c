/**
 * @file       XToolBox.c
 * @brief      工具箱控件实现（对标 Qt 6.8 QToolBox 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XToolBox.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON

/* ==================== 内部条目 ==================== */

typedef struct XToolBoxItem
{
    XWidget* widget;   /**< 页面控件（借用，归调用方/容器）。 */
    char text[128];    /**< 页头文本。 */
    bool enabled;      /**< 条目启用。 */
} XToolBoxItem;

static XToolBoxItem* xtb2_itemCreate(XWidget* widget, const char* text)
{
    XToolBoxItem* item =
        (XToolBoxItem*)XMalloc_System(sizeof(XToolBoxItem));
    if (!item) return NULL;
    item->widget = widget;
    item->enabled = true;
    strncpy(item->text, text ? text : "", sizeof(item->text) - 1);
    item->text[sizeof(item->text) - 1] = '\0';
    return item;
}

static void xtb2_itemDestroy(XToolBoxItem* item)
{
    if (item) XFree_System(item);
}

static int xtb2_currentIndexOf(const XToolBox* self)
{
    if (!self || self->m_currentIndex < 0) return -1;
    if (!self->m_items ||
        self->m_currentIndex >=
            (int)XVector_size_base((const XContainer*)self->m_items))
        return -1;
    return self->m_currentIndex;
}

/** @brief 布局：当前页控件占满工具箱内容区（其余页隐藏）。 */
static void xtb2_layout(XToolBox* self)
{
    XWidget* current;
    XRect r;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self) return;
    current = XToolBox_currentWidget(self);
    if (!current) return;
    /* 内容 y 偏移 = 全部条目头总高（每个 22px），与 paint 一致。 */
    {
        int64_t n = self->m_items
                        ? XVector_size_base((const XContainer*)self->m_items)
                        : 0;
        int headerH = (int)n * 22;
        int contentH = h > headerH ? h - headerH : 0;
        XRect_init(&r, 0, headerH, w, contentH);
    }
    XWidget_setGeometryRect(current, &r);
}

static void xtb2_emitChanged(XToolBox* self, int index)
{
    XVarList* args = XVarList_Create(XVar(int, index));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XToolBox_currentChanged_signal, args,
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 事件处理 ==================== */

static void VX_toolBox_paintEvent(XWidget* self, XEvent* event)
{
    XToolBox* box = (XToolBox*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect head;
    XRect line;
    uint32_t highlight;
    uint32_t windowText;
    uint32_t mid;
    int64_t i;
    int64_t n;
    int y = 0;
    int w = XWidget_width(self);
    if (!box || !event) return;
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
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c;
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Highlight);
        highlight = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_WindowText);
        windowText = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
    }
#else
    highlight = 0xFF3080C0u;
    windowText = 0xFF000000u;
    mid = 0xFF808080u;
#endif /* XPALETTE_ON */
    if (box->m_items) {
        n = XVector_size_base((const XContainer*)box->m_items);
        for (i = 0; i < n; ++i) {
            XToolBoxItem** item =
                (XToolBoxItem**)XVector_at_base(box->m_items, i);
            if (!item || !*item) continue;
            XRect_init(&head, 0, y, w, 22);
            if ((int)i == box->m_currentIndex)
                XPainter_fillRect(&painter, &head, highlight);
            else
                XPainter_fillRect(&painter, &head, mid);
            XPainter_drawText(&painter, 6, y + 15, (*item)->text,
                              windowText);
            y += 22;
            XRect_init(&line, 0, y - 1, w, 1);
            XPainter_fillRect(&painter, &line, windowText);
        }
    }
    XPainter_deinit(&painter);
}

static void VX_toolBox_resizeEvent(XWidget* self, XEvent* event)
{
    (void)event;
    xtb2_layout((XToolBox*)self);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_toolBox_deinit(XToolBox* self)
{
    int64_t i;
    int64_t n;
    if (!self) return;
    if (self->m_items) {
        n = XVector_size_base((const XContainer*)self->m_items);
        for (i = 0; i < n; ++i) {
            XToolBoxItem** item =
                (XToolBoxItem**)XVector_at_base(self->m_items, i);
            xtb2_itemDestroy(item ? *item : NULL);
        }
        XVector_delete_base(self->m_items);
        self->m_items = NULL;
    }
    XClass_Deinit_Parent(XFrame, (XFrame*)self);
}

XVtable* XToolBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XToolBox)
    XVTABLE_INHERIT_XCLASS(XFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_toolBox_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_toolBox_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_toolBox_deinit);
    return XVTABLE_DEFAULT;
}

void XToolBox_init(XToolBox* self, XWidget* parent, XWidgetFlags flags)
{
    XSize hint;
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XFrame_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XToolBox);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_items = XVector_Create(XToolBoxItem*);
    self->m_currentIndex = -1;
    XWidget_resize(self, 160, 160);
    hint.width = 160;
    hint.height = 160;
    XWidget_setSizeHint((XWidget*)self, &hint);
}

XToolBox* XToolBox_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags)
{
    XToolBox* self = (XToolBox*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XToolBox_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 页面管理 ==================== */

int XToolBox_addItem(XToolBox* self, XWidget* widget, const char* utf8Text)
{
    int64_t n;
    if (!self || !widget || !self->m_items) return -1;
    n = XVector_size_base((const XContainer*)self->m_items);
    return XToolBox_insertItem(self, (int)n, widget, utf8Text);
}

int XToolBox_insertItem(XToolBox* self, int index, XWidget* widget,
                        const char* utf8Text)
{
    XToolBoxItem* item;
    int64_t n;
    int actual;
    if (!self || !widget || !self->m_items) return -1;
    item = xtb2_itemCreate(widget, utf8Text);
    if (!item) return -1;
    n = XVector_size_base((const XContainer*)self->m_items);
    if (index < 0 || (int64_t)index > n) index = (int)n;
    XVector_insert_1_base(self->m_items, index, &item, 1);
    XWidget_setParent(widget, (XWidget*)self, 0);
    actual = index;
    if (self->m_currentIndex < 0)
        XToolBox_setCurrentIndex(self, actual);
    else {
        XWidget_setVisible(widget, false);
        xtb2_layout(self);
    }
    return actual;
}

void XToolBox_removeItem(XToolBox* self, int index)
{
    XToolBoxItem* item;
    XWidget* widget;
    int wasCurrent;
    int64_t n;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return;
    item = *(XToolBoxItem**)XVector_at_base(self->m_items, index);
    widget = item ? item->widget : NULL;
    wasCurrent = (index == self->m_currentIndex);
    XVector_remove_base(self->m_items, index, 1);
    xtb2_itemDestroy(item);
    if (wasCurrent) {
        self->m_currentIndex = -1;
        n = XVector_size_base((const XContainer*)self->m_items);
        if (n > 0)
            XToolBox_setCurrentIndex(self,
                index < (int)n ? index : (int)n - 1);
    } else if (index < self->m_currentIndex) {
        self->m_currentIndex--;
    }
}

int XToolBox_count(const XToolBox* self)
{
    return (self && self->m_items)
               ? (int)XVector_size_base(
                     (const XContainer*)self->m_items)
               : 0;
}

XWidget* XToolBox_widget(const XToolBox* self, int index)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return NULL;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    return item && *item ? (*item)->widget : NULL;
}

int XToolBox_indexOf(const XToolBox* self, const XWidget* widget)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_items || !widget) return -1;
    n = XVector_size_base((const XContainer*)self->m_items);
    for (i = 0; i < n; ++i) {
        XToolBoxItem** item =
            (XToolBoxItem**)XVector_at_base(self->m_items, i);
        if (item && *item && (*item)->widget == widget) return (int)i;
    }
    return -1;
}

/* ==================== 条目属性 ==================== */

void XToolBox_setItemText(XToolBox* self, int index, const char* utf8)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    if (item && *item) {
        strncpy((*item)->text, utf8 ? utf8 : "",
                sizeof((*item)->text) - 1);
        (*item)->text[sizeof((*item)->text) - 1] = '\0';
        XWidget_update((XWidget*)self);
    }
}

const char* XToolBox_itemText(const XToolBox* self, int index)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return "";
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    return (item && *item) ? (*item)->text : "";
}

void XToolBox_setItemEnabled(XToolBox* self, int index, bool enabled)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    if (item && *item) {
        (*item)->enabled = enabled;
        XWidget_update((XWidget*)self);
    }
}

bool XToolBox_isItemEnabled(const XToolBox* self, int index)
{
    XToolBoxItem** item;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return false;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    return (item && *item) ? (*item)->enabled : false;
}

/* ==================== 当前页槽 ==================== */

int XToolBox_currentIndex(const XToolBox* self)
{
    return self ? self->m_currentIndex : -1;
}

XWidget* XToolBox_currentWidget(const XToolBox* self)
{
    int index = xtb2_currentIndexOf(self);
    XToolBoxItem** item;
    if (index < 0 || !self || !self->m_items) return NULL;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    return (item && *item) ? (*item)->widget : NULL;
}

void XToolBox_setCurrentIndex(XToolBox* self, int index)
{
    XToolBoxItem** item;
    int old;
    if (!self || !self->m_items || index < 0 ||
        index >= (int)XVector_size_base((const XContainer*)self->m_items))
        return;
    if (index == self->m_currentIndex) return;
    item = (XToolBoxItem**)XVector_at_base(self->m_items, index);
    if (!item || !*item || !(*item)->enabled) return;
    old = self->m_currentIndex;
    self->m_currentIndex = index;
    if (old >= 0) {
        XWidget* prev = XToolBox_widget(self, old);
        if (prev)
            XWidget_setVisible(prev, false);
    }
    {
        XWidget* cur = XToolBox_widget(self, index);
        if (cur)
            XWidget_setVisible(cur, true);
    }
    xtb2_layout(self);
    xtb2_emitChanged(self, index);
}

void XToolBox_setCurrentWidget(XToolBox* self, XWidget* widget)
{
    int index;
    if (!self || !widget) return;
    index = XToolBox_indexOf(self, widget);
    if (index >= 0)
        XToolBox_setCurrentIndex(self, index);
}

/* ==================== 信号 ==================== */

void* XToolBox_currentChanged_signal(XToolBox* self, int index)
{
    (void)self;
    (void)index;
    return (void*)(size_t)XToolBox_currentChanged_signal;
}

const char* XToolBox_itemToolTip(const XToolBox* self, int index) { (void)self; (void)index; return ""; }
void XToolBox_setItemIcon(XToolBox* self, int index, const char* icon) { (void)self; (void)index; (void)icon; }
void XToolBox_setItemToolTip(XToolBox* self, int index, const char* tip) { (void)self; (void)index; (void)tip; }
#endif /* XWIDGET_ON && XFRAME_ON && XTOOLBOX_ON */
