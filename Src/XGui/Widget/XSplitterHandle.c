/**
 * @file       XSplitterHandle.c
 * @brief      分割器把手部件实现（对标 Qt 6.8 QSplitterHandle，
 *             qsplitter.h:96-125 + qsplitter.cpp:86-324；行号级行为
 *             映射见头文件注）。
 * @author     XinYueC 团队
 */

#include "XSplitterHandle.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XCursor.h" /* 方向光标（控件级静态光标，对标 Qt 把手）。 */
#include "XPrintf.h"

#include "XAlgorithm.h"
#include "XObject.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XFRAME_ON && XSPLITTER_ON

/* ==================== 内部工具 ==================== */

/** @brief 轴向拾取坐标（水平分割取 x、垂直取 y，对标
 *  QSplitterPrivate::pick）。 */
static int xh_pick(int x, int y, bool horiz)
{
    return horiz ? x : y;
}

/** @brief 本把手是否水平向条带布局（水平分割=页左右排=把手竖条）。
 *  与 XSplitter.c 的 xsp_horiz 同判据：m_orient 非 Vertical 即水平。 */
static bool xh_horiz(const XSplitterHandle* self)
{
    return !self || self->m_orient != 2;
}

/** @brief 静态方向光标（水平分割=左右调宽 SizeHor；垂直=SizeVer）。
 *  对标 Qt QSplitterHandle 控件级光标：构造/setOrientation 时设一次。
 *  旧分割器本体 hover 命中切换（XSplitter.c:733-752）随部件化退役——
 *  把手即命中区，光标不随悬停位置变化，无需 move 洪水路径。 */
static void xh_applyCursor(XSplitterHandle* self)
{
    XCursor* cursor;
    if (!self) return;
    cursor = XCursor_create_shape(xh_horiz(self) ? XCursor_SizeHor
                                                 : XCursor_SizeVer);
    if (cursor) {
        XWidget_setCursor((XWidget*)self, cursor);
        XClassDelete(cursor);
    }
}

/** @brief 把手点击信号发射（空闲点击/双击，信号标识挂在 XSplitter，
 *  对标 Qt 把手事件驱动分割器信号的路向；实现同 XSplitter.c 的
 *  xsp_emitHandleClicked——跨文件故此处独立成小函数）。 */
static void xh_emitHandleClicked(XSplitter* sp, int index, int doubleClick)
{
    XVarList* args;
    if (!sp) return;
    args = XVarList_Create(XVar(int, index), XVar(int, doubleClick));
    if (!args) return;
    if (((XObject*)sp)->m_signalSlot) {
        XObject_emitSignal((XObject*)sp,
                           (size_t)XSplitter_handleClicked_signal, args,
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 事件处理 ==================== */

/** @brief 绘制：CE_Splitter 交样式，state=Enabled/MouseOver(hover)/
 *  Sunken(pressed)（对标 QSplitterHandle::paintEvent，qsplitter.cpp:
 *  306-324；XCommonStyle 的 CE_Splitter 现实现消费 Mid/Light 与
 *  m_horizontal，hover/sunken 位供 Fusion 等 richer 实现使用）。 */
static void VX_handle_paintEvent(XWidget* self, XEvent* event)
{
    XSplitterHandle* h = (XSplitterHandle*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect line;
    int w;
    int hh;
    if (!h || !event) return;
    w = XWidget_width(self);
    hh = XWidget_height(self);
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
    XRect_init(&line, 0, 0, w, hh);
#if XSTYLE_ON
    if (XStyle_defaultStyle() != NULL) {
        XStyleOption opt;
        XStyleOption_init(&opt, XStyleCE_Splitter);
        opt.m_rect = line;
        opt.m_state = XWidget_isEnabled(self) ? XStyleState_Enabled : 0;
        if (h->m_hover) opt.m_state |= XStyleState_MouseOver;
        if (h->m_pressed) opt.m_state |= XStyleState_Sunken;
        /* true=把手是水平条带（垂直 splitter、上下分栏）。 */
        opt.m_horizontal = xh_horiz(h) ? false : true;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette(self);
#endif
        XStyle_drawControl(XStyle_defaultStyle(), XStyleCE_Splitter, &opt,
                           &painter, self);
    } else
#endif
    {
        /* 无样式回落：Mid 色实条（与旧分割器条带回落同色）。 */
        uint32_t mid;
#if XPALETTE_ON
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
#else
        mid = 0xFF000000u;
#endif
        XPainter_fillRect(&painter, &line, mid);
    }
    XPainter_deinit(&painter);
}

/** @brief 进入/离开：hover 位刷新（MouseOver 绘制态重绘，对标 Qt 把手
 *  的 underMouse 态位；XWidget.h:473-474 Enter/Leave 槽）。 */
static void VX_handle_enterEvent(XWidget* self, XEvent* event)
{
    XSplitterHandle* h = (XSplitterHandle*)self;
    (void)event;
    if (!h || h->m_hover) return;
    h->m_hover = 1;
    XWidget_update(self);
}

static void VX_handle_leaveEvent(XWidget* self, XEvent* event)
{
    XSplitterHandle* h = (XSplitterHandle*)self;
    (void)event;
    if (!h || !h->m_hover) return;
    h->m_hover = 0;
    XWidget_update(self);
}

/** @brief 按下：mouseOffset=pick(local)、pressed=1、抓取鼠标（对标
 *  QSplitterHandle::mousePressEvent qsplitter.cpp:273-281；释放点可能
 *  已移出把手，不抓取会把 RELEASE 路由给别的控件、拖动态卡死）。 */
static void VX_handle_mousePressEvent(XWidget* self, XEvent* event)
{
    XSplitterHandle* h = (XSplitterHandle*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XPoint pos;
    if (!h || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    if (!h->m_splitter || h->m_index < 0) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
    h->m_mouseOffset = (int16_t)xh_pick(pos.x, pos.y, xh_horiz(h) != 0);
    h->m_pressPick = h->m_mouseOffset;
    h->m_pressed = 1;
    h->m_dragTargetValid = 0;
    XWidget_update(self); /* Sunken 绘制态。 */
    XWidget_grabMouse(self);
    XEvent_accept(event);
}

/** @brief 拖动中：目标位=条带原点+拾取−按压偏移（对标
 *  QSplitterHandle::mouseMoveEvent，qsplitter.cpp:255-268）。opaque→
 *  moveSplitter 实时落位；非 opaque→closestLegalPosition 记待落位并
 *  setRubberBand 呈现，页几何留到释放（qsplitter.cpp:255-301 非
 *  opaque 分支）。 */
static void VX_handle_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XSplitterHandle* h = (XSplitterHandle*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XPoint pos;
    int target;
    if (!h || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    if (!h->m_pressed || !h->m_splitter) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
    /* 父（分割器）映射坐标 = 把手原点 + 本地位。 */
    target = (xh_horiz(h) ? XWidget_x(self) : XWidget_y(self))
             + xh_pick(pos.x, pos.y, xh_horiz(h) != 0)
             - h->m_mouseOffset;
    if (!XSplitterHandle_opaqueResize(h)) {
        h->m_dragTarget =
            (int16_t)XSplitterHandle_closestLegalPosition(h, target);
        h->m_dragTargetValid = 1;
        xsp_setRubberBand(h->m_splitter, h->m_dragTarget);
        XEvent_accept(event);
        return;
    }
    XSplitterHandle_moveSplitter(h, target, h->m_index);
    XEvent_accept(event);
}

/** @brief 释放收尾（对标 QSplitterHandle::mouseReleaseEvent，
 *  qsplitter.cpp:286-301）：非 opaque 此时才 setRubberBand(-1)+
 *  落位；释放点距按压点 <=2px=空闲点击（XGui 扩展，发
 *  handleClicked(m_index,0)——迁移自旧分割器 ：800-809）。 */
static void VX_handle_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XSplitterHandle* h = (XSplitterHandle*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XPoint pos;
    if (!h || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    if (!h->m_pressed) {
        XEvent_ignore(event);
        return;
    }
    if (XMouseEvent_button(me) != XMouseButton_NoButton &&
        XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    if (h->m_splitter && !XSplitterHandle_opaqueResize(h)) {
        if (h->m_dragTargetValid)
            XSplitterHandle_moveSplitter(h, h->m_dragTarget, h->m_index);
        xsp_setRubberBand(h->m_splitter, -1); /* deleteLater（Qt :1301-1327）。 */
    }
    h->m_pressed = 0;
    h->m_dragTargetValid = 0;
    XWidget_update(self);
    pos = XMouseEvent_position(me);
    {
        int pick = xh_pick(pos.x, pos.y, xh_horiz(h) != 0);
        int delta = pick - h->m_pressPick;
        if (delta < 0) delta = -delta;
        if (delta <= 2 && h->m_splitter)
            xh_emitHandleClicked(h->m_splitter, h->m_index, 0);
    }
    XWidget_releaseMouse(self);
    XEvent_accept(event);
}

/** @brief 把手双击（XGui 扩展：Qt 6.8/5.15 的 QSplitterHandle 均无
 *  mouseDoubleClickEvent——不内置折叠策略，只发 handleClicked(
 *  m_index,1) 由使用方裁定；迁移自旧分割器 ：821-841）。 */
static void VX_handle_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XSplitterHandle* h = (XSplitterHandle*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    if (!h || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) return;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    if (h->m_splitter)
        xh_emitHandleClicked(h->m_splitter, h->m_index, 1);
    XEvent_accept(event);
}

/* ==================== 生命周期与虚表 ==================== */

XVtable* XSplitterHandle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSplitterHandle)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_handle_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_EnterEvent, VX_handle_enterEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent, VX_handle_leaveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_handle_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_handle_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_handle_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VX_handle_mouseDoubleClickEvent);
    /* resizeEvent 不重载：Qt tiny-mode setMask（qsplitter.cpp:204-226）
       在 XGui 无窗口遮罩对应物，把手全条带命中（登记偏差）。 */
    return XVTABLE_DEFAULT;
}

void XSplitterHandle_init(XSplitterHandle* self, XOrientation orientation,
                          XSplitter* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    /* 挂入分割器子链（触发父侧 CHILD_ADDED；XSplitter 的把手工厂在
       m_blockChildAdd 护栏内创建本部件，事件被吞）。 */
    XWidget_init(&self->m_base, (XWidget*)parent, flags);
    XClassSetVtable(self, XSplitterHandle);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_splitter = parent;
    self->m_orient = (uint8_t)(int)orientation;
    self->m_index = -1;
    xh_applyCursor(self);
}

XSplitterHandle* XSplitterHandle_create_ex(XMemoryType memory,
                                           XOrientation orientation,
                                           XSplitter* parent,
                                           XWidgetFlags flags)
{
    XSplitterHandle* self =
        (XSplitterHandle*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XSplitterHandle_init(self, orientation, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 属性 ==================== */

void XSplitterHandle_setOrientation(XSplitterHandle* self,
                                    XOrientation orientation)
{
    int orient = (int)orientation;
    if (!self || self->m_orient == orient) return;
    self->m_orient = (uint8_t)orient;
    xh_applyCursor(self);
    XWidget_update((XWidget*)self);
}

XOrientation XSplitterHandle_orientation(const XSplitterHandle* self)
{
    if (!self) return XOrientation_Horizontal;
    return (XOrientation)self->m_orient;
}

XSplitter* XSplitterHandle_splitter(const XSplitterHandle* self)
{
    return self ? self->m_splitter : NULL;
}

bool XSplitterHandle_opaqueResize(const XSplitterHandle* self)
{
    if (!self || !self->m_splitter) return false;
    return XSplitter_opaqueResize(self->m_splitter);
}

int XSplitterHandle_splitterIndex(const XSplitterHandle* self)
{
    /* O(1) 读 m_index（槽位单点同步）；Qt 经 indexOf(this) 线性查
       （qsplitter.cpp:166/:183），XGui 等价优化。 */
    if (!self || !self->m_splitter || self->m_index < 0) return -1;
    return self->m_index;
}

/* ==================== 拖动落位 ==================== */

int XSplitterHandle_closestLegalPosition(const XSplitterHandle* self,
                                         int position)
{
    int min;
    int max;
    if (!self || !self->m_splitter || self->m_index < 0) return position;
    if (!XSplitter_getRange(self->m_splitter, self->m_index, &min, &max))
        return position;
    if (position < min) position = min;
    if (position > max) position = max;
    return position;
}

void XSplitterHandle_moveSplitter(XSplitterHandle* self, int position,
                                  int index)
{
    (void)index; /* 形参保 Qt 槽签名；实现恒用 m_index（单点同步真源）。 */
    if (!self || !self->m_splitter || self->m_index < 0) return;
    XSplitter_moveSplitter(self->m_splitter, position, self->m_index);
}

/* ==================== 尺寸提示（CT_Splitter 样式接线） ==================== */

XSize XSplitterHandle_sizeHint(const XSplitterHandle* self)
{
    XSize out;
    XSize content;
    XSize size;
    XStyleOption opt;
    int hw;
    XSize_init(&out, 0, 0);
    if (!self || !self->m_splitter) return out;
    hw = XSplitter_handleWidth(self->m_splitter);
#if XSTYLE_ON
    if (XStyle_defaultStyle() != NULL) {
        XStyleOption_init(&opt, XStyleCT_Splitter);
        {
            XRect r;
            XRect_init(&r, 0, 0, hw, hw);
            opt.m_rect = r;
        }
        /* true=把手是水平条带（垂直 splitter、上下分栏）。 */
        opt.m_horizontal = xh_horiz(self) ? false : true;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette((XWidget*)self);
#endif
        XSize_init(&content, hw, hw);
        size = XStyle_sizeFromContents(XStyle_defaultStyle(),
                                       XStyleCT_Splitter, &opt, content);
        XSize_init(&out, size.width, size.height);
        return out;
    }
#endif /* XSTYLE_ON */
    XSize_init(&out, hw, hw);
    return out;
}

#endif /* XWIDGET_ON && XFRAME_ON && XSPLITTER_ON */
