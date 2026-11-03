/**
 * @file       XLogValueAxis.c
 * @brief      XLogValueAxis 对数数值轴实现（对标 QLogValueAxis）。
 * @details    范围须为正且不跨越 0；tickCount 为只读派生值，按
 *             log_base(range)+1 计算（与 Qt 的只读语义一致）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XLogValueAxis.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XString.h"
#include "XVarList.h"
#include "XEventType.h"

#include <math.h>

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool xlogaxis_canEmit(const XLogValueAxis* self)
{
    return self && ((const XObject*)&self->m_base)->m_signalSlot;
}

static void xlogaxis_emitDouble(XLogValueAxis* self, size_t signal, double v)
{
    XVarList* args;
    if (!xlogaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(double, v));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xlogaxis_emitInt(XLogValueAxis* self, size_t signal, int v)
{
    XVarList* args;
    if (!xlogaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(int, v));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xlogaxis_emitStr(XLogValueAxis* self, size_t signal, const char* s)
{
    XVarList* args;
    if (!xlogaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(const char*, s));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xlogaxis_emitRange(XLogValueAxis* self, size_t signal,
                               double mi, double ma)
{
    XVarList* args;
    if (!xlogaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(double, mi), XVar(double, ma));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 内部助手 ==================== */

/** @brief 按范围与底数推导主刻度数（Qt 只读派生口径）。 */
static int xlogaxis_computeTickCount(const XLogValueAxis* self)
{
    double lo;
    double hi;
    double b;
    int n;
    if (!self) return 0;
    lo = self->m_base.m_min;
    hi = self->m_base.m_max;
    b = self->m_baseValue;
    if (lo <= 0.0 || hi <= 0.0 || hi < lo || b <= 1.0) return 0;
    n = (int)floor(log(hi / lo) / log(b)) + 1;
    if (n < 1) n = 1;
    return n;
}

/* ==================== 类与对象生命周期 ==================== */

static void VXLogValueAxis_deinit(XLogValueAxis* self)
{
    if (!self) return;
    if (self->m_labelFormat) {
        XClassDelete(self->m_labelFormat);
        self->m_labelFormat = NULL;
    }
    XClass_Deinit_Parent(XAbstractAxis, &self->m_base);
}

XVtable* XLogValueAxis_class_init(void)
{
    XVTABLE_INIT_DEFAULT_SIZE(XCLASS_VTABLE_GET_SIZE(XAbstractAxis))
    XVTABLE_INHERIT_XCLASS(XAbstractAxis);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLogValueAxis_deinit);
    return XVTABLE_DEFAULT;
}

void XLogValueAxis_init(XLogValueAxis* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractAxis_init(&self->m_base);
    XClassSetVtable(self, XLogValueAxis);
    /* Qt 默认范围 [1,10]、底数 10。 */
    self->m_base.m_min = 1.0;
    self->m_base.m_max = 10.0;
    self->m_baseValue = 10.0;
    self->m_minorTickCount = 0;
    self->m_labelFormat = XString_create_utf8("%g");
    self->m_base.m_axisType = XAbstractAxis_AxisType_LogValue;
    self->m_tickCount = xlogaxis_computeTickCount(self);
}

XLogValueAxis* XLogValueAxis_create_ex(XMemoryType memory)
{
    XLogValueAxis* self =
        (XLogValueAxis*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XLogValueAxis_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XLogValueAxis_deinit_impl(XLogValueAxis* self)
{
    if (!self) return;
    if (self->m_labelFormat) {
        XClassDelete(self->m_labelFormat);
        self->m_labelFormat = NULL;
    }
    XClassDeinit(&self->m_base);
}

/* ==================== 类型查询 ==================== */

int XLogValueAxis_type(const XLogValueAxis* self)
{
    (void)self;
    return XAbstractAxis_AxisType_LogValue;
}

/* ==================== 范围 ==================== */

void XLogValueAxis_setRange(XLogValueAxis* self, double min, double max)
{
    int oldTicks;
    bool minChanged;
    bool maxChanged;
    if (!self) return;
    /* Qt 约束：对数轴范围必须为正，且不能跨越 0。 */
    if (min <= 0.0 || max <= 0.0 || min > max) return;
    minChanged = (self->m_base.m_min != min);
    maxChanged = (self->m_base.m_max != max);
    if (!minChanged && !maxChanged) return;
    oldTicks = self->m_tickCount;
    self->m_base.m_min = min;
    self->m_base.m_max = max;
    self->m_tickCount = xlogaxis_computeTickCount(self);
    if (minChanged)
        xlogaxis_emitDouble(self, (size_t)XLogValueAxis_minChanged_signal, min);
    if (maxChanged)
        xlogaxis_emitDouble(self, (size_t)XLogValueAxis_maxChanged_signal, max);
    xlogaxis_emitRange(self, (size_t)XLogValueAxis_rangeChanged_signal,
                       min, max);
    if (oldTicks != self->m_tickCount)
        xlogaxis_emitInt(self, (size_t)XLogValueAxis_tickCountChanged_signal,
                         self->m_tickCount);
}

void XLogValueAxis_setMin(XLogValueAxis* self, double min)
{
    if (!self) return;
    XLogValueAxis_setRange(self, min, self->m_base.m_max);
}

void XLogValueAxis_setMax(XLogValueAxis* self, double max)
{
    if (!self) return;
    XLogValueAxis_setRange(self, self->m_base.m_min, max);
}

double XLogValueAxis_min(const XLogValueAxis* self)
{ return self ? self->m_base.m_min : 0.0; }

double XLogValueAxis_max(const XLogValueAxis* self)
{ return self ? self->m_base.m_max : 0.0; }

/* ==================== 标签格式 ==================== */

void XLogValueAxis_setLabelFormat(XLogValueAxis* self, const XString* format)
{
    const char* s;
    if (!self) return;
    if (!self->m_labelFormat) self->m_labelFormat = XString_create();
    if (!self->m_labelFormat) return;
    if (format)
        XString_assign(self->m_labelFormat, format);
    else
        XString_assign_utf8(self->m_labelFormat, "%g");
    s = XString_toUtf8(self->m_labelFormat);
    xlogaxis_emitStr(self, (size_t)XLogValueAxis_labelFormatChanged_signal, s);
}

void XLogValueAxis_setLabelFormat_utf8(XLogValueAxis* self, const char* format)
{
    XString* tmp = NULL;
    if (format) {
        tmp = XString_create_utf8(format);
        if (!tmp) return;
    }
    XLogValueAxis_setLabelFormat(self, tmp);
    if (tmp) XClassDelete(tmp);
}

void XLogValueAxis_setLabelFormat_2(XLogValueAxis* self, const char* format)
{ XLogValueAxis_setLabelFormat_utf8(self, format); }

const XString* XLogValueAxis_labelFormat_const(const XLogValueAxis* self)
{ return (self && self->m_labelFormat) ? self->m_labelFormat : NULL; }

const XString* XLogValueAxis_labelFormat(const XLogValueAxis* self)
{ return XLogValueAxis_labelFormat_const(self); }

const char* XLogValueAxis_labelFormat_toUtf8(const XLogValueAxis* self)
{
    const XString* s = XLogValueAxis_labelFormat_const(self);
    return s ? XString_toUtf8(s) : "%g";
}

/* ==================== 底数 ==================== */

void XLogValueAxis_setBase(XLogValueAxis* self, double base)
{
    int oldTicks;
    if (!self) return;
    /* Qt 约束：底数必须 >0 且 !=1。 */
    if (base <= 0.0 || base == 1.0) return;
    if (self->m_baseValue == base) return;
    oldTicks = self->m_tickCount;
    self->m_baseValue = base;
    self->m_tickCount = xlogaxis_computeTickCount(self);
    xlogaxis_emitDouble(self, (size_t)XLogValueAxis_baseChanged_signal, base);
    if (oldTicks != self->m_tickCount)
        xlogaxis_emitInt(self, (size_t)XLogValueAxis_tickCountChanged_signal,
                         self->m_tickCount);
}

double XLogValueAxis_base(const XLogValueAxis* self)
{ return self ? self->m_baseValue : 0.0; }

/* ==================== 刻度 ==================== */

int XLogValueAxis_tickCount(const XLogValueAxis* self)
{ return self ? self->m_tickCount : 0; }

void XLogValueAxis_setMinorTickCount(XLogValueAxis* self, int minorTickCount)
{
    if (!self) return;
    if (minorTickCount < 0) minorTickCount = 0;
    if (self->m_minorTickCount == minorTickCount) return;
    self->m_minorTickCount = minorTickCount;
    xlogaxis_emitInt(self,
                     (size_t)XLogValueAxis_minorTickCountChanged_signal,
                     minorTickCount);
}

int XLogValueAxis_minorTickCount(const XLogValueAxis* self)
{ return self ? self->m_minorTickCount : 0; }

/* ==================== 信号 ==================== */

void* XLogValueAxis_minChanged_signal(XLogValueAxis* self, double min)
{
    xlogaxis_emitDouble(self, (size_t)XLogValueAxis_minChanged_signal, min);
    return (void*)(size_t)XLogValueAxis_minChanged_signal;
}

void* XLogValueAxis_maxChanged_signal(XLogValueAxis* self, double max)
{
    xlogaxis_emitDouble(self, (size_t)XLogValueAxis_maxChanged_signal, max);
    return (void*)(size_t)XLogValueAxis_maxChanged_signal;
}

void* XLogValueAxis_rangeChanged_signal(XLogValueAxis* self, double min,
                                        double max)
{
    xlogaxis_emitRange(self, (size_t)XLogValueAxis_rangeChanged_signal,
                       min, max);
    return (void*)(size_t)XLogValueAxis_rangeChanged_signal;
}

void* XLogValueAxis_labelFormatChanged_signal(XLogValueAxis* self,
                                              const char* format)
{
    xlogaxis_emitStr(self, (size_t)XLogValueAxis_labelFormatChanged_signal,
                     format);
    return (void*)(size_t)XLogValueAxis_labelFormatChanged_signal;
}

void* XLogValueAxis_baseChanged_signal(XLogValueAxis* self, double base)
{
    xlogaxis_emitDouble(self, (size_t)XLogValueAxis_baseChanged_signal, base);
    return (void*)(size_t)XLogValueAxis_baseChanged_signal;
}

void* XLogValueAxis_tickCountChanged_signal(XLogValueAxis* self, int tickCount)
{
    xlogaxis_emitInt(self, (size_t)XLogValueAxis_tickCountChanged_signal,
                     tickCount);
    return (void*)(size_t)XLogValueAxis_tickCountChanged_signal;
}

void* XLogValueAxis_minorTickCountChanged_signal(XLogValueAxis* self,
                                                 int minorTickCount)
{
    xlogaxis_emitInt(self,
                     (size_t)XLogValueAxis_minorTickCountChanged_signal,
                     minorTickCount);
    return (void*)(size_t)XLogValueAxis_minorTickCountChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
