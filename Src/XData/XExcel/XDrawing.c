#include "XDrawing.h"
#include "XMemory.h"
#include <stdlib.h>#include <string.h>
XDrawing* XDrawing_create(XAbstractSheet* sheet, XAbstractOOXmlFile_CreateFlag flag) {
    XDrawing* self = (XDrawing*)XMalloc_System(sizeof(XDrawing));
    if (!self) return NULL; memset(self, 0, sizeof(XDrawing));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_sheet = sheet;
    self->m_anchors = XVector_Create(XDrawingAnchor*);
    return self;
}
void XDrawing_delete(XDrawing* self) { if (!self) return; if (self->m_anchors) XFree_System(self->m_anchors); XAbstractOOXmlFile_deinit(&self->m_base); XFree_System(self); }
bool XDrawing_saveToXmlFile(XDrawing* self, const char* filePath) { (void)self; (void)filePath; return false; }
bool XDrawing_loadFromXmlFile(XDrawing* self, const char* filePath) { (void)self; (void)filePath; return false; }
