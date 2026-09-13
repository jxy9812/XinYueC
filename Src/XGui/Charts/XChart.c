#include "XChart.h"
#include "XValueAxis.h"
#include "XLineSeries.h"
#include "XPieSeries.h"
#include "XBarSeries.h"
#include "XScatterSeries.h"
#include "XAreaSeries.h"
#include "XSplineSeries.h"
#include "XMemory.h"
#include "XClass.h"
#include <string.h>

#if XCHARTS_ON

/* 主题系列色板（对标 ChartThemeLight 的序列色顺序）。 */
static const uint32_t g_chartTheme[8] = {
    0xFF209ADFu, 0xFFEA8B20u, 0xFF219E38u, 0xFFD1294Bu,
    0xFF8B5AC7u, 0xFF16AFA9u, 0xFFC33E92u, 0xFF6E6E6Eu
};

XVtable* XChart_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XChart);
    return XVTABLE_DEFAULT;
}

void XChart_init(XChart* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    strcpy(self->m_title, "Chart");
    self->m_legendVisible = true;
    self->m_titleVisible = true;
    self->m_axisX = (XValueAxis*)XMalloc_System(sizeof(XValueAxis));
    self->m_axisY = (XValueAxis*)XMalloc_System(sizeof(XValueAxis));
    if (self->m_axisX) XValueAxis_init(self->m_axisX);
    if (self->m_axisY) XValueAxis_init(self->m_axisY);
    memcpy(self->m_theme, g_chartTheme, sizeof(g_chartTheme));
}

XChart* XChart_create_ex(XMemoryType memory)
{
    XChart* self = (XChart*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XChart_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XChart_deinit(XChart* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_lineCount; ++i)
        if (self->m_lineSeries[i]) XLineSeries_delete_base(self->m_lineSeries[i]);
    if (self->m_pieSeries) XPieSeries_delete_base(self->m_pieSeries);
    for (i = 0; i < self->m_barCount; ++i)
        if (self->m_barSeries[i]) XBarSeries_delete_base(self->m_barSeries[i]);
    for (i = 0; i < self->m_scatterCount; ++i)
        if (self->m_scatterSeries[i]) XScatterSeries_delete_base(self->m_scatterSeries[i]);
    for (i = 0; i < self->m_areaCount; ++i)
        if (self->m_areaSeries[i]) XAreaSeries_delete_base(self->m_areaSeries[i]);
    for (i = 0; i < self->m_splineCount; ++i)
        if (self->m_splineSeries[i]) XSplineSeries_delete_base(self->m_splineSeries[i]);
    if (self->m_axisX) XFree_System(self->m_axisX);
    if (self->m_axisY) XFree_System(self->m_axisY);
    self->m_lineCount = 0;
    self->m_pieSeries = NULL;
}

void XChart_setTitle(XChart* self, const char* title)
{
    if (!self || !title) return;
    strncpy(self->m_title, title, sizeof(self->m_title) - 1);
    self->m_title[sizeof(self->m_title) - 1] = 0;
}

const char* XChart_title(const XChart* self) { return self ? self->m_title : ""; }
void XChart_setTitleVisible(XChart* self, bool visible)
{ if (self) self->m_titleVisible = visible; }
bool XChart_isTitleVisible(const XChart* self)
{ return self ? self->m_titleVisible : false; }
void XChart_setLegendVisible(XChart* self, bool visible)
{ if (self) self->m_legendVisible = visible; }
bool XChart_isLegendVisible(const XChart* self)
{ return self ? self->m_legendVisible : false; }

void XChart_addLineSeries(XChart* self, XLineSeries* series)
{
    if (!self || !series || self->m_lineCount >= 8) return;
    self->m_lineSeries[self->m_lineCount++] = series;
}

void XChart_setPieSeries(XChart* self, XPieSeries* series)
{
    if (!self) return;
    if (self->m_pieSeries) XPieSeries_delete_base(self->m_pieSeries);
    self->m_pieSeries = series;
}

int XChart_lineSeriesCount(const XChart* self)
{ return self ? self->m_lineCount : 0; }

XLineSeries* XChart_lineSeries(const XChart* self, int index)
{
    if (!self || index < 0 || index >= self->m_lineCount) return NULL;
    return self->m_lineSeries[index];
}

XPieSeries* XChart_pieSeries(const XChart* self)
{ return self ? self->m_pieSeries : NULL; }

void XChart_addBarSeries(XChart* self, XBarSeries* series)
{
    if (!self || !series || self->m_barCount >= 4) return;
    self->m_barSeries[self->m_barCount++] = series;
}

void XChart_addScatterSeries(XChart* self, XScatterSeries* series)
{
    if (!self || !series || self->m_scatterCount >= 4) return;
    self->m_scatterSeries[self->m_scatterCount++] = series;
}

void XChart_addAreaSeries(XChart* self, XAreaSeries* series)
{
    if (!self || !series || self->m_areaCount >= 4) return;
    self->m_areaSeries[self->m_areaCount++] = series;
}

void XChart_addSplineSeries(XChart* self, XSplineSeries* series)
{
    if (!self || !series || self->m_splineCount >= 4) return;
    self->m_splineSeries[self->m_splineCount++] = series;
}

int XChart_barSeriesCount(const XChart* self) { return self ? self->m_barCount : 0; }
XBarSeries* XChart_barSeries(const XChart* self, int index)
{
    if (!self || index < 0 || index >= self->m_barCount) return NULL;
    return self->m_barSeries[index];
}
int XChart_scatterSeriesCount(const XChart* self) { return self ? self->m_scatterCount : 0; }
XScatterSeries* XChart_scatterSeries(const XChart* self, int index)
{
    if (!self || index < 0 || index >= self->m_scatterCount) return NULL;
    return self->m_scatterSeries[index];
}
int XChart_areaSeriesCount(const XChart* self) { return self ? self->m_areaCount : 0; }
XAreaSeries* XChart_areaSeries(const XChart* self, int index)
{
    if (!self || index < 0 || index >= self->m_areaCount) return NULL;
    return self->m_areaSeries[index];
}
int XChart_splineSeriesCount(const XChart* self) { return self ? self->m_splineCount : 0; }
XSplineSeries* XChart_splineSeries(const XChart* self, int index)
{
    if (!self || index < 0 || index >= self->m_splineCount) return NULL;
    return self->m_splineSeries[index];
}

XValueAxis* XChart_axisX(const XChart* self) { return self ? self->m_axisX : NULL; }
XValueAxis* XChart_axisY(const XChart* self) { return self ? self->m_axisY : NULL; }

uint32_t XChart_themeColor(const XChart* self, int index)
{
    if (!self) return 0xFF000000u;
    index %= 8;
    if (index < 0) index += 8;
    return self->m_theme[index];
}

#endif /* XCHARTS_ON */