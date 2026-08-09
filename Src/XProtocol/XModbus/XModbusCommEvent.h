#ifndef XMODBUSCOMMEVENT_H
#define XMODBUSCOMMEVENT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XModbusCommEvent.h
 * @brief Modbus通信事件（对齐Qt6 QModbusCommEvent）
 * @details 实现Modbus通信事件（FC07/FC0B/FC0C）相关的标志位和操作。
 *          用于管理Modbus从站的通信事件计数器及事件日志。
 *
 * @par 功能特性
 * - 发送标志管理（异常响应、设备忙等）
 * - 接收标志管理（通信错误、超限等）
 * - 事件字节组合与测试
 * - 支持Modbus诊断功能（FC08）
 *
 * @par 相关功能码
 * | 功能码 | 说明 |
 * |--------|------|
 * | FC07   | 读取异常状态 |
 * | FC0B   | 获取通信事件计数器 |
 * | FC0C   | 获取通信事件日志 |
 * | FC08   | 诊断（子功能码） |
 *
 * @par 使用示例
 * @code
 * // 创建通信事件
 * XModbusCommEvent event = XModbusCommEvent_create(XModbusCommEvent_SentEvent);
 *
 * // 设置发送标志
 * XModbusCommEvent_orWithSendFlag(&event, XModbusCommEvent_ReadExceptionSent);
 *
 * // 测试标志
 * if (XModbusCommEvent_testSendFlag(&event, XModbusCommEvent_ReadExceptionSent)) {
 *     // 处理异常发送事件
 * }
 *
 * // 判断事件类型
 * if (XModbusCommEvent_isSentEvent(&event)) {
 *     // 属于发送事件
 * }
 * @endcode
 */

/**
 * @brief Modbus通信事件发送标志枚举
 * @details 定义发送事件相关的标志位，用于记录发送过程中的异常情况
 *
 * @par 标志位说明
 * | 标志位 | 值 | 说明 |
 * |--------|------|------|
 * | ReadExceptionSent | 0x01 | 发送了异常响应 |
 * | ServerAbortExceptionSent | 0x02 | 服务器中止异常已发送 |
 * | ServerDeviceBusy | 0x04 | 服务器设备忙 |
 * | ServerProgramNAKExceptionSent | 0x08 | 服务器程序NAK异常已发送 |
 * | WriteTimeoutErrorOccurred | 0x10 | 写入超时错误发生 |
 * | CurrentlyInListenOnlyMode | 0x20 | 当前处于仅监听模式 |
 */
typedef enum {
    XModbusCommEvent_ReadExceptionSent = 0x01,                ///< 发送了异常响应
    XModbusCommEvent_ServerAbortExceptionSent = 0x02,         ///< 服务器中止异常已发送
    XModbusCommEvent_ServerDeviceBusy = 0x04,                 ///< 服务器设备忙
    XModbusCommEvent_ServerProgramNAKExceptionSent = 0x08,    ///< 服务器程序NAK异常已发送
    XModbusCommEvent_WriteTimeoutErrorOccurred = 0x10,        ///< 写入超时错误发生
    XModbusCommEvent_CurrentlyInListenOnlyMode = 0x20,        ///< 当前处于仅监听模式
} XModbusCommEvent_SendFlag;

/**
 * @brief Modbus通信事件接收标志枚举
 * @details 定义接收事件相关的标志位，用于记录接收过程中的异常情况
 *
 * @par 标志位说明
 * | 标志位 | 值 | 说明 |
 * |--------|------|------|
 * | CommunicationError | 0x02 | 通信错误 |
 * | CharacterOverrun | 0x10 | 字符超限 |
 * | ReceiveListenOnlyMode | 0x20 | 接收时处于仅监听模式 |
 * | BroadcastReceived | 0x40 | 接收到广播消息 |
 */
typedef enum {
    XModbusCommEvent_CommunicationError = 0x02,               ///< 通信错误
    XModbusCommEvent_CharacterOverrun = 0x10,                 ///< 字符超限
    XModbusCommEvent_ReceiveListenOnlyMode = 0x20,            ///< 接收时处于仅监听模式
    XModbusCommEvent_BroadcastReceived = 0x40,                ///< 接收到广播消息
} XModbusCommEvent_ReceiveFlag;

/**
 * @brief Modbus通信事件字节枚举
 * @details 定义事件字节的取值，由Bit0-Bit2和Bit6-Bit7组成：
 *          - Bit0-Bit2: 事件类型（0=重启, 1=已发送, 2=已接收）
 *          - Bit6: 发送事件标志
 *          - Bit7: 接收事件标志
 *
 * @par 事件字节格式
 * | Bit7 | Bit6 | Bit5-3 | Bit2-0 | 说明 |
 * |------|------|--------|--------|------|
 * | 0    | 0    | 000    | 001    | 通信重启 |
 * | 0    | 1    | 000    | 000    | 发送事件 |
 * | 1    | 0    | 000    | 000    | 接收事件 |
 * | 0    | 1    | 000    | 100    | 进入仅监听模式 |
 */
typedef enum {
    XModbusCommEvent_InitiatedCommunicationRestart = 0x00,    ///< 已启动通信重启
    XModbusCommEvent_EnteredListenOnlyMode = 0x04,            ///< 已进入仅监听模式
    XModbusCommEvent_SentEvent = 0x40,                        ///< 发送事件标志
    XModbusCommEvent_ReceiveEvent = 0x80,                     ///< 接收事件标志
} XModbusCommEvent_EventByte;

/**
 * @struct XModbusCommEvent
 * @brief Modbus通信事件核心结构体
 * @details 封装Modbus通信事件的相关信息，通过事件字节存储事件类型和标志位
 */
typedef struct XModbusCommEvent {
    uint8_t m_eventByte;  ///< 事件字节，存储事件类型和标志位
} XModbusCommEvent;

/******************************************************************************************
 * 创建/初始化接口
 ******************************************************************************************/

/**
 * @brief 创建通信事件实例
 * @param byte 事件字节值
 * @return 初始化完成的XModbusCommEvent实例
 */
XModbusCommEvent XModbusCommEvent_create(XModbusCommEvent_EventByte byte);

/**
 * @brief 从uint8值创建通信事件实例
 * @param byte 原始字节值
 * @return 初始化完成的XModbusCommEvent实例
 */
XModbusCommEvent XModbusCommEvent_fromUint8(uint8_t byte);

/******************************************************************************************
 * 查询接口
 ******************************************************************************************/

/**
 * @brief 将通信事件转换为uint8值
 * @param event XModbusCommEvent指针
 * @return 事件字节的uint8值
 */
uint8_t XModbusCommEvent_toUint8(const XModbusCommEvent* event);

/**
 * @brief 将通信事件转换为事件字节枚举
 * @param event XModbusCommEvent指针
 * @return 事件字节枚举值
 */
XModbusCommEvent_EventByte XModbusCommEvent_toEventByte(const XModbusCommEvent* event);

/**
 * @brief 设置事件字节
 * @param event XModbusCommEvent指针（非NULL）
 * @param byte 要设置的事件字节值
 */
void XModbusCommEvent_setEventByte(XModbusCommEvent* event, XModbusCommEvent_EventByte byte);

/******************************************************************************************
 * 标志位操作接口
 ******************************************************************************************/

/**
 * @brief 设置发送标志位（按位或）
 * @param event XModbusCommEvent指针（非NULL）
 * @param flag 要设置的发送标志
 * @return 返回event指针自身，支持链式调用
 */
XModbusCommEvent* XModbusCommEvent_orWithSendFlag(XModbusCommEvent* event, XModbusCommEvent_SendFlag flag);

/**
 * @brief 设置接收标志位（按位或）
 * @param event XModbusCommEvent指针（非NULL）
 * @param flag 要设置的接收标志
 * @return 返回event指针自身，支持链式调用
 */
XModbusCommEvent* XModbusCommEvent_orWithReceiveFlag(XModbusCommEvent* event, XModbusCommEvent_ReceiveFlag flag);

/**
 * @brief 测试指定发送标志位是否被设置
 * @param event XModbusCommEvent指针
 * @param flag 要测试的发送标志
 * @return 如果标志位被设置返回true，否则返回false
 */
bool XModbusCommEvent_testSendFlag(const XModbusCommEvent* event, XModbusCommEvent_SendFlag flag);

/**
 * @brief 测试指定接收标志位是否被设置
 * @param event XModbusCommEvent指针
 * @param flag 要测试的接收标志
 * @return 如果标志位被设置返回true，否则返回false
 */
bool XModbusCommEvent_testReceiveFlag(const XModbusCommEvent* event, XModbusCommEvent_ReceiveFlag flag);

/******************************************************************************************
 * 类型判断接口
 ******************************************************************************************/

/**
 * @brief 判断是否为发送事件
 * @param event XModbusCommEvent指针
 * @return 如果是发送事件返回true，否则返回false
 */
bool XModbusCommEvent_isSentEvent(const XModbusCommEvent* event);

/**
 * @brief 判断是否为接收事件
 * @param event XModbusCommEvent指针
 * @return 如果是接收事件返回true，否则返回false
 */
bool XModbusCommEvent_isReceiveEvent(const XModbusCommEvent* event);

/**
 * @brief 判断是否处于仅监听模式
 * @param event XModbusCommEvent指针
 * @return 如果处于仅监听模式返回true，否则返回false
 */
bool XModbusCommEvent_isListenOnlyMode(const XModbusCommEvent* event);

/**
 * @brief 判断是否为重启事件
 * @param event XModbusCommEvent指针
 * @return 如果是重启事件返回true，否则返回false
 */
bool XModbusCommEvent_isRestartEvent(const XModbusCommEvent* event);

/******************************************************************************************
 * 组合接口
 ******************************************************************************************/

/**
 * @brief 组合事件字节和发送标志
 * @param byte 事件字节
 * @param flag 发送标志
 * @return 组合后的事件字节值
 */
XModbusCommEvent_EventByte XModbusCommEvent_combineEventByte(XModbusCommEvent_EventByte byte, XModbusCommEvent_SendFlag flag);

/**
 * @brief 组合事件字节和接收标志
 * @param byte 事件字节
 * @param flag 接收标志
 * @return 组合后的事件字节值
 */
XModbusCommEvent_EventByte XModbusCommEvent_combineEventByteWithReceive(XModbusCommEvent_EventByte byte, XModbusCommEvent_ReceiveFlag flag);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSCOMMEVENT_H
