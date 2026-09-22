#include "XPieSeries.h"

#include "XAlgorithm.h"
#include "XPieSlice.h"
#include "XString.h"
#include "XMemory.h"
#include "XClass.h"

#if XCHARTS_ON

/**
 * @brief 发射带切片载荷的信号（内部辅助）。
 *
 * @param self   目标序列指针。
 * @param signal 信号标识。
 * @param slice  切片载荷。
 * @return 无返回值。
 */
static void xpieseries_emitSlice(XPieSeries* self, size_t signal,
                                 XPieSlice* slice)
{
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(XPieSlice*, slice));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/**
 * @brief 发射带切片与 bool 载荷的信号（内部辅助）。
 *
 * @param self   目标序列指针。
 * @param signal 信号标识。
 * @param slice  切片载荷。
 * @param state  bool 载荷。
 * @return 无返回值。
 */
static void xpieseries_emitSliceBool(XPieSeries* self, size_t signal,
                                     XPieSlice* slice, bool state)
{
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(XPieSlice*, slice), XVar(bool, state));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/**
 * @brief 发射空参信号（内部辅助）。
 *
 * @param self   目标序列指针。
 * @param signal 信号标识。
 * @return 无返回值。
 */
static void xpieseries_emit0(XPieSeries* self, size_t signal)
{
    if (!self) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
}

static void VXPieSeries_deinit(XPieSeries* self);
static void VXPieSeries_copy(XPieSeries* self, const XPieSeries* other);
static void VXPieSeries_move(XPieSeries* self, XPieSeries* other);

XVtable* XPieSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPieSeries)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPieSeries_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXPieSeries_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXPieSeries_move);
    return XVTABLE_DEFAULT;
}

void XPieSeries_init(XPieSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractSeries_init(&self->m_base);
    XClassSetVtable(self, XPieSeries);
    XAbstractSeries_setName_2(&self->m_base, "pie");
    self->m_base.m_type = XChartSeriesType_Pie;
    self->m_horizontalPosition = 0.5;
    self->m_verticalPosition = 0.5;
    self->m_pieSize = 0.7;
    self->m_pieStartAngle = 0.0;
    self->m_pieEndAngle = 360.0;
    self->m_labelsVisible = false;
}

XPieSeries* XPieSeries_create_ex(XMemoryType memory)
{
    XPieSeries* self = (XPieSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XPieSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXPieSeries_deinit(XPieSeries* self)
{
    int i;
    if (!self) return;
    /* m_name/m_visible 由 XAbstractSeries 基类析构处理。 */
    for (i = 0; i < self->m_count; ++i)
        if (self->m_slices[i]) XPieSlice_delete_base(self->m_slices[i]);
    if (self->m_slices) XFree_System(self->m_slices);
    self->m_slices = NULL;
    self->m_count = 0;
    self->m_capacity = 0;
    XClass_Deinit_Parent(XAbstractSeries, (XAbstractSeries*)self);
}

static void VXPieSeries_copy(XPieSeries* self, const XPieSeries* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XPieSeries_init(self);
    XClass_Parent(XAbstractSeries, EXClass_Copy,
                  void(*)(XAbstractSeries*, const XAbstractSeries*))(
        (XAbstractSeries*)self, (const XAbstractSeries*)other);
    XPieSeries_clear(self);
    /* name/visible 由 XAbstractSeries 基类拷贝处理。 */
    self->m_holeSize = other->m_holeSize;
    (void)0;
    self->m_horizontalPosition = other->m_horizontalPosition;
    self->m_verticalPosition = other->m_verticalPosition;
    self->m_pieSize = other->m_pieSize;
    self->m_pieStartAngle = other->m_pieStartAngle;
    self->m_pieEndAngle = other->m_pieEndAngle;
    self->m_labelsVisible = other->m_labelsVisible;
    /* 切片集合深拷贝（对象所有权转移语义）。 */
    {
        int i;
        for (i = 0; i < other->m_count; ++i) {
            const XPieSlice* src = other->m_slices[i];
            XPieSlice* dup = XPieSlice_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                 src ? XPieSlice_label(src) : "",
                                                 src ? XPieSlice_value(src) : 0.0);
            if (!dup) continue;
            XPieSeries_appendSlice(self, dup);
        }
    }
}

static void VXPieSeries_move(XPieSeries* self, XPieSeries* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XPieSeries_init(self);
    /* 根因（R-104）：此前在父类 move（已把 other 的 name/图表链移入
     * self）之后才调 VXPieSeries_deinit——基类析构把刚移入的 name/
     * axes 连带释放，move 后序列名称/图表链丢失（状态损毁）。对照
     * VXXYSeries_move（XXYSeries.c:295-333）正确范式：先清「自身扩展」
     * （切片集合为派生类独有资源），再父类 move（基类 deinit+资源转移
     * 恰好一次），最后转移 other 扩展。此处只清切片、不整体 deinit，
     * 避免基类资源被二次析构。 */
    {
        int i;
        for (i = 0; i < self->m_count; ++i)
            if (self->m_slices[i]) XPieSlice_delete_base(self->m_slices[i]);
        if (self->m_slices) XFree_System(self->m_slices);
        self->m_slices = NULL;
        self->m_count = 0;
        self->m_capacity = 0;
    }
    XClass_Parent(XAbstractSeries, EXClass_Move,
                  void(*)(XAbstractSeries*, XAbstractSeries*))(
        (XAbstractSeries*)self, (XAbstractSeries*)other);
    self->m_slices = other->m_slices;
    other->m_slices = NULL;
    self->m_count = other->m_count;
    other->m_count = 0;
    self->m_capacity = other->m_capacity;
    other->m_capacity = 0;
    self->m_holeSize = other->m_holeSize;
    self->m_horizontalPosition = other->m_horizontalPosition;
    self->m_verticalPosition = other->m_verticalPosition;
    self->m_pieSize = other->m_pieSize;
    self->m_pieStartAngle = other->m_pieStartAngle;
    self->m_pieEndAngle = other->m_pieEndAngle;
    self->m_labelsVisible = other->m_labelsVisible;
}

/* ==================== 切片集合 ==================== */

/**
 * @brief 扩容切片指针数组（内部辅助）。
 *
 * @param self 目标序列指针。
 * @return 扩容成功返回 true。
 */
static bool xpieseries_grow(XPieSeries* self)
{
    XPieSlice** grown;
    int cap;
    if (self->m_count < self->m_capacity) return true;
    cap = self->m_capacity > 0 ? self->m_capacity * 2 : 8;
    grown = (XPieSlice**)XRealloc_System(self->m_slices,
                                         sizeof(XPieSlice*) * (size_t)cap);
    if (!grown) return false;
    self->m_slices = grown;
    self->m_capacity = cap;
    return true;
}

bool XPieSeries_appendSlice(XPieSeries* self, XPieSlice* slice)
{
    if (!self || !slice) return false;
    if (!xpieseries_grow(self)) return false;
    self->m_slices[self->m_count++] = slice;
    xpieseries_emitSlice(self, (size_t)XPieSeries_added_signal, slice);
    xpieseries_emit0(self, (size_t)XPieSeries_countChanged_signal);
    xpieseries_emit0(self, (size_t)XPieSeries_sumChanged_signal);
    return true;
}

XPieSlice* XPieSeries_append(XPieSeries* self, const XString* label, double value)
{
    XPieSlice* slice;
    if (!self) return NULL;
    slice = XPieSlice_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, label, value);
    if (!slice) return NULL;
    if (!XPieSeries_appendSlice(self, slice)) {
        XPieSlice_delete_base(slice);
        return NULL;
    }
    return slice;
}
XPieSlice* XPieSeries_append_2(XPieSeries* self, const char* label, double value)
{
    XString* tmp = NULL;
    XPieSlice* slice;
    if (!label) return NULL;
    tmp = XString_create_utf8(label);
    if (!tmp) return NULL;
    slice = XPieSeries_append(self, tmp, value);
    XString_delete_base(tmp);
    return slice;
}

bool XPieSeries_insert(XPieSeries* self, int index, XPieSlice* slice)
{
    int i;
    if (!self || !slice || index < 0 || index > self->m_count) return false;
    if (!xpieseries_grow(self)) return false;
    for (i = self->m_count; i > index; --i)
        self->m_slices[i] = self->m_slices[i - 1];
    self->m_slices[index] = slice;
    ++self->m_count;
    xpieseries_emitSlice(self, (size_t)XPieSeries_added_signal, slice);
    xpieseries_emit0(self, (size_t)XPieSeries_countChanged_signal);
    xpieseries_emit0(self, (size_t)XPieSeries_sumChanged_signal);
    return true;
}

bool XPieSeries_remove(XPieSeries* self, XPieSlice* slice)
{
    if (!XPieSeries_take(self, slice)) return false;
    XPieSlice_delete_base(slice);
    return true;
}

bool XPieSeries_take(XPieSeries* self, XPieSlice* slice)
{
    int i;
    int found = 0;
    if (!self || !slice) return false;
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_slices[i] != slice) continue;
        found = 1;
        XMemmove(&self->m_slices[i], &self->m_slices[i + 1],
                (size_t)(self->m_count - i - 1) * sizeof(self->m_slices[0]));
        --self->m_count;
        break;
    }
    if (!found) return false;
    xpieseries_emitSlice(self, (size_t)XPieSeries_removed_signal, slice);
    xpieseries_emit0(self, (size_t)XPieSeries_countChanged_signal);
    xpieseries_emit0(self, (size_t)XPieSeries_sumChanged_signal);
    return true;
}

void XPieSeries_clear(XPieSeries* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_count; ++i)
        if (self->m_slices[i]) XPieSlice_delete_base(self->m_slices[i]);
    self->m_count = 0;
    xpieseries_emit0(self, (size_t)XPieSeries_countChanged_signal);
    xpieseries_emit0(self, (size_t)XPieSeries_sumChanged_signal);
}

int XPieSeries_count(const XPieSeries* self) { return self ? self->m_count : 0; }

XPieSlice* XPieSeries_slice(const XPieSeries* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return NULL;
    return self->m_slices[index];
}

bool XPieSeries_isEmpty(const XPieSeries* self)
{ return !self || self->m_count == 0; }

double XPieSeries_sum(const XPieSeries* self)
{
    double sum = 0;
    int i;
    if (!self) return 0;
    for (i = 0; i < self->m_count; ++i)
        sum += XPieSlice_value(self->m_slices[i]);
    return sum;
}

/* ==================== 几何属性 ==================== */

void XPieSeries_setHoleSize(XPieSeries* self, double hole)
{
    if (!self) return;
    if (hole < 0) hole = 0;
    if (hole > 1.0) hole = 1.0;
    self->m_holeSize = hole;
    /* 对标 Qt setSizes(hole, max(pieSize, hole))：饼外径不小于孔径。 */
    if (self->m_pieSize < hole) self->m_pieSize = hole;
}

double XPieSeries_holeSize(const XPieSeries* self)
{ return self ? self->m_holeSize : 0.0; }

void XPieSeries_setHorizontalPosition(XPieSeries* self, double relativePosition)
{
    if (!self) return;
    if (relativePosition < 0.0) relativePosition = 0.0;
    if (relativePosition > 1.0) relativePosition = 1.0;
    self->m_horizontalPosition = relativePosition;
}

double XPieSeries_horizontalPosition(const XPieSeries* self)
{ return self ? self->m_horizontalPosition : 0.5; }

void XPieSeries_setVerticalPosition(XPieSeries* self, double relativePosition)
{
    if (!self) return;
    if (relativePosition < 0.0) relativePosition = 0.0;
    if (relativePosition > 1.0) relativePosition = 1.0;
    self->m_verticalPosition = relativePosition;
}

double XPieSeries_verticalPosition(const XPieSeries* self)
{ return self ? self->m_verticalPosition : 0.5; }

void XPieSeries_setPieSize(XPieSeries* self, double relativeSize)
{
    if (!self) return;
    if (relativeSize < 0.0) relativeSize = 0.0;
    if (relativeSize > 1.0) relativeSize = 1.0;
    self->m_pieSize = relativeSize;
    /* 对标 Qt setSizes(min(hole, size), size)：孔径不超出外径。 */
    if (self->m_holeSize > relativeSize)
        self->m_holeSize = relativeSize;
}

double XPieSeries_pieSize(const XPieSeries* self)
{ return self ? self->m_pieSize : 0.7; }

void XPieSeries_setPieStartAngle(XPieSeries* self, double startAngle)
{ if (self) self->m_pieStartAngle = startAngle; }

/* 根因（R-107）：空指针回退值此前为 90.0，与 XPieSeries_init 置的构造
 * 默认 0.0 口径矛盾（NULL 序列与刚构造序列 getter 返回不同）。以 init
 * 值为准统一（endAngle 回退 360.0 与 init 一致，无需改）。 */
double XPieSeries_pieStartAngle(const XPieSeries* self)
{ return self ? self->m_pieStartAngle : 0.0; }

void XPieSeries_setPieEndAngle(XPieSeries* self, double endAngle)
{ if (self) self->m_pieEndAngle = endAngle; }

double XPieSeries_pieEndAngle(const XPieSeries* self)
{ return self ? self->m_pieEndAngle : 360.0; }

void XPieSeries_setLabelsVisible(XPieSeries* self, bool visible)
{
    int i;
    if (!self) return;
    self->m_labelsVisible = visible;
    for (i = 0; i < self->m_count; ++i)
        XPieSlice_setLabelVisible(self->m_slices[i], visible);
}

bool XPieSeries_labelsVisible(const XPieSeries* self)
{ return self ? self->m_labelsVisible : false; }

void XPieSeries_setLabelsPosition(XPieSeries* self,
                                  XPieSlice_LabelPosition position)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_count; ++i)
        XPieSlice_setLabelPosition(self->m_slices[i], position);
}

/* ==================== 通用序列属性 ==================== */

int XPieSeries_type(const XPieSeries* self)
{ (void)self; return 5; }

void XPieSeries_updateAngles(XPieSeries* self)
{
    double sum;
    double angle;
    double total;
    double sweepScale;
    int i;
    if (!self || self->m_count <= 0) return;
    sum = XPieSeries_sum(self);
    if (sum <= 0.0) return;
    total = self->m_pieEndAngle - self->m_pieStartAngle;
    if (total <= 0.0) total = 360.0;
    sweepScale = total / sum;
    angle = self->m_pieStartAngle;
    for (i = 0; i < self->m_count; ++i) {
        XPieSlice* slice = self->m_slices[i];
        double value = XPieSlice_value(slice);
        double span = value * sweepScale;
        double oldPercentage = slice->m_percentage;
        double oldStart = slice->m_startAngle;
        double oldSpan = slice->m_angleSpan;
        slice->m_percentage = value / sum;
        slice->m_startAngle = angle;
        slice->m_angleSpan = span;
        angle -= span;
        if (oldPercentage != slice->m_percentage)
            XPieSlice_percentageChanged_signal(slice);
        if (oldStart != slice->m_startAngle)
            XPieSlice_startAngleChanged_signal(slice);
        if (oldSpan != slice->m_angleSpan)
            XPieSlice_angleSpanChanged_signal(slice);
    }
}

/* ==================== 信号 ==================== */

void* XPieSeries_added_signal(XPieSeries* self, XPieSlice* slice)
{
    if (!self) return (void*)(size_t)XPieSeries_added_signal;
    xpieseries_emitSlice(self, (size_t)XPieSeries_added_signal, slice);
    return (void*)(size_t)XPieSeries_added_signal;
}

void* XPieSeries_removed_signal(XPieSeries* self, XPieSlice* slice)
{
    if (!self) return (void*)(size_t)XPieSeries_removed_signal;
    xpieseries_emitSlice(self, (size_t)XPieSeries_removed_signal, slice);
    return (void*)(size_t)XPieSeries_removed_signal;
}

void* XPieSeries_clicked_signal(XPieSeries* self, XPieSlice* slice)
{
    if (!self) return (void*)(size_t)XPieSeries_clicked_signal;
    xpieseries_emitSlice(self, (size_t)XPieSeries_clicked_signal, slice);
    return (void*)(size_t)XPieSeries_clicked_signal;
}

void* XPieSeries_hovered_signal(XPieSeries* self, XPieSlice* slice, bool state)
{
    if (!self) return (void*)(size_t)XPieSeries_hovered_signal;
    xpieseries_emitSliceBool(self, (size_t)XPieSeries_hovered_signal,
                             slice, state);
    return (void*)(size_t)XPieSeries_hovered_signal;
}

void* XPieSeries_pressed_signal(XPieSeries* self, XPieSlice* slice)
{
    if (!self) return (void*)(size_t)XPieSeries_pressed_signal;
    xpieseries_emitSlice(self, (size_t)XPieSeries_pressed_signal, slice);
    return (void*)(size_t)XPieSeries_pressed_signal;
}

void* XPieSeries_released_signal(XPieSeries* self, XPieSlice* slice)
{
    if (!self) return (void*)(size_t)XPieSeries_released_signal;
    xpieseries_emitSlice(self, (size_t)XPieSeries_released_signal, slice);
    return (void*)(size_t)XPieSeries_released_signal;
}

void* XPieSeries_doubleClicked_signal(XPieSeries* self, XPieSlice* slice)
{
    if (!self) return (void*)(size_t)XPieSeries_doubleClicked_signal;
    xpieseries_emitSlice(self, (size_t)XPieSeries_doubleClicked_signal, slice);
    return (void*)(size_t)XPieSeries_doubleClicked_signal;
}

void* XPieSeries_countChanged_signal(XPieSeries* self)
{
    if (!self) return (void*)(size_t)XPieSeries_countChanged_signal;
    xpieseries_emit0(self, (size_t)XPieSeries_countChanged_signal);
    return (void*)(size_t)XPieSeries_countChanged_signal;
}

void* XPieSeries_sumChanged_signal(XPieSeries* self)
{
    if (!self) return (void*)(size_t)XPieSeries_sumChanged_signal;
    xpieseries_emit0(self, (size_t)XPieSeries_sumChanged_signal);
    return (void*)(size_t)XPieSeries_sumChanged_signal;
}

int XPieSeries_slices(const XPieSeries* self, XPieSlice** out, int maxCount)
{
    int n;
    int i;
    if (!self || !out || maxCount <= 0) return 0;
    n = self->m_count < maxCount ? self->m_count : maxCount;
    for (i = 0; i < n; ++i)
        out[i] = self->m_slices[i];
    return n;
}

#endif /* XCHARTS_ON */
