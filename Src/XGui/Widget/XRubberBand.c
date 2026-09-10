/**
 * @file       XRubberBand.c
 * @brief      橡皮筋选择框控件实现（对标 Qt 6.8 QRubberBand 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XRubberBand.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <string.h>

#if XWIDGET_ON && XRUBBERBAND_ON

static void XRubberBand_paintEvent(XWidget* self, XEvent* event)
{
    XRubberBand* rb = (XRubberBand*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    uint32_t outline;
    int w;
    int h;
    if (!rb || !event) return;
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
        outline = XColor_rgba(&c);
    }
#else
    outline = 0xFF3080C0u;
#endif /* XPALETTE_ON */
    if (rb->m_shape == XRubberBandShape_Rectangle) {
        XRect top;
        XRect bottom;
        XRect left;
        XRect right;
        XRect_init(&top, 0, 0, w, 1);
        XRect_init(&bottom, 0, h - 1, w, 1);
        XRect_init(&left, 0, 0, 1, h);
        XRect_init(&right, w - 1, 0, 1, h);
        XPainter_fillRect(&painter, &top, outline);
        XPainter_fillRect(&painter, &bottom, outline);
        XPainter_fillRect(&painter, &left, outline);
        XPainter_fillRect(&painter, &right, outline);
    } else {
        XRect hline;
        XRect_init(&hline, 0, h / 2, w, 1);
        XPainter_fillRect(&painter, &hline, outline);
    }
    XPainter_deinit(&painter);
}

XVtable* XRubberBand_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XRubberBand)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, XRubberBand_paintEvent);
    return XVTABLE_DEFAULT;
}

void XRubberBand_init(XRubberBand* self, XRubberBandShape shape,
                      XWidget* parent)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    XClassSetVtable(self, XRubberBand);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_shape = (int)shape;
}

XRubberBand* XRubberBand_create_ex(XMemoryType memory,
                                   XRubberBandShape shape, XWidget* parent)
{
    XRubberBand* self = (XRubberBand*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XRubberBand_init(self, shape, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XRubberBandShape XRubberBand_shape(const XRubberBand* self)
{
    return self ? (XRubberBandShape)self->m_shape : XRubberBandShape_Line;
}

#endif /* XWIDGET_ON && XRUBBERBAND_ON */
