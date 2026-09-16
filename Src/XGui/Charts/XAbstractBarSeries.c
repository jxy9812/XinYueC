/**
 * @file       XAbstractBarSeries.c
 * @brief      XAbstractBarSeries 柱状序列基类实现（对标 Qt Charts 6.8
 *             QAbstractBarSeries）。
 * @details    XBarSet 集合语义：append/insert/remove/take/clear 接管或
 *             释放所有权；渲染统一走 barSets/barSetAt。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XAbstractBarSeries.h"

#include "XAlgorithm.h"
#include "XString.h"
#include "XMemory.h"
#include "XClass.h"

#if XCHARTS_ON

static void VXAbstractBarSeries_deinit(XAbstractBarSeries* self);
static void VXAbstractBarSeries_copy(XAbstractBarSeries* self,
                                     const XAbstractBarSeries* other);
static void VXAbstractBarSeries_move(XAbstractBarSeries* self,
                                     XAbstractBarSeries* other);

/* ==================== 内部发射辅助 ==================== */

/** @brief 发射无载荷信号（args=NULL）。 */
static void xabs_emitVoid(XAbstractBarSeries* self, size_t signal)
{
    if (!self) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
}

/** @brief 发射 int 载荷信号。 */
static void xabs_emitInt(XAbstractBarSeries* self, size_t signal, int v)
{
    int vi = v;
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(int, vi));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射 double 载荷信号。 */
static void xabs_emitDouble(XAbstractBarSeries* self, size_t signal,
                            double v)
{
    double vd = v;
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(double, vd));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射字符串载荷信号。 */
static void xabs_emitStr(XAbstractBarSeries* self, size_t signal,
                         const char* text)
{
    const char* vs = text ? text : "";
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(const char*, vs));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射柱组集合（数组+数量）载荷信号。 */
static void xabs_emitSets(XAbstractBarSeries* self, size_t signal,
                          XBarSet* const* sets, int count)
{
    XBarSet** vs = (XBarSet**)(void*)sets;
    int vc = count;
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(XBarSet**, vs), XVar(int, vc));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射 int+柱组 载荷信号。 */
static void xabs_emitIndexSet(XAbstractBarSeries* self, size_t signal,
                              int index, XBarSet* set)
{
    int vi = index;
    XBarSet* vs = set;
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(int, vi), XVar(XBarSet*, vs));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射 bool+int+柱组 载荷信号。 */
static void xabs_emitStatusIndexSet(XAbstractBarSeries* self, size_t signal,
                                    bool status, int index, XBarSet* set)
{
    bool vb = status;
    int vi = index;
    XBarSet* vs = set;
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(bool, vb), XVar(int, vi),
                           XVar(XBarSet*, vs));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/* ==================== 类与对象生命周期 ==================== */

XVtable* XAbstractBarSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractBarSeries)
    XVTABLE_INHERIT_XCLASS(XAbstractSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractBarSeries_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXAbstractBarSeries_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXAbstractBarSeries_move);
    return XVTABLE_DEFAULT;
}

void XAbstractBarSeries_init(XAbstractBarSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractSeries_init(&self->m_base);
    XClassSetVtable(self, XAbstractBarSeries);
    self->m_barWidth = 0.5;
    self->m_labelsVisible = false;
    self->m_labelsFormat = XString_create();
    self->m_labelsAngle = 0.0;
    self->m_labelsPosition = XAbstractBarSeries_LabelsCenter;
    self->m_labelsPrecision = 6;
}

XAbstractBarSeries* XAbstractBarSeries_create_ex(XMemoryType memory)
{
    XAbstractBarSeries* self =
        (XAbstractBarSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XAbstractBarSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXAbstractBarSeries_deinit(XAbstractBarSeries* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_barSetCount; ++i)
        if (self->m_barSets[i]) XBarSet_delete_base(self->m_barSets[i]);
    if (self->m_barSets) {
        XFree_System(self->m_barSets);
        self->m_barSets = NULL;
    }
    self->m_barSetCount = 0;
    self->m_barSetCapacity = 0;
    if (self->m_labelsFormat) {
        XString_delete_base(self->m_labelsFormat);
        self->m_labelsFormat = NULL;
    }
    XClass_Deinit_Parent(XAbstractSeries, (XAbstractSeries*)self);
}

static void VXAbstractBarSeries_copy(XAbstractBarSeries* self,
                                     const XAbstractBarSeries* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractBarSeries_init(self);
    XClass_Parent(XAbstractSeries, EXClass_Copy,
                  void(*)(XAbstractSeries*, const XAbstractSeries*))(
        (XAbstractSeries*)self, (const XAbstractSeries*)other);
    XAbstractBarSeries_clear(self);
    for (i = 0; i < other->m_barSetCount; ++i) {
        const XBarSet* src = other->m_barSets[i];
        XBarSet* dup = NULL;
        if (!src) continue;
        dup = XBarSet_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                XBarSet_label(src));
        if (!dup) continue;
        if (src->m_count > 0 && src->m_values)
            XBarSet_appendValues(dup, src->m_values, src->m_count);
        XBarSet_setPen(dup, src->m_penColor, src->m_penWidth);
        XBarSet_setBrush(dup, src->m_brushColor);
        XBarSet_setLabelBrush(dup, src->m_labelBrushColor);
        XBarSet_setLabelFont(dup, XBarSet_labelFont(src),
                             src->m_labelFontSize);
        XBarSet_setSelectedColor(dup, src->m_selectedColor);
        {
            int si;
            for (si = 0; si < src->m_selectedCount; ++si)
                XBarSet_selectBar(dup, src->m_selectedBars[si]);
        }
        XAbstractBarSeries_append(self, dup);
    }
    self->m_barWidth = other->m_barWidth;
    self->m_labelsVisible = other->m_labelsVisible;
    self->m_labelsAngle = other->m_labelsAngle;
    self->m_labelsPosition = other->m_labelsPosition;
    self->m_labelsPrecision = other->m_labelsPrecision;
    if (self->m_labelsFormat && other->m_labelsFormat)
        XString_assign(self->m_labelsFormat, other->m_labelsFormat);
}

static void VXAbstractBarSeries_move(XAbstractBarSeries* self,
                                     XAbstractBarSeries* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractBarSeries_init(self);
    XClass_Parent(XAbstractSeries, EXClass_Move,
                  void(*)(XAbstractSeries*, XAbstractSeries*))(
        (XAbstractSeries*)self, (XAbstractSeries*)other);
    /* 释放 self 原有柱组与快照（基类资源已由父类 move 转移）。 */
    for (i = 0; i < self->m_barSetCount; ++i)
        if (self->m_barSets[i]) XBarSet_delete_base(self->m_barSets[i]);
    if (self->m_barSets) {
        XFree_System(self->m_barSets);
        self->m_barSets = NULL;
    }
    self->m_barSetCount = 0;
    self->m_barSetCapacity = 0;
    if (self->m_labelsFormat) {
        XString_delete_base(self->m_labelsFormat);
        self->m_labelsFormat = NULL;
    }
    self->m_barSets = other->m_barSets;
    other->m_barSets = NULL;
    self->m_barSetCount = other->m_barSetCount;
    other->m_barSetCount = 0;
    self->m_barSetCapacity = other->m_barSetCapacity;
    other->m_barSetCapacity = 0;
    self->m_barWidth = other->m_barWidth;
    self->m_labelsVisible = other->m_labelsVisible;
    self->m_labelsAngle = other->m_labelsAngle;
    self->m_labelsPosition = other->m_labelsPosition;
    self->m_labelsPrecision = other->m_labelsPrecision;
    self->m_labelsFormat = other->m_labelsFormat;
    other->m_labelsFormat = XString_create();
}

/* ==================== 内部辅助 ==================== */

/** @brief 判断柱组是否已在本序列中。 */
static bool xabs_contains(const XAbstractBarSeries* self, const XBarSet* set)
{
    int i;
    if (!self || !set) return false;
    for (i = 0; i < self->m_barSetCount; ++i)
        if (self->m_barSets[i] == set) return true;
    return false;
}

/** @brief 扩容柱组数组（新槽位置零）。 */
static bool xabs_grow(XAbstractBarSeries* self, int need)
{
    int cap;
    int oldCap;
    XBarSet** grown;
    if (need <= self->m_barSetCapacity) return true;
    cap = self->m_barSetCapacity > 0 ? self->m_barSetCapacity : 8;
    oldCap = self->m_barSetCapacity;
    while (cap < need) cap *= 2;
    grown = (XBarSet**)XRealloc_System(self->m_barSets,
        sizeof(XBarSet*) * (size_t)cap);
    if (!grown) return false;
    self->m_barSets = grown;
    self->m_barSetCapacity = cap;
    if (oldCap > 0) {
        int i;
        for (i = oldCap; i < cap; ++i)
            self->m_barSets[i] = NULL;
    } else {
        int i;
        for (i = 0; i < cap; ++i)
            self->m_barSets[i] = NULL;
    }
    return true;
}

/* ==================== 柱组集合 ==================== */

bool XAbstractBarSeries_append(XAbstractBarSeries* self, XBarSet* set)
{
    if (!self || !set || xabs_contains(self, set)) return false;
    if (!xabs_grow(self, self->m_barSetCount + 1)) return false;
    self->m_barSets[self->m_barSetCount++] = set;
    XAbstractBarSeries_barsetsAdded_signal(self, &set, 1);
    XAbstractBarSeries_countChanged_signal(self);
    return true;
}

bool XAbstractBarSeries_appendSets(XAbstractBarSeries* self,
                                   XBarSet* const* sets, int count)
{
    int i;
    int j;
    if (!self || !sets || count <= 0) return false;
    /* 校验：非 NULL、列表内不重复、未已在序列中。 */
    for (i = 0; i < count; ++i) {
        if (!sets[i]) return false;
        for (j = 0; j < i; ++j)
            if (sets[j] == sets[i]) return false;
        if (xabs_contains(self, sets[i])) return false;
    }
    if (!xabs_grow(self, self->m_barSetCount + count)) return false;
    for (i = 0; i < count; ++i)
        self->m_barSets[self->m_barSetCount++] = sets[i];
    XAbstractBarSeries_barsetsAdded_signal(self, sets, count);
    XAbstractBarSeries_countChanged_signal(self);
    return true;
}

bool XAbstractBarSeries_insert(XAbstractBarSeries* self, int index,
                               XBarSet* set)
{
    int i;
    if (!self || !set || xabs_contains(self, set)) return false;
    if (index < 0) index = 0;
    if (index > self->m_barSetCount) index = self->m_barSetCount;
    if (!xabs_grow(self, self->m_barSetCount + 1)) return false;
    for (i = self->m_barSetCount; i > index; --i)
        self->m_barSets[i] = self->m_barSets[i - 1];
    self->m_barSets[index] = set;
    self->m_barSetCount++;
    XAbstractBarSeries_barsetsAdded_signal(self, &set, 1);
    XAbstractBarSeries_countChanged_signal(self);
    return true;
}

bool XAbstractBarSeries_take(XAbstractBarSeries* self, XBarSet* set)
{
    int i;
    if (!self || !set) return false;
    for (i = 0; i < self->m_barSetCount; ++i) {
        if (self->m_barSets[i] != set) continue;
        XMemmove(&self->m_barSets[i], &self->m_barSets[i + 1],
                (size_t)(self->m_barSetCount - i - 1) *
                    sizeof(self->m_barSets[0]));
        --self->m_barSetCount;
        self->m_barSets[self->m_barSetCount] = NULL;
        XAbstractBarSeries_barsetsRemoved_signal(self, &set, 1);
        XAbstractBarSeries_countChanged_signal(self);
        return true;
    }
    return false;
}

bool XAbstractBarSeries_remove(XAbstractBarSeries* self, XBarSet* set)
{
    if (!XAbstractBarSeries_take(self, set)) return false;
    XBarSet_delete_base(set);
    return true;
}

void XAbstractBarSeries_clear(XAbstractBarSeries* self)
{
    XBarSet** removed;
    int count;
    int i;
    if (!self || self->m_barSetCount == 0) return;
    count = self->m_barSetCount;
    removed = (XBarSet**)XMalloc_System(sizeof(XBarSet*) * (size_t)count);
    if (removed) {
        XMemcpy(removed, self->m_barSets, sizeof(XBarSet*) * (size_t)count);
    } else {
        /* 分配失败仍清空计数并逐发单元素信号。 */
        for (i = self->m_barSetCount - 1; i >= 0; --i) {
            XBarSet* s = self->m_barSets[i];
            XAbstractBarSeries_barsetsRemoved_signal(self, &s, 1);
        }
    }
    self->m_barSetCount = 0;
    if (removed) {
        XAbstractBarSeries_barsetsRemoved_signal(self, removed, count);
        XFree_System(removed);
    }
    XAbstractBarSeries_countChanged_signal(self);
    for (i = 0; i < self->m_barSetCapacity; ++i) {
        if (self->m_barSets[i]) {
            XBarSet_delete_base(self->m_barSets[i]);
            self->m_barSets[i] = NULL;
        }
    }
}

int XAbstractBarSeries_count(const XAbstractBarSeries* self)
{ return self ? self->m_barSetCount : 0; }

int XAbstractBarSeries_barSets(const XAbstractBarSeries* self,
                               XBarSet** out, int maxCount)
{
    int n;
    int i;
    if (!self || !out || maxCount <= 0) return 0;
    n = self->m_barSetCount < maxCount ? self->m_barSetCount : maxCount;
    for (i = 0; i < n; ++i)
        out[i] = self->m_barSets[i];
    return n;
}

XBarSet* XAbstractBarSeries_barSetAt(const XAbstractBarSeries* self,
                                     int index)
{
    if (!self || index < 0 || index >= self->m_barSetCount) return NULL;
    return self->m_barSets[index];
}

/* ==================== 外观 ==================== */

void XAbstractBarSeries_setBarWidth(XAbstractBarSeries* self, double width)
{
    if (!self) return;
    if (width < 0.0) width = 0.0;
    self->m_barWidth = width;
}

double XAbstractBarSeries_barWidth(const XAbstractBarSeries* self)
{ return self ? self->m_barWidth : 0.5; }

void XAbstractBarSeries_setLabelsVisible(XAbstractBarSeries* self,
                                         bool visible)
{
    if (!self || self->m_labelsVisible == visible) return;
    self->m_labelsVisible = visible;
    XAbstractBarSeries_labelsVisibleChanged_signal(self);
}

bool XAbstractBarSeries_isLabelsVisible(const XAbstractBarSeries* self)
{ return self ? self->m_labelsVisible : false; }

void XAbstractBarSeries_setLabelsFormat(XAbstractBarSeries* self,
                                        const XString* format)
{
    const char* text;
    if (!self) return;
    if (!self->m_labelsFormat) self->m_labelsFormat = XString_create();
    if (!self->m_labelsFormat) return;
    if (format) {
        if (XString_equals(self->m_labelsFormat, format,
                           XChar_CaseSensitive))
            return;
        XString_assign(self->m_labelsFormat, format);
    } else {
        if (XString_length(self->m_labelsFormat) == 0) return;
        XString_assign_utf8(self->m_labelsFormat, "");
    }
    text = XString_toUtf8(self->m_labelsFormat);
    XAbstractBarSeries_labelsFormatChanged_signal(self,
                                                  text ? text : "");
}
void XAbstractBarSeries_setLabelsFormat_2(XAbstractBarSeries* self,
                                          const char* format)
{
    XString* tmp = NULL;
    if (format) {
        tmp = XString_create_utf8(format);
        if (!tmp) return;
    }
    XAbstractBarSeries_setLabelsFormat(self, tmp);
    if (tmp) XString_delete_base(tmp);
}

const XString* XAbstractBarSeries_labelsFormat(const XAbstractBarSeries* self)
{
    return (self && self->m_labelsFormat) ? self->m_labelsFormat : NULL;
}
const char* XAbstractBarSeries_labelsFormat_2(const XAbstractBarSeries* self)
{
    const XString* s;
    const char* text;
    s = XAbstractBarSeries_labelsFormat(self);
    if (!s) return "";
    text = XString_toUtf8(s);
    return text ? text : "";
}

void XAbstractBarSeries_setLabelsAngle(XAbstractBarSeries* self,
                                       double angle)
{
    if (!self || self->m_labelsAngle == angle) return;
    self->m_labelsAngle = angle;
    XAbstractBarSeries_labelsAngleChanged_signal(self, angle);
}

double XAbstractBarSeries_labelsAngle(const XAbstractBarSeries* self)
{ return self ? self->m_labelsAngle : 0.0; }

void XAbstractBarSeries_setLabelsPosition(
    XAbstractBarSeries* self, XAbstractBarSeries_LabelsPosition position)
{
    int p;
    if (!self) return;
    p = (int)position;
    if (p < 0 || p > 3) position = XAbstractBarSeries_LabelsCenter;
    if (self->m_labelsPosition == position) return;
    self->m_labelsPosition = position;
    XAbstractBarSeries_labelsPositionChanged_signal(self, (int)position);
}

XAbstractBarSeries_LabelsPosition XAbstractBarSeries_labelsPosition(
    const XAbstractBarSeries* self)
{
    return self ? self->m_labelsPosition
                : XAbstractBarSeries_LabelsCenter;
}

void XAbstractBarSeries_setLabelsPrecision(XAbstractBarSeries* self,
                                           int precision)
{
    if (!self || self->m_labelsPrecision == precision) return;
    self->m_labelsPrecision = precision;
    XAbstractBarSeries_labelsPrecisionChanged_signal(self, precision);
}

int XAbstractBarSeries_labelsPrecision(const XAbstractBarSeries* self)
{ return self ? self->m_labelsPrecision : 6; }

/* ==================== 信号 ==================== */

void* XAbstractBarSeries_clicked_signal(XAbstractBarSeries* self, int index,
                                        XBarSet* set)
{
    if (!self) return (void*)(size_t)XAbstractBarSeries_clicked_signal;
    xabs_emitIndexSet(self, (size_t)XAbstractBarSeries_clicked_signal,
                      index, set);
    return (void*)(size_t)XAbstractBarSeries_clicked_signal;
}

void* XAbstractBarSeries_hovered_signal(XAbstractBarSeries* self,
                                        bool status, int index, XBarSet* set)
{
    if (!self) return (void*)(size_t)XAbstractBarSeries_hovered_signal;
    xabs_emitStatusIndexSet(self,
                            (size_t)XAbstractBarSeries_hovered_signal,
                            status, index, set);
    return (void*)(size_t)XAbstractBarSeries_hovered_signal;
}

void* XAbstractBarSeries_pressed_signal(XAbstractBarSeries* self, int index,
                                        XBarSet* set)
{
    if (!self) return (void*)(size_t)XAbstractBarSeries_pressed_signal;
    xabs_emitIndexSet(self, (size_t)XAbstractBarSeries_pressed_signal,
                      index, set);
    return (void*)(size_t)XAbstractBarSeries_pressed_signal;
}

void* XAbstractBarSeries_released_signal(XAbstractBarSeries* self, int index,
                                         XBarSet* set)
{
    if (!self) return (void*)(size_t)XAbstractBarSeries_released_signal;
    xabs_emitIndexSet(self, (size_t)XAbstractBarSeries_released_signal,
                      index, set);
    return (void*)(size_t)XAbstractBarSeries_released_signal;
}

void* XAbstractBarSeries_doubleClicked_signal(XAbstractBarSeries* self,
                                              int index, XBarSet* set)
{
    if (!self) return (void*)(size_t)XAbstractBarSeries_doubleClicked_signal;
    xabs_emitIndexSet(self, (size_t)XAbstractBarSeries_doubleClicked_signal,
                      index, set);
    return (void*)(size_t)XAbstractBarSeries_doubleClicked_signal;
}

void* XAbstractBarSeries_countChanged_signal(XAbstractBarSeries* self)
{
    if (!self) return (void*)(size_t)XAbstractBarSeries_countChanged_signal;
    xabs_emitVoid(self, (size_t)XAbstractBarSeries_countChanged_signal);
    return (void*)(size_t)XAbstractBarSeries_countChanged_signal;
}

void* XAbstractBarSeries_labelsVisibleChanged_signal(XAbstractBarSeries* self)
{
    if (!self)
        return (void*)(size_t)XAbstractBarSeries_labelsVisibleChanged_signal;
    xabs_emitVoid(self,
                  (size_t)XAbstractBarSeries_labelsVisibleChanged_signal);
    return (void*)(size_t)XAbstractBarSeries_labelsVisibleChanged_signal;
}

void* XAbstractBarSeries_labelsFormatChanged_signal(XAbstractBarSeries* self,
                                                    const char* format)
{
    if (!self) return (void*)(size_t)XAbstractBarSeries_labelsFormatChanged_signal;
    xabs_emitStr(self, (size_t)XAbstractBarSeries_labelsFormatChanged_signal,
                 format);
    return (void*)(size_t)XAbstractBarSeries_labelsFormatChanged_signal;
}

void* XAbstractBarSeries_labelsPositionChanged_signal(XAbstractBarSeries* self,
                                                      int position)
{
    if (!self)
        return (void*)(size_t)XAbstractBarSeries_labelsPositionChanged_signal;
    xabs_emitInt(self,
                 (size_t)XAbstractBarSeries_labelsPositionChanged_signal,
                 position);
    return (void*)(size_t)XAbstractBarSeries_labelsPositionChanged_signal;
}

void* XAbstractBarSeries_labelsAngleChanged_signal(XAbstractBarSeries* self,
                                                   double angle)
{
    if (!self)
        return (void*)(size_t)XAbstractBarSeries_labelsAngleChanged_signal;
    xabs_emitDouble(self, (size_t)XAbstractBarSeries_labelsAngleChanged_signal,
                    angle);
    return (void*)(size_t)XAbstractBarSeries_labelsAngleChanged_signal;
}

void* XAbstractBarSeries_labelsPrecisionChanged_signal(
    XAbstractBarSeries* self, int precision)
{
    if (!self)
        return (void*)(size_t)XAbstractBarSeries_labelsPrecisionChanged_signal;
    xabs_emitInt(self,
                 (size_t)XAbstractBarSeries_labelsPrecisionChanged_signal,
                 precision);
    return (void*)(size_t)XAbstractBarSeries_labelsPrecisionChanged_signal;
}

void* XAbstractBarSeries_barsetsAdded_signal(XAbstractBarSeries* self,
                                             XBarSet* const* sets, int count)
{
    if (!self) return (void*)(size_t)XAbstractBarSeries_barsetsAdded_signal;
    xabs_emitSets(self, (size_t)XAbstractBarSeries_barsetsAdded_signal,
                  sets, count);
    return (void*)(size_t)XAbstractBarSeries_barsetsAdded_signal;
}

void* XAbstractBarSeries_barsetsRemoved_signal(XAbstractBarSeries* self,
                                               XBarSet* const* sets,
                                               int count)
{
    if (!self) return (void*)(size_t)XAbstractBarSeries_barsetsRemoved_signal;
    xabs_emitSets(self, (size_t)XAbstractBarSeries_barsetsRemoved_signal,
                  sets, count);
    return (void*)(size_t)XAbstractBarSeries_barsetsRemoved_signal;
}

#endif /* XCHARTS_ON */
