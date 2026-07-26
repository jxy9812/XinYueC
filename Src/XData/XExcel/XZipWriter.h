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
typedef struct XIODevice XIODevice;
/** @brief XZipWriter ZIP 写入器结构体 */
typedef struct XZipWriter {
    void* m_zipHandle;       /**< ZIP 句柄 */
    XString* m_fileName;     /**< 文件名 */
    void* m_entries;        /**< 条目列表 */
    bool m_closeAttempted;  /**< 是否已经执行过关闭 */
    bool m_closed;          /**< ZIP 中央目录是否成功写出 */
} XZipWriter;
/**
 * @brief  创建 ZIP 写入器并打开目标文件
 * @param  fileName  输出 ZIP 文件路径
 * @return 成功返回 XZipWriter 指针，失败返回 NULL
 */
XZipWriter* XZipWriter_create(const XString* fileName);

/**
 * @brief  使用调用方提供的设备创建 ZIP 写入器
 * @param  device 已打开为可写，或可由写入器打开的设备；所有权不转移
 * @return 成功返回 XZipWriter 指针，失败返回 NULL
 */
XZipWriter* XZipWriter_createForDevice(XIODevice* device);

/**
 * @brief  销毁 ZIP 写入器并释放资源（未关闭时自动关闭）
 * @param  self  XZipWriter 指针
 */
void XZipWriter_delete(XZipWriter* self);

/**
 * @brief  向 ZIP 中添加一个文件
 * @param  self  XZipWriter 指针
 * @param  path  ZIP 内文件路径（如 "xl/workbook.xml"）
 * @param  data  文件数据
 * @param  size  数据长度（字节）
 * @return 成功返回 true
 */
bool XZipWriter_addFile(XZipWriter* self, const XString* path, const uint8_t* data, size_t size);

/**
 * @brief  向 ZIP 中添加一个目录条目
 * @param  self  XZipWriter 指针
 * @param  path  目录路径
 * @return 成功返回 true
 */
bool XZipWriter_addDirectory(XZipWriter* self, const XString* path);

/**
 * @brief  关闭 ZIP 文件（写入中央目录并刷新）
 * @param  self  XZipWriter 指针
 * @return 成功返回 true
 */
bool XZipWriter_close(XZipWriter* self);
#ifdef __cplusplus
}
#endif
#endif
