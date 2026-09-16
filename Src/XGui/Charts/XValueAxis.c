#include "XValueAxis.h"
#include "XMemory.h"

#include "XAlgorithm.h"
#include "XString.h"

#if XCHARTS_ON

void XValueAxis_init(XValueAxis* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractAxis_init(&self->m_base);
    self->m_tickCount = 6;
    self->m_labelFormat = XString_create_utf8("%g");
}

void XValueAxis_setRange(XValueAxis* self, double min, double max)
{
    if (!self) return;
    XAbstractAxis_setRange(&self->m_base, min, max);
}

double XValueAxis_min(const XValueAxis* self)
{ return XAbstractAxis_min(self ? &self->m_base : NULL); }
double XValueAxis_max(const XValueAxis* self)
{ return XAbstractAxis_max(self ? &self->m_base : NULL); }

void XValueAxis_setTickCount(XValueAxis* self, int count)
{
    if (!self || count < 2) return;
    self->m_tickCount = count;
}

int XValueAxis_tickCount(const XValueAxis* self) { return self ? self->m_tickCount : 0; }

void XValueAxis_setLabelFormat(XValueAxis* self, const XString* fmt)
{
    if (!self) return;
    if (!self->m_labelFormat) self->m_labelFormat = XString_create();
    if (!self->m_labelFormat) return;
    if (fmt)
        XString_assign(self->m_labelFormat, fmt);
    else
        XString_assign_utf8(self->m_labelFormat, "%g");
}
void XValueAxis_setLabelFormat_2(XValueAxis* self, const char* fmt)
{
    XString* tmp = NULL;
    if (fmt) {
        tmp = XString_create_utf8(fmt);
        if (!tmp) return;
    }
    XValueAxis_setLabelFormat(self, tmp);
    if (tmp) XString_delete_base(tmp);
}

void XValueAxis_setTitleText(XValueAxis* self, const XString* title)
{
    if (!self) return;
    XAbstractAxis_setTitleText(&self->m_base, title);
}
void XValueAxis_setTitleText_2(XValueAxis* self, const char* title)
{
    if (!self) return;
    XAbstractAxis_setTitleText_2(&self->m_base, title);
}

void XValueAxis_setVisible(XValueAxis* self, bool visible)
{ if (self) XAbstractAxis_setVisible(&self->m_base, visible); }
bool XValueAxis_isVisible(const XValueAxis* self)
{ return self ? XAbstractAxis_isVisible(&self->m_base) : false; }
bool XValueAxis_isGridVisible(const XValueAxis* self)
{ return self ? XAbstractAxis_isGridLineVisible(&self->m_base) : false; }
void XValueAxis_setGridVisible(XValueAxis* self, bool visible)
{ if (self) XAbstractAxis_setGridLineVisible(&self->m_base, visible); }

#endif /* XCHARTS_ON */
void XValueAxis_deinit_impl(XValueAxis* self)
{
    if (!self) return;
    if (self->m_labelFormat) {
        XString_delete_base(self->m_labelFormat);
        self->m_labelFormat = NULL;
    }
    XAbstractAxis_deinit_base(&self->m_base);
}
