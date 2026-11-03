/**
 * @file       XVBarModelMapper.c
 * @brief      XVBarModelMapper 实现（对标 QVBarModelMapper）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XVBarModelMapper.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static void xvbarmodelmapper_emitVoid(XVBarModelMapper* self, size_t signal)
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

static void VXVBarModelMapper_deinit(XVBarModelMapper* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XBarModelMapper, &self->m_base);
}

XVtable* XVBarModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVBarModelMapper)
    XVTABLE_INHERIT_XCLASS(XBarModelMapper);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXVBarModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XVBarModelMapper_init(XVBarModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XBarModelMapper_init(&self->m_base);
    XClassSetVtable(self, XVBarModelMapper);
    /* 方向在构造时固定，与 Qt 具体子类一致。 */
    self->m_base.m_orientation = XModelMapper_Orientation_Vertical;
}

XVBarModelMapper* XVBarModelMapper_create_ex(XMemoryType memory)
{
    XVBarModelMapper* self = (XVBarModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XVBarModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 专有参数 ==================== */
void XVBarModelMapper_setFirstBarSetColumn(XVBarModelMapper* self, int firstBarSetColumn)
{
    if (!self) return;
    self->m_base.m_firstBarSetColumn = firstBarSetColumn;
}

int XVBarModelMapper_firstBarSetColumn(const XVBarModelMapper* self)
{ return self ? self->m_base.m_firstBarSetColumn : -1; }

void XVBarModelMapper_setLastBarSetColumn(XVBarModelMapper* self, int lastBarSetColumn)
{
    if (!self) return;
    self->m_base.m_lastBarSetColumn = lastBarSetColumn;
}

int XVBarModelMapper_lastBarSetColumn(const XVBarModelMapper* self)
{ return self ? self->m_base.m_lastBarSetColumn : -1; }

void XVBarModelMapper_setFirstRow(XVBarModelMapper* self, int firstRow)
{
    if (!self) return;
    self->m_base.m_firstRow = firstRow;
}

int XVBarModelMapper_firstRow(const XVBarModelMapper* self)
{ return self ? self->m_base.m_firstRow : -1; }

void XVBarModelMapper_setRowCount(XVBarModelMapper* self, int rowCount)
{
    if (!self) return;
    self->m_base.m_rowCount = rowCount;
}

int XVBarModelMapper_rowCount(const XVBarModelMapper* self)
{ return self ? self->m_base.m_rowCount : -1; }

/* ==================== 信号 ==================== */

void* XVBarModelMapper_seriesReplaced_signal(XVBarModelMapper* self)
{
    xvbarmodelmapper_emitVoid(self, (size_t)XVBarModelMapper_seriesReplaced_signal);
    return (void*)(size_t)XVBarModelMapper_seriesReplaced_signal;
}

void* XVBarModelMapper_modelReplaced_signal(XVBarModelMapper* self)
{
    xvbarmodelmapper_emitVoid(self, (size_t)XVBarModelMapper_modelReplaced_signal);
    return (void*)(size_t)XVBarModelMapper_modelReplaced_signal;
}

void* XVBarModelMapper_FirstBarSetColumnChanged_signal(XVBarModelMapper* self)
{
    xvbarmodelmapper_emitVoid(self, (size_t)XVBarModelMapper_FirstBarSetColumnChanged_signal);
    return (void*)(size_t)XVBarModelMapper_FirstBarSetColumnChanged_signal;
}

void* XVBarModelMapper_LastBarSetColumnChanged_signal(XVBarModelMapper* self)
{
    xvbarmodelmapper_emitVoid(self, (size_t)XVBarModelMapper_LastBarSetColumnChanged_signal);
    return (void*)(size_t)XVBarModelMapper_LastBarSetColumnChanged_signal;
}

void* XVBarModelMapper_FirstRowChanged_signal(XVBarModelMapper* self)
{
    xvbarmodelmapper_emitVoid(self, (size_t)XVBarModelMapper_FirstRowChanged_signal);
    return (void*)(size_t)XVBarModelMapper_FirstRowChanged_signal;
}

void* XVBarModelMapper_RowCountChanged_signal(XVBarModelMapper* self)
{
    xvbarmodelmapper_emitVoid(self, (size_t)XVBarModelMapper_RowCountChanged_signal);
    return (void*)(size_t)XVBarModelMapper_RowCountChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
