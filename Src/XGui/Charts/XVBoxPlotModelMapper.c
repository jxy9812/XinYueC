/**
 * @file       XVBoxPlotModelMapper.c
 * @brief      XVBoxPlotModelMapper 实现（对标 QVBoxPlotModelMapper）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XVBoxPlotModelMapper.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static void xvboxplotmodelmapper_emitVoid(XVBoxPlotModelMapper* self, size_t signal)
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

static void VXVBoxPlotModelMapper_deinit(XVBoxPlotModelMapper* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XBoxPlotModelMapper, &self->m_base);
}

XVtable* XVBoxPlotModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVBoxPlotModelMapper)
    XVTABLE_INHERIT_XCLASS(XBoxPlotModelMapper);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXVBoxPlotModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XVBoxPlotModelMapper_init(XVBoxPlotModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XBoxPlotModelMapper_init(&self->m_base);
    XClassSetVtable(self, XVBoxPlotModelMapper);
    /* 方向在构造时固定，与 Qt 具体子类一致。 */
    self->m_base.m_orientation = XModelMapper_Orientation_Vertical;
}

XVBoxPlotModelMapper* XVBoxPlotModelMapper_create_ex(XMemoryType memory)
{
    XVBoxPlotModelMapper* self = (XVBoxPlotModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XVBoxPlotModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 专有参数 ==================== */
void XVBoxPlotModelMapper_setFirstBoxSetColumn(XVBoxPlotModelMapper* self, int firstBoxSetColumn)
{
    if (!self) return;
    self->m_base.m_firstBoxSetColumn = firstBoxSetColumn;
}

int XVBoxPlotModelMapper_firstBoxSetColumn(const XVBoxPlotModelMapper* self)
{ return self ? self->m_base.m_firstBoxSetColumn : -1; }

void XVBoxPlotModelMapper_setLastBoxSetColumn(XVBoxPlotModelMapper* self, int lastBoxSetColumn)
{
    if (!self) return;
    self->m_base.m_lastBoxSetColumn = lastBoxSetColumn;
}

int XVBoxPlotModelMapper_lastBoxSetColumn(const XVBoxPlotModelMapper* self)
{ return self ? self->m_base.m_lastBoxSetColumn : -1; }

void XVBoxPlotModelMapper_setFirstRow(XVBoxPlotModelMapper* self, int firstRow)
{
    if (!self) return;
    self->m_base.m_firstRow = firstRow;
}

int XVBoxPlotModelMapper_firstRow(const XVBoxPlotModelMapper* self)
{ return self ? self->m_base.m_firstRow : -1; }

void XVBoxPlotModelMapper_setRowCount(XVBoxPlotModelMapper* self, int rowCount)
{
    if (!self) return;
    self->m_base.m_rowCount = rowCount;
}

int XVBoxPlotModelMapper_rowCount(const XVBoxPlotModelMapper* self)
{ return self ? self->m_base.m_rowCount : -1; }

/* ==================== 信号 ==================== */

void* XVBoxPlotModelMapper_seriesReplaced_signal(XVBoxPlotModelMapper* self)
{
    xvboxplotmodelmapper_emitVoid(self, (size_t)XVBoxPlotModelMapper_seriesReplaced_signal);
    return (void*)(size_t)XVBoxPlotModelMapper_seriesReplaced_signal;
}

void* XVBoxPlotModelMapper_modelReplaced_signal(XVBoxPlotModelMapper* self)
{
    xvboxplotmodelmapper_emitVoid(self, (size_t)XVBoxPlotModelMapper_modelReplaced_signal);
    return (void*)(size_t)XVBoxPlotModelMapper_modelReplaced_signal;
}

void* XVBoxPlotModelMapper_FirstBoxSetColumnChanged_signal(XVBoxPlotModelMapper* self)
{
    xvboxplotmodelmapper_emitVoid(self, (size_t)XVBoxPlotModelMapper_FirstBoxSetColumnChanged_signal);
    return (void*)(size_t)XVBoxPlotModelMapper_FirstBoxSetColumnChanged_signal;
}

void* XVBoxPlotModelMapper_LastBoxSetColumnChanged_signal(XVBoxPlotModelMapper* self)
{
    xvboxplotmodelmapper_emitVoid(self, (size_t)XVBoxPlotModelMapper_LastBoxSetColumnChanged_signal);
    return (void*)(size_t)XVBoxPlotModelMapper_LastBoxSetColumnChanged_signal;
}

void* XVBoxPlotModelMapper_FirstRowChanged_signal(XVBoxPlotModelMapper* self)
{
    xvboxplotmodelmapper_emitVoid(self, (size_t)XVBoxPlotModelMapper_FirstRowChanged_signal);
    return (void*)(size_t)XVBoxPlotModelMapper_FirstRowChanged_signal;
}

void* XVBoxPlotModelMapper_RowCountChanged_signal(XVBoxPlotModelMapper* self)
{
    xvboxplotmodelmapper_emitVoid(self, (size_t)XVBoxPlotModelMapper_RowCountChanged_signal);
    return (void*)(size_t)XVBoxPlotModelMapper_RowCountChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
