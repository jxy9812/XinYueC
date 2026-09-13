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

/* 行程角（度）：对标 QCommonStyle::calcArrow/calcLines——
 * 数学角 240°（屏幕左下）起，顺时针（数学角递减）扫 300°，
 * 缺口位于底部。 */
#define XDIAL_START_ANGLE   (240.0)
#define XDIAL_SWEEP_ANGLE   (300.0)
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
    /* 屏幕顺时针 = 数学角递减（对标 calcArrow 的 a = (8π - Δ·10π/range)/6）。 */
    return XDIAL_START_ANGLE - frac * XDIAL_SWEEP_ANGLE;
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
    /* 与 valueToAngle 对应：屏幕顺时针 = 数学角递减。 */
    double rel = XDIAL_START_ANGLE - angle;
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

/** @brief 绘制：对标 QCommonStyle::drawComplexControl(CC_Dial)——
 *         刻度线（windowText）、表盘圆（背景色填充）、凹槽双弧
 *         （dark 左上/light 右下）、三角指示箭头（button 填充 +
 *         light/dark 描边，随值旋转）、聚焦框省略。 */
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
    uint32_t base;
    int width;
    int height;
    int radius;
    int bigLine;
    int i;
    int notchCount;
    int len;
    int back;
    double angleRad;
    double ca;
    double sa;
    XPoint arrow[3];
    XRect br;
    if (!dial || r.width <= 8 || r.height <= 8) return;
    r.x = 0; r.y = 0;
    dark       = xdial_color(dial, XPaletteColorRole_Dark);
    light      = xdial_color(dial, XPaletteColorRole_Light);
    button     = xdial_color(dial, XPaletteColorRole_Button);
    windowText = xdial_color(dial, XPaletteColorRole_WindowText);
    base       = xdial_color(dial, XPaletteColorRole_Base);

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

    width  = r.width;
    height = r.height;
    radius = (width < height ? width : height) / 2;
    bigLine = radius / 6;
    if (bigLine < 4) bigLine = 4;
    if (bigLine > radius / 2) bigLine = radius / 2;
    br.x = radius / 6 + (width - 2 * radius) / 2 + 1;
    br.y = radius / 6 + (height - 2 * radius) / 2 + 1;
    br.width  = radius * 2 - 2 * (radius / 6) - 2;
    br.height = br.width;
    {
        int bcx = br.x + br.width / 2;
        int bcy = br.y + br.height / 2;
        int br2 = br.width / 2;

        /* 1) 刻度线：窗口文本色，从 (r-bigLine) 到 r（对标
         *    QStyleHelper::calcLines + SC_DialTickmarks）。 */
        notchCount = XDial_notchSize(dial);
        if (dial->m_notchesVisible && notchCount > 0) {
            int smallLine = bigLine / 2;
            XPainter_setPen(&painter, windowText);
            for (i = 0; i <= notchCount; ++i) {
                double a = (XDIAL_START_ANGLE +
                            XDIAL_SWEEP_ANGLE * i / notchCount) *
                           3.14159265358979323846 / 180.0;
                double c = cos(a);
                double sn = -sin(a);
                XPainter_drawLine(&painter,
                                  (int)(bcx + (br2 - bigLine) * c),
                                  (int)(bcy + (br2 - bigLine) * sn),
                                  (int)(bcx + br2 * c),
                                  (int)(bcy + br2 * sn));
                (void)smallLine;
            }
        }

        /* 2) 表盘圆：Base 色填充（对标 drawEllipse(br) + 背景色画刷）；
         *    shape 关闭的裁剪构建退化为扫描线圆。 */
        XPainter_setBrush(&painter, base);
        XPainter_setPen(&painter, base);
#if XPAINTER_SHAPE_ON
        XPainter_drawEllipse(&painter, &br);
#else
        {
            int rc = br.width / 2;
            int bcx = br.x + rc;
            int bcy = br.y + rc;
            int dy;
            for (dy = -rc; dy <= rc; ++dy) {
                int dx = (int)(sqrt((double)(rc * rc - dy * dy)));
                if (dx < 1) continue;
                XPainter_drawLine(&painter, bcx - dx, bcy + dy,
                                  bcx + dx, bcy + dy);
            }
        }
#endif

        /* 3) 凹槽双弧：dark 左上半 / light 右下半（对标 drawArc
         *    60*16,180*16 与 240*16,180*16 的 3D 凹陷效果）。 */
#if XPAINTER_SHAPE_ON
        XPainter_setPen(&painter, dark);
        XPainter_drawArc(&painter, &br, 60 * 16, 180 * 16);
        XPainter_setPen(&painter, light);
        XPainter_drawArc(&painter, &br, 240 * 16, 180 * 16);
#endif

        /* 4) 三角指示箭头（对标 calcArrow + drawPolygon）：tip 指向
         *    当前值角度，底边两角在 ±150°，填 Button 色。 */
        angleRad = xdial_valueToAngle(
            dial, XAbstractSlider_value((const XAbstractSlider*)dial)) *
            3.14159265358979323846 / 180.0;
        ca = cos(angleRad);
        sa = -sin(angleRad);
        len = br2 - bigLine - 5;
        if (len < 5) len = 5;
        back = len / 2;
        arrow[0].x = (int)(bcx + len * ca);
        arrow[0].y = (int)(bcy + len * sa);
        arrow[1].x = (int)(bcx + back * cos(angleRad + 3.14159265358979323846 * 5.0 / 6.0));
        arrow[1].y = (int)(bcy + back * -sin(angleRad + 3.14159265358979323846 * 5.0 / 6.0));
        arrow[2].x = (int)(bcx + back * cos(angleRad - 3.14159265358979323846 * 5.0 / 6.0));
        arrow[2].y = (int)(bcy + back * -sin(angleRad - 3.14159265358979323846 * 5.0 / 6.0));
        XPainter_setBrush(&painter, button);
        XPainter_setPen(&painter, dark);
#if XPAINTER_SHAPE_ON
        XPainter_drawConvexPolygon(&painter, arrow, 3);
#else
        XPainter_drawLine(&painter, arrow[0].x, arrow[0].y,
                          arrow[1].x, arrow[1].y);
        XPainter_drawLine(&painter, arrow[1].x, arrow[1].y,
                          arrow[2].x, arrow[2].y);
        XPainter_drawLine(&painter, arrow[2].x, arrow[2].y,
                          arrow[0].x, arrow[0].y);
#endif
        /* 箭头描边（对标按角度区段的 light/dark 高光）：简单取
           light 色沿两边提亮，模拟 3D 凸起。 */
        XPainter_setPen(&painter, light);
        XPainter_drawLine(&painter, arrow[0].x, arrow[0].y,
                          arrow[2].x, arrow[2].y);
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
    XWidget_grabMouse(self);
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
    XWidget_releaseMouse(self);
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
