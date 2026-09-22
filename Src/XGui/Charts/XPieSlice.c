#include "XPieSlice.h"

#include "XAlgorithm.h"
#include "XString.h"
#include "XMemory.h"
#include "XClass.h"

#if XCHARTS_ON

/**
 * @brief 发射空参信号（内部辅助；不在 class_init 中注册）。
 *
 * @param self   目标切片指针。
 * @param signal 信号标识。
 * @return 无返回值。
 */
static void xpieslice_emit0(XPieSlice* self, size_t signal)
{
    if (!self) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
}

/**
 * @brief 发射带 bool 载荷的信号（内部辅助）。
 *
 * @param self   目标切片指针。
 * @param signal 信号标识。
 * @param value  bool 载荷。
 * @return 无返回值。
 */
static void xpieslice_emitBool(XPieSlice* self, size_t signal, bool value)
{
    XVarList* args;
    if (!self) return;
    args = XVarList_Create(XVar(bool, value));
    if (!args) return;
    if (((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else
        XVarList_delete(args);
}

static void VXSlice_deinit(XPieSlice* self);
static void VXSlice_copy(XPieSlice* self, const XPieSlice* other);
static void VXSlice_move(XPieSlice* self, XPieSlice* other);

XVtable* XPieSlice_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPieSlice)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSlice_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSlice_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSlice_move);
    return XVTABLE_DEFAULT;
}

void XPieSlice_init(XPieSlice* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init(&self->m_base);
    XClassSetVtable(self, XPieSlice);
    self->m_label = XString_create();
    self->m_labelVisible = false;
    self->m_labelPosition = XPieSlice_LabelPosition_Outside;
    self->m_explodeDistanceFactor = 0.15;
    self->m_labelArmLengthFactor = 0.15;
}

void XPieSlice_init_ex(XPieSlice* self, const XString* label, double value)
{
    if (!self) return;
    XPieSlice_init(self);
    XPieSlice_setLabel(self, label);
    self->m_value = value;
    self->m_penWidth = 1.0;
    self->m_penColor = 0;
    self->m_brushColor = 0;
    self->m_labelBrushColor = 0;
    self->m_labelFontFamily = NULL;
    self->m_labelFontSize = 0;
}
void XPieSlice_init_ex_2(XPieSlice* self, const char* label, double value)
{
    XString* tmp = NULL;
    if (label) {
        tmp = XString_create_utf8(label);
        if (!tmp) return;
    }
    XPieSlice_init_ex(self, tmp, value);
    if (tmp) XString_delete_base(tmp);
}

XPieSlice* XPieSlice_create_ex(XMemoryType memory, const XString* label,
                               double value)
{
    XPieSlice* self = (XPieSlice*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XPieSlice_init_ex(self, label, value);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}
XPieSlice* XPieSlice_create_ex_2(XMemoryType memory, const char* label,
                                 double value)
{
    XString* tmp = NULL;
    XPieSlice* self;
    if (label) {
        tmp = XString_create_utf8(label);
        if (!tmp) return NULL;
    }
    self = XPieSlice_create_ex(memory, tmp, value);
    if (tmp) XString_delete_base(tmp);
    return self;
}

static void VXSlice_deinit(XPieSlice* self)
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
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static void VXSlice_copy(XPieSlice* self, const XPieSlice* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XPieSlice_init(self);
    if (self->m_label && other->m_label)
        XString_assign(self->m_label, other->m_label);
    self->m_value = other->m_value;
    self->m_color = other->m_color;
    self->m_borderColor = other->m_borderColor;
    self->m_borderWidth = other->m_borderWidth;
    self->m_labelColor = other->m_labelColor;
    self->m_labelVisible = other->m_labelVisible;
    self->m_labelPosition = other->m_labelPosition;
    self->m_exploded = other->m_exploded;
    self->m_explodeDistanceFactor = other->m_explodeDistanceFactor;
    self->m_labelArmLengthFactor = other->m_labelArmLengthFactor;
    self->m_percentage = other->m_percentage;
    self->m_startAngle = other->m_startAngle;
    self->m_angleSpan = other->m_angleSpan;
}

static void VXSlice_move(XPieSlice* self, XPieSlice* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XPieSlice_init(self);
    if (self->m_label) XString_delete_base(self->m_label);
    if (self->m_labelFontFamily) XString_delete_base(self->m_labelFontFamily);
    /* 结构体赋值转移全部字段；other 重置为全新默认对象。 */
    *self = *other;
    XMemset(other, 0, sizeof(XPieSlice));
    XPieSlice_init(other);
    XClassSetVtable(self, XPieSlice);
}

/* ==================== 属性 ==================== */

void XPieSlice_setLabel(XPieSlice* self, const XString* label)
{
    if (!self) return;
    if (!self->m_label) self->m_label = XString_create();
    if (!self->m_label) return;
    if (label)
        XString_assign(self->m_label, label);
    else
        XString_assign_utf8(self->m_label, "");
    xpieslice_emit0(self, (size_t)XPieSlice_labelChanged_signal);
}
void XPieSlice_setLabel_2(XPieSlice* self, const char* label)
{
    XString* tmp = NULL;
    if (label) {
        tmp = XString_create_utf8(label);
        if (!tmp) return;
    }
    XPieSlice_setLabel(self, tmp);
    if (tmp) XString_delete_base(tmp);
}

const XString* XPieSlice_label(const XPieSlice* self)
{
    return (self && self->m_label) ? self->m_label : NULL;
}
const char* XPieSlice_label_2(const XPieSlice* self)
{
    const XString* s;
    const char* text;
    s = XPieSlice_label(self);
    if (!s) return "";
    text = XString_toUtf8(s);
    return text ? text : "";
}

void XPieSlice_setValue(XPieSlice* self, double value)
{
    /* 根因（R-38）：此前把 init_ex 初始化体整段复制进 setter——每次改值
     * 都把用户设置的 pen/brush/labelBrush 颜色清零、m_labelFontFamily
     * 置 NULL（旧串不释放＝内存泄漏），外观被静默清空。对标 Qt 6.8.3
     * QPieSlice::setValue：只更新数值并发 valueChanged，由 series 侧
     * 基于该信号触发百分比/角度重算（本库入口 XPieSeries_updateAngles，
     * 对标 QPieSeriesPrivate 的 sliceUpdated 处理），setter 不触碰
     * pen/brush/labelFont 等样式状态。 */
    if (!self || self->m_value == value) return;
    self->m_value = value;
    xpieslice_emit0(self, (size_t)XPieSlice_valueChanged_signal);
}

double XPieSlice_value(const XPieSlice* self)
{ return self ? self->m_value : 0.0; }

void XPieSlice_setLabelVisible(XPieSlice* self, bool visible)
{
    if (!self || self->m_labelVisible == visible) return;
    self->m_labelVisible = visible;
    xpieslice_emit0(self, (size_t)XPieSlice_labelVisibleChanged_signal);
}

bool XPieSlice_isLabelVisible(const XPieSlice* self)
{ return self ? self->m_labelVisible : false; }

void XPieSlice_setLabelPosition(XPieSlice* self, XPieSlice_LabelPosition position)
{
    if (!self) return;
    if ((int)position < 0 ||
        position > XPieSlice_LabelPosition_InsideNormal)
        position = XPieSlice_LabelPosition_Outside;
    self->m_labelPosition = position;
}

XPieSlice_LabelPosition XPieSlice_labelPosition(const XPieSlice* self)
{
    return self ? self->m_labelPosition : XPieSlice_LabelPosition_Outside;
}

void XPieSlice_setExploded(XPieSlice* self, bool exploded)
{
    if (!self || self->m_exploded == exploded) return;
    self->m_exploded = exploded;
}

bool XPieSlice_isExploded(const XPieSlice* self)
{ return self ? self->m_exploded : false; }

void XPieSlice_setBorderColor(XPieSlice* self, uint32_t color)
{
    if (!self || self->m_borderColor == color) return;
    self->m_borderColor = color;
    xpieslice_emit0(self, (size_t)XPieSlice_borderColorChanged_signal);
}

uint32_t XPieSlice_borderColor(const XPieSlice* self)
{ return self ? self->m_borderColor : 0u; }

void XPieSlice_setBorderWidth(XPieSlice* self, int width)
{
    if (!self) return;
    width = width > 0 ? width : 0;
    if (self->m_borderWidth == width) return;
    self->m_borderWidth = width;
    xpieslice_emit0(self, (size_t)XPieSlice_borderWidthChanged_signal);
}

int XPieSlice_borderWidth(const XPieSlice* self)
{ return self ? self->m_borderWidth : 0; }

void XPieSlice_setColor(XPieSlice* self, uint32_t color)
{
    if (!self || self->m_color == color) return;
    self->m_color = color;
    xpieslice_emit0(self, (size_t)XPieSlice_colorChanged_signal);
}

uint32_t XPieSlice_color(const XPieSlice* self)
{ return self ? self->m_color : 0u; }

void XPieSlice_setLabelColor(XPieSlice* self, uint32_t color)
{
    if (!self || self->m_labelColor == color) return;
    self->m_labelColor = color;
    xpieslice_emit0(self, (size_t)XPieSlice_labelColorChanged_signal);
}

uint32_t XPieSlice_labelColor(const XPieSlice* self)
{
    if (!self) return 0u;
    return self->m_labelColor != 0 ? self->m_labelColor : 0xFFFFFFFFu;
}

void XPieSlice_setLabelArmLengthFactor(XPieSlice* self, double factor)
{
    if (!self) return;
    if (factor < 0.0) factor = 0.0;
    if (factor > 1.0) factor = 1.0;
    self->m_labelArmLengthFactor = factor;
}

double XPieSlice_labelArmLengthFactor(const XPieSlice* self)
{ return self ? self->m_labelArmLengthFactor : 0.15; }

void XPieSlice_setExplodeDistanceFactor(XPieSlice* self, double factor)
{
    if (!self) return;
    if (factor < 0.0) factor = 0.0;
    if (factor > 1.0) factor = 1.0;
    self->m_explodeDistanceFactor = factor;
}

double XPieSlice_explodeDistanceFactor(const XPieSlice* self)
{ return self ? self->m_explodeDistanceFactor : 0.15; }

double XPieSlice_percentage(const XPieSlice* self)
{ return self ? self->m_percentage : 0.0; }

double XPieSlice_startAngle(const XPieSlice* self)
{ return self ? self->m_startAngle : 0.0; }

double XPieSlice_angleSpan(const XPieSlice* self)
{ return self ? self->m_angleSpan : 0.0; }

/* ==================== 信号 ==================== */

void* XPieSlice_clicked_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_clicked_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_clicked_signal);
    return (void*)(size_t)XPieSlice_clicked_signal;
}

void* XPieSlice_hovered_signal(XPieSlice* self, bool state)
{
    if (!self) return (void*)(size_t)XPieSlice_hovered_signal;
    xpieslice_emitBool(self, (size_t)XPieSlice_hovered_signal, state);
    return (void*)(size_t)XPieSlice_hovered_signal;
}

void* XPieSlice_pressed_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_pressed_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_pressed_signal);
    return (void*)(size_t)XPieSlice_pressed_signal;
}

void* XPieSlice_released_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_released_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_released_signal);
    return (void*)(size_t)XPieSlice_released_signal;
}

void* XPieSlice_doubleClicked_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_doubleClicked_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_doubleClicked_signal);
    return (void*)(size_t)XPieSlice_doubleClicked_signal;
}

void* XPieSlice_labelChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_labelChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_labelChanged_signal);
    return (void*)(size_t)XPieSlice_labelChanged_signal;
}

void* XPieSlice_valueChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_valueChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_valueChanged_signal);
    return (void*)(size_t)XPieSlice_valueChanged_signal;
}

void* XPieSlice_labelVisibleChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_labelVisibleChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_labelVisibleChanged_signal);
    return (void*)(size_t)XPieSlice_labelVisibleChanged_signal;
}

void* XPieSlice_colorChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_colorChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_colorChanged_signal);
    return (void*)(size_t)XPieSlice_colorChanged_signal;
}

void* XPieSlice_borderColorChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_borderColorChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_borderColorChanged_signal);
    return (void*)(size_t)XPieSlice_borderColorChanged_signal;
}

void* XPieSlice_percentageChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_percentageChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_percentageChanged_signal);
    return (void*)(size_t)XPieSlice_percentageChanged_signal;
}

void* XPieSlice_startAngleChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_startAngleChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_startAngleChanged_signal);
    return (void*)(size_t)XPieSlice_startAngleChanged_signal;
}

void* XPieSlice_angleSpanChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_angleSpanChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_angleSpanChanged_signal);
    return (void*)(size_t)XPieSlice_angleSpanChanged_signal;
}

void* XPieSlice_borderWidthChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_borderWidthChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_borderWidthChanged_signal);
    return (void*)(size_t)XPieSlice_borderWidthChanged_signal;
}

void* XPieSlice_penChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_penChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_penChanged_signal);
    return (void*)(size_t)XPieSlice_penChanged_signal;
}

void* XPieSlice_brushChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_brushChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_brushChanged_signal);
    return (void*)(size_t)XPieSlice_brushChanged_signal;
}

void* XPieSlice_labelBrushChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_labelBrushChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_labelBrushChanged_signal);
    return (void*)(size_t)XPieSlice_labelBrushChanged_signal;
}

void* XPieSlice_labelFontChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_labelFontChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_labelFontChanged_signal);
    return (void*)(size_t)XPieSlice_labelFontChanged_signal;
}

void* XPieSlice_labelColorChanged_signal(XPieSlice* self)
{
    if (!self) return (void*)(size_t)XPieSlice_labelColorChanged_signal;
    xpieslice_emit0(self, (size_t)XPieSlice_labelColorChanged_signal);
    return (void*)(size_t)XPieSlice_labelColorChanged_signal;
}

void XPieSlice_setPen(XPieSlice* self, uint32_t color, double width)
{
    if (!self) return;
    if (self->m_penColor == color &&
        (width <= 0 || self->m_penWidth == width))
        return;
    self->m_penColor = color;
    if (width > 0) self->m_penWidth = width;
    xpieslice_emit0(self, (size_t)XPieSlice_penChanged_signal);
}

void XPieSlice_pen(const XPieSlice* self, uint32_t* color, double* width)
{
    if (!self) return;
    if (color) *color = self->m_penColor;
    if (width) *width = self->m_penWidth;
}

uint32_t XPieSlice_penColor(const XPieSlice* self)
{ return self ? self->m_penColor : 0; }

void XPieSlice_setBrush(XPieSlice* self, uint32_t color)
{
    if (!self || self->m_brushColor == color) return;
    self->m_brushColor = color;
    xpieslice_emit0(self, (size_t)XPieSlice_brushChanged_signal);
}

uint32_t XPieSlice_brushColor(const XPieSlice* self)
{ return self ? self->m_brushColor : 0; }

/** @brief 读取画刷颜色（头文件声明的 Qt 对齐名，转发 brushColor）。 */
uint32_t XPieSlice_brush(const XPieSlice* self)
{ return XPieSlice_brushColor(self); }

void XPieSlice_setLabelBrush(XPieSlice* self, uint32_t color)
{
    if (!self || self->m_labelBrushColor == color) return;
    self->m_labelBrushColor = color;
    xpieslice_emit0(self, (size_t)XPieSlice_labelBrushChanged_signal);
}

uint32_t XPieSlice_labelBrushColor(const XPieSlice* self)
{ return self ? self->m_labelBrushColor : 0; }

/** @brief 读取标签画刷颜色（头文件声明的 Qt 对齐名，转发 labelBrushColor）。 */
uint32_t XPieSlice_labelBrush(const XPieSlice* self)
{ return XPieSlice_labelBrushColor(self); }

void XPieSlice_setLabelFont(XPieSlice* self, const XString* family,
                            int pointSize)
{
    bool changed = false;
    if (!self) return;
    if (family) {
        if (!self->m_labelFontFamily)
            self->m_labelFontFamily = XString_create();
        if (!self->m_labelFontFamily) return;
        if (!XString_equals(self->m_labelFontFamily, family,
                            XChar_CaseSensitive)) {
            XString_assign(self->m_labelFontFamily, family);
            changed = true;
        }
    }
    if (pointSize > 0 && self->m_labelFontSize != pointSize) {
        self->m_labelFontSize = pointSize;
        changed = true;
    }
    if (changed)
        xpieslice_emit0(self, (size_t)XPieSlice_labelFontChanged_signal);
}
void XPieSlice_setLabelFont_2(XPieSlice* self, const char* family,
                              int pointSize)
{
    XString* tmp = NULL;
    if (family) {
        tmp = XString_create_utf8(family);
        if (!tmp) return;
    }
    XPieSlice_setLabelFont(self, tmp, pointSize);
    if (tmp) XString_delete_base(tmp);
}

const XString* XPieSlice_labelFont(const XPieSlice* self)
{
    return (self && self->m_labelFontFamily) ? self->m_labelFontFamily : NULL;
}
const char* XPieSlice_labelFont_2(const XPieSlice* self)
{
    const XString* s;
    const char* text;
    s = XPieSlice_labelFont(self);
    if (!s) return "";
    text = XString_toUtf8(s);
    return text ? text : "";
}

int XPieSlice_labelFontSize(const XPieSlice* self)
{ return self ? self->m_labelFontSize : 0; }

#endif /* XCHARTS_ON */
