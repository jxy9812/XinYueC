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
    if (self->m_themeName) { XString_deinit_base(self->m_themeName); XFree_System(self->m_themeName); }
    if (self->m_themeXmlData) { XString_deinit_base(self->m_themeXmlData); XFree_System(self->m_themeXmlData); }
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
/**
 * @brief     保存主题 XML 到文件
 * @param self     XTheme 指针
 * @param filePath 文件路径
 * @return    成功返回true
 */
bool XTheme_saveToXmlFile(XTheme* self, const XString* filePath) {
    if (!self || !filePath) return false;
    XFile* file = XFile_create_2((XString*)filePath);
    if (!file || !XIODevice_open_base((XIODevice*)file, XIODevice_WriteOnly | XIODevice_Truncate)) {
        if (file) XFile_deleteLater(file);
        return false;
    }
    if (self->m_themeXmlData && XString_size_base(self->m_themeXmlData) > 0) {
        const char* data = XString_toUtf8(self->m_themeXmlData);
        XIODevice_write_1((XIODevice*)file, data, (int64_t)XString_size_base(self->m_themeXmlData));
    }
    XIODevice_close_base((XIODevice*)file);
    XFile_deleteLater(file);
    return true;
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
    if (!self->m_themeXmlData) self->m_themeXmlData = XString_create();
    if (self->m_themeXmlData) { XString_clear_base(self->m_themeXmlData); XString_append_utf8(self->m_themeXmlData, (const char*)XByteArray_data(allData)); }
    XByteArray_delete_base(allData);
    return true;
}
