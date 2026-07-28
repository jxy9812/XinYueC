#include "XChartsheet.h"
#include "XWorkbook.h"
#include "XMemory.h"
#include "XByteArray.h"
#include "XXmlStreamReader.h"
#include "XXmlStreamWriter.h"
#include <stdlib.h>
#include <string.h>

XChartsheet* XChartsheet_create(const XString* sheetName, int sheetId, void* book, XAbstractOOXmlFile_CreateFlag flag) {
    XChartsheet* self = (XChartsheet*)XMalloc_System(sizeof(XChartsheet));
    if (!self) return NULL; memset(self, 0, sizeof(XChartsheet));
    XAbstractSheet_init(&self->m_base, sheetName, sheetId, (XWorkbook*)book, flag);
    self->m_base.m_sheetType = XAbstractSheet_ST_ChartSheet;
    return self;
}
void XChartsheet_delete(XChartsheet* self) { if (!self) return; if (self->m_ownsChart && self->m_chart) XChart_delete(self->m_chart); XAbstractSheet_deinit(&self->m_base); XFree_System(self); }
void XChartsheet_setChart(XChartsheet* self, XChart* chart) { if (self) { if (self->m_ownsChart && self->m_chart && self->m_chart != chart) XChart_delete(self->m_chart); self->m_chart = chart; self->m_ownsChart = false; } }
XChart* XChartsheet_chart(const XChartsheet* self) { return self ? self->m_chart : NULL; }

bool XChartsheet_saveToXmlData(const XChartsheet* self, uint8_t** outData, size_t* outLen)
{
    if (!self || !outData || !outLen || !self->m_chart) return false;
    *outData = NULL;
    *outLen = 0;
    XXmlStreamWriter* writer = XXmlStreamWriter_create();
    if (!writer) return false;
    XXmlStreamWriter_writeStartDocument(writer);
    XXmlStreamWriter_writeStartElement_utf8(writer, "chartsheet");
    XXmlStreamWriter_writeDefaultNamespace_utf8(writer,
        "http://schemas.openxmlformats.org/spreadsheetml/2006/main");
    XXmlStreamWriter_writeNamespace_utf8(writer,
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships", "r");
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "sheetPr");
    XXmlStreamWriter_writeStartElement_utf8(writer, "sheetViews");
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "sheetView");
    XXmlStreamWriter_writeAttribute_utf8(writer, "workbookViewId", "0");
    XXmlStreamWriter_writeEndElement(writer);
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "pageMargins");
    XXmlStreamWriter_writeAttribute_utf8(writer, "left", "0.7");
    XXmlStreamWriter_writeAttribute_utf8(writer, "right", "0.7");
    XXmlStreamWriter_writeAttribute_utf8(writer, "top", "0.75");
    XXmlStreamWriter_writeAttribute_utf8(writer, "bottom", "0.75");
    XXmlStreamWriter_writeAttribute_utf8(writer, "header", "0.3");
    XXmlStreamWriter_writeAttribute_utf8(writer, "footer", "0.3");
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "drawing");
    XXmlStreamWriter_writeAttribute_utf8(writer, "r:id", "rId1");
    XXmlStreamWriter_writeEndDocument(writer);
    XByteArray* bytes = XXmlStreamWriter_toByteArray(writer);
    size_t size = bytes ? XByteArray_size_base((XContainer*)bytes) : 0;
    if (!XXmlStreamWriter_hasError(writer) && size > 0) {
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

bool XChartsheet_loadFromXmlData(XChartsheet* self, const uint8_t* data, size_t len)
{
    if (!self || !data || len == 0) return false;
    XByteArray* bytes = XByteArray_create_with_data((const char*)data, len);
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!bytes || !reader) {
        if (bytes) XByteArray_delete_base(bytes);
        if (reader) XXmlStreamReader_delete_base(reader);
        return false;
    }
    XXmlStreamReader_addData(reader, bytes);
    XByteArray_delete_base(bytes);
    bool rootSeen = false;
    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        const XString* name = XXmlStreamReader_name(reader);
        if (token == XXmlStream_StartElement && name &&
            XString_equals_utf8(name, "chartsheet", XChar_CaseSensitive)) rootSeen = true;
    }
    bool ok = rootSeen && !XXmlStreamReader_hasError(reader);
    XXmlStreamReader_delete_base(reader);
    return ok;
}

/* ========== UTF-8 便捷变体 ========== */

XChartsheet* XChartsheet_create_utf8(const char* sheetName, int sheetId, void* book, XAbstractOOXmlFile_CreateFlag flag)
{
    XString* s = sheetName ? XString_create_utf8(sheetName) : NULL;
    XChartsheet* result = XChartsheet_create(s, sheetId, book, flag);
    if (s) XString_delete_base(s);
    return result;
}
