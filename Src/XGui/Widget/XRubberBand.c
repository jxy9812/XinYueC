/**
 * @file       XRubberBand.c
 * @brief      橡皮筋选择框控件实现（对标 Qt 6.8 QRubberBand 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XRubberBand.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XRUBBERBAND_ON

static void VXRubberBand_paintEvent(XWidget* self, XEvent* event)
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
        outline = XColor_rgba(&c);
    }
#else
    outline = 0xFF3080C0u;
#endif /* XPALETTE_ON */
#if XSTYLE_ON
    if (XWidget_style((const XWidget*)self) != NULL &&
        rb->m_shape == XRubberBandShape_Rectangle) {
        /* Fusion/公共风格接管：半透明 highlight 填充 + 边框。 */
        XStyle* style = XWidget_style((const XWidget*)self);
        XStyleOption opt;
        XStyleOption_init(&opt, XStyleCE_RubberBand);
        XRect_init(&opt.m_rect, 0, 0, w, h);
        opt.m_state = XStyleState_Enabled;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette((XWidget*)rb);
#endif
        XStyle_drawControl(style, XStyleCE_RubberBand, &opt, &painter,
                           (XWidget*)rb);
        XPainter_deinit(&painter);
        return;
    }
#endif /* XSTYLE_ON */
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
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXRubberBand_paintEvent);
    return XVTABLE_DEFAULT;
}

void XRubberBand_init(XRubberBand* self, XRubberBandShape shape,
                      XWidget* parent)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    XClassSetVtable(self, XRubberBand);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_shape = (int)shape;
    /* 构造期属性对齐 Qt 6.8 QRubberBand 构造函数原文（qrubberband.cpp:
     * 97-106）：
     *   QWidget(..., (p && p->windowType() != Qt::Desktop)
     *                ? Qt::Widget : RUBBERBAND_WINDOW_TYPE)   // = Qt::ToolTip
     *   setAttribute(WA_TransparentForMouseEvents);
     *   setAttribute(WA_NoSystemBackground);
     *   setAttribute(WA_WState_ExplicitShowHide);
     *   setVisible(false);
     * 本机 Qt 6.8.0 + offscreen 实测：`QRubberBand(..., &someWidget)` 的
     * isWindow()=0、windowType()=0（Qt::Widget，普通子控件）；只有无父
     * （或 Desktop 父）时才是 Qt::ToolTip 顶层窗。故「Qt 是顶层窗口」
     * 的旧说法只对无父构造成立，带父构造与本适配的子件模型一致。
     * ① 鼠标穿透——橡皮筋覆盖在被框选内容之上，按下/移动必须继续送达
     *    下层控件，否则框选一开始就把拖动流截断（XSplitter 的非透明拖动
     *    橡皮筋同款：把手拖动期间指针几乎总在橡皮筋覆盖区内）；
     * ② 不绘制系统背景——框选矩形只画半透明填充+边框，系统背景会把
     *    下层内容整块抹掉（Qt 同款：橡皮筋必须透出底图）；
     * ③ 永不接受键盘焦点——纯指示件；
     * ④ **构造即隐藏**：Qt 构造末尾 setVisible(false)（并先置
     *    WA_WState_ExplicitShowHide）——橡皮筋只在拖动开始时由调用方
     *    show()。XWidget_init 会把「父可见」的新子件自动置为可见，故
     *    这里必须显式收一次，否则选框在未拖动时就画在父控件上。 */
    XWidget_setAttribute((XWidget*)self,
                         XWidgetAttribute_TransparentForMouseEvents, true);
    XWidget_setAttribute((XWidget*)self,
                         XWidgetAttribute_NoSystemBackground, true);
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_NoFocus);
    XWidget_setVisible((XWidget*)self, false);
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

#endif /* XGUI_ON */
