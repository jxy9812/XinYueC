/**
 * @file       XBoxPlotSeries.c
 * @brief      XBoxPlotSeries 盒须图序列实现（对标 QBoxPlotSeries）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XBoxPlotSeries.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XClass.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool xboxps_canEmit(const XBoxPlotSeries* self)
{
    return self && ((const XObject*)self)->m_signalSlot;
}

static void xboxps_emitVoid(XBoxPlotSeries* self, size_t signal)
{
    XVarList* args;
    int dummy = 0;
    if (!xboxps_canEmit(self)) return;
    args = XVarList_Create(XVar(int, dummy));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xboxps_emitBool(XBoxPlotSeries* self, size_t signal, bool v)
{
    XVarList* args;
    if (!xboxps_canEmit(self)) return;
    args = XVarList_Create(XVar(bool, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xboxps_emitU32(XBoxPlotSeries* self, size_t signal, uint32_t v)
{
    XVarList* args;
    if (!xboxps_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xboxps_emitDouble(XBoxPlotSeries* self, size_t signal, double v)
{
    XVarList* args;
    if (!xboxps_canEmit(self)) return;
    args = XVarList_Create(XVar(double, v));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xboxps_emitPen(XBoxPlotSeries* self, size_t signal, uint32_t color,
                           double width)
{
    XVarList* args;
    if (!xboxps_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, color), XVar(double, width));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xboxps_emitBox(XBoxPlotSeries* self, size_t signal, XBoxSet* box)
{
    XVarList* args;
    if (!xboxps_canEmit(self)) return;
    args = XVarList_Create(XVar(XBoxSet*, box));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xboxps_emitBoxState(XBoxPlotSeries* self, size_t signal, bool st,
                                XBoxSet* box)
{
    XVarList* args;
    if (!xboxps_canEmit(self)) return;
    args = XVarList_Create(XVar(bool, st), XVar(XBoxSet*, box));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 内部助手 ==================== */

static bool xboxps_grow(XBoxPlotSeries* self)
{
    if (!self) return false;
    if (self->m_count < self->m_capacity) return true;
    {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 8;
        XBoxSet** a = (XBoxSet**)XRealloc_System(self->m_boxSets,
                                                 sizeof(XBoxSet*) * (size_t)cap);
        if (!a) return false;
        self->m_boxSets = a;
        self->m_capacity = (uint16_t)cap;
    }
    return true;
}

static int xboxps_indexOf(const XBoxPlotSeries* self, const XBoxSet* box)
{
    int i;
    if (!self || !self->m_boxSets) return -1;
    for (i = 0; i < (int)self->m_count; ++i)
        if (self->m_boxSets[i] == box) return i;
    return -1;
}

/* ==================== 虚函数实现 ==================== */

static void VXBoxPlotSeries_deinit(XBoxPlotSeries* self)
{
    int i;
    if (!self) return;
    if (self->m_boxSets) {
        for (i = 0; i < (int)self->m_count; ++i) {
            if (self->m_boxSets[i]) {
                XClassDelete(self->m_boxSets[i]);
                self->m_boxSets[i] = NULL;
            }
        }
        XFree_System(self->m_boxSets);
        self->m_boxSets = NULL;
    }
    self->m_count = 0;
    self->m_capacity = 0;
    XClass_Deinit_Parent(XAbstractSeries, &self->m_base);
}

XVtable* XBoxPlotSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XBoxPlotSeries)
    XVTABLE_INHERIT_XCLASS(XAbstractSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBoxPlotSeries_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 生命周期 ==================== */

void XBoxPlotSeries_init(XBoxPlotSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractSeries_init(&self->m_base);
    XClassSetVtable(self, XBoxPlotSeries);
    XAbstractSeries_setName_2(&self->m_base, "boxplot");
    self->m_base.m_type = XChartSeriesType_BoxPlot;
    self->m_boxWidth = 0.5;
    self->m_penWidth = 1.0;
    self->m_boxOutlineVisible = true;
}

XBoxPlotSeries* XBoxPlotSeries_create_ex(XMemoryType memory)
{
    XBoxPlotSeries* self =
        (XBoxPlotSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XBoxPlotSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 类型查询 ==================== */

int XBoxPlotSeries_type(const XBoxPlotSeries* self)
{
    (void)self;
    return XChartSeriesType_BoxPlot;
}

/* ==================== 盒组集合 ==================== */

bool XBoxPlotSeries_append(XBoxPlotSeries* self, XBoxSet* box)
{
    if (!self || !box) return false;
    if (xboxps_indexOf(self, box) >= 0) return false;
    if (!xboxps_grow(self)) return false;
    self->m_boxSets[self->m_count++] = box;
    xboxps_emitVoid(self, (size_t)XBoxPlotSeries_countChanged_signal);
    return true;
}

bool XBoxPlotSeries_appendList(XBoxPlotSeries* self, XBoxSet* const* boxes,
                               int count)
{
    int i;
    if (!self || !boxes || count <= 0) return false;
    for (i = 0; i < count; ++i) {
        if (!boxes[i] || xboxps_indexOf(self, boxes[i]) >= 0) return false;
    }
    for (i = 0; i < count; ++i) {
        if (!xboxps_grow(self)) return false;
        self->m_boxSets[self->m_count++] = boxes[i];
    }
    xboxps_emitVoid(self, (size_t)XBoxPlotSeries_countChanged_signal);
    return true;
}

bool XBoxPlotSeries_insert(XBoxPlotSeries* self, int index, XBoxSet* box)
{
    int i;
    if (!self || !box) return false;
    if (xboxps_indexOf(self, box) >= 0) return false;
    if (index < 0) index = 0;
    if (index > (int)self->m_count) index = (int)self->m_count;
    if (!xboxps_grow(self)) return false;
    for (i = (int)self->m_count; i > index; --i)
        self->m_boxSets[i] = self->m_boxSets[i - 1];
    self->m_boxSets[index] = box;
    ++self->m_count;
    xboxps_emitVoid(self, (size_t)XBoxPlotSeries_countChanged_signal);
    return true;
}

bool XBoxPlotSeries_take(XBoxPlotSeries* self, XBoxSet* box)
{
    int idx;
    int i;
    if (!self || !box) return false;
    idx = xboxps_indexOf(self, box);
    if (idx < 0) return false;
    for (i = idx; i + 1 < (int)self->m_count; ++i)
        self->m_boxSets[i] = self->m_boxSets[i + 1];
    self->m_boxSets[--self->m_count] = NULL;
    xboxps_emitBox(self, (size_t)XBoxPlotSeries_released_signal, box);
    xboxps_emitVoid(self, (size_t)XBoxPlotSeries_countChanged_signal);
    return true;
}

bool XBoxPlotSeries_remove(XBoxPlotSeries* self, XBoxSet* box)
{
    if (!XBoxPlotSeries_take(self, box)) return false;
    /* box 可能正处于自身信号发射帧内，延迟回收（与库内其他容器一致）。 */
    XObject_deleteLater((XObject*)box);
    return true;
}

void XBoxPlotSeries_clear(XBoxPlotSeries* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < (int)self->m_count; ++i) {
        if (self->m_boxSets[i]) {
            XObject_deleteLater((XObject*)self->m_boxSets[i]);
            self->m_boxSets[i] = NULL;
        }
    }
    self->m_count = 0;
    xboxps_emitVoid(self, (size_t)XBoxPlotSeries_countChanged_signal);
}

int XBoxPlotSeries_count(const XBoxPlotSeries* self)
{ return self ? (int)self->m_count : 0; }

XBoxSet* XBoxPlotSeries_boxSetAt(const XBoxPlotSeries* self, int index)
{
    if (!self || index < 0 || index >= (int)self->m_count) return NULL;
    return self->m_boxSets[index];
}

int XBoxPlotSeries_boxSets(const XBoxPlotSeries* self, XBoxSet** out,
                           int maxCount)
{
    int i;
    int n;
    if (!self || !out || maxCount <= 0) return 0;
    n = (int)self->m_count < maxCount ? (int)self->m_count : maxCount;
    for (i = 0; i < n; ++i) out[i] = self->m_boxSets[i];
    return n;
}

/* ==================== 外观 ==================== */

void XBoxPlotSeries_setBoxOutlineVisible(XBoxPlotSeries* self, bool visible)
{
    if (!self || self->m_boxOutlineVisible == visible) return;
    self->m_boxOutlineVisible = visible;
    xboxps_emitBool(self,
                    (size_t)XBoxPlotSeries_boxOutlineVisibilityChanged_signal,
                    visible);
}

bool XBoxPlotSeries_boxOutlineVisible(const XBoxPlotSeries* self)
{ return self ? self->m_boxOutlineVisible : false; }

void XBoxPlotSeries_setBoxWidth(XBoxPlotSeries* self, double width)
{
    if (!self) return;
    if (width < 0.0) width = 0.0;
    if (self->m_boxWidth == width) return;
    self->m_boxWidth = width;
    xboxps_emitDouble(self, (size_t)XBoxPlotSeries_boxWidthChanged_signal,
                      width);
}

double XBoxPlotSeries_boxWidth(const XBoxPlotSeries* self)
{ return self ? self->m_boxWidth : 0.0; }

void XBoxPlotSeries_setPen(XBoxPlotSeries* self, uint32_t color, double width)
{
    if (!self) return;
    if (width < 0.0) width = 0.0;
    if (self->m_penColor == color && self->m_penWidth == width) return;
    self->m_penColor = color;
    self->m_penWidth = width;
    xboxps_emitPen(self, (size_t)XBoxPlotSeries_penChanged_signal, color, width);
}

void XBoxPlotSeries_pen(const XBoxPlotSeries* self, uint32_t* color,
                        double* width)
{
    if (!self) return;
    if (color) *color = self->m_penColor;
    if (width) *width = self->m_penWidth;
}

void XBoxPlotSeries_setBrush(XBoxPlotSeries* self, uint32_t color)
{
    if (!self || self->m_brushColor == color) return;
    self->m_brushColor = color;
    xboxps_emitU32(self, (size_t)XBoxPlotSeries_brushChanged_signal, color);
}

uint32_t XBoxPlotSeries_brush(const XBoxPlotSeries* self)
{ return self ? self->m_brushColor : 0; }

/* ==================== 信号 ==================== */

void* XBoxPlotSeries_clicked_signal(XBoxPlotSeries* self, XBoxSet* boxset)
{
    xboxps_emitBox(self, (size_t)XBoxPlotSeries_clicked_signal, boxset);
    return (void*)(size_t)XBoxPlotSeries_clicked_signal;
}

void* XBoxPlotSeries_hovered_signal(XBoxPlotSeries* self, bool status,
                                    XBoxSet* boxset)
{
    xboxps_emitBoxState(self, (size_t)XBoxPlotSeries_hovered_signal, status,
                        boxset);
    return (void*)(size_t)XBoxPlotSeries_hovered_signal;
}

void* XBoxPlotSeries_pressed_signal(XBoxPlotSeries* self, XBoxSet* boxset)
{
    xboxps_emitBox(self, (size_t)XBoxPlotSeries_pressed_signal, boxset);
    return (void*)(size_t)XBoxPlotSeries_pressed_signal;
}

void* XBoxPlotSeries_released_signal(XBoxPlotSeries* self, XBoxSet* boxset)
{
    xboxps_emitBox(self, (size_t)XBoxPlotSeries_released_signal, boxset);
    return (void*)(size_t)XBoxPlotSeries_released_signal;
}

void* XBoxPlotSeries_doubleClicked_signal(XBoxPlotSeries* self, XBoxSet* boxset)
{
    xboxps_emitBox(self, (size_t)XBoxPlotSeries_doubleClicked_signal, boxset);
    return (void*)(size_t)XBoxPlotSeries_doubleClicked_signal;
}

void* XBoxPlotSeries_countChanged_signal(XBoxPlotSeries* self)
{
    xboxps_emitVoid(self, (size_t)XBoxPlotSeries_countChanged_signal);
    return (void*)(size_t)XBoxPlotSeries_countChanged_signal;
}

void* XBoxPlotSeries_penChanged_signal(XBoxPlotSeries* self, uint32_t color,
                                       double width)
{
    xboxps_emitPen(self, (size_t)XBoxPlotSeries_penChanged_signal, color,
                   width);
    return (void*)(size_t)XBoxPlotSeries_penChanged_signal;
}

void* XBoxPlotSeries_brushChanged_signal(XBoxPlotSeries* self, uint32_t color)
{
    xboxps_emitU32(self, (size_t)XBoxPlotSeries_brushChanged_signal, color);
    return (void*)(size_t)XBoxPlotSeries_brushChanged_signal;
}

void* XBoxPlotSeries_boxOutlineVisibilityChanged_signal(XBoxPlotSeries* self,
                                                        bool visible)
{
    xboxps_emitBool(self,
                    (size_t)XBoxPlotSeries_boxOutlineVisibilityChanged_signal,
                    visible);
    return (void*)(size_t)XBoxPlotSeries_boxOutlineVisibilityChanged_signal;
}

void* XBoxPlotSeries_boxWidthChanged_signal(XBoxPlotSeries* self, double width)
{
    xboxps_emitDouble(self, (size_t)XBoxPlotSeries_boxWidthChanged_signal,
                      width);
    return (void*)(size_t)XBoxPlotSeries_boxWidthChanged_signal;
}

void* XBoxPlotSeries_boxsetsAdded_signal(XBoxPlotSeries* self,
                                         XBoxSet* const* sets, int count)
{
    (void)sets;
    (void)count;
    xboxps_emitVoid(self, (size_t)XBoxPlotSeries_boxsetsAdded_signal);
    return (void*)(size_t)XBoxPlotSeries_boxsetsAdded_signal;
}

void* XBoxPlotSeries_boxsetsRemoved_signal(XBoxPlotSeries* self,
                                           XBoxSet* const* sets, int count)
{
    (void)sets;
    (void)count;
    xboxps_emitVoid(self, (size_t)XBoxPlotSeries_boxsetsRemoved_signal);
    return (void*)(size_t)XBoxPlotSeries_boxsetsRemoved_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
