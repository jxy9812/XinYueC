/******************************************************************************
 * @file       XZipReader.h
 * @brief      XZipReader ZIP 读取器（对标 QXlsx::ZipReader）
 * @author     XinYueC 团队
 * @note       提供 ZIP 文件的读取功能，支持通过文件名或设备打开。
 *             对齐 QXlsx::ZipReader 全部功能
 ******************************************************************************/
#ifndef XZIPREADER_H
#define XZIPREADER_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>#include <stdbool.h>#include <stddef.h>
#include "XString.h"
#include "XStringList.h"
#include "XByteArray.h"
typedef struct XZipReader {
    void* m_zipHandle;
    XStringList* m_filePaths;
    XString* m_fileName;
} XZipReader;
XZipReader* XZipReader_create(const char* fileName);
XZipReader* XZipReader_createFromData(const uint8_t* data, size_t size);
void XZipReader_delete(XZipReader* self);
bool XZipReader_exists(const XZipReader* self);
XStringList* XZipReader_filePaths(const XZipReader* self);
XByteArray* XZipReader_fileData(const XZipReader* self, const char* fileName);
#ifdef __cplusplus
}
#endif
#endif
