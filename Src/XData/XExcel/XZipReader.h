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
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "XString.h"
#include "XStringList.h"
#include "XByteArray.h"
typedef struct XZipReader {
    void* m_zipHandle;
    XStringList* m_filePaths;
    XString* m_fileName;
} XZipReader;
/**
 * @brief  通过文件路径创建 ZIP 读取器
 * @param  fileName  ZIP 文件路径
 * @return 成功返回 XZipReader 指针，失败返回 NULL
 */
XZipReader* XZipReader_create(const XString* fileName);

/**
 * @brief  从内存数据创建 ZIP 读取器
 * @param  data  ZIP 数据缓冲区
 * @param  size  数据长度（字节）
 * @return 成功返回 XZipReader 指针，失败返回 NULL
 */
XZipReader* XZipReader_createFromData(const uint8_t* data, size_t size);

/**
 * @brief  销毁 ZIP 读取器并释放资源
 * @param  self  XZipReader 指针
 */
void XZipReader_delete(XZipReader* self);

/**
 * @brief  判断 ZIP 文件是否存在且有效
 * @param  self  XZipReader 指针
 * @return 存在返回 true
 */
bool XZipReader_exists(const XZipReader* self);

/**
 * @brief  获取 ZIP 内所有文件路径列表
 * @param  self  XZipReader 指针
 * @return 文件路径列表（XStringList*），调用者负责释放
 */
XStringList* XZipReader_filePaths(const XZipReader* self);

/**
 * @brief  读取 ZIP 内指定文件的数据
 * @param  self      XZipReader 指针
 * @param  fileName  ZIP 内文件路径
 * @return 文件数据（XByteArray*），调用者负责释放；文件不存在返回 NULL
 */
XByteArray* XZipReader_fileData(const XZipReader* self, const XString* fileName);
#ifdef __cplusplus
}
#endif
#endif
