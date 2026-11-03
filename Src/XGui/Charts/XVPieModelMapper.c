/**
 * @file       XVPieModelMapper.c
 * @brief      XVPieModelMapper 实现（对标 QVPieModelMapper）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XVPieModelMapper.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static void xvpiemodelmapper_emitVoid(XVPieModelMapper* self, size_t signal)
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

static void VXVPieModelMapper_deinit(XVPieModelMapper* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XPieModelMapper, &self->m_base);
}

XVtable* XVPieModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVPieModelMapper)
    XVTABLE_INHERIT_XCLASS(XPieModelMapper);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXVPieModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XVPieModelMapper_init(XVPieModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XPieModelMapper_init(&self->m_base);
    XClassSetVtable(self, XVPieModelMapper);
    /* 方向在构造时固定，与 Qt 具体子类一致。 */
    self->m_base.m_orientation = XModelMapper_Orientation_Vertical;
}

XVPieModelMapper* XVPieModelMapper_create_ex(XMemoryType memory)
{
    XVPieModelMapper* self = (XVPieModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XVPieModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 专有参数 ==================== */
void XVPieModelMapper_setValuesColumn(XVPieModelMapper* self, int valuesColumn)
{
    if (!self) return;
    self->m_base.m_valuesColumn = valuesColumn;
}

int XVPieModelMapper_valuesColumn(const XVPieModelMapper* self)
{ return self ? self->m_base.m_valuesColumn : -1; }

void XVPieModelMapper_setLabelsColumn(XVPieModelMapper* self, int labelsColumn)
{
    if (!self) return;
    self->m_base.m_labelsColumn = labelsColumn;
}

int XVPieModelMapper_labelsColumn(const XVPieModelMapper* self)
{ return self ? self->m_base.m_labelsColumn : -1; }

void XVPieModelMapper_setFirstRow(XVPieModelMapper* self, int firstRow)
{
    if (!self) return;
    self->m_base.m_firstRow = firstRow;
}

int XVPieModelMapper_firstRow(const XVPieModelMapper* self)
{ return self ? self->m_base.m_firstRow : -1; }

void XVPieModelMapper_setRowCount(XVPieModelMapper* self, int rowCount)
{
    if (!self) return;
    self->m_base.m_rowCount = rowCount;
}

int XVPieModelMapper_rowCount(const XVPieModelMapper* self)
{ return self ? self->m_base.m_rowCount : -1; }

/* ==================== 信号 ==================== */

void* XVPieModelMapper_seriesReplaced_signal(XVPieModelMapper* self)
{
    xvpiemodelmapper_emitVoid(self, (size_t)XVPieModelMapper_seriesReplaced_signal);
    return (void*)(size_t)XVPieModelMapper_seriesReplaced_signal;
}

void* XVPieModelMapper_modelReplaced_signal(XVPieModelMapper* self)
{
    xvpiemodelmapper_emitVoid(self, (size_t)XVPieModelMapper_modelReplaced_signal);
    return (void*)(size_t)XVPieModelMapper_modelReplaced_signal;
}

void* XVPieModelMapper_ValuesColumnChanged_signal(XVPieModelMapper* self)
{
    xvpiemodelmapper_emitVoid(self, (size_t)XVPieModelMapper_ValuesColumnChanged_signal);
    return (void*)(size_t)XVPieModelMapper_ValuesColumnChanged_signal;
}

void* XVPieModelMapper_LabelsColumnChanged_signal(XVPieModelMapper* self)
{
    xvpiemodelmapper_emitVoid(self, (size_t)XVPieModelMapper_LabelsColumnChanged_signal);
    return (void*)(size_t)XVPieModelMapper_LabelsColumnChanged_signal;
}

void* XVPieModelMapper_FirstRowChanged_signal(XVPieModelMapper* self)
{
    xvpiemodelmapper_emitVoid(self, (size_t)XVPieModelMapper_FirstRowChanged_signal);
    return (void*)(size_t)XVPieModelMapper_FirstRowChanged_signal;
}

void* XVPieModelMapper_RowCountChanged_signal(XVPieModelMapper* self)
{
    xvpiemodelmapper_emitVoid(self, (size_t)XVPieModelMapper_RowCountChanged_signal);
    return (void*)(size_t)XVPieModelMapper_RowCountChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
