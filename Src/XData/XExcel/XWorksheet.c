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
#include "XVariant.h"
#include "XStyles.h"
#include "XUtility.h"
#include "XFile.h"
#include "XClass.h"
#include "XXmlStreamReader.h"
#include "XReadSax.h"
#include <string.h>
#include <stdlib.h>

#include <math.h>


/* 常量定义 */
#define XLSX_ROW_MAX 1048576
#define XLSX_COLUMN_MAX 16384

/* 行号与列号编码为 key */
static uint64_t cellKey(int row, int col) { return ((uint64_t)(uint32_t)row << 32) | (uint32_t)col; }

static void* pairPointerValue(XPair* pair)
{
    void* value = NULL;
    if (pair) memcpy(&value, XPair_second(pair), sizeof(value));
    return value;
}

static XFormat* register_format(XWorksheet* self, const XFormat* format, int* styleIndex)
{
    if (styleIndex) *styleIndex = -1;
    if (!self || !format) return NULL;
    XWorkbook* workbook = self->m_base.m_workbook;
    XStyles* styles = workbook ? workbook->m_styles : NULL;
    if (!styles) return (XFormat*)format;
    XStyles_addXfFormat(styles, format, false);
    size_t count = XVector_size_base((XContainer*)styles->m_xfFormatsList);
    for (size_t i = 0; i < count; ++i) {
        XFormat* registered = *(XFormat**)XVector_at_base(styles->m_xfFormatsList, i);
        if (registered && XFormat_equals(registered, format)) {
            if (styleIndex) *styleIndex = (int)i;
            return registered;
        }
    }
    return NULL;
}

static void apply_cell_format(XWorksheet* self, XCell* cell, const XFormat* format)
{
    if (!cell || !format) return;
    int styleIndex = -1;
    XFormat* registered = register_format(self, format, &styleIndex);
    if (registered) {
        cell->m_format = registered;
        cell->m_styleNumber = styleIndex;
    }
}

/* ========== 创建与初始化 ========== */

XWorksheet* XWorksheet_create(const XString* sheetName, int sheetId, XWorkbook* book, XAbstractOOXmlFile_CreateFlag flag)
{
    XWorksheet* self = (XWorksheet*)XMalloc_System(sizeof(XWorksheet));
    if (!self) return NULL;
    memset(self, 0, sizeof(XWorksheet));
    XAbstractSheet_init(&self->m_base, sheetName, sheetId, book, flag);
    self->m_base.m_sheetType = XAbstractSheet_ST_WorkSheet;
    self->m_cellTable = XMap_create_ex(sizeof(uint64_t), sizeof(XCell*), uint64_t_compare, false);
    self->m_rowInfoMap = XMap_create_ex(sizeof(int), sizeof(XWorksheet_RowInfo*), int_compare, false);
    self->m_colInfoMap = XMap_create_ex(sizeof(int), sizeof(XWorksheet_ColumnInfo*), int_compare, false);
    self->m_mergedCells = XVector_Create(XCellRange);
    self->m_dataValidations = XVector_Create(XDataValidation*);
    self->m_conditionalFormatting = XVector_Create(XConditionalFormatting*);
    self->m_hyperlinks = XVector_Create(XWorksheet_Hyperlink);
    self->m_mediaFiles = XVector_Create(XMediaFile*);
    self->m_imagePositions = XVector_Create(XWorksheet_ImagePosition);
    self->m_chartFiles = XVector_Create(XChart*);
    self->m_rowSpans = XMap_create_ex(sizeof(uint64_t), sizeof(uint64_t), uint64_t_compare, false);
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

XWorksheet* XWorksheet_copy(const XWorksheet* self, const XString* distName, int distId)
{
    if (!self) return NULL;
    XWorksheet* ws = XWorksheet_create(distName, distId, 
        (XWorkbook*)self->m_base.m_workbook, XAbstractOOXmlFile_F_NewFromScratch);
    if (!ws) return NULL;
    /* 复制单元格 */
    if (self->m_cellTable) {
        XMap_iterator it = XMap_begin(self->m_cellTable);
        XMap_iterator end = XMap_end(self->m_cellTable);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_cellTable, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            XCell* srcCell = (XCell*)pairPointerValue(pair);
            if (srcCell) {
                XCell* dstCell = XCell_copy(srcCell);
                if (dstCell) {
                    uint64_t key = cellKey(dstCell->m_row, dstCell->m_column);
                    XMap_insert_base((XMapBase*)ws->m_cellTable, &key, &dstCell);
                }
            }
        }
    }
    if (self->m_colInfoMap) {
        XMap_iterator it = XMap_begin(self->m_colInfoMap);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_colInfoMap, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            int column = pair ? *(int*)XPair_first(pair) : 0;
            XWorksheet_ColumnInfo* source =
                (XWorksheet_ColumnInfo*)pairPointerValue(pair);
            if (!source || column <= 0) continue;
            XWorksheet_ColumnInfo* copy =
                (XWorksheet_ColumnInfo*)XMalloc_System(sizeof(*copy));
            if (!copy) continue;
            *copy = *source;
            XMap_insert_base((XMapBase*)ws->m_colInfoMap, &column, &copy);
        }
    }
    if (self->m_rowInfoMap) {
        XMap_iterator it = XMap_begin(self->m_rowInfoMap);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_rowInfoMap, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            int row = pair ? *(int*)XPair_first(pair) : 0;
            XWorksheet_RowInfo* source =
                (XWorksheet_RowInfo*)pairPointerValue(pair);
            if (!source || row <= 0) continue;
            XWorksheet_RowInfo* copy =
                (XWorksheet_RowInfo*)XMalloc_System(sizeof(*copy));
            if (!copy) continue;
            *copy = *source;
            XMap_insert_base((XMapBase*)ws->m_rowInfoMap, &row, &copy);
        }
    }
    if (self->m_mergedCells) {
        size_t count = XVector_size_base((XContainer*)self->m_mergedCells);
        for (size_t i = 0; i < count; ++i) {
            XCellRange* range = (XCellRange*)XVector_at_base(self->m_mergedCells, i);
            if (range) XVector_push_back_2(ws->m_mergedCells, range, 1);
        }
    }
    if (self->m_dataValidations) {
        size_t count = XVector_size_base((XContainer*)self->m_dataValidations);
        for (size_t i = 0; i < count; ++i) {
            XDataValidation* source = *(XDataValidation**)XVector_at_base(self->m_dataValidations, i);
            XDataValidation* copy = source ? XDataValidation_copy(source) : NULL;
            if (copy) XVector_push_back_2(ws->m_dataValidations, &copy, 1);
        }
    }
    if (self->m_conditionalFormatting) {
        size_t count = XVector_size_base((XContainer*)self->m_conditionalFormatting);
        for (size_t i = 0; i < count; ++i) {
            XConditionalFormatting* source =
                *(XConditionalFormatting**)XVector_at_base(self->m_conditionalFormatting, i);
            XConditionalFormatting* copy = source ? XConditionalFormatting_copy(source) : NULL;
            if (copy && !XWorksheet_addConditionalFormatting(ws, copy))
                XConditionalFormatting_delete(copy);
        }
    }
    if (self->m_hyperlinks) {
        size_t count = XVector_size_base((XContainer*)self->m_hyperlinks);
        for (size_t i = 0; i < count; ++i) {
            XWorksheet_Hyperlink* source =
                (XWorksheet_Hyperlink*)XVector_at_base(self->m_hyperlinks, i);
            if (!source) continue;
            XWorksheet_Hyperlink copy;
            memset(&copy, 0, sizeof(copy));
            copy.m_range = source->m_range;
            copy.m_url = source->m_url ? XString_create_copy(source->m_url) : NULL;
            copy.m_relationshipId = source->m_relationshipId
                ? XString_create_copy(source->m_relationshipId) : NULL;
            copy.m_display = source->m_display ? XString_create_copy(source->m_display) : NULL;
            copy.m_tip = source->m_tip ? XString_create_copy(source->m_tip) : NULL;
            XVector_push_back_2(ws->m_hyperlinks, &copy, 1);
        }
    }
    /* 复制属性 */
    ws->m_windowProtection = self->m_windowProtection;
    ws->m_showFormulas = self->m_showFormulas;
    ws->m_showGridLines = self->m_showGridLines;
    ws->m_showRowColHeaders = self->m_showRowColHeaders;
    ws->m_showZeros = self->m_showZeros;
    ws->m_rightToLeft = self->m_rightToLeft;
    ws->m_tabSelected = self->m_tabSelected;
    ws->m_showRuler = self->m_showRuler;
    ws->m_showOutlineSymbols = self->m_showOutlineSymbols;
    ws->m_showWhiteSpace = self->m_showWhiteSpace;
    ws->m_startPage = self->m_startPage;
    ws->m_dimension = self->m_dimension;
    if (self->m_mediaFiles && self->m_imagePositions) {
        size_t imageCount = XVector_size_base((XContainer*)self->m_mediaFiles);
        for (size_t i = 0; i < imageCount; ++i) {
            XMediaFile* source = *(XMediaFile**)XVector_at_base(self->m_mediaFiles, i);
            XWorksheet_ImagePosition* position =
                (XWorksheet_ImagePosition*)XVector_at_base(self->m_imagePositions, i);
            if (!source || !position) continue;
            XMediaFile* copy = XMediaFile_create_data(XMediaFile_contents(source),
                XMediaFile_contentsSize(source), XMediaFile_suffix(source), XMediaFile_mimeType(source));
            if (!copy) continue;
            XMediaFile_setFileName(copy, XMediaFile_fileName(source));
            XVector_push_back_2(ws->m_mediaFiles, &copy, 1);
            XVector_push_back_2(ws->m_imagePositions, position, 1);
        }
    }
    if (self->m_chartFiles) {
        size_t count = XVector_size_base((XContainer*)self->m_chartFiles);
        for (size_t i = 0; i < count; ++i) {
            XChart* source = *(XChart**)XVector_at_base(self->m_chartFiles, i);
            XChart* copy = source ? XChart_copy(source, &ws->m_base) : NULL;
            if (copy) XVector_push_back_2(ws->m_chartFiles, &copy, 1);
        }
    }
    if (self->m_rowSpans) {
        XMap_iterator it = XMap_begin(self->m_rowSpans);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_rowSpans, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            if (pair) XMap_insert_base((XMapBase*)ws->m_rowSpans,
                XPair_first(pair), XPair_second(pair));
        }
    }
    return ws;
}

void XWorksheet_delete(XWorksheet* self)
{
    if (!self) return;
    /* 释放单元格 */
    if (self->m_cellTable) {
        XMap_iterator it = XMap_begin(self->m_cellTable);
        XMap_iterator end = XMap_end(self->m_cellTable);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_cellTable, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            XCell* cell = (XCell*)pairPointerValue(pair);
            if (cell) XCell_delete(cell);
        }
        XMap_delete_base(self->m_cellTable);
    }
    /* 释放列信息 */
    if (self->m_colInfoMap) {
        XMap_iterator it = XMap_begin(self->m_colInfoMap);
        XMap_iterator end = XMap_end(self->m_colInfoMap);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_colInfoMap, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            XWorksheet_ColumnInfo* ci = (XWorksheet_ColumnInfo*)pairPointerValue(pair);
            if (ci) XFree_System(ci);
        }
        XMap_delete_base(self->m_colInfoMap);
    }
    /* 释放行信息 */
    if (self->m_rowInfoMap) {
        XMap_iterator it = XMap_begin(self->m_rowInfoMap);
        XMap_iterator end = XMap_end(self->m_rowInfoMap);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_rowInfoMap, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            XWorksheet_RowInfo* ri = (XWorksheet_RowInfo*)pairPointerValue(pair);
            if (ri) XFree_System(ri);
        }
        XMap_delete_base(self->m_rowInfoMap);
    }
    /* 释放超链接 */
    if (self->m_hyperlinks) {
        for (size_t i = 0; i < XVector_size_base((XContainer*)self->m_hyperlinks); ++i) {
            XWorksheet_Hyperlink* hl = (XWorksheet_Hyperlink*)XVector_at_base(self->m_hyperlinks, i);
            if (hl->m_url) XString_delete_base(hl->m_url);
            if (hl->m_relationshipId) XString_delete_base(hl->m_relationshipId);
            if (hl->m_display) XString_delete_base(hl->m_display);
            if (hl->m_tip) XString_delete_base(hl->m_tip);
        }
        XVector_delete_base(self->m_hyperlinks);
    }
    if (self->m_mergedCells) XVector_delete_base(self->m_mergedCells);
    /* 释放数据验证 */
    if (self->m_dataValidations) {
        for (size_t i = 0; i < XVector_size_base((XContainer*)self->m_dataValidations); ++i) {
            XDataValidation* dv = *(XDataValidation**)XVector_at_base(self->m_dataValidations, i);
            if (dv) XDataValidation_delete(dv);
        }
        XVector_delete_base(self->m_dataValidations);
    }
    /* 释放条件格式 */
    if (self->m_conditionalFormatting) {
        for (size_t i = 0; i < XVector_size_base((XContainer*)self->m_conditionalFormatting); ++i) {
            XConditionalFormatting* cf = *(XConditionalFormatting**)XVector_at_base(self->m_conditionalFormatting, i);
            if (cf) XConditionalFormatting_delete(cf);
        }
        XVector_delete_base(self->m_conditionalFormatting);
    }
    /* 释放媒体文件 */
    if (self->m_mediaFiles) {
        for (size_t i = 0; i < XVector_size_base((XContainer*)self->m_mediaFiles); ++i) {
            XMediaFile* mf = *(XMediaFile**)XVector_at_base(self->m_mediaFiles, i);
            if (mf) XMediaFile_delete(mf);
        }
        XVector_delete_base(self->m_mediaFiles);
    }
    if (self->m_imagePositions) {
        XVector_delete_base(self->m_imagePositions);
    }
    /* 释放图表文件 */
    if (self->m_chartFiles) {
        for (size_t i = 0; i < XVector_size_base((XContainer*)self->m_chartFiles); ++i) {
            XChart* ch = *(XChart**)XVector_at_base(self->m_chartFiles, i);
            if (ch) XChart_delete(ch);
        }
        XVector_delete_base(self->m_chartFiles);
    }
    if (self->m_rowSpans) XMap_delete_base(self->m_rowSpans);
    XAbstractSheet_deinit(&self->m_base);
    XFree_System(self);
}

/* ========== 单元格写入 ========== */

static XCell* getOrCreateCell(XWorksheet* self, int row, int column)
{
    if (!self || row < 1 || row > XLSX_ROW_MAX || column < 1 || column > XLSX_COLUMN_MAX) return NULL;
    uint64_t key = cellKey(row, column);
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_cellTable, &key, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return (XCell*)pairPointerValue(pair);
    }
    XCell* cell = XCell_create();
    if (!cell) return NULL;
    cell->m_row = row;
    cell->m_column = column;
    XMap_insert_base((XMapBase*)self->m_cellTable, &key, &cell);
    /* 更新维度 */
    if (self->m_dimension.m_firstRow < 1 || row < self->m_dimension.m_firstRow) self->m_dimension.m_firstRow = row;
    if (row > self->m_dimension.m_lastRow) self->m_dimension.m_lastRow = row;
    if (self->m_dimension.m_firstColumn < 1 || column < self->m_dimension.m_firstColumn) self->m_dimension.m_firstColumn = column;
    if (column > self->m_dimension.m_lastColumn) self->m_dimension.m_lastColumn = column;
    return cell;
}

bool XWorksheet_write(XWorksheet* self, int row, int column, const XVariant* value, const XFormat* format)
{
    if (!self || !value) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    apply_cell_format(self, cell, format);
    /* 根据 XVariant 类型写入 */
    int type = XVariant_type(value);
    if (type == XVariantType_ByteArray || type == XVariantType_String || type == XVariantType_StringList) {
        XString* tmpStr = XVariant_toString(value);
        XCell_setValue(cell, tmpStr);
        cell->m_cellType = XCell_SharedStringType;
        XString_delete_base(tmpStr);
    } else if (type == XVariantType_Int || type == XVariantType_Double || type == XVariantType_Uint32 || type == XVariantType_Int64 || type == XVariantType_Uint64) {
        double dval = XVariant_toDouble(value);
        XString* numStr = XString_create_fmt_utf8("%.15g", dval);
        XCell_setValue(cell, numStr);
        if (numStr) XString_delete_base(numStr);
        cell->m_cellType = XCell_NumberType;
    } else if (type == XVariantType_Bool) {
        XString* boolStr = XString_create_utf8(XVariant_toBool(value) ? "1" : "0");
        XCell_setValue(cell, boolStr);
        if (boolStr) XString_delete_base(boolStr);
        cell->m_cellType = XCell_BooleanType;
    } else if (type == XVariantType_Double || type == XVariantType_Double || type == XVariantType_Double) {
        XString* dateStr = XString_create_fmt_utf8("%.15g", XVariant_toDouble(value));
        XCell_setValue(cell, dateStr);
        if (dateStr) XString_delete_base(dateStr);
        cell->m_cellType = XCell_DateType;
    }
    return true;
}

bool XWorksheet_writeRef(XWorksheet* self, const XCellReference* cell, const XVariant* value, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_write(self, XCellReference_row(cell), XCellReference_column(cell), value, format);
}

bool XWorksheet_writeString(XWorksheet* self, int row, int column, const XString* value, const XFormat* format)
{
    if (!self || !value) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    apply_cell_format(self, cell, format);
    XCell_setValue(cell, value);
    cell->m_cellType = XCell_SharedStringType;
    return true;
}

bool XWorksheet_writeStringRef(XWorksheet* self, const XCellReference* cell, const XString* value, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_writeString(self, XCellReference_row(cell), XCellReference_column(cell), value, format);
}

bool XWorksheet_writeRichString(XWorksheet* self, int row, int column, const XRichString* value, const XFormat* format)
{
    if (!self || !value) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    apply_cell_format(self, cell, format);
    const XString* plain = XRichString_toPlainString(value);
    if (plain) {
        XCell_setValue(cell, plain);
        cell->m_cellType = XCell_SharedStringType;
    }
    /* 保存富文本 */
    XRichString* rs = XRichString_create();
    if (!rs) return false;
    XRichString_copy(rs, value);
    XCell_setRichString(cell, rs);
    return true;
}

bool XWorksheet_writeRichStringRef(XWorksheet* self, const XCellReference* cell, const XRichString* value, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_writeRichString(self, XCellReference_row(cell), XCellReference_column(cell), value, format);
}

bool XWorksheet_writeInlineString(XWorksheet* self, int row, int column, const XString* value, const XFormat* format)
{
    if (!self || !value) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    apply_cell_format(self, cell, format);
    XCell_setValue(cell, value);
    cell->m_cellType = XCell_InlineStringType;
    return true;
}

bool XWorksheet_writeInlineStringRef(XWorksheet* self, const XCellReference* cell, const XString* value, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_writeInlineString(self, XCellReference_row(cell), XCellReference_column(cell), value, format);
}

bool XWorksheet_writeNumeric(XWorksheet* self, int row, int column, double value, const XFormat* format)
{
    if (!self) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    apply_cell_format(self, cell, format);
    XString* numStr = XString_create_fmt_utf8("%.15g", value);
    XCell_setValue(cell, numStr);
    if (numStr) XString_delete_base(numStr);
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
    apply_cell_format(self, cell, format);
    XCellFormula* formulaCopy = XCellFormula_copy(formula);
    XString* resStr = XString_create_fmt_utf8("%.15g", result);
    if (!formulaCopy || !resStr) {
        if (formulaCopy) XCellFormula_delete(formulaCopy);
        if (resStr) XString_delete_base(resStr);
        return false;
    }
    XCell_setValue(cell, resStr);
    XString_delete_base(resStr);
    XCell_setFormula(cell, formulaCopy);
    cell->m_cellType = XCell_NumberType;
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
    apply_cell_format(self, cell, format);
    XCell_setValue(cell, NULL);
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
    apply_cell_format(self, cell, format);
    XString* boolStr = XString_create_utf8(value ? "1" : "0");
    XCell_setValue(cell, boolStr);
    if (boolStr) XString_delete_base(boolStr);
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
    apply_cell_format(self, cell, format);
    bool date1904 = self->m_base.m_workbook &&
        XWorkbook_isDate1904(self->m_base.m_workbook);
    double excelSerial = XUtility_dateTimeToExcelSerial(timestampMs, date1904);
    XString* dtStr = XString_create_fmt_utf8("%.15g", excelSerial);
    XCell_setValue(cell, dtStr);
    if (dtStr) XString_delete_base(dtStr);
    cell->m_cellType = XCell_DateType;
    return true;
}

bool XWorksheet_writeDateTimeRef(XWorksheet* self, const XCellReference* cell, int64_t timestampMs, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_writeDateTime(self, XCellReference_row(cell), XCellReference_column(cell), timestampMs, format);
}

bool XWorksheet_writeDate(XWorksheet* self, int row, int column, int year, int month, int day, const XFormat* format)
{
    if (!self) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    apply_cell_format(self, cell, format);
    double excelSerial = XUtility_epochToExcel(year, month, day, 0, 0, 0);
    if (self->m_base.m_workbook && XWorkbook_isDate1904(self->m_base.m_workbook))
        excelSerial -= 1462.0;
    XString* dStr = XString_create_fmt_utf8("%.15g", excelSerial);
    XCell_setValue(cell, dStr);
    if (dStr) XString_delete_base(dStr);
    cell->m_cellType = XCell_DateType;
    return true;
}

bool XWorksheet_writeDateRef(XWorksheet* self, const XCellReference* cell, int year, int month, int day, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_writeDate(self, XCellReference_row(cell), XCellReference_column(cell), year, month, day, format);
}

bool XWorksheet_writeTime(XWorksheet* self, int row, int column, int hour, int minute, double second, const XFormat* format)
{
    if (!self) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    apply_cell_format(self, cell, format);
    /* 时间序列号：一天的小数部分 */
    double excelSerial = (hour * 3600.0 + minute * 60.0 + second) / 86400.0;
    XString* tStr = XString_create_fmt_utf8("%.15g", excelSerial);
    XCell_setValue(cell, tStr);
    if (tStr) XString_delete_base(tStr);
    cell->m_cellType = XCell_DateType;
    return true;
}

bool XWorksheet_writeTimeRef(XWorksheet* self, const XCellReference* cell, int hour, int minute, double second, const XFormat* format)
{
    if (!cell) return false;
    return XWorksheet_writeTime(self, XCellReference_row(cell), XCellReference_column(cell), hour, minute, second, format);
}

bool XWorksheet_writeHyperlink(XWorksheet* self, int row, int column, const XString* url, const XFormat* format, const XString* display, const XString* tip)
{
    if (!self || !url || XString_isEmpty_base(url) || row < 1 || row > XLSX_ROW_MAX ||
        column < 1 || column > XLSX_COLUMN_MAX) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    apply_cell_format(self, cell, format);
    if (display) { XCell_setValue(cell, display); cell->m_cellType = XCell_SharedStringType; }
    /* 添加超链接 */
    XWorksheet_Hyperlink hl;
    memset(&hl, 0, sizeof(hl));
    XCellRange_setCell(&hl.m_range, row, column);
    hl.m_url = url ? XString_create_copy(url) : XString_create();
    if (display) { hl.m_display = XString_create_copy(display); }
    if (tip) { hl.m_tip = XString_create_copy(tip); }
    if (!hl.m_url || !XVector_push_back_2(self->m_hyperlinks, &hl, 1)) {
        if (hl.m_url) XString_delete_base(hl.m_url);
        if (hl.m_display) XString_delete_base(hl.m_display);
        if (hl.m_tip) XString_delete_base(hl.m_tip);
        return false;
    }
    return true;
}

bool XWorksheet_writeHyperlinkRef(XWorksheet* self, const XCellReference* cell, const XString* url, const XFormat* format, const XString* display, const XString* tip)
{
    if (!cell) return false;
    return XWorksheet_writeHyperlink(self, XCellReference_row(cell), XCellReference_column(cell), url, format, display, tip);
}

/* ========== 单元格读取 ========== */

XCell* XWorksheet_cellAt(XWorksheet* self, int row, int column)
{
    if (!self || row < 1 || column < 1) return NULL;
    uint64_t key = cellKey(row, column);
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_cellTable, &key, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return (XCell*)pairPointerValue(pair);
    }
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
    const XString* val = XCell_value(cell);
    if (!val || XString_isEmpty_base(val)) return NULL;
    XVariant* v = XVariant_create_null();
    if (!v) return NULL;
    XVariant_setValue_String(v, val);
    return v;
}

XVariant* XWorksheet_readRef(XWorksheet* self, const XCellReference* cell)
{
    if (!cell) return NULL;
    return XWorksheet_read(self, XCellReference_row(cell), XCellReference_column(cell));
}

/* ========== 数据验证与条件格式 ========== */

static bool dataValidationHasValidRange(const XDataValidation* validation)
{
    if (!validation || !validation->m_ranges) return false;
    size_t count = XVector_size_base((XContainer*)validation->m_ranges);
    for (size_t i = 0; i < count; ++i) {
        XCellRange* range = (XCellRange*)XVector_at_base(validation->m_ranges, i);
        if (range && XCellRange_isValid(range)) return true;
    }
    return false;
}

bool XWorksheet_addDataValidation(XWorksheet* self, XDataValidation* validation)
{
    if (!self || !self->m_dataValidations || !dataValidationHasValidRange(validation) ||
        validation->m_validationType < XDataValidation_None ||
        validation->m_validationType > XDataValidation_Custom ||
        validation->m_validationOperator < XDataValidation_Between ||
        validation->m_validationOperator > XDataValidation_GreaterThanOrEqual ||
        validation->m_errorStyle < XDataValidation_Stop ||
        validation->m_errorStyle > XDataValidation_Information) return false;
    return XVector_push_back_2(self->m_dataValidations, &validation, 1);
}

bool XWorksheet_addConditionalFormatting(XWorksheet* self, XConditionalFormatting* cf)
{
    if (!self || !cf) return false;
    XWorkbook* workbook = self->m_base.m_workbook;
    XStyles* styles = workbook ? workbook->m_styles : NULL;
    if (styles && cf->m_rules) {
        size_t count = XVector_size_base((XContainer*)cf->m_rules);
        for (size_t i = 0; i < count; ++i) {
            XConditionalFormatting_Rule* rule =
                (XConditionalFormatting_Rule*)XVector_at_base(cf->m_rules, i);
            if (!rule || !rule->m_format) continue;
            XStyles_addDxfFormat(styles, rule->m_format, false);
            size_t dxfCount = XVector_size_base((XContainer*)styles->m_dxfFormatsList);
            for (size_t j = 0; j < dxfCount; ++j) {
                XFormat* registered = *(XFormat**)XVector_at_base(styles->m_dxfFormatsList, j);
                if (registered && XFormat_equals(registered, rule->m_format)) {
                    XFormat_setDxfIndex(rule->m_format, (int)j);
                    break;
                }
            }
        }
    }
    return XVector_push_back_2(self->m_conditionalFormatting, &cf, 1);
}

/* ========== 图片与图表 ========== */

static bool image_type_from_path(const XString* imagePath, XString** suffix, XString** mimeType)
{
    if (suffix) *suffix = NULL;
    if (mimeType) *mimeType = NULL;
    if (!imagePath || !suffix || !mimeType) return false;

    const char* path = XString_toUtf8(imagePath);
    const char* dot = path ? strrchr(path, '.') : NULL;
    if (!dot || !dot[1]) return false;
    ++dot;

    const char* mime = NULL;
    XString* ext = XString_create_utf8(dot);
    if (!ext) return false;
    if (XString_equals_utf8(ext, "png", XChar_CaseInsensitive)) mime = "image/png";
    else if (XString_equals_utf8(ext, "jpg", XChar_CaseInsensitive) ||
             XString_equals_utf8(ext, "jpeg", XChar_CaseInsensitive)) mime = "image/jpeg";
    else if (XString_equals_utf8(ext, "gif", XChar_CaseInsensitive)) mime = "image/gif";
    else if (XString_equals_utf8(ext, "bmp", XChar_CaseInsensitive)) mime = "image/bmp";
    else {
        XString_delete_base(ext);
        return false;
    }

    *suffix = ext;
    *mimeType = XString_create_utf8(mime);
    if (!*mimeType) {
        XString_delete_base(ext);
        *suffix = NULL;
        return false;
    }
    return true;
}

static XMediaFile* media_file_from_path(const XString* imagePath)
{
    XString* suffix = NULL;
    XString* mimeType = NULL;
    if (!image_type_from_path(imagePath, &suffix, &mimeType)) return NULL;

    XFile* file = XFile_create_2(imagePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        XString_delete_base(suffix);
        XString_delete_base(mimeType);
        return NULL;
    }
    XByteArray* contents = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    if (!contents || XByteArray_size_base(contents) == 0) {
        if (contents) XByteArray_delete_base(contents);
        XString_delete_base(suffix);
        XString_delete_base(mimeType);
        return NULL;
    }

    XMediaFile* media = XMediaFile_create_data(XByteArray_data(contents),
        XByteArray_size_base(contents), suffix, mimeType);
    if (media) XMediaFile_setFileName(media, imagePath);
    XByteArray_delete_base(contents);
    XString_delete_base(suffix);
    XString_delete_base(mimeType);
    return media;
}

int XWorksheet_insertImage(XWorksheet* self, int row, int column, const XString* imagePath)
{
    if (!self || !imagePath || row < 1 || row > XLSX_ROW_MAX ||
        column < 1 || column > XLSX_COLUMN_MAX) return -1;
    XMediaFile* media = media_file_from_path(imagePath);
    if (!media) return -1;
    XWorksheet_ImagePosition position = { row, column };
    if (!XVector_push_back_2(self->m_mediaFiles, &media, 1)) {
        XMediaFile_delete(media);
        return -1;
    }
    if (!XVector_push_back_2(self->m_imagePositions, &position, 1)) {
        XVector_pop_back_base(self->m_mediaFiles);
        XMediaFile_delete(media);
        return -1;
    }
    return (int)XVector_size_base((XContainer*)self->m_mediaFiles) - 1;
}

bool XWorksheet_getImage(XWorksheet* self, int imageIndex, XByteArray* imgData)
{
    if (!self || !imgData || imageIndex < 0 ||
        (size_t)imageIndex >= XVector_size_base((XContainer*)self->m_mediaFiles)) return false;
    XMediaFile* media = *(XMediaFile**)XVector_at_base(self->m_mediaFiles, (size_t)imageIndex);
    if (!media) return false;
    const uint8_t* contents = XMediaFile_contents(media);
    size_t size = XMediaFile_contentsSize(media);
    if (!contents || size == 0) return false;
    XByteArray_clear_base(imgData);
    return XByteArray_push_back_2(imgData, contents, size);
}

bool XWorksheet_getImageAt(XWorksheet* self, int row, int column, XByteArray* imgData)
{
    if (!self || !imgData || !self->m_imagePositions) return false;
    size_t count = XVector_size_base((XContainer*)self->m_imagePositions);
    for (size_t i = 0; i < count; ++i) {
        XWorksheet_ImagePosition* position =
            (XWorksheet_ImagePosition*)XVector_at_base(self->m_imagePositions, i);
        if (position && position->m_row == row && position->m_column == column)
            return XWorksheet_getImage(self, (int)i, imgData);
    }
    return false;
}
unsigned int XWorksheet_getImageCount(const XWorksheet* self) { return self ? (unsigned int)XVector_size_base((XContainer*)self->m_mediaFiles) : 0; }

XChart* XWorksheet_insertChart(XWorksheet* self, int row, int column, int width, int height)
{
    if (!self) return NULL;
    XChart* chart = XChart_create((XAbstractSheet*)&self->m_base, XAbstractOOXmlFile_F_NewFromScratch);
    if (!chart) return NULL;
    XChart_setSize(chart, width, height);
    XChart_setPosition(chart, row, column, 0, 0);
    XVector_push_back_2(self->m_chartFiles, &chart, 1);
    return chart;
}

/* ========== 合并单元格 ========== */

bool XWorksheet_mergeCells(XWorksheet* self, int firstRow, int firstCol, int lastRow, int lastCol, const XFormat* format)
{
    if (!self || firstRow < 1 || firstCol < 1 || lastRow < firstRow ||
        lastCol < firstCol || lastRow > XLSX_ROW_MAX || lastCol > XLSX_COLUMN_MAX) return false;
    XCellRange range;
    XCellRange_setFullRange(&range, firstRow, firstCol, lastRow, lastCol);
    size_t count = XVector_size_base((XContainer*)self->m_mergedCells);
    for (size_t i = 0; i < count; ++i) {
        XCellRange* existing = (XCellRange*)XVector_at_base(self->m_mergedCells, i);
        if (existing && firstRow <= existing->m_lastRow && lastRow >= existing->m_firstRow &&
            firstCol <= existing->m_lastColumn && lastCol >= existing->m_firstColumn)
            return false;
    }
    if (!XVector_push_back_2(self->m_mergedCells, &range, 1)) return false;
    if (format) {
        for (int row = firstRow; row <= lastRow; ++row) {
            for (int column = firstCol; column <= lastCol; ++column) {
                XCell* cell = getOrCreateCell(self, row, column);
                if (cell) apply_cell_format(self, cell, format);
            }
        }
    }
    return true;
}

bool XWorksheet_unmergeCells(XWorksheet* self, int firstRow, int firstCol, int lastRow, int lastCol)
{
    if (!self || !self->m_mergedCells) return false;
    for (int i = (int)XVector_size_base((XContainer*)self->m_mergedCells) - 1; i >= 0; --i) {
        XCellRange* r = (XCellRange*)XVector_at_base(self->m_mergedCells, i);
        if (r->m_firstRow == firstRow && r->m_firstColumn == firstCol &&
            r->m_lastRow == lastRow && r->m_lastColumn == lastCol) {
            XVector_removeAt_base(self->m_mergedCells, (size_t)i);
            return true;
        }
    }
    return false;
}

XCellRange* XWorksheet_mergedCells(const XWorksheet* self, int* count)
{
    if (count) *count = (self && self->m_mergedCells) ? (int)XVector_size_base((XContainer*)self->m_mergedCells) : 0;
    return (self && self->m_mergedCells) ? (XCellRange*)XVector_data(self->m_mergedCells) : NULL;
}

/* ========== 列操作 ========== */

static XWorksheet_ColumnInfo* getOrCreateColInfo(XWorksheet* self, int col)
{
    if (!self || !self->m_colInfoMap || col < 1 || col > XLSX_COLUMN_MAX) return NULL;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_colInfoMap, &col, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return (XWorksheet_ColumnInfo*)pairPointerValue(pair);
    }
    XWorksheet_ColumnInfo* ci = (XWorksheet_ColumnInfo*)XMalloc_System(sizeof(XWorksheet_ColumnInfo));
    if (!ci) return NULL;
    memset(ci, 0, sizeof(XWorksheet_ColumnInfo));
    ci->m_width = -1.0;
    if (!XMap_insert_base((XMapBase*)self->m_colInfoMap, &col, &ci)) {
        XFree_System(ci);
        return NULL;
    }
    return ci;
}

static bool valid_column_range(int first, int last)
{
    return first >= 1 && last >= first && last <= XLSX_COLUMN_MAX;
}

bool XWorksheet_setColumnWidth(XWorksheet* self, int colFirst, int colLast, double width)
{
    if (!self || !valid_column_range(colFirst, colLast) || !isfinite(width) ||
        width < 0.0 || width > 255.0) return false;
    for (int c = colFirst; c <= colLast; ++c) {
        XWorksheet_ColumnInfo* ci = getOrCreateColInfo(self, c);
        if (!ci) return false;
        ci->m_width = width;
    }
    return true;
}

bool XWorksheet_setColumnFormat(XWorksheet* self, int colFirst, int colLast, const XFormat* format)
{
    if (!self || !valid_column_range(colFirst, colLast)) return false;
    XFormat* registered = format ? register_format(self, format, NULL) : NULL;
    if (format && !registered) return false;
    for (int c = colFirst; c <= colLast; ++c) {
        XWorksheet_ColumnInfo* ci = getOrCreateColInfo(self, c);
        if (!ci) return false;
        ci->m_format = registered;
    }
    return true;
}

bool XWorksheet_setColumnHidden(XWorksheet* self, int colFirst, int colLast, bool hidden)
{
    if (!self || !valid_column_range(colFirst, colLast)) return false;
    for (int c = colFirst; c <= colLast; ++c) {
        XWorksheet_ColumnInfo* ci = getOrCreateColInfo(self, c);
        if (!ci) return false;
        ci->m_hidden = hidden;
    }
    return true;
}

double XWorksheet_columnWidth(const XWorksheet* self, int column)
{
    if (!self) return -1.0;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_colInfoMap, &column, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        XWorksheet_ColumnInfo* info = (XWorksheet_ColumnInfo*)pairPointerValue(pair);
        return info ? info->m_width : -1.0;
    }
    return -1.0;
}

XFormat* XWorksheet_columnFormat(const XWorksheet* self, int column)
{
    if (!self) return NULL;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_colInfoMap, &column, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        XWorksheet_ColumnInfo* info = (XWorksheet_ColumnInfo*)pairPointerValue(pair);
        return info ? info->m_format : NULL;
    }
    return NULL;
}

bool XWorksheet_isColumnHidden(const XWorksheet* self, int column)
{
    if (!self) return false;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_colInfoMap, &column, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        XWorksheet_ColumnInfo* info = (XWorksheet_ColumnInfo*)pairPointerValue(pair);
        return info ? info->m_hidden : false;
    }
    return false;
}

bool XWorksheet_groupColumns(XWorksheet* self, int colFirst, int colLast, bool collapsed)
{
    if (!self || !valid_column_range(colFirst, colLast)) return false;
    for (int c = colFirst; c <= colLast; ++c) {
        XWorksheet_ColumnInfo* ci = getOrCreateColInfo(self, c);
        if (!ci) return false;
        ci->m_outlineLevel = 1;
        ci->m_collapsed = collapsed;
    }
    return true;
}


bool XWorksheet_groupColumnsRange(XWorksheet* self, const XCellRange* range, bool collapsed)
{
    if (!self || !range) return false;
    int first = XCellRange_firstColumn(range);
    int last = XCellRange_lastColumn(range);
    if (first <= 0 || last < first) return false;
    return XWorksheet_groupColumns(self, first, last, collapsed);
}
/* ========== 行操作 ========== */

static XWorksheet_RowInfo* getOrCreateRowInfo(XWorksheet* self, int row)
{
    if (!self || !self->m_rowInfoMap || row < 1 || row > XLSX_ROW_MAX) return NULL;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_rowInfoMap, &row, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return (XWorksheet_RowInfo*)pairPointerValue(pair);
    }
    XWorksheet_RowInfo* ri = (XWorksheet_RowInfo*)XMalloc_System(sizeof(XWorksheet_RowInfo));
    if (!ri) return NULL;
    memset(ri, 0, sizeof(XWorksheet_RowInfo));
    ri->m_height = -1.0;
    if (!XMap_insert_base((XMapBase*)self->m_rowInfoMap, &row, &ri)) {
        XFree_System(ri);
        return NULL;
    }
    return ri;
}

static bool valid_row_range(int first, int last)
{
    return first >= 1 && last >= first && last <= XLSX_ROW_MAX;
}

bool XWorksheet_setRowHeight(XWorksheet* self, int rowFirst, int rowLast, double height)
{
    if (!self || !valid_row_range(rowFirst, rowLast) || !isfinite(height) ||
        height < 0.0 || height > 409.5) return false;
    for (int r = rowFirst; r <= rowLast; ++r) {
        XWorksheet_RowInfo* ri = getOrCreateRowInfo(self, r);
        if (!ri) return false;
        ri->m_height = height;
    }
    return true;
}

bool XWorksheet_setRowFormat(XWorksheet* self, int rowFirst, int rowLast, const XFormat* format)
{
    if (!self || !valid_row_range(rowFirst, rowLast)) return false;
    XFormat* registered = format ? register_format(self, format, NULL) : NULL;
    if (format && !registered) return false;
    for (int r = rowFirst; r <= rowLast; ++r) {
        XWorksheet_RowInfo* ri = getOrCreateRowInfo(self, r);
        if (!ri) return false;
        ri->m_format = registered;
    }
    return true;
}

bool XWorksheet_setRowHidden(XWorksheet* self, int rowFirst, int rowLast, bool hidden)
{
    if (!self || !valid_row_range(rowFirst, rowLast)) return false;
    for (int r = rowFirst; r <= rowLast; ++r) {
        XWorksheet_RowInfo* ri = getOrCreateRowInfo(self, r);
        if (!ri) return false;
        ri->m_hidden = hidden;
    }
    return true;
}

double XWorksheet_rowHeight(const XWorksheet* self, int row)
{
    if (!self) return -1.0;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_rowInfoMap, &row, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        XWorksheet_RowInfo* info = (XWorksheet_RowInfo*)pairPointerValue(pair);
        return info ? info->m_height : -1.0;
    }
    return -1.0;
}

XFormat* XWorksheet_rowFormat(const XWorksheet* self, int row)
{
    if (!self) return NULL;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_rowInfoMap, &row, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        XWorksheet_RowInfo* info = (XWorksheet_RowInfo*)pairPointerValue(pair);
        return info ? info->m_format : NULL;
    }
    return NULL;
}

bool XWorksheet_isRowHidden(const XWorksheet* self, int row)
{
    if (!self) return false;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_rowInfoMap, &row, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        XWorksheet_RowInfo* info = (XWorksheet_RowInfo*)pairPointerValue(pair);
        return info ? info->m_hidden : false;
    }
    return false;
}

bool XWorksheet_groupRows(XWorksheet* self, int rowFirst, int rowLast, bool collapsed)
{
    if (!self || !valid_row_range(rowFirst, rowLast)) return false;
    for (int r = rowFirst; r <= rowLast; ++r) {
        XWorksheet_RowInfo* ri = getOrCreateRowInfo(self, r);
        if (!ri) return false;
        ri->m_outlineLevel = 1;
        ri->m_collapsed = collapsed;
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
    int count = (int)XMap_size_base((XContainer*)self->m_cellTable);
    *locations = (XCellLocation*)XMalloc_System(sizeof(XCellLocation) * (size_t)count);
    if (!*locations) return 0;
    int idx = 0;
    XMap_iterator it = XMap_begin(self->m_cellTable);
    XMap_iterator end = XMap_end(self->m_cellTable);
    int mr = 0, mc = 0;
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_cellTable, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        XCell* cell = (XCell*)pairPointerValue(pair);
        XCellLocation* loc = &(*locations)[idx++];
        loc->m_row = cell->m_row;
        loc->m_col = cell->m_column;
        loc->m_cell = (XCell*)cell;
        if (cell->m_row > mr) mr = cell->m_row;
        if (cell->m_column > mc) mc = cell->m_column;
    }
    if (maxRow) *maxRow = mr;
    if (maxCol) *maxCol = mc;
    return count;
}

/* ========== XML 读写 ========== */


/* ========== XML 序列化 ========== */

/* 辅助：单元格类型枚举转字符串 */
static const char* cellTypeToStr(XCell_CellType type) {
    switch (type) {
        case XCell_BooleanType: return "b";
        case XCell_DateType: return "d";
        case XCell_ErrorType: return "e";
        case XCell_InlineStringType: return "inlineStr";
        case XCell_NumberType: return "n";
        case XCell_SharedStringType: return "s";
        case XCell_StringType: return "str";
        default: return "n";
    }
}

/* 辅助：将列号转为字母 */
static void colNumToLetters(int col, char* buf, size_t bufSize) {
    int temp = col;
    buf[0] = '\0';
    while (temp > 0) {
        temp--;
        char c = (char)('A' + (temp % 26));
        size_t len = strlen(buf);
        if (len < bufSize - 1) {
            memmove(buf + 1, buf, len + 1);
            buf[0] = c;
        }
        temp /= 26;
    }
    if (buf[0] == '\0') strcpy(buf, "A");
}

/* 辅助：转义 XML 特殊字符 */
static void xmlEscape(const char* src, XByteArray* dest) {
    if (!src || !dest) return;
    for (const char* p = src; *p; p++) {
        if (*p == '<') XByteArray_append_utf8(dest, "&lt;");
        else if (*p == '>') XByteArray_append_utf8(dest, "&gt;");
        else if (*p == '&') XByteArray_append_utf8(dest, "&amp;");
        else if (*p == '"') XByteArray_append_utf8(dest, "&quot;");
        else if (*p == '\'') XByteArray_append_utf8(dest, "&apos;");
        else {
            char c[2] = {*p, 0};
            XByteArray_append_utf8(dest, c);
        }
    }
}

static void appendXmlAttribute(XByteArray* output, const char* name, const XString* value)
{
    if (!output || !name || !value || XString_isEmpty_base(value)) return;
    XByteArray_append_utf8(output, " ");
    XByteArray_append_utf8(output, name);
    XByteArray_append_utf8(output, "=\"");
    xmlEscape(XString_toUtf8(value), output);
    XByteArray_append_utf8(output, "\"");
}

static bool appendRangeList(XByteArray* output, const XVector* ranges)
{
    if (!output || !ranges || XVector_size_base((XContainer*)ranges) == 0) return false;
    bool wrote = false;
    size_t count = XVector_size_base((XContainer*)ranges);
    for (size_t i = 0; i < count; ++i) {
        XCellRange* range = (XCellRange*)XVector_at_base((XVector*)ranges, i);
        if (!range || !XCellRange_isValid(range)) continue;
        XString text = XCellRange_toString(range, false, false);
        if (wrote) XByteArray_append_utf8(output, " ");
        xmlEscape(XString_toUtf8(&text), output);
        XString_deinit_base(&text);
        wrote = true;
    }
    return wrote;
}

static const char* validationTypeName(XDataValidation_ValidationType type)
{
    static const char* names[] = { "none", "whole", "decimal", "list", "date",
        "time", "textLength", "custom" };
    return type >= XDataValidation_None && type <= XDataValidation_Custom
        ? names[(int)type] : "none";
}

static const char* validationOperatorName(XDataValidation_ValidationOperator op)
{
    static const char* names[] = { "between", "notBetween", "equal", "notEqual",
        "lessThan", "lessThanOrEqual", "greaterThan", "greaterThanOrEqual" };
    return op >= XDataValidation_Between && op <= XDataValidation_GreaterThanOrEqual
        ? names[(int)op] : "between";
}

static const char* validationErrorStyleName(XDataValidation_ErrorStyle style)
{
    static const char* names[] = { "stop", "warning", "information" };
    return style >= XDataValidation_Stop && style <= XDataValidation_Information
        ? names[(int)style] : "stop";
}

static const char* cfValueTypeName(XConditionalFormatting_ValueObjectType type)
{
    static const char* names[] = { "formula", "max", "min", "num", "percent", "percentile" };
    return type >= XCF_VOT_Formula && type <= XCF_VOT_Percentile ? names[(int)type] : "num";
}

static void appendColorAttribute(XByteArray* output, const XColor* color)
{
    char value[16];
    snprintf(value, sizeof(value), "%02X%02X%02X%02X", XColor_alpha(color),
        XColor_red(color), XColor_green(color), XColor_blue(color));
    XByteArray_append_utf8(output, value);
}

static void appendFormulaElement(XByteArray* output, const XString* formula)
{
    if (!formula || XString_isEmpty_base(formula)) return;
    XByteArray_append_utf8(output, "<formula>");
    xmlEscape(XString_toUtf8(formula), output);
    XByteArray_append_utf8(output, "</formula>");
}

static const char* highlightRuleType(XConditionalFormatting_HighlightRuleType type,
                                     const char** operation)
{
    if (operation) *operation = NULL;
    static const char* cellOperators[] = { "lessThan", "lessThanOrEqual", "equal", "notEqual",
        "greaterThanOrEqual", "greaterThan", "between", "notBetween" };
    if (type >= XCF_Highlight_LessThan && type <= XCF_Highlight_NotBetween) {
        if (operation) *operation = cellOperators[(int)type];
        return "cellIs";
    }
    switch (type) {
    case XCF_Highlight_ContainsText: return "containsText";
    case XCF_Highlight_NotContainsText: return "notContainsText";
    case XCF_Highlight_BeginsWith: return "beginsWith";
    case XCF_Highlight_EndsWith: return "endsWith";
    case XCF_Highlight_TimePeriod: return "timePeriod";
    case XCF_Highlight_Duplicate: return "duplicateValues";
    case XCF_Highlight_Unique: return "uniqueValues";
    case XCF_Highlight_Blanks: return "containsBlanks";
    case XCF_Highlight_NoBlanks: return "notContainsBlanks";
    case XCF_Highlight_Errors: return "containsErrors";
    case XCF_Highlight_NoErrors: return "notContainsErrors";
    case XCF_Highlight_Top:
    case XCF_Highlight_TopPercent:
    case XCF_Highlight_Bottom:
    case XCF_Highlight_BottomPercent: return "top10";
    case XCF_Highlight_AboveAverage:
    case XCF_Highlight_AboveOrEqualAverage:
    case XCF_Highlight_AboveStdDev1:
    case XCF_Highlight_AboveStdDev2:
    case XCF_Highlight_AboveStdDev3:
    case XCF_Highlight_BelowAverage:
    case XCF_Highlight_BelowOrEqualAverage:
    case XCF_Highlight_BelowStdDev1:
    case XCF_Highlight_BelowStdDev2:
    case XCF_Highlight_BelowStdDev3: return "aboveAverage";
    default: return "expression";
    }
}

static void appendConditionalRule(XByteArray* output,
                                  const XConditionalFormatting_Rule* rule, int priority)
{
    if (!output || !rule) return;
    char number[64];
    XByteArray_append_utf8(output, "    <cfRule type=\"");
    if (rule->m_ruleType == XCF_Rule_DataBar) XByteArray_append_utf8(output, "dataBar");
    else if (rule->m_ruleType == XCF_Rule_ColorScale2 ||
             rule->m_ruleType == XCF_Rule_ColorScale3) XByteArray_append_utf8(output, "colorScale");
    else {
        const char* operation = NULL;
        XByteArray_append_utf8(output, highlightRuleType(rule->m_highlightType, &operation));
        if (operation) {
            XByteArray_append_utf8(output, "\" operator=\"");
            XByteArray_append_utf8(output, operation);
        }
    }
    snprintf(number, sizeof(number), "\" priority=\"%d\" stopIfTrue=\"%d\"",
        priority, rule->m_stopIfTrue ? 1 : 0);
    XByteArray_append_utf8(output, number);
    if (rule->m_format && XFormat_dxfIndexValid(rule->m_format)) {
        snprintf(number, sizeof(number), " dxfId=\"%d\"", XFormat_dxfIndex(rule->m_format));
        XByteArray_append_utf8(output, number);
    }
    if (rule->m_ruleType == XCF_Rule_HighlightCellsRule) {
        if (rule->m_highlightType == XCF_Highlight_TopPercent ||
            rule->m_highlightType == XCF_Highlight_BottomPercent)
            XByteArray_append_utf8(output, " percent=\"1\"");
        if (rule->m_highlightType == XCF_Highlight_Bottom ||
            rule->m_highlightType == XCF_Highlight_BottomPercent)
            XByteArray_append_utf8(output, " bottom=\"1\"");
        if (rule->m_highlightType >= XCF_Highlight_BelowAverage &&
            rule->m_highlightType <= XCF_Highlight_BelowStdDev3)
            XByteArray_append_utf8(output, " aboveAverage=\"0\"");
        if (rule->m_highlightType == XCF_Highlight_AboveOrEqualAverage ||
            rule->m_highlightType == XCF_Highlight_BelowOrEqualAverage)
            XByteArray_append_utf8(output, " equalAverage=\"1\"");
        if (rule->m_highlightType >= XCF_Highlight_AboveStdDev1 &&
            rule->m_highlightType <= XCF_Highlight_AboveStdDev3) {
            snprintf(number, sizeof(number), " stdDev=\"%d\"",
                (int)rule->m_highlightType - (int)XCF_Highlight_AboveStdDev1 + 1);
            XByteArray_append_utf8(output, number);
        } else if (rule->m_highlightType >= XCF_Highlight_BelowStdDev1 &&
                   rule->m_highlightType <= XCF_Highlight_BelowStdDev3) {
            snprintf(number, sizeof(number), " stdDev=\"%d\"",
                (int)rule->m_highlightType - (int)XCF_Highlight_BelowStdDev1 + 1);
            XByteArray_append_utf8(output, number);
        }
        if (rule->m_highlightType >= XCF_Highlight_Top &&
            rule->m_highlightType <= XCF_Highlight_BottomPercent) {
            snprintf(number, sizeof(number), " rank=\"%d\"", rule->m_rank > 0 ? rule->m_rank : 10);
            XByteArray_append_utf8(output, number);
        }
        if (rule->m_text) appendXmlAttribute(output, "text", rule->m_text);
        if (rule->m_timePeriod) appendXmlAttribute(output, "timePeriod", rule->m_timePeriod);
    }
    XByteArray_append_utf8(output, ">");
    if (rule->m_ruleType == XCF_Rule_HighlightCellsRule) {
        bool topRule = rule->m_highlightType >= XCF_Highlight_Top &&
            rule->m_highlightType <= XCF_Highlight_BottomPercent;
        bool timeRule = rule->m_highlightType == XCF_Highlight_TimePeriod;
        if (!topRule && !timeRule) {
            appendFormulaElement(output, rule->m_formula1);
            appendFormulaElement(output, rule->m_formula2);
        }
    } else {
        bool dataBar = rule->m_ruleType == XCF_Rule_DataBar;
        XByteArray_append_utf8(output, dataBar ? "<dataBar" : "<colorScale");
        if (dataBar) {
            XByteArray_append_utf8(output, " showValue=\"");
            XByteArray_append_utf8(output, rule->m_showData ? "1\">" : "0\">");
        } else XByteArray_append_utf8(output, ">");
        int valueCount = rule->m_ruleType == XCF_Rule_ColorScale3 ? 3 : 2;
        int colorCount = dataBar ? 1 : valueCount;
        for (int i = 0; i < valueCount; ++i) {
            XConditionalFormatting_ValueObjectType type = i == 0 ? rule->m_valType1
                : (valueCount == 3 && i == 1 ? rule->m_valType2
                   : (valueCount == 3 ? rule->m_valType3 : rule->m_valType2));
            XByteArray_append_utf8(output, "<cfvo type=\"");
            XByteArray_append_utf8(output, cfValueTypeName(type));
            const XString* value = i == 0 ? rule->m_formula1
                : (valueCount == 3 && i == 1 ? rule->m_formula2
                   : (valueCount == 3 ? rule->m_formula3 : rule->m_formula2));
            if (value && !XString_isEmpty_base(value)) {
                XByteArray_append_utf8(output, "\" val=\"");
                xmlEscape(XString_toUtf8(value), output);
            } else if (valueCount == 3 && i == 1 && type == XCF_VOT_Percentile) {
                XByteArray_append_utf8(output, "\" val=\"50");
            }
            XByteArray_append_utf8(output, "\"/>");
        }
        for (int i = 0; i < valueCount; ++i) {
            const XColor* color = i == 0 ? &rule->m_color1
                : (i == 1 ? &rule->m_color2 : &rule->m_color3);
            XByteArray_append_utf8(output, "<color rgb=\"");
            appendColorAttribute(output, color);
            XByteArray_append_utf8(output, "\"/>");
        }
        XByteArray_append_utf8(output, dataBar ? "</dataBar>" : "</colorScale>");
    }
    XByteArray_append_utf8(output, "</cfRule>\n");
}

static int compareRowNumber(const void* left, const void* right)
{
    int a = *(const int*)left;
    int b = *(const int*)right;
    return (a > b) - (a < b);
}

static void appendRowStart(XByteArray* output, int row,
                           const XWorksheet_RowInfo* rowInfo)
{
    char entry[256];
    if (rowInfo) {
        snprintf(entry, sizeof(entry),
            "    <row r=\"%d\" ht=\"%.15g\" customHeight=\"%d\" hidden=\"%d\" "
            "outlineLevel=\"%d\" collapsed=\"%d\"",
            row, rowInfo->m_height < 0.0 ? 15.0 : rowInfo->m_height,
            rowInfo->m_height >= 0.0, rowInfo->m_hidden,
            rowInfo->m_outlineLevel, rowInfo->m_collapsed);
        XByteArray_append_utf8(output, entry);
        if (rowInfo->m_format && XFormat_xfIndexValid(rowInfo->m_format)) {
            snprintf(entry, sizeof(entry), " s=\"%d\" customFormat=\"1\"",
                XFormat_xfIndex(rowInfo->m_format));
            XByteArray_append_utf8(output, entry);
        }
        XByteArray_append_utf8(output, ">\n");
    } else {
        snprintf(entry, sizeof(entry), "    <row r=\"%d\">\n", row);
        XByteArray_append_utf8(output, entry);
    }
}

static bool richTextNeedsPreservedSpace(const char* text)
{
    if (!text || !text[0]) return false;
    size_t length = strlen(text);
    return text[0] == ' ' || text[0] == '\t' || text[0] == '\r' || text[0] == '\n' ||
        text[length - 1] == ' ' || text[length - 1] == '\t' ||
        text[length - 1] == '\r' || text[length - 1] == '\n';
}

static void appendRichTextProperties(XByteArray* output, const XFormat* format)
{
    if (!output || !format) return;
    bool bold = XFormat_fontBold(format);
    bool italic = XFormat_fontItalic(format);
    bool strike = XFormat_fontStrikeOut(format);
    bool outline = XFormat_fontOutline(format);
    XFormat_FontUnderline underline = XFormat_fontUnderline(format);
    int fontSize = XFormat_fontSize(format);
    if (!bold && !italic && !strike && !outline &&
        underline == XFormat_FontUnderlineNone && fontSize <= 0) return;

    XByteArray_append_utf8(output, "<rPr>");
    if (bold) XByteArray_append_utf8(output, "<b/>");
    if (italic) XByteArray_append_utf8(output, "<i/>");
    if (strike) XByteArray_append_utf8(output, "<strike/>");
    if (outline) XByteArray_append_utf8(output, "<outline/>");
    if (fontSize > 0) {
        char sizeText[64];
        snprintf(sizeText, sizeof(sizeText), "<sz val=\"%d\"/>", fontSize);
        XByteArray_append_utf8(output, sizeText);
    }
    if (underline != XFormat_FontUnderlineNone) {
        const char* value = underline == XFormat_FontUnderlineDouble ? "double" :
            underline == XFormat_FontUnderlineSingleAccounting ? "singleAccounting" :
            underline == XFormat_FontUnderlineDoubleAccounting ? "doubleAccounting" : "single";
        XByteArray_append_utf8(output, "<u val=\"");
        XByteArray_append_utf8(output, value);
        XByteArray_append_utf8(output, "\"/>");
    }
    XByteArray_append_utf8(output, "</rPr>");
}

static void appendRichStringXml(XByteArray* output, const XRichString* richString)
{
    XByteArray_append_utf8(output, "<is>");
    int count = XRichString_fragmentCount(richString);
    for (int i = 0; i < count; ++i) {
        const XString* fragment = XRichString_fragmentText(richString, i);
        const char* text = fragment ? XString_toUtf8(fragment) : "";
        XByteArray_append_utf8(output, "<r>");
        appendRichTextProperties(output, XRichString_fragmentFormat(richString, i));
        XByteArray_append_utf8(output, richTextNeedsPreservedSpace(text)
            ? "<t xml:space=\"preserve\">" : "<t>");
        xmlEscape(text, output);
        XByteArray_append_utf8(output, "</t></r>");
    }
    XByteArray_append_utf8(output, "</is>");
}

static void appendCellXml(XByteArray* output, const XCell* cell, int row, int column)
{
    if (!output || !cell) return;
    char reference[32];
    char columnText[16];
    colNumToLetters(column, columnText, sizeof(columnText));
    snprintf(reference, sizeof(reference), "%s%d", columnText, row);
    char entry[256];
    const char* type = cellTypeToStr(cell->m_cellType);
    const char* value = cell->m_value ? XString_toUtf8(cell->m_value) : "";
    bool inlineString = cell->m_cellType == XCell_SharedStringType ||
                        cell->m_cellType == XCell_InlineStringType ||
                        (cell->m_richString && XRichString_isRichString(cell->m_richString));
    bool hasValue = cell->m_value && !XString_isEmpty_base(cell->m_value);
    bool hasFormula = cell->m_formula && XCellFormula_isValid(cell->m_formula);
    bool hasRichString = cell->m_richString && XRichString_isRichString(cell->m_richString);
    const char* outputType = inlineString ? "inlineStr" : type;
    if (cell->m_cellType == XCell_NumberType || cell->m_cellType == XCell_DateType)
        outputType = NULL;
    snprintf(entry, sizeof(entry), "      <c r=\"%s\"", reference);
    XByteArray_append_utf8(output, entry);
    if (cell->m_styleNumber >= 0) {
        snprintf(entry, sizeof(entry), " s=\"%d\"", cell->m_styleNumber);
        XByteArray_append_utf8(output, entry);
    }
    if (outputType) {
        XByteArray_append_utf8(output, " t=\"");
        XByteArray_append_utf8(output, outputType);
        XByteArray_append_utf8(output, "\"");
    }
    if (!hasValue && !hasFormula && !hasRichString) {
        XByteArray_append_utf8(output, "/>\n");
        return;
    }
    XByteArray_append_utf8(output, ">");
    if (hasFormula) {
        XByteArray_append_utf8(output, "<f>");
        const XString* formula = XCellFormula_formulaText(cell->m_formula);
        xmlEscape(formula ? XString_toUtf8(formula) : "", output);
        XByteArray_append_utf8(output, "</f>");
    }
    if (hasRichString) {
        appendRichStringXml(output, cell->m_richString);
    } else if (inlineString && hasValue) {
        XByteArray_append_utf8(output, richTextNeedsPreservedSpace(value)
            ? "<is><t xml:space=\"preserve\">" : "<is><t>");
        xmlEscape(value, output);
        XByteArray_append_utf8(output, "</t></is>");
    } else if (hasValue) {
        XByteArray_append_utf8(output, "<v>");
        xmlEscape(value, output);
        XByteArray_append_utf8(output, "</v>");
    }
    XByteArray_append_utf8(output, "</c>\n");
}

bool XWorksheet_saveToXmlData(const XWorksheet* self, uint8_t** outData, size_t* outLen)
{
    if (!self || !outData || !outLen) return false;
    *outData = NULL;
    *outLen = 0;
    
    XByteArray* buf = XByteArray_create();
    if (!buf) return false;
    size_t chartCount = self->m_chartFiles
        ? XVector_size_base((XContainer*)self->m_chartFiles) : 0;
    bool hasDrawing = XWorksheet_getImageCount(self) > 0 || chartCount > 0;
    bool hasExternalHyperlinks = false;
    if (self->m_hyperlinks) {
        size_t count = XVector_size_base((XContainer*)self->m_hyperlinks);
        for (size_t i = 0; i < count; ++i) {
            XWorksheet_Hyperlink* hyperlink =
                (XWorksheet_Hyperlink*)XVector_at_base(self->m_hyperlinks, i);
            const char* url = hyperlink && hyperlink->m_url
                ? XString_toUtf8(hyperlink->m_url) : NULL;
            if (url && url[0] && url[0] != '#') { hasExternalHyperlinks = true; break; }
        }
    }
    
    XByteArray_append_utf8(buf, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    XByteArray_append_utf8(buf, "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"");
    if (hasDrawing || hasExternalHyperlinks)
        XByteArray_append_utf8(buf, " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"");
    XByteArray_append_utf8(buf, ">\n");
    
    /* 写入 sheetPr */
    XByteArray_append_utf8(buf, "  <sheetPr/>\n");
    
    /* 写入 dimension */
    if (self->m_dimension.m_firstRow > 0) {
        char dimBuf[64];
        char startCol[16], endCol[16];
        colNumToLetters(self->m_dimension.m_firstColumn, startCol, sizeof(startCol));
        colNumToLetters(self->m_dimension.m_lastColumn, endCol, sizeof(endCol));
        snprintf(dimBuf, sizeof(dimBuf), "%s%d:%s%d",
            startCol, self->m_dimension.m_firstRow,
            endCol, self->m_dimension.m_lastRow);
        char dimEntry[128];
        snprintf(dimEntry, sizeof(dimEntry), "  <dimension ref=\"%s\"/>\n", dimBuf);
        XByteArray_append_utf8(buf, dimEntry);
    }
    
    /* 写入 sheetViews */
    {
        char view[512];
        snprintf(view, sizeof(view),
            "  <sheetViews><sheetView workbookViewId=\"0\" showFormulas=\"%d\" "
            "showGridLines=\"%d\" showRowColHeaders=\"%d\" showZeros=\"%d\" "
            "rightToLeft=\"%d\" tabSelected=\"%d\" showRuler=\"%d\" "
            "showOutlineSymbols=\"%d\" showWhiteSpace=\"%d\"/></sheetViews>\n",
            self->m_showFormulas, self->m_showGridLines, self->m_showRowColHeaders,
            self->m_showZeros, self->m_rightToLeft, self->m_tabSelected,
            self->m_showRuler, self->m_showOutlineSymbols, self->m_showWhiteSpace);
        XByteArray_append_utf8(buf, view);
    }
    
    /* 写入 sheetFormatPr */
    XByteArray_append_utf8(buf, "  <sheetFormatPr defaultRowHeight=\"15\"/>\n");
    
    /* 写入 cols */
    if (self->m_colInfoMap && XMap_size_base((const XContainer*)self->m_colInfoMap) > 0) {
        XByteArray_append_utf8(buf, "  <cols>\n");
        XMap_iterator colIt = XMap_begin(self->m_colInfoMap);
        for (; !XMap_iterator_isEnd(&colIt); XMap_iterator_add(self->m_colInfoMap, &colIt)) {
            XPair* pair = XMap_iterator_data(&colIt);
            int column = pair ? *(int*)XPair_first(pair) : 0;
            XWorksheet_ColumnInfo* colInfo =
                (XWorksheet_ColumnInfo*)pairPointerValue(pair);
            if (colInfo) {
                char colEntry[256];
                snprintf(colEntry, sizeof(colEntry),
                    "    <col min=\"%d\" max=\"%d\" width=\"%.15g\" customWidth=\"%d\" "
                    "hidden=\"%d\" outlineLevel=\"%d\" collapsed=\"%d\"",
                    column, column, colInfo->m_width < 0.0 ? 8.43 : colInfo->m_width,
                    colInfo->m_width >= 0.0, colInfo->m_hidden,
                    colInfo->m_outlineLevel, colInfo->m_collapsed);
                XByteArray_append_utf8(buf, colEntry);
                if (colInfo->m_format && XFormat_xfIndexValid(colInfo->m_format)) {
                    snprintf(colEntry, sizeof(colEntry), " style=\"%d\"", XFormat_xfIndex(colInfo->m_format));
                    XByteArray_append_utf8(buf, colEntry);
                }
                XByteArray_append_utf8(buf, "/>\n");
            }
        }
        XByteArray_append_utf8(buf, "  </cols>\n");
    }
    
    /* 写入 sheetData - 最关键的部分 */
    XByteArray_append_utf8(buf, "  <sheetData>\n");
    
    XVector* rowNumbers = XVector_Create(int);
    if (!rowNumbers) {
        XByteArray_delete_base(buf);
        return false;
    }
    if (self->m_cellTable) {
        XMap_iterator iterator = XMap_begin(self->m_cellTable);
        int previousRow = -1;
        for (; !XMap_iterator_isEnd(&iterator);
             XMap_iterator_add(self->m_cellTable, &iterator)) {
            XPair* pair = XMap_iterator_data(&iterator);
            uint64_t key = pair ? *(uint64_t*)XPair_first(pair) : 0;
            int row = (int)(key >> 32);
            if (row > 0 && row != previousRow) {
                XVector_push_back_2(rowNumbers, &row, 1);
                previousRow = row;
            }
        }
    }
    if (self->m_rowInfoMap) {
        XMap_iterator iterator = XMap_begin(self->m_rowInfoMap);
        for (; !XMap_iterator_isEnd(&iterator);
             XMap_iterator_add(self->m_rowInfoMap, &iterator)) {
            XPair* pair = XMap_iterator_data(&iterator);
            int row = pair ? *(int*)XPair_first(pair) : 0;
            if (row > 0) XVector_push_back_2(rowNumbers, &row, 1);
        }
    }
    size_t rowCount = XVector_size_base((XContainer*)rowNumbers);
    if (rowCount > 1)
        qsort(XVector_data(rowNumbers), rowCount, sizeof(int), compareRowNumber);
    XMap_iterator cellIterator;
    bool hasCellIterator = self->m_cellTable != NULL;
    if (hasCellIterator) cellIterator = XMap_begin(self->m_cellTable);
    int previousOutputRow = -1;
    for (size_t i = 0; i < rowCount; ++i) {
        int row = *(int*)XVector_at_base(rowNumbers, i);
        if (row == previousOutputRow) continue;
        previousOutputRow = row;
        XWorksheet_RowInfo* rowInfo = NULL;
        XMap_iterator rowIterator;
        if (self->m_rowInfoMap &&
            XMap_find_base((XMapBase*)self->m_rowInfoMap, &row, &rowIterator)) {
            XPair* rowPair = XMap_iterator_data(&rowIterator);
            rowInfo = (XWorksheet_RowInfo*)pairPointerValue(rowPair);
        }
        appendRowStart(buf, row, rowInfo);
        while (hasCellIterator && !XMap_iterator_isEnd(&cellIterator)) {
            XPair* pair = XMap_iterator_data(&cellIterator);
            uint64_t key = pair ? *(uint64_t*)XPair_first(pair) : 0;
            int cellRow = (int)(key >> 32);
            int column = (int)(key & 0xffffffffu);
            if (cellRow > row) break;
            if (cellRow == row && pair) {
                XCell* cell = (XCell*)pairPointerValue(pair);
                if (cell) appendCellXml(buf, cell, row, column);
            }
            XMap_iterator_add(self->m_cellTable, &cellIterator);
        }
        XByteArray_append_utf8(buf, "    </row>\n");
    }
    XVector_delete_base(rowNumbers);
    
    XByteArray_append_utf8(buf, "  </sheetData>\n");
    if (self->m_windowProtection)
        XByteArray_append_utf8(buf, "  <sheetProtection sheet=\"1\"/>\n");
    
    /* 写入 mergeCells */
    if (self->m_mergedCells && XVector_size_base((const XContainer*)self->m_mergedCells) > 0) {
        size_t mergeCount = XVector_size_base((const XContainer*)self->m_mergedCells);
        char mergeHeader[128];
        snprintf(mergeHeader, sizeof(mergeHeader), "  <mergeCells count=\"%d\">\n", (int)mergeCount);
        XByteArray_append_utf8(buf, mergeHeader);
        
        for (size_t i = 0; i < mergeCount; i++) {
            XCellRange* range = (XCellRange*)XVector_at_base(self->m_mergedCells, i);
            if (range) {
                char startCol[16], endCol[16];
                colNumToLetters(range->m_firstColumn, startCol, sizeof(startCol));
                colNumToLetters(range->m_lastColumn, endCol, sizeof(endCol));
                char mergeEntry[128];
                snprintf(mergeEntry, sizeof(mergeEntry),
                    "    <mergeCell ref=\"%s%d:%s%d\"/>\n",
                    startCol, range->m_firstRow, endCol, range->m_lastRow);
                XByteArray_append_utf8(buf, mergeEntry);
            }
        }
        XByteArray_append_utf8(buf, "  </mergeCells>\n");
    }

    /* 条件格式 */
    int priority = 1;
    if (self->m_conditionalFormatting) {
        size_t count = XVector_size_base((XContainer*)self->m_conditionalFormatting);
        for (size_t i = 0; i < count; ++i) {
            XConditionalFormatting* formatting =
                *(XConditionalFormatting**)XVector_at_base(self->m_conditionalFormatting, i);
            if (!formatting || !formatting->m_ranges || !formatting->m_rules ||
                XVector_size_base((XContainer*)formatting->m_ranges) == 0 ||
                XVector_size_base((XContainer*)formatting->m_rules) == 0) continue;
            XByteArray_append_utf8(buf, "  <conditionalFormatting sqref=\"");
            if (!appendRangeList(buf, formatting->m_ranges)) {
                XByteArray_append_utf8(buf, "\"/>\n");
                continue;
            }
            XByteArray_append_utf8(buf, "\">\n");
            size_t ruleCount = XVector_size_base((XContainer*)formatting->m_rules);
            for (size_t j = 0; j < ruleCount; ++j) {
                XConditionalFormatting_Rule* rule =
                    (XConditionalFormatting_Rule*)XVector_at_base(formatting->m_rules, j);
                appendConditionalRule(buf, rule, priority++);
            }
            XByteArray_append_utf8(buf, "  </conditionalFormatting>\n");
        }
    }

    /* 数据验证 */
    if (self->m_dataValidations &&
        XVector_size_base((XContainer*)self->m_dataValidations) > 0) {
        size_t count = XVector_size_base((XContainer*)self->m_dataValidations);
        size_t validCount = 0;
        for (size_t i = 0; i < count; ++i) {
            XDataValidation* validation =
                *(XDataValidation**)XVector_at_base(self->m_dataValidations, i);
            if (dataValidationHasValidRange(validation)) ++validCount;
        }
        if (validCount == 0) goto skip_data_validations;
        char header[96];
        snprintf(header, sizeof(header), "  <dataValidations count=\"%zu\">\n", validCount);
        XByteArray_append_utf8(buf, header);
        for (size_t i = 0; i < count; ++i) {
            XDataValidation* validation =
                *(XDataValidation**)XVector_at_base(self->m_dataValidations, i);
            if (!validation || !validation->m_ranges ||
                XVector_size_base((XContainer*)validation->m_ranges) == 0) continue;
            XByteArray_append_utf8(buf, "    <dataValidation type=\"");
            XByteArray_append_utf8(buf, validationTypeName(validation->m_validationType));
            XByteArray_append_utf8(buf, "\" operator=\"");
            XByteArray_append_utf8(buf, validationOperatorName(validation->m_validationOperator));
            XByteArray_append_utf8(buf, "\" errorStyle=\"");
            XByteArray_append_utf8(buf, validationErrorStyleName(validation->m_errorStyle));
            char flags[160];
            snprintf(flags, sizeof(flags),
                "\" allowBlank=\"%d\" showInputMessage=\"%d\" showErrorMessage=\"%d\"",
                validation->m_allowBlank ? 1 : 0,
                validation->m_promptMessageVisible ? 1 : 0,
                validation->m_errorMessageVisible ? 1 : 0);
            XByteArray_append_utf8(buf, flags);
            appendXmlAttribute(buf, "promptTitle", validation->m_promptMessageTitle);
            appendXmlAttribute(buf, "prompt", validation->m_promptMessage);
            appendXmlAttribute(buf, "errorTitle", validation->m_errorMessageTitle);
            appendXmlAttribute(buf, "error", validation->m_errorMessage);
            XByteArray_append_utf8(buf, " sqref=\"");
            appendRangeList(buf, validation->m_ranges);
            XByteArray_append_utf8(buf, "\">");
            if (validation->m_formula1 && !XString_isEmpty_base(validation->m_formula1)) {
                XByteArray_append_utf8(buf, "<formula1>");
                xmlEscape(XString_toUtf8(validation->m_formula1), buf);
                XByteArray_append_utf8(buf, "</formula1>");
            }
            if (validation->m_formula2 && !XString_isEmpty_base(validation->m_formula2)) {
                XByteArray_append_utf8(buf, "<formula2>");
                xmlEscape(XString_toUtf8(validation->m_formula2), buf);
                XByteArray_append_utf8(buf, "</formula2>");
            }
            XByteArray_append_utf8(buf, "</dataValidation>\n");
        }
        XByteArray_append_utf8(buf, "  </dataValidations>\n");
skip_data_validations:;
    }

    /* 超链接：内部位置直接写 location，外部 URL 按 drawing 之后的 rId 顺序引用。 */
    if (self->m_hyperlinks && XVector_size_base((XContainer*)self->m_hyperlinks) > 0) {
        int relationId = hasDrawing ? 2 : 1;
        XByteArray_append_utf8(buf, "  <hyperlinks>\n");
        size_t count = XVector_size_base((XContainer*)self->m_hyperlinks);
        for (size_t i = 0; i < count; ++i) {
            XWorksheet_Hyperlink* hyperlink =
                (XWorksheet_Hyperlink*)XVector_at_base(self->m_hyperlinks, i);
            if (!hyperlink || !hyperlink->m_url || !XCellRange_isValid(&hyperlink->m_range)) continue;
            XString reference = XCellRange_toString(&hyperlink->m_range, false, false);
            XByteArray_append_utf8(buf, "    <hyperlink ref=\"");
            xmlEscape(XString_toUtf8(&reference), buf);
            XString_deinit_base(&reference);
            const char* url = XString_toUtf8(hyperlink->m_url);
            if (url && url[0] == '#') {
                XByteArray_append_utf8(buf, "\" location=\"");
                xmlEscape(url + 1, buf);
            } else {
                char rid[64];
                snprintf(rid, sizeof(rid), "\" r:id=\"rId%d", relationId++);
                XByteArray_append_utf8(buf, rid);
            }
            XByteArray_append_utf8(buf, "\"");
            appendXmlAttribute(buf, "display", hyperlink->m_display);
            appendXmlAttribute(buf, "tooltip", hyperlink->m_tip);
            XByteArray_append_utf8(buf, "/>\n");
        }
        XByteArray_append_utf8(buf, "  </hyperlinks>\n");
    }
    
    /* 写入 pageMargins */
    XByteArray_append_utf8(buf, "  <pageMargins left=\"0.75\" right=\"0.75\" top=\"1\" bottom=\"1\" header=\"0.5\" footer=\"0.5\"/>\n");
    if (self->m_startPage != 1) {
        char pageSetup[128];
        snprintf(pageSetup, sizeof(pageSetup),
            "  <pageSetup firstPageNumber=\"%d\" useFirstPageNumber=\"1\"/>\n", self->m_startPage);
        XByteArray_append_utf8(buf, pageSetup);
    }
    if (hasDrawing)
        XByteArray_append_utf8(buf, "  <drawing r:id=\"rId1\"/>\n");
    
    XByteArray_append_utf8(buf, "</worksheet>\n");
    
    *outData = (uint8_t*)XMalloc_System(XByteArray_size_base((const XContainer*)buf) + 1);
    if (*outData) {
        memcpy(*outData, XByteArray_data(buf), XByteArray_size_base((const XContainer*)buf));
        *outLen = XByteArray_size_base((const XContainer*)buf);
        (*outData)[*outLen] = '\0';
    }
    XByteArray_delete_base((XClass*)buf);
    return *outData != NULL;
}

bool XWorksheet_saveToXmlFile(XWorksheet* self, const XString* filePath)
{
    uint8_t* data = NULL;
    size_t len = 0;
    if (!XWorksheet_saveToXmlData(self, &data, &len)) return false;
    
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_WriteOnly | XIODevice_Truncate)) {
        if (file) XClass_delete_base((XClass*)file);
        XFree_System(data);
        return false;
    }
    XIODevice_write_1((XIODevice*)file, data, (int64_t)len);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    XFree_System(data);
    return true;
}

static const XString* worksheetAttribute(const XXmlStreamAttributes* attributes,
                                         const char* name)
{
    if (!attributes || !name) return NULL;
    XString_Init_Utf8(key, name);
    /* 带前缀的属性（例如 r:id）按限定名查询；普通属性按本地名查询。 */
    const XString* value = strchr(name, ':')
        ? XXmlStreamAttributes_value(attributes, key)
        : XXmlStreamAttributes_value_ex(attributes, NULL, key);
    XString_deinit_base(key);
    return value;
}

static bool worksheetBoolAttribute(const XXmlStreamAttributes* attributes,
                                   const char* name, bool defaultValue)
{
    const XString* value = worksheetAttribute(attributes, name);
    if (!value) return defaultValue;
    return XString_equals_utf8(value, "1", XChar_CaseSensitive) ||
           XString_equals_utf8(value, "true", XChar_CaseInsensitive);
}

static void parseRangeList(XVector* ranges, const XString* text)
{
    if (!ranges || !text) return;
    const char* input = XString_toUtf8(text);
    while (input && *input) {
        while (*input == ' ' || *input == '\t' || *input == '\r' || *input == '\n') ++input;
        const char* end = input;
        while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n') ++end;
        if (end > input) {
            size_t length = (size_t)(end - input);
            char* token = (char*)XMalloc_System(length + 1);
            if (token) {
                memcpy(token, input, length);
                token[length] = '\0';
                XCellRange range = XCellRange_create_str_utf8(token);
                if (XCellRange_isValid(&range)) XVector_push_back_2(ranges, &range, 1);
                XFree_System(token);
            }
        }
        input = end;
    }
}

static XDataValidation_ValidationType validationTypeFromName(const XString* value)
{
    for (int i = XDataValidation_None; i <= XDataValidation_Custom; ++i)
        if (XString_equals_utf8(value, validationTypeName((XDataValidation_ValidationType)i), XChar_CaseSensitive))
            return (XDataValidation_ValidationType)i;
    return XDataValidation_None;
}

static XDataValidation_ValidationOperator validationOperatorFromName(const XString* value)
{
    for (int i = XDataValidation_Between; i <= XDataValidation_GreaterThanOrEqual; ++i)
        if (XString_equals_utf8(value, validationOperatorName((XDataValidation_ValidationOperator)i), XChar_CaseSensitive))
            return (XDataValidation_ValidationOperator)i;
    return XDataValidation_Between;
}

static XDataValidation_ErrorStyle validationErrorStyleFromName(const XString* value)
{
    for (int i = XDataValidation_Stop; i <= XDataValidation_Information; ++i)
        if (XString_equals_utf8(value, validationErrorStyleName((XDataValidation_ErrorStyle)i), XChar_CaseSensitive))
            return (XDataValidation_ErrorStyle)i;
    return XDataValidation_Stop;
}

static XConditionalFormatting_ValueObjectType cfValueTypeFromName(const XString* value)
{
    for (int i = XCF_VOT_Formula; i <= XCF_VOT_Percentile; ++i)
        if (XString_equals_utf8(value, cfValueTypeName((XConditionalFormatting_ValueObjectType)i), XChar_CaseSensitive))
            return (XConditionalFormatting_ValueObjectType)i;
    return XCF_VOT_Num;
}

static XConditionalFormatting_HighlightRuleType highlightTypeFromAttributes(
    const XString* type, const XString* operation, bool percent, bool bottom,
    bool aboveAverage, bool equalAverage, int stdDev)
{
    if (XString_equals_utf8(type, "cellIs", XChar_CaseSensitive)) {
        static const char* operations[] = { "lessThan", "lessThanOrEqual", "equal", "notEqual",
            "greaterThanOrEqual", "greaterThan", "between", "notBetween" };
        for (int i = 0; i < 8; ++i)
            if (XString_equals_utf8(operation, operations[i], XChar_CaseSensitive))
                return (XConditionalFormatting_HighlightRuleType)i;
    }
    if (XString_equals_utf8(type, "containsText", XChar_CaseSensitive)) return XCF_Highlight_ContainsText;
    if (XString_equals_utf8(type, "notContainsText", XChar_CaseSensitive)) return XCF_Highlight_NotContainsText;
    if (XString_equals_utf8(type, "beginsWith", XChar_CaseSensitive)) return XCF_Highlight_BeginsWith;
    if (XString_equals_utf8(type, "endsWith", XChar_CaseSensitive)) return XCF_Highlight_EndsWith;
    if (XString_equals_utf8(type, "timePeriod", XChar_CaseSensitive)) return XCF_Highlight_TimePeriod;
    if (XString_equals_utf8(type, "duplicateValues", XChar_CaseSensitive)) return XCF_Highlight_Duplicate;
    if (XString_equals_utf8(type, "uniqueValues", XChar_CaseSensitive)) return XCF_Highlight_Unique;
    if (XString_equals_utf8(type, "containsBlanks", XChar_CaseSensitive)) return XCF_Highlight_Blanks;
    if (XString_equals_utf8(type, "notContainsBlanks", XChar_CaseSensitive)) return XCF_Highlight_NoBlanks;
    if (XString_equals_utf8(type, "containsErrors", XChar_CaseSensitive)) return XCF_Highlight_Errors;
    if (XString_equals_utf8(type, "notContainsErrors", XChar_CaseSensitive)) return XCF_Highlight_NoErrors;
    if (XString_equals_utf8(type, "top10", XChar_CaseSensitive))
        return bottom ? (percent ? XCF_Highlight_BottomPercent : XCF_Highlight_Bottom)
                      : (percent ? XCF_Highlight_TopPercent : XCF_Highlight_Top);
    if (XString_equals_utf8(type, "aboveAverage", XChar_CaseSensitive)) {
        if (stdDev > 0) {
            if (aboveAverage) return (XConditionalFormatting_HighlightRuleType)
                (XCF_Highlight_AboveStdDev1 + (stdDev > 3 ? 2 : stdDev - 1));
            return (XConditionalFormatting_HighlightRuleType)
                (XCF_Highlight_BelowStdDev1 + (stdDev > 3 ? 2 : stdDev - 1));
        }
        if (aboveAverage) return equalAverage
            ? XCF_Highlight_AboveOrEqualAverage : XCF_Highlight_AboveAverage;
        return equalAverage ? XCF_Highlight_BelowOrEqualAverage : XCF_Highlight_BelowAverage;
    }
    return XCF_Highlight_Expression;
}

static bool parseArgbColor(const XString* value, XColor* color)
{
    if (!value || !color) return false;
    const char* text = XString_toUtf8(value);
    size_t length = text ? strlen(text) : 0;
    if (length != 6 && length != 8) return false;
    char* end = NULL;
    unsigned long rgba = strtoul(text, &end, 16);
    if (!end || *end) return false;
    int alpha = length == 8 ? (int)((rgba >> 24) & 0xff) : 255;
    XColor_setRgb(color, (int)((rgba >> 16) & 0xff), (int)((rgba >> 8) & 0xff),
        (int)(rgba & 0xff), alpha);
    return true;
}

bool XWorksheet_loadFromXmlData(XWorksheet* self, const uint8_t* data, size_t len)
{
    if (!self || !data || len == 0) return false;

    /* 清理此前加载的单元格和布局数据，保留工作表身份及图片/图表对象。 */
    if (self->m_cellTable) {
        XMap_iterator it = XMap_begin(self->m_cellTable);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_cellTable, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            XCell* cell = (XCell*)pairPointerValue(pair);
            if (cell) XCell_delete(cell);
        }
        XMap_clear_base(self->m_cellTable);
    }
    if (self->m_colInfoMap) {
        XMap_iterator it = XMap_begin(self->m_colInfoMap);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_colInfoMap, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            XWorksheet_ColumnInfo* info =
                (XWorksheet_ColumnInfo*)pairPointerValue(pair);
            if (info) XFree_System(info);
        }
        XMap_clear_base(self->m_colInfoMap);
    }
    if (self->m_rowInfoMap) {
        XMap_iterator it = XMap_begin(self->m_rowInfoMap);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_rowInfoMap, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            XWorksheet_RowInfo* info = (XWorksheet_RowInfo*)pairPointerValue(pair);
            if (info) XFree_System(info);
        }
        XMap_clear_base(self->m_rowInfoMap);
    }
    if (self->m_mergedCells) XVector_clear_base(self->m_mergedCells);
    if (self->m_dataValidations) {
        size_t count = XVector_size_base((XContainer*)self->m_dataValidations);
        for (size_t i = 0; i < count; ++i) {
            XDataValidation* validation =
                *(XDataValidation**)XVector_at_base(self->m_dataValidations, i);
            if (validation) XDataValidation_delete(validation);
        }
        XVector_clear_base(self->m_dataValidations);
    }
    if (self->m_conditionalFormatting) {
        size_t count = XVector_size_base((XContainer*)self->m_conditionalFormatting);
        for (size_t i = 0; i < count; ++i) {
            XConditionalFormatting* formatting =
                *(XConditionalFormatting**)XVector_at_base(self->m_conditionalFormatting, i);
            if (formatting) XConditionalFormatting_delete(formatting);
        }
        XVector_clear_base(self->m_conditionalFormatting);
    }
    if (self->m_hyperlinks) {
        size_t count = XVector_size_base((XContainer*)self->m_hyperlinks);
        for (size_t i = 0; i < count; ++i) {
            XWorksheet_Hyperlink* hyperlink =
                (XWorksheet_Hyperlink*)XVector_at_base(self->m_hyperlinks, i);
            if (!hyperlink) continue;
            if (hyperlink->m_url) XString_delete_base(hyperlink->m_url);
            if (hyperlink->m_relationshipId) XString_delete_base(hyperlink->m_relationshipId);
            if (hyperlink->m_display) XString_delete_base(hyperlink->m_display);
            if (hyperlink->m_tip) XString_delete_base(hyperlink->m_tip);
        }
        XVector_clear_base(self->m_hyperlinks);
    }
    self->m_showFormulas = false;
    self->m_showGridLines = true;
    self->m_showRowColHeaders = true;
    self->m_showZeros = true;
    self->m_rightToLeft = false;
    self->m_tabSelected = false;
    self->m_showRuler = false;
    self->m_showOutlineSymbols = true;
    self->m_showWhiteSpace = true;
    self->m_windowProtection = false;
    self->m_startPage = 1;
    XCellRange_init(&self->m_dimension);

    XByteArray* xml = XByteArray_create_with_data((const char*)data, len);
    XXmlStreamReader* reader = XXmlStreamReader_create();
    XString* value = XString_create();
    XString* formulaText = XString_create();
    XString* cellType = NULL;
    if (!xml || !reader || !value || !formulaText) {
        if (xml) XByteArray_delete_base(xml);
        if (reader) XXmlStreamReader_delete_base(reader);
        if (value) XString_delete_base(value);
        if (formulaText) XString_delete_base(formulaText);
        if (cellType) XString_delete_base(cellType);
        return false;
    }
    XXmlStreamReader_addData(reader, xml);

    int cellRow = 0, cellColumn = 0, cellStyle = -1;
    bool inCell = false;
    XDataValidation* pendingValidation = NULL;
    XConditionalFormatting* pendingFormatting = NULL;
    XConditionalFormatting_Rule pendingRule;
    memset(&pendingRule, 0, sizeof(pendingRule));
    bool inConditionalRule = false;
    int cfvoIndex = 0;
    int colorIndex = 0;
    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        if (token == XXmlStream_StartElement) {
            const XString* element = XXmlStreamReader_name(reader);
            const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
            if (!element) continue;

#define WS_ATTR(varName, literal) \
            XString_Init_Utf8(varName##Name, literal); \
            const XString* varName = attributes ? XXmlStreamAttributes_value_ex(attributes, NULL, varName##Name) : NULL; \
            XString_deinit_base(varName##Name)

            if (XString_equals_utf8(element, "sheetView", XChar_CaseSensitive)) {
                WS_ATTR(showGridLines, "showGridLines");
                WS_ATTR(showHeaders, "showRowColHeaders");
                WS_ATTR(showZeros, "showZeros");
                WS_ATTR(rightToLeft, "rightToLeft");
                WS_ATTR(tabSelected, "tabSelected");
                WS_ATTR(showFormulas, "showFormulas");
                WS_ATTR(showRuler, "showRuler");
                WS_ATTR(showOutline, "showOutlineSymbols");
                WS_ATTR(showWhiteSpace, "showWhiteSpace");
#define WS_BOOL_VALUE(v, defaultValue) (!(v) ? (defaultValue) : \
                (XString_equals_utf8((v), "1", XChar_CaseSensitive) || \
                 XString_equals_utf8((v), "true", XChar_CaseInsensitive)))
                self->m_showGridLines = WS_BOOL_VALUE(showGridLines, true);
                self->m_showRowColHeaders = WS_BOOL_VALUE(showHeaders, true);
                self->m_showZeros = WS_BOOL_VALUE(showZeros, true);
                self->m_rightToLeft = WS_BOOL_VALUE(rightToLeft, false);
                self->m_tabSelected = WS_BOOL_VALUE(tabSelected, false);
                self->m_showFormulas = WS_BOOL_VALUE(showFormulas, false);
                self->m_showRuler = WS_BOOL_VALUE(showRuler, false);
                self->m_showOutlineSymbols = WS_BOOL_VALUE(showOutline, true);
                self->m_showWhiteSpace = WS_BOOL_VALUE(showWhiteSpace, true);
#undef WS_BOOL_VALUE
            } else if (XString_equals_utf8(element, "sheetProtection", XChar_CaseSensitive)) {
                self->m_windowProtection = worksheetBoolAttribute(attributes, "sheet", true);
            } else if (XString_equals_utf8(element, "pageSetup", XChar_CaseSensitive)) {
                const XString* firstPage = worksheetAttribute(attributes, "firstPageNumber");
                if (firstPage) self->m_startPage = atoi(XString_toUtf8(firstPage));
            } else if (XString_equals_utf8(element, "dataValidation", XChar_CaseSensitive)) {
                if (pendingValidation) XDataValidation_delete(pendingValidation);
                pendingValidation = XDataValidation_create();
                if (pendingValidation) {
                    const XString* type = worksheetAttribute(attributes, "type");
                    const XString* operation = worksheetAttribute(attributes, "operator");
                    const XString* style = worksheetAttribute(attributes, "errorStyle");
                    pendingValidation->m_validationType = validationTypeFromName(type);
                    pendingValidation->m_validationOperator = validationOperatorFromName(operation);
                    pendingValidation->m_errorStyle = validationErrorStyleFromName(style);
                    pendingValidation->m_allowBlank = worksheetBoolAttribute(attributes, "allowBlank", false);
                    pendingValidation->m_promptMessageVisible =
                        worksheetBoolAttribute(attributes, "showInputMessage", false);
                    pendingValidation->m_errorMessageVisible =
                        worksheetBoolAttribute(attributes, "showErrorMessage", false);
                    XDataValidation_setPromptMessage(pendingValidation,
                        worksheetAttribute(attributes, "prompt"),
                        worksheetAttribute(attributes, "promptTitle"));
                    XDataValidation_setErrorMessage(pendingValidation,
                        worksheetAttribute(attributes, "error"),
                        worksheetAttribute(attributes, "errorTitle"));
                    parseRangeList(pendingValidation->m_ranges,
                        worksheetAttribute(attributes, "sqref"));
                }
            } else if (pendingValidation &&
                       XString_equals_utf8(element, "formula1", XChar_CaseSensitive)) {
                const XString* text = XXmlStreamReader_readElementText(reader,
                    XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                XDataValidation_setFormula1(pendingValidation, text);
            } else if (pendingValidation &&
                       XString_equals_utf8(element, "formula2", XChar_CaseSensitive)) {
                const XString* text = XXmlStreamReader_readElementText(reader,
                    XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                XDataValidation_setFormula2(pendingValidation, text);
            } else if (XString_equals_utf8(element, "conditionalFormatting", XChar_CaseSensitive)) {
                if (pendingFormatting) XConditionalFormatting_delete(pendingFormatting);
                pendingFormatting = XConditionalFormatting_create();
                if (pendingFormatting) parseRangeList(pendingFormatting->m_ranges,
                    worksheetAttribute(attributes, "sqref"));
            } else if (pendingFormatting &&
                       XString_equals_utf8(element, "cfRule", XChar_CaseSensitive)) {
                memset(&pendingRule, 0, sizeof(pendingRule));
                inConditionalRule = true;
                cfvoIndex = 0;
                colorIndex = 0;
                const XString* typeString = worksheetAttribute(attributes, "type");
                const XString* operation = worksheetAttribute(attributes, "operator");
                const XString* stdDev = worksheetAttribute(attributes, "stdDev");
                pendingRule.m_ruleType = XString_equals_utf8(typeString, "dataBar", XChar_CaseSensitive)
                    ? XCF_Rule_DataBar
                    : XString_equals_utf8(typeString, "colorScale", XChar_CaseSensitive)
                        ? XCF_Rule_ColorScale2
                        : XCF_Rule_HighlightCellsRule;
                if (pendingRule.m_ruleType == XCF_Rule_HighlightCellsRule) {
                    pendingRule.m_highlightType = highlightTypeFromAttributes(typeString,
                        operation,
                        worksheetBoolAttribute(attributes, "percent", false),
                        worksheetBoolAttribute(attributes, "bottom", false),
                        worksheetBoolAttribute(attributes, "aboveAverage", true),
                        worksheetBoolAttribute(attributes, "equalAverage", false),
                        stdDev ? atoi(XString_toUtf8(stdDev)) : 0);
                    pendingRule.m_stdDev = stdDev ? atoi(XString_toUtf8(stdDev)) : 0;
                    const XString* rank = worksheetAttribute(attributes, "rank");
                    pendingRule.m_rank = rank ? atoi(XString_toUtf8(rank)) : 0;
                    const XString* text = worksheetAttribute(attributes, "text");
                    const XString* timePeriod = worksheetAttribute(attributes, "timePeriod");
                    pendingRule.m_text = text ? XString_create_copy(text) : NULL;
                    pendingRule.m_timePeriod = timePeriod ? XString_create_copy(timePeriod) : NULL;
                }
                pendingRule.m_stopIfTrue = worksheetBoolAttribute(attributes, "stopIfTrue", false);
                const XString* dxf = worksheetAttribute(attributes, "dxfId");
                if (dxf && self->m_base.m_workbook) {
                    int dxfId = atoi(XString_toUtf8(dxf));
                    XFormat* source = XStyles_dxfFormat(self->m_base.m_workbook->m_styles, dxfId);
                    if (source) {
                        pendingRule.m_format = XFormat_create();
                        if (pendingRule.m_format) {
                            XFormat_copy(pendingRule.m_format, source);
                            XFormat_setDxfIndex(pendingRule.m_format, dxfId);
                        }
                    }
                }
            } else if (inConditionalRule &&
                       XString_equals_utf8(element, "formula", XChar_CaseSensitive)) {
                const XString* text = XXmlStreamReader_readElementText(reader,
                    XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                XString** destination = pendingRule.m_formula1
                    ? &pendingRule.m_formula2 : &pendingRule.m_formula1;
                if (text) *destination = XString_create_copy(text);
            } else if (inConditionalRule &&
                       XString_equals_utf8(element, "dataBar", XChar_CaseSensitive)) {
                pendingRule.m_ruleType = XCF_Rule_DataBar;
                pendingRule.m_showData = worksheetBoolAttribute(attributes, "showValue", true);
            } else if (inConditionalRule &&
                       XString_equals_utf8(element, "colorScale", XChar_CaseSensitive)) {
                pendingRule.m_ruleType = XCF_Rule_ColorScale2;
            } else if (inConditionalRule &&
                       XString_equals_utf8(element, "cfvo", XChar_CaseSensitive)) {
                const XString* type = worksheetAttribute(attributes, "type");
                const XString* val = worksheetAttribute(attributes, "val");
                XConditionalFormatting_ValueObjectType parsedType = cfValueTypeFromName(type);
                if (cfvoIndex == 0) {
                    pendingRule.m_valType1 = parsedType;
                    if (val) pendingRule.m_formula1 = XString_create_copy(val);
                } else if (cfvoIndex == 1) {
                    pendingRule.m_valType2 = parsedType;
                    if (val) pendingRule.m_formula2 = XString_create_copy(val);
                } else if (cfvoIndex == 2) {
                    pendingRule.m_valType3 = parsedType;
                    if (val) pendingRule.m_formula3 = XString_create_copy(val);
                }
                ++cfvoIndex;
            } else if (inConditionalRule &&
                       XString_equals_utf8(element, "color", XChar_CaseSensitive)) {
                XColor* destination = colorIndex == 0 ? &pendingRule.m_color1
                    : (colorIndex == 1 ? &pendingRule.m_color2 : &pendingRule.m_color3);
                parseArgbColor(worksheetAttribute(attributes, "rgb"), destination);
                ++colorIndex;
                if (colorIndex >= 3) pendingRule.m_ruleType = XCF_Rule_ColorScale3;
            } else if (XString_equals_utf8(element, "hyperlink", XChar_CaseSensitive)) {
                const XString* reference = worksheetAttribute(attributes, "ref");
                XCellRange range = reference ? XCellRange_create_str(reference) : XCellRange_create();
                const XString* location = worksheetAttribute(attributes, "location");
                /* OOXML 的关系属性保留限定名 r:id；兼容无命名空间的旧文档。 */
                const XString* relationId = worksheetAttribute(attributes, "r:id");
                if (!relationId) relationId = worksheetAttribute(attributes, "id");
                if (XCellRange_isValid(&range) && (location || relationId)) {
                    XWorksheet_Hyperlink hyperlink;
                    memset(&hyperlink, 0, sizeof(hyperlink));
                    hyperlink.m_range = range;
                    if (location) {
                        hyperlink.m_url = XString_create_utf8("#");
                        if (hyperlink.m_url) XString_append(hyperlink.m_url, location);
                    } else {
                        hyperlink.m_url = XString_create();
                        hyperlink.m_relationshipId = XString_create_copy(relationId);
                    }
                    const XString* display = worksheetAttribute(attributes, "display");
                    const XString* tip = worksheetAttribute(attributes, "tooltip");
                    hyperlink.m_display = display ? XString_create_copy(display) : NULL;
                    hyperlink.m_tip = tip ? XString_create_copy(tip) : NULL;
                    XVector_push_back_2(self->m_hyperlinks, &hyperlink, 1);
                }
            } else if (XString_equals_utf8(element, "col", XChar_CaseSensitive)) {
                WS_ATTR(minimum, "min");
                WS_ATTR(maximum, "max");
                WS_ATTR(width, "width");
                WS_ATTR(hidden, "hidden");
                WS_ATTR(outline, "outlineLevel");
                WS_ATTR(collapsed, "collapsed");
                WS_ATTR(columnStyle, "style");
                int first = minimum ? atoi(XString_toUtf8(minimum)) : 0;
                int last = maximum ? atoi(XString_toUtf8(maximum)) : first;
                if (first >= 1 && last >= first && last <= XLSX_COLUMN_MAX) {
                    for (int column = first; column <= last; ++column) {
                        XWorksheet_ColumnInfo* info = getOrCreateColInfo(self, column);
                        if (!info) continue;
                        if (width) info->m_width = strtod(XString_toUtf8(width), NULL);
                        info->m_hidden = hidden && (XString_equals_utf8(hidden, "1", XChar_CaseSensitive) || XString_equals_utf8(hidden, "true", XChar_CaseInsensitive));
                        if (outline) info->m_outlineLevel = atoi(XString_toUtf8(outline));
                        info->m_collapsed = collapsed && (XString_equals_utf8(collapsed, "1", XChar_CaseSensitive) || XString_equals_utf8(collapsed, "true", XChar_CaseInsensitive));
                        if (columnStyle && self->m_base.m_workbook)
                            info->m_format = XStyles_xfFormat(self->m_base.m_workbook->m_styles,
                                atoi(XString_toUtf8(columnStyle)));
                    }
                }
            } else if (XString_equals_utf8(element, "row", XChar_CaseSensitive)) {
                WS_ATTR(rowNumber, "r");
                WS_ATTR(height, "ht");
                WS_ATTR(hidden, "hidden");
                WS_ATTR(outline, "outlineLevel");
                WS_ATTR(collapsed, "collapsed");
                WS_ATTR(rowStyle, "s");
                int row = rowNumber ? atoi(XString_toUtf8(rowNumber)) : 0;
                if (row >= 1 && row <= XLSX_ROW_MAX) {
                    XWorksheet_RowInfo* info = getOrCreateRowInfo(self, row);
                    if (info) {
                        if (height) info->m_height = strtod(XString_toUtf8(height), NULL);
                        info->m_hidden = hidden && (XString_equals_utf8(hidden, "1", XChar_CaseSensitive) || XString_equals_utf8(hidden, "true", XChar_CaseInsensitive));
                        if (outline) info->m_outlineLevel = atoi(XString_toUtf8(outline));
                        info->m_collapsed = collapsed && (XString_equals_utf8(collapsed, "1", XChar_CaseSensitive) || XString_equals_utf8(collapsed, "true", XChar_CaseInsensitive));
                        if (rowStyle && self->m_base.m_workbook)
                            info->m_format = XStyles_xfFormat(self->m_base.m_workbook->m_styles,
                                atoi(XString_toUtf8(rowStyle)));
                    }
                }
            } else if (XString_equals_utf8(element, "c", XChar_CaseSensitive)) {
                WS_ATTR(reference, "r");
                WS_ATTR(type, "t");
                WS_ATTR(style, "s");
                cellRow = cellColumn = 0;
                cellStyle = style ? atoi(XString_toUtf8(style)) : -1;
                if (cellType) { XString_delete_base(cellType); cellType = NULL; }
                if (type) cellType = XString_create_copy(type);
                if (reference) XReadSax_parseCellRef(reference, &cellRow, &cellColumn);
                XString_clear_base(value);
                XString_clear_base(formulaText);
                inCell = true;
            } else if (inCell && (XString_equals_utf8(element, "v", XChar_CaseSensitive) ||
                                  XString_equals_utf8(element, "t", XChar_CaseSensitive))) {
                const XString* text = XXmlStreamReader_readElementText(reader,
                    XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (text) XString_append(value, text);
            } else if (inCell && XString_equals_utf8(element, "f", XChar_CaseSensitive)) {
                const XString* text = XXmlStreamReader_readElementText(reader,
                    XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (text) XString_append(formulaText, text);
            } else if (XString_equals_utf8(element, "mergeCell", XChar_CaseSensitive)) {
                WS_ATTR(reference, "ref");
                if (reference) {
                    XCellRange range = XCellRange_create_str(reference);
                    if (XCellRange_isValid(&range)) XVector_push_back_2(self->m_mergedCells, &range, 1);
                }
            }
#undef WS_ATTR
        } else if (token == XXmlStream_EndElement) {
            const XString* element = XXmlStreamReader_name(reader);
            if (element && pendingValidation &&
                XString_equals_utf8(element, "dataValidation", XChar_CaseSensitive)) {
                if (!XWorksheet_addDataValidation(self, pendingValidation))
                    XDataValidation_delete(pendingValidation);
                pendingValidation = NULL;
            } else if (element && inConditionalRule &&
                       XString_equals_utf8(element, "cfRule", XChar_CaseSensitive)) {
                if (pendingFormatting &&
                    !XVector_push_back_2(pendingFormatting->m_rules, &pendingRule, 1)) {
                    if (pendingRule.m_formula1) XString_delete_base(pendingRule.m_formula1);
                    if (pendingRule.m_formula2) XString_delete_base(pendingRule.m_formula2);
                    if (pendingRule.m_formula3) XString_delete_base(pendingRule.m_formula3);
                    if (pendingRule.m_text) XString_delete_base(pendingRule.m_text);
                    if (pendingRule.m_timePeriod) XString_delete_base(pendingRule.m_timePeriod);
                    if (pendingRule.m_format) XFormat_delete(pendingRule.m_format);
                }
                memset(&pendingRule, 0, sizeof(pendingRule));
                inConditionalRule = false;
            } else if (element && pendingFormatting &&
                       XString_equals_utf8(element, "conditionalFormatting", XChar_CaseSensitive)) {
                if (!XVector_push_back_2(self->m_conditionalFormatting, &pendingFormatting, 1))
                    XConditionalFormatting_delete(pendingFormatting);
                pendingFormatting = NULL;
            } else if (inCell && element && XString_equals_utf8(element, "c", XChar_CaseSensitive)) {
                if (cellRow > 0 && cellColumn > 0) {
                    XCell* cell = getOrCreateCell(self, cellRow, cellColumn);
                    if (cell) {
                        XCell_setValue(cell, value);
                        if (XString_equals_utf8(cellType, "b", XChar_CaseSensitive)) cell->m_cellType = XCell_BooleanType;
                        else if (XString_equals_utf8(cellType, "e", XChar_CaseSensitive)) cell->m_cellType = XCell_ErrorType;
                        else if (XString_equals_utf8(cellType, "inlineStr", XChar_CaseSensitive)) cell->m_cellType = XCell_InlineStringType;
                        else if (XString_equals_utf8(cellType, "s", XChar_CaseSensitive)) cell->m_cellType = XCell_SharedStringType;
                        else if (XString_equals_utf8(cellType, "str", XChar_CaseSensitive)) cell->m_cellType = XCell_StringType;
                        else cell->m_cellType = XCell_NumberType;
                        cell->m_styleNumber = cellStyle;
                        if (cellStyle >= 0 && self->m_base.m_workbook)
                            cell->m_format = XStyles_xfFormat(self->m_base.m_workbook->m_styles, cellStyle);
                        if (XString_size_base(formulaText) > 0) {
                            XCellFormula* formula = XCellFormula_create_ex(formulaText);
                            XCell_setFormula(cell, formula);
                        }
                    }
                }
                inCell = false;
            }
        }
    }

    bool result = !XXmlStreamReader_hasError(reader);
    if (pendingValidation) XDataValidation_delete(pendingValidation);
    if (inConditionalRule) {
        if (pendingRule.m_formula1) XString_delete_base(pendingRule.m_formula1);
        if (pendingRule.m_formula2) XString_delete_base(pendingRule.m_formula2);
        if (pendingRule.m_formula3) XString_delete_base(pendingRule.m_formula3);
        if (pendingRule.m_text) XString_delete_base(pendingRule.m_text);
        if (pendingRule.m_timePeriod) XString_delete_base(pendingRule.m_timePeriod);
        if (pendingRule.m_format) XFormat_delete(pendingRule.m_format);
    }
    if (pendingFormatting) XConditionalFormatting_delete(pendingFormatting);
    if (cellType) XString_delete_base(cellType);
    XString_delete_base(value);
    XString_delete_base(formulaText);
    XXmlStreamReader_delete_base(reader);
    XByteArray_delete_base(xml);
    return result;
}

bool XWorksheet_loadFromXmlFile(XWorksheet* self, const XString* filePath)
{
    if (!self || !filePath) return false;
    XFile* file = XFile_create_2(filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        return false;
    }
    XByteArray* xml = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    bool result = xml && XWorksheet_loadFromXmlData(self, XByteArray_data(xml), XByteArray_size_base(xml));
    if (xml) XByteArray_delete_base(xml);
    return result;
}

/* ========== UTF-8 便捷变体 ========== */

bool XWorksheet_writeString_utf8(XWorksheet* self, int row, int column, const char* value, const XFormat* format)
{
    XString* s = value ? XString_create_utf8(value) : NULL;
    bool result = XWorksheet_writeString(self, row, column, s, format);
    if (s) XString_delete_base(s);
    return result;
}
