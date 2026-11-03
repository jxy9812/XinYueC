/**
 * @file       XBoxPlotModelMapper.c
 * @brief      XBoxPlotModelMapper 模型映射器基类实现（对标 QBoxPlotModelMapper）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XBoxPlotModelMapper.h"
#include "XModelMapper_Protected.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXBoxPlotModelMapper_deinit(XBoxPlotModelMapper* self)
{
    if (!self) return;
    /* 模型与序列均为借用指针，不在此释放。 */
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XBoxPlotModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XBoxPlotModelMapper)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBoxPlotModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XBoxPlotModelMapper_init(XBoxPlotModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XBoxPlotModelMapper);
    self->m_first = 0;
    self->m_count = -1;             /* Qt 默认：按模型维度推导 */
    self->m_orientation = XModelMapper_Orientation_Vertical;
    self->m_firstBoxSetSection = -1;
    self->m_lastBoxSetSection = -1;
}

XBoxPlotModelMapper* XBoxPlotModelMapper_create_ex(XMemoryType memory)
{
    XBoxPlotModelMapper* self = (XBoxPlotModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XBoxPlotModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 模型与序列 ==================== */

void XBoxPlotModelMapper_setModel(XBoxPlotModelMapper* self, XAbstractItemModel* model)
{
    if (!self) return;
    self->m_model = model;
}

XAbstractItemModel* XBoxPlotModelMapper_model(const XBoxPlotModelMapper* self)
{ return self ? self->m_model : NULL; }

void XBoxPlotModelMapper_setSeries(XBoxPlotModelMapper* self, XBoxPlotSeries* series)
{
    if (!self) return;
    self->m_series = series;
}

XBoxPlotSeries* XBoxPlotModelMapper_series(const XBoxPlotModelMapper* self)
{ return self ? self->m_series : NULL; }

/* ==================== 通用参数 ==================== */

void XBoxPlotModelMapper_setFirst(XBoxPlotModelMapper* self, int first)
{
    if (!self || first < 0) return;
    self->m_first = first;
}

int XBoxPlotModelMapper_first(const XBoxPlotModelMapper* self)
{ return self ? self->m_first : 0; }

void XBoxPlotModelMapper_setCount(XBoxPlotModelMapper* self, int count)
{
    if (!self) return;
    self->m_count = count;
}

int XBoxPlotModelMapper_count(const XBoxPlotModelMapper* self)
{ return self ? self->m_count : -1; }

void XBoxPlotModelMapper_setOrientation(XBoxPlotModelMapper* self, int orientation)
{
    if (!self) return;
    self->m_orientation = orientation;
}

int XBoxPlotModelMapper_orientation(const XBoxPlotModelMapper* self)
{ return self ? self->m_orientation : 0; }

/* ==================== 专有参数 ==================== */
void XBoxPlotModelMapper_setFirstBoxSetSection(XBoxPlotModelMapper* self, int section)
{
    if (!self) return;
    self->m_firstBoxSetSection = section;
}

int XBoxPlotModelMapper_firstBoxSetSection(const XBoxPlotModelMapper* self)
{ return self ? self->m_firstBoxSetSection : -1; }

void XBoxPlotModelMapper_setLastBoxSetSection(XBoxPlotModelMapper* self, int section)
{
    if (!self) return;
    self->m_lastBoxSetSection = section;
}

int XBoxPlotModelMapper_lastBoxSetSection(const XBoxPlotModelMapper* self)
{ return self ? self->m_lastBoxSetSection : -1; }

/* ==================== 同步 ==================== */

bool XBoxPlotModelMapper_remap(XBoxPlotModelMapper* self)
{
    return false;   /* 基类不实现具体映射；由 H/V 具体子类承担。 */
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
