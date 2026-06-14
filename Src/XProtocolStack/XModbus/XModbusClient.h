#ifndef XMODBUSCLIENT_H
#define XMODBUSCLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include "XModbusDevice.h" 
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
    XModbusDevice m_base;           ///< 继承自XModbusDevice基类
    size_t m_timeout;                  ///< 请求超时时间（毫秒）
    XTimerId m_timeoutTimer;        ///< 超时定时器ID
    XHashMap* m_poolMap;            //轮询映射表 <XTimerId,XModbusReply*>
    // 自动重连配置
    bool m_autoReconnect;           ///< 是否启用自动重连
    int16_t m_numberOfRetries;          ///< 请求重试次数
    int16_t m_reconnectAttempts;        ///< 当前重连尝试次数
    int16_t m_maxReconnectAttempts;     ///< 最大重连次数（-1表示无限）
    size_t m_reconnectInterval;        ///< 重连间隔（毫秒）
    XTimerId m_reconnectTimer;      ///< 重连定时器ID
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
XModbusReply* XModbusClient_pollReadRequest(XModbusClient* client, const XModbusDataUnit* read, int serverAddress, int pollIntervalMs);
/**
* @brief 发送Modbus写入请求
* @param client 客户端实例指针（非NULL）
* @param write 要写入的数据单元（包含寄存器类型、起始地址、值列表）
* @param serverAddress 目标从站地址
* @return 成功返回指向XModbusReply对象的指针，失败返回NULL
* @note 调用者拥有返回的XModbusReply对象，需负责其生命周期管理
*/
XModbusReply* XModbusClient_sendWriteRequest(XModbusClient* client, const XModbusDataUnit* write, int serverAddress);
XModbusReply* XModbusClient_pollWriteRequest(XModbusClient* client, const XModbusDataUnit* write, int serverAddress, int pollIntervalMs);
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
XModbusReply* XModbusClient_pollReadWriteRequest(XModbusClient* client, const XModbusDataUnit* read, const XModbusDataUnit* write, int serverAddress,int pollIntervalMs);
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
XModbusReply* XModbusClient_sendRawRequest_move_base(XModbusClient* client, const XModbusRequest* request, int serverAddress);
XModbusReply* XModbusClient_sendRawRequest_ref_base(XModbusClient* client, const XModbusRequest* request, int serverAddress);
/**
 * @brief 轮询方式发送Modbus请求并等待响应（阻塞直到成功或超时）
 * @param client        客户端实例指针（非NULL）
 * @param request       原始请求PDU
 * @param serverAddress 目标从站地址
 * @param pollIntervalMs 轮询间隔（毫秒），每隔此时间检查一次接收缓冲
 * @return 成功返回指向XModbusReply对象的指针（调用者负责释放），失败返回NULL
 */
XModbusReply* XModbusClient_pollRawRequest(XModbusClient* client,
    const XModbusRequest* request,
    int serverAddress,
    int pollIntervalMs);
XModbusReply* XModbusClient_pollRawRequest_move(XModbusClient* client,
    const XModbusRequest* request,
    int serverAddress,
    int pollIntervalMs);
XModbusReply* XModbusClient_pollRawRequest_ref(XModbusClient* client,
    XModbusRequest* request,
    int serverAddress,
    int pollIntervalMs);
/**
 * @brief 取消指定客户端的当前轮询请求（如果有正在进行的轮询）
 * @param client 客户端实例指针
 * @return 
 * @note 线程安全，可在另一个线程或信号处理函数中调用
 * @note 被取消的轮询函数将返回 NULL，且不会产生 XModbusReply 对象
 */
bool XModbusClient_cancelPoll(XModbusClient* client, XModbusReply*reply);

/**
* @brief 获取当前请求超时时间
* @param client 客户端实例指针（非NULL）
* @return 超时时间（毫秒）
*/
size_t XModbusClient_timeout(const XModbusClient* client);

/**
* @brief 设置请求超时时间
* @param client 客户端实例指针（非NULL）
* @param newTimeout 新的超时时间（毫秒）
* @note 设置后会触发 timeoutChanged 信号
*/
void XModbusClient_setTimeout(XModbusClient* client, size_t newTimeout);

/**
* @brief 获取当前请求重试次数
* @param client 客户端实例指针（非NULL）
* @return 重试次数
*/
int16_t XModbusClient_numberOfRetries(const XModbusClient* client);

/**
* @brief 设置请求重试次数
* @param client 客户端实例指针（非NULL）
* @param number 新的重试次数
*/
void XModbusClient_setNumberOfRetries(XModbusClient* client, uint8_t number);

/******************************************************************************************
 * 自动重连配置 API
 ******************************************************************************************/

/**
* @brief 检查是否启用自动重连
* @param client 客户端实例指针（非NULL）
* @return true表示启用自动重连
*/
bool XModbusClient_autoReconnect(const XModbusClient* client);

/**
* @brief 设置是否启用自动重连
* @param client 客户端实例指针（非NULL）
* @param enabled 是否启用
*/
void XModbusClient_setAutoReconnect(XModbusClient* client, bool enabled);

/**
* @brief 获取重连间隔
* @param client 客户端实例指针（非NULL）
* @return 重连间隔（毫秒）
*/
size_t XModbusClient_reconnectInterval(const XModbusClient* client);

/**
* @brief 设置重连间隔
* @param client 客户端实例指针（非NULL）
* @param interval 重连间隔（毫秒）
*/
void XModbusClient_setReconnectInterval(XModbusClient* client, size_t interval);

/**
* @brief 获取最大重连次数
* @param client 客户端实例指针（非NULL）
* @return 最大重连次数（-1表示无限）
*/
int16_t XModbusClient_maxReconnectAttempts(const XModbusClient* client);

/**
* @brief 设置最大重连次数
* @param client 客户端实例指针（非NULL）
* @param attempts 最大重连次数（-1表示无限）
*/
void XModbusClient_setMaxReconnectAttempts(XModbusClient* client, int16_t attempts);

/**
* @brief 获取当前重连尝试次数
* @param client 客户端实例指针（非NULL）
* @return 当前重连尝试次数
*/
int16_t XModbusClient_reconnectAttempts(const XModbusClient* client);

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