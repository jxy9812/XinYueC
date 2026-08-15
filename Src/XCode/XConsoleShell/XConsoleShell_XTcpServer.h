/**
 * @file XConsoleShell_XTcpServer.h
 * @brief XTcpServer 到 XConsoleShell 多会话的库级适配器。
 * @details
 * 适配器从 XTcpServer 的待处理连接队列取得 XTcpSocket，以固定数组绑定到
 * Shell 附加会话。调用方在自己的事件循环中调用 acceptPending 和 pump；适配器
 * 不拥有 server 或 socket，不监听、不关闭 server，也不包含平台网络头文件。
 *
 * SSH/Telnet 由独立协议栈（XSshServer/XTelnetServer）实现，协议栈继承 XObject
 * 获得信号与槽能力，内部以 XIODevice 作为数据来源，通过 setDevice 绑定连接
 * socket，通过信号槽与 Shell 会话交互；本适配器只负责协议栈生命周期、会话
 * 绑定和 pump 驱动。
 */

#ifndef XCONSOLE_SHELL_XTCPSERVER_H
#define XCONSOLE_SHELL_XTCPSERVER_H

#include "XConsoleShell_XTcpSocket.h"
#include "XTcpServer.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_MULTI_SESSION_ON && XCONSOLE_SHELL_XTCPSERVER_BACKEND_ON

#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
#include "XSshServer.h"
#endif
#if XCONSOLE_SHELL_TELNET_PROTOCOL_ON
#include "XTelnetServer.h"
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
    XConsoleShell* shell;                            /**< 借用的多会话 Shell；槽函数使用。 */
    XConsoleShellSession* session;                   /**< Shell 管理的附加会话。 */
    XTcpSocket* socket;                              /**< XTcpServer 借用的连接对象。 */
    int terminalColumns;                              /**< 最近一次已观察到的 SSH 列数。 */
    int terminalRows;                                 /**< 最近一次已观察到的 SSH 行数。 */
    bool terminalSizeRefreshPending;                  /**< 是否需要在协议处理后重绘。 */
    XConsoleShellXTcpSocketAdapter adapter;          /**< 连接的 I/O 适配器。 */
    XConsoleShellXTcpServerProtocol protocol;        /**< 本连接使用的传输协议。 */
    /* SSH 与 Telnet 协议栈互斥，故共用一块存储。 */
    union {
#if XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON
        XSshServer* ssh;                             /**< 连接绑定的 SSH 协议栈（动态创建）。 */
#endif
#if XCONSOLE_SHELL_TELNET_PROTOCOL_ON
        XTelnetServer telnet;                        /**< 连接绑定的 Telnet 协议栈（栈上）。 */
#endif
    };
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
