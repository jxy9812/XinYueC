/**
 * @file       XAbstractAxis.c
 * @brief      XAbstractAxis 抽象坐标轴基类实现（对标 QAbstractAxis）。
 * @details    完整实现 Qt 6.8 QAbstractAxis 的公共 API：可见性（轴/轴线/
 *             标签/标题/网格/次网格/阴影带）、画笔/画刷/字体（以颜色+线宽、
 *             字体族+字号参数化承载 QPen/QBrush/QFont）、范围、反转、
 *             可编辑/截断标签、朝向/对齐与全套变更信号。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XAbstractAxis.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"

#if XCHARTS_ON

/* ==================== 信号发射助手 ==================== */

static bool xaxis_canEmit(const XAbstractAxis* self)
{
    return self && ((const XObject*)self)->m_signalSlot;
}

static void xaxis_emitBool(XAbstractAxis* self, size_t signal, bool value)
{
    XVarList* args;
    if (!xaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(bool, value));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xaxis_emitInt(XAbstractAxis* self, size_t signal, int value)
{
    XVarList* args;
    if (!xaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(int, value));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xaxis_emitU32(XAbstractAxis* self, size_t signal, uint32_t value)
{
    XVarList* args;
    if (!xaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, value));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xaxis_emitText(XAbstractAxis* self, size_t signal,
                           const XString* text)
{
    XVarList* args;
    if (!xaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(const XString*, text));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

static void xaxis_emitRange(XAbstractAxis* self, size_t signal,
                            double mi, double ma)
{
    XVarList* args;
    if (!xaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(double, mi), XVar(double, ma));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/** @brief 发射「画笔变更」类信号（颜色 + 线宽，对应 QPen 载荷）。 */
static void xaxis_emitPen(XAbstractAxis* self, size_t signal,
                          uint32_t color, double width)
{
    XVarList* args;
    if (!xaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(uint32_t, color), XVar(double, width));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/** @brief 发射「字体变更」类信号（字体族 + 字号，对应 QFont 载荷）。 */
static void xaxis_emitFont(XAbstractAxis* self, size_t signal,
                           const char* family, int pointSize)
{
    XVarList* args;
    if (!xaxis_canEmit(self)) return;
    args = XVarList_Create(XVar(const char*, family), XVar(int, pointSize));
    if (!args) return;
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

/* ==================== 虚函数实现 ==================== */

static void VXAbstractAxis_deinit(XAbstractAxis* self)
{
    if (!self) return;
    if (self->m_titleText) {
        XClassDelete(self->m_titleText);
        self->m_titleText = NULL;
    }
    if (self->m_labelsFontFamily) {
        XClassDelete(self->m_labelsFontFamily);
        self->m_labelsFontFamily = NULL;
    }
    if (self->m_titleFontFamily) {
        XClassDelete(self->m_titleFontFamily);
        self->m_titleFontFamily = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

static void VXAbstractAxis_copy(XAbstractAxis* self, const XAbstractAxis* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractAxis_init(self);
    XClass_Parent(XObject, EXClass_Copy,
                  void(*)(XObject*, const XObject*))((XObject*)self,
                                                     (const XObject*)other);
    self->m_visible = other->m_visible;
    self->m_lineVisible = other->m_lineVisible;
    self->m_gridLineVisible = other->m_gridLineVisible;
    self->m_minorGridLineVisible = other->m_minorGridLineVisible;
    self->m_labelsVisible = other->m_labelsVisible;
    self->m_titleVisible = other->m_titleVisible;
    self->m_labelsEditable = other->m_labelsEditable;
    self->m_truncateLabels = other->m_truncateLabels;
    self->m_min = other->m_min;
    self->m_max = other->m_max;
    self->m_reverse = other->m_reverse;
    self->m_labelsAngle = other->m_labelsAngle;
    self->m_shadesVisible = other->m_shadesVisible;
    self->m_linePenColor = other->m_linePenColor;
    self->m_linePenWidth = other->m_linePenWidth;
    self->m_gridLinePenColor = other->m_gridLinePenColor;
    self->m_gridLinePenWidth = other->m_gridLinePenWidth;
    self->m_minorGridLinePenColor = other->m_minorGridLinePenColor;
    self->m_minorGridLinePenWidth = other->m_minorGridLinePenWidth;
    self->m_labelsBrushColor = other->m_labelsBrushColor;
    self->m_labelsColor = other->m_labelsColor;
    self->m_labelsFontSize = other->m_labelsFontSize;
    self->m_titleBrushColor = other->m_titleBrushColor;
    self->m_titleFontSize = other->m_titleFontSize;
    self->m_shadesPenColor = other->m_shadesPenColor;
    self->m_shadesPenWidth = other->m_shadesPenWidth;
    self->m_shadesBrushColor = other->m_shadesBrushColor;
    if (other->m_titleText)
        XAbstractAxis_setTitleText(self, other->m_titleText);
    else if (self->m_titleText)
        XString_assign_utf8(self->m_titleText, "");
    if (other->m_labelsFontFamily)
        XAbstractAxis_setLabelsFont(self, other->m_labelsFontFamily,
                                    other->m_labelsFontSize);
    if (other->m_titleFontFamily)
        XAbstractAxis_setTitleFont(self, other->m_titleFontFamily,
                                   other->m_titleFontSize);
}

static void VXAbstractAxis_move(XAbstractAxis* self, XAbstractAxis* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractAxis_init(self);
    XClass_Parent(XObject, EXClass_Move,
                  void(*)(XObject*, XObject*))((XObject*)self,
                                               (XObject*)other);
    self->m_visible = other->m_visible;
    self->m_lineVisible = other->m_lineVisible;
    self->m_gridLineVisible = other->m_gridLineVisible;
    self->m_minorGridLineVisible = other->m_minorGridLineVisible;
    self->m_labelsVisible = other->m_labelsVisible;
    self->m_titleVisible = other->m_titleVisible;
    self->m_labelsEditable = other->m_labelsEditable;
    self->m_truncateLabels = other->m_truncateLabels;
    self->m_min = other->m_min;
    self->m_max = other->m_max;
    self->m_reverse = other->m_reverse;
    self->m_labelsAngle = other->m_labelsAngle;
    self->m_shadesVisible = other->m_shadesVisible;
    self->m_linePenColor = other->m_linePenColor;
    self->m_linePenWidth = other->m_linePenWidth;
    self->m_gridLinePenColor = other->m_gridLinePenColor;
    self->m_gridLinePenWidth = other->m_gridLinePenWidth;
    self->m_minorGridLinePenColor = other->m_minorGridLinePenColor;
    self->m_minorGridLinePenWidth = other->m_minorGridLinePenWidth;
    self->m_labelsBrushColor = other->m_labelsBrushColor;
    self->m_labelsColor = other->m_labelsColor;
    self->m_labelsFontSize = other->m_labelsFontSize;
    self->m_titleBrushColor = other->m_titleBrushColor;
    self->m_titleFontSize = other->m_titleFontSize;
    self->m_shadesPenColor = other->m_shadesPenColor;
    self->m_shadesPenWidth = other->m_shadesPenWidth;
    self->m_shadesBrushColor = other->m_shadesBrushColor;
    if (self->m_titleText) XClassDelete(self->m_titleText);
    self->m_titleText = other->m_titleText;
    other->m_titleText = XString_create();
    if (self->m_labelsFontFamily) XClassDelete(self->m_labelsFontFamily);
    self->m_labelsFontFamily = other->m_labelsFontFamily;
    other->m_labelsFontFamily = XString_create();
    if (self->m_titleFontFamily) XClassDelete(self->m_titleFontFamily);
    self->m_titleFontFamily = other->m_titleFontFamily;
    other->m_titleFontFamily = XString_create();
    /* 复原 other 为默认轴状态。 */
    other->m_visible = true;
    other->m_lineVisible = true;
    other->m_gridLineVisible = true;
    other->m_minorGridLineVisible = false;
    other->m_labelsVisible = true;
    other->m_titleVisible = true;
    other->m_labelsEditable = false;
    other->m_truncateLabels = false;
    other->m_min = 0.0;
    other->m_max = 10.0;
    other->m_reverse = false;
    other->m_labelsAngle = 0;
    other->m_shadesVisible = false;
    other->m_linePenColor = 0;
    other->m_linePenWidth = 0.0;
    other->m_gridLinePenColor = 0;
    other->m_gridLinePenWidth = 0.0;
    other->m_minorGridLinePenColor = 0;
    other->m_minorGridLinePenWidth = 0.0;
    other->m_labelsBrushColor = 0;
    other->m_labelsColor = 0;
    other->m_labelsFontSize = 0;
    other->m_titleBrushColor = 0;
    other->m_titleFontSize = 0;
    other->m_shadesPenColor = 0;
    other->m_shadesPenWidth = 0.0;
    other->m_shadesBrushColor = 0;
}

/* ==================== 类初始化 ==================== */

XVtable* XAbstractAxis_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractAxis)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractAxis_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXAbstractAxis_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXAbstractAxis_move);
    return XVTABLE_DEFAULT;
}

void XAbstractAxis_init(XAbstractAxis* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XAbstractAxis);
    self->m_visible = true;
    self->m_lineVisible = true;
    self->m_gridLineVisible = true;
    self->m_minorGridLineVisible = false;
    self->m_labelsVisible = true;
    self->m_titleVisible = true;
    self->m_labelsEditable = false;
    self->m_truncateLabels = false;
    self->m_min = 0.0;
    self->m_max = 10.0;
    self->m_reverse = false;
    self->m_titleText = XString_create();
    self->m_labelsFontFamily = XString_create();
    self->m_titleFontFamily = XString_create();
    self->m_labelsAngle = 0;
    self->m_shadesVisible = false;
    self->m_linePenColor = 0;
    self->m_linePenWidth = 0.0;
    self->m_gridLinePenColor = 0;
    self->m_gridLinePenWidth = 0.0;
    self->m_minorGridLinePenColor = 0;
    self->m_minorGridLinePenWidth = 0.0;
    self->m_labelsBrushColor = 0;
    self->m_labelsColor = 0;
    self->m_labelsFontSize = 0;
    self->m_titleBrushColor = 0;
    self->m_titleFontSize = 0;
    self->m_shadesPenColor = 0;
    self->m_shadesPenWidth = 0.0;
    self->m_shadesBrushColor = 0;
    self->m_axisType = XAbstractAxis_AxisType_NoAxis;
    self->m_axisAlign = 0;
    self->m_axisOrientation = 0;
}

XAbstractAxis* XAbstractAxis_create_ex(XMemoryType memory)
{
    XAbstractAxis* self =
        (XAbstractAxis*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XAbstractAxis_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 可见性与轴线 ==================== */

void XAbstractAxis_setVisible(XAbstractAxis* self, bool visible)
{
    if (!self || self->m_visible == visible) return;
    self->m_visible = visible;
    xaxis_emitBool(self, (size_t)XAbstractAxis_visibleChanged_signal, visible);
}

bool XAbstractAxis_isVisible(const XAbstractAxis* self)
{ return self ? self->m_visible : false; }

void XAbstractAxis_show(XAbstractAxis* self)
{ XAbstractAxis_setVisible(self, true); }

void XAbstractAxis_hide(XAbstractAxis* self)
{ XAbstractAxis_setVisible(self, false); }

void XAbstractAxis_setLineVisible(XAbstractAxis* self, bool visible)
{
    if (!self || self->m_lineVisible == visible) return;
    self->m_lineVisible = visible;
    xaxis_emitBool(self, (size_t)XAbstractAxis_lineVisibleChanged_signal,
                   visible);
}

bool XAbstractAxis_isLineVisible(const XAbstractAxis* self)
{ return self ? self->m_lineVisible : false; }

void XAbstractAxis_setLinePen(XAbstractAxis* self, uint32_t color, double width)
{
    if (!self) return;
    if (self->m_linePenColor == color && self->m_linePenWidth == width) return;
    self->m_linePenColor = color;
    self->m_linePenWidth = (width < 0.0) ? 0.0 : width;
    xaxis_emitPen(self, (size_t)XAbstractAxis_linePenChanged_signal,
                  self->m_linePenColor, self->m_linePenWidth);
    xaxis_emitU32(self, (size_t)XAbstractAxis_colorChanged_signal, color);
}

void XAbstractAxis_linePen(const XAbstractAxis* self, uint32_t* color,
                           double* width)
{
    if (!self) return;
    if (color) *color = self->m_linePenColor;
    if (width) *width = self->m_linePenWidth;
}

void XAbstractAxis_setLinePenColor(XAbstractAxis* self, uint32_t color)
{
    if (!self || self->m_linePenColor == color) return;
    self->m_linePenColor = color;
    xaxis_emitPen(self, (size_t)XAbstractAxis_linePenChanged_signal,
                  self->m_linePenColor, self->m_linePenWidth);
    xaxis_emitU32(self, (size_t)XAbstractAxis_colorChanged_signal, color);
}

uint32_t XAbstractAxis_linePenColor(const XAbstractAxis* self)
{ return self ? self->m_linePenColor : 0; }

/* ==================== 网格线 ==================== */

void XAbstractAxis_setGridLineVisible(XAbstractAxis* self, bool visible)
{
    if (!self || self->m_gridLineVisible == visible) return;
    self->m_gridLineVisible = visible;
    xaxis_emitBool(self, (size_t)XAbstractAxis_gridVisibleChanged_signal,
                   visible);
}

bool XAbstractAxis_isGridLineVisible(const XAbstractAxis* self)
{ return self ? self->m_gridLineVisible : false; }

void XAbstractAxis_setGridLinePen(XAbstractAxis* self, uint32_t color,
                                  double width)
{
    if (!self) return;
    if (self->m_gridLinePenColor == color &&
        self->m_gridLinePenWidth == width) return;
    self->m_gridLinePenColor = color;
    self->m_gridLinePenWidth = (width < 0.0) ? 0.0 : width;
    xaxis_emitPen(self, (size_t)XAbstractAxis_gridLinePenChanged_signal,
                  color, self->m_gridLinePenWidth);
    xaxis_emitU32(self, (size_t)XAbstractAxis_gridLineColorChanged_signal,
                  color);
}

void XAbstractAxis_gridLinePen(const XAbstractAxis* self, uint32_t* color,
                               double* width)
{
    if (!self) return;
    if (color) *color = self->m_gridLinePenColor;
    if (width) *width = self->m_gridLinePenWidth;
}

void XAbstractAxis_setGridLineColor(XAbstractAxis* self, uint32_t color)
{
    XAbstractAxis_setGridLinePen(self, color, self ? self->m_gridLinePenWidth : 0.0);
}

uint32_t XAbstractAxis_gridLineColor(const XAbstractAxis* self)
{ return self ? self->m_gridLinePenColor : 0; }

void XAbstractAxis_setMinorGridLineVisible(XAbstractAxis* self, bool visible)
{
    if (!self || self->m_minorGridLineVisible == visible) return;
    self->m_minorGridLineVisible = visible;
    xaxis_emitBool(self, (size_t)XAbstractAxis_minorGridVisibleChanged_signal,
                   visible);
}

bool XAbstractAxis_isMinorGridLineVisible(const XAbstractAxis* self)
{ return self ? self->m_minorGridLineVisible : false; }

void XAbstractAxis_setMinorGridLinePen(XAbstractAxis* self, uint32_t color,
                                       double width)
{
    if (!self) return;
    if (self->m_minorGridLinePenColor == color &&
        self->m_minorGridLinePenWidth == width) return;
    self->m_minorGridLinePenColor = color;
    self->m_minorGridLinePenWidth = (width < 0.0) ? 0.0 : width;
    xaxis_emitPen(self, (size_t)XAbstractAxis_minorGridLinePenChanged_signal,
                  color, self->m_minorGridLinePenWidth);
    xaxis_emitU32(self,
                  (size_t)XAbstractAxis_minorGridLineColorChanged_signal,
                  color);
}

void XAbstractAxis_minorGridLinePen(const XAbstractAxis* self, uint32_t* color,
                                    double* width)
{
    if (!self) return;
    if (color) *color = self->m_minorGridLinePenColor;
    if (width) *width = self->m_minorGridLinePenWidth;
}

void XAbstractAxis_setMinorGridLineColor(XAbstractAxis* self, uint32_t color)
{
    XAbstractAxis_setMinorGridLinePen(self, color,
        self ? self->m_minorGridLinePenWidth : 0.0);
}

uint32_t XAbstractAxis_minorGridLineColor(const XAbstractAxis* self)
{ return self ? self->m_minorGridLinePenColor : 0; }

/* ==================== 刻度标签 ==================== */

void XAbstractAxis_setLabelsVisible(XAbstractAxis* self, bool visible)
{
    if (!self || self->m_labelsVisible == visible) return;
    self->m_labelsVisible = visible;
    xaxis_emitBool(self, (size_t)XAbstractAxis_labelsVisibleChanged_signal,
                   visible);
}

bool XAbstractAxis_labelsVisible(const XAbstractAxis* self)
{ return self ? self->m_labelsVisible : false; }

void XAbstractAxis_setLabelsBrush(XAbstractAxis* self, uint32_t color)
{
    if (!self || self->m_labelsBrushColor == color) return;
    self->m_labelsBrushColor = color;
    xaxis_emitU32(self, (size_t)XAbstractAxis_labelsBrushChanged_signal,
                  color);
}

uint32_t XAbstractAxis_labelsBrush(const XAbstractAxis* self)
{ return self ? self->m_labelsBrushColor : 0; }

void XAbstractAxis_setLabelsFont(XAbstractAxis* self, const XString* family,
                                 int pointSize)
{
    const char* fam;
    if (!self) return;
    if (!self->m_labelsFontFamily) self->m_labelsFontFamily = XString_create();
    if (self->m_labelsFontFamily) {
        if (family)
            XString_assign(self->m_labelsFontFamily, family);
        else
            XString_assign_utf8(self->m_labelsFontFamily, "");
        fam = XString_toUtf8(self->m_labelsFontFamily);
    } else {
        fam = "";
    }
    self->m_labelsFontSize = (pointSize < 0) ? 0 : pointSize;
    xaxis_emitFont(self, (size_t)XAbstractAxis_labelsFontChanged_signal,
                   fam, self->m_labelsFontSize);
}

void XAbstractAxis_setLabelsFont_utf8(XAbstractAxis* self, const char* family,
                                      int pointSize)
{
    XString* tmp = NULL;
    if (family) {
        tmp = XString_create_utf8(family);
        if (!tmp) return;
    }
    XAbstractAxis_setLabelsFont(self, tmp, pointSize);
    if (tmp) XClassDelete(tmp);
}

const XString* XAbstractAxis_labelsFontFamily_const(const XAbstractAxis* self)
{
    return (self && self->m_labelsFontFamily) ? self->m_labelsFontFamily : NULL;
}

const char* XAbstractAxis_labelsFontFamily_2(const XAbstractAxis* self)
{
    const XString* s = XAbstractAxis_labelsFontFamily_const(self);
    return s ? XString_toUtf8(s) : "";
}

int XAbstractAxis_labelsFontSize(const XAbstractAxis* self)
{ return self ? self->m_labelsFontSize : 0; }

void XAbstractAxis_labelsFont(const XAbstractAxis* self,
                              const XString** family, int* pointSize)
{
    if (!self) return;
    if (family) *family = self->m_labelsFontFamily;
    if (pointSize) *pointSize = self->m_labelsFontSize;
}

void XAbstractAxis_setLabelsAngle(XAbstractAxis* self, int angle)
{
    if (!self || self->m_labelsAngle == angle) return;
    self->m_labelsAngle = angle;
    xaxis_emitInt(self, (size_t)XAbstractAxis_labelsAngleChanged_signal, angle);
}

int XAbstractAxis_labelsAngle(const XAbstractAxis* self)
{ return self ? self->m_labelsAngle : 0; }

void XAbstractAxis_setLabelsColor(XAbstractAxis* self, uint32_t color)
{
    if (!self || self->m_labelsColor == color) return;
    self->m_labelsColor = color;
    xaxis_emitU32(self, (size_t)XAbstractAxis_labelsColorChanged_signal, color);
}

uint32_t XAbstractAxis_labelsColor(const XAbstractAxis* self)
{ return self ? self->m_labelsColor : 0; }

void XAbstractAxis_setLabelsEditable(XAbstractAxis* self, bool editable)
{
    if (!self || self->m_labelsEditable == editable) return;
    self->m_labelsEditable = editable;
    xaxis_emitBool(self, (size_t)XAbstractAxis_labelsEditableChanged_signal,
                   editable);
}

bool XAbstractAxis_labelsEditable(const XAbstractAxis* self)
{ return self ? self->m_labelsEditable : false; }

bool XAbstractAxis_labelsTruncated(const XAbstractAxis* self)
{
    /* Qt：labelsTruncated 为只读派生属性——仅在 truncateLabels 使能时
     * 才可能为真（真正是否截断取决于布局宽度，此处按可截断状态回报）。 */
    return self ? (self->m_truncateLabels && self->m_labelsVisible) : false;
}

void XAbstractAxis_setTruncateLabels(XAbstractAxis* self, bool truncate)
{
    bool wasTruncated;
    if (!self || self->m_truncateLabels == truncate) return;
    wasTruncated = XAbstractAxis_labelsTruncated(self);
    self->m_truncateLabels = truncate;
    xaxis_emitBool(self, (size_t)XAbstractAxis_truncateLabelsChanged_signal,
                   truncate);
    if (wasTruncated != XAbstractAxis_labelsTruncated(self))
        xaxis_emitBool(self,
                       (size_t)XAbstractAxis_labelsTruncatedChanged_signal,
                       XAbstractAxis_labelsTruncated(self));
}

bool XAbstractAxis_truncateLabels(const XAbstractAxis* self)
{ return self ? self->m_truncateLabels : false; }

/* ==================== 轴标题 ==================== */

void XAbstractAxis_setTitleVisible(XAbstractAxis* self, bool visible)
{
    if (!self || self->m_titleVisible == visible) return;
    self->m_titleVisible = visible;
    xaxis_emitBool(self, (size_t)XAbstractAxis_titleVisibleChanged_signal,
                   visible);
}

bool XAbstractAxis_isTitleVisible(const XAbstractAxis* self)
{ return self ? self->m_titleVisible : false; }

void XAbstractAxis_setTitleBrush(XAbstractAxis* self, uint32_t color)
{
    if (!self || self->m_titleBrushColor == color) return;
    self->m_titleBrushColor = color;
    xaxis_emitU32(self, (size_t)XAbstractAxis_titleBrushChanged_signal, color);
}

uint32_t XAbstractAxis_titleBrush(const XAbstractAxis* self)
{ return self ? self->m_titleBrushColor : 0; }

void XAbstractAxis_setTitleFont(XAbstractAxis* self, const XString* family,
                                int pointSize)
{
    const char* fam;
    if (!self) return;
    if (!self->m_titleFontFamily) self->m_titleFontFamily = XString_create();
    if (self->m_titleFontFamily) {
        if (family)
            XString_assign(self->m_titleFontFamily, family);
        else
            XString_assign_utf8(self->m_titleFontFamily, "");
        fam = XString_toUtf8(self->m_titleFontFamily);
    } else {
        fam = "";
    }
    self->m_titleFontSize = (pointSize < 0) ? 0 : pointSize;
    xaxis_emitFont(self, (size_t)XAbstractAxis_titleFontChanged_signal,
                   fam, self->m_titleFontSize);
}

void XAbstractAxis_setTitleFont_utf8(XAbstractAxis* self, const char* family,
                                     int pointSize)
{
    XString* tmp = NULL;
    if (family) {
        tmp = XString_create_utf8(family);
        if (!tmp) return;
    }
    XAbstractAxis_setTitleFont(self, tmp, pointSize);
    if (tmp) XClassDelete(tmp);
}

const XString* XAbstractAxis_titleFontFamily_const(const XAbstractAxis* self)
{
    return (self && self->m_titleFontFamily) ? self->m_titleFontFamily : NULL;
}

const char* XAbstractAxis_titleFontFamily_2(const XAbstractAxis* self)
{
    const XString* s = XAbstractAxis_titleFontFamily_const(self);
    return s ? XString_toUtf8(s) : "";
}

int XAbstractAxis_titleFontSize(const XAbstractAxis* self)
{ return self ? self->m_titleFontSize : 0; }

void XAbstractAxis_titleFont(const XAbstractAxis* self,
                             const XString** family, int* pointSize)
{
    if (!self) return;
    if (family) *family = self->m_titleFontFamily;
    if (pointSize) *pointSize = self->m_titleFontSize;
}

void XAbstractAxis_setTitleText(XAbstractAxis* self, const XString* title)
{
    if (!self) return;
    if (!self->m_titleText) self->m_titleText = XString_create();
    if (!self->m_titleText) return;
    if (title)
        XString_assign(self->m_titleText, title);
    else
        XString_assign_utf8(self->m_titleText, "");
    xaxis_emitText(self, (size_t)XAbstractAxis_titleTextChanged_signal,
                   self->m_titleText);
}

void XAbstractAxis_setTitleText_utf8(XAbstractAxis* self, const char* title)
{
    XString* tmp = NULL;
    if (title) {
        tmp = XString_create_utf8(title);
        if (!tmp) return;
    }
    XAbstractAxis_setTitleText(self, tmp);
    if (tmp) XClassDelete(tmp);
}

const XString* XAbstractAxis_titleText_const(const XAbstractAxis* self)
{
    return (self && self->m_titleText) ? self->m_titleText : NULL;
}

const char* XAbstractAxis_titleText_2(const XAbstractAxis* self)
{
    const XString* s = XAbstractAxis_titleText_const(self);
    return s ? XString_toUtf8(s) : "";
}

/* ==================== 阴影带 ==================== */

void XAbstractAxis_setShadesVisible(XAbstractAxis* self, bool visible)
{
    if (!self || self->m_shadesVisible == visible) return;
    self->m_shadesVisible = visible;
    xaxis_emitBool(self, (size_t)XAbstractAxis_shadesVisibleChanged_signal,
                   visible);
}

bool XAbstractAxis_shadesVisible(const XAbstractAxis* self)
{ return self ? self->m_shadesVisible : false; }

void XAbstractAxis_setShadesPen(XAbstractAxis* self, uint32_t color,
                                double width)
{
    if (!self) return;
    if (self->m_shadesPenColor == color && self->m_shadesPenWidth == width)
        return;
    self->m_shadesPenColor = color;
    self->m_shadesPenWidth = (width < 0.0) ? 0.0 : width;
    xaxis_emitPen(self, (size_t)XAbstractAxis_shadesPenChanged_signal,
                  color, self->m_shadesPenWidth);
    xaxis_emitU32(self, (size_t)XAbstractAxis_shadesBorderColorChanged_signal,
                  color);
}

void XAbstractAxis_shadesPen(const XAbstractAxis* self, uint32_t* color,
                             double* width)
{
    if (!self) return;
    if (color) *color = self->m_shadesPenColor;
    if (width) *width = self->m_shadesPenWidth;
}

void XAbstractAxis_setShadesBrush(XAbstractAxis* self, uint32_t color)
{
    if (!self || self->m_shadesBrushColor == color) return;
    self->m_shadesBrushColor = color;
    xaxis_emitU32(self, (size_t)XAbstractAxis_shadesBrushChanged_signal, color);
}

uint32_t XAbstractAxis_shadesBrush(const XAbstractAxis* self)
{ return self ? self->m_shadesBrushColor : 0; }

void XAbstractAxis_setShadesColor(XAbstractAxis* self, uint32_t color)
{
    if (!self || self->m_shadesBrushColor == color) return;
    self->m_shadesBrushColor = color;
    xaxis_emitU32(self, (size_t)XAbstractAxis_shadesColorChanged_signal, color);
    xaxis_emitU32(self, (size_t)XAbstractAxis_shadesBrushChanged_signal, color);
}

uint32_t XAbstractAxis_shadesColor(const XAbstractAxis* self)
{ return self ? self->m_shadesBrushColor : 0; }

void XAbstractAxis_setShadesBorderColor(XAbstractAxis* self, uint32_t color)
{
    double w = self ? self->m_shadesPenWidth : 0.0;
    if (!self || self->m_shadesPenColor == color) return;
    self->m_shadesPenColor = color;
    xaxis_emitU32(self, (size_t)XAbstractAxis_shadesBorderColorChanged_signal,
                  color);
    xaxis_emitPen(self, (size_t)XAbstractAxis_shadesPenChanged_signal,
                  color, w);
}

uint32_t XAbstractAxis_shadesBorderColor(const XAbstractAxis* self)
{ return self ? self->m_shadesPenColor : 0; }

/* ==================== 方向 / 范围 / 反转 ==================== */

int XAbstractAxis_orientation(const XAbstractAxis* self)
{
    /* 由图表在挂接时写入（0=水平 1=垂直）；未挂接返回 0。 */
    return self ? (int)self->m_axisOrientation : 0;
}

uint32_t XAbstractAxis_alignment(const XAbstractAxis* self)
{
    /* 由图表在挂接时写入（Qt::Alignment 位集合）；未挂接返回 0。 */
    return self ? (uint32_t)self->m_axisAlign : 0;
}

void XAbstractAxis_setOrientation(XAbstractAxis* self, int orientation)
{
    if (self) self->m_axisOrientation = (uint8_t)orientation;
}

void XAbstractAxis_setAlignment(XAbstractAxis* self, uint32_t alignment)
{
    if (self) self->m_axisAlign = (uint8_t)alignment;
}

void XAbstractAxis_setMin(XAbstractAxis* self, double min)
{
    if (!self) return;
    XAbstractAxis_setRange(self, min, self->m_max);
}

void XAbstractAxis_setMax(XAbstractAxis* self, double max)
{
    if (!self) return;
    XAbstractAxis_setRange(self, self->m_min, max);
}

void XAbstractAxis_setRange(XAbstractAxis* self, double min, double max)
{
    if (!self || min > max) return;
    if (self->m_min == min && self->m_max == max) return;
    self->m_min = min;
    self->m_max = max;
    xaxis_emitRange(self, (size_t)XAbstractAxis_rangeChanged_signal, min, max);
}

double XAbstractAxis_min(const XAbstractAxis* self)
{ return self ? self->m_min : 0.0; }

double XAbstractAxis_max(const XAbstractAxis* self)
{ return self ? self->m_max : 0.0; }

void XAbstractAxis_setReverse(XAbstractAxis* self, bool reverse)
{
    if (!self || self->m_reverse == reverse) return;
    self->m_reverse = reverse;
    xaxis_emitBool(self, (size_t)XAbstractAxis_reverseChanged_signal, reverse);
}

bool XAbstractAxis_isReverse(const XAbstractAxis* self)
{ return self ? self->m_reverse : false; }

/* ==================== 类型查询 ==================== */

int XAbstractAxis_type(const XAbstractAxis* self)
{
    /* 对标 QAbstractAxis::type() 纯虚：由派生类 init 写入 m_axisType，
     * 经基类指针调用即得实际类型（C 版无虚函数，以字段承载分派结果）。 */
    return self ? (int)self->m_axisType : XAbstractAxis_AxisType_NoAxis;
}

/* ==================== 信号 ==================== */

void* XAbstractAxis_visibleChanged_signal(XAbstractAxis* self, bool visible)
{
    xaxis_emitBool(self, (size_t)XAbstractAxis_visibleChanged_signal, visible);
    return (void*)(size_t)XAbstractAxis_visibleChanged_signal;
}

void* XAbstractAxis_lineVisibleChanged_signal(XAbstractAxis* self, bool visible)
{
    xaxis_emitBool(self, (size_t)XAbstractAxis_lineVisibleChanged_signal,
                   visible);
    return (void*)(size_t)XAbstractAxis_lineVisibleChanged_signal;
}

void* XAbstractAxis_linePenChanged_signal(XAbstractAxis* self, uint32_t color,
                                          double width)
{
    xaxis_emitPen(self, (size_t)XAbstractAxis_linePenChanged_signal, color,
                  width);
    return (void*)(size_t)XAbstractAxis_linePenChanged_signal;
}

void* XAbstractAxis_colorChanged_signal(XAbstractAxis* self, uint32_t color)
{
    xaxis_emitU32(self, (size_t)XAbstractAxis_colorChanged_signal, color);
    return (void*)(size_t)XAbstractAxis_colorChanged_signal;
}

void* XAbstractAxis_labelsVisibleChanged_signal(XAbstractAxis* self,
                                                bool visible)
{
    xaxis_emitBool(self, (size_t)XAbstractAxis_labelsVisibleChanged_signal,
                   visible);
    return (void*)(size_t)XAbstractAxis_labelsVisibleChanged_signal;
}

void* XAbstractAxis_labelsBrushChanged_signal(XAbstractAxis* self,
                                              uint32_t color)
{
    xaxis_emitU32(self, (size_t)XAbstractAxis_labelsBrushChanged_signal, color);
    return (void*)(size_t)XAbstractAxis_labelsBrushChanged_signal;
}

void* XAbstractAxis_labelsFontChanged_signal(XAbstractAxis* self,
                                             const char* family,
                                             int pointSize)
{
    xaxis_emitFont(self, (size_t)XAbstractAxis_labelsFontChanged_signal,
                   family, pointSize);
    return (void*)(size_t)XAbstractAxis_labelsFontChanged_signal;
}

void* XAbstractAxis_labelsAngleChanged_signal(XAbstractAxis* self, int angle)
{
    xaxis_emitInt(self, (size_t)XAbstractAxis_labelsAngleChanged_signal, angle);
    return (void*)(size_t)XAbstractAxis_labelsAngleChanged_signal;
}

void* XAbstractAxis_labelsColorChanged_signal(XAbstractAxis* self,
                                              uint32_t color)
{
    xaxis_emitU32(self, (size_t)XAbstractAxis_labelsColorChanged_signal, color);
    return (void*)(size_t)XAbstractAxis_labelsColorChanged_signal;
}

void* XAbstractAxis_labelsEditableChanged_signal(XAbstractAxis* self,
                                                 bool editable)
{
    xaxis_emitBool(self, (size_t)XAbstractAxis_labelsEditableChanged_signal,
                   editable);
    return (void*)(size_t)XAbstractAxis_labelsEditableChanged_signal;
}

void* XAbstractAxis_labelsTruncatedChanged_signal(XAbstractAxis* self,
                                                  bool truncated)
{
    xaxis_emitBool(self, (size_t)XAbstractAxis_labelsTruncatedChanged_signal,
                   truncated);
    return (void*)(size_t)XAbstractAxis_labelsTruncatedChanged_signal;
}

void* XAbstractAxis_truncateLabelsChanged_signal(XAbstractAxis* self,
                                                 bool truncate)
{
    xaxis_emitBool(self, (size_t)XAbstractAxis_truncateLabelsChanged_signal,
                   truncate);
    return (void*)(size_t)XAbstractAxis_truncateLabelsChanged_signal;
}

void* XAbstractAxis_gridVisibleChanged_signal(XAbstractAxis* self, bool visible)
{
    xaxis_emitBool(self, (size_t)XAbstractAxis_gridVisibleChanged_signal,
                   visible);
    return (void*)(size_t)XAbstractAxis_gridVisibleChanged_signal;
}

void* XAbstractAxis_gridLinePenChanged_signal(XAbstractAxis* self,
                                              uint32_t color, double width)
{
    xaxis_emitPen(self, (size_t)XAbstractAxis_gridLinePenChanged_signal,
                  color, width);
    return (void*)(size_t)XAbstractAxis_gridLinePenChanged_signal;
}

void* XAbstractAxis_gridLineColorChanged_signal(XAbstractAxis* self,
                                                uint32_t color)
{
    xaxis_emitU32(self, (size_t)XAbstractAxis_gridLineColorChanged_signal,
                  color);
    return (void*)(size_t)XAbstractAxis_gridLineColorChanged_signal;
}

void* XAbstractAxis_minorGridVisibleChanged_signal(XAbstractAxis* self,
                                                   bool visible)
{
    xaxis_emitBool(self, (size_t)XAbstractAxis_minorGridVisibleChanged_signal,
                   visible);
    return (void*)(size_t)XAbstractAxis_minorGridVisibleChanged_signal;
}

void* XAbstractAxis_minorGridLinePenChanged_signal(XAbstractAxis* self,
                                                   uint32_t color,
                                                   double width)
{
    xaxis_emitPen(self, (size_t)XAbstractAxis_minorGridLinePenChanged_signal,
                  color, width);
    return (void*)(size_t)XAbstractAxis_minorGridLinePenChanged_signal;
}

void* XAbstractAxis_minorGridLineColorChanged_signal(XAbstractAxis* self,
                                                     uint32_t color)
{
    xaxis_emitU32(self,
                  (size_t)XAbstractAxis_minorGridLineColorChanged_signal,
                  color);
    return (void*)(size_t)XAbstractAxis_minorGridLineColorChanged_signal;
}

void* XAbstractAxis_shadesVisibleChanged_signal(XAbstractAxis* self,
                                                bool visible)
{
    xaxis_emitBool(self, (size_t)XAbstractAxis_shadesVisibleChanged_signal,
                   visible);
    return (void*)(size_t)XAbstractAxis_shadesVisibleChanged_signal;
}

void* XAbstractAxis_shadesPenChanged_signal(XAbstractAxis* self,
                                            uint32_t color, double width)
{
    xaxis_emitPen(self, (size_t)XAbstractAxis_shadesPenChanged_signal,
                  color, width);
    return (void*)(size_t)XAbstractAxis_shadesPenChanged_signal;
}

void* XAbstractAxis_shadesBrushChanged_signal(XAbstractAxis* self,
                                              uint32_t color)
{
    xaxis_emitU32(self, (size_t)XAbstractAxis_shadesBrushChanged_signal, color);
    return (void*)(size_t)XAbstractAxis_shadesBrushChanged_signal;
}

void* XAbstractAxis_shadesColorChanged_signal(XAbstractAxis* self,
                                              uint32_t color)
{
    xaxis_emitU32(self, (size_t)XAbstractAxis_shadesColorChanged_signal, color);
    return (void*)(size_t)XAbstractAxis_shadesColorChanged_signal;
}

void* XAbstractAxis_shadesBorderColorChanged_signal(XAbstractAxis* self,
                                                    uint32_t color)
{
    xaxis_emitU32(self,
                  (size_t)XAbstractAxis_shadesBorderColorChanged_signal, color);
    return (void*)(size_t)XAbstractAxis_shadesBorderColorChanged_signal;
}

void* XAbstractAxis_titleTextChanged_signal(XAbstractAxis* self,
                                            const XString* title)
{
    xaxis_emitText(self, (size_t)XAbstractAxis_titleTextChanged_signal, title);
    return (void*)(size_t)XAbstractAxis_titleTextChanged_signal;
}

void* XAbstractAxis_titleBrushChanged_signal(XAbstractAxis* self,
                                             uint32_t color)
{
    xaxis_emitU32(self, (size_t)XAbstractAxis_titleBrushChanged_signal, color);
    return (void*)(size_t)XAbstractAxis_titleBrushChanged_signal;
}

void* XAbstractAxis_titleVisibleChanged_signal(XAbstractAxis* self,
                                               bool visible)
{
    xaxis_emitBool(self, (size_t)XAbstractAxis_titleVisibleChanged_signal,
                   visible);
    return (void*)(size_t)XAbstractAxis_titleVisibleChanged_signal;
}

void* XAbstractAxis_titleFontChanged_signal(XAbstractAxis* self,
                                            const char* family,
                                            int pointSize)
{
    xaxis_emitFont(self, (size_t)XAbstractAxis_titleFontChanged_signal,
                   family, pointSize);
    return (void*)(size_t)XAbstractAxis_titleFontChanged_signal;
}

void* XAbstractAxis_rangeChanged_signal(XAbstractAxis* self,
                                        double min, double max)
{
    xaxis_emitRange(self, (size_t)XAbstractAxis_rangeChanged_signal, min, max);
    return (void*)(size_t)XAbstractAxis_rangeChanged_signal;
}

void* XAbstractAxis_reverseChanged_signal(XAbstractAxis* self, bool reverse)
{
    xaxis_emitBool(self, (size_t)XAbstractAxis_reverseChanged_signal, reverse);
    return (void*)(size_t)XAbstractAxis_reverseChanged_signal;
}

/* ==================== UTF-8 兼容别名（_2，转发 _utf8 版本） ==================== */

void XAbstractAxis_setLabelsFont_2(XAbstractAxis* self, const char* family,
                                   int pointSize)
{ XAbstractAxis_setLabelsFont_utf8(self, family, pointSize); }

void XAbstractAxis_setTitleFont_2(XAbstractAxis* self, const char* family,
                                  int pointSize)
{ XAbstractAxis_setTitleFont_utf8(self, family, pointSize); }

void XAbstractAxis_setTitleText_2(XAbstractAxis* self, const char* title)
{ XAbstractAxis_setTitleText_utf8(self, title); }

/* ==================== 借用 getter 兼容别名（无 _const，转发 _const 版） ==================== */

const XString* XAbstractAxis_labelsFontFamily(const XAbstractAxis* self)
{ return XAbstractAxis_labelsFontFamily_const(self); }

const XString* XAbstractAxis_titleFontFamily(const XAbstractAxis* self)
{ return XAbstractAxis_titleFontFamily_const(self); }

const XString* XAbstractAxis_titleText(const XAbstractAxis* self)
{ return XAbstractAxis_titleText_const(self); }

#endif /* XCHARTS_ON */

#endif /* XGUI_ON */
