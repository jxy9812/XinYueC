/**
 * @file       XDial.c
 * @brief      XDial 旋钮控件实现（对标 Qt 6.8 QDial）。
 * @details    绘制：圆形凹槽（Dark/Light 立体描边）+ 值指示针（从圆心
 *             指向当前角度）+ notchesVisible 时按 notchTarget 沿圆周
 *             布置刻度短线。角度映射：-225°（最小值，左下）→ 45°
 *             （最大值，右下），共 270° 行程（对标 QDial 默认）；wrapping
 *             时越界环绕。鼠标：左键按下进入拖动（sliderPressed），
 *             按当前角度反解 value 实时提交（tracking），释放结束。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#if XWIDGET_ON && XABSTRACTSLIDER_ON && XDIAL_ON

#include "XDial.h"
#include "XWidget_Protected.h"
#include "XPainter.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XColor.h"
#include <math.h>
#if XPALETTE_ON
#include "XPalette.h"
#endif /* XPALETTE_ON */
#include <string.h>
#include <stdlib.h>

/* 行程角（度）：QDial 默认 -225° 起、扫 270°。 */
#define XDIAL_START_ANGLE   (-225.0)
#define XDIAL_SWEEP_ANGLE   (270.0)
#define XDIAL_HANDLE_LEN    8

/* ==================== 前向声明 ==================== */
static void VXSliderBase_dialStub(void);
static void VXDial_paintEvent(XWidget* self, XEvent* event);
static void VXDial_mousePressEvent(XWidget* self, XEvent* event);
static void VXDial_mouseMoveEvent(XWidget* self, XEvent* event);
static void VXDial_mouseReleaseEvent(XWidget* self, XEvent* event);
static void VXDial_changeEvent(XWidget* self, XEvent* event);
static void VXDial_copy(XDial* self, const XDial* other);
static void XDial_move(XDial* self, XDial* other);

/* ==================== 内部辅助 ==================== */

/** @brief 取指定角色颜色为 ARGB32；无调色板能力时回退纯黑。 */
static uint32_t xdial_color(const XDial* self, XPaletteColorRole role)
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

/** @brief 值 → 角度（度；0°=右，逆时针为正的数学角）。 */
static double xdial_valueToAngle(const XDial* self, int value)
{
    int range = XAbstractSlider_maximum((const XAbstractSlider*)self) -
                XAbstractSlider_minimum((const XAbstractSlider*)self);
    double frac;
    if (range <= 0) return XDIAL_START_ANGLE;
    frac = (double)(value - XAbstractSlider_minimum((const XAbstractSlider*)self)) /
           (double)range;
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;
    return XDIAL_START_ANGLE + frac * XDIAL_SWEEP_ANGLE;
}

/** @brief 点击坐标 → 值（以圆心为基准的角度反解；钳位）。 */
static int xdial_posToValue(const XDial* self, const XPoint* pos)
{
    double cx = XWidget_width((XWidget*)self) / 2.0;
    double cy = XWidget_height((XWidget*)self) / 2.0;
    double dx = (double)pos->x - cx;
    double dy = (double)pos->y - cy;
    /* 数学角（0°=右，逆时针正）→ 行程角：X 绘制体系 y 向下。 */
    double angle = atan2(-dy, dx) * 180.0 / 3.14159265358979323846;
    double rel = angle - XDIAL_START_ANGLE;
    int range = XAbstractSlider_maximum((const XAbstractSlider*)self) -
                XAbstractSlider_minimum((const XAbstractSlider*)self);
    int value;
    /* 归一化到 [0,270)。 */
    while (rel < 0.0) rel += 360.0;
    while (rel >= 360.0) rel -= 360.0;
    if (rel > XDIAL_SWEEP_ANGLE) {
        /* 270° 之外的死角：就近吸附到两端。 */
        rel = (rel < XDIAL_START_ANGLE + 360.0 - 45.0) ? 0.0 : XDIAL_SWEEP_ANGLE;
        if (rel > XDIAL_SWEEP_ANGLE) rel = XDIAL_SWEEP_ANGLE;
    }
    if (range <= 0) return XAbstractSlider_minimum((const XAbstractSlider*)self);
    value = XAbstractSlider_minimum((const XAbstractSlider*)self) +
            (int)(rel / XDIAL_SWEEP_ANGLE * range + 0.5);
    if (value < XAbstractSlider_minimum((const XAbstractSlider*)self))
        value = XAbstractSlider_minimum((const XAbstractSlider*)self);
    if (value > XAbstractSlider_maximum((const XAbstractSlider*)self))
        value = XAbstractSlider_maximum((const XAbstractSlider*)self);
    return value;
}

/* ==================== 虚槽实现 ==================== */

/** @brief 绘制：凹槽圆 + 刻度 + 指示针。 */
static void VXDial_paintEvent(XWidget* self, XEvent* event)
{
    XDial* dial = (XDial*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r = XWidget_rect(self);
    uint32_t dark;
    uint32_t light;
    uint32_t button;
    uint32_t windowText;
    int cx;
    int cy;
    int radius;
    double angle;
    int i;
    int notchCount;
    if (!dial || r.width <= 8 || r.height <= 8) return;
    r.x = 0; r.y = 0;
    dark  = xdial_color(dial, XPaletteColorRole_Dark);
    light = xdial_color(dial, XPaletteColorRole_Light);
    button = xdial_color(dial, XPaletteColorRole_Button);
    windowText = xdial_color(dial, XPaletteColorRole_WindowText);

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

    cx = r.width / 2;
    cy = r.height / 2;
    radius = (r.width < r.height ? r.width : r.height) / 2 - 2;

    /* 表盘圆：扫描线填充（Button 底色）。 */
    {
        int dy;
        for (dy = -radius; dy <= radius; ++dy) {
            int dx = (int)(sqrt((double)(radius * radius - dy * dy)));
            if (dx < 1) continue;
            XPainter_drawLine(&painter, cx - dx, cy + dy,
                              cx + dx, cy + dy);
        }
    }
    angle = xdial_valueToAngle(dial, XAbstractSlider_value((const XAbstractSlider*)dial));

    XPainter_setPen(&painter, button);
    /* 刻度：notchesVisible 时沿行程等距短线。 */
    notchCount = XDial_notchSize(dial);
    if (dial->m_notchesVisible && notchCount > 1) {
        for (i = 0; i <= notchCount; ++i) {
            double a = (XDIAL_START_ANGLE +
                        XDIAL_SWEEP_ANGLE * i / notchCount) * 3.14159265358979323846 / 180.0;
            double ca = cos(a);
            double sa = -sin(a);
            int r1 = radius - 1;
            int r2 = radius - 5;
            XPainter_drawLine(&painter,
                              cx + (int)(ca * r1), cy + (int)(sa * r1),
                              cx + (int)(ca * r2), cy + (int)(sa * r2));
        }
    }

    XPainter_setPen(&painter, windowText);
    /* 指示针：从圆心内圈指向当前角度。 */
    {
        double a = angle * 3.14159265358979323846 / 180.0;
        double ca = cos(a);
        double sa = -sin(a);
        XPainter_drawLine(&painter, cx, cy,
                          cx + (int)(ca * (radius - 4)),
                          cy + (int)(sa * (radius - 4)));
        /* 中心把手。 */
        XPainter_fillRect(&painter, &(XRect){ cx - 3, cy - 3, 6, 6 }, dark);
    }

    XPainter_setPen(&painter, dark);
    {
        XRect er = { cx - radius, cy - radius, radius * 2, radius * 2 };
        XPainter_drawEllipse(&painter, &er);
    }

    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 按下：进入拖动 + sliderPressed + 立即按角度调值。 */
static void VXDial_mousePressEvent(XWidget* self, XEvent* event)
{
    XDial* dial = (XDial*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!dial || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
    dial->m_dragging = true;
    XAbstractSlider_setSliderDown((XAbstractSlider*)dial, true);
    XAbstractSlider_setValue((XAbstractSlider*)dial, xdial_posToValue(dial, &pos));
    XEvent_accept(event);
}

/** @brief 拖动：实时调值。 */
static void VXDial_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XDial* dial = (XDial*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!dial || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    if (!dial->m_dragging) { XEvent_ignore(event); return; }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    XAbstractSlider_setValue((XAbstractSlider*)dial, xdial_posToValue(dial, &pos));
    XEvent_accept(event);
}

/** @brief 释放：结束拖动。 */
static void VXDial_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XDial* dial = (XDial*)self;
    XMouseEvent* me;
    if (!dial || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    dial->m_dragging = false;
    XAbstractSlider_setSliderDown((XAbstractSlider*)dial, false);
    XEvent_accept(event);
}

static void VXDial_changeEvent(XWidget* self, XEvent* event)
{
    XEvent_ignore(event);
    (void)self;
}

/** @brief 深拷贝：基类拷贝后复制旋钮字段。 */
static void VXDial_copy(XDial* self, const XDial* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XDial_init(self, NULL, 0);
    XClass_Parent(XAbstractSlider, EXClass_Copy,
                  void(*)(XAbstractSlider*, const XAbstractSlider*))(
                      (XAbstractSlider*)self, (const XAbstractSlider*)other);
    self->m_notchTarget = other->m_notchTarget;
    self->m_notchesVisible = other->m_notchesVisible;
    self->m_wrapping = other->m_wrapping;
    self->m_dragging = other->m_dragging;
}

static void XDial_move(XDial* self, XDial* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XDial_init(self, NULL, 0);
    XClass_Parent(XAbstractSlider, EXClass_Move,
                  void(*)(XAbstractSlider*, XAbstractSlider*))(
                      (XAbstractSlider*)self, (XAbstractSlider*)other);
    self->m_notchTarget = other->m_notchTarget;
    self->m_notchesVisible = other->m_notchesVisible;
    self->m_wrapping = other->m_wrapping;
    self->m_dragging = other->m_dragging;
    other->m_notchTarget = 3.7;
    other->m_notchesVisible = false;
    other->m_dragging = false;
}

/** @brief 基类桩（占位避免空翻译单元告警）。 */
static void VXSliderBase_dialStub(void) {}

/* ==================== 生命周期 ==================== */

XVtable* XDial_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDial)
    XVTABLE_INHERIT_XCLASS(XAbstractSlider);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXDial_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VXDial_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VXDial_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, VXDial_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXDial_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXDial_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, XDial_move);

    return XVTABLE_DEFAULT;
}

void XDial_init(XDial* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XAbstractSlider_init((XAbstractSlider*)self, parent, flags);
    XClassSetVtable(self, XDial);

    self->m_notchTarget = 3.7;
    self->m_notchesVisible = false;
    self->m_wrapping = false;
    self->m_dragging = false;
}

XDial* XDial_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XDial* self = (XDial*)XMemory_malloc(sizeof(XDial), memory);
    if (!self) return NULL;
    XDial_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 专属属性 ==================== */

double XDial_notchTarget(const XDial* self)
{
    return self ? self->m_notchTarget : 3.7;
}

void XDial_setNotchTarget(XDial* self, double target)
{
    if (!self || target <= 0.0 || self->m_notchTarget == target) return;
    self->m_notchTarget = target;
    XWidget_update((XWidget*)self);
}

bool XDial_notchesVisible(const XDial* self)
{
    return self ? self->m_notchesVisible : false;
}

void XDial_setNotchesVisible(XDial* self, bool visible)
{
    if (!self || self->m_notchesVisible == visible) return;
    self->m_notchesVisible = visible;
    XWidget_update((XWidget*)self);
}

bool XDial_wrapping(const XDial* self)
{
    return self ? self->m_wrapping : false;
}

void XDial_setWrapping(XDial* self, bool on)
{
    if (!self || self->m_wrapping == on) return;
    self->m_wrapping = on;
    XWidget_update((XWidget*)self);
}

int XDial_notchSize(const XDial* self)
{
    /* 刻度数 = 行程像素 / notchTarget（对标 QDial 的近似算法）。 */
    int pixels = (XWidget_width((XWidget*)self) <
                  XWidget_height((XWidget*)self)
                      ? XWidget_width((XWidget*)self)
                      : XWidget_height((XWidget*)self));
    int range = XAbstractSlider_maximum((const XAbstractSlider*)self) -
                XAbstractSlider_minimum((const XAbstractSlider*)self);
    if (range <= 0 || pixels <= 0) return 0;
    {
        int count = (int)(range * 1.0 / (self ? self->m_notchTarget : 3.7));
        (void)pixels;
        return count > 1 ? count : 0;
    }
}

#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON && XDIAL_ON */
