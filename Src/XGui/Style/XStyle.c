#include "XStyle.h"
#include "XCommonStyle.h"
#include "XWindowsStyle.h"
#include "XStyleSheetStyle.h"
#include "XMemory.h"
#include "XClass.h"
#include "XPainter.h"
#include "XWidget.h"
#include <string.h>

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

XVtable* XStyle_class_init(void)
{
    /* 7 个样式槽位（DrawPrimitive/DrawControl/DrawComplexControl/
       PixelMetric/SizeFromContents/Polish/Unpolish）：基类留空实现，
       由 XCommonStyle/XFusionStyle 覆盖。必须用 ADD_FUNC_LIST 追加以
       推进 vtable size，否则子表继承时槽位丢失（继承复制 size 个槽）。 */
    void* table[XCLASS_VTABLE_GET_SIZE(XStyle) -
                XCLASS_VTABLE_GET_SIZE(XObject)] = {
        NULL, /* DrawPrimitive */
        NULL, /* DrawControl */
        NULL, /* DrawComplexControl */
        NULL, /* PixelMetric */
        NULL, /* SizeFromContents */
        NULL, /* Polish */
        NULL  /* Unpolish */
    };
    XVTABLE_INIT_DEFAULT(XStyle)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    return XVTABLE_DEFAULT;
}

void XStyle_init(XStyle* self)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
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
            strcmp(XVTABLE_GET_NAME(vt), "XStyleSheetStyle") == 0) {
            return XStyleSheetStyle_setStyleSheet(
                (XStyleSheetStyle*)g_defaultStyle, css);
        }
    }
    source = XStyle_defaultStyle();
    ss = XStyleSheetStyle_create();
    if (!ss) return false;
    XStyleSheetStyle_setSourceStyle(ss, source);
    ok = XStyleSheetStyle_setStyleSheet(ss, css);
    if (ok) g_defaultStyle = (XStyle*)ss;
    else XStyleSheetStyle_delete_base(ss);
    return ok;
}

#endif /* XSTYLE_ON */
