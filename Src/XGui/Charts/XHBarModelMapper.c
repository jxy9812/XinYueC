/**
 * @file       XHBarModelMapper.c
 * @brief      XHBarModelMapper 实现（对标 QHBarModelMapper）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XHBarModelMapper.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static void xhbarmodelmapper_emitVoid(XHBarModelMapper* self, size_t signal)
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

static void VXHBarModelMapper_deinit(XHBarModelMapper* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XBarModelMapper, &self->m_base);
}

XVtable* XHBarModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHBarModelMapper)
    XVTABLE_INHERIT_XCLASS(XBarModelMapper);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHBarModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XHBarModelMapper_init(XHBarModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XBarModelMapper_init(&self->m_base);
    XClassSetVtable(self, XHBarModelMapper);
    /* 方向在构造时固定，与 Qt 具体子类一致。 */
    self->m_base.m_orientation = XModelMapper_Orientation_Horizontal;
}

XHBarModelMapper* XHBarModelMapper_create_ex(XMemoryType memory)
{
    XHBarModelMapper* self = (XHBarModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XHBarModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 专有参数 ==================== */
void XHBarModelMapper_setFirstBarSetRow(XHBarModelMapper* self, int firstBarSetRow)
{
    if (!self) return;
    self->m_base.m_firstBarSetRow = firstBarSetRow;
}

int XHBarModelMapper_firstBarSetRow(const XHBarModelMapper* self)
{ return self ? self->m_base.m_firstBarSetRow : -1; }

void XHBarModelMapper_setLastBarSetRow(XHBarModelMapper* self, int lastBarSetRow)
{
    if (!self) return;
    self->m_base.m_lastBarSetRow = lastBarSetRow;
}

int XHBarModelMapper_lastBarSetRow(const XHBarModelMapper* self)
{ return self ? self->m_base.m_lastBarSetRow : -1; }

void XHBarModelMapper_setFirstColumn(XHBarModelMapper* self, int firstColumn)
{
    if (!self) return;
    self->m_base.m_firstColumn = firstColumn;
}

int XHBarModelMapper_firstColumn(const XHBarModelMapper* self)
{ return self ? self->m_base.m_firstColumn : -1; }

void XHBarModelMapper_setColumnCount(XHBarModelMapper* self, int columnCount)
{
    if (!self) return;
    self->m_base.m_columnCount = columnCount;
}

int XHBarModelMapper_columnCount(const XHBarModelMapper* self)
{ return self ? self->m_base.m_columnCount : -1; }

/* ==================== 信号 ==================== */

void* XHBarModelMapper_seriesReplaced_signal(XHBarModelMapper* self)
{
    xhbarmodelmapper_emitVoid(self, (size_t)XHBarModelMapper_seriesReplaced_signal);
    return (void*)(size_t)XHBarModelMapper_seriesReplaced_signal;
}

void* XHBarModelMapper_modelReplaced_signal(XHBarModelMapper* self)
{
    xhbarmodelmapper_emitVoid(self, (size_t)XHBarModelMapper_modelReplaced_signal);
    return (void*)(size_t)XHBarModelMapper_modelReplaced_signal;
}

void* XHBarModelMapper_FirstBarSetRowChanged_signal(XHBarModelMapper* self)
{
    xhbarmodelmapper_emitVoid(self, (size_t)XHBarModelMapper_FirstBarSetRowChanged_signal);
    return (void*)(size_t)XHBarModelMapper_FirstBarSetRowChanged_signal;
}

void* XHBarModelMapper_LastBarSetRowChanged_signal(XHBarModelMapper* self)
{
    xhbarmodelmapper_emitVoid(self, (size_t)XHBarModelMapper_LastBarSetRowChanged_signal);
    return (void*)(size_t)XHBarModelMapper_LastBarSetRowChanged_signal;
}

void* XHBarModelMapper_FirstColumnChanged_signal(XHBarModelMapper* self)
{
    xhbarmodelmapper_emitVoid(self, (size_t)XHBarModelMapper_FirstColumnChanged_signal);
    return (void*)(size_t)XHBarModelMapper_FirstColumnChanged_signal;
}

void* XHBarModelMapper_ColumnCountChanged_signal(XHBarModelMapper* self)
{
    xhbarmodelmapper_emitVoid(self, (size_t)XHBarModelMapper_ColumnCountChanged_signal);
    return (void*)(size_t)XHBarModelMapper_ColumnCountChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
