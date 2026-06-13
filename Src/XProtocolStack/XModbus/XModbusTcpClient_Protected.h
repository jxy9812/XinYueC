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
  * @brief 获取关联的TCP套接字对象
  * @param client XModbusTcpClient实例指针
  * @return TCP套接字对象指针，client为NULL或套接字未创建时返回NULL
  * @note 返回的套接字对象由XModbusTcpClient管理，不需要手动释放
  */
XTcpSocket* XModbusTcpClient_socket(const XModbusTcpClient* client);

/**
 * @brief 获取下一个事务标识符
 * @param client XModbusTcpClient实例指针
 * @return 下一个可用的事务标识符（1-65535循环）
 * @note 该函数是线程安全的，每次调用自动递增事务ID
 */
uint16_t XModbusTcpClient_nextTransactionId(XModbusTcpClient* client);

/**
 * @brief 检查是否有正在等待响应的请求
 * @param client XModbusTcpClient实例指针
 * @return 有待响应请求返回true，否则返回false
 */
bool XModbusTcpClient_hasPendingRequests(const XModbusTcpClient* client);

/**
 * @brief 获取正在等待响应的请求数量
 * @param client XModbusTcpClient实例指针
 * @return 待响应请求的数量
 */
size_t XModbusTcpClient_pendingRequestCount(const XModbusTcpClient* client);

#ifdef __cplusplus
}
#endif

#endif // XMODBUSTCPCLIENT_PROTECTED_H