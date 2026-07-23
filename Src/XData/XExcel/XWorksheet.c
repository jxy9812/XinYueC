/******************************************************************************
 * @file       XWorksheet.c
 * @brief      XWorksheet 工作表类实现（对标 QXlsx::Worksheet）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XWorksheet.h"
#include "XWorkbook.h"
#include "XMemory.h"
#include "XString.h"
#include "XByteArray.h"
#include "XSharedStrings.h"
#include "XStyles.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* 常量定义 */
#define XLSX_ROW_MAX 1048576
#define XLSX_COLUMN_MAX 16384

/* 行号与列号编码为 key */
static uint64_t cellKey(int row, int col) { return ((uint64_t)(uint32_t)row << 32) | (uint32_t)col; }

/* ========== 创建与初始化 ========== */

XWorksheet* XWorksheet_create(const char* sheetName, int sheetId, XWorkbook* book, XAbstractOOXmlFile_CreateFlag flag)
{
    XWorksheet* self = (XWorksheet*)XMalloc_System(sizeof(XWorksheet));
    if (!self) return NULL;
    memset(self, 0, sizeof(XWorksheet));
    XAbstractSheet_init(&self->m_base, sheetName, sheetId, book, flag);
    self->m_base.m_sheetType = XAbstractSheet_ST_WorkSheet;
    self->m_cellTable = XMap_create();
    self->m_rowInfoMap = XMap_create();
    self->m_colInfoMap = XMap_create();
    self->m_mergedCells = XVector_Create(XCellRange);
    self->m_dataValidations = XVector_Create(XDataValidation*);
    self->m_conditionalFormatting = XVector_Create(XConditionalFormatting*);
    self->m_hyperlinks = XVector_Create(XWorksheet_Hyperlink);
    self->m_mediaFiles = XVector_Create(XMediaFile*);
    self->m_chartFiles = XVector_Create(XChart*);
    self->m_rowSpans = XMap_create();
    self->m_showGridLines = true;
    self->m_showRowColHeaders = true;
    self->m_showZeros = true;
    self->m_showOutlineSymbols = true;
    self->m_showWhiteSpace = true;
    self->m_tabSelected = false;
    self->m_startPage = 1;
    XCellRange_init(&self->m_dimension);
    return self;
}

void XWorksheet_delete(XWorksheet* self)
{
    if (!self) return;
    /* 释放单元格 */
    if (self->m_cellTable) {
        XMapIterator it = XMap_begin_base(self->m_cellTable);
        XMapIterator end = XMap_end_base(self->m_cellTable);
        for (; XMapIterator_notEqual(it, end); it = XMapIterator_next(it)) {
            XCell* cell = (XCell*)XMapIterator_value(it);
            if (cell) XCell_delete(cell);
        }
        XFree_System(self->m_cellTable);
    }
    /* 释放列信息 */
    if (self->m_colInfoMap) {
        XMapIterator it = XMap_begin_base(self->m_colInfoMap);
        XMapIterator end = XMap_end_base(self->m_colInfoMap);
        for (; XMapIterator_notEqual(it, end); it = XMapIterator_next(it)) {
            XWorksheet_ColumnInfo* ci = (XWorksheet_ColumnInfo*)XMapIterator_value(it);
            if (ci) XFree_System(ci);
        }
        XFree_System(self->m_colInfoMap);
    }
    /* 释放行信息 */
    if (self->m_rowInfoMap) {
        XMapIterator it = XMap_begin_base(self->m_rowInfoMap);
        XMapIterator end = XMap_end_base(self->m_rowInfoMap);
        for (; XMapIterator_notEqual(it, end); it = XMapIterator_next(it)) {
            XWorksheet_RowInfo* ri = (XWorksheet_RowInfo*)XMapIterator_value(it);
            if (ri) XFree_System(ri);
        }
        XFree_System(self->m_rowInfoMap);
    }
    /* 释放超链接 */
    if (self->m_hyperlinks) {
        for (size_t i = 0; i < XVector_size_base(self->m_hyperlinks); ++i) {
            XWorksheet_Hyperlink* hl = (XWorksheet_Hyperlink*)XVector_at_base(self->m_hyperlinks, i);
            if (hl->m_url) { XString_deinit_base(hl->m_url); XFree_System(hl->m_url); }
            if (hl->m_display) { XString_deinit_base(hl->m_display); XFree_System(hl->m_display); }
            if (hl->m_tip) { XString_deinit_base(hl->m_tip); XFree_System(hl->m_tip); }
        }
        XFree_System(self->m_hyperlinks);
    }
    if (self->m_mergedCells) XFree_System(self->m_mergedCells);
    if (self->m_dataValidations) XFree_System(self->m_dataValidations);
    if (self->m_conditionalFormatting) XFree_System(self->m_conditionalFormatting);
    if (self->m_mediaFiles) XFree_System(self->m_mediaFiles);
    if (self->m_chartFiles) XFree_System(self->m_chartFiles);
    if (self->m_rowSpans) XFree_System(self->m_rowSpans);
    XAbstractSheet_deinit(&self->m_base);
    XFree_System(self);
}

/* ========== 单元格写入 ========== */

static XCell* getOrCreateCell(XWorksheet* self, int row, int column)
{
    if (!self || row < 1 || row > XLSX_ROW_MAX || column < 1 || column > XLSX_COLUMN_MAX) return NULL;
    uint64_t key = cellKey(row, column);
    XMapIterator it = XMap_find_base(self->m_cellTable, &key, sizeof(uint64_t));
    if (XMapIterator_notEqual(it, XMap_end_base(self->m_cellTable))) {
        return (XCell*)XMapIterator_value(it);
    }
    XCell* cell = XCell_create();
    if (!cell) return NULL;
    cell->m_row = row;
    cell->m_column = column;
    XMap_insert_base(self->m_cellTable, &key, sizeof(uint64_t), &cell, sizeof(XCell*));
    /* 更新维度 */
    if (self->m_dimension.m_firstRow < 1 || row < self->m_dimension.m_firstRow) self->m_dimension.m_firstRow = row;
    if (row > self->m_dimension.m_lastRow) self->m_dimension.m_lastRow = row;
    if (self->m_dimension.m_firstCol < 1 || column < self->m_dimension.m_firstCol) self->m_dimension.m_firstCol = column;
    if (column > self->m_dimension.m_lastCol) self->m_dimension.m_lastCol = column;
    return cell;
}

bool XWorksheet_write(XWorksheet* self, int row, int column, const XVariant* value, const XFormat* format)
{
    if (!self || !value) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    if (format) cell->m_format = (XFormat*)format;
    /* 根据 XVariant 类型写入 */
    int type = XVariant_type(value);
    if (type == XByteArray_Type || type == XVariant_String || type == XVariant_StringList) {
        const char* str = XVariant_toString_utf8(value);
        XCell_setValue(cell, str);
        cell->m_cellType = XCell_SharedStringType;
    } else if (type == XVariant_Int || type == XVariant_Double || type == XVariant_UInt || type == XVariant_LongLong || type == XVariant_ULongLong) {
        double dval = XVariant_toDouble(value);
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", dval);
        XCell_setValue(cell, buf);
        cell->m_cellType = XCell_NumberType;
    } else if (type == XVariant_Bool) {
        XCell_setValue(cell, XVariant_toBool(value) ? "1" : "0");
        cell->m_cellType = XCell_BooleanType;
    } else if (type == XVariant_DateTime || type == XVariant_Date || type == XVariant_Time) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", XVariant_toDouble(value));
        XCell_setValue(cell, buf);
        cell->m_cellType = XCell_DateType;
    }
    return true;
}

bool XWorksheet_writeRef(XWorksheet* self, const XCellReference* cell, const XVariant* value, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_write(self, XCellReference_row(cell), XCellReference_column(cell), value, format);
}

bool XWorksheet_writeString(XWorksheet* self, int row, int column, const char* value, const XFormat* format)
{
    if (!self || !value) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    if (format) cell->m_format = (XFormat*)format;
    XCell_setValue(cell, value);
    cell->m_cellType = XCell_SharedStringType;
    return true;
}

bool XWorksheet_writeStringRef(XWorksheet* self, const XCellReference* cell, const char* value, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_writeString(self, XCellReference_row(cell), XCellReference_column(cell), value, format);
}

bool XWorksheet_writeInlineString(XWorksheet* self, int row, int column, const char* value, const XFormat* format)
{
    if (!self || !value) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    if (format) cell->m_format = (XFormat*)format;
    XCell_setValue(cell, value);
    cell->m_cellType = XCell_InlineStringType;
    return true;
}

bool XWorksheet_writeInlineStringRef(XWorksheet* self, const XCellReference* cell, const char* value, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_writeInlineString(self, XCellReference_row(cell), XCellReference_column(cell), value, format);
}

bool XWorksheet_writeNumeric(XWorksheet* self, int row, int column, double value, const XFormat* format)
{
    if (!self) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    if (format) cell->m_format = (XFormat*)format;
    char buf[64];
    snprintf(buf, sizeof(buf), "%.15g", value);
    XCell_setValue(cell, buf);
    cell->m_cellType = XCell_NumberType;
    return true;
}

bool XWorksheet_writeNumericRef(XWorksheet* self, const XCellReference* cell, double value, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_writeNumeric(self, XCellReference_row(cell), XCellReference_column(cell), value, format);
}

bool XWorksheet_writeFormula(XWorksheet* self, int row, int column, const XCellFormula* formula, const XFormat* format, double result)
{
    if (!self || !formula) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    if (format) cell->m_format = (XFormat*)format;
    cell->m_formula = XCellFormula_copy(formula);
    if (cell->m_formula) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", result);
        XCell_setValue(cell, buf);
    }
    return true;
}

bool XWorksheet_writeFormulaRef(XWorksheet* self, const XCellReference* cell, const XCellFormula* formula, const XFormat* format, double result)
{
    if (!cell) return false;
    return XWorksheet_writeFormula(self, XCellReference_row(cell), XCellReference_column(cell), formula, format, result);
}

bool XWorksheet_writeBlank(XWorksheet* self, int row, int column, const XFormat* format)
{
    if (!self) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    if (format) cell->m_format = (XFormat*)format;
    XCell_setValue(cell, "");
    cell->m_cellType = XCell_CustomType;
    return true;
}

bool XWorksheet_writeBlankRef(XWorksheet* self, const XCellReference* cell, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_writeBlank(self, XCellReference_row(cell), XCellReference_column(cell), format);
}

bool XWorksheet_writeBool(XWorksheet* self, int row, int column, bool value, const XFormat* format)
{
    if (!self) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    if (format) cell->m_format = (XFormat*)format;
    XCell_setValue(cell, value ? "1" : "0");
    cell->m_cellType = XCell_BooleanType;
    return true;
}

bool XWorksheet_writeBoolRef(XWorksheet* self, const XCellReference* cell, bool value, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_writeBool(self, XCellReference_row(cell), XCellReference_column(cell), value, format);
}

bool XWorksheet_writeDateTime(XWorksheet* self, int row, int column, int64_t timestampMs, const XFormat* format)
{
    if (!self) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    if (format) cell->m_format = (XFormat*)format;
    /* Excel 日期序列号：从1900-01-01开始 */
    double excelSerial = (double)timestampMs / 86400000.0 + 25569.0; /* 1970-01-01到1900-01-01的偏移 */
    if (excelSerial < 60.0) excelSerial -= 1.0; /* Excel 1900年闰年bug */
    char buf[64];
    snprintf(buf, sizeof(buf), "%.15g", excelSerial);
    XCell_setValue(cell, buf);
    cell->m_cellType = XCell_DateType;
    return true;
}

bool XWorksheet_writeDateTimeRef(XWorksheet* self, const XCellReference* cell, int64_t timestampMs, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_writeDateTime(self, XCellReference_row(cell), XCellReference_column(cell), timestampMs, format);
}

bool XWorksheet_writeHyperlink(XWorksheet* self, int row, int column, const char* url, const XFormat* format, const char* display, const char* tip)
{
    if (!self) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    if (format) cell->m_format = (XFormat*)format;
    if (display) { XCell_setValue(cell, display); cell->m_cellType = XCell_SharedStringType; }
    /* 添加超链接 */
    XWorksheet_Hyperlink hl;
    memset(&hl, 0, sizeof(hl));
    XCellRange_setCell(&hl.m_range, row, column);
    hl.m_url = XString_create(); if (url) XString_append_utf8(hl.m_url, url);
    if (display) { hl.m_display = XString_create(); XString_append_utf8(hl.m_display, display); }
    if (tip) { hl.m_tip = XString_create(); XString_append_utf8(hl.m_tip, tip); }
    XVector_push_back_base(self->m_hyperlinks, &hl, sizeof(XWorksheet_Hyperlink));
    return true;
}

bool XWorksheet_writeHyperlinkRef(XWorksheet* self, const XCellReference* cell, const char* url, const XFormat* format, const char* display, const char* tip)
{
    if (!cell) return false;
    return XWorksheet_writeHyperlink(self, XCellReference_row(cell), XCellReference_column(cell), url, format, display, tip);
}

/* ========== 单元格读取 ========== */

XCell* XWorksheet_cellAt(XWorksheet* self, int row, int column)
{
    if (!self || row < 1 || column < 1) return NULL;
    uint64_t key = cellKey(row, column);
    XMapIterator it = XMap_find_base(self->m_cellTable, &key, sizeof(uint64_t));
    if (XMapIterator_notEqual(it, XMap_end_base(self->m_cellTable)))
        return (XCell*)XMapIterator_value(it);
    return NULL;
}

XCell* XWorksheet_cellAtRef(XWorksheet* self, const XCellReference* cell)
{
    if (!cell) return NULL;
    return XWorksheet_cellAt(self, XCellReference_row(cell), XCellReference_column(cell));
}

XVariant* XWorksheet_read(XWorksheet* self, int row, int column)
{
    XCell* cell = XWorksheet_cellAt(self, row, column);
    if (!cell) return NULL;
    const char* val = XCell_value(cell);
    if (!val || strlen(val) == 0) return NULL;
    XVariant* v = XVariant_create();
    if (!v) return NULL;
    XVariant_setString_utf8(v, val);
    return v;
}

XVariant* XWorksheet_readRef(XWorksheet* self, const XCellReference* cell)
{
    if (!cell) return NULL;
    return XWorksheet_read(self, XCellReference_row(cell), XCellReference_column(cell));
}

/* ========== 数据验证与条件格式 ========== */

bool XWorksheet_addDataValidation(XWorksheet* self, XDataValidation* validation)
{
    if (!self || !validation) return false;
    XVector_push_back_base(self->m_dataValidations, &validation, sizeof(XDataValidation*));
    return true;
}

bool XWorksheet_addConditionalFormatting(XWorksheet* self, XConditionalFormatting* cf)
{
    if (!self || !cf) return false;
    XVector_push_back_base(self->m_conditionalFormatting, &cf, sizeof(XConditionalFormatting*));
    return true;
}

/* ========== 图片与图表 ========== */

int XWorksheet_insertImage(XWorksheet* self, int row, int column, const char* imagePath)
{
    if (!self || !imagePath) return -1;
    XMediaFile* media = XMediaFile_create();
    if (!media) return -1;
    XMediaFile_setFileName(media, imagePath);
    XMediaFile_setLocation(media, row, column);
    XVector_push_back_base(self->m_mediaFiles, &media, sizeof(XMediaFile*));
    return (int)XVector_size_base(self->m_mediaFiles) - 1;
}

bool XWorksheet_getImage(XWorksheet* self, int imageIndex, XByteArray* imgData) { (void)self; (void)imageIndex; (void)imgData; return false; }
bool XWorksheet_getImageAt(XWorksheet* self, int row, int column, XByteArray* imgData) { (void)self; (void)row; (void)column; (void)imgData; return false; }
uint XWorksheet_getImageCount(const XWorksheet* self) { return self ? (uint)XVector_size_base(self->m_mediaFiles) : 0; }

XChart* XWorksheet_insertChart(XWorksheet* self, int row, int column, int width, int height)
{
    if (!self) return NULL;
    XChart* chart = XChart_create((XAbstractSheet*)&self->m_base, XAbstractOOXmlFile_F_NewFromScratch);
    if (!chart) return NULL;
    XChart_setSize(chart, width, height);
    XChart_setPosition(chart, row, column, 0, 0);
    XVector_push_back_base(self->m_chartFiles, &chart, sizeof(XChart*));
    return chart;
}

/* ========== 合并单元格 ========== */

bool XWorksheet_mergeCells(XWorksheet* self, int firstRow, int firstCol, int lastRow, int lastCol, const XFormat* format)
{
    if (!self) return false;
    XCellRange range;
    XCellRange_setFullRange(&range, firstRow, firstCol, lastRow, lastCol);
    XVector_push_back_base(self->m_mergedCells, &range, sizeof(XCellRange));
    (void)format;
    return true;
}

bool XWorksheet_unmergeCells(XWorksheet* self, int firstRow, int firstCol, int lastRow, int lastCol)
{
    if (!self || !self->m_mergedCells) return false;
    for (int i = (int)XVector_size_base(self->m_mergedCells) - 1; i >= 0; --i) {
        XCellRange* r = (XCellRange*)XVector_at_base(self->m_mergedCells, i);
        if (r->m_firstRow == firstRow && r->m_firstCol == firstCol &&
            r->m_lastRow == lastRow && r->m_lastCol == lastCol) {
            XVector_erase_base(self->m_mergedCells, (size_t)i);
            return true;
        }
    }
    return false;
}

XCellRange* XWorksheet_mergedCells(const XWorksheet* self, int* count)
{
    if (count) *count = (self && self->m_mergedCells) ? (int)XVector_size_base(self->m_mergedCells) : 0;
    return (self && self->m_mergedCells) ? (XCellRange*)XVector_data_base(self->m_mergedCells) : NULL;
}

/* ========== 列操作 ========== */

static XWorksheet_ColumnInfo* getOrCreateColInfo(XWorksheet* self, int col)
{
    if (!self || col < 1) return NULL;
    XMapIterator it = XMap_find_base(self->m_colInfoMap, &col, sizeof(int));
    if (XMapIterator_notEqual(it, XMap_end_base(self->m_colInfoMap)))
        return (XWorksheet_ColumnInfo*)XMapIterator_value(it);
    XWorksheet_ColumnInfo* ci = (XWorksheet_ColumnInfo*)XMalloc_System(sizeof(XWorksheet_ColumnInfo));
    if (!ci) return NULL;
    memset(ci, 0, sizeof(XWorksheet_ColumnInfo));
    ci->m_width = -1.0;
    XMap_insert_base(self->m_colInfoMap, &col, sizeof(int), &ci, sizeof(XWorksheet_ColumnInfo*));
    return ci;
}

bool XWorksheet_setColumnWidth(XWorksheet* self, int colFirst, int colLast, double width)
{
    for (int c = colFirst; c <= colLast; ++c) {
        XWorksheet_ColumnInfo* ci = getOrCreateColInfo(self, c);
        if (ci) ci->m_width = width;
    }
    return true;
}

bool XWorksheet_setColumnFormat(XWorksheet* self, int colFirst, int colLast, const XFormat* format)
{
    for (int c = colFirst; c <= colLast; ++c) {
        XWorksheet_ColumnInfo* ci = getOrCreateColInfo(self, c);
        if (ci) ci->m_format = (XFormat*)format;
    }
    return true;
}

bool XWorksheet_setColumnHidden(XWorksheet* self, int colFirst, int colLast, bool hidden)
{
    for (int c = colFirst; c <= colLast; ++c) {
        XWorksheet_ColumnInfo* ci = getOrCreateColInfo(self, c);
        if (ci) ci->m_hidden = hidden;
    }
    return true;
}

double XWorksheet_columnWidth(const XWorksheet* self, int column)
{
    if (!self) return -1.0;
    XMapIterator it = XMap_find_base(self->m_colInfoMap, &column, sizeof(int));
    if (XMapIterator_notEqual(it, XMap_end_base(self->m_colInfoMap)))
        return ((XWorksheet_ColumnInfo*)XMapIterator_value(it))->m_width;
    return -1.0;
}

XFormat* XWorksheet_columnFormat(const XWorksheet* self, int column)
{
    if (!self) return NULL;
    XMapIterator it = XMap_find_base(self->m_colInfoMap, &column, sizeof(int));
    if (XMapIterator_notEqual(it, XMap_end_base(self->m_colInfoMap)))
        return ((XWorksheet_ColumnInfo*)XMapIterator_value(it))->m_format;
    return NULL;
}

bool XWorksheet_isColumnHidden(const XWorksheet* self, int column)
{
    if (!self) return false;
    XMapIterator it = XMap_find_base(self->m_colInfoMap, &column, sizeof(int));
    if (XMapIterator_notEqual(it, XMap_end_base(self->m_colInfoMap)))
        return ((XWorksheet_ColumnInfo*)XMapIterator_value(it))->m_hidden;
    return false;
}

bool XWorksheet_groupColumns(XWorksheet* self, int colFirst, int colLast, bool collapsed)
{
    for (int c = colFirst; c <= colLast; ++c) {
        XWorksheet_ColumnInfo* ci = getOrCreateColInfo(self, c);
        if (ci) { ci->m_outlineLevel = 1; ci->m_collapsed = collapsed; }
    }
    return true;
}

/* ========== 行操作 ========== */

static XWorksheet_RowInfo* getOrCreateRowInfo(XWorksheet* self, int row)
{
    if (!self || row < 1) return NULL;
    XMapIterator it = XMap_find_base(self->m_rowInfoMap, &row, sizeof(int));
    if (XMapIterator_notEqual(it, XMap_end_base(self->m_rowInfoMap)))
        return (XWorksheet_RowInfo*)XMapIterator_value(it);
    XWorksheet_RowInfo* ri = (XWorksheet_RowInfo*)XMalloc_System(sizeof(XWorksheet_RowInfo));
    if (!ri) return NULL;
    memset(ri, 0, sizeof(XWorksheet_RowInfo));
    ri->m_height = -1.0;
    XMap_insert_base(self->m_rowInfoMap, &row, sizeof(int), &ri, sizeof(XWorksheet_RowInfo*));
    return ri;
}

bool XWorksheet_setRowHeight(XWorksheet* self, int rowFirst, int rowLast, double height)
{
    for (int r = rowFirst; r <= rowLast; ++r) {
        XWorksheet_RowInfo* ri = getOrCreateRowInfo(self, r);
        if (ri) ri->m_height = height;
    }
    return true;
}

bool XWorksheet_setRowFormat(XWorksheet* self, int rowFirst, int rowLast, const XFormat* format)
{
    for (int r = rowFirst; r <= rowLast; ++r) {
        XWorksheet_RowInfo* ri = getOrCreateRowInfo(self, r);
        if (ri) ri->m_format = (XFormat*)format;
    }
    return true;
}

bool XWorksheet_setRowHidden(XWorksheet* self, int rowFirst, int rowLast, bool hidden)
{
    for (int r = rowFirst; r <= rowLast; ++r) {
        XWorksheet_RowInfo* ri = getOrCreateRowInfo(self, r);
        if (ri) ri->m_hidden = hidden;
    }
    return true;
}

double XWorksheet_rowHeight(const XWorksheet* self, int row)
{
    if (!self) return -1.0;
    XMapIterator it = XMap_find_base(self->m_rowInfoMap, &row, sizeof(int));
    if (XMapIterator_notEqual(it, XMap_end_base(self->m_rowInfoMap)))
        return ((XWorksheet_RowInfo*)XMapIterator_value(it))->m_height;
    return -1.0;
}

XFormat* XWorksheet_rowFormat(const XWorksheet* self, int row)
{
    if (!self) return NULL;
    XMapIterator it = XMap_find_base(self->m_rowInfoMap, &row, sizeof(int));
    if (XMapIterator_notEqual(it, XMap_end_base(self->m_rowInfoMap)))
        return ((XWorksheet_RowInfo*)XMapIterator_value(it))->m_format;
    return NULL;
}

bool XWorksheet_isRowHidden(const XWorksheet* self, int row)
{
    if (!self) return false;
    XMapIterator it = XMap_find_base(self->m_rowInfoMap, &row, sizeof(int));
    if (XMapIterator_notEqual(it, XMap_end_base(self->m_rowInfoMap)))
        return ((XWorksheet_RowInfo*)XMapIterator_value(it))->m_hidden;
    return false;
}

bool XWorksheet_groupRows(XWorksheet* self, int rowFirst, int rowLast, bool collapsed)
{
    for (int r = rowFirst; r <= rowLast; ++r) {
        XWorksheet_RowInfo* ri = getOrCreateRowInfo(self, r);
        if (ri) { ri->m_outlineLevel = 1; ri->m_collapsed = collapsed; }
    }
    return true;
}

/* ========== 属性 ========== */

XCellRange XWorksheet_dimension(const XWorksheet* self) {
    XCellRange r; XCellRange_init(&r);
    if (self) r = self->m_dimension;
    return r;
}
bool XWorksheet_isWindowProtected(const XWorksheet* self) { return self ? self->m_windowProtection : false; }
void XWorksheet_setWindowProtected(XWorksheet* self, bool protect) { if (self) self->m_windowProtection = protect; }
bool XWorksheet_isFormulasVisible(const XWorksheet* self) { return self ? self->m_showFormulas : false; }
void XWorksheet_setFormulasVisible(XWorksheet* self, bool visible) { if (self) self->m_showFormulas = visible; }
bool XWorksheet_isGridLinesVisible(const XWorksheet* self) { return self ? self->m_showGridLines : true; }
void XWorksheet_setGridLinesVisible(XWorksheet* self, bool visible) { if (self) self->m_showGridLines = visible; }
bool XWorksheet_isRowColumnHeadersVisible(const XWorksheet* self) { return self ? self->m_showRowColHeaders : true; }
void XWorksheet_setRowColumnHeadersVisible(XWorksheet* self, bool visible) { if (self) self->m_showRowColHeaders = visible; }
bool XWorksheet_isZerosVisible(const XWorksheet* self) { return self ? self->m_showZeros : true; }
void XWorksheet_setZerosVisible(XWorksheet* self, bool visible) { if (self) self->m_showZeros = visible; }
bool XWorksheet_isRightToLeft(const XWorksheet* self) { return self ? self->m_rightToLeft : false; }
void XWorksheet_setRightToLeft(XWorksheet* self, bool enable) { if (self) self->m_rightToLeft = enable; }
bool XWorksheet_isSelected(const XWorksheet* self) { return self ? self->m_tabSelected : false; }
void XWorksheet_setSelected(XWorksheet* self, bool select) { if (self) self->m_tabSelected = select; }
bool XWorksheet_isRulerVisible(const XWorksheet* self) { return self ? self->m_showRuler : false; }
void XWorksheet_setRulerVisible(XWorksheet* self, bool visible) { if (self) self->m_showRuler = visible; }
bool XWorksheet_isOutlineSymbolsVisible(const XWorksheet* self) { return self ? self->m_showOutlineSymbols : true; }
void XWorksheet_setOutlineSymbolsVisible(XWorksheet* self, bool visible) { if (self) self->m_showOutlineSymbols = visible; }
bool XWorksheet_isWhiteSpaceVisible(const XWorksheet* self) { return self ? self->m_showWhiteSpace : true; }
void XWorksheet_setWhiteSpaceVisible(XWorksheet* self, bool visible) { if (self) self->m_showWhiteSpace = visible; }
bool XWorksheet_setStartPage(XWorksheet* self, int spagen) { if (self) { self->m_startPage = spagen; return true; } return false; }

/* ========== 全单元格获取 ========== */

int XWorksheet_getFullCells(const XWorksheet* self, XCellLocation** locations, int* maxRow, int* maxCol)
{
    if (!self || !locations) return 0;
    int count = (int)XMap_size_base(self->m_cellTable);
    *locations = (XCellLocation*)XMalloc_System(sizeof(XCellLocation) * (size_t)count);
    if (!*locations) return 0;
    int idx = 0;
    XMapIterator it = XMap_begin_base(self->m_cellTable);
    XMapIterator end = XMap_end_base(self->m_cellTable);
    int mr = 0, mc = 0;
    for (; XMapIterator_notEqual(it, end); it = XMapIterator_next(it)) {
        XCell* cell = (XCell*)XMapIterator_value(it);
        XCellLocation* loc = &(*locations)[idx++];
        loc->m_row = cell->m_row;
        loc->m_column = cell->m_column;
        loc->m_cell = (XCell*)cell;
        if (cell->m_row > mr) mr = cell->m_row;
        if (cell->m_column > mc) mc = cell->m_column;
    }
    if (maxRow) *maxRow = mr;
    if (maxCol) *maxCol = mc;
    return count;
}

/* ========== XML 读写 ========== */

bool XWorksheet_saveToXmlFile(XWorksheet* self, const char* filePath) { (void)self; (void)filePath; return false; }
bool XWorksheet_loadFromXmlFile(XWorksheet* self, const char* filePath) { (void)self; (void)filePath; return false; }
