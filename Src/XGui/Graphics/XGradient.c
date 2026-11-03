/**
 * @file       XGradient.c
 * @brief      XGradient 渐变基类实现（对标 QGradient）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XGradient.h"

#include "XAlgorithm.h"
#include "XMemory.h"

/* ==================== 虚函数实现 ==================== */

static void VXGradient_deinit(XGradient* self)
{
    if (!self) return;
    /* 停止点为定长内联数组，无堆资源需要释放。 */
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXGradient_copy(XGradient* self, const XGradient* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XGradient_init(self);
    XClass_Parent(XClass, EXClass_Copy,
                  void(*)(XClass*, const XClass*))((XClass*)self,
                                                   (const XClass*)other);
    self->m_stopCount = other->m_stopCount;
    self->m_type = other->m_type;
    self->m_spread = other->m_spread;
    self->m_coordinateMode = other->m_coordinateMode;
    self->m_interpolationMode = other->m_interpolationMode;
    XMemcpy(self->m_stops, other->m_stops, sizeof(self->m_stops));
}

static void VXGradient_move(XGradient* self, XGradient* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XGradient_init(self);
    XClass_Parent(XClass, EXClass_Move,
                  void(*)(XClass*, XClass*))((XClass*)self, (XClass*)other);
    self->m_stopCount = other->m_stopCount;
    self->m_type = other->m_type;
    self->m_spread = other->m_spread;
    self->m_coordinateMode = other->m_coordinateMode;
    self->m_interpolationMode = other->m_interpolationMode;
    XMemcpy(self->m_stops, other->m_stops, sizeof(self->m_stops));
    /* 复原 other 为默认空渐变。 */
    other->m_stopCount = 0;
    other->m_type = XGradient_Type_NoGradient;
    other->m_spread = XGradient_Spread_Pad;
    other->m_coordinateMode = XGradient_CoordinateMode_Logical;
    other->m_interpolationMode = XGradient_InterpolationMode_Color;
}

XVtable* XGradient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGradient)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXGradient_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXGradient_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXGradient_move);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XGradient_init(XGradient* self)
{
    if (ISNULL(self, "XGradient")) return;
    XMemset(self, 0, sizeof(XGradient));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XGradient);
    self->m_stopCount = 0;
    self->m_type = XGradient_Type_NoGradient;
    self->m_spread = XGradient_Spread_Pad;
    self->m_coordinateMode = XGradient_CoordinateMode_Logical;
    self->m_interpolationMode = XGradient_InterpolationMode_Color;
}

XGradient* XGradient_create_ex(XMemoryType memory)
{
    XGradient* self = (XGradient*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XGradient_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共属性 ==================== */

int XGradient_type(const XGradient* self)
{ return self ? self->m_type : (int)XGradient_Type_NoGradient; }

int XGradient_spread(const XGradient* self)
{ return self ? self->m_spread : (int)XGradient_Spread_Pad; }

int XGradient_coordinateMode(const XGradient* self)
{ return self ? self->m_coordinateMode : (int)XGradient_CoordinateMode_Logical; }

void XGradient_setCoordinateMode(XGradient* self, int mode)
{
    if (self) self->m_coordinateMode = mode;
}

int XGradient_interpolationMode(const XGradient* self)
{ return self ? self->m_interpolationMode : (int)XGradient_InterpolationMode_Color; }

void XGradient_setInterpolationMode(XGradient* self, int mode)
{
    if (self) self->m_interpolationMode = mode;
}

/* ==================== 停止点 ==================== */

void XGradient_setColorAt(XGradient* self, double pos, uint32_t color)
{
    int i;
    if (!self) return;
    if (pos < 0.0) pos = 0.0;
    if (pos > 1.0) pos = 1.0;
    /* 同位置覆盖（对标 QGradient::setColorAt 的覆盖语义）。 */
    for (i = 0; i < self->m_stopCount; ++i) {
        if (self->m_stops[i].m_position == pos) {
            self->m_stops[i].m_color = color;
            return;
        }
    }
    if (self->m_stopCount >= XGRADIENT_MAX_STOPS) return;
    /* 按位置升序插入。 */
    {
        int j = self->m_stopCount;
        while (j > 0 && self->m_stops[j - 1].m_position > pos) {
            self->m_stops[j] = self->m_stops[j - 1];
            --j;
        }
        self->m_stops[j].m_position = pos;
        self->m_stops[j].m_color = color;
        ++self->m_stopCount;
    }
}

void XGradient_setStops(XGradient* self, const XGradientStop* stops, int count)
{
    int i;
    if (!self) return;
    self->m_stopCount = 0;
    if (!stops || count <= 0) return;
    for (i = 0; i < count; ++i)
        XGradient_setColorAt(self, stops[i].m_position, stops[i].m_color);
}

int XGradient_stopCount(const XGradient* self)
{ return self ? self->m_stopCount : 0; }

bool XGradient_stopAt(const XGradient* self, int index, XGradientStop* out)
{
    if (!self || !out || index < 0 || index >= self->m_stopCount) return false;
    *out = self->m_stops[index];
    return true;
}

void XGradient_clearStops(XGradient* self)
{
    if (self) self->m_stopCount = 0;
}

/* ==================== 绘制层桥接 ==================== */

void XGradient_applyStopsToPainter(const XGradient* self, XPainterGradient* out)
{
    int i;
    if (!self || !out) return;
    for (i = 0; i < self->m_stopCount; ++i)
        XPainterGradient_addStop(out, (float)self->m_stops[i].m_position,
                                 self->m_stops[i].m_color);
}

bool XGradient_toPainterGradient(const XGradient* self, XPainterGradient* out)
{
    if (!self || !out) return false;
    /* 基类只填充类型与停止点；几何参数由派生类补齐（见
     * XLinearGradient_toPainterGradient）。 */
    switch (self->m_type) {
    case XGradient_Type_Linear:
        XPainterGradient_initLinear(out, 0.0f, 0.0f, 1.0f, 0.0f);
        break;
    case XGradient_Type_Radial:
        XPainterGradient_initRadial(out, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
        break;
    case XGradient_Type_Conical:
        XPainterGradient_initConical(out, 0.5f, 0.5f, 0.0f);
        break;
    default:
        return false;
    }
    XGradient_applyStopsToPainter(self, out);
    return true;
}

/* ==================== 比较 ==================== */

bool XGradient_equals(const XGradient* a, const XGradient* b)
{
    int i;
    if (!a || !b) return false;
    if (a == b) return true;
    if (a->m_type != b->m_type) return false;
    if (a->m_spread != b->m_spread) return false;
    if (a->m_coordinateMode != b->m_coordinateMode) return false;
    if (a->m_interpolationMode != b->m_interpolationMode) return false;
    if (a->m_stopCount != b->m_stopCount) return false;
    for (i = 0; i < a->m_stopCount; ++i) {
        if (a->m_stops[i].m_position != b->m_stops[i].m_position) return false;
        if (a->m_stops[i].m_color != b->m_stops[i].m_color) return false;
    }
    return true;
}

#endif /* XGUI_ON */
