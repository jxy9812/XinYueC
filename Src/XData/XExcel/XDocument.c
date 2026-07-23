/******************************************************************************
 * @file       XDocument.c
 * @brief      XDocument 文档主类实现（对标 QXlsx::Document）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XDocument.h"
#include "XMemory.h"
#include "XString.h"
#include "XByteArray.h"
#include "XChartsheet.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ========== 辅助：获取当前工作表 ========== */
static XWorksheet* getCurrentWorksheet(XDocument* self)
{
    if (!self || !self->m_workbook) return NULL;
    XAbstractSheet* sheet = XWorkbook_activeSheet(self->m_workbook);
    if (!sheet || sheet->m_sheetType != XAbstractSheet_ST_WorkSheet) return NULL;
    return (XWorksheet*)sheet;
}

/* ========== 创建与初始化 ========== */

XDocument* XDocument_create(void)
{
    XDocument* self = (XDocument*)XMalloc_System(sizeof(XDocument));
    if (!self) return NULL;
    memset(self, 0, sizeof(XDocument));
    self->m_workbook = XWorkbook_create(XAbstractOOXmlFile_F_NewFromScratch);
    self->m_docPropsApp = XDocPropsApp_create(XAbstractOOXmlFile_F_NewFromScratch);
    self->m_docPropsCore = XDocPropsCore_create(XAbstractOOXmlFile_F_NewFromScratch);
    self->m_isLoaded = false;
    self->m_isModified = false;
    /* 默认添加一个工作表 */
    if (self->m_workbook) {
        XWorkbook_addSheet(self->m_workbook, "Sheet1", XAbstractSheet_ST_WorkSheet);
    }
    return self;
}

XDocument* XDocument_createFromFile(const char* xlsxName)
{
    if (!xlsxName) return NULL;
    XDocument* self = XDocument_create();
    if (!self) return NULL;
    self->m_filePath = XString_create();
    if (self->m_filePath) XString_append_utf8(self->m_filePath, xlsxName);
    /* 加载文件 - 暂未实现 */
    return self;
}

void XDocument_delete(XDocument* self)
{
    if (!self) return;
    if (self->m_workbook) XWorkbook_delete(self->m_workbook);
    if (self->m_docPropsApp) { XFree_System(self->m_docPropsApp); }
    if (self->m_docPropsCore) { XFree_System(self->m_docPropsCore); }
    if (self->m_filePath) { XString_deinit_base(self->m_filePath); XFree_System(self->m_filePath); }
    XFree_System(self);
}

/* ========== 单元格写入 ========== */

bool XDocument_write(XDocument* self, int row, int col, const XVariant* value, const XFormat* format)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return false;
    self->m_isModified = true;
    return XWorksheet_write(ws, row, col, value, format);
}

bool XDocument_writeRef(XDocument* self, const XCellReference* cell, const XVariant* value, const XFormat* format)
{
    if (!cell) return false;
    return XDocument_write(self, XCellReference_row(cell), XCellReference_column(cell), value, format);
}

/* ========== 单元格读取 ========== */

XVariant* XDocument_read(const XDocument* self, int row, int col)
{
    XWorksheet* ws = getCurrentWorksheet((XDocument*)self);
    if (!ws) return NULL;
    return XWorksheet_read(ws, row, col);
}

XVariant* XDocument_readRef(const XDocument* self, const XCellReference* cell)
{
    if (!cell) return NULL;
    return XDocument_read(self, XCellReference_row(cell), XCellReference_column(cell));
}

XCell* XDocument_cellAt(const XDocument* self, int row, int col)
{
    XWorksheet* ws = getCurrentWorksheet((XDocument*)self);
    if (!ws) return NULL;
    return XWorksheet_cellAt(ws, row, col);
}

XCell* XDocument_cellAtRef(const XDocument* self, const XCellReference* cell)
{
    if (!cell) return NULL;
    return XDocument_cellAt(self, XCellReference_row(cell), XCellReference_column(cell));
}

/* ========== 图片 ========== */
int XDocument_insertImage(XDocument* self, int row, int col, const char* imagePath)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return -1;
    self->m_isModified = true;
    return XWorksheet_insertImage(ws, row, col, imagePath);
}
bool XDocument_getImage(const XDocument* self, int imageIndex, XByteArray* imgData) { (void)self; (void)imageIndex; (void)imgData; return false; }
bool XDocument_getImageAt(const XDocument* self, int row, int col, XByteArray* imgData) { (void)self; (void)row; (void)col; (void)imgData; return false; }
uint XDocument_getImageCount(const XDocument* self) { return 0; }

/* ========== 图表 ========== */
XChart* XDocument_insertChart(XDocument* self, int row, int col, int width, int height)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return NULL;
    self->m_isModified = true;
    return XWorksheet_insertChart(ws, row, col, width, height);
}

/* ========== 合并单元格 ========== */
bool XDocument_mergeCells(XDocument* self, int firstRow, int firstCol, int lastRow, int lastCol, const XFormat* format)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return false;
    self->m_isModified = true;
    return XWorksheet_mergeCells(ws, firstRow, firstCol, lastRow, lastCol, format);
}
bool XDocument_unmergeCells(XDocument* self, int firstRow, int firstCol, int lastRow, int lastCol)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return false;
    self->m_isModified = true;
    return XWorksheet_unmergeCells(ws, firstRow, firstCol, lastRow, lastCol);
}

/* ========== 列操作 ========== */
bool XDocument_setColumnWidth(XDocument* self, int colFirst, int colLast, double width)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return false;
    self->m_isModified = true;
    return XWorksheet_setColumnWidth(ws, colFirst, colLast, width);
}
bool XDocument_setColumnFormat(XDocument* self, int colFirst, int colLast, const XFormat* format)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return false;
    self->m_isModified = true;
    return XWorksheet_setColumnFormat(ws, colFirst, colLast, format);
}
bool XDocument_setColumnHidden(XDocument* self, int colFirst, int colLast, bool hidden)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return false;
    self->m_isModified = true;
    return XWorksheet_setColumnHidden(ws, colFirst, colLast, hidden);
}
double XDocument_columnWidth(const XDocument* self, int column)
{
    XWorksheet* ws = getCurrentWorksheet((XDocument*)self);
    return ws ? XWorksheet_columnWidth(ws, column) : -1.0;
}
XFormat* XDocument_columnFormat(const XDocument* self, int column)
{
    XWorksheet* ws = getCurrentWorksheet((XDocument*)self);
    return ws ? XWorksheet_columnFormat(ws, column) : NULL;
}
bool XDocument_isColumnHidden(const XDocument* self, int column)
{
    XWorksheet* ws = getCurrentWorksheet((XDocument*)self);
    return ws ? XWorksheet_isColumnHidden(ws, column) : false;
}

/* ========== 行操作 ========== */
bool XDocument_setRowHeight(XDocument* self, int rowFirst, int rowLast, double height)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return false;
    self->m_isModified = true;
    return XWorksheet_setRowHeight(ws, rowFirst, rowLast, height);
}
bool XDocument_setRowFormat(XDocument* self, int rowFirst, int rowLast, const XFormat* format)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return false;
    self->m_isModified = true;
    return XWorksheet_setRowFormat(ws, rowFirst, rowLast, format);
}
bool XDocument_setRowHidden(XDocument* self, int rowFirst, int rowLast, bool hidden)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return false;
    self->m_isModified = true;
    return XWorksheet_setRowHidden(ws, rowFirst, rowLast, hidden);
}
double XDocument_rowHeight(const XDocument* self, int row)
{
    XWorksheet* ws = getCurrentWorksheet((XDocument*)self);
    return ws ? XWorksheet_rowHeight(ws, row) : -1.0;
}
XFormat* XDocument_rowFormat(const XDocument* self, int row)
{
    XWorksheet* ws = getCurrentWorksheet((XDocument*)self);
    return ws ? XWorksheet_rowFormat(ws, row) : NULL;
}
bool XDocument_isRowHidden(const XDocument* self, int row)
{
    XWorksheet* ws = getCurrentWorksheet((XDocument*)self);
    return ws ? XWorksheet_isRowHidden(ws, row) : false;
}

/* ========== 分组 ========== */
bool XDocument_groupRows(XDocument* self, int rowFirst, int rowLast, bool collapsed)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return false;
    return XWorksheet_groupRows(ws, rowFirst, rowLast, collapsed);
}
bool XDocument_groupColumns(XDocument* self, int colFirst, int colLast, bool collapsed)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return false;
    return XWorksheet_groupColumns(ws, colFirst, colLast, collapsed);
}

/* ========== 数据验证 ========== */
bool XDocument_addDataValidation(XDocument* self, XDataValidation* validation)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return false;
    return XWorksheet_addDataValidation(ws, validation);
}
bool XDocument_addConditionalFormatting(XDocument* self, XConditionalFormatting* cf)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return false;
    return XWorksheet_addConditionalFormatting(ws, cf);
}

/* ========== 定义名称 ========== */
bool XDocument_defineName(XDocument* self, const char* name, const char* formula, const char* comment, const char* scope)
{
    return self ? XWorkbook_defineName(self->m_workbook, name, formula, comment, scope) : false;
}

/* ========== 维度 ========== */
XCellRange XDocument_dimension(const XDocument* self)
{
    XCellRange r; XCellRange_init(&r);
    XWorksheet* ws = getCurrentWorksheet((XDocument*)self);
    if (ws) r = XWorksheet_dimension(ws);
    return r;
}

/* ========== 文档属性 ========== */
const char* XDocument_documentProperty(const XDocument* self, const char* name) {
    if (!self || !name) return "";
    return XDocPropsCore_property(self->m_docPropsCore, name);
}
void XDocument_setDocumentProperty(XDocument* self, const char* name, const char* property) {
    if (!self || !name || !property) return;
    XDocPropsCore_setProperty(self->m_docPropsCore, name, property);
}
int XDocument_documentPropertyNames(const XDocument* self, XString*** names) {
    if (!self || !names) return 0;
    return XDocPropsCore_propertyNames(self->m_docPropsCore, names);
}

/* ========== 工作表管理 ========== */
int XDocument_sheetNames(const XDocument* self, XString*** names) {
    if (!self || !self->m_workbook || !names) return 0;
    return XWorkbook_worksheetNames(self->m_workbook, (int*)names) ? 0 : 0; /* 简化 */
}
bool XDocument_addSheet(XDocument* self, const char* name, XAbstractSheet_SheetType type) {
    return self ? XWorkbook_addSheet(self->m_workbook, name, type) != NULL : false;
}
bool XDocument_insertSheet(XDocument* self, int index, const char* name, XAbstractSheet_SheetType type) {
    return self ? XWorkbook_insertSheet(self->m_workbook, index, name, type) != NULL : false;
}
bool XDocument_selectSheet(XDocument* self, const char* name) {
    if (!self || !self->m_workbook || !name) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && strcmp(XString_toUtf8_const(s->m_sheetName), name) == 0)
            return XWorkbook_setActiveSheet(self->m_workbook, (int)i);
    }
    return false;
}
bool XDocument_selectSheetByIndex(XDocument* self, int index) {
    return self ? XWorkbook_setActiveSheet(self->m_workbook, index) : false;
}
bool XDocument_renameSheet(XDocument* self, const char* oldName, const char* newName) {
    if (!self || !self->m_workbook || !oldName || !newName) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && strcmp(XString_toUtf8_const(s->m_sheetName), oldName) == 0)
            return XWorkbook_renameSheet(self->m_workbook, (int)i, newName);
    }
    return false;
}
bool XDocument_copySheet(XDocument* self, const char* srcName, const char* distName) {
    if (!self || !self->m_workbook || !srcName) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && strcmp(XString_toUtf8_const(s->m_sheetName), srcName) == 0)
            return XWorkbook_copySheet(self->m_workbook, (int)i, distName);
    }
    return false;
}
bool XDocument_moveSheet(XDocument* self, const char* srcName, int distIndex) {
    if (!self || !self->m_workbook || !srcName) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && strcmp(XString_toUtf8_const(s->m_sheetName), srcName) == 0)
            return XWorkbook_moveSheet(self->m_workbook, (int)i, distIndex);
    }
    return false;
}
bool XDocument_deleteSheet(XDocument* self, const char* name) {
    if (!self || !self->m_workbook || !name) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && strcmp(XString_toUtf8_const(s->m_sheetName), name) == 0)
            return XWorkbook_deleteSheet(self->m_workbook, (int)i);
    }
    return false;
}

/* ========== 工作簿访问 ========== */
XWorkbook* XDocument_workbook(const XDocument* self) { return self ? self->m_workbook : NULL; }
XAbstractSheet* XDocument_sheet(const XDocument* self, const char* sheetName) {
    if (!self || !self->m_workbook || !sheetName) return NULL;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && strcmp(XString_toUtf8_const(s->m_sheetName), sheetName) == 0) return s;
    }
    return NULL;
}
XAbstractSheet* XDocument_currentSheet(const XDocument* self) {
    return self ? XWorkbook_activeSheet(self->m_workbook) : NULL;
}
XWorksheet* XDocument_currentWorksheet(const XDocument* self) {
    return getCurrentWorksheet((XDocument*)self);
}

/* ========== 保存和加载 ========== */
bool XDocument_save(const XDocument* self) { (void)self; return false; }
bool XDocument_saveAs(const XDocument* self, const char* xlsxName) { (void)self; (void)xlsxName; return false; }
bool XDocument_isLoadPackage(const XDocument* self) { return self ? self->m_isLoaded : false; }
bool XDocument_load(XDocument* self) { if (!self) return false; self->m_isLoaded = true; return false; }

/* ========== SAX 读取 ========== */
bool XDocument_readSheetSax(XDocument* self, const char* sheetName, const XReadSax_Options* opt, XReadSax_CellCallback onCell, void* userData) { (void)self; (void)sheetName; (void)opt; (void)onCell; (void)userData; return false; }
bool XDocument_readSheetSaxByIndex(XDocument* self, int sheetIndex, const XReadSax_Options* opt, XReadSax_CellCallback onCell, void* userData) { (void)self; (void)sheetIndex; (void)opt; (void)onCell; (void)userData; return false; }

/* ========== 自动列宽 ========== */
bool XDocument_autosizeColumnWidth(XDocument* self, int colFirst, int colLast) { (void)self; (void)colFirst; (void)colLast; return false; }
bool XDocument_autosizeColumnWidthAll(XDocument* self) { (void)self; return false; }
