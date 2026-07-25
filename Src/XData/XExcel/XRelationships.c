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
#include <string.h>

/* 关系 ID 计数器 */
static int g_relIdCounter = 1;

/* 添加关系 */
static void addRel(XRelationships* self, const char* type, const char* target, const char* targetMode) {
    if (!self || !type || !target) return;
    if (!self->m_relationships) {
        self->m_relationships = XVector_Create(XlsxRelationship);
    }
    XlsxRelationship rel;
    memset(&rel, 0, sizeof(rel));
    rel.m_id = XString_create();
    char idBuf[16];
    snprintf(idBuf, sizeof(idBuf), "rId%d", g_relIdCounter++);
    XString_append_utf8(rel.m_id, idBuf);
    rel.m_type = XString_create();
    XString_append_utf8(rel.m_type, type);
    rel.m_target = XString_create();
    XString_append_utf8(rel.m_target, target);
    if (targetMode) {
        rel.m_targetMode = XString_create();
        XString_append_utf8(rel.m_targetMode, targetMode);
    }
    XVector_push_back_1_base(self->m_relationships, &rel);
}

void XRelationships_addDocumentRelationship(XRelationships* self, const XString* relativeType, const XString* target) {
    addRel(self, relativeType ? XString_toUtf8(relativeType) : NULL, target ? XString_toUtf8(target) : NULL, NULL);
}

void XRelationships_addPackageRelationship(XRelationships* self, const XString* relativeType, const XString* target) {
    addRel(self, relativeType ? XString_toUtf8(relativeType) : NULL, target ? XString_toUtf8(target) : NULL, NULL);
}

void XRelationships_addWorksheetRelationship(XRelationships* self, const XString* relativeType, const XString* target, const XString* targetMode) {
    addRel(self, relativeType ? XString_toUtf8(relativeType) : NULL, target ? XString_toUtf8(target) : NULL, targetMode ? XString_toUtf8(targetMode) : NULL);
}

void XRelationships_addMsPackageRelationship(XRelationships* self, const XString* relativeType, const XString* target) {
    addRel(self, relativeType ? XString_toUtf8(relativeType) : NULL, target ? XString_toUtf8(target) : NULL, NULL);
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
    if (!self || !self->m_relationships) return;
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
        if (file) XFile_deleteLater(file);
        XFree_System(data);
        return false;
    }
    XIODevice_write_1((XIODevice*)file, data, (int64_t)len);
    XIODevice_close_base((XIODevice*)file);
    XFile_deleteLater(file);
    XFree_System(data);
    return true;
}

/* 从 XML 文件加载 */
bool XRelationships_loadFromXmlFile(XRelationships* self, const XString* filePath) {
    if (!self || !filePath) return false;
    
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XFile_deleteLater(file);
        return false;
    }
    XByteArray* allData = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XFile_deleteLater(file);
    if (!allData) return false;
    
    size_t size = XByteArray_size_base(allData);
    char* xml = (char*)XMalloc_System(size + 1);
    if (!xml) { XByteArray_delete_base(allData); return false; }
    memcpy(xml, XByteArray_data(allData), size);
    xml[size] = '\0';
    XByteArray_delete_base(allData);
    
    /* 解析 Relationship 标签 */
    char* p = xml;
    while ((p = strstr(p, "<Relationship")) != NULL) {
        char* endTag = strchr(p, '>');
        if (!endTag) break;
        
        char id[64] = {0}, type[512] = {0}, target[512] = {0}, targetMode[64] = {0};
        
        /* 简单属性解析 */
        char* attrs = p + 12; /* 跳过 "<Relationship" */
        char* attrEnd = endTag;
        
        /* 提取 Id */
        char* idP = strstr(attrs, "Id=\"");
        if (idP && idP < attrEnd) {
            idP += 4;
            char* idEnd = strchr(idP, '"');
            if (idEnd && idEnd < attrEnd) {
                size_t len = idEnd - idP;
                if (len < sizeof(id)) { memcpy(id, idP, len); id[len] = '\0'; }
            }
        }
        
        /* 提取 Type */
        char* typeP = strstr(attrs, "Type=\"");
        if (typeP && typeP < attrEnd) {
            typeP += 6;
            char* typeEnd = strchr(typeP, '"');
            if (typeEnd && typeEnd < attrEnd) {
                size_t len = typeEnd - typeP;
                if (len < sizeof(type)) { memcpy(type, typeP, len); type[len] = '\0'; }
            }
        }
        
        /* 提取 Target */
        char* targetP = strstr(attrs, "Target=\"");
        if (targetP && targetP < attrEnd) {
            targetP += 9;
            char* targetEnd = strchr(targetP, '"');
            if (targetEnd && targetEnd < attrEnd) {
                size_t len = targetEnd - targetP;
                if (len < sizeof(target)) { memcpy(target, targetP, len); target[len] = '\0'; }
            }
        }
        
        /* 提取 TargetMode */
        char* modeP = strstr(attrs, "TargetMode=\"");
        if (modeP && modeP < attrEnd) {
            modeP += 12;
            char* modeEnd = strchr(modeP, '"');
            if (modeEnd && modeEnd < attrEnd) {
                size_t len = modeEnd - modeP;
                if (len < sizeof(targetMode)) { memcpy(targetMode, modeP, len); targetMode[len] = '\0'; }
            }
        }
        
        if (strlen(id) > 0 && strlen(type) > 0 && strlen(target) > 0) {
            /* 添加关系 */
            if (!self->m_relationships) {
                self->m_relationships = XVector_Create(XlsxRelationship);
            }
            XlsxRelationship rel;
            memset(&rel, 0, sizeof(rel));
            rel.m_id = XString_create();
            XString_append_utf8(rel.m_id, id);
            rel.m_type = XString_create();
            XString_append_utf8(rel.m_type, type);
            rel.m_target = XString_create();
            XString_append_utf8(rel.m_target, target);
            if (strlen(targetMode) > 0) {
                rel.m_targetMode = XString_create();
                XString_append_utf8(rel.m_targetMode, targetMode);
            }
            XVector_push_back_1_base(self->m_relationships, &rel);
        }
        
        p = endTag + 1;
    }
    
    XFree_System(xml);
    return true;
}

/* 保存为 XML 数据 */
bool XRelationships_saveToXmlData(const XRelationships* self, uint8_t** outData, size_t* outLen) {
    if (!self || !outData || !outLen) return false;
    *outData = NULL;
    *outLen = 0;
    
    /* 直接在内存中构建 XML */
    XByteArray* buf = XByteArray_create();
    if (!buf) return false;
    
    XByteArray_append_utf8(buf, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n");
    XByteArray_append_utf8(buf, "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    
    if (self->m_relationships) {
        size_t count = XVector_size_base(self->m_relationships);
        for (size_t i = 0; i < count; i++) {
            XlsxRelationship* rel = (XlsxRelationship*)XVector_at_base(self->m_relationships, i);
            if (!rel) continue;
            
            XByteArray_append_utf8(buf, "<Relationship");
            if (rel->m_id) {
                XByteArray_append_utf8(buf, " Id=\"");
                XByteArray_append_utf8(buf, XString_toUtf8(rel->m_id));
                XByteArray_append_utf8(buf, "\"");
            }
            if (rel->m_type) {
                XByteArray_append_utf8(buf, " Type=\"");
                XByteArray_append_utf8(buf, XString_toUtf8(rel->m_type));
                XByteArray_append_utf8(buf, "\"");
            }
            if (rel->m_target) {
                XByteArray_append_utf8(buf, " Target=\"");
                XByteArray_append_utf8(buf, XString_toUtf8(rel->m_target));
                XByteArray_append_utf8(buf, "\"");
            }
            if (rel->m_targetMode && XString_size_base(rel->m_targetMode) > 0) {
                XByteArray_append_utf8(buf, " TargetMode=\"");
                XByteArray_append_utf8(buf, XString_toUtf8(rel->m_targetMode));
                XByteArray_append_utf8(buf, "\"");
            }
            XByteArray_append_utf8(buf, "/>");
        }
    }
    
    XByteArray_append_utf8(buf, "</Relationships>");
    
    size_t size = XByteArray_size_base(buf);
    *outData = (uint8_t*)XMalloc_System(size + 1);
    if (*outData) {
        memcpy(*outData, XByteArray_data(buf), size);
        (*outData)[size] = '\0';
        *outLen = size;
    }
    XByteArray_delete_base(buf);
    return *outData != NULL;
}

/* 从 XML 数据加载 */
bool XRelationships_loadFromXmlData(XRelationships* self, const uint8_t* data, size_t len) {
    if (!self || !data || len == 0) return false;
    
    /* 直接在内存中解析 */
    char* xml = (char*)XMalloc_System(len + 1);
    if (!xml) return false;
    memcpy(xml, data, len);
    xml[len] = '\0';
    
    /* 解析 Relationship 标签 */
    char* p = xml;
    while ((p = strstr(p, "<Relationship")) != NULL) {
        char* endTag = strchr(p, '>');
        if (!endTag) break;
        
        char id[64] = {0}, type[512] = {0}, target[512] = {0}, targetMode[64] = {0};
        
        char* attrs = p + 12;
        char* attrEnd = endTag;
        
        char* idP = strstr(attrs, "Id=\"");
        if (idP && idP < attrEnd) {
            idP += 4;
            char* idEnd = strchr(idP, '"');
            if (idEnd && idEnd < attrEnd) {
                size_t l = idEnd - idP;
                if (l < sizeof(id)) { memcpy(id, idP, l); id[l] = '\0'; }
            }
        }
        
        char* typeP = strstr(attrs, "Type=\"");
        if (typeP && typeP < attrEnd) {
            typeP += 6;
            char* typeEnd = strchr(typeP, '"');
            if (typeEnd && typeEnd < attrEnd) {
                size_t l = typeEnd - typeP;
                if (l < sizeof(type)) { memcpy(type, typeP, l); type[l] = '\0'; }
            }
        }
        
        char* targetP = strstr(attrs, "Target=\"");
        if (targetP && targetP < attrEnd) {
            targetP += 9;
            char* targetEnd = strchr(targetP, '"');
            if (targetEnd && targetEnd < attrEnd) {
                size_t l = targetEnd - targetP;
                if (l < sizeof(target)) { memcpy(target, targetP, l); target[l] = '\0'; }
            }
        }
        
        char* modeP = strstr(attrs, "TargetMode=\"");
        if (modeP && modeP < attrEnd) {
            modeP += 12;
            char* modeEnd = strchr(modeP, '"');
            if (modeEnd && modeEnd < attrEnd) {
                size_t l = modeEnd - modeP;
                if (l < sizeof(targetMode)) { memcpy(targetMode, modeP, l); targetMode[l] = '\0'; }
            }
        }
        
        if (strlen(id) > 0 && strlen(type) > 0 && strlen(target) > 0) {
            if (!self->m_relationships) {
                self->m_relationships = XVector_Create(XlsxRelationship);
            }
            XlsxRelationship rel;
            memset(&rel, 0, sizeof(rel));
            rel.m_id = XString_create();
            XString_append_utf8(rel.m_id, id);
            rel.m_type = XString_create();
            XString_append_utf8(rel.m_type, type);
            rel.m_target = XString_create();
            XString_append_utf8(rel.m_target, target);
            if (strlen(targetMode) > 0) {
                rel.m_targetMode = XString_create();
                XString_append_utf8(rel.m_targetMode, targetMode);
            }
            XVector_push_back_1_base(self->m_relationships, &rel);
        }
        
        p = endTag + 1;
    }
    
    XFree_System(xml);
    return true;
}

/* 按类型查询 */
static XlsxRelationship** queryByType(const XRelationships* self, const char* typePrefix, int* outCount) {
    *outCount = 0;
    if (!self || !self->m_relationships) return NULL;
    
    int count = 0;
    size_t total = XVector_size_base(self->m_relationships);
    for (size_t i = 0; i < total; i++) {
        XlsxRelationship* rel = (XlsxRelationship*)XVector_at_base(self->m_relationships, i);
        if (rel && rel->m_type) {
            const char* t = XString_toUtf8(rel->m_type);
            if (t && strstr(t, typePrefix) != NULL) count++;
        }
    }
    
    if (count == 0) return NULL;
    
    XlsxRelationship** result = (XlsxRelationship**)XMalloc_System(sizeof(XlsxRelationship*) * count);
    if (!result) return NULL;
    
    int idx = 0;
    for (size_t i = 0; i < total && idx < count; i++) {
        XlsxRelationship* rel = (XlsxRelationship*)XVector_at_base(self->m_relationships, i);
        if (rel && rel->m_type) {
            const char* t = XString_toUtf8(rel->m_type);
            if (t && strstr(t, typePrefix) != NULL) result[idx++] = rel;
        }
    }
    
    *outCount = count;
    return result;
}

XlsxRelationship** XRelationships_documentRelationships(const XRelationships* self, const XString* relativeType, int* outCount) {
    (void)relativeType;
    return queryByType(self, "document", outCount);
}

XlsxRelationship** XRelationships_packageRelationships(const XRelationships* self, const XString* relativeType, int* outCount) {
    (void)relativeType;
    return queryByType(self, "package", outCount);
}

XlsxRelationship** XRelationships_msPackageRelationships(const XRelationships* self, const XString* relativeType, int* outCount) {
    (void)relativeType;
    return queryByType(self, "ms", outCount);
}

XlsxRelationship** XRelationships_worksheetRelationships(const XRelationships* self, const XString* relativeType, int* outCount) {
    (void)relativeType;
    return queryByType(self, "worksheet", outCount);
}
