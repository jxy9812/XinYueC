#ifndef XFILE_H
#define XFILE_H

/**
 * @file XFile.h
 * @brief 文件类，提供文件读写操作
 * 
 * 移植自 Qt 6.8 QFile 类，继承自 XFileDevice。
 * 提供文件的打开、关闭、读写以及文件系统操作。
 */

#include <stdint.h>
#include <stdbool.h>
#include "XFileDevice.h"
#include "XString.h"
#include "XByteArray.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 虚函数表定义
 * ============================================================================ */

#define XFILE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XFile))

XCLASS_DEFINE_BEGING(XFile)
// XFile 没有新增虚函数，重写的虚函数在 .c 中实现
XCLASS_DEFINE_EXTEND_END(XFile, XFileDevice)

/* ============================================================================
 * 结构体定义
 * ============================================================================ */

/**
 * @brief XFile 结构体，文件类
 */
typedef struct XFile {
    XFileDevice m_parent;           /**< 基类 XFileDevice */
    /* XFile 特有成员 */
    XString* m_fileName;            /**< 文件名 */
} XFile;

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

/**
 * @brief 初始化 XFile 类的虚函数表
 * @return 虚函数表指针
 */
XVtable* XFile_class_init(void);

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

/**
 * @brief 创建 XFile 对象
 * @return XFile 对象指针，失败返回 NULL
 */
XFile* XFile_create(void);

/**
 * @brief 创建 XFile 对象并设置文件名
 * @param name 文件名
 * @return XFile 对象指针，失败返回 NULL
 */
XFile* XFile_create_2(const XString* name);

/**
 * @brief 初始化 XFile 对象
 * @param file XFile 对象指针
 */
void XFile_init(XFile* file);

/**
 * @brief 初始化 XFile 对象并设置文件名
 * @param file XFile 对象指针
 * @param name 文件名
 */
void XFile_init_2(XFile* file, const XString* name);

/**
 * @brief 析构 XFile 对象（供子类调用）
 * @param file XFile 对象指针
 */
//void XFile_deinit_base(XFile* file);

/* ============================================================================
 * 析构函数（继承自 XObject）
 * ============================================================================ */

#define XFile_deinitLater           XFileDevice_deinitLater
#define XFile_deleteLater           XFileDevice_deleteLater

/* ============================================================================
 * 继承自 XIODevice 的虚函数（使用宏映射）
 * ============================================================================ */

#define XFile_open_base             XIODevice_open_base
#define XFile_close_base            XIODevice_close_base
#define XFile_isSequential_base     XIODevice_isSequential_base
#define XFile_pos_base              XIODevice_pos_base
#define XFile_seek_base             XIODevice_seek_base
#define XFile_atEnd_base            XIODevice_atEnd_base
#define XFile_reset_base            XIODevice_reset_base
#define XFile_size_base             XIODevice_size_base
#define XFile_bytesAvailable_base   XIODevice_bytesAvailable_base
#define XFile_bytesToWrite_base     XIODevice_bytesToWrite_base
#define XFile_readData_base         XIODevice_readData_base
#define XFile_writeData_base        XIODevice_writeData_base
#define XFile_readLineData_base     XIODevice_readLineData_base
#define XFile_skipData_base         XIODevice_skipData_base

/* ============================================================================
 * 继承自 XFileDevice 的虚函数（使用宏映射）
 * ============================================================================ */

#define XFile_fileName_base         XFileDevice_fileName_base
#define XFile_resize_base           XFileDevice_resize_base
#define XFile_permissions_base      XFileDevice_permissions_base
#define XFile_setPermissions_base   XFileDevice_setPermissions_base

/* ============================================================================
 * 继承自 XFileDevice 的非虚函数（使用宏映射）
 * ============================================================================ */

#define XFile_flush                 XFileDevice_flush
#define XFile_handle                XFileDevice_handle
#define XFile_error                 XFileDevice_error
#define XFile_unsetError            XFileDevice_unsetError
#define XFile_fileTime              XFileDevice_fileTime
#define XFile_setFileTime           XFileDevice_setFileTime
#define XFile_map                   XFileDevice_map
#define XFile_unmap                 XFileDevice_unmap

/* ============================================================================
 * 文件名操作
 * ============================================================================ */

/**
 * @brief 设置文件名
 * @param file XFile 对象指针
 * @param name 文件名
 */
void XFile_setFileName(XFile* file, const XString* name);

/* ============================================================================
 * 打开文件
 * ============================================================================ */

/**
 * @brief 以指定模式和权限打开文件
 * @param file XFile 对象指针
 * @param mode 打开模式
 * @param permissions 文件权限
 * @return 成功返回 true，失败返回 false
 */
bool XFile_open_2(XFile* file, XIODeviceBaseMode mode, XFilePermissions permissions);

/**
 * @brief 从文件描述符打开文件
 * @param file XFile 对象指针
 * @param fd 文件描述符
 * @param mode 打开模式
 * @param handleFlags 句柄标志
 * @return 成功返回 true，失败返回 false
 */
bool XFile_open_3(XFile* file, int fd, XIODeviceBaseMode mode, XFileDeviceFileHandleFlags handleFlags);

/* ============================================================================
 * 文件存在检查
 * ============================================================================ */

/**
 * @brief 检查文件是否存在（成员函数）
 * @param file XFile 对象指针
 * @return 存在返回 true，否则返回 false
 */
bool XFile_exists(const XFile* file);

/**
 * @brief 检查文件是否存在（静态函数）
 * @param fileName 文件名
 * @return 存在返回 true，否则返回 false
 */
bool XFile_exists_static(const XString* fileName);

/* ============================================================================
 * 文件删除
 * ============================================================================ */

/**
 * @brief 删除文件（成员函数）
 * @param file XFile 对象指针
 * @return 成功返回 true，失败返回 false
 */
bool XFile_remove(XFile* file);

/**
 * @brief 删除文件（静态函数）
 * @param fileName 文件名
 * @return 成功返回 true，失败返回 false
 */
bool XFile_remove_static(const XString* fileName);

/* ============================================================================
 * 文件重命名
 * ============================================================================ */

/**
 * @brief 重命名文件（成员函数）
 * @param file XFile 对象指针
 * @param newName 新文件名
 * @return 成功返回 true，失败返回 false
 */
bool XFile_rename(XFile* file, const XString* newName);

/**
 * @brief 重命名文件（静态函数）
 * @param oldName 原文件名
 * @param newName 新文件名
 * @return 成功返回 true，失败返回 false
 */
bool XFile_rename_static(const XString* oldName, const XString* newName);

/* ============================================================================
 * 文件复制
 * ============================================================================ */

/**
 * @brief 复制文件（成员函数）
 * @param file XFile 对象指针
 * @param newName 目标文件名
 * @return 成功返回 true，失败返回 false
 */
bool XFile_copy(XFile* file, const XString* newName);

/**
 * @brief 复制文件（静态函数）
 * @param fileName 源文件名
 * @param newName 目标文件名
 * @return 成功返回 true，失败返回 false
 */
bool XFile_copy_static(const XString* fileName, const XString* newName);

/* ============================================================================
 * 创建链接
 * ============================================================================ */

/**
 * @brief 创建链接（成员函数）
 * @param file XFile 对象指针
 * @param linkName 链接名
 * @return 成功返回 true，失败返回 false
 */
bool XFile_link(XFile* file, const XString* linkName);

/**
 * @brief 创建链接（静态函数）
 * @param fileName 源文件名
 * @param linkName 链接名
 * @return 成功返回 true，失败返回 false
 */
bool XFile_link_static(const XString* fileName, const XString* linkName);

/* ============================================================================
 * 移到回收站
 * ============================================================================ */

/**
 * @brief 移动文件到回收站（成员函数）
 * @param file XFile 对象指针
 * @return 成功返回 true，失败返回 false
 */
bool XFile_moveToTrash(XFile* file);

/**
 * @brief 移动文件到回收站（静态函数）
 * @param fileName 文件名
 * @param pathInTrash 回收站中的路径（可选）
 * @return 成功返回 true，失败返回 false
 */
bool XFile_moveToTrash_static(const XString* fileName, XString* pathInTrash);

/* ============================================================================
 * 符号链接目标
 * ============================================================================ */

/**
 * @brief 获取符号链接目标（成员函数）
 * @param file XFile 对象指针
 * @return 符号链接目标路径，需要调用者释放
 */
XString* XFile_symLinkTarget(const XFile* file);

/**
 * @brief 获取符号链接目标（静态函数）
 * @param fileName 文件名
 * @return 符号链接目标路径，需要调用者释放
 */
XString* XFile_symLinkTarget_static(const XString* fileName);

/* ============================================================================
 * 静态便捷函数
 * ============================================================================ */

/**
 * @brief 调整文件大小（静态函数）
 * @param fileName 文件名
 * @param sz 新大小
 * @return 成功返回 true，失败返回 false
 */
bool XFile_resize_static(const XString* fileName, int64_t sz);

/**
 * @brief 获取文件权限（静态函数）
 * @param fileName 文件名
 * @return 权限标志组合
 */
XFilePermissions XFile_permissions_static(const XString* fileName);

/**
 * @brief 设置文件权限（静态函数）
 * @param fileName 文件名
 * @param permissions 权限标志组合
 * @return 成功返回 true，失败返回 false
 */
bool XFile_setPermissions_static(const XString* fileName, XFilePermissions permissions);

/* ============================================================================
 * 文件名编码转换
 * ============================================================================ */

/**
 * @brief 将文件名编码为本地编码
 * @param fileName 文件名
 * @return 编码后的字节数组，需要调用者释放
 */
XByteArray* XFile_encodeName(const XString* fileName);

/**
 * @brief 将本地编码解码为文件名
 * @param localFileName 本地编码的字节数组
 * @return 解码后的文件名，需要调用者释放
 */
XString* XFile_decodeName(const XByteArray* localFileName);

/**
 * @brief 将 C 字符串解码为文件名
 * @param localFileName 本地编码的 C 字符串
 * @return 解码后的文件名，需要调用者释放
 */
XString* XFile_decodeName_2(const char* localFileName);

#ifdef __cplusplus
}
#endif

#endif // XFILE_H