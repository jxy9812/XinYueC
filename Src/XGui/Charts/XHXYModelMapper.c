/**
 * @file       XHXYModelMapper.c
 * @brief      横向 XY 模型映射器实现（对标 QHXYModelMapper）。
 * @details    横向语义落在基类字段上：xSection=ySection→行号，
 *             first/count→列号；构造时把 orientation 固定为 Horizontal。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XHXYModelMapper.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static void xhxy_emitVoid(XHXYModelMapper* self, size_t signal)
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

static void VXHXYModelMapper_deinit(XHXYModelMapper* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XXYModelMapper, &self->m_base);
}

XVtable* XHXYModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHXYModelMapper)
    XVTABLE_INHERIT_XCLASS(XXYModelMapper);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHXYModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XHXYModelMapper_init(XHXYModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XXYModelMapper_init(&self->m_base);
    XClassSetVtable(self, XHXYModelMapper);
    /* 横向：固定按列推进。 */
    self->m_base.m_orientation = XModelMapper_Orientation_Horizontal;
}

XHXYModelMapper* XHXYModelMapper_create_ex(XMemoryType memory)
{
    XHXYModelMapper* self =
        (XHXYModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XHXYModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== H 向专有参数 ==================== */

void XHXYModelMapper_setXRow(XHXYModelMapper* self, int xRow)
{
    if (!self || self->m_base.m_xSection == xRow) return;
    self->m_base.m_xSection = xRow;
    xhxy_emitVoid(self, (size_t)XHXYModelMapper_xRowChanged_signal);
}

int XHXYModelMapper_xRow(const XHXYModelMapper* self)
{ return self ? self->m_base.m_xSection : -1; }

void XHXYModelMapper_setYRow(XHXYModelMapper* self, int yRow)
{
    if (!self || self->m_base.m_ySection == yRow) return;
    self->m_base.m_ySection = yRow;
    xhxy_emitVoid(self, (size_t)XHXYModelMapper_yRowChanged_signal);
}

int XHXYModelMapper_yRow(const XHXYModelMapper* self)
{ return self ? self->m_base.m_ySection : -1; }

void XHXYModelMapper_setFirstColumn(XHXYModelMapper* self, int firstColumn)
{
    if (!self || firstColumn < 0 || self->m_base.m_first == firstColumn) return;
    self->m_base.m_first = firstColumn;
    xhxy_emitVoid(self, (size_t)XHXYModelMapper_firstColumnChanged_signal);
}

int XHXYModelMapper_firstColumn(const XHXYModelMapper* self)
{ return self ? self->m_base.m_first : 0; }

void XHXYModelMapper_setColumnCount(XHXYModelMapper* self, int columnCount)
{
    if (!self || self->m_base.m_count == columnCount) return;
    self->m_base.m_count = columnCount;
    xhxy_emitVoid(self, (size_t)XHXYModelMapper_columnCountChanged_signal);
}

int XHXYModelMapper_columnCount(const XHXYModelMapper* self)
{ return self ? self->m_base.m_count : -1; }

/* ==================== 信号 ==================== */

void* XHXYModelMapper_seriesReplaced_signal(XHXYModelMapper* self)
{
    xhxy_emitVoid(self, (size_t)XHXYModelMapper_seriesReplaced_signal);
    return (void*)(size_t)XHXYModelMapper_seriesReplaced_signal;
}

void* XHXYModelMapper_modelReplaced_signal(XHXYModelMapper* self)
{
    xhxy_emitVoid(self, (size_t)XHXYModelMapper_modelReplaced_signal);
    return (void*)(size_t)XHXYModelMapper_modelReplaced_signal;
}

void* XHXYModelMapper_xRowChanged_signal(XHXYModelMapper* self)
{
    xhxy_emitVoid(self, (size_t)XHXYModelMapper_xRowChanged_signal);
    return (void*)(size_t)XHXYModelMapper_xRowChanged_signal;
}

void* XHXYModelMapper_yRowChanged_signal(XHXYModelMapper* self)
{
    xhxy_emitVoid(self, (size_t)XHXYModelMapper_yRowChanged_signal);
    return (void*)(size_t)XHXYModelMapper_yRowChanged_signal;
}

void* XHXYModelMapper_firstColumnChanged_signal(XHXYModelMapper* self)
{
    xhxy_emitVoid(self, (size_t)XHXYModelMapper_firstColumnChanged_signal);
    return (void*)(size_t)XHXYModelMapper_firstColumnChanged_signal;
}

void* XHXYModelMapper_columnCountChanged_signal(XHXYModelMapper* self)
{
    xhxy_emitVoid(self, (size_t)XHXYModelMapper_columnCountChanged_signal);
    return (void*)(size_t)XHXYModelMapper_columnCountChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
