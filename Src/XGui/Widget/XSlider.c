/**
 * @file       XSlider.c
 * @brief      XSlider 滑块控件实现（对齐 Qt 6.8 QSlider；继承
 *             XAbstractSlider）。
 * @details    几何映射（水平为例）：
 *             - 凹槽：垂直居中 3px 凹陷线（Dark 上 / Light 下）；
 *             - handle：宽 12px、高 = 控件高-2 的凸起矩形（Button 底
 *               + Light 顶/左 + Dark 底/右），中心沿凹槽滑动；
 *             - 刻度：tickPosition 非 NoTicks 时按 tickInterval（0 时
 *               自动回退 pageStep→singleStep→1）从 minimum 起画短线，
 *               水平在上/下、垂直在左/右；
 *             - 值 ↔ 位置：handle 中心 x = x0 + handleW/2 +
 *               (value-min)/(max-min) * (width-handleW)；反向按拖动
 *               点（扣除按下偏移）反解 value 后钳位。
 *             交互（左键，对标 QSlider 默认行为）：
 *             - 点击 handle：记录拖动偏移 → setSliderDown(true) +
 *               sliderPressed，进入拖动；
 *             - 点击 handle 外凹槽：setSliderPosition 跳到点击处 +
 *               triggerAction(SliderMove)（handle 绝对定位）；
 *             - 拖动：setSliderPosition（内部发射 sliderMoved；
 *               tracking=true 时提交值发 valueChanged）；
 *             - 释放：setSliderDown(false) → sliderReleased（位置≠值
 *               时提交值，tracking=false 场景）。
 *             invertedAppearance 翻转绘制端；invertedControls 翻转
 *             拖动方向由基类键盘/滚轮处理。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#if XWIDGET_ON && XABSTRACTSLIDER_ON && XSLIDER_ON

#include "XSlider.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XColor.h"
#include "XPainter.h"
#if XPALETTE_ON
#include "XPalette.h"
#endif /* XPALETTE_ON */
#include <stdint.h>

/* handle 固定宽度（水平）/ 高度（垂直）与凹槽厚度、刻度线尺寸 */
#define XSLIDER_HANDLE_SIZE 12
#define XSLIDER_GROOVE_H    3
#define XSLIDER_TICK_SIZE   3

/* ==================== 前向声明 ==================== */
static void VXSlider_mousePressEvent(XWidget* self, XEvent* event);
static void VXSlider_mouseMoveEvent(XWidget* self, XEvent* event);
static void VXSlider_mouseReleaseEvent(XWidget* self, XEvent* event);
static void VXSlider_paintEvent(XWidget* self, XEvent* event);
static void VXSlider_copy(XSlider* self, const XSlider* other);
static void VXSlider_move(XSlider* self, XSlider* other);

/* ==================== 内部辅助 ==================== */

/** @brief 取指定角色颜色为 ARGB32；无调色板能力时回退纯黑。 */
static uint32_t xslider_color(const XSlider* self, XPaletteColorRole role)
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

/** @brief 基类快捷引用。 */
static XAbstractSlider* xslider_base(XSlider* self)
{
    return (XAbstractSlider*)self;
}

/** @brief 值域宽度（max-min）。 */
static int xslider_range(const XAbstractSlider* self)
{
    return self->m_max - self->m_min;
}

/**
 * @brief      值 → handle 中心像素（沿主轴，控件本地坐标）。
 * @details    水平：返回 handle 中心 x；垂直：返回中心 y。
 */
static int xslider_valueToPos(const XSlider* self, int value)
{
    const XAbstractSlider* base = (const XAbstractSlider*)self;
    int span;
    int len = 0;
    if (xslider_range(base) > 0) {
        span = ((base->m_orientation ==
                     XAbstractSliderOrientation_Horizontal)
                    ? XWidget_width((XWidget*)self)
                    : XWidget_height((XWidget*)self)) - XSLIDER_HANDLE_SIZE;
        if (span < 0) span = 0;
        len = (int)(((int64_t)(value - base->m_min) * (int64_t)span) /
                    xslider_range(base));
        if (base->m_invertedAppearance) len = span - len;
    }
    return XSLIDER_HANDLE_SIZE / 2 + len;
}

/**
 * @brief      主轴像素（handle 中心语义）→ 值（钳位）。
 * @param      pos 主轴坐标（控件本地）。
 */
static int xslider_posToValue(const XSlider* self, int pos)
{
    const XAbstractSlider* base = (const XAbstractSlider*)self;
    int span = ((base->m_orientation ==
                     XAbstractSliderOrientation_Horizontal)
                    ? XWidget_width((XWidget*)self)
                    : XWidget_height((XWidget*)self)) - XSLIDER_HANDLE_SIZE;
    int rel;
    int value;
    if (span <= 0) return base->m_min;
    rel = pos - XSLIDER_HANDLE_SIZE / 2;
    if (rel < 0) rel = 0;
    if (rel > span) rel = span;
    value = base->m_min + (int)(((int64_t)rel * (int64_t)xslider_range(base)) /
                                span);
    if (base->m_invertedAppearance)
        value = base->m_min + (base->m_max - value);
    if (value < base->m_min) value = base->m_min;
    if (value > base->m_max) value = base->m_max;
    return value;
}

/** @brief 主轴坐标（水平取 x、垂直取 y）。 */
static int xslider_pick(const XAbstractSlider* base, XPoint pos)
{
    return (base->m_orientation == XAbstractSliderOrientation_Horizontal)
               ? pos.x : pos.y;
}

/* ==================== 鼠标交互（对标 QSlider） ==================== */

/** @brief 按下：命中 handle → 进入拖动 + sliderPressed；否则 handle
 *         跳到点击处（Qt 绝对定位语义）。 */
static void VXSlider_mousePressEvent(XWidget* self, XEvent* event)
{
    XSlider* slider = (XSlider*)self;
    XAbstractSlider* base;
    XMouseEvent* me;
    XPoint pos;
    int pickPos;
    int handlePos;
    int value;

    if (!slider || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    base = xslider_base(slider);
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    if (base->m_max == base->m_min) { XEvent_ignore(event); return; }
    pos = XMouseEvent_position(me);
    pickPos = xslider_pick(base, pos);
    handlePos = xslider_valueToPos(slider, base->m_value);

    if (pickPos >= handlePos - XSLIDER_HANDLE_SIZE / 2 &&
        pickPos <= handlePos + XSLIDER_HANDLE_SIZE / 2) {
        /* 命中 handle：记录拖动偏移，进入拖动并发射 sliderPressed。 */
        slider->m_dragOffset = pickPos - handlePos;
        XAbstractSlider_setSliderDown(base, true);
        XEvent_accept(event);
        return;
    }
    /* 点击凹槽：handle 跳到点击处（Qt 默认左键绝对定位）。 */
    value = xslider_posToValue(slider, pickPos);
    XAbstractSlider_setSliderPosition(base, value);
    XAbstractSlider_triggerAction(base, XAbstractSliderSliderAction_Move);
    XEvent_accept(event);
}

/** @brief 拖动：按拖动偏移换算位置 → setSliderPosition（内部发射
 *         sliderMoved；tracking 时提交值）。 */
static void VXSlider_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XSlider* slider = (XSlider*)self;
    XAbstractSlider* base;
    XMouseEvent* me;
    XPoint pos;
    int value;
    if (!slider || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;
    base = xslider_base(slider);
    if (!base->m_sliderDown) { XEvent_ignore(event); return; }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    value = xslider_posToValue(slider, xslider_pick(base, pos) -
                                           slider->m_dragOffset);
    XAbstractSlider_setSliderPosition(base, value);
    XEvent_accept(event);
}

/** @brief 释放：setSliderDown(false)（内部发射 sliderReleased；位置≠值
 *         时提交值）。 */
static void VXSlider_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XSlider* slider = (XSlider*)self;
    XAbstractSlider* base;
    XMouseEvent* me;
    if (!slider || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    base = xslider_base(slider);
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    if (base->m_sliderDown) XAbstractSlider_setSliderDown(base, false);
    XEvent_accept(event);
}

/* ==================== 绘制 ==================== */

/** @brief 绘制刻度短线（tickPosition 非 NoTicks 时）。 */
static void VXSlider_paintTicks(XSlider* slider, XPainter* painter,
                                const XRect* r, uint32_t color)
{
    XAbstractSlider* base = xslider_base(slider);
    int interval = slider->m_tickInterval;
    int above;
    int below;
    int v;
    if (interval <= 0) {
        /* 自动间隔：pageStep → singleStep → 1（对标 QSlider 文档）。 */
        interval = base->m_pageStep > 0
                       ? base->m_pageStep
                       : (base->m_singleStep > 0 ? base->m_singleStep : 1);
    }
    above = (slider->m_tickPosition == XSliderTickPosition_TicksAbove ||
             slider->m_tickPosition == XSliderTickPosition_TicksBothSides);
    below = (slider->m_tickPosition == XSliderTickPosition_TicksBelow ||
             slider->m_tickPosition == XSliderTickPosition_TicksBothSides);
    if (!above && !below) return;

    for (v = base->m_min; v <= base->m_max; v += interval) {
        int pos = xslider_valueToPos(slider, v);
        XRect tick;
        if (base->m_orientation == XAbstractSliderOrientation_Horizontal) {
            if (above) {
                tick.x = pos - XSLIDER_TICK_SIZE / 2;
                tick.y = 1;
                tick.width = XSLIDER_TICK_SIZE;
                tick.height = XSLIDER_TICK_SIZE;
                XPainter_fillRect(painter, &tick, color);
            }
            if (below) {
                tick.x = pos - XSLIDER_TICK_SIZE / 2;
                tick.y = r->height - 1 - XSLIDER_TICK_SIZE;
                tick.width = XSLIDER_TICK_SIZE;
                tick.height = XSLIDER_TICK_SIZE;
                XPainter_fillRect(painter, &tick, color);
            }
        } else {
            if (above) {
                tick.x = 1;
                tick.y = pos - XSLIDER_TICK_SIZE / 2;
                tick.width = XSLIDER_TICK_SIZE;
                tick.height = XSLIDER_TICK_SIZE;
                XPainter_fillRect(painter, &tick, color);
            }
            if (below) {
                tick.x = r->width - 1 - XSLIDER_TICK_SIZE;
                tick.y = pos - XSLIDER_TICK_SIZE / 2;
                tick.width = XSLIDER_TICK_SIZE;
                tick.height = XSLIDER_TICK_SIZE;
                XPainter_fillRect(painter, &tick, color);
            }
        }
    }
}

/** @brief 绘制凹槽 + handle + 刻度（水平/垂直）。 */
static void VXSlider_paintEvent(XWidget* self, XEvent* event)
{
    XSlider* slider = (XSlider*)self;
    XAbstractSlider* base;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r = XWidget_rect(self);
    uint32_t baseColor;
    uint32_t dark;
    uint32_t light;
    uint32_t button;
    int grooveX;
    int grooveY;
    int handlePos;
    XRect groove;
    XRect handle;
    (void)event;
    if (!slider || r.width <= 2 || r.height <= 2) return;
    base = xslider_base(slider);

    r.x = 0; r.y = 0;
    baseColor = xslider_color(slider, XPaletteColorRole_Base);
    dark      = xslider_color(slider, XPaletteColorRole_Dark);
    light     = xslider_color(slider, XPaletteColorRole_Light);
    button    = xslider_color(slider, XPaletteColorRole_Button);
    handlePos = xslider_valueToPos(slider, base->m_value);

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

    if (base->m_orientation == XAbstractSliderOrientation_Vertical) {
        grooveX = (r.width - XSLIDER_GROOVE_H) / 2;
        groove.x = grooveX; groove.y = r.y + 1;
        groove.width = XSLIDER_GROOVE_H;
        groove.height = r.height - 2;
        handle.width = r.width - 2;
        handle.height = XSLIDER_HANDLE_SIZE;
        handle.x = r.x + 1;
        handle.y = handlePos - handle.height / 2;
    } else {
        grooveY = (r.height - XSLIDER_GROOVE_H) / 2;
        groove.x = r.x + 1; groove.y = grooveY;
        groove.width = r.width - 2;
        groove.height = XSLIDER_GROOVE_H;
        handle.width = XSLIDER_HANDLE_SIZE;
        handle.height = r.height - 2;
        handle.x = handlePos - handle.width / 2;
        handle.y = r.y + 1;
    }

    /* 凹槽：Dark 上线 + Light 下线（凹陷）。 */
    XPainter_fillRect(&painter, &groove, baseColor);
    {
        XRect line = groove;
        line.height = 1;
        XPainter_fillRect(&painter, &line, dark);
        line = groove;
        line.y = groove.y + groove.height - 1;
        line.height = 1;
        XPainter_fillRect(&painter, &line, light);
    }

    /* 刻度线（在 handle 之下绘制）。 */
    VXSlider_paintTicks(slider, &painter, &r, dark);

    /* handle：Button 底 + Light 顶/左 + Dark 底/右（凸起）。 */
    if (handle.x < r.x) handle.x = r.x;
    if (handle.y < r.y) handle.y = r.y;
    XPainter_fillRect(&painter, &handle, button);
    {
        XRect e = handle;
        e.height = 1;
        XPainter_fillRect(&painter, &e, light);
        e = handle; e.width = 1;
        XPainter_fillRect(&painter, &e, light);
        e = handle; e.x = handle.x + handle.width - 1; e.width = 1;
        XPainter_fillRect(&painter, &e, dark);
        e = handle; e.y = handle.y + handle.height - 1; e.height = 1;
        XPainter_fillRect(&painter, &e, dark);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 控件事件入口：转发父类（对标 QSlider::event）。 */

/** @brief 深拷贝：基类 XAbstractSlider 深拷贝后复制刻度字段。 */
static void VXSlider_copy(XSlider* self, const XSlider* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XSlider_init(self, NULL, 0);
    XClass_Parent(XAbstractSlider, EXClass_Copy,
                  void(*)(XAbstractSlider*, const XAbstractSlider*))(
        (XAbstractSlider*)self, (const XAbstractSlider*)other);
    self->m_tickPosition = other->m_tickPosition;
    self->m_tickInterval = other->m_tickInterval;
    self->m_dragOffset = 0; /* 拖动偏移为瞬态，不复制。 */
}

/** @brief 移动语义：基类移动后转移刻度字段，源对象归构造默认值。 */
static void VXSlider_move(XSlider* self, XSlider* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XSlider_init(self, NULL, 0);
    XClass_Parent(XAbstractSlider, EXClass_Move,
                  void(*)(XAbstractSlider*, XAbstractSlider*))(
        (XAbstractSlider*)self, (XAbstractSlider*)other);
    self->m_tickPosition = other->m_tickPosition;
    self->m_tickInterval = other->m_tickInterval;
    self->m_dragOffset = 0;
    other->m_tickPosition = XSliderTickPosition_NoTicks;
    other->m_tickInterval = 0;
    other->m_dragOffset = 0;
}

/* ==================== 生命周期 ==================== */

XVtable* XSlider_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSlider)
    XVTABLE_INHERIT_XCLASS(XAbstractSlider);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VXSlider_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VXSlider_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, VXSlider_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXSlider_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSlider_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSlider_move);

    return XVTABLE_DEFAULT;
}

void XSlider_init(XSlider* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XAbstractSlider_init((XAbstractSlider*)self, parent, flags);
    XClassSetVtable(self, XSlider);

    self->m_tickPosition = XSliderTickPosition_NoTicks;
    self->m_tickInterval = 0;
    self->m_dragOffset = 0;
}

XSlider* XSlider_create_ex(XMemoryType memory, XWidget* parent,
                           XWidgetFlags flags)
{
    XSlider* self = (XSlider*)XMemory_malloc(sizeof(XSlider), memory);
    if (!self) return NULL;
    XSlider_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 刻度（对标 QSlider） ==================== */

int XSlider_tickPosition(const XSlider* self)
{
    return self ? self->m_tickPosition : XSliderTickPosition_NoTicks;
}

void XSlider_setTickPosition(XSlider* self, int position)
{
    if (!self || self->m_tickPosition == position) return;
    if (position < XSliderTickPosition_NoTicks ||
        position > XSliderTickPosition_TicksBothSides)
        return;
    self->m_tickPosition = position;
    XWidget_update((XWidget*)self);
}

int XSlider_tickInterval(const XSlider* self)
{
    return self ? self->m_tickInterval : 0;
}

void XSlider_setTickInterval(XSlider* self, int interval)
{
    if (!self) return;
    if (interval < 0) interval = 0; /* Qt：qMax(0, ts)。 */
    if (self->m_tickInterval == interval) return;
    self->m_tickInterval = interval;
    XWidget_update((XWidget*)self);
}

#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON && XSLIDER_ON */
