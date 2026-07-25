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
        XVector_deinit_base(self->m_anchors);
        XFree_System(self->m_anchors);
    }
    XAbstractOOXmlFile_deinit(&self->m_base);
    XFree_System(self);
}

bool XDrawing_saveToXmlFile(XDrawing* self, const XString* filePath) {
    if (!self || !filePath) return false;

    /* 使用 XXmlStreamWriter 生成 XML 内容 */
    XXmlStreamWriter* writer = XXmlStreamWriter_create();
    XXmlStreamWriter_setAutoFormatting(writer, true);
    XXmlStreamWriter_setAutoFormattingIndent(writer, 1);
    XXmlStreamWriter_writeStartDocument_ex_utf8(writer, "1.0");
    XXmlStreamWriter_writeStartElement_utf8(writer, "xdr:wsDr");
    XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:xdr", "http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing");
    XXmlStreamWriter_writeAttribute_utf8(writer, "xmlns:a", "http://schemas.openxmlformats.org/drawingml/2006/main");
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

    /* 通过 XFile 写入文件 */
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file) { XXmlStreamWriter_delete_base(writer); return false; }

    if (!XIODevice_open_base((XIODevice*)file, XIODevice_WriteOnly | XIODevice_Truncate)) {
        XFile_deleteLater(file);
        XXmlStreamWriter_delete_base(writer);
        return false;
    }

    if (xml) {
        XIODevice_write_1((XIODevice*)file, xml, (int64_t)strlen(xml));
    }

    XIODevice_close_base((XIODevice*)file);
    XFile_deleteLater(file);
    XXmlStreamWriter_delete_base(writer);
    return true;
}

bool XDrawing_loadFromXmlFile(XDrawing* self, const XString* filePath) {
    if (!self || !filePath) return false;

    XFile* file = XFile_create_2((XString*)filePath);
    if (!file) return false;

    if (!XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        XFile_deleteLater(file);
        return false;
    }

    XByteArray* xmlData = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XFile_deleteLater(file);

    if (!xmlData || XByteArray_size_base(xmlData) == 0) {
        if (xmlData) XByteArray_delete_base(xmlData);
        return false;
    }

    /* 追加 NUL 终止符 */
    XByteArray_append_1(xmlData, 0);
    const char* xml = (const char*)XByteArray_data(xmlData);

    XXmlStreamReader* reader = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(reader, xml);
    XByteArray_delete_base(xmlData);

    while (!XXmlStreamReader_atEnd(reader)) {
        int tt = XXmlStreamReader_readNext(reader);
        if (tt == XXmlStream_StartElement) {
            const XString* name = XXmlStreamReader_name_const(reader);
            if (!name) continue;
            if (XString_equals_utf8(name, "xdr:twoCellAnchor", XChar_CaseSensitive) ||
                XString_equals_utf8(name, "xdr:oneCellAnchor", XChar_CaseSensitive) ||
                XString_equals_utf8(name, "xdr:absoluteAnchor", XChar_CaseSensitive)) {
                XDrawingAnchor* anchor = XDrawingAnchor_create(self, XDAnchor_Unknown);
                if (anchor) {
                    XDrawingAnchor_loadFromXml(anchor, reader);
                    XDrawingAnchor** pp = &anchor;
                    XVector_push_back_1_base(self->m_anchors, pp);
                }
            }
        }
    }
    XXmlStreamReader_delete_base(reader);
    return true;
}
