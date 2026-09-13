#include "XSplineSeries.h"
#include "XMemory.h"
#include "XClass.h"
#include <string.h>

#if XCHARTS_ON

XSplineSeries* XSplineSeries_create_ex(XMemoryType memory)
{
    XSplineSeries* self = (XSplineSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    strcpy(self->m_name, "spline");
    self->m_width = 2.0;
    self->m_visible = true;
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XSplineSeries_delete_base(XSplineSeries* self)
{
    if (!self) return;
    if (self->m_points) XFree_System(self->m_points);
    XFree_System(self);
}

void XSplineSeries_append(XSplineSeries* self, double x, double y)
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

int XSplineSeries_count(const XSplineSeries* self)
{ return self ? self->m_count : 0; }

void XSplineSeries_setColor(XSplineSeries* self, uint32_t color)
{ if (self) self->m_color = color; }

void XSplineSeries_setName(XSplineSeries* self, const char* name)
{
    if (!self || !name) return;
    strncpy(self->m_name, name, sizeof(self->m_name) - 1);
    self->m_name[sizeof(self->m_name) - 1] = 0;
}

void XSplineSeries_clear(XSplineSeries* self)
{ if (self) self->m_count = 0; }

#endif /* XCHARTS_ON */