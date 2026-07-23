#include "XZipReader.h"
#include "XMemory.h"
#include <stdlib.h>#include <string.h>
XZipReader* XZipReader_create(const char* fileName) {
    XZipReader* self = (XZipReader*)XMalloc_System(sizeof(XZipReader));
    if (!self) return NULL; memset(self, 0, sizeof(XZipReader));
    if (fileName) { self->m_fileName = XString_create(); XString_append_utf8(self->m_fileName, fileName); }
    self->m_filePaths = XStringList_create();
    return self;
}
XZipReader* XZipReader_createFromData(const uint8_t* data, size_t size) {
    (void)data; (void)size;
    XZipReader* self = (XZipReader*)XMalloc_System(sizeof(XZipReader));
    if (!self) return NULL; memset(self, 0, sizeof(XZipReader));
    self->m_filePaths = XStringList_create();
    return self;
}
void XZipReader_delete(XZipReader* self) {
    if (!self) return;
    if (self->m_fileName) { XString_deinit_base(self->m_fileName); XFree_System(self->m_fileName); }
    if (self->m_filePaths) XStringList_delete(self->m_filePaths);
    XFree_System(self);
}
bool XZipReader_exists(const XZipReader* self) { (void)self; return false; }
XStringList* XZipReader_filePaths(const XZipReader* self) { return self ? self->m_filePaths : NULL; }
XByteArray* XZipReader_fileData(const XZipReader* self, const char* fileName) { (void)self; (void)fileName; return NULL; }
