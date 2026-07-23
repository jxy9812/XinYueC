/******************************************************************************
 * @file       XCellRange.c
 * @brief      XCellRange 单元格范围类实现（对标 QXlsx::CellRange）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XCellRange.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief      从字符串解析范围
 * @param self  指针
 * @param range 范围字符串
 */
static void parseRange(XCellRange* self, const char* range)
{
    if (!self || !range || !*range) return;
    const char* colon = strchr(range, ':');
    if (colon)
    {
        /* "A1:B2" 格式 */
        int len1 = (int)(colon - range);
        char cell1[64], cell2[64];
        if (len1 >= 64) return;
        strncpy(cell1, range, len1);
        cell1[len1] = '\0';
        strncpy(cell2, colon + 1, sizeof(cell2) - 1);
        cell2[sizeof(cell2) - 1] = '\0';

        XCellReference tl, br;
        XCellReference_init_str(&tl, cell1);
        XCellReference_init_str(&br, cell2);

        if (XCellReference_isValid(&tl) && XCellReference_isValid(&br))
        {
            self->m_firstRow = XCellReference_row(&tl);
            self->m_firstColumn = XCellReference_column(&tl);
            self->m_lastRow = XCellReference_row(&br);
            self->m_lastColumn = XCellReference_column(&br);
        }
    }
    else
    {
        /* 单个单元格 "A1" 格式 */
        XCellReference ref;
        XCellReference_init_str(&ref, range);
        if (XCellReference_isValid(&ref))
        {
            int r = XCellReference_row(&ref);
            int c = XCellReference_column(&ref);
            self->m_firstRow = r;
            self->m_firstColumn = c;
            self->m_lastRow = r;
            self->m_lastColumn = c;
        }
    }
}

XCellRange XCellRange_create(void)
{
    XCellRange r = { -1, -1, -1, -1 };
    return r;
}

XCellRange XCellRange_create_ex(int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    XCellRange r = { firstRow, firstColumn, lastRow, lastColumn };
    return r;
}

XCellRange XCellRange_create_ref(const XCellReference* topLeft, const XCellReference* bottomRight)
{
    XCellRange r = { -1, -1, -1, -1 };
    if (topLeft && bottomRight && XCellReference_isValid(topLeft) && XCellReference_isValid(bottomRight))
    {
        r.m_firstRow = XCellReference_row(topLeft);
        r.m_firstColumn = XCellReference_column(topLeft);
        r.m_lastRow = XCellReference_row(bottomRight);
        r.m_lastColumn = XCellReference_column(bottomRight);
    }
    return r;
}

XCellRange XCellRange_create_str(const char* range)
{
    XCellRange r;
    XCellRange_init_str(&r, range);
    return r;
}

XCellRange XCellRange_create_char(const char* range)
{
    return XCellRange_create_str(range);
}

void XCellRange_copy(XCellRange* self, const XCellRange* other)
{
    if (self && other) *self = *other;
}

void XCellRange_init(XCellRange* self)
{
    if (self)
    {
        self->m_firstRow = -1;
        self->m_firstColumn = -1;
        self->m_lastRow = -1;
        self->m_lastColumn = -1;
    }
}

void XCellRange_init_ex(XCellRange* self, int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    if (self)
    {
        self->m_firstRow = firstRow;
        self->m_firstColumn = firstColumn;
        self->m_lastRow = lastRow;
        self->m_lastColumn = lastColumn;
    }
}

void XCellRange_init_str(XCellRange* self, const char* range)
{
    XCellRange_init(self);
    parseRange(self, range);
}

void XCellRange_setFirstRow(XCellRange* self, int row) { if (self) self->m_firstRow = row; }
void XCellRange_setLastRow(XCellRange* self, int row) { if (self) self->m_lastRow = row; }
void XCellRange_setFirstColumn(XCellRange* self, int col) { if (self) self->m_firstColumn = col; }
void XCellRange_setLastColumn(XCellRange* self, int col) { if (self) self->m_lastColumn = col; }
int XCellRange_firstRow(const XCellRange* self) { return self ? self->m_firstRow : -1; }
int XCellRange_lastRow(const XCellRange* self) { return self ? self->m_lastRow : -1; }
int XCellRange_firstColumn(const XCellRange* self) { return self ? self->m_firstColumn : -1; }
int XCellRange_lastColumn(const XCellRange* self) { return self ? self->m_lastColumn : -1; }
int XCellRange_rowCount(const XCellRange* self) { return self ? self->m_lastRow - self->m_firstRow + 1 : 0; }
int XCellRange_columnCount(const XCellRange* self) { return self ? self->m_lastColumn - self->m_firstColumn + 1 : 0; }

XCellReference XCellRange_topLeft(const XCellRange* self)
{
    if (!self) return XCellReference_create();
    return XCellReference_create_ex(self->m_firstRow, self->m_firstColumn);
}

XCellReference XCellRange_topRight(const XCellRange* self)
{
    if (!self) return XCellReference_create();
    return XCellReference_create_ex(self->m_firstRow, self->m_lastColumn);
}

XCellReference XCellRange_bottomLeft(const XCellRange* self)
{
    if (!self) return XCellReference_create();
    return XCellReference_create_ex(self->m_lastRow, self->m_firstColumn);
}

XCellReference XCellRange_bottomRight(const XCellRange* self)
{
    if (!self) return XCellReference_create();
    return XCellReference_create_ex(self->m_lastRow, self->m_lastColumn);
}

bool XCellRange_isValid(const XCellRange* self)
{
    return self && self->m_firstRow > 0 && self->m_firstColumn > 0
        && self->m_lastRow >= self->m_firstRow
        && self->m_lastColumn >= self->m_firstColumn;
}

XString XCellRange_toString(const XCellRange* self, bool row_abs, bool col_abs)
{
    XString result;
    XString_init(&result);
    if (!self || !XCellRange_isValid(self)) return result;

    XString tl = XCellReference_toString(&(XCellReference){self->m_firstRow, self->m_firstColumn}, row_abs, col_abs);
    XString_append_utf8(&result, ":");
    XString br = XCellReference_toString(&(XCellReference){self->m_lastRow, self->m_lastColumn}, row_abs, col_abs);
    XString_append_utf8(&result, XString_toUtf8_const(&br));
    XString_deinit_base(&br);
    XString_deinit_base(&tl);
    return result;
}
