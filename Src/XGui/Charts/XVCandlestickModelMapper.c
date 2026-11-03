/**
 * @file       XVCandlestickModelMapper.c
 * @brief      XVCandlestickModelMapper 实现（对标 QVCandlestickModelMapper）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XVCandlestickModelMapper.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static void xvcandlestickmodelmapper_emitVoid(XVCandlestickModelMapper* self, size_t signal)
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

static void VXVCandlestickModelMapper_deinit(XVCandlestickModelMapper* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XCandlestickModelMapper, &self->m_base);
}

XVtable* XVCandlestickModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVCandlestickModelMapper)
    XVTABLE_INHERIT_XCLASS(XCandlestickModelMapper);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXVCandlestickModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XVCandlestickModelMapper_init(XVCandlestickModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XCandlestickModelMapper_init(&self->m_base);
    XClassSetVtable(self, XVCandlestickModelMapper);
    /* 方向在构造时固定，与 Qt 具体子类一致。 */
    self->m_base.m_orientation = XModelMapper_Orientation_Vertical;
}

XVCandlestickModelMapper* XVCandlestickModelMapper_create_ex(XMemoryType memory)
{
    XVCandlestickModelMapper* self = (XVCandlestickModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XVCandlestickModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 专有参数 ==================== */
void XVCandlestickModelMapper_setTimestampRow(XVCandlestickModelMapper* self, int timestampRow)
{
    if (!self || self->m_base.m_timestampSection == timestampRow) return;
    self->m_base.m_timestampSection = timestampRow;
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_timestampRowChanged_signal);
}

int XVCandlestickModelMapper_TimestampRow(const XVCandlestickModelMapper* self)
{ return self ? self->m_base.m_timestampSection : -1; }

void XVCandlestickModelMapper_setOpenRow(XVCandlestickModelMapper* self, int openRow)
{
    if (!self || self->m_base.m_openSection == openRow) return;
    self->m_base.m_openSection = openRow;
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_openRowChanged_signal);
}

int XVCandlestickModelMapper_OpenRow(const XVCandlestickModelMapper* self)
{ return self ? self->m_base.m_openSection : -1; }

void XVCandlestickModelMapper_setHighRow(XVCandlestickModelMapper* self, int highRow)
{
    if (!self || self->m_base.m_highSection == highRow) return;
    self->m_base.m_highSection = highRow;
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_highRowChanged_signal);
}

int XVCandlestickModelMapper_HighRow(const XVCandlestickModelMapper* self)
{ return self ? self->m_base.m_highSection : -1; }

void XVCandlestickModelMapper_setLowRow(XVCandlestickModelMapper* self, int lowRow)
{
    if (!self || self->m_base.m_lowSection == lowRow) return;
    self->m_base.m_lowSection = lowRow;
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_lowRowChanged_signal);
}

int XVCandlestickModelMapper_LowRow(const XVCandlestickModelMapper* self)
{ return self ? self->m_base.m_lowSection : -1; }

void XVCandlestickModelMapper_setCloseRow(XVCandlestickModelMapper* self, int closeRow)
{
    if (!self || self->m_base.m_closeSection == closeRow) return;
    self->m_base.m_closeSection = closeRow;
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_closeRowChanged_signal);
}

int XVCandlestickModelMapper_CloseRow(const XVCandlestickModelMapper* self)
{ return self ? self->m_base.m_closeSection : -1; }

void XVCandlestickModelMapper_setFirstSetColumn(XVCandlestickModelMapper* self, int firstSetColumn)
{
    if (!self || self->m_base.m_firstSetSection == firstSetColumn) return;
    self->m_base.m_firstSetSection = firstSetColumn;
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_firstSetColumnChanged_signal);
}

int XVCandlestickModelMapper_FirstSetColumn(const XVCandlestickModelMapper* self)
{ return self ? self->m_base.m_firstSetSection : -1; }

void XVCandlestickModelMapper_setLastSetColumn(XVCandlestickModelMapper* self, int lastSetColumn)
{
    if (!self || self->m_base.m_lastSetSection == lastSetColumn) return;
    self->m_base.m_lastSetSection = lastSetColumn;
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_lastSetColumnChanged_signal);
}

int XVCandlestickModelMapper_LastSetColumn(const XVCandlestickModelMapper* self)
{ return self ? self->m_base.m_lastSetSection : -1; }

/* ==================== 信号 ==================== */

void* XVCandlestickModelMapper_timestampRowChanged_signal(XVCandlestickModelMapper* self)
{
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_timestampRowChanged_signal);
    return (void*)(size_t)XVCandlestickModelMapper_timestampRowChanged_signal;
}

void* XVCandlestickModelMapper_openRowChanged_signal(XVCandlestickModelMapper* self)
{
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_openRowChanged_signal);
    return (void*)(size_t)XVCandlestickModelMapper_openRowChanged_signal;
}

void* XVCandlestickModelMapper_highRowChanged_signal(XVCandlestickModelMapper* self)
{
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_highRowChanged_signal);
    return (void*)(size_t)XVCandlestickModelMapper_highRowChanged_signal;
}

void* XVCandlestickModelMapper_lowRowChanged_signal(XVCandlestickModelMapper* self)
{
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_lowRowChanged_signal);
    return (void*)(size_t)XVCandlestickModelMapper_lowRowChanged_signal;
}

void* XVCandlestickModelMapper_closeRowChanged_signal(XVCandlestickModelMapper* self)
{
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_closeRowChanged_signal);
    return (void*)(size_t)XVCandlestickModelMapper_closeRowChanged_signal;
}

void* XVCandlestickModelMapper_firstSetColumnChanged_signal(XVCandlestickModelMapper* self)
{
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_firstSetColumnChanged_signal);
    return (void*)(size_t)XVCandlestickModelMapper_firstSetColumnChanged_signal;
}

void* XVCandlestickModelMapper_lastSetColumnChanged_signal(XVCandlestickModelMapper* self)
{
    xvcandlestickmodelmapper_emitVoid(self, (size_t)XVCandlestickModelMapper_lastSetColumnChanged_signal);
    return (void*)(size_t)XVCandlestickModelMapper_lastSetColumnChanged_signal;
}


#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
