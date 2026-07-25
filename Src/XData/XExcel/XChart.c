#include "XChart.h"
#include "XMemory.h"
#include <stdlib.h>

#include <string.h>

XChart* XChart_create(XAbstractSheet* parent, XAbstractOOXmlFile_CreateFlag flag) {
    (void)parent;
    XChart* self = (XChart*)XMalloc_System(sizeof(XChart));
    if (!self) return NULL; memset(self, 0, sizeof(XChart));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_chartType = XChart_NoStatementChart;
    self->m_chartStyle = -1;
    self->m_legendPos = XChart_AxisPosRight;
    self->m_series = XVector_Create(XChart_Series);
    self->m_width = 480; self->m_height = 290;
    return self;
}
void XChart_delete(XChart* self) {
    if (!self) return;
    if (self->m_chartTitle) { XString_deinit_base(self->m_chartTitle); XFree_System(self->m_chartTitle); }
    if (self->m_axisTitleLeft) { XString_deinit_base(self->m_axisTitleLeft); XFree_System(self->m_axisTitleLeft); }
    if (self->m_axisTitleRight) { XString_deinit_base(self->m_axisTitleRight); XFree_System(self->m_axisTitleRight); }
    if (self->m_axisTitleTop) { XString_deinit_base(self->m_axisTitleTop); XFree_System(self->m_axisTitleTop); }
    if (self->m_axisTitleBottom) { XString_deinit_base(self->m_axisTitleBottom); XFree_System(self->m_axisTitleBottom); }
    if (self->m_series) { XVector_deinit_base(self->m_series); XFree_System(self->m_series); }
    XAbstractOOXmlFile_deinit(&self->m_base); XFree_System(self);
}
void XChart_addSeries(XChart* self, const XCellRange* range, bool headerH, bool headerV, bool swapHeaders) {
    if (!self || !range) return;
    XChart_Series s; memset(&s, 0, sizeof(s)); s.m_range = *range; s.m_headerH = headerH; s.m_headerV = headerV; s.m_swapHeaders = swapHeaders;
    XVector_push_back_2(self->m_series, &s, 1);
}
void XChart_setChartType(XChart* self, XChart_ChartType type) { if (self) self->m_chartType = type; }
void XChart_setChartStyle(XChart* self, int id) { if (self) self->m_chartStyle = id; }
void XChart_setAxisTitle(XChart* self, XChart_ChartAxisPos pos, const XString* axisTitle) {
    if (!self) return;
    XString** target = NULL;
    if (pos == XChart_AxisPosLeft) target = &self->m_axisTitleLeft;
    else if (pos == XChart_AxisPosRight) target = &self->m_axisTitleRight;
    else if (pos == XChart_AxisPosTop) target = &self->m_axisTitleTop;
    else if (pos == XChart_AxisPosBottom) target = &self->m_axisTitleBottom;
    if (target) { if (!*target) *target = XString_create(); if (*target) { XString_clear_base(*target); if (axisTitle) XString_append(*target, axisTitle); } }
}
void XChart_setChartTitle(XChart* self, const XString* title) {
    if (!self) return;
    if (!self->m_chartTitle) self->m_chartTitle = XString_create();
    if (self->m_chartTitle) { XString_clear_base(self->m_chartTitle); if (title) XString_append(self->m_chartTitle, title); }
}
void XChart_setChartLegend(XChart* self, XChart_ChartAxisPos legendPos, bool overlap) { if (self) { self->m_legendPos = legendPos; self->m_legendOverlay = overlap; } }
void XChart_setGridlinesEnable(XChart* self, bool majorEnable, bool minorEnable) { if (self) { self->m_majorGridlinesEnable = majorEnable; self->m_minorGridlinesEnable = minorEnable; } }
void XChart_setSize(XChart* self, int width, int height) { if (self) { self->m_width = width; self->m_height = height; } }
void XChart_setPosition(XChart* self, int row, int col, int rowOff, int colOff) { if (self) { self->m_row = row; self->m_col = col; self->m_rowOffset = rowOff; self->m_colOffset = colOff; } }
bool XChart_loadFromXmlFile(XChart* self, const XString* filePath) { (void)self; (void)filePath; return false; }
bool XChart_saveToXmlFile(XChart* self, const XString* filePath) { (void)self; (void)filePath; return false; }
