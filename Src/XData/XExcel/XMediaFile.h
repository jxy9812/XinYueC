/******************************************************************************
 * @file       XMediaFile.h
 * @brief      XMediaFile 媒体文件类（对标 QXlsx::MediaFile）
 * @author     XinYueC 团队
 * @note       提供嵌入 Excel 的媒体文件（图片等）的表示，包含文件名、内容和 MIME 类型。
 *             对齐 QXlsx::MediaFile 全部功能
 ******************************************************************************/
#ifndef XMEDIAFILE_H
#define XMEDIAFILE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include <stdbool.h>

#include <stddef.h>

#include "XString.h"
#include "XByteArray.h"

/**
 * @brief      XMediaFile 媒体文件结构体
 * @note       包含文件名、二进制内容、后缀和 MIME 类型。对齐 QXlsx::MediaFile 全部功能。
 */
typedef struct XMediaFile
{
    XString* m_fileName;     /**< 文件名 */
    XByteArray* m_contents;  /**< 文件二进制内容 */
    XString* m_suffix;       /**< 文件后缀 */
    XString* m_mimeType;     /**< MIME 类型 */
    int m_index;             /**< 索引（用于排序） */
    bool m_indexValid;       /**< 索引是否有效 */
} XMediaFile;

/**
 * @brief      从文件名创建 XMediaFile 对象
 * @param fileName 文件名
 * @return     指向新创建的 XMediaFile 的指针，失败返回 NULL
 */
XMediaFile* XMediaFile_create(const XString* fileName);

/**
 * @brief      从二进制数据创建 XMediaFile 对象
 * @param bytes    二进制数据
 * @param dataSize 数据大小
 * @param suffix   文件后缀
 * @param mimeType MIME 类型
 * @return     指向新创建的 XMediaFile 的指针
 */
XMediaFile* XMediaFile_create_data(const uint8_t* bytes, size_t dataSize, const XString* suffix, const XString* mimeType);

/**
 * @brief      在堆上删除 XMediaFile 实例
 * @param self 待删除的指针
 */
void XMediaFile_delete(XMediaFile* self);

/**
 * @brief      设置文件内容
 * @param self     指针
 * @param bytes    数据
 * @param dataSize 数据大小
 * @param suffix   后缀
 * @param mimeType MIME 类型
 */
void XMediaFile_set(XMediaFile* self, const uint8_t* bytes, size_t dataSize, const XString* suffix, const XString* mimeType);

/**
 * @brief      获取文件后缀
 * @param self 指针
 * @return     文件后缀
 */
const XString* XMediaFile_suffix(const XMediaFile* self);

/**
 * @brief      获取 MIME 类型
 * @param self 指针
 * @return     MIME 类型
 */
const XString* XMediaFile_mimeType(const XMediaFile* self);

/**
 * @brief      获取文件内容
 * @param self 指针
 * @return     内容数据指针
 */
const uint8_t* XMediaFile_contents(const XMediaFile* self);

/**
 * @brief      获取内容大小
 * @param self 指针
 * @return     内容大小
 */
size_t XMediaFile_contentsSize(const XMediaFile* self);

/**
 * @brief      判断索引是否有效
 * @param self 指针
 * @return     有效返回 true
 */
bool XMediaFile_isIndexValid(const XMediaFile* self);

/**
 * @brief      获取索引
 * @param self 指针
 * @return     索引
 */
int XMediaFile_index(const XMediaFile* self);

/**
 * @brief      设置索引
 * @param self 指针
 * @param idx  索引
 */
void XMediaFile_setIndex(XMediaFile* self, int idx);

/**
 * @brief      设置文件名
 * @param self 指针
 * @param name 文件名
 */
void XMediaFile_setFileName(XMediaFile* self, const XString* name);

/**
 * @brief      获取文件名
 * @param self 指针
 * @return     文件名
 */
const XString* XMediaFile_fileName(const XMediaFile* self);

/**
 * @brief      获取内容的哈希键（用于去重）
 * @param self    指针
 * @param outKey  输出哈希键数据
 * @param outLen  输出哈希键长度
 */
void XMediaFile_hashKey(const XMediaFile* self, uint8_t** outKey, size_t* outLen);

#ifdef __cplusplus
}
#endif
#endif /* XMEDIAFILE_H */
