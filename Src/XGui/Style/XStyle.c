#include "XStyle.h"
#include "XStringUtils.h"

#include "XAlgorithm.h"
#include "XCommonStyle.h"
#include "XWindowsStyle.h"
#include "XStyleSheetStyle.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPainter.h"
#include "XPixmap.h"
#include "XWidget.h"
#include "XPalette.h"
#include "XAlignment.h"

#if XSTYLE_ON

static XStyle* g_defaultStyle = NULL;

typedef void (*XStyleDrawFn)(XStyle*, int, const XStyleOption*, XPainter*,
                              const XWidget*);

/** @brief 调用绘制基元槽位（无实现时忽略）。 */
static void xstyle_callPrimitive(XStyle* self, int pe,
                                 const XStyleOption* option,
                                 XPainter* painter, const XWidget* widget)
{
    XStyleDrawFn fn = (XStyleDrawFn)XVtableGetFunc(
        XClassGetVtable((XClass*)self), EXStyle_DrawPrimitive, XStyleDrawFn);
    if (fn) fn(self, pe, option, painter, widget);
}

/** @brief 调用控件槽位（无实现时忽略）。 */
static void xstyle_callControl(XStyle* self, int ce,
                               const XStyleOption* option,
                               XPainter* painter, const XWidget* widget)
{
    XStyleDrawFn fn = (XStyleDrawFn)XVtableGetFunc(
        XClassGetVtable((XClass*)self), EXStyle_DrawControl, XStyleDrawFn);
    if (fn) fn(self, ce, option, painter, widget);
}

/* ==================== 基础实现（对标 QStyle 非纯虚默认） ==================== */

/** @brief 按对齐位在矩形内定位单行内容（对标 QStyle::alignedRect 逻辑）。 */
static XRect xstyle_alignedRectInt(int alignment, int cw, int ch,
                                   const XRect* rectangle)
{
    XRect r;
    int x = rectangle->x;
    int y = rectangle->y;
    if ((alignment & 0x80) == 0x80) /* AlignVCenter。 */
        y += rectangle->height / 2 - ch / 2;
    else if ((alignment & 0x40) == 0x40) /* AlignBottom。 */
        y += rectangle->height - ch;
    if ((alignment & 0x2) == 0x2) /* AlignRight。 */
        x += rectangle->width - cw;
    else if ((alignment & 0x4) == 0x4) /* AlignHCenter。 */
        x += rectangle->width / 2 - cw / 2;
    XRect_init(&r, x, y, cw, ch);
    return r;
}

/** @brief 基础标准调色板（对标 QStyle::standardPalette）。 */
static void xstyle_standardPalette(XPalette* out)
{
    uint32_t background = 0xFFD4D0C8u; /* win 2000 grey。 */
    uint32_t light = 0xFFFFFFFFu;      /* lighter(150) 溢出为白。 */
    uint32_t dark = 0xFF6A6A6Au;       /* darker(200)：212/2=106。 */
    uint32_t mid = 0xFF808080u;        /* Qt::gray。 */
    XColor c;
    int g;
    if (!out) return;
    XMemset(out, 0, sizeof(*out));
    /* 常规组：windowText=black，window=background，light，dark，mid，
     * text=black，base=white。 */
    XColor_setRgba(&c, 0xFF000000u);
    XPalette_setColor(out, XPaletteColorGroup_Active,
                      XPaletteColorRole_WindowText, c);
    XPalette_setColor(out, XPaletteColorGroup_Active, XPaletteColorRole_Text, c);
    XPalette_setColor(out, XPaletteColorGroup_Active,
                      XPaletteColorRole_ButtonText, c);
    XColor_setRgba(&c, background);
    XPalette_setColor(out, XPaletteColorGroup_Active,
                      XPaletteColorRole_Window, c);
    XPalette_setColor(out, XPaletteColorGroup_Active,
                      XPaletteColorRole_Button, c);
    XColor_setRgba(&c, light);
    XPalette_setColor(out, XPaletteColorGroup_Active,
                      XPaletteColorRole_Light, c);
    XPalette_setColor(out, XPaletteColorGroup_Active,
                      XPaletteColorRole_Midlight, c);
    XColor_setRgba(&c, dark);
    XPalette_setColor(out, XPaletteColorGroup_Active,
                      XPaletteColorRole_Dark, c);
    XPalette_setColor(out, XPaletteColorGroup_Active,
                      XPaletteColorRole_Shadow, c);
    XColor_setRgba(&c, mid);
    XPalette_setColor(out, XPaletteColorGroup_Active,
                      XPaletteColorRole_Mid, c);
    XColor_setRgba(&c, 0xFFFFFFFFu);
    XPalette_setColor(out, XPaletteColorGroup_Active,
                      XPaletteColorRole_Base, c);
    /* 禁用组：windowText/text/buttonText=dark，base=background。 */
    for (g = XPaletteColorGroup_Disabled; g < XPaletteColorGroup_NColorGroups;
         ++g) {
        XColor_setRgba(&c, dark);
        XPalette_setColor(out, (XPaletteColorGroup)g,
                          XPaletteColorRole_WindowText, c);
        XPalette_setColor(out, (XPaletteColorGroup)g,
                          XPaletteColorRole_Text, c);
        XPalette_setColor(out, (XPaletteColorGroup)g,
                          XPaletteColorRole_ButtonText, c);
        XColor_setRgba(&c, background);
        XPalette_setColor(out, (XPaletteColorGroup)g,
                          XPaletteColorRole_Base, c);
    }
}

XVtable* XStyle_class_init(void)
{
    /* 20 个样式槽位（DrawPrimitive/DrawControl/DrawComplexControl/
       PixelMetric/SizeFromContents/Polish/Unpolish/StyleHint/
       SubElementRect/SubControlRect/HitTestComplexControl/StandardPixmap/
       StandardIcon/GeneratedIconPixmap/LayoutSpacing/DrawItemText/
       DrawItemPixmap/ItemTextRect/ItemPixmapRect/StandardPalette）：
       基类留空实现，由 XCommonStyle/XFusionStyle 覆盖。必须用
       ADD_FUNC_LIST 追加以推进 vtable size，否则子表继承时槽位丢失。 */
    void* table[XCLASS_VTABLE_GET_SIZE(XStyle) -
                XCLASS_VTABLE_GET_SIZE(XObject)] = {
        NULL, /* DrawPrimitive */
        NULL, /* DrawControl */
        NULL, /* DrawComplexControl */
        NULL, /* PixelMetric */
        NULL, /* SizeFromContents */
        NULL, /* Polish */
        NULL, /* Unpolish */
        NULL, /* StyleHint */
        NULL, /* SubElementRect */
        NULL, /* SubControlRect */
        NULL, /* HitTestComplexControl */
        NULL, /* StandardPixmap */
        NULL, /* StandardIcon */
        NULL, /* GeneratedIconPixmap */
        NULL, /* LayoutSpacing */
        NULL, /* DrawItemText */
        NULL, /* DrawItemPixmap */
        NULL, /* ItemTextRect */
        NULL, /* ItemPixmapRect */
        NULL  /* StandardPalette */
    };
    XVTABLE_INIT_DEFAULT(XStyle)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    return XVTABLE_DEFAULT;
}

void XStyle_init(XStyle* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init(&self->m_base);
    XClassSetVtable(self, XStyle);
}

XStyle* XStyle_create_ex(XMemoryType memory)
{
    XStyle* self = (XStyle*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XStyle_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XStyle_drawPrimitive(XStyle* self, int pe, const XStyleOption* option,
                          XPainter* painter, const XWidget* widget)
{
    if (!self || !option || !painter) return;
    xstyle_callPrimitive(self, pe, option, painter, widget);
}

void XStyle_drawControl(XStyle* self, int ce, const XStyleOption* option,
                        XPainter* painter, const XWidget* widget)
{
    if (!self || !option || !painter) return;
    xstyle_callControl(self, ce, option, painter, widget);
}

void XStyle_drawComplexControl(XStyle* self, int cc,
                               const XStyleOption* option,
                               XPainter* painter, const XWidget* widget)
{
    XStyleDrawFn fn;
    if (!self || !option || !painter) return;
    fn = (XStyleDrawFn)XVtableGetFunc(XClassGetVtable((XClass*)self),
                                      EXStyle_DrawComplexControl,
                                      XStyleDrawFn);
    if (fn) fn(self, cc, option, painter, widget);
}

int XStyle_pixelMetric(XStyle* self, int pm, const XStyleOption* option)
{
    int (*fn)(XStyle*, int, const XStyleOption*);
    if (!self) return 0;
    fn = (int (*)(XStyle*, int, const XStyleOption*))XVtableGetFunc(
        XClassGetVtable((XClass*)self), EXStyle_PixelMetric,
        int (*)(XStyle*, int, const XStyleOption*));
    if (fn) return fn(self, pm, option);
    return 0;
}

XSize XStyle_sizeFromContents(XStyle* self, int ct,
                              const XStyleOption* option,
                              XSize contentSize)
{
    XSize (*fn)(XStyle*, int, const XStyleOption*, XSize);
    XSize result;
    if (!self) {
        XSize_init(&result, 0, 0);
        return result;
    }
    fn = (XSize (*)(XStyle*, int, const XStyleOption*, XSize))XVtableGetFunc(
        XClassGetVtable((XClass*)self), EXStyle_SizeFromContents,
        XSize (*)(XStyle*, int, const XStyleOption*, XSize));
    if (fn) return fn(self, ct, option, contentSize);
    return contentSize;
}

void XStyle_polish(XStyle* self, XWidget* widget)
{
    void (*fn)(XStyle*, XWidget*);
    if (!self || !widget) return;
    fn = (void (*)(XStyle*, XWidget*))XVtableGetFunc(
        XClassGetVtable((XClass*)self), EXStyle_Polish,
        void (*)(XStyle*, XWidget*));
    if (fn) fn(self, widget);
}

void XStyle_unpolish(XStyle* self, XWidget* widget)
{
    void (*fn)(XStyle*, XWidget*);
    if (!self || !widget) return;
    fn = (void (*)(XStyle*, XWidget*))XVtableGetFunc(
        XClassGetVtable((XClass*)self), EXStyle_Unpolish,
        void (*)(XStyle*, XWidget*));
    if (fn) fn(self, widget);
}

int XStyle_styleHint(XStyle* self, int hint, const XStyleOption* option,
                     const XWidget* widget)
{
    int (*fn)(XStyle*, int, const XStyleOption*, const XWidget*);
    if (!self) return 0;
    fn = (int (*)(XStyle*, int, const XStyleOption*, const XWidget*))
        XVtableGetFunc(XClassGetVtable((XClass*)self), EXStyle_StyleHint,
        int (*)(XStyle*, int, const XStyleOption*, const XWidget*));
    if (fn) return fn(self, hint, option, widget);
    return 0;
}

XRect XStyle_subElementRect(XStyle* self, int subElement,
                            const XStyleOption* option,
                            const XWidget* widget)
{
    XRect (*fn)(XStyle*, int, const XStyleOption*, const XWidget*);
    XRect zero;
    XRect_init(&zero, 0, 0, 0, 0);
    if (!self) return zero;
    fn = (XRect (*)(XStyle*, int, const XStyleOption*, const XWidget*))
        XVtableGetFunc(XClassGetVtable((XClass*)self), EXStyle_SubElementRect,
        XRect (*)(XStyle*, int, const XStyleOption*, const XWidget*));
    if (fn) return fn(self, subElement, option, widget);
    return option ? option->m_rect : zero;
}

XRect XStyle_subControlRect(XStyle* self, int cc,
                            const XStyleOption* option, int sc,
                            const XWidget* widget)
{
    XRect (*fn)(XStyle*, int, const XStyleOption*, int, const XWidget*);
    XRect zero;
    XRect_init(&zero, 0, 0, 0, 0);
    if (!self) return zero;
    fn = (XRect (*)(XStyle*, int, const XStyleOption*, int, const XWidget*))
        XVtableGetFunc(XClassGetVtable((XClass*)self), EXStyle_SubControlRect,
        XRect (*)(XStyle*, int, const XStyleOption*, int, const XWidget*));
    if (fn) return fn(self, cc, option, sc, widget);
    return zero;
}

int XStyle_hitTestComplexControl(XStyle* self, int cc,
                                 const XStyleOption* option, int x, int y,
                                 const XWidget* widget)
{
    int (*fn)(XStyle*, int, const XStyleOption*, int, int, const XWidget*);
    if (!self) return XStyleSC_None;
    fn = (int (*)(XStyle*, int, const XStyleOption*, int, int,
                  const XWidget*))XVtableGetFunc(
        XClassGetVtable((XClass*)self), EXStyle_HitTestComplexControl,
        int (*)(XStyle*, int, const XStyleOption*, int, int,
                const XWidget*));
    if (fn) return fn(self, cc, option, x, y, widget);
    return XStyleSC_None;
}

XPixmap* XStyle_standardPixmap(XStyle* self, int sp,
                               const XStyleOption* option,
                               const XWidget* widget)
{
    XPixmap* (*fn)(XStyle*, int, const XStyleOption*, const XWidget*);
    if (!self) return NULL;
    fn = (XPixmap* (*)(XStyle*, int, const XStyleOption*, const XWidget*))
        XVtableGetFunc(XClassGetVtable((XClass*)self), EXStyle_StandardPixmap,
        XPixmap* (*)(XStyle*, int, const XStyleOption*, const XWidget*));
    if (fn) return fn(self, sp, option, widget);
    return NULL;
}

XIcon* XStyle_standardIcon(XStyle* self, int sp,
                           const XStyleOption* option,
                           const XWidget* widget)
{
    XIcon* (*fn)(XStyle*, int, const XStyleOption*, const XWidget*);
    if (!self) return NULL;
    fn = (XIcon* (*)(XStyle*, int, const XStyleOption*, const XWidget*))
        XVtableGetFunc(XClassGetVtable((XClass*)self), EXStyle_StandardIcon,
        XIcon* (*)(XStyle*, int, const XStyleOption*, const XWidget*));
    if (fn) return fn(self, sp, option, widget);
    return NULL;
}

XPixmap* XStyle_generatedIconPixmap(XStyle* self, int mode,
                                    const XPixmap* pixmap,
                                    const XStyleOption* option)
{
    XPixmap* (*fn)(XStyle*, int, const XPixmap*, const XStyleOption*);
    if (!self || !pixmap) return NULL;
    fn = (XPixmap* (*)(XStyle*, int, const XPixmap*, const XStyleOption*))
        XVtableGetFunc(XClassGetVtable((XClass*)self),
                       EXStyle_GeneratedIconPixmap,
        XPixmap* (*)(XStyle*, int, const XPixmap*, const XStyleOption*));
    if (fn) return fn(self, mode, pixmap, option);
    return NULL;
}

int XStyle_layoutSpacing(XStyle* self, int control1, int control2,
                         int orientation, const XStyleOption* option,
                         const XWidget* widget)
{
    int (*fn)(XStyle*, int, int, int, const XStyleOption*, const XWidget*);
    if (!self) return -1;
    fn = (int (*)(XStyle*, int, int, int, const XStyleOption*,
                  const XWidget*))XVtableGetFunc(
        XClassGetVtable((XClass*)self), EXStyle_LayoutSpacing,
        int (*)(XStyle*, int, int, int, const XStyleOption*,
                const XWidget*));
    if (fn) return fn(self, control1, control2, orientation, option, widget);
    return -1;
}

void XStyle_drawItemText(XStyle* self, XPainter* painter,
                         const XRect* rect, int alignment,
                         const XPalette* palette, bool enabled,
                         const char* text, int textRole)
{
    void (*fn)(XStyle*, XPainter*, const XRect*, int, const XPalette*,
               bool, const char*, int);
    if (!self || !painter || !rect) return;
    fn = (void (*)(XStyle*, XPainter*, const XRect*, int, const XPalette*,
                   bool, const char*, int))XVtableGetFunc(
        XClassGetVtable((XClass*)self), EXStyle_DrawItemText,
        void (*)(XStyle*, XPainter*, const XRect*, int, const XPalette*,
                 bool, const char*, int));
    if (fn) {
        fn(self, painter, rect, alignment, palette, enabled, text, textRole);
        return;
    }
    /* 基类默认（对标 QStyle::drawItemText 的 etch 分支子集）。 */
    if (!text || !text[0]) return;
    if (textRole != XPaletteColorRole_NoRole && palette) {
        XColor c = XPalette_color((XPalette*)palette,
                                  enabled ? XPaletteColorGroup_Active
                                          : XPaletteColorGroup_Disabled,
                                  (XPaletteColorRole)textRole);
        XPainter_setPen(painter, XColor_rgba(&c));
    }
    if (!enabled && self &&
        XStyle_styleHint(self, XStyleSH_EtchDisabledText, NULL, NULL)) {
        XRect off = *rect;
        uint32_t light = 0xFFFFFFFFu;
        if (palette) {
            XColor lc = XPalette_color((XPalette*)palette,
                                       XPaletteColorGroup_Active,
                                       XPaletteColorRole_Light);
            light = XColor_rgba(&lc);
        }
        off.x += 1;
        off.y += 1;
        XPainter_setPen(painter, light);
        XPainter_drawText(painter, off.x, off.y + off.height - 4, text,
                          light);
    }
    XPainter_drawText(painter, rect->x, rect->y + rect->height - 4, text,
                      XPainter_penColor(painter));
}

void XStyle_drawItemPixmap(XStyle* self, XPainter* painter,
                           const XRect* rect, int alignment,
                           const XPixmap* pixmap)
{
    void (*fn)(XStyle*, XPainter*, const XRect*, int, const XPixmap*);
    XRect aligned;
    if (!self || !painter || !rect || !pixmap) return;
    fn = (void (*)(XStyle*, XPainter*, const XRect*, int, const XPixmap*))
        XVtableGetFunc(XClassGetVtable((XClass*)self), EXStyle_DrawItemPixmap,
        void (*)(XStyle*, XPainter*, const XRect*, int, const XPixmap*));
    if (fn) {
        fn(self, painter, rect, alignment, pixmap);
        return;
    }
    {
        XSize s;
        XSize_init(&s, XPixmap_width(pixmap), XPixmap_height(pixmap));
        aligned = XStyle_alignedRect(0, alignment, &s, rect);
#if XPAINTER_PIXMAP_ON
        XPainter_drawPixmap(painter, pixmap, aligned.x, aligned.y);
#else
        /* XPAINTER_PIXMAP_ON 裁剪时位图绘制不可用，跳过（与裁剪配置一致）。 */
        (void)aligned;
#endif
    }
}

XRect XStyle_itemTextRect(XStyle* self, const XFont* font,
                          const XRect* rect, int alignment, bool enabled,
                          const char* text)
{
    XRect (*fn)(XStyle*, const XFont*, const XRect*, int, bool, const char*);
    XRect result;
    XRect_init(&result, 0, 0, 0, 0);
    if (!self || !rect) return result;
    fn = (XRect (*)(XStyle*, const XFont*, const XRect*, int, bool,
                    const char*))XVtableGetFunc(
        XClassGetVtable((XClass*)self), EXStyle_ItemTextRect,
        XRect (*)(XStyle*, const XFont*, const XRect*, int, bool,
                  const char*));
    if (fn) return fn(self, font, rect, alignment, enabled, text);
    /* 基类默认：单行文本度量 + 对齐（对标 boundingRect 的单行子集）。 */
    if (text && text[0]) {
        int tw = XPainter_textWidth(font, text);
        int th = XPainter_textHeight(font);
        result = xstyle_alignedRectInt(alignment, tw, th, rect);
    } else {
        result = *rect;
    }
    return result;
}

XRect XStyle_itemPixmapRect(XStyle* self, const XRect* rect,
                            int alignment, const XPixmap* pixmap)
{
    XRect (*fn)(XStyle*, const XRect*, int, const XPixmap*);
    XRect result;
    XRect_init(&result, 0, 0, 0, 0);
    if (!self || !rect || !pixmap) return result;
    fn = (XRect (*)(XStyle*, const XRect*, int, const XPixmap*))
        XVtableGetFunc(XClassGetVtable((XClass*)self), EXStyle_ItemPixmapRect,
        XRect (*)(XStyle*, const XRect*, int, const XPixmap*));
    if (fn) return fn(self, rect, alignment, pixmap);
    /* 基类默认：对齐定位 + 位图尺寸（对标 QStyle::itemPixmapRect）。 */
    return xstyle_alignedRectInt(alignment, XPixmap_width(pixmap),
                                 XPixmap_height(pixmap), rect);
}

XPalette XStyle_standardPalette(XStyle* self)
{
    XPalette (*fn)(XStyle*);
    XPalette out;
    XMemset(&out, 0, sizeof(out));
    if (!self) return out;
    fn = (XPalette (*)(XStyle*))XVtableGetFunc(
        XClassGetVtable((XClass*)self), EXStyle_StandardPalette,
        XPalette (*)(XStyle*));
    if (fn) return fn(self);
    xstyle_standardPalette(&out);
    return out;
}

/* ==================== 静态工具（对标 QStyle 静态函数） ==================== */

XRect XStyle_visualRect(int direction, const XRect* boundingRect,
                        const XRect* logicalRect)
{
    XRect rect;
    if (!boundingRect || !logicalRect) {
        XRect_init(&rect, 0, 0, 0, 0);
        return rect;
    }
    rect = *logicalRect;
    if (direction == 1 /* RightToLeft */) {
        rect.x += 2 * (XRect_right(boundingRect) - XRect_right(logicalRect)) +
                  logicalRect->width - boundingRect->width;
    }
    return rect;
}

XPoint XStyle_visualPos(int direction, const XRect* boundingRect,
                        const XPoint* logicalPos)
{
    XPoint p;
    XPoint_init(&p, 0, 0);
    if (!boundingRect || !logicalPos) return p;
    p = *logicalPos;
    if (direction == 1 /* RightToLeft */)
        p.x = XRect_right(boundingRect) - logicalPos->x;
    return p;
}

int XStyle_visualAlignment(int direction, int alignment)
{
    if (direction == 1 /* RightToLeft */) {
        if ((alignment & 0x1f) == 0) /* 无水平对齐位 → 默认右对齐。 */
            alignment |= XAlignment_Right;
        if ((alignment & (XAlignment_Left | XAlignment_Right)) &&
            !(alignment & XAlignment_Absolute))
            alignment ^= (XAlignment_Left | XAlignment_Right);
    }
    return alignment;
}

XRect XStyle_alignedRect(int direction, int alignment, const XSize* size,
                         const XRect* rectangle)
{
    XRect r;
    int x;
    int y;
    int w;
    int h;
    XRect_init(&r, 0, 0, 0, 0);
    if (!size || !rectangle) return r;
    alignment = XStyle_visualAlignment(direction, alignment);
    x = rectangle->x;
    y = rectangle->y;
    w = size->width;
    h = size->height;
    if ((alignment & XAlignment_VCenter) == XAlignment_VCenter)
        y += rectangle->height / 2 - h / 2;
    else if ((alignment & XAlignment_Bottom) == XAlignment_Bottom)
        y += rectangle->height - h;
    if ((alignment & XAlignment_Right) == XAlignment_Right)
        x += rectangle->width - w;
    else if ((alignment & XAlignment_HCenter) == XAlignment_HCenter)
        x += rectangle->width / 2 - w / 2;
    XRect_init(&r, x, y, w, h);
    return r;
}

int XStyle_sliderPositionFromValue(int min, int max, int logicalValue,
                                   int span, bool upsideDown)
{
    uint32_t range;
    uint32_t p;
    if (span <= 0 || max <= min) return 0;
    if (logicalValue < min) return upsideDown ? span : 0;
    if (logicalValue > max) return upsideDown ? 0 : span;
    range = (uint32_t)((int64_t)max - min);
    p = upsideDown ? (uint32_t)((int64_t)max - logicalValue)
                   : (uint32_t)((int64_t)logicalValue - min);
    if (range > (uint32_t)2147483647u / 4096u) {
        double dpos = ((double)p) / ((double)range / (double)span);
        return (int)dpos;
    }
    if (range > (uint32_t)span)
        return (int)((2 * p * (uint32_t)span + range) / (2 * range));
    {
        uint32_t div = (uint32_t)span / range;
        uint32_t mod = (uint32_t)span % range;
        return (int)(p * div + (2 * p * mod + range) / (2 * range));
    }
}

int XStyle_sliderValueFromPosition(int min, int max, int pos, int span,
                                   bool upsideDown)
{
    int64_t range;
    if (span <= 0 || pos <= 0) return upsideDown ? max : min;
    if (pos >= span) return upsideDown ? min : max;
    range = (int64_t)max - min;
    if ((uint32_t)span > (uint64_t)range) {
        int64_t tmp = (2 * range * pos + span) / ((int64_t)2 * span);
        return upsideDown ? (int)(max - tmp) : (int)(tmp + min);
    }
    {
        int64_t div = range / span;
        int64_t mod = range % span;
        int64_t tmp = (int64_t)pos * div +
                      (2 * mod * pos + span) / ((int64_t)2 * span);
        return upsideDown ? (int)(max - tmp) : (int)(tmp + min);
    }
}

void XStyle_setDefaultStyle(XStyle* style)
{
    if (g_defaultStyle && g_defaultStyle != style)
        XStyle_delete_base(g_defaultStyle);
    g_defaultStyle = style;
}

XStyle* XStyle_defaultStyle(void)
{
    if (!g_defaultStyle) {
        XCommonStyle* cs = XCommonStyle_create();
        if (cs) g_defaultStyle = (XStyle*)cs;
    }
    return g_defaultStyle;
}

bool XStyle_installStyleSheet(const char* css)
{
    XStyleSheetStyle* ss;
    XStyle* source;
    bool ok;
    if (g_defaultStyle) {
        /* 已是样式表风格则复用（重复调用更新规则表）。 */
        XVtable* vt = XClassGetVtable((XClass*)g_defaultStyle);
        if (XVTABLE_GET_NAME(vt) &&
            XStrcmp(XVTABLE_GET_NAME(vt), "XStyleSheetStyle") == 0) {
            return XStyleSheetStyle_setStyleSheet(
                (XStyleSheetStyle*)g_defaultStyle, css);
        }
    }
    source = XStyle_defaultStyle();
    ss = XStyleSheetStyle_create();
    if (!ss) return false;
    /* 所有权转移：source（可能即旧 g_defaultStyle）由 ss 拥有，
     * 替换默认样式后仍存活，且 ss 析构时一并释放，消除泄漏。 */
    XStyleSheetStyle_setSourceStyle_move(ss, source);
    ok = XStyleSheetStyle_setStyleSheet(ss, css);
    if (ok) g_defaultStyle = (XStyle*)ss;
    else XStyleSheetStyle_delete_base(ss);
    return ok;
}

#endif /* XSTYLE_ON */
