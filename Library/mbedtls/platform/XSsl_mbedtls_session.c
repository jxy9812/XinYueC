// XSsl_mbedtls_session.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
//
// XSsl 基于 mbedTLS 后端的会话实现。
// 当定义了 XSSL_USE_MBEDTLS 时启用。
//   - 所有内存分配均通过 XMemory 管理。
//   - 套接字级别的 BIO 管理委托给 mbedTLS 内置接口。

#include "XSsl_session.h"

#ifdef XSSL_USE_MBEDTLS

/* NOTE(mbedtls 4.x): the public API does not expose alert msg_callback nor
 * session-ticket receive callback, so XSslSocket_alertSent/alertReceived/
 * newSessionTicketReceived signals cannot be wired here. They remain
 * emittable externally; once the OpenSSL backend lands, revisit. */


#include "XSsl_mbedtls_p.h"
#include "XMemory.h"
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <string.h>
#include "XPrintf.h"
#include "XVector.h"

 /* ---- XSslSession 结构体：保存 mbedTLS SSL 上下文和会话状态 ---- */
struct XSslSession {
    mbedtls_ssl_context     ssl;
    mbedtls_ssl_config      conf;
    mbedtls_x509_crt        ca_chain;   /* 从信任存储加载的 CA 证书链 */
    bool                    ca_inited;

    /* BIO */
    void* bio_user;
    XSslBioSend             bio_send;
    XSslBioRecv             bio_recv;

    /* 会话状态标志 */
    bool                    is_server;
    bool                    encrypted;
    XSslPeerVerifyMode      verify_mode;
    bool                    setup_done;

    /* 证书和私钥 */
    XSslCertificate* own_cert;
    XSslKey* own_key;

    /* 错误信息缓冲区 */
    char                    err_buf[128];
};

static XSslProtocol s_proto_of(XSslProtocol p) { return p; }

static int map_bio_send(void* ctx, const unsigned char* buf, size_t len) {
    XSslSession* s = (XSslSession*)ctx;
    if (!s->bio_send) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    int r = s->bio_send(s->bio_user, buf, len);
    if (r >= 0) return r;
    if (r == XSSL_BIO_WANT_WRITE) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}
static int map_bio_recv(void* ctx, unsigned char* buf, size_t len) {
    XSslSession* s = (XSslSession*)ctx;
    if (!s->bio_recv) return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    int r = s->bio_recv(s->bio_user, buf, len);
    if (r > 0) return r;
    if (r == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    if (r == XSSL_BIO_WANT_READ) return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
}

static int session_map_result(XSslSession* s, int r) {
    if (r == 0) return XSSL_S_OK;
    if (r == MBEDTLS_ERR_SSL_WANT_READ) return XSSL_S_WANT_READ;
    if (r == MBEDTLS_ERR_SSL_WANT_WRITE) return XSSL_S_WANT_WRITE;
    if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) return XSSL_S_CLOSED;
    /* 其他错误：记录日志后返回错误 */
    mbedtls_strerror(r, s->err_buf, sizeof(s->err_buf));
    XPrintf("[XSsl] mbedtls err %d (-0x%04x): %s\n", r, (unsigned)(-r), s->err_buf);
    return XSSL_S_ERROR;
}

/* 平台初始化辅助函数 */
static void xssl_ensure_platform(void) {
    static int inited = 0;
    if (inited) return;
    if (XSsl_platform_init()) { inited = 1; }
    else { XPrintf("[XSsl] XSsl_platform_init FAILED\n"); }
}

/* ---- 会话创建与销毁 ---- */
XSslSession* XSsl_sessionCreate(XSslProtocol protocol, bool isServer) {
    xssl_ensure_platform();
    XSslSession* s = (XSslSession*)XMalloc_System(sizeof(XSslSession));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));

    mbedtls_ssl_init(&s->ssl);
    mbedtls_ssl_config_init(&s->conf);
    mbedtls_x509_crt_init(&s->ca_chain);
    s->is_server = isServer;
    s->verify_mode = XSSL_AutoVerifyPeer;

    int endpoint = isServer ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT;
    int transport = MBEDTLS_SSL_TRANSPORT_STREAM; /* DTLS 暂不支持，后续扩展 */

    if (mbedtls_ssl_config_defaults(&s->conf, endpoint, transport,
        MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        goto fail;
    }
    /* TLS 版本配置 */
    /* TLS version selection: XSSL_TlsV1_x -> exact, XSSL_TlsV1_xOrLater -> min only,
     * XSSL_SecureProtocols/XSSL_AnyProtocol -> [TLS1_2, TLS1_2] until TLS1_3 backend is complete. */
    int vmin = MBEDTLS_SSL_VERSION_TLS1_2;
    int vmax = MBEDTLS_SSL_VERSION_TLS1_2;
    bool has_max = true;
    switch (protocol) {
    case XSSL_TlsV1_2:        vmin = vmax = MBEDTLS_SSL_VERSION_TLS1_2; has_max = true;  break;
    case XSSL_TlsV1_3:        vmin = vmax = MBEDTLS_SSL_VERSION_TLS1_3; has_max = true;  break;
    case XSSL_TlsV1_2OrLater: vmin = MBEDTLS_SSL_VERSION_TLS1_2;        has_max = false; break;
    case XSSL_TlsV1_3OrLater: vmin = MBEDTLS_SSL_VERSION_TLS1_3;        has_max = false; break;
    case XSSL_AnyProtocol:
    case XSSL_SecureProtocols:
        /* TODO(mbedtls 4.x): TLS1.3 ClientHello triggers -0x6600 on some servers (e.g. baidu). Cap at 1.2 until upstream fix. */
    default:                  vmin = MBEDTLS_SSL_VERSION_TLS1_2; vmax = MBEDTLS_SSL_VERSION_TLS1_2; has_max = true; break;
    }
    mbedtls_ssl_conf_min_tls_version(&s->conf, vmin);
    if (has_max) mbedtls_ssl_conf_max_tls_version(&s->conf, vmax);
    /* AutoVerify: client REQUIRED, server NONE */
    if (isServer) {
        mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_NONE);
    }
    else {
        mbedtls_ssl_conf_authmode(&s->conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    }
    (void)s_proto_of;
    return s;
fail:
    mbedtls_x509_crt_free(&s->ca_chain);
    mbedtls_ssl_config_free(&s->conf);
    mbedtls_ssl_free(&s->ssl);
    XFree_System(s);
    return NULL;
}

void XSsl_sessionDestroy(XSslSession* s) {
    if (!s) return;
    mbedtls_ssl_free(&s->ssl);
    mbedtls_ssl_config_free(&s->conf);
    mbedtls_x509_crt_free(&s->ca_chain);
    XFree_System(s);
}

/* ---- 会话配置：BIO、证书、验证模式 ---- */
void XSsl_sessionSetBio(XSslSession* s, void* user, XSslBioSend send_cb, XSslBioRecv recv_cb) {
    if (!s) return;
    s->bio_user = user;
    s->bio_send = send_cb;
    s->bio_recv = recv_cb;
    mbedtls_ssl_set_bio(&s->ssl, s, map_bio_send, map_bio_recv, NULL);
}

bool XSsl_sessionSetHostname(XSslSession* s, const char* hostname) {
    if (!s) return false;
    if (!hostname) {
#if defined(MBEDTLS_SSL_SERVER_NAME_INDICATION)
        return mbedtls_ssl_set_hostname(&s->ssl, NULL) == 0;
#else
        return true;
#endif
    }
#if defined(MBEDTLS_SSL_SERVER_NAME_INDICATION)
    return mbedtls_ssl_set_hostname(&s->ssl, hostname) == 0;
#else
    (void)hostname; return true;
#endif
}

bool XSsl_sessionSetCertificate(XSslSession* s, XSslCertificate* cert, XSslKey* key) {
    if (!s || !cert || !key) return false;
    s->own_cert = cert;
    s->own_key = key;
    return mbedtls_ssl_conf_own_cert(&s->conf, &cert->crt, &key->pk) == 0;
}

bool XSsl_sessionAddCaCertificate(XSslSession* s, XSslCertificate* ca) {
    if (!s || !ca) return false;
    /* 添加 CA 证书到信任链 */
    s->ca_inited = true;
    mbedtls_ssl_conf_ca_chain(&s->conf, &ca->crt, NULL);
    return true;
}

void XSsl_sessionSetPeerVerify(XSslSession* s, XSslPeerVerifyMode mode) {
    if (!s) return;
    s->verify_mode = mode;
    int m = MBEDTLS_SSL_VERIFY_REQUIRED;
    switch (mode) {
    case XSSL_VerifyNone:     m = MBEDTLS_SSL_VERIFY_NONE;     break;
    case XSSL_QueryPeer:      m = MBEDTLS_SSL_VERIFY_OPTIONAL; break;
    case XSSL_VerifyPeer:     m = MBEDTLS_SSL_VERIFY_REQUIRED; break;
    case XSSL_AutoVerifyPeer: m = s->is_server ? MBEDTLS_SSL_VERIFY_NONE
        : MBEDTLS_SSL_VERIFY_REQUIRED; break;
    }
    mbedtls_ssl_conf_authmode(&s->conf, m);
}

/* ---- 内部辅助函数：延迟初始化（setup + BIO 绑定） ---- */
static bool ensure_setup(XSslSession* s) {
    if (s->setup_done) return true;
    int r = mbedtls_ssl_setup(&s->ssl, &s->conf);
    if (r != 0) {
        mbedtls_strerror(r, s->err_buf, sizeof(s->err_buf));
        return false;
    }
    /* setup 完成后绑定 BIO 读写回调 */
    mbedtls_ssl_set_bio(&s->ssl, s, map_bio_send, map_bio_recv, NULL);
    s->setup_done = true;
    return true;
}

int XSsl_sessionHandshake(XSslSession* s) {
    if (!s) return XSSL_S_ERROR;
    if (!ensure_setup(s)) return XSSL_S_ERROR;
    int r = mbedtls_ssl_handshake(&s->ssl);
    int mapped = session_map_result(s, r);
    if (mapped == XSSL_S_OK) s->encrypted = true;
    return mapped;
}

int XSsl_sessionRead(XSslSession* s, uint8_t* buf, size_t len) {
    if (!s || !buf || !len) return XSSL_S_ERROR;
    int r = mbedtls_ssl_read(&s->ssl, buf, len);
    if (r > 0) return r;
    /* WANT_READ/WANT_WRITE -> return 0 so caller does not confuse status
       code with legitimate 1/2-byte read counts. */
    if (r == MBEDTLS_ERR_SSL_WANT_READ) return 0;
    if (r == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
    if (r == 0) return 0;
    if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) { s->encrypted = false; return XSSL_S_CLOSED; }
    mbedtls_strerror(r, s->err_buf, sizeof(s->err_buf));
    XPrintf("[XSsl] read err %d (-0x%04x): %s\n", r, (unsigned)(-r), s->err_buf);
    return XSSL_S_ERROR;
}

int XSsl_sessionWrite(XSslSession* s, const uint8_t* buf, size_t len) {
    if (!s || !buf || !len) return XSSL_S_ERROR;
    int r = mbedtls_ssl_write(&s->ssl, buf, len);
    if (r > 0) return r;
    /* WANT_READ/WANT_WRITE -> 0 (would-block); do not collide with byte counts. */
    if (r == MBEDTLS_ERR_SSL_WANT_READ) return 0;
    if (r == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
    if (r == 0) return 0;
    if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) { s->encrypted = false; return XSSL_S_CLOSED; }
    mbedtls_strerror(r, s->err_buf, sizeof(s->err_buf));
    XPrintf("[XSsl] write err %d (-0x%04x): %s\n", r, (unsigned)(-r), s->err_buf);
    return XSSL_S_ERROR;
}

int XSsl_sessionShutdown(XSslSession* s) {
    if (!s) return XSSL_S_ERROR;
    int r = mbedtls_ssl_close_notify(&s->ssl);
    if (r == 0) return XSSL_S_OK;
    if (r == MBEDTLS_ERR_SSL_WANT_READ)  return XSSL_S_WANT_READ;
    if (r == MBEDTLS_ERR_SSL_WANT_WRITE) return XSSL_S_WANT_WRITE;
    /* Peer already gone / lower BIO error is expected during close; do not log. */
    return XSSL_S_CLOSED;

}
/* ---- 会话信息查询 ---- */
bool XSsl_sessionIsEncrypted(const XSslSession* s) {
    return s ? s->encrypted : false;
}
uint32_t XSsl_sessionVerifyResult(const XSslSession* s) {
    if (!s) return (uint32_t)-1;
    return mbedtls_ssl_get_verify_result((mbedtls_ssl_context*)&s->ssl);
}
const char* XSsl_sessionProtocolString(const XSslSession* s) {
    if (!s) return "unknown";
    const char* v = mbedtls_ssl_get_version((mbedtls_ssl_context*)&s->ssl);
    return v ? v : "unknown";
}
const char* XSsl_sessionCipherName(const XSslSession* s) {
    if (!s) return NULL;
    return mbedtls_ssl_get_ciphersuite((mbedtls_ssl_context*)&s->ssl);
}
const char* XSsl_sessionLastErrorString(const XSslSession* s) {
    return s ? s->err_buf : "";
}



/* ---- Peer certificate access (Qt 6.8 alignment) ------------------------ */
XSslCertificate* XSsl_sessionPeerCertificate(XSslSession* s) {
    if (!s) return NULL;
    const mbedtls_x509_crt* c = mbedtls_ssl_get_peer_cert(&s->ssl);
    if (!c || !c->raw.p || c->raw.len == 0) return NULL;
    return XSsl_certificateFromDer(c->raw.p, c->raw.len);
}

XVector* XSsl_sessionPeerCertificateChain(XSslSession* s) {
    if (!s) return NULL;
    const mbedtls_x509_crt* c = mbedtls_ssl_get_peer_cert(&s->ssl);
    if (!c) return NULL;
    XVector* v = XVector_create(sizeof(XSslCertificate*));
    if (!v) return NULL;
    for (const mbedtls_x509_crt* it = c; it != NULL; it = it->next) {
        if (!it->raw.p || it->raw.len == 0) continue;
        XSslCertificate* one = XSsl_certificateFromDer(it->raw.p, it->raw.len);
        if (one) XVector_push_back_1_base(v, &one);
    }
    return v;
}

#endif /* XSSL_USE_MBEDTLS */

