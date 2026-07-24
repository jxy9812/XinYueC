#include "XSharedStrings.h"
#include "XXmlStreamReader.h"
#include "XMemory.h"
#include <stdlib.h>

#include "XByteArray.h"
#include "XString.h"
#include <string.h>


/* const char* 比较函数 */
static int32_t str_compare(const void* lhs, const void* rhs)
{
    const char* a = *(const char**)lhs;
    const char* b = *(const char**)rhs;
    if (!a && !b) return XCompare_Equality;
    if (!a) return XCompare_Less;
    if (!b) return XCompare_Greater;
    int ret = strcmp(a, b);
    return (ret < 0) ? XCompare_Less : (ret > 0) ? XCompare_Greater : XCompare_Equality;
}

XSharedStrings* XSharedStrings_create(XAbstractOOXmlFile_CreateFlag flag)
{
    XSharedStrings* self = (XSharedStrings*)XMalloc_System(sizeof(XSharedStrings));
    if (!self) return NULL;
    memset(self, 0, sizeof(XSharedStrings));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_stringTable = XMap_create(sizeof(const char*), sizeof(int), str_compare);
    self->m_stringList = XVector_create(sizeof(XRichString*));
    self->m_stringCount = 0;
    return self;
}

void XSharedStrings_delete(XSharedStrings* self)
{
    if (!self) return;
    if (self->m_stringTable) { XMap_deinit_base(self->m_stringTable); XFree_System(self->m_stringTable); }
    if (self->m_stringList)
    {
        size_t count = XVector_size_base(self->m_stringList);
        for (size_t i = 0; i < count; i++)
        {
            XRichString* rich = *(XRichString**)XVector_at_base(self->m_stringList, i);
            if (rich) XRichString_delete(rich);
        }
        XVector_deinit_base(self->m_stringList);
        XFree_System(self->m_stringList);
    }
    XAbstractOOXmlFile_deinit(&self->m_base);
    XFree_System(self);
}

int XSharedStrings_count(const XSharedStrings* self)
{
    return self ? (int)XVector_size_base(self->m_stringList) : 0;
}

bool XSharedStrings_isEmpty(const XSharedStrings* self)
{
    return self ? XVector_isEmpty_base(self->m_stringList) : true;
}

int XSharedStrings_addSharedString(XSharedStrings* self, const char* string)
{
    if (!self || !string) return -1;
    int idx = XSharedStrings_getSharedStringIndex(self, string);
    if (idx >= 0) { self->m_stringCount++; return idx; }
    XRichString* rich = XRichString_create();
    if (!rich) return -1;
    XRichString_setText(rich, string);
    idx = (int)XVector_size_base(self->m_stringList);
    XRichString* p = rich;
    XVector_push_back_1_base(self->m_stringList, &p);
    self->m_stringCount++;
    return idx;
}

int XSharedStrings_addSharedRichString(XSharedStrings* self, const XRichString* rich)
{
    if (!self || !rich) return -1;
    int idx = (int)XVector_size_base(self->m_stringList);
    XRichString* p = XRichString_create();
    if (p) XRichString_copy(p, rich);
    XVector_push_back_1_base(self->m_stringList, &p);
    self->m_stringCount++;
    return idx;
}

void XSharedStrings_removeSharedString(XSharedStrings* self, const char* string)
{
    if (!self || !string) return;
    int idx = XSharedStrings_getSharedStringIndex(self, string);
    if (idx >= 0) {
        XRichString* rich = *(XRichString**)XVector_at_base(self->m_stringList, idx);
        if (rich) XRichString_delete(rich);
        XVector_removeAt_base(self->m_stringList, (size_t)idx);
    }
}

void XSharedStrings_incRefByStringIndex(XSharedStrings* self, int idx)
{
    if (self && idx >= 0) self->m_stringCount++;
}

int XSharedStrings_getSharedStringIndex(XSharedStrings* self, const char* string)
{
    if (!self || !string || !self->m_stringList) return -1;
    size_t count = XVector_size_base(self->m_stringList);
    for (size_t i = 0; i < count; i++) {
        XRichString* rich = *(XRichString**)XVector_at_base(self->m_stringList, i);
        if (rich) {
            const char* text = XRichString_text(rich);
            if (text && strcmp(text, string) == 0) return (int)i;
        }
    }
    return -1;
}

XRichString* XSharedStrings_getSharedString(XSharedStrings* self, int index)
{
    if (!self || !self->m_stringList || index < 0) return NULL;
    size_t count = XVector_size_base(self->m_stringList);
    if ((size_t)index >= count) return NULL;
    return *(XRichString**)XVector_at_base(self->m_stringList, index);
}

XVector* XSharedStrings_getSharedStrings(XSharedStrings* self)
{
    return self ? self->m_stringList : NULL;
}

/* ========== XML 序列化 ========== */

bool XSharedStrings_saveToXmlData(const XSharedStrings* self, uint8_t** outData, size_t* outLen)
{
    if (!self || !outData || !outLen) return false;
    *outData = NULL;
    *outLen = 0;
    
    XByteArray* buf = XByteArray_create();
    if (!buf) return false;
    
    XByteArray_append_utf8(buf, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    XByteArray_append_utf8(buf, "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"");
    
    int count = XSharedStrings_count(self);
    int uniqueCount = count;
    char countStr[64];
    snprintf(countStr, sizeof(countStr), " count=\"%d\" uniqueCount=\"%d\"", count, uniqueCount);
    XByteArray_append_utf8(buf, countStr);
    XByteArray_append_utf8(buf, ">\n");
    
    for (int i = 0; i < count; i++) {
        XRichString* rich = XSharedStrings_getSharedString(self, i);
        if (!rich) continue;
        
        XByteArray_append_utf8(buf, "  <si>");
        
        if (XRichString_isRichString(rich)) {
            /* 富文本 */
            for (int j = 0; j < XRichString_fragmentCount(rich); j++) {
                const char* text = XRichString_fragmentText(rich, j);
                if (!text) continue;
                
                XByteArray_append_utf8(buf, "<r><t");
                /* 检查是否需要 xml:space="preserve" */
                if (text[0] == ' ' || text[strlen(text)-1] == ' ') {
                    XByteArray_append_utf8(buf, " xml:space=\"preserve\"");
                }
                XByteArray_append_utf8(buf, ">");
                /* 转义 XML 特殊字符 */
                for (const char* p = text; *p; p++) {
                    if (*p == '<') XByteArray_append_utf8(buf, "&lt;");
                    else if (*p == '>') XByteArray_append_utf8(buf, "&gt;");
                    else if (*p == '&') XByteArray_append_utf8(buf, "&amp;");
                    else {
                        char c[2] = {*p, 0};
                        XByteArray_append_utf8(buf, c);
                    }
                }
                XByteArray_append_utf8(buf, "</t></r>");
            }
        } else {
            /* 纯文本 */
            const char* text = XRichString_text(rich);
            if (text) {
                XByteArray_append_utf8(buf, "<t");
                if (text[0] == ' ' || (text[0] && text[strlen(text)-1] == ' ')) {
                    XByteArray_append_utf8(buf, " xml:space=\"preserve\"");
                }
                XByteArray_append_utf8(buf, ">");
                /* 转义 XML 特殊字符 */
                for (const char* p = text; *p; p++) {
                    if (*p == '<') XByteArray_append_utf8(buf, "&lt;");
                    else if (*p == '>') XByteArray_append_utf8(buf, "&gt;");
                    else if (*p == '&') XByteArray_append_utf8(buf, "&amp;");
                    else {
                        char c[2] = {*p, 0};
                        XByteArray_append_utf8(buf, c);
                    }
                }
                XByteArray_append_utf8(buf, "</t>");
            }
        }
        
        XByteArray_append_utf8(buf, "</si>\n");
    }
    
    XByteArray_append_utf8(buf, "</sst>\n");
    
    *outData = (uint8_t*)XMalloc_System(XByteArray_size_base(buf) + 1);
    if (*outData) {
        memcpy(*outData, XByteArray_data(buf), XByteArray_size_base(buf));
        *outLen = XByteArray_size_base(buf);
    }
    XByteArray_delete_base(buf);
    return *outData != NULL;
}

bool XSharedStrings_saveToXmlFile(XSharedStrings* self, const char* filePath)
{
    uint8_t* data = NULL;
    size_t len = 0;
    if (!XSharedStrings_saveToXmlData(self, &data, &len)) return false;
    
    FILE* fp = fopen(filePath, "wb");
    if (!fp) { XFree_System(data); return false; }
    fwrite(data, 1, len, fp);
    fclose(fp);
    XFree_System(data);
    return true;
}

/**
 * @brief     使用 XXmlStreamReader 解析共享字符串 XML
 * @param self   XSharedStrings 指针
 * @param reader XXmlStreamReader 指针（需已设置数据）
 * @return    成功返回true
 */
static bool sharedStrings_loadFromReader(XSharedStrings* self, XXmlStreamReader* reader)
{
    if (!self || !reader) return false;
    bool in_si = false;
    XString* acc = XString_create();
    bool in_t = false;

    while (!XXmlStreamReader_atEnd(reader)) {
        int tt = XXmlStreamReader_readNext(reader);
        if (XXmlStreamReader_hasError(reader)) break;

        if (tt == XXmlStream_StartElement) {
            const char* name = XXmlStreamReader_name(reader);
            if (!name) continue;

            if (strcmp(name, "si") == 0) {
                in_si = true;
                XString_clear_base(acc);
            } else if (in_si && strcmp(name, "t") == 0) {
                /* 纯文本片段：<t>text</t> */
                in_t = true;
            }
            /* 富文本片段：<r><rPr>...</rPr><t>text</t></r> - 在 Characters 中处理 */
            (void)in_t;
        } else if (tt == XXmlStream_Characters ) {
            if (in_si) {
                const char* txt = XXmlStreamReader_text(reader);
                if (txt) XString_append_utf8(acc, txt);
            }
        } else if (tt == XXmlStream_EndElement) {
            const char* name = XXmlStreamReader_name(reader);
            if (!name) continue;

            if (in_si && strcmp(name, "t") == 0) {
                in_t = false;
            } else if (strcmp(name, "si") == 0) {
                in_si = false;
                /* 如果 acc 为空但有文本，也添加 */
                const char* str = XString_toUtf8(acc);
                XSharedStrings_addSharedString(self, str ? str : "");
                XString_clear_base(acc);
            }
        }
    }
    XString_delete_base(acc);
    return !XXmlStreamReader_hasError(reader);
}

bool XSharedStrings_loadFromXmlData(XSharedStrings* self, const uint8_t* data, size_t len) {
    if (!self || !data || len == 0) return false;

    XXmlStreamReader* reader = XXmlStreamReader_create();
    XXmlStreamReader_addData_utf8(reader, (const char*)data);
    bool ok = sharedStrings_loadFromReader(self, reader);
    XXmlStreamReader_delete(reader);
    return ok;
}

bool XSharedStrings_loadFromXmlFile(XSharedStrings* self, const char* filePath) {
    if (!self || !filePath) return false;
    
    FILE* fp = fopen(filePath, "rb");
    if (!fp) return false;
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    uint8_t* data = (uint8_t*)XMalloc_System(size + 1);
    if (!data) { fclose(fp); return false; }
    
    fread(data, 1, size, fp);
    fclose(fp);
    
    bool result = XSharedStrings_loadFromXmlData(self, data, size);
    XFree_System(data);
    return result;
}
