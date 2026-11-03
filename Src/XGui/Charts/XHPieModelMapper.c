/**
 * @file       XHPieModelMapper.c
 * @brief      XHPieModelMapper 实现（对标 QHPieModelMapper）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XHPieModelMapper.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static void xhpiemodelmapper_emitVoid(XHPieModelMapper* self, size_t signal)
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

static void VXHPieModelMapper_deinit(XHPieModelMapper* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XPieModelMapper, &self->m_base);
}

XVtable* XHPieModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHPieModelMapper)
    XVTABLE_INHERIT_XCLASS(XPieModelMapper);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHPieModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XHPieModelMapper_init(XHPieModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XPieModelMapper_init(&self->m_base);
    XClassSetVtable(self, XHPieModelMapper);
    /* 方向在构造时固定，与 Qt 具体子类一致。 */
    self->m_base.m_orientation = XModelMapper_Orientation_Horizontal;
}

XHPieModelMapper* XHPieModelMapper_create_ex(XMemoryType memory)
{
    XHPieModelMapper* self = (XHPieModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XHPieModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 专有参数 ==================== */
void XHPieModelMapper_setValuesRow(XHPieModelMapper* self, int valuesRow)
{
    if (!self) return;
    self->m_base.m_valuesRow = valuesRow;
}

int XHPieModelMapper_valuesRow(const XHPieModelMapper* self)
{ return self ? self->m_base.m_valuesRow : -1; }

void XHPieModelMapper_setLabelsRow(XHPieModelMapper* self, int labelsRow)
{
    if (!self) return;
    self->m_base.m_labelsRow = labelsRow;
}

int XHPieModelMapper_labelsRow(const XHPieModelMapper* self)
{ return self ? self->m_base.m_labelsRow : -1; }

void XHPieModelMapper_setFirstColumn(XHPieModelMapper* self, int firstColumn)
{
    if (!self) return;
    self->m_base.m_firstColumn = firstColumn;
}

int XHPieModelMapper_firstColumn(const XHPieModelMapper* self)
{ return self ? self->m_base.m_firstColumn : -1; }

void XHPieModelMapper_setColumnCount(XHPieModelMapper* self, int columnCount)
{
    if (!self) return;
    self->m_base.m_columnCount = columnCount;
}

int XHPieModelMapper_columnCount(const XHPieModelMapper* self)
{ return self ? self->m_base.m_columnCount : -1; }

/* ==================== 信号 ==================== */

void* XHPieModelMapper_seriesReplaced_signal(XHPieModelMapper* self)
{
    xhpiemodelmapper_emitVoid(self, (size_t)XHPieModelMapper_seriesReplaced_signal);
    return (void*)(size_t)XHPieModelMapper_seriesReplaced_signal;
}

void* XHPieModelMapper_modelReplaced_signal(XHPieModelMapper* self)
{
    xhpiemodelmapper_emitVoid(self, (size_t)XHPieModelMapper_modelReplaced_signal);
    return (void*)(size_t)XHPieModelMapper_modelReplaced_signal;
}

void* XHPieModelMapper_ValuesRowChanged_signal(XHPieModelMapper* self)
{
    xhpiemodelmapper_emitVoid(self, (size_t)XHPieModelMapper_ValuesRowChanged_signal);
    return (void*)(size_t)XHPieModelMapper_ValuesRowChanged_signal;
}

void* XHPieModelMapper_LabelsRowChanged_signal(XHPieModelMapper* self)
{
    xhpiemodelmapper_emitVoid(self, (size_t)XHPieModelMapper_LabelsRowChanged_signal);
    return (void*)(size_t)XHPieModelMapper_LabelsRowChanged_signal;
}

void* XHPieModelMapper_FirstColumnChanged_signal(XHPieModelMapper* self)
{
    xhpiemodelmapper_emitVoid(self, (size_t)XHPieModelMapper_FirstColumnChanged_signal);
    return (void*)(size_t)XHPieModelMapper_FirstColumnChanged_signal;
}

void* XHPieModelMapper_ColumnCountChanged_signal(XHPieModelMapper* self)
{
    xhpiemodelmapper_emitVoid(self, (size_t)XHPieModelMapper_ColumnCountChanged_signal);
    return (void*)(size_t)XHPieModelMapper_ColumnCountChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
