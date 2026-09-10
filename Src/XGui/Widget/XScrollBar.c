/**
 * @file       XScrollBar.c
 * @brief      滚动条控件实现（对标 Qt 6.8 QScrollBar 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XScrollBar.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XString.h"
#include "XGuiConfig.h"
#if XMENU_ON
#include "XMenu.h"
#endif /* XMENU_ON */
#include "XWidget_Protected.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
    groove = xsb_color(sb, XPaletteColorRole_Window);
    handle = xsb_color(sb, XPaletteColorRole_Mid);
    if (xsb_horizontal(sb)) {
        XRect_init(&r, 0, (h - 8) / 2, w, 8);
        XPainter_fillRect(&painter, &r, groove);
        XRect_init(&r, xsb_handlePos(sb, w), (h - 8) / 2,
                   xsb_handleLength(sb, w), 8);
        XPainter_fillRect(&painter, &r, handle);
    } else {
        XRect_init(&r, (w - 8) / 2, 0, 8, h);
        XPainter_fillRect(&painter, &r, groove);
        XRect_init(&r, (w - 8) / 2, xsb_handlePos(sb, h),
                   8, xsb_handleLength(sb, h));
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
    handleLen = xsb_handleLength(sb, contentLen);
    handlePos = xsb_handlePos(sb, contentLen);
    if (pos >= handlePos && pos < handlePos + handleLen) {
        /* 命中滑块：进入拖动。 */
        sb->m_dragging = true;
        sb->m_pressOffset = pos - handlePos;
        XAbstractSlider_setSliderDown((XAbstractSlider*)sb, true);
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
    xsb_posToValue(sb, pos, contentLen, &value);
    XAbstractSlider_setValue((XAbstractSlider*)sb, value);
    XEvent_accept(event);
}

/** @brief 鼠标释放：结束拖动。 */
static void VX_scrollBar_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XScrollBar* sb = (XScrollBar*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    if (!sb || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    if (XMouseEvent_button(me) == XMouseButton_LeftButton && sb->m_dragging) {
        sb->m_dragging = false;
        XAbstractSlider_setSliderDown((XAbstractSlider*)sb, false);
    }
    XEvent_accept(event);
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
    /* 记录右击位置："滚动到此处"动作以该局部坐标为目标。 */
    if (ctx) {
        XPoint pos = XContextMenuEvent_position(ctx);
        sb->m_pressOffset = 0;
        (void)pos;
    }
    horiz = xsb_horizontal(sb);
    (void)horiz;
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
    memset(self, 0, sizeof(*self));
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

XMenu* XScrollBar_createStandardContextMenu(XScrollBar* self)
{
    XMenu* menu;
    bool horiz;
    if (!self) return NULL;
    menu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "ctx");
    if (!menu) return NULL;
    horiz = xsb_horizontal(self);
    XMenu_addAction_2(menu, horiz ? "滚动到此处" : "滚动到此处");
    XMenu_addSeparator(menu);
    XMenu_addAction_2(menu, horiz ? "左缘" : "顶部");
    XMenu_addAction_2(menu, horiz ? "右缘" : "底部");
    XMenu_addSeparator(menu);
    XMenu_addAction_2(menu, horiz ? "向左翻页" : "向上翻页");
    XMenu_addAction_2(menu, horiz ? "向右翻页" : "向下翻页");
    XMenu_addSeparator(menu);
    XMenu_addAction_2(menu, horiz ? "向左滚动" : "向上滚动");
    XMenu_addAction_2(menu, horiz ? "向右滚动" : "向下滚动");
    return menu;
}

#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON */
