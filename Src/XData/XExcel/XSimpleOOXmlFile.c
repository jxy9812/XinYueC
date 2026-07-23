#include "XSimpleOOXmlFile.h"
#include "XMemory.h"
#include <stdlib.h>#include <string.h>
XSimpleOOXmlFile* XSimpleOOXmlFile_create(XAbstractOOXmlFile_CreateFlag flag) {
    XSimpleOOXmlFile* self = (XSimpleOOXmlFile*)XMalloc_System(sizeof(XSimpleOOXmlFile));
    if (!self) return NULL; memset(self, 0, sizeof(XSimpleOOXmlFile));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_xmlData = XByteArray_create();
    return self;
}
void XSimpleOOXmlFile_delete(XSimpleOOXmlFile* self) {
    if (!self) return;
    if (self->m_xmlData) { XByteArray_deinit_base(self->m_xmlData); XFree_System(self->m_xmlData); }
    XAbstractOOXmlFile_deinit(&self->m_base); XFree_System(self);
}
void XSimpleOOXmlFile_setXmlData(XSimpleOOXmlFile* self, const char* data, size_t len) {
    if (!self || !self->m_xmlData) return;
    XByteArray_clear_base(self->m_xmlData);
    XByteArray_append_base(self->m_xmlData, (const uint8_t*)data, len);
}
const char* XSimpleOOXmlFile_xmlData(const XSimpleOOXmlFile* self, size_t* len) {
    if (!self || !self->m_xmlData) { if (len) *len = 0; return NULL; }
    if (len) *len = XByteArray_size_base(self->m_xmlData);
    return (const char*)XByteArray_data_const(self->m_xmlData);
}
bool XSimpleOOXmlFile_saveToXmlFile(XSimpleOOXmlFile* self, const char* filePath) { (void)self; (void)filePath; return false; }
bool XSimpleOOXmlFile_loadFromXmlFile(XSimpleOOXmlFile* self, const char* filePath) { (void)self; (void)filePath; return false; }
