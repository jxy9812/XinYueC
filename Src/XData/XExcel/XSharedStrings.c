#include "XSharedStrings.h"
#include "XXmlStreamReader.h"
#include "XMemory.h"
#include "XFile.h"
#include "XByteArray.h"
#include "XString.h"
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

static void shared_strings_clear(XSharedStrings* self)
{
    if (!self) return;
    if (self->m_stringTable) XMap_clear_base(self->m_stringTable);
    if (self->m_stringList) {
        for (size_t i = 0; i < XVector_size_base(self->m_stringList); ++i) {
            XRichString* rich = *(XRichString**)XVector_at_base(self->m_stringList, i);
            if (rich) XRichString_delete(rich);
        }
        XVector_clear_base(self->m_stringList);
    }
    self->m_stringCount = 0;
}

static XlsxSharedStringInfo* shared_string_info(XSharedStrings* self,
                                                 const XString* string)
{
    if (!self || !string || !self->m_stringTable) return NULL;
    XString* key = (XString*)string;
    XMap_iterator iterator;
    if (!XMap_find_base((XMapBase*)self->m_stringTable, &key, &iterator)) return NULL;
    XPair* pair = XMap_iterator_data(&iterator);
    return pair ? (XlsxSharedStringInfo*)XPair_second(pair) : NULL;
}

static bool rich_font_formats_equal(const XFormat* left, const XFormat* right)
{
    bool leftEmpty = !left || !XFormat_hasFontData(left);
    bool rightEmpty = !right || !XFormat_hasFontData(right);
    if (leftEmpty || rightEmpty) return leftEmpty && rightEmpty;
    uint8_t* leftKey = NULL;
    uint8_t* rightKey = NULL;
    size_t leftLength = 0;
    size_t rightLength = 0;
    XFormat_fontKey(left, &leftKey, &leftLength);
    XFormat_fontKey(right, &rightKey, &rightLength);
    bool equal = leftKey && rightKey && leftLength == rightLength &&
        memcmp(leftKey, rightKey, leftLength) == 0;
    if (leftKey) XFree_System(leftKey);
    if (rightKey) XFree_System(rightKey);
    return equal;
}

static bool append_loaded_string(XSharedStrings* self, const XRichString* source)
{
    if (!self || !source || !self->m_stringList) return false;
    XRichString* rich = XRichString_create();
    if (!rich) return false;
    XRichString_copy(rich, source);
    int index = (int)XVector_size_base(self->m_stringList);
    if (!XVector_push_back_2(self->m_stringList, &rich, 1)) {
        XRichString_delete(rich);
        return false;
    }
    if (!XRichString_isRichString(rich)) {
        XString* key = (XString*)XRichString_text(rich);
        XlsxSharedStringInfo* existing = shared_string_info(self, key);
        if (existing) {
            existing->m_index = index;
            existing->m_count = 0;
        } else {
            XlsxSharedStringInfo info = {index, 0};
            if (!key || !XMap_insert_base(self->m_stringTable, &key, &info)) {
                XRichString_delete(rich);
                XVector_removeAt_base(self->m_stringList, (size_t)index);
                return false;
            }
        }
    }
    return true;
}

XSharedStrings* XSharedStrings_create(XAbstractOOXmlFile_CreateFlag flag)
{
    XSharedStrings* self = (XSharedStrings*)XMalloc_System(sizeof(XSharedStrings));
    if (!self) return NULL;
    memset(self, 0, sizeof(XSharedStrings));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_stringTable = XMap_create(sizeof(XString*), sizeof(XlsxSharedStringInfo), str_compare);
    self->m_stringList = XVector_create(sizeof(XRichString*));
    if (!self->m_stringTable || !self->m_stringList) {
        XSharedStrings_delete(self);
        return NULL;
    }
    self->m_stringCount = 0;
    return self;
}

void XSharedStrings_delete(XSharedStrings* self)
{
    if (!self) return;
    shared_strings_clear(self);
    if (self->m_stringTable) XMap_delete_base(self->m_stringTable);
    if (self->m_stringList)
    {
        size_t count = XVector_size_base(self->m_stringList);
        for (size_t i = 0; i < count; i++)
        {
            XRichString* rich = *(XRichString**)XVector_at_base(self->m_stringList, i);
            if (rich) XRichString_delete(rich);
        }
        XVector_delete_base(self->m_stringList);
    }
    XAbstractOOXmlFile_deinit(&self->m_base);
    XFree_System(self);
}

int XSharedStrings_count(const XSharedStrings* self)
{
    return self ? self->m_stringCount : 0;
}

bool XSharedStrings_isEmpty(const XSharedStrings* self)
{
    return self ? XVector_isEmpty_base(self->m_stringList) : true;
}

int XSharedStrings_addSharedString(XSharedStrings* self, const XString* string)
{
    if (!self || !string) return -1;
    XlsxSharedStringInfo* existing = shared_string_info(self, string);
    if (existing) {
        existing->m_count++;
        self->m_stringCount++;
        return existing->m_index;
    }
    XRichString* rich = XRichString_create();
    if (!rich) return -1;
    XRichString_setText(rich, string);
    int idx = (int)XVector_size_base(self->m_stringList);
    XRichString* p = rich;
    if (!XVector_push_back_2(self->m_stringList, &p, 1)) {
        XRichString_delete(rich);
        return -1;
    }
    XString* key = (XString*)XRichString_text(rich);
    XlsxSharedStringInfo info = {idx, 1};
    if (!key || !XMap_insert_base(self->m_stringTable, &key, &info)) {
        XRichString_delete(rich);
        XVector_removeAt_base(self->m_stringList, (size_t)idx);
        return -1;
    }
    self->m_stringCount++;
    return idx;
}

int XSharedStrings_addSharedRichString(XSharedStrings* self, const XRichString* rich)
{
    if (!self || !rich) return -1;
    size_t count = XVector_size_base(self->m_stringList);
    for (size_t i = 0; i < count; ++i) {
        XRichString* existing = *(XRichString**)XVector_at_base(self->m_stringList, i);
        if (!existing || XRichString_fragmentCount(existing) !=
                         XRichString_fragmentCount(rich)) continue;
        bool equal = true;
        for (int fragment = 0; fragment < XRichString_fragmentCount(rich); ++fragment) {
            const XString* leftText = XRichString_fragmentText(existing, fragment);
            const XString* rightText = XRichString_fragmentText(rich, fragment);
            const XFormat* leftFormat = XRichString_fragmentFormat(existing, fragment);
            const XFormat* rightFormat = XRichString_fragmentFormat(rich, fragment);
            bool textEqual = (!leftText && !rightText) ||
                (leftText && rightText &&
                 XString_compare(leftText, rightText) == XCompare_Equality);
            bool formatEqual = rich_font_formats_equal(leftFormat, rightFormat);
            if (!textEqual || !formatEqual) { equal = false; break; }
        }
        if (equal) {
            self->m_stringCount++;
            return (int)i;
        }
    }
    int idx = (int)XVector_size_base(self->m_stringList);
    XRichString* p = XRichString_create();
    if (p) XRichString_copy(p, rich);
    if (!p || !XVector_push_back_2(self->m_stringList, &p, 1)) {
        if (p) XRichString_delete(p);
        return -1;
    }
    self->m_stringCount++;
    return idx;
}

void XSharedStrings_removeSharedString(XSharedStrings* self, const XString* string)
{
    if (!self || !string) return;
    XlsxSharedStringInfo* info = shared_string_info(self, string);
    if (!info) return;
    if (self->m_stringCount > 0) self->m_stringCount--;
    if (--info->m_count <= 0) {
        int idx = info->m_index;
        XString* key = (XString*)string;
        XMap_remove_base((XMapBase*)self->m_stringTable, &key);
        XRichString* rich = *(XRichString**)XVector_at_base(self->m_stringList, idx);
        if (rich) XRichString_delete(rich);
        XVector_removeAt_base(self->m_stringList, (size_t)idx);
        XMap_iterator it = XMap_begin(self->m_stringTable);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_stringTable, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            XlsxSharedStringInfo* current =
                pair ? (XlsxSharedStringInfo*)XPair_second(pair) : NULL;
            if (current && current->m_index > idx) current->m_index--;
        }
        for (size_t i = XVector_size_base(self->m_stringList); i > 0; --i) {
            XRichString* candidate = *(XRichString**)XVector_at_base(
                self->m_stringList, i - 1);
            const XString* candidateText = candidate && !XRichString_isRichString(candidate)
                ? XRichString_text(candidate) : NULL;
            if (candidateText &&
                XString_compare(candidateText, string) == XCompare_Equality) {
                XString* candidateKey = (XString*)candidateText;
                XlsxSharedStringInfo replacement = {(int)(i - 1), 0};
                XMap_insert_base(self->m_stringTable, &candidateKey, &replacement);
                break;
            }
        }
    }
}

void XSharedStrings_incRefByStringIndex(XSharedStrings* self, int idx)
{
    if (!self || idx < 0 || (size_t)idx >= XVector_size_base(self->m_stringList)) return;
    XRichString* rich = *(XRichString**)XVector_at_base(self->m_stringList, idx);
    XlsxSharedStringInfo* info = rich && !XRichString_isRichString(rich)
        ? shared_string_info(self, XRichString_text(rich)) : NULL;
    if (info) info->m_count++;
    self->m_stringCount++;
}

int XSharedStrings_getSharedStringIndex(XSharedStrings* self, const XString* string)
{
    XlsxSharedStringInfo* info = shared_string_info(self, string);
    return info ? info->m_index : -1;
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

static void append_xml_escaped(XByteArray* output, const XString* value)
{
    const char* text = value ? XString_toUtf8(value) : NULL;
    if (!output || !text) return;
    for (const char* p = text; *p; ++p) {
        if (*p == '<') XByteArray_append_utf8(output, "&lt;");
        else if (*p == '>') XByteArray_append_utf8(output, "&gt;");
        else if (*p == '&') XByteArray_append_utf8(output, "&amp;");
        else if (*p == '\"') XByteArray_append_utf8(output, "&quot;");
        else if (*p == '\'') XByteArray_append_utf8(output, "&apos;");
        else {
            char character[2] = {*p, '\0'};
            XByteArray_append_utf8(output, character);
        }
    }
}

static void append_rich_properties(XByteArray* output, const XFormat* format)
{
    if (!output || !format || XFormat_isEmpty(format)) return;
    XByteArray_append_utf8(output, "<rPr>");
    if (XFormat_fontBold(format)) XByteArray_append_utf8(output, "<b/>");
    if (XFormat_fontItalic(format)) XByteArray_append_utf8(output, "<i/>");
    if (XFormat_fontStrikeOut(format)) XByteArray_append_utf8(output, "<strike/>");
    if (XFormat_fontOutline(format)) XByteArray_append_utf8(output, "<outline/>");
    if (XFormat_boolProperty(format, XFormat_P_Font_Shadow, false))
        XByteArray_append_utf8(output, "<shadow/>");
    XFormat_FontUnderline underline = XFormat_fontUnderline(format);
    if (underline != XFormat_FontUnderlineNone) {
        XByteArray_append_utf8(output, "<u");
        if (underline == XFormat_FontUnderlineDouble)
            XByteArray_append_utf8(output, " val=\"double\"");
        else if (underline == XFormat_FontUnderlineSingleAccounting)
            XByteArray_append_utf8(output, " val=\"singleAccounting\"");
        else if (underline == XFormat_FontUnderlineDoubleAccounting)
            XByteArray_append_utf8(output, " val=\"doubleAccounting\"");
        XByteArray_append_utf8(output, "/>");
    }
    XFormat_FontScript script = XFormat_fontScript(format);
    if (script == XFormat_FontScriptSuper)
        XByteArray_append_utf8(output, "<vertAlign val=\"superscript\"/>");
    else if (script == XFormat_FontScriptSub)
        XByteArray_append_utf8(output, "<vertAlign val=\"subscript\"/>");
    int size = XFormat_fontSize(format);
    if (size > 0) {
        char value[32];
        snprintf(value, sizeof(value), "<sz val=\"%d\"/>", size);
        XByteArray_append_utf8(output, value);
    }
    if (XFormat_hasProperty(format, XFormat_P_Font_Color)) {
        XColor color = XFormat_fontColor(format);
        char value[48];
        snprintf(value, sizeof(value), "<color rgb=\"%02X%02X%02X%02X\"/>",
            XColor_alpha(&color), XColor_red(&color),
            XColor_green(&color), XColor_blue(&color));
        XByteArray_append_utf8(output, value);
    }
    if (XFormat_hasProperty(format, XFormat_P_Font_Charset)) {
        char value[32];
        snprintf(value, sizeof(value), "<charset val=\"%d\"/>",
            XFormat_intProperty(format, XFormat_P_Font_Charset, 0));
        XByteArray_append_utf8(output, value);
    }
    const XString* name = XFormat_fontName(format);
    if (name && !XString_isEmpty_base(name)) {
        XByteArray_append_utf8(output, "<rFont val=\"");
        append_xml_escaped(output, name);
        XByteArray_append_utf8(output, "\"/>");
    }
    if (XFormat_hasProperty(format, XFormat_P_Font_Family)) {
        char value[32];
        snprintf(value, sizeof(value), "<family val=\"%d\"/>",
            XFormat_intProperty(format, XFormat_P_Font_Family, 0));
        XByteArray_append_utf8(output, value);
    }
    if (XFormat_boolProperty(format, XFormat_P_Font_Condense, false))
        XByteArray_append_utf8(output, "<condense val=\"1\"/>");
    if (XFormat_boolProperty(format, XFormat_P_Font_Extend, false))
        XByteArray_append_utf8(output, "<extend val=\"1\"/>");
    XByteArray_append_utf8(output, "</rPr>");
}

bool XSharedStrings_saveToXmlData(const XSharedStrings* self, uint8_t** outData, size_t* outLen)
{
    if (!self || !outData || !outLen) return false;
    *outData = NULL;
    *outLen = 0;
    
    XByteArray* buf = XByteArray_create();
    if (!buf) return false;
    
    XByteArray_append_utf8(buf, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    XByteArray_append_utf8(buf, "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"");
    
    int count = self->m_stringCount;
    int uniqueCount = (int)XVector_size_base(self->m_stringList);
    char countStr[64];
    snprintf(countStr, sizeof(countStr), " count=\"%d\" uniqueCount=\"%d\"", count, uniqueCount);
    XByteArray_append_utf8(buf, countStr);
    XByteArray_append_utf8(buf, ">\n");
    
    for (int i = 0; i < uniqueCount; i++) {
        XRichString* rich = XSharedStrings_getSharedString(self, i);
        if (!rich) continue;
        
        XByteArray_append_utf8(buf, "  <si>");
        
        if (XRichString_isRichString(rich)) {
            /* 富文本 */
            for (int j = 0; j < XRichString_fragmentCount(rich); j++) {
                const XString* textX = XRichString_fragmentText(rich, j);
                const char* text = textX ? XString_toUtf8(textX) : NULL;
                if (!text) continue;
                
                XByteArray_append_utf8(buf, "<r>");
                append_rich_properties(buf, XRichString_fragmentFormat(rich, j));
                XByteArray_append_utf8(buf, "<t");
                /* 检查是否需要 xml:space="preserve" */
                size_t textLength = strlen(text);
                if (textLength > 0 && (text[0] == ' ' || text[textLength - 1] == ' ')) {
                    XByteArray_append_utf8(buf, " xml:space=\"preserve\"");
                }
                XByteArray_append_utf8(buf, ">");
                append_xml_escaped(buf, textX);
                XByteArray_append_utf8(buf, "</t></r>");
            }
        } else {
            /* 纯文本 */
            const XString* textX = XRichString_text(rich);
            const char* text = textX ? XString_toUtf8(textX) : NULL;
            if (text) {
                XByteArray_append_utf8(buf, "<t");
                if (text[0] == ' ' || (text[0] && text[strlen(text)-1] == ' ')) {
                    XByteArray_append_utf8(buf, " xml:space=\"preserve\"");
                }
                XByteArray_append_utf8(buf, ">");
                append_xml_escaped(buf, textX);
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
        (*outData)[*outLen] = '\0';
    }
    XByteArray_delete_base(buf);
    return *outData != NULL;
}

bool XSharedStrings_saveToXmlFile(XSharedStrings* self, const XString* filePath)
{
    uint8_t* data = NULL;
    size_t len = 0;
    if (!XSharedStrings_saveToXmlData(self, &data, &len)) return false;
    
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_WriteOnly | XIODevice_Truncate)) {
        if (file) XClass_delete_base((XClass*)file);
        XFree_System(data);
        return false;
    }
    bool result = XIODevice_write_1((XIODevice*)file, (const char*)data,
        (int64_t)len) == (int64_t)len;
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    XFree_System(data);
    return result;
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
    bool in_t = false;
    bool in_run = false;
    bool in_run_properties = false;
    int declaredCount = -1;
    int declaredUniqueCount = -1;
    bool ok = true;
    XString* acc = XString_create();
    XRichString* rich = NULL;
    XFormat* format = NULL;
    if (!acc) return false;

#define READER_ATTRIBUTE(attributeName) \
    XString_Init_Utf8(attributeName##Key, #attributeName); \
    const XString* attributeName##Value = XXmlStreamAttributes_value( \
        XXmlStreamReader_attributes(reader), attributeName##Key); \
    XString_deinit_base(attributeName##Key)

    while (!XXmlStreamReader_atEnd(reader)) {
        int tt = XXmlStreamReader_readNext(reader);
        if (XXmlStreamReader_hasError(reader)) break;

        if (tt == XXmlStream_StartElement) {
            const XString* name = XXmlStreamReader_name(reader);
            if (!name) continue;

            if (XString_equals_utf8(name, "sst", XChar_CaseSensitive)) {
                READER_ATTRIBUTE(count);
                const char* value = countValue ? XString_toUtf8(countValue) : NULL;
                if (value) {
                    char* end = NULL;
                    long parsed = strtol(value, &end, 10);
                    if (end && *end == '\0' && parsed >= 0 && parsed <= INT32_MAX)
                        declaredCount = (int)parsed;
                }
                READER_ATTRIBUTE(uniqueCount);
                value = uniqueCountValue ? XString_toUtf8(uniqueCountValue) : NULL;
                if (value) {
                    char* end = NULL;
                    long parsed = strtol(value, &end, 10);
                    if (end && *end == '\0' && parsed >= 0 && parsed <= INT32_MAX)
                        declaredUniqueCount = (int)parsed;
                }
            } else if (XString_equals_utf8(name, "si", XChar_CaseSensitive)) {
                in_si = true;
                XString_clear_base(acc);
                rich = XRichString_create();
                if (!rich) break;
            } else if (in_si && XString_equals_utf8(name, "r", XChar_CaseSensitive)) {
                in_run = true;
                if (format) XFormat_delete(format);
                format = XFormat_create();
                if (!format) break;
            } else if (in_run && XString_equals_utf8(name, "rPr", XChar_CaseSensitive)) {
                in_run_properties = true;
            } else if (in_run_properties) {
                if (XString_equals_utf8(name, "b", XChar_CaseSensitive))
                    XFormat_setFontBold(format, true);
                else if (XString_equals_utf8(name, "i", XChar_CaseSensitive))
                    XFormat_setFontItalic(format, true);
                else if (XString_equals_utf8(name, "strike", XChar_CaseSensitive))
                    XFormat_setFontStrikeOut(format, true);
                else if (XString_equals_utf8(name, "outline", XChar_CaseSensitive))
                    XFormat_setFontOutline(format, true);
                else if (XString_equals_utf8(name, "shadow", XChar_CaseSensitive)) {
                    int enabled = 1;
                    XFormat_setProperty(format, XFormat_P_Font_Shadow, &enabled);
                }
                else if (XString_equals_utf8(name, "u", XChar_CaseSensitive)) {
                    READER_ATTRIBUTE(val);
                    XFormat_FontUnderline underline = XFormat_FontUnderlineSingle;
                    if (XString_equals_utf8(valValue, "double", XChar_CaseSensitive))
                        underline = XFormat_FontUnderlineDouble;
                    else if (XString_equals_utf8(valValue, "singleAccounting", XChar_CaseSensitive))
                        underline = XFormat_FontUnderlineSingleAccounting;
                    else if (XString_equals_utf8(valValue, "doubleAccounting", XChar_CaseSensitive))
                        underline = XFormat_FontUnderlineDoubleAccounting;
                    XFormat_setFontUnderline(format, underline);
                } else if (XString_equals_utf8(name, "sz", XChar_CaseSensitive)) {
                    READER_ATTRIBUTE(val);
                    if (valValue) XFormat_setFontSize(format, atoi(XString_toUtf8(valValue)));
                } else if (XString_equals_utf8(name, "rFont", XChar_CaseSensitive)) {
                    READER_ATTRIBUTE(val);
                    if (valValue) XFormat_setFontName(format, valValue);
                } else if (XString_equals_utf8(name, "charset", XChar_CaseSensitive) ||
                           XString_equals_utf8(name, "family", XChar_CaseSensitive) ||
                           XString_equals_utf8(name, "condense", XChar_CaseSensitive) ||
                           XString_equals_utf8(name, "extend", XChar_CaseSensitive)) {
                    READER_ATTRIBUTE(val);
                    int value = valValue ? atoi(XString_toUtf8(valValue)) : 1;
                    int property = XString_equals_utf8(name, "charset", XChar_CaseSensitive)
                        ? XFormat_P_Font_Charset
                        : XString_equals_utf8(name, "family", XChar_CaseSensitive)
                            ? XFormat_P_Font_Family
                            : XString_equals_utf8(name, "condense", XChar_CaseSensitive)
                                ? XFormat_P_Font_Condense : XFormat_P_Font_Extend;
                    XFormat_setProperty(format, property, &value);
                } else if (XString_equals_utf8(name, "color", XChar_CaseSensitive)) {
                    READER_ATTRIBUTE(rgb);
                    const char* value = rgbValue ? XString_toUtf8(rgbValue) : NULL;
                    size_t length = value ? strlen(value) : 0;
                    if (length == 6 || length == 8) {
                        char* end = NULL;
                        unsigned long packed = strtoul(value, &end, 16);
                        if (end && *end == '\0') {
                            XColor color = XColor_create_rgb(
                                (uint8_t)((packed >> 16) & 0xff),
                                (uint8_t)((packed >> 8) & 0xff),
                                (uint8_t)(packed & 0xff),
                                length == 8 ? (uint8_t)((packed >> 24) & 0xff) : 0xff);
                            XFormat_setFontColor(format, &color);
                        }
                    }
                } else if (XString_equals_utf8(name, "vertAlign", XChar_CaseSensitive)) {
                    READER_ATTRIBUTE(val);
                    if (valValue) XFormat_setFontScript(format,
                        XString_equals_utf8(valValue, "superscript", XChar_CaseSensitive)
                            ? XFormat_FontScriptSuper : XFormat_FontScriptSub);
                }
            } else if (in_si && XString_equals_utf8(name, "t", XChar_CaseSensitive)) {
                in_t = true;
                XString_clear_base(acc);
            }
        } else if (tt == XXmlStream_Characters ) {
            if (in_si && in_t) {
                const XString* txt = XXmlStreamReader_text(reader);
                if (txt) XString_append(acc, txt);
            }
        } else if (tt == XXmlStream_EndElement) {
            const XString* name = XXmlStreamReader_name(reader);
            if (!name) continue;

            if (in_si && XString_equals_utf8(name, "t", XChar_CaseSensitive)) {
                in_t = false;
                if (in_run) XRichString_addFragment(rich, acc, format);
                else XRichString_setText(rich, acc);
            } else if (XString_equals_utf8(name, "rPr", XChar_CaseSensitive)) {
                in_run_properties = false;
            } else if (XString_equals_utf8(name, "r", XChar_CaseSensitive)) {
                in_run = false;
                if (format) { XFormat_delete(format); format = NULL; }
            } else if (XString_equals_utf8(name, "si", XChar_CaseSensitive)) {
                in_si = false;
                if (rich && !append_loaded_string(self, rich)) ok = false;
                XRichString_delete(rich);
                rich = NULL;
                XString_clear_base(acc);
                if (!ok) break;
            }
        }
    }
    if (format) XFormat_delete(format);
    if (rich) XRichString_delete(rich);
    XString_delete_base(acc);
    size_t loadedCount = XVector_size_base(self->m_stringList);
    if (declaredUniqueCount >= 0 && (size_t)declaredUniqueCount != loadedCount) ok = false;
    if (declaredCount >= 0 && (size_t)declaredCount < loadedCount) ok = false;
    if (ok && !XXmlStreamReader_hasError(reader))
        self->m_stringCount = declaredCount >= 0 ? declaredCount : 0;
#undef READER_ATTRIBUTE
    return ok && !XXmlStreamReader_hasError(reader);
}

bool XSharedStrings_loadFromXmlData(XSharedStrings* self, const uint8_t* data, size_t len) {
    if (!self || !data || len == 0) return false;

    XByteArray* bytes = XByteArray_create_with_data((const char*)data, len);
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!bytes || !reader) {
        if (bytes) XByteArray_delete_base(bytes);
        if (reader) XXmlStreamReader_delete_base(reader);
        return false;
    }
    shared_strings_clear(self);
    XXmlStreamReader_addData(reader, bytes);
    XByteArray_delete_base(bytes);
    bool ok = sharedStrings_loadFromReader(self, reader);
    XXmlStreamReader_delete_base(reader);
    if (!ok) shared_strings_clear(self);
    return ok;
}

bool XSharedStrings_loadFromXmlFile(XSharedStrings* self, const XString* filePath) {
    if (!self || !filePath) return false;
    
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        return false;
    }
    XByteArray* allData = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    if (!allData) return false;
    
    bool result = XSharedStrings_loadFromXmlData(self, XByteArray_data(allData), XByteArray_size_base(allData));
    XByteArray_delete_base(allData);
    return result;
}
