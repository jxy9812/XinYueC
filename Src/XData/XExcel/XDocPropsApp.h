/******************************************************************************
 * @file       XDocPropsApp.h
 * @brief      XDocPropsApp 应用程序属性类（对标 QXlsx::DocPropsApp）
 * @author     XinYueC 团队
 * @note       提供应用程序级别的文档属性，如标题、公司、管理器等。
 *             对齐 QXlsx::DocPropsApp 全部功能
 ******************************************************************************/
#ifndef XDOCPROPSAPP_H
#define XDOCPROPSAPP_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "XString.h"
#include "XVector.h"
#include "XMap.h"
#include "XAbstractOOXmlFile.h"
typedef struct XDocPropsApp {
    XAbstractOOXmlFile m_base;
    XStringList* m_titlesOfPartsList;
    XVector* m_headingPairsList;
    XMap* m_properties;
} XDocPropsApp;
XDocPropsApp* XDocPropsApp_create(XAbstractOOXmlFile_CreateFlag flag);
void XDocPropsApp_delete(XDocPropsApp* self);
void XDocPropsApp_addPartTitle(XDocPropsApp* self, const char* title);
void XDocPropsApp_addHeadingPair(XDocPropsApp* self, const char* name, int value);
bool XDocPropsApp_setProperty(XDocPropsApp* self, const char* name, const char* value);
const char* XDocPropsApp_property(const XDocPropsApp* self, const char* name);
int XDocPropsApp_propertyNames(const XDocPropsApp* self, XString*** names);
bool XDocPropsApp_saveToXmlFile(XDocPropsApp* self, const char* filePath);
bool XDocPropsApp_loadFromXmlFile(XDocPropsApp* self, const char* filePath);
#ifdef __cplusplus
}
#endif
#endif
