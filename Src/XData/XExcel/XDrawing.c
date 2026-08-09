/******************************************************************************
 * @file       XDrawing.c
 * @brief      XDrawing 绘图容器类实现
 *             对齐 QXlsx::Drawing 全部功能
 ******************************************************************************/
#include "XDrawing.h"
#include "XMemory.h"
#include "XByteArray.h"
#include "XFile.h"
#include "XXmlStreamWriter.h"
#include "XXmlStreamReader.h"
#include "XDrawingAnchor.h"
#include <string.h>

XDrawing* XDrawing_create(XAbstractSheet* sheet, XAbstractOOXmlFile_CreateFlag flag) {
    XDrawing* self = (XDrawing*)XMalloc_System(sizeof(XDrawing));
    if (!self) return NULL;
    memset(self, 0, sizeof(XDrawing));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_sheet = sheet;
    self->m_anchors = XVector_Create(XDrawingAnchor*);
    return self;
}

void XDrawing_delete(XDrawing* self) {
    if (!self) return;
    if (self->m_anchors) {
        size_t n = XVector_size_base(self->m_anchors);
        for (size_t i = 0; i < n; i++) {
            XDrawingAnchor** pp = (XDrawingAnchor**)XVector_at_base(self->m_anchors, i);
            if (pp && *pp) XDrawingAnchor_delete(*pp);
        }
        XVector_delete_base(self->m_anchors);
    }
    XAbstractOOXmlFile_deinit(&self->m_base);
    XFree_System(self);
}

bool XDrawing_saveToXmlData(const XDrawing* self, uint8_t** data, size_t* length) {
    if (!self || !data || !length || !self->m_anchors) return false;
    *data = NULL;
    *length = 0;
    XXmlStreamWriter* writer = XXmlStreamWriter_create();
    if (!writer) return false;
    XXmlStreamWriter_setAutoFormatting(writer, true);
    XXmlStreamWriter_setAutoFormattingIndent(writer, 1);
    XXmlStreamWriter_writeStartDocument_ex_utf8(writer, "1.0");
    XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:wsDr");
    XXmlStreamWriter_writeNamespace_utf8(writer, "http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing", "xdr");
    XXmlStreamWriter_writeNamespace_utf8(writer, "http://schemas.openxmlformats.org/drawingml/2006/main", "a");

    /* 写入每个锚点 */
    size_t n = XVector_size_base(self->m_anchors);
    for (size_t i = 0; i < n; i++) {
        XDrawingAnchor** pp = (XDrawingAnchor**)XVector_at_base(self->m_anchors, i);
        if (pp && *pp) {
            XDrawingAnchor_saveToXml(*pp, writer);
        }
    }
    XXmlStreamWriter_writeEndElement(writer); /* xdr:wsDr */
    XXmlStreamWriter_writeEndDocument(writer);
    const char* xml = XXmlStreamWriter_toString(writer);
    bool result = xml && !XXmlStreamWriter_hasError(writer);
    if (result) {
        size_t byteLength = strlen(xml);
        uint8_t* copy = (uint8_t*)XMalloc_System(byteLength + 1);
        if (!copy) result = false;
        else {
            memcpy(copy, xml, byteLength + 1);
            *data = copy;
            *length = byteLength;
        }
    }
    XXmlStreamWriter_delete_base(writer);
    return result;
}

bool XDrawing_saveToXmlFile(XDrawing* self, const XString* filePath) {
    if (!self || !filePath) return false;
    uint8_t* xml = NULL;
    size_t length = 0;
    if (!XDrawing_saveToXmlData(self, &xml, &length)) return false;

    /* 通过 XFile 写入文件 */
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file) { XFree_System(xml); return false; }

    if (!XIODevice_open_base((XIODevice*)file, XIODevice_WriteOnly | XIODevice_Truncate)) {
        XClass_delete_base((XClass*)file);
        XFree_System(xml);
        return false;
    }
    bool result = length == 0 || XIODevice_write_1((XIODevice*)file,
        (const char*)xml, (int64_t)length) == (int64_t)length;
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    XFree_System(xml);
    return result;
}

bool XDrawing_loadFromXmlData(XDrawing* self, const uint8_t* data, size_t length) {
    if (!self || !self->m_anchors || !data || length == 0) return false;
    XByteArray* xmlData = XByteArray_create();
    if (!xmlData || !XByteArray_push_back_2(xmlData, data, length)) {
        if (xmlData) XByteArray_delete_base(xmlData);
        return false;
    }
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!reader) { XByteArray_delete_base(xmlData); return false; }
    XXmlStreamReader_addData(reader, xmlData);
    XByteArray_delete_base(xmlData);

    for (size_t i = 0; i < XVector_size_base(self->m_anchors); ++i) {
        XDrawingAnchor* anchor = *(XDrawingAnchor**)XVector_at_base(self->m_anchors, i);
        if (anchor) XDrawingAnchor_delete(anchor);
    }
    XVector_clear_base(self->m_anchors);

    while (!XXmlStreamReader_atEnd(reader)) {
        int tt = XXmlStreamReader_readNext(reader);
        if (tt == XXmlStream_StartElement) {
            const XString* name = XXmlStreamReader_name(reader);
            if (!name) continue;
            if (XString_equals_utf8(name, "twoCellAnchor", XChar_CaseSensitive) ||
                XString_equals_utf8(name, "oneCellAnchor", XChar_CaseSensitive) ||
                XString_equals_utf8(name, "absoluteAnchor", XChar_CaseSensitive) ||
                XString_equals_utf8(name, "xdr:twoCellAnchor", XChar_CaseSensitive) ||
                XString_equals_utf8(name, "xdr:oneCellAnchor", XChar_CaseSensitive) ||
                XString_equals_utf8(name, "xdr:absoluteAnchor", XChar_CaseSensitive)) {
                XDrawingAnchor* anchor = XDrawingAnchor_create(self, XDAnchor_Unknown);
                if (anchor) {
                    if (!XDrawingAnchor_loadFromXml(anchor, reader) ||
                        !XVector_push_back_2(self->m_anchors, &anchor, 1))
                        XDrawingAnchor_delete(anchor);
                }
            }
        }
    }
    bool ok = !XXmlStreamReader_hasError(reader);
    XXmlStreamReader_delete_base(reader);
    return ok;
}

bool XDrawing_loadFromXmlFile(XDrawing* self, const XString* filePath) {
    if (!self || !filePath) return false;
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        return false;
    }
    XByteArray* xmlData = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    bool result = xmlData && XDrawing_loadFromXmlData(self, XByteArray_data(xmlData),
        XByteArray_size_base((XContainer*)xmlData));
    if (xmlData) XByteArray_delete_base(xmlData);
    return result;
}
