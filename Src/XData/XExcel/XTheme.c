#include "XTheme.h"
#include "XMemory.h"
#include <stdlib.h>
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
    if (self->m_themeXmlData) { XByteArray_deinit_base(self->m_themeXmlData); XFree_System(self->m_themeXmlData); }
    XAbstractOOXmlFile_deinit(&self->m_base); XFree_System(self);
}
void XTheme_setThemeName(XTheme* self, const char* name) {
    if (!self) return;
    if (!self->m_themeName) self->m_themeName = XString_create();
    if (self->m_themeName) { XString_clear_base(self->m_themeName); XString_append_utf8(self->m_themeName, name); }
}
const char* XTheme_themeName(const XTheme* self) { return (self && self->m_themeName) ? XString_toUtf8(self->m_themeName) : ""; }
void XTheme_setThemeXmlData(XTheme* self, const char* data, size_t len) {
    if (!self) return;
    if (!self->m_themeXmlData) self->m_themeXmlData = XByteArray_create();
    if (self->m_themeXmlData) { XByteArray_clear_base(self->m_themeXmlData); XByteArray_append_2(self->m_themeXmlData, (const uint8_t*)data, len); }
}
const char* XTheme_themeXmlData(const XTheme* self, size_t* len) {
    if (!self || !self->m_themeXmlData) { if (len) *len = 0; return NULL; }
    if (len) *len = XByteArray_size_base(self->m_themeXmlData);
    return (const char*)XByteArray_data(self->m_themeXmlData);
}
/**
 * @brief     保存主题 XML 到文件
 * @param self     XTheme 指针
 * @param filePath 文件路径
 * @return    成功返回true
 */
bool XTheme_saveToXmlFile(XTheme* self, const char* filePath) {
    if (!self || !filePath) return false;
    FILE* fp = fopen(filePath, "wb");
    if (!fp) return false;
    if (self->m_themeXmlData && XByteArray_size_base(self->m_themeXmlData) > 0) {
        size_t len = XByteArray_size_base(self->m_themeXmlData);
        const uint8_t* data = XByteArray_data(self->m_themeXmlData);
        fwrite(data, 1, len, fp);
    }
    fclose(fp);
    return true;
}

/**
 * @brief     从文件加载主题 XML
 * @param self     XTheme 指针
 * @param filePath 文件路径
 * @return    成功返回true
 */
bool XTheme_loadFromXmlFile(XTheme* self, const char* filePath) {
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
    XTheme_setThemeXmlData(self, (const char*)data, (size_t)size);
    XFree_System(data);
    return true;
}
