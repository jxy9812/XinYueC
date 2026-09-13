#include "XAreaSeries.h"
#include "XMemory.h"
#include "XClass.h"
#include <string.h>

#if XCHARTS_ON

XAreaSeries* XAreaSeries_create_ex(XMemoryType memory)
{
    XAreaSeries* self = (XAreaSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    strcpy(self->m_name, "area");
    self->m_upper = XLineSeries_create_ex(memory);
    self->m_visible = true;
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XAreaSeries_delete_base(XAreaSeries* self)
{
    if (!self) return;
    if (self->m_upper) XLineSeries_delete_base(self->m_upper);
    XFree_System(self);
}

XLineSeries* XAreaSeries_upperSeries(const XAreaSeries* self)
{ return self ? self->m_upper : NULL; }

void XAreaSeries_setBaseValue(XAreaSeries* self, double base)
{ if (self) self->m_baseValue = base; }

void XAreaSeries_setColor(XAreaSeries* self, uint32_t color)
{ if (self) self->m_color = color; }

void XAreaSeries_setName(XAreaSeries* self, const char* name)
{
    if (!self || !name) return;
    strncpy(self->m_name, name, sizeof(self->m_name) - 1);
    self->m_name[sizeof(self->m_name) - 1] = 0;
}

#endif /* XCHARTS_ON */