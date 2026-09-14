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
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot)
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
    if (!self || index < 0 || index >= self->m_count) return false;
    for (i = index; i < self->m_count - 1; ++i)
        self->m_points[i] = self->m_points[i + 1];
    self->m_count--;
    if (self->m_selected) {
        for (i = index; i < self->m_count; ++i)
            self->m_selected[i] = self->m_selected[i + 1];
    }
    xxy_emitIndex(self, (size_t)XXYSeries_pointRemoved_signal, index);
    return true;
}

void XXYSeries_removePoints(XXYSeries* self, int index, int count)
{
    int i;
    if (!self || index < 0 || count <= 0) return;
    if (index + count > self->m_count) count = self->m_count - index;
    for (i = index; i < self->m_count - count; ++i)
        self->m_points[i] = self->m_points[i + count];
    self->m_count -= count;
    if (self->m_selected) {
        for (i = index; i < self->m_count; ++i)
            self->m_selected[i] = self->m_selected[i + count];
    }
}

bool XXYSeries_insert(XXYSeries* self, int index, const XPointF* point)
{
    int i;
    XPointF* p;
    if (!self || !point || index < 0 || index > self->m_count) return false;
    if (self->m_count >= self->m_capacity) {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 16;
        p = (XPointF*)XRealloc_System(self->m_points,
            sizeof(XPointF) * (size_t)cap);
        if (!p) return false;
        self->m_points = p;
        self->m_capacity = cap;
    }
    for (i = self->m_count; i > index; --i)
        self->m_points[i] = self->m_points[i - 1];
    self->m_points[index] = *point;
    self->m_count++;
    xxy_emitIndex(self, (size_t)XXYSeries_pointAdded_signal, index);
    return true;
}

void XXYSeries_clear(XXYSeries* self)
{
    if (!self) return;
    self->m_count = 0;
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
    xxy_emitIndex(self, (size_t)XXYSeries_colorChanged_signal, (int)color);
}

uint32_t XXYSeries_color(const XXYSeries* self)
{ return self ? self->m_color : 0; }

void XXYSeries_setWidth(XXYSeries* self, double width)
{ if (self && width > 0) self->m_width = width; }

double XXYSeries_width(const XXYSeries* self)
{ return self ? self->m_width : 0; }

void XXYSeries_setMarkerSize(XXYSeries* self, double size)
{ if (self && size > 0) self->m_markerSize = size; }

double XXYSeries_markerSize(const XXYSeries* self)
{ return self ? self->m_markerSize : 0; }

void XXYSeries_setPointsVisible(XXYSeries* self, bool visible)
{ if (self) self->m_pointsVisible = visible; }

bool XXYSeries_pointsVisible(const XXYSeries* self)
{ return self ? self->m_pointsVisible : false; }

void XXYSeries_setPointLabelsFormat(XXYSeries* self, const char* format)
{
    if (!self) return;
    if (!self->m_pointLabelsFormat)
        self->m_pointLabelsFormat = XString_create();
    if (self->m_pointLabelsFormat)
        XString_assign_utf8(self->m_pointLabelsFormat,
                            format ? format : "@xPoint, @yPoint");
}

const char* XXYSeries_pointLabelsFormat(const XXYSeries* self)
{
    const char* text;
    if (!self || !self->m_pointLabelsFormat) return "@xPoint, @yPoint";
    text = XString_toUtf8(self->m_pointLabelsFormat);
    return text ? text : "@xPoint, @yPoint";
}

void XXYSeries_setPointLabelsVisible(XXYSeries* self, bool visible)
{ if (self) self->m_pointLabelsVisible = visible; }

bool XXYSeries_pointLabelsVisible(const XXYSeries* self)
{ return self ? self->m_pointLabelsVisible : false; }

void XXYSeries_setPointLabelsColor(XXYSeries* self, uint32_t color)
{ if (self) self->m_pointLabelsColor = color; }

uint32_t XXYSeries_pointLabelsColor(const XXYSeries* self)
{ return self ? self->m_pointLabelsColor : 0; }

/* ==================== 点选择 ==================== */

bool XXYSeries_isPointSelected(const XXYSeries* self, int index)
{
    if (!self || !self->m_selected || index < 0 || index >= self->m_count)
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
    self->m_selected[index] = selected;
}

void XXYSeries_selectPoint(XXYSeries* self, int index)
{ XXYSeries_setPointSelected(self, index, true); }

void XXYSeries_deselectPoint(XXYSeries* self, int index)
{ XXYSeries_setPointSelected(self, index, false); }

void XXYSeries_selectAllPoints(XXYSeries* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_count; ++i)
        XXYSeries_setPointSelected(self, i, true);
}

void XXYSeries_deselectAllPoints(XXYSeries* self)
{
    if (!self) return;
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

/* ==================== 画笔/画刷/选中色/点标签字体（参数化） ==================== */

void XXYSeries_setPen(XXYSeries* self, uint32_t color, double width)
{
    if (!self) return;
    self->m_color = color;
    if (width > 0) self->m_width = width;
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
{ if (self) self->m_selectedColor = color; }

uint32_t XXYSeries_selectedColor(const XXYSeries* self)
{ return self ? self->m_selectedColor : 0; }

void XXYSeries_setPointLabelsClipping(XXYSeries* self, bool clip)
{ if (self) self->m_pointLabelsClipping = clip; }

bool XXYSeries_pointLabelsClipping(const XXYSeries* self)
{ return self ? self->m_pointLabelsClipping : true; }

void XXYSeries_setPointLabelsFont(XXYSeries* self, const char* family,
                                  int pointSize)
{
    if (!self) return;
    if (family) {
        if (!self->m_pointLabelsFontFamily)
            self->m_pointLabelsFontFamily = XString_create();
        if (self->m_pointLabelsFontFamily)
            XString_assign_utf8(self->m_pointLabelsFontFamily, family);
    }
    if (pointSize > 0) self->m_pointLabelsFontSize = pointSize;
}

const char* XXYSeries_pointLabelsFontFamily(const XXYSeries* self)
{
    const char* text;
    if (!self || !self->m_pointLabelsFontFamily) return "";
    text = XString_toUtf8(self->m_pointLabelsFontFamily);
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
{ if (self) self->m_bestFitVisible = visible; }

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
{ if (self) self->m_bestFitColor = color; }

uint32_t XXYSeries_bestFitLineColor(const XXYSeries* self)
{ return self ? self->m_bestFitColor : 0; }

void XXYSeries_setBestFitLineWidth(XXYSeries* self, double width)
{ if (self && width > 0) self->m_bestFitWidth = width; }

/* ==================== 批量选择 ==================== */

void XXYSeries_selectPoints(XXYSeries* self, const int* indexes, int count)
{
    int i;
    if (!self || !indexes) return;
    for (i = 0; i < count; ++i)
        XXYSeries_selectPoint(self, indexes[i]);
}

void XXYSeries_deselectPoints(XXYSeries* self, const int* indexes, int count)
{
    int i;
    if (!self || !indexes) return;
    for (i = 0; i < count; ++i)
        XXYSeries_deselectPoint(self, indexes[i]);
}

void XXYSeries_toggleSelection(XXYSeries* self, const int* indexes, int count)
{
    int i;
    if (!self || !indexes) return;
    for (i = 0; i < count; ++i) {
        int idx = indexes[i];
        if (idx >= 0 && idx < self->m_count)
            XXYSeries_setPointSelected(self, idx,
                !XXYSeries_isPointSelected(self, idx));
    }
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
    if (!self) return;
    self->m_bestFitColor = color;
    if (width > 0) self->m_bestFitWidth = width;
}

void XXYSeries_bestFitLinePen(const XXYSeries* self, uint32_t* color,
                              double* width)
{
    if (!self) return;
    if (color) *color = self->m_bestFitColor;
    if (width) *width = self->m_bestFitWidth;
}

const char* XXYSeries_pointLabelsFont(const XXYSeries* self)
{ return XXYSeries_pointLabelsFontFamily(self); }

void XXYSeries_setLightMarker(XXYSeries* self, const XPixmap* marker)
{ if (self) self->m_lightMarker = marker; }

const XPixmap* XXYSeries_lightMarker(const XXYSeries* self)
{ return self ? self->m_lightMarker : NULL; }

void XXYSeries_setSelectedLightMarker(XXYSeries* self, const XPixmap* marker)
{ if (self) self->m_selectedLightMarker = marker; }

const XPixmap* XXYSeries_selectedLightMarker(const XXYSeries* self)
{ return self ? self->m_selectedLightMarker : NULL; }

void XXYSeries_setPointConfiguration(XXYSeries* self, int index,
                                     uint32_t color, double size)
{
    uint32_t* pc;
    double* ps;
    if (!self || index < 0) return;
    if (index >= self->m_pointConfigCapacity) {
        int cap = self->m_pointConfigCapacity > 0
            ? self->m_pointConfigCapacity : 8;
        while (cap <= index) cap *= 2;
        pc = (uint32_t*)XRealloc_System(self->m_pointColors,
            sizeof(uint32_t) * (size_t)cap);
        ps = (double*)XRealloc_System(self->m_pointSizes,
            sizeof(double) * (size_t)cap);
        if (!pc || !ps) return;
        self->m_pointColors = pc;
        self->m_pointSizes = ps;
        self->m_pointConfigCapacity = cap;
    }
    if (color != 0 && self->m_pointColors)
        self->m_pointColors[index] = color;
    if (size != 0 && self->m_pointSizes)
        self->m_pointSizes[index] = size;
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
    if (!self) return;
    if (index < 0) {
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
        if (self->m_pointColors)
            self->m_pointColors[index] = 0;
        if (self->m_pointSizes)
            self->m_pointSizes[index] = 0;
    }
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
