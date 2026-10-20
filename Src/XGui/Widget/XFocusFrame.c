/**
 * @file       XFocusFrame.c
 * @brief      焦点指示框控件实现（对标 Qt 6.8 QFocusFrame 全部公共 API）。
 * @details    环绕目标控件绘制焦点指示框：setWidget 后框体挂到目标的
 *             父控件上（对标 d->frameParent = widget->parentWidget() 的
 *             非 frameAbove 路径）并贴合目标矩形外扩 2px；随后以低频
 *             轮询定时器 + 目标 destroyed 信号实现几何/显隐跟随与销毁
 *             解除。跟随机制的轻量等价说明：Qt 经 installEventFilter
 *             随目标 Move/Resize/Show/Hide 事件调整（qfocusframe.cpp:
 *             149-205、243-300），本框架内部几何事件经
 *             XWidget_event_base 直发、不经 XCoreApplication_notify 分
 *             发，事件过滤器收不到——故以 30ms 轮询定时器等价承载。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XFocusFrame.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XFOCUSFRAME_ON

/** @brief 焦点框外扩边距（对标 Qt 样式 PM_FocusFrame*Margin 的轻量
 *         等价：固定 2px，与 paintEvent 的 2px 描边一致）。 */
#define XFOCUSFRAME_MARGIN 2

/** @brief 几何跟随轮询周期 ms（轻量等价 Qt eventFilter 的即时刻度）。 */
#define XFOCUSFRAME_FOLLOW_MS 30

/* ==================== 内部辅助 ==================== */

/** @brief 按目标当前几何贴合（对标 QFocusFramePrivate::updateSize，
 *         qfocusframe.cpp:37-58：geom = widget 几何外扩边距）。 */
static void xfocusframe_updateSize(XFocusFrame* self)
{
    XRect geom;
    if (!self || !self->m_widget) return;
    XRect_init(&geom,
               XWidget_x(self->m_widget) - XFOCUSFRAME_MARGIN,
               XWidget_y(self->m_widget) - XFOCUSFRAME_MARGIN,
               XWidget_width(self->m_widget) + 2 * XFOCUSFRAME_MARGIN,
               XWidget_height(self->m_widget) + 2 * XFOCUSFRAME_MARGIN);
    if (geom.width < 1) geom.width = 1;
    if (geom.height < 1) geom.height = 1;
    XWidget_setGeometryRect((XWidget*)self, &geom);
    self->m_lastX = (int16_t)XWidget_x(self->m_widget);
    self->m_lastY = (int16_t)XWidget_y(self->m_widget);
    self->m_lastW = (uint16_t)XWidget_width(self->m_widget);
    self->m_lastH = (uint16_t)XWidget_height(self->m_widget);
}

/** @brief 重挂父级并按目标几何/显隐同步（对标 QFocusFramePrivate::
 *         update，qfocusframe.cpp:31-46：setParent(frameParent) +
 *         updateSize + show/stackUnder）。 */
static void xfocusframe_update(XFocusFrame* self)
{
    if (!self) return;
    if (!self->m_widget) {
        XWidget_hide((XWidget*)self);
        return;
    }
    if (XWidget_parentWidget((XWidget*)self) !=
        XWidget_parentWidget(self->m_widget))
        XWidget_setParent((XWidget*)self,
                          XWidget_parentWidget(self->m_widget), 0);
    xfocusframe_updateSize(self);
    if (!XWidget_isHidden(self->m_widget))
        XWidget_show((XWidget*)self); /* 框浮于目标上方（对标 raise）。 */
    else
        XWidget_hide((XWidget*)self);
    XWidget_update((XWidget*)self);
}

/** @brief 跟随轮询启停。 */
static void xfocusframe_followStart(XFocusFrame* self)
{
    if (!self || self->m_followTimer != XTIMER_INVALID_ID) return;
    self->m_followTimer = XObject_startTimer_ms(
        (XObject*)self, XFOCUSFRAME_FOLLOW_MS, XTimerType_CoarseTimer);
}

static void xfocusframe_followStop(XFocusFrame* self)
{
    if (!self || self->m_followTimer == XTIMER_INVALID_ID) return;
    XObject_killTimer((XObject*)self, self->m_followTimer);
    self->m_followTimer = XTIMER_INVALID_ID;
}

/* ==================== 虚槽实现 ==================== */

/** @brief 轮询拍：目标几何/显隐变化时贴合与显隐同步（轻量等价
 *  QFocusFrame::eventFilter 的 Move/Resize/Show/Hide 分支）。 */
static void VXFocusFrame_timerEvent(XObject* object, XTimerEvent* event)
{
    XFocusFrame* ff = (XFocusFrame*)object;
    if (!ff || !event) return;
    if (XTimerEvent_timerId(event) == ff->m_followTimer) {
        if (ff->m_widget) {
            int x = XWidget_x(ff->m_widget);
            int y = XWidget_y(ff->m_widget);
            int w = XWidget_width(ff->m_widget);
            int h = XWidget_height(ff->m_widget);
            if (x != ff->m_lastX || y != ff->m_lastY ||
                w != ff->m_lastW || h != ff->m_lastH) {
                xfocusframe_update(ff);
            } else if (XWidget_isHidden(ff->m_widget) !=
                       XWidget_isHidden((XWidget*)ff)) {
                /* 显隐同步（对标 eventFilter 的 Show/Hide 分支）。 */
                if (XWidget_isHidden(ff->m_widget))
                    XWidget_hide((XWidget*)ff);
                else
                    XWidget_show((XWidget*)ff);
            }
        }
        XEvent_accept((XEvent*)event);
        return;
    }
    XEvent_ignore((XEvent*)event);
}

/** @brief 目标销毁槽：解除关联并隐藏（对标 eventFilter 的 Destroy →
 *  setWidget(nullptr)；destroyed 在析构帧内同步发射，先于内存归还）。 */
static void xfocusframe_widgetDestroyedSlot(XObject* receiver,
                                            XVarList* args)
{
    XFocusFrame* ff = (XFocusFrame*)receiver;
    (void)args;
    if (!ff) return;
    ff->m_widget = NULL;
    xfocusframe_followStop(ff);
    XWidget_hide((XWidget*)ff);
}

static void VXFocusFrame_paintEvent(XWidget* self, XEvent* event)
{
    XFocusFrame* ff = (XFocusFrame*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    uint32_t highlight;
    int w;
    int h;
    if (!ff || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
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
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Highlight);
        highlight = XColor_rgba(&c);
    }
#else
    highlight = 0xFF3080C0u;
#endif /* XPALETTE_ON */
#if XSTYLE_ON
    if (XStyle_defaultStyle() != NULL) {
        /* Fusion/公共风格接管：焦点框走 PE_FrameFocusRect。 */
        XStyle* style = XStyle_defaultStyle();
        XStyleOption opt;
        XStyleOption_init(&opt, XStylePE_FrameFocusRect);
        XRect_init(&opt.m_rect, 0, 0, w, h);
        opt.m_state = XStyleState_Enabled | XStyleState_HasFocus;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette(self);
#endif
        XStyle_drawPrimitive(style, XStylePE_FrameFocusRect, &opt,
                             &painter, self);
        XPainter_deinit(&painter);
        return;
    }
#endif /* XSTYLE_ON */
    XRect_init(&r, 0, 0, w, 2);
    XPainter_fillRect(&painter, &r, highlight);
    XRect_init(&r, 0, h - 2, w, 2);
    XPainter_fillRect(&painter, &r, highlight);
    XRect_init(&r, 0, 0, 2, h);
    XPainter_fillRect(&painter, &r, highlight);
    XRect_init(&r, w - 2, 0, 2, h);
    XPainter_fillRect(&painter, &r, highlight);
    XPainter_deinit(&painter);
}

static void VXFocusFrame_deinit(XFocusFrame* self)
{
    if (!self) return;
    xfocusframe_followStop(self);
    if (self->m_widget) {
        XObject_disconnect_1((XObject*)self->m_widget,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self,
                             xfocusframe_widgetDestroyedSlot);
        self->m_widget = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XFocusFrame_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFocusFrame)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXFocusFrame_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXFocusFrame_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFocusFrame_deinit);
    return XVTABLE_DEFAULT;
}

void XFocusFrame_init(XFocusFrame* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XFocusFrame);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_followTimer = XTIMER_INVALID_ID;
    /* 对标 QFocusFrame 构造（qfocusframe.cpp:124-129）：鼠标穿透 +
     * NoFocus——框体贴合目标后叠于其上，不得截获目标的点击/焦点。 */
    XWidget_setAttribute((XWidget*)self,
                         XWidgetAttribute_TransparentForMouseEvents, true);
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_NoFocus);
}

XFocusFrame* XFocusFrame_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XFocusFrame* self = (XFocusFrame*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XFocusFrame_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XFocusFrame_setWidget(XFocusFrame* self, XWidget* widget)
{
    if (!self) return;
    if (widget == self->m_widget) return;
    if (self->m_widget) {
        XObject_disconnect_1((XObject*)self->m_widget,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self,
                             xfocusframe_widgetDestroyedSlot);
    }
    xfocusframe_followStop(self);
    self->m_widget = widget;
    if (widget && !XWidget_isWindow(widget)) {
        /* 对标 qfocusframe.cpp:163-205：框随父链挂接并贴合目标几何
         * （本框架取 frameParent=目标父控件的非 frameAbove 简化路径）；
         * 目标为顶层窗口时不跟踪（Qt 同 isWindow 分支 → 置空隐藏）。
         * 跟随机制见文件头 @details（轮询定时器 + destroyed 解除）。 */
        XObject_connect_1((XObject*)widget,
                          XSignal(XObject_destroyed_signal),
                          (XObject*)self,
                          xfocusframe_widgetDestroyedSlot,
                          XConnectionType_Direct);
        xfocusframe_update(self);
        xfocusframe_followStart(self);
    } else {
        self->m_widget = NULL;
        XWidget_hide((XWidget*)self);
    }
    XWidget_update((XWidget*)self);
}

XWidget* XFocusFrame_widget(const XFocusFrame* self)
{
    return self ? self->m_widget : NULL;
}

#endif /* XWIDGET_ON && XFOCUSFRAME_ON */

#endif /* XGUI_ON */
