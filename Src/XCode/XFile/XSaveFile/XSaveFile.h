#ifndef XSAVEFILE_H
#define XSAVEFILE_H

/**
 * @file XSaveFile.h
 * @brief 安全文件写入类，提供原子性文件保存操作
 * 
 * 移植自 Qt 6.8 QSaveFile 类，继承自 XFileDevice。
 * 通过写入临时文件并在提交时原子性重命名，确保文件写入的安全性。
 */

#include <stdint.h>
#include <stdbool.h>
#include "XFileDevice.h"
#include "XString.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 虚函数表定义
 * ============================================================================ */

#define XSAVEFILE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XSaveFile))

XCLASS_DEFINE_BEGING(XSaveFile)
// XSaveFile 没有新增虚函数，重写的虚函数在 .c 中实现
XCLASS_DEFINE_EXTEND_END(XSaveFile, XFileDevice)

/* ============================================================================
 * 结构体定义
 * ============================================================================ */

/**
 * @brief XSaveFile 结构体，安全文件写入类
 * 
 * 写入时先创建临时文件，commit() 时原子性重命名为目标文件。
 * 如果写入失败或取消，原文件不受影响。
 */
typedef struct XSaveFile {
    XFileDevice m_parent;           /**< 基类 XFileDevice */
    
    /* XSaveFile 特有成员 */
    XString* m_fileName;            /**< 目标文件名 */
    XString* m_tempFileName;        /**< 临时文件名 */
    
    /* 状态标志（使用位字段节省内存） */
    uint8_t m_useTempFile        : 1;  /**< 是否使用临时文件（directWriteFallback 时为 false） */
    uint8_t m_canceled           : 1;  /**< 是否已取消写入 */
    uint8_t m_committed          : 1;  /**< 是否已提交 */
    uint8_t m_directWriteFallback: 1;  /**< 是否允许直接写入回退 */
    uint8_t m_writeError         : 1;  /**< 写入过程中是否发生错误 */
    uint8_t _reserved            : 3;  /**< 保留位 */
} XSaveFile;

/* ============================================================================
 * 虚函数表初始化
 * ============================================================================ */

/**
 * @brief 初始化 XSaveFile 类的虚函数表
 * @return 虚函数表指针
 */
XVtable* XSaveFile_class_init(void);

/* ============================================================================
 * 构造与析构
 * ============================================================================ */

/**
 * @brief 创建 XSaveFile 对象
 * @return XSaveFile 对象指针，失败返回 NULL
 */
XSaveFile* XSaveFile_create_1(void);

/**
 * @brief 创建 XSaveFile 对象并设置文件名
 * @param name 目标文件名
 * @return XSaveFile 对象指针，失败返回 NULL
 */
XSaveFile* XSaveFile_create_2(const XString* name);

/**
 * @brief 初始化 XSaveFile 对象
 * @param file XSaveFile 对象指针
 */
void XSaveFile_init_1(XSaveFile* file);

/**
 * @brief 初始化 XSaveFile 对象并设置文件名
 * @param file XSaveFile 对象指针
 * @param name 目标文件名
 */
void XSaveFile_init_2(XSaveFile* file, const XString* name);


/* ============================================================================
 * 析构函数（继承自 XObject）
 * ============================================================================ */

#define XSaveFile_deinitLater           XFileDevice_deinitLater
#define XSaveFile_deleteLater           XFileDevice_deleteLater

/* ============================================================================
 * 继承自 XFileDevice 的虚函数（使用宏映射）
 * ============================================================================ */

#define XSaveFile_fileName_base         XFileDevice_fileName_base
#define XSaveFile_open_base             XIODevice_open_base
#define XSaveFile_close_base            XIODevice_close_base

/* ============================================================================
 * 继承自 XFileDevice 的非虚函数（使用宏映射）
 * ============================================================================ */

#define XSaveFile_error                 XFileDevice_error
#define XSaveFile_unsetError            XFileDevice_unsetError
#define XSaveFile_flush                 XFileDevice_flush
#define XSaveFile_handle                XFileDevice_handle
#define XSaveFile_fileTime              XFileDevice_fileTime
#define XSaveFile_setFileTime           XFileDevice_setFileTime
#define XSaveFile_map                   XFileDevice_map
#define XSaveFile_unmap                 XFileDevice_unmap
#define XSaveFile_permissions_base      XFileDevice_permissions_base
#define XSaveFile_setPermissions_base   XFileDevice_setPermissions_base
#define XSaveFile_resize_base           XFileDevice_resize_base

/* ============================================================================
 * 继承自 XIODevice 的虚函数（使用宏映射）
 * ============================================================================ */

#define XSaveFile_isSequential_base     XIODevice_isSequential_base
#define XSaveFile_pos_base              XIODevice_pos_base
#define XSaveFile_seek_base             XIODevice_seek_base
#define XSaveFile_atEnd_base            XIODevice_atEnd_base
#define XSaveFile_reset_base            XIODevice_reset_base
#define XSaveFile_size_base             XIODevice_size_base
#define XSaveFile_bytesAvailable_base   XIODevice_bytesAvailable_base
#define XSaveFile_bytesToWrite_base     XIODevice_bytesToWrite_base
#define XSaveFile_readData_base         XIODevice_readData_base
#define XSaveFile_readLineData_base     XIODevice_readLineData_base
#define XSaveFile_skipData_base         XIODevice_skipData_base

/* ============================================================================
 * 文件名操作
 * ============================================================================ */

/**
 * @brief 设置目标文件名
 * @param file XSaveFile 对象指针
 * @param name 目标文件名
 * @note 必须在 open() 之前调用
 */
void XSaveFile_setFileName(XSaveFile* file, const XString* name);

/* ============================================================================
 * 打开与提交
 * ============================================================================ */

/**
 * @brief 提交更改，将临时文件原子性重命名为目标文件
 * @param file XSaveFile 对象指针
 * @return 成功返回 true，失败返回 false
 * @note 必须在写入完成后调用，否则临时文件会被丢弃
 */
bool XSaveFile_commit(XSaveFile* file);

/**
 * @brief 取消写入
 * @param file XSaveFile 对象指针
 * @note 调用后 commit() 将丢弃临时文件
 */
void XSaveFile_cancelWriting(XSaveFile* file);

/* ============================================================================
 * 直接写入回退模式
 * ============================================================================ */

/**
 * @brief 设置是否允许直接写入回退
 * @param file XSaveFile 对象指针
 * @param enabled true 允许直接写入，false 必须使用临时文件
 * @note 当目录权限不允许创建临时文件时，可以直接写入目标文件
 *       但这会失去原子性保证
 */
void XSaveFile_setDirectWriteFallback(XSaveFile* file, bool enabled);

/**
 * @brief 获取是否允许直接写入回退
 * @param file XSaveFile 对象指针
 * @return true 允许直接写入，false 必须使用临时文件
 */
bool XSaveFile_directWriteFallback(const XSaveFile* file);

/* ============================================================================
 * 内部函数
 * ============================================================================ */

/**
 * @brief 生成临时文件名（内部函数）
 * @param targetPath 目标文件路径
 * @param tempPath 输出临时文件路径缓冲区
 * @param tempPathSize 缓冲区大小
 * @return 成功返回 true，失败返回 false
 */
bool XSaveFile_generateTempFileName(const XString* targetPath, XString* tempPath);

#ifdef __cplusplus
}
#endif

#endif // XSAVEFILE_H