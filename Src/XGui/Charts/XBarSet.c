/**
 * @file       XBarSet.c
 * @brief      XBarSet 柱组对象实现（对标 Qt Charts 6.8 QBarSet）。
 * @details    数值/选中集合/画笔画刷/字体与全部属性信号；交互信号
 *             （clicked/hovered/pressed/released/doubleClicked）由渲染
 *             命中测试（2.18b）调用发射。
 * @note       模块总开关 XCHARTS_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XBarSet.h"

#include "XAlgorithm.h"
#include "XString.h"
#include "XMemory.h"
#include "XClass.h"

#if XCHARTS_ON

/* ==================== 内部发射辅助 ==================== */

/** @brief 发射无载荷信号。 */
static void xbs_emitVoid(XBarSet* self, size_t signal)
{
    if (!self) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
}

/** @brief 发射单 int 载荷信号。 */
static void xbs_emitInt(XBarSet* self, size_t signal, int index)
{
    int vi = index;
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

/** @brief 发射双 int 载荷信号。 */
static void xbs_emitIntInt(XBarSet* self, size_t signal, int index, int count)
{
    int vi = index;
    int vc = count;
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(int, vi), XVar(int, vc));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射 uint32_t 颜色载荷信号。 */
static void xbs_emitColor(XBarSet* self, size_t signal, uint32_t color)
{
    uint32_t vc = color;
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(uint32_t, vc));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射 bool+int 载荷信号。 */
static void xbs_emitBoolInt(XBarSet* self, size_t signal, bool status,
                            int index)
{
    bool vb = status;
    int vi = index;
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(bool, vb), XVar(int, vi));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射字体（族+字号）载荷信号。 */
static void xbs_emitFont(XBarSet* self, size_t signal, const char* family,
                         int pointSize)
{
    const char* vf = family ? family : "";
    int vs = pointSize;
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(const char*, vf), XVar(int, vs));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射选中集合（数组+数量）载荷信号。 */
static void xbs_emitSelected(XBarSet* self, size_t signal,
                             const int* indexes, int count)
{
    const int* vi = indexes;
    int vc = count;
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(const int*, vi), XVar(int, vc));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/* ==================== 类与对象生命周期 ==================== */

static void VXBarSet_deinit(XBarSet* self);
static void VXBarSet_copy(XBarSet* self, const XBarSet* other);
static void VXBarSet_move(XBarSet* self, XBarSet* other);

XVtable* XBarSet_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XBarSet)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXBarSet_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXBarSet_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXBarSet_move);
    return XVTABLE_DEFAULT;
}

void XBarSet_init(XBarSet* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init(&self->m_base);
    XClassSetVtable(self, XBarSet);
    self->m_label = XString_create();
    self->m_penWidth = 2.0;
}

void XBarSet_init_ex(XBarSet* self, const XString* label)
{
    if (!self) return;
    XBarSet_init(self);
    XBarSet_setLabel(self, label);
}
void XBarSet_init_ex_2(XBarSet* self, const char* label)
{
    XString* tmp = NULL;
    if (label) {
        tmp = XString_create_utf8(label);
        if (!tmp) return;
    }
    XBarSet_init_ex(self, tmp);
    if (tmp) XString_delete_base(tmp);
}

XBarSet* XBarSet_create_ex(XMemoryType memory, const XString* label)
{
    XBarSet* self = (XBarSet*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XBarSet_init_ex(self, label);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}
XBarSet* XBarSet_create_ex_2(XMemoryType memory, const char* label)
{
    XString* tmp = NULL;
    XBarSet* self;
    if (label) {
        tmp = XString_create_utf8(label);
        if (!tmp) return NULL;
    }
    self = XBarSet_create_ex(memory, tmp);
    if (tmp) XString_delete_base(tmp);
    return self;
}

static void VXBarSet_deinit(XBarSet* self)
{
    if (!self) return;
    if (self->m_label) {
        XString_delete_base(self->m_label);
        self->m_label = NULL;
    }
    if (self->m_labelFontFamily) {
        XString_delete_base(self->m_labelFontFamily);
        self->m_labelFontFamily = NULL;
    }
    if (self->m_values) {
        XFree_System(self->m_values);
        self->m_values = NULL;
    }
    if (self->m_selectedBars) {
        XFree_System(self->m_selectedBars);
        self->m_selectedBars = NULL;
    }
    self->m_count = 0;
    self->m_capacity = 0;
    self->m_selectedCount = 0;
    self->m_selectedCapacity = 0;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static void VXBarSet_copy(XBarSet* self, const XBarSet* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XBarSet_init(self);
    if (self->m_label && other->m_label)
        XString_assign(self->m_label, other->m_label);
    if (other->m_count > 0 && other->m_values) {
        double* v = (double*)XMalloc_System(
            sizeof(double) * (size_t)other->m_count);
        if (v) {
            XMemcpy(v, other->m_values, sizeof(double) * (size_t)other->m_count);
            if (self->m_values) XFree_System(self->m_values);
            self->m_values = v;
            self->m_count = other->m_count;
            self->m_capacity = other->m_count;
        }
    } else {
        if (self->m_values) XFree_System(self->m_values);
        self->m_values = NULL;
        self->m_count = 0;
        self->m_capacity = 0;
    }
    if (other->m_selectedCount > 0 && other->m_selectedBars) {
        int* s = (int*)XMalloc_System(
            sizeof(int) * (size_t)other->m_selectedCount);
        if (s) {
            XMemcpy(s, other->m_selectedBars,
                    sizeof(int) * (size_t)other->m_selectedCount);
            if (self->m_selectedBars) XFree_System(self->m_selectedBars);
            self->m_selectedBars = s;
            self->m_selectedCount = other->m_selectedCount;
            self->m_selectedCapacity = other->m_selectedCount;
        }
    } else {
        if (self->m_selectedBars) XFree_System(self->m_selectedBars);
        self->m_selectedBars = NULL;
        self->m_selectedCount = 0;
        self->m_selectedCapacity = 0;
    }
    self->m_penColor = other->m_penColor;
    self->m_penWidth = other->m_penWidth;
    self->m_brushColor = other->m_brushColor;
    self->m_labelBrushColor = other->m_labelBrushColor;
    self->m_labelFontSize = other->m_labelFontSize;
    self->m_selectedColor = other->m_selectedColor;
    if (self->m_labelFontFamily && other->m_labelFontFamily)
        XString_assign(self->m_labelFontFamily, other->m_labelFontFamily);
}

static void VXBarSet_move(XBarSet* self, XBarSet* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XBarSet_init(self);
    VXBarSet_deinit(self);
    *self = *other;
    XMemset(other, 0, sizeof(XBarSet));
    XBarSet_init(other);
    XClassSetVtable(self, XBarSet);
}

/* ==================== 标签 ==================== */

void XBarSet_setLabel(XBarSet* self, const XString* label)
{
    if (!self) return;
    if (!self->m_label) self->m_label = XString_create();
    if (!self->m_label) return;
    if (label)
        XString_assign(self->m_label, label);
    else
        XString_assign_utf8(self->m_label, "");
    xbs_emitVoid(self, (size_t)XBarSet_labelChanged_signal);
}
void XBarSet_setLabel_2(XBarSet* self, const char* label)
{
    XString* tmp = NULL;
    if (label) {
        tmp = XString_create_utf8(label);
        if (!tmp) return;
    }
    XBarSet_setLabel(self, tmp);
    if (tmp) XString_delete_base(tmp);
}

const XString* XBarSet_label(const XBarSet* self)
{
    return (self && self->m_label) ? self->m_label : NULL;
}
const char* XBarSet_label_2(const XBarSet* self)
{
    const XString* s;
    const char* text;
    s = XBarSet_label(self);
    if (!s) return "";
    text = XString_toUtf8(s);
    return text ? text : "";
}

/* ==================== 数值操作 ==================== */

static bool xbs_growValues(XBarSet* self, int need)
{
    int cap;
    double* v;
    if (need <= self->m_capacity) return true;
    cap = self->m_capacity > 0 ? self->m_capacity : 8;
    while (cap < need) cap *= 2;
    v = (double*)XRealloc_System(self->m_values, sizeof(double) * (size_t)cap);
    if (!v) return false;
    self->m_values = v;
    self->m_capacity = cap;
    return true;
}

void XBarSet_append(XBarSet* self, double value)
{
    int index;
    if (!self) return;
    if (!xbs_growValues(self, self->m_count + 1)) return;
    index = self->m_count;
    self->m_values[index] = value;
    self->m_count++;
    xbs_emitIntInt(self, (size_t)XBarSet_valuesAdded_signal, index, 1);
}

void XBarSet_appendValues(XBarSet* self, const double* values, int count)
{
    int index;
    if (!self || !values || count <= 0) return;
    if (!xbs_growValues(self, self->m_count + count)) return;
    index = self->m_count;
    XMemcpy(&self->m_values[index], values, sizeof(double) * (size_t)count);
    self->m_count += count;
    xbs_emitIntInt(self, (size_t)XBarSet_valuesAdded_signal, index, count);
}

void XBarSet_insert(XBarSet* self, int index, double value)
{
    int i;
    int moved;
    if (!self) return;
    if (index < 0) index = 0;
    if (index > self->m_count) index = self->m_count;
    if (!xbs_growValues(self, self->m_count + 1)) return;
    for (i = self->m_count; i > index; --i)
        self->m_values[i] = self->m_values[i - 1];
    self->m_values[index] = value;
    self->m_count++;
    xbs_emitIntInt(self, (size_t)XBarSet_valuesAdded_signal, index, 1);
    /* 插入后选中下标 >= index 者 +1（对标 Qt）。 */
    moved = 0;
    for (i = 0; i < self->m_selectedCount; ++i) {
        if (self->m_selectedBars[i] >= index) {
            self->m_selectedBars[i]++;
            moved = 1;
        }
    }
    if (moved) {
        int tmp[64];
        int cnt = XBarSet_selectedBars(self, tmp, 64);
        xbs_emitSelected(self, (size_t)XBarSet_selectedBarsChanged_signal,
                         tmp, cnt);
    }
}

void XBarSet_remove(XBarSet* self, int index, int count)
{
    int removeCount;
    int i;
    int changed;
    if (!self || index < 0 || self->m_count == 0 || count <= 0) return;
    if (index + count > self->m_count) removeCount = self->m_count - index;
    else removeCount = count;
    XMemmove(&self->m_values[index], &self->m_values[index + removeCount],
             sizeof(double) * (size_t)(self->m_count - index - removeCount));
    self->m_count -= removeCount;
    /* 选中集合：移除区间内丢弃，其后下标平移。 */
    changed = 0;
    if (self->m_selectedCount > 0) {
        int* keep = (int*)XMalloc_System(
            sizeof(int) * (size_t)self->m_selectedCount);
        int k = 0;
        if (keep) {
            for (i = 0; i < self->m_selectedCount; ++i) {
                int s = self->m_selectedBars[i];
                if (s < index) {
                    keep[k++] = s;
                } else if (s >= index + removeCount) {
                    keep[k++] = s - removeCount;
                    changed = 1;
                } else {
                    changed = 1;
                }
            }
            XFree_System(self->m_selectedBars);
            self->m_selectedBars = keep;
            self->m_selectedCount = k;
            self->m_selectedCapacity = k;
        }
    }
    xbs_emitIntInt(self, (size_t)XBarSet_valuesRemoved_signal, index,
                   removeCount);
    if (changed) {
        int tmp[64];
        int cnt = XBarSet_selectedBars(self, tmp, 64);
        xbs_emitSelected(self, (size_t)XBarSet_selectedBarsChanged_signal,
                         tmp, cnt);
    }
}

bool XBarSet_replace(XBarSet* self, int index, double value)
{
    if (!self || index < 0 || index >= self->m_count) return false;
    self->m_values[index] = value;
    xbs_emitInt(self, (size_t)XBarSet_valueChanged_signal, index);
    return true;
}

double XBarSet_at(const XBarSet* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return 0;
    return self->m_values[index];
}

int XBarSet_count(const XBarSet* self)
{ return self ? self->m_count : 0; }

double XBarSet_sum(const XBarSet* self)
{
    double total = 0;
    int i;
    if (!self) return 0;
    for (i = 0; i < self->m_count; ++i)
        total += self->m_values[i];
    return total;
}

int XBarSet_values(const XBarSet* self, double* out, int maxCount)
{
    int n;
    if (!self || !out || maxCount <= 0) return 0;
    n = self->m_count < maxCount ? self->m_count : maxCount;
    if (self->m_values)
        XMemcpy(out, self->m_values, sizeof(double) * (size_t)n);
    return n;
}

/* ==================== 画笔/画刷/字体 ==================== */

void XBarSet_setPen(XBarSet* self, uint32_t color, double width)
{
    if (!self) return;
    if (self->m_penColor == color && self->m_penWidth == width) return;
    self->m_penColor = color;
    if (width > 0) self->m_penWidth = width;
    xbs_emitVoid(self, (size_t)XBarSet_penChanged_signal);
}

void XBarSet_pen(const XBarSet* self, uint32_t* color, double* width)
{
    if (!self) return;
    if (color) *color = self->m_penColor;
    if (width) *width = self->m_penWidth;
}

void XBarSet_setBrush(XBarSet* self, uint32_t color)
{
    if (!self || self->m_brushColor == color) return;
    self->m_brushColor = color;
    xbs_emitVoid(self, (size_t)XBarSet_brushChanged_signal);
}

uint32_t XBarSet_brush(const XBarSet* self)
{ return self ? self->m_brushColor : 0; }

void XBarSet_setLabelBrush(XBarSet* self, uint32_t color)
{
    if (!self || self->m_labelBrushColor == color) return;
    self->m_labelBrushColor = color;
    xbs_emitVoid(self, (size_t)XBarSet_labelBrushChanged_signal);
}

uint32_t XBarSet_labelBrush(const XBarSet* self)
{ return self ? self->m_labelBrushColor : 0; }

void XBarSet_setLabelFont(XBarSet* self, const XString* family,
                          int pointSize)
{
    bool changed = false;
    if (!self) return;
    if (family) {
        if (!self->m_labelFontFamily)
            self->m_labelFontFamily = XString_create();
        if (!self->m_labelFontFamily) return;
        if (!XString_equals(self->m_labelFontFamily, family,
                            XChar_CaseSensitive))
            changed = true;
        if (changed) XString_assign(self->m_labelFontFamily, family);
    }
    if (pointSize > 0 && self->m_labelFontSize != pointSize) {
        self->m_labelFontSize = pointSize;
        changed = true;
    }
    if (changed)
        xbs_emitFont(self, (size_t)XBarSet_labelFontChanged_signal,
                     XBarSet_labelFont_2(self), self->m_labelFontSize);
}
void XBarSet_setLabelFont_2(XBarSet* self, const char* family, int pointSize)
{
    XString* tmp = NULL;
    if (family) {
        tmp = XString_create_utf8(family);
        if (!tmp) return;
    }
    XBarSet_setLabelFont(self, tmp, pointSize);
    if (tmp) XString_delete_base(tmp);
}

const XString* XBarSet_labelFont(const XBarSet* self)
{
    return (self && self->m_labelFontFamily) ? self->m_labelFontFamily : NULL;
}
const char* XBarSet_labelFont_2(const XBarSet* self)
{
    const XString* s;
    const char* text;
    s = XBarSet_labelFont(self);
    if (!s) return "";
    text = XString_toUtf8(s);
    return text ? text : "";
}

int XBarSet_labelFontSize(const XBarSet* self)
{ return self ? self->m_labelFontSize : 0; }

/* ==================== 颜色便捷 ==================== */

void XBarSet_setColor(XBarSet* self, uint32_t color)
{
    if (!self || self->m_brushColor == color) return;
    self->m_brushColor = color;
    xbs_emitVoid(self, (size_t)XBarSet_brushChanged_signal);
    xbs_emitColor(self, (size_t)XBarSet_colorChanged_signal, color);
}

uint32_t XBarSet_color(const XBarSet* self)
{ return self ? self->m_brushColor : 0; }

void XBarSet_setBorderColor(XBarSet* self, uint32_t color)
{
    if (!self || self->m_penColor == color) return;
    self->m_penColor = color;
    xbs_emitVoid(self, (size_t)XBarSet_penChanged_signal);
    xbs_emitColor(self, (size_t)XBarSet_borderColorChanged_signal, color);
}

uint32_t XBarSet_borderColor(const XBarSet* self)
{ return self ? self->m_penColor : 0; }

void XBarSet_setLabelColor(XBarSet* self, uint32_t color)
{
    if (!self || self->m_labelBrushColor == color) return;
    self->m_labelBrushColor = color;
    xbs_emitVoid(self, (size_t)XBarSet_labelBrushChanged_signal);
    xbs_emitColor(self, (size_t)XBarSet_labelColorChanged_signal, color);
}

uint32_t XBarSet_labelColor(const XBarSet* self)
{ return self ? self->m_labelBrushColor : 0; }

void XBarSet_setSelectedColor(XBarSet* self, uint32_t color)
{
    if (!self || self->m_selectedColor == color) return;
    self->m_selectedColor = color;
    xbs_emitColor(self, (size_t)XBarSet_selectedColorChanged_signal, color);
}

uint32_t XBarSet_selectedColor(const XBarSet* self)
{ return self ? self->m_selectedColor : 0; }

/* ==================== 选中状态 ==================== */

static bool xbs_contains(const XBarSet* self, int index)
{
    int lo = 0;
    int hi = self->m_selectedCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (self->m_selectedBars[mid] == index) return true;
        if (self->m_selectedBars[mid] < index) lo = mid + 1;
        else hi = mid - 1;
    }
    return false;
}

static void xbs_addSelected(XBarSet* self, int index, bool* changed)
{
    int i;
    int pos;
    int* s;
    if (xbs_contains(self, index)) return;
    if (self->m_selectedCount >= self->m_selectedCapacity) {
        int cap = self->m_selectedCapacity > 0
            ? self->m_selectedCapacity * 2 : 8;
        s = (int*)XRealloc_System(self->m_selectedBars,
                                  sizeof(int) * (size_t)cap);
        if (!s) return;
        self->m_selectedBars = s;
        self->m_selectedCapacity = cap;
    }
    pos = self->m_selectedCount;
    for (i = 0; i < self->m_selectedCount; ++i) {
        if (self->m_selectedBars[i] > index) { pos = i; break; }
    }
    for (i = self->m_selectedCount; i > pos; --i)
        self->m_selectedBars[i] = self->m_selectedBars[i - 1];
    self->m_selectedBars[pos] = index;
    self->m_selectedCount++;
    if (changed) *changed = true;
}

static void xbs_removeSelected(XBarSet* self, int index, bool* changed)
{
    int i;
    if (!xbs_contains(self, index)) return;
    for (i = 0; i < self->m_selectedCount; ++i) {
        if (self->m_selectedBars[i] == index) {
            XMemmove(&self->m_selectedBars[i], &self->m_selectedBars[i + 1],
                     sizeof(int) * (size_t)(self->m_selectedCount - i - 1));
            self->m_selectedCount--;
            if (changed) *changed = true;
            return;
        }
    }
}

bool XBarSet_isBarSelected(const XBarSet* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return false;
    return xbs_contains(self, index);
}

void XBarSet_setBarSelected(XBarSet* self, int index, bool selected)
{
    bool changed = false;
    int tmp[64];
    int cnt;
    if (!self || index < 0 || index >= self->m_count) return;
    if (selected) xbs_addSelected(self, index, &changed);
    else xbs_removeSelected(self, index, &changed);
    if (changed) {
        cnt = XBarSet_selectedBars(self, tmp, 64);
        xbs_emitSelected(self, (size_t)XBarSet_selectedBarsChanged_signal,
                         tmp, cnt);
    }
}

void XBarSet_selectBar(XBarSet* self, int index)
{ XBarSet_setBarSelected(self, index, true); }

void XBarSet_deselectBar(XBarSet* self, int index)
{ XBarSet_setBarSelected(self, index, false); }

void XBarSet_selectAllBars(XBarSet* self)
{
    bool changed = false;
    int i;
    int tmp[64];
    int cnt;
    if (!self) return;
    for (i = 0; i < self->m_count; ++i)
        xbs_addSelected(self, i, &changed);
    if (changed) {
        cnt = XBarSet_selectedBars(self, tmp, 64);
        xbs_emitSelected(self, (size_t)XBarSet_selectedBarsChanged_signal,
                         tmp, cnt);
    }
}

void XBarSet_deselectAllBars(XBarSet* self)
{
    int tmp[64];
    int cnt;
    if (!self || self->m_selectedCount == 0) return;
    self->m_selectedCount = 0;
    cnt = XBarSet_selectedBars(self, tmp, 64);
    xbs_emitSelected(self, (size_t)XBarSet_selectedBarsChanged_signal,
                     tmp, cnt);
}

void XBarSet_selectBars(XBarSet* self, const int* indexes, int count)
{
    bool changed = false;
    int i;
    int tmp[64];
    int cnt;
    if (!self || !indexes) return;
    for (i = 0; i < count; ++i)
        xbs_addSelected(self, indexes[i], &changed);
    if (changed) {
        cnt = XBarSet_selectedBars(self, tmp, 64);
        xbs_emitSelected(self, (size_t)XBarSet_selectedBarsChanged_signal,
                         tmp, cnt);
    }
}

void XBarSet_deselectBars(XBarSet* self, const int* indexes, int count)
{
    bool changed = false;
    int i;
    int tmp[64];
    int cnt;
    if (!self || !indexes) return;
    for (i = 0; i < count; ++i)
        xbs_removeSelected(self, indexes[i], &changed);
    if (changed) {
        cnt = XBarSet_selectedBars(self, tmp, 64);
        xbs_emitSelected(self, (size_t)XBarSet_selectedBarsChanged_signal,
                         tmp, cnt);
    }
}

void XBarSet_toggleSelection(XBarSet* self, const int* indexes, int count)
{
    bool changed = false;
    int i;
    int tmp[64];
    int cnt;
    if (!self || !indexes) return;
    for (i = 0; i < count; ++i) {
        int idx = indexes[i];
        if (idx < 0 || idx >= self->m_count) continue;
        if (xbs_contains(self, idx))
            xbs_removeSelected(self, idx, &changed);
        else
            xbs_addSelected(self, idx, &changed);
    }
    if (changed) {
        cnt = XBarSet_selectedBars(self, tmp, 64);
        xbs_emitSelected(self, (size_t)XBarSet_selectedBarsChanged_signal,
                         tmp, cnt);
    }
}

int XBarSet_selectedBars(const XBarSet* self, int* out, int maxCount)
{
    int n;
    if (!self) return 0;
    if (!out || maxCount <= 0) return self->m_selectedCount;
    n = self->m_selectedCount < maxCount ? self->m_selectedCount : maxCount;
    if (self->m_selectedBars)
        XMemcpy(out, self->m_selectedBars, sizeof(int) * (size_t)n);
    return n;
}

/* ==================== 信号 ==================== */

void* XBarSet_clicked_signal(XBarSet* self, int index)
{
    if (!self) return (void*)(size_t)XBarSet_clicked_signal;
    xbs_emitInt(self, (size_t)XBarSet_clicked_signal, index);
    return (void*)(size_t)XBarSet_clicked_signal;
}

void* XBarSet_hovered_signal(XBarSet* self, bool status, int index)
{
    if (!self) return (void*)(size_t)XBarSet_hovered_signal;
    xbs_emitBoolInt(self, (size_t)XBarSet_hovered_signal, status, index);
    return (void*)(size_t)XBarSet_hovered_signal;
}

void* XBarSet_pressed_signal(XBarSet* self, int index)
{
    if (!self) return (void*)(size_t)XBarSet_pressed_signal;
    xbs_emitInt(self, (size_t)XBarSet_pressed_signal, index);
    return (void*)(size_t)XBarSet_pressed_signal;
}

void* XBarSet_released_signal(XBarSet* self, int index)
{
    if (!self) return (void*)(size_t)XBarSet_released_signal;
    xbs_emitInt(self, (size_t)XBarSet_released_signal, index);
    return (void*)(size_t)XBarSet_released_signal;
}

void* XBarSet_doubleClicked_signal(XBarSet* self, int index)
{
    if (!self) return (void*)(size_t)XBarSet_doubleClicked_signal;
    xbs_emitInt(self, (size_t)XBarSet_doubleClicked_signal, index);
    return (void*)(size_t)XBarSet_doubleClicked_signal;
}

void* XBarSet_labelChanged_signal(XBarSet* self)
{
    if (!self) return (void*)(size_t)XBarSet_labelChanged_signal;
    xbs_emitVoid(self, (size_t)XBarSet_labelChanged_signal);
    return (void*)(size_t)XBarSet_labelChanged_signal;
}

void* XBarSet_penChanged_signal(XBarSet* self)
{
    if (!self) return (void*)(size_t)XBarSet_penChanged_signal;
    xbs_emitVoid(self, (size_t)XBarSet_penChanged_signal);
    return (void*)(size_t)XBarSet_penChanged_signal;
}

void* XBarSet_brushChanged_signal(XBarSet* self)
{
    if (!self) return (void*)(size_t)XBarSet_brushChanged_signal;
    xbs_emitVoid(self, (size_t)XBarSet_brushChanged_signal);
    return (void*)(size_t)XBarSet_brushChanged_signal;
}

void* XBarSet_labelBrushChanged_signal(XBarSet* self)
{
    if (!self) return (void*)(size_t)XBarSet_labelBrushChanged_signal;
    xbs_emitVoid(self, (size_t)XBarSet_labelBrushChanged_signal);
    return (void*)(size_t)XBarSet_labelBrushChanged_signal;
}

void* XBarSet_labelFontChanged_signal(XBarSet* self, const char* family,
                                      int pointSize)
{
    if (!self) return (void*)(size_t)XBarSet_labelFontChanged_signal;
    xbs_emitFont(self, (size_t)XBarSet_labelFontChanged_signal,
                 family, pointSize);
    return (void*)(size_t)XBarSet_labelFontChanged_signal;
}

void* XBarSet_colorChanged_signal(XBarSet* self, uint32_t color)
{
    if (!self) return (void*)(size_t)XBarSet_colorChanged_signal;
    xbs_emitColor(self, (size_t)XBarSet_colorChanged_signal, color);
    return (void*)(size_t)XBarSet_colorChanged_signal;
}

void* XBarSet_borderColorChanged_signal(XBarSet* self, uint32_t color)
{
    if (!self) return (void*)(size_t)XBarSet_borderColorChanged_signal;
    xbs_emitColor(self, (size_t)XBarSet_borderColorChanged_signal, color);
    return (void*)(size_t)XBarSet_borderColorChanged_signal;
}

void* XBarSet_labelColorChanged_signal(XBarSet* self, uint32_t color)
{
    if (!self) return (void*)(size_t)XBarSet_labelColorChanged_signal;
    xbs_emitColor(self, (size_t)XBarSet_labelColorChanged_signal, color);
    return (void*)(size_t)XBarSet_labelColorChanged_signal;
}

void* XBarSet_selectedColorChanged_signal(XBarSet* self, uint32_t color)
{
    if (!self) return (void*)(size_t)XBarSet_selectedColorChanged_signal;
    xbs_emitColor(self, (size_t)XBarSet_selectedColorChanged_signal, color);
    return (void*)(size_t)XBarSet_selectedColorChanged_signal;
}

void* XBarSet_valuesAdded_signal(XBarSet* self, int index, int count)
{
    if (!self) return (void*)(size_t)XBarSet_valuesAdded_signal;
    xbs_emitIntInt(self, (size_t)XBarSet_valuesAdded_signal, index, count);
    return (void*)(size_t)XBarSet_valuesAdded_signal;
}

void* XBarSet_valuesRemoved_signal(XBarSet* self, int index, int count)
{
    if (!self) return (void*)(size_t)XBarSet_valuesRemoved_signal;
    xbs_emitIntInt(self, (size_t)XBarSet_valuesRemoved_signal, index, count);
    return (void*)(size_t)XBarSet_valuesRemoved_signal;
}

void* XBarSet_valueChanged_signal(XBarSet* self, int index)
{
    if (!self) return (void*)(size_t)XBarSet_valueChanged_signal;
    xbs_emitInt(self, (size_t)XBarSet_valueChanged_signal, index);
    return (void*)(size_t)XBarSet_valueChanged_signal;
}

void* XBarSet_selectedBarsChanged_signal(XBarSet* self, const int* indexes,
                                         int count)
{
    if (!self) return (void*)(size_t)XBarSet_selectedBarsChanged_signal;
    xbs_emitSelected(self, (size_t)XBarSet_selectedBarsChanged_signal,
                     indexes, count);
    return (void*)(size_t)XBarSet_selectedBarsChanged_signal;
}

#endif /* XCHARTS_ON */
