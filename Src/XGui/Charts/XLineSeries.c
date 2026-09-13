#include "XLineSeries.h"
#include "XMemory.h"
#include "XClass.h"
#include <string.h>

#if XCHARTS_ON

XLineSeries* XLineSeries_create_ex(XMemoryType memory)
{
    XLineSeries* self = (XLineSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    strcpy(self->m_name, "line");
    self->m_width = 2.0;
    self->m_visible = true;
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XLineSeries_delete_base(XLineSeries* self)
{
    if (!self) return;
    if (self->m_points) XFree_System(self->m_points);
    XFree_System(self);
}

void XLineSeries_setName(XLineSeries* self, const char* name)
{
    if (!self || !name) return;
    strncpy(self->m_name, name, sizeof(self->m_name) - 1);
    self->m_name[sizeof(self->m_name) - 1] = 0;
}

const char* XLineSeries_name(const XLineSeries* self)
{ return self ? self->m_name : ""; }

void XLineSeries_setColor(XLineSeries* self, uint32_t color)
{ if (self) self->m_color = color; }
uint32_t XLineSeries_color(const XLineSeries* self)
{ return self ? self->m_color : 0; }
void XLineSeries_setWidth(XLineSeries* self, double width)
{ if (self && width > 0) self->m_width = width; }

void XLineSeries_append(XLineSeries* self, double x, double y)
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

void XLineSeries_clear(XLineSeries* self) { if (self) self->m_count = 0; }
int XLineSeries_count(const XLineSeries* self) { return self ? self->m_count : 0; }

const XPointF* XLineSeries_at(const XLineSeries* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return NULL;
    return &self->m_points[index];
}

void XLineSeries_setVisible(XLineSeries* self, bool visible)
{ if (self) self->m_visible = visible; }

#endif /* XCHARTS_ON */