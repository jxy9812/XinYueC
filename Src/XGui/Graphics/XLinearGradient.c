/**
 * @file       XLinearGradient.c
 * @brief      XLinearGradient 线性渐变实现（对标 QLinearGradient）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XLinearGradient.h"

#include "XAlgorithm.h"
#include "XMemory.h"

/* ==================== 虚函数实现 ==================== */

static void VXLinearGradient_deinit(XLinearGradient* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XGradient, &self->m_base);
}

static void VXLinearGradient_copy(XLinearGradient* self,
                                  const XLinearGradient* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XLinearGradient_init(self);
    XClass_Parent(XGradient, EXClass_Copy,
                  void(*)(XGradient*, const XGradient*))(&self->m_base,
                                                         &other->m_base);
    self->m_start = other->m_start;
    self->m_finalStop = other->m_finalStop;
}

static void VXLinearGradient_move(XLinearGradient* self,
                                  XLinearGradient* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XLinearGradient_init(self);
    XClass_Parent(XGradient, EXClass_Move,
                  void(*)(XGradient*, XGradient*))(&self->m_base,
                                                   &other->m_base);
    self->m_start = other->m_start;
    self->m_finalStop = other->m_finalStop;
    other->m_start.x = 0.0f;
    other->m_start.y = 0.0f;
    other->m_finalStop.x = 1.0f;
    other->m_finalStop.y = 0.0f;
}

XVtable* XLinearGradient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLinearGradient)
    XVTABLE_INHERIT_XCLASS(XGradient);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLinearGradient_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXLinearGradient_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXLinearGradient_move);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XLinearGradient_init(XLinearGradient* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XGradient_init(&self->m_base);
    XClassSetVtable(self, XLinearGradient);
    self->m_base.m_type = XGradient_Type_Linear;
    /* Qt 默认：(0,0) → (1,0)。 */
    self->m_start.x = 0.0f;
    self->m_start.y = 0.0f;
    self->m_finalStop.x = 1.0f;
    self->m_finalStop.y = 0.0f;
}

void XLinearGradient_init_ex(XLinearGradient* self, double x1, double y1,
                             double x2, double y2)
{
    XLinearGradient_init(self);
    if (!self) return;
    self->m_start.x = (float)x1;
    self->m_start.y = (float)y1;
    self->m_finalStop.x = (float)x2;
    self->m_finalStop.y = (float)y2;
}

XLinearGradient* XLinearGradient_create_ex(XMemoryType memory)
{
    XLinearGradient* self =
        (XLinearGradient*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XLinearGradient_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XLinearGradient* XLinearGradient_create_ex_2(XMemoryType memory, double x1,
                                             double y1, double x2, double y2)
{
    XLinearGradient* self =
        (XLinearGradient*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XLinearGradient_init_ex(self, x1, y1, x2, y2);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 几何 ==================== */

void XLinearGradient_setStart(XLinearGradient* self, double x, double y)
{
    if (!self) return;
    self->m_start.x = (float)x;
    self->m_start.y = (float)y;
}

XPointF XLinearGradient_start(const XLinearGradient* self)
{
    XPointF p;
    p.x = 0.0f;
    p.y = 0.0f;
    if (self) p = self->m_start;
    return p;
}

void XLinearGradient_setFinalStop(XLinearGradient* self, double x, double y)
{
    if (!self) return;
    self->m_finalStop.x = (float)x;
    self->m_finalStop.y = (float)y;
}

XPointF XLinearGradient_finalStop(const XLinearGradient* self)
{
    XPointF p;
    p.x = 1.0f;
    p.y = 0.0f;
    if (self) p = self->m_finalStop;
    return p;
}

/* ==================== 绘制层桥接 ==================== */

bool XLinearGradient_toPainterGradient(const XLinearGradient* self,
                                       XPainterGradient* out)
{
    if (!self || !out) return false;
    XPainterGradient_initLinear(out, self->m_start.x, self->m_start.y,
                                self->m_finalStop.x, self->m_finalStop.y);
    XGradient_applyStopsToPainter(&self->m_base, out);
    return true;
}

#endif /* XGUI_ON */
