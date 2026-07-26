#include "XChart.h"
#include "XMemory.h"
#include "XFile.h"
#include "XIODevice.h"
#include "XByteArray.h"
#include "XXmlStreamWriter.h"
#include "XXmlStreamReader.h"
#include "XClass.h"
#include <stdlib.h>

#include <string.h>
#include <stdio.h>

XChart* XChart_create(XAbstractSheet* parent, XAbstractOOXmlFile_CreateFlag flag) {
    (void)parent;
    XChart* self = (XChart*)XMalloc_System(sizeof(XChart));
    if (!self) return NULL; memset(self, 0, sizeof(XChart));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_chartType = XChart_NoStatementChart;
    self->m_chartStyle = -1;
    self->m_legendPos = XChart_AxisPosRight;
    self->m_series = XVector_Create(XChart_Series);
    self->m_width = 480; self->m_height = 290;
    return self;
}
XChart* XChart_copy(const XChart* source, XAbstractSheet* parent) {
    if (!source) return NULL;
    XChart* copy = XChart_create(parent, XAbstractOOXmlFile_F_NewFromScratch);
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
    if (source->m_chartTitle) copy->m_chartTitle = XString_create_copy(source->m_chartTitle);
    if (source->m_axisTitleLeft) copy->m_axisTitleLeft = XString_create_copy(source->m_axisTitleLeft);
    if (source->m_axisTitleRight) copy->m_axisTitleRight = XString_create_copy(source->m_axisTitleRight);
    if (source->m_axisTitleTop) copy->m_axisTitleTop = XString_create_copy(source->m_axisTitleTop);
    if (source->m_axisTitleBottom) copy->m_axisTitleBottom = XString_create_copy(source->m_axisTitleBottom);
    size_t count = source->m_series ? XVector_size_base((XContainer*)source->m_series) : 0;
    for (size_t i = 0; i < count; ++i) {
        XChart_Series* series = (XChart_Series*)XVector_at_base(source->m_series, i);
        if (series) XVector_push_back_2(copy->m_series, series, 1);
    }
    return copy;
}
void XChart_delete(XChart* self) {
    if (!self) return;
    if (self->m_chartTitle) XString_delete_base(self->m_chartTitle);
    if (self->m_axisTitleLeft) XString_delete_base(self->m_axisTitleLeft);
    if (self->m_axisTitleRight) XString_delete_base(self->m_axisTitleRight);
    if (self->m_axisTitleTop) XString_delete_base(self->m_axisTitleTop);
    if (self->m_axisTitleBottom) XString_delete_base(self->m_axisTitleBottom);
    if (self->m_series) XVector_delete_base(self->m_series);
    XAbstractOOXmlFile_deinit(&self->m_base); XFree_System(self);
}
void XChart_addSeries(XChart* self, const XCellRange* range, bool headerH, bool headerV, bool swapHeaders) {
    if (!self || !range) return;
    XChart_Series s; memset(&s, 0, sizeof(s)); s.m_range = *range; s.m_headerH = headerH; s.m_headerV = headerV; s.m_swapHeaders = swapHeaders;
    XVector_push_back_2(self->m_series, &s, 1);
}
void XChart_setChartType(XChart* self, XChart_ChartType type) { if (self) self->m_chartType = type; }
void XChart_setChartStyle(XChart* self, int id) { if (self) self->m_chartStyle = id; }
void XChart_setAxisTitle(XChart* self, XChart_ChartAxisPos pos, const XString* axisTitle) {
    if (!self) return;
    XString** target = NULL;
    if (pos == XChart_AxisPosLeft) target = &self->m_axisTitleLeft;
    else if (pos == XChart_AxisPosRight) target = &self->m_axisTitleRight;
    else if (pos == XChart_AxisPosTop) target = &self->m_axisTitleTop;
    else if (pos == XChart_AxisPosBottom) target = &self->m_axisTitleBottom;
    if (target) { if (!*target) *target = XString_create(); if (*target) { XString_clear_base(*target); if (axisTitle) XString_append(*target, axisTitle); } }
}
void XChart_setChartTitle(XChart* self, const XString* title) {
    if (!self) return;
    if (!self->m_chartTitle) self->m_chartTitle = XString_create();
    if (self->m_chartTitle) { XString_clear_base(self->m_chartTitle); if (title) XString_append(self->m_chartTitle, title); }
}
void XChart_setChartLegend(XChart* self, XChart_ChartAxisPos legendPos, bool overlap) { if (self) { self->m_legendPos = legendPos; self->m_legendOverlay = overlap; } }
void XChart_setGridlinesEnable(XChart* self, bool majorEnable, bool minorEnable) { if (self) { self->m_majorGridlinesEnable = majorEnable; self->m_minorGridlinesEnable = minorEnable; } }
void XChart_setSize(XChart* self, int width, int height) { if (self) { self->m_width = width; self->m_height = height; } }
void XChart_setPosition(XChart* self, int row, int col, int rowOff, int colOff) { if (self) { self->m_row = row; self->m_col = col; self->m_rowOffset = rowOff; self->m_colOffset = colOff; } }

static const char* chart_type_name(XChart_ChartType type)
{
    static const char* names[] = {
        "", "areaChart", "area3DChart", "lineChart", "line3DChart", "stockChart",
        "radarChart", "scatterChart", "pieChart", "pie3DChart", "doughnutChart",
        "barChart", "bar3DChart", "ofPieChart", "surfaceChart", "surface3DChart",
        "bubbleChart"
    };
    return (type > XChart_NoStatementChart && type <= XChart_BubbleChart)
        ? names[(int)type] : "lineChart";
}

static XChart_ChartType chart_type_from_name(const XString* name)
{
    if (!name) return XChart_NoStatementChart;
    for (int type = XChart_AreaChart; type <= XChart_BubbleChart; ++type) {
        if (XString_equals_utf8(name, chart_type_name((XChart_ChartType)type), XChar_CaseSensitive))
            return (XChart_ChartType)type;
    }
    return XChart_NoStatementChart;
}

static const char* legend_position_name(XChart_ChartAxisPos position)
{
    switch (position) {
        case XChart_AxisPosLeft: return "l";
        case XChart_AxisPosTop: return "t";
        case XChart_AxisPosBottom: return "b";
        case XChart_AxisPosRight: return "r";
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
    XXmlStreamWriter_writeStartElement_utf8(writer, "a:p");
    XXmlStreamWriter_writeStartElement_utf8(writer, "a:r");
    XXmlStreamWriter_writeTextElement_utf8(writer, "a:t", XString_toUtf8(title));
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
}

static void write_axis_extension(XXmlStreamWriter* writer, const char* position, const XString* title)
{
    if (!title || XString_isEmpty_base(title)) return;
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "xync:axis");
    XXmlStreamWriter_writeAttribute_utf8(writer, "pos", position);
    XXmlStreamWriter_writeAttribute_utf8(writer, "title", XString_toUtf8(title));
}

static bool write_chart_xml(XChart* self, XXmlStreamWriter* writer)
{
    if (!self || !writer) return false;
    XXmlStreamWriter_writeStartDocument_ex_utf8(writer, "1.0");
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:chartSpace");
    XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:c",
        "http://schemas.openxmlformats.org/drawingml/2006/chart");
    XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:a",
        "http://schemas.openxmlformats.org/drawingml/2006/main");
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
    size_t seriesCount = XVector_size_base((XContainer*)self->m_series);
    for (size_t i = 0; i < seriesCount; ++i) {
        const XChart_Series* series = (const XChart_Series*)XVector_at_base(self->m_series, i);
        if (!series || !XCellRange_isValid(&series->m_range)) continue;
        char index[32];
        snprintf(index, sizeof(index), "%zu", i);
        XXmlStreamWriter_writeStartElement_utf8(writer, "c:ser");
        XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:xync", "urn:xinyuec:xlsx:chart");
        XXmlStreamWriter_writeAttribute_utf8(writer, "xync:headerH", series->m_headerH ? "1" : "0");
        XXmlStreamWriter_writeAttribute_utf8(writer, "xync:headerV", series->m_headerV ? "1" : "0");
        XXmlStreamWriter_writeAttribute_utf8(writer, "xync:swapHeaders", series->m_swapHeaders ? "1" : "0");
        write_value_element(writer, "c:idx", index);
        write_value_element(writer, "c:order", index);
        XXmlStreamWriter_writeStartElement_utf8(writer, "c:val");
        XXmlStreamWriter_writeStartElement_utf8(writer, "c:numRef");
        XString range = XCellRange_toString(&series->m_range, true, true);
        XXmlStreamWriter_writeTextElement_utf8(writer, "c:f", XString_toUtf8(&range));
        XString_deinit_base(&range);
        XXmlStreamWriter_writeEndElement(writer);
        XXmlStreamWriter_writeEndElement(writer);
        XXmlStreamWriter_writeEndElement(writer);
    }
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
    if (self->m_legendPos != XChart_AxisPosNone) {
        XXmlStreamWriter_writeStartElement_utf8(writer, "c:legend");
        write_value_element(writer, "c:legendPos", legend_position_name(self->m_legendPos));
        write_value_element(writer, "c:overlay", self->m_legendOverlay ? "1" : "0");
        XXmlStreamWriter_writeEndElement(writer);
    }
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:extLst");
    XXmlStreamWriter_writeStartElement_utf8(writer, "c:ext");
    XXmlStreamWriter_writeAttribute_utf8(writer, "uri", "{B59E14A5-612A-4A8D-9C04-XINYUEC}");
    XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:xync", "urn:xinyuec:xlsx:chart");
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "xync:settings");
    char value[32];
#define WRITE_INT_ATTRIBUTE(name, number) do { snprintf(value, sizeof(value), "%d", (number)); XXmlStreamWriter_writeAttribute_utf8(writer, (name), value); } while (0)
    WRITE_INT_ATTRIBUTE("row", self->m_row);
    WRITE_INT_ATTRIBUTE("col", self->m_col);
    WRITE_INT_ATTRIBUTE("rowOffset", self->m_rowOffset);
    WRITE_INT_ATTRIBUTE("colOffset", self->m_colOffset);
    WRITE_INT_ATTRIBUTE("width", self->m_width);
    WRITE_INT_ATTRIBUTE("height", self->m_height);
    XXmlStreamWriter_writeAttribute_utf8(writer, "majorGridlines", self->m_majorGridlinesEnable ? "1" : "0");
    XXmlStreamWriter_writeAttribute_utf8(writer, "minorGridlines", self->m_minorGridlinesEnable ? "1" : "0");
#undef WRITE_INT_ATTRIBUTE
    write_axis_extension(writer, "left", self->m_axisTitleLeft);
    write_axis_extension(writer, "right", self->m_axisTitleRight);
    write_axis_extension(writer, "top", self->m_axisTitleTop);
    write_axis_extension(writer, "bottom", self->m_axisTitleBottom);
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEndDocument(writer);

    return !XXmlStreamWriter_hasError(writer);
}

bool XChart_saveToXmlData(XChart* self, uint8_t** outData, size_t* outLen)
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

bool XChart_saveToXmlFile(XChart* self, const XString* filePath)
{
    if (!self || !filePath) return false;
    uint8_t* xml = NULL;
    size_t size = 0;
    if (!XChart_saveToXmlData(self, &xml, &size)) return false;
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

bool XChart_loadFromXmlData(XChart* self, const uint8_t* bytes, size_t len)
{
    if (!self || !bytes || len == 0) return false;
    XByteArray* data = XByteArray_create_with_data((const char*)bytes, len);
    if (!data) return false;
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!reader) { XByteArray_delete_base(data); return false; }
    XXmlStreamReader_addData(reader, data);
    XByteArray_delete_base(data);
    XVector_clear_base(self->m_series);
    XChart_Series pending;
    memset(&pending, 0, sizeof(pending));
    pending.m_range = XCellRange_create();
    bool inSeries = false;
    bool inTitle = false;

    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        const XString* name = XXmlStreamReader_name_const(reader);
        if (token == XXmlStream_StartElement) {
            XChart_ChartType type = chart_type_from_name(name);
            if (type != XChart_NoStatementChart) self->m_chartType = type;
            if (name && XString_equals_utf8(name, "title", XChar_CaseSensitive)) inTitle = true;
            else if (name && XString_equals_utf8(name, "t", XChar_CaseSensitive) && inTitle) {
                const XString* title = XXmlStreamReader_readElementText_const(reader,
                    XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                XChart_setChartTitle(self, title);
            } else if (name && XString_equals_utf8(name, "ser", XChar_CaseSensitive)) {
                memset(&pending, 0, sizeof(pending));
                pending.m_range = XCellRange_create();
                const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
                const XString* flag = chart_attribute(attributes, "headerH");
                pending.m_headerH = flag && atoi(XString_toUtf8(flag)) != 0;
                flag = chart_attribute(attributes, "headerV");
                pending.m_headerV = flag && atoi(XString_toUtf8(flag)) != 0;
                flag = chart_attribute(attributes, "swapHeaders");
                pending.m_swapHeaders = flag && atoi(XString_toUtf8(flag)) != 0;
                inSeries = true;
            } else if (name && XString_equals_utf8(name, "f", XChar_CaseSensitive) && inSeries) {
                const XString* formula = XXmlStreamReader_readElementText_const(reader,
                    XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                char normalized[160];
                normalize_chart_range(formula ? XString_toUtf8(formula) : NULL,
                                      normalized, sizeof(normalized));
                pending.m_range = XCellRange_create_str_utf8(normalized);
            } else if (name && XString_equals_utf8(name, "style", XChar_CaseSensitive)) {
                const XString* style = chart_attribute(XXmlStreamReader_attributes(reader), "val");
                if (style) self->m_chartStyle = atoi(XString_toUtf8(style));
            } else if (name && XString_equals_utf8(name, "legendPos", XChar_CaseSensitive)) {
                const XString* valueString = chart_attribute(XXmlStreamReader_attributes(reader), "val");
                self->m_legendPos = XString_equals_utf8(valueString, "l", XChar_CaseSensitive) ?
                    XChart_AxisPosLeft :
                    XString_equals_utf8(valueString, "t", XChar_CaseSensitive) ?
                    XChart_AxisPosTop :
                    XString_equals_utf8(valueString, "b", XChar_CaseSensitive) ?
                    XChart_AxisPosBottom : XChart_AxisPosRight;
            } else if (name && XString_equals_utf8(name, "overlay", XChar_CaseSensitive)) {
                const XString* overlay = chart_attribute(XXmlStreamReader_attributes(reader), "val");
                self->m_legendOverlay = overlay && atoi(XString_toUtf8(overlay)) != 0;
            } else if (name && XString_equals_utf8(name, "settings", XChar_CaseSensitive)) {
                const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
#define READ_INT_ATTRIBUTE(field, key) do { const XString* a = chart_attribute(attributes, (key)); if (a) (field) = atoi(XString_toUtf8(a)); } while (0)
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
                XChart_setAxisTitle(self, XString_equals_utf8(position, "left", XChar_CaseSensitive) ?
                    XChart_AxisPosLeft :
                    XString_equals_utf8(position, "right", XChar_CaseSensitive) ?
                    XChart_AxisPosRight :
                    XString_equals_utf8(position, "top", XChar_CaseSensitive) ?
                    XChart_AxisPosTop : XChart_AxisPosBottom, title);
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
    bool ok = !XXmlStreamReader_hasError(reader) && self->m_chartType != XChart_NoStatementChart;
    XXmlStreamReader_delete_base(reader);
    return ok;
}

bool XChart_loadFromXmlFile(XChart* self, const XString* filePath)
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
    bool ok = data && XChart_loadFromXmlData(self, XByteArray_data(data),
        XByteArray_size_base((XContainer*)data));
    if (data) XByteArray_delete_base(data);
    return ok;
}

/* ========== UTF-8 便捷变体 ========== */

void XChart_setChartTitle_utf8(XChart* self, const char* title)
{
    XString* s = title ? XString_create_utf8(title) : NULL;
    XChart_setChartTitle(self, s);
    if (s) XString_delete_base(s);
}
