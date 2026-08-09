#include "XSimpleOOXmlFile.h"
#include "XMemory.h"
#include "XFile.h"
#include "XByteArray.h"
#include <string.h>

XSimpleOOXmlFile* XSimpleOOXmlFile_create(XAbstractOOXmlFile_CreateFlag flag) {
    XSimpleOOXmlFile* self = (XSimpleOOXmlFile*)XMalloc_System(sizeof(XSimpleOOXmlFile));
    if (!self) return NULL; memset(self, 0, sizeof(XSimpleOOXmlFile));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    self->m_xmlData = XString_create();
    return self;
}
void XSimpleOOXmlFile_delete(XSimpleOOXmlFile* self) {
    if (!self) return;
    if (self->m_xmlData) XString_delete_base(self->m_xmlData);
    XAbstractOOXmlFile_deinit(&self->m_base); XFree_System(self);
}
void XSimpleOOXmlFile_setXmlData(XSimpleOOXmlFile* self, const XString* data) {
    if (!self || !self->m_xmlData) return;
    XString_clear_base(self->m_xmlData);
    if (data) XString_append(self->m_xmlData, data);
}
const XString* XSimpleOOXmlFile_xmlData(const XSimpleOOXmlFile* self) {
    if (!self || !self->m_xmlData) return NULL;
    return self->m_xmlData;
}

bool XSimpleOOXmlFile_saveToXmlData(const XSimpleOOXmlFile* self, uint8_t** data, size_t* length) {
    if (!self || !data || !length) return false;
    *data = NULL;
    *length = 0;
    const char* utf8 = self->m_xmlData ? XString_toUtf8(self->m_xmlData) : "";
    if (!utf8) utf8 = "";
    size_t byteLength = strlen(utf8);
    uint8_t* result = (uint8_t*)XMalloc_System(byteLength + 1);
    if (!result) return false;
    if (byteLength > 0) memcpy(result, utf8, byteLength);
    result[byteLength] = 0;
    *data = result;
    *length = byteLength;
    return true;
}

bool XSimpleOOXmlFile_loadFromXmlData(XSimpleOOXmlFile* self, const uint8_t* data, size_t length) {
    if (!self || (!data && length > 0) || !self->m_xmlData) return false;
    return XString_assign_with_length_utf8(self->m_xmlData,
        data ? (const char*)data : "", length);
}
/**
 * @brief     保存 XML 到文件
 * @param self     XSimpleOOXmlFile 指针
 * @param filePath 文件路径
 * @return    成功返回true
 */
bool XSimpleOOXmlFile_saveToXmlFile(XSimpleOOXmlFile* self, const XString* filePath) {
    if (!self || !filePath) return false;
    uint8_t* data = NULL;
    size_t length = 0;
    if (!XSimpleOOXmlFile_saveToXmlData(self, &data, &length)) return false;
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_WriteOnly | XIODevice_Truncate)) {
        if (file) XClass_delete_base((XClass*)file);
        XFree_System(data);
        return false;
    }
    bool result = length == 0 || XIODevice_write_1((XIODevice*)file,
        (const char*)data, (int64_t)length) == (int64_t)length;
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    XFree_System(data);
    return result;
}

/**
 * @brief     从文件加载 XML
 * @param self     XSimpleOOXmlFile 指针
 * @param filePath 文件路径
 * @return    成功返回true
 */
bool XSimpleOOXmlFile_loadFromXmlFile(XSimpleOOXmlFile* self, const XString* filePath) {
    if (!self || !filePath) return false;
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        return false;
    }
    XByteArray* allData = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    bool result = allData && XSimpleOOXmlFile_loadFromXmlData(self,
        XByteArray_data(allData), XByteArray_size_base((XContainer*)allData));
    if (allData) XByteArray_delete_base(allData);
    return result;
}
