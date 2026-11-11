/**
 * @file       XDateTimeAxis.c
 * @brief      XDateTimeAxis 日期时间轴实现（对标 QDateTimeAxis）。
 * @details    范围以 int64 毫秒纪元存储（Qt 用 QDateTime，本库按值结构体
 *             对外、内部转毫秒，避免双精度丢精度）；标签格式沿用 Qt 的
 *             日期格式串语义。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XDateTimeAxis.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XString.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool xdtaxis_canEmit(const XDateTimeAxis* self)
{
    return self && ((const XObject*)&self->m_base)->m_signalSlot;
}

static void xdtaxis_emitI64(XDateTimeAxis* self, size_t signal, int64_t v)
{
    XVarList* args;
    if (!xdtaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(int64_t, v));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xdtaxis_emitInt(XDateTimeAxis* self, size_t signal, int v)
{
    XVarList* args;
    if (!xdtaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(int, v));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xdtaxis_emitStr(XDateTimeAxis* self, size_t signal, const char* s)
{
    XVarList* args;
    if (!xdtaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(const char*, s));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xdtaxis_emitRange(XDateTimeAxis* self, size_t signal,
                              int64_t mi, int64_t ma)
{
    XVarList* args;
    if (!xdtaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(int64_t, mi), XVar(int64_t, ma));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 类与对象生命周期 ==================== */

static void VXDateTimeAxis_deinit(XDateTimeAxis* self)
{
    if (!self) return;
    if (self->m_format) {
        XClassDelete(self->m_format);
        self->m_format = NULL;
    }
    XClass_Deinit_Parent(XAbstractAxis, &self->m_base);
}

XVtable* XDateTimeAxis_class_init(void)
{
    XVTABLE_INIT_DEFAULT_SIZE(XCLASS_VTABLE_GET_SIZE(XAbstractAxis))
    XCLASS_SET_CLASS_NAME_DEFAULT("XDateTimeAxis");
    XVTABLE_INHERIT_XCLASS(XAbstractAxis);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXDateTimeAxis_deinit);
    return XVTABLE_DEFAULT;
}

void XDateTimeAxis_init(XDateTimeAxis* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractAxis_init(&self->m_base);
    XClassSetVtable(self, XDateTimeAxis);
    self->m_minMSecs = 0;
    self->m_maxMSecs = 0;
    self->m_tickCount = 5;   /* Qt 默认 5 */
    self->m_format = XString_create_utf8("dd-MM-yyyy");
    self->m_base.m_axisType = XAbstractAxis_AxisType_DateTime;
    /* 基类数值范围同步为毫秒值，供通用布局代码读取。 */
    self->m_base.m_min = 0.0;
    self->m_base.m_max = 0.0;
}

XDateTimeAxis* XDateTimeAxis_create_ex(XMemoryType memory)
{
    XDateTimeAxis* self =
        (XDateTimeAxis*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XDateTimeAxis_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XDateTimeAxis_deinit_impl(XDateTimeAxis* self)
{
    if (!self) return;
    if (self->m_format) {
        XClassDelete(self->m_format);
        self->m_format = NULL;
    }
    XClassDeinit(&self->m_base);
}

/* ==================== 类型查询 ==================== */

int XDateTimeAxis_type(const XDateTimeAxis* self)
{
    (void)self;
    return XAbstractAxis_AxisType_DateTime;
}

/* ==================== 范围 ==================== */

void XDateTimeAxis_setMin(XDateTimeAxis* self, XDateTime min)
{
    int64_t v;
    if (!self) return;
    v = XDateTime_toMSecsSinceEpoch(&min);
    XDateTimeAxis_setRangeMSecs(self, v, self->m_maxMSecs);
}

XDateTime XDateTimeAxis_min(const XDateTimeAxis* self)
{
    XDateTime dt;
    XMemset(&dt, 0, sizeof(dt));
    if (!self) return dt;
    XDateTime_setMSecsSinceEpoch(&dt, self->m_minMSecs);
    return dt;
}

void XDateTimeAxis_setMax(XDateTimeAxis* self, XDateTime max)
{
    int64_t v;
    if (!self) return;
    v = XDateTime_toMSecsSinceEpoch(&max);
    XDateTimeAxis_setRangeMSecs(self, self->m_minMSecs, v);
}

XDateTime XDateTimeAxis_max(const XDateTimeAxis* self)
{
    XDateTime dt;
    XMemset(&dt, 0, sizeof(dt));
    if (!self) return dt;
    XDateTime_setMSecsSinceEpoch(&dt, self->m_maxMSecs);
    return dt;
}

void XDateTimeAxis_setRange(XDateTimeAxis* self, XDateTime min, XDateTime max)
{
    if (!self) return;
    XDateTimeAxis_setRangeMSecs(self, XDateTime_toMSecsSinceEpoch(&min),
                                XDateTime_toMSecsSinceEpoch(&max));
}

void XDateTimeAxis_setRangeMSecs(XDateTimeAxis* self, int64_t minMSecs,
                                 int64_t maxMSecs)
{
    bool minChanged;
    bool maxChanged;
    if (!self || minMSecs > maxMSecs) return;
    minChanged = (self->m_minMSecs != minMSecs);
    maxChanged = (self->m_maxMSecs != maxMSecs);
    if (!minChanged && !maxChanged) return;
    self->m_minMSecs = minMSecs;
    self->m_maxMSecs = maxMSecs;
    /* 基类数值域同步（通用布局/映射代码读 m_min/m_max）。 */
    self->m_base.m_min = (double)minMSecs;
    self->m_base.m_max = (double)maxMSecs;
    if (minChanged)
        xdtaxis_emitI64(self, (size_t)XDateTimeAxis_minChanged_signal,
                        minMSecs);
    if (maxChanged)
        xdtaxis_emitI64(self, (size_t)XDateTimeAxis_maxChanged_signal,
                        maxMSecs);
    xdtaxis_emitRange(self, (size_t)XDateTimeAxis_rangeChanged_signal,
                      minMSecs, maxMSecs);
}

int64_t XDateTimeAxis_minMSecs(const XDateTimeAxis* self)
{ return self ? self->m_minMSecs : 0; }

int64_t XDateTimeAxis_maxMSecs(const XDateTimeAxis* self)
{ return self ? self->m_maxMSecs : 0; }

/* ==================== 标签格式 ==================== */

void XDateTimeAxis_setFormat(XDateTimeAxis* self, const XString* format)
{
    const char* s;
    if (!self) return;
    if (!self->m_format) self->m_format = XString_create();
    if (!self->m_format) return;
    if (format)
        XString_assign(self->m_format, format);
    else
        XString_assign_utf8(self->m_format, "dd-MM-yyyy");
    s = XString_toUtf8(self->m_format);
    xdtaxis_emitStr(self, (size_t)XDateTimeAxis_formatChanged_signal, s);
}

void XDateTimeAxis_setFormat_utf8(XDateTimeAxis* self, const char* format)
{
    XString* tmp = NULL;
    if (format) {
        tmp = XString_create_utf8(format);
        if (!tmp) return;
    }
    XDateTimeAxis_setFormat(self, tmp);
    if (tmp) XClassDelete(tmp);
}

void XDateTimeAxis_setFormat_2(XDateTimeAxis* self, const char* format)
{ XDateTimeAxis_setFormat_utf8(self, format); }

const XString* XDateTimeAxis_format_const(const XDateTimeAxis* self)
{ return (self && self->m_format) ? self->m_format : NULL; }

const XString* XDateTimeAxis_format(const XDateTimeAxis* self)
{ return XDateTimeAxis_format_const(self); }

const char* XDateTimeAxis_format_toUtf8(const XDateTimeAxis* self)
{
    const XString* s = XDateTimeAxis_format_const(self);
    return s ? XString_toUtf8(s) : "";
}

/* ==================== 刻度数 ==================== */

void XDateTimeAxis_setTickCount(XDateTimeAxis* self, int count)
{
    if (!self) return;
    if (count < 2) count = 2;
    if (self->m_tickCount == count) return;
    self->m_tickCount = count;
    xdtaxis_emitInt(self, (size_t)XDateTimeAxis_tickCountChanged_signal, count);
}

int XDateTimeAxis_tickCount(const XDateTimeAxis* self)
{ return self ? self->m_tickCount : 0; }

/* ==================== 信号 ==================== */

void* XDateTimeAxis_minChanged_signal(XDateTimeAxis* self, int64_t minMSecs)
{
    xdtaxis_emitI64(self, (size_t)XDateTimeAxis_minChanged_signal, minMSecs);
    return (void*)(size_t)XDateTimeAxis_minChanged_signal;
}

void* XDateTimeAxis_maxChanged_signal(XDateTimeAxis* self, int64_t maxMSecs)
{
    xdtaxis_emitI64(self, (size_t)XDateTimeAxis_maxChanged_signal, maxMSecs);
    return (void*)(size_t)XDateTimeAxis_maxChanged_signal;
}

void* XDateTimeAxis_rangeChanged_signal(XDateTimeAxis* self, int64_t minMSecs,
                                        int64_t maxMSecs)
{
    xdtaxis_emitRange(self, (size_t)XDateTimeAxis_rangeChanged_signal,
                      minMSecs, maxMSecs);
    return (void*)(size_t)XDateTimeAxis_rangeChanged_signal;
}

void* XDateTimeAxis_formatChanged_signal(XDateTimeAxis* self, const char* format)
{
    xdtaxis_emitStr(self, (size_t)XDateTimeAxis_formatChanged_signal, format);
    return (void*)(size_t)XDateTimeAxis_formatChanged_signal;
}

void* XDateTimeAxis_tickCountChanged_signal(XDateTimeAxis* self, int tickCount)
{
    xdtaxis_emitInt(self, (size_t)XDateTimeAxis_tickCountChanged_signal,
                    tickCount);
    return (void*)(size_t)XDateTimeAxis_tickCountChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
