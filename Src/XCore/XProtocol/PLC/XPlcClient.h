#ifndef XPLCCLIENT_H
#define XPLCCLIENT_H

#include "XPlc_config.h"
#include <stdint.h>
#include <stdbool.h>
#include "XPlcDevice.h"
#include "XPlcReply.h"
#include "XByteArray.h"
#include "XHashMap.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XPLC_CORE_ON

/**
 * @file XPlcClient.h
 * @brief PLC 客户端核心结构体（继承自XPlcDevice，对齐 XModbusClient 层位）
 * @details 封装 PLC 主站/客户端的核心功能：超时/重试/自动重连配置、
 *          异步 Reply 创建、超时与重连定时器管理，以及两个协议无关虚槽
 *          [SendRequest, ProcessResponse]。这是一个抽象基类，具体的
 *          协议后端（如 XS7TcpClient）负责实现两虚槽与 open/close。
 *
 * @par 虚槽说明
 * - SendRequest：子类负责封帧（如 TPKT/COTP）、发送、登记 pending 并起超时定时器
 * - ProcessResponse：收到完整协议 PDU（已剥传输层头）后由子类解 data 并回填 Reply
 *
 * @note 基类两虚槽默认实现返回 false（抽象），对外入口一律经 *_base 查表分派
 */

 /******************************************************************************************
  * 虚函数表枚举定义
  ******************************************************************************************/

XCLASS_DEFINE_BEGING(XPlcClient)
XCLASS_DEFINE_ENUM(XPlcClient, SendRequest) = XCLASS_VTABLE_GET_SIZE(XPlcDevice),
XCLASS_DEFINE_ENUM(XPlcClient, ProcessResponse),
XCLASS_DEFINE_END(XPlcClient)

/******************************************************************************************
 * 结构体定义
 ******************************************************************************************/

 /**
  * @brief PLC 客户端核心结构体
  * @details 继承自XPlcDevice，附加超时/重试/自动重连配置与定时器
  *
  * @par 成员说明
  * | 成员 | 类型 | 说明 |
  * |------|------|------|
  * | m_base | XPlcDevice | 继承自XPlcDevice基类 |
  * | m_timeout | size_t | 请求超时时间（毫秒），默认3000 |
  * | m_timeoutTimer | XTimerId | 当前in-flight请求的超时定时器 |
  * | m_poolMap | XHashMap* | 轮询映射（首期保留字段，poll API 后续接） |
  * | m_autoReconnect | bool | 是否启用自动重连 |
  * | m_numberOfRetries | int16_t | 请求超时重试次数 |
  * | m_reconnectAttempts | int16_t | 当前重连尝试次数 |
  * | m_maxReconnectAttempts | int16_t | 最大重连次数（-1表示无限） |
  * | m_reconnectInterval | size_t | 重连间隔（毫秒） |
  * | m_reconnectTimer | XTimerId | 重连定时器ID |
  */
typedef struct XPlcClient {
    XPlcDevice m_base;                ///< 继承自XPlcDevice基类
    size_t m_timeout;                 ///< 请求超时时间（毫秒）
    XTimerId m_timeoutTimer;          ///< 超时定时器ID
    XHashMap* m_poolMap;              ///< 轮询映射表（首期保留字段）
    // 自动重连配置
    bool m_autoReconnect;             ///< 是否启用自动重连
    int16_t m_numberOfRetries;        ///< 请求超时重试次数
    int16_t m_reconnectAttempts;      ///< 当前重连尝试次数
    int16_t m_maxReconnectAttempts;   ///< 最大重连次数（-1表示无限）
    size_t m_reconnectInterval;       ///< 重连间隔（毫秒）
    XTimerId m_reconnectTimer;        ///< 重连定时器ID
} XPlcClient;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

 /**
  * @brief 初始化XPlcClient的虚函数表
  * @return 指向初始化完成的XVtable的指针
  * @note 该函数是线程安全的，多次调用返回同一虚表实例
  */
XVtable* XPlcClient_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XPlcClient实例
 * @param memory 内存分配类型
 * @return 成功返回指向新分配XPlcClient对象的指针，失败返回NULL
 * @note 返回的对象必须通过 XObject_deleteLater 或 XPlcClient_deleteLater 释放
 * @warning 此为抽象基类，通常不应直接创建实例，而应使用子类的创建函数
 */
XPlcClient* XPlcClient_create_ex(XMemoryType memory);

/**
 * @brief 初始化一个已分配的XPlcClient实例
 * @param client 待初始化的XPlcClient对象指针（非NULL）
 * @note 调用基类 XPlcDevice_init，并设置超时/重试/重连默认值
 * @par 默认值
 * - 超时时间：3000毫秒（S7 握手比 Modbus 慢）
 * - 重试次数：3次
 * - 自动重连：关闭；间隔1000毫秒；最大次数-1（无限）
 */
void XPlcClient_init(XPlcClient* client);

/******************************************************************************************
 * 超时/重试配置 API
 ******************************************************************************************/

 /**
  * @brief 获取当前请求超时时间
  * @param client 客户端实例指针（非NULL）
  * @return 超时时间（毫秒），client为NULL时返回默认3000
  */
size_t XPlcClient_timeout(const XPlcClient* client);

/**
 * @brief 设置请求超时时间
 * @param client 客户端实例指针（非NULL）
 * @param newTimeout 新的超时时间（毫秒）
 * @note 设置后会触发 timeoutChanged 信号
 */
void XPlcClient_setTimeout(XPlcClient* client, size_t newTimeout);

/**
 * @brief 获取当前请求重试次数
 * @param client 客户端实例指针（非NULL）
 * @return 重试次数，client为NULL时返回默认3
 */
int16_t XPlcClient_numberOfRetries(const XPlcClient* client);

/**
 * @brief 设置请求重试次数
 * @param client 客户端实例指针（非NULL）
 * @param number 新的重试次数
 */
void XPlcClient_setNumberOfRetries(XPlcClient* client, uint8_t number);

/******************************************************************************************
 * 自动重连配置 API
 ******************************************************************************************/

 /**
  * @brief 检查是否启用自动重连
  * @param client 客户端实例指针（非NULL）
  * @return true表示启用自动重连
  */
bool XPlcClient_autoReconnect(const XPlcClient* client);

/**
 * @brief 设置是否启用自动重连
 * @param client 客户端实例指针（非NULL）
 * @param enabled 是否启用
 * @note 禁用时会停止正在进行的重连定时器
 */
void XPlcClient_setAutoReconnect(XPlcClient* client, bool enabled);

/**
 * @brief 获取重连间隔
 * @param client 客户端实例指针（非NULL）
 * @return 重连间隔（毫秒），client为NULL时返回默认1000
 */
size_t XPlcClient_reconnectInterval(const XPlcClient* client);

/**
 * @brief 设置重连间隔
 * @param client 客户端实例指针（非NULL）
 * @param interval 重连间隔（毫秒）
 */
void XPlcClient_setReconnectInterval(XPlcClient* client, size_t interval);

/**
 * @brief 获取最大重连次数
 * @param client 客户端实例指针（非NULL）
 * @return 最大重连次数（-1表示无限），client为NULL时返回-1
 */
int16_t XPlcClient_maxReconnectAttempts(const XPlcClient* client);

/**
 * @brief 设置最大重连次数
 * @param client 客户端实例指针（非NULL）
 * @param attempts 最大重连次数（-1表示无限）
 */
void XPlcClient_setMaxReconnectAttempts(XPlcClient* client, int16_t attempts);

/**
 * @brief 获取当前重连尝试次数
 * @param client 客户端实例指针（非NULL）
 * @return 当前重连尝试次数，client为NULL时返回0
 */
int16_t XPlcClient_reconnectAttempts(const XPlcClient* client);

/******************************************************************************************
 * 信号接口
 ******************************************************************************************/

 /**
  * @brief 触发 timeoutChanged 信号
  * @param client 客户端实例指针（非NULL）
  * @param newTimeout 新的超时值
  * @return 信号句柄
  */
void* XPlcClient_timeoutChanged_signal(XPlcClient* client, int newTimeout);

/******************************************************************************************
 * 内存管理宏
 ******************************************************************************************/

#define XPlcClient_deleteLater        XObject_deleteLater
#define XPlcClient_deinitLater        XObject_deinitLater

#endif /* XPLC_CORE_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XPlcClient_create
#define XPlcClient_create() XPlcClient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XPLCCLIENT_H
