#include "XSimpleOOXmlFile.h"
#include "XMemory.h"
#include <stdlib.h>
#include <string.h>

XSimpleOOXmlFile* XSimpleOOXmlFile_create(XAbstractOOXmlFile_CreateFlag flag) {
    XSimpleOOXmlFile* self = (XSimpleOOXmlFile*)XMalloc_System(sizeof(XSimpleOOXmlFile));
    if (!self) return NULL; memset(self, 0, sizeof(XSimpleOOXmlFile));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_xmlData = XByteArray_create();
    return self;
}
void XSimpleOOXmlFile_delete(XSimpleOOXmlFile* self) {
    if (!self) return;
    if (self->m_xmlData) { XByteArray_deinit_base(self->m_xmlData); XFree_System(self->m_xmlData); }
    XAbstractOOXmlFile_deinit(&self->m_base); XFree_System(self);
}
void XSimpleOOXmlFile_setXmlData(XSimpleOOXmlFile* self, const char* data, size_t len) {
    if (!self || !self->m_xmlData) return;
    XByteArray_clear_base(self->m_xmlData);
    XByteArray_append_2(self->m_xmlData, (const uint8_t*)data, len);
}
const char* XSimpleOOXmlFile_xmlData(const XSimpleOOXmlFile* self, size_t* len) {
    if (!self || !self->m_xmlData) { if (len) *len = 0; return NULL; }
    if (len) *len = XByteArray_size_base(self->m_xmlData);
    return (const char*)XByteArray_data(self->m_xmlData);
}
/**
 * @brief     保存 XML 到文件
 * @param self     XSimpleOOXmlFile 指针
 * @param filePath 文件路径
 * @return    成功返回true
 */
bool XSimpleOOXmlFile_saveToXmlFile(XSimpleOOXmlFile* self, const char* filePath) {
    if (!self || !filePath) return false;
    FILE* fp = fopen(filePath, "wb");
    if (!fp) return false;
    if (self->m_xmlData && XByteArray_size_base(self->m_xmlData) > 0) {
        size_t len = XByteArray_size_base(self->m_xmlData);
        const uint8_t* data = XByteArray_data(self->m_xmlData);
        fwrite(data, 1, len, fp);
    }
    fclose(fp);
    return true;
}

/**
 * @brief     从文件加载 XML
 * @param self     XSimpleOOXmlFile 指针
 * @param filePath 文件路径
 * @return    成功返回true
 */
bool XSimpleOOXmlFile_loadFromXmlFile(XSimpleOOXmlFile* self, const char* filePath) {
    if (!self || !filePath) return false;
    FILE* fp = fopen(filePath, "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size <= 0) { fclose(fp); return false; }
    uint8_t* data = (uint8_t*)XMalloc_System((size_t)size);
    if (!data) { fclose(fp); return false; }
    size_t read = fread(data, 1, (size_t)size, fp);
    fclose(fp);
    if (read != (size_t)size) { XFree_System(data); return false; }
    XSimpleOOXmlFile_setXmlData(self, (const char*)data, (size_t)size);
    XFree_System(data);
    return true;
}
