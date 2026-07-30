#include "XDocPropsCore.h"
#include "XMemory.h"
#include "XMap.h"
#include "XFile.h"
#include "XByteArray.h"
#include "XXmlStreamReader.h"
#include "XXmlStreamWriter.h"
#include <stdlib.h>
#include <stdio.h>

#include <string.h>


/* XMap 键是 XString*，直接复用 XString 的字典序比较。 */
static int32_t str_compare(const void* lhs, const void* rhs)
{
    XString* a = *(XString**)lhs;
    XString* b = *(XString**)rhs;
    if (!a && !b) return XCompare_Equality;
    if (!a) return XCompare_Less;
    if (!b) return XCompare_Greater;
    return XString_compare(a, b);
}

static bool is_author_alias(const XString* name)
{
    return name && XString_equals_utf8(name, "author", XChar_CaseSensitive);
}

static bool property_name_equals(const XString* stored, const XString* requested)
{
    if (!stored || !requested) return false;
    if (is_author_alias(requested))
        return XString_equals_utf8(stored, "creator", XChar_CaseSensitive);
    return XString_equals(stored, requested, XChar_CaseSensitive);
}

XDocPropsCore* XDocPropsCore_create(XAbstractOOXmlFile_CreateFlag flag)
{
    XDocPropsCore* self = (XDocPropsCore*)XMalloc_System(sizeof(XDocPropsCore));
    if (!self) return NULL;
    memset(self, 0, sizeof(XDocPropsCore));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_properties = XMap_create(sizeof(XString*), sizeof(XString*), str_compare);
    return self;
}

void XDocPropsCore_delete(XDocPropsCore* self)
{
    if (!self) return;
    if (self->m_properties)
    {
        XMap_iterator it = XMap_begin(self->m_properties);
        XMap_iterator end = XMap_end(self->m_properties);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_properties, &it))
        {
            XPair* pair = XMap_iterator_data(&it);
            if (pair)
            {
                XString* key = *(XString**)XPair_first(pair);
                if (key) XString_delete_base(key);
                XString* val = *(XString**)XPair_second(pair);
                if (val) XString_delete_base(val);
            }
        }
        XMap_delete_base(self->m_properties);
    }
    XAbstractOOXmlFile_deinit(&self->m_base);
    XFree_System(self);
}

bool XDocPropsCore_setProperty(XDocPropsCore* self, const XString* name, const XString* value)
{
    if (!self || !name || !value) return false;
    XMap_iterator it = XMap_begin(self->m_properties);
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_properties, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        XString* key = pair ? *(XString**)XPair_first(pair) : NULL;
        if (property_name_equals(key, name)) {
            XString* current = *(XString**)XPair_second(pair);
            return current && XString_assign(current, value);
        }
    }
    XString* keyStr = is_author_alias(name)
        ? XString_create_utf8("creator") : XString_create_copy(name);
    if (!keyStr) return false;
    XString* valStr = XString_create_copy(value);
    if (!valStr) { XString_delete_base(keyStr); return false; }
    XMap_insert_base(self->m_properties, &keyStr, &valStr);
    return true;
}

static void clear_properties(XDocPropsCore* self)
{
    if (!self || !self->m_properties) return;
    XMap_iterator it = XMap_begin(self->m_properties);
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_properties, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        if (!pair) continue;
        XString* key = *(XString**)XPair_first(pair);
        XString* value = *(XString**)XPair_second(pair);
        if (key) XString_delete_base(key);
        if (value) XString_delete_base(value);
    }
    XMap_clear_base(self->m_properties);
}

const XString* XDocPropsCore_property(const XDocPropsCore* self, const XString* name)
{
    if (!self || !name) return NULL;
    XMap* map = (XMap*)self->m_properties;
    XMap_iterator it = XMap_begin(map);
    XMap_iterator end = XMap_end(map);
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(map, &it))
    {
        XPair* pair = XMap_iterator_data(&it);
        if (pair)
        {
            XString* key = *(XString**)XPair_first(pair);
            if (property_name_equals(key, name))
            {
                XString* val = *(XString**)XPair_second(pair);
                return val;
            }
        }
    }
    return NULL;
}

int XDocPropsCore_propertyNames(const XDocPropsCore* self, XString*** names)
{
    if (!self || !names) return 0;
    XMap* map = (XMap*)self->m_properties;
    if (!map) { *names = NULL; return 0; }
    int count = (int)XMap_size_base(map);
    if (count == 0) { *names = NULL; return 0; }
    *names = (XString**)XMalloc_System(sizeof(XString*) * (size_t)count);
    if (!*names) return 0;
    int idx = 0;
    XMap_iterator it = XMap_begin(map);
    XMap_iterator end = XMap_end(map);
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(map, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        if (pair) {
            XString* key = *(XString**)XPair_first(pair);
            XString* ks = XString_create_copy(key);
            (*names)[idx++] = ks;
        }
    }
    return count;
}

bool XDocPropsCore_saveToXmlFile(XDocPropsCore* self, const XString* filePath)
{
    if (!self || !filePath) return false;
    uint8_t* data = NULL;
    size_t len = 0;
    if (!XDocPropsCore_saveToXmlData(self, &data, &len)) return false;
    XFile* file = XFile_create_2((XString*)filePath);
    bool ok = file && XIODevice_open_base((XIODevice*)file,
        XIODevice_WriteOnly | XIODevice_Truncate) &&
        XIODevice_write_1((XIODevice*)file, (const char*)data, (int64_t)len) == (int64_t)len;
    if (file) {
        if (XIODevice_isOpen((XIODevice*)file)) XIODevice_close_base((XIODevice*)file);
        XClass_delete_base((XClass*)file);
    }
    XFree_System(data);
    return ok;
}

bool XDocPropsCore_loadFromXmlFile(XDocPropsCore* self, const XString* filePath)
{
    if (!self || !filePath) return false;
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        return false;
    }
    XByteArray* data = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    if (!data) return false;
    bool ok = XDocPropsCore_loadFromXmlData(self, XByteArray_data(data),
        XByteArray_size_base((XContainer*)data));
    XByteArray_delete_base(data);
    return ok;
}

static const char* qualified_property_name(const XString* name)
{
    if (!name) return NULL;
    if (XString_equals_utf8(name, "title", XChar_CaseSensitive) ||
        XString_equals_utf8(name, "subject", XChar_CaseSensitive) ||
        XString_equals_utf8(name, "creator", XChar_CaseSensitive) ||
        XString_equals_utf8(name, "description", XChar_CaseSensitive) ||
        XString_equals_utf8(name, "language", XChar_CaseSensitive)) {
        static char qualified[64];
        snprintf(qualified, sizeof(qualified), "dc:%s", XString_toUtf8(name));
        return qualified;
    }
    if (XString_equals_utf8(name, "created", XChar_CaseSensitive) ||
        XString_equals_utf8(name, "modified", XChar_CaseSensitive)) {
        static char qualified[64];
        snprintf(qualified, sizeof(qualified), "dcterms:%s", XString_toUtf8(name));
        return qualified;
    }
    if (XString_equals_utf8(name, "keywords", XChar_CaseSensitive) ||
        XString_equals_utf8(name, "lastModifiedBy", XChar_CaseSensitive) ||
        XString_equals_utf8(name, "revision", XChar_CaseSensitive) ||
        XString_equals_utf8(name, "category", XChar_CaseSensitive) ||
        XString_equals_utf8(name, "contentStatus", XChar_CaseSensitive) ||
        XString_equals_utf8(name, "lastPrinted", XChar_CaseSensitive) ||
        XString_equals_utf8(name, "version", XChar_CaseSensitive)) {
        static char qualified[64];
        snprintf(qualified, sizeof(qualified), "cp:%s", XString_toUtf8(name));
        return qualified;
    }
    return XString_toUtf8(name);
}

bool XDocPropsCore_saveToXmlData(const XDocPropsCore* self, uint8_t** outData, size_t* outLen)
{
    if (!self || !outData || !outLen) return false;
    *outData = NULL;
    *outLen = 0;
    XXmlStreamWriter* writer = XXmlStreamWriter_create();
    if (!writer) return false;
    XXmlStreamWriter_writeStartDocument(writer);
    XXmlStreamWriter_writeStartElement_utf8(writer, "cp:coreProperties");
    XXmlStreamWriter_writeNamespace_utf8(writer,
        "http://schemas.openxmlformats.org/package/2006/metadata/core-properties", "cp");
    XXmlStreamWriter_writeNamespace_utf8(writer, "http://purl.org/dc/elements/1.1/", "dc");
    XXmlStreamWriter_writeNamespace_utf8(writer, "http://purl.org/dc/terms/", "dcterms");
    XXmlStreamWriter_writeNamespace_utf8(writer, "http://www.w3.org/2001/XMLSchema-instance", "xsi");

    XMap_iterator it = XMap_begin(self->m_properties);
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_properties, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        XString* key = pair ? *(XString**)XPair_first(pair) : NULL;
        XString* value = pair ? *(XString**)XPair_second(pair) : NULL;
        if (!key || !value) continue;
        const char* qualified = qualified_property_name(key);
        XXmlStreamWriter_writeStartElement_utf8(writer, qualified);
        if (XString_equals_utf8(key, "created", XChar_CaseSensitive) ||
            XString_equals_utf8(key, "modified", XChar_CaseSensitive))
            XXmlStreamWriter_writeAttribute_utf8(writer, "xsi:type", "dcterms:W3CDTF");
        XXmlStreamWriter_writeCharacters(writer, value);
        XXmlStreamWriter_writeEndElement(writer);
    }
    XXmlStreamWriter_writeEndDocument(writer);
    XByteArray* buffer = XXmlStreamWriter_toByteArray(writer);
    size_t size = buffer ? XByteArray_size_base((XContainer*)buffer) : 0;
    if (!XXmlStreamWriter_hasError(writer) && size > 0) {
        *outData = (uint8_t*)XMalloc_System(size + 1);
        if (*outData) {
            memcpy(*outData, XByteArray_data(buffer), size);
            (*outData)[size] = '\0';
            *outLen = size;
        }
    }
    XXmlStreamWriter_delete_base(writer);
    return *outData != NULL;
}

bool XDocPropsCore_loadFromXmlData(XDocPropsCore* self, const uint8_t* data, size_t len)
{
    if (!self || !data || len == 0) return false;
    XByteArray* bytes = XByteArray_create_with_data((const char*)data, len);
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!bytes || !reader) {
        if (bytes) XByteArray_delete_base(bytes);
        if (reader) XXmlStreamReader_delete_base(reader);
        return false;
    }
    XXmlStreamReader_addData(reader, bytes);
    XByteArray_delete_base(bytes);
    clear_properties(self);
    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        if (token != XXmlStream_StartElement) continue;
        const XString* name = XXmlStreamReader_name(reader);
        if (!name || XString_equals_utf8(name, "coreProperties", XChar_CaseSensitive)) continue;
        XString* key = XString_create_copy(name);
        const XString* value = XXmlStreamReader_readElementText(reader,
            XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
        if (!key || !value || !XDocPropsCore_setProperty(self, key, value)) {
            if (key) XString_delete_base(key);
            XXmlStreamReader_delete_base(reader);
            clear_properties(self);
            return false;
        }
        XString_delete_base(key);
    }
    bool ok = !XXmlStreamReader_hasError(reader);
    XXmlStreamReader_delete_base(reader);
    if (!ok) clear_properties(self);
    return ok;
}
