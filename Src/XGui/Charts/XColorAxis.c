/**
 * @file       XColorAxis.c
 * @brief      XColorAxis 颜色轴实现（对标 QColorAxis）。
 * @details    渐变以 XLinearGradient 承载（对应 Qt 的 QLinearGradient），
 *             数值域按归一化位置映射到渐变色。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XColorAxis.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool xcaxis_canEmit(const XColorAxis* self)
{
    return self && ((const XObject*)&self->m_base)->m_signalSlot;
}

static void xcaxis_emitDouble(XColorAxis* self, size_t signal, double v)
{
    XVarList* args;
    if (!xcaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(double, v));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcaxis_emitInt(XColorAxis* self, size_t signal, int v)
{
    XVarList* args;
    if (!xcaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(int, v));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcaxis_emitBool(XColorAxis* self, size_t signal, bool v)
{
    XVarList* args;
    if (!xcaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(bool, v));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcaxis_emitRange(XColorAxis* self, size_t signal, double mi,
                             double ma)
{
    XVarList* args;
    if (!xcaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(double, mi), XVar(double, ma));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 内部助手 ==================== */

/** @brief 单通道线性插值（a..b, t 在 [0,1]）。 */
static uint32_t xcaxis_lerpArgb(uint32_t a, uint32_t b, double t)
{
    int i;
    uint32_t out = 0;
    if (t <= 0.0) return a;
    if (t >= 1.0) return b;
    for (i = 0; i < 4; ++i) {
        int shift = i * 8;
        int ca = (int)((a >> shift) & 0xFFu);
        int cb = (int)((b >> shift) & 0xFFu);
        int cv = ca + (int)((cb - ca) * t + 0.5);
        if (cv < 0) cv = 0;
        if (cv > 255) cv = 255;
        out |= ((uint32_t)cv) << shift;
    }
    return out;
}

/* ==================== 虚函数实现 ==================== */

static void VXColorAxis_deinit(XColorAxis* self)
{
    if (!self) return;
    if (self->m_gradient) {
        XClassDelete(self->m_gradient);
        self->m_gradient = NULL;
    }
    XClass_Deinit_Parent(XAbstractAxis, &self->m_base);
}

XVtable* XColorAxis_class_init(void)
{
    XVTABLE_INIT_DEFAULT_SIZE(XCLASS_VTABLE_GET_SIZE(XAbstractAxis))
    XVTABLE_INHERIT_XCLASS(XAbstractAxis);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXColorAxis_deinit);
    return XVTABLE_DEFAULT;
}

void XColorAxis_init(XColorAxis* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractAxis_init(&self->m_base);
    XClassSetVtable(self, XColorAxis);
    self->m_base.m_axisType = XAbstractAxis_AxisType_Color;
    self->m_base.m_min = 0.0;
    self->m_base.m_max = 1.0;
    self->m_tickCount = 5;
    self->m_size = 50.0;         /* Qt 默认色条厚度 */
    self->m_autoRange = true;
    /* 默认渐变：黑到白（Qt 未设渐变时的中性兜底）。 */
    self->m_gradient = XLinearGradient_create();
    if (self->m_gradient) {
        XGradient_setColorAt(&self->m_gradient->m_base, 0.0, 0xFF000000u);
        XGradient_setColorAt(&self->m_gradient->m_base, 1.0, 0xFFFFFFFFu);
    }
}

XColorAxis* XColorAxis_create_ex(XMemoryType memory)
{
    XColorAxis* self = (XColorAxis*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XColorAxis_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XColorAxis_deinit_impl(XColorAxis* self)
{
    if (!self) return;
    if (self->m_gradient) {
        XClassDelete(self->m_gradient);
        self->m_gradient = NULL;
    }
    XClassDeinit(&self->m_base);
}

/* ==================== 类型查询 ==================== */

int XColorAxis_type(const XColorAxis* self)
{
    (void)self;
    return XAbstractAxis_AxisType_Color;
}

/* ==================== 数值域 ==================== */

void XColorAxis_setRange(XColorAxis* self, double min, double max)
{
    bool minChanged;
    bool maxChanged;
    if (!self || min > max) return;
    minChanged = (self->m_base.m_min != min);
    maxChanged = (self->m_base.m_max != max);
    if (!minChanged && !maxChanged) return;
    self->m_base.m_min = min;
    self->m_base.m_max = max;
    if (minChanged)
        xcaxis_emitDouble(self, (size_t)XColorAxis_minChanged_signal, min);
    if (maxChanged)
        xcaxis_emitDouble(self, (size_t)XColorAxis_maxChanged_signal, max);
    xcaxis_emitRange(self, (size_t)XColorAxis_rangeChanged_signal, min, max);
}

void XColorAxis_setMin(XColorAxis* self, double min)
{
    if (!self) return;
    XColorAxis_setRange(self, min, self->m_base.m_max);
}

double XColorAxis_min(const XColorAxis* self)
{ return self ? self->m_base.m_min : 0.0; }

void XColorAxis_setMax(XColorAxis* self, double max)
{
    if (!self) return;
    XColorAxis_setRange(self, self->m_base.m_min, max);
}

double XColorAxis_max(const XColorAxis* self)
{ return self ? self->m_base.m_max : 0.0; }

/* ==================== 刻度 / 尺寸 ==================== */

void XColorAxis_setTickCount(XColorAxis* self, int count)
{
    if (!self) return;
    if (count < 2) count = 2;
    if (self->m_tickCount == count) return;
    self->m_tickCount = count;
    xcaxis_emitInt(self, (size_t)XColorAxis_tickCountChanged_signal, count);
}

int XColorAxis_tickCount(const XColorAxis* self)
{ return self ? self->m_tickCount : 0; }

void XColorAxis_setSize(XColorAxis* self, double size)
{
    if (!self) return;
    if (size < 0.0) size = 0.0;
    if (self->m_size == size) return;
    self->m_size = size;
    xcaxis_emitDouble(self, (size_t)XColorAxis_sizeChanged_signal, size);
}

double XColorAxis_size(const XColorAxis* self)
{ return self ? self->m_size : 0.0; }

void XColorAxis_setAutoRange(XColorAxis* self, bool autoRange)
{
    if (!self || self->m_autoRange == autoRange) return;
    self->m_autoRange = autoRange;
    xcaxis_emitBool(self, (size_t)XColorAxis_autoRangeChanged_signal,
                    autoRange);
}

bool XColorAxis_autoRange(const XColorAxis* self)
{ return self ? self->m_autoRange : false; }

/* ==================== 渐变 ==================== */

void XColorAxis_setGradient(XColorAxis* self, const XLinearGradient* gradient)
{
    if (!self) return;
    if (!gradient) {
        if (self->m_gradient) {
            XClassDelete(self->m_gradient);
            self->m_gradient = NULL;
        }
        xcaxis_emitInt(self, (size_t)XColorAxis_gradientChanged_signal, 0);
        return;
    }
    if (!self->m_gradient) {
        self->m_gradient = XLinearGradient_create();
        if (!self->m_gradient) return;
    }
    /* 对标 QColorAxis::setGradient 的按值语义：内部保存副本。 */
    XClassCopy((XClass*)self->m_gradient, (const XClass*)gradient);
    xcaxis_emitInt(self, (size_t)XColorAxis_gradientChanged_signal,
                   XGradient_stopCount(&self->m_gradient->m_base));
}

const XLinearGradient* XColorAxis_gradient_const(const XColorAxis* self)
{ return self ? self->m_gradient : NULL; }

const XLinearGradient* XColorAxis_gradient(const XColorAxis* self)
{ return XColorAxis_gradient_const(self); }

void XColorAxis_setGradientStops(XColorAxis* self, const double* positions,
                                 const uint32_t* colors, int count)
{
    int i;
    if (!self) return;
    if (!self->m_gradient) {
        self->m_gradient = XLinearGradient_create();
        if (!self->m_gradient) return;
    }
    XGradient_clearStops(&self->m_gradient->m_base);
    if (positions && colors && count > 0) {
        for (i = 0; i < count; ++i)
            XGradient_setColorAt(&self->m_gradient->m_base, positions[i],
                                 colors[i]);
    }
    xcaxis_emitInt(self, (size_t)XColorAxis_gradientChanged_signal,
                   XGradient_stopCount(&self->m_gradient->m_base));
}

int XColorAxis_gradientStopCount(const XColorAxis* self)
{
    if (!self || !self->m_gradient) return 0;
    return XGradient_stopCount(&self->m_gradient->m_base);
}

bool XColorAxis_gradientStopAt(const XColorAxis* self, int index,
                               double* position, uint32_t* color)
{
    XGradientStop st;
    if (!self || !self->m_gradient) return false;
    if (!XGradient_stopAt(&self->m_gradient->m_base, index, &st)) return false;
    if (position) *position = st.m_position;
    if (color) *color = st.m_color;
    return true;
}

uint32_t XColorAxis_colorAt(const XColorAxis* self, double pos)
{
    int i;
    const XGradient* g;
    if (!self || !self->m_gradient) return 0;
    g = &self->m_gradient->m_base;
    if (g->m_stopCount == 0) return 0;
    if (pos <= g->m_stops[0].m_position) return g->m_stops[0].m_color;
    if (pos >= g->m_stops[g->m_stopCount - 1].m_position)
        return g->m_stops[g->m_stopCount - 1].m_color;
    for (i = 0; i + 1 < g->m_stopCount; ++i) {
        double a = g->m_stops[i].m_position;
        double b = g->m_stops[i + 1].m_position;
        if (pos >= a && pos <= b) {
            double t = (b > a) ? (pos - a) / (b - a) : 0.0;
            return xcaxis_lerpArgb(g->m_stops[i].m_color,
                                   g->m_stops[i + 1].m_color, t);
        }
    }
    return g->m_stops[g->m_stopCount - 1].m_color;
}

bool XColorAxis_toPainterGradient(const XColorAxis* self,
                                  XPainterGradient* out)
{
    if (!self || !out || !self->m_gradient) return false;
    return XLinearGradient_toPainterGradient(self->m_gradient, out);
}

/* ==================== 信号 ==================== */

void* XColorAxis_minChanged_signal(XColorAxis* self, double min)
{
    xcaxis_emitDouble(self, (size_t)XColorAxis_minChanged_signal, min);
    return (void*)(size_t)XColorAxis_minChanged_signal;
}

void* XColorAxis_maxChanged_signal(XColorAxis* self, double max)
{
    xcaxis_emitDouble(self, (size_t)XColorAxis_maxChanged_signal, max);
    return (void*)(size_t)XColorAxis_maxChanged_signal;
}

void* XColorAxis_rangeChanged_signal(XColorAxis* self, double min, double max)
{
    xcaxis_emitRange(self, (size_t)XColorAxis_rangeChanged_signal, min, max);
    return (void*)(size_t)XColorAxis_rangeChanged_signal;
}

void* XColorAxis_tickCountChanged_signal(XColorAxis* self, int tickCount)
{
    xcaxis_emitInt(self, (size_t)XColorAxis_tickCountChanged_signal, tickCount);
    return (void*)(size_t)XColorAxis_tickCountChanged_signal;
}

void* XColorAxis_gradientChanged_signal(XColorAxis* self, int stopCount)
{
    xcaxis_emitInt(self, (size_t)XColorAxis_gradientChanged_signal, stopCount);
    return (void*)(size_t)XColorAxis_gradientChanged_signal;
}

void* XColorAxis_sizeChanged_signal(XColorAxis* self, double size)
{
    xcaxis_emitDouble(self, (size_t)XColorAxis_sizeChanged_signal, size);
    return (void*)(size_t)XColorAxis_sizeChanged_signal;
}

void* XColorAxis_autoRangeChanged_signal(XColorAxis* self, bool autoRange)
{
    xcaxis_emitBool(self, (size_t)XColorAxis_autoRangeChanged_signal,
                    autoRange);
    return (void*)(size_t)XColorAxis_autoRangeChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
