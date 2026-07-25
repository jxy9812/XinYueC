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
    XString* m_themeXmlData;
} XTheme;
/**
 * @brief  创建主题对象
 * @param  flag  创建标志
 * @return 成功返回 XTheme 指针，失败返回 NULL
 */
XTheme* XTheme_create(XAbstractOOXmlFile_CreateFlag flag);

/**
 * @brief  销毁主题对象并释放资源
 * @param  self  XTheme 指针
 */
void XTheme_delete(XTheme* self);

/**
 * @brief  设置主题名称
 * @param  self  XTheme 指针
 * @param  name  主题名称
 */
void XTheme_setThemeName(XTheme* self, const XString* name);

/**
 * @brief  获取主题名称
 * @param  self  XTheme 指针
 * @return 主题名称字符串，未设置返回 NULL
 */
const XString* XTheme_themeName(const XTheme* self);

/**
 * @brief  设置主题 XML 数据
 * @param  self  XTheme 指针
 * @param  data  XML 数据字符串
 */
void XTheme_setThemeXmlData(XTheme* self, const XString* data);

/**
 * @brief  获取主题 XML 数据
 * @param  self  XTheme 指针
 * @return XML 数据字符串，未设置返回 NULL
 */
const XString* XTheme_themeXmlData(const XTheme* self);

/**
 * @brief  将主题保存为 XML 文件
 * @param  self      XTheme 指针
 * @param  filePath  输出文件路径
 * @return 成功返回 true
 */
bool XTheme_saveToXmlFile(XTheme* self, const XString* filePath);

/**
 * @brief  从 XML 文件加载主题
 * @param  self      XTheme 指针
 * @param  filePath  输入文件路径
 * @return 成功返回 true
 */
bool XTheme_loadFromXmlFile(XTheme* self, const XString* filePath);
#ifdef __cplusplus
}
#endif
#endif
