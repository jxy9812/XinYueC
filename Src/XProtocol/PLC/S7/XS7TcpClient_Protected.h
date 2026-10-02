#ifndef XS7TCPCLIENT_PROTECTED_H
#define XS7TCPCLIENT_PROTECTED_H

#include "XS7TcpClient.h"
#include "XTcpSocket.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON
#if XS7_CORE_ON

/******************************************************************************************
 * 受保护接口声明（供子类和内部模块使用，不暴露给用户）
 ******************************************************************************************/

 /**
  * @brief 获取关联的TCP套接字对象
  * @param client XS7TcpClient实例指针
  * @return TCP套接字对象指针，client为NULL或套接字未创建时返回NULL
  * @note 返回的套接字对象由 XS7TcpClient 管理（XTcpSocket 四信号接线在 open 中完成），
  *       调用者不应释放
  */
XTcpSocket* XS7TcpClient_socket(const XS7TcpClient* client);

/**
 * @brief 检查是否有正在等待响应的请求
 * @param client XS7TcpClient实例指针
 * @return 有待响应请求返回true，否则返回false
 */
bool XS7TcpClient_hasPendingRequests(const XS7TcpClient* client);

/**
 * @brief 获取正在等待响应的请求数量
 * @param client XS7TcpClient实例指针
 * @return 待响应请求的数量（AmQ=1 时 in-flight 至多 1，数量含排队项）
 */
size_t XS7TcpClient_pendingRequestCount(const XS7TcpClient* client);

/**
 * @brief 清理所有待处理请求（断线/关闭时调用）
 * @param client XS7TcpClient实例指针（非NULL）
 * @param error 结束各请求的错误码（XPlcDevice_NoError 表示不置错误）
 * @param errorMsg 结束各请求的错误描述（可为NULL）
 * @note 模式对齐 XModbusTcpClient：停定时器、清 pending 表与 timerMap、
 *       对各 Reply setError+setState
 */
void XS7TcpClient_clearAllPendingRequests(XS7TcpClient* client, XPlcDevice_Error error, const char* errorMsg);

#endif /* XS7_CORE_ON */
#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif // XS7TCPCLIENT_PROTECTED_H
