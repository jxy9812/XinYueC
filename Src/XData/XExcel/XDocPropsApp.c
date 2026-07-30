#include "XDocPropsApp.h"
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

XDocPropsApp* XDocPropsApp_create(XAbstractOOXmlFile_CreateFlag flag)
{
    XDocPropsApp* self = (XDocPropsApp*)XMalloc_System(sizeof(XDocPropsApp));
    if (!self) return NULL;
    memset(self, 0, sizeof(XDocPropsApp));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_titlesOfPartsList = XStringList_create();
    self->m_headingPairsList = XVector_create(sizeof(XDocPropsAppHeadingPair));
    self->m_properties = XMap_create(sizeof(XString*), sizeof(XString*), str_compare);
    return self;
}

void XDocPropsApp_delete(XDocPropsApp* self)
{
    if (!self) return;
    XDocPropsApp_clearParts(self);
    if (self->m_titlesOfPartsList) XStringList_delete_base(self->m_titlesOfPartsList);
    if (self->m_headingPairsList) XVector_delete_base(self->m_headingPairsList);
    if (self->m_properties)
    {
        /* 释放所有 XString* 键和值 */
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

void XDocPropsApp_addPartTitle(XDocPropsApp* self, const XString* title)
{
    if (self && self->m_titlesOfPartsList && title)
        XStringList_push_back_base(self->m_titlesOfPartsList, (void*)title);
}

void XDocPropsApp_addHeadingPair(XDocPropsApp* self, const XString* name, int value)
{
    if (!self || !self->m_headingPairsList || !name || value < 0) return;
    XDocPropsAppHeadingPair pair;
    pair.m_name = XString_create_copy(name);
    pair.m_value = value;
    if (!pair.m_name || !XVector_push_back_2(self->m_headingPairsList, &pair, 1)) {
        if (pair.m_name) XString_delete_base(pair.m_name);
    }
}

bool XDocPropsApp_setProperty(XDocPropsApp* self, const XString* name, const XString* value)
{
    if (!self || !name || !value) return false;
    XMap_iterator it = XMap_begin(self->m_properties);
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_properties, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        XString* key = pair ? *(XString**)XPair_first(pair) : NULL;
        if (key && XString_equals(key, name, XChar_CaseSensitive)) {
            XString* current = *(XString**)XPair_second(pair);
            return current && XString_assign(current, value);
        }
    }
    XString* keyStr = XString_create_copy(name);
    if (!keyStr) return false;
    XString* valStr = XString_create_copy(value);
    if (!valStr) { XString_delete_base(keyStr); return false; }
    XMap_insert_base(self->m_properties, &keyStr, &valStr);
    return true;
}

const XString* XDocPropsApp_property(const XDocPropsApp* self, const XString* name)
{
    if (!self || !name) return NULL;
    XMap_iterator it = XMap_begin(self->m_properties);
    XMap_iterator end = XMap_end(self->m_properties);
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_properties, &it))
    {
        XPair* pair = XMap_iterator_data(&it);
        if (pair)
        {
            XString* key = *(XString**)XPair_first(pair);
            if (key && XString_equals(key, name, XChar_CaseSensitive))
            {
                XString* val = *(XString**)XPair_second(pair);
                return val;
            }
        }
    }
    return NULL;
}

int XDocPropsApp_propertyNames(const XDocPropsApp* self, XString*** names)
{
    if (!self || !names || !self->m_properties) return 0;
    *names = NULL;
    int count = (int)XMap_size_base((XContainer*)self->m_properties);
    if (count <= 0) return 0;
    XString** result = (XString**)XMalloc_System((size_t)count * sizeof(XString*));
    if (!result) return 0;
    int index = 0;
    XMap_iterator it = XMap_begin(self->m_properties);
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_properties, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        XString* key = pair ? *(XString**)XPair_first(pair) : NULL;
        result[index] = key ? XString_create_copy(key) : NULL;
        if (!result[index]) {
            for (int i = 0; i < index; ++i) XString_delete_base(result[i]);
            XFree_System(result);
            return 0;
        }
        index++;
    }
    *names = result;
    return index;
}

static void clear_properties(XDocPropsApp* self)
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

void XDocPropsApp_clearParts(XDocPropsApp* self)
{
    if (!self) return;
    if (self->m_titlesOfPartsList) XStringList_clear_base(self->m_titlesOfPartsList);
    if (self->m_headingPairsList) {
        size_t count = XVector_size_base((XContainer*)self->m_headingPairsList);
        for (size_t i = 0; i < count; ++i) {
            XDocPropsAppHeadingPair* pair =
                (XDocPropsAppHeadingPair*)XVector_at_base(self->m_headingPairsList, i);
            if (pair && pair->m_name) XString_delete_base(pair->m_name);
        }
        XVector_clear_base(self->m_headingPairsList);
    }
}

bool XDocPropsApp_saveToXmlFile(XDocPropsApp* self, const XString* filePath)
{
    if (!self || !filePath) return false;
    uint8_t* data = NULL;
    size_t len = 0;
    if (!XDocPropsApp_saveToXmlData(self, &data, &len)) return false;
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

bool XDocPropsApp_loadFromXmlFile(XDocPropsApp* self, const XString* filePath)
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
    bool ok = XDocPropsApp_loadFromXmlData(self, XByteArray_data(data),
        XByteArray_size_base((XContainer*)data));
    XByteArray_delete_base(data);
    return ok;
}

bool XDocPropsApp_saveToXmlData(const XDocPropsApp* self, uint8_t** outData, size_t* outLen)
{
    if (!self || !outData || !outLen) return false;
    *outData = NULL;
    *outLen = 0;
    XXmlStreamWriter* writer = XXmlStreamWriter_create();
    if (!writer) return false;
    XXmlStreamWriter_writeStartDocument(writer);
    XXmlStreamWriter_writeStartElement_utf8(writer, "Properties");
    XXmlStreamWriter_writeDefaultNamespace_utf8(writer,
        "http://schemas.openxmlformats.org/officeDocument/2006/extended-properties");
    XXmlStreamWriter_writeNamespace_utf8(writer,
        "http://schemas.openxmlformats.org/officeDocument/2006/docPropsVTypes", "vt");

    XMap_iterator it = XMap_begin(self->m_properties);
    for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_properties, &it)) {
        XPair* pair = XMap_iterator_data(&it);
        XString* key = pair ? *(XString**)XPair_first(pair) : NULL;
        XString* value = pair ? *(XString**)XPair_second(pair) : NULL;
        if (key && value) XXmlStreamWriter_writeTextElement(writer, key, value);
    }

    int headingCount = self->m_headingPairsList
        ? (int)XVector_size_base((XContainer*)self->m_headingPairsList) : 0;
    if (headingCount > 0) {
        char count[32];
        snprintf(count, sizeof(count), "%d", headingCount * 2);
        XXmlStreamWriter_writeStartElement_utf8(writer, "HeadingPairs");
        XXmlStreamWriter_writeStartElement_utf8(writer, "vt:vector");
        XXmlStreamWriter_writeAttribute_utf8(writer, "size", count);
        XXmlStreamWriter_writeAttribute_utf8(writer, "baseType", "variant");
        for (int i = 0; i < headingCount; ++i) {
            XDocPropsAppHeadingPair* pair = (XDocPropsAppHeadingPair*)
                XVector_at_base(self->m_headingPairsList, (size_t)i);
            if (!pair || !pair->m_name) continue;
            XXmlStreamWriter_writeStartElement_utf8(writer, "vt:variant");
            XXmlStreamWriter_writeTextElement_utf8(writer, "vt:lpstr", XString_toUtf8(pair->m_name));
            XXmlStreamWriter_writeEndElement(writer);
            char value[32];
            snprintf(value, sizeof(value), "%d", pair->m_value);
            XXmlStreamWriter_writeStartElement_utf8(writer, "vt:variant");
            XXmlStreamWriter_writeTextElement_utf8(writer, "vt:i4", value);
            XXmlStreamWriter_writeEndElement(writer);
        }
        XXmlStreamWriter_writeEndElement(writer);
        XXmlStreamWriter_writeEndElement(writer);
    }

    int titleCount = self->m_titlesOfPartsList
        ? (int)XStringList_size_base((XContainer*)self->m_titlesOfPartsList) : 0;
    if (titleCount > 0) {
        char count[32];
        snprintf(count, sizeof(count), "%d", titleCount);
        XXmlStreamWriter_writeStartElement_utf8(writer, "TitlesOfParts");
        XXmlStreamWriter_writeStartElement_utf8(writer, "vt:vector");
        XXmlStreamWriter_writeAttribute_utf8(writer, "size", count);
        XXmlStreamWriter_writeAttribute_utf8(writer, "baseType", "lpstr");
        for (int i = 0; i < titleCount; ++i) {
            XString* title = (XString*)XStringList_at_base(
                (XVector*)self->m_titlesOfPartsList, (size_t)i);
            if (title) XXmlStreamWriter_writeTextElement_utf8(writer, "vt:lpstr", XString_toUtf8(title));
        }
        XXmlStreamWriter_writeEndElement(writer);
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

bool XDocPropsApp_loadFromXmlData(XDocPropsApp* self, const uint8_t* data, size_t len)
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
    XDocPropsApp_clearParts(self);
    clear_properties(self);
    bool inHeadingPairs = false;
    bool inTitles = false;
    XString* pendingHeading = NULL;
    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        const XString* name = XXmlStreamReader_name(reader);
        if (token == XXmlStream_StartElement && name) {
            if (XString_equals_utf8(name, "HeadingPairs", XChar_CaseSensitive)) {
                inHeadingPairs = true;
            } else if (XString_equals_utf8(name, "TitlesOfParts", XChar_CaseSensitive)) {
                inTitles = true;
            } else if (XString_equals_utf8(name, "lpstr", XChar_CaseSensitive)) {
                const XString* value = XXmlStreamReader_readElementText(reader,
                    XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (inTitles) XDocPropsApp_addPartTitle(self, value);
                else if (inHeadingPairs) {
                    if (pendingHeading) XString_delete_base(pendingHeading);
                    pendingHeading = value ? XString_create_copy(value) : NULL;
                }
            } else if (inHeadingPairs && XString_equals_utf8(name, "i4", XChar_CaseSensitive)) {
                const XString* value = XXmlStreamReader_readElementText(reader,
                    XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (pendingHeading && value) {
                    XDocPropsApp_addHeadingPair(self, pendingHeading, atoi(XString_toUtf8(value)));
                    XString_delete_base(pendingHeading);
                    pendingHeading = NULL;
                }
            } else if (!inHeadingPairs && !inTitles &&
                       !XString_equals_utf8(name, "Properties", XChar_CaseSensitive) &&
                       !XString_equals_utf8(name, "vector", XChar_CaseSensitive) &&
                       !XString_equals_utf8(name, "variant", XChar_CaseSensitive)) {
                XString* key = XString_create_copy(name);
                const XString* value = XXmlStreamReader_readElementText(reader,
                    XXmlStream_ReadElementTextBehaviour_IncludeChildElements);
                if (!key || !value || !XDocPropsApp_setProperty(self, key, value)) {
                    if (key) XString_delete_base(key);
                    if (pendingHeading) XString_delete_base(pendingHeading);
                    XXmlStreamReader_delete_base(reader);
                    XDocPropsApp_clearParts(self);
                    clear_properties(self);
                    return false;
                }
                XString_delete_base(key);
            }
        } else if (token == XXmlStream_EndElement && name) {
            if (XString_equals_utf8(name, "HeadingPairs", XChar_CaseSensitive)) inHeadingPairs = false;
            else if (XString_equals_utf8(name, "TitlesOfParts", XChar_CaseSensitive)) inTitles = false;
        }
    }
    if (pendingHeading) XString_delete_base(pendingHeading);
    bool ok = !XXmlStreamReader_hasError(reader);
    XXmlStreamReader_delete_base(reader);
    if (!ok) {
        XDocPropsApp_clearParts(self);
        clear_properties(self);
    }
    return ok;
}
