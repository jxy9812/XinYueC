#if XGUI_ON

#include "XAreaSeries.h"

#include "XAlgorithm.h"
#include "XAbstractSeries.h"
#include "XString.h"
#include "XMemory.h"
#include "XClass.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手（供 setter 复用） ==================== */

static bool xarea_canEmit(const XAreaSeries* self)
{
    return self && ((const XObject*)self)->m_signalSlot;
}

static void xarea_emitVoid(XAreaSeries* self, size_t signal)
{
    XVarList* args;
    int dummy = 0;
    if (!xarea_canEmit(self)) return;
    args = XVarList_Create(XVar(int, dummy));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xarea_emitPoint(XAreaSeries* self, size_t signal, double x, double y)
{
    XVarList* args;
    if (!xarea_canEmit(self)) return;
    args = XVarList_Create(XVar(double, x), XVar(double, y));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xarea_emitPointState(XAreaSeries* self, size_t signal,
                                 double x, double y, bool state)
{
    XVarList* args;
    if (!xarea_canEmit(self)) return;
    args = XVarList_Create(XVar(double, x), XVar(double, y),
                           XVar(bool, state));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xarea_emitU32(XAreaSeries* self, size_t signal, uint32_t v)
{
    XVarList* args;
    if (!xarea_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xarea_emitBool(XAreaSeries* self, size_t signal, bool v)
{
    XVarList* args;
    if (!xarea_canEmit(self)) return;
    args = XVarList_Create(XVar(bool, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xarea_emitFormat(XAreaSeries* self, size_t signal, const char* fmt)
{
    XVarList* args;
    if (!xarea_canEmit(self)) return;
    args = XVarList_Create(XVar(const char*, fmt));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xarea_emitFont(XAreaSeries* self, size_t signal,
                           const char* family, int pointSize)
{
    XVarList* args;
    if (!xarea_canEmit(self)) return;
    args = XVarList_Create(XVar(const char*, family), XVar(int, pointSize));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}


static void VXAreaSeries_deinit(XAreaSeries* self);

static void VXAreaSeries_deinit(XAreaSeries* self)
{
    if (!self) return;
    if (self->m_upper) {
        XClassDelete(self->m_upper);
        self->m_upper = NULL;
    }
    if (self->m_pointLabelsFormat) {
        XClassDelete(self->m_pointLabelsFormat);
        self->m_pointLabelsFormat = NULL;
    }
    if (self->m_pointLabelsFontFamily) {
        XClassDelete(self->m_pointLabelsFontFamily);
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
    XAbstractSeries_setName_2(&self->m_base, "area");
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
{
    if (!self || self->m_color == color) return;
    self->m_color = color;
    xarea_emitU32(self, (size_t)XAreaSeries_colorChanged_signal, color);
}

void XAreaSeries_setName(XAreaSeries* self, const XString* name)
{ if (self) XAbstractSeries_setName(&self->m_base, name); }

void XAreaSeries_setName_2(XAreaSeries* self, const char* name)
{ if (self) XAbstractSeries_setName_2(&self->m_base, name); }

const XString* XAreaSeries_name(const XAreaSeries* self)
{ return self ? XAbstractSeries_name(&self->m_base) : NULL; }

const char* XAreaSeries_name_2(const XAreaSeries* self)
{ return self ? XAbstractSeries_name_2(&self->m_base) : ""; }

void XAreaSeries_setUpperSeries(XAreaSeries* self, XLineSeries* series)
{
    if (!self) return;
    if (self->m_upper && self->m_upper != series) {
        /* 旧 upper 序列可能正被本系列绘制遍历或其自身信号链引用，延迟回收。 */
        XObject_deleteLater((XObject*)self->m_upper);
    }
    self->m_upper = series;
}

void XAreaSeries_setLowerSeries(XAreaSeries* self, XLineSeries* series)
{ if (self) self->m_lower = series; }

XLineSeries* XAreaSeries_lowerSeries(const XAreaSeries* self)
{ return self ? self->m_lower : NULL; }

void XAreaSeries_setBorderColor(XAreaSeries* self, uint32_t color)
{
    if (!self || self->m_borderColor == color) return;
    self->m_borderColor = color;
    xarea_emitU32(self, (size_t)XAreaSeries_borderColorChanged_signal, color);
}

uint32_t XAreaSeries_borderColor(const XAreaSeries* self)
{ return self ? self->m_borderColor : 0; }

void XAreaSeries_setPointsVisible(XAreaSeries* self, bool visible)
{ if (self) self->m_pointsVisible = visible; }

bool XAreaSeries_pointsVisible(const XAreaSeries* self)
{ return self ? self->m_pointsVisible : false; }

void XAreaSeries_setPointLabelsVisible(XAreaSeries* self, bool visible)
{
    if (!self || self->m_pointLabelsVisible == visible) return;
    self->m_pointLabelsVisible = visible;
    xarea_emitBool(self,
                   (size_t)XAreaSeries_pointLabelsVisibilityChanged_signal,
                   visible);
}

bool XAreaSeries_pointLabelsVisible(const XAreaSeries* self)
{ return self ? self->m_pointLabelsVisible : false; }

void XAreaSeries_setPointLabelsFormat(XAreaSeries* self, const XString* format)
{
    if (!self) return;
    if (!self->m_pointLabelsFormat)
        self->m_pointLabelsFormat = XString_create();
    if (!self->m_pointLabelsFormat) return;
    if (format)
        XString_assign(self->m_pointLabelsFormat, format);
    else
        XString_assign_utf8(self->m_pointLabelsFormat, "@xPoint, @yPoint");
}
void XAreaSeries_setPointLabelsFormat_2(XAreaSeries* self, const char* format)
{
    XString* tmp = NULL;
    if (format) {
        tmp = XString_create_utf8(format);
        if (!tmp) return;
    }
    XAreaSeries_setPointLabelsFormat(self, tmp);
    if (tmp) XClassDelete(tmp);
}

const XString* XAreaSeries_pointLabelsFormat(const XAreaSeries* self)
{
    return (self && self->m_pointLabelsFormat)
               ? self->m_pointLabelsFormat : NULL;
}
const char* XAreaSeries_pointLabelsFormat_2(const XAreaSeries* self)
{
    const XString* s;
    s = XAreaSeries_pointLabelsFormat(self);
    return s ? XString_toUtf8(s) : "@xPoint, @yPoint";
}

void XAreaSeries_setPointLabelsColor(XAreaSeries* self, uint32_t color)
{ if (self) self->m_pointLabelsColor = color; }

uint32_t XAreaSeries_pointLabelsColor(const XAreaSeries* self)
{ return self ? self->m_pointLabelsColor : 0; }

void XAreaSeries_setPointLabelsFont(XAreaSeries* self, const XString* family,
                                    int pointSize)
{
    if (!self) return;
    if (family) {
        if (!self->m_pointLabelsFontFamily)
            self->m_pointLabelsFontFamily = XString_create();
        if (self->m_pointLabelsFontFamily)
            XString_assign(self->m_pointLabelsFontFamily, family);
    }
    if (pointSize > 0) self->m_pointLabelsFontSize = pointSize;
}
void XAreaSeries_setPointLabelsFont_2(XAreaSeries* self, const char* family,
                                      int pointSize)
{
    XString* tmp = NULL;
    if (family) {
        tmp = XString_create_utf8(family);
        if (!tmp) return;
    }
    XAreaSeries_setPointLabelsFont(self, tmp, pointSize);
    if (tmp) XClassDelete(tmp);
}

const XString* XAreaSeries_pointLabelsFontFamily(const XAreaSeries* self)
{
    return (self && self->m_pointLabelsFontFamily)
               ? self->m_pointLabelsFontFamily : NULL;
}
const char* XAreaSeries_pointLabelsFontFamily_2(const XAreaSeries* self)
{
    const XString* s;
    s = XAreaSeries_pointLabelsFontFamily(self);
    return s ? XString_toUtf8(s) : "";
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

const XString* XAreaSeries_pointLabelsFont(const XAreaSeries* self)
{ return XAreaSeries_pointLabelsFontFamily(self); }
const char* XAreaSeries_pointLabelsFont_2(const XAreaSeries* self)
{ return XAreaSeries_pointLabelsFontFamily_2(self); }

/* ==================== 类型查询 ==================== */

int XAreaSeries_type(const XAreaSeries* self)
{
    (void)self;
    return XChartSeriesType_Area;
}

/* ==================== 交互信号（对标 QAreaSeries Q_SIGNALS） ==================== */

void* XAreaSeries_clicked_signal(XAreaSeries* self, double x, double y)
{
    xarea_emitPoint(self, (size_t)XAreaSeries_clicked_signal, x, y);
    return (void*)(size_t)XAreaSeries_clicked_signal;
}

void* XAreaSeries_hovered_signal(XAreaSeries* self, double x, double y,
                                 bool state)
{
    xarea_emitPointState(self, (size_t)XAreaSeries_hovered_signal, x, y, state);
    return (void*)(size_t)XAreaSeries_hovered_signal;
}

void* XAreaSeries_pressed_signal(XAreaSeries* self, double x, double y)
{
    xarea_emitPoint(self, (size_t)XAreaSeries_pressed_signal, x, y);
    return (void*)(size_t)XAreaSeries_pressed_signal;
}

void* XAreaSeries_released_signal(XAreaSeries* self, double x, double y)
{
    xarea_emitPoint(self, (size_t)XAreaSeries_released_signal, x, y);
    return (void*)(size_t)XAreaSeries_released_signal;
}

void* XAreaSeries_doubleClicked_signal(XAreaSeries* self, double x, double y)
{
    xarea_emitPoint(self, (size_t)XAreaSeries_doubleClicked_signal, x, y);
    return (void*)(size_t)XAreaSeries_doubleClicked_signal;
}

void* XAreaSeries_selected_signal(XAreaSeries* self)
{
    xarea_emitVoid(self, (size_t)XAreaSeries_selected_signal);
    return (void*)(size_t)XAreaSeries_selected_signal;
}

void* XAreaSeries_colorChanged_signal(XAreaSeries* self, uint32_t color)
{
    xarea_emitU32(self, (size_t)XAreaSeries_colorChanged_signal, color);
    return (void*)(size_t)XAreaSeries_colorChanged_signal;
}

void* XAreaSeries_borderColorChanged_signal(XAreaSeries* self, uint32_t color)
{
    xarea_emitU32(self, (size_t)XAreaSeries_borderColorChanged_signal, color);
    return (void*)(size_t)XAreaSeries_borderColorChanged_signal;
}

void* XAreaSeries_pointLabelsFormatChanged_signal(XAreaSeries* self,
                                                  const char* format)
{
    xarea_emitFormat(self, (size_t)XAreaSeries_pointLabelsFormatChanged_signal,
                     format);
    return (void*)(size_t)XAreaSeries_pointLabelsFormatChanged_signal;
}

void* XAreaSeries_pointLabelsVisibilityChanged_signal(XAreaSeries* self,
                                                      bool visible)
{
    xarea_emitBool(self,
                   (size_t)XAreaSeries_pointLabelsVisibilityChanged_signal,
                   visible);
    return (void*)(size_t)XAreaSeries_pointLabelsVisibilityChanged_signal;
}

void* XAreaSeries_pointLabelsFontChanged_signal(XAreaSeries* self,
                                                const char* family,
                                                int pointSize)
{
    xarea_emitFont(self, (size_t)XAreaSeries_pointLabelsFontChanged_signal,
                   family, pointSize);
    return (void*)(size_t)XAreaSeries_pointLabelsFontChanged_signal;
}

void* XAreaSeries_pointLabelsColorChanged_signal(XAreaSeries* self,
                                                 uint32_t color)
{
    xarea_emitU32(self, (size_t)XAreaSeries_pointLabelsColorChanged_signal,
                  color);
    return (void*)(size_t)XAreaSeries_pointLabelsColorChanged_signal;
}

void* XAreaSeries_pointLabelsClippingChanged_signal(XAreaSeries* self,
                                                    bool clipping)
{
    xarea_emitBool(self,
                   (size_t)XAreaSeries_pointLabelsClippingChanged_signal,
                   clipping);
    return (void*)(size_t)XAreaSeries_pointLabelsClippingChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
