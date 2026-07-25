/******************************************************************************
 * @file       XSimpleOOXmlFile.h
 * @brief      XSimpleOOXmlFile 简单 OOXML 文件类（对标 QXlsx::SimpleOOXmlFile）
 * @author     XinYueC 团队
 * @note       提供简单的 OOXML XML 数据透传文件类。
 *             对齐 QXlsx::SimpleOOXmlFile 全部功能
 ******************************************************************************/
#ifndef XSIMPLEOOXMLFILE_H
#define XSIMPLEOOXMLFILE_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "XString.h"
#include "XByteArray.h"
#include "XAbstractOOXmlFile.h"
typedef struct XSimpleOOXmlFile {
    XAbstractOOXmlFile m_base;
    XString* m_xmlData;
} XSimpleOOXmlFile;
/**
 * @brief  创建简单 OOXML 文件对象
 * @param  flag  创建标志（F_NewFromScratch / F_LoadFromExists）
 * @return 新对象指针，失败返回 NULL
 */
XSimpleOOXmlFile* XSimpleOOXmlFile_create(XAbstractOOXmlFile_CreateFlag flag);

/**
 * @brief  销毁简单 OOXML 文件对象并释放资源
 * @param  self  对象指针
 */
void XSimpleOOXmlFile_delete(XSimpleOOXmlFile* self);

/**
 * @brief  设置 XML 数据内容
 * @param  self  对象指针
 * @param  data  XML 数据字符串
 */
void XSimpleOOXmlFile_setXmlData(XSimpleOOXmlFile* self, const XString* data);

/**
 * @brief  获取 XML 数据内容
 * @param  self  对象指针
 * @return XML 数据字符串，无数据返回 NULL
 */
const XString* XSimpleOOXmlFile_xmlData(const XSimpleOOXmlFile* self);

/**
 * @brief  将 XML 数据保存到文件
 * @param  self      对象指针
 * @param  filePath  输出文件路径
 * @return 成功返回 true
 */
bool XSimpleOOXmlFile_saveToXmlFile(XSimpleOOXmlFile* self, const XString* filePath);

/**
 * @brief  从文件加载 XML 数据
 * @param  self      对象指针
 * @param  filePath  输入文件路径
 * @return 成功返回 true
 */
bool XSimpleOOXmlFile_loadFromXmlFile(XSimpleOOXmlFile* self, const XString* filePath);
#ifdef __cplusplus
}
#endif
#endif
