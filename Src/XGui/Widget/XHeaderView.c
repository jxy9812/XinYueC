/**
 * @file       XHeaderView.c
 * @brief      XHeaderView 表头视图实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XHeaderView.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEvent.h"

#if XWIDGET_ON && XTABLEWIDGET_ON

/** @brief 发射 int 信号（载荷单值）。 */
static void xhv_emitInt(XHeaderView* self, size_t signal, int value)
{
    XVarList* arguments = XVarList_Create(XVar(int, value));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

static void VXHeaderView_deinit(XHeaderView* self)
{
    if (!self) return;
    if (self->m_sections) {
        XVector_delete_base(self->m_sections);
        self->m_sections = NULL;
    }
    if (self->m_hidden) {
        XFree_System(self->m_hidden);
        self->m_hidden = NULL;
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
    if (self->m_hidden) {
        XFree_System(self->m_hidden);
        self->m_hidden = NULL;
    }
    if (n > 0) {
        self->m_hidden = (bool*)XMalloc_System(sizeof(bool) * (size_t)n);
        if (self->m_hidden)
            XMemmove(self->m_hidden, other->m_hidden,
                     sizeof(bool) * (size_t)n);
    }
    self->m_hiddenCount = other->m_hiddenCount;
    self->m_sectionsClickable = other->m_sectionsClickable;
    self->m_sectionsMovable = other->m_sectionsMovable;
    self->m_sortIndicatorShown = other->m_sortIndicatorShown;
    self->m_sortIndicatorSection = other->m_sortIndicatorSection;
    self->m_sortIndicatorOrder = other->m_sortIndicatorOrder;
}

static void VXHeaderView_move(XHeaderView* self, XHeaderView* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XHeaderView_init(self, NULL, 0, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    if (self->m_sections) XVector_delete_base(self->m_sections);
    if (self->m_hidden) XFree_System(self->m_hidden);
    self->m_orientation = other->m_orientation;
    self->m_count = other->m_count;
    self->m_defaultSize = other->m_defaultSize;
    self->m_stretchLast = other->m_stretchLast;
    self->m_sections = other->m_sections;
    other->m_sections = XVector_Create(int);
    self->m_hidden = other->m_hidden;
    other->m_hidden = NULL;
    self->m_hiddenCount = other->m_hiddenCount;
    other->m_hiddenCount = 0;
    self->m_sectionsClickable = other->m_sectionsClickable;
    self->m_sectionsMovable = other->m_sectionsMovable;
    self->m_sortIndicatorShown = other->m_sortIndicatorShown;
    self->m_sortIndicatorSection = other->m_sortIndicatorSection;
    self->m_sortIndicatorOrder = other->m_sortIndicatorOrder;
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
    self->m_hidden = NULL;
    self->m_hiddenCount = 0;
    self->m_sectionsClickable = false;
    self->m_sectionsMovable = false;
    self->m_sortIndicatorShown = false;
    self->m_sortIndicatorSection = -1;
    self->m_sortIndicatorOrder = 0;
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
            bool h = false;
            XVector_push_back_1_base(self->m_sections, &v);
            self->m_hidden = (bool*)XRealloc_System(
                self->m_hidden, sizeof(bool) * (size_t)(i + 1));
            if (self->m_hidden) self->m_hidden[i] = h;
        }
    } else if (count < n) {
        for (i = count; i < n; ++i) {
            if (self->m_hidden && self->m_hidden[i]) --self->m_hiddenCount;
        }
        XVector_remove_base(self->m_sections, (int64_t)count,
                            (int64_t)(n - count));
    }
    self->m_count = count;
}

/** @brief 查询区间是否隐藏（对标 isSectionHidden）。 */
bool XHeaderView_isSectionHidden(const XHeaderView* self, int section)
{
    int n;
    if (!self || !self->m_hidden || section < 0) return false;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (section >= n) return false;
    return self->m_hidden[section];
}

void XHeaderView_hideSection(XHeaderView* self, int section)
{
    int n;
    if (!self || !self->m_hidden || section < 0) return;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (section >= n) return;
    if (!self->m_hidden[section]) {
        self->m_hidden[section] = true;
        ++self->m_hiddenCount;
    }
}

void XHeaderView_showSection(XHeaderView* self, int section)
{
    int n;
    if (!self || !self->m_hidden || section < 0) return;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (section >= n) return;
    if (self->m_hidden[section]) {
        self->m_hidden[section] = false;
        --self->m_hiddenCount;
    }
}

int XHeaderView_hiddenSectionCount(const XHeaderView* self)
{ return self ? self->m_hiddenCount : 0; }

void XHeaderView_setSectionsClickable(XHeaderView* self, bool clickable)
{ if (self) self->m_sectionsClickable = clickable; }

bool XHeaderView_sectionsClickable(const XHeaderView* self)
{ return self ? self->m_sectionsClickable : false; }

void XHeaderView_setSectionsMovable(XHeaderView* self, bool movable)
{ if (self) self->m_sectionsMovable = movable; }

bool XHeaderView_sectionsMovable(const XHeaderView* self)
{ return self ? self->m_sectionsMovable : false; }

void XHeaderView_swapSections(XHeaderView* self, int first, int second)
{
    int n;
    int tmp;
    bool htmp;
    if (!self || !self->m_sections || first < 0 || second < 0) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (first >= n || second >= n || first == second) return;
    tmp = XVector_At_Base(self->m_sections, (int64_t)first, int);
    XVector_At_Base(self->m_sections, (int64_t)first, int) =
        XVector_At_Base(self->m_sections, (int64_t)second, int);
    XVector_At_Base(self->m_sections, (int64_t)second, int) = tmp;
    if (self->m_hidden) {
        htmp = self->m_hidden[first];
        self->m_hidden[first] = self->m_hidden[second];
        self->m_hidden[second] = htmp;
    }
    self->m_sectionMovedFrom = first;
}

void XHeaderView_moveSection(XHeaderView* self, int from, int to)
{
    int i;
    int n;
    int size;
    bool hidden;
    if (!self || !self->m_sections || from < 0 || to < 0 || from == to)
        return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (from >= n || to >= n) return;
    size = XVector_At_Base(self->m_sections, (int64_t)from, int);
    hidden = self->m_hidden ? self->m_hidden[from] : false;
    if (from < to) {
        for (i = from; i < to; ++i) {
            XVector_At_Base(self->m_sections, (int64_t)i, int) =
                XVector_At_Base(self->m_sections, (int64_t)(i + 1), int);
            if (self->m_hidden)
                self->m_hidden[i] = self->m_hidden[i + 1];
        }
    } else {
        for (i = from; i > to; --i) {
            XVector_At_Base(self->m_sections, (int64_t)i, int) =
                XVector_At_Base(self->m_sections, (int64_t)(i - 1), int);
            if (self->m_hidden)
                self->m_hidden[i] = self->m_hidden[i - 1];
        }
    }
    XVector_At_Base(self->m_sections, (int64_t)to, int) = size;
    if (self->m_hidden) self->m_hidden[to] = hidden;
    self->m_sectionMovedFrom = from;
}

void XHeaderView_setSortIndicator(XHeaderView* self, int section, int order)
{
    if (!self) return;
    if (section < -1) section = -1;
    if (self->m_sortIndicatorSection == section &&
        self->m_sortIndicatorOrder == order)
        return;
    self->m_sortIndicatorSection = section;
    self->m_sortIndicatorOrder = order;
    self->m_sortIndicatorShown = true;
    xhv_emitInt(self, (size_t)XHeaderView_sortIndicatorChanged_signal(
                          self, section, order), section);
}

int XHeaderView_sortIndicatorSection(const XHeaderView* self)
{ return self ? self->m_sortIndicatorSection : -1; }

int XHeaderView_sortIndicatorOrder(const XHeaderView* self)
{ return self ? self->m_sortIndicatorOrder : 0; }

void XHeaderView_setSortIndicatorShown(XHeaderView* self, bool shown)
{ if (self) self->m_sortIndicatorShown = shown; }

bool XHeaderView_isSortIndicatorShown(const XHeaderView* self)
{ return self ? self->m_sortIndicatorShown : false; }

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

/* ==================== 视觉序与位置反查（对标 QHeaderView visualIndex 族） ==================== */

int XHeaderView_visualIndex(const XHeaderView* self, int logicalIndex)
{
    int n;
    if (!self || logicalIndex < 0) return -1;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (logicalIndex >= n) return -1;
    /* 移动即重排：逻辑序与视觉序恒一致（见头文件 @note）。 */
    return logicalIndex;
}

int XHeaderView_visualIndexAt(const XHeaderView* self, int visualIndex)
{
    int n;
    if (!self || visualIndex < 0) return -1;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (visualIndex >= n) return -1;
    return visualIndex;
}

int XHeaderView_logicalIndexAt(const XHeaderView* self, int position)
{
    int n;
    int i;
    if (!self || position < 0) return -1;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    for (i = 0; i < n; ++i) {
        int size = XVector_At_Base(self->m_sections, (int64_t)i, int);
        if (position < size) return i;
        position -= size;
    }
    return -1;
}

int XHeaderView_sectionSizeHint(const XHeaderView* self, int logicalIndex)
{
    (void)logicalIndex;
    return self ? self->m_defaultSize : 0;
}

int XHeaderView_length(const XHeaderView* self)
{
    int n;
    int i;
    int total = 0;
    if (!self || !self->m_sections) return 0;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    for (i = 0; i < n; ++i) {
        if (self->m_hidden && self->m_hidden[i]) continue;
        total += XVector_At_Base(self->m_sections, (int64_t)i, int);
    }
    return total;
}

int XHeaderView_offset(const XHeaderView* self)
{
    (void)self;
    return 0;
}

void* XHeaderView_sectionClicked_signal(XHeaderView* self, int section)
{
    (void)section;
    return (void*)(size_t)XHeaderView_sectionClicked_signal;
}

void* XHeaderView_sortIndicatorChanged_signal(XHeaderView* self,
                                              int logicalIndex, int order)
{
    (void)logicalIndex; (void)order;
    return (void*)(size_t)XHeaderView_sortIndicatorChanged_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
