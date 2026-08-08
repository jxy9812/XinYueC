/**
 * @file       XConsoleShellPwm.h
 * @brief      XConsoleShell PWM 命令、固定槽位和产品策略接口。
 * @details
 * 本模块没有直接对应的 Qt 类；会话和对象生命周期遵循 XConsoleShell 与
 * XObject 约定。
 *
 * PWM 命令只调用 XPwm 纯公共 API。句柄保存于 Shell 固定数组，避免嵌入式
 * 命令执行路径依赖动态容器；启动、停止和配置等会改变输出的操作可以由
 * 产品回调进行二次授权。公共头文件不包含 Linux PWM、Windows、MCU HAL
 * 或其他平台 API。
 */
#ifndef XCONSOLE_SHELL_PWM_H
#define XCONSOLE_SHELL_PWM_H

#include "XConsoleShellCommand.h"
#include "XPwm.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_PWM_ON

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Shell 对象前置声明；实际对象由 XConsoleShell.h 定义和管理。 */
typedef struct XConsoleShell XConsoleShell;
/** @brief Shell 会话前置声明；会话由所属 Shell 拥有，调用者不得释放。 */
typedef struct XConsoleShellSession XConsoleShellSession;

/** @brief 产品策略需要判断的 PWM 操作类型；枚举值互斥，不可按位组合。 */
typedef enum XConsoleShellPwmOperation {
    XConsoleShellPwmOperation_List = 0, /**< 枚举已打开 PWM。 */
    XConsoleShellPwmOperation_Open,     /**< 占用 PWM 通道。 */
    XConsoleShellPwmOperation_Close,    /**< 释放 PWM 通道。 */
    XConsoleShellPwmOperation_Info,     /**< 查询配置和运行状态。 */
    XConsoleShellPwmOperation_Configure,/**< 修改 PWM 配置。 */
    XConsoleShellPwmOperation_Start,    /**< 启动输出。 */
    XConsoleShellPwmOperation_Stop,     /**< 停止输出。 */
    XConsoleShellPwmOperation_SetFrequency, /**< 修改频率。 */
    XConsoleShellPwmOperation_SetDuty   /**< 修改占空比。 */
} XConsoleShellPwmOperation;

/**
 * @brief      产品 PWM 通道授权回调。
 * @param      userData 产品上下文；可为 NULL；Shell 只借用且不释放。
 * @param      session 当前命令会话的只读借用指针；不能为空且只在回调期间有效。
 * @param      channel 当前逻辑控制器和通道的只读借用指针；不能为空且只在
 *             回调期间有效。
 * @param      operation 即将执行的互斥操作类型。
 * @return     允许操作返回 true；拒绝返回 false，拒绝时 Shell 不调用 PWM
 *             后端且不改变对应槽位。
 */
typedef bool (*XConsoleShellPwmAuthorizeFn)(
    void* userData, const XConsoleShellSession* session,
    const XPwmChannel* channel, XConsoleShellPwmOperation operation);

/**
 * @brief PWM 固定槽位。
 * @details 槽位存储由所属 Shell 内嵌拥有，不得按值复制或由调用者释放。
 */
typedef struct XConsoleShellPwmSlot {
    XPwm* pwm; /**< Shell 拥有的 PWM 句柄；不允许外部释放。 */
} XConsoleShellPwmSlot;

/**
 * @brief      设置或清除 PWM 产品策略回调。
 * @param      self Shell 对象；不能为空。
 * @param      authorize 策略回调；Shell 只借用，传 NULL 清除现有策略。
 * @param      userData 产品上下文；可为 NULL；Shell 只借用且不释放；authorize
 *             为 NULL 时忽略该参数并清除已保存的上下文。
 * @return     设置成功返回 true；self 为 NULL 返回 false 且不修改原策略。
 */
bool XConsoleShell_setPwmAuthorizeCallback(
    XConsoleShell* self, XConsoleShellPwmAuthorizeFn authorize,
    void* userData);

/**
 * @brief pwm 根命令静态描述；子命令由各功能宏独立裁剪。
 * @details 该对象由库静态拥有，调用者只可借用其地址进行命令注册，不得
 *          修改或释放；对象在程序整个运行期间有效。
 */
extern const XConsoleCommand XConsoleShellPwm_command;

#ifdef __cplusplus
}
#endif

#endif /* Shell、命令、I/O 和 PWM 均启用 */
#endif /* XCONSOLE_SHELL_PWM_H */
