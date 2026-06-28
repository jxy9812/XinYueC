/**
 * @file XSsl_mbedtls.c
 * @brief SSL/TLS mbedTLS后端实现
 * 
 * 使用mbedTLS库实现SSL/TLS功能
 * 适用于嵌入式系统和资源受限环境
 * 
 * 注意：此实现需要完整版mbedTLS库，包含以下模块：
 * - entropy (熵源)
 * - ctr_drbg (随机数生成器)
 * - pk (公钥抽象层)
 * - platform (平台抽象层)
 * 
 * 如果使用精简版mbedTLS，请确保启用 XSSL_USE_MBEDTLS_MINIMAL 宏
 */

#include "XSsl_platform.h"
#include "XMemory.h"
#include "XString.h"
#include "XByteArray.h"

#ifdef XSSL_USE_MBEDTLS

/* 检查mbedTLS版本 */
#include <mbedtls/version.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509_csr.h>
#include <mbedtls/error.h>
#include <mbedtls/debug.h>

/* 检查是否有完整的mbedTLS模块 */
#if defined(MBEDTLS_ENTROPY_C) && defined(MBEDTLS_CTR_DRBG_C)
    #define XSSL_HAS_ENTROPY_MODULE
    #include <mbedtls/entropy.h>
    #include <mbedtls/ctr_drbg.h>
#endif

#if defined(MBEDTLS_PK_C)
    #define XSSL_HAS_PK_MODULE
    #include <mbedtls/pk.h>
#endif

#if defined(MBEDTLS_PLATFORM_C)
    #define XSSL_HAS_PLATFORM_MODULE
    #include <mbedtls/platform.h>
#endif

#if defined(MBEDTLS_NET_C)
    #define XSSL_HAS_NET_MODULE
    #include <mbedtls/net_sockets.h>
#endif

#include <string.h>
#include <stdio.h>
#include <time.h>

/* =========================================================================
 * 内部结构体定义
 * ========================================================================= */

/**
 * @brief SSL上下文结构体
 */
struct XSslContext {
    mbedtls_ssl_config config;      /**< SSL配置 */
#ifdef XSSL_HAS_ENTROPY_MODULE
    mbedtls_entropy_context entropy; /**< 熵上下文 */
    mbedtls_ctr_drbg_context ctr_drbg; /**< 随机数生成器 */
#endif
    XSslProtocol protocol;          /**< 协议版本 */
    bool isServer;                  /**< 是否为服务端 */
};

/**
 * @brief 证书结构体
 */
struct XSslCertificate {
    mbedtls_x509_crt cert;          /**< mbedTLS证书 */
    XSslEncodingFormat format;      /**< 编码格式 */
    char* subjectInfo;              /**< 主题信息缓存 */
    char* issuerInfo;               /**< 颁发者信息缓存 */
};

#ifdef XSSL_HAS_PK_MODULE
/**
 * @brief 密钥结构体
 */
struct XSslKey {
    mbedtls_pk_context pk;          /**< mbedTLS密钥 */
    XSslKeyAlgorithm algorithm;     /**< 密钥算法 */
    XSslKeyType type;               /**< 密钥类型 */
    XSslEncodingFormat format;      /**< 编码格式 */
};
#endif

/* =========================================================================
 * 全局变量
 * ========================================================================= */

static bool g_sslInitialized = false;
#ifdef XSSL_HAS_ENTROPY_MODULE
static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_ctr_drbg;
#endif

/* =========================================================================
 * 内部辅助函数
 * ========================================================================= */

/**
 * @brief 将mbedTLS错误码转换为XSslError
 */
static XSslError mbedtlsToXSslError(int ret)
{
    // 根据mbedTLS错误码映射到XSslError
    switch (ret) {
        case MBEDTLS_ERR_X509_CERT_VERIFY_FAILED:
            return XSSL_CertificateUntrusted;
        case MBEDTLS_ERR_X509_INVALID_FORMAT:
            return XSSL_UnableToGetIssuerCertificate;
        case MBEDTLS_ERR_X509_INVALID_VERSION:
        case MBEDTLS_ERR_X509_INVALID_SERIAL:
            return XSSL_InvalidSslVersion;
        case MBEDTLS_ERR_X509_INVALID_ALG:
        case MBEDTLS_ERR_X509_UNKNOWN_SIG_ALG:
            return XSSL_CertificateSignatureFailed;
        case MBEDTLS_ERR_X509_INVALID_NAME:
            return XSSL_SubjectIssuerMismatch;
        case MBEDTLS_ERR_X509_INVALID_DATE:
            return XSSL_CertificateNotYetValid;
        case MBEDTLS_ERR_X509_FUTURE_VER:
            return XSSL_CertificateNotYetValid;
        case MBEDTLS_ERR_X509_EXPIRED:
            return XSSL_CertificateExpired;
        case MBEDTLS_ERR_X509_SIG_MISMATCH:
            return XSSL_CertificateSignatureFailed;
        case MBEDTLS_ERR_SSL_NO_CLIENT_CERTIFICATE:
            return XSSL_NoPeerCertificate;
        case MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE:
            return XSSL_HandshakeFailed;
        case MBEDTLS_ERR_SSL_BAD_HS_PROTOCOL_VERSION:
            return XSSL_InvalidSslVersion;
        case MBEDTLS_ERR_SSL_NO_USABLE_CIPHERSUITE:
            return XSSL_HandshakeFailed;
        case MBEDTLS_ERR_SSL_INVALID_MAC:
            return XSSL_HandshakeFailed;
        case MBEDTLS_ERR_SSL_INVALID_RECORD:
            return XSSL_HandshakeFailed;
        case MBEDTLS_ERR_SSL_CONN_EOF:
            return XSSL_HandshakeFailed;
        case MBEDTLS_ERR_SSL_UNKNOWN_CIPHER:
            return XSSL_HandshakeFailed;
        default:
            return XSSL_UnspecifiedError;
    }
}

/**
 * @brief 将XSslProtocol转换为mbedTLS传输方式
 */
static int xsslToMbedTlsVersion(XSslProtocol protocol)
{
    switch (protocol) {
        case XSSL_TlsV1_0:
        case XSSL_TlsV1_0OrLater:
            return MBEDTLS_SSL_MINOR_VERSION_1;
        case XSSL_TlsV1_1:
        case XSSL_TlsV1_1OrLater:
            return MBEDTLS_SSL_MINOR_VERSION_2;
        case XSSL_TlsV1_2:
        case XSSL_TlsV1_2OrLater:
        case XSSL_TlsV1_3:
        case XSSL_TlsV1_3OrLater:
        case XSSL_SecureProtocols:
        case XSSL_AnyProtocol:
        default:
            return MBEDTLS_SSL_MINOR_VERSION_3;
        case XSSL_DtlsV1_0:
        case XSSL_DtlsV1_0OrLater:
            return MBEDTLS_SSL_MINOR_VERSION_2;
        case XSSL_DtlsV1_2:
        case XSSL_DtlsV1_2OrLater:
            return MBEDTLS_SSL_MINOR_VERSION_3;
    }
}

/**
 * @brief 判断是否为DTLS协议
 */
static bool isDtlsProtocol(XSslProtocol protocol)
{
    return (protocol == XSSL_DtlsV1_0 || protocol == XSSL_DtlsV1_0OrLater ||
            protocol == XSSL_DtlsV1_2 || protocol == XSSL_DtlsV1_2OrLater);
}

/**
 * @brief 获取mbedTLS密钥类型
 */
static mbedtls_pk_type_t xsslToMbedTlsKeyType(XSslKeyAlgorithm algorithm)
{
    switch (algorithm) {
        case XSSL_KeyAlgorithm_Rsa:
            return MBEDTLS_PK_RSA;
        case XSSL_KeyAlgorithm_Ec:
            return MBEDTLS_PK_ECKEY;
        case XSSL_KeyAlgorithm_Dsa:
            // mbedTLS不直接支持DSA，使用默认
            return MBEDTLS_PK_NONE;
        case XSSL_KeyAlgorithm_Dh:
            // mbedTLS中DH作为密钥交换而非签名算法
            return MBEDTLS_PK_NONE;
        default:
            return MBEDTLS_PK_NONE;
    }
}

/* =========================================================================
 * 平台初始化与后端查询实现
 * ========================================================================= */

bool XSsl_platform_init(void)
{
    if (g_sslInitialized) {
        return true;
    }

#ifdef XSSL_HAS_ENTROPY_MODULE
    int ret;

    // 初始化熵源
    mbedtls_entropy_init(&g_entropy);

    // 初始化随机数生成器
    mbedtls_ctr_drbg_init(&g_ctr_drbg);

    // 使用默认熵源种子随机数生成器
    const char* pers = "XSsl_mbedtls";
    ret = mbedtls_ctr_drbg_seed(&g_ctr_drbg, mbedtls_entropy_func, &g_entropy,
                                 (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        mbedtls_entropy_free(&g_entropy);
        mbedtls_ctr_drbg_free(&g_ctr_drbg);
        return false;
    }
#endif

    g_sslInitialized = true;
    return true;
}

void XSsl_platform_deinit(void)
{
    if (!g_sslInitialized) {
        return;
    }

#ifdef XSSL_HAS_ENTROPY_MODULE
    mbedtls_ctr_drbg_free(&g_ctr_drbg);
    mbedtls_entropy_free(&g_entropy);
#endif

    g_sslInitialized = false;
}

const char* XSsl_backendName(void)
{
    return "mbedtls";
}

bool XSsl_isClassImplemented(XSslImplementedClass cls)
{
    switch (cls) {
        case XSSL_ImplementedClass_Key:
        case XSSL_ImplementedClass_Certificate:
        case XSSL_ImplementedClass_Socket:
        case XSSL_ImplementedClass_Dtls:
            return true;
        case XSSL_ImplementedClass_DiffieHellman:
        case XSSL_ImplementedClass_EllipticCurve:
            return true;
        case XSSL_ImplementedClass_DtlsCookie:
            return true;
        default:
            return false;
    }
}

bool XSsl_isFeatureSupported(XSslSupportedFeature feature)
{
    switch (feature) {
        case XSSL_SupportedFeature_CertificateVerification:
        case XSSL_SupportedFeature_Psk:
        case XSSL_SupportedFeature_SessionTicket:
            return true;
        case XSSL_SupportedFeature_ClientSideAlpn:
        case XSSL_SupportedFeature_ServerSideAlpn:
            return true;
        case XSSL_SupportedFeature_Ocsp:
            // mbedTLS支持OCSP装订，但需要额外配置
            return false;
        case XSSL_SupportedFeature_Alerts:
            return true;
        default:
            return false;
    }
}

XSslProtocol XSsl_supportedProtocols(void)
{
    // mbedTLS支持TLS 1.0/1.1/1.2，部分版本支持TLS 1.3
    return XSSL_SecureProtocols;
}

XSslProtocol XSsl_defaultProtocol(void)
{
    return XSSL_SecureProtocols;
}

/* =========================================================================
 * SSL上下文实现
 * ========================================================================= */

XSslContext* XSsl_contextCreate(XSslProtocol protocol)
{
    if (!g_sslInitialized) {
        if (!XSsl_platform_init()) {
            return NULL;
        }
    }

    XSslContext* ctx = (XSslContext*)XMalloc_System(sizeof(XSslContext));
    if (!ctx) {
        return NULL;
    }
    memset(ctx, 0, sizeof(XSslContext));

    ctx->protocol = protocol;
    ctx->isServer = false;

    // 初始化SSL配置
    mbedtls_ssl_config_init(&ctx->config);
#ifdef XSSL_HAS_ENTROPY_MODULE
    mbedtls_entropy_init(&ctx->entropy);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
#endif

    // 设置默认配置
    int ret;
    int transport = isDtlsProtocol(protocol) ? MBEDTLS_SSL_TRANSPORT_DATAGRAM 
                                              : MBEDTLS_SSL_TRANSPORT_STREAM;

    ret = mbedtls_ssl_config_defaults(&ctx->config, MBEDTLS_SSL_IS_CLIENT,
                                       transport, MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        mbedtls_ssl_config_free(&ctx->config);
#ifdef XSSL_HAS_ENTROPY_MODULE
        mbedtls_entropy_free(&ctx->entropy);
        mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
#endif
        XFree_System(ctx);
        return NULL;
    }

    // 设置协议版本
    int min_version = xsslToMbedTlsVersion(protocol);
    mbedtls_ssl_conf_min_version(&ctx->config, MBEDTLS_SSL_MAJOR_VERSION_3, min_version);

#ifdef XSSL_HAS_ENTROPY_MODULE
    // 设置随机数生成器
    const char* pers = "XSsl_context";
    ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func, &ctx->entropy,
                                 (const unsigned char*)pers, strlen(pers));
    if (ret != 0) {
        mbedtls_ssl_config_free(&ctx->config);
        mbedtls_entropy_free(&ctx->entropy);
        mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
        XFree_System(ctx);
        return NULL;
    }

    mbedtls_ssl_conf_rng(&ctx->config, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);
#else
    /* 没有熵源模块时，使用硬件随机数或外部随机源 */
    /* 用户需要通过 XSSL_setHardwareRng 设置随机源 */
#endif

    return ctx;
}

void XSsl_contextDestroy(XSslContext* ctx)
{
    if (!ctx) {
        return;
    }

    mbedtls_ssl_config_free(&ctx->config);
#ifdef XSSL_HAS_ENTROPY_MODULE
    mbedtls_entropy_free(&ctx->entropy);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
#endif
    XFree_System(ctx);
}

/* =========================================================================
 * 证书操作实现
 * ========================================================================= */

XSslCertificate* XSsl_certificateFromPem(const char* data, size_t len)
{
    if (!data || len == 0) {
        return NULL;
    }

    XSslCertificate* cert = (XSslCertificate*)XMalloc_System(sizeof(XSslCertificate));
    if (!cert) {
        return NULL;
    }
    memset(cert, 0, sizeof(XSslCertificate));

    mbedtls_x509_crt_init(&cert->cert);
    cert->format = XSSL_Pem;

    int ret = mbedtls_x509_crt_parse(&cert->cert, (const unsigned char*)data, len + 1);
    if (ret != 0) {
        XFree_System(cert);
        return NULL;
    }

    return cert;
}

XSslCertificate* XSsl_certificateFromDer(const uint8_t* data, size_t len)
{
    if (!data || len == 0) {
        return NULL;
    }

    XSslCertificate* cert = (XSslCertificate*)XMalloc_System(sizeof(XSslCertificate));
    if (!cert) {
        return NULL;
    }
    memset(cert, 0, sizeof(XSslCertificate));

    mbedtls_x509_crt_init(&cert->cert);
    cert->format = XSSL_Der;

    int ret = mbedtls_x509_crt_parse_der(&cert->cert, data, len);
    if (ret != 0) {
        XFree_System(cert);
        return NULL;
    }

    return cert;
}

XSslCertificate* XSsl_certificateLoad(const char* path, XSslEncodingFormat format)
{
    if (!path) {
        return NULL;
    }

    XSslCertificate* cert = (XSslCertificate*)XMalloc_System(sizeof(XSslCertificate));
    if (!cert) {
        return NULL;
    }
    memset(cert, 0, sizeof(XSslCertificate));

    mbedtls_x509_crt_init(&cert->cert);
    cert->format = format;

    int ret = mbedtls_x509_crt_parse_file(&cert->cert, path);
    if (ret != 0) {
        XFree_System(cert);
        return NULL;
    }

    return cert;
}

void XSsl_certificateDestroy(XSslCertificate* cert)
{
    if (!cert) {
        return;
    }

    mbedtls_x509_crt_free(&cert->cert);

    if (cert->subjectInfo) {
        XFree_System(cert->subjectInfo);
    }
    if (cert->issuerInfo) {
        XFree_System(cert->issuerInfo);
    }

    XFree_System(cert);
}

/* =========================================================================
 * 密钥操作实现
 * ========================================================================= */

#ifdef XSSL_HAS_PK_MODULE

XSslKey* XSsl_keyFromPem(const char* data, size_t len, XSslKeyAlgorithm algorithm, 
                          XSslKeyType type, const char* passphrase)
{
    if (!data || len == 0) {
        return NULL;
    }

    XSslKey* key = (XSslKey*)XMalloc_System(sizeof(XSslKey));
    if (!key) {
        return NULL;
    }
    memset(key, 0, sizeof(XSslKey));

    mbedtls_pk_init(&key->pk);
    key->algorithm = algorithm;
    key->type = type;
    key->format = XSSL_Pem;

    int ret;
    if (passphrase) {
        ret = mbedtls_pk_parse_key(&key->pk, (const unsigned char*)data, len + 1,
                                    (const unsigned char*)passphrase, strlen(passphrase));
    } else {
        ret = mbedtls_pk_parse_key(&key->pk, (const unsigned char*)data, len + 1,
                                    NULL, 0);
    }

    if (ret != 0) {
        XFree_System(key);
        return NULL;
    }

    return key;
}

XSslKey* XSsl_keyFromDer(const uint8_t* data, size_t len, XSslKeyAlgorithm algorithm,
                          XSslKeyType type)
{
    if (!data || len == 0) {
        return NULL;
    }

    XSslKey* key = (XSslKey*)XMalloc_System(sizeof(XSslKey));
    if (!key) {
        return NULL;
    }
    memset(key, 0, sizeof(XSslKey));

    mbedtls_pk_init(&key->pk);
    key->algorithm = algorithm;
    key->type = type;
    key->format = XSSL_Der;

    int ret = mbedtls_pk_parse_key(&key->pk, data, len, NULL, 0);
    if (ret != 0) {
        XFree_System(key);
        return NULL;
    }

    return key;
}

void XSsl_keyDestroy(XSslKey* key)
{
    if (!key) {
        return;
    }

    mbedtls_pk_free(&key->pk);
    XFree_System(key);
}

#else /* XSSL_HAS_PK_MODULE */

/* 没有PK模块时的存根实现 */
XSslKey* XSsl_keyFromPem(const char* data, size_t len, XSslKeyAlgorithm algorithm, 
                          XSslKeyType type, const char* passphrase)
{
    (void)data; (void)len; (void)algorithm; (void)type; (void)passphrase;
    return NULL; /* 不支持 */
}

XSslKey* XSsl_keyFromDer(const uint8_t* data, size_t len, XSslKeyAlgorithm algorithm,
                          XSslKeyType type)
{
    (void)data; (void)len; (void)algorithm; (void)type;
    return NULL; /* 不支持 */
}

void XSsl_keyDestroy(XSslKey* key)
{
    (void)key;
}

#endif /* XSSL_HAS_PK_MODULE */

/* =========================================================================
 * 随机数生成实现
 * ========================================================================= */

bool XSsl_randomBytes(uint8_t* buf, size_t len)
{
    if (!buf || len == 0) {
        return false;
    }

    if (!g_sslInitialized) {
        if (!XSsl_platform_init()) {
            return false;
        }
    }

#ifdef XSSL_HAS_ENTROPY_MODULE
    int ret = mbedtls_ctr_drbg_random(&g_ctr_drbg, buf, len);
    return (ret == 0);
#else
    /* 没有熵源模块时，返回失败 */
    /* 用户需要通过 XSSL_setHardwareRng 设置随机源 */
    return false;
#endif
}

/* =========================================================================
 * 扩展功能：证书信息获取
 * ========================================================================= */

/**
 * @brief 获取证书主题信息
 */
char* XSsl_certificateSubjectInfo(const XSslCertificate* cert)
{
    if (!cert) {
        return NULL;
    }

    if (cert->subjectInfo) {
        return XStrdup(cert->subjectInfo);
    }

    char buf[1024];
    int ret = mbedtls_x509_dn_gets(buf, sizeof(buf), &cert->cert.subject);
    if (ret < 0) {
        return NULL;
    }

    return XStrdup(buf);
}

/**
 * @brief 获取证书颁发者信息
 */
char* XSsl_certificateIssuerInfo(const XSslCertificate* cert)
{
    if (!cert) {
        return NULL;
    }

    if (cert->issuerInfo) {
        return XStrdup(cert->issuerInfo);
    }

    char buf[1024];
    int ret = mbedtls_x509_dn_gets(buf, sizeof(buf), &cert->cert.issuer);
    if (ret < 0) {
        return NULL;
    }

    return XStrdup(buf);
}

/**
 * @brief 检查证书是否有效
 */
bool XSsl_certificateIsValid(const XSslCertificate* cert)
{
    if (!cert) {
        return false;
    }

    // 检查时间有效性
    time_t now = time(NULL);
    struct tm* tm_now = gmtime(&now);

    mbedtls_x509_time* not_before = &cert->cert.valid_from;
    mbedtls_x509_time* not_after = &cert->cert.valid_to;

    // 简单的时间比较
    if (not_before->year > tm_now->tm_year + 1900) {
        return false;
    }
    if (not_after->year < tm_now->tm_year + 1900) {
        return false;
    }

    return true;
}

/**
 * @brief 获取证书过期时间
 */
bool XSsl_certificateExpiryDate(const XSslCertificate* cert, int* year, int* month, 
                                 int* day, int* hour, int* min, int* sec)
{
    if (!cert) {
        return false;
    }

    if (year) *year = cert->cert.valid_to.year;
    if (month) *month = cert->cert.valid_to.mon;
    if (day) *day = cert->cert.valid_to.day;
    if (hour) *hour = cert->cert.valid_to.hour;
    if (min) *min = cert->cert.valid_to.min;
    if (sec) *sec = cert->cert.valid_to.sec;

    return true;
}

/* =========================================================================
 * 扩展功能：密钥信息获取
 * ========================================================================= */

/**
 * @brief 获取密钥位数
 */
int XSsl_keyBits(const XSslKey* key)
{
#ifdef XSSL_HAS_PK_MODULE
    if (!key) {
        return 0;
    }

    return (int)mbedtls_pk_get_bitlen(&key->pk);
#else
    (void)key;
    return 0;
#endif
}

/**
 * @brief 检查密钥是否为私钥
 */
bool XSsl_keyIsPrivate(const XSslKey* key)
{
    if (!key) {
        return false;
    }

    return (key->type == XSSL_PrivateKey);
}

#ifdef XSSL_HAS_PK_MODULE
/**
 * @brief 将密钥导出为PEM格式
 */
bool XSsl_keyToPem(const XSslKey* key, char* buf, size_t* len)
{
    if (!key || !buf || !len) {
        return false;
    }

    int ret = mbedtls_pk_write_key_pem(&key->pk, (unsigned char*)buf, *len);
    if (ret < 0) {
        return false;
    }

    *len = ret;
    return true;
}

/**
 * @brief 将公钥导出为PEM格式
 */
bool XSsl_publicKeyToPem(const XSslKey* key, char* buf, size_t* len)
{
    if (!key || !buf || !len) {
        return false;
    }

    int ret = mbedtls_pk_write_pubkey_pem(&key->pk, (unsigned char*)buf, *len);
    if (ret < 0) {
        return false;
    }

    *len = ret;
    return true;
}
#else
bool XSsl_keyToPem(const XSslKey* key, char* buf, size_t* len)
{
    (void)key; (void)buf; (void)len;
    return false;
}

bool XSsl_publicKeyToPem(const XSslKey* key, char* buf, size_t* len)
{
    (void)key; (void)buf; (void)len;
    return false;
}
#endif

#endif /* XSSL_USE_MBEDTLS */