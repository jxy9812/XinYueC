/******************************************************************************
 * @file       XMediaFile.c
 * @brief      XMediaFile 媒体文件类实现
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XMediaFile.h"
#include "XCryptographic.h"
#include "XMemory.h"
#include <stdlib.h>

#include <string.h>

static void delete_string(XString* value)
{
    if (value) XString_delete_base(value);
}

static void delete_contents(XByteArray* value)
{
    if (value) XByteArray_delete_base(value);
}

XMediaFile* XMediaFile_create(const XString* fileName)
{
    XMediaFile* self = (XMediaFile*)XMalloc_System(sizeof(XMediaFile));
    if (!self) return NULL;
    memset(self, 0, sizeof(XMediaFile));
    self->m_index = 0;
    if (fileName) XMediaFile_setFileName(self, fileName);
    return self;
}

XMediaFile* XMediaFile_create_data(const uint8_t* bytes, size_t dataSize, const XString* suffix, const XString* mimeType)
{
    if (!bytes && dataSize > 0) return NULL;
    XMediaFile* self = XMediaFile_create(NULL);
    if (!self) return NULL;
    XMediaFile_set(self, bytes, dataSize, suffix, mimeType);
    if (!self->m_contents || (suffix && !self->m_suffix) ||
        (mimeType && !self->m_mimeType)) {
        XMediaFile_delete(self);
        return NULL;
    }
    return self;
}

void XMediaFile_delete(XMediaFile* self)
{
    if (!self) return;
    delete_string(self->m_fileName);
    delete_contents(self->m_contents);
    delete_string(self->m_suffix);
    delete_string(self->m_mimeType);
    XFree_System(self);
}

void XMediaFile_set(XMediaFile* self, const uint8_t* bytes, size_t dataSize, const XString* suffix, const XString* mimeType)
{
    if (!self || (!bytes && dataSize > 0)) return;
    XByteArray* newContents = XByteArray_create_with_data((const char*)bytes, dataSize);
    XString* newSuffix = suffix ? XString_create_copy(suffix) : NULL;
    XString* newMimeType = mimeType ? XString_create_copy(mimeType) : NULL;
    if (!newContents || (suffix && !newSuffix) || (mimeType && !newMimeType)) {
        delete_contents(newContents);
        delete_string(newSuffix);
        delete_string(newMimeType);
        return;
    }
    delete_contents(self->m_contents);
    delete_string(self->m_suffix);
    delete_string(self->m_mimeType);
    self->m_contents = newContents;
    self->m_suffix = newSuffix;
    self->m_mimeType = newMimeType;
    self->m_indexValid = false;
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
    XString* replacement = name ? XString_create_copy(name) : NULL;
    if (name && !replacement) return;
    delete_string(self->m_fileName);
    self->m_fileName = replacement;
}
const XString* XMediaFile_fileName(const XMediaFile* self)
{ return (self && self->m_fileName) ? self->m_fileName : NULL; }

void XMediaFile_hashKey(const XMediaFile* self, uint8_t** outKey, size_t* outLen)
{
    if (!self || !outKey || !outLen) { if (outKey) *outKey = NULL; if (outLen) *outLen = 0; return; }
    if (!self->m_contents) { *outKey = NULL; *outLen = 0; return; }
    const uint8_t* contents = XMediaFile_contents(self);
    size_t size = XMediaFile_contentsSize(self);
    if (!contents && size > 0) { *outKey = NULL; *outLen = 0; return; }
    XByteArray* digest = XCryptographicHash_hash(
        contents ? (const char*)contents : "", size,
        XCryptographicHash_Md5);
    size_t digestSize = digest ? XByteArray_size_base(digest) : 0;
    uint8_t* key = digestSize > 0 ? (uint8_t*)XMalloc_System(digestSize) : NULL;
    if (!key) {
        if (digest) XByteArray_delete_base(digest);
        *outKey = NULL;
        *outLen = 0;
        return;
    }
    memcpy(key, XByteArray_data(digest), digestSize);
    XByteArray_delete_base(digest);
    *outKey = key;
    *outLen = digestSize;
}
