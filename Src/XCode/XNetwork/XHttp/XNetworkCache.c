/**
 * @file       XNetworkCache.c
 * @brief      网络缓存元数据和缓存容器实现。
 */

#include "XNetworkCache.h"

#include "XDir.h"
#include "XFile.h"
#include "XHashFunc.h"
#include "XMemory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct XNetworkCacheEntry {
    XNetworkCacheMetaData* m_metadata;
    XByteArray* m_data;
} XNetworkCacheEntry;

static bool xcache_append_u32(XByteArray* data, uint32_t value)
{
    uint8_t bytes[4];
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
    return data && XByteArray_push_back_2(data, bytes, sizeof(bytes));
}

static bool xcache_append_u64(XByteArray* data, uint64_t value)
{
    uint8_t bytes[8];
    size_t i;
    for (i = 0; i < sizeof(bytes); ++i)
        bytes[i] = (uint8_t)(value >> (56 - i * 8));
    return data && XByteArray_push_back_2(data, bytes, sizeof(bytes));
}

static bool xcache_read_bytes(const XByteArray* data, size_t* offset,
                              void* output, size_t size)
{
    if (!data || !offset || !output || *offset > XByteArray_size_base(data) ||
        size > XByteArray_size_base(data) - *offset)
        return false;
    memcpy(output, XByteArray_constData((XByteArray*)data) + *offset, size);
    *offset += size;
    return true;
}

static bool xcache_read_u32(const XByteArray* data, size_t* offset, uint32_t* value)
{
    uint8_t bytes[4];
    if (!value || !xcache_read_bytes(data, offset, bytes, sizeof(bytes)))
        return false;
    *value = ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
             ((uint32_t)bytes[2] << 8) | bytes[3];
    return true;
}

static bool xcache_read_u64(const XByteArray* data, size_t* offset, uint64_t* value)
{
    uint8_t bytes[8];
    size_t i;
    if (!value || !xcache_read_bytes(data, offset, bytes, sizeof(bytes)))
        return false;
    *value = 0;
    for (i = 0; i < sizeof(bytes); ++i)
        *value = (*value << 8) | bytes[i];
    return true;
}

static XString* xcache_file_path(const XNetworkDiskCache* self, const XUrl* url)
{
    XString* urlText;
    XString* fileName;
    XString* result;
    XDir* directory;
    uint64_t hash;
    if (!self || !self->m_cacheDirectory || !url ||
        XString_isEmpty_base(self->m_cacheDirectory))
        return NULL;
    urlText = XUrl_toString(url);
    if (!urlText)
        return NULL;
    hash = XHash_fnv1a_64(XString_toUtf8(urlText), XString_toUtf8_length(urlText));
    fileName = XString_create_fmt_utf8("%016llX.xnc", (unsigned long long)hash);
    directory = XDir_create_2(self->m_cacheDirectory);
    result = directory && fileName ? XDir_filePath(directory, fileName) : NULL;
    if (directory) XClass_delete_base((XClass*)directory);
    if (fileName) XClass_delete_base((XClass*)fileName);
    XClass_delete_base((XClass*)urlText);
    return result;
}

static bool xcache_ensure_directory(const XNetworkDiskCache* self)
{
    XDir* directory;
    XString* current;
    bool result;
    if (!self || !self->m_cacheDirectory ||
        XString_isEmpty_base(self->m_cacheDirectory))
        return false;
    directory = XDir_create_2(self->m_cacheDirectory);
    if (!directory)
        return false;
    if (XDir_exists_1(directory)) {
        XClass_delete_base((XClass*)directory);
        return true;
    }
    current = XString_create_utf8(".");
    result = current && XDir_mkpath(directory, current);
    if (current) XClass_delete_base((XClass*)current);
    XClass_delete_base((XClass*)directory);
    return result;
}

static XByteArray* xcache_serialize(const XNetworkCacheMetaData* metadata,
                                    const XByteArray* data)
{
    XByteArray* result;
    XString* urlText;
    size_t i;
    if (!metadata || !data || !XNetworkCacheMetaData_isValid(metadata) ||
        !metadata->m_url || !metadata->m_headers)
        return NULL;
    urlText = XUrl_toString(metadata->m_url);
    result = XByteArray_create();
    if (!urlText || !result)
        goto failed;
    if (!XByteArray_push_back_2(result, "XNC1", 4) ||
        !xcache_append_u32(result, (uint32_t)XString_toUtf8_length(urlText)) ||
        !XByteArray_push_back_2(result, XString_toUtf8(urlText), XString_toUtf8_length(urlText)) ||
        !xcache_append_u64(result, (uint64_t)metadata->m_lastModifiedMSecs) ||
        !xcache_append_u64(result, (uint64_t)metadata->m_expirationMSecs) ||
        !XByteArray_push_back_1(result, metadata->m_saveToDisk ? 1 : 0) ||
        !XByteArray_push_back_1(result, metadata->m_valid ? 1 : 0) ||
        !xcache_append_u32(result, (uint32_t)XHttpHeaders_size(metadata->m_headers)))
        goto failed;
    for (i = 0; i < XHttpHeaders_size(metadata->m_headers); ++i) {
        const XByteArray* name = XHttpHeaders_nameAt_const(metadata->m_headers, i);
        const XByteArray* value = XHttpHeaders_valueAt_const(metadata->m_headers, i);
        if (!name || !value || XByteArray_size_base(name) > UINT32_MAX ||
            XByteArray_size_base(value) > UINT32_MAX ||
            !xcache_append_u32(result, (uint32_t)XByteArray_size_base(name)) ||
            !XByteArray_push_back_2(result, XByteArray_constData((XByteArray*)name), XByteArray_size_base(name)) ||
            !xcache_append_u32(result, (uint32_t)XByteArray_size_base(value)) ||
            !XByteArray_push_back_2(result, XByteArray_constData((XByteArray*)value), XByteArray_size_base(value)))
            goto failed;
    }
    if (XByteArray_size_base(data) > UINT64_MAX ||
        !xcache_append_u64(result, (uint64_t)XByteArray_size_base(data)) ||
        !XByteArray_push_back_2(result, XByteArray_constData((XByteArray*)data), XByteArray_size_base(data)))
        goto failed;
    XClass_delete_base((XClass*)urlText);
    return result;
failed:
    if (urlText) XClass_delete_base((XClass*)urlText);
    if (result) XClass_delete_base((XClass*)result);
    return NULL;
}

static bool xcache_write_file(const XNetworkDiskCache* self, const XUrl* url,
                              const XNetworkCacheMetaData* metadata,
                              const XByteArray* data)
{
    XString* path;
    XByteArray* serialized;
    XFile* file;
    bool result = false;
    if (!xcache_ensure_directory(self))
        return false;
    path = xcache_file_path(self, url);
    serialized = xcache_serialize(metadata, data);
    file = path ? XFile_create_2(path) : NULL;
    if (file && serialized && XFile_open_2(file, XIODevice_WriteOnly | XIODevice_Truncate | XIODevice_Create, 0)) {
        result = XIODevice_write_2((XIODevice*)file, serialized) == (int64_t)XByteArray_size_base(serialized);
        XFile_close_base(file);
    }
    if (file) XClass_delete_base((XClass*)file);
    if (serialized) XClass_delete_base((XClass*)serialized);
    if (path) XClass_delete_base((XClass*)path);
    return result;
}

static bool xcache_read_file(const XNetworkDiskCache* self, const XUrl* url,
                             XNetworkCacheMetaData** metadata, XByteArray** data)
{
    XString* path;
    XFile* file;
    XByteArray* serialized;
    size_t offset = 0;
    uint32_t length;
    uint32_t headerCount;
    uint64_t value64;
    XString* urlText = NULL;
    XUrl* parsedUrl = NULL;
    XNetworkCacheMetaData* metadataValue = NULL;
    XByteArray* dataValue = NULL;
    size_t i;
    bool ok = false;
    if (metadata) *metadata = NULL;
    if (data) *data = NULL;
    if (!metadata || !data)
        return false;
    path = xcache_file_path(self, url);
    file = path ? XFile_create_2(path) : NULL;
    if (!file || !XFile_open_2(file, XIODevice_ReadOnly, 0))
        goto done;
    serialized = XIODevice_readAll_3((XIODevice*)file);
    XFile_close_base(file);
    if (!serialized || XByteArray_size_base(serialized) < 4 ||
        memcmp(XByteArray_constData(serialized), "XNC1", 4) != 0)
        goto done_serialized;
    offset = 4;
    if (!xcache_read_u32(serialized, &offset, &length) ||
        length > XByteArray_size_base(serialized) - offset)
        goto done_serialized;
    urlText = XString_create_with_length_utf8((const char*)XByteArray_constData(serialized) + offset, length);
    offset += length;
    if (!urlText || !xcache_read_u64(serialized, &offset, &value64)) goto done_serialized;
    metadataValue = XNetworkCacheMetaData_create();
    if (!metadataValue) goto done_serialized;
    parsedUrl = XUrl_create_ex(urlText, XUrl_TolerantMode);
    if (!parsedUrl || !XNetworkCacheMetaData_setUrl(metadataValue, parsedUrl))
        goto done_serialized;
    XClass_delete_base((XClass*)parsedUrl);
    parsedUrl = NULL;
    metadataValue->m_lastModifiedMSecs = (int64_t)value64;
    if (!xcache_read_u64(serialized, &offset, &value64)) goto done_serialized;
    metadataValue->m_expirationMSecs = (int64_t)value64;
    {
        uint8_t flag;
        if (!xcache_read_bytes(serialized, &offset, &flag, 1)) goto done_serialized;
        metadataValue->m_saveToDisk = flag != 0;
        if (!xcache_read_bytes(serialized, &offset, &flag, 1)) goto done_serialized;
        metadataValue->m_valid = flag != 0;
    }
    if (!xcache_read_u32(serialized, &offset, &headerCount)) goto done_serialized;
    for (i = 0; i < headerCount; ++i) {
        XByteArray* name;
        XByteArray* value;
        if (!xcache_read_u32(serialized, &offset, &length) || length > XByteArray_size_base(serialized) - offset) goto done_serialized;
        name = XByteArray_create_with_data((const char*)XByteArray_constData(serialized) + offset, length);
        offset += length;
        if (!xcache_read_u32(serialized, &offset, &length) || length > XByteArray_size_base(serialized) - offset) {
            if (name) XClass_delete_base((XClass*)name);
            goto done_serialized;
        }
        value = XByteArray_create_with_data((const char*)XByteArray_constData(serialized) + offset, length);
        offset += length;
        if (!name || !value || !XHttpHeaders_append(metadataValue->m_headers, name, value)) {
            if (name) XClass_delete_base((XClass*)name);
            if (value) XClass_delete_base((XClass*)value);
            goto done_serialized;
        }
        XClass_delete_base((XClass*)name);
        XClass_delete_base((XClass*)value);
    }
    if (!xcache_read_u64(serialized, &offset, &value64) || value64 > SIZE_MAX ||
        value64 > XByteArray_size_base(serialized) - offset)
        goto done_serialized;
    dataValue = XByteArray_create_with_data((const char*)XByteArray_constData(serialized) + offset, (size_t)value64);
    if (!dataValue) goto done_serialized;
    if (url && !XUrl_equals(metadataValue->m_url, url)) goto done_serialized;
    *metadata = metadataValue;
    *data = dataValue;
    metadataValue = NULL;
    dataValue = NULL;
    ok = true;
done_serialized:
    if (serialized) XClass_delete_base((XClass*)serialized);
done:
    if (file) XClass_delete_base((XClass*)file);
    if (path) XClass_delete_base((XClass*)path);
    if (urlText) XClass_delete_base((XClass*)urlText);
    if (parsedUrl) XClass_delete_base((XClass*)parsedUrl);
    if (metadataValue) XClass_delete_base((XClass*)metadataValue);
    if (dataValue) XClass_delete_base((XClass*)dataValue);
    return ok;
}

static void xcache_metadata_release(XNetworkCacheMetaData* self)
{
    if (!self)
        return;
    if (self->m_url) XClass_delete_base((XClass*)self->m_url);
    if (self->m_headers) XClass_delete_base((XClass*)self->m_headers);
    self->m_url = NULL;
    self->m_headers = NULL;
}

static void VXNetworkCacheMetaData_deinit(XNetworkCacheMetaData* self)
{
    if (!self)
        return;
    xcache_metadata_release(self);
    XClass_Deinit_Parent(XClass, (XClass*)self);
}

static void VXNetworkCacheMetaData_copy(XNetworkCacheMetaData* dest,
                                        const XNetworkCacheMetaData* src)
{
    XUrl* url;
    XHttpHeaders* headers;
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XNetworkCacheMetaData_init(dest);
    url = src->m_url ? XUrl_create_copy(src->m_url) : XUrl_create();
    headers = src->m_headers ? XHttpHeaders_create_copy(src->m_headers) : XHttpHeaders_create();
    if (!url || !headers) {
        if (url) XClass_delete_base((XClass*)url);
        if (headers) XClass_delete_base((XClass*)headers);
        return;
    }
    xcache_metadata_release(dest);
    dest->m_url = url;
    dest->m_headers = headers;
    dest->m_lastModifiedMSecs = src->m_lastModifiedMSecs;
    dest->m_expirationMSecs = src->m_expirationMSecs;
    dest->m_saveToDisk = src->m_saveToDisk;
    dest->m_valid = src->m_valid;
}

static void VXNetworkCacheMetaData_move(XNetworkCacheMetaData* dest,
                                        XNetworkCacheMetaData* src)
{
    if (!dest || !src || dest == src)
        return;
    if (XClassIsVtableNull(dest))
        XNetworkCacheMetaData_init(dest);
    xcache_metadata_release(dest);
    dest->m_url = src->m_url;
    dest->m_headers = src->m_headers;
    dest->m_lastModifiedMSecs = src->m_lastModifiedMSecs;
    dest->m_expirationMSecs = src->m_expirationMSecs;
    dest->m_saveToDisk = src->m_saveToDisk;
    dest->m_valid = src->m_valid;
    src->m_url = NULL;
    src->m_headers = NULL;
    src->m_lastModifiedMSecs = -1;
    src->m_expirationMSecs = -1;
    src->m_saveToDisk = false;
    src->m_valid = false;
}

XVtable* XNetworkCacheMetaData_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
    //虚函数表初始化
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XNetworkCacheMetaData)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    //继承类
    XVTABLE_INHERIT_XCLASS(XClass);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkCacheMetaData_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXNetworkCacheMetaData_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXNetworkCacheMetaData_move);
#if SHOWCONTAINERSIZE
    printf("XNetworkCacheMetaData size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

void XNetworkCacheMetaData_init(XNetworkCacheMetaData* self)
{
    if (!self)
        return;
    memset(self, 0, sizeof(*self));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XNetworkCacheMetaData);
    self->m_url = XUrl_create();
    self->m_headers = XHttpHeaders_create();
    self->m_lastModifiedMSecs = -1;
    self->m_expirationMSecs = -1;
}

XNetworkCacheMetaData* XNetworkCacheMetaData_create(void)
{
    XNetworkCacheMetaData* self =
        (XNetworkCacheMetaData*)XMalloc_System(sizeof(XNetworkCacheMetaData));
    if (!self)
        return NULL;
    XNetworkCacheMetaData_init(self);
    if (!self->m_url || !self->m_headers) {
        XNetworkCacheMetaData_deinit_base((XClass*)self);
        XFree_System(self);
        return NULL;
    }
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XNetworkCacheMetaData* XNetworkCacheMetaData_create_copy(const XNetworkCacheMetaData* other)
{
    XNetworkCacheMetaData* self;
    if (!other)
        return NULL;
    self = XNetworkCacheMetaData_create();
    if (self) XClass_copy_base((XClass*)self, (const XClass*)other);
    return self;
}

XNetworkCacheMetaData* XNetworkCacheMetaData_create_move(XNetworkCacheMetaData* other)
{
    XNetworkCacheMetaData* self;
    if (!other)
        return NULL;
    self = XNetworkCacheMetaData_create();
    if (self) XClass_move_base((XClass*)self, (XClass*)other);
    return self;
}

bool XNetworkCacheMetaData_setUrl(XNetworkCacheMetaData* self, const XUrl* url)
{
    XUrl* replacement = url ? XUrl_create_copy(url) : XUrl_create();
    if (!self || !replacement)
        return false;
    if (self->m_url) XClass_delete_base((XClass*)self->m_url);
    self->m_url = replacement;
    self->m_valid = url && XUrl_isValid(url);
    return true;
}

const XUrl* XNetworkCacheMetaData_url_const(const XNetworkCacheMetaData* self)
{
    return self ? self->m_url : NULL;
}

bool XNetworkCacheMetaData_setHeaders(XNetworkCacheMetaData* self,
                                      const XHttpHeaders* headers)
{
    XHttpHeaders* replacement = headers ? XHttpHeaders_create_copy(headers) : XHttpHeaders_create();
    if (!self || !replacement)
        return false;
    if (self->m_headers) XClass_delete_base((XClass*)self->m_headers);
    self->m_headers = replacement;
    return true;
}

const XHttpHeaders* XNetworkCacheMetaData_headers_const(const XNetworkCacheMetaData* self)
{
    return self ? self->m_headers : NULL;
}

void XNetworkCacheMetaData_setLastModifiedMSecs(XNetworkCacheMetaData* self, int64_t msecs)
{
    if (self) self->m_lastModifiedMSecs = msecs;
}

int64_t XNetworkCacheMetaData_lastModifiedMSecs(const XNetworkCacheMetaData* self)
{
    return self ? self->m_lastModifiedMSecs : -1;
}

void XNetworkCacheMetaData_setExpirationMSecs(XNetworkCacheMetaData* self, int64_t msecs)
{
    if (self) self->m_expirationMSecs = msecs;
}

int64_t XNetworkCacheMetaData_expirationMSecs(const XNetworkCacheMetaData* self)
{
    return self ? self->m_expirationMSecs : -1;
}

void XNetworkCacheMetaData_setSaveToDisk(XNetworkCacheMetaData* self, bool enabled)
{
    if (self) self->m_saveToDisk = enabled;
}

bool XNetworkCacheMetaData_saveToDisk(const XNetworkCacheMetaData* self)
{
    return self ? self->m_saveToDisk : false;
}

bool XNetworkCacheMetaData_isValid(const XNetworkCacheMetaData* self)
{
    return self && self->m_valid && self->m_url && XUrl_isValid(self->m_url);
}

bool XNetworkCacheMetaData_equals(const XNetworkCacheMetaData* lhs,
                                  const XNetworkCacheMetaData* rhs)
{
    XString* leftUrl;
    XString* rightUrl;
    bool result;
    if (!lhs || !rhs)
        return false;
    if (lhs == rhs)
        return true;
    leftUrl = lhs->m_url ? XUrl_toString(lhs->m_url) : XString_create();
    rightUrl = rhs->m_url ? XUrl_toString(rhs->m_url) : XString_create();
    result = leftUrl && rightUrl && XString_equals(leftUrl, rightUrl, XChar_CaseSensitive) &&
             lhs->m_lastModifiedMSecs == rhs->m_lastModifiedMSecs &&
             lhs->m_expirationMSecs == rhs->m_expirationMSecs &&
             lhs->m_saveToDisk == rhs->m_saveToDisk &&
             lhs->m_valid == rhs->m_valid &&
             XHttpHeaders_size(lhs->m_headers) == XHttpHeaders_size(rhs->m_headers);
    if (result) {
        for (size_t i = 0; i < XHttpHeaders_size(lhs->m_headers); ++i) {
            const XByteArray* leftName = XHttpHeaders_nameAt_const(lhs->m_headers, i);
            const XByteArray* rightName = XHttpHeaders_nameAt_const(rhs->m_headers, i);
            const XByteArray* leftValue = XHttpHeaders_valueAt_const(lhs->m_headers, i);
            const XByteArray* rightValue = XHttpHeaders_valueAt_const(rhs->m_headers, i);
            if (!leftName || !rightName || !leftValue || !rightValue ||
                XByteArray_compare(leftName, rightName) != 0 ||
                XByteArray_compare(leftValue, rightValue) != 0) {
                result = false;
                break;
            }
        }
    }
    if (leftUrl) XClass_delete_base((XClass*)leftUrl);
    if (rightUrl) XClass_delete_base((XClass*)rightUrl);
    return result;
}

void XNetworkCacheMetaData_swap(XNetworkCacheMetaData* lhs, XNetworkCacheMetaData* rhs)
{
    XUrl* url;
    XHttpHeaders* headers;
    int64_t value;
    bool flag;
    if (!lhs || !rhs || lhs == rhs)
        return;
    url = lhs->m_url; lhs->m_url = rhs->m_url; rhs->m_url = url;
    headers = lhs->m_headers; lhs->m_headers = rhs->m_headers; rhs->m_headers = headers;
    value = lhs->m_lastModifiedMSecs; lhs->m_lastModifiedMSecs = rhs->m_lastModifiedMSecs; rhs->m_lastModifiedMSecs = value;
    value = lhs->m_expirationMSecs; lhs->m_expirationMSecs = rhs->m_expirationMSecs; rhs->m_expirationMSecs = value;
    flag = lhs->m_saveToDisk; lhs->m_saveToDisk = rhs->m_saveToDisk; rhs->m_saveToDisk = flag;
    flag = lhs->m_valid; lhs->m_valid = rhs->m_valid; rhs->m_valid = flag;
}

static void xcache_entry_delete(XNetworkCacheEntry* entry)
{
    if (!entry)
        return;
    if (entry->m_metadata) XClass_delete_base((XClass*)entry->m_metadata);
    if (entry->m_data) XClass_delete_base((XClass*)entry->m_data);
    XFree_System(entry);
}

static void VXNetworkDiskCache_deinit(XNetworkDiskCache* self)
{
    if (!self)
        return;
    if (self->m_entries) {
        for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_entries); ++i) {
            XNetworkCacheEntry** entry = (XNetworkCacheEntry**)XVector_at_base(self->m_entries, (int64_t)i);
            if (entry && *entry) xcache_entry_delete(*entry);
        }
        XClass_delete_base((XClass*)self->m_entries);
    }
    if (self->m_cacheDirectory) XClass_delete_base((XClass*)self->m_cacheDirectory);
    self->m_entries = NULL;
    self->m_cacheDirectory = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XNetworkDiskCache_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
    //虚函数表初始化
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XNetworkDiskCache)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    //继承类
    XVTABLE_INHERIT_XCLASS(XObject);
    //重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXNetworkDiskCache_deinit);
#if SHOWCONTAINERSIZE
    printf("XNetworkDiskCache size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

void XNetworkDiskCache_init(XNetworkDiskCache* self)
{
    if (!self)
        return;
    XObject_init((XObject*)self);
    XClassSetVtable(self, XNetworkDiskCache);
    self->m_cacheDirectory = XString_create();
    self->m_entries = XVector_create(sizeof(XNetworkCacheEntry*));
    self->m_maximumCacheSize = -1;
    self->m_cacheSize = 0;
}

XNetworkDiskCache* XNetworkDiskCache_create(void)
{
    XNetworkDiskCache* self = (XNetworkDiskCache*)XMalloc_System(sizeof(XNetworkDiskCache));
    if (!self)
        return NULL;
    XNetworkDiskCache_init(self);
    if (!self->m_cacheDirectory || !self->m_entries) {
        XNetworkDiskCache_deinit_base((XClass*)self);
        XFree_System(self);
        return NULL;
    }
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

static int64_t xcache_url_index(const XNetworkDiskCache* self, const XUrl* url)
{
    XString* wanted;
    int64_t result = -1;
    if (!self || !self->m_entries || !url)
        return -1;
    wanted = XUrl_toString(url);
    if (!wanted)
        return -1;
    for (size_t i = 0; i < XContainer_size_base((const XContainer*)self->m_entries); ++i) {
        XNetworkCacheEntry** entry = (XNetworkCacheEntry**)XVector_at_base(self->m_entries, (int64_t)i);
        XString* current;
        if (!entry || !*entry || !(*entry)->m_metadata || !(*entry)->m_metadata->m_url)
            continue;
        current = XUrl_toString((*entry)->m_metadata->m_url);
        if (current && XString_equals(current, wanted, XChar_CaseSensitive))
            result = (int64_t)i;
        if (current) XClass_delete_base((XClass*)current);
        if (result >= 0) break;
    }
    XClass_delete_base((XClass*)wanted);
    return result;
}

bool XNetworkDiskCache_setCacheDirectory(XNetworkDiskCache* self, const XString* directory)
{
    XString* replacement;
    if (!self)
        return false;
    replacement = directory ? XString_create_copy(directory) : XString_create();
    if (!replacement)
        return false;
    if (self->m_cacheDirectory) XClass_delete_base((XClass*)self->m_cacheDirectory);
    self->m_cacheDirectory = replacement;
    if (self->m_cacheDirectory && !XString_isEmpty_base(self->m_cacheDirectory))
        xcache_ensure_directory(self);
    return true;
}

const XString* XNetworkDiskCache_cacheDirectory_const(const XNetworkDiskCache* self)
{
    return self ? self->m_cacheDirectory : NULL;
}

void XNetworkDiskCache_setMaximumCacheSize(XNetworkDiskCache* self, int64_t size)
{
    if (!self)
        return;
    self->m_maximumCacheSize = size < 0 ? -1 : size;
    while (self->m_maximumCacheSize >= 0 && self->m_cacheSize > self->m_maximumCacheSize &&
           self->m_entries && XContainer_size_base((const XContainer*)self->m_entries) != 0) {
        XNetworkCacheEntry** entry = (XNetworkCacheEntry**)XVector_at_base(self->m_entries, 0);
        if (entry && *entry) {
            self->m_cacheSize -= (int64_t)XContainer_size_base((const XContainer*)(*entry)->m_data);
            xcache_entry_delete(*entry);
        }
        XVector_remove_base(self->m_entries, 0, 1);
    }
}

int64_t XNetworkDiskCache_maximumCacheSize(const XNetworkDiskCache* self)
{
    return self ? self->m_maximumCacheSize : -1;
}

int64_t XNetworkDiskCache_cacheSize(const XNetworkDiskCache* self)
{
    return self ? self->m_cacheSize : 0;
}

XNetworkCacheMetaData* XNetworkDiskCache_metaData(const XNetworkDiskCache* self,
                                                  const XUrl* url)
{
    int64_t index = xcache_url_index(self, url);
    XNetworkCacheMetaData* diskMetadata = NULL;
    XByteArray* diskData = NULL;
    if (index >= 0) {
        XNetworkCacheEntry** entry = (XNetworkCacheEntry**)XVector_at_base(self->m_entries, index);
        return entry && *entry ? XNetworkCacheMetaData_create_copy((*entry)->m_metadata) : NULL;
    }
    if (xcache_read_file(self, url, &diskMetadata, &diskData)) {
        if (diskData) XClass_delete_base((XClass*)diskData);
        return diskMetadata;
    }
    return XNetworkCacheMetaData_create();
}

XByteArray* XNetworkDiskCache_data(const XNetworkDiskCache* self, const XUrl* url)
{
    int64_t index = xcache_url_index(self, url);
    XNetworkCacheEntry** entry;
    if (index < 0) {
        XNetworkCacheMetaData* diskMetadata = NULL;
        XByteArray* diskData = NULL;
        if (xcache_read_file(self, url, &diskMetadata, &diskData)) {
            if (diskMetadata) XClass_delete_base((XClass*)diskMetadata);
            return diskData;
        }
        return NULL;
    }
    entry = (XNetworkCacheEntry**)XVector_at_base(self->m_entries, index);
    return entry && *entry ? XByteArray_create_copy((*entry)->m_data) : NULL;
}

bool XNetworkDiskCache_insert(XNetworkDiskCache* self,
                              const XNetworkCacheMetaData* metadata,
                              const XByteArray* data)
{
    XNetworkCacheEntry* entry;
    XNetworkCacheMetaData* metadataCopy;
    XByteArray* dataCopy;
    int64_t index;
    int64_t dataSize;
    if (!self || !self->m_entries || !metadata || !data || !XNetworkCacheMetaData_isValid(metadata))
        return false;
    dataSize = (int64_t)XContainer_size_base((const XContainer*)data);
    if (self->m_maximumCacheSize >= 0 && dataSize > self->m_maximumCacheSize)
        return false;
    metadataCopy = XNetworkCacheMetaData_create_copy(metadata);
    dataCopy = XByteArray_create_copy(data);
    entry = (XNetworkCacheEntry*)XMalloc_System(sizeof(XNetworkCacheEntry));
    if (!metadataCopy || !dataCopy || !entry) {
        if (metadataCopy) XClass_delete_base((XClass*)metadataCopy);
        if (dataCopy) XClass_delete_base((XClass*)dataCopy);
        if (entry) XFree_System(entry);
        return false;
    }
    entry->m_metadata = metadataCopy;
    entry->m_data = dataCopy;
    index = xcache_url_index(self, XNetworkCacheMetaData_url_const(metadata));
    if (index >= 0) {
        XNetworkCacheEntry** old = (XNetworkCacheEntry**)XVector_at_base(self->m_entries, index);
        if (old && *old) {
            self->m_cacheSize -= (int64_t)XContainer_size_base((const XContainer*)(*old)->m_data);
            xcache_entry_delete(*old);
            *old = entry;
        }
    } else if (!XVector_push_back_1_base(self->m_entries, &entry)) {
        xcache_entry_delete(entry);
        return false;
    }
    self->m_cacheSize += dataSize;
    XNetworkDiskCache_setMaximumCacheSize(self, self->m_maximumCacheSize);
    if (self->m_cacheDirectory && !XString_isEmpty_base(self->m_cacheDirectory)) {
        if (metadata->m_saveToDisk)
            xcache_write_file(self, XNetworkCacheMetaData_url_const(metadata), metadata, data);
        else {
            XString* path = xcache_file_path(self, XNetworkCacheMetaData_url_const(metadata));
            if (path) {
                XFile_remove_static(path);
                XClass_delete_base((XClass*)path);
            }
        }
    }
    return true;
}

bool XNetworkDiskCache_remove(XNetworkDiskCache* self, const XUrl* url)
{
    int64_t index = xcache_url_index(self, url);
    XNetworkCacheEntry** entry;
    if (index < 0)
        return false;
    entry = (XNetworkCacheEntry**)XVector_at_base(self->m_entries, index);
    if (entry && *entry) {
        self->m_cacheSize -= (int64_t)XContainer_size_base((const XContainer*)(*entry)->m_data);
        xcache_entry_delete(*entry);
    }
    XVector_remove_base(self->m_entries, index, 1);
    if (self->m_cacheDirectory && !XString_isEmpty_base(self->m_cacheDirectory)) {
        XString* path = xcache_file_path(self, url);
        if (path) {
            XFile_remove_static(path);
            XClass_delete_base((XClass*)path);
        }
    }
    return true;
}

void XNetworkDiskCache_clear(XNetworkDiskCache* self)
{
    if (!self || !self->m_entries)
        return;
    while (XContainer_size_base((const XContainer*)self->m_entries) != 0) {
        XNetworkCacheEntry** entry = (XNetworkCacheEntry**)XVector_at_base(self->m_entries, 0);
        if (entry && *entry) xcache_entry_delete(*entry);
        XVector_remove_base(self->m_entries, 0, 1);
    }
    self->m_cacheSize = 0;
    if (self->m_cacheDirectory && !XString_isEmpty_base(self->m_cacheDirectory)) {
        XDir* directory = XDir_create_2(self->m_cacheDirectory);
        XStringList* names;
        if (directory) {
            names = XDir_entryList_1(directory, XDir_Files, XDir_NoSort);
            if (names) {
                for (size_t i = 0; i < XContainer_size_base((const XContainer*)names); ++i) {
                    XString* name = (XString*)XStringList_at_base(names, i);
                    if (name && XString_endsWith_utf8(name, ".xnc", XChar_CaseSensitive))
                        XDir_remove(directory, name);
                }
                XClass_delete_base((XClass*)names);
            }
            XClass_delete_base((XClass*)directory);
        }
    }
}
