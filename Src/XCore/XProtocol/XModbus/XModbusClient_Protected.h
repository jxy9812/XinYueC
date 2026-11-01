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
 * @brief 处理标准Modbus响应——ProcessResponse 虚槽的分派入口。
 * @param client XModbusClient实例指针；NULL 或虚表缺失返回 false。
 * @param response 接收到的响应PDU（借用，不取得所有权；可为异常响应——
 *        默认实现对异常响应返回 false）。
 * @param data 解析结果输出（调用方提供的数据单元；默认实现按功能码
 *        重置其类型/起始地址/值）。
 * @return 解析成功 true；client/response/data 为 NULL、异常响应、
 *         报文长度不合法或功能码无法处理返回 false（各分支先校验后
 *         写入，失败时 data 保持进入时内容）。
 * @note 默认实现覆盖 FC01/02/03/04/05/06/15/16/23，其余功能码转发给
 *       processPrivateResponse_base；子类重载实现特定协议处理。
 */
bool XModbusClient_processResponse_base(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);

/**
 * @brief 处理私有/自定义功能码响应——ProcessPrivateResponse 虚槽的分派入口。
 * @param client XModbusClient实例指针；NULL 或虚表缺失返回 false。
 * @param response 接收到的响应PDU（借用，不取得所有权）。
 * @param data 解析结果输出（调用方提供的数据单元，成功时由子类实现回填）。
 * @return 子类实现处理成功 true；基类默认实现不处理私有响应，一律
 *         返回 false。
 * @note 标准 FC 处理不了的功能码由 processResponse_base 转发至此。
 */
bool XModbusClient_processPrivateResponse_base(XModbusClient* client, const XModbusResponse* response, XModbusDataUnit* data);

/**
 * @brief 辅助函数：创建并初始化 Reply 对象（请求深拷贝，类型按功能码推断）。
 * @param client 客户端实例指针；NULL、request 为 NULL 或
 *        serverAddress<0 返回 NULL。
 * @param request 原始请求PDU（借用：函数内深拷贝并登记到 Reply，
 *        调用方保留并自行释放原对象）。
 * @param serverAddress 目标从站地址。
 * @return 新 Reply 对象（调用方 XModbusReply_deleteLater 释放；请求
 *         副本由 Reply 析构释放）；内存分配失败返回 NULL。
 * @note 供子类实现 sendRawRequest 时调用。回复类型推断：标准读写
 *       功能码（FC01-06/15/16/23）为 Common，其余为 Raw。
 */
XModbusReply* XModbusClient_createReply(XModbusClient* client, const XModbusRequest* request, int serverAddress);

/**
 * @brief 辅助函数：创建并初始化 Reply 对象（请求移动语义）。
 * @param client 客户端实例指针；NULL、request 为 NULL 或
 *        serverAddress<0 返回 NULL（request 保持原状）。
 * @param request 原始请求PDU（移动：函数内经 create_move 将其资源转移到
 *        新请求对象，源对象变为空壳；函数内不释放源对象，仍由调用者
 *        按其分配方式负责释放）。
 * @param serverAddress 目标从站地址。
 * @return 新 Reply 对象（调用方 XModbusReply_deleteLater 释放；已接管
 *         移动出的新请求对象，随 Reply 一起释放）；create_move 失败或
 *         Reply 分配失败返回 NULL（函数内删除已创建的新请求对象）。
 * @note 类型推断同 createReply。
 */
XModbusReply* XModbusClient_createReply_move(XModbusClient* client, XModbusRequest* request, int serverAddress);

/**
 * @brief 辅助函数：创建并初始化 Reply 对象（直接接管调用方的请求指针）。
 * @param client 客户端实例指针；NULL、request 为 NULL 或
 *        serverAddress<0 返回 NULL。
 * @param request 请求PDU 指针（所有权移交：成功后由 Reply 析构释放，
 *        调用方不得再释放；失败时所有权留在调用方）。
 * @param serverAddress 目标从站地址。
 * @return 新 Reply 对象（调用方 XModbusReply_deleteLater 释放）；
 *         Reply 分配失败返回 NULL（此时 request 未被接管，仍归调用方）。
 * @note 类型推断同 createReply；pollRawRequest_ref 在定时器创建失败时
 *       置 reply->m_request=NULL 再销毁 Reply，正是依赖本函数
 *       「失败不接管」的约定避免双重释放。
 */
XModbusReply* XModbusClient_createReply_ref(XModbusClient* client, XModbusRequest* request, int serverAddress);

/**
 * @brief 停止超时定时器（未运行时为空操作）。
 * @param client XModbusClient实例指针；NULL 时不执行任何操作。
 * @return 无。
 * @note kill 后计时器 id 复位为 XTIMER_INVALID_ID；析构路径亦调用。
 */
void XModbusClient_timeoutTimerStop(XModbusClient* client);

/**
 * @brief 以当前 timeout() 间隔启动（重启）超时定时器。
 * @param client XModbusClient实例指针；NULL 时不执行任何操作。
 * @return 无。
 * @note 先停止既有超时计时再按 XModbusClient_timeout() 毫秒启动——
 *       重复调用不叠加计时器；发送请求前调用。
 */
void XModbusClient_timeoutTimerStart(XModbusClient* client);

/**
 * @brief 停止重连定时器（未运行时为空操作）。
 * @param client XModbusClient实例指针；NULL 时不执行任何操作。
 * @return 无。
 * @note 连接成功或关闭自动重连时调用；kill 后计时器 id 复位。
 */
void XModbusClient_reconnectTimerStop(XModbusClient* client);

/**
 * @brief 以当前 reconnectInterval() 间隔启动（重启）自动重连定时器。
 * @param client XModbusClient实例指针；NULL 时不执行任何操作。
 * @return 无。
 * @note 先停止既有重连计时再按 XModbusClient_reconnectInterval() 毫秒
 *       启动；连接断开且 autoReconnect 开启时由子类调用。
 */
void XModbusClient_reconnectTimerStart(XModbusClient* client);

#endif /* XMODBUS_CLIENT_ON */
#endif /* XMODBUS_ON */

#ifdef __cplusplus
}
#endif

#endif // XMODBUSCLIENT_PROTECTED_H



