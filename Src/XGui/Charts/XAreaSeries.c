#include "XAreaSeries.h"

#include "XAlgorithm.h"
#include "XAbstractSeries.h"
#include "XString.h"
#include "XMemory.h"
#include "XClass.h"

#if XCHARTS_ON

static void VXAreaSeries_deinit(XAreaSeries* self);

static void VXAreaSeries_deinit(XAreaSeries* self)
{
    if (!self) return;
    if (self->m_upper) {
        XLineSeries_delete_base(self->m_upper);
        self->m_upper = NULL;
    }
    if (self->m_pointLabelsFormat) {
        XString_delete_base(self->m_pointLabelsFormat);
        self->m_pointLabelsFormat = NULL;
    }
    if (self->m_pointLabelsFontFamily) {
        XString_delete_base(self->m_pointLabelsFontFamily);
        self->m_pointLabelsFontFamily = NULL;
    }
    XClass_Deinit_Parent(XAbstractSeries, &self->m_base);
}

XVtable* XAreaSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAreaSeries)
    XVTABLE_INHERIT_XCLASS(XAbstractSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAreaSeries_deinit);
    return XVTABLE_DEFAULT;
}

void XAreaSeries_init(XAreaSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractSeries_init(&self->m_base);
    XClassSetVtable(self, XAreaSeries);
    XAbstractSeries_setName(&self->m_base, "area");
    self->m_base.m_type = XChartSeriesType_Area;
    self->m_upper = XLineSeries_create();
    self->m_lower = NULL;
    self->m_borderColor = 0;
    self->m_borderWidth = 1.0;
    self->m_brushColor = 0;
    self->m_pointsVisible = false;
    self->m_pointLabelsVisible = false;
    self->m_pointLabelsFormat = XString_create_utf8("@xPoint, @yPoint");
    self->m_pointLabelsColor = 0;
    self->m_pointLabelsFontFamily = NULL;
    self->m_pointLabelsFontSize = 0;
    self->m_pointLabelsClipping = true;
}

XAreaSeries* XAreaSeries_create_ex(XMemoryType memory)
{
    XAreaSeries* self = (XAreaSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XAreaSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}


XLineSeries* XAreaSeries_upperSeries(const XAreaSeries* self)
{ return self ? self->m_upper : NULL; }

void XAreaSeries_setBaseValue(XAreaSeries* self, double base)
{ if (self) self->m_baseValue = base; }

void XAreaSeries_setColor(XAreaSeries* self, uint32_t color)
{ if (self) self->m_color = color; }

void XAreaSeries_setName(XAreaSeries* self, const char* name)
{ if (self) XAbstractSeries_setName(&self->m_base, name); }

const char* XAreaSeries_name(const XAreaSeries* self)
{ return self ? XAbstractSeries_name(&self->m_base) : ""; }

void XAreaSeries_setUpperSeries(XAreaSeries* self, XLineSeries* series)
{
    if (!self) return;
    if (self->m_upper && self->m_upper != series)
        XLineSeries_delete_base(self->m_upper);
    self->m_upper = series;
}

void XAreaSeries_setLowerSeries(XAreaSeries* self, XLineSeries* series)
{ if (self) self->m_lower = series; }

XLineSeries* XAreaSeries_lowerSeries(const XAreaSeries* self)
{ return self ? self->m_lower : NULL; }

void XAreaSeries_setBorderColor(XAreaSeries* self, uint32_t color)
{ if (self) self->m_borderColor = color; }

uint32_t XAreaSeries_borderColor(const XAreaSeries* self)
{ return self ? self->m_borderColor : 0; }

void XAreaSeries_setPointsVisible(XAreaSeries* self, bool visible)
{ if (self) self->m_pointsVisible = visible; }

bool XAreaSeries_pointsVisible(const XAreaSeries* self)
{ return self ? self->m_pointsVisible : false; }

void XAreaSeries_setPointLabelsVisible(XAreaSeries* self, bool visible)
{ if (self) self->m_pointLabelsVisible = visible; }

bool XAreaSeries_pointLabelsVisible(const XAreaSeries* self)
{ return self ? self->m_pointLabelsVisible : false; }

void XAreaSeries_setPointLabelsFormat(XAreaSeries* self, const char* format)
{
    if (!self) return;
    if (!self->m_pointLabelsFormat)
        self->m_pointLabelsFormat = XString_create();
    if (self->m_pointLabelsFormat)
        XString_assign_utf8(self->m_pointLabelsFormat,
                            format ? format : "@xPoint, @yPoint");
}

const char* XAreaSeries_pointLabelsFormat(const XAreaSeries* self)
{
    const char* text;
    if (!self || !self->m_pointLabelsFormat) return "@xPoint, @yPoint";
    text = XString_toUtf8(self->m_pointLabelsFormat);
    return text ? text : "@xPoint, @yPoint";
}

void XAreaSeries_setPointLabelsColor(XAreaSeries* self, uint32_t color)
{ if (self) self->m_pointLabelsColor = color; }

uint32_t XAreaSeries_pointLabelsColor(const XAreaSeries* self)
{ return self ? self->m_pointLabelsColor : 0; }

void XAreaSeries_setPointLabelsFont(XAreaSeries* self, const char* family,
                                    int pointSize)
{
    if (!self) return;
    if (family) {
        if (!self->m_pointLabelsFontFamily)
            self->m_pointLabelsFontFamily = XString_create();
        if (self->m_pointLabelsFontFamily)
            XString_assign_utf8(self->m_pointLabelsFontFamily, family);
    }
    if (pointSize > 0) self->m_pointLabelsFontSize = pointSize;
}

const char* XAreaSeries_pointLabelsFontFamily(const XAreaSeries* self)
{
    const char* text;
    if (!self || !self->m_pointLabelsFontFamily) return "";
    text = XString_toUtf8(self->m_pointLabelsFontFamily);
    return text ? text : "";
}

int XAreaSeries_pointLabelsFontSize(const XAreaSeries* self)
{ return self ? self->m_pointLabelsFontSize : 0; }

void XAreaSeries_setPointLabelsClipping(XAreaSeries* self, bool clip)
{ if (self) self->m_pointLabelsClipping = clip; }

bool XAreaSeries_pointLabelsClipping(const XAreaSeries* self)
{ return self ? self->m_pointLabelsClipping : true; }

uint32_t XAreaSeries_color(const XAreaSeries* self)
{ return self ? self->m_color : 0; }

void XAreaSeries_setPen(XAreaSeries* self, uint32_t color, double width)
{
    if (!self) return;
    self->m_color = color;
    if (width > 0) self->m_borderWidth = width;
}

void XAreaSeries_pen(const XAreaSeries* self, uint32_t* color,
                     double* width)
{
    if (!self) return;
    if (color) *color = self->m_color;
    if (width) *width = self->m_borderWidth;
}

void XAreaSeries_setBrush(XAreaSeries* self, uint32_t color)
{ if (self) self->m_brushColor = color; }

uint32_t XAreaSeries_brush(const XAreaSeries* self)
{ return self ? self->m_brushColor : 0; }

const char* XAreaSeries_pointLabelsFont(const XAreaSeries* self)
{ return XAreaSeries_pointLabelsFontFamily(self); }

#endif /* XCHARTS_ON */