/**
 * @file       XHCandlestickModelMapper.c
 * @brief      XHCandlestickModelMapper 实现（对标 QHCandlestickModelMapper）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XHCandlestickModelMapper.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static void xhcandlestickmodelmapper_emitVoid(XHCandlestickModelMapper* self, size_t signal)
{
    XVarList* args;
    int dummy = 0;
    if (!self || !((const XObject*)self)->m_signalSlot) return;
    args = XVarList_Create(XVar(int, dummy));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 虚函数实现 ==================== */

static void VXHCandlestickModelMapper_deinit(XHCandlestickModelMapper* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XCandlestickModelMapper, &self->m_base);
}

XVtable* XHCandlestickModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHCandlestickModelMapper)
    XVTABLE_INHERIT_XCLASS(XCandlestickModelMapper);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHCandlestickModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XHCandlestickModelMapper_init(XHCandlestickModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XCandlestickModelMapper_init(&self->m_base);
    XClassSetVtable(self, XHCandlestickModelMapper);
    /* 方向在构造时固定，与 Qt 具体子类一致。 */
    self->m_base.m_orientation = XModelMapper_Orientation_Horizontal;
}

XHCandlestickModelMapper* XHCandlestickModelMapper_create_ex(XMemoryType memory)
{
    XHCandlestickModelMapper* self = (XHCandlestickModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XHCandlestickModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 专有参数 ==================== */
void XHCandlestickModelMapper_setTimestampColumn(XHCandlestickModelMapper* self, int timestampColumn)
{
    if (!self || self->m_base.m_timestampSection == timestampColumn) return;
    self->m_base.m_timestampSection = timestampColumn;
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_timestampColumnChanged_signal);
}

int XHCandlestickModelMapper_TimestampColumn(const XHCandlestickModelMapper* self)
{ return self ? self->m_base.m_timestampSection : -1; }

void XHCandlestickModelMapper_setOpenColumn(XHCandlestickModelMapper* self, int openColumn)
{
    if (!self || self->m_base.m_openSection == openColumn) return;
    self->m_base.m_openSection = openColumn;
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_openColumnChanged_signal);
}

int XHCandlestickModelMapper_OpenColumn(const XHCandlestickModelMapper* self)
{ return self ? self->m_base.m_openSection : -1; }

void XHCandlestickModelMapper_setHighColumn(XHCandlestickModelMapper* self, int highColumn)
{
    if (!self || self->m_base.m_highSection == highColumn) return;
    self->m_base.m_highSection = highColumn;
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_highColumnChanged_signal);
}

int XHCandlestickModelMapper_HighColumn(const XHCandlestickModelMapper* self)
{ return self ? self->m_base.m_highSection : -1; }

void XHCandlestickModelMapper_setLowColumn(XHCandlestickModelMapper* self, int lowColumn)
{
    if (!self || self->m_base.m_lowSection == lowColumn) return;
    self->m_base.m_lowSection = lowColumn;
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_lowColumnChanged_signal);
}

int XHCandlestickModelMapper_LowColumn(const XHCandlestickModelMapper* self)
{ return self ? self->m_base.m_lowSection : -1; }

void XHCandlestickModelMapper_setCloseColumn(XHCandlestickModelMapper* self, int closeColumn)
{
    if (!self || self->m_base.m_closeSection == closeColumn) return;
    self->m_base.m_closeSection = closeColumn;
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_closeColumnChanged_signal);
}

int XHCandlestickModelMapper_CloseColumn(const XHCandlestickModelMapper* self)
{ return self ? self->m_base.m_closeSection : -1; }

void XHCandlestickModelMapper_setFirstSetRow(XHCandlestickModelMapper* self, int firstSetRow)
{
    if (!self || self->m_base.m_firstSetSection == firstSetRow) return;
    self->m_base.m_firstSetSection = firstSetRow;
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_firstSetRowChanged_signal);
}

int XHCandlestickModelMapper_FirstSetRow(const XHCandlestickModelMapper* self)
{ return self ? self->m_base.m_firstSetSection : -1; }

void XHCandlestickModelMapper_setLastSetRow(XHCandlestickModelMapper* self, int lastSetRow)
{
    if (!self || self->m_base.m_lastSetSection == lastSetRow) return;
    self->m_base.m_lastSetSection = lastSetRow;
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_lastSetRowChanged_signal);
}

int XHCandlestickModelMapper_LastSetRow(const XHCandlestickModelMapper* self)
{ return self ? self->m_base.m_lastSetSection : -1; }

/* ==================== 信号 ==================== */

void* XHCandlestickModelMapper_timestampColumnChanged_signal(XHCandlestickModelMapper* self)
{
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_timestampColumnChanged_signal);
    return (void*)(size_t)XHCandlestickModelMapper_timestampColumnChanged_signal;
}

void* XHCandlestickModelMapper_openColumnChanged_signal(XHCandlestickModelMapper* self)
{
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_openColumnChanged_signal);
    return (void*)(size_t)XHCandlestickModelMapper_openColumnChanged_signal;
}

void* XHCandlestickModelMapper_highColumnChanged_signal(XHCandlestickModelMapper* self)
{
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_highColumnChanged_signal);
    return (void*)(size_t)XHCandlestickModelMapper_highColumnChanged_signal;
}

void* XHCandlestickModelMapper_lowColumnChanged_signal(XHCandlestickModelMapper* self)
{
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_lowColumnChanged_signal);
    return (void*)(size_t)XHCandlestickModelMapper_lowColumnChanged_signal;
}

void* XHCandlestickModelMapper_closeColumnChanged_signal(XHCandlestickModelMapper* self)
{
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_closeColumnChanged_signal);
    return (void*)(size_t)XHCandlestickModelMapper_closeColumnChanged_signal;
}

void* XHCandlestickModelMapper_firstSetRowChanged_signal(XHCandlestickModelMapper* self)
{
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_firstSetRowChanged_signal);
    return (void*)(size_t)XHCandlestickModelMapper_firstSetRowChanged_signal;
}

void* XHCandlestickModelMapper_lastSetRowChanged_signal(XHCandlestickModelMapper* self)
{
    xhcandlestickmodelmapper_emitVoid(self, (size_t)XHCandlestickModelMapper_lastSetRowChanged_signal);
    return (void*)(size_t)XHCandlestickModelMapper_lastSetRowChanged_signal;
}


#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
