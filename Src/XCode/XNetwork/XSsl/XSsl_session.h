// XSsl_session.h
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
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
typedef struct XSslSession XSslSession;

/**
 * @brief BIO 发送回调：将 [buf,len) 送到底层传输。
 * @return 已发送字节数（>0），或 XSSL_BIO_WANT_WRITE / XSSL_BIO_ERROR。
 */
typedef int (*XSslBioSend)(void* userdata, const uint8_t* buf, size_t len);

/**
 * @brief BIO 接收回调：从底层传输读取到 [buf,len)。
 * @return 已读字节数（>0），或 XSSL_BIO_WANT_READ / XSSL_BIO_ERROR。返回 0 表示对端关闭。
 */
typedef int (*XSslBioRecv)(void* userdata, uint8_t* buf, size_t len);

/* ---- 生命周期 --------------------------------------------------------- */

/**
 * @brief 创建 TLS 会话。
 * @param protocol 协议版本（XSSL_TlsV1_2 / XSSL_TlsV1_3 / XSSL_SecureProtocols）
 * @param isServer true=服务端, false=客户端
 */
XSslSession* XSsl_sessionCreate(XSslProtocol protocol, bool isServer);

/** 销毁会话（自动做 shutdown 语义前请自行调 XSsl_sessionShutdown）。 */
void XSsl_sessionDestroy(XSslSession* s);

/* ---- 配置（必须在 handshake 前调） ------------------------------------ */

/** 绑定 BIO 回调（必须；否则 handshake 无法进行）。 */
void XSsl_sessionSetBio(XSslSession* s, void* userdata,
                        XSslBioSend send_cb, XSslBioRecv recv_cb);

/** 设置 SNI 主机名（客户端；服务端接收 SNI 由内部处理）。 */
bool XSsl_sessionSetHostname(XSslSession* s, const char* hostname);

/** 设置本端证书与私钥（服务端必需，客户端可选做 mutual TLS）。所有权：外部持有，会话仅弱引用。 */
bool XSsl_sessionSetCertificate(XSslSession* s, XSslCertificate* cert, XSslKey* key);

/** 追加 CA 证书用于校验对端。可多次调用累加。 */
bool XSsl_sessionAddCaCertificate(XSslSession* s, XSslCertificate* ca);

/** 设置对端校验模式，默认 XSSL_AutoVerifyPeer。 */
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
 * @brief 推进握手（可被反复调用直到返回 XSSL_S_OK）。
 * @return XSSL_S_OK 完成；XSSL_S_WANT_READ/WRITE 需要外部驱动 BIO；XSSL_S_ERROR 失败。
 */
int XSsl_sessionHandshake(XSslSession* s);

/** 读取解密后的应用数据。返回 >0 字节数或状态码。 */
int XSsl_sessionRead(XSslSession* s, uint8_t* buf, size_t len);

/** 写入应用数据（内部加密后经 BIO 送出）。返回 >0 字节数或状态码。 */
int XSsl_sessionWrite(XSslSession* s, const uint8_t* buf, size_t len);

/** 优雅关闭（发送 close_notify）。 */
int XSsl_sessionShutdown(XSslSession* s);

/* ---- 查询 -------------------------------------------------------------- */

/** 握手是否完成。 */
bool XSsl_sessionIsEncrypted(const XSslSession* s);

/** 对端证书校验结果，0=OK；否则错误位掩码（后端相关）。 */
uint32_t XSsl_sessionVerifyResult(const XSslSession* s);

/** 当前使用的 TLS 版本字符串（"TLSv1.2"/"TLSv1.3"），未知返回 "unknown"。 */
const char* XSsl_sessionProtocolString(const XSslSession* s);

/** 当前会话使用的 cipher suite 名称，未协商返回 NULL。 */
const char* XSsl_sessionCipherName(const XSslSession* s);

/** @param s TLS 会话；可为 NULL。 @return 协商协议名借用指针，未协商返回 NULL。 */
const char* XSsl_sessionNextNegotiatedProtocol(const XSslSession* s);
/** @param s TLS 会话；可为 NULL。 @return ALPN 协商状态。 */
XSslNextProtocolNegotiationStatus XSsl_sessionNextProtocolNegotiationStatus(
    const XSslSession* s);

/** 上一次错误的简短描述（人类可读）。 */
const char* XSsl_sessionLastErrorString(const XSslSession* s);

/* ---- Peer certificate access (Qt 6.8 alignment) ---- */
#include "XVector.h"
/** Returns a newly-allocated clone of the peer leaf certificate, or NULL. */
XSslCertificate* XSsl_sessionPeerCertificate(XSslSession* s);
/** Returns a newly-allocated XVector<XSslCertificate*> of the entire peer chain (leaf first). */
XVector*         XSsl_sessionPeerCertificateChain(XSslSession* s);
#ifdef __cplusplus
}
#endif


#endif /* XSSL_SESSION_H */
