#ifndef XPLCCLIENT_PROTECTED_H
#define XPLCCLIENT_PROTECTED_H

#include "XPlcClient.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XPLC_CORE_ON

/******************************************************************************************
 * 受保护接口声明（供子类和内部模块使用，不暴露给用户）
 ******************************************************************************************/

 /**
  * @brief 发送请求（通过虚函数表查表分派）
  * @param client XPlcClient实例指针（非NULL）
  * @param payload 待发送的协议 PDU（不含传输层帧头，由子类封帧）
  * @param reply 关联的异步回复对象（非NULL）
  * @return 处理成功返回true，失败返回false
  * @note 子类负责封传输层帧（如 TPKT/COTP）、发送、登记 pending 并起超时定时器；
  *       基类默认实现返回false（抽象槽）
  */
bool XPlcClient_sendRequest_base(XPlcClient* client, const XByteArray* payload, XPlcReply* reply);

/**
 * @brief 处理应答（通过虚函数表查表分派）
 * @param client XPlcClient实例指针（非NULL）
 * @param payload 收到的完整协议 PDU（已剥离传输层帧头）
 * @param reply 关联的异步回复对象（非NULL）
 * @return 解析并回填成功返回true，失败返回false
 * @note 子类负责解析 data 并回填 Reply（setResult/setRawResult/setState）；
  *       基类默认实现返回false（抽象槽）
 */
bool XPlcClient_processResponse_base(XPlcClient* client, const XByteArray* payload, XPlcReply* reply);

/**
 * @brief 辅助函数：创建并初始化 Reply 对象（深拷贝请求）
 * @param client 客户端实例指针（非NULL）
 * @param request 请求回带数据（内部复制，调用者可安全释放）
 * @param type 回复类型（XPlcReply_Raw / XPlcReply_Common）
 * @return 成功返回指向XPlcReply对象的指针，失败返回NULL
 * @note 供子类在实现 SendRequest 时调用
 */
XPlcReply* XPlcClient_createReply(XPlcClient* client, const XByteArray* request, XPlcReply_ReplyType type);

/**
 * @brief 辅助函数：创建并初始化 Reply 对象（移动语义）
 * @param client 客户端实例指针（非NULL）
 * @param request 请求回带数据（移动，函数内接管所有权）
 * @param type 回复类型
 * @return 成功返回指向XPlcReply对象的指针，失败返回NULL
 */
XPlcReply* XPlcClient_createReply_move(XPlcClient* client, XByteArray* request, XPlcReply_ReplyType type);

/**
 * @brief 辅助函数：创建并初始化 Reply 对象（引用语义）
 * @param client 客户端实例指针（非NULL）
 * @param request 请求回带数据（引用，函数内接管所有权，失败时不释放）
 * @param type 回复类型
 * @return 成功返回指向XPlcReply对象的指针，失败返回NULL
 */
XPlcReply* XPlcClient_createReply_ref(XPlcClient* client, XByteArray* request, XPlcReply_ReplyType type);

/**
 * @brief 停止超时定时器
 * @param client XPlcClient实例指针（非NULL）
 * @note 在请求完成或超时时调用，停止当前请求的超时计时
 */
void XPlcClient_timeoutTimerStop(XPlcClient* client);

/**
 * @brief 启动超时定时器
 * @param client XPlcClient实例指针（非NULL）
 * @note 在发送请求时调用，按当前 timeout 配置开始超时计时；
 *       S7 每片 in-flight 请求独立计时
 */
void XPlcClient_timeoutTimerStart(XPlcClient* client);

/**
 * @brief 停止重连定时器
 * @param client XPlcClient实例指针（非NULL）
 * @note 在连接成功时调用，停止自动重连尝试
 */
void XPlcClient_reconnectTimerStop(XPlcClient* client);

/**
 * @brief 启动重连定时器
 * @param client XPlcClient实例指针（非NULL）
 * @note 在连接断开时调用，按 reconnectInterval 启动自动重连尝试
 */
void XPlcClient_reconnectTimerStart(XPlcClient* client);

#endif /* XPLC_CORE_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif // XPLCCLIENT_PROTECTED_H
