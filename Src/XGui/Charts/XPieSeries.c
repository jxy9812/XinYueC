#include "XPieSeries.h"
#include "XMemory.h"
#include "XClass.h"
#include <string.h>

#if XCHARTS_ON

XPieSeries* XPieSeries_create_ex(XMemoryType memory)
{
    XPieSeries* self = (XPieSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    strcpy(self->m_name, "pie");
    self->m_holeSize = 0.0;
    self->m_visible = true;
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XPieSeries_delete_base(XPieSeries* self)
{
    if (!self) return;
    if (self->m_slices) XFree_System(self->m_slices);
    XFree_System(self);
}

int XPieSeries_append(XPieSeries* self, const char* label, double value)
{
    XPieSlice* p;
    int idx;
    if (!self || !label) return -1;
    if (self->m_count >= self->m_capacity) {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 8;
        p = (XPieSlice*)XRealloc_System(self->m_slices,
            sizeof(XPieSlice) * (size_t)cap);
        if (!p) return -1;
        self->m_slices = p;
        self->m_capacity = cap;
    }
    idx = self->m_count;
    memset(&self->m_slices[idx], 0, sizeof(XPieSlice));
    strncpy(self->m_slices[idx].m_label, label,
            sizeof(self->m_slices[idx].m_label) - 1);
    self->m_slices[idx].m_value = value;
    self->m_count++;
    return idx;
}

int XPieSeries_count(const XPieSeries* self) { return self ? self->m_count : 0; }

XPieSlice* XPieSeries_slice(const XPieSeries* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return NULL;
    return &self->m_slices[index];
}

double XPieSeries_sum(const XPieSeries* self)
{
    double sum = 0;
    int i;
    if (!self) return 0;
    for (i = 0; i < self->m_count; ++i) sum += self->m_slices[i].m_value;
    return sum;
}

void XPieSeries_setHoleSize(XPieSeries* self, double hole)
{
    if (!self) return;
    if (hole < 0) hole = 0;
    if (hole > 0.9) hole = 0.9;
    self->m_holeSize = hole;
}

void XPieSeries_setName(XPieSeries* self, const char* name)
{
    if (!self || !name) return;
    strncpy(self->m_name, name, sizeof(self->m_name) - 1);
    self->m_name[sizeof(self->m_name) - 1] = 0;
}

#endif /* XCHARTS_ON */