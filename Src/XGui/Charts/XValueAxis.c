/**
 * @file       XValueAxis.c
 * @brief      XValueAxis 数值轴实现（对标 QValueAxis）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XValueAxis.h"
#include "XMemory.h"

#include "XAlgorithm.h"
#include "XString.h"
#include "XVarList.h"
#include "XEventType.h"

#include <math.h>

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool vaxis_canEmit(const XValueAxis* self)
{
    return self && ((const XObject*)self)->m_signalSlot;
}

static void vaxis_emitDouble(XValueAxis* self, size_t signal, double v)
{
    XVarList* args;
    if (!vaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(double, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void vaxis_emitInt(XValueAxis* self, size_t signal, int v)
{
    XVarList* args;
    if (!vaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(int, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void vaxis_emitRange(XValueAxis* self, size_t signal,
                           double mi, double ma)
{
    XVarList* args;
    if (!vaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(double, mi), XVar(double, ma));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 类与对象生命周期 ==================== */

static void VXValueAxis_deinit(XValueAxis* self)
{
    if (!self) return;
    if (self->m_labelFormat) {
        XClassDelete(self->m_labelFormat);
        self->m_labelFormat = NULL;
    }
    XClass_Deinit_Parent(XAbstractAxis, &self->m_base);
}

XVtable* XValueAxis_class_init(void)
{
    /* 本类为组合式继承且不新增虚槽（头文件未定义虚表枚举）：
       按父类槽位容量建表，仅重载 EXClass_Deinit 释放标签格式。 */
    XVTABLE_INIT_DEFAULT_SIZE(XCLASS_VTABLE_GET_SIZE(XAbstractAxis))
    XCLASS_SET_CLASS_NAME_DEFAULT("XValueAxis");
    XVTABLE_INHERIT_XCLASS(XAbstractAxis);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXValueAxis_deinit);
    return XVTABLE_DEFAULT;
}

void XValueAxis_init(XValueAxis* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractAxis_init(&self->m_base);
    XClassSetVtable(self, XValueAxis);
    self->m_tickCount = 6;
    self->m_minorTickCount = 0;
    self->m_tickAnchor = 0.0;
    self->m_tickInterval = 0.0;
    self->m_tickType = XValueAxis_TickType_Fixed;
    self->m_labelFormat = XString_create_utf8("%g");
    self->m_base.m_axisType = XAbstractAxis_AxisType_Value;
}

XValueAxis* XValueAxis_create_ex(XMemoryType memory)
{
    XValueAxis* self = (XValueAxis*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XValueAxis_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XValueAxis_deinit_impl(XValueAxis* self)
{
    if (!self) return;
    if (self->m_labelFormat) {
        XClassDelete(self->m_labelFormat);
        self->m_labelFormat = NULL;
    }
    XClassDeinit(&self->m_base);
}

/* ==================== 类型查询 ==================== */

int XValueAxis_type(const XValueAxis* self)
{
    (void)self;
    return XAbstractAxis_AxisType_Value;
}

/* ==================== 范围 ==================== */

void XValueAxis_setRange(XValueAxis* self, double min, double max)
{
    double oldMin;
    double oldMax;
    if (!self) return;
    oldMin = self->m_base.m_min;
    oldMax = self->m_base.m_max;
    XAbstractAxis_setRange(&self->m_base, min, max);
    /* Qt：QValueAxis 在基类 rangeChanged 之外另发 minChanged/maxChanged。 */
    if (oldMin != self->m_base.m_min)
        vaxis_emitDouble(self, (size_t)XValueAxis_minChanged_signal,
                         self->m_base.m_min);
    if (oldMax != self->m_base.m_max)
        vaxis_emitDouble(self, (size_t)XValueAxis_maxChanged_signal,
                         self->m_base.m_max);
}

void XValueAxis_setMin(XValueAxis* self, double min)
{
    if (!self) return;
    XValueAxis_setRange(self, min, self->m_base.m_max);
}

void XValueAxis_setMax(XValueAxis* self, double max)
{
    if (!self) return;
    XValueAxis_setRange(self, self->m_base.m_min, max);
}

double XValueAxis_min(const XValueAxis* self)
{ return XAbstractAxis_min(self ? &self->m_base : NULL); }

double XValueAxis_max(const XValueAxis* self)
{ return XAbstractAxis_max(self ? &self->m_base : NULL); }

/* ==================== 刻度 ==================== */

void XValueAxis_setTickCount(XValueAxis* self, int count)
{
    if (!self || count < 2 || self->m_tickCount == count) return;
    self->m_tickCount = count;
    vaxis_emitInt(self, (size_t)XValueAxis_tickCountChanged_signal, count);
}

int XValueAxis_tickCount(const XValueAxis* self)
{ return self ? self->m_tickCount : 0; }

void XValueAxis_setMinorTickCount(XValueAxis* self, int count)
{
    if (!self || count < 0 || self->m_minorTickCount == count) return;
    self->m_minorTickCount = count;
    vaxis_emitInt(self, (size_t)XValueAxis_minorTickCountChanged_signal,
                  count);
}

int XValueAxis_minorTickCount(const XValueAxis* self)
{ return self ? self->m_minorTickCount : 0; }

void XValueAxis_setTickAnchor(XValueAxis* self, double anchor)
{
    if (!self || self->m_tickAnchor == anchor) return;
    self->m_tickAnchor = anchor;
    vaxis_emitDouble(self, (size_t)XValueAxis_tickAnchorChanged_signal,
                     anchor);
}

double XValueAxis_tickAnchor(const XValueAxis* self)
{ return self ? self->m_tickAnchor : 0.0; }

void XValueAxis_setTickInterval(XValueAxis* self, double interval)
{
    if (!self || self->m_tickInterval == interval) return;
    self->m_tickInterval = interval;
    vaxis_emitDouble(self, (size_t)XValueAxis_tickIntervalChanged_signal,
                     interval);
}

double XValueAxis_tickInterval(const XValueAxis* self)
{ return self ? self->m_tickInterval : 0.0; }

void XValueAxis_setTickType(XValueAxis* self, XValueAxis_TickType type)
{
    if (!self || self->m_tickType == (int)type) return;
    self->m_tickType = (int)type;
    vaxis_emitInt(self, (size_t)XValueAxis_tickTypeChanged_signal, (int)type);
}

XValueAxis_TickType XValueAxis_tickType(const XValueAxis* self)
{
    return self ? (XValueAxis_TickType)self->m_tickType
                : XValueAxis_TickType_Fixed;
}

/* ==================== 标签格式 ==================== */

void XValueAxis_setLabelFormat(XValueAxis* self, const XString* fmt)
{
    if (!self) return;
    if (!self->m_labelFormat) self->m_labelFormat = XString_create();
    if (!self->m_labelFormat) return;
    if (fmt)
        XString_assign(self->m_labelFormat, fmt);
    else
        XString_assign_utf8(self->m_labelFormat, "%g");
    if (vaxis_canEmit(self))
        vaxis_emitInt(self, (size_t)XValueAxis_labelFormatChanged_signal, 0);
}

void XValueAxis_setLabelFormat_utf8(XValueAxis* self, const char* fmt)
{
    XString* tmp = NULL;
    if (fmt) {
        tmp = XString_create_utf8(fmt);
        if (!tmp) return;
    }
    XValueAxis_setLabelFormat(self, tmp);
    if (tmp) XClassDelete(tmp);
}

const XString* XValueAxis_labelFormat(const XValueAxis* self)
{ return (self && self->m_labelFormat) ? self->m_labelFormat : NULL; }

const char* XValueAxis_labelFormat_2(const XValueAxis* self)
{
    const XString* s = XValueAxis_labelFormat(self);
    return s ? XString_toUtf8(s) : "%g";
}

/* ==================== 自定义刻度标签回调 ==================== */

void XValueAxis_setLabelTextCallback(XValueAxis* self, XValueAxisLabelTextFn fn,
                                     void* userData)
{
    if (!self) return;
    /* 回调/私有数据只影响渲染文本，不发信号（与 m_labelFormat 的
     * labelFormatChanged 不同：那是 Qt 契约的一部分）。视图在
     * xcv_axisLabel 里直接读字段，下一帧即生效。 */
    self->m_labelTextFn = fn;
    self->m_labelTextUserData = userData;
}

XValueAxisLabelTextFn XValueAxis_labelTextCallback(const XValueAxis* self)
{
    return self ? self->m_labelTextFn : NULL;
}

void* XValueAxis_labelTextCallbackData(const XValueAxis* self)
{
    return self ? self->m_labelTextUserData : NULL;
}

/* ==================== 自动刻度 ==================== */

/**
 * @brief 把 v 向上取到 1/2/5×10^n 档（对标 Qt niceNumber 的 ceiling 口径）。
 */
static double vaxis_niceCeil(double v)
{
    double expv;
    double f;
    double nf;
    int expi;
    if (v <= 0.0) return 0.0;
    expv = log10(v);
    expi = (int)floor(expv);
    f = v / pow(10.0, (double)expi);
    if (f <= 1.0)      nf = 1.0;
    else if (f <= 2.0) nf = 2.0;
    else if (f <= 5.0) nf = 5.0;
    else               nf = 10.0;
    return nf * pow(10.0, (double)expi);
}

void XValueAxis_applyNiceNumbers(XValueAxis* self)
{
    double minV;
    double maxV;
    double range;
    double step;
    double newMin;
    double newMax;
    int n;
    if (!self) return;
    minV = self->m_base.m_min;
    maxV = self->m_base.m_max;
    range = maxV - minV;
    if (range <= 0.0) return;
    step = vaxis_niceCeil(range / 5.0);
    if (step <= 0.0) return;
    newMin = floor(minV / step) * step;
    newMax = ceil(maxV / step) * step;
    n = (int)((newMax - newMin) / step) + 1;
    if (n < 2) n = 2;
    XValueAxis_setRange(self, newMin, newMax);
    XValueAxis_setTickCount(self, n);
}

/* ==================== 信号 ==================== */

void* XValueAxis_minChanged_signal(XValueAxis* self, double min)
{
    vaxis_emitDouble(self, (size_t)XValueAxis_minChanged_signal, min);
    return (void*)(size_t)XValueAxis_minChanged_signal;
}

void* XValueAxis_maxChanged_signal(XValueAxis* self, double max)
{
    vaxis_emitDouble(self, (size_t)XValueAxis_maxChanged_signal, max);
    return (void*)(size_t)XValueAxis_maxChanged_signal;
}

void* XValueAxis_rangeChanged_signal(XValueAxis* self, double min, double max)
{
    vaxis_emitRange(self, (size_t)XValueAxis_rangeChanged_signal, min, max);
    return (void*)(size_t)XValueAxis_rangeChanged_signal;
}

void* XValueAxis_tickCountChanged_signal(XValueAxis* self, int count)
{
    vaxis_emitInt(self, (size_t)XValueAxis_tickCountChanged_signal, count);
    return (void*)(size_t)XValueAxis_tickCountChanged_signal;
}

void* XValueAxis_minorTickCountChanged_signal(XValueAxis* self, int count)
{
    vaxis_emitInt(self, (size_t)XValueAxis_minorTickCountChanged_signal,
                  count);
    return (void*)(size_t)XValueAxis_minorTickCountChanged_signal;
}

void* XValueAxis_labelFormatChanged_signal(XValueAxis* self,
                                           const char* format)
{
    (void)format;
    if (vaxis_canEmit(self))
        vaxis_emitInt(self, (size_t)XValueAxis_labelFormatChanged_signal, 0);
    return (void*)(size_t)XValueAxis_labelFormatChanged_signal;
}

void* XValueAxis_tickIntervalChanged_signal(XValueAxis* self, double interval)
{
    vaxis_emitDouble(self, (size_t)XValueAxis_tickIntervalChanged_signal,
                     interval);
    return (void*)(size_t)XValueAxis_tickIntervalChanged_signal;
}

void* XValueAxis_tickAnchorChanged_signal(XValueAxis* self, double anchor)
{
    vaxis_emitDouble(self, (size_t)XValueAxis_tickAnchorChanged_signal,
                     anchor);
    return (void*)(size_t)XValueAxis_tickAnchorChanged_signal;
}

void* XValueAxis_tickTypeChanged_signal(XValueAxis* self, int type)
{
    vaxis_emitInt(self, (size_t)XValueAxis_tickTypeChanged_signal, type);
    return (void*)(size_t)XValueAxis_tickTypeChanged_signal;
}

/* ==================== UTF-8 兼容别名（_2，转发 _utf8 版本） ==================== */

void XValueAxis_setLabelFormat_2(XValueAxis* self, const char* fmt)
{ XValueAxis_setLabelFormat_utf8(self, fmt); }

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
