/******************************************************************************
 * @file       XDocument.c
 * @brief      XDocument 文档主类实现（对标 QXlsx::Document）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XDocument.h"
#include "XMemory.h"
#include "XString.h"
#include "XByteArray.h"
#include "XZipWriter.h"
#include "XContentTypes.h"
#include "XStyles.h"
#include "XSharedStrings.h"
#include "XRelationships.h"
#include "XZipReader.h"
#include "XReadSax.h"
#include "XVector.h"
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
    if (self->m_docPropsApp) { XDocPropsApp_delete(self->m_docPropsApp); }
    if (self->m_docPropsCore) { XDocPropsCore_delete(self->m_docPropsCore); }
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
unsigned int XDocument_getImageCount(const XDocument* self) { return 0; }

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
    if (!self->m_docPropsCore) { *names = NULL; return 0; }
    return XDocPropsCore_propertyNames(self->m_docPropsCore, names);
}

/* ========== 工作表管理 ========== */
int XDocument_sheetNames(const XDocument* self, XString*** names) {
    if (!self || !self->m_workbook || !names) return 0;
    int count = 0;
    XString** arr = (XString**)XMalloc_System(sizeof(XString*) * (size_t)XWorkbook_sheetCount(self->m_workbook));
    if (!arr) return 0;
    for (int i = 0; i < XWorkbook_sheetCount(self->m_workbook); ++i) {
        XAbstractSheet* s = XWorkbook_sheet(self->m_workbook, i);
        if (s && s->m_sheetName) {
            arr[count] = XString_create();
            if (arr[count]) {
                XString_append_utf8(arr[count], XString_toUtf8(s->m_sheetName));
                count++;
            }
        }
    }
    *names = arr;
    return count;
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
        if (s->m_sheetName && strcmp(XString_toUtf8(s->m_sheetName), name) == 0)
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
        if (s->m_sheetName && strcmp(XString_toUtf8(s->m_sheetName), oldName) == 0)
            return XWorkbook_renameSheet(self->m_workbook, (int)i, newName);
    }
    return false;
}
bool XDocument_copySheet(XDocument* self, const char* srcName, const char* distName) {
    if (!self || !self->m_workbook || !srcName) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && strcmp(XString_toUtf8(s->m_sheetName), srcName) == 0)
            return XWorkbook_copySheet(self->m_workbook, (int)i, distName);
    }
    return false;
}
bool XDocument_moveSheet(XDocument* self, const char* srcName, int distIndex) {
    if (!self || !self->m_workbook || !srcName) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && strcmp(XString_toUtf8(s->m_sheetName), srcName) == 0)
            return XWorkbook_moveSheet(self->m_workbook, (int)i, distIndex);
    }
    return false;
}
bool XDocument_deleteSheet(XDocument* self, const char* name) {
    if (!self || !self->m_workbook || !name) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && strcmp(XString_toUtf8(s->m_sheetName), name) == 0)
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
        if (s->m_sheetName && strcmp(XString_toUtf8(s->m_sheetName), sheetName) == 0) return s;
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
/* ========== 保存和加载 ========== */

bool XDocument_save(const XDocument* self) {
    if (!self) return false;
    const char* path = self->m_filePath ? XString_toUtf8(self->m_filePath) : NULL;
    if (!path || strlen(path) == 0) return false;
    return XDocument_saveAs(self, path);
}

bool XDocument_saveAs(const XDocument* self, const char* xlsxName) {
    if (!self || !xlsxName || !self->m_workbook) return false;
    
    XZipWriter* zip = XZipWriter_create(xlsxName);
    if (!zip) return false;
    
    /* 获取工作簿和样式 */
    XWorkbook* wb = self->m_workbook;
    XStyles* styles = wb->m_styles;
    XSharedStrings* sharedStrings = wb->m_sharedStrings;
    
    /* ========== 创建 ContentTypes ========== */
    XContentTypes* contentTypes = XContentTypes_create();
    
    /* 添加默认类型 */
    XContentTypes_addDefault(contentTypes, "rels", "application/vnd.openxmlformats-package.relationships+xml");
    XContentTypes_addDefault(contentTypes, "xml", "application/xml");
    
    /* 添加工作表 */
    int sheetCount = XWorkbook_sheetCount(wb);
    for (int i = 0; i < sheetCount; i++) {
        XContentTypes_addWorksheetName(contentTypes, NULL);
    }
    
    /* 添加覆盖类型 */
    XContentTypes_addOverride(contentTypes, "/docProps/core.xml", "application/vnd.openxmlformats-package.core-properties+xml");
    XContentTypes_addOverride(contentTypes, "/docProps/app.xml", "application/vnd.openxmlformats-officedocument.extended-properties+xml");
    XContentTypes_addOverride(contentTypes, "/xl/workbook.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");
    XContentTypes_addOverride(contentTypes, "/xl/styles.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml");
    XContentTypes_addOverride(contentTypes, "/xl/theme/theme1.xml", "application/vnd.openxmlformats-officedocument.theme+xml");
    
    /* 添加共享字符串 */
    if (!XSharedStrings_isEmpty(sharedStrings)) {
        XContentTypes_addOverride(contentTypes, "/xl/sharedStrings.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml");
    }
    
    /* 写入 [Content_Types].xml */
    uint8_t* ctData = NULL;
    size_t ctLen = 0;
    if (XContentTypes_saveToXmlData(contentTypes, &ctData, &ctLen)) {
        XZipWriter_addFile(zip, "[Content_Types].xml", ctData, ctLen);
        XFree_System(ctData);
    }
    XContentTypes_delete(contentTypes);
    
    /* ========== 创建根目录 .rels ========== */
    XRelationships rootRels;
    /* 初始化 */
    memset(&rootRels, 0, sizeof(rootRels));
    rootRels.m_relationships = XVector_Create(XlsxRelationship);
    
    XRelationships_addDocumentRelationship(&rootRels, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument", "xl/workbook.xml");
    XRelationships_addPackageRelationship(&rootRels, "http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties", "docProps/core.xml");
    XRelationships_addDocumentRelationship(&rootRels, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties", "docProps/app.xml");
    
    uint8_t* relData = NULL;
    size_t relLen = 0;
    if (XRelationships_saveToXmlData(&rootRels, &relData, &relLen)) {
        XZipWriter_addFile(zip, "_rels/.rels", relData, relLen);
        XFree_System(relData);
    }
    /* 手动清理，不释放栈变量 */ XRelationships_clear(&rootRels); if (rootRels.m_relationships) { XVector_delete_base(rootRels.m_relationships); rootRels.m_relationships = NULL; }
    
    /* ========== 创建 xl/_rels/workbook.xml.rels ========== */
    XRelationships workbookRels;
    memset(&workbookRels, 0, sizeof(workbookRels));
    workbookRels.m_relationships = XVector_Create(XlsxRelationship);
    
    XRelationships_addDocumentRelationship(&workbookRels, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles", "styles.xml");
    if (!XSharedStrings_isEmpty(sharedStrings)) {
        XRelationships_addDocumentRelationship(&workbookRels, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings", "sharedStrings.xml");
    }
    XRelationships_addDocumentRelationship(&workbookRels, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme", "theme/theme1.xml");
    
    /* 添加工作表关系 */
    for (int i = 0; i < sheetCount; i++) {
        char sheetPath[64];
        snprintf(sheetPath, sizeof(sheetPath), "worksheets/sheet%d.xml", i + 1);
        XRelationships_addDocumentRelationship(&workbookRels, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet", sheetPath);
    }
    
    relData = NULL;
    relLen = 0;
    if (XRelationships_saveToXmlData(&workbookRels, &relData, &relLen)) {
        XZipWriter_addFile(zip, "xl/_rels/workbook.xml.rels", relData, relLen);
        XFree_System(relData);
    }
    /* 手动清理，不释放栈变量 */ XRelationships_clear(&workbookRels); if (workbookRels.m_relationships) { XVector_delete_base(workbookRels.m_relationships); workbookRels.m_relationships = NULL; }
    
    /* ========== 写入 xl/workbook.xml ========== */
    uint8_t* wbData = NULL;
    size_t wbLen = 0;
    if (XWorkbook_saveToXmlData(wb, &wbData, &wbLen)) {
        XZipWriter_addFile(zip, "xl/workbook.xml", wbData, wbLen);
        XFree_System(wbData);
    }
    
    /* ========== 写入 xl/styles.xml ========== */
    uint8_t* styleData = NULL;
    size_t styleLen = 0;
    if (XStyles_saveToXmlData(styles, &styleData, &styleLen)) {
        XZipWriter_addFile(zip, "xl/styles.xml", styleData, styleLen);
        XFree_System(styleData);
    }
    
    /* ========== 写入 xl/sharedStrings.xml ========== */
    if (!XSharedStrings_isEmpty(sharedStrings)) {
        uint8_t* ssData = NULL;
        size_t ssLen = 0;
        if (XSharedStrings_saveToXmlData(sharedStrings, &ssData, &ssLen)) {
            XZipWriter_addFile(zip, "xl/sharedStrings.xml", ssData, ssLen);
            XFree_System(ssData);
        }
    }
    
    /* ========== 写入工作表 xl/worksheets/sheetN.xml ========== */
    for (int i = 0; i < sheetCount; i++) {
        XAbstractSheet* sheet = XWorkbook_sheet(wb, i);
        if (!sheet || sheet->m_sheetType != XAbstractSheet_ST_WorkSheet) continue;
        
        XWorksheet* ws = (XWorksheet*)sheet;
        uint8_t* wsData = NULL;
        size_t wsLen = 0;
        if (XWorksheet_saveToXmlData(ws, &wsData, &wsLen)) {
            char wsPath[64];
            snprintf(wsPath, sizeof(wsPath), "xl/worksheets/sheet%d.xml", i + 1);
            XZipWriter_addFile(zip, wsPath, wsData, wsLen);
            XFree_System(wsData);
        }
        
        /* 写入工作表关系 (空的) */
        XRelationships wsRels;
        memset(&wsRels, 0, sizeof(wsRels));
        wsRels.m_relationships = XVector_Create(XlsxRelationship);
        
        relData = NULL;
        relLen = 0;
        if (XRelationships_saveToXmlData(&wsRels, &relData, &relLen)) {
            char wsRelPath[64];
            snprintf(wsRelPath, sizeof(wsRelPath), "xl/worksheets/_rels/sheet%d.xml.rels", i + 1);
            XZipWriter_addFile(zip, wsRelPath, relData, relLen);
            XFree_System(relData);
        }
        /* 手动清理，不释放栈变量 */ XRelationships_clear(&wsRels); if (wsRels.m_relationships) { XVector_delete_base(wsRels.m_relationships); wsRels.m_relationships = NULL; }
    }
    
    /* ========== 写入 xl/theme/theme1.xml ========== */
    XByteArray* themeBuf = XByteArray_create();
    XByteArray_append_utf8(themeBuf, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    XByteArray_append_utf8(themeBuf, "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"Office Theme\">\n");
    XByteArray_append_utf8(themeBuf, "  <a:themeElements>\n");
    XByteArray_append_utf8(themeBuf, "    <a:clrScheme name=\"Office\">\n");
    XByteArray_append_utf8(themeBuf, "      <a:dk1><a:sysClr val=\"windowText\" lastClr=\"000000\"/></a:dk1>\n");
    XByteArray_append_utf8(themeBuf, "      <a:lt1><a:sysClr val=\"window\" lastClr=\"FFFFFF\"/></a:lt1>\n");
    XByteArray_append_utf8(themeBuf, "      <a:dk2><a:srgbClr val=\"1F497D\"/></a:dk2>\n");
    XByteArray_append_utf8(themeBuf, "      <a:lt2><a:srgbClr val=\"EEF1F6\"/></a:lt2>\n");
    XByteArray_append_utf8(themeBuf, "      <a:accent1><a:srgbClr val=\"5B9BD5\"/></a:accent1>\n");
    XByteArray_append_utf8(themeBuf, "      <a:accent2><a:srgbClr val=\"ED7D31\"/></a:accent2>\n");
    XByteArray_append_utf8(themeBuf, "      <a:accent3><a:srgbClr val=\"A5A5A5\"/></a:accent3>\n");
    XByteArray_append_utf8(themeBuf, "      <a:accent4><a:srgbClr val=\"FFC000\"/></a:accent4>\n");
    XByteArray_append_utf8(themeBuf, "      <a:accent5><a:srgbClr val=\"4472C4\"/></a:accent5>\n");
    XByteArray_append_utf8(themeBuf, "      <a:accent6><a:srgbClr val=\"70AD47\"/></a:accent6>\n");
    XByteArray_append_utf8(themeBuf, "      <a:hlink><a:srgbClr val=\"0070C0\"/></a:hlink>\n");
    XByteArray_append_utf8(themeBuf, "      <a:folHlink><a:srgbClr val=\"7030A0\"/></a:folHlink>\n");
    XByteArray_append_utf8(themeBuf, "    </a:clrScheme>\n");
    XByteArray_append_utf8(themeBuf, "    <a:fontScheme name=\"Office\">\n");
    XByteArray_append_utf8(themeBuf, "      <a:majorFont><a:latin typeface=\"Calibri\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont>\n");
    XByteArray_append_utf8(themeBuf, "      <a:minorFont><a:latin typeface=\"Calibri\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont>\n");
    XByteArray_append_utf8(themeBuf, "    </a:fontScheme>\n");
    XByteArray_append_utf8(themeBuf, "    <a:fmtScheme name=\"Office\">\n");
    XByteArray_append_utf8(themeBuf, "      <a:fillStyleList><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:gradFill rotWithShape=\"1\"><a:gsL><a:pos val=\"0\"/><a:schemeClr val=\"phClr\"/></a:gsL><a:gsL><a:pos val=\"10000\"/><a:schemeClr val=\"phClr\"/></a:gsL><a:path path=\"circle\"><a:fillRect l=\"0\" r=\"0\" t=\"0\" b=\"0\"/></a:path></a:gradFill></a:fillStyleList>\n");
    XByteArray_append_utf8(themeBuf, "    </a:fmtScheme>\n");
    XByteArray_append_utf8(themeBuf, "  </a:themeElements>\n");
    XByteArray_append_utf8(themeBuf, "</a:theme>\n");
    
    XZipWriter_addFile(zip, "xl/theme/theme1.xml", (const uint8_t*)XByteArray_data(themeBuf), XByteArray_size_base(themeBuf));
    XByteArray_delete_base(themeBuf);
    
    /* ========== 写入 docProps/core.xml ========== */
    XByteArray* coreBuf = XByteArray_create();
    XByteArray_append_utf8(coreBuf, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    XByteArray_append_utf8(coreBuf, "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" xmlns:dcmitype=\"http://purl.org/dc/dcmitype/\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\">\n");
    XByteArray_append_utf8(coreBuf, "  <dc:creator>XinYueC</dc:creator>\n");
    XByteArray_append_utf8(coreBuf, "</cp:coreProperties>\n");
    XZipWriter_addFile(zip, "docProps/core.xml", (const uint8_t*)XByteArray_data(coreBuf), XByteArray_size_base(coreBuf));
    XByteArray_delete_base(coreBuf);
    
    /* ========== 写入 docProps/app.xml ========== */
    XByteArray* appBuf = XByteArray_create();
    XByteArray_append_utf8(appBuf, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    XByteArray_append_utf8(appBuf, "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\" xmlns:vt=\"http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes\">\n");
    XByteArray_append_utf8(appBuf, "  <Application>XinYueC</Application>\n");
    char appEntry[256];
    snprintf(appEntry, sizeof(appEntry), "  <HeadingPairs><vt:variant><vt:lpstr>Worksheets</vt:lpstr></vt:variant><vt:variant><vt:i4>%d</vt:i4></vt:variant></HeadingPairs>\n", sheetCount);
    XByteArray_append_utf8(appBuf, appEntry);
    XByteArray_append_utf8(appBuf, "  <TitlesOfParts><vt:variant>");
    for (int i = 0; i < sheetCount; i++) {
        XAbstractSheet* sheet = XWorkbook_sheet(wb, i);
        const char* name = sheet && sheet->m_sheetName ? XString_toUtf8(sheet->m_sheetName) : "";
        XByteArray_append_utf8(appBuf, "<vt:lpstr>");
        XByteArray_append_utf8(appBuf, name);
        XByteArray_append_utf8(appBuf, "</vt:lpstr>");
    }
    XByteArray_append_utf8(appBuf, "</vt:variant></TitlesOfParts>\n");
    XByteArray_append_utf8(appBuf, "</Properties>\n");
    XZipWriter_addFile(zip, "docProps/app.xml", (const uint8_t*)XByteArray_data(appBuf), XByteArray_size_base(appBuf));
    XByteArray_delete_base(appBuf);
    
    /* 关闭 ZIP 文件 */
    bool result = XZipWriter_close(zip);
    XZipWriter_delete(zip);
    
    /* 更新文档状态 */
    if (result && self->m_filePath) {
        XString_clear_base(self->m_filePath);
        XString_append_utf8(self->m_filePath, xlsxName);
        ((XDocument*)self)->m_isModified = false;
    }
    
    return result;
}

bool XDocument_isLoadPackage(const XDocument* self) {
    return self ? self->m_isLoaded : false;
}

bool XDocument_load(XDocument* self) {
    if (!self) return false;
    /* 占位实现：未真正加载任何包 */
    return false;
}

/* ========== 辅助：从ZIP解析 sheet name -> path 映射 ========== */
static char* find_sheet_path_in_zip(const char* zipPath, const char* sheetName, int sheetIndex)
{
    if (!zipPath) return NULL;
    XZipReader* zip = XZipReader_create(zipPath);
    if (!zip) return NULL;

    /* 读取 workbook.xml.rels 获取 rId -> path */
    typedef struct { char rid[32]; char path[256]; } RelEntry;
    RelEntry rels[32];
    int relCount = 0;
    memset(rels, 0, sizeof(rels));

    XByteArray* relXml = XZipReader_fileData(zip, "xl/_rels/workbook.xml.rels");
    if (relXml) {
        char* relStr = (char*)XByteArray_data(relXml);
        size_t relLen = XByteArray_size_base(relXml);
        if (relStr && relLen > 0) {
            relStr[relLen] = '\0';
            const char* rp = relStr;
            while ((rp = strstr(rp, "<Relationship")) != NULL && relCount < 32) {
                const char* idS = strstr(rp, "Id=\""); const char* tgtS = strstr(rp, "Target=\"");
                if (idS && tgtS) {
                    idS += 4; const char* idE = strchr(idS, '"');
                    tgtS += 9; const char* tgtE = strchr(tgtS, '"');
                    if (idE && tgtE && idE > idS && tgtE > tgtS) {
                        size_t rl = (size_t)(idE - idS) < 31 ? (size_t)(idE - idS) : 31;
                        size_t tl = (size_t)(tgtE - tgtS) < 255 ? (size_t)(tgtE - tgtS) : 255;
                        memcpy(rels[relCount].rid, idS, rl); rels[relCount].rid[rl] = '\0';
                        memcpy(rels[relCount].path, tgtS, tl); rels[relCount].path[tl] = '\0';
                        /* 转换为绝对路径 */
                        if (rels[relCount].path[0] != '/') {
                            char tmp[256];
                            snprintf(tmp, sizeof(tmp), "xl/%s", rels[relCount].path);
                            strncpy(rels[relCount].path, tmp, 255); rels[relCount].path[255] = '\0';
                        }
                        relCount++;
                    }
                }
                rp++;
            }
        }
        XByteArray_delete_base(relXml);
    }

    /* 读取 workbook.xml 找 sheet */
    XByteArray* wbXml = XZipReader_fileData(zip, "xl/workbook.xml");
    XZipReader_delete(zip);
    if (!wbXml) return NULL;

    char* xml = (char*)XByteArray_data(wbXml);
    size_t xmlLen = XByteArray_size_base(wbXml);
    xml[xmlLen] = '\0';

    char* result = NULL;
    const char* sp = xml;
    int currentIdx = 0;

    while ((sp = strstr(sp, "<sheet ")) != NULL) {
        const char* nameS = strstr(sp, "name=\""); const char* ridS = strstr(sp, "r:id=\"");
        if (nameS && ridS) {
            nameS += 7; const char* nameE = strchr(nameS, '"');
            ridS += 6; const char* ridE = strchr(ridS, '"');
            if (nameE && ridE && nameE > nameS && ridE > ridS) {
                size_t nl = (size_t)(nameE - nameS);
                size_t rrl = (size_t)(ridE - ridS);
                char tmpName[256] = {0}; char tmpRid[32] = {0};
                if (nl < sizeof(tmpName) && rrl < sizeof(tmpRid)) {
                    memcpy(tmpName, nameS, nl); tmpName[nl] = '\0';
                    memcpy(tmpRid, ridS, rrl); tmpRid[rrl] = '\0';
                    bool match = false;
                    if (sheetName && strcmp(tmpName, sheetName) == 0) match = true;
                    else if (!sheetName && currentIdx == sheetIndex) match = true;
                    if (match) {
                        for (int i = 0; i < relCount; i++) {
                            if (strcmp(rels[i].rid, tmpRid) == 0) {
                                result = strdup(rels[i].path);
                                break;
                            }
                        }
                    }
                }
            }
        }
        sp++; currentIdx++;
    }

    XByteArray_delete_base(wbXml);
    return result;
}

/* ========== SAX 读取 ========== */
bool XDocument_readSheetSax(XDocument* self, const char* sheetName,
                            const XReadSax_Options* opt,
                            XReadSax_CellCallback onCell, void* userData)
{
    if (!self || !onCell) return false;
    const char* zipPath = self->m_filePath ? XString_toUtf8(self->m_filePath) : NULL;
    if (!zipPath || strlen(zipPath) == 0) return false;

    /* 加载共享字符串 */
    XStringList* sharedStrings = XStringList_create();
    XReadSax_loadSharedStringsFromZip(zipPath, sharedStrings);

    /* 查找 sheet path */
    char* sheetPath = find_sheet_path_in_zip(zipPath, sheetName, -1);
    if (!sheetPath) {
        XStringList_delete_base(sharedStrings);
        return false;
    }

    /* SAX 读取 */
    bool ok = XReadSax_readSheetFromZip(zipPath, sheetPath, sharedStrings, opt, onCell, userData);

    /* 释放共享字符串 */
    {
        size_t cnt = XStringList_size_base(sharedStrings);
        for (size_t i = 0; i < cnt; i++) {
            XString** pp = (XString**)XStringList_at_base(sharedStrings, i);
            if (pp && *pp) XString_delete_base(*pp);
        }
    }
    XStringList_delete_base(sharedStrings);
    free(sheetPath);
    (void)self;  /* suppress unused warning */
    return ok;
}

bool XDocument_readSheetSaxByIndex(XDocument* self, int sheetIndex,
                                   const XReadSax_Options* opt,
                                   XReadSax_CellCallback onCell, void* userData)
{
    if (!self || !onCell || sheetIndex < 0) return false;
    const char* zipPath = self->m_filePath ? XString_toUtf8(self->m_filePath) : NULL;
    if (!zipPath || strlen(zipPath) == 0) return false;

    XStringList* sharedStrings = XStringList_create();
    XReadSax_loadSharedStringsFromZip(zipPath, sharedStrings);

    char* sheetPath = find_sheet_path_in_zip(zipPath, NULL, sheetIndex);
    if (!sheetPath) {
        XStringList_delete_base(sharedStrings);
        return false;
    }

    bool ok = XReadSax_readSheetFromZip(zipPath, sheetPath, sharedStrings, opt, onCell, userData);

    {
        size_t cnt = XStringList_size_base(sharedStrings);
        for (size_t i = 0; i < cnt; i++) {
            XString** pp = (XString**)XStringList_at_base(sharedStrings, i);
            if (pp && *pp) XString_delete_base(*pp);
        }
    }
    XStringList_delete_base(sharedStrings);
    free(sheetPath);
    return ok;
}


/* ========== CSV 导出 ========== */
/**
 * @brief     将文档导出为 CSV 文件
 * @param self       XDocument 指针
 * @param csvFileName CSV 文件路径
 * @return    成功返回true
 * @note      导出第一个工作表的数据，以逗号为分隔符
 */
bool XDocument_saveAsCsv(const XDocument* self, const char* csvFileName)
{
    if (!self || !csvFileName || !self->m_workbook) return false;

    /* 获取第一个工作表 */
    XAbstractSheet* sheet = XWorkbook_activeSheet(self->m_workbook);
    if (!sheet || sheet->m_sheetType != XAbstractSheet_ST_WorkSheet) return false;
    XWorksheet* ws = (XWorksheet*)sheet;

    /* 获取所有单元格 */
    XCellLocation* locs = NULL;
    int maxRow = 0, maxCol = 0;
    int count = XWorksheet_getFullCells(ws, &locs, &maxRow, &maxCol);
    if (count <= 0 || maxRow <= 0 || maxCol <= 0) {
        if (locs) XFree_System(locs);
        return false;
    }

    /* 构建值矩阵（1索引 -> 0索引） */
    char*** matrix = (char***)calloc((size_t)maxRow, sizeof(char**));
    if (!matrix) { XFree_System(locs); return false; }
    for (int r = 0; r < maxRow; r++) {
        matrix[r] = (char**)calloc((size_t)maxCol, sizeof(char*));
    }

    for (int i = 0; i < count; i++) {
        int r = locs[i].m_row - 1;
        int c = locs[i].m_col - 1;
        if (r >= 0 && r < maxRow && c >= 0 && c < maxCol) {
            XCell* cell = locs[i].m_cell;
            const char* val = XCell_readValue(cell);
            if (val) matrix[r][c] = strdup(val);
        }
    }
    XFree_System(locs);

    /* 打开文件 */
    FILE* fp = fopen(csvFileName, "w");
    if (!fp) {
        for (int r = 0; r < maxRow; r++) {
            if (matrix[r]) {
                for (int c = 0; c < maxCol; c++) {
                    if (matrix[r][c]) free(matrix[r][c]);
                }
                free(matrix[r]);
            }
        }
        free(matrix);
        return false;
    }

    /* 写入 CSV */
    for (int r = 0; r < maxRow; r++) {
        for (int c = 0; c < maxCol; c++) {
            char* val = matrix[r][c];
            if (val && strlen(val) > 0) {
                /* 检查是否需要加引号（包含逗号、引号、换行） */
                bool needsQuote = false;
                for (const char* p = val; *p; p++) {
                    if (*p == '\",' || *p == '"' || *p == '\n' || *p == '\r') {
                        needsQuote = true;
                        break;
                    }
                }
                if (needsQuote) {
                    /* 双引号转义 */
                    fputc('"', fp);
                    for (const char* p = val; *p; p++) {
                        if (*p == '"') fputc('"', fp);
                        fputc(*p, fp);
                    }
                    fputc('"', fp);
                } else {
                    fputs(val, fp);
                }
            }
            if (c < maxCol - 1) fputc(',', fp);
            if (val) free(val);
        }
        fputc('\n', fp);
        free(matrix[r]);
    }
    fclose(fp);
    free(matrix);
    (void)self;  /* suppress unused */
    return true;
}

/* ========== 自动列宽 ========== */
/* ========== 自动列宽 ========== */
/**
 * @brief     自动调整列宽
 * @param self       XDocument 指针
 * @param colFirst  起始列（1索引）
 * @param colLast   结束列（1索引）
 * @return    成功返回true
 */
bool XDocument_autosizeColumnWidth(XDocument* self, int colFirst, int colLast)
{
    if (!self || !self->m_workbook || colFirst <= 0 || colLast < colFirst) return false;

    XAbstractSheet* sheet = XWorkbook_activeSheet(self->m_workbook);
    if (!sheet || sheet->m_sheetType != XAbstractSheet_ST_WorkSheet) return false;
    XWorksheet* ws = (XWorksheet*)sheet;

    XCellLocation* locs = NULL;
    int maxRow = 0, maxCol = 0;
    int count = XWorksheet_getFullCells(ws, &locs, &maxRow, &maxCol);
    if (count <= 0 || maxCol <= 0) { if (locs) XFree_System(locs); return false; }

    if (colFirst > maxCol) { XFree_System(locs); return false; }
    if (colLast > maxCol) colLast = maxCol;

    /* 初始化每列最大宽度为8（默认宽度） */
    double* maxWidth = (double*)calloc((size_t)(colLast - colFirst + 1), sizeof(double));
    if (!maxWidth) { XFree_System(locs); return false; }
    for (int c = colFirst; c <= colLast; c++) maxWidth[c - colFirst] = 8.0;

    for (int i = 0; i < count; i++) {
        int col = locs[i].m_col;
        if (col < colFirst || col > colLast) continue;
        XCell* cell = locs[i].m_cell;
        const char* val = XCell_readValue(cell);
        if (val && strlen(val) > 0) {
            double len = (double)strlen(val);
            int idx = col - colFirst;
            if (len > maxWidth[idx]) maxWidth[idx] = len;
        }
    }

    bool ok = true;
    for (int c = colFirst; c <= colLast; c++) {
        int idx = c - colFirst;
        double width = maxWidth[idx] * 1.1;  /* 稍微增加一点边距 */
        if (width < 2.0) width = 2.0;
        if (width > 255.0) width = 255.0;
        if (!XWorksheet_setColumnWidth(ws, c, c, width)) ok = false;
    }

    XFree_System(locs);
    free(maxWidth);
    return ok;
}

/**
 * @brief     自动调整所有列的宽度
 * @param self XDocument 指针
 * @return    成功返回true
 */
bool XDocument_autosizeColumnWidthAll(XDocument* self)
{
    if (!self || !self->m_workbook) return false;
    XAbstractSheet* sheet = XWorkbook_activeSheet(self->m_workbook);
    if (!sheet || sheet->m_sheetType != XAbstractSheet_ST_WorkSheet) return false;
    XWorksheet* ws = (XWorksheet*)sheet;

    XCellLocation* locs = NULL;
    int maxRow = 0, maxCol = 0;
    int count = XWorksheet_getFullCells(ws, &locs, &maxRow, &maxCol);
    if (count <= 0 || maxCol <= 0) { if (locs) XFree_System(locs); return false; }

    double* maxWidth = (double*)calloc((size_t)maxCol, sizeof(double));
    if (!maxWidth) { XFree_System(locs); return false; }
    for (int c = 0; c < maxCol; c++) maxWidth[c] = 8.0;

    for (int i = 0; i < count; i++) {
        int col = locs[i].m_col;
        if (col < 1 || col > maxCol) continue;
        XCell* cell = locs[i].m_cell;
        const char* val = XCell_readValue(cell);
        if (val && strlen(val) > 0) {
            double len = (double)strlen(val);
            if (len > maxWidth[col - 1]) maxWidth[col - 1] = len;
        }
    }

    bool ok = true;
    for (int c = 1; c <= maxCol; c++) {
        double width = maxWidth[c - 1] * 1.1;
        if (width < 2.0) width = 2.0;
        if (width > 255.0) width = 255.0;
        if (!XWorksheet_setColumnWidth(ws, c, c, width)) ok = false;
    }

    XFree_System(locs);
    free(maxWidth);
    return ok;
}


/* ========== 样式复制 ========== */
/**
 * @brief      从源文件复制样式到目标文件（静态方法）
 * @param fromPath 源 XLSX 文件路径
 * @param toPath   目标 XLSX 文件路径
 * @return     成功返回 true
 * @note       对标 QXlsx::Document::copyStyle
 *             使用临时文件实现，因为 ZipWriter 无法直接修改已存在的文件
 */
bool XDocument_copyStyle(const char* fromPath, const char* toPath)
{
    if (!fromPath || !toPath) return false;

    /* 打开源文件和目标文件 */
    XZipReader* fromZip = XZipReader_create(fromPath);
    if (!fromZip) return false;

    XZipReader* toZip = XZipReader_create(toPath);
    if (!toZip) { XZipReader_delete(fromZip); return false; }

    /* 创建临时文件用于写入 */
    char tempPath[] = "/tmp/xlsx_style_copy_XXXXXX";
    int tempFd = mkstemp(tempPath);
    if (tempFd < 0) { XZipReader_delete(fromZip); XZipReader_delete(toZip); return false; }
    close(tempFd);

    XZipWriter* tempZip = XZipWriter_create(tempPath);
    if (!tempZip) { 
        XZipReader_delete(fromZip); 
        XZipReader_delete(toZip); 
        unlink(tempPath);
        return false; 
    }

    bool ok = true;

    /* 获取源文件和目标文件的所有文件路径 */
    XStringList* fromPaths = XZipReader_filePaths(fromZip);
    XStringList* toPaths = XZipReader_filePaths(toZip);

    if (!fromPaths || !toPaths) {
        ok = false;
        goto cleanup;
    }

    size_t fromCount = XVector_size((XVector*)fromPaths);
    size_t toCount = XVector_size((XVector*)toPaths);

    /* 获取目标文件的所有路径用于比较 */
    for (size_t i = 0; i < toCount && ok; i++) {
        XString** ppPath = (XString**)XVector_at_base((const XVector*)toPaths, (int64_t)i);
        if (!ppPath || !*ppPath) continue;
        const char* toFilePath = XString_toUtf8(*ppPath);
        if (!toFilePath) continue;

        /* 检查是否需要复制样式相关文件 */
        bool needCopy = false;
        const char* content = NULL;
        size_t contentLen = 0;

        /* 1. 复制 styles.xml */
        if (strstr(toFilePath, "xl/styles") != NULL) {
            if (XZipReader_fileData(fromZip, toFilePath) != NULL) {
                XByteArray* data = XZipReader_fileData(fromZip, toFilePath);
                if (data) {
                    content = (const char*)XByteArray_data(data);
                    contentLen = XByteArray_size_base(data);
                    needCopy = true;
                }
            }
        }
        /* 2. 复制 workbook.xml 中的 workbookPr */
        else if (strstr(toFilePath, "xl/workbook") != NULL) {
            if (XZipReader_fileData(fromZip, toFilePath) != NULL) {
                XByteArray* data = XZipReader_fileData(fromZip, toFilePath);
                if (data) {
                    content = (const char*)XByteArray_data(data);
                    contentLen = XByteArray_size_base(data);
                    needCopy = true;
                }
            }
        }
        /* 3. 复制 worksheets 中的相关样式 */
        else if (strstr(toFilePath, "xl/worksheets/sheet") != NULL) {
            if (XZipReader_fileData(fromZip, toFilePath) != NULL) {
                XByteArray* data = XZipReader_fileData(fromZip, toFilePath);
                if (data) {
                    content = (const char*)XByteArray_data(data);
                    contentLen = XByteArray_size_base(data);
                    needCopy = true;
                }
            }
        }

        if (needCopy && content && contentLen > 0) {
            /* 直接复制源文件的内容到临时文件 */
            XZipWriter_addFile(tempZip, toFilePath, (const uint8_t*)content, contentLen);
        } else {
            /* 复制目标文件的原始内容 */
            XByteArray* origData = XZipReader_fileData(toZip, toFilePath);
            if (origData) {
                const uint8_t* d = XByteArray_data(origData);
                size_t dlen = XByteArray_size_base(origData);
                XZipWriter_addFile(tempZip, toFilePath, d, dlen);
                XByteArray_delete_base(origData);
            } else {
                /* 如果目标文件中没有，使用空数据 */
                XZipWriter_addFile(tempZip, toFilePath, (const uint8_t*)"", 0);
            }
        }
    }

cleanup:
    if (fromPaths) XStringList_delete_base(fromPaths);
    if (toPaths) XStringList_delete_base(toPaths);
    XZipWriter_delete(tempZip);
    XZipReader_delete(fromZip);
    XZipReader_delete(toZip);

    if (ok) {
        /* 用临时文件替换目标文件 */
        ok = (rename(tempPath, toPath) == 0);
    } else {
        unlink(tempPath);
    }

    return ok;
}

/* ========== 图片修改 ========== */
/**
 * @brief      修改文档中的图片
 * @param self           文档指针
 * @param imageIndex     图片索引
 * @param newImagePath   新图片路径
 * @return              成功返回 true
 * @note       对标 QXlsx::Document::changeimage
 */
bool XDocument_changeImage(XDocument* self, int imageIndex, const char* newImagePath)
{
    if (!self || !self->m_workbook || imageIndex < 0 || !newImagePath) return false;

    /* 获取媒体文件列表 */
    int mediaCount = 0;
    XMediaFile** mediaFiles = XWorkbook_mediaFiles(self->m_workbook, &mediaCount);
    if (!mediaFiles || imageIndex >= mediaCount) {
        if (mediaFiles) XFree_System(mediaFiles);
        return false;
    }

    XMediaFile* mediaFile = mediaFiles[imageIndex];
    if (!mediaFile) { XFree_System(mediaFiles); return false; }

    /* 获取新图片的文件扩展名 */
    const char* ext = strrchr(newImagePath, '.');
    if (!ext) { XFree_System(mediaFiles); return false; }
    ext++; /* 跳过点 */

    /* 确定 MIME 类型 */
    const char* mimeType = "image/png";
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) {
        mimeType = "image/jpeg";
    } else if (strcasecmp(ext, "bmp") == 0) {
        mimeType = "image/bmp";
    } else if (strcasecmp(ext, "gif") == 0) {
        mimeType = "image/gif";
    } else if (strcasecmp(ext, "png") == 0) {
        mimeType = "image/png";
    }

    /* 读取新图片文件 */
    FILE* fp = fopen(newImagePath, "rb");
    if (!fp) { XFree_System(mediaFiles); return false; }

    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fileSize <= 0) { fclose(fp); XFree_System(mediaFiles); return false; }

    uint8_t* imageData = (uint8_t*)malloc((size_t)fileSize);
    if (!imageData) { fclose(fp); XFree_System(mediaFiles); return false; }

    size_t readSize = fread(imageData, 1, (size_t)fileSize, fp);
    fclose(fp);

    if (readSize != (size_t)fileSize) {
        free(imageData);
        XFree_System(mediaFiles);
        return false;
    }

    /* 更新媒体文件内容 */
    XMediaFile_set(mediaFile, imageData, (size_t)fileSize, ext, mimeType);
    XMediaFile_setFileName(mediaFile, newImagePath);

    free(imageData);
    XFree_System(mediaFiles);
    return true;
}

/* ========== 设备接口（对标 QXlsx::Document(QIODevice*) / saveAs(QIODevice*)） ========== */

XDocument* XDocument_createFromDevice(struct XIODevice* device)
{
    (void)device;
    /* 从设备读取 XLSX 数据并解析 - 框架预留 */
    XDocument* self = XDocument_create();
    if (!self) return NULL;
    /* TODO: 从 device 读取全部字节，解压 ZIP，解析工作簿 */
    return self;
}

bool XDocument_saveAsDevice(const XDocument* self, struct XIODevice* device)
{
    (void)device;
    if (!self || !self->m_workbook) return false;
    /* TODO: 将 XLSX ZIP 数据写入 device */
    return false;
}
