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
    XByteArray* m_xmlData;
} XSimpleOOXmlFile;
XSimpleOOXmlFile* XSimpleOOXmlFile_create(XAbstractOOXmlFile_CreateFlag flag);
void XSimpleOOXmlFile_delete(XSimpleOOXmlFile* self);
void XSimpleOOXmlFile_setXmlData(XSimpleOOXmlFile* self, const char* data, size_t len);
const char* XSimpleOOXmlFile_xmlData(const XSimpleOOXmlFile* self, size_t* len);
bool XSimpleOOXmlFile_saveToXmlFile(XSimpleOOXmlFile* self, const char* filePath);
bool XSimpleOOXmlFile_loadFromXmlFile(XSimpleOOXmlFile* self, const char* filePath);
#ifdef __cplusplus
}
#endif
#endif
