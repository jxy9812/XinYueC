/**
 * @file       XConsoleShellGpio.h
 * @brief      XConsoleShell GPIO 命令、固定槽位和产品引脚策略接口。
 * @details
 * 本模块没有直接对应的 Qt 类；会话和对象生命周期遵循 XConsoleShell 与
 * XObject 约定。
 *
 * GPIO 命令只调用 XGpio 公共 API，不包含 Linux GPIO、STM32 HAL、ESP-IDF
 * 或其他平台头文件。每个 Shell 使用固定容量槽位持有已经打开的 XGpio；
 * 槽位在 Shell 析构时统一关闭和释放。产品策略回调用于限制 Flash、JTAG、
 * 电源控制等敏感引脚，Shell 不取得回调和 userData 的所有权。
 */

#ifndef XCONSOLE_SHELL_GPIO_H
#define XCONSOLE_SHELL_GPIO_H

#include "XConsoleShellCommand.h"
#include "XGpio.h"
#include "XAtomic.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_GPIO_ON

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Shell 对象前置声明；实际对象由 XConsoleShell.h 定义和管理。 */
typedef struct XConsoleShell XConsoleShell;
/** @brief Shell 会话前置声明；会话由所属 Shell 拥有，调用者不得释放。 */
typedef struct XConsoleShellSession XConsoleShellSession;

/** @brief 产品策略需要判断的 GPIO 操作类型；枚举值互斥，不可按位组合。 */
typedef enum XConsoleShellGpioOperation {
    XConsoleShellGpioOperation_List = 0, /**< 枚举已打开引脚。 */
    XConsoleShellGpioOperation_Open,     /**< 创建并占用引脚。 */
    XConsoleShellGpioOperation_Close,    /**< 关闭并释放引脚。 */
    XConsoleShellGpioOperation_Info,     /**< 查询引脚配置和能力。 */
    XConsoleShellGpioOperation_Read,     /**< 读取物理或有效状态。 */
    XConsoleShellGpioOperation_Write,    /**< 写入物理或有效状态。 */
    XConsoleShellGpioOperation_Toggle,   /**< 翻转输出状态。 */
    XConsoleShellGpioOperation_Configure,/**< 重新配置方向、电气和有效电平。 */
    XConsoleShellGpioOperation_Interrupt /**< 配置、启停或等待中断。 */
} XConsoleShellGpioOperation;

/**
 * @brief      产品 GPIO 引脚授权回调。
 * @param      userData 设置策略时传入的产品上下文；可为 NULL；Shell 只借用
 *             且不释放。
 * @param      session 当前命令会话的只读借用指针；不能为空且只在回调期间有效。
 * @param      pin 当前操作的逻辑控制器和引脚的只读借用指针；不能为空且只在
 *             回调期间有效。
 * @param      operation 即将执行的互斥操作类型。
 * @return     允许操作返回 true；拒绝返回 false，拒绝时 Shell 不调用 GPIO
 *             后端且不改变对应槽位。
 * @note 回调不得保存 session 或 pin 指针，也不得在回调中释放 GPIO。
 */
typedef bool (*XConsoleShellGpioAuthorizeFn)(
    void* userData, const XConsoleShellSession* session,
    const XGpioPin* pin, XConsoleShellGpioOperation operation);

/**
 * @brief Shell 内部固定 GPIO 槽位。
 * @details gpio 为 NULL 即表示空槽，不额外保存 used 标志。事件回调只原子增加
 * eventCount，不在中断上下文访问 Shell 输出或其他非中断安全成员。
 */
typedef struct XConsoleShellGpioSlot {
    XGpio* gpio;                    /**< Shell 拥有的 GPIO 句柄；空槽为 NULL。 */
    XAtomic_uint32_t eventCount;    /**< 自打开或上次清零后的中断事件数量。 */
} XConsoleShellGpioSlot;

/**
 * @brief      设置或清除 GPIO 产品引脚策略。
 * @param      self Shell 对象；不能为空。
 * @param      authorize 策略回调；Shell 只借用，传 NULL 清除现有策略。
 * @param      userData 产品上下文；可为 NULL；Shell 只借用且不释放；authorize
 *             为 NULL 时忽略该参数并清除已保存的上下文。
 * @return     设置成功返回 true；self 为 NULL 返回 false 且不修改原策略。
 * @note XCONSOLE_SHELL_GPIO_REQUIRE_POLICY_ON 为 1 时，没有策略将拒绝所有
 *       会占用或改变硬件状态的命令。
 */
bool XConsoleShell_setGpioAuthorizeCallback(
    XConsoleShell* self, XConsoleShellGpioAuthorizeFn authorize,
    void* userData);

/**
 * @brief gpio 根命令静态描述；子命令由各功能宏独立裁剪。
 * @details 该对象由库静态拥有，调用者只可借用其地址进行命令注册，不得
 *          修改或释放；对象在程序整个运行期间有效。
 */
extern const XConsoleCommand XConsoleShellGpio_command;

#ifdef __cplusplus
}
#endif

#endif /* Shell、命令、I/O 和 GPIO 均启用 */
#endif /* XCONSOLE_SHELL_GPIO_H */
