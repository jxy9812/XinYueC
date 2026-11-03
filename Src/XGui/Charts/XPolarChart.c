/**
 * @file       XPolarChart.c
 * @brief      XPolarChart 极坐标图表实现（对标 QPolarChart）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XPolarChart.h"

#include "XAlgorithm.h"
#include "XMemory.h"

#if XCHARTS_ON

/* ==================== 虚函数实现 ==================== */

static void VXPolarChart_deinit(XPolarChart* self)
{
    if (!self) return;
    /* 轴与序列均由基类 XChart 拥有并释放；此处只清借用指针。 */
    self->m_radialAxis = NULL;
    self->m_angularAxis = NULL;
    XClass_Deinit_Parent(XChart, &self->m_base);
}

XVtable* XPolarChart_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPolarChart)
    XVTABLE_INHERIT_XCLASS(XChart);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPolarChart_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XPolarChart_init(XPolarChart* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XChart_init(&self->m_base);
    XClassSetVtable(self, XPolarChart);
    self->m_base.m_chartType = XChart_ChartType_Polar;
    self->m_radialAxis = NULL;
    self->m_angularAxis = NULL;
}

XPolarChart* XPolarChart_create_ex(XMemoryType memory)
{
    XPolarChart* self = (XPolarChart*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XPolarChart_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 轴角色绑定 ==================== */

void XPolarChart_addAxis(XPolarChart* self, XAbstractAxis* axis,
                         int polarOrientation)
{
    if (!self || !axis) return;
    if (polarOrientation & XPolarChart_PolarOrientation_Radial)
        self->m_radialAxis = axis;
    if (polarOrientation & XPolarChart_PolarOrientation_Angular)
        self->m_angularAxis = axis;
    /* 同步到基类的轴槽，使既有布局/缩放/主题逻辑可复用：
     * 径向轴承担 Y 语义（半径），角度轴承担 X 语义（扫掠）。 */
    if (polarOrientation & XPolarChart_PolarOrientation_Radial)
        XChart_setAxisY_abstract(&self->m_base, axis);
    else if (polarOrientation & XPolarChart_PolarOrientation_Angular)
        XChart_setAxisX_abstract(&self->m_base, axis);
}

int XPolarChart_axes(const XPolarChart* self, int polarOrientation,
                     XAbstractAxis** out, int maxCount)
{
    int n = 0;
    if (!self || !out || maxCount <= 0) return 0;
    if (polarOrientation == 0)
        polarOrientation = XPolarChart_PolarOrientation_Radial |
                           XPolarChart_PolarOrientation_Angular;
    if ((polarOrientation & XPolarChart_PolarOrientation_Radial) &&
        self->m_radialAxis && n < maxCount)
        out[n++] = self->m_radialAxis;
    if ((polarOrientation & XPolarChart_PolarOrientation_Angular) &&
        self->m_angularAxis && n < maxCount)
        out[n++] = self->m_angularAxis;
    return n;
}

int XPolarChart_axisPolarOrientation(const XPolarChart* self,
                                     XAbstractAxis* axis)
{
    int role = 0;
    if (!self || !axis) return 0;
    if (self->m_radialAxis == axis) role |= XPolarChart_PolarOrientation_Radial;
    if (self->m_angularAxis == axis) role |= XPolarChart_PolarOrientation_Angular;
    return role;
}

int XPolarChart_chartType(const XPolarChart* self)
{
    (void)self;
    return XChart_ChartType_Polar;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
