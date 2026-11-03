/**
 * @file       XBoxSet.c
 * @brief      XBoxSet 盒须图数据组实现（对标 QBoxSet）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XBoxSet.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool xboxset_canEmit(const XBoxSet* self)
{
    return self && ((const XObject*)self)->m_signalSlot;
}

static void xboxset_emitVoid(XBoxSet* self, size_t signal)
{
    XVarList* args;
    int dummy = 0;
    if (!xboxset_canEmit(self)) return;
    args = XVarList_Create(XVar(int, dummy));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xboxset_emitBool(XBoxSet* self, size_t signal, bool v)
{
    XVarList* args;
    if (!xboxset_canEmit(self)) return;
    args = XVarList_Create(XVar(bool, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xboxset_emitInt(XBoxSet* self, size_t signal, int v)
{
    XVarList* args;
    if (!xboxset_canEmit(self)) return;
    args = XVarList_Create(XVar(int, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xboxset_emitU32(XBoxSet* self, size_t signal, uint32_t v)
{
    XVarList* args;
    if (!xboxset_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xboxset_emitPen(XBoxSet* self, size_t signal, uint32_t color,
                            double width)
{
    XVarList* args;
    if (!xboxset_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, color), XVar(double, width));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 虚函数实现 ==================== */

static void VXBoxSet_deinit(XBoxSet* self)
{
    if (!self) return;
    if (self->m_label) {
        XClassDelete(self->m_label);
        self->m_label = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XBoxSet_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XBoxSet)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBoxSet_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XBoxSet_init(XBoxSet* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XBoxSet);
    self->m_label = XString_create();
    self->m_penWidth = 1.0;
    self->m_valueCount = 0;
}

void XBoxSet_init_ex(XBoxSet* self, const XString* label)
{
    XBoxSet_init(self);
    if (!self) return;
    XBoxSet_setLabel(self, label);
}

void XBoxSet_init_ex_5(XBoxSet* self, double le, double lq, double m, double uq,
                       double ue, const XString* label)
{
    XBoxSet_init(self);
    if (!self) return;
    self->m_values[XBoxSet_ValuePositions_LowerExtreme] = le;
    self->m_values[XBoxSet_ValuePositions_LowerQuartile] = lq;
    self->m_values[XBoxSet_ValuePositions_Median] = m;
    self->m_values[XBoxSet_ValuePositions_UpperQuartile] = uq;
    self->m_values[XBoxSet_ValuePositions_UpperExtreme] = ue;
    self->m_valueCount = XBOXSET_VALUE_COUNT;
    XBoxSet_setLabel(self, label);
}

XBoxSet* XBoxSet_create_ex(XMemoryType memory, const XString* label)
{
    XBoxSet* self = (XBoxSet*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XBoxSet_init_ex(self, label);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XBoxSet* XBoxSet_create_ex_2(XMemoryType memory, const char* label)
{
    XString* tmp = NULL;
    XBoxSet* self;
    if (label) {
        tmp = XString_create_utf8(label);
        if (!tmp) return NULL;
    }
    self = XBoxSet_create_ex(memory, tmp);
    if (tmp) XClassDelete(tmp);
    return self;
}

/* ==================== 标签 ==================== */

void XBoxSet_setLabel(XBoxSet* self, const XString* label)
{
    if (!self) return;
    if (!self->m_label) self->m_label = XString_create();
    if (!self->m_label) return;
    if (label)
        XString_assign(self->m_label, label);
    else
        XString_assign_utf8(self->m_label, "");
}

void XBoxSet_setLabel_utf8(XBoxSet* self, const char* label)
{
    XString* tmp = NULL;
    if (label) {
        tmp = XString_create_utf8(label);
        if (!tmp) return;
    }
    XBoxSet_setLabel(self, tmp);
    if (tmp) XClassDelete(tmp);
}

void XBoxSet_setLabel_2(XBoxSet* self, const char* label)
{ XBoxSet_setLabel_utf8(self, label); }

const XString* XBoxSet_label_const(const XBoxSet* self)
{ return (self && self->m_label) ? self->m_label : NULL; }

const XString* XBoxSet_label(const XBoxSet* self)
{ return XBoxSet_label_const(self); }

const char* XBoxSet_label_toUtf8(const XBoxSet* self)
{
    const XString* s = XBoxSet_label_const(self);
    return s ? XString_toUtf8(s) : "";
}

/* ==================== 取值 ==================== */

void XBoxSet_setValue(XBoxSet* self, int index, double value)
{
    if (!self || index < 0 || index >= XBOXSET_VALUE_COUNT) return;
    if (self->m_values[index] == value) return;
    self->m_values[index] = value;
    if (index >= self->m_valueCount) self->m_valueCount = (uint8_t)(index + 1);
    xboxset_emitInt(self, (size_t)XBoxSet_valueChanged_signal, index);
    xboxset_emitVoid(self, (size_t)XBoxSet_valuesChanged_signal);
}

double XBoxSet_at(const XBoxSet* self, int index)
{
    if (!self || index < 0 || index >= XBOXSET_VALUE_COUNT) return 0.0;
    return self->m_values[index];
}

void XBoxSet_clear(XBoxSet* self)
{
    if (!self) return;
    XMemset(self->m_values, 0, sizeof(self->m_values));
    self->m_valueCount = 0;
    xboxset_emitVoid(self, (size_t)XBoxSet_cleared_signal);
    xboxset_emitVoid(self, (size_t)XBoxSet_valuesChanged_signal);
}

int XBoxSet_count(const XBoxSet* self)
{ return self ? (int)self->m_valueCount : 0; }

/* ==================== 画笔 / 画刷 ==================== */

void XBoxSet_setPen(XBoxSet* self, uint32_t color, double width)
{
    if (!self) return;
    if (width < 0.0) width = 0.0;
    if (self->m_penColor == color && self->m_penWidth == width) return;
    self->m_penColor = color;
    self->m_penWidth = width;
    xboxset_emitPen(self, (size_t)XBoxSet_penChanged_signal, color, width);
}

void XBoxSet_pen(const XBoxSet* self, uint32_t* color, double* width)
{
    if (!self) return;
    if (color) *color = self->m_penColor;
    if (width) *width = self->m_penWidth;
}

void XBoxSet_setBrush(XBoxSet* self, uint32_t color)
{
    if (!self || self->m_brushColor == color) return;
    self->m_brushColor = color;
    xboxset_emitU32(self, (size_t)XBoxSet_brushChanged_signal, color);
}

uint32_t XBoxSet_brush(const XBoxSet* self)
{ return self ? self->m_brushColor : 0; }

/* ==================== 信号 ==================== */

void* XBoxSet_clicked_signal(XBoxSet* self)
{
    xboxset_emitVoid(self, (size_t)XBoxSet_clicked_signal);
    return (void*)(size_t)XBoxSet_clicked_signal;
}

void* XBoxSet_hovered_signal(XBoxSet* self, bool status)
{
    xboxset_emitBool(self, (size_t)XBoxSet_hovered_signal, status);
    return (void*)(size_t)XBoxSet_hovered_signal;
}

void* XBoxSet_pressed_signal(XBoxSet* self)
{
    xboxset_emitVoid(self, (size_t)XBoxSet_pressed_signal);
    return (void*)(size_t)XBoxSet_pressed_signal;
}

void* XBoxSet_released_signal(XBoxSet* self)
{
    xboxset_emitVoid(self, (size_t)XBoxSet_released_signal);
    return (void*)(size_t)XBoxSet_released_signal;
}

void* XBoxSet_doubleClicked_signal(XBoxSet* self)
{
    xboxset_emitVoid(self, (size_t)XBoxSet_doubleClicked_signal);
    return (void*)(size_t)XBoxSet_doubleClicked_signal;
}

void* XBoxSet_penChanged_signal(XBoxSet* self, uint32_t color, double width)
{
    xboxset_emitPen(self, (size_t)XBoxSet_penChanged_signal, color, width);
    return (void*)(size_t)XBoxSet_penChanged_signal;
}

void* XBoxSet_brushChanged_signal(XBoxSet* self, uint32_t color)
{
    xboxset_emitU32(self, (size_t)XBoxSet_brushChanged_signal, color);
    return (void*)(size_t)XBoxSet_brushChanged_signal;
}

void* XBoxSet_valuesChanged_signal(XBoxSet* self)
{
    xboxset_emitVoid(self, (size_t)XBoxSet_valuesChanged_signal);
    return (void*)(size_t)XBoxSet_valuesChanged_signal;
}

void* XBoxSet_valueChanged_signal(XBoxSet* self, int index)
{
    xboxset_emitInt(self, (size_t)XBoxSet_valueChanged_signal, index);
    return (void*)(size_t)XBoxSet_valueChanged_signal;
}

void* XBoxSet_cleared_signal(XBoxSet* self)
{
    xboxset_emitVoid(self, (size_t)XBoxSet_cleared_signal);
    return (void*)(size_t)XBoxSet_cleared_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
