#include "XValueAxis.h"
#include <string.h>

#if XCHARTS_ON

void XValueAxis_init(XValueAxis* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    self->m_min = 0.0;
    self->m_max = 10.0;
    self->m_tickCount = 6;
    strcpy(self->m_labelFormat, "%g");
    self->m_visible = true;
    self->m_gridVisible = true;
}

void XValueAxis_setRange(XValueAxis* self, double min, double max)
{
    if (!self || min > max) return;
    self->m_min = min;
    self->m_max = max;
}

double XValueAxis_min(const XValueAxis* self) { return self ? self->m_min : 0.0; }
double XValueAxis_max(const XValueAxis* self) { return self ? self->m_max : 0.0; }

void XValueAxis_setTickCount(XValueAxis* self, int count)
{
    if (!self || count < 2) return;
    self->m_tickCount = count;
}

int XValueAxis_tickCount(const XValueAxis* self) { return self ? self->m_tickCount : 0; }

void XValueAxis_setLabelFormat(XValueAxis* self, const char* fmt)
{
    if (!self || !fmt) return;
    strncpy(self->m_labelFormat, fmt, sizeof(self->m_labelFormat) - 1);
    self->m_labelFormat[sizeof(self->m_labelFormat) - 1] = 0;
}

void XValueAxis_setTitleText(XValueAxis* self, const char* title)
{
    if (!self || !title) return;
    strncpy(self->m_titleText, title, sizeof(self->m_titleText) - 1);
    self->m_titleText[sizeof(self->m_titleText) - 1] = 0;
}

void XValueAxis_setVisible(XValueAxis* self, bool visible)
{ if (self) self->m_visible = visible; }
bool XValueAxis_isVisible(const XValueAxis* self)
{ return self ? self->m_visible : false; }
bool XValueAxis_isGridVisible(const XValueAxis* self)
{ return self ? self->m_gridVisible : false; }
void XValueAxis_setGridVisible(XValueAxis* self, bool visible)
{ if (self) self->m_gridVisible = visible; }

#endif /* XCHARTS_ON */