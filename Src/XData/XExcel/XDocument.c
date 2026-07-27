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
#include "XXmlStreamReader.h"
#include "XVector.h"
#include "XChartsheet.h"
#include "XFile.h"
#include "XSaveFile.h"
#include "XChar.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "XStringList.h"

/* ========== 内部辅助：将 C 字符串字面量包装为 XString 传递给其他模块 ========== */
static void addDefault_cstr(XContentTypes* ct, const char* key, const char* value) {
    XString* k = XString_create_utf8(key);
    XString* v = XString_create_utf8(value);
    XContentTypes_addDefault(ct, k, v);
    XString_delete_base(k);
    XString_delete_base(v);
}
static void addOverride_cstr(XContentTypes* ct, const char* key, const char* value) {
    XString* k = XString_create_utf8(key);
    XString* v = XString_create_utf8(value);
    XContentTypes_addOverride(ct, k, v);
    XString_delete_base(k);
    XString_delete_base(v);
}
static void addDocRel_cstr(XRelationships* rels, const char* type, const char* target) {
    XString* t = XString_create_utf8(type);
    XString* tg = XString_create_utf8(target);
    XRelationships_addDocumentRelationship(rels, t, tg);
    XString_delete_base(t);
    XString_delete_base(tg);
}
static void addPkgRel_cstr(XRelationships* rels, const char* type, const char* target) {
    XString* t = XString_create_utf8(type);
    XString* tg = XString_create_utf8(target);
    XRelationships_addPackageRelationship(rels, t, tg);
    XString_delete_base(t);
    XString_delete_base(tg);
}
static void addWorksheetRel_cstr(XRelationships* rels, const char* type,
                                 const char* target, const char* targetMode) {
    XString* t = XString_create_utf8(type);
    XString* tg = XString_create_utf8(target);
    XString* mode = targetMode ? XString_create_utf8(targetMode) : NULL;
    XRelationships_addWorksheetRelationship(rels, t, tg, mode);
    if (t) XString_delete_base(t);
    if (tg) XString_delete_base(tg);
    if (mode) XString_delete_base(mode);
}
static bool zipAddFile_cstr(XZipWriter* zip, const char* path, const uint8_t* data, size_t size) {
    XString* p = XString_create_utf8(path);
    bool result = p && XZipWriter_addFile(zip, p, data, size);
    if (p) XString_delete_base(p);
    return result;
}

static void appendDrawingMarker(XByteArray* xml, int row, int column)
{
    char buffer[256];
    snprintf(buffer, sizeof(buffer),
        "<xdr:from><xdr:col>%d</xdr:col><xdr:colOff>0</xdr:colOff>"
        "<xdr:row>%d</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:from>",
        column - 1, row - 1);
    XByteArray_append_utf8(xml, buffer);
}

static uint32_t readBigEndianUInt32(const uint8_t* data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
        ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static void imageDisplaySize(const XMediaFile* media, int* width, int* height)
{
    const int maxWidth = 480;
    const int maxHeight = 240;
    int sourceWidth = 128;
    int sourceHeight = 128;
    const uint8_t* bytes = media ? XMediaFile_contents(media) : NULL;
    size_t size = media ? XMediaFile_contentsSize(media) : 0;
    static const uint8_t pngSignature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    if (bytes && size >= 24 && memcmp(bytes, pngSignature, sizeof(pngSignature)) == 0) {
        uint32_t pngWidth = readBigEndianUInt32(bytes + 16);
        uint32_t pngHeight = readBigEndianUInt32(bytes + 20);
        if (pngWidth > 0 && pngHeight > 0 && pngWidth <= 100000 && pngHeight <= 100000) {
            sourceWidth = (int)pngWidth;
            sourceHeight = (int)pngHeight;
        }
    }

    double scale = 1.0;
    if (sourceWidth > maxWidth) scale = (double)maxWidth / sourceWidth;
    if (sourceHeight * scale > maxHeight) scale = (double)maxHeight / sourceHeight;
    *width = (int)(sourceWidth * scale + 0.5);
    *height = (int)(sourceHeight * scale + 0.5);
    if (*width < 1) *width = 1;
    if (*height < 1) *height = 1;
}

static bool saveWorksheetDrawing(XZipWriter* zip, const XWorksheet* ws,
                                 int drawingIndex, int* nextImageIndex,
                                 int* nextChartIndex)
{
    if (!zip || !ws || !nextImageIndex || !nextChartIndex) return false;
    size_t imageCount = XWorksheet_getImageCount(ws);
    size_t chartCount = ws->m_chartFiles
        ? XVector_size_base((XContainer*)ws->m_chartFiles) : 0;
    if (imageCount == 0 && chartCount == 0) return true;

    XByteArray* drawing = XByteArray_create();
    if (!drawing) return false;
    XByteArray_append_utf8(drawing,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");

    XRelationships drawingRels;
    memset(&drawingRels, 0, sizeof(drawingRels));
    drawingRels.m_relationships = XVector_Create(XlsxRelationship);

    bool ok = drawingRels.m_relationships != NULL;
    for (size_t i = 0; ok && i < imageCount; ++i) {
        XMediaFile* media = *(XMediaFile**)XVector_at_base(ws->m_mediaFiles, i);
        XWorksheet_ImagePosition* position =
            (XWorksheet_ImagePosition*)XVector_at_base(ws->m_imagePositions, i);
        if (!media || !position) continue;

        int imageIndex = (*nextImageIndex)++;
        const char* suffix = XMediaFile_suffix(media) ? XString_toUtf8(XMediaFile_suffix(media)) : "png";
        char packagePath[128];
        char relationTarget[128];
        snprintf(packagePath, sizeof(packagePath), "xl/media/image%d.%s", imageIndex, suffix);
        snprintf(relationTarget, sizeof(relationTarget), "../media/image%d.%s", imageIndex, suffix);
        ok = zipAddFile_cstr(zip, packagePath, XMediaFile_contents(media),
            XMediaFile_contentsSize(media));

        addDocRel_cstr(&drawingRels,
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image",
            relationTarget);
        int rid = XRelationships_lastAssignedRidFor(&drawingRels);

        int imageWidth = 0;
        int imageHeight = 0;
        imageDisplaySize(media, &imageWidth, &imageHeight);
        long long widthEmu = (long long)imageWidth * 9525LL;
        long long heightEmu = (long long)imageHeight * 9525LL;
        XByteArray_append_utf8(drawing, "<xdr:oneCellAnchor>");
        appendDrawingMarker(drawing, position->m_row, position->m_column);
        char extentXml[128];
        snprintf(extentXml, sizeof(extentXml), "<xdr:ext cx=\"%lld\" cy=\"%lld\"/>",
            widthEmu, heightEmu);
        XByteArray_append_utf8(drawing, extentXml);
        char pictureXml[1024];
        snprintf(pictureXml, sizeof(pictureXml),
            "<xdr:pic><xdr:nvPicPr><xdr:cNvPr id=\"%d\" name=\"Picture %d\"/>"
            "<xdr:cNvPicPr/></xdr:nvPicPr><xdr:blipFill><a:blip r:embed=\"rId%d\"/>"
            "<a:stretch><a:fillRect/></a:stretch></xdr:blipFill><xdr:spPr>"
            "<a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%lld\" cy=\"%lld\"/></a:xfrm>"
            "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></xdr:spPr></xdr:pic>"
            "<xdr:clientData/></xdr:oneCellAnchor>",
            (int)i + 1, imageIndex, rid, widthEmu, heightEmu);
        XByteArray_append_utf8(drawing, pictureXml);
    }
    for (size_t i = 0; ok && i < chartCount; ++i) {
        XChart* chart = *(XChart**)XVector_at_base(ws->m_chartFiles, i);
        if (!chart) continue;
        int chartIndex = (*nextChartIndex)++;
        uint8_t* chartData = NULL;
        size_t chartLength = 0;
        char chartPath[128];
        char chartTarget[128];
        snprintf(chartPath, sizeof(chartPath), "xl/charts/chart%d.xml", chartIndex);
        snprintf(chartTarget, sizeof(chartTarget), "../charts/chart%d.xml", chartIndex);
        ok = XChart_saveToXmlData(chart, &chartData, &chartLength) &&
             zipAddFile_cstr(zip, chartPath, chartData, chartLength);
        if (chartData) XFree_System(chartData);
        if (!ok) break;
        addDocRel_cstr(&drawingRels,
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart",
            chartTarget);
        int rid = XRelationships_lastAssignedRidFor(&drawingRels);
        XByteArray_append_utf8(drawing, "<xdr:oneCellAnchor>");
        appendDrawingMarker(drawing, chart->m_row, chart->m_col);
        char frame[2048];
        snprintf(frame, sizeof(frame),
             "<xdr:ext cx=\"%lld\" cy=\"%lld\"/><xdr:graphicFrame macro=\"\">"
             "<xdr:nvGraphicFramePr><xdr:cNvPr id=\"%d\" name=\"Chart %d\"/>"
             "<xdr:cNvGraphicFramePr/></xdr:nvGraphicFramePr><xdr:xfrm>"
             "<a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm>"
             "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">"
             "<c:chart xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
             "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
             "r:id=\"rId%d\"/></a:graphicData></a:graphic></xdr:graphicFrame>"
             "<xdr:clientData/></xdr:oneCellAnchor>",
             (long long)chart->m_width * 9525LL, (long long)chart->m_height * 9525LL,
             (int)imageCount + (int)i + 1, chartIndex, rid);
        XByteArray_append_utf8(drawing, frame);
    }
    XByteArray_append_utf8(drawing, "</xdr:wsDr>");

    char drawingPath[128];
    snprintf(drawingPath, sizeof(drawingPath), "xl/drawings/drawing%d.xml", drawingIndex);
    if (ok) ok = zipAddFile_cstr(zip, drawingPath, XByteArray_data(drawing),
        XByteArray_size_base((XContainer*)drawing));
    XByteArray_delete_base(drawing);

    uint8_t* relData = NULL;
    size_t relLen = 0;
    if (ok && XRelationships_saveToXmlData(&drawingRels, &relData, &relLen)) {
        char relPath[160];
        snprintf(relPath, sizeof(relPath), "xl/drawings/_rels/drawing%d.xml.rels", drawingIndex);
        ok = zipAddFile_cstr(zip, relPath, relData, relLen);
        XFree_System(relData);
    } else if (ok) ok = false;
    XRelationships_clear(&drawingRels);
    if (drawingRels.m_relationships) XVector_delete_base(drawingRels.m_relationships);
    return ok;
}

static bool saveChartsheetDrawing(XZipWriter* zip, const XChartsheet* chartsheet,
                                  int drawingIndex, int chartIndex)
{
    if (!zip || !chartsheet || !chartsheet->m_chart) return false;
    XChart* chart = chartsheet->m_chart;
    uint8_t* chartData = NULL;
    size_t chartLength = 0;
    char path[160];
    snprintf(path, sizeof(path), "xl/charts/chart%d.xml", chartIndex);
    bool ok = XChart_saveToXmlData(chart, &chartData, &chartLength) &&
        zipAddFile_cstr(zip, path, chartData, chartLength);
    if (chartData) XFree_System(chartData);
    if (!ok) return false;

    XByteArray* drawing = XByteArray_create();
    if (!drawing) return false;
    XByteArray_append_utf8(drawing,
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\" "
        "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<xdr:absoluteAnchor><xdr:pos x=\"0\" y=\"0\"/>");
    char frame[2048];
    snprintf(frame, sizeof(frame),
         "<xdr:ext cx=\"%lld\" cy=\"%lld\"/><xdr:graphicFrame macro=\"\">"
        "<xdr:nvGraphicFramePr><xdr:cNvPr id=\"2\" name=\"Chart %d\"/>"
        "<xdr:cNvGraphicFramePr><a:graphicFrameLocks noGrp=\"1\"/>"
        "</xdr:cNvGraphicFramePr></xdr:nvGraphicFramePr><xdr:xfrm>"
        "<a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/></xdr:xfrm>"
        "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">"
        "<c:chart xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "r:id=\"rId1\"/></a:graphicData></a:graphic></xdr:graphicFrame>"
        "<xdr:clientData/></xdr:absoluteAnchor></xdr:wsDr>",
        (long long)chart->m_width * 9525LL, (long long)chart->m_height * 9525LL,
        chartIndex);
    XByteArray_append_utf8(drawing, frame);
    snprintf(path, sizeof(path), "xl/drawings/drawing%d.xml", drawingIndex);
    ok = zipAddFile_cstr(zip, path, XByteArray_data(drawing),
        XByteArray_size_base((XContainer*)drawing));
    XByteArray_delete_base(drawing);
    if (!ok) return false;

    XRelationships relations;
    memset(&relations, 0, sizeof(relations));
    relations.m_relationships = XVector_Create(XlsxRelationship);
    char target[128];
    snprintf(target, sizeof(target), "../charts/chart%d.xml", chartIndex);
    addDocRel_cstr(&relations,
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart", target);
    uint8_t* relationData = NULL;
    size_t relationLength = 0;
    ok = XRelationships_saveToXmlData(&relations, &relationData, &relationLength);
    if (ok) {
        snprintf(path, sizeof(path), "xl/drawings/_rels/drawing%d.xml.rels", drawingIndex);
        ok = zipAddFile_cstr(zip, path, relationData, relationLength);
    }
    if (relationData) XFree_System(relationData);
    XRelationships_clear(&relations);
    if (relations.m_relationships) XVector_delete_base(relations.m_relationships);
    return ok;
}


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
    if (self->m_docPropsCore) {
        XString_Init_Utf8(name, "creator");
        XString_Init_Utf8(value, "XinYueC");
        XDocPropsCore_setProperty(self->m_docPropsCore, name, value);
        XString_deinit_base(name);
        XString_deinit_base(value);
    }
    if (self->m_docPropsApp) {
        XString_Init_Utf8(name, "Application");
        XString_Init_Utf8(value, "XinYueC");
        XDocPropsApp_setProperty(self->m_docPropsApp, name, value);
        XString_deinit_base(name);
        XString_deinit_base(value);
    }
    /* 默认添加一个工作表 */
    if (self->m_workbook) {
        XString* sheet1Name = XString_create_utf8("Sheet1");
        XWorkbook_addSheet(self->m_workbook, sheet1Name, XAbstractSheet_ST_WorkSheet);
        XString_delete_base(sheet1Name);
    }
    return self;
}

XDocument* XDocument_createFromFile(const XString* xlsxName)
{
    if (!xlsxName) return NULL;
    XDocument* self = XDocument_create();
    if (!self) return NULL;
    self->m_filePath = XString_create_copy(xlsxName);
    if (!XDocument_load(self)) {
        XDocument_delete(self);
        return NULL;
    }
    return self;
}

void XDocument_delete(XDocument* self)
{
    if (!self) return;
    if (self->m_workbook) XWorkbook_delete(self->m_workbook);
    if (self->m_docPropsApp) { XDocPropsApp_delete(self->m_docPropsApp); }
    if (self->m_docPropsCore) { XDocPropsCore_delete(self->m_docPropsCore); }
    if (self->m_filePath) XString_delete_base(self->m_filePath);
    if (self->m_packageData) XByteArray_delete_base(self->m_packageData);
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
int XDocument_insertImage(XDocument* self, int row, int col, const XString* imagePath)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws) return -1;
    self->m_isModified = true;
    return XWorksheet_insertImage(ws, row, col, imagePath);
}
bool XDocument_getImage(const XDocument* self, int imageIndex, XByteArray* imgData)
{
    XWorksheet* ws = getCurrentWorksheet((XDocument*)self);
    return ws ? XWorksheet_getImage(ws, imageIndex, imgData) : false;
}
bool XDocument_getImageAt(const XDocument* self, int row, int col, XByteArray* imgData)
{
    XWorksheet* ws = getCurrentWorksheet((XDocument*)self);
    return ws ? XWorksheet_getImageAt(ws, row, col, imgData) : false;
}
unsigned int XDocument_getImageCount(const XDocument* self)
{
    XWorksheet* ws = getCurrentWorksheet((XDocument*)self);
    return ws ? XWorksheet_getImageCount(ws) : 0;
}

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
bool XDocument_defineName(XDocument* self, const XString* name, const XString* formula, const XString* comment, const XString* scope)
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
const XString* XDocument_documentProperty(const XDocument* self, const XString* name) {
    if (!self || !name) return NULL;
    return XDocPropsCore_property(self->m_docPropsCore, name);
}
void XDocument_setDocumentProperty(XDocument* self, const XString* name, const XString* property) {
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
bool XDocument_addSheet(XDocument* self, const XString* name, XAbstractSheet_SheetType type) {
    return self ? XWorkbook_addSheet(self->m_workbook, name, type) != NULL : false;
}
bool XDocument_insertSheet(XDocument* self, int index, const XString* name, XAbstractSheet_SheetType type) {
    return self ? XWorkbook_insertSheet(self->m_workbook, index, name, type) != NULL : false;
}
bool XDocument_selectSheet(XDocument* self, const XString* name) {
    if (!self || !self->m_workbook || !name) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && XString_equals(s->m_sheetName, name, XChar_CaseSensitive))
            return XWorkbook_setActiveSheet(self->m_workbook, (int)i);
    }
    return false;
}
bool XDocument_selectSheetByIndex(XDocument* self, int index) {
    return self ? XWorkbook_setActiveSheet(self->m_workbook, index) : false;
}
bool XDocument_renameSheet(XDocument* self, const XString* oldName, const XString* newName) {
    if (!self || !self->m_workbook || !oldName || !newName) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && XString_equals(s->m_sheetName, oldName, XChar_CaseSensitive))
            return XWorkbook_renameSheet(self->m_workbook, (int)i, newName);
    }
    return false;
}
bool XDocument_copySheet(XDocument* self, const XString* srcName, const XString* distName) {
    if (!self || !self->m_workbook || !srcName) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && XString_equals(s->m_sheetName, srcName, XChar_CaseSensitive))
            return XWorkbook_copySheet(self->m_workbook, (int)i, distName);
    }
    return false;
}
bool XDocument_moveSheet(XDocument* self, const XString* srcName, int distIndex) {
    if (!self || !self->m_workbook || !srcName) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && XString_equals(s->m_sheetName, srcName, XChar_CaseSensitive))
            return XWorkbook_moveSheet(self->m_workbook, (int)i, distIndex);
    }
    return false;
}
bool XDocument_deleteSheet(XDocument* self, const XString* name) {
    if (!self || !self->m_workbook || !name) return false;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && XString_equals(s->m_sheetName, name, XChar_CaseSensitive))
            return XWorkbook_deleteSheet(self->m_workbook, (int)i);
    }
    return false;
}

/* ========== 工作簿访问 ========== */
XWorkbook* XDocument_workbook(const XDocument* self) { return self ? self->m_workbook : NULL; }
XAbstractSheet* XDocument_sheet(const XDocument* self, const XString* sheetName) {
    if (!self || !self->m_workbook || !sheetName) return NULL;
    for (size_t i = 0; i < XVector_size_base(self->m_workbook->m_sheets); ++i) {
        XAbstractSheet* s = *(XAbstractSheet**)XVector_at_base(self->m_workbook->m_sheets, i);
        if (s->m_sheetName && XString_equals(s->m_sheetName, sheetName, XChar_CaseSensitive)) return s;
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
    if (!self->m_filePath || XString_size_base(self->m_filePath) == 0) return false;
    return XDocument_saveAs(self, self->m_filePath);
}

static bool documentSaveToZip(const XDocument* self, XZipWriter* zip) {
    if (!self || !zip || !self->m_workbook) return false;
    /* 获取工作簿和样式 */
    XWorkbook* wb = self->m_workbook;
    XStyles* styles = wb->m_styles;
    XSharedStrings* sharedStrings = wb->m_sharedStrings;
    
    /* ========== 创建 ContentTypes ========== */
    XContentTypes* contentTypes = XContentTypes_create();
    if (!contentTypes) return false;
    
    /* 添加默认类型 */
    addDefault_cstr(contentTypes, "rels", "application/vnd.openxmlformats-package.relationships+xml");
    addDefault_cstr(contentTypes, "xml", "application/xml");
    
    /* 添加工作表、Chartsheet、绘图和图表部件。 */
    int sheetCount = XWorkbook_sheetCount(wb);
    if (sheetCount <= 0) {
        XContentTypes_delete(contentTypes);
        return false;
    }
    for (int i = 0; i < sheetCount; i++) {
        XAbstractSheet* sheet = XWorkbook_sheet(wb, i);
        if (!sheet) continue;
        char partName[128];
        if (sheet->m_sheetType == XAbstractSheet_ST_ChartSheet) {
            snprintf(partName, sizeof(partName), "xl/chartsheets/sheet%d.xml", i + 1);
            XString_Init_Utf8(name, partName);
            XContentTypes_addChartsheetName(contentTypes, name);
            XString_deinit_base(name);
            snprintf(partName, sizeof(partName), "xl/drawings/drawing%d.xml", i + 1);
            XString_Init_Utf8(drawingName, partName);
            XContentTypes_addDrawingName(contentTypes, drawingName);
            XString_deinit_base(drawingName);
            if (((XChartsheet*)sheet)->m_chart) XContentTypes_addChartName(contentTypes, NULL);
        } else {
            snprintf(partName, sizeof(partName), "xl/worksheets/sheet%d.xml", i + 1);
            XString_Init_Utf8(name, partName);
            XContentTypes_addWorksheetName(contentTypes, name);
            XString_deinit_base(name);
            XWorksheet* worksheet = (XWorksheet*)sheet;
            size_t chartCount = worksheet->m_chartFiles
                ? XVector_size_base((XContainer*)worksheet->m_chartFiles) : 0;
            if (XWorksheet_getImageCount(worksheet) > 0 || chartCount > 0) {
                snprintf(partName, sizeof(partName), "xl/drawings/drawing%d.xml", i + 1);
                XString_Init_Utf8(drawingName, partName);
                XContentTypes_addDrawingName(contentTypes, drawingName);
                XString_deinit_base(drawingName);
            }
            for (size_t j = 0; j < chartCount; ++j)
                XContentTypes_addChartName(contentTypes, NULL);
        }
    }
    
    /* 添加覆盖类型 */
    addOverride_cstr(contentTypes, "/docProps/core.xml", "application/vnd.openxmlformats-package.core-properties+xml");
    addOverride_cstr(contentTypes, "/docProps/app.xml", "application/vnd.openxmlformats-officedocument.extended-properties+xml");
    addOverride_cstr(contentTypes, "/xl/workbook.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml");
    addOverride_cstr(contentTypes, "/xl/styles.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml");
    addOverride_cstr(contentTypes, "/xl/theme/theme1.xml", "application/vnd.openxmlformats-officedocument.theme+xml");
    
    /* 添加共享字符串 */
    if (!XSharedStrings_isEmpty(sharedStrings)) {
        addOverride_cstr(contentTypes, "/xl/sharedStrings.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml");
    }

    /* 添加图片内容类型；绘图 Override 已在上面按实际路径注册。 */
    for (int i = 0; i < sheetCount; ++i) {
        XAbstractSheet* sheet = XWorkbook_sheet(wb, i);
        if (!sheet || sheet->m_sheetType != XAbstractSheet_ST_WorkSheet) continue;
        XWorksheet* ws = (XWorksheet*)sheet;
        if (XWorksheet_getImageCount(ws) == 0) continue;
        size_t mediaCount = XVector_size_base((XContainer*)ws->m_mediaFiles);
        for (size_t j = 0; j < mediaCount; ++j) {
            XMediaFile* media = *(XMediaFile**)XVector_at_base(ws->m_mediaFiles, j);
            if (media && XMediaFile_suffix(media) && XMediaFile_mimeType(media))
                XContentTypes_addDefault(contentTypes, XMediaFile_suffix(media), XMediaFile_mimeType(media));
        }
    }
    
    /* 写入 [Content_Types].xml */
    uint8_t* ctData = NULL;
    size_t ctLen = 0;
    bool partOk = XContentTypes_saveToXmlData(contentTypes, &ctData, &ctLen) &&
        zipAddFile_cstr(zip, "[Content_Types].xml", ctData, ctLen);
    if (ctData) XFree_System(ctData);
    XContentTypes_delete(contentTypes);
    if (!partOk) return false;
    
    /* ========== 创建根目录 .rels ========== */
    XRelationships rootRels;
    /* 初始化 */
    memset(&rootRels, 0, sizeof(rootRels));
    rootRels.m_relationships = XVector_Create(XlsxRelationship);
    if (!rootRels.m_relationships) return false;
    
    addDocRel_cstr(&rootRels, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument", "xl/workbook.xml");
    addPkgRel_cstr(&rootRels, "http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties", "docProps/core.xml");
    addDocRel_cstr(&rootRels, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties", "docProps/app.xml");
    
    uint8_t* relData = NULL;
    size_t relLen = 0;
    partOk = XRelationships_count(&rootRels) == 3 &&
        XRelationships_saveToXmlData(&rootRels, &relData, &relLen) &&
        zipAddFile_cstr(zip, "_rels/.rels", relData, relLen);
    if (relData) XFree_System(relData);
    /* 手动清理，不释放栈变量 */ XRelationships_clear(&rootRels); if (rootRels.m_relationships) { XVector_delete_base(rootRels.m_relationships); rootRels.m_relationships = NULL; }
    if (!partOk) return false;
    
    /* ========== 创建 xl/_rels/workbook.xml.rels ========== */
    XRelationships workbookRels;
    memset(&workbookRels, 0, sizeof(workbookRels));
    workbookRels.m_relationships = XVector_Create(XlsxRelationship);
    if (!workbookRels.m_relationships) return false;
    
    addDocRel_cstr(&workbookRels, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles", "styles.xml");
    if (!XSharedStrings_isEmpty(sharedStrings)) {
        addDocRel_cstr(&workbookRels, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings", "sharedStrings.xml");
    }
    addDocRel_cstr(&workbookRels, "http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme", "theme/theme1.xml");
    
    /* 添加工作表关系，并回填 rId 到 XAbstractSheet，使 workbook.xml 的 r:id 与 rels 一致 */
    int nextImageIndex = 1;
    int nextChartIndex = 1;
    for (int i = 0; i < sheetCount; i++) {
        XAbstractSheet* sheet = XWorkbook_sheet(wb, i);
        if (!sheet) continue;
        char sheetPath[96];
        const char* relationType = "worksheet";
        if (sheet->m_sheetType == XAbstractSheet_ST_ChartSheet) {
            snprintf(sheetPath, sizeof(sheetPath), "chartsheets/sheet%d.xml", i + 1);
            relationType = "chartsheet";
        } else {
            snprintf(sheetPath, sizeof(sheetPath), "worksheets/sheet%d.xml", i + 1);
        }
        XString_Init_Utf8(type, relationType);
        XString_Init_Utf8(target, sheetPath);
        XRelationships_addDocumentRelationship(&workbookRels, type, target);
        XString_deinit_base(type);
        XString_deinit_base(target);
        /* 将本次分配的 rId 写回 XAbstractSheet.m_rid */
        XAbstractSheet_setRid(sheet, XRelationships_lastAssignedRidFor(&workbookRels));
    }
    
    relData = NULL;
    relLen = 0;
    int expectedWorkbookRelationships = sheetCount + 2 +
        (!XSharedStrings_isEmpty(sharedStrings) ? 1 : 0);
    partOk = XRelationships_count(&workbookRels) == expectedWorkbookRelationships &&
        XRelationships_saveToXmlData(&workbookRels, &relData, &relLen) &&
        zipAddFile_cstr(zip, "xl/_rels/workbook.xml.rels", relData, relLen);
    if (relData) XFree_System(relData);
    /* 手动清理，不释放栈变量 */ XRelationships_clear(&workbookRels); if (workbookRels.m_relationships) { XVector_delete_base(workbookRels.m_relationships); workbookRels.m_relationships = NULL; }
    if (!partOk) return false;
    
    /* ========== 写入 xl/workbook.xml ========== */
    uint8_t* wbData = NULL;
    size_t wbLen = 0;
    partOk = XWorkbook_saveToXmlData(wb, &wbData, &wbLen) &&
        zipAddFile_cstr(zip, "xl/workbook.xml", wbData, wbLen);
    if (wbData) XFree_System(wbData);
    if (!partOk) return false;
    
    /* ========== 写入 xl/styles.xml ========== */
    uint8_t* styleData = NULL;
    size_t styleLen = 0;
    partOk = XStyles_saveToXmlData(styles, &styleData, &styleLen) &&
        zipAddFile_cstr(zip, "xl/styles.xml", styleData, styleLen);
    if (styleData) XFree_System(styleData);
    if (!partOk) return false;
    
    /* ========== 写入 xl/sharedStrings.xml ========== */
    if (!XSharedStrings_isEmpty(sharedStrings)) {
        uint8_t* ssData = NULL;
        size_t ssLen = 0;
        partOk = XSharedStrings_saveToXmlData(sharedStrings, &ssData, &ssLen) &&
            zipAddFile_cstr(zip, "xl/sharedStrings.xml", ssData, ssLen);
        if (ssData) XFree_System(ssData);
        if (!partOk) return false;
    }
    
    /* ========== 写入工作表 xl/worksheets/sheetN.xml ========== */
    for (int i = 0; i < sheetCount; i++) {
        XAbstractSheet* sheet = XWorkbook_sheet(wb, i);
        if (!sheet) continue;
        XWorksheet* ws = sheet->m_sheetType == XAbstractSheet_ST_WorkSheet
            ? (XWorksheet*)sheet : NULL;
        XChartsheet* chartsheet = sheet->m_sheetType == XAbstractSheet_ST_ChartSheet
            ? (XChartsheet*)sheet : NULL;
        uint8_t* sheetData = NULL;
        size_t sheetLength = 0;
        char sheetPath[96];
        bool sheetOk = false;
        if (ws) {
            sheetOk = XWorksheet_saveToXmlData(ws, &sheetData, &sheetLength);
            snprintf(sheetPath, sizeof(sheetPath), "xl/worksheets/sheet%d.xml", i + 1);
        } else if (chartsheet) {
            sheetOk = XChartsheet_saveToXmlData(chartsheet, &sheetData, &sheetLength);
            snprintf(sheetPath, sizeof(sheetPath), "xl/chartsheets/sheet%d.xml", i + 1);
        }
        if (!sheetOk || !zipAddFile_cstr(zip, sheetPath, sheetData, sheetLength)) {
            if (sheetData) XFree_System(sheetData);
            return false;
        }
        XFree_System(sheetData);
        
        /* 写入工作表关系 (空的) */
        XRelationships wsRels;
        memset(&wsRels, 0, sizeof(wsRels));
        wsRels.m_relationships = XVector_Create(XlsxRelationship);
        size_t worksheetChartCount = ws && ws->m_chartFiles
            ? XVector_size_base((XContainer*)ws->m_chartFiles) : 0;
        if ((ws && (XWorksheet_getImageCount(ws) > 0 || worksheetChartCount > 0)) ||
            (chartsheet && chartsheet->m_chart)) {
            char drawingTarget[128];
            snprintf(drawingTarget, sizeof(drawingTarget), "../drawings/drawing%d.xml", i + 1);
            addDocRel_cstr(&wsRels,
                "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing",
                drawingTarget);
        }
        if (ws && ws->m_hyperlinks) {
            size_t hyperlinkCount = XVector_size_base((XContainer*)ws->m_hyperlinks);
            for (size_t j = 0; j < hyperlinkCount; ++j) {
                XWorksheet_Hyperlink* hyperlink =
                    (XWorksheet_Hyperlink*)XVector_at_base(ws->m_hyperlinks, j);
                const char* url = hyperlink && hyperlink->m_url
                    ? XString_toUtf8(hyperlink->m_url) : NULL;
                if (url && url[0] && url[0] != '#')
                    addWorksheetRel_cstr(&wsRels, "hyperlink", url, "External");
            }
        }
        
        relData = NULL;
        relLen = 0;
        bool relationSerialized = XRelationships_saveToXmlData(&wsRels, &relData, &relLen);
        if (relationSerialized) {
            char wsRelPath[96];
            snprintf(wsRelPath, sizeof(wsRelPath), chartsheet
                ? "xl/chartsheets/_rels/sheet%d.xml.rels"
                : "xl/worksheets/_rels/sheet%d.xml.rels", i + 1);
            bool relationOk = zipAddFile_cstr(zip, wsRelPath, relData, relLen);
            XFree_System(relData);
            if (!relationOk) {
                XRelationships_clear(&wsRels);
                if (wsRels.m_relationships) XVector_delete_base(wsRels.m_relationships);
                return false;
            }
        }
        /* 手动清理，不释放栈变量 */ XRelationships_clear(&wsRels); if (wsRels.m_relationships) { XVector_delete_base(wsRels.m_relationships); wsRels.m_relationships = NULL; }
        if (!relationSerialized) return false;
        if (ws) {
            if (!saveWorksheetDrawing(zip, ws, i + 1, &nextImageIndex, &nextChartIndex))
                return false;
        } else if (chartsheet && chartsheet->m_chart) {
            if (!saveChartsheetDrawing(zip, chartsheet, i + 1, nextChartIndex++))
                return false;
        }
    }
    
    /* ========== 写入 xl/theme/theme1.xml ========== */
    const XString* customTheme = wb->m_theme
        ? XTheme_themeXmlData(wb->m_theme) : NULL;
    if (customTheme && !XString_isEmpty_base(customTheme)) {
        uint8_t* themeData = NULL;
        size_t themeLength = 0;
        bool themeOk = XTheme_saveToXmlData(wb->m_theme, &themeData, &themeLength) &&
            themeLength > 0 &&
            zipAddFile_cstr(zip, "xl/theme/theme1.xml", themeData, themeLength);
        if (themeData) XFree_System(themeData);
        if (!themeOk) return false;
    } else {
    XByteArray* themeBuf = XByteArray_create();
    if (!themeBuf) return false;
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
    XByteArray_append_utf8(themeBuf, "      <a:fillStyleLst>\n");
    XByteArray_append_utf8(themeBuf, "        <a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>\n");
    XByteArray_append_utf8(themeBuf, "        <a:gradFill rotWithShape=\"1\"><a:gsLst><a:gs pos=\"0\"><a:schemeClr val=\"phClr\"/></a:gs><a:gs pos=\"100000\"><a:schemeClr val=\"phClr\"/></a:gs></a:gsLst><a:lin ang=\"5400000\" scaled=\"0\"/></a:gradFill>\n");
    XByteArray_append_utf8(themeBuf, "        <a:gradFill rotWithShape=\"1\"><a:gsLst><a:gs pos=\"0\"><a:schemeClr val=\"phClr\"/></a:gs><a:gs pos=\"100000\"><a:schemeClr val=\"phClr\"/></a:gs></a:gsLst><a:path path=\"circle\"><a:fillToRect l=\"0\" t=\"0\" r=\"0\" b=\"0\"/></a:path></a:gradFill>\n");
    XByteArray_append_utf8(themeBuf, "      </a:fillStyleLst>\n");
    XByteArray_append_utf8(themeBuf, "      <a:lnStyleLst>\n");
    XByteArray_append_utf8(themeBuf, "        <a:ln w=\"9525\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>\n");
    XByteArray_append_utf8(themeBuf, "        <a:ln w=\"25400\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>\n");
    XByteArray_append_utf8(themeBuf, "        <a:ln w=\"38100\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/></a:ln>\n");
    XByteArray_append_utf8(themeBuf, "      </a:lnStyleLst>\n");
    XByteArray_append_utf8(themeBuf, "      <a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst>\n");
    XByteArray_append_utf8(themeBuf, "      <a:bgFillStyleLst>\n");
    XByteArray_append_utf8(themeBuf, "        <a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>\n");
    XByteArray_append_utf8(themeBuf, "        <a:gradFill rotWithShape=\"1\"><a:gsLst><a:gs pos=\"0\"><a:schemeClr val=\"phClr\"/></a:gs><a:gs pos=\"100000\"><a:schemeClr val=\"phClr\"/></a:gs></a:gsLst><a:lin ang=\"5400000\" scaled=\"0\"/></a:gradFill>\n");
    XByteArray_append_utf8(themeBuf, "        <a:gradFill rotWithShape=\"1\"><a:gsLst><a:gs pos=\"0\"><a:schemeClr val=\"phClr\"/></a:gs><a:gs pos=\"100000\"><a:schemeClr val=\"phClr\"/></a:gs></a:gsLst><a:path path=\"circle\"><a:fillToRect l=\"0\" t=\"0\" r=\"0\" b=\"0\"/></a:path></a:gradFill>\n");
    XByteArray_append_utf8(themeBuf, "      </a:bgFillStyleLst>\n");
    XByteArray_append_utf8(themeBuf, "    </a:fmtScheme>\n");
    XByteArray_append_utf8(themeBuf, "  </a:themeElements>\n");
    XByteArray_append_utf8(themeBuf, "</a:theme>\n");
    
    partOk = zipAddFile_cstr(zip, "xl/theme/theme1.xml",
        (const uint8_t*)XByteArray_data(themeBuf),
        XByteArray_size_base((XContainer*)themeBuf));
    XByteArray_delete_base(themeBuf);
    if (!partOk) return false;
    }
    
    /* ========== 写入文档属性 ========== */
    uint8_t* propertyData = NULL;
    size_t propertyLength = 0;
    if (!XDocPropsCore_saveToXmlData(self->m_docPropsCore, &propertyData, &propertyLength) ||
        !zipAddFile_cstr(zip, "docProps/core.xml", propertyData, propertyLength)) {
        if (propertyData) XFree_System(propertyData);
        return false;
    }
    XFree_System(propertyData);

    XDocPropsApp* appProperties = ((XDocument*)self)->m_docPropsApp;
    XDocPropsApp_clearParts(appProperties);
    int worksheetCount = 0;
    int chartsheetCount = 0;
    for (int i = 0; i < sheetCount; ++i) {
        XAbstractSheet* sheet = XWorkbook_sheet(wb, i);
        if (!sheet) continue;
        if (sheet->m_sheetType == XAbstractSheet_ST_ChartSheet) ++chartsheetCount;
        else ++worksheetCount;
    }
    if (worksheetCount > 0) {
        XString_Init_Utf8(worksheetHeading, "Worksheets");
        XDocPropsApp_addHeadingPair(appProperties, worksheetHeading, worksheetCount);
        XString_deinit_base(worksheetHeading);
    }
    if (chartsheetCount > 0) {
        XString_Init_Utf8(chartHeading, "Charts");
        XDocPropsApp_addHeadingPair(appProperties, chartHeading, chartsheetCount);
        XString_deinit_base(chartHeading);
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < sheetCount; ++i) {
            XAbstractSheet* sheet = XWorkbook_sheet(wb, i);
            bool isChartsheet = sheet &&
                sheet->m_sheetType == XAbstractSheet_ST_ChartSheet;
            if (sheet && sheet->m_sheetName && isChartsheet == (pass == 1))
                XDocPropsApp_addPartTitle(appProperties, sheet->m_sheetName);
        }
    }
    propertyData = NULL;
    propertyLength = 0;
    if (!XDocPropsApp_saveToXmlData(appProperties, &propertyData, &propertyLength) ||
        !zipAddFile_cstr(zip, "docProps/app.xml", propertyData, propertyLength)) {
        if (propertyData) XFree_System(propertyData);
        return false;
    }
    XFree_System(propertyData);
    
    return true;
}

bool XDocument_saveAs(const XDocument* self, const XString* xlsxName) {
    if (!self || !xlsxName || !self->m_workbook) return false;
    XZipWriter* zip = XZipWriter_create(xlsxName);
    if (!zip) return false;
    bool packageComplete = documentSaveToZip(self, zip);
    bool result = packageComplete && XZipWriter_close(zip);
    if (!packageComplete) zip->m_closeAttempted = true;
    XZipWriter_delete(zip);
    if (result) {
        XDocument* mutableSelf = (XDocument*)self;
        if (!mutableSelf->m_filePath) mutableSelf->m_filePath = XString_create();
        if (mutableSelf->m_filePath) {
            XString_clear_base(mutableSelf->m_filePath);
            XString_append(mutableSelf->m_filePath, xlsxName);
        }
        mutableSelf->m_isModified = false;
    }
    return result;
}

bool XDocument_isLoadPackage(const XDocument* self) {
    return self ? self->m_isLoaded : false;
}

/* 前向声明：find_sheet_path_in_zip 定义在后面 */
static char* find_sheet_path_in_zip(const char* zipPath, const XString* sheetName, int sheetIndex);

/* ========== 辅助：从 workbook.xml 解析所有 sheet 名称 ========== */
typedef struct {
    char names[64][256];
    int count;
} SheetNameList;

static XByteArray* zipFileDataUtf8(const XZipReader* zip, const char* path)
{
    if (!zip || !path) return NULL;
    XString* pathString = XString_create_utf8(path);
    if (!pathString) return NULL;
    XByteArray* result = XZipReader_fileData(zip, pathString);
    XString_delete_base(pathString);
    return result;
}

static XString* sheetPathFromReader(const XZipReader* zip, const XAbstractSheet* sheet)
{
    if (!zip || !sheet || sheet->m_rid < 0) return NULL;
    XByteArray* relXml = zipFileDataUtf8(zip, "xl/_rels/workbook.xml.rels");
    if (!relXml) return NULL;
    XRelationships* relationships = XRelationships_create();
    if (!relationships) {
        XByteArray_delete_base(relXml);
        return NULL;
    }
    bool loaded = XRelationships_loadFromXmlData(relationships, XByteArray_data(relXml),
        XByteArray_size_base(relXml));
    XByteArray_delete_base(relXml);
    if (!loaded) {
        XRelationships_delete(relationships);
        return NULL;
    }

    XString* rid = XString_create_fmt_utf8("rId%d", sheet->m_rid);
    XlsxRelationship* relationship = XRelationships_getRelationshipById(relationships, rid);
    XString_delete_base(rid);
    XString* result = NULL;
    if (relationship && relationship->m_target) {
        const char* target = XString_toUtf8(relationship->m_target);
        if (relationship->m_target &&
            XString_startsWith_utf8(relationship->m_target, "/", XChar_CaseSensitive))
            result = XString_create_utf8(target + 1);
        else if (relationship->m_target &&
                 XString_startsWith_utf8(relationship->m_target, "xl/", XChar_CaseSensitive)) {
            result = XString_create_utf8(target);
        }
        else if (target) result = XString_create_fmt_utf8("xl/%s", target);
    }
    XRelationships_delete(relationships);
    return result;
}

static bool alignWorkbookSheetTypesFromRelationships(XWorkbook* workbook,
                                                      const XZipReader* zip)
{
    if (!workbook || !zip) return false;
    XByteArray* xml = zipFileDataUtf8(zip, "xl/_rels/workbook.xml.rels");
    if (!xml) return false;
    XRelationships* relationships = XRelationships_create();
    bool ok = relationships && XRelationships_loadFromXmlData(relationships,
        XByteArray_data(xml), XByteArray_size_base((XContainer*)xml));
    XByteArray_delete_base(xml);
    if (!ok) {
        if (relationships) XRelationships_delete(relationships);
        return false;
    }
    int sheetCount = XWorkbook_sheetCount(workbook);
    for (int i = 0; i < sheetCount; ++i) {
        XAbstractSheet* oldSheet = XWorkbook_sheet(workbook, i);
        if (!oldSheet) continue;
        XString* rid = XString_create_fmt_utf8("rId%d", oldSheet->m_rid);
        XlsxRelationship* relationship = rid
            ? XRelationships_getRelationshipById(relationships, rid) : NULL;
        if (rid) XString_delete_base(rid);
        bool isChartsheet = relationship && relationship->m_type &&
            XString_endsWith_utf8(relationship->m_type, "chartsheet", XChar_CaseSensitive);
        XAbstractSheet_SheetType expected = isChartsheet
            ? XAbstractSheet_ST_ChartSheet : XAbstractSheet_ST_WorkSheet;
        if (oldSheet->m_sheetType == expected) continue;
        XAbstractSheet* replacement = NULL;
        if (expected == XAbstractSheet_ST_ChartSheet) {
            XChartsheet* chartsheet = XChartsheet_create(oldSheet->m_sheetName,
                oldSheet->m_sheetId, workbook, XAbstractOOXmlFile_F_LoadFromExists);
            replacement = chartsheet ? &chartsheet->m_base : NULL;
        } else {
            replacement = (XAbstractSheet*)XWorksheet_create(oldSheet->m_sheetName,
                oldSheet->m_sheetId, workbook, XAbstractOOXmlFile_F_LoadFromExists);
        }
        if (!replacement) { ok = false; break; }
        replacement->m_rid = oldSheet->m_rid;
        replacement->m_sheetState = oldSheet->m_sheetState;
        *(XAbstractSheet**)XVector_at_base(workbook->m_sheets, (size_t)i) = replacement;
        if (oldSheet->m_sheetType == XAbstractSheet_ST_ChartSheet)
            XChartsheet_delete((XChartsheet*)oldSheet);
        else XWorksheet_delete((XWorksheet*)oldSheet);
    }
    XRelationships_delete(relationships);
    return ok;
}

static bool loadSharedStringsFromReader(const XZipReader* zip, XStringList* list)
{
    XByteArray* xml = zipFileDataUtf8(zip, "xl/sharedStrings.xml");
    if (!xml) return true;
    bool result = XReadSax_loadSharedStringsXml(XByteArray_data(xml),
        XByteArray_size_base(xml), list);
    XByteArray_delete_base(xml);
    return result;
}

static bool resolveWorksheetSharedStrings(XWorksheet* worksheet, const XStringList* strings)
{
    if (!worksheet || !strings) return false;
    XCellLocation* cells = NULL;
    int maxRow = 0;
    int maxColumn = 0;
    int count = XWorksheet_getFullCells(worksheet, &cells, &maxRow, &maxColumn);
    (void)maxRow;
    (void)maxColumn;
    if (count < 0) return false;
    for (int i = 0; i < count; ++i) {
        XCell* cell = cells[i].m_cell;
        if (!cell || cell->m_cellType != XCell_SharedStringType || !cell->m_value) continue;
        const char* indexText = XString_toUtf8(cell->m_value);
        char* end = NULL;
        long index = indexText ? strtol(indexText, &end, 10) : -1;
        if (!end || *end != '\0' || index < 0 ||
            (size_t)index >= XStringList_size_base((XContainer*)strings)) {
            XFree_System(cells);
            return false;
        }
        XString* value = (XString*)XStringList_at_base((XVector*)strings, (size_t)index);
        XCell_setValue(cell, value);
    }
    if (cells) XFree_System(cells);
    return true;
}

static const char* image_mime_from_suffix(const XString* suffix)
{
    if (!suffix || XString_equals_utf8(suffix, "png", XChar_CaseInsensitive)) return "image/png";
    if (XString_equals_utf8(suffix, "jpg", XChar_CaseInsensitive) ||
        XString_equals_utf8(suffix, "jpeg", XChar_CaseInsensitive)) return "image/jpeg";
    if (XString_equals_utf8(suffix, "gif", XChar_CaseInsensitive)) return "image/gif";
    if (XString_equals_utf8(suffix, "bmp", XChar_CaseInsensitive)) return "image/bmp";
    return NULL;
}

static bool append_loaded_image(XWorksheet* worksheet, int row, int column,
                                const uint8_t* data, size_t size, const XString* suffix)
{
    const char* mime = image_mime_from_suffix(suffix);
    if (!worksheet || row <= 0 || column <= 0 || !data || size == 0 || !mime) return false;
    XString_Init_Utf8(mimeString, mime);
    XMediaFile* media = XMediaFile_create_data(data, size, suffix, mimeString);
    XString_deinit_base(mimeString);
    if (!media) return false;
    XWorksheet_ImagePosition position = { row, column };
    if (!XVector_push_back_2(worksheet->m_mediaFiles, &media, 1)) {
        XMediaFile_delete(media);
        return false;
    }
    if (!XVector_push_back_2(worksheet->m_imagePositions, &position, 1)) {
        XVector_pop_back_base(worksheet->m_mediaFiles);
        XMediaFile_delete(media);
        return false;
    }
    return true;
}

static XRelationships* loadPartRelationships(const XZipReader* zip, const XString* partPath)
{
    if (!zip || !partPath) return NULL;
    const char* part = XString_toUtf8(partPath);
    const char* slash = part ? strrchr(part, '/') : NULL;
    if (!part || !slash || !slash[1]) return NULL;
    char relationshipPath[512];
    size_t directoryLength = (size_t)(slash - part);
    if (directoryLength + strlen(slash + 1) + 14 >= sizeof(relationshipPath)) return NULL;
    memcpy(relationshipPath, part, directoryLength);
    relationshipPath[directoryLength] = '\0';
    snprintf(relationshipPath + directoryLength,
        sizeof(relationshipPath) - directoryLength, "/_rels/%s.rels", slash + 1);
    XByteArray* xml = zipFileDataUtf8(zip, relationshipPath);
    if (!xml) return NULL;
    XRelationships* relationships = XRelationships_create();
    bool ok = relationships && XRelationships_loadFromXmlData(relationships,
        XByteArray_data(xml), XByteArray_size_base((XContainer*)xml));
    XByteArray_delete_base(xml);
    if (!ok) {
        if (relationships) XRelationships_delete(relationships);
        return NULL;
    }
    return relationships;
}

static XString* resolvePartTarget(const XString* sourcePart, const XString* target)
{
    const char* source = sourcePart ? XString_toUtf8(sourcePart) : NULL;
    XString* relative = target ? XString_create_copy(target) : NULL;
    if (!source || !relative || XString_size_base(relative) == 0) {
        if (relative) XString_delete_base(relative);
        return NULL;
    }
    const char* relativeText = XString_toUtf8(relative);
    if (XString_startsWith_utf8(relative, "/", XChar_CaseSensitive)) {
        XString* result = XString_create_utf8(relativeText + 1);
        XString_delete_base(relative);
        return result;
    }
    const char* slash = strrchr(source, '/');
    size_t directoryLength = slash ? (size_t)(slash - source) : 0;
    char directory[512];
    if (directoryLength >= sizeof(directory)) {
        XString_delete_base(relative);
        return NULL;
    }
    memcpy(directory, source, directoryLength);
    directory[directoryLength] = '\0';
    while (XString_startsWith_utf8(relative, "../", XChar_CaseSensitive)) {
        char* parent = strrchr(directory, '/');
        if (parent) *parent = '\0';
        else directory[0] = '\0';
        if (!XString_slice(relative, 3)) {
            XString_delete_base(relative);
            return NULL;
        }
    }
    while (XString_startsWith_utf8(relative, "./", XChar_CaseSensitive)) {
        if (!XString_slice(relative, 2)) {
            XString_delete_base(relative);
            return NULL;
        }
    }
    relativeText = XString_toUtf8(relative);
    char result[1024];
    if (directory[0]) snprintf(result, sizeof(result), "%s/%s", directory, relativeText ? relativeText : "");
    else snprintf(result, sizeof(result), "%s", relativeText ? relativeText : "");
    XString_delete_base(relative);
    return XString_create_utf8(result);
}

static XString* relatedPartPath(const XZipReader* zip, const XString* sourcePart,
                                const char* relativeType)
{
    XRelationships* relationships = loadPartRelationships(zip, sourcePart);
    if (!relationships) return NULL;
    XString_Init_Utf8(type, relativeType);
    int count = 0;
    XlsxRelationship** matches = XRelationships_documentRelationships(
        relationships, type, &count);
    XString_deinit_base(type);
    XString* result = count > 0 && matches[0] && matches[0]->m_target
        ? resolvePartTarget(sourcePart, matches[0]->m_target) : NULL;
    if (matches) XFree_System(matches);
    XRelationships_delete(relationships);
    return result;
}

static bool loadChartsheetFromReader(const XZipReader* zip, XChartsheet* chartsheet,
                                     const XString* sheetPath)
{
    if (!zip || !chartsheet || !sheetPath) return false;
    XByteArray* sheetXml = XZipReader_fileData(zip, sheetPath);
    if (!sheetXml) return false;
    bool ok = XChartsheet_loadFromXmlData(chartsheet, XByteArray_data(sheetXml),
        XByteArray_size_base((XContainer*)sheetXml));
    XByteArray_delete_base(sheetXml);
    if (!ok) return false;
    XString* drawingPath = relatedPartPath(zip, sheetPath, "drawing");
    if (!drawingPath) return false;
    XByteArray* drawingXml = XZipReader_fileData(zip, drawingPath);
    XString* chartPath = drawingXml ? relatedPartPath(zip, drawingPath, "chart") : NULL;
    if (drawingXml) XByteArray_delete_base(drawingXml);
    XString_delete_base(drawingPath);
    if (!chartPath) return false;
    XByteArray* chartXml = XZipReader_fileData(zip, chartPath);
    XString_delete_base(chartPath);
    if (!chartXml) return false;
    XChart* chart = XChart_create(&chartsheet->m_base,
        XAbstractOOXmlFile_F_LoadFromExists);
    ok = chart && XChart_loadFromXmlData(chart, XByteArray_data(chartXml),
        XByteArray_size_base((XContainer*)chartXml));
    XByteArray_delete_base(chartXml);
    if (!ok) {
        if (chart) XChart_delete(chart);
        return false;
    }
    if (chartsheet->m_ownsChart && chartsheet->m_chart) XChart_delete(chartsheet->m_chart);
    chartsheet->m_chart = chart;
    chartsheet->m_ownsChart = true;
    return true;
}

static bool resolveWorksheetHyperlinks(const XZipReader* zip, XWorksheet* worksheet,
                                       const XString* sheetPath)
{
    if (!worksheet || !worksheet->m_hyperlinks ||
        XVector_size_base((XContainer*)worksheet->m_hyperlinks) == 0) return true;
    bool needsRelationships = false;
    size_t count = XVector_size_base((XContainer*)worksheet->m_hyperlinks);
    for (size_t i = 0; i < count; ++i) {
        XWorksheet_Hyperlink* hyperlink =
            (XWorksheet_Hyperlink*)XVector_at_base(worksheet->m_hyperlinks, i);
        if (hyperlink && hyperlink->m_relationshipId) { needsRelationships = true; break; }
    }
    if (!needsRelationships) return true;
    XRelationships* relationships = loadPartRelationships(zip, sheetPath);
    if (!relationships) return false;
    bool ok = true;
    for (size_t i = 0; i < count; ++i) {
        XWorksheet_Hyperlink* hyperlink =
            (XWorksheet_Hyperlink*)XVector_at_base(worksheet->m_hyperlinks, i);
        if (!hyperlink || !hyperlink->m_relationshipId) continue;
        XlsxRelationship* relationship =
            XRelationships_getRelationshipById(relationships, hyperlink->m_relationshipId);
        if (!relationship || !relationship->m_target) { ok = false; continue; }
        if (!hyperlink->m_url) hyperlink->m_url = XString_create();
        if (!hyperlink->m_url) { ok = false; continue; }
        XString_clear_base(hyperlink->m_url);
        XString_append(hyperlink->m_url, relationship->m_target);
    }
    XRelationships_delete(relationships);
    return ok;
}

static bool loadWorksheetImagesFromReader(const XZipReader* zip, XWorksheet* worksheet,
                                          int sheetIndex)
{
    char drawingPath[96];
    char relationshipsPath[128];
    snprintf(drawingPath, sizeof(drawingPath), "xl/drawings/drawing%d.xml", sheetIndex + 1);
    snprintf(relationshipsPath, sizeof(relationshipsPath),
             "xl/drawings/_rels/drawing%d.xml.rels", sheetIndex + 1);
    XByteArray* drawingXml = zipFileDataUtf8(zip, drawingPath);
    if (!drawingXml) return true;
    XByteArray* relationshipsXml = zipFileDataUtf8(zip, relationshipsPath);
    if (!relationshipsXml) {
        XByteArray_delete_base(drawingXml);
        return false;
    }
    XRelationships* relationships = XRelationships_create();
    bool ok = relationships && XRelationships_loadFromXmlData(relationships,
        XByteArray_data(relationshipsXml), XByteArray_size_base((XContainer*)relationshipsXml));
    XByteArray_delete_base(relationshipsXml);
    if (!ok) {
        if (relationships) XRelationships_delete(relationships);
        XByteArray_delete_base(drawingXml);
        return false;
    }

    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!reader) {
        XRelationships_delete(relationships);
        XByteArray_delete_base(drawingXml);
        return false;
    }
    XXmlStreamReader_addData(reader, drawingXml);
    XByteArray_delete_base(drawingXml);
    int row = 0;
    int column = 0;
    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        if (token != XXmlStream_StartElement) continue;
        const XString* name = XXmlStreamReader_name_const(reader);
        if (!name) continue;
        if (XString_equals_utf8(name, "oneCellAnchor", XChar_CaseSensitive)) {
            row = 0;
            column = 0;
        } else if (XString_equals_utf8(name, "col", XChar_CaseSensitive)) {
            const XString* value = XXmlStreamReader_readElementText_const(reader,
                XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
            if (value) column = atoi(XString_toUtf8(value)) + 1;
        } else if (XString_equals_utf8(name, "row", XChar_CaseSensitive)) {
            const XString* value = XXmlStreamReader_readElementText_const(reader,
                XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
            if (value) row = atoi(XString_toUtf8(value)) + 1;
        } else if (XString_equals_utf8(name, "blip", XChar_CaseSensitive)) {
            XString_Init_Utf8(embedName, "r:embed");
            const XString* rid = XXmlStreamAttributes_value(
                XXmlStreamReader_attributes(reader), embedName);
            XString_deinit_base(embedName);
            XlsxRelationship* relationship = rid
                ? XRelationships_getRelationshipById(relationships, rid) : NULL;
            const char* target = relationship && relationship->m_target
                ? XString_toUtf8(relationship->m_target) : NULL;
            if (!target) { ok = false; break; }
            char mediaPath[160];
            if (relationship->m_target &&
                XString_startsWith_utf8(relationship->m_target, "../", XChar_CaseSensitive))
                snprintf(mediaPath, sizeof(mediaPath), "xl/%s", target + 3);
            else if (XString_startsWith_utf8(relationship->m_target, "/", XChar_CaseSensitive))
                snprintf(mediaPath, sizeof(mediaPath), "%s", target + 1);
            else
                snprintf(mediaPath, sizeof(mediaPath), "xl/drawings/%s", target);
            const char* dot = strrchr(mediaPath, '.');
            XByteArray* mediaData = zipFileDataUtf8(zip, mediaPath);
            XString* suffix = dot ? XString_create_utf8(dot + 1) : NULL;
            if (!mediaData || !dot || !suffix || !append_loaded_image(worksheet, row, column,
                    XByteArray_data(mediaData), XByteArray_size_base((XContainer*)mediaData), suffix)) {
                if (suffix) XString_delete_base(suffix);
                if (mediaData) XByteArray_delete_base(mediaData);
                ok = false;
                break;
            }
            XString_delete_base(suffix);
            XByteArray_delete_base(mediaData);
        } else if (XString_equals_utf8(name, "chart", XChar_CaseSensitive)) {
            XString_Init_Utf8(idName, "r:id");
            const XString* rid = XXmlStreamAttributes_value(
                XXmlStreamReader_attributes(reader), idName);
            XString_deinit_base(idName);
            XlsxRelationship* relationship = rid
                ? XRelationships_getRelationshipById(relationships, rid) : NULL;
            const char* target = relationship && relationship->m_target
                ? XString_toUtf8(relationship->m_target) : NULL;
            if (!target) { ok = false; break; }
            char chartPath[160];
            if (relationship->m_target &&
                XString_startsWith_utf8(relationship->m_target, "../", XChar_CaseSensitive))
                snprintf(chartPath, sizeof(chartPath), "xl/%s", target + 3);
            else if (XString_startsWith_utf8(relationship->m_target, "/", XChar_CaseSensitive))
                snprintf(chartPath, sizeof(chartPath), "%s", target + 1);
            else
                snprintf(chartPath, sizeof(chartPath), "xl/drawings/%s", target);
            XByteArray* chartData = zipFileDataUtf8(zip, chartPath);
            XChart* chart = chartData
                ? XChart_create(&worksheet->m_base, XAbstractOOXmlFile_F_LoadFromExists) : NULL;
            if (!chart || !XChart_loadFromXmlData(chart, XByteArray_data(chartData),
                    XByteArray_size_base((XContainer*)chartData))) {
                if (chart) XChart_delete(chart);
                if (chartData) XByteArray_delete_base(chartData);
                ok = false;
                break;
            }
            if (chart->m_row <= 0 || chart->m_col <= 0)
                XChart_setPosition(chart, row, column, 0, 0);
            if (!XVector_push_back_2(worksheet->m_chartFiles, &chart, 1)) {
                XChart_delete(chart);
                XByteArray_delete_base(chartData);
                ok = false;
                break;
            }
            XByteArray_delete_base(chartData);
        }
    }
    if (XXmlStreamReader_hasError(reader)) ok = false;
    XXmlStreamReader_delete_base(reader);
    XRelationships_delete(relationships);
    return ok;
}

static bool documentLoadFromReader(XDocument* self, XZipReader* zip)
{
    if (!self || !self->m_workbook || !zip || !XZipReader_exists(zip)) return false;
    XByteArray* workbookXml = zipFileDataUtf8(zip, "xl/workbook.xml");
    if (!workbookXml) return false;
    bool result = XWorkbook_loadFromXmlData(self->m_workbook, XByteArray_data(workbookXml),
        XByteArray_size_base(workbookXml));
    XByteArray_delete_base(workbookXml);
    if (!result || !alignWorkbookSheetTypesFromRelationships(self->m_workbook, zip)) return false;

    XByteArray* coreXml = zipFileDataUtf8(zip, "docProps/core.xml");
    if (coreXml) {
        result = XDocPropsCore_loadFromXmlData(self->m_docPropsCore,
            XByteArray_data(coreXml), XByteArray_size_base((XContainer*)coreXml));
        XByteArray_delete_base(coreXml);
        if (!result) return false;
    }
    XByteArray* appXml = zipFileDataUtf8(zip, "docProps/app.xml");
    if (appXml) {
        result = XDocPropsApp_loadFromXmlData(self->m_docPropsApp,
            XByteArray_data(appXml), XByteArray_size_base((XContainer*)appXml));
        XByteArray_delete_base(appXml);
        if (!result) return false;
    }
    XByteArray* stylesXml = zipFileDataUtf8(zip, "xl/styles.xml");
    if (stylesXml) {
        result = XStyles_loadFromXmlData(self->m_workbook->m_styles,
            XByteArray_data(stylesXml), XByteArray_size_base((XContainer*)stylesXml));
        XByteArray_delete_base(stylesXml);
        if (!result) return false;
    }
    XByteArray* themeXml = zipFileDataUtf8(zip, "xl/theme/theme1.xml");
    if (themeXml) {
        result = XTheme_loadFromXmlData(self->m_workbook->m_theme,
            XByteArray_data(themeXml), XByteArray_size_base((XContainer*)themeXml));
        XByteArray_delete_base(themeXml);
        if (!result) return false;
    }

    XStringList* sharedStrings = XStringList_create();
    if (!sharedStrings || !loadSharedStringsFromReader(zip, sharedStrings)) {
        if (sharedStrings) XStringList_delete_base(sharedStrings);
        return false;
    }

    int sheetCount = XWorkbook_sheetCount(self->m_workbook);
    for (int i = 0; i < sheetCount; ++i) {
        XAbstractSheet* sheet = XWorkbook_sheet(self->m_workbook, i);
        if (!sheet) continue;
        XString* sheetPath = sheetPathFromReader(zip, sheet);
        if (!sheetPath) { result = false; break; }
        if (sheet->m_sheetType == XAbstractSheet_ST_ChartSheet) {
            bool chartResult = loadChartsheetFromReader(zip, (XChartsheet*)sheet, sheetPath);
            XString_delete_base(sheetPath);
            if (!chartResult) { result = false; break; }
            continue;
        }
        XByteArray* sheetXml = XZipReader_fileData(zip, sheetPath);
        if (!sheetXml) { XString_delete_base(sheetPath); result = false; break; }
        XWorksheet* worksheet = (XWorksheet*)sheet;
        bool sheetResult = XWorksheet_loadFromXmlData(worksheet, XByteArray_data(sheetXml),
            XByteArray_size_base((XContainer*)sheetXml));
        XByteArray_delete_base(sheetXml);
        if (!sheetResult || !resolveWorksheetSharedStrings(worksheet, sharedStrings) ||
            !resolveWorksheetHyperlinks(zip, worksheet, sheetPath) ||
            !loadWorksheetImagesFromReader(zip, worksheet, i)) {
            XString_delete_base(sheetPath);
            result = false;
            break;
        }
        XString_delete_base(sheetPath);
    }
    XStringList_delete_base(sharedStrings);
    if (!result) return false;
    self->m_isLoaded = true;
    self->m_isModified = false;
    return true;
}

static void parse_sheet_names_from_zip(const char* zipPath, SheetNameList* out)
{
    out->count = 0;
    XString* zipPathStr = XString_create_utf8(zipPath);
    XZipReader* zip = XZipReader_create(zipPathStr);
    XString_delete_base(zipPathStr);
    if (!zip) { printf("[LOAD_DBG] XZipReader_create failed\n"); return; }

    /* 调试：列出 ZIP 中所有文件 */
    XStringList* paths = XZipReader_filePaths(zip);
    if (paths) {
        size_t pc = XStringList_size_base(paths);
        printf("[LOAD_DBG] zip contains %zu files:\n", pc);
        for (size_t pi = 0; pi < pc && pi < 20; pi++) {
            XString* p = (XString*)XStringList_at_base(paths, pi);
            if (p) printf("[LOAD_DBG]   [%zu] %s\n", pi, XString_toUtf8(p));
        }
    } else {
        printf("[LOAD_DBG] filePaths returned NULL\n");
    }

    XString* wbPathStr = XString_create_utf8("xl/workbook.xml");
    XByteArray* wbXml = XZipReader_fileData(zip, wbPathStr);
    XString_delete_base(wbPathStr);
    XZipReader_delete(zip);
    if (!wbXml) { printf("[LOAD_DBG] workbook.xml not found in zip\n"); return; }

    char* xml = (char*)XByteArray_data(wbXml);
    size_t xmlLen = XByteArray_size_base(wbXml);
    printf("[LOAD_DBG] workbook.xml len=%zu content=%.200s\n", xmlLen, xml ? xml : "NULL");
    if (xml && xmlLen > 0) {
        /*
         * XByteArray 内部缓冲可能没有为 '\0' 预留空间，直接写 xml[xmlLen]
         * 会越界破坏相邻堆内存。临时扩容 1 字节写入 '\0'，处理完再恢复。
         */
        XByteArray_resize_base(wbXml, xmlLen + 1);
        xml = (char*)XByteArray_data(wbXml);
        xml[xmlLen] = '\0';
        const char* sp = xml;
        while ((sp = strstr(sp, "<sheet ")) != NULL && out->count < 64) {
            const char* nameS = strstr(sp, "name=\"");
            if (nameS) {
                nameS += 6;
                const char* nameE = strchr(nameS, '"');
                if (nameE && nameE > nameS) {
                    size_t nl = (size_t)(nameE - nameS);
                    if (nl < 256) {
                        memcpy(out->names[out->count], nameS, nl);
                        out->names[out->count][nl] = '\0';
                        out->count++;
                    }
                }
            }
            sp++;
        }
    }
    XByteArray_delete_base(wbXml);
}

bool XDocument_load(XDocument* self) {
    if (!self || !self->m_filePath || XString_size_base(self->m_filePath) == 0) return false;
    XZipReader* zip = XZipReader_create(self->m_filePath);
    if (!zip) return false;
    bool result = documentLoadFromReader(self, zip);
    XZipReader_delete(zip);
    return result;
}

/* ========== 辅助：从ZIP解析 sheet name -> path 映射 ========== */
static char* find_sheet_path_in_zip(const char* zipPath, const XString* sheetName, int sheetIndex)
{
    if (!zipPath) return NULL;
    XString* zipPathStr = XString_create_utf8(zipPath);
    XZipReader* zip = XZipReader_create(zipPathStr);
    XString_delete_base(zipPathStr);
    if (!zip) return NULL;

    /* 读取 workbook.xml.rels 获取 rId -> path */
    typedef struct { char rid[32]; char path[256]; } RelEntry;
    RelEntry rels[32];
    int relCount = 0;
    memset(rels, 0, sizeof(rels));

    XString* relsPathStr = XString_create_utf8("xl/_rels/workbook.xml.rels");
    XByteArray* relXml = XZipReader_fileData(zip, relsPathStr);
    XString_delete_base(relsPathStr);
    if (relXml) {
        char* relStr = (char*)XByteArray_data(relXml);
        size_t relLen = XByteArray_size_base(relXml);
        if (relStr && relLen > 0) {
            /*
             * XByteArray 内部缓冲可能没有为 '\0' 预留空间，直接写 relStr[relLen]
             * 会越界破坏相邻堆内存。临时扩容 1 字节写入 '\0'，处理完再恢复。
             */
            XByteArray_resize_base(relXml, relLen + 1);
            relStr = (char*)XByteArray_data(relXml);
            relStr[relLen] = '\0';
            const char* rp = relStr;
            /*
             * 跳过 <Relationships> 包装标签；只匹配真实的 Relationship 元素。
             * 不能在 || 表达式里连写两个 strstr，否则前一个返回 NULL 时
             * strstr(NULL, ...) 会触发段错误（UB）。用临时变量中转。
             */
            while (true) {
                const char* hit = strstr(rp, "<Relationship ");
                if (!hit) hit = strstr(rp, "<Relationship>");
                if (!hit) break;
                rp = hit;
                const char* idS = strstr(rp, "Id=\""); const char* tgtS = strstr(rp, "Target=\"");
                if (idS && tgtS) {
                    idS += 4; const char* idE = strchr(idS, '"');
                    tgtS += 8; const char* tgtE = strchr(tgtS, '"');
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
    XString* wbPathStr = XString_create_utf8("xl/workbook.xml");
    XByteArray* wbXml = XZipReader_fileData(zip, wbPathStr);
    XString_delete_base(wbPathStr);
    XZipReader_delete(zip);
    if (!wbXml) return NULL;

    /*
     * XByteArray 内部缓冲可能没有为 '\0' 预留空间，直接写 xml[xmlLen]
     * 会越界破坏相邻堆内存。临时扩容 1 字节写入 '\0'。
     */
    char* xml = NULL;
    size_t xmlLen = 0;
    if (wbXml) {
        xml = (char*)XByteArray_data(wbXml);
        xmlLen = XByteArray_size_base(wbXml);
        if (xml && xmlLen > 0) {
            XByteArray_resize_base(wbXml, xmlLen + 1);
            xml = (char*)XByteArray_data(wbXml);
            xml[xmlLen] = '\0';
        }
    }

    char* result = NULL;
    const char* sp = xml;
    int currentIdx = 0;

    while ((sp = strstr(sp, "<sheet ")) != NULL) {
        const char* nameS = strstr(sp, "name=\""); const char* ridS = strstr(sp, "r:id=\"");
        if (nameS && ridS) {
            /* 跳过 name=\" / r:id=\" 各自的 6 字节定界符，从值首字节开始读 */
            nameS += 6; const char* nameE = strchr(nameS, '"');
            ridS += 6; const char* ridE = strchr(ridS, '"');
            if (nameE && ridE && nameE > nameS && ridE > ridS) {
                size_t nl = (size_t)(nameE - nameS);
                size_t rrl = (size_t)(ridE - ridS);
                char tmpName[256] = {0}; char tmpRid[32] = {0};
                if (nl < sizeof(tmpName) && rrl < sizeof(tmpRid)) {
                    memcpy(tmpName, nameS, nl); tmpName[nl] = '\0';
                    memcpy(tmpRid, ridS, rrl); tmpRid[rrl] = '\0';
                    XString* tmpRidStr = XString_create_utf8(tmpRid);
                    bool match = false;
                    if (sheetName && XString_equals_utf8(sheetName, tmpName, XChar_CaseSensitive)) match = true;
                    else if (!sheetName && currentIdx == sheetIndex) match = true;
                    if (match && tmpRidStr) {
                        for (int i = 0; i < relCount; i++) {
                            if (XString_equals_utf8(tmpRidStr, rels[i].rid, XChar_CaseSensitive)) {
                                size_t pathLen = strlen(rels[i].path);
                                result = (char*)XMalloc_System(pathLen + 1);
                                if (result) { memcpy(result, rels[i].path, pathLen + 1); }
                                break;
                            }
                        }
                    }
                    if (tmpRidStr) XString_delete_base(tmpRidStr);
                }
            }
        }
        sp++; currentIdx++;
    }

    XByteArray_delete_base(wbXml);
    return result;
}

/* ========== SAX 读取 ========== */
bool XDocument_readSheetSax(XDocument* self, const XString* sheetName,
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

    /* 释放共享字符串 - XStringList 的值类型 deinit 由 delete_base 自动处理 */
    XStringList_delete_base(sharedStrings);
    XFree_System(sheetPath);
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

    /* 释放共享字符串 - XStringList 的值类型 deinit 由 delete_base 自动处理 */
    XStringList_delete_base(sharedStrings);
    XFree_System(sheetPath);
    return ok;
}

/**
 * @brief     将文档导出为 CSV 文件
 * @param self       XDocument 指针
 * @param csvFileName CSV 文件路径
 * @return    成功返回true
 * @note      导出第一个工作表的数据，以逗号为分隔符
 */
bool XDocument_saveAsCsv(const XDocument* self, const XString* csvFileName)
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
    char*** matrix = (char***)XMalloc_System((size_t)maxRow * sizeof(char**));
    if (!matrix) { XFree_System(locs); return false; }
    memset(matrix, 0, (size_t)maxRow * sizeof(char**));
    for (int r = 0; r < maxRow; r++) {
        matrix[r] = (char**)XMalloc_System((size_t)maxCol * sizeof(char*));
        if (!matrix[r]) {
            for (int previous = 0; previous < r; ++previous) XFree_System(matrix[previous]);
            XFree_System(matrix);
            XFree_System(locs);
            return false;
        }
        memset(matrix[r], 0, (size_t)maxCol * sizeof(char*));
    }

    for (int i = 0; i < count; i++) {
        int r = locs[i].m_row - 1;
        int c = locs[i].m_col - 1;
        if (r >= 0 && r < maxRow && c >= 0 && c < maxCol) {
            XCell* cell = locs[i].m_cell;
            const XString* val = XCell_readValue(cell);
            if (val) {
                const char* valCstr = XString_toUtf8(val);
                size_t vlen = strlen(valCstr);
                matrix[r][c] = (char*)XMalloc_System(vlen + 1);
                if (matrix[r][c]) memcpy(matrix[r][c], valCstr, vlen + 1);
            }
        }
    }
    XFree_System(locs);

    /* 打开文件 */
    XFile* csvFile = XFile_create_2((XString*)csvFileName);
    if (!csvFile || !XIODevice_open_base((XIODevice*)csvFile, XIODevice_WriteOnly | XIODevice_Truncate)) {
        if (csvFile) XClass_delete_base((XClass*)csvFile);
        for (int r = 0; r < maxRow; r++) {
            if (matrix[r]) {
                for (int c = 0; c < maxCol; c++) {
                    if (matrix[r][c]) XFree_System(matrix[r][c]);
                }
                XFree_System(matrix[r]);
            }
        }
        XFree_System(matrix);
        return false;
    }

    /* 写入 CSV */
    XByteArray* csvBuf = XByteArray_create();
    if (!csvBuf) {
        XIODevice_close_base((XIODevice*)csvFile);
        XClass_delete_base((XClass*)csvFile);
        for (int r = 0; r < maxRow; ++r) {
            for (int c = 0; c < maxCol; ++c) if (matrix[r][c]) XFree_System(matrix[r][c]);
            XFree_System(matrix[r]);
        }
        XFree_System(matrix);
        return false;
    }
    for (int r = 0; r < maxRow; r++) {
        for (int c = 0; c < maxCol; c++) {
            char* val = matrix[r][c];
            if (val && strlen(val) > 0) {
                /* 检查是否需要加引号（包含逗号、引号、换行） */
                bool needsQuote = false;
                for (const char* p = val; *p; p++) {
                    if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
                        needsQuote = true;
                        break;
                    }
                }
                if (needsQuote) {
                    /* 双引号转义 */
                    XByteArray_append_1(csvBuf, (uint8_t)'"');
                    for (const char* p = val; *p; p++) {
                        if (*p == '"') XByteArray_append_1(csvBuf, (uint8_t)'"');
                        XByteArray_append_1(csvBuf, (uint8_t)*p);
                    }
                    XByteArray_append_1(csvBuf, (uint8_t)'"');
                } else {
                    XByteArray_append_utf8(csvBuf, val);
                }
            }
            if (c < maxCol - 1) XByteArray_append_1(csvBuf, (uint8_t)',');
            if (val) XFree_System(val);
        }
        XByteArray_append_1(csvBuf, (uint8_t)'\n');
        XFree_System(matrix[r]);
    }
    int64_t csvLength = (int64_t)XByteArray_size_base((XContainer*)csvBuf);
    bool result = XIODevice_write_1((XIODevice*)csvFile,
        (const char*)XByteArray_data(csvBuf), csvLength) == csvLength;
    XByteArray_delete_base(csvBuf);
    XIODevice_close_base((XIODevice*)csvFile);
    XClass_delete_base((XClass*)csvFile);
    XFree_System(matrix);
    (void)self;  /* suppress unused */
    return result;
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
    double* maxWidth = (double*)XMalloc_System((size_t)(colLast - colFirst + 1) * sizeof(double));
    if (!maxWidth) { XFree_System(locs); return false; }
    memset(maxWidth, 0, (size_t)(colLast - colFirst + 1) * sizeof(double));
    for (int c = colFirst; c <= colLast; c++) maxWidth[c - colFirst] = 8.0;

    for (int i = 0; i < count; i++) {
        int col = locs[i].m_col;
        if (col < colFirst || col > colLast) continue;
        XCell* cell = locs[i].m_cell;
        const XString* val = XCell_readValue(cell);
        if (val && !XString_isEmpty_base(val)) {
            double len = (double)XString_toUtf8_length(val);
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
    XFree_System(maxWidth);
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

    double* maxWidth = (double*)XMalloc_System((size_t)maxCol * sizeof(double));
    if (!maxWidth) { XFree_System(locs); return false; }
    memset(maxWidth, 0, (size_t)maxCol * sizeof(double));
    for (int c = 0; c < maxCol; c++) maxWidth[c] = 8.0;

    for (int i = 0; i < count; i++) {
        int col = locs[i].m_col;
        if (col < 1 || col > maxCol) continue;
        XCell* cell = locs[i].m_cell;
        const XString* val = XCell_readValue(cell);
        if (val && !XString_isEmpty_base(val)) {
            double len = (double)XString_toUtf8_length(val);
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
    XFree_System(maxWidth);
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
bool XDocument_copyStyle(const XString* fromPath, const XString* toPath)
{
    if (!fromPath || !toPath) return false;

    /* 打开源文件和目标文件 */
    XZipReader* fromZip = XZipReader_create(fromPath);
    if (!fromZip) return false;

    XZipReader* toZip = XZipReader_create(toPath);
    if (!toZip) { XZipReader_delete(fromZip); return false; }

    /* 创建临时文件用于写入 */
    XString* toPathStr = XString_create_copy(toPath);
    XString* tempPathStr = XString_create();
    if (!toPathStr || !tempPathStr || !XSaveFile_generateTempFileName(toPathStr, tempPathStr)) {
        if (toPathStr) XString_delete_base(toPathStr);
        if (tempPathStr) XString_delete_base(tempPathStr);
        XZipReader_delete(fromZip);
        XZipReader_delete(toZip);
        return false;
    }
    XZipWriter* tempZip = XZipWriter_create(tempPathStr);
    if (!tempZip) { 
        XZipReader_delete(fromZip); 
        XZipReader_delete(toZip); 
        XFile_remove_static(tempPathStr);
        XString_delete_base(toPathStr);
        XString_delete_base(tempPathStr);
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
            if (XZipReader_fileData(fromZip, *ppPath) != NULL) {
                XByteArray* data = XZipReader_fileData(fromZip, *ppPath);
                if (data) {
                    content = (const char*)XByteArray_data(data);
                    contentLen = XByteArray_size_base(data);
                    needCopy = true;
                }
            }
        }
        /* 2. 复制 workbook.xml 中的 workbookPr */
        else if (strstr(toFilePath, "xl/workbook") != NULL) {
            if (XZipReader_fileData(fromZip, *ppPath) != NULL) {
                XByteArray* data = XZipReader_fileData(fromZip, *ppPath);
                if (data) {
                    content = (const char*)XByteArray_data(data);
                    contentLen = XByteArray_size_base(data);
                    needCopy = true;
                }
            }
        }
        /* 3. 复制 worksheets 中的相关样式 */
        else if (strstr(toFilePath, "xl/worksheets/sheet") != NULL) {
            if (XZipReader_fileData(fromZip, *ppPath) != NULL) {
                XByteArray* data = XZipReader_fileData(fromZip, *ppPath);
                if (data) {
                    content = (const char*)XByteArray_data(data);
                    contentLen = XByteArray_size_base(data);
                    needCopy = true;
                }
            }
        }

        if (needCopy && content && contentLen > 0) {
            /* 直接复制源文件的内容到临时文件 */
            XZipWriter_addFile(tempZip, *ppPath, (const uint8_t*)content, contentLen);
        } else {
            /* 复制目标文件的原始内容 */
            XByteArray* origData = XZipReader_fileData(toZip, *ppPath);
            if (origData) {
                const uint8_t* d = XByteArray_data(origData);
                size_t dlen = XByteArray_size_base(origData);
                XZipWriter_addFile(tempZip, *ppPath, d, dlen);
                XByteArray_delete_base(origData);
            } else {
                /* 如果目标文件中没有，使用空数据 */
                XZipWriter_addFile(tempZip, *ppPath, (const uint8_t*)"", 0);
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
        ok = XFile_rename_static(tempPathStr, toPathStr);
    } else {
        XFile_remove_static(tempPathStr);
    }

    XString_delete_base(toPathStr);
    XString_delete_base(tempPathStr);
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
bool XDocument_changeImage(XDocument* self, int imageIndex, const XString* newImagePath)
{
    XWorksheet* ws = getCurrentWorksheet(self);
    if (!ws || imageIndex < 0 || !newImagePath) return false;

    int mediaCount = (int)XVector_size_base((XContainer*)ws->m_mediaFiles);
    if (imageIndex >= mediaCount) return false;

    XMediaFile* mediaFile = *(XMediaFile**)XVector_at_base(ws->m_mediaFiles, (size_t)imageIndex);
    if (!mediaFile) return false;

    /* 获取新图片的文件扩展名 */
    const char* newImagePathCstr = XString_toUtf8(newImagePath);
    if (!newImagePathCstr) return false;
    const char* ext = strrchr(newImagePathCstr, '.');
    if (!ext) return false;
    ext++; /* 跳过点 */

    /* 确定 MIME 类型 */
    const char* mimeType = "image/png";
    XString* extStr = XString_create_utf8(ext);
    if (!extStr) return false;
    if (XString_equals_utf8(extStr, "jpg", XChar_CaseInsensitive) ||
        XString_equals_utf8(extStr, "jpeg", XChar_CaseInsensitive)) {
        mimeType = "image/jpeg";
    } else if (XString_equals_utf8(extStr, "bmp", XChar_CaseInsensitive)) {
        mimeType = "image/bmp";
    } else if (XString_equals_utf8(extStr, "gif", XChar_CaseInsensitive)) {
        mimeType = "image/gif";
    } else if (XString_equals_utf8(extStr, "png", XChar_CaseInsensitive)) {
        mimeType = "image/png";
    }
    XString_delete_base(extStr);

    /* 读取新图片文件 */
    XFile* imgFile = XFile_create_2((XString*)newImagePath);
    if (!imgFile || !XIODevice_open_base((XIODevice*)imgFile, XIODevice_ReadOnly)) {
        if (imgFile) XClass_delete_base((XClass*)imgFile);
        return false;
    }
    XByteArray* imgData = XIODevice_readAll_3((XIODevice*)imgFile);
    XIODevice_close_base((XIODevice*)imgFile);
    XClass_delete_base((XClass*)imgFile);
    if (!imgData || XByteArray_size_base(imgData) == 0) {
        if (imgData) XByteArray_delete_base(imgData);
        return false;
    }
    uint8_t* imageData = XByteArray_data(imgData);
    size_t fileSize = XByteArray_size_base(imgData);

    /* 更新媒体文件内容 */
    {
        XString* extXStr = XString_create_utf8(ext);
        XString* mimeXStr = XString_create_utf8(mimeType);
        XMediaFile_set(mediaFile, imageData, fileSize, extXStr, mimeXStr);
        XString_delete_base(extXStr);
        XString_delete_base(mimeXStr);
    }
    XMediaFile_setFileName(mediaFile, newImagePath);

    XByteArray_delete_base(imgData);
    self->m_isModified = true;
    return true;
}

/* ========== 设备接口（对标 QXlsx::Document(QIODevice*) / saveAs(QIODevice*)） ========== */

XDocument* XDocument_createFromDevice(struct XIODevice* device)
{
    if (!device) return NULL;
    bool openedHere = false;
    if (!XIODevice_isOpen(device)) {
        if (!XIODevice_open_base(device, XIODevice_ReadOnly)) return NULL;
        openedHere = true;
    } else if (!XIODevice_isReadable(device)) {
        return NULL;
    }
    XByteArray* package = XIODevice_readAll_3(device);
    if (openedHere) XIODevice_close_base(device);
    if (!package || XByteArray_size_base(package) == 0) {
        if (package) XByteArray_delete_base(package);
        return NULL;
    }

    XZipReader* zip = XZipReader_createFromData(XByteArray_data(package), XByteArray_size_base(package));
    XDocument* self = zip ? XDocument_create() : NULL;
    if (!self || !documentLoadFromReader(self, zip)) {
        if (self) XDocument_delete(self);
        if (zip) XZipReader_delete(zip);
        XByteArray_delete_base(package);
        return NULL;
    }
    self->m_packageData = package;
    XZipReader_delete(zip);
    return self;
}

bool XDocument_saveAsDevice(const XDocument* self, struct XIODevice* device)
{
    if (!self || !self->m_workbook || !device) return false;
    XZipWriter* zip = XZipWriter_createForDevice(device);
    if (!zip) return false;
    bool packageComplete = documentSaveToZip(self, zip);
    bool result = packageComplete && XZipWriter_close(zip);
    if (!packageComplete) zip->m_closeAttempted = true;
    XZipWriter_delete(zip);
    if (result) ((XDocument*)self)->m_isModified = false;
    return result;
}

/* ========== UTF-8 便捷变体 ========== */

void XDocument_setDocumentProperty_utf8(XDocument* self, const char* name, const char* property)
{
    XString* n = name ? XString_create_utf8(name) : NULL;
    XString* p = property ? XString_create_utf8(property) : NULL;
    XDocument_setDocumentProperty(self, n, p);
    if (n) XString_delete_base(n);
    if (p) XString_delete_base(p);
}

bool XDocument_addSheet_utf8(XDocument* self, const char* name, XAbstractSheet_SheetType type)
{
    XString* s = name ? XString_create_utf8(name) : NULL;
    bool result = XDocument_addSheet(self, s, type);
    if (s) XString_delete_base(s);
    return result;
}

bool XDocument_selectSheet_utf8(XDocument* self, const char* name)
{
    XString* s = name ? XString_create_utf8(name) : NULL;
    bool result = XDocument_selectSheet(self, s);
    if (s) XString_delete_base(s);
    return result;
}

bool XDocument_renameSheet_utf8(XDocument* self, const char* oldName, const char* newName)
{
    XString* o = oldName ? XString_create_utf8(oldName) : NULL;
    XString* n = newName ? XString_create_utf8(newName) : NULL;
    bool result = XDocument_renameSheet(self, o, n);
    if (o) XString_delete_base(o);
    if (n) XString_delete_base(n);
    return result;
}

bool XDocument_deleteSheet_utf8(XDocument* self, const char* name)
{
    XString* s = name ? XString_create_utf8(name) : NULL;
    bool result = XDocument_deleteSheet(self, s);
    if (s) XString_delete_base(s);
    return result;
}

bool XDocument_defineName_utf8(XDocument* self, const char* name, const char* formula, const char* comment, const char* scope)
{
    XString* n = name ? XString_create_utf8(name) : NULL;
    XString* f = formula ? XString_create_utf8(formula) : NULL;
    XString* c = comment ? XString_create_utf8(comment) : NULL;
    XString* s = scope ? XString_create_utf8(scope) : NULL;
    bool result = XDocument_defineName(self, n, f, c, s);
    if (n) XString_delete_base(n);
    if (f) XString_delete_base(f);
    if (c) XString_delete_base(c);
    if (s) XString_delete_base(s);
    return result;
}

bool XDocument_saveAs_utf8(const XDocument* self, const char* xlsxName)
{
    XString* s = xlsxName ? XString_create_utf8(xlsxName) : NULL;
    bool result = XDocument_saveAs(self, s);
    if (s) XString_delete_base(s);
    return result;
}

int XDocument_insertImage_utf8(XDocument* self, int row, int col, const char* imagePath)
{
    XString* s = imagePath ? XString_create_utf8(imagePath) : NULL;
    int result = XDocument_insertImage(self, row, col, s);
    if (s) XString_delete_base(s);
    return result;
}

const XString* XDocument_documentProperty_utf8(const XDocument* self, const char* name)
{
    XString* s = name ? XString_create_utf8(name) : NULL;
    const XString* result = XDocument_documentProperty(self, s);
    if (s) XString_delete_base(s);
    return result;
}
