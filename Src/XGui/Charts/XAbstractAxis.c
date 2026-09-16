/**
 * @file       XAbstractAxis.c
 * @brief      XAbstractAxis 抽象坐标轴基类实现（对标 QAbstractAxis）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XAbstractAxis.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static void xaxis_emitBool(XAbstractAxis* self, size_t signal, bool value)
{
    XVarList* args = XVarList_Create(XVar(bool, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xaxis_emitText(XAbstractAxis* self, size_t signal,
                           const XString* text)
{
    XVarList* args = XVarList_Create(XVar(const XString*, text));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xaxis_emitRange(XAbstractAxis* self, size_t signal,
                            double mi, double ma)
{
    XVarList* args = XVarList_Create(XVar(double, mi), XVar(double, ma));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 虚函数实现 ==================== */

static void VXAbstractAxis_deinit(XAbstractAxis* self)
{
    if (!self) return;
    if (self->m_titleText) {
        XString_delete_base(self->m_titleText);
        self->m_titleText = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static void VXAbstractAxis_copy(XAbstractAxis* self, const XAbstractAxis* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractAxis_init(self);
    XClass_Parent(XObject, EXClass_Copy,
                  void(*)(XObject*, const XObject*))((XObject*)self,
                                                     (const XObject*)other);
    self->m_visible = other->m_visible;
    self->m_gridLineVisible = other->m_gridLineVisible;
    self->m_min = other->m_min;
    self->m_max = other->m_max;
    self->m_reverse = other->m_reverse;
    self->m_labelsAngle = other->m_labelsAngle;
    self->m_shadesVisible = other->m_shadesVisible;
    self->m_linePenColor = other->m_linePenColor;
    self->m_labelsBrushColor = other->m_labelsBrushColor;
    if (other->m_titleText)
        XAbstractAxis_setTitleText(self, other->m_titleText);
    else if (self->m_titleText)
        XString_assign_utf8(self->m_titleText, "");
}

static void VXAbstractAxis_move(XAbstractAxis* self, XAbstractAxis* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractAxis_init(self);
    XClass_Parent(XObject, EXClass_Move,
                  void(*)(XObject*, XObject*))((XObject*)self,
                                               (XObject*)other);
    self->m_visible = other->m_visible;
    self->m_gridLineVisible = other->m_gridLineVisible;
    self->m_min = other->m_min;
    self->m_max = other->m_max;
    self->m_reverse = other->m_reverse;
    self->m_labelsAngle = other->m_labelsAngle;
    self->m_shadesVisible = other->m_shadesVisible;
    self->m_linePenColor = other->m_linePenColor;
    self->m_labelsBrushColor = other->m_labelsBrushColor;
    if (self->m_titleText) XString_delete_base(self->m_titleText);
    self->m_titleText = other->m_titleText;
    other->m_titleText = XString_create();
    other->m_visible = true;
    other->m_gridLineVisible = true;
    other->m_min = 0.0;
    other->m_max = 10.0;
    other->m_reverse = false;
    other->m_labelsAngle = 0;
    other->m_shadesVisible = false;
    other->m_linePenColor = 0;
    other->m_labelsBrushColor = 0;
}

/* ==================== 类初始化 ==================== */

XVtable* XAbstractAxis_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractAxis)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractAxis_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXAbstractAxis_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXAbstractAxis_move);
    return XVTABLE_DEFAULT;
}

void XAbstractAxis_init(XAbstractAxis* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XAbstractAxis);
    self->m_visible = true;
    self->m_gridLineVisible = true;
    self->m_min = 0.0;
    self->m_max = 10.0;
    self->m_reverse = false;
    self->m_titleText = XString_create();
    self->m_labelsAngle = 0;
    self->m_shadesVisible = false;
    self->m_linePenColor = 0;
    self->m_labelsBrushColor = 0;
}

XAbstractAxis* XAbstractAxis_create_ex(XMemoryType memory)
{
    XAbstractAxis* self =
        (XAbstractAxis*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XAbstractAxis_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 属性 ==================== */

void XAbstractAxis_setVisible(XAbstractAxis* self, bool visible)
{
    if (!self || self->m_visible == visible) return;
    self->m_visible = visible;
    xaxis_emitBool(self, (size_t)XAbstractAxis_visibleChanged_signal, visible);
}

bool XAbstractAxis_isVisible(const XAbstractAxis* self)
{ return self ? self->m_visible : false; }

void XAbstractAxis_setGridLineVisible(XAbstractAxis* self, bool visible)
{
    if (!self || self->m_gridLineVisible == visible) return;
    self->m_gridLineVisible = visible;
    xaxis_emitBool(self, (size_t)XAbstractAxis_gridLineVisibleChanged_signal,
                   visible);
}

bool XAbstractAxis_isGridLineVisible(const XAbstractAxis* self)
{ return self ? self->m_gridLineVisible : false; }

void XAbstractAxis_setRange(XAbstractAxis* self, double min, double max)
{
    if (!self || min > max) return;
    if (self->m_min == min && self->m_max == max) return;
    self->m_min = min;
    self->m_max = max;
    xaxis_emitRange(self, (size_t)XAbstractAxis_rangeChanged_signal, min, max);
}

double XAbstractAxis_min(const XAbstractAxis* self)
{ return self ? self->m_min : 0.0; }

double XAbstractAxis_max(const XAbstractAxis* self)
{ return self ? self->m_max : 0.0; }

void XAbstractAxis_setReverse(XAbstractAxis* self, bool reverse)
{
    if (!self || self->m_reverse == reverse) return;
    self->m_reverse = reverse;
    xaxis_emitBool(self, (size_t)XAbstractAxis_reverseChanged_signal, reverse);
}

bool XAbstractAxis_isReverse(const XAbstractAxis* self)
{ return self ? self->m_reverse : false; }

void XAbstractAxis_setTitleText(XAbstractAxis* self, const XString* title)
{
    if (!self) return;
    if (!self->m_titleText) self->m_titleText = XString_create();
    if (!self->m_titleText) return;
    if (title)
        XString_assign(self->m_titleText, title);
    else
        XString_assign_utf8(self->m_titleText, "");
    xaxis_emitText(self, (size_t)XAbstractAxis_titleTextChanged_signal,
                   self->m_titleText);
}

void XAbstractAxis_setTitleText_2(XAbstractAxis* self, const char* title)
{
    XString* tmp = NULL;
    if (title) {
        tmp = XString_create_utf8(title);
        if (!tmp) return;
    }
    XAbstractAxis_setTitleText(self, tmp);
    if (tmp) XString_delete_base(tmp);
}

const XString* XAbstractAxis_titleText(const XAbstractAxis* self)
{
    return (self && self->m_titleText) ? self->m_titleText : NULL;
}

const char* XAbstractAxis_titleText_2(const XAbstractAxis* self)
{
    const XString* s;
    s = XAbstractAxis_titleText(self);
    return s ? XString_toUtf8(s) : "";
}

void XAbstractAxis_setLabelsAngle(XAbstractAxis* self, int angle)
{
    if (self) self->m_labelsAngle = angle;
}

int XAbstractAxis_labelsAngle(const XAbstractAxis* self)
{ return self ? self->m_labelsAngle : 0; }

void XAbstractAxis_setShadesVisible(XAbstractAxis* self, bool visible)
{
    if (self) self->m_shadesVisible = visible;
}

bool XAbstractAxis_shadesVisible(const XAbstractAxis* self)
{ return self ? self->m_shadesVisible : false; }

void XAbstractAxis_setLinePenColor(XAbstractAxis* self, uint32_t color)
{
    if (self) self->m_linePenColor = color;
}

uint32_t XAbstractAxis_linePenColor(const XAbstractAxis* self)
{ return self ? self->m_linePenColor : 0; }

void XAbstractAxis_setLabelsBrushColor(XAbstractAxis* self, uint32_t color)
{
    if (self) self->m_labelsBrushColor = color;
}

uint32_t XAbstractAxis_labelsBrushColor(const XAbstractAxis* self)
{ return self ? self->m_labelsBrushColor : 0; }

/* ==================== 信号 ==================== */

void* XAbstractAxis_visibleChanged_signal(XAbstractAxis* self, bool visible)
{
    xaxis_emitBool(self, (size_t)XAbstractAxis_visibleChanged_signal, visible);
    return (void*)(size_t)XAbstractAxis_visibleChanged_signal;
}

void* XAbstractAxis_gridLineVisibleChanged_signal(XAbstractAxis* self,
                                                  bool visible)
{
    xaxis_emitBool(self,
                   (size_t)XAbstractAxis_gridLineVisibleChanged_signal,
                   visible);
    return (void*)(size_t)XAbstractAxis_gridLineVisibleChanged_signal;
}

void* XAbstractAxis_titleTextChanged_signal(XAbstractAxis* self,
                                            const XString* title)
{
    xaxis_emitText(self, (size_t)XAbstractAxis_titleTextChanged_signal, title);
    return (void*)(size_t)XAbstractAxis_titleTextChanged_signal;
}

void* XAbstractAxis_rangeChanged_signal(XAbstractAxis* self,
                                        double min, double max)
{
    xaxis_emitRange(self, (size_t)XAbstractAxis_rangeChanged_signal, min, max);
    return (void*)(size_t)XAbstractAxis_rangeChanged_signal;
}

void* XAbstractAxis_reverseChanged_signal(XAbstractAxis* self, bool reverse)
{
    xaxis_emitBool(self, (size_t)XAbstractAxis_reverseChanged_signal, reverse);
    return (void*)(size_t)XAbstractAxis_reverseChanged_signal;
}

#endif /* XCHARTS_ON */
