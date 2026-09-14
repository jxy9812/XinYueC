#include "XAbstractSeries.h"

#include "XAlgorithm.h"
#include "XValueAxis.h"
#include "XString.h"
#include "XMemory.h"
#include "XClass.h"

#if XCHARTS_ON

static void VXAbstractSeries_deinit(XAbstractSeries* self);
static void VXAbstractSeries_copy(XAbstractSeries* self,
                                  const XAbstractSeries* other);
static void VXAbstractSeries_move(XAbstractSeries* self,
                                  XAbstractSeries* other);

XVtable* XAbstractSeries_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractSeries)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractSeries_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXAbstractSeries_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXAbstractSeries_move);
    return XVTABLE_DEFAULT;
}

void XAbstractSeries_init(XAbstractSeries* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init(&self->m_base);
    XClassSetVtable(self, XAbstractSeries);
    self->m_name = XString_create();
    self->m_visible = true;
    self->m_opacity = 1.0;
    self->m_useOpenGL = false;
    self->m_chart = NULL;
    self->m_type = XChartSeriesType_Line;
    self->m_axes = NULL;
    self->m_axisCount = 0;
    self->m_axisCapacity = 0;
}

XAbstractSeries* XAbstractSeries_create_ex(XMemoryType memory)
{
    XAbstractSeries* self =
        (XAbstractSeries*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XAbstractSeries_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXAbstractSeries_deinit(XAbstractSeries* self)
{
    if (!self) return;
    if (self->m_name) {
        XString_delete_base(self->m_name);
        self->m_name = NULL;
    }
    if (self->m_axes) {
        XFree_System(self->m_axes);
        self->m_axes = NULL;
    }
    self->m_axisCount = 0;
    self->m_axisCapacity = 0;
    self->m_chart = NULL;
    self->m_type = XChartSeriesType_Line;
    self->m_axes = NULL;
    self->m_axisCount = 0;
    self->m_axisCapacity = 0;
}

static void VXAbstractSeries_copy(XAbstractSeries* self,
                                  const XAbstractSeries* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractSeries_init(self);
    if (self->m_name && other->m_name)
        XString_assign(self->m_name, other->m_name);
    else if (!self->m_name && other->m_name)
        self->m_name = XString_create_copy(other->m_name);
    self->m_visible = other->m_visible;
    self->m_opacity = other->m_opacity;
    self->m_useOpenGL = other->m_useOpenGL;
    /* m_chart 为借用指针，不随拷贝转移。 */
    self->m_chart = NULL;
    self->m_type = XChartSeriesType_Line;
    self->m_axes = NULL;
    self->m_axisCount = 0;
    self->m_axisCapacity = 0;
}

static void VXAbstractSeries_move(XAbstractSeries* self,
                                  XAbstractSeries* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractSeries_init(self);
    if (self->m_name) XString_delete_base(self->m_name);
    self->m_name = other->m_name;
    other->m_name = NULL;
    self->m_visible = other->m_visible;
    self->m_opacity = other->m_opacity;
    self->m_useOpenGL = other->m_useOpenGL;
    self->m_type = other->m_type;
    self->m_chart = other->m_chart;
    other->m_chart = NULL;
    self->m_axes = other->m_axes;
    other->m_axes = NULL;
    self->m_axisCount = other->m_axisCount;
    other->m_axisCount = 0;
    self->m_axisCapacity = other->m_axisCapacity;
    other->m_axisCapacity = 0;
}

void XAbstractSeries_setName(XAbstractSeries* self, const char* name)
{
    if (!self) return;
    if (!self->m_name) self->m_name = XString_create();
    if (self->m_name)
        XString_assign_utf8(self->m_name, name ? name : "");
}

const char* XAbstractSeries_name(const XAbstractSeries* self)
{
    const char* text;
    if (!self || !self->m_name) return "";
    text = XString_toUtf8(self->m_name);
    return text ? text : "";
}

void XAbstractSeries_setVisible(XAbstractSeries* self, bool visible)
{ if (self) self->m_visible = visible; }

bool XAbstractSeries_isVisible(const XAbstractSeries* self)
{ return self ? self->m_visible : false; }

void XAbstractSeries_setOpacity(XAbstractSeries* self, double opacity)
{
    if (!self) return;
    if (opacity < 0.0) opacity = 0.0;
    if (opacity > 1.0) opacity = 1.0;
    self->m_opacity = opacity;
}

double XAbstractSeries_opacity(const XAbstractSeries* self)
{ return self ? self->m_opacity : 1.0; }

void XAbstractSeries_setUseOpenGL(XAbstractSeries* self, bool enable)
{ if (self) self->m_useOpenGL = enable; }

bool XAbstractSeries_useOpenGL(const XAbstractSeries* self)
{ return self ? self->m_useOpenGL : false; }

XChart* XAbstractSeries_chart(const XAbstractSeries* self)
{ return self ? self->m_chart : NULL; }

int XAbstractSeries_type(const XAbstractSeries* self)
{ return self ? self->m_type : XChartSeriesType_Line; }

void XAbstractSeries_attachAxis(XAbstractSeries* self, XValueAxis* axis)
{
    int i;
    XValueAxis** ax;
    if (!self || !axis) return;
    for (i = 0; i < self->m_axisCount; ++i)
        if (self->m_axes[i] == axis) return;
    if (self->m_axisCount >= self->m_axisCapacity) {
        int cap = self->m_axisCapacity > 0 ? self->m_axisCapacity * 2 : 4;
        ax = (XValueAxis**)XRealloc_System(self->m_axes,
            sizeof(XValueAxis*) * (size_t)cap);
        if (!ax) return;
        self->m_axes = ax;
        self->m_axisCapacity = cap;
    }
    self->m_axes[self->m_axisCount++] = axis;
}

void XAbstractSeries_detachAxis(XAbstractSeries* self, XValueAxis* axis)
{
    int i;
    if (!self || !axis) return;
    for (i = 0; i < self->m_axisCount; ++i) {
        if (self->m_axes[i] == axis) {
            for (; i < self->m_axisCount - 1; ++i)
                self->m_axes[i] = self->m_axes[i + 1];
            self->m_axisCount--;
            return;
        }
    }
}

int XAbstractSeries_axisCount(const XAbstractSeries* self)
{ return self ? self->m_axisCount : 0; }

XValueAxis* XAbstractSeries_axisAt(const XAbstractSeries* self, int index)
{
    if (!self || index < 0 || index >= self->m_axisCount) return NULL;
    return self->m_axes ? self->m_axes[index] : NULL;
}

int XAbstractSeries_attachedAxes(const XAbstractSeries* self,
                                 XValueAxis** out, int maxCount)
{
    int n;
    int i;
    if (!self || !out || maxCount <= 0) return 0;
    n = self->m_axisCount < maxCount ? self->m_axisCount : maxCount;
    for (i = 0; i < n; ++i)
        out[i] = self->m_axes ? self->m_axes[i] : NULL;
    return n;
}

#endif /* XCHARTS_ON */
