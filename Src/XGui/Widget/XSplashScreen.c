/**
 * @file       XSplashScreen.c
 * @brief      启动画面控件实现（对标 Qt 6.8 QSplashScreen 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XSplashScreen.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XAlignment.h"
#include "XWidget_Protected.h"
#include <string.h>

#if XWIDGET_ON && XSPLASHSCREEN_ON

static void xsp2_emitMessageChanged(XSplashScreen* self, const char* text)
{
    XVarList* args;
    XString* value;
    if (!self || !((XObject*)self)->m_signalSlot) return;
    value = XString_create_utf8(text ? text : "");
    if (!value) return;
    args = XVarList_Create(XVar(XString*, value));
    if (!args) {
        XString_delete_base((XClass*)value);
        return;
    }
    XObject_emitSignal((XObject*)self,
                       (size_t)XSplashScreen_messageChanged_signal, args,
                       NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

static void VX_splash_paintEvent(XWidget* self, XEvent* event)
{
    XSplashScreen* sp = (XSplashScreen*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    int w;
    int h;
    if (!sp || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
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
#if XPIXMAP_ON
    if (sp->m_pixmap)
        XPainter_drawPixmap(&painter, sp->m_pixmap, 0, 0);
#endif /* XPIXMAP_ON */
    if (sp->m_message[0] != '\0') {
        XFont font = XWidget_font(self);
        XPainter_setFont(&painter, &font);
        XPainter_drawText(&painter, 8, h - 12, sp->m_message, sp->m_color);
    }
    (void)w;
    (void)h;
    XPainter_deinit(&painter);
}

static void VX_splash_mousePressEvent(XWidget* self, XEvent* event)
{
    XSplashScreen* sp = (XSplashScreen*)self;
    if (!sp || !event) return;
    if (XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS)
        XWidget_close(self);
}

XVtable* XSplashScreen_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSplashScreen)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_splash_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_splash_mousePressEvent);
    return XVTABLE_DEFAULT;
}

void XSplashScreen_init(XSplashScreen* self, XWidget* parent,
                        XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XSplashScreen);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_alignment = (int)XAlignment_Left;
    self->m_color = 0xFF000000u;
    XWidget_resize(self, 400, 300);
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
        XPixmap_delete_base(self->m_pixmap);
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
            XWidget_resize(self, size.width, size.height);
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
    strncpy(self->m_message, utf8 ? utf8 : "",
            sizeof(self->m_message) - 1);
    self->m_message[sizeof(self->m_message) - 1] = '\0';
    self->m_alignment = alignment;
    self->m_color = color;
    xsp2_emitMessageChanged(self, self->m_message);
    XWidget_update((XWidget*)self);
}

void XSplashScreen_clearMessage(XSplashScreen* self)
{
    if (!self) return;
    if (self->m_message[0] == '\0') return;
    self->m_message[0] = '\0';
    xsp2_emitMessageChanged(self, "");
    XWidget_update((XWidget*)self);
}

const char* XSplashScreen_message(const XSplashScreen* self)
{
    return self ? self->m_message : "";
}

void XSplashScreen_finish(XSplashScreen* self, XWidget* widget)
{
    /* 对标 finish(widget)：等 widget 显示后关闭；第一版直接关闭。 */
    (void)widget;
    if (!self) return;
    XWidget_close(self);
}

void XSplashScreen_repaint(XSplashScreen* self)
{
    if (!self) return;
    XWidget_update(self);
    XWidget_flushBackingStore(self, NULL);
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
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        } else if (value) {
            XString_delete_base((XClass*)value);
        }
    }
    return (void*)(size_t)XSplashScreen_messageChanged_signal;
}

#endif /* XWIDGET_ON && XSPLASHSCREEN_ON */
