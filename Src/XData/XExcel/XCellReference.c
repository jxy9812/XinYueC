/******************************************************************************
 * @file       XCellReference.c
 * @brief      XCellReference 单元格引用类实现（对标 QXlsx::CellReference）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XCellReference.h"
#include <string.h>

#include <stdio.h>

#include <stdlib.h>

#include <ctype.h>


/* ========== 内部辅助函数 ========== */

/**
 * @brief      将列名字符串转换为列号
 * @param colName 列名字符串（如 "A"、"AA"）
 * @return     列号（1 索引），无效返回 -1
 */
static int columnNameToNumber(const char* colName)
{
    if (!colName || !*colName) return -1;
    int result = 0;
    for (const char* p = colName; *p; p++)
    {
        if (!isalpha((unsigned char)*p)) return -1;
        result = result * 26 + (toupper((unsigned char)*p) - 'A' + 1);
    }
    return result;
}

/**
 * @brief      将列号转换为列名字符串
 * @param column 列号（1 索引）
 * @param buf    输出缓冲区（至少 12 字节）
 * @return     缓冲区指针
 */
static char* numberToColumnName(int column, char* buf)
{
    if (column < 1) { buf[0] = '\0'; return buf; }
    char tmp[12];
    int i = 0;
    int n = column;
    while (n > 0)
    {
        int remainder = (n - 1) % 26;
        tmp[i++] = (char)('A' + remainder);
        n = (n - 1) / 26;
    }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
    return buf;
}

/**
 * @brief      解析单元格引用字符串
 * @param cell  单元格引用字符串（如 "A1"、"$B$2"、"AB123"）
 * @param row   输出行号
 * @param col   输出列号
 * @return     解析成功返回 true
 */
static bool parseCellReference(const char* cell, int* row, int* col)
{
    *row = -1;
    *col = -1;
    if (!cell || !*cell) return false;

    const char* p = cell;
    bool colAbs = false;
    bool rowAbs = false;

    /* 处理列绝对引用 $ */
    if (*p == '$') { colAbs = true; p++; }
    if (!*p) return false;

    /* 解析列字母部分 */
    const char* colStart = p;
    while (*p && isalpha((unsigned char)*p)) p++;
    if (p == colStart) return false;

    /* 提取列名字符串 */
    int colLen = (int)(p - colStart);
    char colName[16];
    if (colLen >= 16) return false;
    strncpy(colName, colStart, colLen);
    colName[colLen] = '\0';

    *col = columnNameToNumber(colName);
    if (*col < 1) return false;

    /* 处理行绝对引用 $ */
    if (*p == '$') { rowAbs = true; p++; }
    if (!*p) return false; /* 仅列名，无行号 */

    /* 解析行号数字 */
    char* end = NULL;
    long r = strtol(p, &end, 10);
    if (end == p || *end != '\0') return false;
    *row = (int)r;
    if (*row < 1) return false;

    return true;
}

/* ========== 创建与初始化 ========== */

XCellReference XCellReference_create(void)
{
    XCellReference ref = { -1, -1 };
    return ref;
}

XCellReference XCellReference_create_ex(int row, int column)
{
    XCellReference ref = { row, column };
    return ref;
}

XCellReference XCellReference_create_str(const XString* cell)
{
    XCellReference ref;
    XCellReference_init_str(&ref, cell);
    return ref;
}

XCellReference XCellReference_create_char(const XString* cell)
{
    return XCellReference_create_str(cell);
}

void XCellReference_copy(XCellReference* self, const XCellReference* other)
{
    if (self && other)
    {
        self->m_row = other->m_row;
        self->m_column = other->m_column;
    }
}

void XCellReference_init(XCellReference* self)
{
    if (self)
    {
        self->m_row = -1;
        self->m_column = -1;
    }
}

void XCellReference_init_ex(XCellReference* self, int row, int column)
{
    if (self)
    {
        self->m_row = row;
        self->m_column = column;
    }
}

void XCellReference_init_str(XCellReference* self, const XString* cell)
{
    if (self)
    {
        parseCellReference(cell ? XString_toUtf8(cell) : NULL, &self->m_row, &self->m_column);
    }
}

/* ========== 访问方法 ========== */

void XCellReference_setRow(XCellReference* self, int row)
{
    if (self) self->m_row = row;
}

void XCellReference_setColumn(XCellReference* self, int column)
{
    if (self) self->m_column = column;
}

int XCellReference_row(const XCellReference* self)
{
    return self ? self->m_row : -1;
}

int XCellReference_column(const XCellReference* self)
{
    return self ? self->m_column : -1;
}

/* ========== 查询方法 ========== */

bool XCellReference_isValid(const XCellReference* self)
{
    return self && self->m_row > 0 && self->m_column > 0;
}

XString XCellReference_toString(const XCellReference* self, bool row_abs, bool col_abs)
{
    XString result;
    XString_init(&result);
    if (!self || !XCellReference_isValid(self)) return result;

    char colBuf[16];
    numberToColumnName(self->m_column, colBuf);

    /* 格式: [col_abs?$]列名[row_abs?$]行号 */
    if (col_abs) XString_append_utf8(&result, "$");
    XString_append_utf8(&result, colBuf);
    if (row_abs) XString_append_utf8(&result, "$");

    char rowBuf[32];
    snprintf(rowBuf, sizeof(rowBuf), "%d", self->m_row);
    XString_append_utf8(&result, rowBuf);

    return result;
}

/* ========== 静态工具方法 ========== */

XString XCellReference_columnToName(int column)
{
    XString result;
    XString_init(&result);
    char buf[16];
    numberToColumnName(column, buf);
    XString_append_utf8(&result, buf);
    return result;
}

int XCellReference_nameToColumn(const XString* colName)
{
    return columnNameToNumber(colName ? XString_toUtf8(colName) : NULL);
}

/* ========== UTF-8 便捷变体 ========== */

XCellReference XCellReference_create_str_utf8(const char* cell)
{
    XString* s = cell ? XString_create_utf8(cell) : NULL;
    XCellReference result = XCellReference_create_str(s);
    if (s) XString_delete_base(s);
    return result;
}

int XCellReference_nameToColumn_utf8(const char* colName)
{
    return columnNameToNumber(colName);
}

void XCellReference_init_str_utf8(XCellReference* self, const char* cell)
{
    if (self)
    {
        parseCellReference(cell, &self->m_row, &self->m_column);
    }
}
