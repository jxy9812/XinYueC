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

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 单个 TCP 会话绑定；socket 为 XTcpServer 借用对象。 */
typedef struct XConsoleShellXTcpServerBinding {
    XConsoleShellSession* session;                   /**< Shell 管理的附加会话。 */
    XTcpSocket* socket;                              /**< XTcpServer 借用的连接对象。 */
    XConsoleShellXTcpSocketAdapter adapter;          /**< 连接的 I/O 适配器。 */
} XConsoleShellXTcpServerBinding;

/** @brief TCP 服务端 Shell 适配器；所有成员均为借用或固定存储。 */
typedef struct XConsoleShellXTcpServerAdapter {
    XConsoleShell* shell;                            /**< 借用的多会话 Shell。 */
    XTcpServer* server;                              /**< 借用的 TCP 服务端。 */
    XConsoleShellXTcpServerBinding
        bindings[XCONSOLE_SHELL_MAX_SESSIONS - 1u];  /**< 固定连接绑定槽。 */
} XConsoleShellXTcpServerAdapter;

/**
 * @brief 初始化 TCP 服务端适配器。
 * @param adapter 待初始化适配器；不能为空。
 * @param shell 已启用多会话的 Shell 借用指针。
 * @param server 已监听的 XTcpServer 借用指针。
 */
void XConsoleShellXTcpServerAdapter_init(XConsoleShellXTcpServerAdapter* adapter,
                                         XConsoleShell* shell, XTcpServer* server);
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
