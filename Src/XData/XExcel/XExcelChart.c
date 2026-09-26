#include "XExcelChart.h"
#include "XAbstractSheet.h"
#include "XMemory.h"
#include "XFile.h"
#include "XIODevice.h"
#include "XByteArray.h"
#include "XXmlStreamWriter.h"
#include "XXmlStreamReader.h"
#include "XClass.h"

#include <string.h>
#include <stdio.h>

XExcelChart* XExcelChart_create(XAbstractSheet* parent, XAbstractOOXmlFile_CreateFlag flag) {
    XExcelChart* self = (XExcelChart*)XMalloc_System(sizeof(XExcelChart));
    if (!self) return NULL; memset(self, 0, sizeof(XExcelChart));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_chartType = XExcelChart_NoStatementChart;
    self->m_chartStyle = -1;
    self->m_legendPos = XExcelChart_AxisPosRight;
    self->m_series = XVector_Create(XExcelChart_Series);
    self->m_width = 480; self->m_height = 290;
    if (parent && parent->m_sheetName)
        self->m_dataSheetName = XString_create_copy(parent->m_sheetName);
    return self;
}
XExcelChart* XExcelChart_copy(const XExcelChart* source, XAbstractSheet* parent) {
    if (!source) return NULL;
    XExcelChart* copy = XExcelChart_create(parent, XAbstractOOXmlFile_F_NewFromScratch);
    if (!copy) return NULL;
    copy->m_chartType = source->m_chartType;
    copy->m_chartStyle = source->m_chartStyle;
    copy->m_legendPos = source->m_legendPos;
    copy->m_legendOverlay = source->m_legendOverlay;
    copy->m_majorGridlinesEnable = source->m_majorGridlinesEnable;
    copy->m_minorGridlinesEnable = source->m_minorGridlinesEnable;
    copy->m_row = source->m_row;
    copy->m_col = source->m_col;
    copy->m_width = source->m_width;
    copy->m_height = source->m_height;
    copy->m_colOffset = source->m_colOffset;
    copy->m_rowOffset = source->m_rowOffset;
    if (source->m_dataSheetName)
        copy->m_dataSheetName = XString_create_copy(source->m_dataSheetName);
    if (source->m_chartTitle) copy->m_chartTitle = XString_create_copy(source->m_chartTitle);
    if (source->m_axisTitleLeft) copy->m_axisTitleLeft = XString_create_copy(source->m_axisTitleLeft);
    if (source->m_axisTitleRight) copy->m_axisTitleRight = XString_create_copy(source->m_axisTitleRight);
    if (source->m_axisTitleTop) copy->m_axisTitleTop = XString_create_copy(source->m_axisTitleTop);
    if (source->m_axisTitleBottom) copy->m_axisTitleBottom = XString_create_copy(source->m_axisTitleBottom);
    size_t count = source->m_series ? XVector_size_base((XContainer*)source->m_series) : 0;
    for (size_t i = 0; i < count; ++i) {
        XExcelChart_Series* series = (XExcelChart_Series*)XVector_at_base(source->m_series, i);
        if (series) XVector_push_back_2(copy->m_series, series, 1);
    }
    return copy;
}
void XExcelChart_delete(XExcelChart* self) {
    if (!self) return;
    if (self->m_chartTitle) XString_delete_base(self->m_chartTitle);
    if (self->m_axisTitleLeft) XString_delete_base(self->m_axisTitleLeft);
    if (self->m_axisTitleRight) XString_delete_base(self->m_axisTitleRight);
    if (self->m_axisTitleTop) XString_delete_base(self->m_axisTitleTop);
    if (self->m_axisTitleBottom) XString_delete_base(self->m_axisTitleBottom);
    if (self->m_dataSheetName) XString_delete_base(self->m_dataSheetName);
    if (self->m_series) XVector_delete_base(self->m_series);
    XAbstractOOXmlFile_deinit(&self->m_base); XFree_System(self);
}
void XExcelChart_addSeries(XExcelChart* self, const XCellRange* range, bool headerH, bool headerV, bool swapHeaders) {
    if (!self || !range) return;
    XExcelChart_Series s; memset(&s, 0, sizeof(s)); s.m_range = *range; s.m_headerH = headerH; s.m_headerV = headerV; s.m_swapHeaders = swapHeaders;
    XVector_push_back_2(self->m_series, &s, 1);
}
void XExcelChart_setDataSheetName(XExcelChart* self, const XString* name) {
    if (!self) return;
    if (self->m_dataSheetName) {
        XString_delete_base(self->m_dataSheetName);
        self->m_dataSheetName = NULL;
    }
    if (name) self->m_dataSheetName = XString_create_copy(name);
}
void XExcelChart_setDataSheetName_utf8(XExcelChart* self, const char* name) {
    XString* sheetName = name ? XString_create_utf8(name) : NULL;
    XExcelChart_setDataSheetName(self, sheetName);
    if (sheetName) XString_delete_base(sheetName);
}
void XExcelChart_setChartType(XExcelChart* self, XExcelChart_ChartType type) { if (self) self->m_chartType = type; }
void XExcelChart_setChartStyle(XExcelChart* self, int id) { if (self) self->m_chartStyle = id; }
void XExcelChart_setAxisTitle(XExcelChart* self, XExcelChart_ChartAxisPos pos, const XString* axisTitle) {
    if (!self) return;
    XString** target = NULL;
    if (pos == XExcelChart_AxisPosLeft) target = &self->m_axisTitleLeft;
    else if (pos == XExcelChart_AxisPosRight) target = &self->m_axisTitleRight;
    else if (pos == XExcelChart_AxisPosTop) target = &self->m_axisTitleTop;
    else if (pos == XExcelChart_AxisPosBottom) target = &self->m_axisTitleBottom;
    if (target) { if (!*target) *target = XString_create(); if (*target) { XString_clear_base(*target); if (axisTitle) XString_append(*target, axisTitle); } }
}
void XExcelChart_setChartTitle(XExcelChart* self, const XString* title) {
    if (!self) return;
    if (!self->m_chartTitle) self->m_chartTitle = XString_create();
    if (self->m_chartTitle) { XString_clear_base(self->m_chartTitle); if (title) XString_append(self->m_chartTitle, title); }
}
void XExcelChart_setChartLegend(XExcelChart* self, XExcelChart_ChartAxisPos legendPos, bool overlap) { if (self) { self->m_legendPos = legendPos; self->m_legendOverlay = overlap; } }
void XExcelChart_setGridlinesEnable(XExcelChart* self, bool majorEnable, bool minorEnable) { if (self) { self->m_majorGridlinesEnable = majorEnable; self->m_minorGridlinesEnable = minorEnable; } }
void XExcelChart_setSize(XExcelChart* self, int width, int height) { if (self) { self->m_width = width; self->m_height = height; } }
void XExcelChart_setPosition(XExcelChart* self, int row, int col, int rowOff, int colOff) { if (self) { self->m_row = row; self->m_col = col; self->m_rowOffset = rowOff; self->m_colOffset = colOff; } }

static const char* chart_type_name(XExcelChart_ChartType type)
{
    static const char* names[] = {
        "", "areaChart", "area3DChart", "lineChart", "line3DChart", "stockChart",
        "radarChart", "scatterChart", "pieChart", "pie3DChart", "doughnutChart",
        "barChart", "bar3DChart", "ofPieChart", "surfaceChart", "surface3DChart",
        "bubbleChart"
    };
    return (type > XExcelChart_NoStatementChart && type <= XExcelChart_BubbleChart)
        ? names[(int)type] : "lineChart";
}

static XExcelChart_ChartType chart_type_from_name(const XString* name)
{
    if (!name) return XExcelChart_NoStatementChart;
    for (int type = XExcelChart_AreaChart; type <= XExcelChart_BubbleChart; ++type) {
        if (XString_equals_utf8(name, chart_type_name((XExcelChart_ChartType)type), XChar_CaseSensitive))
            return (XExcelChart_ChartType)type;
    }
    return XExcelChart_NoStatementChart;
}

static const char* legend_position_name(XExcelChart_ChartAxisPos position)
{
    switch (position) {
        case XExcelChart_AxisPosLeft: return "l";
        case XExcelChart_AxisPosTop: return "t";
        case XExcelChart_AxisPosBottom: return "b";
        case XExcelChart_AxisPosRight: return "r";
        default: return "r";
    }
}

static const XString* chart_attribute(const XXmlStreamAttributes* attributes, const char* name)
{
    if (!attributes || !name) return NULL;
    XString_Init_Utf8(key, name);
    const XString* result = XXmlStreamAttributes_value_ex(attributes, NULL, key);
    XString_deinit_base(key);
    return result;
}

static void write_value_element(XXmlStreamWriter* writer, const char* name, const char* value)
{
    XXmlStreamWriter_writeEmptyElement_utf8(writer, name);
    XXmlStreamWriter_writeAttribute_utf8(writer, "val", value ? value : "");
}

static void write_chart_title(XXmlStreamWriter* writer, const XString* title)
{
    if (!title || XString_isEmpty_base(title)) return;
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:title");
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:tx");
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:rich");
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "a:bodyPr");
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "a:lstStyle");
    XXmlStreamWriter_writeStartElement_utf8(writer, "a:p");
    XXmlStreamWriter_writeStartElement_utf8(writer, "a:r");
    XXmlStreamWriter_writeTextElement_utf8(writer, "a:t", XString_toUtf8(title));
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
}

static XString chart_formula_for_range(const XExcelChart* self, const XCellRange* range)
{
    XString formula;
    XString_init(&formula);
    if (!range || !XCellRange_isValid(range)) return formula;

    if (self && self->m_dataSheetName &&
        !XString_isEmpty_base(self->m_dataSheetName)) {
        XString_append_utf8(&formula, "'");
        const char* sheetName = XString_toUtf8(self->m_dataSheetName);
        for (const char* p = sheetName ? sheetName : ""; *p; ++p) {
            if (*p == '\'') {
                XString_append_utf8(&formula, "''");
            } else {
                char escaped[2];
                escaped[0] = *p;
                escaped[1] = '\0';
                XString_append_utf8(&formula, escaped);
            }
        }
        XString_append_utf8(&formula, "'!");
    }
    XString rangeText = XCellRange_toString(range, true, true);
    XString_append(&formula, &rangeText);
    XString_deinit_base(&rangeText);
    return formula;
}

static void write_chart_reference(XXmlStreamWriter* writer, const char* containerName,
                                  const char* referenceName, const XExcelChart* chart,
                                  const XCellRange* range)
{
    XXmlStreamWriter_writeStartElement_utf8(writer, containerName);
    XXmlStreamWriter_writeStartElement_utf8(writer, referenceName);
    XString formula = chart_formula_for_range(chart, range);
    XXmlStreamWriter_writeTextElement_utf8(writer, "c:f", XString_toUtf8(&formula));
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
    XString_deinit_base(&formula);
}

static bool chart_series_ranges(const XExcelChart_Series* series, XCellRange* valueRange,
                                XCellRange* categoryRange, XCellRange* titleCell)
{
    if (!series || !valueRange || !categoryRange || !titleCell ||
        !XCellRange_isValid(&series->m_range)) return false;
    XCellRange range = series->m_range;
    int firstRow = range.m_firstRow + (series->m_headerH ? 1 : 0);
    int firstColumn = range.m_firstColumn + (series->m_headerV ? 1 : 0);
    if (firstRow > range.m_lastRow || firstColumn > range.m_lastColumn) return false;

    *valueRange = XCellRange_create();
    *categoryRange = XCellRange_create();
    *titleCell = XCellRange_create();
    if (!series->m_swapHeaders) {
        /* 每次 addSeries 输出一个合法的列系列，避免把二维混合区域写入 numRef。 */
        *valueRange = XCellRange_create_ex(firstRow, firstColumn,
            range.m_lastRow, firstColumn);
        if (series->m_headerV)
            *categoryRange = XCellRange_create_ex(firstRow, range.m_firstColumn,
                range.m_lastRow, range.m_firstColumn);
        if (series->m_headerH)
            *titleCell = XCellRange_create_ex(range.m_firstRow, firstColumn,
                range.m_firstRow, firstColumn);
    } else {
        /* swapHeaders 表示按行生成系列，此时只取第一行数据。 */
        *valueRange = XCellRange_create_ex(firstRow, firstColumn,
            firstRow, range.m_lastColumn);
        if (series->m_headerH)
            *categoryRange = XCellRange_create_ex(range.m_firstRow, firstColumn,
                range.m_firstRow, range.m_lastColumn);
        if (series->m_headerV)
            *titleCell = XCellRange_create_ex(firstRow, range.m_firstColumn,
                firstRow, range.m_firstColumn);
    }
    return XCellRange_isValid(valueRange);
}

static void write_chart_series(XXmlStreamWriter* writer, const XExcelChart* chart,
                               const XExcelChart_Series* series, size_t index)
{
    XCellRange valueRange;
    XCellRange categoryRange;
    XCellRange titleCell;
    if (!chart_series_ranges(series, &valueRange, &categoryRange, &titleCell)) return;

    char indexText[32];
    snprintf(indexText, sizeof(indexText), "%zu", index);
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:ser");
    write_value_element(writer, "c:idx", indexText);
    write_value_element(writer, "c:order", indexText);
    if (XCellRange_isValid(&titleCell))
        write_chart_reference(writer, "c:tx", "c:strRef", chart, &titleCell);
    if (chart->m_chartType == XExcelChart_LineChart) {
        XXmlStreamWriter_writeStartElement_utf8(writer, "c:marker");
        write_value_element(writer, "c:symbol", "none");
        XXmlStreamWriter_writeEndElement(writer);
    }
    if (XCellRange_isValid(&categoryRange))
        write_chart_reference(writer, "c:cat", "c:strRef", chart, &categoryRange);
    write_chart_reference(writer, "c:val", "c:numRef", chart, &valueRange);
    XXmlStreamWriter_writeEndElement(writer);
}

static bool chart_type_has_axes(XExcelChart_ChartType type)
{
    return type == XExcelChart_AreaChart || type == XExcelChart_LineChart ||
        type == XExcelChart_BarChart;
}

static void write_chart_axes(XXmlStreamWriter* writer, bool majorGridlinesEnable)
{
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:catAx");
    write_value_element(writer, "c:axId", "1");
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:scaling");
    write_value_element(writer, "c:orientation", "minMax");
    XXmlStreamWriter_writeEndElement(writer);
    write_value_element(writer, "c:delete", "0");
    write_value_element(writer, "c:axPos", "b");
    write_value_element(writer, "c:majorTickMark", "none");
    write_value_element(writer, "c:minorTickMark", "none");
    write_value_element(writer, "c:tickLblPos", "nextTo");
    write_value_element(writer, "c:crossAx", "2");
    write_value_element(writer, "c:crosses", "autoZero");
    write_value_element(writer, "c:auto", "1");
    write_value_element(writer, "c:lblAlgn", "ctr");
    write_value_element(writer, "c:lblOffset", "100");
    XXmlStreamWriter_writeEndElement(writer);

    XXmlStreamWriter_writeStartElement_utf8(writer, "c:valAx");
    write_value_element(writer, "c:axId", "2");
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:scaling");
    write_value_element(writer, "c:orientation", "minMax");
    XXmlStreamWriter_writeEndElement(writer);
    write_value_element(writer, "c:delete", "0");
    write_value_element(writer, "c:axPos", "l");
    if (majorGridlinesEnable)
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "c:majorGridlines");
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "c:numFmt");
    XXmlStreamWriter_writeAttribute_utf8(writer, "formatCode", "General");
    XXmlStreamWriter_writeAttribute_utf8(writer, "sourceLinked", "1");
    write_value_element(writer, "c:majorTickMark", "none");
    write_value_element(writer, "c:minorTickMark", "none");
    write_value_element(writer, "c:tickLblPos", "nextTo");
    write_value_element(writer, "c:crossAx", "1");
    write_value_element(writer, "c:crosses", "autoZero");
    write_value_element(writer, "c:crossBetween", "between");
    XXmlStreamWriter_writeEndElement(writer);
}

static bool write_chart_xml(XExcelChart* self, XXmlStreamWriter* writer)
{
    if (!self || !writer) return false;
    XXmlStreamWriter_writeStartDocument_ex_utf8(writer, "1.0");
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:chartSpace");
    XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:c",
        "http://schemas.openxmlformats.org/drawingml/2006/chart");
    XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:a",
        "http://schemas.openxmlformats.org/drawingml/2006/main");
    XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:r",
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships");
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:lang");
    XXmlStreamWriter_writeAttribute_utf8(writer, "val", "zh-CN");
    XXmlStreamWriter_writeEndElement(writer);
    if (self->m_chartStyle >= 0) {
        char value[32];
        snprintf(value, sizeof(value), "%d", self->m_chartStyle);
        write_value_element(writer, "c:style", value);
    }
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:chart");
    write_chart_title(writer, self->m_chartTitle);
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:plotArea");
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "c:layout");
    char chartElementName[40];
    snprintf(chartElementName, sizeof(chartElementName), "c:%s", chart_type_name(self->m_chartType));
    XXmlStreamWriter_writeStartElement_utf8(writer, chartElementName);
    if (self->m_chartType == XExcelChart_BarChart) {
        write_value_element(writer, "c:barDir", "col");
        write_value_element(writer, "c:grouping", "clustered");
    } else if (self->m_chartType == XExcelChart_LineChart || self->m_chartType == XExcelChart_AreaChart) {
        write_value_element(writer, "c:grouping", "standard");
    }
    if (self->m_chartType == XExcelChart_LineChart || self->m_chartType == XExcelChart_AreaChart ||
        self->m_chartType == XExcelChart_BarChart || self->m_chartType == XExcelChart_PieChart ||
        self->m_chartType == XExcelChart_DoughnutChart)
        write_value_element(writer, "c:varyColors", "0");
    size_t seriesCount = XVector_size_base((XContainer*)self->m_series);
    for (size_t i = 0; i < seriesCount; ++i) {
        const XExcelChart_Series* series = (const XExcelChart_Series*)XVector_at_base(self->m_series, i);
        if (!series || !XCellRange_isValid(&series->m_range)) continue;
        write_chart_series(writer, self, series, i);
    }
    if (self->m_chartType == XExcelChart_BarChart)
        write_value_element(writer, "c:gapWidth", "150");
    if (chart_type_has_axes(self->m_chartType)) {
        write_value_element(writer, "c:axId", "1");
        write_value_element(writer, "c:axId", "2");
    }
    XXmlStreamWriter_writeEndElement(writer);
    if (chart_type_has_axes(self->m_chartType))
        write_chart_axes(writer, self->m_majorGridlinesEnable);
    XXmlStreamWriter_writeEndElement(writer);
    if (self->m_legendPos != XExcelChart_AxisPosNone) {
        XXmlStreamWriter_writeStartElement_utf8(writer, "c:legend");
        write_value_element(writer, "c:legendPos", legend_position_name(self->m_legendPos));
        write_value_element(writer, "c:overlay", self->m_legendOverlay ? "1" : "0");
        XXmlStreamWriter_writeEndElement(writer);
    }
    write_value_element(writer, "c:plotVisOnly", "1");
    write_value_element(writer, "c:dispBlanksAs", "gap");
    XXmlStreamWriter_writeEndElement(writer);

    /* 图表位置和尺寸由 Drawing 保存，但独立图表 XML 也要保留对象属性，
       这样 XExcelChart_saveToXmlData/XExcelChart_loadFromXmlData 可以无损往返。 */
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "settings");
    char setting[32];
#define WRITE_CHART_SETTING(field, name) do { \
        snprintf(setting, sizeof(setting), "%d", (field)); \
        XXmlStreamWriter_writeAttribute_utf8(writer, (name), setting); \
    } while (0)
    WRITE_CHART_SETTING(self->m_row, "row");
    WRITE_CHART_SETTING(self->m_col, "col");
    WRITE_CHART_SETTING(self->m_rowOffset, "rowOffset");
    WRITE_CHART_SETTING(self->m_colOffset, "colOffset");
    WRITE_CHART_SETTING(self->m_width, "width");
    WRITE_CHART_SETTING(self->m_height, "height");
    WRITE_CHART_SETTING(self->m_majorGridlinesEnable, "majorGridlines");
    WRITE_CHART_SETTING(self->m_minorGridlinesEnable, "minorGridlines");
#undef WRITE_CHART_SETTING

    XXmlStreamWriter_writeEndDocument(writer);

    return !XXmlStreamWriter_hasError(writer);
}

bool XExcelChart_saveToXmlData(XExcelChart* self, uint8_t** outData, size_t* outLen)
{
    if (!self || !outData || !outLen) return false;
    *outData = NULL;
    *outLen = 0;
    XXmlStreamWriter* writer = XXmlStreamWriter_create();
    if (!writer) return false;
    bool ok = write_chart_xml(self, writer);
    XByteArray* bytes = ok ? XXmlStreamWriter_toByteArray(writer) : NULL;
    size_t size = bytes ? XByteArray_size_base((XContainer*)bytes) : 0;
    if (size > 0) {
        *outData = (uint8_t*)XMalloc_System(size + 1);
        if (*outData) {
            memcpy(*outData, XByteArray_data(bytes), size);
            (*outData)[size] = '\0';
            *outLen = size;
        }
    }
    XXmlStreamWriter_delete_base(writer);
    return *outData != NULL;
}

bool XExcelChart_saveToXmlFile(XExcelChart* self, const XString* filePath)
{
    if (!self || !filePath) return false;
    uint8_t* xml = NULL;
    size_t size = 0;
    if (!XExcelChart_saveToXmlData(self, &xml, &size)) return false;
    XFile* file = XFile_create_2((XString*)filePath);
    bool ok = file &&
        XIODevice_open_base((XIODevice*)file, XIODevice_WriteOnly | XIODevice_Truncate);
    if (ok) ok = XIODevice_write_1((XIODevice*)file, xml, (int64_t)size) == (int64_t)size;
    if (file) {
        XIODevice_close_base((XIODevice*)file);
        XClass_delete_base((XClass*)file);
    }
    XFree_System(xml);
    return ok;
}

static void normalize_chart_range(const char* formula, char* output, size_t outputSize)
{
    if (!output || outputSize == 0) return;
    output[0] = '\0';
    if (!formula) return;
    const char* start = strrchr(formula, '!');
    start = start ? start + 1 : formula;
    size_t written = 0;
    while (*start && written + 1 < outputSize) {
        if (*start != '$' && *start != '\'') output[written++] = *start;
        start++;
    }
    output[written] = '\0';
}

static void set_chart_sheet_from_formula(XExcelChart* self, const char* formula)
{
    if (!self || !formula) return;
    const char* bang = strrchr(formula, '!');
    if (!bang || bang == formula) return;
    size_t length = (size_t)(bang - formula);
    const char* start = formula;
    if (length >= 2 && formula[0] == '\'' && formula[length - 1] == '\'') {
        start++;
        length -= 2;
    }
    char name[256];
    size_t written = 0;
    for (size_t i = 0; i < length && written + 1 < sizeof(name); ++i) {
        if (start[i] == '\'' && i + 1 < length && start[i + 1] == '\'') ++i;
        name[written++] = start[i];
    }
    name[written] = '\0';
    if (written > 0) XExcelChart_setDataSheetName_utf8(self, name);
}

bool XExcelChart_loadFromXmlData(XExcelChart* self, const uint8_t* bytes, size_t len)
{
    if (!self || !bytes || len == 0) return false;
    XByteArray* data = XByteArray_create_with_data((const char*)bytes, len);
    if (!data) return false;
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!reader) { XByteArray_delete_base(data); return false; }
    XXmlStreamReader_addData(reader, data);
    XByteArray_delete_base(data);
    XVector_clear_base(self->m_series);
    XExcelChart_Series pending;
    memset(&pending, 0, sizeof(pending));
    pending.m_range = XCellRange_create();
    bool inSeries = false;
    bool inTitle = false;

    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        const XString* name = XXmlStreamReader_name(reader);
        if (token == XXmlStream_StartElement) {
            XExcelChart_ChartType type = chart_type_from_name(name);
            if (type != XExcelChart_NoStatementChart) self->m_chartType = type;
            if (name && XString_equals_utf8(name, "title", XChar_CaseSensitive)) inTitle = true;
            else if (name && XString_equals_utf8(name, "t", XChar_CaseSensitive) && inTitle) {
                const XString* title = XXmlStreamReader_readElementText(reader,
                    XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                XExcelChart_setChartTitle(self, title);
            } else if (name && XString_equals_utf8(name, "ser", XChar_CaseSensitive)) {
                memset(&pending, 0, sizeof(pending));
                pending.m_range = XCellRange_create();
                const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
                const XString* flag = chart_attribute(attributes, "headerH");
                pending.m_headerH = flag && XString_toInt(flag, NULL, 10) != 0;
                flag = chart_attribute(attributes, "headerV");
                pending.m_headerV = flag && XString_toInt(flag, NULL, 10) != 0;
                flag = chart_attribute(attributes, "swapHeaders");
                pending.m_swapHeaders = flag && XString_toInt(flag, NULL, 10) != 0;
                inSeries = true;
            } else if (name && XString_equals_utf8(name, "f", XChar_CaseSensitive) && inSeries) {
                const XString* formula = XXmlStreamReader_readElementText(reader,
                    XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                set_chart_sheet_from_formula(self, formula ? XString_toUtf8(formula) : NULL);
                char normalized[160];
                normalize_chart_range(formula ? XString_toUtf8(formula) : NULL,
                                      normalized, sizeof(normalized));
                pending.m_range = XCellRange_create_str_utf8(normalized);
            } else if (name && XString_equals_utf8(name, "style", XChar_CaseSensitive)) {
                const XString* style = chart_attribute(XXmlStreamReader_attributes(reader), "val");
                if (style) self->m_chartStyle = XString_toInt(style, NULL, 10);
            } else if (name && XString_equals_utf8(name, "legendPos", XChar_CaseSensitive)) {
                const XString* valueString = chart_attribute(XXmlStreamReader_attributes(reader), "val");
                self->m_legendPos = XString_equals_utf8(valueString, "l", XChar_CaseSensitive) ?
                    XExcelChart_AxisPosLeft :
                    XString_equals_utf8(valueString, "t", XChar_CaseSensitive) ?
                    XExcelChart_AxisPosTop :
                    XString_equals_utf8(valueString, "b", XChar_CaseSensitive) ?
                    XExcelChart_AxisPosBottom : XExcelChart_AxisPosRight;
            } else if (name && XString_equals_utf8(name, "overlay", XChar_CaseSensitive)) {
                const XString* overlay = chart_attribute(XXmlStreamReader_attributes(reader), "val");
                self->m_legendOverlay = overlay && XString_toInt(overlay, NULL, 10) != 0;
            } else if (name && XString_equals_utf8(name, "settings", XChar_CaseSensitive)) {
                const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
#define READ_INT_ATTRIBUTE(field, key) do { const XString* a = chart_attribute(attributes, (key)); if (a) (field) = XString_toInt(a, NULL, 10); } while (0)
                READ_INT_ATTRIBUTE(self->m_row, "row");
                READ_INT_ATTRIBUTE(self->m_col, "col");
                READ_INT_ATTRIBUTE(self->m_rowOffset, "rowOffset");
                READ_INT_ATTRIBUTE(self->m_colOffset, "colOffset");
                READ_INT_ATTRIBUTE(self->m_width, "width");
                READ_INT_ATTRIBUTE(self->m_height, "height");
                READ_INT_ATTRIBUTE(self->m_majorGridlinesEnable, "majorGridlines");
                READ_INT_ATTRIBUTE(self->m_minorGridlinesEnable, "minorGridlines");
#undef READ_INT_ATTRIBUTE
            } else if (name && XString_equals_utf8(name, "axis", XChar_CaseSensitive)) {
                const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
                const XString* position = chart_attribute(attributes, "pos");
                const XString* title = chart_attribute(attributes, "title");
                XExcelChart_setAxisTitle(self, XString_equals_utf8(position, "left", XChar_CaseSensitive) ?
                    XExcelChart_AxisPosLeft :
                    XString_equals_utf8(position, "right", XChar_CaseSensitive) ?
                    XExcelChart_AxisPosRight :
                    XString_equals_utf8(position, "top", XChar_CaseSensitive) ?
                    XExcelChart_AxisPosTop : XExcelChart_AxisPosBottom, title);
            }
        } else if (token == XXmlStream_EndElement) {
            if (name && XString_equals_utf8(name, "title", XChar_CaseSensitive)) inTitle = false;
            else if (name && XString_equals_utf8(name, "ser", XChar_CaseSensitive)) {
                if (inSeries && XCellRange_isValid(&pending.m_range))
                    XVector_push_back_2(self->m_series, &pending, 1);
                inSeries = false;
            }
        }
    }
    bool ok = !XXmlStreamReader_hasError(reader) && self->m_chartType != XExcelChart_NoStatementChart;
    XXmlStreamReader_delete_base(reader);
    return ok;
}

bool XExcelChart_loadFromXmlFile(XExcelChart* self, const XString* filePath)
{
    if (!self || !filePath) return false;
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        return false;
    }
    XByteArray* data = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    bool ok = data && XExcelChart_loadFromXmlData(self, XByteArray_data(data),
        XByteArray_size_base((XContainer*)data));
    if (data) XByteArray_delete_base(data);
    return ok;
}

/* ========== UTF-8 便捷变体 ========== */

void XExcelChart_setChartTitle_utf8(XExcelChart* self, const char* title)
{
    XString* s = title ? XString_create_utf8(title) : NULL;
    XExcelChart_setChartTitle(self, s);
    if (s) XString_delete_base(s);
}
