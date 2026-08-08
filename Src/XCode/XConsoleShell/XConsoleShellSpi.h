/**
 * @file       XConsoleShellSpi.h
 * @brief      XConsoleShell SPI 命令公共声明。
 * @details    本模块没有直接对应的 Qt 类；会话和对象生命周期遵循
 *             XConsoleShell 与 XObject 约定。
 *
 *             命令实现只依赖 XSpi 公共事务接口和固定槽位；平台 SPI 句柄、
 *             片选 GPIO 以及 DMA 资源均由后端管理。写入型 transfer 可通过
 *             产品授权回调限制，Shell 不直接调用 Linux spidev、Windows、
 *             MCU HAL 或其他平台 API。
 */
#ifndef XCONSOLE_SHELL_SPI_H
#define XCONSOLE_SHELL_SPI_H

#include "XConsoleShellCommand.h"
#include "XSpi.h"
#include <stdbool.h>

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_SPI_ON

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 产品策略需要判断的 SPI 操作类型；枚举值互斥，不可按位组合。 */
typedef enum XConsoleShellSpiOperation {
    XConsoleShellSpiOperation_List = 0, /**< 枚举已打开的 SPI 目标。 */
    XConsoleShellSpiOperation_Open,     /**< 创建并打开 SPI 目标。 */
    XConsoleShellSpiOperation_Close,    /**< 关闭并释放 SPI 目标。 */
    XConsoleShellSpiOperation_Info,     /**< 查询目标配置和后端能力。 */
    XConsoleShellSpiOperation_Transfer  /**< 执行全双工传输事务。 */
} XConsoleShellSpiOperation;

/**
 * @brief      产品 SPI 访问策略回调。
 * @param      userData 产品上下文；可为 NULL；Shell 只借用且不释放。
 * @param      session 当前会话的只读借用指针；不能为空且仅在回调期间有效。
 * @param      config SPI 配置快照的只读借用指针；不能为空且仅在回调期间有效。
 * @param      operation 请求的互斥 SPI 操作类型。
 * @return     允许本次操作返回 true；拒绝返回 false，拒绝时 Shell 不调用
 *             SPI 后端且不改变对应槽位。
 */
typedef bool (*XConsoleShellSpiAuthorizeFn)(
    void* userData, const XConsoleShellSession* session,
    const XSpiConfig* config, XConsoleShellSpiOperation operation);

/**
 * @brief Shell 持有的固定 SPI 句柄槽位。
 * @details 槽位存储由所属 Shell 内嵌拥有，不得按值复制或由调用者释放。
 */
typedef struct XConsoleShellSpiSlot {
    XSpi* m_spi; /**< Shell 拥有的 SPI 句柄；NULL 表示空槽位。 */
} XConsoleShellSpiSlot;

/**
 * @brief      设置或清除 SPI 产品访问策略回调。
 * @param      self Shell 对象；不能为空。
 * @param      authorize 授权回调；Shell 只借用，传 NULL 清除回调。
 * @param      userData 回调上下文；可为 NULL；Shell 只借用且不释放；authorize
 *             为 NULL 时忽略该参数并清除已保存的上下文。
 * @return     设置成功返回 true；self 为 NULL 返回 false 且不修改原策略。
 */
bool XConsoleShell_setSpiAuthorizeCallback(
    XConsoleShell* self, XConsoleShellSpiAuthorizeFn authorize, void* userData);

/**
 * @brief 内置根命令 `spi` 的静态描述；子命令由各功能宏独立裁剪。
 * @details 该对象由库静态拥有，调用者只可借用其地址进行命令注册，不得
 *          修改或释放；对象在程序整个运行期间有效。
 */
extern const XConsoleCommand XConsoleShellSpi_command;

#ifdef __cplusplus
}
#endif

#endif /* Shell、命令、I/O 和 SPI 均启用 */
#endif /* XCONSOLE_SHELL_SPI_H */
