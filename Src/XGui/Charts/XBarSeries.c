#include "XBarSeries.h"
#include "XMemory.h"
#include "XClass.h"
#include <string.h>

#if XCHARTS_ON

XBarSeries* XBarSeries_create_ex(XMemoryType memory)
{
    XBarSeries* self = (XBarSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    strcpy(self->m_name, "bar");
    self->m_barWidth = 0.8;
    self->m_visible = true;
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XBarSeries_delete_base(XBarSeries* self)
{
    if (!self) return;
    if (self->m_values) XFree_System(self->m_values);
    if (self->m_categories) XFree_System(self->m_categories);
    XFree_System(self);
}

int XBarSeries_append(XBarSeries* self, const char* label, double value)
{
    int idx;
    if (!self || !label) return -1;
    if (self->m_count >= self->m_capacity) {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 8;
        double* v = (double*)XRealloc_System(self->m_values,
            sizeof(double) * (size_t)cap);
        char (*cat)[64] = (char(*)[64])XRealloc_System(self->m_categories,
            sizeof(char[64]) * (size_t)cap);
        if (!v || !cat) return -1;
        self->m_values = v;
        self->m_categories = cat;
        self->m_capacity = cap;
    }
    idx = self->m_count;
    self->m_values[idx] = value;
    memset(self->m_categories[idx], 0, 64);
    strncpy(self->m_categories[idx], label, 63);
    self->m_count++;
    return idx;
}

int XBarSeries_count(const XBarSeries* self) { return self ? self->m_count : 0; }

double XBarSeries_value(const XBarSeries* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return 0;
    return self->m_values[index];
}

const char* XBarSeries_category(const XBarSeries* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return "";
    return self->m_categories[index];
}

void XBarSeries_setBarWidth(XBarSeries* self, double width)
{
    if (!self) return;
    if (width < 0.1) width = 0.1;
    if (width > 1.0) width = 1.0;
    self->m_barWidth = width;
}

void XBarSeries_setColor(XBarSeries* self, uint32_t color)
{ if (self) self->m_color = color; }

void XBarSeries_setName(XBarSeries* self, const char* name)
{
    if (!self || !name) return;
    strncpy(self->m_name, name, sizeof(self->m_name) - 1);
    self->m_name[sizeof(self->m_name) - 1] = 0;
}

const char* XBarSeries_name(const XBarSeries* self)
{ return self ? self->m_name : ""; }

void XBarSeries_clear(XBarSeries* self) { if (self) self->m_count = 0; }

#endif /* XCHARTS_ON */