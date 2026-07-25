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
    if (self->m_xmlData) { XString_deinit_base(self->m_xmlData); XFree_System(self->m_xmlData); }
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
/**
 * @brief     保存 XML 到文件
 * @param self     XSimpleOOXmlFile 指针
 * @param filePath 文件路径
 * @return    成功返回true
 */
bool XSimpleOOXmlFile_saveToXmlFile(XSimpleOOXmlFile* self, const XString* filePath) {
    if (!self || !filePath) return false;
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_WriteOnly | XIODevice_Truncate)) {
        if (file) XFile_deleteLater(file);
        return false;
    }
    if (self->m_xmlData && XString_size_base(self->m_xmlData) > 0) {
        const char* data = XString_toUtf8(self->m_xmlData);
        XIODevice_write_1((XIODevice*)file, data, (int64_t)XString_size_base(self->m_xmlData));
    }
    XIODevice_close_base((XIODevice*)file);
    XFile_deleteLater(file);
    return true;
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
        if (file) XFile_deleteLater(file);
        return false;
    }
    XByteArray* allData = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XFile_deleteLater(file);
    if (!allData || XByteArray_size_base(allData) == 0) {
        if (allData) XByteArray_delete_base(allData);
        return false;
    }
    XString_clear_base(self->m_xmlData);
    XString_append_utf8(self->m_xmlData, (const char*)XByteArray_data(allData));
    XByteArray_delete_base(allData);
    return true;
}
