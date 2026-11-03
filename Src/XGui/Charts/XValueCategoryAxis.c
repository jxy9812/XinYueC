/**
 * @file       XValueCategoryAxis.c
 * @brief      XValueCategoryAxis 数值类别轴实现（对标 QCategoryAxis）。
 * @details    类别以「标签 + 结束值」升序保存；类别 i 占
 *             [前一结束值, 本类别结束值] 区间，首类别起点为轴 min。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XValueCategoryAxis.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool xvcax_canEmit(const XValueCategoryAxis* self)
{
    return self && ((const XObject*)&self->m_base)->m_signalSlot;
}

static void xvcax_emitVoid(XValueCategoryAxis* self, size_t signal)
{
    XVarList* args;
    int dummy = 0;
    if (!xvcax_canEmit(self)) return;
    args = XVarList_Create(XVar(int, dummy));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xvcax_emitInt(XValueCategoryAxis* self, size_t signal, int v)
{
    XVarList* args;
    if (!xvcax_canEmit(self)) return;
    args = XVarList_Create(XVar(int, v));
    if (!args) return;
    XObject_emitSignal((XObject*)&self->m_base, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 内部助手 ==================== */

/** @brief 确保容量足够。 @return 成功返回 true。 */
static bool xvcax_grow(XValueCategoryAxis* self)
{
    int cap;
    XString** labels;
    double* ends;
    if (!self) return false;
    if (self->m_count < self->m_capacity) return true;
    cap = self->m_capacity > 0 ? self->m_capacity * 2 : 8;
    labels = (XString**)XRealloc_System(self->m_labels,
                                        sizeof(XString*) * (size_t)cap);
    ends = (double*)XRealloc_System(self->m_endValues,
                                    sizeof(double) * (size_t)cap);
    if (!labels || !ends) {
        /* 一侧成功一侧失败的收编：保持「块长度 >= 容量」不变式。 */
        if (labels) self->m_labels = labels;
        if (ends) self->m_endValues = ends;
        return false;
    }
    self->m_labels = labels;
    self->m_endValues = ends;
    self->m_capacity = cap;
    return true;
}

/** @brief 按标签查找下标；未找到返回 -1。 */
static int xvcax_indexOf(const XValueCategoryAxis* self, const XString* label)
{
    int i;
    if (!self || !label || !self->m_labels) return -1;
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_labels[i] && XString_compare(self->m_labels[i], label) == 0)
            return i;
    }
    return -1;
}

/** @brief 按 UTF-8 标签查找下标；未找到返回 -1。 */
static int xvcax_indexOfUtf8(const XValueCategoryAxis* self, const char* label)
{
    int i;
    if (!self || !label || !self->m_labels) return -1;
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_labels[i] &&
            XString_equals_utf8(self->m_labels[i], label,
                                XChar_CaseSensitive) == true)
            return i;
    }
    return -1;
}

/* ==================== 虚函数实现 ==================== */

static void VXValueCategoryAxis_deinit(XValueCategoryAxis* self)
{
    int i;
    if (!self) return;
    if (self->m_labels) {
        for (i = 0; i < self->m_count; ++i) {
            if (self->m_labels[i]) {
                XClassDelete(self->m_labels[i]);
                self->m_labels[i] = NULL;
            }
        }
        XFree_System(self->m_labels);
        self->m_labels = NULL;
    }
    if (self->m_endValues) {
        XFree_System(self->m_endValues);
        self->m_endValues = NULL;
    }
    self->m_count = 0;
    self->m_capacity = 0;
    XClass_Deinit_Parent(XValueAxis, &self->m_base);
}

XVtable* XValueCategoryAxis_class_init(void)
{
    XVTABLE_INIT_DEFAULT_SIZE(XCLASS_VTABLE_GET_SIZE(XValueAxis))
    XVTABLE_INHERIT_XCLASS(XValueAxis);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXValueCategoryAxis_deinit);
    return XVTABLE_DEFAULT;
}

void XValueCategoryAxis_init(XValueCategoryAxis* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XValueAxis_init(&self->m_base);
    XClassSetVtable(self, XValueCategoryAxis);
    self->m_base.m_base.m_axisType = XAbstractAxis_AxisType_Category;
    self->m_labelsPosition = XValueCategoryAxis_AxisLabelsPosition_Center;
}

XValueCategoryAxis* XValueCategoryAxis_create_ex(XMemoryType memory)
{
    XValueCategoryAxis* self =
        (XValueCategoryAxis*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XValueCategoryAxis_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XValueCategoryAxis_deinit_impl(XValueCategoryAxis* self)
{
    int i;
    if (!self) return;
    if (self->m_labels) {
        for (i = 0; i < self->m_count; ++i) {
            if (self->m_labels[i]) {
                XClassDelete(self->m_labels[i]);
                self->m_labels[i] = NULL;
            }
        }
        XFree_System(self->m_labels);
        self->m_labels = NULL;
    }
    if (self->m_endValues) {
        XFree_System(self->m_endValues);
        self->m_endValues = NULL;
    }
    XClassDeinit(&self->m_base);
}

/* ==================== 类型查询 ==================== */

int XValueCategoryAxis_type(const XValueCategoryAxis* self)
{
    (void)self;
    return XAbstractAxis_AxisType_Category;
}

/* ==================== 类别边界 ==================== */

bool XValueCategoryAxis_append(XValueCategoryAxis* self, const XString* label,
                               double categoryEndValue)
{
    if (!self || !label) return false;
    /* 结束值须严格大于已有最大值，保持升序（对标 QCategoryAxis 语义）。 */
    if (self->m_count > 0 && categoryEndValue <= self->m_endValues[self->m_count - 1])
        return false;
    if (!xvcax_grow(self)) return false;
    self->m_labels[self->m_count] = XString_create_copy(label);
    if (!self->m_labels[self->m_count]) return false;
    self->m_endValues[self->m_count] = categoryEndValue;
    ++self->m_count;
    xvcax_emitVoid(self, (size_t)XValueCategoryAxis_categoriesChanged_signal);
    return true;
}

bool XValueCategoryAxis_append_utf8(XValueCategoryAxis* self, const char* label,
                                    double categoryEndValue)
{
    XString* tmp;
    bool ok;
    if (!label) return false;
    tmp = XString_create_utf8(label);
    if (!tmp) return false;
    ok = XValueCategoryAxis_append(self, tmp, categoryEndValue);
    XClassDelete(tmp);
    return ok;
}

bool XValueCategoryAxis_append_2(XValueCategoryAxis* self, const char* label,
                                 double categoryEndValue)
{ return XValueCategoryAxis_append_utf8(self, label, categoryEndValue); }

bool XValueCategoryAxis_remove(XValueCategoryAxis* self, const XString* label)
{
    int idx;
    int i;
    if (!self || !label) return false;
    idx = xvcax_indexOf(self, label);
    if (idx < 0) return false;
    XClassDelete(self->m_labels[idx]);
    for (i = idx; i + 1 < self->m_count; ++i) {
        self->m_labels[i] = self->m_labels[i + 1];
        self->m_endValues[i] = self->m_endValues[i + 1];
    }
    self->m_labels[--self->m_count] = NULL;
    xvcax_emitVoid(self, (size_t)XValueCategoryAxis_categoriesChanged_signal);
    return true;
}

bool XValueCategoryAxis_remove_utf8(XValueCategoryAxis* self, const char* label)
{
    int idx;
    int i;
    if (!self || !label) return false;
    idx = xvcax_indexOfUtf8(self, label);
    if (idx < 0) return false;
    XClassDelete(self->m_labels[idx]);
    for (i = idx; i + 1 < self->m_count; ++i) {
        self->m_labels[i] = self->m_labels[i + 1];
        self->m_endValues[i] = self->m_endValues[i + 1];
    }
    self->m_labels[--self->m_count] = NULL;
    xvcax_emitVoid(self, (size_t)XValueCategoryAxis_categoriesChanged_signal);
    return true;
}

bool XValueCategoryAxis_replaceLabel(XValueCategoryAxis* self,
                                     const XString* oldLabel,
                                     const XString* newLabel)
{
    int idx;
    XString* cp;
    if (!self || !oldLabel || !newLabel) return false;
    idx = xvcax_indexOf(self, oldLabel);
    if (idx < 0) return false;
    cp = XString_create_copy(newLabel);
    if (!cp) return false;
    XClassDelete(self->m_labels[idx]);
    self->m_labels[idx] = cp;
    xvcax_emitVoid(self, (size_t)XValueCategoryAxis_categoriesChanged_signal);
    return true;
}

bool XValueCategoryAxis_replaceLabel_utf8(XValueCategoryAxis* self,
                                          const char* oldLabel,
                                          const char* newLabel)
{
    int idx;
    XString* cp;
    if (!self || !oldLabel || !newLabel) return false;
    idx = xvcax_indexOfUtf8(self, oldLabel);
    if (idx < 0) return false;
    cp = XString_create_utf8(newLabel);
    if (!cp) return false;
    XClassDelete(self->m_labels[idx]);
    self->m_labels[idx] = cp;
    xvcax_emitVoid(self, (size_t)XValueCategoryAxis_categoriesChanged_signal);
    return true;
}

void XValueCategoryAxis_clear(XValueCategoryAxis* self)
{
    int i;
    if (!self) return;
    if (self->m_labels) {
        for (i = 0; i < self->m_count; ++i) {
            if (self->m_labels[i]) {
                XClassDelete(self->m_labels[i]);
                self->m_labels[i] = NULL;
            }
        }
    }
    self->m_count = 0;
    xvcax_emitVoid(self, (size_t)XValueCategoryAxis_categoriesChanged_signal);
}

int XValueCategoryAxis_count(const XValueCategoryAxis* self)
{ return self ? self->m_count : 0; }

void XValueCategoryAxis_setStartValue(XValueCategoryAxis* self, double min)
{
    if (!self) return;
    XAbstractAxis_setRange(&self->m_base.m_base, min, self->m_base.m_base.m_max);
}

int XValueCategoryAxis_categoriesLabels(const XValueCategoryAxis* self,
                                        const XString** out, int maxCount)
{
    int i;
    int n;
    if (!self || !out || maxCount <= 0) return 0;
    n = self->m_count < maxCount ? self->m_count : maxCount;
    for (i = 0; i < n; ++i) out[i] = self->m_labels[i];
    return n;
}

double XValueCategoryAxis_startValue(const XValueCategoryAxis* self,
                                     const XString* label)
{
    int idx;
    if (!self) return 0.0;
    if (!label) return self->m_base.m_base.m_min;
    idx = xvcax_indexOf(self, label);
    if (idx < 0) return 0.0;
    if (idx == 0) return self->m_base.m_base.m_min;
    return self->m_endValues[idx - 1];
}

double XValueCategoryAxis_endValue(const XValueCategoryAxis* self,
                                   const XString* label)
{
    int idx;
    if (!self || !label) return 0.0;
    idx = xvcax_indexOf(self, label);
    if (idx < 0) return 0.0;
    return self->m_endValues[idx];
}

const XString* XValueCategoryAxis_labelAt_const(const XValueCategoryAxis* self,
                                                int index)
{
    if (!self || index < 0 || index >= self->m_count) return NULL;
    return self->m_labels[index];
}

const char* XValueCategoryAxis_labelAt_2(const XValueCategoryAxis* self,
                                         int index)
{
    const XString* s = XValueCategoryAxis_labelAt_const(self, index);
    return s ? XString_toUtf8(s) : "";
}

/* ==================== 标签位置 ==================== */

void XValueCategoryAxis_setLabelsPosition(
    XValueCategoryAxis* self, XValueCategoryAxis_AxisLabelsPosition position)
{
    if (!self || self->m_labelsPosition == (int)position) return;
    self->m_labelsPosition = (int)position;
    xvcax_emitInt(self,
                  (size_t)XValueCategoryAxis_labelsPositionChanged_signal,
                  (int)position);
}

XValueCategoryAxis_AxisLabelsPosition XValueCategoryAxis_labelsPosition(
    const XValueCategoryAxis* self)
{
    return self ? (XValueCategoryAxis_AxisLabelsPosition)self->m_labelsPosition
                : XValueCategoryAxis_AxisLabelsPosition_Center;
}

/* ==================== 信号 ==================== */

void* XValueCategoryAxis_categoriesChanged_signal(XValueCategoryAxis* self)
{
    xvcax_emitVoid(self,
                   (size_t)XValueCategoryAxis_categoriesChanged_signal);
    return (void*)(size_t)XValueCategoryAxis_categoriesChanged_signal;
}

void* XValueCategoryAxis_labelsPositionChanged_signal(
    XValueCategoryAxis* self, int position)
{
    xvcax_emitInt(self,
                  (size_t)XValueCategoryAxis_labelsPositionChanged_signal,
                  position);
    return (void*)(size_t)XValueCategoryAxis_labelsPositionChanged_signal;
}

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
