#ifndef XMODBUSRTUSERIALCLIENT_H
#define XMODBUSRTUSERIALCLIENT_H

#include "XModbusClient.h"
#include "XModbusPdu.h"
#include "XSerialPort.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XModbusRtuSerialClient.h
 * @brief Modbus RTU串行主站客户端（对齐Qt6 QModbusRtuSerialClient）
 * @details 实现基于串口通信的Modbus RTU主站功能。
 *          内部使用有限状态机管理请求生命周期：Idle -> WaitingForReply -> ProcessReply。
 *
 * @par 行为对齐（Qt6 QModbusRtuSerialClient）
 * - 队列驱动：请求入队后按序处理（FIFO）
 * - 帧间延迟：自动根据波特率计算，或由用户指定
 * - 广播处理：使用 turnaroundDelay（100-200ms）等待广播完成后发送下一帧
 * - 超时重试：支持可配置的重试次数和超时时间
 * - CRC校验：接收帧自动校验CRC16
 * - 请求/响应匹配：验证从站地址、功能码一致性
 *
 * @par 新增特性（超越Qt6）
 * - 自动重连：串口断开后自动尝试重新连接
 * - 轮询模式：支持阻塞式轮询请求
 * - 中间错误：记录CRC错误等中间错误到Reply
 *
 * @par 使用示例
 * @code
 * XModbusRtuSerialClient* client = XModbusRtuSerialClient_create();
 * XSerialPort* port = XModbusRtuSerialClient_serialPort(client);
 * XSerialPort_setPortName(port, "COM1");
 * XSerialPort_setBaudRate(port, 9600);
 *
 * if (XModbusDevice_connectDevice((XModbusDevice*)client)) {
 *     XModbusDataUnit data;
 *     XModbusDataUnit_init(&data, XModbusHoldingRegisters, 0, 10);
 *     XModbusReply* reply = XModbusClient_sendReadRequest((XModbusClient*)client, &data, 1);
 * }
 * XModbusRtuSerialClient_deleteLater(client);
 * @endcode
 */

XCLASS_DEFINE_BEGING(XModbusRtuSerialClient)
XCLASS_DEFINE_EXTEND_END(XModbusRtuSerialClient, XModbusClient)

/**
 * @brief RTU状态机枚举
 */
typedef enum {
    XModbusRtuSerialClient_Idle = 0,           ///< 空闲，可发送下一请求
    XModbusRtuSerialClient_WaitingForReply,     ///< 已发送请求，等待响应
    XModbusRtuSerialClient_ProcessReply         ///< 正在处理接收到的响应
} XModbusRtuSerialClient_State;

/**
 * @brief Modbus RTU串行主站结构体
 * @details 继承自XModbusClient，实现RTU串口通信的主站功能。
 *          优化内存布局以适应嵌入式环境。
 *
 * @par 内存布局
 * 字段按类型分组排列以减小填充开销：
 *  1. 基类（XModbusClient）
 *  2. 状态/标志（uint8_t x 4）
 *  3. 定时器（XTimerId）
 *  4. 配置参数（int x 2）
 *  5. 从站地址（uint8_t）
 *  6. 指针/引用（XModbusReply* + XByteArray* x 2 + XQueueBase*）
 *  7. 计数（int64_t）
 */
typedef struct XModbusRtuSerialClient {
    XModbusClient m_base;               ///< 继承自XModbusClient

    /* --- 状态机 & 标志（紧凑排列） --- */
    uint8_t m_state;                    ///< 状态机当前状态（XModbusRtuSerialClient_State）
    uint8_t m_retryCount;               ///< 当前请求已重试次数
    uint8_t m_waitingForTurnaround;     ///< 是否正在等待广播响应延迟（0/1）

    /* --- 定时器 --- */
    XTimerId m_interFrameTimer;         ///< 帧间延迟定时器ID

    /* --- 配置参数 --- */
    int m_interFrameDelay;              ///< 帧间延迟（微秒），0=自动计算
    int m_turnaroundDelay;              ///< 广播响应延迟（毫秒），默认100ms

    /* --- 当前请求状态 --- */
    uint8_t m_currentServerAddress;     ///< 当前请求的从站地址

    /* --- 指针/引用 --- */
    XModbusReply* m_currentReply;       ///< 当前正在等待响应的Reply对象
    XByteArray* m_receiveBuffer;        ///< 接收缓冲区（累积接收数据）
    XByteArray* m_requestAdu;           ///< 当前请求的ADU帧数据（用于重发）
    XQueueBase* m_queue;                ///< 请求队列（元素类型：XModbusReply*）

    int64_t m_bytesWritten;             ///< 已写入串口的字节数（用于跟踪写完成）
} XModbusRtuSerialClient;

/******************************************************************************************
 * 类初始化/实例创建
 ******************************************************************************************/

/**
 * @brief 初始化虚函数表
 * @return 完成初始化的虚函数表指针
 */
XVtable* XModbusRtuSerialClient_class_init(void);

/**
 * @brief 创建并初始化实例（堆分配）
 * @return 新实例指针，失败返回NULL
 */
XModbusRtuSerialClient* XModbusRtuSerialClient_create(void);

/**
 * @brief 初始化已分配的实例
 * @param client 待初始化的实例指针（非NULL）
 */
void XModbusRtuSerialClient_init(XModbusRtuSerialClient* client);

/******************************************************************************************
 * RTU特定配置接口
 ******************************************************************************************/

/**
 * @brief 获取帧间延迟（微秒）
 * @param client 实例指针
 * @return 帧间延迟（微秒），0表示自动计算
 */
int XModbusRtuSerialClient_interFrameDelay(const XModbusRtuSerialClient* client);

/**
 * @brief 设置帧间延迟（微秒）
 * @param client 实例指针
 * @param microseconds 帧间延迟（微秒），-1或小于自动计算值时使用自动计算值
 * @note 自动计算值基于波特率：38500000/baudRate，最小1750微秒
 *       设置后不会影响已建立的连接
 */
void XModbusRtuSerialClient_setInterFrameDelay(XModbusRtuSerialClient* client, int microseconds);

/**
 * @brief 获取广播响应延迟（毫秒）
 * @param client 实例指针
 * @return 广播响应延迟（毫秒），默认100ms
 */
int XModbusRtuSerialClient_turnaroundDelay(const XModbusRtuSerialClient* client);

/**
 * @brief 设置广播响应延迟（毫秒）
 * @param client 实例指针
 * @param turnaroundDelay 延迟时间（毫秒），典型值100-200ms
 * @note 广播请求后，从站需要时间处理，此延迟防止立即发送下一帧
 */
void XModbusRtuSerialClient_setTurnaroundDelay(XModbusRtuSerialClient* client, int turnaroundDelay);

/******************************************************************************************
 * 串口对象访问
 ******************************************************************************************/

/**
 * @brief 获取关联的串口对象
 * @param client 实例指针
 * @return 串口对象指针，无串口时返回NULL
 * @note 返回的串口由客户端管理，调用者不应释放
 */
XSerialPort* XModbusRtuSerialClient_serialPort(const XModbusRtuSerialClient* client);

/******************************************************************************************
 * 继承自父类的API宏
 ******************************************************************************************/

#define XModbusRtuSerialClient_sendRawRequest       XModbusClient_sendRawRequest_base
#define XModbusRtuSerialClient_sendReadRequest      XModbusClient_sendReadRequest
#define XModbusRtuSerialClient_sendWriteRequest     XModbusClient_sendWriteRequest
#define XModbusRtuSerialClient_sendReadWriteRequest XModbusClient_sendReadWriteRequest
#define XModbusRtuSerialClient_timeout              XModbusClient_timeout
#define XModbusRtuSerialClient_setTimeout           XModbusClient_setTimeout
#define XModbusRtuSerialClient_numberOfRetries      XModbusClient_numberOfRetries
#define XModbusRtuSerialClient_setNumberOfRetries   XModbusClient_setNumberOfRetries
#define XModbusRtuSerialClient_open_base            XModbusDevice_open_base
#define XModbusRtuSerialClient_close_base           XModbusDevice_close_base
#define XModbusRtuSerialClient_deinitLater          XModbusClient_deinitLater
#define XModbusRtuSerialClient_deleteLater          XModbusClient_deleteLater

#ifdef __cplusplus
}
#endif

#endif // XMODBUSRTUSERIALCLIENT_H
