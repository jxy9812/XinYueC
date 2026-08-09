/******************************************************************************
 * @file       XRelationships.c
 * @brief      XRelationships OOXML 关系类（对标 QXlsx::Relationships）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XRelationships.h"
#include "XMemory.h"
#include "XString.h"
#include "XVector.h"
#include "XFile.h"
#include "XByteArray.h"
#include "XXmlStreamReader.h"
#include "XXmlStreamWriter.h"
#include <string.h>
#include <stdlib.h>

/* 仅供旧的无实例 API 返回最近一次结果；内部逻辑使用每个集合的计数。 */
static int g_lastAssignedRid = 0;

static int next_rid(const XRelationships* self)
{
    int candidate = 1;
    while (self && self->m_relationships) {
        char id[32];
        snprintf(id, sizeof(id), "rId%d", candidate);
        bool used = false;
        size_t count = XVector_size_base(self->m_relationships);
        for (size_t i = 0; i < count; ++i) {
            XlsxRelationship* relationship =
                (XlsxRelationship*)XVector_at_base(self->m_relationships, i);
            if (relationship && relationship->m_id &&
                XString_equals_utf8(relationship->m_id, id, XChar_CaseSensitive)) {
                used = true;
                break;
            }
        }
        if (!used) break;
        ++candidate;
    }
    return candidate;
}

/* 添加关系 */
static void addRel(XRelationships* self, const char* type, const char* target, const char* targetMode) {
    if (!self || !type || !target) return;
    if (!self->m_relationships) {
        self->m_relationships = XVector_Create(XlsxRelationship);
    }
    if (!self->m_relationships) return;
    XlsxRelationship rel;
    memset(&rel, 0, sizeof(rel));
    rel.m_id = XString_create();
    int assigned = next_rid(self);
    char idBuf[16];
    snprintf(idBuf, sizeof(idBuf), "rId%d", assigned);
    if (rel.m_id) XString_append_utf8(rel.m_id, idBuf);
    rel.m_type = XString_create();
    if (rel.m_type) XString_append_utf8(rel.m_type, type);
    rel.m_target = XString_create();
    if (rel.m_target) XString_append_utf8(rel.m_target, target);
    if (targetMode) {
        rel.m_targetMode = XString_create();
        if (rel.m_targetMode) XString_append_utf8(rel.m_targetMode, targetMode);
    }
    if (!rel.m_id || !rel.m_type || !rel.m_target ||
        (targetMode && !rel.m_targetMode) ||
        !XVector_push_back_2(self->m_relationships, &rel, 1)) {
        if (rel.m_id) XString_delete_base(rel.m_id);
        if (rel.m_type) XString_delete_base(rel.m_type);
        if (rel.m_target) XString_delete_base(rel.m_target);
        if (rel.m_targetMode) XString_delete_base(rel.m_targetMode);
        return;
    }
    self->m_lastAssignedRid = assigned;
    g_lastAssignedRid = assigned;
}

static void add_category_relationship(XRelationships* self, const char* base,
                                      const XString* relativeType, const XString* target,
                                      const XString* targetMode)
{
    const char* relative = relativeType ? XString_toUtf8(relativeType) : NULL;
    const char* targetText = target ? XString_toUtf8(target) : NULL;
    if (!relative || !targetText) return;
    char fullType[1024];
    const char* type = relative;
    if (!XString_contains_utf8(relativeType, "://", XChar_CaseSensitive)) {
        snprintf(fullType, sizeof(fullType), "%s%s", base, relative);
        type = fullType;
    }
    addRel(self, type, targetText, targetMode ? XString_toUtf8(targetMode) : NULL);
}

void XRelationships_addDocumentRelationship(XRelationships* self, const XString* relativeType, const XString* target) {
    add_category_relationship(self,
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/",
        relativeType, target, NULL);
}

void XRelationships_addPackageRelationship(XRelationships* self, const XString* relativeType, const XString* target) {
    add_category_relationship(self,
        "http://schemas.openxmlformats.org/package/2006/relationships/",
        relativeType, target, NULL);
}

void XRelationships_addWorksheetRelationship(XRelationships* self, const XString* relativeType, const XString* target, const XString* targetMode) {
    add_category_relationship(self,
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/",
        relativeType, target, targetMode);
}

void XRelationships_addMsPackageRelationship(XRelationships* self, const XString* relativeType, const XString* target) {
    add_category_relationship(self,
        "http://schemas.microsoft.com/office/2006/relationships/",
        relativeType, target, NULL);
}

XlsxRelationship* XRelationships_getRelationshipById(const XRelationships* self, const XString* id) {
    if (!self || !self->m_relationships || !id) return NULL;
    size_t count = XVector_size_base(self->m_relationships);
    for (size_t i = 0; i < count; i++) {
        XlsxRelationship* rel = (XlsxRelationship*)XVector_at_base(self->m_relationships, i);
        if (rel && rel->m_id) {
            if (rel->m_id && XString_equals(rel->m_id, id, XChar_CaseSensitive)) return rel;
        }
    }
    return NULL;
}

void XRelationships_clear(XRelationships* self) {
    if (!self) return;
    if (!self->m_relationships) {
        self->m_lastAssignedRid = 0;
        return;
    }
    size_t count = XVector_size_base(self->m_relationships);
    for (size_t i = 0; i < count; i++) {
        XlsxRelationship* rel = (XlsxRelationship*)XVector_at_base(self->m_relationships, i);
        if (rel) {
            if (rel->m_id) { XString_delete_base(rel->m_id); }
            if (rel->m_type) { XString_delete_base(rel->m_type); }
            if (rel->m_target) { XString_delete_base(rel->m_target); }
            if (rel->m_targetMode) { XString_delete_base(rel->m_targetMode); }
        }
    }
    XVector_clear_base(self->m_relationships);
    self->m_lastAssignedRid = 0;
}

int XRelationships_count(const XRelationships* self) {
    return self && self->m_relationships ? (int)XVector_size_base(self->m_relationships) : 0;
}

bool XRelationships_isEmpty(const XRelationships* self) {
    return !self || !self->m_relationships || XVector_size_base(self->m_relationships) == 0;
}

XRelationships* XRelationships_create(void) {
    XRelationships* self = (XRelationships*)XMalloc_System(sizeof(XRelationships));
    if (!self) return NULL;
    memset(self, 0, sizeof(XRelationships));
    self->m_relationships = XVector_Create(XlsxRelationship);
    return self;
}

void XRelationships_delete(XRelationships* self) {
    if (!self) return;
    if (self->m_relationships) {
        size_t count = XVector_size_base(self->m_relationships);
        for (size_t i = 0; i < count; i++) {
            XlsxRelationship* rel = (XlsxRelationship*)XVector_at_base(self->m_relationships, i);
            if (rel) {
                if (rel->m_id) { XString_delete_base(rel->m_id); }
                if (rel->m_type) { XString_delete_base(rel->m_type); }
                if (rel->m_target) { XString_delete_base(rel->m_target); }
                if (rel->m_targetMode) { XString_delete_base(rel->m_targetMode); }
            }
        }
        XVector_delete_base(self->m_relationships);
    }
    XFree_System(self);
}

/* 保存为 XML 文件 */
bool XRelationships_saveToXmlFile(const XRelationships* self, const XString* filePath) {
    if (!self || !filePath) return false;
    
    uint8_t* data = NULL;
    size_t len = 0;
    if (!XRelationships_saveToXmlData(self, &data, &len)) return false;
    
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

/* 从 XML 文件加载 */
bool XRelationships_loadFromXmlFile(XRelationships* self, const XString* filePath) {
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
    bool result = XRelationships_loadFromXmlData(self, XByteArray_data(allData),
        XByteArray_size_base((XContainer*)allData));
    XByteArray_delete_base(allData);
    return result;
}

/* 保存为 XML 数据 */
bool XRelationships_saveToXmlData(const XRelationships* self, uint8_t** outData, size_t* outLen) {
    if (!self || !outData || !outLen) return false;
    *outData = NULL;
    *outLen = 0;
    
    XXmlStreamWriter* writer = XXmlStreamWriter_create();
    if (!writer) return false;
    XXmlStreamWriter_writeStartDocument(writer);
    XXmlStreamWriter_writeStartElement_utf8(writer, "Relationships");
    XXmlStreamWriter_writeDefaultNamespace_utf8(writer,
        "http://schemas.openxmlformats.org/package/2006/relationships");
    
    if (self->m_relationships) {
        size_t count = XVector_size_base(self->m_relationships);
        for (size_t i = 0; i < count; i++) {
            XlsxRelationship* rel = (XlsxRelationship*)XVector_at_base(self->m_relationships, i);
            if (!rel) continue;
            
            if (!rel->m_id || !rel->m_type || !rel->m_target) continue;
            XXmlStreamWriter_writeEmptyElement_utf8(writer, "Relationship");
            XXmlStreamWriter_writeAttribute_utf8(writer, "Id", XString_toUtf8(rel->m_id));
            XXmlStreamWriter_writeAttribute_utf8(writer, "Type", XString_toUtf8(rel->m_type));
            XXmlStreamWriter_writeAttribute_utf8(writer, "Target", XString_toUtf8(rel->m_target));
            if (rel->m_targetMode && XString_size_base(rel->m_targetMode) > 0) {
                XXmlStreamWriter_writeAttribute_utf8(writer, "TargetMode",
                    XString_toUtf8(rel->m_targetMode));
            }
        }
    }
    XXmlStreamWriter_writeEndDocument(writer);
    XByteArray* buf = XXmlStreamWriter_toByteArray(writer);
    size_t size = buf ? XByteArray_size_base((XContainer*)buf) : 0;
    *outData = (uint8_t*)XMalloc_System(size + 1);
    if (*outData && !XXmlStreamWriter_hasError(writer) && size > 0) {
        memcpy(*outData, XByteArray_data(buf), size);
        (*outData)[size] = '\0';
        *outLen = size;
    } else {
        XFree_System(*outData);
        *outData = NULL;
    }
    XXmlStreamWriter_delete_base(writer);
    return *outData != NULL;
}

static const XString* relationship_attribute(const XXmlStreamAttributes* attributes,
                                             const char* name)
{
    if (!attributes || !name) return NULL;
    XString_Init_Utf8(key, name);
    const XString* value = XXmlStreamAttributes_value_ex(attributes, NULL, key);
    XString_deinit_base(key);
    return value;
}

/* 从 XML 数据加载 */
bool XRelationships_loadFromXmlData(XRelationships* self, const uint8_t* data, size_t len) {
    if (!self || !data || len == 0) return false;
    XByteArray* bytes = XByteArray_create_with_data((const char*)data, len);
    XXmlStreamReader* reader = XXmlStreamReader_create();
    if (!bytes || !reader) {
        if (bytes) XByteArray_delete_base(bytes);
        if (reader) XXmlStreamReader_delete_base(reader);
        return false;
    }
    XRelationships_clear(self);
    self->m_lastAssignedRid = 0;
    XXmlStreamReader_addData(reader, bytes);
    XByteArray_delete_base(bytes);
    bool rootSeen = false;
    while (!XXmlStreamReader_atEnd(reader)) {
        int token = XXmlStreamReader_readNext(reader);
        if (token != XXmlStream_StartElement) continue;
        const XString* name = XXmlStreamReader_name(reader);
        if (name && XString_equals_utf8(name, "Relationships", XChar_CaseSensitive)) {
            rootSeen = true;
            continue;
        }
        if (!name || !XString_equals_utf8(name, "Relationship", XChar_CaseSensitive)) continue;
        const XXmlStreamAttributes* attributes = XXmlStreamReader_attributes(reader);
        const XString* id = relationship_attribute(attributes, "Id");
        const XString* type = relationship_attribute(attributes, "Type");
        const XString* target = relationship_attribute(attributes, "Target");
        const XString* mode = relationship_attribute(attributes, "TargetMode");
        if (!id || !type || !target) continue;
        XlsxRelationship relationship;
        memset(&relationship, 0, sizeof(relationship));
        relationship.m_id = XString_create_copy(id);
        relationship.m_type = XString_create_copy(type);
        relationship.m_target = XString_create_copy(target);
        relationship.m_targetMode = mode ? XString_create_copy(mode) : NULL;
        if (!relationship.m_id || !relationship.m_type || !relationship.m_target ||
            !XVector_push_back_1_base(self->m_relationships, &relationship)) {
            if (relationship.m_id) XString_delete_base(relationship.m_id);
            if (relationship.m_type) XString_delete_base(relationship.m_type);
            if (relationship.m_target) XString_delete_base(relationship.m_target);
            if (relationship.m_targetMode) XString_delete_base(relationship.m_targetMode);
            continue;
        }
        const char* idText = XString_toUtf8(id);
        int numeric = XString_startsWith_utf8(id, "rId", XChar_CaseSensitive) ? atoi(idText + 3) : 0;
        if (numeric > self->m_lastAssignedRid) self->m_lastAssignedRid = numeric;
    }
    bool result = rootSeen && !XXmlStreamReader_hasError(reader);
    XXmlStreamReader_delete_base(reader);
    if (!result) XRelationships_clear(self);
    return result;
}

/* 按类型查询 */
static bool relationship_type_matches(const XString* actual, const char* categoryBase,
                                      const XString* relativeType)
{
    if (!actual || !categoryBase) return false;
    if (relativeType && XString_contains_utf8(relativeType, "://", XChar_CaseSensitive))
        return XString_equals(actual, relativeType, XChar_CaseSensitive);
    if (!XString_startsWith_utf8(actual, categoryBase, XChar_CaseSensitive)) return false;
    return !relativeType || XString_length_base(relativeType) == 0 ||
        XString_endsWith(actual, relativeType, XChar_CaseSensitive);
}

static XlsxRelationship** queryByType(const XRelationships* self, const char* categoryBase,
                                      const XString* relativeType, int* outCount) {
    if (outCount) *outCount = 0;
    if (!outCount || !self || !self->m_relationships) return NULL;
    
    int count = 0;
    size_t total = XVector_size_base(self->m_relationships);
    for (size_t i = 0; i < total; i++) {
        XlsxRelationship* rel = (XlsxRelationship*)XVector_at_base(self->m_relationships, i);
        if (rel && rel->m_type &&
            relationship_type_matches(rel->m_type, categoryBase, relativeType)) count++;
    }
    
    if (count == 0) return NULL;
    
    XlsxRelationship** result = (XlsxRelationship**)XMalloc_System(sizeof(XlsxRelationship*) * count);
    if (!result) return NULL;
    
    int idx = 0;
    for (size_t i = 0; i < total && idx < count; i++) {
        XlsxRelationship* rel = (XlsxRelationship*)XVector_at_base(self->m_relationships, i);
        if (rel && rel->m_type &&
            relationship_type_matches(rel->m_type, categoryBase, relativeType))
            result[idx++] = rel;
    }
    
    *outCount = count;
    return result;
}

XlsxRelationship** XRelationships_documentRelationships(const XRelationships* self, const XString* relativeType, int* outCount) {
    return queryByType(self,
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/",
        relativeType, outCount);
}

XlsxRelationship** XRelationships_packageRelationships(const XRelationships* self, const XString* relativeType, int* outCount) {
    return queryByType(self,
        "http://schemas.openxmlformats.org/package/2006/relationships/",
        relativeType, outCount);
}

XlsxRelationship** XRelationships_msPackageRelationships(const XRelationships* self, const XString* relativeType, int* outCount) {
    return queryByType(self,
        "http://schemas.microsoft.com/office/2006/relationships/",
        relativeType, outCount);
}

XlsxRelationship** XRelationships_worksheetRelationships(const XRelationships* self, const XString* relativeType, int* outCount) {
    return queryByType(self,
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/",
        relativeType, outCount);
}
int XRelationships_lastAssignedRid(void) {
    return g_lastAssignedRid;
}

int XRelationships_lastAssignedRidFor(const XRelationships* self) {
    return self ? self->m_lastAssignedRid : 0;
}
