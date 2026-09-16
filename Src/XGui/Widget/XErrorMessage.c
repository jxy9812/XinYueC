#include "XErrorMessage.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON

static void VX_errMsg_paintEvent(XWidget* self, XEvent* event)
{
    XErrorMessage* em = (XErrorMessage*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    uint32_t text;
    if (!em || !event) return;
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
    r.x = 0; r.y = 0;
    r.width = XWidget_width(self);
    r.height = XWidget_height(self);
    /* 白色背景。 */
    XPainter_fillRect(&painter, &r, 0xFFFFFFFFu);
    /* 消息文本。 */
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_WindowText);
        text = XColor_rgba(&c);
    }
#else
    text = 0xFF000000u;
#endif
    if (em->m_message && XString_toUtf8(em->m_message) &&
        XString_toUtf8(em->m_message)[0])
        XPainter_drawText(&painter, 12, r.height / 2,
                          XString_toUtf8(em->m_message), text);
    XPainter_deinit(&painter);
}

static void VXErrorMessage_deinit(XErrorMessage* self)
{
    if (!self) return;
    if (self->m_message) {
        XString_delete_base(self->m_message);
        self->m_message = NULL;
    }
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

XVtable* XErrorMessage_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XErrorMessage)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_errMsg_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXErrorMessage_deinit);
    return XVTABLE_DEFAULT;
}

void XErrorMessage_init(XErrorMessage* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XErrorMessage);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_doneShown = true;
    self->m_message = XString_create();
}

XErrorMessage* XErrorMessage_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XErrorMessage* self = (XErrorMessage*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XErrorMessage_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XErrorMessage_showMessage(XErrorMessage* self, const char* msg)
{
    if (!self || !msg) return;
    if (!self->m_message) self->m_message = XString_create();
    if (self->m_message)
        XString_assign_utf8(self->m_message, msg ? msg : "");
    XWidget_show((XWidget*)self);
}

const char* XErrorMessage_currentMessage(const XErrorMessage* self)
{
    const char* text;
    if (!self || !self->m_message) return "";
    text = XString_toUtf8(self->m_message);
    return text ? text : "";
}

void XErrorMessage_setDoneShown(XErrorMessage* self, bool on)
{
    if (!self) return;
    self->m_doneShown = on;
}

bool XErrorMessage_isDoneShown(const XErrorMessage* self)
{
    return self ? self->m_doneShown : false;
}






#endif /* XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON */