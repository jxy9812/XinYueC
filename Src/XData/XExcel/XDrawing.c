/******************************************************************************
 * @file       XDrawing.c
 * @brief      XDrawing 绘图容器类实现
 *             对齐 QXlsx::Drawing 全部功能
 ******************************************************************************/
#include "XDrawing.h"
#include "XMemory.h"
#include "XByteArray.h"
#include "XXmlStreamWriter.h"
#include "XXmlStreamReader.h"
#include "XDrawingAnchor.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
        XVector_deinit_base(self->m_anchors);
        XFree_System(self->m_anchors);
    }
    XAbstractOOXmlFile_deinit(&self->m_base);
    XFree_System(self);
}

bool XDrawing_saveToXmlFile(XDrawing* self, const char* filePath) {
    if (!self || !filePath) return false;
    FILE* fp = fopen(filePath, "wb");
    if (!fp) return false;
    fclose(fp);
    /* 使用 XXmlStreamWriter 写字符串到文件 */
    XXmlStreamWriter* writer = XXmlStreamWriter_create();
    XByteArray* buf = XByteArray_create();
    XXmlStreamWriter_setAutoFormatting(writer, true);
    XXmlStreamWriter_setAutoFormattingIndent(writer, 1);
    XXmlStreamWriter_writeStartDocument_ex(writer, "1.0");
    XXmlStreamWriter_writeStartElement(writer, "xdr:wsDr");
    XXmlStreamWriter_writeAttribute(writer, "xmlns:xdr", "http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing");
    XXmlStreamWriter_writeAttribute(writer, "xmlns:a", "http://schemas.openxmlformats.org/drawingml/2006/main");
    XXmlStreamWriter_writeNamespace(writer, "http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing", "xdr");
    XXmlStreamWriter_writeNamespace(writer, "http://schemas.openxmlformats.org/drawingml/2006/main", "a");
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
    FILE* f = fopen(filePath, "wb");
    if (f) {
        fputs(xml ? xml : "", f);
        fclose(f);
    }
    XXmlStreamWriter_delete(writer);
    XByteArray_delete_base(buf);
    return true;
}

bool XDrawing_loadFromXmlFile(XDrawing* self, const char* filePath) {
    if (!self || !filePath) return false;
    FILE* fp = fopen(filePath, "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize <= 0) { fclose(fp); return false; }
    char* xml = (char*)XMalloc_System((size_t)fsize + 1);
    if (!xml) { fclose(fp); return false; }
    size_t r = fread(xml, 1, (size_t)fsize, fp);
    fclose(fp);
    if (r != (size_t)fsize) { XFree_System(xml); return false; }
    xml[fsize] = '\0';
    XXmlStreamReader* reader = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(reader, xml);
    XFree_System(xml);
    while (!XXmlStreamReader_atEnd(reader)) {
        int tt = XXmlStreamReader_readNext(reader);
        if (tt == XXmlStream_StartElement) {
            const char* name = XXmlStreamReader_name(reader);
            if (!name) continue;
            if (strcmp(name, "xdr:twoCellAnchor") == 0 ||
                strcmp(name, "xdr:oneCellAnchor") == 0 ||
                strcmp(name, "xdr:absoluteAnchor") == 0) {
                XDrawingAnchor* anchor = XDrawingAnchor_create(self, XDAnchor_Unknown);
                if (anchor) {
                    XDrawingAnchor_loadFromXml(anchor, reader);
                    XDrawingAnchor** pp = &anchor;
                    XVector_push_back_1_base(self->m_anchors, pp);
                }
            }
        }
    }
    XXmlStreamReader_delete(reader);
    return true;
}
