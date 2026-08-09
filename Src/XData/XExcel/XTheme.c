#include "XTheme.h"
#include "XMemory.h"
#include "XFile.h"
#include "XByteArray.h"
#include <string.h>

XTheme* XTheme_create(XAbstractOOXmlFile_CreateFlag flag) {
    XTheme* self = (XTheme*)XMalloc_System(sizeof(XTheme));
    if (!self) return NULL; memset(self, 0, sizeof(XTheme));
    XAbstractOOXmlFile_init(&self->m_base, flag);
    return self;
}
void XTheme_delete(XTheme* self) {
    if (!self) return;
    if (self->m_themeName) XString_delete_base(self->m_themeName);
    if (self->m_themeXmlData) XString_delete_base(self->m_themeXmlData);
    XAbstractOOXmlFile_deinit(&self->m_base); XFree_System(self);
}
void XTheme_setThemeName(XTheme* self, const XString* name) {
    if (!self) return;
    if (!self->m_themeName) self->m_themeName = XString_create();
    if (self->m_themeName) { XString_clear_base(self->m_themeName); if (name) XString_append(self->m_themeName, name); }
}
const XString* XTheme_themeName(const XTheme* self) { return (self && self->m_themeName) ? self->m_themeName : NULL; }
void XTheme_setThemeXmlData(XTheme* self, const XString* data) {
    if (!self) return;
    if (!self->m_themeXmlData) self->m_themeXmlData = XString_create();
    if (self->m_themeXmlData) { XString_clear_base(self->m_themeXmlData); if (data) XString_append(self->m_themeXmlData, data); }
}
const XString* XTheme_themeXmlData(const XTheme* self) {
    if (!self || !self->m_themeXmlData) return NULL;
    return self->m_themeXmlData;
}

bool XTheme_saveToXmlData(const XTheme* self, uint8_t** data, size_t* length) {
    if (!self || !data || !length) return false;
    *data = NULL;
    *length = 0;
    const char* utf8 = self->m_themeXmlData ? XString_toUtf8(self->m_themeXmlData) : "";
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

bool XTheme_loadFromXmlData(XTheme* self, const uint8_t* data, size_t length) {
    if (!self || (!data && length > 0)) return false;
    if (!self->m_themeXmlData) self->m_themeXmlData = XString_create();
    return self->m_themeXmlData && XString_assign_with_length_utf8(
        self->m_themeXmlData, data ? (const char*)data : "", length);
}
/**
 * @brief     保存主题 XML 到文件
 * @param self     XTheme 指针
 * @param filePath 文件路径
 * @return    成功返回true
 */
bool XTheme_saveToXmlFile(XTheme* self, const XString* filePath) {
    if (!self || !filePath) return false;
    uint8_t* data = NULL;
    size_t length = 0;
    if (!XTheme_saveToXmlData(self, &data, &length)) return false;
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
 * @brief     从文件加载主题 XML
 * @param self     XTheme 指针
 * @param filePath 文件路径
 * @return    成功返回true
 */
bool XTheme_loadFromXmlFile(XTheme* self, const XString* filePath) {
    if (!self || !filePath) return false;
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly)) {
        if (file) XClass_delete_base((XClass*)file);
        return false;
    }
    XByteArray* allData = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClass_delete_base((XClass*)file);
    bool result = allData && XTheme_loadFromXmlData(self, XByteArray_data(allData),
        XByteArray_size_base((XContainer*)allData));
    if (allData) XByteArray_delete_base(allData);
    return result;
}
