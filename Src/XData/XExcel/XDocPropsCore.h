/******************************************************************************
 * @file       XDocPropsCore.h
 * @brief      XDocPropsCore 核心属性类（对标 QXlsx::DocPropsCore）
 * @author     XinYueC 团队
 * @note       提供核心文档属性，如标题、作者、创建日期等。
 *             对齐 QXlsx::DocPropsCore 全部功能
 ******************************************************************************/
#ifndef XDOCPROPSCORE_H
#define XDOCPROPSCORE_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>#include <stdbool.h>#include <stddef.h>
#include "XString.h"
#include "XMap.h"
#include "XAbstractOOXmlFile.h"
typedef struct XDocPropsCore {
    XAbstractOOXmlFile m_base;
    XMap* m_properties;
} XDocPropsCore;
XDocPropsCore* XDocPropsCore_create(XAbstractOOXmlFile_CreateFlag flag);
void XDocPropsCore_delete(XDocPropsCore* self);
bool XDocPropsCore_setProperty(XDocPropsCore* self, const char* name, const char* value);
const char* XDocPropsCore_property(const XDocPropsCore* self, const char* name);
int XDocPropsCore_propertyNames(const XDocPropsCore* self, XString*** names);
bool XDocPropsCore_saveToXmlFile(XDocPropsCore* self, const char* filePath);
bool XDocPropsCore_loadFromXmlFile(XDocPropsCore* self, const char* filePath);
#ifdef __cplusplus
}
#endif
#endif
