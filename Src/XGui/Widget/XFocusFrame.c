/**
 * @file       XFocusFrame.c
 * @brief      焦点指示框控件实现（对标 Qt 6.8 QFocusFrame 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XFocusFrame.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <string.h>

#if XWIDGET_ON && XFOCUSFRAME_ON

static void XFocusFrame_paintEvent(XWidget* self, XEvent* event)
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

XVtable* XFocusFrame_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFocusFrame)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, XFocusFrame_paintEvent);
    return XVTABLE_DEFAULT;
}

void XFocusFrame_init(XFocusFrame* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XFocusFrame);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
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
    self->m_widget = widget;
    XWidget_update(self);
}

XWidget* XFocusFrame_widget(const XFocusFrame* self)
{
    return self ? self->m_widget : NULL;
}

#endif /* XWIDGET_ON && XFOCUSFRAME_ON */
