/**
 * @file       XConsoleShellAdc.h
 * @brief      XConsoleShell ADC 命令、固定槽位和产品策略接口。
 * @details
 * 本模块没有直接对应的 Qt 类；会话和对象生命周期遵循 XConsoleShell 与
 * XObject 约定。
 *
 * ADC 命令只依赖 XAdc 公共 API，不直接访问平台驱动。每个 Shell 使用固定
 * 数量槽位保存打开的 ADC 句柄，Shell 销毁时统一关闭并释放。产品策略回调
 * 可限制电池采样、校准通道等敏感资源，Shell 不拥有回调上下文。公共头
 * 文件不包含 Linux IIO、Windows、MCU HAL 或其他平台 API。
 */
#ifndef XCONSOLE_SHELL_ADC_H
#define XCONSOLE_SHELL_ADC_H

#include "XConsoleShellCommand.h"
#include "XAdc.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ADC_ON

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Shell 对象前置声明；实际对象由 XConsoleShell.h 定义和管理。 */
typedef struct XConsoleShell XConsoleShell;
/** @brief Shell 会话前置声明；会话由所属 Shell 拥有，调用者不得释放。 */
typedef struct XConsoleShellSession XConsoleShellSession;

/** @brief 产品策略需要判断的 ADC 操作类型；枚举值互斥，不可按位组合。 */
typedef enum XConsoleShellAdcOperation {
    XConsoleShellAdcOperation_List = 0, /**< 枚举已打开 ADC。 */
    XConsoleShellAdcOperation_Open,     /**< 占用 ADC 通道。 */
    XConsoleShellAdcOperation_Close,    /**< 释放 ADC 通道。 */
    XConsoleShellAdcOperation_Info,     /**< 查询 ADC 配置和能力。 */
    XConsoleShellAdcOperation_Read,     /**< 读取采样值。 */
    XConsoleShellAdcOperation_Configure /**< 修改采样配置。 */
} XConsoleShellAdcOperation;

/**
 * @brief      产品 ADC 通道授权回调。
 * @param      userData 产品上下文；可为 NULL；Shell 只借用且不释放。
 * @param      session 当前命令会话的只读借用指针；不能为空且只在回调期间有效。
 * @param      channel 当前逻辑控制器和通道的只读借用指针；不能为空且只在
 *             回调期间有效。
 * @param      operation 即将执行的互斥操作类型。
 * @return     允许操作返回 true；拒绝返回 false，拒绝时 Shell 不调用 ADC
 *             后端且不改变对应槽位。
 */
typedef bool (*XConsoleShellAdcAuthorizeFn)(
    void* userData, const XConsoleShellSession* session,
    const XAdcChannel* channel, XConsoleShellAdcOperation operation);

/**
 * @brief ADC 固定槽位。
 * @details 槽位存储由所属 Shell 内嵌拥有，不得按值复制或由调用者释放。
 */
typedef struct XConsoleShellAdcSlot {
    XAdc* adc; /**< Shell 拥有的 ADC 句柄；不允许外部释放。 */
} XConsoleShellAdcSlot;

/**
 * @brief      设置或清除 ADC 产品策略回调。
 * @param      self Shell 对象；不能为空。
 * @param      authorize 策略回调；Shell 只借用，传 NULL 清除现有策略。
 * @param      userData 产品上下文；可为 NULL；Shell 只借用且不释放；authorize
 *             为 NULL 时忽略该参数并清除已保存的上下文。
 * @return     设置成功返回 true；self 为 NULL 返回 false 且不修改原策略。
 */
bool XConsoleShell_setAdcAuthorizeCallback(
    XConsoleShell* self, XConsoleShellAdcAuthorizeFn authorize,
    void* userData);

/**
 * @brief adc 根命令静态描述；子命令由各功能宏独立裁剪。
 * @details 该对象由库静态拥有，调用者只可借用其地址进行命令注册，不得
 *          修改或释放；对象在程序整个运行期间有效。
 */
extern const XConsoleCommand XConsoleShellAdc_command;

#ifdef __cplusplus
}
#endif

#endif /* Shell、命令、I/O 和 ADC 均启用 */
#endif /* XCONSOLE_SHELL_ADC_H */
