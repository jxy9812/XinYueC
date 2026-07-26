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

/** @brief 应用属性中的标题对（名称及对应部件数量） */
typedef struct XDocPropsAppHeadingPair {
    XString* m_name; /**< 标题对名称，内部所有 */
    int m_value;     /**< 对应数量 */
} XDocPropsAppHeadingPair;
/** @brief XDocPropsApp 应用程序属性结构体 */
typedef struct XDocPropsApp {
    XAbstractOOXmlFile m_base;         /**< 基类 */
    XStringList* m_titlesOfPartsList;  /**< 部分标题列表 */
    XVector* m_headingPairsList;       /**< 标题对列表 */
    XMap* m_properties;               /**< 属性映射 */
} XDocPropsApp;

/**
 * @brief      创建应用程序属性对象
 * @param flag 创建标志
 * @return     新对象指针
 */
XDocPropsApp* XDocPropsApp_create(XAbstractOOXmlFile_CreateFlag flag);

/**
 * @brief      销毁应用程序属性对象
 * @param self 应用程序属性对象指针
 */
void XDocPropsApp_delete(XDocPropsApp* self);

/**
 * @brief      添加部分标题
 * @param self  应用程序属性对象指针
 * @param title 标题
 */
void XDocPropsApp_addPartTitle(XDocPropsApp* self, const XString* title);

/**
 * @brief      添加标题对
 * @param self  应用程序属性对象指针
 * @param name  名称
 * @param value 值
 */
void XDocPropsApp_addHeadingPair(XDocPropsApp* self, const XString* name, int value);

/**
 * @brief      设置属性
 * @param self  应用程序属性对象指针
 * @param name  属性名称
 * @param value 属性值
 * @return      成功返回 true
 */
bool XDocPropsApp_setProperty(XDocPropsApp* self, const XString* name, const XString* value);

/**
 * @brief      获取属性值
 * @param self 应用程序属性对象指针
 * @param name 属性名称
 * @return     属性值，不存在返回 NULL
 */
const XString* XDocPropsApp_property(const XDocPropsApp* self, const XString* name);

/**
 * @brief      获取所有属性名称
 * @param self  应用程序属性对象指针
 * @param names [out] 输出名称数组
 * @return      属性数量
 */
int XDocPropsApp_propertyNames(const XDocPropsApp* self, XString*** names);

/**
 * @brief      清空标题对与部件标题列表
 * @param self 应用程序属性对象指针
 */
void XDocPropsApp_clearParts(XDocPropsApp* self);

/**
 * @brief  将应用程序属性序列化为 XML 数据
 * @param  self    应用程序属性对象指针
 * @param  outData [out] XML 数据，调用者使用 XFree_System 释放
 * @param  outLen  [out] XML 数据长度
 * @return 成功返回 true
 */
bool XDocPropsApp_saveToXmlData(const XDocPropsApp* self, uint8_t** outData, size_t* outLen);

/**
 * @brief  从 XML 数据加载应用程序属性
 * @param  self 应用程序属性对象指针
 * @param  data XML 数据
 * @param  len  XML 数据长度
 * @return 成功返回 true
 */
bool XDocPropsApp_loadFromXmlData(XDocPropsApp* self, const uint8_t* data, size_t len);

/**
 * @brief      保存到 XML 文件
 * @param self     应用程序属性对象指针
 * @param filePath 文件路径
 * @return          成功返回 true
 */
bool XDocPropsApp_saveToXmlFile(XDocPropsApp* self, const XString* filePath);

/**
 * @brief      从 XML 文件加载
 * @param self     应用程序属性对象指针
 * @param filePath 文件路径
 * @return          成功返回 true
 */
bool XDocPropsApp_loadFromXmlFile(XDocPropsApp* self, const XString* filePath);
#ifdef __cplusplus
}
#endif
#endif
