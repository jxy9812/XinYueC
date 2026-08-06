/**
 * @file XConsoleShell_XTcpSocket.h
 * @brief XConsoleShell 到 XTcpSocket 的 TCP 传输适配器。
 * @details
 * XTcpSocket 经 XAbstractSocket 继承 XIODevice。本适配器只复用 XIODevice
 * 的公开读、写、刷新 API，不连接、断开或释放套接字；连接状态和事件循环仍由
 * 产品通过 XTcpSocket/XAbstractSocket 管理。
 */

#ifndef XCONSOLE_SHELL_XTCPSOCKET_H
#define XCONSOLE_SHELL_XTCPSOCKET_H

#include "XConsoleShell_XIODevice.h"
#include "XTcpSocket.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON

#ifdef __cplusplus
extern "C" {
#endif

/** @brief XTcpSocket 适配器；socket 为借用指针，不由适配器释放。 */
typedef struct XConsoleShellXTcpSocketAdapter {
    XConsoleShellXIODeviceAdapter ioDevice; /**< 复用的 XIODevice 适配器状态。 */
} XConsoleShellXTcpSocketAdapter;

/**
 * @brief 初始化 TCP Socket 适配器。
 * @param adapter 待初始化适配器；不能为空。
 * @param socket 已连接的 XTcpSocket 借用指针，使用期间必须保持有效。
 */
void XConsoleShellXTcpSocketAdapter_init(XConsoleShellXTcpSocketAdapter* adapter,
                                         XTcpSocket* socket);
/**
 * @brief 生成 Shell I/O 回调。
 * @param adapter 已初始化的 TCP Socket 适配器。
 * @param io 输出 Shell I/O 回调集合，由调用方提供存储。
 * @return 生成成功返回 true；适配器、套接字或 io 无效返回 false。
 */
bool XConsoleShellXTcpSocketAdapter_makeIo(XConsoleShellXTcpSocketAdapter* adapter,
                                           XConsoleShellIo* io);

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON */
#endif /* XCONSOLE_SHELL_XTCPSOCKET_H */
