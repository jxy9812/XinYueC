/******************************************************************************
 * @file       XTheme.h
 * @brief      XTheme 主题类（对标 QXlsx::Theme）
 * @author     XinYueC 团队
 * @note       提供主题管理，包括主题名称、主题XML数据等。
 *             对齐 QXlsx::Theme 全部功能
 ******************************************************************************/
#ifndef XTHEME_H
#define XTHEME_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "XString.h"
#include "XByteArray.h"
#include "XAbstractOOXmlFile.h"
typedef struct XTheme {
    XAbstractOOXmlFile m_base;
    XString* m_themeName;
    XByteArray* m_themeXmlData;
} XTheme;
XTheme* XTheme_create(XAbstractOOXmlFile_CreateFlag flag);
void XTheme_delete(XTheme* self);
void XTheme_setThemeName(XTheme* self, const char* name);
const char* XTheme_themeName(const XTheme* self);
void XTheme_setThemeXmlData(XTheme* self, const char* data, size_t len);
const char* XTheme_themeXmlData(const XTheme* self, size_t* len);
bool XTheme_saveToXmlFile(XTheme* self, const char* filePath);
bool XTheme_loadFromXmlFile(XTheme* self, const char* filePath);
#ifdef __cplusplus
}
#endif
#endif
