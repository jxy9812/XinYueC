/**
 * @file       XBarModelMapper.c
 * @brief      XBarModelMapper 模型映射器基类实现（对标 QBarModelMapper）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XBarModelMapper.h"
#include "XModelMapper_Protected.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXBarModelMapper_deinit(XBarModelMapper* self)
{
    if (!self) return;
    /* 模型与序列均为借用指针，不在此释放。 */
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XBarModelMapper_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XBarModelMapper)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBarModelMapper_deinit);
    return XVTABLE_DEFAULT;
}

void XBarModelMapper_init(XBarModelMapper* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XBarModelMapper);
    self->m_first = 0;
    self->m_count = -1;             /* Qt 默认：按模型维度推导 */
    self->m_orientation = XModelMapper_Orientation_Vertical;
    self->m_firstBarSetSection = -1;
    self->m_lastBarSetSection = -1;
}

XBarModelMapper* XBarModelMapper_create_ex(XMemoryType memory)
{
    XBarModelMapper* self = (XBarModelMapper*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XBarModelMapper_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 模型与序列 ==================== */

void XBarModelMapper_setModel(XBarModelMapper* self, XAbstractItemModel* model)
{
    if (!self) return;
    self->m_model = model;
}

XAbstractItemModel* XBarModelMapper_model(const XBarModelMapper* self)
{ return self ? self->m_model : NULL; }

void XBarModelMapper_setSeries(XBarModelMapper* self, XAbstractBarSeries* series)
{
    if (!self) return;
    self->m_series = series;
}

XAbstractBarSeries* XBarModelMapper_series(const XBarModelMapper* self)
{ return self ? self->m_series : NULL; }

/* ==================== 通用参数 ==================== */

void XBarModelMapper_setFirst(XBarModelMapper* self, int first)
{
    if (!self || first < 0) return;
    self->m_first = first;
}

int XBarModelMapper_first(const XBarModelMapper* self)
{ return self ? self->m_first : 0; }

void XBarModelMapper_setCount(XBarModelMapper* self, int count)
{
    if (!self) return;
    self->m_count = count;
}

int XBarModelMapper_count(const XBarModelMapper* self)
{ return self ? self->m_count : -1; }

void XBarModelMapper_setOrientation(XBarModelMapper* self, int orientation)
{
    if (!self) return;
    self->m_orientation = orientation;
}

int XBarModelMapper_orientation(const XBarModelMapper* self)
{ return self ? self->m_orientation : 0; }

/* ==================== 专有参数 ==================== */
void XBarModelMapper_setFirstBarSetSection(XBarModelMapper* self, int section)
{
    if (!self) return;
    self->m_firstBarSetSection = section;
}

int XBarModelMapper_firstBarSetSection(const XBarModelMapper* self)
{ return self ? self->m_firstBarSetSection : -1; }

void XBarModelMapper_setLastBarSetSection(XBarModelMapper* self, int section)
{
    if (!self) return;
    self->m_lastBarSetSection = section;
}

int XBarModelMapper_lastBarSetSection(const XBarModelMapper* self)
{ return self ? self->m_lastBarSetSection : -1; }

/* ==================== 同步 ==================== */

bool XBarModelMapper_remap(XBarModelMapper* self)
{
    return false;   /* 基类不实现具体映射；由 H/V 具体子类承担。 */
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
