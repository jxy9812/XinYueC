/**
 * @file       XModbusTcpClient_Protected.h
 * @brief      XModbusTcpClient 受保护接口（套接字访问与事务管理）。
 * @details    仅供子类和内部实现使用，不对外公开；公开 API 见
 *             XModbusTcpClient.h。
 */
#ifndef XMODBUSTCPCLIENT_PROTECTED_H
#define XMODBUSTCPCLIENT_PROTECTED_H

#include "XModbusTcpClient.h"

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************************
 * 受保护接口声明（供子类和内部模块使用，不暴露给用户）
 ******************************************************************************************/

 /**
  * @brief 获取关联的TCP套接字对象。
  * @param client XModbusTcpClient实例指针；NULL 返回 NULL。
  * @return 套接字对象的借用指针（由 client 创建与释放，禁止调用方
  *         释放或越权改写）；client 为 NULL 或套接字尚未创建返回 NULL。
  * @note 即设备 m_ioDevice（open 时按需创建）；借用指针仅在 client
  *       存续期间有效。
  */
XTcpSocket* XModbusTcpClient_socket(const XModbusTcpClient* client);

/**
 * @brief 获取下一个事务标识符（自增后返回，MBAP 头用）。
 * @param client XModbusTcpClient实例指针；NULL 返回 0 且不递增。
 * @return 下一事务标识符（1-65535 循环；65535 之后回绕到 1）。
 * @note 非线程安全：直接递增成员 m_transactionId，仅限拥有该客户端
 *       事件循环的同一线程内调用。
 */
uint16_t XModbusTcpClient_nextTransactionId(XModbusTcpClient* client);

/**
 * @brief 检查是否有正在等待响应的请求。
 * @param client XModbusTcpClient实例指针。
 * @return 待响应请求表至少一条 true；client 为 NULL、请求表未创建或
 *         表为空返回 false。
 */
bool XModbusTcpClient_hasPendingRequests(const XModbusTcpClient* client);

/**
 * @brief 获取正在等待响应的请求数量。
 * @param client XModbusTcpClient实例指针。
 * @return 待响应请求的数量（请求表当前条目数）；client 为 NULL 或
 *         请求表未创建返回 0。
 */
size_t XModbusTcpClient_pendingRequestCount(const XModbusTcpClient* client);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSTCPCLIENT_PROTECTED_H