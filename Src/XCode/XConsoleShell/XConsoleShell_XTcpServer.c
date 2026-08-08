/**
 * @file XConsoleShell_XTcpServer.c
 * @brief XTcpServer 多会话 Shell 适配实现。
 */

#include "XConsoleShell_XTcpServer.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_MULTI_SESSION_ON && XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON

#include <string.h>

#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
#include "XConsoleShell_XSsh.h"
#endif

static XConsoleShellXTcpServerBinding* xcs_tcpserver_free_binding(
    XConsoleShellXTcpServerAdapter* adapter)
{
    size_t i;
    if (!adapter) return NULL;
    for (i = 0; i < XCONSOLE_SHELL_MAX_SESSIONS - 1u; ++i) {
        if (!adapter->bindings[i].session) return &adapter->bindings[i];
    }
    return NULL;
}

void XConsoleShellXTcpServerAdapter_init(XConsoleShellXTcpServerAdapter* adapter,
                                         XConsoleShell* shell, XTcpServer* server)
{
    if (!adapter) return;
    memset(adapter, 0, sizeof(*adapter));
    adapter->shell = shell;
    adapter->server = server;
}

size_t XConsoleShellXTcpServerAdapter_acceptPending(XConsoleShellXTcpServerAdapter* adapter)
{
    size_t accepted = 0;
    if (!adapter || !adapter->shell || !adapter->server) return 0;
    while (XTcpServer_hasPendingConnections_base(adapter->server)) {
        XConsoleShellXTcpServerBinding* binding = xcs_tcpserver_free_binding(adapter);
        XTcpSocket* socket;
        XConsoleShellIo transportIo;
        XConsoleShellIo io;
        if (!binding) break;
        socket = XTcpServer_nextPendingConnection_base(adapter->server);
        if (!socket) break;
        XConsoleShellXTcpSocketAdapter_init(&binding->adapter, socket);
        if (!XConsoleShellXTcpSocketAdapter_makeIo(&binding->adapter, &transportIo)) {
            XTcpSocket_close_base((XIODevice*)socket);
            continue;
        }
#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
        binding->ssh = XConsoleShellSshAdapter_create(&transportIo);
        if (!binding->ssh) {
            XTcpSocket_close_base((XIODevice*)socket);
            continue;
        }
        if (!XConsoleShellSshAdapter_makeIo(binding->ssh, &io)) {
            XConsoleShellSshAdapter_destroy(binding->ssh);
            binding->ssh = NULL;
            XTcpSocket_close_base((XIODevice*)socket);
            continue;
        }
        binding->session = XConsoleShell_openSession(adapter->shell, &io);
        if (!binding->session) {
            XConsoleShellSshAdapter_destroy(binding->ssh);
            binding->ssh = NULL;
            XTcpSocket_close_base((XIODevice*)socket);
            continue;
        }
        XConsoleShellSshAdapter_setSession(binding->ssh, adapter->shell, binding->session);
#else
        (void)transportIo;
        if (!XConsoleShellXTcpSocketAdapter_makeIo(&binding->adapter, &io)) {
            XTcpSocket_close_base((XIODevice*)socket);
            continue;
        }
        binding->session = XConsoleShell_openSession(adapter->shell, &io);
        if (!binding->session) {
            XTcpSocket_close_base((XIODevice*)socket);
            continue;
        }
#endif
        binding->socket = socket;
        ++accepted;
    }
    return accepted;
}

size_t XConsoleShellXTcpServerAdapter_pump(XConsoleShellXTcpServerAdapter* adapter,
                                           size_t maxBytes)
{
    uint8_t bytes[4096];
    size_t processed = 0;
    size_t i;
    if (!adapter || !adapter->shell) return 0;
    if (maxBytes == 0) maxBytes = sizeof(bytes);
    for (i = 0; i < XCONSOLE_SHELL_MAX_SESSIONS - 1u; ++i) {
        XConsoleShellXTcpServerBinding* binding = &adapter->bindings[i];
        int64_t count;
        if (!binding->session || !binding->socket) continue;
        if (XTcpSocket_state((XAbstractSocket*)binding->socket) !=
            XAbstractSocket_ConnectedState) {
            (void)XConsoleShellXTcpServerAdapter_closeSession(adapter, binding->session);
            continue;
        }
        /* 循环读取同一会话，直到本次暂时读不到更多数据，避免握手包被拆成
         * 多次到达时只消费第一片而遗留后续数据。 */
        for (;;) {
            size_t chunk = maxBytes;
            if (chunk > sizeof(bytes)) chunk = sizeof(bytes);
            count = XIODevice_read_1((XIODevice*)binding->socket, (char*)bytes,
                                     (int64_t)chunk);
            if (count <= 0) break;
#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
            if (binding->ssh) {
                (void)XConsoleShellSshAdapter_feedData(binding->ssh, adapter->shell,
                                                       binding->session, bytes, (size_t)count);
                if (XConsoleShellSshAdapter_isClosed(binding->ssh)) {
                    (void)XConsoleShellXTcpServerAdapter_closeSession(adapter, binding->session);
                    break;
                }
            } else {
                (void)XConsoleShell_feedDataForSession(adapter->shell, binding->session,
                                                       bytes, (size_t)count);
            }
#else
            (void)XConsoleShell_feedDataForSession(adapter->shell, binding->session,
                                                   bytes, (size_t)count);
#endif
            processed += (size_t)count;
        }
#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
        /* 即使本轮没有新数据也刷新待发送缓冲区，避免握手包积压导致死锁 */
        if (binding->ssh && !XConsoleShellSshAdapter_flush(binding->ssh)) {
            (void)XConsoleShellXTcpServerAdapter_closeSession(adapter, binding->session);
            continue;
        }
#endif
        if (XTcpSocket_state((XAbstractSocket*)binding->socket) !=
            XAbstractSocket_ConnectedState) {
            (void)XConsoleShellXTcpServerAdapter_closeSession(adapter, binding->session);
        }
    }
    return processed;
}

void XConsoleShellXTcpServerAdapter_closeAll(XConsoleShellXTcpServerAdapter* adapter)
{
    size_t i;
    if (!adapter) return;
    for (i = 0; i < XCONSOLE_SHELL_MAX_SESSIONS - 1u; ++i) {
        XConsoleShellXTcpServerBinding* binding = &adapter->bindings[i];
        if (binding->session)
            (void)XConsoleShellXTcpServerAdapter_closeSession(adapter, binding->session);
    }
}

bool XConsoleShellXTcpServerAdapter_closeSession(XConsoleShellXTcpServerAdapter* adapter,
                                                 XConsoleShellSession* session)
{
    size_t i;
    if (!adapter || !adapter->shell || !session) return false;
    for (i = 0; i < XCONSOLE_SHELL_MAX_SESSIONS - 1u; ++i) {
        XConsoleShellXTcpServerBinding* binding = &adapter->bindings[i];
        if (binding->session != session) continue;
        if (binding->socket) XTcpSocket_close_base((XIODevice*)binding->socket);
#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
        if (binding->ssh) {
            XConsoleShellSshAdapter_destroy(binding->ssh);
            binding->ssh = NULL;
        }
#endif
        if (!XConsoleShell_closeSession(adapter->shell, session)) return false;
        memset(binding, 0, sizeof(*binding));
        return true;
    }
    return false;
}

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && MULTI_SESSION && XTCPSERVER */
