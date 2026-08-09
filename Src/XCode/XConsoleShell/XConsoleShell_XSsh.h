/**
 * @file XConsoleShell_XSsh.h
 * @brief XConsoleShell 的 mbedTLS 精简 SSH Server 适配器。
 * @details
 * 适配器在 XTcpServer 之上实现 SSH 2.0 传输、密钥交换(ecdh-sha2-nistp256)、
 * 主机认证(ecdsa-sha2-nistp256)、AES-128-CTR 加密、HMAC-SHA2-256 完整性、
 * 密码登录和 session 通道。它不创建线程、不监听端口，底层字节传输由调用方
 * 提供的 XConsoleShellIo 回调拥有；应用收到网络数据后调用 feedData，Shell
 * 输出经 makeIo 交给独立会话。
 *
 * 本适配器只依赖 mbedTLS 的 PSA Crypto 公共 API，不使用 TLS 高性能 API，
 * 适合嵌入式裁剪场景。主机密钥首次启动生成并持久化到
 * XCONSOLE_SHELL_XSSH_HOSTKEY_FILE 指定的文件，此后跨进程重启保持不变。
 */

#ifndef XCONSOLE_SHELL_XSSH_H
#define XCONSOLE_SHELL_XSSH_H

#include "XConsoleShell.h"
#include "XConsoleShellLogin.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_XSSHSERVER_BACKEND_ON

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 不透明 SSH 适配器；由 XConsoleShellSshAdapter_create 分配。 */
typedef struct XConsoleShellSshAdapter XConsoleShellSshAdapter;

/**
 * @brief 创建并绑定底层字节传输。
 * @param transport 底层传输回调集合；适配器只借用，可为 NULL。
 * @return 新适配器所有权；失败返回 NULL，调用方必须用 destroy 释放。
 * @note 创建成功后立即向传输写出 SSH 版本横幅。
 */
XConsoleShellSshAdapter* XConsoleShellSshAdapter_create(
    const XConsoleShellIo* transport);

/** @brief 销毁适配器并释放 PSA 密钥与密码操作上下文。 */
void XConsoleShellSshAdapter_destroy(XConsoleShellSshAdapter* adapter);

/**
 * @brief 生成供 Shell 输出使用的 I/O 回调。
 * @param adapter 已创建的 SSH 适配器。
 * @param io 输出的 Shell I/O 回调集合，由调用方提供存储。
 * @return 生成成功返回 true；参数为空或底层传输缺少 write 回调返回 false。
 */
bool XConsoleShellSshAdapter_makeIo(XConsoleShellSshAdapter* adapter,
                                    XConsoleShellIo* io);

/**
 * @brief 绑定目标 Shell 与会话。
 * @param adapter SSH 适配器；不能为空。
 * @param shell 目标多会话 Shell；不能为空。
 * @param session 目标会话；不能为空。
 * @note 必须在收到任何认证或通道消息前设置；认证成功后登录状态写入该会话。
 */
void XConsoleShellSshAdapter_setSession(XConsoleShellSshAdapter* adapter,
                                        XConsoleShell* shell,
                                        XConsoleShellSession* session);

/**
 * @brief 投入一段网络字节到 SSH 状态机。
 * @param adapter SSH 适配器；不能为空。
 * @param shell 目标 Shell；不能为空。
 * @param session 目标会话；不能为空。
 * @param data 收到的网络字节；只在调用期间借用。
 * @param size 字节数。
 * @return 处理成功返回 XConsoleResult_Ok；协议错误或传输失败返回错误码。
 */
XConsoleResult XConsoleShellSshAdapter_feedData(XConsoleShellSshAdapter* adapter,
                                                XConsoleShell* shell,
                                                XConsoleShellSession* session,
                                                const void* data, size_t size);

/**
 * @brief 将待发送缓冲区中的字节写入底层传输。
 * @param adapter SSH 适配器；不能为空。
 * @return 成功写入或暂时无待发送数据返回 true；传输失败返回 false。
 * @note 调用方应在事件循环空闲时周期性调用，避免握手包积压导致死锁。
 */
bool XConsoleShellSshAdapter_flush(XConsoleShellSshAdapter* adapter);

/**
 * @brief 查询适配器是否已进入关闭状态。
 * @param adapter SSH 适配器；可为 NULL。
 * @return 已关闭或参数为空返回 true。
 */
bool XConsoleShellSshAdapter_isClosed(const XConsoleShellSshAdapter* adapter);

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XSSHSERVER */
#endif /* XCONSOLE_SHELL_XSSH_H */
