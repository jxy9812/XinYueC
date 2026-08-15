/** @file XSshClient.h
 * @brief SSH 客户端协议栈：XSshClient。
 * @details
 * 实现 SSH 2.0 版本协商、curve25519-sha256 / ecdh-sha2-nistp256 密钥交换、
 * ecdsa-sha2-nistp256 主机密钥签名验证、AES-256/192/128-CTR、
 * HMAC-SHA2-256、password 认证和交互式 session 通道。
 * 调用方负责创建和连接 XIODevice；客户端不直接依赖 TCP、线程或 Shell。
 * 收到主机密钥后必须通过 hostKeyVerificationRequested 信号显式确认，
 * 默认拒绝未知主机密钥。
 *
 * 文件编码：UTF-8 带 BOM，中文注释。
 */

#ifndef XSSHCLIENT_H
#define XSSHCLIENT_H

#include "XSsh_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON && XSSH_ON && XSSH_CLIENT_ON

#include "XObject.h"
#include "XIODevice.h"
#include "XProtocolIo.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

XCLASS_DEFINE_BEGING(XSshClient)
XCLASS_DEFINE_EXTEND_END(XSshClient, XObject)

/** @brief SSH 客户端；内部协议状态与密钥材料存于 m_data。 */
typedef struct XSshClient {
    XObject m_class;
    XIODevice* m_device;            /**< 借用的已连接字节流设备。 */
    XConnection* m_readyRead;       /**< 设备 readyRead 信号连接。 */
    void* m_data;                   /**< 私有协议状态。 */
    bool m_hostKeyAccepted;         /**< 主机密钥验证信号的同步回填结果。 */
} XSshClient;

XVtable* XSshClient_class_init(void);
void XSshClient_init(XSshClient* self);
XSshClient* XSshClient_create_ex(XMemoryType memory);
#define XSshClient_deinit_base XClass_deinit_base
#define XSshClient_delete_base XClass_delete_base
#define XSshClient_deleteLater XObject_deleteLater

/** @brief 绑定已连接的字节流设备，并连接 readyRead 驱动。 */
bool XSshClient_setDevice(XSshClient* self, XIODevice* device);
/** @brief 设置 password 认证使用的 UTF-8 用户名和密码；连接后不可修改。 */
bool XSshClient_setCredentials(XSshClient* self, const char* user, const char* password);
/** @brief 开始 SSH 握手；调用前必须设置设备与非空用户名。 */
bool XSshClient_start(XSshClient* self);
/** @brief 停止协议栈并断开 readyRead 连接；不关闭借用的设备。 */
void XSshClient_stop(XSshClient* self);
/** @brief 投入收到的 SSH 字节。 */
XProtocolResult XSshClient_feedData(XSshClient* self, const void* data, size_t size);
/** @brief 向已打开的 session 通道发送数据。 */
int64_t XSshClient_write(XSshClient* self, const void* data, size_t size);
/** @brief 发送 channel EOF/CLOSE，随后停止本地协议栈。 */
bool XSshClient_closeChannel(XSshClient* self);
/** @brief 刷新待发送 SSH 报文和底层设备。 */
bool XSshClient_flush(XSshClient* self);
/** @brief 查询客户端是否已经关闭。 */
bool XSshClient_isClosed(const XSshClient* self);
/** @brief 查询 password 认证是否成功。 */
bool XSshClient_isAuthenticated(const XSshClient* self);
/** @brief 查询交互式 shell 通道是否已经可写。 */
bool XSshClient_isReady(const XSshClient* self);

/** @brief 收到服务端主机密钥，参数依次为 algorithm、keyBlob、keyBlobLen。
 * @note 槽函数须调用 XSshClient_setHostKeyAccepted 同步确认或拒绝。 */
void* XSshClient_hostKeyVerificationRequested_signal(XSshClient* self,
                                                      const char* algorithm,
                                                      const void* keyBlob,
                                                      size_t keyBlobLen);
/** @brief 收到 session 通道数据，参数依次为 data、size。 */
void* XSshClient_dataReceived_signal(XSshClient* self,
                                     const void* data, size_t size);
/** @brief password 认证成功。 */
void* XSshClient_authenticated_signal(XSshClient* self);
/** @brief pty/shell 均已建立，交互通道可写。 */
void* XSshClient_ready_signal(XSshClient* self);
/** @brief 协议栈关闭。 */
void* XSshClient_closed_signal(XSshClient* self);
/** @brief 协议或传输错误，参数为 XProtocolResult。 */
void* XSshClient_errorOccurred_signal(XSshClient* self, int error);

/** @brief 回填主机密钥验证结果。 */
void XSshClient_setHostKeyAccepted(XSshClient* self, bool accepted);

#endif /* XPROTOCOL_ON && XSSH_ON && XSSH_CLIENT_ON */

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XSshClient_create
#define XSshClient_create(...) XSshClient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif /* XSSHCLIENT_H */
