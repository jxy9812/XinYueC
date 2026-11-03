/**
 * @file       XHBoxPlotModelMapper.c
 * @brief      XHBoxPlotModelMapper 实现（对标 QHBoxPlotModelMapper）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XHBoxPlotModelMapper.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static void xhboxplotmodelmapper_emitVoid(XHBoxPlotModelMapper* self, size_t signal)
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

static void VXHBoxPlotModelMapper_deinit(XHBoxPlotModelMapper* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XBoxPlotModelMapper, &self->m_base);
}

XVtable* XHBoxPlotModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHBoxPlotModelMapper)
    XVTABLE_INHERIT_XCLASS(XBoxPlotModelMapper);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHBoxPlotModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XHBoxPlotModelMapper_init(XHBoxPlotModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XBoxPlotModelMapper_init(&self->m_base);
    XClassSetVtable(self, XHBoxPlotModelMapper);
    /* 方向在构造时固定，与 Qt 具体子类一致。 */
    self->m_base.m_orientation = XModelMapper_Orientation_Horizontal;
}

XHBoxPlotModelMapper* XHBoxPlotModelMapper_create_ex(XMemoryType memory)
{
    XHBoxPlotModelMapper* self = (XHBoxPlotModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XHBoxPlotModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 专有参数 ==================== */
void XHBoxPlotModelMapper_setFirstBoxSetRow(XHBoxPlotModelMapper* self, int firstBoxSetRow)
{
    if (!self) return;
    self->m_base.m_firstBoxSetRow = firstBoxSetRow;
}

int XHBoxPlotModelMapper_firstBoxSetRow(const XHBoxPlotModelMapper* self)
{ return self ? self->m_base.m_firstBoxSetRow : -1; }

void XHBoxPlotModelMapper_setLastBoxSetRow(XHBoxPlotModelMapper* self, int lastBoxSetRow)
{
    if (!self) return;
    self->m_base.m_lastBoxSetRow = lastBoxSetRow;
}

int XHBoxPlotModelMapper_lastBoxSetRow(const XHBoxPlotModelMapper* self)
{ return self ? self->m_base.m_lastBoxSetRow : -1; }

void XHBoxPlotModelMapper_setFirstColumn(XHBoxPlotModelMapper* self, int firstColumn)
{
    if (!self) return;
    self->m_base.m_firstColumn = firstColumn;
}

int XHBoxPlotModelMapper_firstColumn(const XHBoxPlotModelMapper* self)
{ return self ? self->m_base.m_firstColumn : -1; }

void XHBoxPlotModelMapper_setColumnCount(XHBoxPlotModelMapper* self, int columnCount)
{
    if (!self) return;
    self->m_base.m_columnCount = columnCount;
}

int XHBoxPlotModelMapper_columnCount(const XHBoxPlotModelMapper* self)
{ return self ? self->m_base.m_columnCount : -1; }

/* ==================== 信号 ==================== */

void* XHBoxPlotModelMapper_seriesReplaced_signal(XHBoxPlotModelMapper* self)
{
    xhboxplotmodelmapper_emitVoid(self, (size_t)XHBoxPlotModelMapper_seriesReplaced_signal);
    return (void*)(size_t)XHBoxPlotModelMapper_seriesReplaced_signal;
}

void* XHBoxPlotModelMapper_modelReplaced_signal(XHBoxPlotModelMapper* self)
{
    xhboxplotmodelmapper_emitVoid(self, (size_t)XHBoxPlotModelMapper_modelReplaced_signal);
    return (void*)(size_t)XHBoxPlotModelMapper_modelReplaced_signal;
}

void* XHBoxPlotModelMapper_FirstBoxSetRowChanged_signal(XHBoxPlotModelMapper* self)
{
    xhboxplotmodelmapper_emitVoid(self, (size_t)XHBoxPlotModelMapper_FirstBoxSetRowChanged_signal);
    return (void*)(size_t)XHBoxPlotModelMapper_FirstBoxSetRowChanged_signal;
}

void* XHBoxPlotModelMapper_LastBoxSetRowChanged_signal(XHBoxPlotModelMapper* self)
{
    xhboxplotmodelmapper_emitVoid(self, (size_t)XHBoxPlotModelMapper_LastBoxSetRowChanged_signal);
    return (void*)(size_t)XHBoxPlotModelMapper_LastBoxSetRowChanged_signal;
}

void* XHBoxPlotModelMapper_FirstColumnChanged_signal(XHBoxPlotModelMapper* self)
{
    xhboxplotmodelmapper_emitVoid(self, (size_t)XHBoxPlotModelMapper_FirstColumnChanged_signal);
    return (void*)(size_t)XHBoxPlotModelMapper_FirstColumnChanged_signal;
}

void* XHBoxPlotModelMapper_ColumnCountChanged_signal(XHBoxPlotModelMapper* self)
{
    xhboxplotmodelmapper_emitVoid(self, (size_t)XHBoxPlotModelMapper_ColumnCountChanged_signal);
    return (void*)(size_t)XHBoxPlotModelMapper_ColumnCountChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
