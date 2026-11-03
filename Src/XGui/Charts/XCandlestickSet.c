/**
 * @file       XCandlestickSet.c
 * @brief      XCandlestickSet K 线数据组实现（对标 QCandlestickSet）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XCandlestickSet.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool xcsv_canEmit(const XCandlestickSet* self)
{
    return self && ((const XObject*)self)->m_signalSlot;
}

static void xcsv_emitVoid(XCandlestickSet* self, size_t signal)
{
    XVarList* args;
    int dummy = 0;
    if (!xcsv_canEmit(self)) return;
    args = XVarList_Create(XVar(int, dummy));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcsv_emitBool(XCandlestickSet* self, size_t signal, bool v)
{
    XVarList* args;
    if (!xcsv_canEmit(self)) return;
    args = XVarList_Create(XVar(bool, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcsv_emitDouble(XCandlestickSet* self, size_t signal, double v)
{
    XVarList* args;
    if (!xcsv_canEmit(self)) return;
    args = XVarList_Create(XVar(double, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcsv_emitU32(XCandlestickSet* self, size_t signal, uint32_t v)
{
    XVarList* args;
    if (!xcsv_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcsv_emitPen(XCandlestickSet* self, size_t signal, uint32_t color,
                         double width)
{
    XVarList* args;
    if (!xcsv_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, color), XVar(double, width));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 虚函数实现 ==================== */

static void VXCandlestickSet_deinit(XCandlestickSet* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XCandlestickSet_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XCandlestickSet)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCandlestickSet_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XCandlestickSet_init(XCandlestickSet* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XCandlestickSet);
    self->m_penWidth = 1.0;
}

void XCandlestickSet_init_ex(XCandlestickSet* self, double timestamp)
{
    XCandlestickSet_init(self);
    if (self) self->m_timestamp = timestamp;
}

void XCandlestickSet_init_ex_5(XCandlestickSet* self, double open, double high,
                               double low, double close, double timestamp)
{
    XCandlestickSet_init(self);
    if (!self) return;
    self->m_open = open;
    self->m_high = high;
    self->m_low = low;
    self->m_close = close;
    self->m_timestamp = timestamp;
}

XCandlestickSet* XCandlestickSet_create_ex(XMemoryType memory, double timestamp)
{
    XCandlestickSet* self =
        (XCandlestickSet*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XCandlestickSet_init_ex(self, timestamp);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 属性 ==================== */

void XCandlestickSet_setTimestamp(XCandlestickSet* self, double timestamp)
{
    if (!self || self->m_timestamp == timestamp) return;
    self->m_timestamp = timestamp;
    xcsv_emitDouble(self, (size_t)XCandlestickSet_timestampChanged_signal,
                    timestamp);
}

double XCandlestickSet_timestamp(const XCandlestickSet* self)
{ return self ? self->m_timestamp : 0.0; }

void XCandlestickSet_setOpen(XCandlestickSet* self, double open)
{
    if (!self || self->m_open == open) return;
    self->m_open = open;
    xcsv_emitDouble(self, (size_t)XCandlestickSet_openChanged_signal, open);
}

double XCandlestickSet_open(const XCandlestickSet* self)
{ return self ? self->m_open : 0.0; }

void XCandlestickSet_setHigh(XCandlestickSet* self, double high)
{
    if (!self || self->m_high == high) return;
    self->m_high = high;
    xcsv_emitDouble(self, (size_t)XCandlestickSet_highChanged_signal, high);
}

double XCandlestickSet_high(const XCandlestickSet* self)
{ return self ? self->m_high : 0.0; }

void XCandlestickSet_setLow(XCandlestickSet* self, double low)
{
    if (!self || self->m_low == low) return;
    self->m_low = low;
    xcsv_emitDouble(self, (size_t)XCandlestickSet_lowChanged_signal, low);
}

double XCandlestickSet_low(const XCandlestickSet* self)
{ return self ? self->m_low : 0.0; }

void XCandlestickSet_setClose(XCandlestickSet* self, double close)
{
    if (!self || self->m_close == close) return;
    self->m_close = close;
    xcsv_emitDouble(self, (size_t)XCandlestickSet_closeChanged_signal, close);
}

double XCandlestickSet_close(const XCandlestickSet* self)
{ return self ? self->m_close : 0.0; }

/* ==================== 画笔 / 画刷 ==================== */

void XCandlestickSet_setPen(XCandlestickSet* self, uint32_t color, double width)
{
    if (!self) return;
    if (width < 0.0) width = 0.0;
    if (self->m_penColor == color && self->m_penWidth == width) return;
    self->m_penColor = color;
    self->m_penWidth = width;
    xcsv_emitPen(self, (size_t)XCandlestickSet_penChanged_signal, color, width);
}

void XCandlestickSet_pen(const XCandlestickSet* self, uint32_t* color,
                         double* width)
{
    if (!self) return;
    if (color) *color = self->m_penColor;
    if (width) *width = self->m_penWidth;
}

void XCandlestickSet_setBrush(XCandlestickSet* self, uint32_t color)
{
    if (!self || self->m_brushColor == color) return;
    self->m_brushColor = color;
    xcsv_emitU32(self, (size_t)XCandlestickSet_brushChanged_signal, color);
}

uint32_t XCandlestickSet_brush(const XCandlestickSet* self)
{ return self ? self->m_brushColor : 0; }

/* ==================== 信号 ==================== */

void* XCandlestickSet_clicked_signal(XCandlestickSet* self)
{
    xcsv_emitVoid(self, (size_t)XCandlestickSet_clicked_signal);
    return (void*)(size_t)XCandlestickSet_clicked_signal;
}

void* XCandlestickSet_hovered_signal(XCandlestickSet* self, bool status)
{
    xcsv_emitBool(self, (size_t)XCandlestickSet_hovered_signal, status);
    return (void*)(size_t)XCandlestickSet_hovered_signal;
}

void* XCandlestickSet_pressed_signal(XCandlestickSet* self)
{
    xcsv_emitVoid(self, (size_t)XCandlestickSet_pressed_signal);
    return (void*)(size_t)XCandlestickSet_pressed_signal;
}

void* XCandlestickSet_released_signal(XCandlestickSet* self)
{
    xcsv_emitVoid(self, (size_t)XCandlestickSet_released_signal);
    return (void*)(size_t)XCandlestickSet_released_signal;
}

void* XCandlestickSet_doubleClicked_signal(XCandlestickSet* self)
{
    xcsv_emitVoid(self, (size_t)XCandlestickSet_doubleClicked_signal);
    return (void*)(size_t)XCandlestickSet_doubleClicked_signal;
}

void* XCandlestickSet_timestampChanged_signal(XCandlestickSet* self,
                                              double timestamp)
{
    xcsv_emitDouble(self, (size_t)XCandlestickSet_timestampChanged_signal,
                    timestamp);
    return (void*)(size_t)XCandlestickSet_timestampChanged_signal;
}

void* XCandlestickSet_openChanged_signal(XCandlestickSet* self, double open)
{
    xcsv_emitDouble(self, (size_t)XCandlestickSet_openChanged_signal, open);
    return (void*)(size_t)XCandlestickSet_openChanged_signal;
}

void* XCandlestickSet_highChanged_signal(XCandlestickSet* self, double high)
{
    xcsv_emitDouble(self, (size_t)XCandlestickSet_highChanged_signal, high);
    return (void*)(size_t)XCandlestickSet_highChanged_signal;
}

void* XCandlestickSet_lowChanged_signal(XCandlestickSet* self, double low)
{
    xcsv_emitDouble(self, (size_t)XCandlestickSet_lowChanged_signal, low);
    return (void*)(size_t)XCandlestickSet_lowChanged_signal;
}

void* XCandlestickSet_closeChanged_signal(XCandlestickSet* self, double close)
{
    xcsv_emitDouble(self, (size_t)XCandlestickSet_closeChanged_signal, close);
    return (void*)(size_t)XCandlestickSet_closeChanged_signal;
}

void* XCandlestickSet_brushChanged_signal(XCandlestickSet* self, uint32_t color)
{
    xcsv_emitU32(self, (size_t)XCandlestickSet_brushChanged_signal, color);
    return (void*)(size_t)XCandlestickSet_brushChanged_signal;
}

void* XCandlestickSet_penChanged_signal(XCandlestickSet* self, uint32_t color,
                                        double width)
{
    xcsv_emitPen(self, (size_t)XCandlestickSet_penChanged_signal, color, width);
    return (void*)(size_t)XCandlestickSet_penChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
