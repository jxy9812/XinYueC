/**
 * @file       XSizeGrip.c
 * @brief      窗口尺寸拖拽把手控件实现（对标 Qt 6.8 QSizeGrip 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XSizeGrip.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <string.h>

#if XWIDGET_ON && XSIZEGRIP_ON

static void VX_sizeGrip_paintEvent(XWidget* self, XEvent* event)
{
    XSizeGrip* grip = (XSizeGrip*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    int w;
    int h;
    uint32_t mid;
    int i;
    if (!grip || !event) return;
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
                                  XPaletteColorRole_Mid);
        mid = XColor_rgba(&c);
    }
#else
    mid = 0xFF808080u;
#endif /* XPALETTE_ON */
    /* 右下角斜纹三角（对标 QSizeGrip 的风格化绘制）。 */
    for (i = 0; i < w + h; i += 4) {
        XRect dot;
        int px = w - 3 - i;
        int py = h - 3;
        while (px < w - 3 && py < h - 3) {
            ++px;
            ++py;
        }
        if (px < 0 || py < 0) continue;
        XRect_init(&dot, px, py, 2, 2);
        XPainter_fillRect(&painter, &dot, mid);
    }
    XPainter_deinit(&painter);
}

static void VX_sizeGrip_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XSizeGrip* grip = (XSizeGrip*)self;
    XMouseEvent* me = (XMouseEvent*)event;
    XWidget* top;
    XPoint pos;
    int newW;
    int newH;
    if (!grip || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    top = XWidget_topLevelWidget(self);
    if (!top) return;
    pos = XMouseEvent_position(me);
    newW = pos.x + 4;
    newH = pos.y + 4;
    if (newW > 100)
        XWidget_resize(top, newW, XWidget_height(top));
    if (newH > 60)
        XWidget_resize(top, XWidget_width(top), newH);
    XEvent_accept(event);
}

XVtable* XSizeGrip_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSizeGrip)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_sizeGrip_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_sizeGrip_mouseMoveEvent);
    return XVTABLE_DEFAULT;
}

void XSizeGrip_init(XSizeGrip* self, XWidget* parent)
{
    XSize hint;
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    XClassSetVtable(self, XSizeGrip);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XWidget_resize(self, 16, 16);
    hint.width = 16;
    hint.height = 16;
    XWidget_setSizeHint((XWidget*)self, &hint);
}

XSizeGrip* XSizeGrip_create_ex(XMemoryType memory, XWidget* parent)
{
    XSizeGrip* self = (XSizeGrip*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XSizeGrip_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

#endif /* XWIDGET_ON && XSIZEGRIP_ON */
