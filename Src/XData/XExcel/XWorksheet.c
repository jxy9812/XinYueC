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
#include "XFile.h"
#include <string.h>

#include <math.h>


/* 常量定义 */
#define XLSX_ROW_MAX 1048576
#define XLSX_COLUMN_MAX 16384

/* 行号与列号编码为 key */
static uint64_t cellKey(int row, int col) { return ((uint64_t)(uint32_t)row << 32) | (uint32_t)col; }

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
            XCell* srcCell = *(XCell**)XPair_second(pair);
            if (srcCell) {
                XCell* dstCell = XCell_copy(srcCell);
                if (dstCell) {
                    uint64_t key = cellKey(dstCell->m_row, dstCell->m_column);
                    XMap_insert_base((XMapBase*)ws->m_cellTable, &key, &dstCell);
                }
            }
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
            XCell* cell = *(XCell**)XPair_second(pair);
            if (cell) XCell_delete(cell);
        }
        XMap_deinit_base(self->m_cellTable); XFree_System(self->m_cellTable);
    }
    /* 释放列信息 */
    if (self->m_colInfoMap) {
        XMap_iterator it = XMap_begin(self->m_colInfoMap);
        XMap_iterator end = XMap_end(self->m_colInfoMap);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_colInfoMap, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            XWorksheet_ColumnInfo* ci = *(XWorksheet_ColumnInfo**)XPair_second(pair);
            if (ci) XFree_System(ci);
        }
        XMap_deinit_base(self->m_colInfoMap); XFree_System(self->m_colInfoMap);
    }
    /* 释放行信息 */
    if (self->m_rowInfoMap) {
        XMap_iterator it = XMap_begin(self->m_rowInfoMap);
        XMap_iterator end = XMap_end(self->m_rowInfoMap);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_rowInfoMap, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            XWorksheet_RowInfo* ri = *(XWorksheet_RowInfo**)XPair_second(pair);
            if (ri) XFree_System(ri);
        }
        XMap_deinit_base(self->m_rowInfoMap); XFree_System(self->m_rowInfoMap);
    }
    /* 释放超链接 */
    if (self->m_hyperlinks) {
        for (size_t i = 0; i < XVector_size_base((XContainer*)self->m_hyperlinks); ++i) {
            XWorksheet_Hyperlink* hl = (XWorksheet_Hyperlink*)XVector_at_base(self->m_hyperlinks, i);
            if (hl->m_url) { XString_deinit_base(hl->m_url); XFree_System(hl->m_url); }
            if (hl->m_display) { XString_deinit_base(hl->m_display); XFree_System(hl->m_display); }
            if (hl->m_tip) { XString_deinit_base(hl->m_tip); XFree_System(hl->m_tip); }
        }
        XVector_deinit_base(self->m_hyperlinks); XFree_System(self->m_hyperlinks);
    }
    if (self->m_mergedCells) { XVector_deinit_base(self->m_mergedCells); XFree_System(self->m_mergedCells); }
    /* 释放数据验证 */
    if (self->m_dataValidations) {
        for (size_t i = 0; i < XVector_size_base((XContainer*)self->m_dataValidations); ++i) {
            XDataValidation* dv = *(XDataValidation**)XVector_at_base(self->m_dataValidations, i);
            if (dv) XDataValidation_delete(dv);
        }
        XVector_deinit_base(self->m_dataValidations); XFree_System(self->m_dataValidations);
    }
    /* 释放条件格式 */
    if (self->m_conditionalFormatting) {
        for (size_t i = 0; i < XVector_size_base((XContainer*)self->m_conditionalFormatting); ++i) {
            XConditionalFormatting* cf = *(XConditionalFormatting**)XVector_at_base(self->m_conditionalFormatting, i);
            if (cf) XConditionalFormatting_delete(cf);
        }
        XVector_deinit_base(self->m_conditionalFormatting); XFree_System(self->m_conditionalFormatting);
    }
    /* 释放媒体文件 */
    if (self->m_mediaFiles) {
        for (size_t i = 0; i < XVector_size_base((XContainer*)self->m_mediaFiles); ++i) {
            XMediaFile* mf = *(XMediaFile**)XVector_at_base(self->m_mediaFiles, i);
            if (mf) XMediaFile_delete(mf);
        }
        XVector_deinit_base(self->m_mediaFiles); XFree_System(self->m_mediaFiles);
    }
    /* 释放图表文件 */
    if (self->m_chartFiles) {
        for (size_t i = 0; i < XVector_size_base((XContainer*)self->m_chartFiles); ++i) {
            XChart* ch = *(XChart**)XVector_at_base(self->m_chartFiles, i);
            if (ch) XChart_delete(ch);
        }
        XVector_deinit_base(self->m_chartFiles); XFree_System(self->m_chartFiles);
    }
    if (self->m_rowSpans) { XMap_deinit_base(self->m_rowSpans); XFree_System(self->m_rowSpans); }
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
        return *(XCell**)XPair_second(pair);
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
    if (format) cell->m_format = (XFormat*)format;
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
    if (format) cell->m_format = (XFormat*)format;
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
    if (format) cell->m_format = (XFormat*)format;
    const XString* plain = XRichString_toPlainString(value);
    if (plain) {
        XCell_setValue(cell, plain);
        cell->m_cellType = XCell_SharedStringType;
    }
    /* 保存富文本 */
    XRichString* rs = XRichString_create();
    if (rs) {
        XRichString_copy(rs, value);
        cell->m_richString = rs;
    }
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
    if (format) cell->m_format = (XFormat*)format;
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
    if (format) cell->m_format = (XFormat*)format;
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
    if (format) cell->m_format = (XFormat*)format;
    cell->m_formula = XCellFormula_copy(formula);
    if (cell->m_formula) {
        XString* resStr = XString_create_fmt_utf8("%.15g", result);
        XCell_setValue(cell, resStr);
        if (resStr) XString_delete_base(resStr);
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
    if (format) cell->m_format = (XFormat*)format;
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
    if (format) cell->m_format = (XFormat*)format;
    /* Excel 日期序列号：从1900-01-01开始 */
    double excelSerial = (double)timestampMs / 86400000.0 + 25569.0; /* 1970-01-01到1900-01-01的偏移 */
    if (excelSerial < 60.0) excelSerial -= 1.0; /* Excel 1900年闰年bug */
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
    if (format) cell->m_format = (XFormat*)format;
    /* 日期序列号：从1900-01-01开始 */
    int y = year, m = month, d = day;
    if (m <= 2) { y--; m += 12; }
    double excelSerial = (double)(365*y + y/4 - y/100 + y/400 + (153*m - 457)/5 + d - 1461) - 1.0;
    if (excelSerial < 60.0) excelSerial -= 1.0; /* Excel 1900年闰年bug */
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
    if (format) cell->m_format = (XFormat*)format;
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
    if (!self) return false;
    XCell* cell = getOrCreateCell(self, row, column);
    if (!cell) return false;
    if (format) cell->m_format = (XFormat*)format;
    if (display) { XCell_setValue(cell, display); cell->m_cellType = XCell_SharedStringType; }
    /* 添加超链接 */
    XWorksheet_Hyperlink hl;
    memset(&hl, 0, sizeof(hl));
    XCellRange_setCell(&hl.m_range, row, column);
    hl.m_url = url ? XString_create_copy(url) : XString_create();
    if (display) { hl.m_display = XString_create_copy(display); }
    if (tip) { hl.m_tip = XString_create_copy(tip); }
    XVector_push_back_2(self->m_hyperlinks, &hl, 1);
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
        return *(XCell**)XPair_second(pair);
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

bool XWorksheet_addDataValidation(XWorksheet* self, XDataValidation* validation)
{
    if (!self || !validation) return false;
    XVector_push_back_2(self->m_dataValidations, &validation, 1);
    return true;
}

bool XWorksheet_addConditionalFormatting(XWorksheet* self, XConditionalFormatting* cf)
{
    if (!self || !cf) return false;
    XVector_push_back_2(self->m_conditionalFormatting, &cf, 1);
    return true;
}

/* ========== 图片与图表 ========== */

int XWorksheet_insertImage(XWorksheet* self, int row, int column, const XString* imagePath)
{
    if (!self || !imagePath) return -1;
    XMediaFile* media = XMediaFile_create(imagePath);
    if (!media) return -1;
    XMediaFile_setFileName(media, imagePath);
    XVector_push_back_2(self->m_mediaFiles, &media, 1);
    return (int)XVector_size_base((XContainer*)self->m_mediaFiles) - 1;
}

bool XWorksheet_getImage(XWorksheet* self, int imageIndex, XByteArray* imgData) { (void)self; (void)imageIndex; (void)imgData; return false; }
bool XWorksheet_getImageAt(XWorksheet* self, int row, int column, XByteArray* imgData) { (void)self; (void)row; (void)column; (void)imgData; return false; }
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
    if (!self) return false;
    XCellRange range;
    XCellRange_setFullRange(&range, firstRow, firstCol, lastRow, lastCol);
    XVector_push_back_2(self->m_mergedCells, &range, 1);
    (void)format;
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
    if (!self || col < 1) return NULL;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_colInfoMap, &col, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return *(XWorksheet_ColumnInfo**)XPair_second(pair);
    }
    XWorksheet_ColumnInfo* ci = (XWorksheet_ColumnInfo*)XMalloc_System(sizeof(XWorksheet_ColumnInfo));
    if (!ci) return NULL;
    memset(ci, 0, sizeof(XWorksheet_ColumnInfo));
    ci->m_width = -1.0;
    XMap_insert_base((XMapBase*)self->m_colInfoMap, &col, &ci);
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
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_colInfoMap, &column, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return (*(XWorksheet_ColumnInfo**)XPair_second(pair))->m_width;
    }
    return -1.0;
}

XFormat* XWorksheet_columnFormat(const XWorksheet* self, int column)
{
    if (!self) return NULL;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_colInfoMap, &column, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return (*(XWorksheet_ColumnInfo**)XPair_second(pair))->m_format;
    }
    return NULL;
}

bool XWorksheet_isColumnHidden(const XWorksheet* self, int column)
{
    if (!self) return false;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_colInfoMap, &column, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return (*(XWorksheet_ColumnInfo**)XPair_second(pair))->m_hidden;
    }
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
    if (!self || row < 1) return NULL;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_rowInfoMap, &row, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return *(XWorksheet_RowInfo**)XPair_second(pair);
    }
    XWorksheet_RowInfo* ri = (XWorksheet_RowInfo*)XMalloc_System(sizeof(XWorksheet_RowInfo));
    if (!ri) return NULL;
    memset(ri, 0, sizeof(XWorksheet_RowInfo));
    ri->m_height = -1.0;
    XMap_insert_base((XMapBase*)self->m_rowInfoMap, &row, &ri);
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
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_rowInfoMap, &row, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return (*(XWorksheet_RowInfo**)XPair_second(pair))->m_height;
    }
    return -1.0;
}

XFormat* XWorksheet_rowFormat(const XWorksheet* self, int row)
{
    if (!self) return NULL;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_rowInfoMap, &row, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return (*(XWorksheet_RowInfo**)XPair_second(pair))->m_format;
    }
    return NULL;
}

bool XWorksheet_isRowHidden(const XWorksheet* self, int row)
{
    if (!self) return false;
    XMap_iterator it;
    if (XMap_find_base((XMapBase*)self->m_rowInfoMap, &row, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        return (*(XWorksheet_RowInfo**)XPair_second(pair))->m_hidden;
    }
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
    int count = (int)XMap_size_base((XContainer*)self->m_cellTable);
    *locations = (XCellLocation*)XMalloc_System(sizeof(XCellLocation) * (size_t)count);
    if (!*locations) return 0;
    int idx = 0;
    XMap_iterator it = XMap_begin(self->m_cellTable);
    XMap_iterator end = XMap_end(self->m_cellTable);
    int mr = 0, mc = 0;
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_cellTable, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        XCell* cell = *(XCell**)XPair_second(pair);
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

bool XWorksheet_saveToXmlData(const XWorksheet* self, uint8_t** outData, size_t* outLen)
{
    if (!self || !outData || !outLen) return false;
    *outData = NULL;
    *outLen = 0;
    
    XByteArray* buf = XByteArray_create();
    if (!buf) return false;
    
    XByteArray_append_utf8(buf, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    XByteArray_append_utf8(buf, "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">\n");
    
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
    XByteArray_append_utf8(buf, "  <sheetViews>\n");
    XByteArray_append_utf8(buf, "    <sheetView workbookViewId=\"0\">\n");
    XByteArray_append_utf8(buf, "    </sheetView>\n");
    XByteArray_append_utf8(buf, "  </sheetViews>\n");
    
    /* 写入 sheetFormatPr */
    XByteArray_append_utf8(buf, "  <sheetFormatPr defaultRowHeight=\"15\"/>\n");
    
    /* 写入 cols */
    if (self->m_colInfo && XVector_size_base((const XContainer*)self->m_colInfo) > 0) {
        XByteArray_append_utf8(buf, "  <cols>\n");
        size_t colCount = XVector_size_base((const XContainer*)self->m_colInfo);
        for (size_t i = 0; i < colCount; i++) {
            XWorksheet_ColumnInfo* colInfo = (XWorksheet_ColumnInfo*)XVector_at_base(self->m_colInfo, i);
            if (colInfo) {
                char colEntry[256];
                snprintf(colEntry, sizeof(colEntry),
                    "    <col min=\"%d\" max=\"%d\" width=\"%.2f\" hidden=\"%d\"/>\n",
                    (int)i + 1, (int)i + 1, colInfo->m_width, colInfo->m_hidden ? 1 : 0);
                XByteArray_append_utf8(buf, colEntry);
            }
        }
        XByteArray_append_utf8(buf, "  </cols>\n");
    }
    
    /* 写入 sheetData - 最关键的部分 */
    XByteArray_append_utf8(buf, "  <sheetData>\n");
    
    if (self->m_cellTable && XMap_size_base((const XContainer*)self->m_cellTable) > 0) {
        /* 遍历所有单元格 */
        XMap_iterator it = XMap_begin(self->m_cellTable);
        XMap_iterator end = XMap_end(self->m_cellTable);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_cellTable, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            if (!pair) continue;
            
            /* 从 key 获取行列号 */
            intptr_t key = *(intptr_t*)XPair_first(pair);
            int row = (key >> 16) & 0xFFFF;
            int col = key & 0xFFFF;
            
            /* 获取单元格指针 */
            XCell** cellPtr = (XCell**)XPair_second(pair);
            if (!cellPtr || !*cellPtr) continue;
            XCell* cell = *cellPtr;
            
            /* 生成单元格引用如 "A1" */
            char refBuf[32];
            char colBuf[16];
            colNumToLetters(col, colBuf, sizeof(colBuf));
            snprintf(refBuf, sizeof(refBuf), "%s%d", colBuf, row);
            
            /* 生成单元格 XML */
            char cellEntry[2048];
            const char* typeStr = cellTypeToStr(cell->m_cellType);
            int styleIdx = cell->m_styleNumber;
            
            /* 获取值 */
            const char* value = cell->m_value ? XString_toUtf8(cell->m_value) : "";
            
            if (cell->m_formula && XCellFormula_isValid(cell->m_formula)) {
                /* 公式单元格 */
                const char* formulaText = XCellFormula_formulaText(cell->m_formula);
                snprintf(cellEntry, sizeof(cellEntry),
                    "    <c r=\"%s\" s=\"%d\" t=\"%s\"><f>%s</f><v>%s</v></c>\n",
                    refBuf, styleIdx, typeStr, formulaText ? formulaText : "", value);
            } else if (cell->m_richString && XRichString_isRichString(cell->m_richString)) {
                /* 富文本字符串 - 使用 inlineStr */
                snprintf(cellEntry, sizeof(cellEntry), "    <c r=\"%s\" s=\"%d\" t=\"inlineStr\"><is><t></t></is></c>\n",
                    refBuf, styleIdx);
            } else if (cell->m_cellType == XCell_SharedStringType) {
                /* 共享字符串 */
                snprintf(cellEntry, sizeof(cellEntry), "    <c r=\"%s\" s=\"%d\" t=\"%s\"><v>%s</v></c>\n",
                    refBuf, styleIdx, typeStr, value);
            } else if (cell->m_cellType == XCell_NumberType) {
                /* 数字 */
                snprintf(cellEntry, sizeof(cellEntry), "    <c r=\"%s\" s=\"%d\"><v>%s</v></c>\n",
                    refBuf, styleIdx, value);
            } else if (cell->m_cellType == XCell_StringType) {
                /* 普通字符串 */
                snprintf(cellEntry, sizeof(cellEntry), "    <c r=\"%s\" s=\"%d\" t=\"%s\"><v>%s</v></c>\n",
                    refBuf, styleIdx, typeStr, value);
            } else if (cell->m_cellType == XCell_BooleanType) {
                /* 布尔值 */
                snprintf(cellEntry, sizeof(cellEntry), "    <c r=\"%s\" s=\"%d\" t=\"%s\"><v>%s</v></c>\n",
                    refBuf, styleIdx, typeStr, value);
            } else {
                /* 其他类型 */
                snprintf(cellEntry, sizeof(cellEntry), "    <c r=\"%s\" s=\"%d\" t=\"%s\"><v>%s</v></c>\n",
                    refBuf, styleIdx, typeStr, value);
            }
            
            XByteArray_append_utf8(buf, cellEntry);
        }
    }
    
    XByteArray_append_utf8(buf, "  </sheetData>\n");
    
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
    
    /* 写入 pageMargins */
    XByteArray_append_utf8(buf, "  <pageMargins left=\"0.75\" right=\"0.75\" top=\"1\" bottom=\"1\" header=\"0.5\" footer=\"0.5\"/>\n");
    
    XByteArray_append_utf8(buf, "</worksheet>\n");
    
    *outData = (uint8_t*)XMalloc_System(XByteArray_size_base((const XContainer*)buf) + 1);
    if (*outData) {
        memcpy(*outData, XByteArray_data(buf), XByteArray_size_base((const XContainer*)buf));
        *outLen = XByteArray_size_base((const XContainer*)buf);
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
        if (file) XFile_deleteLater(file);
        XFree_System(data);
        return false;
    }
    XIODevice_write_1((XIODevice*)file, data, (int64_t)len);
    XIODevice_close_base((XIODevice*)file);
    XFile_deleteLater(file);
    XFree_System(data);
    return true;
}

bool XWorksheet_loadFromXmlData(XWorksheet* self, const uint8_t* data, size_t len)
{
    (void)self; (void)data; (void)len;
    /* TODO: 实现 XML 解析 */
    return false;
}

bool XWorksheet_loadFromXmlFile(XWorksheet* self, const XString* filePath)
{
    (void)self; (void)filePath;
    /* TODO: 实现 XML 文件解析 */
    return false;
}
