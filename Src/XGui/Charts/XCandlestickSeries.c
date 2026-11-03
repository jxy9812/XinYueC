/**
 * @file       XCandlestickSeries.c
 * @brief      XCandlestickSeries K 线图序列实现（对标 QCandlestickSeries）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XCandlestickSeries.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XClass.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool xcand_canEmit(const XCandlestickSeries* self)
{
    return self && ((const XObject*)self)->m_signalSlot;
}

static void xcand_emitVoid(XCandlestickSeries* self, size_t signal)
{
    XVarList* args;
    int dummy = 0;
    if (!xcand_canEmit(self)) return;
    args = XVarList_Create(XVar(int, dummy));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcand_emitBool(XCandlestickSeries* self, size_t signal, bool v)
{
    XVarList* args;
    if (!xcand_canEmit(self)) return;
    args = XVarList_Create(XVar(bool, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcand_emitDouble(XCandlestickSeries* self, size_t signal, double v)
{
    XVarList* args;
    if (!xcand_canEmit(self)) return;
    args = XVarList_Create(XVar(double, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcand_emitU32(XCandlestickSeries* self, size_t signal, uint32_t v)
{
    XVarList* args;
    if (!xcand_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcand_emitPen(XCandlestickSeries* self, size_t signal,
                          uint32_t color, double width)
{
    XVarList* args;
    if (!xcand_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, color), XVar(double, width));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcand_emitSet(XCandlestickSeries* self, size_t signal,
                          XCandlestickSet* set)
{
    XVarList* args;
    if (!xcand_canEmit(self)) return;
    args = XVarList_Create(XVar(XCandlestickSet*, set));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xcand_emitSetState(XCandlestickSeries* self, size_t signal,
                               bool st, XCandlestickSet* set)
{
    XVarList* args;
    if (!xcand_canEmit(self)) return;
    args = XVarList_Create(XVar(bool, st), XVar(XCandlestickSet*, set));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 内部助手 ==================== */

static bool xcand_grow(XCandlestickSeries* self)
{
    if (!self) return false;
    if (self->m_count < self->m_capacity) return true;
    {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 8;
        XCandlestickSet** a = (XCandlestickSet**)XRealloc_System(
            self->m_sets, sizeof(XCandlestickSet*) * (size_t)cap);
        if (!a) return false;
        self->m_sets = a;
        self->m_capacity = (uint16_t)cap;
    }
    return true;
}

static int xcand_indexOf(const XCandlestickSeries* self,
                         const XCandlestickSet* set)
{
    int i;
    if (!self || !self->m_sets) return -1;
    for (i = 0; i < (int)self->m_count; ++i)
        if (self->m_sets[i] == set) return i;
    return -1;
}

/* ==================== 虚函数实现 ==================== */

static void VXCandlestickSeries_deinit(XCandlestickSeries* self)
{
    int i;
    if (!self) return;
    if (self->m_sets) {
        for (i = 0; i < (int)self->m_count; ++i) {
            if (self->m_sets[i]) {
                XClassDelete(self->m_sets[i]);
                self->m_sets[i] = NULL;
            }
        }
        XFree_System(self->m_sets);
        self->m_sets = NULL;
    }
    self->m_count = 0;
    self->m_capacity = 0;
    XClass_Deinit_Parent(XAbstractSeries, &self->m_base);
}

XVtable* XCandlestickSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XCandlestickSeries)
    XVTABLE_INHERIT_XCLASS(XAbstractSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCandlestickSeries_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XCandlestickSeries_init(XCandlestickSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractSeries_init(&self->m_base);
    XClassSetVtable(self, XCandlestickSeries);
    XAbstractSeries_setName_2(&self->m_base, "candlestick");
    self->m_base.m_type = XChartSeriesType_Candlestick;
    /* Qt 默认值：柱宽上下界 1.0/0，实体 0.8、帽 0.5，轮廓与帽默认可见。 */
    self->m_maximumColumnWidth = 1.0;
    self->m_minimumColumnWidth = 0.0;
    self->m_bodyWidth = 0.8;
    self->m_capsWidth = 0.5;
    self->m_penWidth = 1.0;
    self->m_increasingColor = 0xFF00FF00u;
    self->m_decreasingColor = 0xFFFF0000u;
    self->m_bodyOutlineVisible = true;
    self->m_capsVisible = true;
}

XCandlestickSeries* XCandlestickSeries_create_ex(XMemoryType memory)
{
    XCandlestickSeries* self =
        (XCandlestickSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XCandlestickSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XCandlestickSeries_type(const XCandlestickSeries* self)
{
    (void)self;
    return XChartSeriesType_Candlestick;
}

/* ==================== 集合 ==================== */

bool XCandlestickSeries_append(XCandlestickSeries* self, XCandlestickSet* set)
{
    if (!self || !set) return false;
    if (xcand_indexOf(self, set) >= 0) return false;
    if (!xcand_grow(self)) return false;
    self->m_sets[self->m_count++] = set;
    xcand_emitVoid(self, (size_t)XCandlestickSeries_countChanged_signal);
    return true;
}

bool XCandlestickSeries_appendList(XCandlestickSeries* self,
                                   XCandlestickSet* const* sets, int count)
{
    int i;
    if (!self || !sets || count <= 0) return false;
    for (i = 0; i < count; ++i)
        if (!sets[i] || xcand_indexOf(self, sets[i]) >= 0) return false;
    for (i = 0; i < count; ++i) {
        if (!xcand_grow(self)) return false;
        self->m_sets[self->m_count++] = sets[i];
    }
    xcand_emitVoid(self, (size_t)XCandlestickSeries_countChanged_signal);
    return true;
}

bool XCandlestickSeries_insert(XCandlestickSeries* self, int index,
                               XCandlestickSet* set)
{
    int i;
    if (!self || !set) return false;
    if (xcand_indexOf(self, set) >= 0) return false;
    if (index < 0) index = 0;
    if (index > (int)self->m_count) index = (int)self->m_count;
    if (!xcand_grow(self)) return false;
    for (i = (int)self->m_count; i > index; --i)
        self->m_sets[i] = self->m_sets[i - 1];
    self->m_sets[index] = set;
    ++self->m_count;
    xcand_emitVoid(self, (size_t)XCandlestickSeries_countChanged_signal);
    return true;
}

bool XCandlestickSeries_take(XCandlestickSeries* self, XCandlestickSet* set)
{
    int idx;
    int i;
    if (!self || !set) return false;
    idx = xcand_indexOf(self, set);
    if (idx < 0) return false;
    for (i = idx; i + 1 < (int)self->m_count; ++i)
        self->m_sets[i] = self->m_sets[i + 1];
    self->m_sets[--self->m_count] = NULL;
    xcand_emitVoid(self, (size_t)XCandlestickSeries_countChanged_signal);
    return true;
}

bool XCandlestickSeries_remove(XCandlestickSeries* self, XCandlestickSet* set)
{
    if (!XCandlestickSeries_take(self, set)) return false;
    /* set 可能正处于自身信号发射帧内，延迟回收。 */
    XObject_deleteLater((XObject*)set);
    return true;
}

bool XCandlestickSeries_removeList(XCandlestickSeries* self,
                                   XCandlestickSet* const* sets, int count)
{
    int i;
    bool all = true;
    if (!self || !sets || count <= 0) return false;
    for (i = 0; i < count; ++i)
        if (!XCandlestickSeries_remove(self, sets[i])) all = false;
    return all;
}

void XCandlestickSeries_clear(XCandlestickSeries* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < (int)self->m_count; ++i) {
        if (self->m_sets[i]) {
            XObject_deleteLater((XObject*)self->m_sets[i]);
            self->m_sets[i] = NULL;
        }
    }
    self->m_count = 0;
    xcand_emitVoid(self, (size_t)XCandlestickSeries_countChanged_signal);
}

int XCandlestickSeries_count(const XCandlestickSeries* self)
{ return self ? (int)self->m_count : 0; }

XCandlestickSet* XCandlestickSeries_setAt(const XCandlestickSeries* self,
                                          int index)
{
    if (!self || index < 0 || index >= (int)self->m_count) return NULL;
    return self->m_sets[index];
}

int XCandlestickSeries_sets(const XCandlestickSeries* self,
                            XCandlestickSet** out, int maxCount)
{
    int i;
    int n;
    if (!self || !out || maxCount <= 0) return 0;
    n = (int)self->m_count < maxCount ? (int)self->m_count : maxCount;
    for (i = 0; i < n; ++i) out[i] = self->m_sets[i];
    return n;
}

/* ==================== 外观 ==================== */

void XCandlestickSeries_setMaximumColumnWidth(XCandlestickSeries* self,
                                              double width)
{
    if (!self || self->m_maximumColumnWidth == width) return;
    self->m_maximumColumnWidth = width;
    xcand_emitDouble(self,
                     (size_t)XCandlestickSeries_maximumColumnWidthChanged_signal,
                     width);
}

double XCandlestickSeries_maximumColumnWidth(const XCandlestickSeries* self)
{ return self ? self->m_maximumColumnWidth : 0.0; }

void XCandlestickSeries_setMinimumColumnWidth(XCandlestickSeries* self,
                                              double width)
{
    if (!self || self->m_minimumColumnWidth == width) return;
    self->m_minimumColumnWidth = width;
    xcand_emitDouble(self,
                     (size_t)XCandlestickSeries_minimumColumnWidthChanged_signal,
                     width);
}

double XCandlestickSeries_minimumColumnWidth(const XCandlestickSeries* self)
{ return self ? self->m_minimumColumnWidth : 0.0; }

void XCandlestickSeries_setBodyWidth(XCandlestickSeries* self, double width)
{
    if (!self || self->m_bodyWidth == width) return;
    self->m_bodyWidth = width;
    xcand_emitDouble(self, (size_t)XCandlestickSeries_bodyWidthChanged_signal,
                     width);
}

double XCandlestickSeries_bodyWidth(const XCandlestickSeries* self)
{ return self ? self->m_bodyWidth : 0.0; }

void XCandlestickSeries_setBodyOutlineVisible(XCandlestickSeries* self,
                                              bool visible)
{
    if (!self || self->m_bodyOutlineVisible == visible) return;
    self->m_bodyOutlineVisible = visible;
    xcand_emitBool(self,
                   (size_t)XCandlestickSeries_bodyOutlineVisibilityChanged_signal,
                   visible);
}

bool XCandlestickSeries_bodyOutlineVisible(const XCandlestickSeries* self)
{ return self ? self->m_bodyOutlineVisible : false; }

void XCandlestickSeries_setCapsWidth(XCandlestickSeries* self, double width)
{
    if (!self || self->m_capsWidth == width) return;
    self->m_capsWidth = width;
    xcand_emitDouble(self, (size_t)XCandlestickSeries_capsWidthChanged_signal,
                     width);
}

double XCandlestickSeries_capsWidth(const XCandlestickSeries* self)
{ return self ? self->m_capsWidth : 0.0; }

void XCandlestickSeries_setCapsVisible(XCandlestickSeries* self, bool visible)
{
    if (!self || self->m_capsVisible == visible) return;
    self->m_capsVisible = visible;
    xcand_emitBool(self, (size_t)XCandlestickSeries_capsVisibilityChanged_signal,
                   visible);
}

bool XCandlestickSeries_capsVisible(const XCandlestickSeries* self)
{ return self ? self->m_capsVisible : false; }

void XCandlestickSeries_setIncreasingColor(XCandlestickSeries* self,
                                           uint32_t color)
{
    if (!self || self->m_increasingColor == color) return;
    self->m_increasingColor = color;
    xcand_emitU32(self,
                  (size_t)XCandlestickSeries_increasingColorChanged_signal,
                  color);
}

uint32_t XCandlestickSeries_increasingColor(const XCandlestickSeries* self)
{ return self ? self->m_increasingColor : 0; }

void XCandlestickSeries_setDecreasingColor(XCandlestickSeries* self,
                                           uint32_t color)
{
    if (!self || self->m_decreasingColor == color) return;
    self->m_decreasingColor = color;
    xcand_emitU32(self,
                  (size_t)XCandlestickSeries_decreasingColorChanged_signal,
                  color);
}

uint32_t XCandlestickSeries_decreasingColor(const XCandlestickSeries* self)
{ return self ? self->m_decreasingColor : 0; }

void XCandlestickSeries_setBrush(XCandlestickSeries* self, uint32_t color)
{
    if (!self || self->m_brushColor == color) return;
    self->m_brushColor = color;
    xcand_emitU32(self, (size_t)XCandlestickSeries_brushChanged_signal, color);
}

uint32_t XCandlestickSeries_brush(const XCandlestickSeries* self)
{ return self ? self->m_brushColor : 0; }

void XCandlestickSeries_setPen(XCandlestickSeries* self, uint32_t color,
                               double width)
{
    if (!self) return;
    if (width < 0.0) width = 0.0;
    if (self->m_penColor == color && self->m_penWidth == width) return;
    self->m_penColor = color;
    self->m_penWidth = width;
    xcand_emitPen(self, (size_t)XCandlestickSeries_penChanged_signal, color,
                  width);
}

void XCandlestickSeries_pen(const XCandlestickSeries* self, uint32_t* color,
                            double* width)
{
    if (!self) return;
    if (color) *color = self->m_penColor;
    if (width) *width = self->m_penWidth;
}

/* ==================== 信号 ==================== */

void* XCandlestickSeries_clicked_signal(XCandlestickSeries* self,
                                        XCandlestickSet* set)
{
    xcand_emitSet(self, (size_t)XCandlestickSeries_clicked_signal, set);
    return (void*)(size_t)XCandlestickSeries_clicked_signal;
}

void* XCandlestickSeries_hovered_signal(XCandlestickSeries* self, bool status,
                                        XCandlestickSet* set)
{
    xcand_emitSetState(self, (size_t)XCandlestickSeries_hovered_signal, status,
                       set);
    return (void*)(size_t)XCandlestickSeries_hovered_signal;
}

void* XCandlestickSeries_pressed_signal(XCandlestickSeries* self,
                                        XCandlestickSet* set)
{
    xcand_emitSet(self, (size_t)XCandlestickSeries_pressed_signal, set);
    return (void*)(size_t)XCandlestickSeries_pressed_signal;
}

void* XCandlestickSeries_released_signal(XCandlestickSeries* self,
                                         XCandlestickSet* set)
{
    xcand_emitSet(self, (size_t)XCandlestickSeries_released_signal, set);
    return (void*)(size_t)XCandlestickSeries_released_signal;
}

void* XCandlestickSeries_doubleClicked_signal(XCandlestickSeries* self,
                                              XCandlestickSet* set)
{
    xcand_emitSet(self, (size_t)XCandlestickSeries_doubleClicked_signal, set);
    return (void*)(size_t)XCandlestickSeries_doubleClicked_signal;
}

void* XCandlestickSeries_candlestickSetsAdded_signal(XCandlestickSeries* self,
                                                     XCandlestickSet* const* sets,
                                                     int count)
{
    (void)sets;
    (void)count;
    xcand_emitVoid(self,
                   (size_t)XCandlestickSeries_candlestickSetsAdded_signal);
    return (void*)(size_t)XCandlestickSeries_candlestickSetsAdded_signal;
}

void* XCandlestickSeries_candlestickSetsRemoved_signal(
    XCandlestickSeries* self, XCandlestickSet* const* sets, int count)
{
    (void)sets;
    (void)count;
    xcand_emitVoid(self,
                   (size_t)XCandlestickSeries_candlestickSetsRemoved_signal);
    return (void*)(size_t)XCandlestickSeries_candlestickSetsRemoved_signal;
}

void* XCandlestickSeries_countChanged_signal(XCandlestickSeries* self)
{
    xcand_emitVoid(self, (size_t)XCandlestickSeries_countChanged_signal);
    return (void*)(size_t)XCandlestickSeries_countChanged_signal;
}

void* XCandlestickSeries_maximumColumnWidthChanged_signal(
    XCandlestickSeries* self, double width)
{
    xcand_emitDouble(
        self, (size_t)XCandlestickSeries_maximumColumnWidthChanged_signal,
        width);
    return (void*)(size_t)XCandlestickSeries_maximumColumnWidthChanged_signal;
}

void* XCandlestickSeries_minimumColumnWidthChanged_signal(
    XCandlestickSeries* self, double width)
{
    xcand_emitDouble(
        self, (size_t)XCandlestickSeries_minimumColumnWidthChanged_signal,
        width);
    return (void*)(size_t)XCandlestickSeries_minimumColumnWidthChanged_signal;
}

void* XCandlestickSeries_bodyWidthChanged_signal(XCandlestickSeries* self,
                                                 double width)
{
    xcand_emitDouble(self,
                     (size_t)XCandlestickSeries_bodyWidthChanged_signal, width);
    return (void*)(size_t)XCandlestickSeries_bodyWidthChanged_signal;
}

void* XCandlestickSeries_bodyOutlineVisibilityChanged_signal(
    XCandlestickSeries* self, bool visible)
{
    xcand_emitBool(
        self, (size_t)XCandlestickSeries_bodyOutlineVisibilityChanged_signal,
        visible);
    return (void*)(size_t)XCandlestickSeries_bodyOutlineVisibilityChanged_signal;
}

void* XCandlestickSeries_capsWidthChanged_signal(XCandlestickSeries* self,
                                                 double width)
{
    xcand_emitDouble(self,
                     (size_t)XCandlestickSeries_capsWidthChanged_signal, width);
    return (void*)(size_t)XCandlestickSeries_capsWidthChanged_signal;
}

void* XCandlestickSeries_capsVisibilityChanged_signal(XCandlestickSeries* self,
                                                      bool visible)
{
    xcand_emitBool(self,
                   (size_t)XCandlestickSeries_capsVisibilityChanged_signal,
                   visible);
    return (void*)(size_t)XCandlestickSeries_capsVisibilityChanged_signal;
}

void* XCandlestickSeries_increasingColorChanged_signal(XCandlestickSeries* self,
                                                       uint32_t color)
{
    xcand_emitU32(
        self, (size_t)XCandlestickSeries_increasingColorChanged_signal, color);
    return (void*)(size_t)XCandlestickSeries_increasingColorChanged_signal;
}

void* XCandlestickSeries_decreasingColorChanged_signal(XCandlestickSeries* self,
                                                       uint32_t color)
{
    xcand_emitU32(
        self, (size_t)XCandlestickSeries_decreasingColorChanged_signal, color);
    return (void*)(size_t)XCandlestickSeries_decreasingColorChanged_signal;
}

void* XCandlestickSeries_brushChanged_signal(XCandlestickSeries* self,
                                             uint32_t color)
{
    xcand_emitU32(self, (size_t)XCandlestickSeries_brushChanged_signal, color);
    return (void*)(size_t)XCandlestickSeries_brushChanged_signal;
}

void* XCandlestickSeries_penChanged_signal(XCandlestickSeries* self,
                                           uint32_t color, double width)
{
    xcand_emitPen(self, (size_t)XCandlestickSeries_penChanged_signal, color,
                  width);
    return (void*)(size_t)XCandlestickSeries_penChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
