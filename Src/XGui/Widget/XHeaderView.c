/**
 * @file       XHeaderView.c
 * @brief      XHeaderView 表头视图实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XHeaderView.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

static void VXHeaderView_deinit(XHeaderView* self)
{
    if (!self) return;
    if (self->m_sections) {
        XVector_delete_base(self->m_sections);
        self->m_sections = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

static void VXHeaderView_copy(XHeaderView* self, const XHeaderView* other)
{
    int i;
    int n;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XHeaderView_init(self, NULL, 0, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    self->m_orientation = other->m_orientation;
    self->m_count = other->m_count;
    self->m_defaultSize = other->m_defaultSize;
    self->m_stretchLast = other->m_stretchLast;
    XVector_clear_base(self->m_sections);
    n = other->m_sections
            ? (int)XVector_size_base((const XContainer*)other->m_sections)
            : 0;
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(other->m_sections, (int64_t)i, int);
        XVector_push_back_1_base(self->m_sections, &v);
    }
}

static void VXHeaderView_move(XHeaderView* self, XHeaderView* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XHeaderView_init(self, NULL, 0, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    if (self->m_sections) XVector_delete_base(self->m_sections);
    self->m_orientation = other->m_orientation;
    self->m_count = other->m_count;
    self->m_defaultSize = other->m_defaultSize;
    self->m_stretchLast = other->m_stretchLast;
    self->m_sections = other->m_sections;
    other->m_sections = XVector_Create(int);
    other->m_count = 0;
}

XVtable* XHeaderView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHeaderView)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHeaderView_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHeaderView_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHeaderView_move);
    return XVTABLE_DEFAULT;
}

void XHeaderView_init(XHeaderView* self, XWidget* parent, XWidgetFlags flags,
                      int orientation)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XHeaderView);
    self->m_orientation = (orientation == 1) ? 1 : 0;
    self->m_count = 0;
    self->m_defaultSize = 30;
    self->m_stretchLast = false;
    self->m_sectionMovedFrom = -1;
    self->m_sections = XVector_Create(int);
}

XHeaderView* XHeaderView_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags, int orientation)
{
    XHeaderView* self = (XHeaderView*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XHeaderView_init(self, parent, flags, orientation);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

int XHeaderView_orientation(const XHeaderView* self)
{ return self ? self->m_orientation : 0; }

int XHeaderView_count(const XHeaderView* self)
{ return self ? self->m_count : 0; }

void XHeaderView_setCount(XHeaderView* self, int count)
{
    int i;
    int n;
    if (!self || !self->m_sections || count < 0) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (count > n) {
        for (i = n; i < count; ++i) {
            int v = self->m_defaultSize;
            XVector_push_back_1_base(self->m_sections, &v);
        }
    } else if (count < n) {
        XVector_remove_base(self->m_sections, (int64_t)count,
                            (int64_t)(n - count));
    }
    self->m_count = count;
}

int XHeaderView_sectionSize(const XHeaderView* self, int section)
{
    int n;
    if (!self || !self->m_sections || section < 0) return 0;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (section >= n) return self->m_defaultSize;
    return XVector_At_Base(self->m_sections, (int64_t)section, int);
}

void XHeaderView_setSectionSize(XHeaderView* self, int section, int size)
{
    int n;
    if (!self || !self->m_sections || section < 0 || size <= 0) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (section >= n) return;
    XVector_At_Base(self->m_sections, (int64_t)section, int) = size;
}

int XHeaderView_defaultSectionSize(const XHeaderView* self)
{ return self ? self->m_defaultSize : 0; }

void XHeaderView_setDefaultSectionSize(XHeaderView* self, int size)
{
    if (self && size > 0) self->m_defaultSize = size;
}

int XHeaderView_sectionPosition(const XHeaderView* self, int section)
{
    int pos;
    int i;
    if (!self || !self->m_sections || section < 0) return -1;
    pos = 0;
    for (i = 0; i < section; ++i)
        pos += XHeaderView_sectionSize(self, i);
    return pos;
}

void XHeaderView_setStretchLastSection(XHeaderView* self, bool stretch)
{
    if (self) self->m_stretchLast = stretch;
}

bool XHeaderView_isStretchLastSection(const XHeaderView* self)
{ return self ? self->m_stretchLast : false; }

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
