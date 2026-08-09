/** @file XSshServer.h
 * @brief SSH 服务器协议栈：XSshServer。
 * @details
 * 移植自 XConsoleShell 的 mbedTLS(PSA) 精简 SSH Server 适配器，改为独立协议栈，
 * 继承 XObject 获得信号与槽能力，内部包含 XIODevice 作为数据来源，
 * 可接入 TCP/串口等任何字节流设备，并保留原 SSH 状态机。
 *
 * 仅实现 SSH 2.0 传输层：版本协商、KEXINIT、ecdh-sha2-nistp256 密钥交换、
 * AES-128-CTR 加密、HMAC-SHA2-256 完整性、password 用户认证和 session 通道。
 * 输入字节经 bytesReceived_signal 交给宿主，宿主可通过 setter 回填查询结果，
 * 输出/回显/提示符等操作通过公共方法 write/flush/setInputEcho/emitPrompt 完成。
 *
 * @note 使用前必须先调用 setDevice/setHostContext 完成绑定，
 *       再调用 start 启动协议栈。
 *
 * 文件编码：UTF-8 带 BOM，中文注释。
 */

#ifndef XSSHSERVER_H
#define XSSHSERVER_H

#include "XSsh_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON && XSSH_ON && XSSH_SERVER_ON

#include "XObject.h"
#include "XIODevice.h"
#include "XProtocolIo.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

XCLASS_DEFINE_BEGING(XSshServer)
XCLASS_DEFINE_EXTEND_END(XSshServer, XObject)

/** @brief SSH 服务器协议栈。 */
typedef struct XSshServer {
    XObject m_class;                    /**< 基类 XObject，提供信号槽机制。 */
    XIODevice* m_device;                /**< 借用的 XIODevice 字节流设备。 */
    void* m_hostContext;                /**< 借用宿主上下文，供槽函数使用。 */
    XConnection* m_readyRead;           /**< 设备 readyRead 信号连接。 */
    void* m_data;                       /**< 协议内部状态数据。 */
    /* 查询请求槽结果；同一时刻仅一个查询在途，故用联合压缩，布尔结果用位域。 */
    union {
        int m_bytesReceivedResult;      /**< bytesReceived 信号处理结果。 */
        uint32_t m_authResult : 1;      /**< 认证查询结果。 */
        uint32_t m_isRunningResult : 1;      /**< isRunning 查询结果。 */
        uint32_t m_closeRequestedResult : 1; /**< closeRequested 查询结果。 */
        uint32_t m_suppressPromptResult : 1; /**< suppressPrompt 查询结果。 */
        struct {
            char m_userNameResult[XSSH_LOGIN_NAME_SIZE]; /**< userName 查询结果。 */
            size_t m_userNameResultLen;  /**< userName 查询结果长度。 */
        };
    };
} XSshServer;

/** @brief 初始化 SSH 服务器虚表。 */
XVtable* XSshServer_class_init(void);
/** @brief 初始化 SSH 服务器对象。 */
void XSshServer_init(XSshServer* self);
/** @brief 创建 SSH 服务器；销毁时使用 XSshServer_delete_base 释放。 */
XSshServer* XSshServer_create(void);
#define XSshServer_deinit_base XClass_deinit_base
#define XSshServer_delete_base XClass_delete_base
#define XSshServer_deleteLater XObject_deleteLater

/**
 * @brief 绑定底层字节流设备。
 * @param self SSH 服务器；不能为 NULL。
 * @param device 字节流 XIODevice；不能为 NULL。
 * @return 绑定成功返回 true；device 为空返回 false。
 * @note 绑定过程会接管设备并连接 readyRead 信号，
 *       设备就绪后自动驱动 SSH 状态机。
 */
bool XSshServer_setDevice(XSshServer* self, XIODevice* device);

/**
 * @brief 绑定宿主上下文。
 * @param self SSH 服务器；不能为 NULL。
 * @param context 宿主上下文指针；不能为 NULL。
 * @note 槽函数通过 server->m_hostContext 获取宿主绑定。
 */
void XSshServer_setHostContext(XSshServer* self, void* context);

/**
 * @brief 启动协议栈并发送 SSH 版本横幅 KEXINIT。
 * @param self SSH 服务器；不能为 NULL。
 * @return 启动成功返回 true，失败返回 false。
 */
bool XSshServer_start(XSshServer* self);

/**
 * @brief 停止协议栈并发送关闭信号。
 * @param self SSH 服务器；可为 NULL。
 */
void XSshServer_stop(XSshServer* self);

/**
 * @brief 投入一段字节到 SSH 协议状态机。
 * @param self SSH 服务器；不能为 NULL。
 * @param data 收到的字节流；NULL 且 size 为 0 时无效。
 * @param size 字节数。
 * @return 处理结果，出错时返回错误码。
 * @note 通常由 readyRead 信号触发，也可由宿主主动 pump 数据。
 */
XProtocolResult XSshServer_feedData(XSshServer* self, const void* data, size_t size);

/**
 * @brief 向设备写入 Shell 输出，按 SSH session 通道封装并处理 CRLF。
 * @param self SSH 服务器；不能为 NULL。
 * @param data 输出数据；NULL 且 size 为 0 时无效。
 * @param size 字节数。
 * @return 已写入字节数；未就绪或出错返回 -1。
 */
int64_t XSshServer_write(XSshServer* self, const void* data, size_t size);

/**
 * @brief 刷新底层设备待写缓冲。
 * @param self SSH 服务器；不能为 NULL。
 * @return 刷新成功且无待发送数据返回 true。
 */
bool XSshServer_flush(XSshServer* self);

/**
 * @brief 查询协议栈是否已关闭。
 * @param self SSH 服务器；可为 NULL。
 * @return 已关闭返回 true。
 */
bool XSshServer_isClosed(const XSshServer* self);

/**
 * @brief 设置输入回显开关。
 * @param self SSH 服务器；不能为 NULL。
 * @param enabled true 开启回显；false 关闭回显。
 * @return 设置成功返回 true。
 */
bool XSshServer_setInputEcho(XSshServer* self, bool enabled);

/**
 * @brief 向设备发送当前会话提示符。
 * @param self SSH 服务器；不能为 NULL。
 * @return 写入成功返回 true。
 */
bool XSshServer_emitPrompt(XSshServer* self);

/* ==================== 信号定义 -> 宿主 ==================== */

/**
 * @brief 输入字节信号。
 * @param self SSH 服务器。
 * @param data 输入字节缓冲区。
 * @param size 字节数。
 * @return 信号返回值。
 * @note 槽函数处理后应调用 XSshServer_setBytesReceivedResult 回填，
 *       默认结果视为 Ok。
 */
void* XSshServer_bytesReceived_signal(XSshServer* self,
                                      const void* data, size_t size);
/**
 * @brief 认证请求信号；槽函数调用 XSshServer_setAuthenticateResult 回填。
 * @param self SSH 服务器。
 * @param user 用户名缓冲区。
 * @param password 密码缓冲区。
 * @return 信号返回值。
 */
void* XSshServer_authenticateRequested_signal(XSshServer* self,
                                              const char* user,
                                              const char* password);
/**
 * @brief 查询是否运行中的信号；槽函数调用 setIsRunningResult 回填。
 * @param self SSH 服务器。
 * @return 信号返回值。
 */
void* XSshServer_isRunningRequested_signal(XSshServer* self);
/**
 * @brief 查询是否请求关闭的信号；槽函数调用 setCloseRequestedResult 回填。
 * @param self SSH 服务器。
 * @return 信号返回值。
 */
void* XSshServer_closeRequested_signal(XSshServer* self);
/**
 * @brief 查询是否抑制提示符的信号；槽函数调用 setSuppressPromptResult 回填。
 * @param self SSH 服务器。
 * @return 信号返回值。
 */
void* XSshServer_suppressPromptRequested_signal(XSshServer* self);
/**
 * @brief 查询用户名的信号；槽函数调用 setUserNameResult 回填。
 * @param self SSH 服务器。
 * @param buffer 输出缓冲区，具有 capacity 容量。
 * @param capacity 缓冲区容量。
 * @return 信号返回值。
 */
void* XSshServer_userNameRequested_signal(XSshServer* self,
                                          char* buffer, size_t capacity);
/**
 * @brief 协议栈已关闭信号。
 * @param self SSH 服务器。
 * @return 信号返回值。
 */
void* XSshServer_closed_signal(XSshServer* self);
/**
 * @brief 错误信号。
 * @param self SSH 服务器。
 * @param error 错误码，取 XProtocolResult 枚举值。
 * @return 信号返回值。
 */
void* XSshServer_errorOccurred_signal(XSshServer* self, int error);

/* ==================== setter 回填 -> 协议栈 ==================== */

/** @brief 回填 bytesReceived 结果 @param self SSH 服务器 @param result 处理结果 */
void XSshServer_setBytesReceivedResult(XSshServer* self, int result);
/** @brief 回填认证结果 @param self SSH 服务器 @param result 是否认证通过 */
void XSshServer_setAuthenticateResult(XSshServer* self, bool result);
/** @brief 回填 isRunning 结果 @param self SSH 服务器 @param result 是否运行 */
void XSshServer_setIsRunningResult(XSshServer* self, bool result);
/** @brief 回填 closeRequested 结果 @param self SSH 服务器 @param result 是否请求关闭 */
void XSshServer_setCloseRequestedResult(XSshServer* self, bool result);
/** @brief 回填 suppressPrompt 结果 @param self SSH 服务器 @param result 是否抑制提示符 */
void XSshServer_setSuppressPromptResult(XSshServer* self, bool result);
/** @brief 回填 userName 结果 @param self SSH 服务器 @param name 用户名；可为 NULL @param len 长度（不含 NUL） */
void XSshServer_setUserNameResult(XSshServer* self,
                                  const char* name, size_t len);

#endif /* XPROTOCOL_ON && XSSH_ON && XSSH_SERVER_ON */

#ifdef __cplusplus
}
#endif

#endif /* XSSHSERVER_H */