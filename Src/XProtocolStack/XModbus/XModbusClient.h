#ifndef XMODBUSCLIENT_H
#define XMODBUSCLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "XModbusDevice.h" // Inherits from XModbusDevice
#include "XModbusReply.h"
#include "XModbusDataUnit.h"
#include "XModbusPdu.h"

#ifdef __cplusplus
extern "C" {
#endif
XCLASS_DEFINE_BEGING(XModbusClient)
XCLASS_DEFINE_ENUM(XModbusClient, ProcessResponse) = XCLASS_VTABLE_GET_SIZE(XModbusDevice),
XCLASS_DEFINE_ENUM(XModbusClient, ProcessPrivateResponse),
XCLASS_DEFINE_ENUM(XModbusClient, SendRawRequest),    ///< 发送原始请求（虚函数，子类重写）
XCLASS_DEFINE_END(XModbusClient)
/**
 * @brief Modbus客户端核心结构体（继承自XModbusDevice）
 * @details 封装了Modbus主站/客户端的核心功能，用于向从站发送读写请求。
 *          这是一个抽象基类，具体的通信后端（如RTU/TCP）需由子类实现。
 */
typedef struct XModbusClient {
    XModbusDevice m_base;  ///< 继承自XModbusDevice基类
    int m_timeout;         ///< 请求超时时间（毫秒）
    int m_numberOfRetries; ///< 请求重试次数
} XModbusClient;


/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

/**
 * @brief 初始化XModbusClient的虚函数表
 * @return 指向初始化完成的XVtable的指针
 * @note 该函数是线程安全的，多次调用返回同一虚表实例
 */
XVtable* XModbusClient_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XModbusClient实例
 * @return 成功返回指向新分配XModbusClient对象的指针，失败返回NULL
 * @note 返回的对象必须通过 XObject_deleteLater 或 XModbusClient_delete_base 释放
 */
XModbusClient* XModbusClient_create(void);

/**
 * @brief 初始化一个已分配的XModbusClient实例
 * @param client 待初始化的XModbusClient对象指针（非NULL）
 * @note 该函数会初始化基类成员、超时和重试次数等默认值
 */
void XModbusClient_init(XModbusClient* client);

/******************************************************************************************
 * Public API (严格对齐 QModbusClient)
 ******************************************************************************************/

 /**
 * @brief 发送Modbus读取请求
 * @param client 客户端实例指针（非NULL）
 * @param read 要读取的数据单元（包含寄存器类型、起始地址、数量）
 * @param serverAddress 目标从站地址
 * @return 成功返回指向XModbusReply对象的指针，失败返回NULL
 * @note 调用者拥有返回的XModbusReply对象，需负责其生命周期管理
 */
XModbusReply* XModbusClient_sendReadRequest(XModbusClient* client, const XModbusDataUnit* read, int serverAddress);

/**
* @brief 发送Modbus写入请求
* @param client 客户端实例指针（非NULL）
* @param write 要写入的数据单元（包含寄存器类型、起始地址、值列表）
* @param serverAddress 目标从站地址
* @return 成功返回指向XModbusReply对象的指针，失败返回NULL
* @note 调用者拥有返回的XModbusReply对象，需负责其生命周期管理
*/
XModbusReply* XModbusClient_sendWriteRequest(XModbusClient* client, const XModbusDataUnit* write, int serverAddress);

/**
* @brief 发送Modbus读写组合请求（仅支持0x17功能码）
* @param client 客户端实例指针（非NULL）
* @param read 读取部分的数据单元
* @param write 写入部分的数据单元
* @param serverAddress 目标从站地址
* @return 成功返回指向XModbusReply对象的指针，失败返回NULL
* @note 调用者拥有返回的XModbusReply对象，需负责其生命周期管理
*/
XModbusReply* XModbusClient_sendReadWriteRequest(XModbusClient* client, const XModbusDataUnit* read, const XModbusDataUnit* write, int serverAddress);

/**
* @brief 发送原始Modbus请求PDU（虚函数，通过虚函数表调用）
* @param client 客户端实例指针（非NULL）
* @param request 原始请求PDU
* @param serverAddress 目标从站地址
* @return 成功返回指向XModbusReply对象的指针，失败返回NULL
* @note 调用者拥有返回的XModbusReply对象，需负责其生命周期管理
* @note 子类必须重写此虚函数，实现协议特定的帧构建和发送逻辑
*/
XModbusReply* XModbusClient_sendRawRequest_base(XModbusClient* client, const XModbusRequest* request, int serverAddress);

/**
* @brief 辅助函数：创建并初始化 Reply 对象
* @param client 客户端实例指针（非NULL）
* @param request 原始请求PDU
* @param serverAddress 目标从站地址
* @return 成功返回指向XModbusReply对象的指针，失败返回NULL
* @note 供子类在实现 sendRawRequest 时调用，用于创建 Reply 对象
*/
XModbusReply* XModbusClient_createReply(XModbusClient* client, const XModbusRequest* request, int serverAddress);

/**
* @brief 获取当前请求超时时间
* @param client 客户端实例指针（非NULL）
* @return 超时时间（毫秒）
*/
int XModbusClient_timeout(const XModbusClient* client);

/**
* @brief 设置请求超时时间
* @param client 客户端实例指针（非NULL）
* @param newTimeout 新的超时时间（毫秒）
* @note 设置后会触发 timeoutChanged 信号
*/
void XModbusClient_setTimeout(XModbusClient* client, int newTimeout);

/**
* @brief 获取当前请求重试次数
* @param client 客户端实例指针（非NULL）
* @return 重试次数
*/
int XModbusClient_numberOfRetries(const XModbusClient* client);

/**
* @brief 设置请求重试次数
* @param client 客户端实例指针（非NULL）
* @param number 新的重试次数
*/
void XModbusClient_setNumberOfRetries(XModbusClient* client, int number);

/******************************************************************************************
 * 信号接口
 ******************************************************************************************/

 /**
 * @brief 触发 timeoutChanged 信号
 * @param client 客户端实例指针（非NULL）
 * @param newTimeout 新的超时值
 * @return 信号标识
 */
void* XModbusClient_timeoutChanged_signal(XModbusClient* client, int newTimeout);
#define XModbusClient_deleteLater		XObject_deleteLater
#define XModbusClient_deinitLater		XObject_deinitLater
//#define XModbusClient_move_base			XObject_move_base
//#define XModbusClient_copy_base			XObject_copy_base
#ifdef __cplusplus
}
#endif

#endif // XMODBUSCLIENT_H