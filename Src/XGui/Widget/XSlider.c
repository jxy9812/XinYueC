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
 *               时提交值，tracking=false 场景）；
 *             - 按下即 grabMouse、释放解除（拖动语义，XDial/XScrollBar
 *               同款）：真实鼠标拖出控件靠抓取回投，触摸合成事件不参与
 *               buttonDown 隐式抓取，触摸手势状态机转拖以「抓取者在位」
 *               续持仿真，缺抓取超阈值即转滚轮收口、拖动脱手。
 *             invertedAppearance 翻转绘制端；invertedControls 翻转
 *             拖动方向由基类键盘/滚轮处理。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "CXinYueConfig.h"
#if XWIDGET_ON && XABSTRACTSLIDER_ON && XSLIDER_ON

#include "XSlider.h"
#include "XStyle.h"
#include "XStyleOption.h"
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
#define XSLIDER_HANDLE_SIZE 16
#define XSLIDER_GROOVE_H    5
#define XSLIDER_TICK_SIZE   3
/* 刻度留白：Qt QSlider::sizeHint 的 const int TickSpace = 5
 * （qslider.cpp:428），每有一条刻度边就加一份厚度。 */
#define XSLIDER_TICK_SPACE  5
/* 槽长：Qt QSlider::sizeHint 的 const int SliderLength = 84
 * （qslider.cpp:428）。 */
#define XSLIDER_LENGTH      84

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

/** @brief 拖动期鼠标抓取（按下即抓，释放解除；对标 QWidget::grabMouse
 *         拖动语义，XDial/XScrollBar 同款）。
 * @details 真实鼠标靠按下登记的 buttonDown 隐式抓取改投即可跟手，但
 *          触摸合成事件按设计不参与该隐式抓取（见 XWidget.c
 *          dispatchPointerEvent 合成豁免注）：触摸手势状态机转拖时以
 *          「抓取者在位」为准续持左键仿真，缺抓取则超拖动阈值即转滚轮
 *          并在远点合成 RELEASE 收口——触摸拖动只跟 startDragDistance
 *          就脱手，且该远点 RELEASE 按命中派发多不落回本控件，
 *          m_sliderDown 卡死。按下即抓取消除两条分叉。 */
static void xslider_grabForDrag(XWidget* self)
{
    XWidget_grabMouse(self);
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
    /* 左键按下交付键盘焦点（对标 Qt QApplicationPrivate::
     * giveFocusAccordingToFocusPolicy 点击聚焦：QSlider 经
     * qslider.cpp:46 SH_Button_FocusPolicy 取 Qt::StrongFocus，点击
     * 即持焦点、方向键步进可达；XGui 无应用级点击聚焦层，沿
     * XLineEdit.c:768 / XTreeWidget.c:2038 首轮范式在按下路径显式
     * setFocus，窗口型滑块不抢焦点）。修复 night #41②：此前焦点
     * 滞留页3 行编辑，Right 被其消费，XAbstractSlider::keyPressEvent
     * 永不触达（41y_mid_r_srow.png 手柄恒 50% 实证）。 */
    if (!self->m_isWindow) XWidget_setFocus(self);
    pos = XMouseEvent_position(me);
    pickPos = xslider_pick(base, pos);
    handlePos = xslider_valueToPos(slider, base->m_value);

    if (pickPos >= handlePos - XSLIDER_HANDLE_SIZE / 2 &&
        pickPos <= handlePos + XSLIDER_HANDLE_SIZE / 2) {
        /* 命中 handle：记录拖动偏移，进入拖动并发射 sliderPressed。 */
        slider->m_dragOffset = pickPos - handlePos;
        XAbstractSlider_setSliderDown(base, true);
        xslider_grabForDrag(self);
        XEvent_accept(event);
        return;
    }
    /* 点击凹槽：handle 跳到点击处并进入拖动（对标 Qt：左键
     * 绝对定位后 move 持续跟踪，否则后续 mouseMove 全被忽略）。 */
    value = xslider_posToValue(slider, pickPos);
    slider->m_dragOffset = 0;
    XAbstractSlider_setSliderDown(base, true);
    xslider_grabForDrag(self);
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
    XWidget_releaseMouse(self);
    XEvent_accept(event);
}

/* ==================== 绘制 ==================== */

/** @brief 绘制刻度短线（tickPosition 非 NoTicks 时）。
 *  @note 按位判定 TicksAbove/TicksBelow（对标 QSlider::sizeHint 与
 *        QCommonStyle 的 `tickPosition & TicksAbove` 口径）：越界值如 9
 *        含 bit0 ⇒ 画上/左侧刻度，与 Qt 一致（此前按等值匹配会整段不画）。 */
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
    above = (slider->m_tickPosition & XSliderTickPosition_TicksAbove) != 0;
    below = (slider->m_tickPosition & XSliderTickPosition_TicksBelow) != 0;
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

#if XSTYLE_ON
/** @brief 填充滑块样式选项（对标 QSlider::initStyleOption
 *         qslider.cpp:416-446）。
 *  @details 与 Qt 唯一的取舍：Qt 把 upsideDown 单独算出并把 direction
 *           固定为 LTR；X 样式层没有 upsideDown 字段，倒置语义由绘制
 *           路径按 invertedAppearance 自行处理（既有实现），本函数填
 *           写可表达的等价字段。tickPosition **原样**写入（越界值也
 *           保留，样式层按位判定）。 */
void XSlider_initStyleOption(const XSlider* self, XStyleOption* option)
{
    const XAbstractSlider* base;
    if (!self || !option) return;
    base = (const XAbstractSlider*)self;
    XStyleOption_initFrom(option, (const XWidget*)self);
    option->m_type = (uint8_t)XStyleCC_Slider;
    /* 对标 QStyleOptionSlider::Type = SO_Slider。 */
    option->m_optionType = XStyleOptionType_Slider;
    option->m_rect = XWidget_rect((const XWidget*)self);
    option->m_horizontal =
        base->m_orientation == XAbstractSliderOrientation_Horizontal;
    option->m_sliderMin = base->m_min;
    option->m_sliderMax = base->m_max;
    option->m_sliderValue = base->m_value;
    option->m_sliderSingleStep = base->m_singleStep;
    option->m_sliderPageStep = base->m_pageStep;
    option->m_sliderTickInterval = self->m_tickInterval;
    /* m_sliderTickPosition 为 uint8_t（XStyleOption 的文档化小值域字段）；
     * 调用方传 int 原值，位判定所需的低 2 位在 0..255 内无损。 */
    option->m_sliderTickPosition = (uint8_t)self->m_tickPosition;
    option->m_state |= XWidget_isEnabled((const XWidget*)self)
        ? XStyleState_Enabled | XStyleState_Raised : 0;
    /* 对标 Qt：State_KeyboardFocusChange 由持焦控件随 State_HasFocus
     * 一并携带；样式侧把手键盘焦点高亮 outline（XCommonStyle.c
     * CC_Slider 把手段）要求 HasFocus&&KeyboardFocusChange 双位。 */
    if (XWidget_hasFocus((const XWidget*)self))
        option->m_state |= XStyleState_HasFocus |
                           XStyleState_KeyboardFocusChange;
    if (XWidget_underMouse((const XWidget*)self) &&
        XWidget_isEnabled((const XWidget*)self))
        option->m_state |= XStyleState_MouseOver;
}
#endif /* XSTYLE_ON */

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

    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);

#if XSTYLE_ON
    if (XWidget_style((const XWidget*)self) != NULL) {
        /* Fusion/公共风格接管：凹槽/子页高亮/刻度/把手走 CC_Slider。 */
        XStyle* style = XWidget_style((const XWidget*)self);
        XStyleOption opt;
        XSlider_initStyleOption(slider, &opt);
#if XPALETTE_ON
        opt.m_palette = XWidget_palette(self);
#endif
        XStyle_drawComplexControl(style, XStyleCC_Slider, &opt,
                                  &painter, self);
        XPainter_end(&painter);
        XPainter_deinit(&painter);
        return;
    }
#endif /* XSTYLE_ON */
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

    /* 凹槽：Base 底 + Dark 1px 边框（凹陷），已走过部分填 Highlight
     * （对标 Fusion 风格 sub-page 高亮）。 */
    XPainter_fillRect(&painter, &groove, baseColor);
    {
        XRect line;
        line = groove;
        line.height = 1;
        XPainter_fillRect(&painter, &line, dark);
        line = groove;
        line.y = groove.y + groove.height - 1;
        line.height = 1;
        XPainter_fillRect(&painter, &line, dark);
        line = groove;
        line.width = 1;
        XPainter_fillRect(&painter, &line, dark);
        line = groove;
        line.x = groove.x + groove.width - 1;
        line.width = 1;
        XPainter_fillRect(&painter, &line, dark);
    }
    {
        /* 子页高亮：从凹槽起点到 handle 中心（仅水平/垂直主方向）。 */
        uint32_t highlight = xslider_color(slider, XPaletteColorRole_Highlight);
        XRect sub = groove;
        if (base->m_orientation == XAbstractSliderOrientation_Vertical) {
            int h = handlePos - sub.y;
            if (h > 0) {
                if (h > sub.height) h = sub.height;
                sub.height = h;
                XPainter_fillRect(&painter, &sub, highlight);
            }
        } else {
            int wgt = handlePos - sub.x;
            if (wgt > 0) {
                if (wgt > sub.width) wgt = sub.width;
                sub.width = wgt;
                XPainter_fillRect(&painter, &sub, highlight);
            }
        }
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

/* ==================== 尺寸提示（对标 Qt QSlider） ==================== */

/** @brief 刷新尺寸提示（sizeHint + minimumSizeHint）。
 *  @details 对标 Qt 6.8.3 QSlider::sizeHint（qslider.cpp:424-445）：
 *           槽长 84（Qt 的 SliderLength 常量）+ 厚度
 *           （PM_SliderThickness；刻度每占一条边再加 TickSpace=5），
 *           水平方向时宽=槽长、高=厚度；再经样式 CT_Slider 收口。
 *           minimumSizeHint 同 Qt（qslider.cpp:447-460）：取 sizeHint
 *           但主轴回落到 PM_SliderLength（其余轴含刻度留白不变）。
 *  @note    此前本类**既无 sizeHint 也无 minimumSizeHint** ⇒ 布局
 *           条目首选/最小尺寸皆为 0：放进盒布局竖排时控件高度塌 0
 *           （不可见，同 XSpinBox 2026-10-27 的暴露面）。 */
static void xslider_updateSizeHint(XSlider* self)
{
    XSize hint;
    XSize minHint;
    XStyleOption opt;
    int thick;
    int length = XSLIDER_LENGTH;
    int w;
    int h;
    bool horizontal;
    bool ticksAbove;
    bool ticksBelow;
    if (!self) return;
    horizontal = XAbstractSlider_orientation((XAbstractSlider*)self) ==
                XAbstractSliderOrientation_Horizontal;
    XStyleOption_init(&opt, XStyleCT_Slider);
    opt.m_horizontal = horizontal ? 1 : 0;
    opt.m_sliderMin = 0;
    opt.m_sliderMax = 0;
    opt.m_sliderValue = 0;
    opt.m_sliderTickPosition = (int)self->m_tickPosition;
    opt.m_sliderTickInterval = self->m_tickInterval;
    thick = XStyle_pixelMetric(XWidget_style((const XWidget*)self),
                               XStylePM_SliderThickness, &opt);
    if (thick <= 0) thick = 16;
    /* 对标 Qt qslider.cpp:428-440：刻度每占一条边，厚度加 TickSpace
     * （Qt 6.8 实测 84x15 → TicksAbove 84x20 → Both 84x25；Windows 风格
     * 16→21/26，可见是常量 5 而非按 PM_SliderLength 折算）。判定用
     * **位与**（Qt 原文 `d->tickPosition & TicksAbove`）——越界值 9 的
     * bit0 同样加一份厚度。 */
    ticksAbove = (self->m_tickPosition & XSliderTickPosition_TicksAbove) != 0;
    ticksBelow = (self->m_tickPosition & XSliderTickPosition_TicksBelow) != 0;
    if (ticksAbove) thick += XSLIDER_TICK_SPACE;
    if (ticksBelow) thick += XSLIDER_TICK_SPACE;
    w = horizontal ? length : thick;
    h = horizontal ? thick : length;
    {
        XSize content;
        XSize_init(&content, w, h);
        hint = XStyle_sizeFromContents(XWidget_style((const XWidget*)self),
                                       XStyleCT_Slider, &opt, content);
    }
    XWidget_setSizeHint((XWidget*)self, &hint);
    /* minimumSizeHint（qslider.cpp:447-460）：主轴回落到 PM_SliderLength，
     * 另一轴保持含刻度留白的厚度（Qt 实测竖直 TicksAbove 20x15、水平
     * TicksAbove 15x20）。 */
    minHint = hint;
    {
        int minLen = XStyle_pixelMetric(XWidget_style((const XWidget*)self),
                                        XStylePM_SliderLength, &opt);
        if (minLen <= 0) minLen = 16;
        if (horizontal) minHint.width = minLen;
        else minHint.height = minLen;
    }
    XWidget_setMinimumSizeHint((XWidget*)self, &minHint);
}

/* ==================== 生命周期 ==================== */

/* update 函数定义在后段，此处先声明供槽桥接调用。 */
static void xslider_updateSizeHint(XSlider* self);

/* SizeHint 虚槽桥接（对标 Qt 的 sizeHint 虚函数）：先按当前内容
   重算缓存，再读回，等价「查询时重算」语义。 */
static XSize VXSlider_sizeHintSlot(const XWidget* self);
static XSize VXSlider_minimumSizeHintSlot(const XWidget* self);

static XSize VXSlider_sizeHintSlot(const XWidget* self)
{
    if (!self) { XSize out; XSize_init(&out, -1, -1); return out; }
    xslider_updateSizeHint((XSlider*)self);
    return XWidget_sizeHint_base(self);
}

static XSize VXSlider_minimumSizeHintSlot(const XWidget* self)
{
    if (!self) { XSize out; XSize_init(&out, -1, -1); return out; }
    xslider_updateSizeHint((XSlider*)self);
    return XWidget_minimumSizeHint_base(self);
}

XVtable* XSlider_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSlider)
    XVTABLE_INHERIT_XCLASS(XAbstractSlider);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXSlider_sizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint, VXSlider_minimumSizeHintSlot);

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
    /* 缺省垂直——对齐 Qt 6.8 `QSlider(QWidget*)` 委托
     * `QSlider(Qt::Vertical, parent)`（qslider.cpp:253-255，实测方向=2）。
     * 需要水平的调用方显式用 XSlider_init_2(Horizontal, ...)。 */
    XSlider_init_2(self, XAbstractSliderOrientation_Vertical, parent, flags);
}

void XSlider_init_2(XSlider* self, int orientation, XWidget* parent,
                    XWidgetFlags flags)
{
    XWidgetSizePolicy policy;
    if (!self) return;
    if (orientation != XAbstractSliderOrientation_Vertical)
        orientation = XAbstractSliderOrientation_Horizontal;
    XAbstractSlider_init((XAbstractSlider*)self, parent, flags);
    XAbstractSlider_setOrientation((XAbstractSlider*)self, orientation);
    XClassSetVtable(self, XSlider);

    self->m_tickPosition = XSliderTickPosition_NoTicks;
    self->m_tickInterval = 0;
    self->m_dragOffset = 0;
    /* 对标 Qt QSliderPrivate::init 的
     * setFocusPolicy(style()->styleHint(SH_Button_FocusPolicy))
     * （QCommonStyle 该 hint 恒为 Qt::StrongFocus）：滑块可经 Tab/
     * 点击取得焦点并接受方向键步进——修复 night #41：默认 NoFocus
     * 使 Tab 焦点链跳过滑块、XAbstractSlider::keyPressEvent 的方向
     * 键步进永不触发。 */
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_StrongFocus);
    /* 悬停外观 opt-in：绘制按 State_MouseOver 出悬停高亮，ENTER/
     * LEAVE 须标脏自矩形（对标 Qt polish 的 WA_Hover 收口）。 */
    XWidget_setAttribute((XWidget*)self, XWidgetAttribute_Hover, true);
    /* 对标 QSliderPrivate::init（qslider.cpp:57-63）：QSizePolicy sp(
     * Expanding, Fixed, Slider)，垂直方向转置；随后把 OwnSizePolicy
     * **清回 false**（Qt 同函数末尾），使后续 setOrientation 继续按
     * 转置跟随。两参 setSizePolicy 会丢 controlType，故用 Full 版。 */
    policy = XWidgetSizePolicy_create_ex(XWidgetSizePolicy_Expanding,
                                         XWidgetSizePolicy_Fixed,
                                         XWidgetSizePolicyControl_Slider);
    if (orientation == XAbstractSliderOrientation_Vertical)
        policy = XWidgetSizePolicy_transposed(&policy);
    XWidget_setSizePolicyFull((XWidget*)self, &policy);
    XWidget_setAttribute((XWidget*)self,
                         XWidgetAttribute_WState_OwnSizePolicy, false);
    xslider_updateSizeHint(self);
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

XSlider* XSlider_create_ex_2(XMemoryType memory, int orientation,
                             XWidget* parent, XWidgetFlags flags)
{
    XSlider* self = (XSlider*)XMemory_malloc(sizeof(XSlider), memory);
    if (!self) return NULL;
    XSlider_init_2(self, orientation, parent, flags);
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
    /* 对标 QSlider::setTickPosition（qslider.cpp:472-479）：Qt **不校验**
     * 枚举范围，原样存 int（实测 setTickPosition(9) 读回 9）。绘制/尺寸
     * 提示按位判定（&TicksAbove / &TicksBelow），越界值自然落到「含该位
     * 就画那一边」，与 Qt 一致。 */
    self->m_tickPosition = position;
    /* 刻度位置参与 sizeHint（每多一条刻度边厚度 +TickSpace，见
     * xslider_updateSizeHint），故这里同时刷缓存并通知布局重算。 */
    xslider_updateSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
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

#endif /* XGUI_ON */
