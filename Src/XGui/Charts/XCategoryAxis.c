#include "XCategoryAxis.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/** @brief 发射 countChanged 信号（走基类 XObject 信号槽）。 */
static void xcat_emitCount(XCategoryAxis* self, int count)
{
    XVarList* args = XVarList_Create(XVar(int, count));
    if (!args) return;
    if (self && ((XObject*)&self->m_base)->m_signalSlot) {
        XObject_emitSignal((XObject*)&self->m_base,
                           (size_t)XCategoryAxis_countChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

void XCategoryAxis_init(XCategoryAxis* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractAxis_init(&self->m_base);
    self->m_base.m_min = 0.0;
    self->m_base.m_max = 0.0;
}

int XCategoryAxis_append(XCategoryAxis* self, const XString* label)
{
    int idx;
    if (!self || !label) return -1;
    if (self->m_count >= self->m_capacity) {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 8;
        int oldCap = self->m_capacity;
        int ci;
        XString** cat = (XString**)XRealloc_System(self->m_categories,
            sizeof(XString*) * (size_t)cap);
        if (!cat) return -1;
        self->m_categories = cat;
        for (ci = oldCap; ci < cap; ++ci)
            self->m_categories[ci] = NULL;
        self->m_capacity = cap;
    }
    idx = self->m_count;
    self->m_categories[idx] = XString_create_copy(label);
    self->m_count++;
    self->m_base.m_max = (double)self->m_count;
    xcat_emitCount(self, self->m_count);
    return idx;
}

int XCategoryAxis_append_2(XCategoryAxis* self, const char* label)
{
    XString* tmp = NULL;
    int idx;
    if (!label) return -1;
    tmp = XString_create_utf8(label);
    if (!tmp) return -1;
    idx = XCategoryAxis_append(self, tmp);
    XString_delete_base(tmp);
    return idx;
}

XCategoryAxis* XCategoryAxis_create_ex(XMemoryType memory)
{
    XCategoryAxis* self =
        (XCategoryAxis*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XCategoryAxis_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

int XCategoryAxis_count(const XCategoryAxis* self)
{ return self ? self->m_count : 0; }

const XString* XCategoryAxis_category(const XCategoryAxis* self, int index)
{
    if (!self || index < 0 || index >= self->m_count ||
        !self->m_categories || !self->m_categories[index])
        return NULL;
    return self->m_categories[index];
}
const char* XCategoryAxis_category_2(const XCategoryAxis* self, int index)
{
    const XString* s;
    s = XCategoryAxis_category(self, index);
    return s ? XString_toUtf8(s) : "";
}

void XCategoryAxis_setVisible(XCategoryAxis* self, bool visible)
{ if (self) XAbstractAxis_setVisible(&self->m_base, visible); }

double XCategoryAxis_min(const XCategoryAxis* self)
{ return self ? self->m_base.m_min : 0.0; }

double XCategoryAxis_max(const XCategoryAxis* self)
{ return self ? self->m_base.m_max : 0.0; }

void XCategoryAxis_setRange(XCategoryAxis* self, double min, double max)
{
    double lo;
    double hi;
    if (!self) return;
    lo = (min < 0.0) ? 0.0 : min;
    hi = (max > (double)self->m_count) ? (double)self->m_count : max;
    if (hi < lo) hi = lo;
    XAbstractAxis_setRange(&self->m_base, lo, hi);
}

void* XCategoryAxis_countChanged_signal(XCategoryAxis* self, int count)
{
    xcat_emitCount(self, count);
    return (void*)(size_t)XCategoryAxis_countChanged_signal;
}

#endif /* XCHARTS_ON */
void XCategoryAxis_deinit_impl(XCategoryAxis* self)
{
    int i;
    if (!self) return;
    if (self->m_categories) {
        for (i = 0; i < self->m_count; ++i) {
            if (self->m_categories[i]) {
                XString_delete_base(self->m_categories[i]);
                self->m_categories[i] = NULL;
            }
        }
        XFree_System(self->m_categories);
        self->m_categories = NULL;
    }
    XAbstractAxis_deinit_base(&self->m_base);
}
