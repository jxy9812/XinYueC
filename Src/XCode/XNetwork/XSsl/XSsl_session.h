/**
 * @file       XSsl_session.h
 * @brief      后端无关的 TLS 会话状态机接口。
 * @details    会话通过 BIO 回调连接到 XAbstractSocket 或其他字节流，负责握手、
 *             加解密、证书校验和 ALPN 状态，不拥有底层传输对象。
 * @copyright  Copyright (C) 2026 Your Project Authors
 * @license    MIT OR LGPL-3.0-only
 */
// TLS 会话（Session）抽象层：位于 XSsl_platform.h 之上、XSslSocket 之下。
//
//   XSslSocket        <-- 类层：对齐 QSslSocket，继承 XTcpSocket
//        |
//   XSsl_session.h    <-- 会话层（本文件）：不透明 XSslSession + BIO 回调
//        |
//   XSsl_platform.h   <-- 平台层：XSslContext / XSslCertificate / XSslKey / RNG
//        |
//   openssl / mbedtls <-- 具体后端
//
// 设计：完全后端无关的接口。用户提供 send/recv BIO 回调（比如通过
// XAbstractSocket 或纯内存管道），会话内部驱动 TLS 状态机。
// 参照 mbedtls_ssl_set_bio / SSL_set_bio 的语义。

#ifndef XSSL_SESSION_H
#define XSSL_SESSION_H

#include "XSsl_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 会话返回码 -------------------------------------------------------- */
#define XSSL_S_OK          0   /**< 操作完成 */
#define XSSL_S_WANT_READ   1   /**< 需要 BIO 提供更多入站字节 */
#define XSSL_S_WANT_WRITE  2   /**< 需要 BIO 将更多出站字节送出 */
#define XSSL_S_ERROR      (-1) /**< 致命错误 */
#define XSSL_S_CLOSED     (-2) /**< 对端已发 close_notify */

/* ---- BIO 回调返回值 --------------------------------------------------- */
#define XSSL_BIO_WANT_READ   (-1)  /**< recv 侧：无数据可读、非致命 */
#define XSSL_BIO_WANT_WRITE  (-2)  /**< send 侧：底层缓冲已满、非致命 */
#define XSSL_BIO_ERROR       (-3)  /**< 底层 I/O 致命错误 */

/* ---- 对端校验模式 ----------------------------------------------------- */
typedef enum XSslPeerVerifyMode {
    XSSL_VerifyNone     = 0, /**< 不校验对端 */
    XSSL_QueryPeer      = 1, /**< 请求对端证书但不校验 */
    XSSL_VerifyPeer     = 2, /**< 强制校验对端证书 */
    XSSL_AutoVerifyPeer = 3  /**< 客户端=VerifyPeer, 服务端=VerifyNone */
} XSslPeerVerifyMode;

/* ---- 不透明会话对象 --------------------------------------------------- */
/** @brief TLS 会话不透明类型；由 XSsl_sessionCreate/Destroy 管理。 */
typedef struct XSslSession XSslSession;

/**
 * @brief BIO 发送回调：将 [buf,len) 送到底层传输。
 * @param userdata 绑定会话时传入的用户数据借用指针。
 * @param buf 待发送字节；借用，回调返回前有效。
 * @param len buf 中的字节数。
 * @return 已发送字节数（>0），或 XSSL_BIO_WANT_WRITE / XSSL_BIO_ERROR。
 */
typedef int (*XSslBioSend)(void* userdata, const uint8_t* buf, size_t len);

/**
 * @brief BIO 接收回调：从底层传输读取到 [buf,len)。
 * @param userdata 绑定会话时传入的用户数据借用指针。
 * @param buf 输出缓冲区；回调必须写入不超过 len 字节。
 * @param len buf 的容量字节数。
 * @return 已读字节数（>0），或 XSSL_BIO_WANT_READ / XSSL_BIO_ERROR。返回 0 表示对端关闭。
 */
typedef int (*XSslBioRecv)(void* userdata, uint8_t* buf, size_t len);

/* ---- 生命周期 --------------------------------------------------------- */

/**
 * @brief 创建 TLS 会话。
 * @param protocol 协议版本（XSSL_TlsV1_2 / XSSL_TlsV1_3 / XSSL_SecureProtocols）。
 * @param isServer true 表示服务端，false 表示客户端。
 * @return 新会话所有权；调用者必须使用 XSsl_sessionDestroy 释放，失败返回 NULL。
 */
XSslSession* XSsl_sessionCreate(XSslProtocol protocol, bool isServer);

/**
 * @brief 销毁 TLS 会话。
 * @param s 会话所有权；可为 NULL。
 * @return 无；不会替调用者向对端发送 close_notify，需要优雅关闭时先调用 shutdown。
 */
void XSsl_sessionDestroy(XSslSession* s);

/* ---- 配置（必须在 handshake 前调） ------------------------------------ */

/**
 * @brief 绑定 BIO 回调。
 * @param s TLS 会话；不能为 NULL。
 * @param userdata 回调用户数据借用指针；会话只保存指针，不释放。
 * @param send_cb 发送回调；不能为 NULL。
 * @param recv_cb 接收回调；不能为 NULL。
 * @return 无；必须在首次握手前调用，回调对象由调用方保持有效。
 */
void XSsl_sessionSetBio(XSslSession* s, void* userdata,
                        XSslBioSend send_cb, XSslBioRecv recv_cb);

/**
 * @brief 设置客户端 SNI 主机名。
 * @param s TLS 会话；不能为 NULL。
 * @param hostname UTF-8 主机名；借用并由会话复制，可为 NULL 以清除。
 * @return 设置成功返回 true；握手已开始或内存不足返回 false。
 */
bool XSsl_sessionSetHostname(XSslSession* s, const char* hostname);

/**
 * @brief 设置允许的 TLS 密码套件列表。
 * @param s TLS 会话；不能为 NULL。
 * @param cipherSuites UTF-8 名称列表，使用冒号、逗号或空白分隔；借用并复制，可为 NULL 以清除。
 * @return 设置成功返回 true；握手已开始、名称无效或后端不支持返回 false。
 */
bool XSsl_sessionSetCipherSuites(XSslSession* s, const char* cipherSuites);

/**
 * @brief 设置本端证书与私钥。
 * @param s TLS 会话；不能为 NULL。
 * @param cert 本端证书；借用，会话只保存弱引用，可为 NULL。
 * @param key 本端私钥；借用，会话只保存弱引用，可为 NULL。
 * @return 成功返回 true；握手已开始、参数不匹配或后端拒绝返回 false。
 * @note 服务端通常必须提供证书和私钥，客户端仅在 mutual TLS 时需要。
 */
bool XSsl_sessionSetCertificate(XSslSession* s, XSslCertificate* cert, XSslKey* key);

/**
 * @brief 追加 CA 证书用于校验对端。
 * @param s TLS 会话；不能为 NULL。
 * @param ca CA 证书；借用，会话不释放，可为 NULL 时忽略。
 * @return 成功返回 true；握手已开始或后端拒绝返回 false。
 */
bool XSsl_sessionAddCaCertificate(XSslSession* s, XSslCertificate* ca);

/**
 * @brief 从 CA 目录追加证书用于校验对端。
 * @param s TLS 会话；不能为 NULL。
 * @param path UTF-8 目录路径；借用，调用期间有效，不能为 NULL。
 * @return 成功返回 true；握手已开始、路径无效或加载失败返回 false。
 */
bool XSsl_sessionAddCaPath(XSslSession* s, const char* path);

/**
 * @brief 从 CRL 文件加载证书吊销列表。
 * @param s TLS 会话；不能为 NULL。
 * @param path UTF-8 文件路径；借用，调用期间有效，不能为 NULL。
 * @return 成功返回 true；握手已开始、路径无效或加载失败返回 false。
 */
bool XSsl_sessionAddCrlFile(XSslSession* s, const char* path);

/**
 * @brief 从 CRL 目录加载证书吊销列表。
 * @param s TLS 会话；不能为 NULL。
 * @param path UTF-8 目录路径；借用，调用期间有效，不能为 NULL。
 * @return 成功返回 true；握手已开始、路径无效或加载失败返回 false。
 */
bool XSsl_sessionAddCrlPath(XSslSession* s, const char* path);

/**
 * @brief 设置对端证书校验模式。
 * @param s TLS 会话；不能为 NULL。
 * @param mode 校验模式；默认值为 XSSL_AutoVerifyPeer。
 * @return 无；握手开始后修改不会影响已经协商的会话。
 */
void XSsl_sessionSetPeerVerify(XSslSession* s, XSslPeerVerifyMode mode);

/**
 * @brief 设置允许发送或选择的 ALPN 协议列表。
 * @param s TLS 会话；不能为 NULL。
 * @param protocols XVector<XByteArray*>；元素借用并在函数内深拷贝；NULL/空列表清除 ALPN。
 * @return 成功返回 true；必须在首次握手前调用。
 */
bool XSsl_sessionSetAllowedNextProtocols(XSslSession* s, const XVector* protocols);

/* ---- 状态机驱动 ------------------------------------------------------- */

/**
 * @brief 推进 TLS 握手。
 * @param s TLS 会话；不能为 NULL。
 * @return XSSL_S_OK 表示完成；XSSL_S_WANT_READ/WRITE 表示需要外部驱动 BIO；XSSL_S_ERROR 表示失败。
 * @note 可重复调用，直到返回 XSSL_S_OK 或致命错误。
 */
int XSsl_sessionHandshake(XSslSession* s);

/**
 * @brief 读取解密后的应用数据。
 * @param s TLS 会话；不能为 NULL。
 * @param buf 输出缓冲区；不能为 NULL，容量由 len 指定。
 * @param len 输出缓冲区字节数。
 * @return 已读字节数（大于 0），或 XSSL_S_WANT_READ、XSSL_S_WANT_WRITE、XSSL_S_CLOSED、XSSL_S_ERROR。
 */
int XSsl_sessionRead(XSslSession* s, uint8_t* buf, size_t len);

/**
 * @brief 写入应用数据并经 BIO 加密发送。
 * @param s TLS 会话；不能为 NULL。
 * @param buf 输入明文；借用，调用期间有效，len 为 0 时可为 NULL。
 * @param len 明文字节数。
 * @return 已接受字节数（大于 0），或 XSSL_S_WANT_READ、XSSL_S_WANT_WRITE、XSSL_S_ERROR。
 */
int XSsl_sessionWrite(XSslSession* s, const uint8_t* buf, size_t len);

/**
 * @brief 优雅关闭 TLS 会话并发送 close_notify。
 * @param s TLS 会话；不能为 NULL。
 * @return XSSL_S_OK 表示关闭通知已发送，或返回 WANT_WRITE、WANT_READ、ERROR。
 */
int XSsl_sessionShutdown(XSslSession* s);

/* ---- 查询 -------------------------------------------------------------- */

/** @brief 判断握手是否完成并进入加密状态。 @param s 会话；可为 NULL。 @return 已加密返回 true，否则返回 false。 */
bool XSsl_sessionIsEncrypted(const XSslSession* s);

/** @brief 获取对端证书校验结果。 @param s 会话；可为 NULL。 @return 0 表示通过，否则返回后端相关错误位掩码。 */
uint32_t XSsl_sessionVerifyResult(const XSslSession* s);

/** @brief 获取协商的 TLS 版本名称。 @param s 会话；可为 NULL。 @return 会话持有的 UTF-8 借用字符串；未知时为 unknown，不得释放。 */
const char* XSsl_sessionProtocolString(const XSslSession* s);

/** @brief 获取协商的密码套件名称。 @param s 会话；可为 NULL。 @return 会话持有的 UTF-8 借用字符串；未协商返回 NULL，不得释放。 */
const char* XSsl_sessionCipherName(const XSslSession* s);

/** @param s TLS 会话；可为 NULL。 @return 协商协议名借用指针，未协商返回 NULL。 */
const char* XSsl_sessionNextNegotiatedProtocol(const XSslSession* s);
/** @param s TLS 会话；可为 NULL。 @return ALPN 协商状态。 */
XSslNextProtocolNegotiationStatus XSsl_sessionNextProtocolNegotiationStatus(
    const XSslSession* s);

/** @brief 获取最近一次错误的可读描述。 @param s 会话；可为 NULL。 @return 会话持有的 UTF-8 借用字符串；无错误时返回 NULL，不得释放。 */
const char* XSsl_sessionLastErrorString(const XSslSession* s);

/* ---- Peer certificate access (Qt 6.8 alignment) ---- */
#include "XVector.h"
/**
 * @brief 获取对端叶证书副本。
 * @param s TLS 会话；可为 NULL。
 * @return 新证书所有权；调用者使用 XSsl_certificateDelete 释放，未完成握手或无证书返回 NULL。
 */
XSslCertificate* XSsl_sessionPeerCertificate(XSslSession* s);
/**
 * @brief 获取对端完整证书链副本，叶证书位于首元素。
 * @param s TLS 会话；可为 NULL。
 * @return 新 XVector 所有权；元素为新证书指针，调用者负责释放元素和容器；无链或失败返回 NULL。
 */
XVector*         XSsl_sessionPeerCertificateChain(XSslSession* s);
#ifdef __cplusplus
}
#endif


#endif /* XSSL_SESSION_H */
