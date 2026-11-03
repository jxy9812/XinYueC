/**
 * @file       XPieModelMapper.c
 * @brief      XPieModelMapper 模型映射器基类实现（对标 QPieModelMapper）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XPieModelMapper.h"
#include "XModelMapper_Protected.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXPieModelMapper_deinit(XPieModelMapper* self)
{
    if (!self) return;
    /* 模型与序列均为借用指针，不在此释放。 */
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XPieModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPieModelMapper)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPieModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XPieModelMapper_init(XPieModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XPieModelMapper);
    self->m_first = 0;
    self->m_count = -1;             /* Qt 默认：按模型维度推导 */
    self->m_orientation = XModelMapper_Orientation_Vertical;
    self->m_valuesSection = -1;
    self->m_labelsSection = -1;
}

XPieModelMapper* XPieModelMapper_create_ex(XMemoryType memory)
{
    XPieModelMapper* self = (XPieModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XPieModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 模型与序列 ==================== */

void XPieModelMapper_setModel(XPieModelMapper* self, XAbstractItemModel* model)
{
    if (!self) return;
    self->m_model = model;
}

XAbstractItemModel* XPieModelMapper_model(const XPieModelMapper* self)
{ return self ? self->m_model : NULL; }

void XPieModelMapper_setSeries(XPieModelMapper* self, XPieSeries* series)
{
    if (!self) return;
    self->m_series = series;
}

XPieSeries* XPieModelMapper_series(const XPieModelMapper* self)
{ return self ? self->m_series : NULL; }

/* ==================== 通用参数 ==================== */

void XPieModelMapper_setFirst(XPieModelMapper* self, int first)
{
    if (!self || first < 0) return;
    self->m_first = first;
}

int XPieModelMapper_first(const XPieModelMapper* self)
{ return self ? self->m_first : 0; }

void XPieModelMapper_setCount(XPieModelMapper* self, int count)
{
    if (!self) return;
    self->m_count = count;
}

int XPieModelMapper_count(const XPieModelMapper* self)
{ return self ? self->m_count : -1; }

void XPieModelMapper_setOrientation(XPieModelMapper* self, int orientation)
{
    if (!self) return;
    self->m_orientation = orientation;
}

int XPieModelMapper_orientation(const XPieModelMapper* self)
{ return self ? self->m_orientation : 0; }

/* ==================== 专有参数 ==================== */
void XPieModelMapper_setValuesSection(XPieModelMapper* self, int section)
{
    if (!self) return;
    self->m_valuesSection = section;
}

int XPieModelMapper_valuesSection(const XPieModelMapper* self)
{ return self ? self->m_valuesSection : -1; }

void XPieModelMapper_setLabelsSection(XPieModelMapper* self, int section)
{
    if (!self) return;
    self->m_labelsSection = section;
}

int XPieModelMapper_labelsSection(const XPieModelMapper* self)
{ return self ? self->m_labelsSection : -1; }

/* ==================== 同步 ==================== */

bool XPieModelMapper_remap(XPieModelMapper* self)
{
    return false;   /* 基类不实现具体映射；由 H/V 具体子类承担。 */
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
