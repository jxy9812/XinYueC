/**
 * @file XConsoleShell_XTcpServer.c
 * @brief XTcpServer 多会话 Shell 适配实现。
 */

#include "XConsoleShell_XTcpServer.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_MULTI_SESSION_ON && XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON

#include <string.h>
#include <stdint.h>

#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
#include "XSshServer.h"
#include "XConsoleShellLogin.h"
#endif
#if XCONSOLE_SHELL_TELNET_PROTOCOL_ON
#include "XTelnetServer.h"
#endif

/* 协议操作表：SSH/Telnet/裸 TCP 共用同一套多会话绑定、连接生命周期和
 * 关闭清理逻辑，仅在“建立协议适配器、投喂输入、刷新输出、销毁协议状态”
 * 四处差异。通过 vtable 让 acceptPending/pump/closeSession 不再按协议分支。
 * SSH/Telnet 协议栈通过 XObject 信号槽与 Shell 交互，setup 负责创建协议栈、
 * 绑定 socket、连接信号并生成 Shell I/O 回调集合。 */
typedef struct XConsoleShellXTcpServerProtocolOps {
    bool (*setup)(XConsoleShellXTcpServerBinding* binding, XTcpSocket* socket,
                  const XConsoleShellIo* transportIo, XConsoleShellIo* shellIo);
    void (*onSession)(XConsoleShellXTcpServerBinding* binding, XConsoleShell* shell);
    void (*feed)(XConsoleShellXTcpServerBinding* binding, XConsoleShell* shell,
                 const uint8_t* bytes, size_t count, bool* closed);
    bool (*flush)(XConsoleShellXTcpServerBinding* binding);
    void (*close)(XConsoleShellXTcpServerBinding* binding);
} XConsoleShellXTcpServerProtocolOps;

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

/* 丢弃尚未绑定成功的连接：关闭并延迟销毁，同时解除与 server 的父子关系，
 * 避免失败路径也占用 XFd 表。 */
static void xcs_tcpserver_discard_socket(XTcpSocket* socket)
{
    if (!socket) return;
    XTcpSocket_close_base((XIODevice*)socket);
    XObject_setParent((XObject*)socket, NULL);
    XObject_deleteLater((XObject*)socket);
}

static XConsoleShellXTcpServerProtocol xcs_tcpserver_default_protocol(void)
{
#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
    return XConsoleShellXTcpServerProtocol_Ssh;
#elif XCONSOLE_SHELL_TELNET_PROTOCOL_ON
    return XConsoleShellXTcpServerProtocol_Telnet;
#else
    return XConsoleShellXTcpServerProtocol_None;
#endif
}

static bool xcs_tcpserver_protocol_available(XConsoleShellXTcpServerProtocol protocol)
{
    switch (protocol) {
    case XConsoleShellXTcpServerProtocol_Ssh:
        return XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON != 0;
    case XConsoleShellXTcpServerProtocol_Telnet:
        return XCONSOLE_SHELL_TELNET_PROTOCOL_ON != 0;
    case XConsoleShellXTcpServerProtocol_None:
        return XCONSOLE_SHELL_XTCPSERVER_RAW_BACKEND_ON != 0;
    default:
        return false;
    }
}

/* ==================== SSH 协议栈接入（信号槽 + setter 回填） ==================== */
#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
static void xcs_tcpserver_ssh_bytes_slot(XObject* receiver, XVarList* args);
static void xcs_tcpserver_ssh_auth_slot(XObject* receiver, XVarList* args);
static void xcs_tcpserver_ssh_is_running_slot(XObject* receiver, XVarList* args);
static void xcs_tcpserver_ssh_close_requested_slot(XObject* receiver, XVarList* args);
static void xcs_tcpserver_ssh_suppress_slot(XObject* receiver, XVarList* args);
static void xcs_tcpserver_ssh_user_name_slot(XObject* receiver, XVarList* args);
static void xcs_tcpserver_ssh_closed_slot(XObject* receiver, XVarList* args);
static void xcs_tcpserver_ssh_error_slot(XObject* receiver, XVarList* args);

static XConsoleShellXTcpServerBinding* xcs_tcpserver_ssh_binding(XSshServer* server)
{
    return server ? (XConsoleShellXTcpServerBinding*)server->m_hostContext : NULL;
}

static void xcs_tcpserver_ssh_bytes_slot(XObject* receiver, XVarList* args)
{
    XSshServer* server = (XSshServer*)receiver;
    XConsoleShellXTcpServerBinding* binding;
    if (!server) return;
    binding = xcs_tcpserver_ssh_binding(server);
    if (!binding || !binding->shell || !binding->session) {
        XSshServer_setBytesReceivedResult(server, (int)XProtocolResult_InvalidArgument);
        return;
    }
    XVarList_args_2(args, const void*, data, size_t, size);
    XSshServer_setBytesReceivedResult(
        server, (int)XConsoleShell_feedDataForSession(binding->shell, binding->session, data, size));
}

static void xcs_tcpserver_ssh_auth_slot(XObject* receiver, XVarList* args)
{
    XSshServer* server = (XSshServer*)receiver;
    XConsoleShellXTcpServerBinding* binding;
    if (!server) return;
    binding = xcs_tcpserver_ssh_binding(server);
#if XCONSOLE_SHELL_LOGIN_ON
    if (!binding || !binding->shell || !binding->session) {
        XSshServer_setAuthenticateResult(server, false);
        return;
    }
    XVarList_args_2(args, const char*, user, const char*, password);
    XSshServer_setAuthenticateResult(
        server, XConsoleShellLogin_authenticateSession(binding->shell, binding->session,
                                                       user, password));
#else
    (void)binding;
    (void)args;
    XSshServer_setAuthenticateResult(server, false);
#endif
}

static void xcs_tcpserver_ssh_is_running_slot(XObject* receiver, XVarList* args)
{
    XSshServer* server = (XSshServer*)receiver;
    XConsoleShellXTcpServerBinding* binding;
    (void)args;
    if (!server) return;
    binding = xcs_tcpserver_ssh_binding(server);
    XSshServer_setIsRunningResult(server,
        binding && binding->shell && XConsoleShell_isRunning(binding->shell));
}

static void xcs_tcpserver_ssh_close_requested_slot(XObject* receiver, XVarList* args)
{
    XSshServer* server = (XSshServer*)receiver;
    XConsoleShellXTcpServerBinding* binding;
    (void)args;
    if (!server) return;
    binding = xcs_tcpserver_ssh_binding(server);
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    XSshServer_setCloseRequestedResult(server,
        binding && binding->session && binding->session->m_closeRequested);
#else
    XSshServer_setCloseRequestedResult(server, false);
#endif
}

static void xcs_tcpserver_ssh_suppress_slot(XObject* receiver, XVarList* args)
{
    XSshServer* server = (XSshServer*)receiver;
    XConsoleShellXTcpServerBinding* binding;
    (void)args;
    if (!server) return;
    binding = xcs_tcpserver_ssh_binding(server);
#if XCONSOLE_SHELL_LOGIN_ON || XCONSOLE_SHELL_EDITOR_ON
    XSshServer_setSuppressPromptResult(server,
        binding && binding->session && binding->session->suppressPrompt);
#else
    XSshServer_setSuppressPromptResult(server, false);
#endif
}

static void xcs_tcpserver_ssh_user_name_slot(XObject* receiver, XVarList* args)
{
    XSshServer* server = (XSshServer*)receiver;
    XConsoleShellXTcpServerBinding* binding;
    if (!server) return;
    binding = xcs_tcpserver_ssh_binding(server);
#if XCONSOLE_SHELL_LOGIN_ON
    if (!binding || !binding->session || !binding->session->authenticated ||
        !binding->session->userName[0]) {
        XSshServer_setUserNameResult(server, NULL, 0);
        return;
    }
    XVarList_args_2(args, char*, buffer, size_t, capacity);
    (void)buffer; (void)capacity;
    XSshServer_setUserNameResult(server, binding->session->userName,
                                 strlen(binding->session->userName));
#else
    (void)binding;
    (void)args;
    XSshServer_setUserNameResult(server, NULL, 0);
#endif
}

static void xcs_tcpserver_ssh_closed_slot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
}

static void xcs_tcpserver_ssh_error_slot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
}

static int64_t xcs_tcpserver_ssh_write(void* userData, const void* data, size_t size)
{
    XSshServer* server = (XSshServer*)userData;
    if (!server) return -1;
    return XSshServer_write(server, data, size);
}

static bool xcs_tcpserver_ssh_flush_cb(void* userData)
{
    XSshServer* server = (XSshServer*)userData;
    return server && XSshServer_flush(server);
}

static bool xcs_tcpserver_ssh_cancelled(void* userData)
{
    XSshServer* server = (XSshServer*)userData;
    return XSshServer_isClosed(server);
}

static bool xcs_tcpserver_ssh_input_echo(void* userData, bool enabled)
{
    XSshServer* server = (XSshServer*)userData;
    return server && XSshServer_setInputEcho(server, enabled);
}

static void xcs_tcpserver_ssh_prompt(void* userData, XConsoleShell* shell)
{
    XSshServer* server = (XSshServer*)userData;
    XConsoleShellXTcpServerBinding* binding;
    (void)shell;
    if (!server) return;
    binding = xcs_tcpserver_ssh_binding(server);
    if (!binding || !binding->shell || XSshServer_isClosed(server)) return;
    if (!XConsoleShell_isRunning(binding->shell)) return;
    (void)XSshServer_emitPrompt(server);
}

static bool xcs_tcpserver_ssh_setup(XConsoleShellXTcpServerBinding* binding,
                                    XTcpSocket* socket,
                                    const XConsoleShellIo* transportIo,
                                    XConsoleShellIo* shellIo)
{
    XSshServer* server;
    (void)transportIo;
    if (!binding || !socket || !shellIo) return false;
    server = XSshServer_create();
    if (!server) return false;
    binding->ssh = server;
    if (!XSshServer_setDevice(server, (XIODevice*)socket)) {
        XSshServer_delete_base(server);
        binding->ssh = NULL;
        return false;
    }
    XSshServer_setHostContext(server, binding);
    XObject_connect_1((XObject*)server, XSignal(XSshServer_bytesReceived_signal),
                      (XObject*)server, xcs_tcpserver_ssh_bytes_slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XSshServer_authenticateRequested_signal),
                      (XObject*)server, xcs_tcpserver_ssh_auth_slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XSshServer_isRunningRequested_signal),
                      (XObject*)server, xcs_tcpserver_ssh_is_running_slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XSshServer_closeRequested_signal),
                      (XObject*)server, xcs_tcpserver_ssh_close_requested_slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XSshServer_suppressPromptRequested_signal),
                      (XObject*)server, xcs_tcpserver_ssh_suppress_slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XSshServer_userNameRequested_signal),
                      (XObject*)server, xcs_tcpserver_ssh_user_name_slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XSshServer_closed_signal),
                      (XObject*)server, xcs_tcpserver_ssh_closed_slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XSshServer_errorOccurred_signal),
                      (XObject*)server, xcs_tcpserver_ssh_error_slot, XConnectionType_Direct);
    memset(shellIo, 0, sizeof(*shellIo));
    shellIo->write = xcs_tcpserver_ssh_write;
    shellIo->flush = xcs_tcpserver_ssh_flush_cb;
    shellIo->cancelled = xcs_tcpserver_ssh_cancelled;
    shellIo->inputEcho = xcs_tcpserver_ssh_input_echo;
    shellIo->prompt = xcs_tcpserver_ssh_prompt;
    shellIo->userData = server;
    return true;
}

static void xcs_tcpserver_ssh_on_session(XConsoleShellXTcpServerBinding* binding,
                                         XConsoleShell* shell)
{
    (void)shell;
    if (!binding || !binding->ssh) return;
    (void)XSshServer_start(binding->ssh);
}

static void xcs_tcpserver_ssh_feed(XConsoleShellXTcpServerBinding* binding,
                                   XConsoleShell* shell, const uint8_t* bytes,
                                   size_t count, bool* closed)
{
    (void)shell;
    if (!binding || !binding->ssh) {
        if (closed) *closed = true;
        return;
    }
    (void)XSshServer_feedData(binding->ssh, bytes, count);
    if (closed && XSshServer_isClosed(binding->ssh)) *closed = true;
}

static bool xcs_tcpserver_ssh_flush(XConsoleShellXTcpServerBinding* binding)
{
    return binding && binding->ssh && XSshServer_flush(binding->ssh);
}

static void xcs_tcpserver_ssh_close(XConsoleShellXTcpServerBinding* binding)
{
    if (binding && binding->ssh) {
        XSshServer_delete_base(binding->ssh);
        binding->ssh = NULL;
    }
}
#endif /* XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON */

/* ==================== Telnet 协议栈接入（信号槽 + setter 回填） ==================== */
#if XCONSOLE_SHELL_TELNET_PROTOCOL_ON
static void xcs_tcpserver_telnet_bytes_slot(XObject* receiver, XVarList* args);
static void xcs_tcpserver_telnet_is_running_slot(XObject* receiver, XVarList* args);
static void xcs_tcpserver_telnet_close_requested_slot(XObject* receiver, XVarList* args);
static void xcs_tcpserver_telnet_suppress_slot(XObject* receiver, XVarList* args);
static void xcs_tcpserver_telnet_user_name_slot(XObject* receiver, XVarList* args);
static void xcs_tcpserver_telnet_closed_slot(XObject* receiver, XVarList* args);
static void xcs_tcpserver_telnet_error_slot(XObject* receiver, XVarList* args);

static XConsoleShellXTcpServerBinding* xcs_tcpserver_telnet_binding(XTelnetServer* server)
{
    return server ? (XConsoleShellXTcpServerBinding*)server->m_hostContext : NULL;
}

static void xcs_tcpserver_telnet_bytes_slot(XObject* receiver, XVarList* args)
{
    XTelnetServer* server = (XTelnetServer*)receiver;
    XConsoleShellXTcpServerBinding* binding;
    if (!server) return;
    binding = xcs_tcpserver_telnet_binding(server);
    if (!binding || !binding->shell || !binding->session) {
        XTelnetServer_setBytesReceivedResult(server, (int)XProtocolResult_InvalidArgument);
        return;
    }
    XVarList_args_2(args, const void*, data, size_t, size);
    {
        int r = (int)XConsoleShell_feedDataForSession(binding->shell, binding->session, data, size);
        XTelnetServer_setBytesReceivedResult(server, r);
    }
}

static void xcs_tcpserver_telnet_is_running_slot(XObject* receiver, XVarList* args)
{
    XTelnetServer* server = (XTelnetServer*)receiver;
    XConsoleShellXTcpServerBinding* binding;
    (void)args;
    if (!server) return;
    binding = xcs_tcpserver_telnet_binding(server);
    XTelnetServer_setIsRunningResult(server,
        binding && binding->shell && XConsoleShell_isRunning(binding->shell));
}

static void xcs_tcpserver_telnet_close_requested_slot(XObject* receiver, XVarList* args)
{
    XTelnetServer* server = (XTelnetServer*)receiver;
    XConsoleShellXTcpServerBinding* binding;
    (void)args;
    if (!server) return;
    binding = xcs_tcpserver_telnet_binding(server);
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    XTelnetServer_setCloseRequestedResult(server,
        binding && binding->session && binding->session->m_closeRequested);
#else
    XTelnetServer_setCloseRequestedResult(server, false);
#endif
}

static void xcs_tcpserver_telnet_suppress_slot(XObject* receiver, XVarList* args)
{
    XTelnetServer* server = (XTelnetServer*)receiver;
    XConsoleShellXTcpServerBinding* binding;
    (void)args;
    if (!server) return;
    binding = xcs_tcpserver_telnet_binding(server);
#if XCONSOLE_SHELL_LOGIN_ON || XCONSOLE_SHELL_EDITOR_ON
    XTelnetServer_setSuppressPromptResult(server,
        binding && binding->session && binding->session->suppressPrompt);
#else
    XTelnetServer_setSuppressPromptResult(server, false);
#endif
}

static void xcs_tcpserver_telnet_user_name_slot(XObject* receiver, XVarList* args)
{
    XTelnetServer* server = (XTelnetServer*)receiver;
    XConsoleShellXTcpServerBinding* binding;
    if (!server) return;
    binding = xcs_tcpserver_telnet_binding(server);
#if XCONSOLE_SHELL_LOGIN_ON
    if (!binding || !binding->session || !binding->session->authenticated ||
        !binding->session->userName[0]) {
        XTelnetServer_setUserNameResult(server, NULL, 0);
        return;
    }
    XVarList_args_2(args, char*, buffer, size_t, capacity);
    (void)buffer; (void)capacity;
    XTelnetServer_setUserNameResult(server, binding->session->userName,
                                    strlen(binding->session->userName));
#else
    (void)binding;
    (void)args;
    XTelnetServer_setUserNameResult(server, NULL, 0);
#endif
}

static void xcs_tcpserver_telnet_closed_slot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
}

static void xcs_tcpserver_telnet_error_slot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
}

static int64_t xcs_tcpserver_telnet_write(void* userData, const void* data, size_t size)
{
    XTelnetServer* server = (XTelnetServer*)userData;
    if (!server) return -1;
    return XTelnetServer_write(server, data, size);
}

static bool xcs_tcpserver_telnet_flush_cb(void* userData)
{
    XTelnetServer* server = (XTelnetServer*)userData;
    return server && XTelnetServer_flush(server);
}

static bool xcs_tcpserver_telnet_cancelled(void* userData)
{
    XTelnetServer* server = (XTelnetServer*)userData;
    return XTelnetServer_isClosed(server);
}

static bool xcs_tcpserver_telnet_input_echo(void* userData, bool enabled)
{
    XTelnetServer* server = (XTelnetServer*)userData;
    return server && XTelnetServer_setInputEcho(server, enabled);
}

static void xcs_tcpserver_telnet_prompt(void* userData, XConsoleShell* shell)
{
    XTelnetServer* server = (XTelnetServer*)userData;
    XConsoleShellXTcpServerBinding* binding;
    (void)shell;
    if (!server) return;
    binding = xcs_tcpserver_telnet_binding(server);
    if (!binding || !binding->shell || XTelnetServer_isClosed(server)) return;
    if (!XConsoleShell_isRunning(binding->shell)) return;
    (void)XTelnetServer_emitPrompt(server);
}

static bool xcs_tcpserver_telnet_setup(XConsoleShellXTcpServerBinding* binding,
                                       XTcpSocket* socket,
                                       const XConsoleShellIo* transportIo,
                                       XConsoleShellIo* shellIo)
{
    XTelnetServer* server;
    (void)transportIo;
    if (!binding || !socket || !shellIo) return false;
    server = &binding->telnet;
    XTelnetServer_init(server);
    if (!XTelnetServer_setDevice(server, (XIODevice*)socket)) return false;
    XTelnetServer_setHostContext(server, binding);
    XObject_connect_1((XObject*)server, XSignal(XTelnetServer_bytesReceived_signal),
                      (XObject*)server, xcs_tcpserver_telnet_bytes_slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XTelnetServer_isRunningRequested_signal),
                      (XObject*)server, xcs_tcpserver_telnet_is_running_slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XTelnetServer_closeRequested_signal),
                      (XObject*)server, xcs_tcpserver_telnet_close_requested_slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XTelnetServer_suppressPromptRequested_signal),
                      (XObject*)server, xcs_tcpserver_telnet_suppress_slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XTelnetServer_userNameRequested_signal),
                      (XObject*)server, xcs_tcpserver_telnet_user_name_slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XTelnetServer_closed_signal),
                      (XObject*)server, xcs_tcpserver_telnet_closed_slot, XConnectionType_Direct);
    XObject_connect_1((XObject*)server, XSignal(XTelnetServer_errorOccurred_signal),
                      (XObject*)server, xcs_tcpserver_telnet_error_slot, XConnectionType_Direct);
    memset(shellIo, 0, sizeof(*shellIo));
    shellIo->write = xcs_tcpserver_telnet_write;
    shellIo->flush = xcs_tcpserver_telnet_flush_cb;
    shellIo->cancelled = xcs_tcpserver_telnet_cancelled;
    shellIo->inputEcho = xcs_tcpserver_telnet_input_echo;
    shellIo->prompt = xcs_tcpserver_telnet_prompt;
    shellIo->userData = server;
    return true;
}

static void xcs_tcpserver_telnet_on_session(XConsoleShellXTcpServerBinding* binding,
                                            XConsoleShell* shell)
{
    (void)shell;
    if (!binding) return;
    (void)XTelnetServer_start(&binding->telnet);
}

static void xcs_tcpserver_telnet_feed(XConsoleShellXTcpServerBinding* binding,
                                      XConsoleShell* shell, const uint8_t* bytes,
                                      size_t count, bool* closed)
{
    XProtocolResult result;
    (void)shell;
    if (!binding) return;
    result = XTelnetServer_feedData(&binding->telnet, bytes, count);
    if (!closed) return;
    if (result == XProtocolResult_IoError) { *closed = true; return; }
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    /* exit 命令已请求关闭当前会话：与 SSH 通道一致，由 pump 关闭连接。 */
    if (binding->session && binding->session->m_closeRequested) *closed = true;
#endif
}

static bool xcs_tcpserver_telnet_flush(XConsoleShellXTcpServerBinding* binding)
{
    return binding && XTelnetServer_flush(&binding->telnet);
}

static void xcs_tcpserver_telnet_close(XConsoleShellXTcpServerBinding* binding)
{
    if (binding) XTelnetServer_stop(&binding->telnet);
}
#endif /* XCONSOLE_SHELL_TELNET_PROTOCOL_ON */

#if XCONSOLE_SHELL_XTCPSERVER_RAW_BACKEND_ON
static bool xcs_tcpserver_none_setup(XConsoleShellXTcpServerBinding* binding,
                                     XTcpSocket* socket,
                                     const XConsoleShellIo* transportIo,
                                     XConsoleShellIo* shellIo)
{
    (void)socket;
    (void)transportIo;
    return XConsoleShellXTcpSocketAdapter_makeIo(&binding->adapter, shellIo);
}

static void xcs_tcpserver_none_on_session(XConsoleShellXTcpServerBinding* binding,
                                          XConsoleShell* shell)
{
    (void)binding;
    (void)shell;
}

static void xcs_tcpserver_none_feed(XConsoleShellXTcpServerBinding* binding,
                                    XConsoleShell* shell, const uint8_t* bytes,
                                    size_t count, bool* closed)
{
    (void)XConsoleShell_feedDataForSession(shell, binding->session, bytes, count);
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    /* exit 命令已请求关闭当前会话：与 SSH/Telnet 行为一致，关闭裸 TCP 连接。 */
    if (closed && binding->session && binding->session->m_closeRequested)
        *closed = true;
#endif
}

static bool xcs_tcpserver_none_flush(XConsoleShellXTcpServerBinding* binding)
{
    (void)binding;
    return true;
}

static void xcs_tcpserver_none_close(XConsoleShellXTcpServerBinding* binding)
{
    (void)binding;
}
#endif /* XCONSOLE_SHELL_XTCPSERVER_RAW_BACKEND_ON */

static const XConsoleShellXTcpServerProtocolOps* xcs_tcpserver_ops(
    XConsoleShellXTcpServerProtocol protocol)
{
    switch (protocol) {
    case XConsoleShellXTcpServerProtocol_Ssh:
#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
        {
            static const XConsoleShellXTcpServerProtocolOps ops = {
                xcs_tcpserver_ssh_setup, xcs_tcpserver_ssh_on_session,
                xcs_tcpserver_ssh_feed, xcs_tcpserver_ssh_flush,
                xcs_tcpserver_ssh_close
            };
            return &ops;
        }
#endif
        break;
    case XConsoleShellXTcpServerProtocol_Telnet:
#if XCONSOLE_SHELL_TELNET_PROTOCOL_ON
        {
            static const XConsoleShellXTcpServerProtocolOps ops = {
                xcs_tcpserver_telnet_setup, xcs_tcpserver_telnet_on_session,
                xcs_tcpserver_telnet_feed, xcs_tcpserver_telnet_flush,
                xcs_tcpserver_telnet_close
            };
            return &ops;
        }
#endif
        break;
    case XConsoleShellXTcpServerProtocol_None:
#if XCONSOLE_SHELL_XTCPSERVER_RAW_BACKEND_ON
        {
            static const XConsoleShellXTcpServerProtocolOps ops = {
                xcs_tcpserver_none_setup, xcs_tcpserver_none_on_session,
                xcs_tcpserver_none_feed, xcs_tcpserver_none_flush,
                xcs_tcpserver_none_close
            };
            return &ops;
        }
#else
        break;
#endif
    default:
        break;
    }
    return NULL;
}

void XConsoleShellXTcpServerAdapter_init(XConsoleShellXTcpServerAdapter* adapter,
                                         XConsoleShell* shell, XTcpServer* server)
{
    (void)XConsoleShellXTcpServerAdapter_initProtocol(
        adapter, shell, server, xcs_tcpserver_default_protocol());
}

bool XConsoleShellXTcpServerAdapter_initProtocol(
    XConsoleShellXTcpServerAdapter* adapter, XConsoleShell* shell,
    XTcpServer* server, XConsoleShellXTcpServerProtocol protocol)
{
    if (!adapter || !shell || !server ||
        !xcs_tcpserver_protocol_available(protocol)) return false;
    memset(adapter, 0, sizeof(*adapter));
    adapter->shell = shell;
    adapter->server = server;
    adapter->protocol = protocol;
    return true;
}

size_t XConsoleShellXTcpServerAdapter_acceptPending(XConsoleShellXTcpServerAdapter* adapter)
{
    size_t accepted = 0;
    const XConsoleShellXTcpServerProtocolOps* ops;
    if (!adapter || !adapter->shell || !adapter->server) return 0;
    ops = xcs_tcpserver_ops(adapter->protocol);
    if (!ops) return 0;
    while (XTcpServer_hasPendingConnections_base(adapter->server)) {
        XConsoleShellXTcpServerBinding* binding = xcs_tcpserver_free_binding(adapter);
        XTcpSocket* socket;
        XConsoleShellIo transportIo;
        XConsoleShellIo io;
        if (!binding) break;
        socket = XTcpServer_nextPendingConnection_base(adapter->server);
        if (!socket) break;
        binding->protocol = adapter->protocol;
        binding->shell = adapter->shell;
        XConsoleShellXTcpSocketAdapter_init(&binding->adapter, socket);
        if (!XConsoleShellXTcpSocketAdapter_makeIo(&binding->adapter, &transportIo)) {
            xcs_tcpserver_discard_socket(socket);
            continue;
        }
        if (!ops->setup(binding, socket, &transportIo, &io)) {
            xcs_tcpserver_discard_socket(socket);
            continue;
        }
        binding->session = XConsoleShell_openSession(adapter->shell, &io);
        if (!binding->session) {
            ops->close(binding);
            xcs_tcpserver_discard_socket(socket);
            continue;
        }
        ops->onSession(binding, adapter->shell);
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
    const XConsoleShellXTcpServerProtocolOps* ops;
    if (!adapter || !adapter->shell) return 0;
    ops = xcs_tcpserver_ops(adapter->protocol);
    if (!ops) return 0;
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
            bool closed = false;
            if (chunk > sizeof(bytes)) chunk = sizeof(bytes);
            count = XIODevice_read_1((XIODevice*)binding->socket, (char*)bytes,
                                     (int64_t)chunk);
            if (count <= 0) break;
            ops->feed(binding, adapter->shell, bytes, (size_t)count, &closed);
            processed += (size_t)count;
            if (closed) {
                (void)XConsoleShellXTcpServerAdapter_closeSession(adapter, binding->session);
                break;
            }
        }
        /* 即使本轮没有新数据也刷新待发送缓冲区，避免 SSH 握手包或 Telnet
         * 响应积压导致死锁。 */
        if (!ops->flush(binding)) {
            (void)XConsoleShellXTcpServerAdapter_closeSession(adapter, binding->session);
            continue;
        }
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
    const XConsoleShellXTcpServerProtocolOps* ops;
    if (!adapter || !adapter->shell || !session) return false;
    ops = xcs_tcpserver_ops(adapter->protocol);
    for (i = 0; i < XCONSOLE_SHELL_MAX_SESSIONS - 1u; ++i) {
        XConsoleShellXTcpServerBinding* binding = &adapter->bindings[i];
        if (binding->session != session) continue;
        if (binding->socket) {
            /* 关闭前先刷新待发送数据：Telnet/裸 TCP 的客户端输出走异步
             * io_uring/IOCP 写队列，exit 这类“提示后立即关闭”的响应若不刷新，
             * 关闭 socket 会取消尚未完成的写请求，导致客户端收不到完整消息。 */
            (void)XTcpSocket_flush(binding->socket);
            /* flush 内部会处理事件循环，可能重入 pump 并已把当前绑定关闭和
             * 清零（例如 SSH 通道关闭后再次投递读到 closed 状态）。此时不能再
             * 关闭 socket，否则会以 NULL 调用 XIODevice_close_base。 */
            if (!binding->socket) return true;
            XTcpSocket_close_base((XIODevice*)binding->socket);
            /* socket 在 VXTcpServer_IncomingConnection() 中被挂到 server 名下，
             * 必须先解除父子关系再延迟销毁，避免 server 析构时重复释放；
             * 同时释放 socket 占用的 XFd，否则多轮连接/关闭会耗尽 XFd 表。 */
            XObject_setParent((XObject*)binding->socket, NULL);
            XObject_deleteLater((XObject*)binding->socket);
            binding->socket = NULL;
        }
        if (ops) ops->close(binding);
        if (!XConsoleShell_closeSession(adapter->shell, session)) return false;
        memset(binding, 0, sizeof(*binding));
        return true;
    }
    return false;
}

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && MULTI_SESSION && XTCPSERVER */