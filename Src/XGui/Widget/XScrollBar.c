/**
 * @file       XScrollBar.c
 * @brief      滚动条控件实现（对标 Qt 6.8 QScrollBar 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XScrollBar.h"
#include "XAbstractSlider_Protected.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#if XWINDOWEVENT_ON
#include "XWindowEvent.h"
#endif /* XWINDOWEVENT_ON */
#include "XPainter.h"
#include "XVarList.h"
#include "XString.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#if XMENU_ON
#include "XMenu.h"
#endif /* XMENU_ON */
#include "XWidget_Protected.h"

#if XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON

/* ==================== 内部工具 ==================== */

/** @brief 是否水平方向。 */
static int xsb_horizontal(const XScrollBar* self)
{
    return self->m_base.m_orientation ==
           (int)XAbstractSliderOrientation_Horizontal;
}

/** @brief 取调色板角色颜色（无调色板能力时回退灰阶）。 */
static uint32_t xsb_color(const XScrollBar* self, XPaletteColorRole role)
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

#if XSTYLE_ON
/** @brief 填充本控件口径的 CC_ScrollBar 样式选项（几何/提示共用）。
 *  @note 只填标量字段（不取字体度量）：本函数也在命中测试/每次事件里
 *        调用，XStyleOption_initFrom 的字体度量副本有分配代价。 */
static void xsb_initOption(const XScrollBar* self, XStyleOption* opt)
{
    if (!opt) return;
    XStyleOption_init(opt, XStyleCC_ScrollBar);
    opt->m_styleObject = (XObject*)self;
    opt->m_state = XWidget_isEnabled((const XWidget*)self)
                       ? XStyleState_Enabled : 0;
    if (XWidget_hasFocus((const XWidget*)self))
        opt->m_state |= XStyleState_HasFocus;
    if (XWidget_underMouse((const XWidget*)self) &&
        XWidget_isEnabled((const XWidget*)self))
        opt->m_state |= XStyleState_MouseOver;
    opt->m_horizontal = xsb_horizontal(self) ? 1 : 0;
    opt->m_sliderMin = self->m_base.m_min;
    opt->m_sliderMax = self->m_base.m_max;
    opt->m_sliderValue = self->m_base.m_value;
    opt->m_sliderSingleStep = self->m_base.m_singleStep;
    opt->m_sliderPageStep = self->m_base.m_pageStep;
    opt->m_scrollSubLine = self->m_showButtons ? 1 : 0;
    opt->m_scrollAddLine = self->m_showButtons ? 1 : 0;
    opt->m_scrollActiveSub = self->m_activeSub ? 1 : 0;
}
#endif /* XSTYLE_ON */

/** @brief 无样式时的按钮厚度回退（XCommonStyle 缺省 extent 为 16）。 */
#define XSCROLLBAR_BUTTON_W 16
/** @brief 无样式时的滑块最小长度回退。 */
#define XSCROLLBAR_SLIDER_MIN 16

/** @brief 两端按钮厚度（对标 PM_ScrollBarExtent；隐藏按钮时 0）。
 *  @note 命中几何必须与样式层 CC_ScrollBar 的绘制几何同源（
 *        XCommonStyle/XFusionStyle 的 SC_ScrollBarSubLine 用
 *        PM_ScrollBarExtent），否则按钮可见但点击落进轨道翻页。 */
static int xsb_buttonLen(const XScrollBar* self)
{
    int extent = 0;
    if (!self || !self->m_showButtons) return 0;
#if XSTYLE_ON
    {
        XStyleOption opt;
        xsb_initOption(self, &opt);
        extent = XStyle_pixelMetric(XWidget_style((const XWidget*)self),
                                    XStylePM_ScrollBarExtent, &opt);
    }
#endif /* XSTYLE_ON */
    if (extent <= 0) extent = XSCROLLBAR_BUTTON_W;
    return extent;
}

/** @brief 滑块最小长度（对标 PM_ScrollBarSliderMin）。 */
static int xsb_sliderMin(const XScrollBar* self)
{
    int minLen = 0;
#if XSTYLE_ON
    {
        XStyleOption opt;
        xsb_initOption(self, &opt);
        minLen = XStyle_pixelMetric(XWidget_style((const XWidget*)self),
                                    XStylePM_ScrollBarSliderMin, &opt);
    }
#else
    (void)self;
#endif /* XSTYLE_ON */
    if (minLen <= 0) minLen = XSCROLLBAR_SLIDER_MIN;
    return minLen;
}

/** @brief 该内容长度下的实际按钮厚度：不超过内容一半
 *         （对标样式层 SC_ScrollBarSubLine/AddLine 的
 *         `bw = qMin(rect.size() / 2, extent)`，防极小控件两按钮重叠）。 */
static int xsb_buttonLenAt(const XScrollBar* self, int contentLen)
{
    int bw = xsb_buttonLen(self);
    if (bw <= 0) return 0;
    if (contentLen / 2 < bw) bw = contentLen / 2;
    return bw;
}

/** @brief 滑轨区间原点（按钮之后）。 */
static int xsb_sliderOrigin(const XScrollBar* self, int contentLen)
{
    return xsb_buttonLenAt(self, contentLen);
}

/** @brief 滑轨长度（扣除两端按钮）。 */
static int xsb_sliderLen(const XScrollBar* self, int contentLen)
{
    int btn = xsb_buttonLenAt(self, contentLen) * 2;
    int len = contentLen - btn;
    return len > 0 ? len : 0;
}

/** @brief 命中起始按钮（坐标位于滑轨起点之前）。 */
static bool xsb_hitSub(const XScrollBar* self, int pos, int contentLen)
{
    return self->m_showButtons && pos < xsb_buttonLenAt(self, contentLen);
}

/** @brief 命中结束按钮。 */
static bool xsb_hitAdd(const XScrollBar* self, int pos, int contentLen)
{
    return self->m_showButtons &&
           pos >= contentLen - xsb_buttonLenAt(self, contentLen);
}

/** @brief 滑块长度（在**滑轨长度**上按 page/(range+page) 折算，
 *         下限 PM_ScrollBarSliderMin——与样式层 CC_ScrollBar 同口径）。 */
static int xsb_handleLength(const XScrollBar* self, int sliderLen)
{
    int range = XAbstractSlider_maximum((const XAbstractSlider*)self) -
                XAbstractSlider_minimum((const XAbstractSlider*)self);
    int page = XAbstractSlider_pageStep((const XAbstractSlider*)self);
    int len;
    if (range <= 0) return sliderLen;
    len = sliderLen * page / (range + page);
    if (len < xsb_sliderMin(self)) len = xsb_sliderMin(self);
    if (len > sliderLen) len = sliderLen;
    return len;
}

/** @brief 滑块原点像素（相对滑轨原点；0 = 滑轨起点）。 */
static int xsb_handlePos(const XScrollBar* self, int sliderLen)
{
    int range = XAbstractSlider_maximum((const XAbstractSlider*)self) -
                XAbstractSlider_minimum((const XAbstractSlider*)self);
    int travel = sliderLen - xsb_handleLength(self, sliderLen);
    int value = XAbstractSlider_value((const XAbstractSlider*)self);
    if (range <= 0 || travel <= 0) return 0;
    return (value - XAbstractSlider_minimum((const XAbstractSlider*)self)) *
           travel / range;
}

/** @brief 像素位置 → 滑块原点相对滑轨的偏移（拖动中保持按下偏移）。 */
static int xsb_posToValue(const XScrollBar* self, int pos, int sliderLen,
                          int* outValue)
{
    int range = XAbstractSlider_maximum((XAbstractSlider*)self) -
                XAbstractSlider_minimum((XAbstractSlider*)self);
    int travel = sliderLen - xsb_handleLength(self, sliderLen);
    int handlePos = pos - self->m_pressOffset;
    if (range <= 0 || travel <= 0) {
        *outValue = XAbstractSlider_minimum((XAbstractSlider*)self);
        return 1;
    }
    if (handlePos < 0) handlePos = 0;
    if (handlePos > travel) handlePos = travel;
    *outValue = XAbstractSlider_minimum((XAbstractSlider*)self) +
                (int)(((long long)handlePos * range + travel / 2) / travel);
    return 1;
}

/** @brief 样式 transient 提示（SH_ScrollBar_Transient，Qt Fusion=0）。 */
static bool xsb_transientHint(const XScrollBar* self)
{
#if XSTYLE_ON
    XStyleOption opt;
    xsb_initOption(self, &opt);
    return XStyle_styleHint(XWidget_style((const XWidget*)self),
                            XStyleSH_ScrollBar_Transient, &opt,
                            (const XWidget*)self) != 0;
#else
    (void)self;
    return false; /* 无样式层：按 Fusion 非 transient 口径。 */
#endif /* XSTYLE_ON */
}

/** @brief 按方向/样式重算 sizeHint（对标 QScrollBar::sizeHint
 *         qscrollbar.cpp:411-425）：extent*2+sliderMin，横竖互换。 */
static void xsb_updateSizeHint(XScrollBar* self)
{
    XSize hint;
#if XSTYLE_ON
    XSize content;
#endif /* XSTYLE_ON */
    int extent = XSCROLLBAR_BUTTON_W;
    int sliderMin = XSCROLLBAR_SLIDER_MIN;
    int w;
    int h;
    if (!self) return;
#if XSTYLE_ON
    {
        XStyleOption opt;
        xsb_initOption(self, &opt);
        extent = XStyle_pixelMetric(XWidget_style((const XWidget*)self),
                                    XStylePM_ScrollBarExtent, &opt);
        if (extent <= 0) extent = XSCROLLBAR_BUTTON_W;
        sliderMin = XStyle_pixelMetric(XWidget_style((const XWidget*)self),
                                       XStylePM_ScrollBarSliderMin, &opt);
        if (sliderMin <= 0) sliderMin = XSCROLLBAR_SLIDER_MIN;
        if (xsb_horizontal(self)) {
            w = extent * 2 + sliderMin;
            h = extent;
        } else {
            w = extent;
            h = extent * 2 + sliderMin;
        }
        XSize_init(&content, w, h);
        hint = XStyle_sizeFromContents(XWidget_style((const XWidget*)self),
                                       XStyleCT_ScrollBar, &opt, content);
    }
#else
    if (xsb_horizontal(self)) {
        w = extent * 2 + sliderMin;
        h = extent;
    } else {
        w = extent;
        h = extent * 2 + sliderMin;
    }
    XSize_init(&hint, w, h);
#endif /* XSTYLE_ON */
    XWidget_setSizeHint((XWidget*)self, &hint);
}

/* ==================== 事件处理 ==================== */

/** @brief paintEvent：槽体 + 滑块矩形（水平/垂直）。 */
static void VXScrollBar_paintEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    int w = XWidget_width(self);
    int h = XWidget_height(self);
    uint32_t groove;
    uint32_t handle;
    XRect r;
    if (!sb || !event) return;
    if (w <= 2 || h <= 2) return;
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
        /* Fusion/公共风格接管：渐变槽 + 渐变滑块 + 双层描边走
         * CC_ScrollBar（完整对标 QFusionStyle 非 transient 路径）。 */
        XStyle* style = XWidget_style((const XWidget*)self);
        XStyleOption opt;
        xsb_initOption(sb, &opt);
        {
            XRect rr;
            XRect_init(&rr, 0, 0, w, h);
            opt.m_rect = rr;
        }
        opt.m_state = XWidget_isEnabled(self)
            ? XStyleState_Enabled | XStyleState_Raised : 0;
        if (XWidget_hasFocus(self))
            opt.m_state |= XStyleState_HasFocus;
        if (XWidget_underMouse(self) && XWidget_isEnabled(self))
            opt.m_state |= XStyleState_MouseOver;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette(self);
#endif
        XStyle_drawComplexControl(style, XStyleCC_ScrollBar, &opt,
                                  &painter, self);
        XPainter_deinit(&painter);
        return;
    }
#endif /* XSTYLE_ON */
    groove = xsb_color(sb, XPaletteColorRole_Window);
    handle = xsb_color(sb, XPaletteColorRole_Mid);
    if (sb->m_showButtons) {
        /* 按钮区（原路径：Button 底 + Mid 箭头）。 */
        uint32_t btnC = xsb_color(sb, XPaletteColorRole_Button);
        int bw = xsb_buttonLenAt(sb, xsb_horizontal(sb) ? w : h);
        if (bw <= 0) bw = XSCROLLBAR_BUTTON_W;
        if (xsb_horizontal(sb)) {
            XRect b1, b2;
            XRect_init(&b1, 0, 0, bw, h);
            XRect_init(&b2, w - bw, 0, bw, h);
            XPainter_fillRect(&painter, &b1, btnC);
            XPainter_fillRect(&painter, &b2, btnC);
            XPainter_setPen(&painter, handle);
            XPainter_drawLine(&painter, bw / 2 + 3, h / 2,
                              bw / 2 - 3, h / 2 - 3);
            XPainter_drawLine(&painter, bw / 2 - 3, h / 2 - 3,
                              bw / 2 - 3, h / 2 + 3);
            XPainter_drawLine(&painter, w - bw / 2 - 3, h / 2,
                              w - bw / 2 + 3, h / 2 - 3);
            XPainter_drawLine(&painter, w - bw / 2 + 3, h / 2 - 3,
                              w - bw / 2 + 3, h / 2 + 3);
        } else {
            XRect b1, b2;
            XRect_init(&b1, 0, 0, w, bw);
            XRect_init(&b2, 0, h - bw, w, bw);
            XPainter_fillRect(&painter, &b1, btnC);
            XPainter_fillRect(&painter, &b2, btnC);
            XPainter_setPen(&painter, handle);
            XPainter_drawLine(&painter, w / 2, bw / 2 + 3,
                              w / 2 - 3, bw / 2 - 3);
            XPainter_drawLine(&painter, w / 2 - 3, bw / 2 - 3,
                              w / 2 + 3, bw / 2 - 3);
            XPainter_drawLine(&painter, w / 2, h - bw / 2 - 3,
                              w / 2 - 3, h - bw / 2 + 3);
            XPainter_drawLine(&painter, w / 2 - 3, h - bw / 2 + 3,
                              w / 2 + 3, h - bw / 2 + 3);
        }
    }
    if (xsb_horizontal(sb)) {
        int sliderLen = xsb_sliderLen(sb, w);
        int origin = xsb_sliderOrigin(sb, w);
        XRect_init(&r, origin, (h - 8) / 2, sliderLen, 8);
        XPainter_fillRect(&painter, &r, groove);
        XRect_init(&r, origin + xsb_handlePos(sb, sliderLen),
                   (h - 8) / 2, xsb_handleLength(sb, sliderLen), 8);
        XPainter_fillRect(&painter, &r, handle);
    } else {
        int sliderLen = xsb_sliderLen(sb, h);
        int origin = xsb_sliderOrigin(sb, h);
        XRect_init(&r, (w - 8) / 2, origin, 8, sliderLen);
        XPainter_fillRect(&painter, &r, groove);
        XRect_init(&r, (w - 8) / 2, origin + xsb_handlePos(sb, sliderLen),
                   8, xsb_handleLength(sb, sliderLen));
        XPainter_fillRect(&painter, &r, handle);
    }
    XPainter_deinit(&painter);
}

/** @brief 鼠标按下：命中滑块开始拖动；命中轨道执行翻页或（中键）
 *         绝对定位（对标 QScrollBar::mousePressEvent qscrollbar.cpp:
 *         545-638）。
 *  @details Qt 6.8 语义：
 *           - 首部守卫：maximum==minimum、或已有别的键按下、或按键不是
 *             左键/（中键且 SH_ScrollBar_MiddleClickAbsolutePosition）
 *             时直接返回；
 *           - 按下 AddLine/SubLine → 单步 + 500ms/50ms 连发；
 *           - 按下滑块 → setSliderDown(true) 进入拖动；
 *           - 按下轨道页区：Fusion 的 SH_ScrollBar_LeftClickAbsolute
 *             Position=0 ⇒ 左键走翻页；**中键**（Fusion 该提示=1）把
 *             滑块中心直接定位到点击处（click - sliderLength/2），随后
 *             以 clickOffset=sliderLength/2 进入拖动。 */
static void VXScrollBar_mousePressEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XMouseButton button;
    int pos;
    int contentLen;
    int sliderLen;
    int handlePos;
    int handleLen;
    if (!sb || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    button = XMouseEvent_button(me);
    /* 只接受左键与中键（中键绝对定位是 QScrollBar 专有语义）。 */
    if (button != XMouseButton_LeftButton &&
        button != XMouseButton_MiddleButton) {
        XEvent_ignore(event);
        return;
    }
    /* 事件发生时还有别的键按着 ⇒ 不响应（Qt `e->buttons() & ~e->button()`）。 */
    if (XMouseEvent_buttons(me) & ~(XMouseButton)button) {
        XEvent_ignore(event);
        return;
    }
    /* 对标 QScrollBar::mousePressEvent 首行：maximum == minimum（无范围）
     * 时直接返回——此时既不该翻页也不该发按下信号。 */
    if (XAbstractSlider_maximum((const XAbstractSlider*)sb) ==
        XAbstractSlider_minimum((const XAbstractSlider*)sb)) {
        XEvent_ignore(event);
        return;
    }
    contentLen = xsb_horizontal(sb) ? XWidget_width(self)
                                    : XWidget_height(self);
    pos = xsb_horizontal(sb) ? XMouseEvent_position(me).x
                             : XMouseEvent_position(me).y;
    if (sb->m_showButtons && xsb_hitSub(sb, pos, contentLen)) {
        sb->m_activeSub = true;
        sb->m_activeIsAdd = 0;
        XAbstractSlider_triggerAction((XAbstractSlider*)sb,
            XAbstractSliderSliderAction_SingleStepSub);
        /* [长按连发 2026-10-07] 对标 QAbstractSlider::setRepeatAction
         * （initialDelay 500ms → 每 50ms 连发，qscrollbar.cpp 同口径）。 */
        XAbstractSlider_setRepeatAction((XAbstractSlider*)sb,
            XAbstractSliderSliderAction_SingleStepSub, 500, 50);
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    if (sb->m_showButtons && xsb_hitAdd(sb, pos, contentLen)) {
        sb->m_activeSub = true;
        sb->m_activeIsAdd = 1;
        XAbstractSlider_triggerAction((XAbstractSlider*)sb,
            XAbstractSliderSliderAction_SingleStepAdd);
        XAbstractSlider_setRepeatAction((XAbstractSlider*)sb,
            XAbstractSliderSliderAction_SingleStepAdd, 500, 50);
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    sliderLen = xsb_sliderLen(sb, contentLen);
    handleLen = xsb_handleLength(sb, sliderLen);
    handlePos = xsb_sliderOrigin(sb, contentLen) +
                xsb_handlePos(sb, sliderLen);
    if (pos >= handlePos && pos < handlePos + handleLen) {
        /* 命中滑块：进入拖动并抓取鼠标——释放时鼠标可能已移出控件，
         * 不抓取会导致 RELEASE 路由给别的控件、拖动状态卡死
         * （对标 QWidget::grabMouse 拖动语义）。 */
        sb->m_dragging = true;
        sb->m_pressOffset = pos - handlePos;
        XAbstractSlider_setSliderDown((XAbstractSlider*)sb, true);
        XWidget_grabMouse((XWidget*)sb);
    } else if (button == XMouseButton_MiddleButton) {
        /* 中键绝对定位（Qt qscrollbar.cpp:585-596）：滑块中心跳到点击处
         * 继续拖动；pressOffset = sliderLength/2 与 Qt 的 clickOffset
         * 同值，后续 mouseMove 走同一像素→值映射。顺序与 Qt 一致——
         * 先 setSliderPosition（此时 sliderDown 仍为 false，初始跳位不发
         * sliderMoved；tracking 时经 SliderMove 提交值），再
         * setSliderDown(true) 发 sliderPressed。 */
        int value = 0;
        sb->m_dragging = true;
        sb->m_pressOffset = handleLen / 2;
        xsb_posToValue(sb, pos - xsb_sliderOrigin(sb, contentLen),
                       sliderLen, &value);
        XAbstractSlider_setSliderPosition((XAbstractSlider*)sb, value);
        XAbstractSlider_setSliderDown((XAbstractSlider*)sb, true);
        XWidget_grabMouse((XWidget*)sb);
    } else {
        /* 轨道翻页：按下点在滑块之前 → 向回翻页；之后 → 向前翻页。 */
        XAbstractSliderSliderAction page =
            pos < handlePos ? XAbstractSliderSliderAction_PageStepSub
                            : XAbstractSliderSliderAction_PageStepAdd;
        XAbstractSlider_triggerAction((XAbstractSlider*)sb, page);
        /* [长按连发] 轨道按住持续翻页（同 Qt 按压连发口径）。 */
        XAbstractSlider_setRepeatAction((XAbstractSlider*)sb, page, 500, 50);
    }
    XEvent_accept(event);
}

/** @brief 鼠标移动：拖动中按像素映射更新**句柄位置**（对标
 *         QScrollBarPrivate::pixelPosToRangeValue → setSliderPosition，
 *         qscrollbar.cpp:657-673）。tracking=false 时只动 position，
 *         释放才提交值（Qt 同）。 */
static void VXScrollBar_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    int pos;
    int contentLen;
    int value = 0;
    if (!sb || !event || !sb->m_dragging ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    contentLen = xsb_horizontal(sb) ? XWidget_width(self)
                                    : XWidget_height(self);
    pos = xsb_horizontal(sb) ? XMouseEvent_position(me).x
                             : XMouseEvent_position(me).y;
    xsb_posToValue(sb, pos - xsb_sliderOrigin(sb, contentLen),
                   xsb_sliderLen(sb, contentLen), &value);
    XAbstractSlider_setSliderPosition((XAbstractSlider*)sb, value);
    XEvent_accept(event);
}

/** @brief 鼠标释放：结束拖动（左键/中键绝对定位拖动均收口）。 */
static void VXScrollBar_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sbr = (XScrollBar*)self;
    XMouseButton button;
    if (sbr && (sbr->m_activeSub)) {
        sbr->m_activeSub = false;
        XWidget_update(self);
    }
    XScrollBar* sb = (XScrollBar*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    if (!sb || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    button = XMouseEvent_button(me);
    if ((button == XMouseButton_LeftButton ||
         button == XMouseButton_MiddleButton) && sb->m_dragging) {
        sb->m_dragging = false;
        XAbstractSlider_setSliderDown((XAbstractSlider*)sb, false);
        XWidget_releaseMouse((XWidget*)sb);
    }
    /* [长按连发] 释放收闸（对标 qscrollbar 释放 stopRepeatAction）。 */
    if (button == XMouseButton_LeftButton ||
        button == XMouseButton_MiddleButton)
        XAbstractSlider_setRepeatAction((XAbstractSlider*)sb,
            XAbstractSliderSliderAction_NoAction, 0, 0);
    XEvent_accept(event);
}

/** @brief 隐藏：收掉按压连发与拖动闸（对标 QScrollBar::hideEvent
 *         qscrollbar.cpp:692-700：pressedControl 清空 + SliderNoAction）。
 *  @note  Qt 侧只用 pressedControl 表达按压，故 hideEvent 不动
 *         sliderDown；X 侧按压态拆成 m_dragging + m_activeSub + sliderDown
 *         三处，隐藏时若不把 sliderDown 一并抬起，控件会带着「按下」
 *         状态回来（Qt 无此中间态）。 */
static void VXScrollBar_hideEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    if (!sb) return;
    /* Qt 的条件是 pressedControl 非空（滑块/轨道/按钮任一按下）；
     * X 侧该信息散在 m_dragging、m_activeSub 与重复动作三处，逐项判定
     * 后统一收闸。 */
    if (sb->m_dragging || sb->m_activeSub ||
        XAbstractSlider_repeatAction((const XAbstractSlider*)sb) !=
            XAbstractSliderSliderAction_NoAction) {
        sb->m_dragging = false;
        sb->m_activeSub = false;
        XAbstractSlider_setRepeatAction((XAbstractSlider*)sb,
            XAbstractSliderSliderAction_NoAction, 0, 0);
        if (XAbstractSlider_isSliderDown((const XAbstractSlider*)sb)) {
            XAbstractSlider_setSliderDown((XAbstractSlider*)sb, false);
            XWidget_releaseMouse(self);
        }
    }
    XClass_Parent(XWidget, EXWidget_HideEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/* ==================== 右键标准菜单动作执行 ==================== */

/* 上下文目标记录：菜单为模态弹出，同一时刻至多一个滚动条菜单在途。
 * 记录拥有者与右击沿滚动方向分量（"滚动到此处"的目标坐标——此前
 * 右击位置被丢弃、动作零连接），动作触发时校验拥有者防串扰。 */
static const XScrollBar* xsb_ctxOwner = NULL;
static int xsb_ctxPos = 0;

/** @brief "滚动到此处"：右击点沿滑轨映射为滑块原点值并定位
 *         （映射公式与拖动 VXScrollBar_mouseMoveEvent 同源，
 *         pressOffset 已由 contextMenuEvent 清零）。 */
static void xsb_actScrollHereSlot(XObject* receiver, XVarList* args)
{
    XScrollBar* sb = (XScrollBar*)receiver;
    int contentLen;
    int value = 0;
    (void)args;
    if (!sb || (const XScrollBar*)sb != xsb_ctxOwner) return;
    contentLen = xsb_horizontal(sb) ? XWidget_width((XWidget*)sb)
                                    : XWidget_height((XWidget*)sb);
    xsb_posToValue(sb, xsb_ctxPos - xsb_sliderOrigin(sb, contentLen),
                   xsb_sliderLen(sb, contentLen), &value);
    XAbstractSlider_setValue((XAbstractSlider*)sb, value);
}

/** @brief "顶部/左缘"：滚动到最小值。 */
static void xsb_actMinimumSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XAbstractSlider_setValue((XAbstractSlider*)receiver,
                                 XAbstractSlider_minimum(
                                     (const XAbstractSlider*)receiver));
}

/** @brief "底部/右缘"：滚动到最大值（本库滚动条最大值即可滚跨度，
 *         对标 QScrollArea 的 range 收敛）。 */
static void xsb_actMaximumSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XAbstractSlider_setValue((XAbstractSlider*)receiver,
                                 XAbstractSlider_maximum(
                                     (const XAbstractSlider*)receiver));
}

/** @brief "向上(左)翻页"。 */
static void xsb_actPageSubSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XAbstractSlider_triggerAction((XAbstractSlider*)receiver,
            XAbstractSliderSliderAction_PageStepSub);
}

/** @brief "向下(右)翻页"。 */
static void xsb_actPageAddSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XAbstractSlider_triggerAction((XAbstractSlider*)receiver,
            XAbstractSliderSliderAction_PageStepAdd);
}

/** @brief "向上(左)滚动"（单步）。 */
static void xsb_actStepSubSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XAbstractSlider_triggerAction((XAbstractSlider*)receiver,
            XAbstractSliderSliderAction_SingleStepSub);
}

/** @brief "向下(右)滚动"（单步）。 */
static void xsb_actStepAddSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver)
        XAbstractSlider_triggerAction((XAbstractSlider*)receiver,
            XAbstractSliderSliderAction_SingleStepAdd);
}

/** @brief 右键标准菜单（对标 QScrollBar::contextMenuEvent）：滚动到此处/
 *         上(左)缘/下(右)缘/翻页/单步，条目文本随方向变化；
 *         popup + DeleteOnClose 呈现。 */
static void VXScrollBar_contextMenuEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    XContextMenuEvent* ctx = (XContextMenuEvent*)event;
    XMenu* menu;
    XPoint global;
    bool horiz;
    if (!sb || !event ||
        XEvent_type(event) != XEVENT_TYPE_CONTEXT_MENU) return;
    menu = XScrollBar_createStandardContextMenu(sb);
    if (!menu) return;
    horiz = xsb_horizontal(sb);
    /* 记录右击位置："滚动到此处"动作以该局部坐标为目标（此前坐标被
     * 丢弃）；同时清拖动偏移基线，供滑轨→值映射按零偏移复用。 */
    if (ctx) {
        XPoint pos = XContextMenuEvent_position(ctx);
        sb->m_pressOffset = 0;
        xsb_ctxOwner = sb;
        xsb_ctxPos = horiz ? pos.x : pos.y;
    }
    global = XContextMenuEvent_globalPosition(ctx);
    XWidget_setAttribute((XWidget*)menu, XWidgetAttribute_DeleteOnClose,
                         true);
    XMenu_popup(menu, &global);
    XEvent_accept(event);
}

/* ==================== 滚轮 / 事件 ==================== */

/** @brief 滚轮：对标 QScrollBar::wheelEvent（qscrollbar.cpp:475-497）。
 *  @details 与基类 wheelEvent 的差别（这正是此前「跨轴方向与 Qt 相反」
 *           的根因）：
 *           1) 按 **|dx| > |dy|** 选轴（基类只看 y 是否非零）；
 *           2) 水平轨道上若 y 不是主轴、但 x 非零 → **不消费**（Qt 原文
 *              "This is to avoid chaotic scrolling on touchpads"）；
 *           3) delta 取 `horizontal ? -dx : dy`，再交给方向感知的
 *              scrollByDelta（其内部对水平轴再取负一次，等价 Qt 的
 *              双重取负 + scrollbar 缺省 invertedControls=true）。
 *           实测锚点：水平滚动条 dx=+120（value=50）→ 47；竖直滚动条
 *           dx=+120 → 47；水平滚动条同时带 dx=+120/dy=+120 → 忽略。 */
static void VXScrollBar_wheelEvent(XWidget* self, XEvent* event)
{
#if XWINDOWEVENT_ON
    XScrollBar* sb = (XScrollBar*)self;
    XWheelEvent* we;
    XPoint delta;
    int horizontal;
    int d;
    XKeyboardModifiers mods;
    if (!sb || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
    we = (XWheelEvent*)event;
    XEvent_ignore(event);
    delta = XWheelEvent_angleDelta(we);
    horizontal = (delta.x < 0 ? -delta.x : delta.x) >
                 (delta.y < 0 ? -delta.y : delta.y);
    if (!horizontal && delta.x != 0 &&
        XAbstractSlider_orientation((const XAbstractSlider*)sb) ==
            XAbstractSliderOrientation_Horizontal)
        return; /* 触摸板混沌滚动守卫：保持 ignore。 */
    mods = XWheelEvent_modifiers(we);
    d = horizontal ? -delta.x : delta.y;
    if (XAbstractSlider_scrollByDelta(
            (XAbstractSlider*)sb,
            horizontal ? XAbstractSliderOrientation_Horizontal
                       : XAbstractSliderOrientation_Vertical,
            d, mods)) {
        XEvent_accept(event);
        return;
    }
    /* 高分辨率滚轮：angleDelta 为 0 时回落 pixelDelta（X 扩展）。 */
    {
        XPoint pixel = XWheelEvent_pixelDelta(we);
        int pd = (pixel.y != 0) ? pixel.y : pixel.x;
        if (pd != 0 &&
            XAbstractSlider_scrollByDelta(
                (XAbstractSlider*)sb,
                horizontal ? XAbstractSliderOrientation_Horizontal
                           : XAbstractSliderOrientation_Vertical,
                pd, mods))
            XEvent_accept(event);
    }
#else
    XEvent_ignore(event);
#endif /* XWINDOWEVENT_ON */
}

/** @brief 事件入口：对标 QScrollBar::event（qscrollbar.cpp:434-473）的
 *         StyleChange 分支——样式变化后按 SH_ScrollBar_Transient 重新
 *         决定是否显示两端按钮（用户经 X 扩展显式覆盖过则不再回写）。
 *  @note  Qt 的 HoverEnter/HoverLeave/HoverMove 子控件追踪与 transient
 *         收缩 flash 定时器未移植：X 的滚动事件不带悬停坐标（没有
 *         XHoverEvent），XStyleOption 也没有 hover 子控件字段，需先补
 *         这两个子系统；见 gaps 的 deferred 登记。 */
static bool VXScrollBar_event(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    if (sb && event && XEvent_type(event) == XEVENT_TYPE_STYLE_CHANGE) {
        if (!sb->m_showButtonsExplicit) {
            bool show = !xsb_transientHint(sb);
            if (sb->m_showButtons != show) {
                sb->m_showButtons = show;
                XWidget_update(self);
            }
        }
        /* 样式切换会改变 PM_ScrollBarExtent/SliderMin，sizeHint 必须刷新并
         * 通知布局（QScrollBar::event 的 StyleChange 分支本意同此）。 */
        xsb_updateSizeHint(sb);
        XWidget_updateGeometry(self);
    }
    return XClass_Parent(XWidget, EXObject_Event,
                         bool(*)(XObject*, XEvent*))((XObject*)self, event);
}

/** @brief sizeHint 虚槽（对标 QScrollBar::sizeHint）：按当前方向与样式
 *         度量现场重算，方向切换后自动横竖互换。 */
static XSize VXScrollBar_sizeHintSlot(const XWidget* self)
{
    if (!self) {
        XSize out;
        XSize_init(&out, -1, -1);
        return out;
    }
    xsb_updateSizeHint((XScrollBar*)self);
    return XWidget_sizeHint_base(self);
}

/* ==================== 生命周期与虚表 ==================== */

XVtable* XScrollBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XScrollBar)
    XVTABLE_INHERIT_XCLASS(XAbstractSlider);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXScrollBar_sizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXScrollBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXScrollBar_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXScrollBar_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXScrollBar_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VXScrollBar_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXScrollBar_event);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VXScrollBar_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ContextMenuEvent,
                             VXScrollBar_contextMenuEvent);
    return XVTABLE_DEFAULT;
}

void XScrollBar_init(XScrollBar* self, XWidget* parent, XWidgetFlags flags)
{
    XScrollBar_init_2(self, (int)XAbstractSliderOrientation_Vertical,
                      parent, flags);
}

void XScrollBar_init_2(XScrollBar* self, int orientation,
                       XWidget* parent, XWidgetFlags flags)
{
    XWidgetSizePolicy policy;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractSlider_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XScrollBar);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XAbstractSlider_setOrientation((XAbstractSlider*)self, orientation);
    /* 对标 QScrollBarPrivate::init (qscrollbar.cpp:345)：
     * invertedControls = true（"since the scrollbar is inverted by
     * default"）——裸滚动条自带的滚轮方向因此与基类缺省相反（Qt
     * scrollByDelta 末尾按 invertedControls 取负）。XAbstractScrollArea
     * 的滚轮走自身 wheelEvent（不经本虚槽），不受影响。 */
    XAbstractSlider_setInvertedControls((XAbstractSlider*)self, true);
    /* 两端按钮缺省可见（对标 qscrollbar.cpp:353：transient 取
     * SH_ScrollBar_Transient 原值，Fusion=0 ⇒ !transient ⇒ 画
     * Add/SubLine 按钮）。 */
    self->m_showButtons = !xsb_transientHint(self);
    self->m_showButtonsExplicit = false;
    /* sizeHint（qscrollbar.cpp:411-425）：extent*2+sliderMin，横竖互换。
     * minimumSizeHint **不设置**——Qt 未覆写，基类返回无效尺寸
     * (-1,-1)（本机实测），布局按 Minimum 策略回落 sizeHint。 */
    xsb_updateSizeHint(self);
    /* 对标 QScrollBarPrivate::init（qscrollbar.cpp:357-361）：
     * QSizePolicy(Minimum, Fixed, Slider)，竖直转置；随后清
     * OwnSizePolicy 位（Qt 同函数），setOrientation 才能继续转置。 */
    policy = XWidgetSizePolicy_create_ex(XWidgetSizePolicy_Minimum,
                                         XWidgetSizePolicy_Fixed,
                                         XWidgetSizePolicyControl_Slider);
    if (orientation == XAbstractSliderOrientation_Vertical)
        policy = XWidgetSizePolicy_transposed(&policy);
    XWidget_setSizePolicyFull((XWidget*)self, &policy);
    XWidget_setAttribute((XWidget*)self,
                         XWidgetAttribute_WState_OwnSizePolicy, false);
    /* 悬停外观 opt-in：绘制按 State_MouseOver 出悬停高亮，ENTER/
     * LEAVE 须标脏自矩形（对标 Qt polish 的 WA_Hover 收口）。 */
    XWidget_setAttribute((XWidget*)self, XWidgetAttribute_Hover, true);
}

void XScrollBar_setShowButtons(XScrollBar* self, bool show)
{
    if (!self || self->m_showButtons == show) {
        if (self) self->m_showButtonsExplicit = true;
        return;
    }
    self->m_showButtons = show;
    self->m_showButtonsExplicit = true;
    XWidget_update((XWidget*)self);
}

bool XScrollBar_showButtons(const XScrollBar* self)
{ return self ? self->m_showButtons : false; }

XScrollBar* XScrollBar_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags)
{
    XScrollBar* self = (XScrollBar*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XScrollBar_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XScrollBar* XScrollBar_create_ex_2(XMemoryType memory, int orientation,
                                   XWidget* parent, XWidgetFlags flags)
{
    XScrollBar* self = (XScrollBar*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XScrollBar_init_2(self, orientation, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 右键标准菜单 ==================== */

/** @brief 追加动作并接通触发槽（动作由菜单拥有，连接随任一侧析构
 *         自动摘除；此前动作零连接、选择无效果）。 */
static void xsb_addWiredAction(XMenu* menu, const char* utf8, XScrollBar* sb,
                               XSlotFunc1 slot)
{
    XAction* action = XMenu_addAction_2(menu, utf8);
    if (!action) return;
    XObject_connect_1((XObject*)action,
                      XSignal(XAction_triggered_signal),
                      (XObject*)sb, slot, XConnectionType_Direct);
}

XMenu* XScrollBar_createStandardContextMenu(XScrollBar* self)
{
    XMenu* menu;
    bool horiz;
    if (!self) return NULL;
    menu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "ctx");
    if (!menu) return NULL;
    horiz = xsb_horizontal(self);
    xsb_addWiredAction(menu, "滚动到此处", self, xsb_actScrollHereSlot);
    XMenu_addSeparator(menu);
    xsb_addWiredAction(menu, horiz ? "左缘" : "顶部", self,
                       xsb_actMinimumSlot);
    xsb_addWiredAction(menu, horiz ? "右缘" : "底部", self,
                       xsb_actMaximumSlot);
    XMenu_addSeparator(menu);
    xsb_addWiredAction(menu, horiz ? "向左翻页" : "向上翻页", self,
                       xsb_actPageSubSlot);
    xsb_addWiredAction(menu, horiz ? "向右翻页" : "向下翻页", self,
                       xsb_actPageAddSlot);
    XMenu_addSeparator(menu);
    xsb_addWiredAction(menu, horiz ? "向左滚动" : "向上滚动", self,
                       xsb_actStepSubSlot);
    xsb_addWiredAction(menu, horiz ? "向右滚动" : "向下滚动", self,
                       xsb_actStepAddSlot);
    return menu;
}

#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON */

#endif /* XGUI_ON */
