/**
 * @file       XSplashScreen.c
 * @brief      启动画面控件实现（对标 Qt 6.8 QSplashScreen 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XSplashScreen.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XAlignment.h"
#include "XWidget_Protected.h"
#if XWINDOW_ON
#include "XWindow.h"
#endif
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XThread.h"
#include "XDateTime.h"
#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"      /* setPixmap 的屏幕居中（对标 Qt）。 */
#endif
#if XSCREEN_ON
#include "XScreen.h"
#endif

#if XWIDGET_ON && XSPLASHSCREEN_ON

/** @brief argsDel 回调：释放 XString 载荷（对齐 XObject 信号惯例）。 */
static void xsp2_str_args_del(XVarList* list)
{
    XVarList_args_1(list, XString*, val);
    if (val) XClassDelete((XClass*)val);
}

static void xsp2_emitMessageChanged(XSplashScreen* self, const char* text)
{
    XVarList* args;
    XString* value;
    if (!self) return;
    value = XString_create_utf8(text ? text : "");
    if (!value) return;
    args = XVarList_Create(XVar(XString*, value));
    if (!args) {
        XClassDelete((XClass*)value);
        return;
    }
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XSplashScreen_messageChanged_signal, args,
                           xsp2_str_args_del, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_setArgsDel(args, xsp2_str_args_del);
        XVarList_delete(args);
    }
}

static void VX_splash_paintEvent(XWidget* self, XEvent* event)
{
    XSplashScreen* sp = (XSplashScreen*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    int h;
    if (!sp || !event) return;
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
    {
        /* 确定性黑底：后备存储新页在 fbdev 双缓冲/GPU 路径上内容
         * 未定义（此前黑观感只是 X11 calloc 零页的偶然），黑底必须
         * 自填再叠 pixmap/文字。 */
        XRect full;
        XRect_init(&full, 0, 0, XWidget_width(self), h);
        XPainter_fillRect(&painter, &full, 0xFF000000u);
    }
#if XPIXMAP_ON && XPAINTER_PIXMAP_ON
    if (sp->m_pixmap)
        XPainter_drawPixmap(&painter, sp->m_pixmap, 0, 0);
#endif /* XPIXMAP_ON && XPAINTER_PIXMAP_ON */
    if (sp->m_message && XString_toUtf8(sp->m_message) &&
        XString_toUtf8(sp->m_message)[0] != '\0') {
        XFont font = XWidget_font(self);
        XPainter_setFont(&painter, &font);
        XPainter_drawText(&painter, 8, h - 12,
                          XString_toUtf8(sp->m_message), sp->m_color);
        XClassDeinit(&font);
    }
    XPainter_deinit(&painter);
}

static void VX_splash_mousePressEvent(XWidget* self, XEvent* event)
{
    XSplashScreen* sp = (XSplashScreen*)self;
    if (!sp || !event) return;
    if (XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS)
        XWidget_close(self);
}

static void VXSplashScreen_deinit(XSplashScreen* self)
{
    if (!self) return;
    if (self->m_pixmap) {
        /* setPixmap 深拷贝所得，对象拥有；无人级联回收，须在此释放。 */
        XClassDelete(self->m_pixmap);
        self->m_pixmap = NULL;
    }
    if (self->m_message) {
        XClassDelete(self->m_message);
        self->m_message = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XSplashScreen_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSplashScreen)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_splash_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_splash_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSplashScreen_deinit);
    return XVTABLE_DEFAULT;
}

void XSplashScreen_init(XSplashScreen* self, XWidget* parent,
                        XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    /* 对标 QSplashScreen 构造（qsplashscreen.cpp:99-103）：
     *   `QWidget(..., Qt::SplashScreen | Qt::FramelessWindowHint | f)`
     * —— 类型位与无边框位**无条件并入**调用方 flags。本机 Qt 6.8.0 实测：
     * 无论是否带 pixmap，默认 windowFlags() == 0x80f
     * （SplashScreen|Window|FramelessWindowHint）。启动画面因此恒为
     * 「无边框 + 置顶 + 无任务栏」窗口（SplashScreen 类型位自带
     * WindowStaysOnTop 语义），而不是普通顶层窗。 */
    XWidget_init(&self->m_class, parent,
                 (XWidgetFlags)(flags |
                                (XWidgetFlags)XWindowType_SplashScreen |
                                (XWidgetFlags)XWindowType_FramelessWindowHint));
    XClassSetVtable(self, XSplashScreen);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_alignment = (int)XAlignment_Left;
    self->m_color = 0xFF000000u;
    XWidget_resize((XWidget*)self, 400, 300);
}

XSplashScreen* XSplashScreen_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags)
{
    XSplashScreen* self =
        (XSplashScreen*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XSplashScreen_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

#if XPIXMAP_ON
void XSplashScreen_setPixmap(XSplashScreen* self, const XPixmap* pixmap)
{
    XSize size;
    if (!self) return;
    if (self->m_pixmap) {
        XClassDelete(self->m_pixmap);
        self->m_pixmap = NULL;
    }
    if (pixmap) {
        /* 第一版为深拷贝（copyRect 全区域），调用方 pixmap 可随即释放。 */
        XPixmap* copy = XPixmap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XSize size;
        XRect full;
        XPixmap_size(pixmap, &size);
        if (copy) {
            XRect_init(&full, 0, 0, size.width, size.height);
            XPixmap_copyRect(pixmap, &full, copy);
            self->m_pixmap = copy;
            XWidget_resize((XWidget*)self, size.width, size.height);
            /* 对标 QSplashScreen::setPixmap（qsplashscreen.cpp:130-142）：
             *   `resize(r.size()); move(screen()->geometry().center() -
             *    r.center());` —— 启动画面按主屏几何居中。顶层窗口无父，
             *   XWidget_move 即全局坐标。 */
#if XGUIAPPLICATION_ON && XSCREEN_ON
            {
                XScreen* screen = XGuiApplication_primaryScreen();
                if (screen) {
                    XRect g = XScreen_geometry(screen);
                    XWidget_move((XWidget*)self,
                                 g.x + (g.width - size.width) / 2,
                                 g.y + (g.height - size.height) / 2);
                }
            }
#endif /* XGUIAPPLICATION_ON && XSCREEN_ON */
        }
    }
    XWidget_update((XWidget*)self);
}

const XPixmap* XSplashScreen_pixmap(const XSplashScreen* self)
{
    return self ? self->m_pixmap : NULL;
}
#endif /* XPIXMAP_ON */

void XSplashScreen_showMessage(XSplashScreen* self, const char* utf8,
                               int alignment, uint32_t color)
{
    if (!self) return;
    if (!self->m_message) self->m_message = XString_create();
    if (self->m_message)
        XString_assign_utf8(self->m_message, utf8 ? utf8 : "");
    self->m_alignment = alignment;
    self->m_color = color;
    xsp2_emitMessageChanged(self,
        self->m_message ? XString_toUtf8(self->m_message) : "");
    XWidget_update((XWidget*)self);
}

void XSplashScreen_clearMessage(XSplashScreen* self)
{
    if (!self) return;
    /* 对标 Qt 6.8 qsplashscreen.cpp QSplashScreen::clearMessage：无条件
     * 置空并发射 messageChanged("")（Qt 不做空串判定，空消息再清除仍
     * 发射）；此前"空消息早退不发射"为对 Qt 的偏离，已收口。 */
    if (self->m_message)
        XString_assign_utf8(self->m_message, "");
    xsp2_emitMessageChanged(self, "");
    XWidget_update((XWidget*)self);
}

const char* XSplashScreen_message(const XSplashScreen* self)
{
    const char* text;
    if (!self || !self->m_message) return "";
    text = XString_toUtf8(self->m_message);
    return text ? text : "";
}

/**
 * @brief      控件是否已映射到屏幕（对标 qsplashscreen.cpp 的 isMapped：
 *             `widget->windowHandle() && widget->windowHandle()->isVisible()`）。
 * @param      widget 待查询控件借用指针；可为 NULL。
 * @return     有桥接窗口且该窗口可见返回 true。
 */
static bool xsp2_widgetMapped(const XWidget* widget)
{
    XWindow* handle;
    if (!widget) return false;
    handle = XWidget_windowHandle(widget);
    return handle != NULL && XWindow_isVisible(handle);
}

void XSplashScreen_finish(XSplashScreen* self, XWidget* widget)
{
    /* 对标 QSplashScreen::finish（qsplashscreen.cpp:236-243）：
     *   `if (mainWin) { if (!mainWin->windowHandle()) mainWin->createWinId();
     *    waitForWidgetMapped(mainWin); } close();`
     * waitForWidgetMapped（qsplashscreen.cpp:190-215）以 **1000ms** 为上限
     * 循环：processEvents(AllEvents, remaining) + sendPostedEvents + 10ms
     * 睡眠，直到窗口句柄存在且可见；超时后无论是否映射都继续 close()。
     * 本框架对应原语：XWidget_winId 惰性建柄（Qt createWinId 等价）、
     * XCoreApplication_processEventsWithMaxTime + sendPostedEvents、
     * XThread_msleep。终态与 Qt 一致：finish 返回后启动画面不可见。
     * 本机 Qt 6.8.0 探针：finish(未显示 widget) 阻塞到 ~1000ms 超时后
     * 返回、isVisible()==false；finish(已显示 widget) 立即返回。 */
    if (!self) return;
    if (widget) {
        const uint64_t deadline =
            (uint64_t)XDateTime_currentMSecsSinceEpoch() + 1000u;
        /* 请求窗口句柄（对标 Qt 的 createWinId：Qt 在此**创建**原生窗；
         * 本框架的句柄随 show 惰性创建，XWidget_winId 只对已存在的桥接
         * 窗口取 id、无句柄时返回 0——未显示的控件与 Qt 一样等不到
         * 映射，走满超时后继续 close()）。 */
        (void)XWidget_winId(widget);
        while (!xsp2_widgetMapped(widget)) {
            const uint64_t now =
                (uint64_t)XDateTime_currentMSecsSinceEpoch();
            const int remaining = now < deadline ? (int)(deadline - now) : 0;
            if (remaining <= 0) break;
            XCoreApplication_processEventsWithMaxTime(XEventLoop_AllEvents,
                                                      remaining);
            XCoreApplication_sendPostedEvents(NULL, (XEventType)0);
            XThread_msleep(10);
        }
    }
    XWidget_close((XWidget*)self);
}

void XSplashScreen_repaint(XSplashScreen* self)
{
    if (!self) return;
    XWidget_update((XWidget*)self);
    XWidget_flushBackingStore((XWidget*)self, NULL);
}

/* ==================== 信号 ==================== */

void* XSplashScreen_messageChanged_signal(XSplashScreen* self,
                                          const char* message)
{
    XVarList* args;
    XString* value;
    (void)message;
    if (self && ((XObject*)self)->m_signalSlot) {
        value = XString_create_utf8(message ? message : "");
        args = value ? XVarList_Create(XVar(XString*, value)) : NULL;
        if (args) {
            XObject_emitSignal((XObject*)self,
                               (size_t)XSplashScreen_messageChanged_signal,
                               args, xsp2_str_args_del, NULL,
                               XEVENT_PRIORITY_NORMAL);
        } else if (value) {
            XClassDelete((XClass*)value);
        }
    }
    return (void*)(size_t)XSplashScreen_messageChanged_signal;
}

#endif /* XWIDGET_ON && XSPLASHSCREEN_ON */

#endif /* XGUI_ON */
