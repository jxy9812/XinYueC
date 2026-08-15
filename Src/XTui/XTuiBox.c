/**
 * @file       XTuiBox.c
 * @brief      XTui 盒式控件实现。
 */

#include "XTuiBox.h"

#if XTUI_ON && XTUI_WIDGET_ON
#include <string.h>

/* ==================== 虚函数实现 ==================== */

static bool VXTuiBox_render(XTuiWidget* base, XTuiScreen* screen)
{
    XTuiBox* box = (XTuiBox*)base;
    if (!box || !screen)
        return false;
    XRect r = XTuiWidget_rect(base);
    if (r.width < 2 || r.height < 2)
        return false;

    XColor fg = box->m_borderFg;
    XColor bg = box->m_borderBg;
    XColor cf = box->m_contentFg;

    int x2 = r.x + r.width - 1;
    int y2 = r.y + r.height - 1;

    /* 边框。 */
    for (int x = r.x; x <= x2; ++x) {
        XTuiScreen_setCell(screen, x, r.y, "-", fg, bg, 0);
        XTuiScreen_setCell(screen, x, y2, "-", fg, bg, 0);
    }
    for (int y = r.y; y <= y2; ++y) {
        XTuiScreen_setCell(screen, r.x, y, "|", fg, bg, 0);
        XTuiScreen_setCell(screen, x2, y, "|", fg, bg, 0);
    }
    XTuiScreen_setCell(screen, r.x, r.y, "+", fg, bg, 0);
    XTuiScreen_setCell(screen, x2, r.y, "+", fg, bg, 0);
    XTuiScreen_setCell(screen, r.x, y2, "+", fg, bg, 0);
    XTuiScreen_setCell(screen, x2, y2, "+", fg, bg, 0);

    /* 居中标题。 */
    if (box->m_title && box->m_title[0]) {
        int titleLen = (int)strlen(box->m_title);
        int startX = r.x + 1;
        int maxW = r.width - 2;
        if (titleLen > maxW)
            titleLen = maxW;
        int tx = startX + (maxW - titleLen) / 2;
        XTuiScreen_writeText(screen, tx, r.y, box->m_title, titleLen);
    }

    /* 内容区清为空格并设置内容前景色。 */
    for (int y = r.y + 1; y < y2; ++y) {
        for (int x = r.x + 1; x < x2; ++x) {
            XTuiScreen_setCell(screen, x, y, " ", cf, XTUI_COLOR_DEFAULT, 0);
        }
    }
    return true;
}

static void VXTuiBox_deinit(XTuiBox* self)
{
    if (!self)
        return;
    if (self->m_title) {
        XFree_System(self->m_title);
        self->m_title = NULL;
    }
    XClass_Deinit_Parent(XTuiWidget, (XTuiWidget*)self);
}

static bool copyTitle(XTuiBox* dest, const XTuiBox* src)
{
    if (!src->m_title)
        return true;
    size_t len = strlen(src->m_title);
    char* title = (char*)XMalloc_System(len + 1);
    if (!title)
        return false;
    memcpy(title, src->m_title, len + 1);
    if (dest->m_title)
        XFree_System(dest->m_title);
    dest->m_title = title;
    return true;
}

static void VXTuiBox_copy(XTuiBox* dest, const XTuiBox* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTuiBox_init(dest);
    XClass_Parent(XTuiWidget, EXClass_Copy, void(*)(XTuiWidget*, const XTuiWidget*))((XTuiWidget*)dest, (const XTuiWidget*)src);
    dest->m_borderFg = src->m_borderFg;
    dest->m_borderBg = src->m_borderBg;
    dest->m_contentFg = src->m_contentFg;
    copyTitle(dest, src);
}

static void VXTuiBox_move(XTuiBox* dest, XTuiBox* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTuiBox_init(dest);
    XClass_Parent(XTuiWidget, EXClass_Move, void(*)(XTuiWidget*, XTuiWidget*))((XTuiWidget*)dest, (XTuiWidget*)src);
    dest->m_borderFg = src->m_borderFg;
    dest->m_borderBg = src->m_borderBg;
    dest->m_contentFg = src->m_contentFg;
    if (dest->m_title)
        XFree_System(dest->m_title);
    dest->m_title = src->m_title;
    src->m_title = NULL;
}

/* ==================== 虚函数表 ==================== */

XVtable* XTuiBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTuiBox)
    XVTABLE_INHERIT_XCLASS(XTuiWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXTuiWidget_Render, VXTuiBox_render);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTuiBox_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTuiBox_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTuiBox_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XTuiBox);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造与析构 ==================== */

void XTuiBox_init(XTuiBox* box)
{
    if (!box)
        return;
    memset(((XClass*)box) + 1, 0, sizeof(XTuiBox) - sizeof(XClass));
    XTuiWidget_init((XTuiWidget*)box);
    XClassGetVtable(box) = XTuiBox_class_init();
    box->m_borderFg = XColor_create();
    box->m_borderBg = XColor_create();
    box->m_contentFg = XColor_create();
}

XTuiBox* XTuiBox_create_ex(XMemoryType memory)
{
    XTuiBox* box = (XTuiBox*)XMemory_malloc(sizeof(XTuiBox), memory);
    if (!box)
        return NULL;
    XTuiBox_init(box);
    Set_Class_Memory(box, memory); Set_Class_IsHeap(box, true);
    return box;
}

/* ==================== 属性 ==================== */

void XTuiBox_setTitle(XTuiBox* box, const char* title)
{
    if (!box)
        return;
    if (!title) {
        if (box->m_title) {
            XFree_System(box->m_title);
            box->m_title = NULL;
        }
        return;
    }
    size_t len = strlen(title);
    char* copy = (char*)XMalloc_System(len + 1);
    if (!copy)
        return;
    memcpy(copy, title, len + 1);
    if (box->m_title)
        XFree_System(box->m_title);
    box->m_title = copy;
}

const char* XTuiBox_title(const XTuiBox* box)
{
    return box ? (const char*)box->m_title : NULL;
}

void XTuiBox_setColors(XTuiBox* box, XColor borderFg, XColor borderBg, XColor contentFg)
{
    if (!box)
        return;
    box->m_borderFg = borderFg;
    box->m_borderBg = borderBg;
    box->m_contentFg = contentFg;
}

#endif /* XTUI_ON && XTUI_WIDGET_ON */
