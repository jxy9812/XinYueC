/******************************************************************************
 * @file       XContentTypes.c
 * @brief      XContentTypes OOXML 内容类型管理实现（对标 QXlsx::ContentTypes）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XContentTypes.h"
#include "XByteArray.h"
#include "XMemory.h"
#include "XFile.h"
#include "XXmlStreamReader.h"
#include <string.h>


/* XMap 键以 intptr_t 保存 XString*，比较时仍使用 XString 原生语义。 */
static int32_t str_compare(const void* lhs, const void* rhs)
{
    XString* ls = (XString*)(*(const intptr_t*)lhs);
    XString* rs = (XString*)(*(const intptr_t*)rhs);
    if (!ls && !rs) return XCompare_Equality;
    if (!ls) return XCompare_Less;
    if (!rs) return XCompare_Greater;
    return XString_compare(ls, rs);
}

/* 内部辅助：接受 const char* 的 addDefault */
static void addDefault_cstr(XContentTypes* self, const char* key, const char* value)
{
    if (!self || !key || !value) return;
    XString* kstr = XString_create();
    if (!kstr) return;
    XString_append_utf8(kstr, key);
    XString* vstr = XString_create();
    if (!vstr) { XString_delete_base(kstr); return; }
    XString_append_utf8(vstr, value);
    intptr_t pk = (intptr_t)kstr;
    intptr_t pv = (intptr_t)vstr;
    XMapBase_insert_base((XMapBase*)self->m_defaults, &pk, &pv);
}

void XContentTypes_addDefault(XContentTypes* self, const XString* key, const XString* value)
{
    if (!self || !key || !value) return;
    addDefault_cstr(self, XString_toUtf8(key), XString_toUtf8(value));
}

/* 释放 map entry 中存的 XString* 副本（被 XMap_deinit 通过 setKeyDeinit/setDataDeinit 回调） */
static void XStringContainer_deinit(void* p)
{
    if (!p) return;
    /* 这里 p 是 intptr_t 字节（sizeof(intptr_t) 大小），不是 XString* 指针 */
    intptr_t v;
    memcpy(&v, p, sizeof(v));
    XString* s = (XString*)v;
    if (s) XString_delete_base(s);
}

/* 内部辅助：接受 const char* 的 addOverride */
static void addOverride_cstr(XContentTypes* self, const char* key, const char* value)
{
    if (!self || !key || !value) return;
    XString* kstr = XString_create();
    if (!kstr) return;
    XString_append_utf8(kstr, key);
    XString* vstr = XString_create();
    if (!vstr) { XString_delete_base(kstr); return; }
    XString_append_utf8(vstr, value);
    intptr_t pk = (intptr_t)kstr;
    intptr_t pv = (intptr_t)vstr;
    XMapBase_insert_base((XMapBase*)self->m_overrides, &pk, &pv);
}

void XContentTypes_addOverride(XContentTypes* self, const XString* key, const XString* value)
{
    if (!self || !key || !value) return;
    addOverride_cstr(self, XString_toUtf8(key), XString_toUtf8(value));
}

void XContentTypes_addDocPropCore(XContentTypes* self)
{ addOverride_cstr(self, "/docProps/core.xml", "application/vnd.openxmlformats-package.core-properties+xml"); }

void XContentTypes_addDocPropApp(XContentTypes* self)
{ addOverride_cstr(self, "/docProps/app.xml", "application/vnd.openxmlformats-officedocument.extended-properties+xml"); }

void XContentTypes_addStyles(XContentTypes* self)
{ addOverride_cstr(self, "/xl/styles.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"); }

void XContentTypes_addTheme(XContentTypes* self)
{ addOverride_cstr(self, "/xl/theme/theme1.xml", "application/vnd.openxmlformats-officedocument.theme+xml"); }

void XContentTypes_addWorkbook(XContentTypes* self)
{ addOverride_cstr(self, "/xl/workbook.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"); }

static void add_named_override(XContentTypes* self, const XString* name,
                               const char* directory, const char* fallbackPrefix,
                               int index, const char* contentType)
{
    const char* supplied = name ? XString_toUtf8(name) : NULL;
    char key[512];
    if (!supplied || !supplied[0]) {
        if (directory && directory[0])
            snprintf(key, sizeof(key), "/xl/%s/%s%d.xml", directory, fallbackPrefix, index);
        else
            snprintf(key, sizeof(key), "/xl/%s%d.xml", fallbackPrefix, index);
    } else if (XString_startsWith_utf8(name, "/", XChar_CaseSensitive)) {
        snprintf(key, sizeof(key), "%s", supplied);
    } else if (XString_startsWith_utf8(name, "xl/", XChar_CaseSensitive)) {
        snprintf(key, sizeof(key), "/%s", supplied);
    } else if (strchr(supplied, '/')) {
        snprintf(key, sizeof(key), "/xl/%s", supplied);
    } else {
        snprintf(key, sizeof(key), "/xl/%s/%s", directory, supplied);
    }
    addOverride_cstr(self, key, contentType);
}

void XContentTypes_addWorksheetName(XContentTypes* self, const XString* name)
{
    if (!self) return;
    add_named_override(self, name, "worksheets", "sheet", self->m_worksheetCount + 1,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml");
    self->m_worksheetCount++;
}

void XContentTypes_addChartsheetName(XContentTypes* self, const XString* name)
{
    if (!self) return;
    add_named_override(self, name, "chartsheets", "sheet", self->m_chartsheetCount + 1,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.chartsheet+xml");
    self->m_chartsheetCount++;
}

void XContentTypes_addChartName(XContentTypes* self, const XString* name)
{
    if (!self) return;
    add_named_override(self, name, "charts", "chart", self->m_chartCount + 1,
        "application/vnd.openxmlformats-officedocument.drawingml.chart+xml");
    self->m_chartCount++;
}

void XContentTypes_addDrawingName(XContentTypes* self, const XString* name)
{
    if (!self) return;
    add_named_override(self, name, "drawings", "drawing", self->m_drawingCount + 1,
        "application/vnd.openxmlformats-officedocument.drawing+xml");
    self->m_drawingCount++;
}

void XContentTypes_addCommentName(XContentTypes* self, const XString* name)
{
    if (!self) return;
    add_named_override(self, name, "", "comments", self->m_commentCount + 1,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml");
    self->m_commentCount++;
}

void XContentTypes_addTableName(XContentTypes* self, const XString* name)
{
    if (!self) return;
    add_named_override(self, name, "tables", "table", self->m_tableCount + 1,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml");
    self->m_tableCount++;
}

void XContentTypes_addExternalLinkName(XContentTypes* self, const XString* name)
{
    if (!self) return;
    add_named_override(self, name, "externalLinks", "externalLink", self->m_externalLinkCount + 1,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.externalLink+xml");
    self->m_externalLinkCount++;
}

void XContentTypes_addSharedString(XContentTypes* self)
{ addOverride_cstr(self, "/xl/sharedStrings.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"); }

void XContentTypes_addVmlName(XContentTypes* self)
{
    char key[256];
    snprintf(key, sizeof(key), "/xl/drawings/vmlDrawing%d.vml", self->m_vmlCount + 1);
    addOverride_cstr(self, key, "application/vnd.openxmlformats-officedocument.vmlDrawing");
    self->m_vmlCount++;
}

void XContentTypes_addCalcChain(XContentTypes* self)
{ addOverride_cstr(self, "/xl/calcChain.xml", "application/vnd.openxmlformats-officedocument.spreadsheetml.calcChain+xml"); }

void XContentTypes_addVbaProject(XContentTypes* self)
{ addOverride_cstr(self, "/xl/vbaProject.bin", "application/vnd.ms-office.vbaProject"); }

void XContentTypes_clearOverrides(XContentTypes* self)
{
    if (self) { XMap_clear_base(self->m_overrides); }
}

/* ========== 创建与销毁 ========== */

XContentTypes* XContentTypes_create(void)
{
    XContentTypes* self = (XContentTypes*)XMalloc_System(sizeof(XContentTypes));
    if (!self) return NULL;
    memset(self, 0, sizeof(XContentTypes));
    /* 用 sizeof(intptr_t) 存 XString*，并注册 deinit 方法让 XMap_delete_base 自动清理 */
    self->m_defaults = XMap_create_ex(sizeof(intptr_t), sizeof(intptr_t), str_compare, false);
    self->m_overrides = XMap_create_ex(sizeof(intptr_t), sizeof(intptr_t), str_compare, false);
    /* 通知 map 在清空条目时调用的 deinit 方法（按 key、按 value 各一次） */
    XMapBaseSetKeyDeinitMethod(self->m_defaults, XStringContainer_deinit);
    XContainerSetDataDeinitMethod(self->m_defaults, XStringContainer_deinit);
    XMapBaseSetKeyDeinitMethod(self->m_overrides, XStringContainer_deinit);
    XContainerSetDataDeinitMethod(self->m_overrides, XStringContainer_deinit);
    self->m_worksheetCount = 0;
    self->m_chartsheetCount = 0;
    self->m_chartCount = 0;
    self->m_drawingCount = 0;
    self->m_commentCount = 0;
    self->m_tableCount = 0;
    self->m_externalLinkCount = 0;
    self->m_vmlCount = 0;
    return self;
}

void XContentTypes_delete(XContentTypes* self)
{
    if (!self) return;
    /* map 创建时已设置 KeyDeinit/DataDeinit 为 XStringContainer_deinit，
       XMap_delete_base 会自动调用它们释放每个 entry 中的 XString* */
    if (self->m_defaults) XMap_delete_base(self->m_defaults);
    if (self->m_overrides) XMap_delete_base(self->m_overrides);
    XFree_System(self);
}

/* ========== XML 序列化 ========== */

bool XContentTypes_saveToXmlData(const XContentTypes* self, uint8_t** outData, size_t* outLen)
{
    if (!self || !outData || !outLen) return false;
    *outData = NULL;
    *outLen = 0;
    
    XByteArray* buf = XByteArray_create();
    if (!buf) return false;
    
    XByteArray_append_utf8(buf, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    XByteArray_append_utf8(buf, "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n");
    
    /* 写入默认类型 */
    if (self->m_defaults && XMap_size_base(self->m_defaults) > 0) {
        XMap_iterator it = XMap_begin(self->m_defaults);
        XMap_iterator end = XMap_end(self->m_defaults);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_defaults, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            if (pair) {
                intptr_t kptr = *(intptr_t*)XPair_first(pair);
                intptr_t vptr = *(intptr_t*)XPair_second(pair);
                XString* ks = (XString*)kptr;
                XString* vs = (XString*)vptr;
                char entry[512];
                snprintf(entry, sizeof(entry), 
                    "  <Default Extension=\"%s\" ContentType=\"%s\"/>\n",
                    XString_toUtf8(ks), XString_toUtf8(vs));
                XByteArray_append_utf8(buf, entry);
            }
        }
    }
    
    /* 写入覆盖类型 */
    if (self->m_overrides && XMap_size_base(self->m_overrides) > 0) {
        XMap_iterator it = XMap_begin(self->m_overrides);
        XMap_iterator end = XMap_end(self->m_overrides);
        for (; !XMap_iterator_isEnd(&it); XMap_iterator_add(self->m_overrides, &it)) {
            XPair* pair = XMap_iterator_data(&it);
            if (pair) {
                intptr_t kptr = *(intptr_t*)XPair_first(pair);
                intptr_t vptr = *(intptr_t*)XPair_second(pair);
                XString* ks = (XString*)kptr;
                XString* vs = (XString*)vptr;
                char entry[512];
                snprintf(entry, sizeof(entry), 
                    "  <Override PartName=\"%s\" ContentType=\"%s\"/>\n",
                    XString_toUtf8(ks), XString_toUtf8(vs));
                XByteArray_append_utf8(buf, entry);
            }
        }
    }
    
    XByteArray_append_utf8(buf, "</Types>\n");
    
    *outData = (uint8_t*)XMalloc_System(XByteArray_size_base(buf) + 1);
    if (*outData) {
        memcpy(*outData, XByteArray_data(buf), XByteArray_size_base(buf));
        *outLen = XByteArray_size_base(buf);
        (*outData)[*outLen] = '\0';
    }
    XByteArray_delete_base(buf);
    return *outData != NULL;
}

bool XContentTypes_saveToXmlFile(const XContentTypes* self, const XString* filePath)
{
    uint8_t* data = NULL;
    size_t len = 0;
    if (!XContentTypes_saveToXmlData(self, &data, &len)) return false;
    
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

static const XString* content_type_attribute(const XXmlStreamAttributes* attributes,
                                             const char* name)
{
    XString_Init_Utf8(attributeName, name);
    const XString* result = XXmlStreamAttributes_value_ex(attributes, NULL, attributeName);
    XString_deinit_base(attributeName);
    return result;
}

bool XContentTypes_loadFromXmlData(XContentTypes* self, const uint8_t* data, size_t len) {
    if (!self || !data || len == 0) return false;
    XByteArray* bytes = XByteArray_create_with_data((const char*)data, len);
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!bytes || !reader) {
        if (bytes) XByteArray_delete_base(bytes);
        if (reader) XXmlStreamReader_delete_base(reader);
        return false;
    }
    XMap_clear_base(self->m_defaults);
    XMap_clear_base(self->m_overrides);
    self->m_worksheetCount = self->m_chartsheetCount = self->m_chartCount = 0;
    self->m_drawingCount = self->m_commentCount = self->m_tableCount = 0;
    self->m_externalLinkCount = self->m_vmlCount = 0;
    XXmlStreamReader_addData(reader, bytes);
    XByteArray_delete_base(bytes);
    bool sawRoot = false;
    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        if (token != XXmlStream_StartElement) continue;
        const XString* element = XXmlStreamReader_name(reader);
        const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
        if (!element) continue;
        if (XString_equals_utf8(element, "Types", XChar_CaseSensitive)) {
            sawRoot = true;
        } else if (XString_equals_utf8(element, "Default", XChar_CaseSensitive)) {
            const XString* extension = content_type_attribute(attributes, "Extension");
            const XString* contentType = content_type_attribute(attributes, "ContentType");
            if (extension && contentType) XContentTypes_addDefault(self, extension, contentType);
        } else if (XString_equals_utf8(element, "Override", XChar_CaseSensitive)) {
            const XString* partName = content_type_attribute(attributes, "PartName");
            const XString* contentType = content_type_attribute(attributes, "ContentType");
            if (partName && contentType) XContentTypes_addOverride(self, partName, contentType);
        }
    }
    bool result = sawRoot && !XXmlStreamReader_hasError(reader);
    XXmlStreamReader_delete_base(reader);
    if (!result) {
        XMap_clear_base(self->m_defaults);
        XMap_clear_base(self->m_overrides);
    }
    return result;
}

bool XContentTypes_loadFromXmlFile(XContentTypes* self, const XString* filePath) {
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
    
    bool result = XContentTypes_loadFromXmlData(self, XByteArray_data(allData), XByteArray_size_base(allData));
    XByteArray_delete_base(allData);
    return result;
}

/* ========== UTF-8 便捷变体 ========== */

void XContentTypes_addDefault_utf8(XContentTypes* self, const char* key, const char* value)
{
    addDefault_cstr(self, key, value);
}

void XContentTypes_addOverride_utf8(XContentTypes* self, const char* key, const char* value)
{
    addOverride_cstr(self, key, value);
}

void XContentTypes_addWorksheetName_utf8(XContentTypes* self, const char* name)
{
    XString* value = name ? XString_create_utf8(name) : NULL;
    XContentTypes_addWorksheetName(self, value);
    if (value) XString_delete_base(value);
}
