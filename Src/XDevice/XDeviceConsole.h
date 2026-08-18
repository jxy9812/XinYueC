/**
 * @file       XDeviceConsole.h
 * @brief      统一控制台设备接口。
 * @details    控制台设备以 XFd 对外提供标准输入的读写和终端回显控制。
 *             平台私有控制台状态由现有文件后端创建和释放，设备层只持有
 *             一个内部后端 XFd，避免把 Win32/POSIX 类型泄漏到公共接口。
 */
#ifndef XDEVICECONSOLE_H
#define XDEVICECONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "XDevice.h"

/** @brief 控制台设备专有控制命令。 */
typedef enum XDeviceConsoleCommand
{
    XDeviceConsoleCommand_SetEcho = XDeviceCommand_Count, /**< 设置终端输入是否回显；in 为 bool。 */
    XDeviceConsoleCommand_Count                              /**< 控制台命令数量边界，不是有效命令。 */
} XDeviceConsoleCommand;

/** @brief 控制台设备打开选项；当前仅使用通用基类选项。 */
typedef struct XDeviceConsoleOpenOptions
{
    XDeviceOpenOptions m_base; /**< 通用打开选项；m_target 通常为空。 */
} XDeviceConsoleOpenOptions;

/** @brief 控制台打开上下文；内部后端句柄不直接暴露给调用方。 */
typedef struct XDeviceConsoleContext
{
    XDeviceContext m_base; /**< 通用设备上下文，必须是第一个成员。 */
    XFd m_backendFd;       /**< 平台标准输入后端句柄；仅设备内部借用。 */
} XDeviceConsoleContext;

/** @brief XDeviceConsole 类虚函数表；继承 XDevice，不新增虚槽。 */
XCLASS_DEFINE_BEGING(XDeviceConsole)
XCLASS_DEFINE_EXTEND_END(XDeviceConsole, XDevice)

/** @brief 控制台设备类对象；基类必须是第一个成员。 */
typedef struct XDeviceConsole
{
    XDevice m_base;
} XDeviceConsole;

/** @brief 初始化控制台设备类虚函数表。 @return 共享虚函数表，失败返回 NULL。 */
XVtable* XDeviceConsole_class_init(void);
/** @brief 初始化已分配的控制台设备对象。 @param self 待初始化对象，不能为 NULL。 */
void XDeviceConsole_init(XDeviceConsole* self);
/** @brief 创建控制台设备类对象。 @return 新对象，失败返回 NULL；调用方负责释放。 */
XDeviceConsole* XDeviceConsole_create(void);
/** @brief 注册控制台设备类别。 @return 首次注册或已注册返回 true，失败返回 false。 */
bool XDeviceConsole_register(void);

/** @brief 复用 XDevice 的标准设备门面；参数和返回值契约与对应 XDevice API 相同。 */
#define XDeviceConsole_open(options, error) \
    XDevice_open(XDeviceType_Console, \
        (const XDeviceOpenOptions*)(options), (error))
#define XDeviceConsole_close   XDevice_close
#define XDeviceConsole_read    XDevice_read
#define XDeviceConsole_write   XDevice_write
#define XDeviceConsole_flush   XDevice_flush
#define XDeviceConsole_control XDevice_control

/**
 * @brief 打开当前进程的标准输入控制台设备。
 * @param error 可选错误码输出。
 * @return 成功返回控制台设备 XFd，失败返回 XFD_INVALID。
 */
XFd XDeviceConsole_openStandardInput(int* error);

/**
 * @brief 设置控制台输入回显状态。
 * @param fd 控制台设备 XFd。
 * @param enabled true 开启回显，false 关闭回显。
 * @return 平台设置成功返回 true。
 */
bool XDeviceConsole_setEcho(XFd fd, bool enabled);

/**
 * @brief 获取平台内部控制台后端 XFd。
 * @details 仅供事件分发器绑定平台输入事件和 owner 使用；业务层读写仍应
 *          使用外层控制台 XFd，不得关闭返回的后端句柄。
 * @param fd 控制台设备 XFd。
 * @return 内部后端 XFd；无效或非控制台设备返回 XFD_INVALID。
 */
XFd XDeviceConsole_backendFd(XFd fd);

#ifdef __cplusplus
}
#endif

#endif /* XDEVICECONSOLE_H */
