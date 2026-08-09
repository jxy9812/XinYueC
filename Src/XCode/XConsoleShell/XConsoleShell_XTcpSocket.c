/**
 * @file XConsoleShell_XTcpSocket.c
 * @brief XTcpSocket 到 XConsoleShell 的公共 API 适配实现。
 */

#include "XConsoleShell_XTcpSocket.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON

void XConsoleShellXTcpSocketAdapter_init(XConsoleShellXTcpSocketAdapter* adapter,
                                         XTcpSocket* socket)
{
    if (!adapter) return;
    /* XTcpSocket 经 XAbstractSocket 以 XIODevice 为首基类，布局公开且稳定。 */
    XConsoleShellXIODeviceAdapter_init(&adapter->ioDevice, (XIODevice*)socket);
}

bool XConsoleShellXTcpSocketAdapter_makeIo(XConsoleShellXTcpSocketAdapter* adapter,
                                           XConsoleShellIo* io)
{
    return adapter && XConsoleShellXIODeviceAdapter_makeIo(&adapter->ioDevice, io);
}

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_XTCPSOCKET_BACKEND_ON */
