#include "XZipWriter.h"
#include "XMemory.h"
#include <stdlib.h>#include <string.h>
XZipWriter* XZipWriter_create(const char* fileName) {
    XZipWriter* self = (XZipWriter*)XMalloc_System(sizeof(XZipWriter));
    if (!self) return NULL; memset(self, 0, sizeof(XZipWriter));
    if (fileName) { self->m_fileName = XString_create(); XString_append_utf8(self->m_fileName, fileName); }
    return self;
}
void XZipWriter_delete(XZipWriter* self) {
    if (!self) return;
    if (self->m_fileName) { XString_deinit_base(self->m_fileName); XFree_System(self->m_fileName); }
    XFree_System(self);
}
bool XZipWriter_addFile(XZipWriter* self, const char* path, const uint8_t* data, size_t size) { (void)self; (void)path; (void)data; (void)size; return false; }
bool XZipWriter_addDirectory(XZipWriter* self, const char* path) { (void)self; (void)path; return false; }
bool XZipWriter_close(XZipWriter* self) { (void)self; return false; }
