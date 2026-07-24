/******************************************************************************
 * @file       XZipWriter.h
 * @brief      XZipWriter ZIP 写入器（对标 QXlsx::ZipWriter）
 * @author     XinYueC 团队
 * @note       提供 ZIP 文件的写入功能，支持添加文件和目录。
 *             对齐 QXlsx::ZipWriter 全部功能
 ******************************************************************************/
#ifndef XZIPWRITER_H
#define XZIPWRITER_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "XString.h"
#include "XByteArray.h"
typedef struct XZipWriter {
    void* m_zipHandle;
    XString* m_fileName;
    void* m_entries;  /* XVector<ZipFileEntry>
 */
} XZipWriter;
XZipWriter* XZipWriter_create(const char* fileName);
void XZipWriter_delete(XZipWriter* self);
bool XZipWriter_addFile(XZipWriter* self, const char* path, const uint8_t* data, size_t size);
bool XZipWriter_addDirectory(XZipWriter* self, const char* path);
bool XZipWriter_close(XZipWriter* self);
#ifdef __cplusplus
}
#endif
#endif
