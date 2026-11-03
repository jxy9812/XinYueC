/**
 * @file       XVXYModelMapper.c
 * @brief      纵向 XY 模型映射器实现（对标 QVXYModelMapper）。
 * @details    纵向语义落在基类字段上：xSection=ySection→列号，
 *             first/count→行号；构造时把 orientation 固定为 Vertical。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XVXYModelMapper.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static void xvxy_emitVoid(XVXYModelMapper* self, size_t signal)
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

static void VXVXYModelMapper_deinit(XVXYModelMapper* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XXYModelMapper, &self->m_base);
}

XVtable* XVXYModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVXYModelMapper)
    XVTABLE_INHERIT_XCLASS(XXYModelMapper);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXVXYModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XVXYModelMapper_init(XVXYModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XXYModelMapper_init(&self->m_base);
    XClassSetVtable(self, XVXYModelMapper);
    /* 纵向：固定按行推进。 */
    self->m_base.m_orientation = XModelMapper_Orientation_Vertical;
}

XVXYModelMapper* XVXYModelMapper_create_ex(XMemoryType memory)
{
    XVXYModelMapper* self =
        (XVXYModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XVXYModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== V 向专有参数 ==================== */

void XVXYModelMapper_setXColumn(XVXYModelMapper* self, int xColumn)
{
    if (!self || self->m_base.m_xSection == xColumn) return;
    self->m_base.m_xSection = xColumn;
    xvxy_emitVoid(self, (size_t)XVXYModelMapper_xColumnChanged_signal);
}

int XVXYModelMapper_xColumn(const XVXYModelMapper* self)
{ return self ? self->m_base.m_xSection : -1; }

void XVXYModelMapper_setYColumn(XVXYModelMapper* self, int yColumn)
{
    if (!self || self->m_base.m_ySection == yColumn) return;
    self->m_base.m_ySection = yColumn;
    xvxy_emitVoid(self, (size_t)XVXYModelMapper_yColumnChanged_signal);
}

int XVXYModelMapper_yColumn(const XVXYModelMapper* self)
{ return self ? self->m_base.m_ySection : -1; }

void XVXYModelMapper_setFirstRow(XVXYModelMapper* self, int firstRow)
{
    if (!self || firstRow < 0 || self->m_base.m_first == firstRow) return;
    self->m_base.m_first = firstRow;
    xvxy_emitVoid(self, (size_t)XVXYModelMapper_firstRowChanged_signal);
}

int XVXYModelMapper_firstRow(const XVXYModelMapper* self)
{ return self ? self->m_base.m_first : 0; }

void XVXYModelMapper_setRowCount(XVXYModelMapper* self, int rowCount)
{
    if (!self || self->m_base.m_count == rowCount) return;
    self->m_base.m_count = rowCount;
    xvxy_emitVoid(self, (size_t)XVXYModelMapper_rowCountChanged_signal);
}

int XVXYModelMapper_rowCount(const XVXYModelMapper* self)
{ return self ? self->m_base.m_count : -1; }

/* ==================== 信号 ==================== */

void* XVXYModelMapper_seriesReplaced_signal(XVXYModelMapper* self)
{
    xvxy_emitVoid(self, (size_t)XVXYModelMapper_seriesReplaced_signal);
    return (void*)(size_t)XVXYModelMapper_seriesReplaced_signal;
}

void* XVXYModelMapper_modelReplaced_signal(XVXYModelMapper* self)
{
    xvxy_emitVoid(self, (size_t)XVXYModelMapper_modelReplaced_signal);
    return (void*)(size_t)XVXYModelMapper_modelReplaced_signal;
}

void* XVXYModelMapper_xColumnChanged_signal(XVXYModelMapper* self)
{
    xvxy_emitVoid(self, (size_t)XVXYModelMapper_xColumnChanged_signal);
    return (void*)(size_t)XVXYModelMapper_xColumnChanged_signal;
}

void* XVXYModelMapper_yColumnChanged_signal(XVXYModelMapper* self)
{
    xvxy_emitVoid(self, (size_t)XVXYModelMapper_yColumnChanged_signal);
    return (void*)(size_t)XVXYModelMapper_yColumnChanged_signal;
}

void* XVXYModelMapper_firstRowChanged_signal(XVXYModelMapper* self)
{
    xvxy_emitVoid(self, (size_t)XVXYModelMapper_firstRowChanged_signal);
    return (void*)(size_t)XVXYModelMapper_firstRowChanged_signal;
}

void* XVXYModelMapper_rowCountChanged_signal(XVXYModelMapper* self)
{
    xvxy_emitVoid(self, (size_t)XVXYModelMapper_rowCountChanged_signal);
    return (void*)(size_t)XVXYModelMapper_rowCountChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
