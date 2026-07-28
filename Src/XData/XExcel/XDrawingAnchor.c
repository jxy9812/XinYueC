/******************************************************************************
 * @file       XDrawingAnchor.c
 * @brief      XDrawingAnchor 绘图锚点实现
 *             对齐 QXlsx::DrawingAnchor 全部功能
 ******************************************************************************/
#include "XDrawingAnchor.h"
#include "XDrawing.h"
#include "XMemory.h"
#include "XByteArray.h"
#include "XXmlStreamWriter.h"
#include "XXmlStreamReader.h"
#include "XMediaFile.h"
#include "XChart.h"
#include "XFile.h"
#include "XIODevice.h"
#include "XClass.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* EMU: English Metric Units, 1 inch = 914400 EMU, 1 pixel = 9525 EMU */

/* ========== 创建与删除 ========== */
XDrawingAnchor* XDrawingAnchor_create(XDrawing* drawing, XDrawingAnchor_ObjectType objectType) {
    XDrawingAnchor* self = (XDrawingAnchor*)XMalloc_System(sizeof(XDrawingAnchor));
    if (!self) return NULL;
    memset(self, 0, sizeof(XDrawingAnchor));
    self->m_drawing = drawing;
    self->m_objectType = objectType;
    self->m_id = 1;
    /* 分配ID */
    return self;
}

void XDrawingAnchor_delete(XDrawingAnchor* self) {
    if (!self) return;
    if (self->m_pictureFile) XMediaFile_delete(self->m_pictureFile);
    XFree_System(self);
}

/* ========== 对象设置 ========== */
static bool mime_details(const XString* mimeType, const char** suffix)
{
    if (!mimeType || !suffix) return false;
    if (XString_equals_utf8(mimeType, "image/png", XChar_CaseSensitive)) *suffix = "png";
    else if (XString_equals_utf8(mimeType, "image/jpeg", XChar_CaseSensitive)) *suffix = "jpeg";
    else if (XString_equals_utf8(mimeType, "image/gif", XChar_CaseSensitive)) *suffix = "gif";
    else if (XString_equals_utf8(mimeType, "image/bmp", XChar_CaseSensitive)) *suffix = "bmp";
    else return false;
    return true;
}

static const char* mime_from_path(const XString* path, const char** suffix)
{
    if (!path || !suffix) return NULL;
    if (XString_endsWith_utf8(path, ".png", XChar_CaseInsensitive)) { *suffix = "png"; return "image/png"; }
    if (XString_endsWith_utf8(path, ".jpg", XChar_CaseInsensitive) ||
        XString_endsWith_utf8(path, ".jpeg", XChar_CaseInsensitive)) {
        *suffix = "jpeg"; return "image/jpeg";
    }
    if (XString_endsWith_utf8(path, ".gif", XChar_CaseInsensitive)) { *suffix = "gif"; return "image/gif"; }
    if (XString_endsWith_utf8(path, ".bmp", XChar_CaseInsensitive)) { *suffix = "bmp"; return "image/bmp"; }
    return NULL;
}

void XDrawingAnchor_setPicture(XDrawingAnchor* self, const XString* imagePath) {
    if (!self || !imagePath) return;
    const char* suffix = NULL;
    const char* mimeType = mime_from_path(imagePath, &suffix);
    if (!mimeType) return;
    XFile* file = XFile_create_2((XString*)imagePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        return;
    }
    XByteArray* bytes = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    if (bytes) {
        XString_Init_Utf8(mime, mimeType);
        XDrawingAnchor_setPictureFromData(self, (const uint8_t*)XByteArray_data(bytes),
                                          XByteArray_size_base((XContainer*)bytes), mime);
        XString_deinit_base(mime);
        XByteArray_delete_base(bytes);
    }
}

bool XDrawingAnchor_setPictureFromData(XDrawingAnchor* self, const uint8_t* data, size_t len, const XString* mimeType) {
    if (!self || !data || len == 0 || !mimeType) return false;
    const char* suffix = NULL;
    if (!mime_details(mimeType, &suffix)) return false;
    XString_Init_Utf8(suffixString, suffix);
    XMediaFile* media = XMediaFile_create_data(data, len, suffixString, mimeType);
    XString_deinit_base(suffixString);
    if (!media || XMediaFile_contentsSize(media) != len) {
        if (media) XMediaFile_delete(media);
        return false;
    }
    if (self->m_pictureFile) XMediaFile_delete(self->m_pictureFile);
    self->m_pictureFile = media;
    self->m_chartFile = NULL;
    self->m_objectType = XDAnchor_Picture;
    return true;
}

void XDrawingAnchor_setChart(XDrawingAnchor* self, XChart* chart) {
    if (!self) return;
    if (self->m_pictureFile) {
        XMediaFile_delete(self->m_pictureFile);
        self->m_pictureFile = NULL;
    }
    self->m_chartFile = chart;
    self->m_objectType = chart ? XDAnchor_GraphicFrame : XDAnchor_Unknown;
}

bool XDrawingAnchor_getPicture(XDrawingAnchor* self, XByteArray* outData) {
    if (!self || !self->m_pictureFile || !outData) return false;
    const uint8_t* contents = XMediaFile_contents(self->m_pictureFile);
    size_t size = XMediaFile_contentsSize(self->m_pictureFile);
    if (!contents || size == 0) return false;
    XByteArray_clear_base(outData);
    return XByteArray_push_back_2(outData, contents, size);
}

/* ========== 位置查询 ========== */
int XDrawingAnchor_row(const XDrawingAnchor* self) {
    return self ? self->m_row : -1;
}
int XDrawingAnchor_col(const XDrawingAnchor* self) {
    return self ? self->m_col : -1;
}
int XDrawingAnchor_id(const XDrawingAnchor* self) {
    return self ? self->m_id : -1;
}
XDrawingAnchor_Type XDrawingAnchor_anchorType(const XDrawingAnchor* self) {
    return self ? self->m_anchorType : XDAnchor_Unknown;
}

/* ========== 辅助：写Marker ========== */
static void write_marker(XXmlStreamWriter* w, const char* tag, const XlsxMarker* m) {
    XXmlStreamWriter_writeStartElement_utf8(w, tag);
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", m->m_col);
        XXmlStreamWriter_writeTextElement_utf8(w, "xdr:col", buf);
        snprintf(buf, sizeof(buf), "%d", m->m_colOffset);
        XXmlStreamWriter_writeTextElement_utf8(w, "xdr:colOff", buf);
    }
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", m->m_row);
        XXmlStreamWriter_writeTextElement_utf8(w, "xdr:row", buf);
        snprintf(buf, sizeof(buf), "%d", m->m_rowOffset);
        XXmlStreamWriter_writeTextElement_utf8(w, "xdr:rowOff", buf);
    }
    XXmlStreamWriter_writeEndElement(w);
}

/* ========== 辅助：读Marker ========== */
static bool xml_name_is(const XString* name, const char* localName)
{
    return name && localName && XString_endsWith_utf8(name, localName, XChar_CaseSensitive);
}

static int attribute_int(const XXmlStreamAttributes* attributes, const char* name, int fallback)
{
    if (!attributes || !name) return fallback;
    XString_Init_Utf8(key, name);
    const XString* value = XXmlStreamAttributes_value_ex(attributes, NULL, key);
    int result = value ? atoi(XString_toUtf8(value)) : fallback;
    XString_deinit_base(key);
    return result;
}

static bool read_marker(XXmlStreamReader* reader, XlsxMarker* out) {
    memset(out, 0, sizeof(XlsxMarker));
    while (!XXmlStreamReader_atEnd(reader)) {
        int tt = XXmlStreamReader_readNext(reader);
        if (tt == XXmlStream_StartElement) {
            const XString* name = XXmlStreamReader_name(reader);
            if (!name) continue;
            if (xml_name_is(name, "col")) {
                const XString* t = XXmlStreamReader_readElementText(reader, XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (t) out->m_col = atoi(XString_toUtf8(t));
            } else if (xml_name_is(name, "colOff")) {
                const XString* t = XXmlStreamReader_readElementText(reader, XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (t) out->m_colOffset = atoi(XString_toUtf8(t));
            } else if (xml_name_is(name, "row")) {
                const XString* t = XXmlStreamReader_readElementText(reader, XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (t) out->m_row = atoi(XString_toUtf8(t));
            } else if (xml_name_is(name, "rowOff")) {
                const XString* t = XXmlStreamReader_readElementText(reader, XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (t) out->m_rowOffset = atoi(XString_toUtf8(t));
            }
        } else if (tt == XXmlStream_EndElement) {
            const XString* name = XXmlStreamReader_name(reader);
            if (xml_name_is(name, "from") || xml_name_is(name, "to"))
                break;
        }
    }
    return true;
}

/* ========== XML 序列化 ========== */
static void write_anchor_object(const XDrawingAnchor* self, XXmlStreamWriter* writer)
{
    char id[32];
    char relationshipId[32];
    snprintf(id, sizeof(id), "%d", self->m_id);
    snprintf(relationshipId, sizeof(relationshipId), "rId%d", self->m_id);
    if (self->m_chartFile) {
        XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:graphicFrame");
        XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:nvGraphicFramePr");
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "xdr:cNvPr");
        XXmlStreamWriter_writeAttribute_utf8(writer, "id", id);
        XXmlStreamWriter_writeAttribute_utf8(writer, "name", "Chart");
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "xdr:cNvGraphicFramePr");
        XXmlStreamWriter_writeEndElement(writer);
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "xdr:xfrm");
        XXmlStreamWriter_writeStartElement_utf8(writer, "a:graphic");
        XXmlStreamWriter_writeStartElement_utf8(writer, "a:graphicData");
        XXmlStreamWriter_writeAttribute_utf8(writer, "uri",
            "http://schemas.openxmlformats.org/drawingml/2006/chart");
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "c:chart");
        XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:c",
            "http://schemas.openxmlformats.org/drawingml/2006/chart");
        XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:r",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships");
        XXmlStreamWriter_writeAttribute_utf8(writer, "r:id", relationshipId);
        XXmlStreamWriter_writeEndElement(writer);
        XXmlStreamWriter_writeEndElement(writer);
        XXmlStreamWriter_writeEndElement(writer);
    } else if (self->m_pictureFile) {
        XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:pic");
        XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:nvPicPr");
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "xdr:cNvPr");
        XXmlStreamWriter_writeAttribute_utf8(writer, "id", id);
        XXmlStreamWriter_writeAttribute_utf8(writer, "name", "Picture");
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "xdr:cNvPicPr");
        XXmlStreamWriter_writeEndElement(writer);
        XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:blipFill");
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "a:blip");
        XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:r",
            "http://schemas.openxmlformats.org/officeDocument/2006/relationships");
        XXmlStreamWriter_writeAttribute_utf8(writer, "r:embed", relationshipId);
        XXmlStreamWriter_writeStartElement_utf8(writer, "a:stretch");
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "a:fillRect");
        XXmlStreamWriter_writeEndElement(writer);
        XXmlStreamWriter_writeEndElement(writer);
        XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:spPr");
        XXmlStreamWriter_writeStartElement_utf8(writer, "a:prstGeom");
        XXmlStreamWriter_writeAttribute_utf8(writer, "prst", "rect");
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "a:avLst");
        XXmlStreamWriter_writeEndElement(writer);
        XXmlStreamWriter_writeEndElement(writer);
        XXmlStreamWriter_writeEndElement(writer);
    }
}

bool XDrawingAnchor_saveToXml(const XDrawingAnchor* self, void* writer_) {
    XXmlStreamWriter* writer = (XXmlStreamWriter*)writer_;
    if (!self || !writer || (!self->m_chartFile && !self->m_pictureFile)) return false;

    XlsxMarker from = {self->m_row, self->m_col, self->m_rowOffset, self->m_colOffset};
    if (self->m_anchorType == XDAnchor_Absolute) {
        char buffer[32];
        XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:absoluteAnchor");
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "xdr:pos");
        snprintf(buffer, sizeof(buffer), "%d", self->m_x);
        XXmlStreamWriter_writeAttribute_utf8(writer, "x", buffer);
        snprintf(buffer, sizeof(buffer), "%d", self->m_y);
        XXmlStreamWriter_writeAttribute_utf8(writer, "y", buffer);
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "xdr:ext");
        snprintf(buffer, sizeof(buffer), "%d", self->m_width);
        XXmlStreamWriter_writeAttribute_utf8(writer, "cx", buffer);
        snprintf(buffer, sizeof(buffer), "%d", self->m_height);
        XXmlStreamWriter_writeAttribute_utf8(writer, "cy", buffer);
    } else if (self->m_anchorType == XDAnchor_OneCell) {
        char buffer[32];
        XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:oneCellAnchor");
        write_marker(writer, "xdr:from", &from);
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "xdr:ext");
        snprintf(buffer, sizeof(buffer), "%d", self->m_width);
        XXmlStreamWriter_writeAttribute_utf8(writer, "cx", buffer);
        snprintf(buffer, sizeof(buffer), "%d", self->m_height);
        XXmlStreamWriter_writeAttribute_utf8(writer, "cy", buffer);
    } else {
        XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:twoCellAnchor");
        XlsxMarker to = {self->m_toRow, self->m_toCol,
                         self->m_toRowOffset, self->m_toColOffset};
        write_marker(writer, "xdr:from", &from);
        write_marker(writer, "xdr:to", &to);
    }
    write_anchor_object(self, writer);
    XXmlStreamWriter_writeEmptyElement_utf8(writer, "xdr:clientData");
    XXmlStreamWriter_writeEndElement(writer);
    return !XXmlStreamWriter_hasError(writer);
}

static bool load_anchor_xml(XDrawingAnchor* self, XXmlStreamReader* reader)
{
    const XString* element = XXmlStreamReader_name(reader);
    const char* anchorEnd = NULL;
    if (xml_name_is(element, "twoCellAnchor")) {
        self->m_anchorType = XDAnchor_TwoCell;
        anchorEnd = "twoCellAnchor";
    } else if (xml_name_is(element, "oneCellAnchor")) {
        self->m_anchorType = XDAnchor_OneCell;
        anchorEnd = "oneCellAnchor";
    } else if (xml_name_is(element, "absoluteAnchor")) {
        self->m_anchorType = XDAnchor_Absolute;
        anchorEnd = "absoluteAnchor";
    } else {
        return false;
    }

    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        const XString* name = XXmlStreamReader_name(reader);
        if (token == XXmlStream_StartElement) {
            if (xml_name_is(name, "from")) {
                XlsxMarker marker;
                if (!read_marker(reader, &marker)) return false;
                self->m_row = marker.m_row;
                self->m_col = marker.m_col;
                self->m_rowOffset = marker.m_rowOffset;
                self->m_colOffset = marker.m_colOffset;
            } else if (xml_name_is(name, "to")) {
                XlsxMarker marker;
                if (!read_marker(reader, &marker)) return false;
                self->m_toRow = marker.m_row;
                self->m_toCol = marker.m_col;
                self->m_toRowOffset = marker.m_rowOffset;
                self->m_toColOffset = marker.m_colOffset;
            } else if (xml_name_is(name, "pos")) {
                const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
                self->m_x = attribute_int(attributes, "x", self->m_x);
                self->m_y = attribute_int(attributes, "y", self->m_y);
            } else if (xml_name_is(name, "ext")) {
                const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
                self->m_width = attribute_int(attributes, "cx", self->m_width);
                self->m_height = attribute_int(attributes, "cy", self->m_height);
            } else if (xml_name_is(name, "pic")) {
                self->m_objectType = XDAnchor_Picture;
            } else if (xml_name_is(name, "graphicFrame")) {
                self->m_objectType = XDAnchor_GraphicFrame;
            }
        } else if (token == XXmlStream_EndElement && xml_name_is(name, anchorEnd)) {
            return !XXmlStreamReader_hasError(reader);
        } else if (token == XXmlStream_Invalid) {
            return false;
        }
    }
    return false;
}

bool XDrawingAnchor_loadFromXml(XDrawingAnchor* self, void* reader_) {
    XXmlStreamReader* reader = (XXmlStreamReader*)reader_;
    if (!self || !reader) return false;

    return load_anchor_xml(self, reader);
}
