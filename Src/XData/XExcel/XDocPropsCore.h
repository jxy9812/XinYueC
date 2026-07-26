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
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "XString.h"
#include "XMap.h"
#include "XAbstractOOXmlFile.h"
/** @brief XDocPropsCore 核心属性结构体 */
typedef struct XDocPropsCore {
    XAbstractOOXmlFile m_base;     /**< 基类 */
    XMap* m_properties;           /**< 属性映射 */
} XDocPropsCore;
/**
 * @brief  创建核心属性对象
 * @param  flag  创建标志（F_NewFromScratch / F_LoadFromExists）
 * @return 新对象指针，失败返回 NULL
 */
XDocPropsCore* XDocPropsCore_create(XAbstractOOXmlFile_CreateFlag flag);

/**
 * @brief  销毁核心属性对象并释放资源
 * @param  self  核心属性对象指针
 */
void XDocPropsCore_delete(XDocPropsCore* self);

/**
 * @brief  设置核心属性
 * @param  self   核心属性对象指针
 * @param  name   属性名称（如 "title"、"creator"、"created"）
 * @param  value  属性值
 * @return 成功返回 true
 */
bool XDocPropsCore_setProperty(XDocPropsCore* self, const XString* name, const XString* value);

/**
 * @brief  获取核心属性值
 * @param  self  核心属性对象指针
 * @param  name  属性名称
 * @return 属性值字符串，不存在返回 NULL
 */
const XString* XDocPropsCore_property(const XDocPropsCore* self, const XString* name);

/**
 * @brief  获取所有属性名称列表
 * @param  self   核心属性对象指针
 * @param  names  [out] 接收属性名称数组的指针
 * @return 属性数量
 */
int XDocPropsCore_propertyNames(const XDocPropsCore* self, XString*** names);

/**
 * @brief  将核心属性序列化为 XML 数据
 * @param  self    核心属性对象指针
 * @param  outData [out] XML 数据，调用者使用 XFree_System 释放
 * @param  outLen  [out] XML 数据长度
 * @return 成功返回 true
 */
bool XDocPropsCore_saveToXmlData(const XDocPropsCore* self, uint8_t** outData, size_t* outLen);

/**
 * @brief  从 XML 数据加载核心属性
 * @param  self 核心属性对象指针
 * @param  data XML 数据
 * @param  len  XML 数据长度
 * @return 成功返回 true
 */
bool XDocPropsCore_loadFromXmlData(XDocPropsCore* self, const uint8_t* data, size_t len);

/**
 * @brief  将核心属性保存为 XML 文件（docProps/core.xml）
 * @param  self      核心属性对象指针
 * @param  filePath  输出文件路径
 * @return 成功返回 true
 */
bool XDocPropsCore_saveToXmlFile(XDocPropsCore* self, const XString* filePath);

/**
 * @brief  从 XML 文件加载核心属性
 * @param  self      核心属性对象指针
 * @param  filePath  输入文件路径
 * @return 成功返回 true
 */
bool XDocPropsCore_loadFromXmlFile(XDocPropsCore* self, const XString* filePath);
#ifdef __cplusplus
}
#endif
#endif
