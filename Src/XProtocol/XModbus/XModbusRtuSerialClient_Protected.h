#ifndef XMODBUSRTUSERIALCLIENT_PROTECTED_H
#define XMODBUSRTUSERIALCLIENT_PROTECTED_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XModbusRtuSerialClient_Protected.h
 * @brief XModbusRtuSerialClient 受保护接口（供子类和内部模块使用）
 * @details 提供内部状态机的控制和查询接口，以及辅助函数。
 *          这些接口不暴露给最终用户，仅用于模块内部和测试。
 */

/******************************************************************************************
 * 状态机控制接口
 ******************************************************************************************/

/**
 * @brief 获取当前状态机状态
 * @param client 实例指针（非NULL）
 * @return 当前状态（XModbusRtuSerialClient_State枚举值）
 */
XModbusRtuSerialClient_State XModbusRtuSerialClient_state(const XModbusRtuSerialClient* client);

/**
 * @brief 设置状态机状态（供内部使用）
 * @param client 实例指针（非NULL）
 * @param state 新状态
 * @note 状态切换时自动处理相关逻辑：
 *       - 进入Idle时自动调度下一请求
 *       - 进入WaitingForReply时启动超时定时器
 */
void XModbusRtuSerialClient_setState(XModbusRtuSerialClient* client, XModbusRtuSerialClient_State state);

/******************************************************************************************
 * 内部辅助函数
 ******************************************************************************************/

/**
 * @brief 计算帧间延迟（基于波特率）
 * @param baudRate 串口波特率
 * @return 帧间延迟（微秒），最小1750微秒
 * @note 计算公式：38500000 / baudRate，结果不小于1750微秒
 *       此公式来自Modbus规范，确保3.5个字符时间的静默间隔
 */
int XModbusRtuSerialClient_calculateInterFrameDelay(int baudRate);

/**
 * @brief 停止帧间延迟定时器
 * @param client 实例指针（非NULL）
 */
void XModbusRtuSerialClient_interFrameTimerStop(XModbusRtuSerialClient* client);

/**
 * @brief 启动帧间延迟定时器
 * @param client 实例指针（非NULL）
 * @note 定时器触发后会自动调用 startNewRequest 处理队列中的下一请求
 */
void XModbusRtuSerialClient_interFrameTimerStart(XModbusRtuSerialClient* client);

/**
 * @brief 启动广播响应延迟定时器
 * @param client 实例指针（非NULL）
 * @note 广播请求不需要等待回复，此定时器用于在发送广播后等待从站完成处理
 */
void XModbusRtuSerialClient_turnaroundTimerStart(XModbusRtuSerialClient* client);

/**
 * @brief 验证RTU帧的CRC校验
 * @param frame 帧数据指针
 * @param frameLen 帧长度（字节）
 * @return CRC校验通过返回true
 * @note 帧格式：[地址(1)][功能码(1)][数据(n)][CRC低(1)][CRC高(1)]
 *       CRC以小端序附加在帧末尾
 */
bool XModbusRtuSerialClient_validateRtuFrame(const uint8_t* frame, size_t frameLen);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSRTUSERIALCLIENT_PROTECTED_H
