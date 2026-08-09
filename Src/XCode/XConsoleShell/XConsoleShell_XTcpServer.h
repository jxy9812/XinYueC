/**
 * @file XConsoleShell_XTcpServer.h
 * @brief XTcpServer 到 XConsoleShell 多会话的库级适配器。
 * @details
 * 适配器从 XTcpServer 的待处理连接队列取得 XTcpSocket，以固定数组绑定到
 * Shell 附加会话。调用方在自己的事件循环中调用 acceptPending 和 pump；适配器
 * 不拥有 server 或 socket，不监听、不关闭 server，也不包含平台网络头文件。
 */

#ifndef XCONSOLE_SHELL_XTCPSERVER_H
#define XCONSOLE_SHELL_XTCPSERVER_H

#include "XConsoleShell_XTcpSocket.h"
#include "XTcpServer.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_MULTI_SESSION_ON && XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON

#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
#include "XConsoleShell_XSsh.h"
#endif
#if XCONSOLE_SHELL_TELNET_PROTOCOL_ON
#include "XConsoleShell_Telnet.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 单个 TCP 会话使用的传输协议；每种协议对应一个独立监听端口。 */
typedef enum XConsoleShellXTcpServerProtocol {
    XConsoleShellXTcpServerProtocol_None = 0, /**< 裸 TCP 字节流（无协议协商）；仅当
                                               *   XCONSOLE_SHELL_XTCPSERVER_RAW_BACKEND_ON
                                               *   开启且 SSH/Telnet 均关闭时可用。 */
    XConsoleShellXTcpServerProtocol_Ssh,      /**< SSH 传输协议。 */
    XConsoleShellXTcpServerProtocol_Telnet    /**< Telnet NVT/协商协议。 */
} XConsoleShellXTcpServerProtocol;

/** @brief 单个 TCP 会话绑定；socket 为 XTcpServer 借用对象。 */
typedef struct XConsoleShellXTcpServerBinding {
    XConsoleShellSession* session;                   /**< Shell 管理的附加会话。 */
    XTcpSocket* socket;                              /**< XTcpServer 借用的连接对象。 */
    XConsoleShellXTcpSocketAdapter adapter;          /**< 连接的 I/O 适配器。 */
    XConsoleShellXTcpServerProtocol protocol;        /**< 本连接使用的传输协议。 */
#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
    XConsoleShellSshAdapter* ssh;                    /**< 连接绑定的 SSH 适配器。 */
#endif
#if XCONSOLE_SHELL_TELNET_PROTOCOL_ON
    XConsoleShellTelnetAdapter telnet;               /**< 连接绑定的 Telnet 字节过滤适配器。 */
#endif
} XConsoleShellXTcpServerBinding;

/** @brief TCP 服务端 Shell 适配器；所有成员均为借用或固定存储。 */
typedef struct XConsoleShellXTcpServerAdapter {
    XConsoleShell* shell;                            /**< 借用的多会话 Shell。 */
    XTcpServer* server;                              /**< 借用的 TCP 服务端。 */
    XConsoleShellXTcpServerProtocol protocol;        /**< 本服务端使用的传输协议。 */
    XConsoleShellXTcpServerBinding
        bindings[XCONSOLE_SHELL_MAX_SESSIONS - 1u];  /**< 固定连接绑定槽。 */
} XConsoleShellXTcpServerAdapter;

/**
 * @brief 初始化 TCP 服务端适配器；自动按编译配置选择协议。
 * @details 优先级：SSH Server 开启时选 SSH；否则 Telnet Server 开启时选
 * Telnet；否则使用裸 TCP 字节流。
 * @param adapter 待初始化适配器；不能为空。
 * @param shell 已启用多会话的 Shell 借用指针。
 * @param server 已监听的 XTcpServer 借用指针。
 */
void XConsoleShellXTcpServerAdapter_init(XConsoleShellXTcpServerAdapter* adapter,
                                         XConsoleShell* shell, XTcpServer* server);
/**
 * @brief 按指定协议初始化 TCP 服务端适配器。
 * @param adapter 待初始化适配器；不能为空。
 * @param shell 已启用多会话的 Shell 借用指针。
 * @param server 已监听的 XTcpServer 借用指针。
 * @param protocol 需要的传输协议；该协议未编译进当前配置时返回 false。
 * @return 指定的协议可用并已绑定返回 true；参数无效或协议未编译返回 false。
 */
bool XConsoleShellXTcpServerAdapter_initProtocol(
    XConsoleShellXTcpServerAdapter* adapter, XConsoleShell* shell,
    XTcpServer* server, XConsoleShellXTcpServerProtocol protocol);
/**
 * @brief 接收并绑定当前所有待处理连接。
 * @param adapter 已初始化的服务端适配器。
 * @return 本轮成功打开并绑定的会话数量。
 */
size_t XConsoleShellXTcpServerAdapter_acceptPending(XConsoleShellXTcpServerAdapter* adapter);
/**
 * @brief 从已绑定连接读取并投递至各自会话。
 * @param adapter 已初始化的服务端适配器。
 * @param maxBytes 每个连接本轮最多读取的字节数。
 * @return 本轮处理的连接数量。
 */
size_t XConsoleShellXTcpServerAdapter_pump(XConsoleShellXTcpServerAdapter* adapter,
                                           size_t maxBytes);
/** @param adapter 服务端适配器；不能为空。 @brief 关闭并解除全部已绑定会话。 */
void XConsoleShellXTcpServerAdapter_closeAll(XConsoleShellXTcpServerAdapter* adapter);
/**
 * @brief 解除一个会话绑定。
 * @param adapter 服务端适配器；不能为空。
 * @param session 由该适配器打开的附加会话。
 * @return 成功解除返回 true；未找到返回 false。
 */
bool XConsoleShellXTcpServerAdapter_closeSession(XConsoleShellXTcpServerAdapter* adapter,
                                                 XConsoleShellSession* session);

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && MULTI_SESSION && XTCPSERVER */
#endif /* XCONSOLE_SHELL_XTCPSERVER_H */
