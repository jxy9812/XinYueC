/**
 * @file       XTuiScreen.c
 * @brief      XTui 屏幕单元格缓冲类实现。
 */

#include "XTuiScreen.h"

#if XTUI_ON && XTUI_SCREEN_ON
#include <string.h>
#include <stdio.h>

/* ==================== 内部工具 ==================== */

static bool isValidCellArea(int width, int height)
{
    return width > 0 && height > 0 &&
           width <= XTUI_SCREEN_MAX_COLUMNS && height <= XTUI_SCREEN_MAX_ROWS &&
           (size_t)width * (size_t)height <= (size_t)XTUI_SCREEN_MAX_COLUMNS * (size_t)XTUI_SCREEN_MAX_ROWS;
}

static bool validIndex(const XTuiScreen* screen, int x, int y)
{
    return screen && x >= 0 && y >= 0 && x < screen->m_width && y < screen->m_height;
}

static XTuiCell* cellAt(XTuiScreen* screen, int x, int y)
{
    if (!validIndex(screen, x, y))
        return NULL;
#if XTUI_SCREEN_DYNAMIC_BUFFER
    return &screen->m_cells[(size_t)y * (size_t)screen->m_width + (size_t)x];
#else
    return &screen->m_cells[(size_t)y * (size_t)screen->m_width + (size_t)x];
#endif
}

static void clearCell(XTuiCell* cell)
{
    if (!cell)
        return;
    memset(cell, 0, sizeof(*cell));
    cell->m_utf8[0] = ' ';
    cell->m_utf8[1] = '\0';
    cell->m_fg = 0xFF;
    cell->m_bg = 0xFF;
    cell->m_attrs = 0;
}

void XTuiCell_makeDefault(XTuiCell* cell)
{
    clearCell(cell);
}

/* ==================== 虚函数实现 ==================== */

static void VXTuiScreen_deinit(XTuiScreen* self)
{
    if (!self)
        return;
#if XTUI_SCREEN_DYNAMIC_BUFFER
    if (self->m_cells) {
        XFree_System(self->m_cells);
        self->m_cells = NULL;
    }
#endif
    self->m_width = 0;
    self->m_height = 0;
    self->m_cursor.x = 0;
    self->m_cursor.y = 0;
}

static void VXTuiScreen_copy(XTuiScreen* dest, const XTuiScreen* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTuiScreen_init(dest);

    if (dest->m_width != src->m_width || dest->m_height != src->m_height) {
        if (!XTuiScreen_resize(dest, src->m_width, src->m_height))
            return;
        if (dest->m_width != src->m_width || dest->m_height != src->m_height)
            return;
    }
    size_t count = (size_t)src->m_width * (size_t)src->m_height;
#if XTUI_SCREEN_DYNAMIC_BUFFER
    if (dest->m_cells && src->m_cells)
        memcpy(dest->m_cells, src->m_cells, count * sizeof(XTuiCell));
#else
    memcpy(dest->m_cells, src->m_cells, count * sizeof(XTuiCell));
#endif
    dest->m_cursor = src->m_cursor;
}

static void VXTuiScreen_move(XTuiScreen* dest, XTuiScreen* src)
{
    if (!dest || !src)
        return;
    if (dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XTuiScreen_init(dest);

    dest->m_width = src->m_width;
    dest->m_height = src->m_height;
    dest->m_cursor = src->m_cursor;
#if XTUI_SCREEN_DYNAMIC_BUFFER
    if (dest->m_cells)
        XFree_System(dest->m_cells);
    dest->m_cells = src->m_cells;
    src->m_cells = NULL;
#else
    memcpy(dest->m_cells, src->m_cells, sizeof(dest->m_cells));
#endif
    src->m_width = 0;
    src->m_height = 0;
    src->m_cursor.x = 0;
    src->m_cursor.y = 0;
}

/* ==================== 虚函数表 ==================== */

XVtable* XTuiScreen_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTuiScreen)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTuiScreen_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTuiScreen_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTuiScreen_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XTuiScreen);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造与析构 ==================== */

void XTuiScreen_init(XTuiScreen* screen)
{
    if (!screen)
        return;
    memset(((XClass*)screen) + 1, 0, sizeof(XTuiScreen) - sizeof(XClass));
    XClass_init(screen);
    XClassGetVtable(screen) = XTuiScreen_class_init();
    screen->m_width = 1;
    screen->m_height = 1;
#if XTUI_SCREEN_DYNAMIC_BUFFER
    screen->m_cells = (XTuiCell*)XCalloc_System((size_t)1, sizeof(XTuiCell));
    if (screen->m_cells)
        clearCell(&screen->m_cells[0]);
#else
    clearCell(&screen->m_cells[0]);
#endif
    screen->m_cursor.x = 0;
    screen->m_cursor.y = 0;
}

XTuiScreen* XTuiScreen_create_ex(XMemoryType memory, int width, int height)
{
    if (!isValidCellArea(width, height))
        return NULL;
    XTuiScreen* screen = (XTuiScreen*)XMemory_malloc(sizeof(XTuiScreen), memory);
    if (!screen)
        return NULL;
    XTuiScreen_init(screen);
    Set_Class_Memory(screen, memory); Set_Class_IsHeap(screen, true);
    if (!XTuiScreen_resize(screen, width, height)) {
        XTuiScreen_delete_base(screen);
        return NULL;
    }
    return screen;
}

/* ==================== 尺寸与清屏 ==================== */

bool XTuiScreen_resize(XTuiScreen* screen, int width, int height)
{
    if (!screen || !isValidCellArea(width, height))
        return false;
    if (screen->m_width == width && screen->m_height == height)
        return true;

#if XTUI_SCREEN_DYNAMIC_BUFFER
    size_t count = (size_t)width * (size_t)height;
    XTuiCell* cells = (XTuiCell*)XCalloc_System(count, sizeof(XTuiCell));
    if (!cells)
        return false;
    for (size_t i = 0; i < count; ++i)
        clearCell(&cells[i]);

    int copyW = width < screen->m_width ? width : screen->m_width;
    int copyH = height < screen->m_height ? height : screen->m_height;
    if (screen->m_cells) {
        for (int y = 0; y < copyH; ++y) {
            for (int x = 0; x < copyW; ++x) {
                cells[(size_t)y * (size_t)width + (size_t)x] = screen->m_cells[(size_t)y * (size_t)screen->m_width + (size_t)x];
            }
        }
    }

    if (screen->m_cells)
        XFree_System(screen->m_cells);
    screen->m_cells = cells;
#else
    {
        int copyW = width < screen->m_width ? width : screen->m_width;
        int copyH = height < screen->m_height ? height : screen->m_height;
        size_t copyCount = (size_t)copyW * (size_t)copyH;
        XTuiCell* tmp = NULL;
        if (copyCount > 0) {
            tmp = (XTuiCell*)XMalloc_System(copyCount * sizeof(XTuiCell));
            if (!tmp)
                return false;
            for (int y = 0; y < copyH; ++y) {
                for (int x = 0; x < copyW; ++x) {
                    tmp[(size_t)y * (size_t)copyW + (size_t)x] =
                        screen->m_cells[(size_t)y * (size_t)screen->m_width + (size_t)x];
                }
            }
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                clearCell(&screen->m_cells[(size_t)y * (size_t)width + (size_t)x]);
            }
        }
        if (tmp) {
            for (int y = 0; y < copyH; ++y) {
                for (int x = 0; x < copyW; ++x) {
                    screen->m_cells[(size_t)y * (size_t)width + (size_t)x] =
                        tmp[(size_t)y * (size_t)copyW + (size_t)x];
                }
            }
            XFree_System(tmp);
        }
    }
#endif

    screen->m_width = width;
    screen->m_height = height;
    if (screen->m_cursor.x >= width)
        screen->m_cursor.x = width - 1;
    if (screen->m_cursor.y >= height)
        screen->m_cursor.y = height - 1;
    return true;
}

int XTuiScreen_width(const XTuiScreen* screen)
{
    return screen ? screen->m_width : 0;
}

int XTuiScreen_height(const XTuiScreen* screen)
{
    return screen ? screen->m_height : 0;
}

void XTuiScreen_clear(XTuiScreen* screen)
{
    if (!screen)
        return;
    for (int y = 0; y < screen->m_height; ++y) {
        for (int x = 0; x < screen->m_width; ++x) {
            XTuiCell* cell = cellAt(screen, x, y);
            if (cell)
                clearCell(cell);
        }
    }
    screen->m_cursor.x = 0;
    screen->m_cursor.y = 0;
}

void XTuiScreen_fill(XTuiScreen* screen, const XTuiCell* cell)
{
    if (!screen || !cell)
        return;
    for (int y = 0; y < screen->m_height; ++y) {
        for (int x = 0; x < screen->m_width; ++x) {
            XTuiCell* dst = cellAt(screen, x, y);
            if (dst)
                *dst = *cell;
        }
    }
}

const XTuiCell* XTuiScreen_cell(const XTuiScreen* screen, int x, int y)
{
    if (!validIndex(screen, x, y))
        return NULL;
#if XTUI_SCREEN_DYNAMIC_BUFFER
    return &screen->m_cells[(size_t)y * (size_t)screen->m_width + (size_t)x];
#else
    return &screen->m_cells[(size_t)y * (size_t)screen->m_width + (size_t)x];
#endif
}

bool XTuiScreen_setUtf8(XTuiScreen* screen, int x, int y, const char* utf8)
{
    XColor def = XColor_create();
    return XTuiScreen_setCell(screen, x, y, utf8, def, def, 0);
}

bool XTuiScreen_setCell(XTuiScreen* screen, int x, int y,
                        const char* utf8, XColor fg, XColor bg, int attrs)
{
    XTuiCell* cell = cellAt(screen, x, y);
    if (!cell)
        return false;
    clearCell(cell);
    if (utf8) {
        size_t len = strlen(utf8);
        if (len > XTUI_CELL_UTF8_MAX)
            len = XTUI_CELL_UTF8_MAX;
        memcpy(cell->m_utf8, utf8, len);
        cell->m_utf8[len] = '\0';
    }
    cell->m_fg = XTuiColor_toIndex(&fg);
    cell->m_bg = XTuiColor_toIndex(&bg);
    cell->m_attrs = (uint8_t)attrs;
    return true;
}

int XTuiScreen_writeText(XTuiScreen* screen, int x, int y, const char* text, int length)
{
    if (!screen || !text || y < 0 || y >= screen->m_height)
        return 0;
    if (x < 0)
        x = 0;
    int cells = 0;
    int pos = 0;
    while (x < screen->m_width && (length < 0 || pos < length)) {
        unsigned char c = (unsigned char)text[pos];
        if (c == '\0')
            break;
        size_t clen = 1;
        if (c >= 0x80) {
            if ((c & 0xE0) == 0xC0) clen = 2;
            else if ((c & 0xF0) == 0xE0) clen = 3;
            else if ((c & 0xF8) == 0xF0) clen = 4;
            else clen = 1;
            if (clen > XTUI_CELL_UTF8_MAX)
                clen = 1;
        }
        if (length >= 0 && pos + (int)clen > length)
            break;
        char buf[XTUI_CELL_UTF8_MAX + 1];
        memcpy(buf, text + pos, clen);
        buf[clen] = '\0';
        XTuiScreen_setCell(screen, x, y, buf, XTUI_COLOR_DEFAULT, XTUI_COLOR_DEFAULT, 0);
        x += 1;
        pos += (int)clen;
        ++cells;
    }
    return cells;
}

void XTuiScreen_setCursor(XTuiScreen* screen, int x, int y)
{
    if (!screen)
        return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= screen->m_width) x = screen->m_width - 1;
    if (y >= screen->m_height) y = screen->m_height - 1;
    screen->m_cursor.x = x;
    screen->m_cursor.y = y;
}

XPoint XTuiScreen_cursor(const XTuiScreen* screen)
{
    XPoint p = {0, 0};
    if (screen)
        p = screen->m_cursor;
    return p;
}

bool XTuiScreen_copyFrom(XTuiScreen* dest, const XTuiScreen* src)
{
    if (!dest || !src)
        return false;
    XClass_copy_base(dest, (const XClass*)src);
    return true;
}

bool XTuiScreen_equal(const XTuiScreen* a, const XTuiScreen* b)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    if (a->m_width != b->m_width || a->m_height != b->m_height)
        return false;
    size_t count = (size_t)a->m_width * (size_t)a->m_height;
#if XTUI_SCREEN_DYNAMIC_BUFFER
    if (!a->m_cells || !b->m_cells)
        return false;
    return memcmp(a->m_cells, b->m_cells, count * sizeof(XTuiCell)) == 0;
#else
    return memcmp(a->m_cells, b->m_cells, count * sizeof(XTuiCell)) == 0;
#endif
}

#endif /* XTUI_ON && XTUI_SCREEN_ON */
