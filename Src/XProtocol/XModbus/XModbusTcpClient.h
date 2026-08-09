#ifndef XMODBUSTCPCLIENT_H
#define XMODBUSTCPCLIENT_H

#include "XModbusClient.h"
#include "XModbusPdu.h"
#include "XTcpSocket.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XModbusTcpClient.h
 * @brief Modbus TCP客户端（对齐Qt6 QModbusTcpClient）
 * @details 实现基于TCP/IP通信的Modbus TCP主站功能
 *
 * @par 功能特性
 * - 支持标准Modbus TCP协议（MBAP头部 + PDU）
 * - 支持多请求并行处理
 * - 自动管理事务标识符
 * - 支持超时重试机制
 *
 * @par 协议说明
 * Modbus TCP帧结构（MBAP头部 + PDU）：
 * | 字节 | 长度 | 说明 |
 * |------|------|------|
 * | 0-1  | 2    | 事务标识符 |
 * | 2-3  | 2    | 协议标识符（固定0x0000） |
 * | 4-5  | 2    | 长度（后续字节数） |
 * | 6    | 1    | 单元标识符 |
 * | 7+   | N    | Modbus PDU |
 *
/**
 * @par 使用示例
 * @code
 * // 创建TCP客户端
 * XModbusTcpClient* client = XModbusTcpClient_create();
 *
 * // 设置连接参数
 * XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
 *     XModbusDevice_NetworkAddressParameter, XVariant_create_utf8_str("192.168.1.100"));
 * XModbusDevice_setConnectionParameter_ref((XModbusDevice*)client,
 *     XModbusDevice_NetworkPortParameter, XVariant_create_int(502));
 *
 * // 连接设备
 * if (XModbusDevice_connectDevice((XModbusDevice*)client)) {
 *     // 发送请求
 *     XModbusDataUnit data;
 *     XModbusDataUnit_init(&data, XModbusHoldingRegisters, 0, 10);
 *     XModbusReply* reply = XModbusClient_sendReadRequest((XModbusClient*)client, &data, 1);
 * }
 *
 * // 清理
 * XModbusTcpClient_deleteLater(client);
 * @endcode
 */

XCLASS_DEFINE_BEGING(XModbusTcpClient)
XCLASS_DEFINE_EXTEND_END(XModbusTcpClient, XModbusClient)

/**
 * @brief Modbus TCP客户端结构体
 * @details 继承自XModbusClient，实现TCP/IP通信的主站功能
 *
 * @par 成员说明
 * | 成员 | 类型 | 说明 |
 * |------|------|------|
 * | m_base | XModbusClient | 基类，继承自XModbusClient |
 * | m_transactionId | uint16_t | 当前事务标识符（自动递增） |
 * | m_pendingRequests | XMap* | 待响应的请求映射表（事务ID -> Reply） |
 * | m_receiveBuffer | XByteArray* | 接收缓冲区 |
 */
typedef struct XModbusTcpClient {
    XModbusClient m_base;               ///< 继承自XModbusClient
    uint16_t m_transactionId;           ///< 当前事务标识符（自动递增）
    XHashMap* m_pendingRequests;        ///< 待响应的请求映射表 (uint16_t -> XModbusTcpPendingRequest*)
    XHashMap* m_timerMap;               ///< timerId -> transactionId（反向映射）
    XByteArray* m_receiveBuffer;        ///< 接收缓冲区
    XByteArray* m_requestData;          //请求缓冲区
} XModbusTcpClient;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

 /**
  * @brief 初始化XModbusTcpClient的虚函数表
  * @return 初始化完成的虚函数表指针
  * @note 该函数是线程安全的，多次调用返回同一虚表实例
  */
XVtable* XModbusTcpClient_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XModbusTcpClient实例
 * @return 成功返回指向新分配XModbusTcpClient对象的指针，失败返回NULL
 * @note 返回的对象必须通过 XObject_deleteLater 释放
 * @par 内存管理
 * - 创建时会自动创建关联的XTcpSocket对象
 * - 删除时会自动释放关联的XTcpSocket对象
 */
XModbusTcpClient* XModbusTcpClient_create(void);

/**
 * @brief 初始化一个已分配的XModbusTcpClient实例
 * @param client 待初始化的XModbusTcpClient对象指针（非NULL）
 * @note 该函数会初始化基类成员、创建TCP套接字、设置默认参数
 * @par 默认参数
 * - 超时时间：1000毫秒（继承自基类）
 * - 重试次数：3次（继承自基类）
 * - 事务标识符：从1开始
 */
/**
 * @brief 初始化TCP客户端实例
 * @param client 待初始化的客户端指针（非NULL）
 */
void XModbusTcpClient_init(XModbusTcpClient* client);

/******************************************************************************************
 * 继承自父类的API（使用宏定义）
 ******************************************************************************************/

 /**
  * @brief 发送Modbus读取请求
  * @param client XModbusTcpClient实例指针
  * @param read 要读取的数据单元
  * @param serverAddress 目标从站地址（单元标识符）
  * @return 成功返回XModbusReply指针，失败返回NULL
  */
#define XModbusTcpClient_sendReadRequest      XModbusClient_sendReadRequest

/**
 * @brief 发送Modbus写入请求
 * @param client XModbusTcpClient实例指针
 * @param write 要写入的数据单元
 * @param serverAddress 目标从站地址（单元标识符）
 * @return 成功返回XModbusReply指针，失败返回NULL
 */
#define XModbusTcpClient_sendWriteRequest     XModbusClient_sendWriteRequest

/**
 * @brief 发送Modbus读写组合请求（功能码0x17）
 * @param client XModbusTcpClient实例指针
 * @param read 读取部分的数据单元
 * @param write 写入部分的数据单元
 * @param serverAddress 目标从站地址（单元标识符）
 * @return 成功返回XModbusReply指针，失败返回NULL
 */
#define XModbusTcpClient_sendReadWriteRequest XModbusClient_sendReadWriteRequest

/**
 * @brief 获取请求超时时间
 * @param client XModbusTcpClient实例指针
 * @return 超时时间（毫秒）
 */
#define XModbusTcpClient_timeout              XModbusClient_timeout

/**
 * @brief 设置请求超时时间
 * @param client XModbusTcpClient实例指针
 * @param newTimeout 新的超时时间（毫秒）
 */
#define XModbusTcpClient_setTimeout           XModbusClient_setTimeout

/**
 * @brief 获取请求重试次数
 * @param client XModbusTcpClient实例指针
 * @return 重试次数
 */
#define XModbusTcpClient_numberOfRetries      XModbusClient_numberOfRetries

/**
 * @brief 设置请求重试次数
 * @param client XModbusTcpClient实例指针
 * @param number 新的重试次数
 */
#define XModbusTcpClient_setNumberOfRetries   XModbusClient_setNumberOfRetries

/******************************************************************************************
 * 内存管理宏
 ******************************************************************************************/

 /**
  * @brief 析构函数（延迟删除）
  * @param obj XModbusTcpClient实例指针
  * @note 将对象加入待删除队列，在事件循环中删除
  */
#define XModbusTcpClient_deinitLater          XModbusClient_deinitLater

/**
 * @brief 删除对象（延迟删除）
 * @param obj XModbusTcpClient实例指针
 * @note 将对象加入待删除队列，在事件循环中删除
 */
#define XModbusTcpClient_deleteLater          XModbusClient_deleteLater

#ifdef __cplusplus
}
#endif

#endif // XMODBUSTCPCLIENT_H

