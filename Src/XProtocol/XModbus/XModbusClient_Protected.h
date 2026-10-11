/**
 * @file       XModbusClient_Protected.h
 * @brief      XModbusClient 受保护接口。
 * @details    仅供子类和内部实现使用，不对外公开。
 */

#ifndef XMODBUSCLIENT_PROTECTED_H
#define XMODBUSCLIENT_PROTECTED_H

#include "XModbusClient.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XMODBUS_ON
#if XMODBUS_CLIENT_ON

/******************************************************************************************
 * 受保护接口声明（供子类和内部模块使用，不暴露给用户）
 ******************************************************************************************/

/**
 * @brief 处理标准Modbus响应（通过虚函数表调用）
 * @param client XModbusClient实例指针（非NULL）
 * @param response 接收到的响应PDU
 * @param data 用于存储解析结果的数据单元
 * @return 成功返回true，失败返回false
 * @note 子类可重载此虚函数实现特定协议处理
 */
bool XModbusClient_processResponse_base(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);

/**
 * @brief 处理私有响应（虚函数）
 * @param client XModbusClient实例指针（非NULL）
 * @param response 接收到的响应PDU
 * @param data 输出参数，用于接收解析后的数据单元
 * @return 处理成功返回true，失败返回false
 * @note 通过虚函数表调用，由子类实现具体响应处理逻辑
 */
bool XModbusClient_processPrivateResponse_base(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);

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
 * @brief 辅助函数：创建并初始化 Reply 对象（移动语义）
 * @param client 客户端实例指针（非NULL）
 * @param request 原始请求PDU（移动：函数内经 create_move 将其资源转移到新请求对象，
 *        源对象变为空壳；函数内不释放源对象，仍由调用者按其分配方式负责释放）
 * @param serverAddress 目标从站地址
 * @return 成功返回指向XModbusReply对象的指针（接管新请求对象，随 Reply 一起释放），
 *         失败返回NULL（函数内删除已创建的新请求对象）
 */
XModbusReply* XModbusClient_createReply_move(XModbusClient* client, XModbusRequest* request, int serverAddress);

/**
 * @brief 辅助函数：创建并初始化 Reply 对象（引用语义）
 * @param client 客户端实例指针（非NULL）
 * @param request 原始请求PDU（引用，函数内不释放）
 * @param serverAddress 目标从站地址
 * @return 成功返回指向XModbusReply对象的指针，失败返回NULL
 */
XModbusReply* XModbusClient_createReply_ref(XModbusClient* client, XModbusRequest* request, int serverAddress);

/**
 * @brief 停止超时定时器
 * @param client XModbusClient实例指针（非NULL）
 * @note 在请求完成或超时时调用，停止当前请求的超时计时
 */
void XModbusClient_timeoutTimerStop(XModbusClient* client);

/**
 * @brief 启动超时定时器
 * @param client XModbusClient实例指针（非NULL）
 * @note 在发送请求时调用，开始请求的超时计时
 */
void XModbusClient_timeoutTimerStart(XModbusClient* client);

/**
 * @brief 停止重连定时器
 * @param client XModbusClient实例指针（非NULL）
 * @note 在连接成功时调用，停止自动重连尝试
 */
void XModbusClient_reconnectTimerStop(XModbusClient* client);

/**
 * @brief 启动重连定时器
 * @param client XModbusClient实例指针（非NULL）
 * @note 在连接断开时调用，启动自动重连尝试
 */
void XModbusClient_reconnectTimerStart(XModbusClient* client);

#endif /* XMODBUS_CLIENT_ON */
#endif /* XMODBUS_ON */

#ifdef __cplusplus
}
#endif

#endif // XMODBUSCLIENT_PROTECTED_H



