#include "XXYSeries.h"

#include "XAlgorithm.h"
#include "XString.h"
#include "XMemory.h"
#include "XClass.h"

#if XCHARTS_ON

static void VXXYSeries_deinit(XXYSeries* self);
static void VXXYSeries_copy(XXYSeries* self, const XXYSeries* other);
static void VXXYSeries_move(XXYSeries* self, XXYSeries* other);

/** @brief 发射双 double 载荷信号。 */
static void xxy_emitXY(XXYSeries* self, size_t signal, double x, double y)
{
    double vx = x;
    double vy = y;
    XVarList* args = XVarList_Create(XVar(double, vx), XVar(double, vy));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射单 int 载荷信号。 */
static void xxy_emitIndex(XXYSeries* self, size_t signal, int index)
{
    int vi = index;
    XVarList* args = XVarList_Create(XVar(int, vi));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射无载荷信号。 */
static void xxy_emitVoid(XXYSeries* self, size_t signal)
{
    if (!self) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
}

/** @brief 发射双 int 载荷信号。 */
static void xxy_emitIndexCount(XXYSeries* self, size_t signal, int index,
                               int count)
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

/** @brief 发射 bool 载荷信号。 */
static void xxy_emitBool(XXYSeries* self, size_t signal, bool value)
{
    bool vb = value;
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(bool, vb));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射 double 载荷信号。 */
static void xxy_emitDouble(XXYSeries* self, size_t signal, double value)
{
    double vd = value;
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

/** @brief 发射 uint32_t 颜色载荷信号。 */
static void xxy_emitColor(XXYSeries* self, size_t signal, uint32_t color)
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

/** @brief 发射字符串载荷信号。 */
static void xxy_emitStr(XXYSeries* self, size_t signal, const char* text)
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

/** @brief 发射字体（族+字号）载荷信号。 */
static void xxy_emitFont(XXYSeries* self, size_t signal, const char* family,
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

/** @brief 发射指针载荷信号。 */
static void xxy_emitPtr(XXYSeries* self, size_t signal, const void* ptr)
{
    const void* vp = ptr;
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(const void*, vp));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

/** @brief 发射画笔（颜色+线宽）载荷信号。 */
static void xxy_emitPen(XXYSeries* self, size_t signal, uint32_t color,
                        double width)
{
    uint32_t vc = color;
    double vw = width;
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(uint32_t, vc), XVar(double, vw));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

XVtable* XXYSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XXYSeries)
    XVTABLE_INHERIT_XCLASS(XAbstractSeries);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXXYSeries_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXXYSeries_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXXYSeries_move);
    return XVTABLE_DEFAULT;
}

void XXYSeries_init(XXYSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractSeries_init(&self->m_base);
    XClassSetVtable(self, XXYSeries);
    self->m_width = 2.0;
    self->m_markerSize = 8.0;
    self->m_pointLabelsFormat = XString_create_utf8("@xPoint, @yPoint");
    self->m_pointsVisible = false;
    self->m_pointLabelsVisible = false;
    self->m_brush = 0;
    self->m_selectedColor = 0;
    self->m_pointLabelsClipping = true;
    self->m_pointLabelsFontFamily = NULL;
    self->m_pointLabelsFontSize = 0;
    self->m_bestFitVisible = false;
    self->m_bestFitColor = 0;
    self->m_bestFitWidth = 2.0;
}

XXYSeries* XXYSeries_create_ex(XMemoryType memory)
{
    XXYSeries* self = (XXYSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XXYSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXXYSeries_deinit(XXYSeries* self)
{
    int i;
    if (!self) return;
    if (self->m_points) {
        XFree_System(self->m_points);
        self->m_points = NULL;
    }
    if (self->m_selected) {
        XFree_System(self->m_selected);
        self->m_selected = NULL;
    }
    if (self->m_pointColors) {
        XFree_System(self->m_pointColors);
        self->m_pointColors = NULL;
    }
    if (self->m_pointSizes) {
        XFree_System(self->m_pointSizes);
        self->m_pointSizes = NULL;
    }
    if (self->m_pointLabelsFormat) {
        XString_delete_base(self->m_pointLabelsFormat);
        self->m_pointLabelsFormat = NULL;
    }
    if (self->m_pointLabelsFontFamily) {
        XString_delete_base(self->m_pointLabelsFontFamily);
        self->m_pointLabelsFontFamily = NULL;
    }
    XClass_Deinit_Parent(XAbstractSeries, (XAbstractSeries*)self);
}

static void VXXYSeries_copy(XXYSeries* self, const XXYSeries* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XXYSeries_init(self);
    XClass_Parent(XAbstractSeries, EXClass_Copy,
                  void(*)(XAbstractSeries*, const XAbstractSeries*))(
        (XAbstractSeries*)self, (const XAbstractSeries*)other);
    XXYSeries_clear(self);
    if (other->m_count > 0 && other->m_points) {
        self->m_points = (XPointF*)XMalloc_System(
            sizeof(XPointF) * (size_t)other->m_count);
        if (self->m_points) {
            XMemcpy(self->m_points, other->m_points,
                   sizeof(XPointF) * (size_t)other->m_count);
            self->m_count = other->m_count;
            self->m_capacity = other->m_count;
        }
    }
    if (other->m_selected && other->m_count > 0) {
        self->m_selected = (bool*)XMalloc_System(
            sizeof(bool) * (size_t)other->m_count);
        if (self->m_selected)
            XMemcpy(self->m_selected, other->m_selected,
                   sizeof(bool) * (size_t)other->m_count);
    }
    self->m_color = other->m_color;
    self->m_width = other->m_width;
    self->m_markerSize = other->m_markerSize;
    self->m_pointsVisible = other->m_pointsVisible;
    self->m_pointLabelsVisible = other->m_pointLabelsVisible;
    self->m_pointLabelsColor = other->m_pointLabelsColor;
    self->m_brush = other->m_brush;
    self->m_selectedColor = other->m_selectedColor;
    self->m_pointLabelsClipping = other->m_pointLabelsClipping;
    self->m_pointLabelsFontSize = other->m_pointLabelsFontSize;
    self->m_bestFitVisible = other->m_bestFitVisible;
    self->m_bestFitColor = other->m_bestFitColor;
    self->m_bestFitWidth = other->m_bestFitWidth;
    if (self->m_pointLabelsFontFamily && other->m_pointLabelsFontFamily)
        XString_assign(self->m_pointLabelsFontFamily,
                       other->m_pointLabelsFontFamily);
    if (self->m_pointLabelsFormat && other->m_pointLabelsFormat)
        XString_assign(self->m_pointLabelsFormat,
                       other->m_pointLabelsFormat);
}

static void VXXYSeries_move(XXYSeries* self, XXYSeries* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XXYSeries_init(self);
    XClass_Parent(XAbstractSeries, EXClass_Move,
                  void(*)(XAbstractSeries*, XAbstractSeries*))(
        (XAbstractSeries*)self, (XAbstractSeries*)other);
    if (self->m_points) XFree_System(self->m_points);
    if (self->m_selected) XFree_System(self->m_selected);
    if (self->m_pointLabelsFormat)
        XString_delete_base(self->m_pointLabelsFormat);
    self->m_points = other->m_points;
    other->m_points = NULL;
    self->m_count = other->m_count;
    other->m_count = 0;
    self->m_capacity = other->m_capacity;
    other->m_capacity = 0;
    self->m_selected = other->m_selected;
    other->m_selected = NULL;
    self->m_color = other->m_color;
    self->m_width = other->m_width;
    self->m_markerSize = other->m_markerSize;
    self->m_pointsVisible = other->m_pointsVisible;
    self->m_pointLabelsVisible = other->m_pointLabelsVisible;
    self->m_pointLabelsColor = other->m_pointLabelsColor;
    self->m_brush = other->m_brush;
    self->m_selectedColor = other->m_selectedColor;
    self->m_pointLabelsClipping = other->m_pointLabelsClipping;
    self->m_pointLabelsFontSize = other->m_pointLabelsFontSize;
    self->m_bestFitVisible = other->m_bestFitVisible;
    self->m_bestFitColor = other->m_bestFitColor;
    self->m_bestFitWidth = other->m_bestFitWidth;
    if (self->m_pointLabelsFontFamily)
        XString_delete_base(self->m_pointLabelsFontFamily);
    self->m_pointLabelsFontFamily = other->m_pointLabelsFontFamily;
    other->m_pointLabelsFontFamily = NULL;
    self->m_pointLabelsFormat = other->m_pointLabelsFormat;
    other->m_pointLabelsFormat = XString_create_utf8("@xPoint, @yPoint");
}

/* ==================== 数据操作 ==================== */

/**
 * @brief 选中位图与点数组容量同步扩容（P0-1 根因修复）。
 *
 * 根因：m_selected 仅在首次按需分配时取当时 m_capacity，append/insert 使
 * m_points 倍增后位图未同步扩容，isPointSelected/setPointSelected 按新
 * m_count 索引旧容量位图 → 越界读/越界写（堆溢出）。本文件统一维护不变式：
 * m_selected 非 NULL ⇒ 分配长度 == m_capacity（对标 Qt 选中状态随数据
 * 增删同步平移/扩容的语义）。
 *
 * @param self        目标序列指针。
 * @param newCapacity 点数组倍增后的新容量。
 * @return 无返回值。调用点约定在 m_capacity 更新之前调用。
 */
static void xxy_syncSelectedCapacity(XXYSeries* self, int newCapacity)
{
    bool* s;
    int i;
    if (!self || !self->m_selected) return;   /* 未分配：按需分配时直接取 m_capacity。 */
    if (newCapacity <= self->m_capacity) return;
    s = (bool*)XRealloc_System(self->m_selected,
                               sizeof(bool) * (size_t)newCapacity);
    if (!s) {
        /* realloc 失败：丢弃位图退化为“全未选”（NULL 语义），杜绝半扩容
         * 状态（位图短于 m_capacity）下的后续越界访问。 */
        XFree_System(self->m_selected);
        self->m_selected = NULL;
        return;
    }
    /* 新增尾段 [旧容量, 新容量) 清零；旧区由 realloc 保留原选中状态。 */
    for (i = self->m_capacity; i < newCapacity; ++i) s[i] = false;
    self->m_selected = s;
}

void XXYSeries_append(XXYSeries* self, double x, double y)
{
    XPointF* p;
    if (!self) return;
    if (self->m_count >= self->m_capacity) {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 16;
        p = (XPointF*)XRealloc_System(self->m_points,
            sizeof(XPointF) * (size_t)cap);
        if (!p) return;
        self->m_points = p;
        /* P0-1：点数组倍增时选中位图同步倍增（须在 m_capacity 更新前调用）。 */
        xxy_syncSelectedCapacity(self, cap);
        self->m_capacity = cap;
    }
    self->m_points[self->m_count].x = x;
    self->m_points[self->m_count].y = y;
    self->m_count++;
    xxy_emitIndex(self, (size_t)XXYSeries_pointAdded_signal,
                  self->m_count - 1);
}

void XXYSeries_appendPoint(XXYSeries* self, const XPointF* point)
{
    if (!self || !point) return;
    XXYSeries_append(self, point->x, point->y);
}

void XXYSeries_appendPoints(XXYSeries* self, const XPointF* points, int count)
{
    int i;
    if (!self || !points || count <= 0) return;
    for (i = 0; i < count; ++i)
        XXYSeries_append(self, points[i].x, points[i].y);
}

bool XXYSeries_replace(XXYSeries* self, double oldX, double oldY,
                       double newX, double newY)
{
    int i;
    if (!self) return false;
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_points[i].x == oldX && self->m_points[i].y == oldY) {
            self->m_points[i].x = newX;
            self->m_points[i].y = newY;
            xxy_emitIndex(self, (size_t)XXYSeries_pointReplaced_signal, i);
            return true;
        }
    }
    return false;
}

bool XXYSeries_replaceAt(XXYSeries* self, int index, double newX, double newY)
{
    if (!self || index < 0 || index >= self->m_count) return false;
    self->m_points[index].x = newX;
    self->m_points[index].y = newY;
    xxy_emitIndex(self, (size_t)XXYSeries_pointReplaced_signal, index);
    return true;
}

bool XXYSeries_remove(XXYSeries* self, double x, double y)
{
    int i;
    if (!self) return false;
    for (i = 0; i < self->m_count; ++i) {
        if (self->m_points[i].x == x && self->m_points[i].y == y) {
            XXYSeries_removeAt(self, i);
            return true;
        }
    }
    return false;
}

bool XXYSeries_removeAt(XXYSeries* self, int index)
{
    int i;
    bool anyChanged = false;
    if (!self || index < 0 || index >= self->m_count) return false;
    for (i = index; i < self->m_count - 1; ++i)
        self->m_points[i] = self->m_points[i + 1];
    self->m_count--;
    if (self->m_selected) {
        /* 平移同步检查：写 [index, 新count)，读 [index+1, 旧count)，均落在
         * 位图容量（== m_capacity >= 旧count）之内，无越界。 */
        for (i = index; i < self->m_count; ++i)
            self->m_selected[i] = self->m_selected[i + 1];
        for (i = 0; i < self->m_count; ++i) {
            if (self->m_selected[i]) { anyChanged = true; break; }
        }
    }
    xxy_emitIndex(self, (size_t)XXYSeries_pointRemoved_signal, index);
    if (anyChanged)
        xxy_emitVoid(self, (size_t)XXYSeries_selectedPointsChanged_signal);
    return true;
}

void XXYSeries_removePoints(XXYSeries* self, int index, int count)
{
    int i;
    bool anyChanged = false;
    if (!self || index < 0 || count <= 0) return;
    if (index + count > self->m_count) count = self->m_count - index;
    if (count <= 0) return;
    for (i = index; i < self->m_count - count; ++i)
        self->m_points[i] = self->m_points[i + count];
    self->m_count -= count;
    if (self->m_selected) {
        /* 平移同步检查：写 [index, 新count)，读上界 (新count-1)+count == 旧count-1，
         * 均落在位图容量（== m_capacity >= 旧count）之内，无越界。 */
        for (i = index; i < self->m_count; ++i)
            self->m_selected[i] = self->m_selected[i + count];
        for (i = 0; i < self->m_count; ++i) {
            if (self->m_selected[i]) { anyChanged = true; break; }
        }
    }
    xxy_emitIndexCount(self, (size_t)XXYSeries_pointsRemoved_signal,
                       index, count);
    if (anyChanged)
        xxy_emitVoid(self, (size_t)XXYSeries_selectedPointsChanged_signal);
}

bool XXYSeries_insert(XXYSeries* self, int index, const XPointF* point)
{
    int i;
    bool anySelected;
    XPointF* p;
    if (!self || !point || index < 0 || index > self->m_count) return false;
    if (self->m_count >= self->m_capacity) {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 16;
        p = (XPointF*)XRealloc_System(self->m_points,
            sizeof(XPointF) * (size_t)cap);
        if (!p) return false;
        self->m_points = p;
        /* P0-1：点数组倍增时选中位图同步倍增（须在 m_capacity 更新前调用），
         * 保证下方重建循环对旧位图的读取不越界。 */
        xxy_syncSelectedCapacity(self, cap);
        self->m_capacity = cap;
    }
    for (i = self->m_count; i > index; --i)
        self->m_points[i] = self->m_points[i - 1];
    self->m_points[index] = *point;
    self->m_count++;
    xxy_emitIndex(self, (size_t)XXYSeries_pointAdded_signal, index);
    /* 插入后选中下标 >= index 者后移（对标 Qt，选中变化时发信号）。 */
    anySelected = false;
    if (self->m_selected) {
        bool* ns;
        int cap = self->m_capacity > 0 ? self->m_capacity : self->m_count;
        ns = (bool*)XMalloc_System(sizeof(bool) * (size_t)cap);
        if (ns) {
            XMemset(ns, 0, sizeof(bool) * (size_t)cap);
            for (i = 0; i < self->m_count - 1; ++i) {
                if (self->m_selected[i]) {
                    ns[(i >= index) ? i + 1 : i] = true;
                    anySelected = true;
                }
            }
            XFree_System(self->m_selected);
            self->m_selected = ns;
        }
    }
    if (anySelected)
        xxy_emitVoid(self, (size_t)XXYSeries_selectedPointsChanged_signal);
    return true;
}

void XXYSeries_clear(XXYSeries* self)
{
    int n;
    bool hadSelected;
    int i;
    if (!self) return;
    n = self->m_count;
    hadSelected = false;
    if (self->m_selected) {
        for (i = 0; i < n; ++i) {
            if (self->m_selected[i]) { hadSelected = true; break; }
        }
        XFree_System(self->m_selected);
        self->m_selected = NULL;
    }
    self->m_count = 0;
    if (self->m_pointColors) {
        XFree_System(self->m_pointColors);
        self->m_pointColors = NULL;
    }
    if (self->m_pointSizes) {
        XFree_System(self->m_pointSizes);
        self->m_pointSizes = NULL;
    }
    if (n > 0)
        xxy_emitIndexCount(self, (size_t)XXYSeries_pointsRemoved_signal, 0, n);
    if (hadSelected)
        xxy_emitVoid(self, (size_t)XXYSeries_selectedPointsChanged_signal);
}

int XXYSeries_count(const XXYSeries* self)
{ return self ? self->m_count : 0; }

const XPointF* XXYSeries_at(const XXYSeries* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return NULL;
    return &self->m_points[index];
}

/* ==================== 外观 ==================== */

void XXYSeries_setColor(XXYSeries* self, uint32_t color)
{
    if (!self || self->m_color == color) return;
    self->m_color = color;
    xxy_emitColor(self, (size_t)XXYSeries_colorChanged_signal, color);
    xxy_emitPen(self, (size_t)XXYSeries_penChanged_signal, color,
                self->m_width);
}

uint32_t XXYSeries_color(const XXYSeries* self)
{ return self ? self->m_color : 0; }

void XXYSeries_setWidth(XXYSeries* self, double width)
{ if (self && width > 0) self->m_width = width; }

double XXYSeries_width(const XXYSeries* self)
{ return self ? self->m_width : 0; }

void XXYSeries_setMarkerSize(XXYSeries* self, double size)
{
    if (!self || size <= 0 || self->m_markerSize == size) return;
    self->m_markerSize = size;
    xxy_emitDouble(self, (size_t)XXYSeries_markerSizeChanged_signal, size);
}

double XXYSeries_markerSize(const XXYSeries* self)
{ return self ? self->m_markerSize : 0; }

void XXYSeries_setPointsVisible(XXYSeries* self, bool visible)
{ if (self) self->m_pointsVisible = visible; }

bool XXYSeries_pointsVisible(const XXYSeries* self)
{ return self ? self->m_pointsVisible : false; }

void XXYSeries_setPointLabelsFormat(XXYSeries* self, const XString* format)
{
    const char* text;
    if (!self) return;
    if (!self->m_pointLabelsFormat)
        self->m_pointLabelsFormat = XString_create();
    if (!self->m_pointLabelsFormat) return;
    if (format) {
        if (XString_equals(self->m_pointLabelsFormat, format,
                           XChar_CaseSensitive))
            return;
        XString_assign(self->m_pointLabelsFormat, format);
    } else {
        if (XString_equals_utf8(self->m_pointLabelsFormat,
                                "@xPoint, @yPoint", XChar_CaseSensitive))
            return;
        XString_assign_utf8(self->m_pointLabelsFormat, "@xPoint, @yPoint");
    }
    text = XString_toUtf8(self->m_pointLabelsFormat);
    xxy_emitStr(self, (size_t)XXYSeries_pointLabelsFormatChanged_signal,
                text ? text : "");
}
void XXYSeries_setPointLabelsFormat_2(XXYSeries* self, const char* format)
{
    XString* tmp = NULL;
    if (format) {
        tmp = XString_create_utf8(format);
        if (!tmp) return;
    }
    XXYSeries_setPointLabelsFormat(self, tmp);
    if (tmp) XString_delete_base(tmp);
}

const XString* XXYSeries_pointLabelsFormat(const XXYSeries* self)
{
    return (self && self->m_pointLabelsFormat)
               ? self->m_pointLabelsFormat : NULL;
}
const char* XXYSeries_pointLabelsFormat_2(const XXYSeries* self)
{
    const XString* s;
    const char* text;
    s = XXYSeries_pointLabelsFormat(self);
    if (!s) return "@xPoint, @yPoint";
    text = XString_toUtf8(s);
    return text ? text : "";
}

void XXYSeries_setPointLabelsVisible(XXYSeries* self, bool visible)
{
    if (!self || self->m_pointLabelsVisible == visible) return;
    self->m_pointLabelsVisible = visible;
    xxy_emitBool(self, (size_t)XXYSeries_pointLabelsVisibilityChanged_signal,
                 visible);
}

bool XXYSeries_pointLabelsVisible(const XXYSeries* self)
{ return self ? self->m_pointLabelsVisible : false; }

void XXYSeries_setPointLabelsColor(XXYSeries* self, uint32_t color)
{
    if (!self || self->m_pointLabelsColor == color) return;
    self->m_pointLabelsColor = color;
    xxy_emitColor(self, (size_t)XXYSeries_pointLabelsColorChanged_signal,
                  color);
}

uint32_t XXYSeries_pointLabelsColor(const XXYSeries* self)
{ return self ? self->m_pointLabelsColor : 0; }

/* ==================== 点选择 ==================== */

bool XXYSeries_isPointSelected(const XXYSeries* self, int index)
{
    /* 防御性边界检查：index 须同时落在点数与容量内；容量异常（位图短于
     * 下标）时按未选处理，拒绝索引以免越界读。 */
    if (!self || !self->m_selected || index < 0 ||
        index >= self->m_count || index >= self->m_capacity)
        return false;
    return self->m_selected[index];
}

void XXYSeries_setPointSelected(XXYSeries* self, int index, bool selected)
{
    int cap;
    bool* s;
    if (!self || index < 0 || index >= self->m_count) return;
    if (!self->m_selected) {
        cap = self->m_capacity > 0 ? self->m_capacity : self->m_count;
        s = (bool*)XMalloc_System(sizeof(bool) * (size_t)cap);
        if (!s) return;
        XMemset(s, 0, sizeof(bool) * (size_t)cap);
        self->m_selected = s;
    }
    /* 防御性边界检查：不变式保证位图分配长度 == m_capacity，index 超容量
     * 即状态异常，拒绝写入以免堆溢出（正常路径不可达）。 */
    if (index >= self->m_capacity) return;
    if (self->m_selected[index] == selected) return;
    self->m_selected[index] = selected;
    xxy_emitVoid(self, (size_t)XXYSeries_selectedPointsChanged_signal);
}

void XXYSeries_selectPoint(XXYSeries* self, int index)
{ XXYSeries_setPointSelected(self, index, true); }

void XXYSeries_deselectPoint(XXYSeries* self, int index)
{ XXYSeries_setPointSelected(self, index, false); }

void XXYSeries_selectAllPoints(XXYSeries* self)
{
    int i;
    bool changed = false;
    if (!self) return;
    for (i = 0; i < self->m_count; ++i) {
        int cap;
        bool* s;
        /* 防御性边界检查：下标超容量视为状态异常，跳过以免越界写。 */
        if (i >= self->m_capacity) continue;
        if (!self->m_selected) {
            cap = self->m_capacity > 0 ? self->m_capacity : self->m_count;
            s = (bool*)XMalloc_System(sizeof(bool) * (size_t)cap);
            if (!s) return;
            XMemset(s, 0, sizeof(bool) * (size_t)cap);
            self->m_selected = s;
        }
        if (!self->m_selected[i]) {
            self->m_selected[i] = true;
            changed = true;
        }
    }
    if (changed)
        xxy_emitVoid(self, (size_t)XXYSeries_selectedPointsChanged_signal);
}

void XXYSeries_deselectAllPoints(XXYSeries* self)
{
    int i;
    bool hadSelected = false;
    if (!self) return;
    if (self->m_selected) {
        for (i = 0; i < self->m_count; ++i) {
            if (self->m_selected[i]) { hadSelected = true; break; }
        }
        XFree_System(self->m_selected);
        self->m_selected = NULL;
    }
    if (self->m_pointColors) {
        XFree_System(self->m_pointColors);
        self->m_pointColors = NULL;
    }
    if (self->m_pointSizes) {
        XFree_System(self->m_pointSizes);
        self->m_pointSizes = NULL;
    }
    if (hadSelected)
        xxy_emitVoid(self, (size_t)XXYSeries_selectedPointsChanged_signal);
}

/* ==================== 信号标识 ==================== */

void* XXYSeries_clicked_signal(XXYSeries* self, double x, double y)
{ (void)self; (void)x; (void)y;
  return (void*)(size_t)XXYSeries_clicked_signal; }
void* XXYSeries_hovered_signal(XXYSeries* self, double x, double y, bool state)
{ (void)self; (void)x; (void)y; (void)state;
  return (void*)(size_t)XXYSeries_hovered_signal; }
void* XXYSeries_pressed_signal(XXYSeries* self, double x, double y)
{ (void)self; (void)x; (void)y;
  return (void*)(size_t)XXYSeries_pressed_signal; }
void* XXYSeries_released_signal(XXYSeries* self, double x, double y)
{ (void)self; (void)x; (void)y;
  return (void*)(size_t)XXYSeries_released_signal; }
void* XXYSeries_doubleClicked_signal(XXYSeries* self, double x, double y)
{ (void)self; (void)x; (void)y;
  return (void*)(size_t)XXYSeries_doubleClicked_signal; }
void* XXYSeries_pointReplaced_signal(XXYSeries* self, int index)
{ (void)self; (void)index;
  return (void*)(size_t)XXYSeries_pointReplaced_signal; }
void* XXYSeries_pointRemoved_signal(XXYSeries* self, int index)
{ (void)self; (void)index;
  return (void*)(size_t)XXYSeries_pointRemoved_signal; }
void* XXYSeries_pointAdded_signal(XXYSeries* self, int index)
{ (void)self; (void)index;
  return (void*)(size_t)XXYSeries_pointAdded_signal; }
void* XXYSeries_pointsReplaced_signal(XXYSeries* self)
{ (void)self; return (void*)(size_t)XXYSeries_pointsReplaced_signal; }
void* XXYSeries_colorChanged_signal(XXYSeries* self, uint32_t color)
{ (void)self; (void)color;
  return (void*)(size_t)XXYSeries_colorChanged_signal; }
void* XXYSeries_selectedColorChanged_signal(XXYSeries* self, uint32_t color)
{ (void)self; (void)color;
  return (void*)(size_t)XXYSeries_selectedColorChanged_signal; }
void* XXYSeries_pointsRemoved_signal(XXYSeries* self, int index, int count)
{ (void)self; (void)index; (void)count;
  return (void*)(size_t)XXYSeries_pointsRemoved_signal; }
void* XXYSeries_penChanged_signal(XXYSeries* self, uint32_t color,
                                  double width)
{ (void)self; (void)color; (void)width;
  return (void*)(size_t)XXYSeries_penChanged_signal; }
void* XXYSeries_selectedPointsChanged_signal(XXYSeries* self)
{ (void)self; return (void*)(size_t)XXYSeries_selectedPointsChanged_signal; }
void* XXYSeries_pointLabelsFormatChanged_signal(XXYSeries* self,
                                                const char* format)
{ (void)self; (void)format;
  return (void*)(size_t)XXYSeries_pointLabelsFormatChanged_signal; }
void* XXYSeries_pointLabelsVisibilityChanged_signal(XXYSeries* self,
                                                    bool visible)
{ (void)self; (void)visible;
  return (void*)(size_t)XXYSeries_pointLabelsVisibilityChanged_signal; }
void* XXYSeries_pointLabelsFontChanged_signal(XXYSeries* self,
                                              const char* family,
                                              int pointSize)
{ (void)self; (void)family; (void)pointSize;
  return (void*)(size_t)XXYSeries_pointLabelsFontChanged_signal; }
void* XXYSeries_pointLabelsColorChanged_signal(XXYSeries* self,
                                               uint32_t color)
{ (void)self; (void)color;
  return (void*)(size_t)XXYSeries_pointLabelsColorChanged_signal; }
void* XXYSeries_pointLabelsClippingChanged_signal(XXYSeries* self,
                                                  bool clipping)
{ (void)self; (void)clipping;
  return (void*)(size_t)XXYSeries_pointLabelsClippingChanged_signal; }
void* XXYSeries_lightMarkerChanged_signal(XXYSeries* self,
                                          const XPixmap* marker)
{ (void)self; (void)marker;
  return (void*)(size_t)XXYSeries_lightMarkerChanged_signal; }
void* XXYSeries_selectedLightMarkerChanged_signal(XXYSeries* self,
                                                  const XPixmap* marker)
{ (void)self; (void)marker;
  return (void*)(size_t)XXYSeries_selectedLightMarkerChanged_signal; }
void* XXYSeries_markerSizeChanged_signal(XXYSeries* self, double size)
{ (void)self; (void)size;
  return (void*)(size_t)XXYSeries_markerSizeChanged_signal; }
void* XXYSeries_bestFitLineVisibilityChanged_signal(XXYSeries* self,
                                                    bool visible)
{ (void)self; (void)visible;
  return (void*)(size_t)XXYSeries_bestFitLineVisibilityChanged_signal; }
void* XXYSeries_bestFitLinePenChanged_signal(XXYSeries* self,
                                             uint32_t color, double width)
{ (void)self; (void)color; (void)width;
  return (void*)(size_t)XXYSeries_bestFitLinePenChanged_signal; }
void* XXYSeries_bestFitLineColorChanged_signal(XXYSeries* self,
                                               uint32_t color)
{ (void)self; (void)color;
  return (void*)(size_t)XXYSeries_bestFitLineColorChanged_signal; }
void* XXYSeries_pointsConfigurationChanged_signal(XXYSeries* self)
{ (void)self; return (void*)(size_t)XXYSeries_pointsConfigurationChanged_signal; }

/* ==================== 画笔/画刷/选中色/点标签字体（参数化） ==================== */

void XXYSeries_setPen(XXYSeries* self, uint32_t color, double width)
{
    bool colorChanged;
    bool penChanged;
    if (!self) return;
    colorChanged = self->m_color != color;
    penChanged = colorChanged ||
                 (width > 0 && self->m_width != width);
    self->m_color = color;
    if (width > 0) self->m_width = width;
    if (!penChanged) return;
    if (colorChanged)
        xxy_emitColor(self, (size_t)XXYSeries_colorChanged_signal, color);
    xxy_emitPen(self, (size_t)XXYSeries_penChanged_signal,
                self->m_color, self->m_width);
}

void XXYSeries_pen(const XXYSeries* self, uint32_t* color, double* width)
{
    if (!self) return;
    if (color) *color = self->m_color;
    if (width) *width = self->m_width;
}

void XXYSeries_setBrush(XXYSeries* self, uint32_t color)
{ if (self) self->m_brush = color; }

uint32_t XXYSeries_brush(const XXYSeries* self)
{ return self ? self->m_brush : 0; }

void XXYSeries_setSelectedColor(XXYSeries* self, uint32_t color)
{
    if (!self || self->m_selectedColor == color) return;
    self->m_selectedColor = color;
    xxy_emitColor(self, (size_t)XXYSeries_selectedColorChanged_signal, color);
}

uint32_t XXYSeries_selectedColor(const XXYSeries* self)
{ return self ? self->m_selectedColor : 0; }

void XXYSeries_setPointLabelsClipping(XXYSeries* self, bool clip)
{
    if (!self || self->m_pointLabelsClipping == clip) return;
    self->m_pointLabelsClipping = clip;
    xxy_emitBool(self, (size_t)XXYSeries_pointLabelsClippingChanged_signal,
                 clip);
}

bool XXYSeries_pointLabelsClipping(const XXYSeries* self)
{ return self ? self->m_pointLabelsClipping : true; }

void XXYSeries_setPointLabelsFont(XXYSeries* self, const XString* family,
                                  int pointSize)
{
    bool changed = false;
    if (!self) return;
    if (family) {
        if (!self->m_pointLabelsFontFamily)
            self->m_pointLabelsFontFamily = XString_create();
        if (!self->m_pointLabelsFontFamily) return;
        if (!XString_equals(self->m_pointLabelsFontFamily, family,
                            XChar_CaseSensitive)) {
            XString_assign(self->m_pointLabelsFontFamily, family);
            changed = true;
        }
    }
    if (pointSize > 0 && self->m_pointLabelsFontSize != pointSize) {
        self->m_pointLabelsFontSize = pointSize;
        changed = true;
    }
    if (changed)
        xxy_emitFont(self, (size_t)XXYSeries_pointLabelsFontChanged_signal,
                     XXYSeries_pointLabelsFontFamily_2(self),
                     self->m_pointLabelsFontSize);
}
void XXYSeries_setPointLabelsFont_2(XXYSeries* self, const char* family,
                                    int pointSize)
{
    XString* tmp = NULL;
    if (family) {
        tmp = XString_create_utf8(family);
        if (!tmp) return;
    }
    XXYSeries_setPointLabelsFont(self, tmp, pointSize);
    if (tmp) XString_delete_base(tmp);
}

const XString* XXYSeries_pointLabelsFontFamily(const XXYSeries* self)
{
    return (self && self->m_pointLabelsFontFamily)
               ? self->m_pointLabelsFontFamily : NULL;
}
const char* XXYSeries_pointLabelsFontFamily_2(const XXYSeries* self)
{
    const XString* s;
    const char* text;
    s = XXYSeries_pointLabelsFontFamily(self);
    if (!s) return "";
    text = XString_toUtf8(s);
    return text ? text : "";
}

int XXYSeries_pointLabelsFontSize(const XXYSeries* self)
{ return self ? self->m_pointLabelsFontSize : 0; }

int XXYSeries_points(const XXYSeries* self, XPointF* out, int maxCount)
{
    int n;
    if (!self || !out || maxCount <= 0) return 0;
    n = self->m_count < maxCount ? self->m_count : maxCount;
    if (self->m_points)
        XMemcpy(out, self->m_points, sizeof(XPointF) * (size_t)n);
    return n;
}

int XXYSeries_pointsVector(const XXYSeries* self, XPointF* out, int maxCount)
{ return XXYSeries_points(self, out, maxCount); }

/* ==================== 最佳拟合线 ==================== */

void XXYSeries_setBestFitLineVisible(XXYSeries* self, bool visible)
{
    if (!self || self->m_bestFitVisible == visible) return;
    self->m_bestFitVisible = visible;
    xxy_emitBool(self, (size_t)XXYSeries_bestFitLineVisibilityChanged_signal,
                 visible);
}

bool XXYSeries_bestFitLineVisible(const XXYSeries* self)
{ return self ? self->m_bestFitVisible : false; }

bool XXYSeries_bestFitLineEquation(const XXYSeries* self, double* slope,
                                   double* intercept)
{
    double sx;
    double sy;
    double sxx;
    double sxy;
    double denom;
    int i;
    if (!self || !slope || !intercept || self->m_count < 2) return false;
    sx = 0.0; sy = 0.0; sxx = 0.0; sxy = 0.0;
    for (i = 0; i < self->m_count; ++i) {
        double x = self->m_points[i].x;
        double y = self->m_points[i].y;
        sx += x; sy += y;
        sxx += x * x; sxy += x * y;
    }
    denom = (double)self->m_count * sxx - sx * sx;
    if (denom == 0.0) return false;
    *slope = ((double)self->m_count * sxy - sx * sy) / denom;
    *intercept = (sy - *slope * sx) / (double)self->m_count;
    return true;
}

void XXYSeries_setBestFitLineColor(XXYSeries* self, uint32_t color)
{
    if (!self || self->m_bestFitColor == color) return;
    self->m_bestFitColor = color;
    xxy_emitColor(self, (size_t)XXYSeries_bestFitLineColorChanged_signal,
                  color);
    xxy_emitPen(self, (size_t)XXYSeries_bestFitLinePenChanged_signal,
                color, self->m_bestFitWidth);
}

uint32_t XXYSeries_bestFitLineColor(const XXYSeries* self)
{ return self ? self->m_bestFitColor : 0; }

void XXYSeries_setBestFitLineWidth(XXYSeries* self, double width)
{ if (self && width > 0) self->m_bestFitWidth = width; }

/* ==================== 批量选择 ==================== */

void XXYSeries_selectPoints(XXYSeries* self, const int* indexes, int count)
{
    int i;
    bool changed = false;
    if (!self || !indexes) return;
    for (i = 0; i < count; ++i) {
        int idx = indexes[i];
        int cap;
        bool* s;
        if (idx < 0 || idx >= self->m_count) continue;
        /* 防御性边界检查：下标超容量视为状态异常，跳过以免越界写。 */
        if (idx >= self->m_capacity) continue;
        if (!self->m_selected) {
            cap = self->m_capacity > 0 ? self->m_capacity : self->m_count;
            s = (bool*)XMalloc_System(sizeof(bool) * (size_t)cap);
            if (!s) return;
            XMemset(s, 0, sizeof(bool) * (size_t)cap);
            self->m_selected = s;
        }
        if (!self->m_selected[idx]) {
            self->m_selected[idx] = true;
            changed = true;
        }
    }
    if (changed)
        xxy_emitVoid(self, (size_t)XXYSeries_selectedPointsChanged_signal);
}

void XXYSeries_deselectPoints(XXYSeries* self, const int* indexes, int count)
{
    int i;
    bool changed = false;
    if (!self || !indexes || !self->m_selected) return;
    for (i = 0; i < count; ++i) {
        int idx = indexes[i];
        if (idx < 0 || idx >= self->m_count) continue;
        if (self->m_selected[idx]) {
            self->m_selected[idx] = false;
            changed = true;
        }
    }
    if (changed)
        xxy_emitVoid(self, (size_t)XXYSeries_selectedPointsChanged_signal);
}

void XXYSeries_toggleSelection(XXYSeries* self, const int* indexes, int count)
{
    int i;
    bool changed = false;
    if (!self || !indexes) return;
    for (i = 0; i < count; ++i) {
        int idx = indexes[i];
        int cap;
        bool* s;
        if (idx < 0 || idx >= self->m_count) continue;
        /* 防御性边界检查：下标超容量视为状态异常，跳过以免越界写。 */
        if (idx >= self->m_capacity) continue;
        if (!self->m_selected) {
            cap = self->m_capacity > 0 ? self->m_capacity : self->m_count;
            s = (bool*)XMalloc_System(sizeof(bool) * (size_t)cap);
            if (!s) return;
            XMemset(s, 0, sizeof(bool) * (size_t)cap);
            self->m_selected = s;
        }
        self->m_selected[idx] = !self->m_selected[idx];
        changed = true;
    }
    if (changed)
        xxy_emitVoid(self, (size_t)XXYSeries_selectedPointsChanged_signal);
}

int XXYSeries_selectedPoints(const XXYSeries* self, int* out, int maxCount)
{
    int i;
    int n = 0;
    if (!self || !out || maxCount <= 0) return 0;
    for (i = 0; i < self->m_count && n < maxCount; ++i)
        if (XXYSeries_isPointSelected(self, i)) out[n++] = i;
    return n;
}

void XXYSeries_setBestFitLinePen(XXYSeries* self, uint32_t color,
                                 double width)
{
    bool colorChanged;
    bool penChanged;
    if (!self) return;
    colorChanged = self->m_bestFitColor != color;
    penChanged = colorChanged ||
                 (width > 0 && self->m_bestFitWidth != width);
    self->m_bestFitColor = color;
    if (width > 0) self->m_bestFitWidth = width;
    if (!penChanged) return;
    if (colorChanged)
        xxy_emitColor(self, (size_t)XXYSeries_bestFitLineColorChanged_signal,
                      color);
    xxy_emitPen(self, (size_t)XXYSeries_bestFitLinePenChanged_signal,
                self->m_bestFitColor, self->m_bestFitWidth);
}

void XXYSeries_bestFitLinePen(const XXYSeries* self, uint32_t* color,
                              double* width)
{
    if (!self) return;
    if (color) *color = self->m_bestFitColor;
    if (width) *width = self->m_bestFitWidth;
}

const XString* XXYSeries_pointLabelsFont(const XXYSeries* self)
{ return XXYSeries_pointLabelsFontFamily(self); }
const char* XXYSeries_pointLabelsFont_2(const XXYSeries* self)
{ return XXYSeries_pointLabelsFontFamily_2(self); }

void XXYSeries_setLightMarker(XXYSeries* self, const XPixmap* marker)
{
    if (!self || self->m_lightMarker == marker) return;
    self->m_lightMarker = marker;
    xxy_emitPtr(self, (size_t)XXYSeries_lightMarkerChanged_signal, marker);
}

const XPixmap* XXYSeries_lightMarker(const XXYSeries* self)
{ return self ? self->m_lightMarker : NULL; }

void XXYSeries_setSelectedLightMarker(XXYSeries* self, const XPixmap* marker)
{
    if (!self || self->m_selectedLightMarker == marker) return;
    self->m_selectedLightMarker = marker;
    xxy_emitPtr(self, (size_t)XXYSeries_selectedLightMarkerChanged_signal,
                marker);
}

const XPixmap* XXYSeries_selectedLightMarker(const XXYSeries* self)
{ return self ? self->m_selectedLightMarker : NULL; }

void XXYSeries_setPointConfiguration(XXYSeries* self, int index,
                                     uint32_t color, double size)
{
    uint32_t* pc;
    double* ps;
    bool changed = false;
    if (!self || index < 0) return;
    if (index >= self->m_pointConfigCapacity) {
        int cap = self->m_pointConfigCapacity > 0
            ? self->m_pointConfigCapacity : 8;
        int oldCap = self->m_pointConfigCapacity;
        int i;
        while (cap <= index) cap *= 2;
        /* 根因（R-109）：双数组先后 realloc，此前第二块失败即 return——
         * 第一块已被 realloc 搬迁而 self 指针未回写（悬垂，后续读旧址），
         * 新块又无人引用（泄漏）。改为容量记账：谁成功就地收编谁
         * （realloc 保留旧数据，块长度 ≥ 容量），失败路径容量保持旧值，
         * 「块长度 ≥ m_pointConfigCapacity」不变式恒成立；两块齐备才
         * 推进容量。 */
        pc = (uint32_t*)XRealloc_System(self->m_pointColors,
            sizeof(uint32_t) * (size_t)cap);
        if (pc) {
            /* 新增区清零：0 是「未配置」哨兵，杜绝未初始化值被
             * pointColor/pointSize 读成有效配置（XRealloc 不清零）。 */
            for (i = oldCap; i < cap; ++i) pc[i] = 0;
            self->m_pointColors = pc;
        }
        ps = (double*)XRealloc_System(self->m_pointSizes,
            sizeof(double) * (size_t)cap);
        if (ps) {
            for (i = oldCap; i < cap; ++i) ps[i] = 0.0;
            self->m_pointSizes = ps;
        }
        if (!pc || !ps) return;
        self->m_pointConfigCapacity = cap;
    }
    if (color != 0 && self->m_pointColors) {
        if (self->m_pointColors[index] != color) {
            self->m_pointColors[index] = color;
            changed = true;
        }
    }
    if (size != 0 && self->m_pointSizes) {
        if (self->m_pointSizes[index] != size) {
            self->m_pointSizes[index] = size;
            changed = true;
        }
    }
    if (changed)
        xxy_emitVoid(self,
                     (size_t)XXYSeries_pointsConfigurationChanged_signal);
}

uint32_t XXYSeries_pointColor(const XXYSeries* self, int index)
{
    if (!self || !self->m_pointColors || index < 0 ||
        index >= self->m_pointConfigCapacity)
        return 0;
    return self->m_pointColors[index];
}

double XXYSeries_pointSize(const XXYSeries* self, int index)
{
    if (!self || !self->m_pointSizes || index < 0 ||
        index >= self->m_pointConfigCapacity)
        return 0;
    return self->m_pointSizes[index];
}

void XXYSeries_clearPointConfiguration(XXYSeries* self, int index)
{
    bool changed = false;
    if (!self) return;
    if (index < 0) {
        if (self->m_pointColors || self->m_pointSizes) changed = true;
        if (self->m_pointColors) {
            XFree_System(self->m_pointColors);
            self->m_pointColors = NULL;
        }
        if (self->m_pointSizes) {
            XFree_System(self->m_pointSizes);
            self->m_pointSizes = NULL;
        }
        self->m_pointConfigCapacity = 0;
    } else if (index < self->m_pointConfigCapacity) {
        if (self->m_pointColors && self->m_pointColors[index] != 0) {
            self->m_pointColors[index] = 0;
            changed = true;
        }
        if (self->m_pointSizes && self->m_pointSizes[index] != 0) {
            self->m_pointSizes[index] = 0;
            changed = true;
        }
    }
    if (changed)
        xxy_emitVoid(self,
                     (size_t)XXYSeries_pointsConfigurationChanged_signal);
}

void XXYSeries_sizeBy(XXYSeries* self, const double* sourceData, int count,
                      double minSize, double maxSize)
{
    double dmin;
    double dmax;
    int i;
    if (!self || !sourceData || count <= 0) return;
    dmin = sourceData[0];
    dmax = sourceData[0];
    for (i = 1; i < count; ++i) {
        if (sourceData[i] < dmin) dmin = sourceData[i];
        if (sourceData[i] > dmax) dmax = sourceData[i];
    }
    for (i = 0; i < count && i < self->m_count; ++i) {
        double t = (dmax > dmin) ? (sourceData[i] - dmin) / (dmax - dmin)
                                 : 0.5;
        XXYSeries_setPointConfiguration(self, i, 0,
            minSize + t * (maxSize - minSize));
    }
}

void XXYSeries_colorBy(XXYSeries* self, const double* sourceData, int count,
                       uint32_t colorStart, uint32_t colorEnd)
{
    double dmin;
    double dmax;
    int i;
    if (!self || !sourceData || count <= 0) return;
    dmin = sourceData[0];
    dmax = sourceData[0];
    for (i = 1; i < count; ++i) {
        if (sourceData[i] < dmin) dmin = sourceData[i];
        if (sourceData[i] > dmax) dmax = sourceData[i];
    }
    for (i = 0; i < count && i < self->m_count; ++i) {
        double t = (dmax > dmin) ? (sourceData[i] - dmin) / (dmax - dmin)
                                 : 0.5;
        uint32_t r = (uint32_t)(((colorStart >> 16) & 0xFF) * (1.0 - t) +
                                ((colorEnd >> 16) & 0xFF) * t);
        uint32_t g = (uint32_t)(((colorStart >> 8) & 0xFF) * (1.0 - t) +
                                ((colorEnd >> 8) & 0xFF) * t);
        uint32_t b = (uint32_t)((colorStart & 0xFF) * (1.0 - t) +
                                (colorEnd & 0xFF) * t);
        uint32_t a = (uint32_t)(((colorStart >> 24) & 0xFF) * (1.0 - t) +
                                ((colorEnd >> 24) & 0xFF) * t);
        XXYSeries_setPointConfiguration(self, i,
            (a << 24) | (r << 16) | (g << 8) | b, 0);
    }
}

void XXYSeries_pointConfiguration(const XXYSeries* self, int index,
                                  uint32_t* color, double* size)
{
    if (!self) return;
    if (color) *color = XXYSeries_pointColor(self, index);
    if (size) *size = XXYSeries_pointSize(self, index);
}

void XXYSeries_setPointsConfiguration(XXYSeries* self,
                                      const uint32_t* colors,
                                      const double* sizes, int count)
{
    int i;
    if (!self) return;
    for (i = 0; i < count; ++i)
        XXYSeries_setPointConfiguration(self, i,
            colors ? colors[i] : 0, sizes ? sizes[i] : 0);
}

int XXYSeries_pointsConfiguration(const XXYSeries* self, uint32_t* colors,
                                  double* sizes, int maxCount)
{
    int n;
    int i;
    if (!self || maxCount <= 0) return 0;
    n = self->m_pointConfigCapacity < maxCount
        ? self->m_pointConfigCapacity : maxCount;
    for (i = 0; i < n; ++i) {
        if (colors) colors[i] = XXYSeries_pointColor(self, i);
        if (sizes) sizes[i] = XXYSeries_pointSize(self, i);
    }
    return n;
}

void XXYSeries_clearPointsConfiguration(XXYSeries* self)
{ XXYSeries_clearPointConfiguration(self, -1); }

#endif /* XCHARTS_ON */
