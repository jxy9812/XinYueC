/******************************************************************************
 * @file       XMediaFile.c
 * @brief      XMediaFile 媒体文件类实现
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XMediaFile.h"
#include "XMemory.h"
#include <stdlib.h>

#include <string.h>


XMediaFile* XMediaFile_create(const XString* fileName)
{
    XMediaFile* self = (XMediaFile*)XMalloc_System(sizeof(XMediaFile));
    if (!self) return NULL;
    memset(self, 0, sizeof(XMediaFile));
    self->m_index = -1;
    if (fileName) XMediaFile_setFileName(self, fileName);
    return self;
}

XMediaFile* XMediaFile_create_data(const uint8_t* bytes, size_t dataSize, const XString* suffix, const XString* mimeType)
{
    XMediaFile* self = XMediaFile_create(NULL);
    if (!self) return NULL;
    XMediaFile_set(self, bytes, dataSize, suffix, mimeType);
    return self;
}

void XMediaFile_delete(XMediaFile* self)
{
    if (!self) return;
    if (self->m_fileName) { XString_deinit_base(self->m_fileName); XFree_System(self->m_fileName); }
    if (self->m_contents) { XByteArray_deinit_base(self->m_contents); XFree_System(self->m_contents); }
    if (self->m_suffix) { XString_deinit_base(self->m_suffix); XFree_System(self->m_suffix); }
    if (self->m_mimeType) { XString_deinit_base(self->m_mimeType); XFree_System(self->m_mimeType); }
    XFree_System(self);
}

void XMediaFile_set(XMediaFile* self, const uint8_t* bytes, size_t dataSize, const XString* suffix, const XString* mimeType)
{
    if (!self) return;
    if (self->m_contents) { XByteArray_deinit_base(self->m_contents); XFree_System(self->m_contents); }
    self->m_contents = XByteArray_create_with_data((const char*)bytes, dataSize);
    if (suffix)
    {
        if (!self->m_suffix) self->m_suffix = XString_create();
        if (self->m_suffix) { XString_clear_base(self->m_suffix); XString_append(self->m_suffix, suffix); }
    }
    if (mimeType)
    {
        if (!self->m_mimeType) self->m_mimeType = XString_create();
        if (self->m_mimeType) { XString_clear_base(self->m_mimeType); XString_append(self->m_mimeType, mimeType); }
    }
}

const XString* XMediaFile_suffix(const XMediaFile* self)
{ return (self && self->m_suffix) ? self->m_suffix : NULL; }

const XString* XMediaFile_mimeType(const XMediaFile* self)
{ return (self && self->m_mimeType) ? self->m_mimeType : NULL; }

const uint8_t* XMediaFile_contents(const XMediaFile* self)
{ return (self && self->m_contents) ? (const uint8_t*)XByteArray_data(self->m_contents) : NULL; }

size_t XMediaFile_contentsSize(const XMediaFile* self)
{ return (self && self->m_contents) ? XByteArray_size_base(self->m_contents) : 0; }

bool XMediaFile_isIndexValid(const XMediaFile* self) { return self && self->m_indexValid; }
int XMediaFile_index(const XMediaFile* self) { return self ? self->m_index : -1; }
void XMediaFile_setIndex(XMediaFile* self, int idx) { if (self) { self->m_index = idx; self->m_indexValid = true; } }
void XMediaFile_setFileName(XMediaFile* self, const XString* name)
{
    if (!self) return;
    if (!self->m_fileName) self->m_fileName = XString_create();
    if (self->m_fileName) { XString_clear_base(self->m_fileName); if (name) XString_append(self->m_fileName, name); }
}
const XString* XMediaFile_fileName(const XMediaFile* self)
{ return (self && self->m_fileName) ? self->m_fileName : NULL; }

void XMediaFile_hashKey(const XMediaFile* self, uint8_t** outKey, size_t* outLen)
{
    if (!self || !outKey || !outLen) { if (outKey) *outKey = NULL; if (outLen) *outLen = 0; return; }
    const uint8_t* contents = XMediaFile_contents(self);
    size_t size = XMediaFile_contentsSize(self);
    if (!contents || size == 0) { *outKey = NULL; *outLen = 0; return; }
    /* 简单哈希：使用内容的直接拷贝作为键（与 QXlsx 的 QCryptographicHash 对齐） */
    uint8_t* key = (uint8_t*)XMalloc_System(size);
    if (!key) { *outKey = NULL; *outLen = 0; return; }
    memcpy(key, contents, size);
    *outKey = key;
    *outLen = size;
}
