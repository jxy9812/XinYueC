/**
 * @file       XConsoleShellCan.h
 * @brief      XConsoleShell CAN 命令、固定槽位和产品授权接口。
 * @details
 * 本模块没有直接对应的 Qt 类；会话和对象生命周期遵循 XConsoleShell 与
 * XObject 约定。
 *
 * CAN 命令只调用 XCan 公共 API，不包含 SocketCAN、Win32、STM32 HAL、
 * ESP-IDF 或供应商驱动头文件。Shell 使用固定槽位持有已经创建的 XCan
 * 句柄，并在析构时统一停止、关闭和释放；通道名称复制到槽位内，避免
 * 保存解析器临时参数的指针。
 */
#ifndef XCONSOLE_SHELL_CAN_H
#define XCONSOLE_SHELL_CAN_H

#include "XConsoleShellCommand.h"
#include "XCan.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_CAN_ON

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Shell 对象前置声明；实际对象由 XConsoleShell.h 定义和管理。 */
typedef struct XConsoleShell XConsoleShell;
/** @brief Shell 会话前置声明；会话由所属 Shell 拥有，调用者不得释放。 */
typedef struct XConsoleShellSession XConsoleShellSession;

/** @brief 产品策略需要判断的 CAN 操作类型；枚举值互斥，不可按位组合。 */
typedef enum XConsoleShellCanOperation {
    XConsoleShellCanOperation_List = 0, /**< 枚举已打开控制器。 */
    XConsoleShellCanOperation_Open,     /**< 创建并打开控制器。 */
    XConsoleShellCanOperation_Close,    /**< 关闭并释放控制器。 */
    XConsoleShellCanOperation_Info,     /**< 查询配置和能力。 */
    XConsoleShellCanOperation_Status,   /**< 查询总线状态。 */
    XConsoleShellCanOperation_Start,    /**< 启动控制器。 */
    XConsoleShellCanOperation_Stop,     /**< 停止控制器。 */
    XConsoleShellCanOperation_Send,     /**< 发送数据帧。 */
    XConsoleShellCanOperation_Receive,  /**< 接收数据帧。 */
    XConsoleShellCanOperation_Recover,  /**< 执行 Bus Off 恢复。 */
    XConsoleShellCanOperation_Filter    /**< 修改接收过滤器。 */
} XConsoleShellCanOperation;

/**
 * @brief      产品 CAN 操作授权回调。
 * @param      userData 设置策略时传入的产品上下文；可为 NULL；Shell 只借用
 *             且不释放。
 * @param      session 当前命令会话的只读借用指针；不能为空且只在回调期间有效。
 * @param      channel 当前逻辑 CAN 通道的只读借用指针；不能为空且只在回调
 *             期间有效。
 * @param      operation 即将执行的互斥操作类型。
 * @return     允许操作返回 true；拒绝返回 false，拒绝时 Shell 不调用 CAN
 *             后端且不改变对应槽位。
 * @note 回调不得保存 session 或 channel 指针，也不得在回调中释放 CAN。
 */
typedef bool (*XConsoleShellCanAuthorizeFn)(
    void* userData, const XConsoleShellSession* session,
    const XCanChannel* channel, XConsoleShellCanOperation operation);

/**
 * @brief Shell 内部固定 CAN 槽位。
 * @details can 为 NULL 表示空槽；name 用于保存配置中的可选 UTF-8 通道名，
 *          避免引用命令解析缓冲。Shell 不额外分配命令级 CAN 内存。
 */
typedef struct XConsoleShellCanSlot {
    XCan* can;                                      /**< Shell 拥有的 CAN 句柄；NULL 表示空槽，调用者不得释放。 */
    uint32_t controller;                            /**< 逻辑控制器编号。 */
    uint32_t channel;                               /**< 控制器内通道编号。 */
    char name[XCONSOLE_SHELL_CAN_NAME_SIZE];        /**< NUL 结尾 UTF-8 通道名称固定缓存；最长为容量减 1 字节。 */
} XConsoleShellCanSlot;

/**
 * @brief      设置或清除 CAN 产品授权策略。
 * @param      self Shell 对象；不能为空。
 * @param      authorize 策略回调；Shell 只借用，传 NULL 清除现有策略。
 * @param      userData 产品上下文；可为 NULL；Shell 只借用且不释放；authorize
 *             为 NULL 时忽略该参数并清除已保存的上下文。
 * @return     设置成功返回 true；self 为 NULL 返回 false 且不修改原策略。
 * @note 启用 XCONSOLE_SHELL_CAN_REQUIRE_POLICY_ON 时，没有策略将拒绝会
 *       打开、启动、停止、发送或恢复总线等可能改变硬件状态的操作。
 */
bool XConsoleShell_setCanAuthorizeCallback(
    XConsoleShell* self, XConsoleShellCanAuthorizeFn authorize,
    void* userData);

/**
 * @brief      关闭并释放 Shell 持有的全部 CAN 槽位并清除授权策略。
 * @param      self Shell 对象；可为 NULL；函数不释放 self 本身。
 * @return     无。self 为 NULL 时不执行操作；后端关闭错误不通过本函数返回。
 */
void XConsoleShellCan_deinit(XConsoleShell* self);

/**
 * @brief can 根命令静态描述；子命令由各功能宏独立裁剪。
 * @details 该对象由库静态拥有，调用者只可借用其地址进行命令注册，不得
 *          修改或释放；对象在程序整个运行期间有效。
 */
extern const XConsoleCommand XConsoleShellCan_command;

#ifdef __cplusplus
}
#endif

#endif /* Shell、命令、I/O 和 CAN 均启用 */
#endif /* XCONSOLE_SHELL_CAN_H */
