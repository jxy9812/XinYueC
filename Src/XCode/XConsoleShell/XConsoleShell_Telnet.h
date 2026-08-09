/**
 * @file XConsoleShell_Telnet.h
 * @brief XConsoleShell 的轻量 Telnet 字节过滤适配器。
 * @details
 * 适配器处理 IAC 转义、DO/DONT/WILL/WONT 协商、子协商跳过和 CR-NUL 规则。
 * 未支持的选项会被显式拒绝，不分配内存、不创建线程、不包含网络平台头文件。
 * 底层传输由调用方提供的 XConsoleShellIo 回调拥有；应用收到网络数据后调用
 * feedData，输出回调可通过 makeIo 交给独立 Shell 会话。
 */

#ifndef XCONSOLE_SHELL_TELNET_H
#define XCONSOLE_SHELL_TELNET_H

#include "XConsoleShell.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_TELNET_PROTOCOL_ON

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Telnet 输入状态；仅适配器内部状态机使用。 */
typedef enum XConsoleShellTelnetState {
    XConsoleShellTelnetState_Data = 0,      /**< 普通文本数据。 */
    XConsoleShellTelnetState_Iac,           /**< 已收到 IAC。 */
    XConsoleShellTelnetState_Option,        /**< 等待协商选项号。 */
    XConsoleShellTelnetState_Subnegotiation,/**< 跳过子协商负载。 */
    XConsoleShellTelnetState_SubnegotiationIac /**< 子协商中的 IAC。 */
} XConsoleShellTelnetState;

/** @brief Telnet 适配器；transport 为借用回调集合，不由适配器释放。 */
typedef struct XConsoleShellTelnetAdapter {
    XConsoleShellIo transport;              /**< 借用的底层字节传输。 */
    XConsoleShellSession* session;          /**< 绑定的 Shell 会话；用于提示符取用户名。 */
    XConsoleShellTelnetState state;         /**< 输入协议状态。 */
    uint8_t negotiation;                    /**< 等待选项号的协商命令。 */
    bool afterCarriageReturn;               /**< 用于吞掉 CR 后的 NUL。 */
    bool echoEnabled;                       /**< 服务端是否回显普通输入；密码输入时由 Shell 关闭。 */
    bool echoPendingCr;                     /**< 已回显 CR，等待吞掉 LF 以避免重复。 */
    bool echoEscape;                        /**< 正在跳过 ESC 转义序列，不回显。 */
} XConsoleShellTelnetAdapter;

/**
 * @brief 绑定底层传输并清空协议状态。
 * @param adapter 待初始化适配器；不能为空。
 * @param transport 底层字节传输回调；适配器只借用，可为 NULL。
 */
void XConsoleShellTelnetAdapter_init(XConsoleShellTelnetAdapter* adapter,
                                     const XConsoleShellIo* transport);
/**
 * @brief 生成供 Shell 输出使用的 I/O 回调。
 * @param adapter 已初始化的 Telnet 适配器。
 * @param io 输出的 Shell I/O 回调集合，由调用方提供存储。
 * @return 适配成功返回 true；参数为空返回 false。
 */
bool XConsoleShellTelnetAdapter_makeIo(XConsoleShellTelnetAdapter* adapter,
                                       XConsoleShellIo* io);
/**
 * @brief 绑定会话；提示符回调会读取会话当前登录用户名。
 * @param adapter Telnet 适配器；不能为空。
 * @param session 目标 Shell 会话；可为 NULL（表示未绑定）。
 */
void XConsoleShellTelnetAdapter_setSession(XConsoleShellTelnetAdapter* adapter,
                                           XConsoleShellSession* session);
/**
 * @brief 向传输发送当前会话提示符（未登录用默认名，已登录用用户名）。
 * @param adapter Telnet 适配器；不能为空。
 * @return 写入成功返回 true；传输失败返回 false。
 */
bool XConsoleShellTelnetAdapter_emitPrompt(XConsoleShellTelnetAdapter* adapter);
/**
 * @brief 过滤一段 Telnet 数据并投入默认或指定会话。
 * @param adapter Telnet 适配器；不能为空。
 * @param shell 目标 Shell；不能为空。
 * @param session 目标会话；NULL 表示默认会话。
 * @param data 收到的网络字节；只在调用期间借用。
 * @param size 字节数。
 * @return 过滤后最后一个有效字节对应的 Shell 结果码。
 */
XConsoleResult XConsoleShellTelnetAdapter_feedData(XConsoleShellTelnetAdapter* adapter,
                                                   XConsoleShell* shell,
                                                   XConsoleShellSession* session,
                                                   const void* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_TELNET_PROTOCOL_ON */
#endif /* XCONSOLE_SHELL_TELNET_H */
