/**
 * @file       XTreeWidget.c
 * @brief      XTreeWidget 树控件实现（树条目 + 递归渲染）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XTreeWidget.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

#define XTW_HEADER_H 20
#define XTW_ROW_H 24

static void VXTreeWidget_deinit(XTreeWidget* self);
static void VXTreeWidget_paintEvent(XWidget* self, XEvent* event);
static void VXTreeWidget_mousePressEvent(XWidget* self, XEvent* event);

static void xtwitem_freeSubtree(XTreeWidgetItem* item);

XTreeWidgetItem* XTreeWidgetItem_create(const XString* text,
                                        XTreeWidgetItem* parent)
{
    XTreeWidgetItem* item =
        (XTreeWidgetItem*)XMalloc_System(sizeof(XTreeWidgetItem));
    if (!item) return NULL;
    XMemset(item, 0, sizeof(*item));
    item->text = text ? XString_create_copy(text) : XString_create();
    item->parent = parent;
    return item;
}

XTreeWidgetItem* XTreeWidgetItem_create_2(const char* text,
                                          XTreeWidgetItem* parent)
{
    XString* tmp = NULL;
    XTreeWidgetItem* item;
    if (text) {
        tmp = XString_create_utf8(text);
        if (!tmp) return NULL;
    }
    item = XTreeWidgetItem_create(tmp, parent);
    if (tmp) XString_delete_base(tmp);
    return item;
}

void XTreeWidgetItem_delete(XTreeWidgetItem* item)
{
    if (!item) return;
    xtwitem_freeSubtree(item);
    XFree_System(item);
}

static void xtwitem_freeSubtree(XTreeWidgetItem* item)
{
    int i;
    if (!item) return;
    for (i = 0; i < item->childCount; ++i) {
        if (item->children[i]) {
            xtwitem_freeSubtree(item->children[i]);
            XFree_System(item->children[i]);
            item->children[i] = NULL;
        }
    }
    if (item->children) XFree_System(item->children);
    if (item->text) XString_delete_base(item->text);
    item->children = NULL;
    item->childCount = 0;
    item->childCapacity = 0;
    item->text = NULL;
}

const XString* XTreeWidgetItem_text(const XTreeWidgetItem* item)
{ return (item && item->text) ? item->text : NULL; }

const char* XTreeWidgetItem_text_2(const XTreeWidgetItem* item)
{
    const XString* s;
    s = XTreeWidgetItem_text(item);
    return s ? XString_toUtf8(s) : "";
}

void XTreeWidgetItem_setText(XTreeWidgetItem* item, const XString* text)
{
    if (!item) return;
    if (!item->text) item->text = XString_create();
    if (!item->text) return;
    if (text)
        XString_assign(item->text, text);
    else
        XString_assign_utf8(item->text, "");
}

void XTreeWidgetItem_setText_2(XTreeWidgetItem* item, const char* text)
{
    XString* tmp = NULL;
    if (text) {
        tmp = XString_create_utf8(text);
        if (!tmp) return;
    }
    XTreeWidgetItem_setText(item, tmp);
    if (tmp) XString_delete_base(tmp);
}

bool XTreeWidgetItem_addChild(XTreeWidgetItem* item,
                              XTreeWidgetItem* child)
{
    XTreeWidgetItem** grown;
    if (!item || !child) return false;
    if (item->childCount >= item->childCapacity) {
        int cap = item->childCapacity > 0 ? item->childCapacity * 2 : 4;
        grown = (XTreeWidgetItem**)XRealloc_System(
            item->children, sizeof(XTreeWidgetItem*) * (size_t)cap);
        if (!grown) return false;
        item->children = grown;
        item->childCapacity = cap;
    }
    item->children[item->childCount++] = child;
    child->parent = item;
    return true;
}

int XTreeWidgetItem_childCount(const XTreeWidgetItem* item)
{ return item ? item->childCount : 0; }

XTreeWidgetItem* XTreeWidgetItem_child(const XTreeWidgetItem* item,
                                       int index)
{
    if (!item || index < 0 || index >= item->childCount) return NULL;
    return item->children[index];
}

/* ==================== XTreeWidget ==================== */

static void xtw_ensureTop(XTreeWidget* self, int need)
{
    XTreeWidgetItem** grown;
    int cap = self->m_topCapacity > 0 ? self->m_topCapacity : 4;
    if (need <= self->m_topCapacity) return;
    while (cap < need) cap *= 2;
    grown = (XTreeWidgetItem**)XRealloc_System(
        self->m_topItems, sizeof(XTreeWidgetItem*) * (size_t)cap);
    if (!grown) return;
    self->m_topItems = grown;
    self->m_topCapacity = cap;
}

XVtable* XTreeWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTreeWidget)
    XVTABLE_INHERIT_XCLASS(XTreeView);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTreeWidget_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXTreeWidget_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXTreeWidget_mousePressEvent);
    return XVTABLE_DEFAULT;
}

void XTreeWidget_init(XTreeWidget* self, XWidget* parent,
                      XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XTreeView_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XTreeWidget);
}

XTreeWidget* XTreeWidget_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XTreeWidget* self =
        (XTreeWidget*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTreeWidget_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXTreeWidget_deinit(XTreeWidget* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_topCount; ++i) {
        if (self->m_topItems[i]) XTreeWidgetItem_delete(self->m_topItems[i]);
    }
    if (self->m_topItems) XFree_System(self->m_topItems);
    self->m_topItems = NULL;
    self->m_topCount = 0;
    self->m_topCapacity = 0;
    XClass_Deinit_Parent(XTreeView, (XTreeView*)self);
}

bool XTreeWidget_addTopLevelItem(XTreeWidget* self, XTreeWidgetItem* item)
{
    if (!self || !item) return false;
    xtw_ensureTop(self, self->m_topCount + 1);
    self->m_topItems[self->m_topCount++] = item;
    item->parent = NULL;
    XWidget_update((XWidget*)self);
    return true;
}

bool XTreeWidget_insertTopLevelItem(XTreeWidget* self, int index,
                                    XTreeWidgetItem* item)
{
    int i;
    if (!self || !item || index < 0 || index > self->m_topCount)
        return false;
    xtw_ensureTop(self, self->m_topCount + 1);
    for (i = self->m_topCount; i > index; --i)
        self->m_topItems[i] = self->m_topItems[i - 1];
    self->m_topItems[index] = item;
    item->parent = NULL;
    self->m_topCount++;
    XWidget_update((XWidget*)self);
    return true;
}

XTreeWidgetItem* XTreeWidget_topLevelItem(const XTreeWidget* self, int index)
{
    if (!self || index < 0 || index >= self->m_topCount) return NULL;
    return self->m_topItems[index];
}

int XTreeWidget_topLevelItemCount(const XTreeWidget* self)
{ return self ? self->m_topCount : 0; }

XTreeWidgetItem* XTreeWidget_takeTopLevelItem(XTreeWidget* self, int index)
{
    XTreeWidgetItem* item;
    int i;
    if (!self || index < 0 || index >= self->m_topCount) return NULL;
    item = self->m_topItems[index];
    for (i = index; i < self->m_topCount - 1; ++i)
        self->m_topItems[i] = self->m_topItems[i + 1];
    self->m_topCount--;
    item->parent = NULL;
    XWidget_update((XWidget*)self);
    return item;
}

void XTreeWidget_clear(XTreeWidget* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_topCount; ++i) {
        if (self->m_topItems[i]) XTreeWidgetItem_delete(self->m_topItems[i]);
    }
    self->m_topCount = 0;
    XWidget_update((XWidget*)self);
}

/* ==================== 渲染 ==================== */

static void xtw_drawItem(XTreeWidget* self, XTreeWidgetItem* item,
                         XPainter* painter, int depth, int* y, int maxY)
{
    XTreeView* tv = &self->m_base;
    int rh = tv->m_rowHeight > 0 ? tv->m_rowHeight : XTW_ROW_H;
    int indent = (tv->m_indentation > 0 ? tv->m_indentation : 20);
    XRect cell;
    const char* text;
    int y0 = *y;
    if (y0 >= maxY) return;
    cell.x = 0;
    cell.y = y0;
    cell.width = XWidget_width((XWidget*)self);
    cell.height = rh;
    if (item->parent == NULL) {
        XPainter_fillRect(painter, &cell, 0xFFFFFFFFu);
    }
    text = XTreeWidgetItem_text_2(item);
    if (text && text[0]) {
        XPainter_setPen(painter, 0xFF000000u);
        XPainter_drawText(painter, indent * depth + 12, y0 + rh - 6,
                          text, 0);
    }
    /* 子节点指示。 */
    if (item->childCount > 0) {
        int bx = indent * depth + 4;
        int by = y0 + rh / 2;
        XPainter_setPen(painter, 0xFF888888u);
        XPainter_drawLine(painter, bx, by - 3, bx, by + 3);
        XPainter_drawLine(painter, bx - 2, by, bx + 2, by);
    }
    XPainter_setPen(painter, 0xFFDDDDDDu);
    XPainter_drawLine(painter, 0, y0 + rh - 1,
                      XWidget_width((XWidget*)self), y0 + rh - 1);
    *y += rh;
    {
        int i;
        for (i = 0; i < item->childCount; ++i) {
            if (item->children[i])
                xtw_drawItem(self, item->children[i], painter, depth + 1,
                             y, maxY);
        }
    }
}

static void VXTreeWidget_paintEvent(XWidget* self, XEvent* event)
{
    XTreeWidget* tw = (XTreeWidget*)self;
    XImage* image;
    XPainter painter;
    XRect r;
    int y;
    int i;
    int h;
    (void)event;
    if (!tw) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    h = XWidget_height(self);
    XRect_init(&r, 0, 0, XWidget_width(self), h);
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    XPainter_fillRect(&painter, &r, 0xFFFFFFFFu);
    y = 0;
    for (i = 0; i < tw->m_topCount; ++i) {
        if (tw->m_topItems[i])
            xtw_drawItem(tw, tw->m_topItems[i], &painter, 0, &y, h);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

static void VXTreeWidget_mousePressEvent(XWidget* self, XEvent* event)
{
    XTreeWidget* tw = (XTreeWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    if (!tw || !event) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    /* 简化命中：按行号选中（当前树为展开渲染，行序 = 前序编号）。 */
    row = pos.y / (tw->m_base.m_rowHeight > 0 ? tw->m_base.m_rowHeight
                                              : XTW_ROW_H);
    if (row >= 0) {
        XAbstractItemView_setCurrentIndex(&tw->m_base.m_base, row, 0);
    }
    XEvent_accept(event);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
