#include "XAbstractItemView.h"
#include "XMemory.h"
#include "XClass.h"
#include <string.h>

#if XTABLEWIDGET_ON || 1

static void VXAbstractItemView_deinit(XAbstractItemView* self);

XVtable* XAbstractItemView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractItemView)
    XVTABLE_INHERIT_XCLASS(XAbstractScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractItemView_deinit);
    return XVTABLE_DEFAULT;
}

void XAbstractItemView_init(XAbstractItemView* self, XWidget* parent,
                            XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XAbstractScrollArea_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XAbstractItemView);
    self->m_currentRow = -1;
    self->m_currentColumn = -1;
    self->m_selectionMode = XAbstractItemViewSelectionMode_ExtendedSelection;
    self->m_selectionBehavior =
        XAbstractItemViewSelectionBehavior_SelectItems;
    self->m_editTriggers = XAbstractItemViewEditTrigger_CurrentChanged |
        XAbstractItemViewEditTrigger_DoubleClicked |
        XAbstractItemViewEditTrigger_EditKeyPressed;
    self->m_alternatingRowColors = false;
    self->m_autoScroll = true;
}

XAbstractItemView* XAbstractItemView_create_ex(XMemoryType memory,
                                               XWidget* parent,
                                               XWidgetFlags flags)
{
    XAbstractItemView* self =
        (XAbstractItemView*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XAbstractItemView_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXAbstractItemView_deinit(XAbstractItemView* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XAbstractScrollArea, (XAbstractScrollArea*)self);
}

void XAbstractItemView_setCurrentIndex(XAbstractItemView* self, int row,
                                       int column)
{
    if (!self) return;
    self->m_currentRow = row;
    self->m_currentColumn = column;
}

int XAbstractItemView_currentRow(const XAbstractItemView* self)
{ return self ? self->m_currentRow : -1; }
int XAbstractItemView_currentColumn(const XAbstractItemView* self)
{ return self ? self->m_currentColumn : -1; }

void XAbstractItemView_setSelectionMode(XAbstractItemView* self, int mode)
{ if (self) self->m_selectionMode = mode; }
int XAbstractItemView_selectionMode(const XAbstractItemView* self)
{ return self ? self->m_selectionMode : 0; }

void XAbstractItemView_setSelectionBehavior(XAbstractItemView* self,
                                            int behavior)
{ if (self) self->m_selectionBehavior = behavior; }
int XAbstractItemView_selectionBehavior(const XAbstractItemView* self)
{ return self ? self->m_selectionBehavior : 0; }

void XAbstractItemView_setEditTriggers(XAbstractItemView* self, int triggers)
{ if (self) self->m_editTriggers = triggers; }
int XAbstractItemView_editTriggers(const XAbstractItemView* self)
{ return self ? self->m_editTriggers : 0; }

void XAbstractItemView_setAlternatingRowColors(XAbstractItemView* self,
                                               bool enable)
{ if (self) self->m_alternatingRowColors = enable; }
bool XAbstractItemView_alternatingRowColors(const XAbstractItemView* self)
{ return self ? self->m_alternatingRowColors : false; }

void XAbstractItemView_setAutoScroll(XAbstractItemView* self, bool enable)
{ if (self) self->m_autoScroll = enable; }
bool XAbstractItemView_hasAutoScroll(const XAbstractItemView* self)
{ return self ? self->m_autoScroll : true; }

void XAbstractItemView_scrollTo(XAbstractItemView* self, int row, int column)
{
    /* 默认无滚动区域实现；派生视图可提升具体滚动。 */
    if (!self) return;
    (void)row;
    (void)column;
}

#endif /* XTABLEWIDGET_ON || 1 */
