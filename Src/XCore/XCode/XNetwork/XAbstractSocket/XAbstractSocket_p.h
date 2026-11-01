/**
 * @file       XAbstractSocket_p.h
 * @brief      XAbstractSocket 本地流传输内部适配接口。
 * @details    此文件不是公开 API，只允许网络实现和 MySQL 协议客户端包含。
 *             它把 Unix 域套接字和 Windows 命名管道的连接细节限制在平台层，
 *             不改变 XAbstractSocket 的公开 TCP API。
 */
#ifndef XABSTRACTSOCKET_P_H
#define XABSTRACTSOCKET_P_H
#include "XNetwork_config.h"

#include "XAbstractSocket.h"
#include "XDeviceNetwork.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XNETWORK_ON
#if XNETWORK_ABSTRACT_SOCKET_ON

/**
 * @brief 使用平台本地流传输连接端点。
 * @param sock 已初始化的抽象套接字；不能为 NULL，调用成功后由它接管底层句柄。
 * @param endpoint 本地端点名称或路径；借用，调用期间有效，不能为 NULL。
 * @param streamType 本地流类型，例如 Unix 域套接字或 Windows 命名管道。
 * @param timeoutMs 连接超时毫秒数；负值表示使用平台默认值。
 * @return 连接成功返回 true；平台不支持、参数无效或超时返回 false，并设置套接字错误状态。
 * @note 私有接口；lwIP 后端没有操作系统本地 IPC 能力时必须返回 false。
 */
bool XAbstractSocket_connectLocalStream_private(XAbstractSocket* sock,
                                                const XString* endpoint,
                                                XDeviceNetworkLocalStreamType streamType,
                                                int timeoutMs);

#endif // XNETWORK_ABSTRACT_SOCKET_ON
#endif /* XNETWORK_ON */
#ifdef __cplusplus
}
#endif

#endif
