/**
 * @file       XConsoleShellI2c.h
 * @brief      XConsoleShell I2C 命令公共声明。
 * @details    本模块没有直接对应的 Qt 类；会话和对象生命周期遵循
 *             XConsoleShell 与 XObject 约定。
 *
 *             命令实现使用固定槽位保存 XI2c 句柄，不持有平台文件描述符或
 *             芯片寄存器。写事务可通过产品授权回调限制；回调应在执行上下文
 *             内快速返回，不得保存 Shell 或会话指针。公共头文件不包含
 *             Linux i2c-dev、Windows、MCU HAL 或其他平台 API。
 */
#ifndef XCONSOLE_SHELL_I2C_H
#define XCONSOLE_SHELL_I2C_H

#include "XConsoleShellCommand.h"
#include "XI2c.h"
#include <stdbool.h>

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_I2C_ON

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 产品策略需要判断的 I2C 操作类型；枚举值互斥，不可按位组合。 */
typedef enum XConsoleShellI2cOperation {
    XConsoleShellI2cOperation_List = 0, /**< 枚举已打开的 I2C 目标。 */
    XConsoleShellI2cOperation_Open,     /**< 创建并打开 I2C 目标。 */
    XConsoleShellI2cOperation_Close,    /**< 关闭并释放 I2C 目标。 */
    XConsoleShellI2cOperation_Info,     /**< 查询目标配置和后端能力。 */
    XConsoleShellI2cOperation_Read,     /**< 从目标读取字节。 */
    XConsoleShellI2cOperation_Write,    /**< 向目标写入字节。 */
    XConsoleShellI2cOperation_WriteRead /**< 执行不释放总线的组合写读事务。 */
} XConsoleShellI2cOperation;

/**
 * @brief      产品 I2C 访问策略回调。
 * @param      userData 产品上下文；可为 NULL；Shell 只借用且不释放。
 * @param      session 当前会话的只读借用指针；不能为空且仅在回调期间有效。
 * @param      target 目标控制器、从设备地址和地址模式的只读借用指针；不能
 *             为空且仅在回调期间有效。
 * @param      operation 请求的互斥 I2C 操作类型。
 * @return     允许本次操作返回 true；拒绝返回 false，拒绝时 Shell 不调用
 *             I2C 后端且不改变对应槽位。
 */
typedef bool (*XConsoleShellI2cAuthorizeFn)(
    void* userData, const XConsoleShellSession* session,
    const XI2cTarget* target, XConsoleShellI2cOperation operation);

/**
 * @brief Shell 持有的固定 I2C 句柄槽位。
 * @details 槽位存储由所属 Shell 内嵌拥有，不得按值复制或由调用者释放。
 */
typedef struct XConsoleShellI2cSlot {
    XI2c* m_bus; /**< Shell 拥有的 I2C 句柄；NULL 表示空槽位。 */
} XConsoleShellI2cSlot;

/**
 * @brief      设置或清除 I2C 产品访问策略回调。
 * @param      self Shell 对象；不能为空。
 * @param      authorize 授权回调；Shell 只借用，传 NULL 清除回调。
 * @param      userData 回调上下文；可为 NULL；Shell 只借用且不释放；authorize
 *             为 NULL 时忽略该参数并清除已保存的上下文。
 * @return     设置成功返回 true；self 为 NULL 返回 false 且不修改原策略。
 */
bool XConsoleShell_setI2cAuthorizeCallback(
    XConsoleShell* self, XConsoleShellI2cAuthorizeFn authorize, void* userData);

/**
 * @brief 内置根命令 `i2c` 的静态描述；子命令由各功能宏独立裁剪。
 * @details 该对象由库静态拥有，调用者只可借用其地址进行命令注册，不得
 *          修改或释放；对象在程序整个运行期间有效。
 */
extern const XConsoleCommand XConsoleShellI2c_command;

#ifdef __cplusplus
}
#endif

#endif /* Shell、命令、I/O 和 I2C 均启用 */
#endif /* XCONSOLE_SHELL_I2C_H */
