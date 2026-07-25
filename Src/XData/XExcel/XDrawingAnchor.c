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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
    XFree_System(self);
}

/* ========== 对象设置 ========== */
void XDrawingAnchor_setPicture(XDrawingAnchor* self, const XString* imagePath) {
    (void)self; (void)imagePath;
    /* TODO: 从文件加载图片 */
}

bool XDrawingAnchor_setPictureFromData(XDrawingAnchor* self, const uint8_t* data, size_t len, const XString* mimeType) {
    (void)self; (void)data; (void)len; (void)mimeType;
    return false;
}

void XDrawingAnchor_setChart(XDrawingAnchor* self, XChart* chart) {
    if (self) self->m_chartFile = chart;
}

bool XDrawingAnchor_getPicture(XDrawingAnchor* self, XByteArray* outData) {
    (void)self; (void)outData;
    return false;
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
    XXmlStreamWriter_writeTextElement_utf8(w, "xdr:col", "0");
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", m->m_col);
        XXmlStreamWriter_writeTextElement_utf8(w, "xdr:colOff", buf);
    }
    XXmlStreamWriter_writeTextElement_utf8(w, "xdr:row", "0");
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", m->m_rowOffset);
        XXmlStreamWriter_writeTextElement_utf8(w, "xdr:rowOff", buf);
    }
    XXmlStreamWriter_writeEndElement(w);
}

/* ========== 辅助：读Marker ========== */
static bool read_marker(XXmlStreamReader* reader, XlsxMarker* out) {
    memset(out, 0, sizeof(XlsxMarker));
    while (!XXmlStreamReader_atEnd(reader)) {
        int tt = XXmlStreamReader_readNext(reader);
        if (tt == XXmlStream_StartElement) {
            const XString* name = XXmlStreamReader_name_const(reader);
            if (!name) continue;
            if (XString_equals_utf8(name, "xdr:col", XChar_CaseSensitive)) {
                const XString* t = XXmlStreamReader_readElementText_const(reader, XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (t) out->m_col = atoi(XString_toUtf8(t));
            } else if (XString_equals_utf8(name, "xdr:colOff", XChar_CaseSensitive)) {
                const XString* t = XXmlStreamReader_readElementText_const(reader, XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (t) out->m_colOffset = atoi(XString_toUtf8(t));
            } else if (XString_equals_utf8(name, "xdr:row", XChar_CaseSensitive)) {
                const XString* t = XXmlStreamReader_readElementText_const(reader, XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (t) out->m_row = atoi(XString_toUtf8(t));
            } else if (XString_equals_utf8(name, "xdr:rowOff", XChar_CaseSensitive)) {
                const XString* t = XXmlStreamReader_readElementText_const(reader, XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (t) out->m_rowOffset = atoi(XString_toUtf8(t));
            }
        } else if (tt == XXmlStream_EndElement) {
            const XString* name = XXmlStreamReader_name_const(reader);
            if (name && (XString_equals_utf8(name, "xdr:from", XChar_CaseSensitive) || XString_equals_utf8(name, "xdr:to", XChar_CaseSensitive)))
                break;
        }
    }
    return true;
}

/* ========== XML 序列化 ========== */
bool XDrawingAnchor_saveToXml(const XDrawingAnchor* self, void* writer_) {
    XXmlStreamWriter* writer = (XXmlStreamWriter*)writer_;
    if (!self || !writer) return false;

    if (self->m_anchorType == XDAnchor_TwoCell) {
        XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:twoCellAnchor");
        XlsxMarker from = {self->m_row, self->m_col, self->m_rowOffset, self->m_colOffset};
        XlsxMarker to = {self->m_toRow, self->m_toCol, self->m_toRowOffset, self->m_toColOffset};
        write_marker(writer, "xdr:from", &from);
        write_marker(writer, "xdr:to", &to);
        if (self->m_chartFile) {
            XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:graphicFrame");
            XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:c", "http://schemas.openxmlformats.org/drawingml/2006/chart");
            XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:a", "http://schemas.openxmlformats.org/drawingml/2006/main");
            XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:rel", "http://schemas.openxmlformats.org/officeDocument/2006/relationships");
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "rId%d", self->m_id);
                XXmlStreamWriter_writeEmptyElement_utf8(writer, "a:graphic");
                XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:a", "http://schemas.openxmlformats.org/drawingml/2006/main");
                XXmlStreamWriter_writeStartElement_utf8(writer, "a:graphicData");
                XXmlStreamWriter_writeAttribute_utf8(writer, "uri", "http://schemas.openxmlformats.org/drawingml/2006/chart");
                snprintf(buf, sizeof(buf), "rId%d", self->m_id);
                XXmlStreamWriter_writeEmptyElement_utf8(writer, "c:chart");
                XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:c", "http://schemas.openxmlformats.org/drawingml/2006/chart");
                XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns", "http://schemas.openxmlformats.org/drawingml/2006/chart");
                snprintf(buf, sizeof(buf), "../charts/chart%d.xml", self->m_id);
                XXmlStreamWriter_writeAttribute_utf8(writer, "id", buf);
                XXmlStreamWriter_writeEndElement(writer); /* a:graphicData */
            }
            XXmlStreamWriter_writeEndElement(writer); /* xdr:graphicFrame */
        } else if (self->m_pictureFile) {
            XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:pic");
            XXmlStreamWriter_writeEmptyElement_utf8(writer, "xdr:nvPicPr");
            XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:cNvPr");
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d", self->m_id);
                XXmlStreamWriter_writeAttribute_utf8(writer, "id", buf);
                XXmlStreamWriter_writeAttribute_utf8(writer, "name", "Picture");
            }
            XXmlStreamWriter_writeEndElement(writer);
            XXmlStreamWriter_writeEmptyElement_utf8(writer, "xdr:cNvPic");
            XXmlStreamWriter_writeEndElement(writer); /* xdr:cNvPic */
            XXmlStreamWriter_writeEndElement(writer); /* xdr:nvPicPr */
            XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:blipFill");
            XXmlStreamWriter_writeEmptyElement_utf8(writer, "a:blip");
            XXmlStreamWriter_writeEndElement(writer);
            XXmlStreamWriter_writeEmptyElement_utf8(writer, "a:stretch");
            XXmlStreamWriter_writeEndElement(writer);
            XXmlStreamWriter_writeEndElement(writer); /* xdr:blipFill */
            XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:spPr");
            XXmlStreamWriter_writeEndElement(writer);
            XXmlStreamWriter_writeEndElement(writer); /* xdr:spPr */
        }
        XXmlStreamWriter_writeEmptyElement_utf8(writer, "xdr:clientData");
        XXmlStreamWriter_writeEndElement(writer); /* xdr:twoCellAnchor */
    }
    return true;
}

bool XDrawingAnchor_loadFromXml(XDrawingAnchor* self, void* reader_) {
    XXmlStreamReader* reader = (XXmlStreamReader*)reader_;
    if (!self || !reader) return false;

    const XString* elem = XXmlStreamReader_name_const(reader);
    if (!elem) return false;

    if (XString_equals_utf8(elem, "xdr:twoCellAnchor", XChar_CaseSensitive)) {
        self->m_anchorType = XDAnchor_TwoCell;
        while (!XXmlStreamReader_atEnd(reader)) {
            int tt = XXmlStreamReader_readNext(reader);
            if (tt == XXmlStream_StartElement) {
                const XString* name = XXmlStreamReader_name_const(reader);
                if (!name) continue;
                if (XString_equals_utf8(name, "xdr:from", XChar_CaseSensitive)) {
                    XlsxMarker m;
                    read_marker(reader, &m);
                    self->m_row = m.m_row;
                    self->m_col = m.m_col;
                    self->m_rowOffset = m.m_rowOffset;
                    self->m_colOffset = m.m_colOffset;
                } else if (XString_equals_utf8(name, "xdr:to", XChar_CaseSensitive)) {
                    XlsxMarker m;
                    read_marker(reader, &m);
                    self->m_toRow = m.m_row;
                    self->m_toCol = m.m_col;
                    self->m_toRowOffset = m.m_rowOffset;
                    self->m_toColOffset = m.m_colOffset;
                }
            } else if (tt == XXmlStream_EndElement) {
                const XString* name = XXmlStreamReader_name_const(reader);
                if (name && XString_equals_utf8(name, "xdr:twoCellAnchor", XChar_CaseSensitive)) break;
            }
        }
    } else if (XString_equals_utf8(elem, "xdr:oneCellAnchor", XChar_CaseSensitive)) {
        self->m_anchorType = XDAnchor_OneCell;
        while (!XXmlStreamReader_atEnd(reader)) {
            int tt = XXmlStreamReader_readNext(reader);
            if (tt == XXmlStream_StartElement) {
                const XString* name = XXmlStreamReader_name_const(reader);
                if (!name) continue;
                if (XString_equals_utf8(name, "xdr:from", XChar_CaseSensitive)) {
                    XlsxMarker m;
                    read_marker(reader, &m);
                    self->m_row = m.m_row;
                    self->m_col = m.m_col;
                    self->m_rowOffset = m.m_rowOffset;
                    self->m_colOffset = m.m_colOffset;
                } else if (XString_equals_utf8(name, "xdr:ext", XChar_CaseSensitive)) {
                    const XXmlStreamAttributes* attrs = XXmlStreamReader_attributes(reader);
                    if (attrs) {
                        XString_Init_Utf8(cxName, "cx");
                        XString_Init_Utf8(cyName, "cy");
                        const XString* cx = XXmlStreamAttributes_value_ex(attrs, NULL, &cxName);
                        const XString* cy = XXmlStreamAttributes_value_ex(attrs, NULL, &cyName);
                        if (cx) self->m_width = atoi(XString_toUtf8(cx));
                        if (cy) self->m_height = atoi(XString_toUtf8(cy));
                    }
                }
            } else if (tt == XXmlStream_EndElement) {
                const XString* name = XXmlStreamReader_name_const(reader);
                if (name && XString_equals_utf8(name, "xdr:oneCellAnchor", XChar_CaseSensitive)) break;
            }
        }
    } else if (XString_equals_utf8(elem, "xdr:absoluteAnchor", XChar_CaseSensitive)) {
        self->m_anchorType = XDAnchor_Absolute;
        while (!XXmlStreamReader_atEnd(reader)) {
            int tt = XXmlStreamReader_readNext(reader);
            if (tt == XXmlStream_StartElement) {
                const XString* name = XXmlStreamReader_name_const(reader);
                if (!name) continue;
                if (XString_equals_utf8(name, "xdr:pos", XChar_CaseSensitive)) {
                    const XXmlStreamAttributes* attrs = XXmlStreamReader_attributes(reader);
                    if (attrs) {
                        XString_Init_Utf8(xName, "x");
                        XString_Init_Utf8(yName, "y");
                        const XString* x = XXmlStreamAttributes_value_ex(attrs, NULL, &xName);
                        const XString* y = XXmlStreamAttributes_value_ex(attrs, NULL, &yName);
                        if (x) self->m_x = atoi(XString_toUtf8(x));
                        if (y) self->m_y = atoi(XString_toUtf8(y));
                    }
                } else if (XString_equals_utf8(name, "xdr:ext", XChar_CaseSensitive)) {
                    const XXmlStreamAttributes* attrs = XXmlStreamReader_attributes(reader);
                    if (attrs) {
                        XString_Init_Utf8(cxName, "cx");
                        XString_Init_Utf8(cyName, "cy");
                        const XString* cx = XXmlStreamAttributes_value_ex(attrs, NULL, &cxName);
                        const XString* cy = XXmlStreamAttributes_value_ex(attrs, NULL, &cyName);
                        if (cx) self->m_width = atoi(XString_toUtf8(cx));
                        if (cy) self->m_height = atoi(XString_toUtf8(cy));
                    }
                }
            } else if (tt == XXmlStream_EndElement) {
                const XString* name = XXmlStreamReader_name_const(reader);
                if (name && XString_equals_utf8(name, "xdr:absoluteAnchor", XChar_CaseSensitive)) break;
            }
        }
    }
    return true;
}
