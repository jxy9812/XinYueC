#include "XScatterSeries.h"
#include "XMemory.h"
#include "XClass.h"
#include <string.h>

#if XCHARTS_ON

XScatterSeries* XScatterSeries_create_ex(XMemoryType memory)
{
    XScatterSeries* self = (XScatterSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    strcpy(self->m_name, "scatter");
    self->m_markerSize = 8;
    self->m_visible = true;
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XScatterSeries_delete_base(XScatterSeries* self)
{
    if (!self) return;
    if (self->m_points) XFree_System(self->m_points);
    XFree_System(self);
}

void XScatterSeries_setName(XScatterSeries* self, const char* name)
{
    if (!self || !name) return;
    strncpy(self->m_name, name, sizeof(self->m_name) - 1);
    self->m_name[sizeof(self->m_name) - 1] = 0;
}

void XScatterSeries_setColor(XScatterSeries* self, uint32_t color)
{ if (self) self->m_color = color; }

void XScatterSeries_setMarkerSize(XScatterSeries* self, int size)
{ if (self && size >= 2) self->m_markerSize = size; }

void XScatterSeries_append(XScatterSeries* self, double x, double y)
{
    if (!self) return;
    if (self->m_count >= self->m_capacity) {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 16;
        XPointF* p = (XPointF*)XRealloc_System(self->m_points,
            sizeof(XPointF) * (size_t)cap);
        if (!p) return;
        self->m_points = p;
        self->m_capacity = cap;
    }
    self->m_points[self->m_count].x = x;
    self->m_points[self->m_count].y = y;
    self->m_count++;
}

int XScatterSeries_count(const XScatterSeries* self)
{ return self ? self->m_count : 0; }

void XScatterSeries_clear(XScatterSeries* self)
{ if (self) self->m_count = 0; }

#endif /* XCHARTS_ON */