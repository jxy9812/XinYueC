/**
 * @file       XDeviceDir.h
 * @brief      统一目录设备接口。
 * @details    XDeviceDir 只负责目录句柄的生命周期和顺序遍历。路径级的
 *             mkdir/rmdir/stat 等无状态文件系统操作仍属于 XDeviceFile。
 *             目录后端句柄只保存在 XDeviceDirContext 中，业务层统一使用 XFd。
 */
#ifndef XDEVICEDIR_H
#define XDEVICEDIR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XDevice.h"

struct XString;
typedef struct XString XString;

/**
 * @brief 目录遍历返回的一项目录条目。
 * @details name 由调用方提供并拥有；平台后端只在调用期间写入，不接管所有权。
 */
typedef struct XDirEntry
{
    XString* name;            /**< 条目名称输出对象；调用方分配、初始化并拥有。 */
    uint8_t isDir     : 1;    /**< 条目是目录。 */
    uint8_t isFile    : 1;    /**< 条目是普通文件。 */
    uint8_t isSymLink : 1;    /**< 条目是符号链接。 */
    uint8_t isHidden  : 1;    /**< 条目具有隐藏属性。 */
    uint8_t reserved  : 4;    /**< 保留位，必须初始化为 0。 */
} XDirEntry;

/** @brief 目录设备专有控制命令。 */
typedef enum XDeviceDirCommand
{
    XDeviceDirCommand_ReadNext = XDeviceCommand_Count, /**< 读取下一目录条目。 */
    XDeviceDirCommand_Count                              /**< 目录命令数量边界，不是有效命令。 */
} XDeviceDirCommand;

/** @brief 目录设备打开选项；m_base.m_target 必须指向目录路径。 */
typedef struct XDeviceDirOpenOptions
{
    XDeviceOpenOptions m_base; /**< 通用打开选项；m_target 必须为目录路径。 */
} XDeviceDirOpenOptions;

/** @brief 目录设备打开上下文；后端句柄不暴露给调用方。 */
typedef struct XDeviceDirContext
{
    XDeviceContext m_base; /**< 通用设备上下文，必须是第一个成员。 */
    void* m_backendHandle; /**< 平台目录句柄；由设备创建和释放，调用方不得访问。 */
} XDeviceDirContext;

/** @brief XDeviceDir 类虚函数表；继承 XDevice，不新增虚槽。 */
XCLASS_DEFINE_BEGING(XDeviceDir)
XCLASS_DEFINE_EXTEND_END(XDeviceDir, XDevice)

/** @brief 目录设备类对象；基类必须是第一个成员。 */
typedef struct XDeviceDir
{
    XDevice m_base;
} XDeviceDir;

/** @brief 初始化目录设备类虚函数表。 @return 共享虚函数表，失败返回 NULL。 */
XVtable* XDeviceDir_class_init(void);
/** @brief 初始化已分配的目录设备对象。 @param self 待初始化对象，不能为 NULL。 */
void XDeviceDir_init(XDeviceDir* self);
/** @brief 创建目录设备类对象。 @return 新对象，失败返回 NULL；调用方负责释放。 */
XDeviceDir* XDeviceDir_create(void);
/** @brief 注册目录设备类别。 @return 首次注册或已注册返回 true，失败返回 false。 */
bool XDeviceDir_register(void);

/** @brief 复用 XDevice 的打开、关闭和控制门面；参数契约与父类 API 相同。 */
#define XDeviceDir_open(options, error) \
    XDevice_open(XDeviceType_Dir, \
        (const XDeviceOpenOptions*)(options), (error))
#define XDeviceDir_close   XDevice_close
#define XDeviceDir_control XDevice_control

/**
 * @brief 按路径打开目录设备。
 * @param path 目录路径；调用期间借用。
 * @param error 可选错误码输出。
 * @return 成功返回 XFd，失败返回 XFD_INVALID。
 */
XFd XDeviceDir_openPath(const XString* path, int* error);

/**
 * @brief 读取目录中的下一项。
 * @param fd 已打开的目录设备描述符。
 * @param entry 调用方提供的输出条目；entry->name 必须指向已初始化的 XString。
 * @return 成功读取一项返回 true，到达末尾或失败返回 false。
 */
bool XDeviceDir_readNext(XFd fd, XDirEntry* entry);

#ifdef __cplusplus
}
#endif

#endif /* XDEVICEDIR_H */
