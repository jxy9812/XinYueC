/**
 * @file       XProgressBar.c
 * @brief      XProgressBar 进度条控件实现（对标 Qt 6.8 QProgressBar）。
 * @details    绘制分层：
 *             - 凹陷凹槽：调色板 Dark（上/左）与 Light（下/右）1px
 *               描边 + Base 底色填充（无调色板能力时退化为深灰描边）；
 *             - 进度块：Highlight 色，按 value 在 [min,max] 的比例沿
 *               方向填充（invertedAppearance 翻转生长端）；
 *             - 文本：按 format 替换 %p/%v/%m 后沿 alignment 绘制，
 *               并按进度块边界分两段裁剪着色（块内 HighlightedText、
 *               块外 WindowText，对标 Qt 的分段文本）。
 *             数值语义：setValue 越界钳位到 [min,max]，实际变化才发射
 *             valueChanged(int) 并 update()；setRange 交换 min>max；
 *             setMinimum/setMaximum 按 Qt 语义联动另一端端点。垂直进度
 *             条文本按 textDirection 旋转 90°（TopToBottom 自上而下、
 *             BottomToTop 自下而上），单色居中（分段裁剪为后续扩展）。
 *             事件入口 event() 保留并调用父类（Qt 的动态属性处理标注
 *             后续扩展）。
 * @note       本文件仅处理进度条自身逻辑，不涉及布局与父子裁剪
 *             （由 XWidget 基类提供）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#if XWIDGET_ON && XPROGRESSBAR_ON

#include "XProgressBar.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XColor.h"
#if XPALETTE_ON
#include "XPalette.h"
#endif /* XPALETTE_ON */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* 内部格式串缓冲上限（含 NUL） */
#define XPROGRESSBAR_FORMAT_MAX 32

/* ==================== 前向声明 ==================== */
static bool  VXProgressBar_event(XWidget* self, XEvent* event);
static void  VXProgressBar_paintEvent(XWidget* self, XEvent* event);
static void  VXProgressBar_changeEvent(XWidget* self, XEvent* event);
static void  VXProgressBar_copy(XProgressBar* self, const XProgressBar* other);
static void  VXProgressBar_move(XProgressBar* self, XProgressBar* other);

/* ==================== 内部辅助 ==================== */

/** @brief 取指定角色颜色为 ARGB32；无调色板能力时回退纯黑。 */
static uint32_t xprogressbar_color(const XProgressBar* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

/** @brief 把值钳位到 [min,max]（min>max 时视为 [max,min]）。 */
static int xprogressbar_clamp(const XProgressBar* self, int value)
{
    int lo = self->m_min <= self->m_max ? self->m_min : self->m_max;
    int hi = self->m_min <= self->m_max ? self->m_max : self->m_min;
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

/** @brief 发射 int 参数信号（对标 abstractbutton_emitInt 模式）。 */
static void xprogressbar_emitInt(XProgressBar* self, size_t signal, int value)
{
    XVarList* arguments = XVarList_Create(XVar(int, value));
    if (!arguments) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 按格式串生成文本（%p 百分比、%v 当前值、%m 最大值）。 */
static void xprogressbar_buildText(const XProgressBar* self, char* out,
                                   int outSize)
{
    const char* fmt = self->m_format;
    int range = self->m_max - self->m_min;
    int percent = (range > 0)
        ? (int)(((int64_t)(self->m_value - self->m_min) * 100 + range / 2) / range)
        : 0;
    int o = 0;
    const char* p;
    if (!out || outSize <= 0) return;
    if (!fmt || !fmt[0]) fmt = "%p%";
    for (p = fmt; *p != '\0' && o < outSize - 1; ++p) {
        if (p[0] == '%' && p[1] != '\0') {
            char tmp[16];
            ++p;
            if (p[0] == 'p') {
                snprintf(tmp, sizeof(tmp), "%d", percent);
            } else if (p[0] == 'v') {
                snprintf(tmp, sizeof(tmp), "%d", self->m_value);
            } else if (p[0] == 'm') {
                snprintf(tmp, sizeof(tmp), "%d", self->m_max);
            } else if (p[0] == '%') {
                tmp[0] = '%'; tmp[1] = '\0';
            } else {
                tmp[0] = '%'; tmp[1] = p[0]; tmp[2] = '\0';
            }
            {
                const char* t = tmp;
                while (*t != '\0' && o < outSize - 1) out[o++] = *t++;
            }
        } else {
            out[o++] = p[0];
        }
    }
    out[o] = '\0';
}

/* ==================== 绘制 ==================== */

/**
 * @brief      绘制凹槽 + 进度块 + 分段文本。
 * @details    坐标全部为控件本地坐标（paintEvent 已平移）。
 * @param      self    进度条对象。
 * @param      painter 画笔（已绑定绘制图像并应用平移/裁剪）。
 * @return     无返回值。
 */
void XProgressBar_drawControl(const XProgressBar* self, XPainter* painter)
{
    XRect r = XWidget_rect((XWidget*)self);
    uint32_t base;
    uint32_t dark;
    uint32_t light;
    uint32_t highlight;
    uint32_t highlightText;
    uint32_t windowText;
    int bw;
    int bh;
    int chunkLen;
    int range;
    int filled;
    bool fromStart;
    char text[64];
    r.x = 0;
    r.y = 0;
    if (r.width <= 2 || r.height <= 2) return;

    base        = xprogressbar_color(self, XPaletteColorRole_Base);
    dark        = xprogressbar_color(self, XPaletteColorRole_Dark);
    light       = xprogressbar_color(self, XPaletteColorRole_Light);
    highlight   = xprogressbar_color(self, XPaletteColorRole_Highlight);
    highlightText = xprogressbar_color(self, XPaletteColorRole_HighlightedText);
    windowText  = xprogressbar_color(self, XPaletteColorRole_WindowText);
    bw = r.width;
    bh = r.height;

    /* 1) Base 底 + 凹陷 1px 描边（上/左 Dark，下/右 Light）。 */
    XPainter_fillRect(painter, &r, base);
    {
        XRect edge = r;
        edge.height = 1;
        XPainter_fillRect(painter, &edge, dark);            /* 顶 */
        edge = r;
        edge.width = 1;
        XPainter_fillRect(painter, &edge, dark);            /* 左 */
        edge = r;
        edge.x = r.x + r.width - 1;
        edge.width = 1;
        XPainter_fillRect(painter, &edge, light);           /* 右 */
        edge = r;
        edge.y = r.y + r.height - 1;
        edge.height = 1;
        XPainter_fillRect(painter, &edge, light);           /* 底 */
    }

    /* 2) 进度块（内缩 1px，Highlight 色）。 */
    range = self->m_max - self->m_min;
    filled = (range > 0)
        ? ((int64_t)(self->m_value - self->m_min) * 100 + range / 2) / range
        : 0;
    if (filled < 0) filled = 0;
    if (filled > 100) filled = 100;
    fromStart = !self->m_invertedAppearance;
    if (self->m_orientation == XProgressBarOrientation_Vertical) {
        chunkLen = (bh - 2) * filled / 100;
        if (chunkLen < 0) chunkLen = 0;
        if (chunkLen > 0) {
            XRect chunk;
            chunk.x = r.x + 1;
            chunk.width = bw - 2;
            if (fromStart) { /* 垂直默认从下往上 */
                chunk.height = chunkLen;
                chunk.y = r.y + (bh - 1) - chunkLen;
            } else {
                chunk.y = r.y + 1;
                chunk.height = chunkLen;
            }
            XPainter_fillRect(painter, &chunk, highlight);
        }
    } else {
        chunkLen = (bw - 2) * filled / 100;
        if (chunkLen < 0) chunkLen = 0;
        if (chunkLen > 0) {
            XRect chunk;
            chunk.y = r.y + 1;
            chunk.height = bh - 2;
            if (fromStart) { /* 水平默认从左往右 */
                chunk.x = r.x + 1;
                chunk.width = chunkLen;
            } else {
                chunk.width = chunkLen;
                chunk.x = r.x + (bw - 1) - chunkLen;
            }
            XPainter_fillRect(painter, &chunk, highlight);
        }
    }

    /* 3) 分段文本（textVisible 且有对齐时）：块内 HighlightedText、
     *    块外 WindowText，按进度块边界分两段裁剪绘制。 */
    if (self->m_textVisible) {
        XProgressBar* mutableSelf = (XProgressBar*)self;
        int chunkPixel = (chunkLen > 0) ? chunkLen : 0;
        XPainter_setPen(painter, windowText);
        if (self->m_orientation == XProgressBarOrientation_Vertical) {
            /* 垂直文本：按 textDirection 旋转 90° 排布，单色居中绘制
               （块内/块外分段裁剪为后续扩展）。 */
            XProgressBar_text(mutableSelf, text, (int)sizeof(text));
            {
                /* 旋转文本同样按真实字体度量居中（8px 估算会偏出中心）。 */
                int textW = XPainter_textWidth(XPainter_font(painter), text);
                int textH = 14;
                int cx = r.x + bw / 2;
                int cy = r.y + bh / 2;
                XImageTransform savedTransform;
                XPainter_transform(painter, &savedTransform);
                XPainter_translate(painter, (float)cx, (float)cy);
                /* TopToBottom：顺时针 90°（文本自上而下）；BottomToTop：
                   逆时针 90°（文本自下而上）。 */
                XPainter_rotate(painter,
                    (self->m_textDirection ==
                     XProgressBarDirection_BottomToTop) ? -90.0f : 90.0f);
                XPainter_drawText(painter, -textW / 2, -textH / 2 + textH - 2,
                                  text, windowText);
                XPainter_setTransform(painter, &savedTransform, false);
            }
        } else {
            XProgressBar_text(mutableSelf, text, (int)sizeof(text));
            /* 文本宽度必须按当前字体真实度量测量：轮廓字库的字符步进
               远大于旧点阵的 8px（'%' 约 15px），用估算宽度做居中与
               分段裁剪会把 % 字形切掉一角（视觉上变成"9"等残形）。 */
            {
                int textW = XPainter_textWidth(XPainter_font(painter), text);
                int textH = 14;
                int tx = r.x + 1;
                int ty = r.y + (bh - textH) / 2;
                int baseline = ty + textH - 2;
                if (self->m_alignment & XAlignment_Right)
                    tx = r.x + bw - 1 - textW;
                else if (self->m_alignment & XAlignment_HCenter ||
                         self->m_alignment == 0)
                    tx = r.x + (bw - textW) / 2;
                if (chunkPixel > 0) {
                    /* 块内段：与进度块的交集用 HighlightedText；块外段：
                       文本区间减进度块后用 WindowText。两段的裁剪区间都
                       以真实文本右缘 textRight 为界，避免截断字形。 */
                    int textRight = tx + textW;
                    int chunkLeft = fromStart
                        ? (r.x + 1)
                        : (r.x + (bw - 1) - chunkPixel);
                    int chunkRight = fromStart
                        ? (r.x + 1 + chunkPixel)
                        : (r.x + (bw - 1));
                    int insideL = fromStart
                        ? tx
                        : (chunkLeft > tx ? chunkLeft : tx);
                    int insideR = fromStart
                        ? (chunkRight < textRight ? chunkRight : textRight)
                        : textRight;
                    int outsideL = fromStart
                        ? (chunkRight > tx ? chunkRight : tx)
                        : tx;
                    int outsideR = fromStart
                        ? textRight
                        : (chunkLeft < textRight ? chunkLeft : textRight);
                    if (insideR > insideL) {
                        XRect clip;
                        XRect_init(&clip, insideL, ty, insideR - insideL,
                                   textH);
                        XPainter_setClipRect(painter, &clip,
                                             XPainterClipOperation_ReplaceClip);
                        XPainter_drawText(painter, tx, baseline, text,
                                          highlightText);
                    }
                    if (outsideR > outsideL) {
                        XRect clip;
                        XRect_init(&clip, outsideL, ty, outsideR - outsideL,
                                   textH);
                        XPainter_setClipRect(painter, &clip,
                                             XPainterClipOperation_ReplaceClip);
                        XPainter_drawText(painter, tx, baseline, text,
                                          windowText);
                    }
                    XPainter_setClipRect(painter, NULL,
                                         XPainterClipOperation_NoClip);
                } else {
                    XPainter_drawText(painter, tx, baseline, text,
                                      windowText);
                }
            }
        }
        (void)mutableSelf;
    }
    (void)bw;
    (void)bh;
}

/* ==================== 虚槽实现（对标 QProgressBar 事件/复制/移动） ==================== */

/** @brief 控件事件入口（对标 QProgressBar::event）。
 *  @details Qt 在 event() 中处理动态属性（QDynamicPropertyChangeEvent）
 *          与相关刷新；本项目动态属性机制未接入（后续扩展），此处仅
 *          调用父类 XWidget 事件分派后原样返回结果。 */
static bool VXProgressBar_event(XWidget* self, XEvent* event)
{
    if (!self || !event) return false;
    return XClass_Parent(XWidget, EXObject_Event,
                         bool(*)(XObject*, XEvent*))((XObject*)self, event);
}

/** @brief 变更事件：忽略（同 XWidget 默认，无状态重算需求）。 */
static void VXProgressBar_changeEvent(XWidget* self, XEvent* event)
{
    XEvent_ignore(event);
    (void)self;
}

/** @brief 绘制事件：在 paintDevice 上平移裁剪后调用 drawControl。 */
static void VXProgressBar_paintEvent(XWidget* self, XEvent* event)
{
    XImage* image;
    XPoint offset;
    XPainter painter;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    image = XWidget_paintDevice(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    XProgressBar_drawControl((XProgressBar*)self, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 深拷贝：基类 XWidget 深拷贝后复制全部进度条标量字段。 */
static void VXProgressBar_copy(XProgressBar* self, const XProgressBar* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XProgressBar_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    self->m_min = other->m_min;
    self->m_max = other->m_max;
    self->m_value = other->m_value;
    self->m_orientation = other->m_orientation;
    self->m_invertedAppearance = other->m_invertedAppearance;
    self->m_textVisible = other->m_textVisible;
    self->m_textDirection = other->m_textDirection;
    self->m_alignment = other->m_alignment;
    memcpy(self->m_format, other->m_format, sizeof(self->m_format));
}

/** @brief 移动语义：基类移动后转移字段，源对象归构造默认值。 */
static void VXProgressBar_move(XProgressBar* self, XProgressBar* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XProgressBar_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    self->m_min = other->m_min;
    self->m_max = other->m_max;
    self->m_value = other->m_value;
    self->m_orientation = other->m_orientation;
    self->m_invertedAppearance = other->m_invertedAppearance;
    self->m_textVisible = other->m_textVisible;
    self->m_textDirection = other->m_textDirection;
    self->m_alignment = other->m_alignment;
    memcpy(self->m_format, other->m_format, sizeof(self->m_format));
    other->m_min = 0;
    other->m_max = 100;
    other->m_value = 0;
    other->m_orientation = XProgressBarOrientation_Horizontal;
    other->m_invertedAppearance = false;
    other->m_textVisible = true;
    other->m_textDirection = XProgressBarDirection_TopToBottom;
    other->m_alignment = XAlignment_HCenter | XAlignment_VCenter;
    other->m_format[0] = '%';
    other->m_format[1] = 'p';
    other->m_format[2] = '%';
    other->m_format[3] = '\0';
}

/* ==================== 生命周期 ==================== */

XVtable* XProgressBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XProgressBar)
    XVTABLE_INHERIT_XCLASS(XWidget);

    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXProgressBar_event);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXProgressBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXProgressBar_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXProgressBar_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXProgressBar_move);

    return XVTABLE_DEFAULT;
}

void XProgressBar_init(XProgressBar* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XProgressBar);

    self->m_min = 0;
    self->m_max = 100;
    self->m_value = 0;
    self->m_orientation = XProgressBarOrientation_Horizontal;
    self->m_invertedAppearance = false;
    self->m_textVisible = true;
    self->m_textDirection = XProgressBarDirection_TopToBottom;
    self->m_alignment = XAlignment_HCenter | XAlignment_VCenter;
    self->m_format[0] = '%';
    self->m_format[1] = 'p';
    self->m_format[2] = '%';
    self->m_format[3] = '\0';
}

XProgressBar* XProgressBar_create_ex(XMemoryType memory, XWidget* parent,
                                     XWidgetFlags flags)
{
    XProgressBar* self =
        (XProgressBar*)XMemory_malloc(sizeof(XProgressBar), memory);
    if (!self) return NULL;
    XProgressBar_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 范围与数值 ==================== */

int XProgressBar_minimum(const XProgressBar* self)
{
    return self ? self->m_min : 0;
}

int XProgressBar_maximum(const XProgressBar* self)
{
    return self ? self->m_max : 100;
}

void XProgressBar_setRange(XProgressBar* self, int min, int max)
{
    if (!self) return;
    if (min > max) { int t = min; min = max; max = t; }
    self->m_min = min;
    self->m_max = max;
    XProgressBar_setValue(self, self->m_value); /* 钳位 + 条件信号/重绘 */
}

void XProgressBar_setMinimum(XProgressBar* self, int minimum)
{
    int newMax;
    if (!self) return;
    /* Qt 语义：newMax = qMax(max, minimum) 后 setRange。 */
    newMax = self->m_max > minimum ? self->m_max : minimum;
    XProgressBar_setRange(self, minimum, newMax);
}

void XProgressBar_setMaximum(XProgressBar* self, int maximum)
{
    int newMin;
    if (!self) return;
    /* Qt 语义：newMin = qMin(min, maximum) 后 setRange。 */
    newMin = self->m_min < maximum ? self->m_min : maximum;
    XProgressBar_setRange(self, newMin, maximum);
}

int XProgressBar_value(const XProgressBar* self)
{
    return self ? self->m_value : 0;
}

void XProgressBar_setValue(XProgressBar* self, int value)
{
    int clamped;
    if (!self) return;
    clamped = xprogressbar_clamp(self, value);
    if (clamped == self->m_value) return;
    self->m_value = clamped;
    xprogressbar_emitInt(self, (size_t)XProgressBar_valueChanged_signal,
                         self->m_value);
    XWidget_update((XWidget*)self);
}

void XProgressBar_reset(XProgressBar* self)
{
    if (!self) return;
    self->m_value = xprogressbar_clamp(self, self->m_min);
    XWidget_update((XWidget*)self);
}

/* ==================== 外观属性 ==================== */

int XProgressBar_orientation(const XProgressBar* self)
{
    return self ? self->m_orientation : XProgressBarOrientation_Horizontal;
}

void XProgressBar_setOrientation(XProgressBar* self, int orientation)
{
    if (!self || self->m_orientation == orientation) return;
    if (orientation != XProgressBarOrientation_Horizontal &&
        orientation != XProgressBarOrientation_Vertical)
        return;
    self->m_orientation = orientation;
    XWidget_update((XWidget*)self);
}

bool XProgressBar_invertedAppearance(const XProgressBar* self)
{
    return self ? self->m_invertedAppearance : false;
}

void XProgressBar_setInvertedAppearance(XProgressBar* self, bool inverted)
{
    if (!self || self->m_invertedAppearance == inverted) return;
    self->m_invertedAppearance = inverted;
    XWidget_update((XWidget*)self);
}

int XProgressBar_textDirection(const XProgressBar* self)
{
    return self ? self->m_textDirection : XProgressBarDirection_TopToBottom;
}

void XProgressBar_setTextDirection(XProgressBar* self, int textDirection)
{
    if (!self || self->m_textDirection == textDirection) return;
    if (textDirection != XProgressBarDirection_TopToBottom &&
        textDirection != XProgressBarDirection_BottomToTop)
        return;
    self->m_textDirection = textDirection;
    XWidget_update((XWidget*)self);
}

bool XProgressBar_isTextVisible(const XProgressBar* self)
{
    return self ? self->m_textVisible : true;
}

void XProgressBar_setTextVisible(XProgressBar* self, bool visible)
{
    if (!self || self->m_textVisible == visible) return;
    self->m_textVisible = visible;
    XWidget_update((XWidget*)self);
}

int XProgressBar_alignment(const XProgressBar* self)
{
    return self ? self->m_alignment
                : (XAlignment_HCenter | XAlignment_VCenter);
}

void XProgressBar_setAlignment(XProgressBar* self, int alignment)
{
    if (!self || self->m_alignment == alignment) return;
    self->m_alignment = alignment;
    XWidget_update((XWidget*)self);
}

const char* XProgressBar_format(const XProgressBar* self)
{
    return (self && self->m_format[0]) ? self->m_format : "%p%";
}

void XProgressBar_setFormat(XProgressBar* self, const char* format)
{
    if (!self) return;
    if (!format) format = "%p%";
    strncpy(self->m_format, format, sizeof(self->m_format) - 1);
    self->m_format[sizeof(self->m_format) - 1] = '\0';
    XWidget_update((XWidget*)self);
}

void XProgressBar_resetFormat(XProgressBar* self)
{
    if (!self) return;
    strcpy(self->m_format, "%p%");
    XWidget_update((XWidget*)self);
}

void XProgressBar_text(const XProgressBar* self, char* out, int outSize)
{
    if (!self) { if (out && outSize > 0) out[0] = '\0'; return; }
    xprogressbar_buildText(self, out, outSize);
}

/* ==================== 信号 ==================== */

void* XProgressBar_valueChanged_signal(XProgressBar* self)
{
    if (!self)
        return (void*)(size_t)XProgressBar_valueChanged_signal;
    xprogressbar_emitInt(self, (size_t)XProgressBar_valueChanged_signal,
                         self->m_value);
    return (void*)(size_t)XProgressBar_valueChanged_signal;
}

#endif /* XWIDGET_ON && XPROGRESSBAR_ON */
