#include "XAbstractBarSeries.h"
#include "XStringUtils.h"

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

/** @brief 发射无载荷信号。 */
static void xabs_emitVoid(XAbstractBarSeries* self, size_t signal)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

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
    self->m_barWidth = 0.8;
    self->m_labelsVisible = false;
    self->m_labelsFormat = XString_create_utf8("@value");
    self->m_labelsAngle = 0.0;
    self->m_labelsPosition = 0;
    self->m_labelsPrecision = 1;
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
    if (self->m_values) {
        XFree_System(self->m_values);
        self->m_values = NULL;
    }
    if (self->m_categories) {
        for (i = 0; i < self->m_capacity; ++i) {
            if (self->m_categories[i]) {
                XString_delete_base(self->m_categories[i]);
                self->m_categories[i] = NULL;
            }
        }
        XFree_System(self->m_categories);
        self->m_categories = NULL;
    }
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
    for (i = 0; i < other->m_count; ++i)
        XAbstractBarSeries_append(self,
            XAbstractBarSeries_category(other, i),
            other->m_values ? other->m_values[i] : 0.0);
    self->m_barWidth = other->m_barWidth;
    self->m_labelsVisible = other->m_labelsVisible;
    self->m_labelsAngle = other->m_labelsAngle;
    if (self->m_labelsFormat && other->m_labelsFormat)
        XString_assign(self->m_labelsFormat, other->m_labelsFormat);
}

static void VXAbstractBarSeries_move(XAbstractBarSeries* self,
                                     XAbstractBarSeries* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractBarSeries_init(self);
    XClass_Parent(XAbstractSeries, EXClass_Move,
                  void(*)(XAbstractSeries*, XAbstractSeries*))(
        (XAbstractSeries*)self, (XAbstractSeries*)other);
    if (self->m_values) XFree_System(self->m_values);
    if (self->m_categories) {
        int i;
        for (i = 0; i < self->m_capacity; ++i)
            if (self->m_categories[i])
                XString_delete_base(self->m_categories[i]);
        XFree_System(self->m_categories);
    }
    if (self->m_labelsFormat) XString_delete_base(self->m_labelsFormat);
    self->m_values = other->m_values;
    other->m_values = NULL;
    self->m_categories = other->m_categories;
    other->m_categories = NULL;
    self->m_count = other->m_count;
    other->m_count = 0;
    self->m_capacity = other->m_capacity;
    other->m_capacity = 0;
    self->m_barWidth = other->m_barWidth;
    self->m_labelsVisible = other->m_labelsVisible;
    self->m_labelsAngle = other->m_labelsAngle;
    self->m_labelsFormat = other->m_labelsFormat;
    other->m_labelsFormat = XString_create_utf8("@value");
}

int XAbstractBarSeries_append(XAbstractBarSeries* self, const char* label,
                              double value)
{
    int idx;
    if (!self || !label) return -1;
    if (self->m_count >= self->m_capacity) {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 8;
        int oldCap = self->m_capacity;
        int ci;
        double* v = (double*)XRealloc_System(self->m_values,
            sizeof(double) * (size_t)cap);
        XString** cat = (XString**)XRealloc_System(self->m_categories,
            sizeof(XString*) * (size_t)cap);
        if (!v || !cat) return -1;
        self->m_values = v;
        self->m_categories = cat;
        for (ci = oldCap; ci < cap; ++ci)
            self->m_categories[ci] = NULL;
        self->m_capacity = cap;
    }
    idx = self->m_count;
    self->m_values[idx] = value;
    self->m_categories[idx] = XString_create_utf8(label);
    self->m_count++;
    xabs_emitVoid(self, (size_t)XAbstractBarSeries_countChanged_signal);
    return idx;
}

bool XAbstractBarSeries_remove(XAbstractBarSeries* self, int index)
{
    int i;
    if (!self || index < 0 || index >= self->m_count) return false;
    if (self->m_categories && self->m_categories[index]) {
        XString_delete_base(self->m_categories[index]);
        self->m_categories[index] = NULL;
    }
    for (i = index; i < self->m_count - 1; ++i) {
        self->m_values[i] = self->m_values[i + 1];
        self->m_categories[i] = self->m_categories[i + 1];
    }
    self->m_count--;
    if (self->m_categories)
        self->m_categories[self->m_count] = NULL;
    xabs_emitVoid(self, (size_t)XAbstractBarSeries_countChanged_signal);
    return true;
}

int XAbstractBarSeries_count(const XAbstractBarSeries* self)
{ return self ? self->m_count : 0; }

void XAbstractBarSeries_clear(XAbstractBarSeries* self)
{
    int i;
    if (!self) return;
    if (self->m_categories) {
        for (i = 0; i < self->m_count; ++i) {
            if (self->m_categories[i]) {
                XString_delete_base(self->m_categories[i]);
                self->m_categories[i] = NULL;
            }
        }
    }
    self->m_count = 0;
}

double XAbstractBarSeries_value(const XAbstractBarSeries* self, int index)
{
    if (!self || index < 0 || index >= self->m_count) return 0;
    return self->m_values ? self->m_values[index] : 0;
}

const char* XAbstractBarSeries_category(const XAbstractBarSeries* self,
                                        int index)
{
    const char* text;
    if (!self || index < 0 || index >= self->m_count ||
        !self->m_categories || !self->m_categories[index])
        return "";
    text = XString_toUtf8(self->m_categories[index]);
    return text ? text : "";
}

void XAbstractBarSeries_setBarWidth(XAbstractBarSeries* self, double width)
{ if (self && width > 0) self->m_barWidth = width; }

double XAbstractBarSeries_barWidth(const XAbstractBarSeries* self)
{ return self ? self->m_barWidth : 0.8; }

void XAbstractBarSeries_setLabelsVisible(XAbstractBarSeries* self,
                                         bool visible)
{
    if (!self || self->m_labelsVisible == visible) return;
    self->m_labelsVisible = visible;
    xabs_emitVoid(self,
        (size_t)XAbstractBarSeries_labelsVisibleChanged_signal);
}

bool XAbstractBarSeries_isLabelsVisible(const XAbstractBarSeries* self)
{ return self ? self->m_labelsVisible : false; }

void XAbstractBarSeries_setLabelsFormat(XAbstractBarSeries* self,
                                        const char* format)
{
    if (!self) return;
    if (!self->m_labelsFormat) self->m_labelsFormat = XString_create();
    if (self->m_labelsFormat)
        XString_assign_utf8(self->m_labelsFormat,
                            format ? format : "@value");
}

const char* XAbstractBarSeries_labelsFormat(const XAbstractBarSeries* self)
{
    const char* text;
    if (!self || !self->m_labelsFormat) return "@value";
    text = XString_toUtf8(self->m_labelsFormat);
    return text ? text : "@value";
}

void XAbstractBarSeries_setLabelsAngle(XAbstractBarSeries* self,
                                       double angle)
{ if (self) self->m_labelsAngle = angle; }

double XAbstractBarSeries_labelsAngle(const XAbstractBarSeries* self)
{ return self ? self->m_labelsAngle : 0.0; }

void* XAbstractBarSeries_countChanged_signal(XAbstractBarSeries* self)
{ (void)self; return (void*)(size_t)XAbstractBarSeries_countChanged_signal; }
void* XAbstractBarSeries_labelsVisibleChanged_signal(XAbstractBarSeries* self)
{ (void)self; return (void*)(size_t)XAbstractBarSeries_labelsVisibleChanged_signal; }
void* XAbstractBarSeries_labelsFormatChanged_signal(XAbstractBarSeries* self,
                                                    const char* format)
{ (void)self; (void)format;
  return (void*)(size_t)XAbstractBarSeries_labelsFormatChanged_signal; }

bool XAbstractBarSeries_insert(XAbstractBarSeries* self, int index,
                               const char* label, double value)
{
    int i;
    if (!self || !label || index < 0 || index > self->m_count) return false;
    if (self->m_count >= self->m_capacity) {
        int cap = self->m_capacity > 0 ? self->m_capacity * 2 : 8;
        int oldCap = self->m_capacity;
        int ci;
        double* v = (double*)XRealloc_System(self->m_values,
            sizeof(double) * (size_t)cap);
        XString** cat = (XString**)XRealloc_System(self->m_categories,
            sizeof(XString*) * (size_t)cap);
        if (!v || !cat) return false;
        self->m_values = v;
        self->m_categories = cat;
        for (ci = oldCap; ci < cap; ++ci)
            self->m_categories[ci] = NULL;
        self->m_capacity = cap;
    }
    for (i = self->m_count; i > index; --i) {
        self->m_values[i] = self->m_values[i - 1];
        self->m_categories[i] = self->m_categories[i - 1];
    }
    self->m_values[index] = value;
    self->m_categories[index] = XString_create_utf8(label);
    self->m_count++;
    xabs_emitVoid(self, (size_t)XAbstractBarSeries_countChanged_signal);
    return true;
}

double XAbstractBarSeries_take(XAbstractBarSeries* self, int index,
                               char** labelOut)
{
    double v;
    if (!self || index < 0 || index >= self->m_count) {
        if (labelOut) *labelOut = NULL;
        return 0;
    }
    v = self->m_values ? self->m_values[index] : 0.0;
    if (labelOut) {
        const char* t = XAbstractBarSeries_category(self, index);
        *labelOut = (char*)XMalloc_System(XStrlen(t) + 1);
        if (*labelOut) XStrcpy(*labelOut, t);
    }
    XAbstractBarSeries_remove(self, index);
    return v;
}

int XAbstractBarSeries_barSets(XAbstractBarSeries* self, double* out,
                               int maxCount)
{
    int n;
    int i;
    if (!self || !out || maxCount <= 0) return 0;
    n = self->m_count < maxCount ? self->m_count : maxCount;
    for (i = 0; i < n; ++i)
        out[i] = self->m_values ? self->m_values[i] : 0.0;
    return n;
}

void XAbstractBarSeries_setLabelsPosition(XAbstractBarSeries* self,
                                          int position)
{ if (self) self->m_labelsPosition = position; }

int XAbstractBarSeries_labelsPosition(const XAbstractBarSeries* self)
{ return self ? self->m_labelsPosition : 0; }

void XAbstractBarSeries_setLabelsPrecision(XAbstractBarSeries* self,
                                           int precision)
{ if (self) self->m_labelsPrecision = precision; }

int XAbstractBarSeries_labelsPrecision(const XAbstractBarSeries* self)
{ return self ? self->m_labelsPrecision : 1; }

#endif /* XCHARTS_ON */
