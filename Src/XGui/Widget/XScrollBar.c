/**
 * @file       XScrollBar.c
 * @brief      滚动条控件实现（对标 Qt 6.8 QScrollBar 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XScrollBar.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XString.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#if XMENU_ON
#include "XMenu.h"
#endif /* XMENU_ON */
#include "XWidget_Protected.h"
#include <stdio.h>

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

/** @brief 滑块像素长度：handle 长度 = 内容长 * page/(max-min+page+1)
 *         （对标 QStyle::sliderLength 的比例语义简化）。 */
#define XSCROLLBAR_BUTTON_W 16

/** @brief 按钮区长度（显示按钮时=按钮宽，否则 0）。 */
static int xsb_buttonLen(const XScrollBar* self)
{
    return (self && self->m_showButtons) ? XSCROLLBAR_BUTTON_W : 0;
}

/** @brief 滑轨区间原点（按钮之后）。 */
static int xsb_sliderOrigin(const XScrollBar* self, int contentLen)
{
    (void)contentLen;
    return xsb_buttonLen(self);
}

/** @brief 滑轨长度（扣除两端按钮）。 */
static int xsb_sliderLen(const XScrollBar* self, int contentLen)
{
    int btn = xsb_buttonLen(self) * 2;
    int len = contentLen - btn;
    return len > 0 ? len : 0;
}

/** @brief 命中起始按钮（坐标位于滑轨起点之前）。 */
static bool xsb_hitSub(const XScrollBar* self, int pos)
{
    return self->m_showButtons && pos < xsb_buttonLen(self);
}

/** @brief 命中结束按钮。 */
static bool xsb_hitAdd(const XScrollBar* self, int pos, int contentLen)
{
    return self->m_showButtons &&
           pos >= contentLen - xsb_buttonLen(self);
}

static int xsb_handleLength(const XScrollBar* self, int contentLen)
{
    int range = XAbstractSlider_maximum((const XAbstractSlider*)self) -
                XAbstractSlider_minimum((const XAbstractSlider*)self);
    int page = XAbstractSlider_pageStep((const XAbstractSlider*)self);
    int len;
    if (range <= 0) return contentLen;
    len = contentLen * page / (range + page);
    if (len < 16) len = 16;
    if (len > contentLen) len = contentLen;
    return len;
}

/** @brief 滑块原点像素（0 = 内容起点）。 */
static int xsb_handlePos(const XScrollBar* self, int contentLen)
{
    int range = XAbstractSlider_maximum((const XAbstractSlider*)self) -
                XAbstractSlider_minimum((const XAbstractSlider*)self);
    int travel = contentLen - xsb_handleLength(self, contentLen);
    int value = XAbstractSlider_value((const XAbstractSlider*)self);
    if (range <= 0 || travel <= 0) return 0;
    return (value - XAbstractSlider_minimum((const XAbstractSlider*)self)) *
           travel / range;
}

/** @brief 像素位置 → 滑块原点相对内容起点的偏移（拖动中保持按下偏移）。 */
static int xsb_posToValue(const XScrollBar* self, int pos, int contentLen,
                          int* outValue)
{
    int range = XAbstractSlider_maximum((XAbstractSlider*)self) -
                XAbstractSlider_minimum((XAbstractSlider*)self);
    int travel = contentLen - xsb_handleLength(self, contentLen);
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

/* ==================== 事件处理 ==================== */

/** @brief paintEvent：槽体 + 滑块矩形（水平/垂直）。 */
static void VX_scrollBar_paintEvent(XWidget* self, XEvent* event)
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
    if (XStyle_defaultStyle() != NULL) {
        /* Fusion/公共风格接管：渐变槽 + 渐变滑块 + 双层描边走
         * CC_ScrollBar（完整对标 QFusionStyle 非 transient 路径）。 */
        XStyle* style = XStyle_defaultStyle();
        XStyleOption opt;
        XStyleOption_init(&opt, XStyleCC_ScrollBar);
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
        opt.m_horizontal = xsb_horizontal(sb);
        opt.m_sliderMin = XAbstractSlider_minimum((XAbstractSlider*)sb);
        opt.m_sliderMax = XAbstractSlider_maximum((XAbstractSlider*)sb);
        opt.m_sliderValue = XAbstractSlider_value((XAbstractSlider*)sb);
        opt.m_sliderPageStep = XAbstractSlider_pageStep(
            (XAbstractSlider*)sb);
        opt.m_sliderSingleStep = XAbstractSlider_singleStep(
            (XAbstractSlider*)sb);
        opt.m_scrollSubLine = sb->m_showButtons;
        opt.m_scrollAddLine = sb->m_showButtons;
        opt.m_scrollActiveSub = sb->m_activeSub;
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
        int bw = XSCROLLBAR_BUTTON_W;
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
        XRect_init(&r, 0, (h - 8) / 2, w, 8);
        XPainter_fillRect(&painter, &r, groove);
        XRect_init(&r, xsb_sliderOrigin(sb, w) + xsb_handlePos(sb, w),
                   (h - 8) / 2, xsb_handleLength(sb, w), 8);
        XPainter_fillRect(&painter, &r, handle);
    } else {
        XRect_init(&r, (w - 8) / 2, 0, 8, h);
        XPainter_fillRect(&painter, &r, groove);
        XRect_init(&r, (w - 8) / 2, xsb_sliderOrigin(sb, h) +
                   xsb_handlePos(sb, h), 8, xsb_handleLength(sb, h));
        XPainter_fillRect(&painter, &r, handle);
    }
    XPainter_deinit(&painter);
}

/** @brief 鼠标按下：命中滑块开始拖动；命中轨道执行翻页（对标
 *         QScrollBar::mousePressEvent 的轨道翻页语义）。 */
static void VX_scrollBar_mousePressEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    int pos;
    int contentLen;
    int handlePos;
    int handleLen;
    if (!sb || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    contentLen = xsb_horizontal(sb) ? XWidget_width(self)
                                    : XWidget_height(self);
    pos = xsb_horizontal(sb) ? XMouseEvent_position(me).x
                             : XMouseEvent_position(me).y;
    if (sb->m_showButtons && xsb_hitSub(sb, pos)) {
        sb->m_activeSub = true;
        sb->m_activeIsAdd = 0;
        XAbstractSlider_triggerAction((XAbstractSlider*)sb,
            XAbstractSliderSliderAction_SingleStepSub);
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    if (sb->m_showButtons && xsb_hitAdd(sb, pos, contentLen)) {
        sb->m_activeSub = true;
        sb->m_activeIsAdd = 1;
        XAbstractSlider_triggerAction((XAbstractSlider*)sb,
            XAbstractSliderSliderAction_SingleStepAdd);
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    handleLen = xsb_handleLength(sb, contentLen);
    handlePos = xsb_sliderOrigin(sb, contentLen) +
                xsb_handlePos(sb, contentLen);
    if (pos >= handlePos && pos < handlePos + handleLen) {
        /* 命中滑块：进入拖动并抓取鼠标——释放时鼠标可能已移出控件，
         * 不抓取会导致 RELEASE 路由给别的控件、拖动状态卡死
         * （对标 QWidget::grabMouse 拖动语义）。 */
        sb->m_dragging = true;
        sb->m_pressOffset = pos - handlePos;
        XAbstractSlider_setSliderDown((XAbstractSlider*)sb, true);
        XWidget_grabMouse((XWidget*)sb);
    } else {
        /* 轨道翻页：按下点在滑块之前 → 向回翻页；之后 → 向前翻页。 */
        XAbstractSlider_triggerAction((XAbstractSlider*)sb,
            pos < handlePos ? XAbstractSliderSliderAction_PageStepSub
                            : XAbstractSliderSliderAction_PageStepAdd);
    }
    XEvent_accept(event);
}

/** @brief 鼠标移动：拖动中按像素映射更新值。 */
static void VX_scrollBar_mouseMoveEvent(XWidget* self, XEvent* event)
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
    XAbstractSlider_setValue((XAbstractSlider*)sb, value);
    XEvent_accept(event);
}

/** @brief 鼠标释放：结束拖动。 */
static void VX_scrollBar_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sbr = (XScrollBar*)self;
    if (sbr && (sbr->m_activeSub)) {
        sbr->m_activeSub = false;
        XWidget_update(self);
    }
    XScrollBar* sb = (XScrollBar*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    if (!sb || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    if (XMouseEvent_button(me) == XMouseButton_LeftButton && sb->m_dragging) {
        sb->m_dragging = false;
        XAbstractSlider_setSliderDown((XAbstractSlider*)sb, false);
        XWidget_releaseMouse((XWidget*)sb);
    }
    XEvent_accept(event);
}

/* ==================== 右键标准菜单动作执行 ==================== */

/* 上下文目标记录：菜单为模态弹出，同一时刻至多一个滚动条菜单在途。
 * 记录拥有者与右击沿滚动方向分量（"滚动到此处"的目标坐标——此前
 * 右击位置被丢弃、动作零连接），动作触发时校验拥有者防串扰。 */
static const XScrollBar* xsb_ctxOwner = NULL;
static int xsb_ctxPos = 0;

/** @brief "滚动到此处"：右击点沿滑轨映射为滑块原点值并定位
 *         （映射公式与拖动 VX_scrollBar_mouseMoveEvent 同源，
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
static void VX_scrollBar_contextMenuEvent(XWidget* self, XEvent* event)
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

/* ==================== 生命周期与虚表 ==================== */

XVtable* XScrollBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XScrollBar)
    XVTABLE_INHERIT_XCLASS(XAbstractSlider);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_scrollBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_scrollBar_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_scrollBar_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_scrollBar_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ContextMenuEvent,
                             VX_scrollBar_contextMenuEvent);
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
    XSize hint;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XAbstractSlider_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XScrollBar);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XAbstractSlider_setOrientation((XAbstractSlider*)self, orientation);
    /* 对标文档默认值：0..99、singleStep 1、pageStep 10、value 0
       （基类默认一致，此处显式声明意图）。 */
    hint.width = 15;
    hint.height = 15;
    XWidget_setSizeHint((XWidget*)self, &hint);
}

void XScrollBar_setShowButtons(XScrollBar* self, bool show)
{
    if (!self || self->m_showButtons == show) return;
    self->m_showButtons = show;
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
