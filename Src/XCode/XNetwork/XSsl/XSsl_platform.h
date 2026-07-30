/**
 * @file XSsl_platform.h
 * @brief SSL/TLS平台抽象接口（参考Qt 6.8 QSsl命名空间）
 *
 * 职责：
 * - SSL/TLS操作的统一抽象接口
 * - 支持多种SSL后端（OpenSSL、mbedTLS等）
 * - 证书、密钥、上下文管理
 *
 * 架构层级：
 *   XSslSocket / XSslCertificate / XSslKey  ← 业务中间层（通用代码）
 *        │
 *   XSsl_platform.h (本文件)                ← 平台抽象
 *   ├────── openssl/XSsl_openssl.c          ← OpenSSL 实现
 *   ├────── mbedtls/XSsl_mbedtls.c          ← mbedTLS 实现
 *   └────── ...
 *
 * 使用方式：
 *   编译时通过宏选择后端：
 *   - XSSL_USE_OPENSSL: 使用OpenSSL
 *   - XSSL_USE_MBEDTLS: 使用mbedTLS
 */

#ifndef XSSL_PLATFORM_H
#define XSSL_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XClass.h"
#include "XString.h"
#include "XByteArray.h"
#include "XVector.h"

#ifdef __cplusplus
extern "C" {
#endif

    /* =========================================================================
     * 一、SslProtocol - SSL/TLS协议版本（参考Qt QSsl::SslProtocol）
     * ========================================================================= */

    typedef enum XSslProtocol {
        XSSL_TlsV1_0 = 0,           /**< TLSv1.0 */
        XSSL_TlsV1_0OrLater = 5,    /**< TLSv1.0及更新版本 */
        XSSL_TlsV1_1 = 1,           /**< TLSv1.1 */
        XSSL_TlsV1_1OrLater = 6,    /**< TLSv1.1及更新版本 */
        XSSL_TlsV1_2 = 2,           /**< TLSv1.2 */
        XSSL_TlsV1_2OrLater = 7,    /**< TLSv1.2及更新版本 */
        XSSL_TlsV1_3 = 12,          /**< TLSv1.3 */
        XSSL_TlsV1_3OrLater = 13,   /**< TLSv1.3及更新版本 */
        XSSL_DtlsV1_0 = 8,          /**< DTLSv1.0 */
        XSSL_DtlsV1_0OrLater = 9,   /**< DTLSv1.0及更新版本 */
        XSSL_DtlsV1_2 = 10,         /**< DTLSv1.2 */
        XSSL_DtlsV1_2OrLater = 11,  /**< DTLSv1.2及更新版本 */
        XSSL_AnyProtocol = 3,       /**< 任何支持的协议 */
        XSSL_SecureProtocols = 4,   /**< 默认选项，使用已知安全的协议 */
        XSSL_UnknownProtocol = -1   /**< 无法确定的协议 */
    } XSslProtocol;

    /* =========================================================================
     * 二、KeyAlgorithm - 密钥算法（参考Qt QSsl::KeyAlgorithm）
     * ========================================================================= */

    typedef enum XSslKeyAlgorithm {
        XSSL_KeyAlgorithm_Rsa = 1,      /**< RSA算法 */
        XSSL_KeyAlgorithm_Dsa = 2,      /**< DSA算法 */
        XSSL_KeyAlgorithm_Ec = 3,       /**< 椭圆曲线算法 */
        XSSL_KeyAlgorithm_Dh = 4,       /**< Diffie-Hellman算法 */
        XSSL_KeyAlgorithm_Opaque = 0    /**< 不透明密钥（如PKCS#11） */
    } XSslKeyAlgorithm;

    /* =========================================================================
     * 三、KeyType - 密钥类型（参考Qt QSsl::KeyType）
     * ========================================================================= */

    typedef enum XSslKeyType {
        XSSL_PrivateKey = 0,    /**< 私钥 */
        XSSL_PublicKey = 1      /**< 公钥 */
    } XSslKeyType;

    /* =========================================================================
     * 四、EncodingFormat - 编码格式（参考Qt QSsl::EncodingFormat）
     * ========================================================================= */

    typedef enum XSslEncodingFormat {
        XSSL_Pem = 0,    /**< PEM格式 */
        XSSL_Der = 1     /**< DER格式 */
    } XSslEncodingFormat;

    /* =========================================================================
     * 五、AlternativeNameEntryType - 备用名称条目类型
     * ========================================================================= */

    typedef enum XSslAlternativeNameEntryType {
        XSSL_EmailEntry = 0,       /**< 邮箱条目 */
        XSSL_DnsEntry = 1,         /**< DNS主机名条目 */
        XSSL_IpAddressEntry = 2    /**< IP地址条目 */
    } XSslAlternativeNameEntryType;

    /* =========================================================================
     * 六、AlertLevel - 警告级别（Qt 6.0+）
     * ========================================================================= */

    typedef enum XSslAlertLevel {
        XSSL_AlertLevel_Warning = 0,   /**< 非致命警告 */
        XSSL_AlertLevel_Fatal = 1,     /**< 致命警告，连接将被关闭 */
        XSSL_AlertLevel_Unknown = 2    /**< 未知级别 */
    } XSslAlertLevel;

    /* =========================================================================
     * 七、AlertType - 警告类型（Qt 6.0+，参考RFC 8446）
     * ========================================================================= */

    typedef enum XSslAlertType {
        XSSL_AlertType_CloseNotify = 0,
        XSSL_AlertType_UnexpectedMessage = 10,
        XSSL_AlertType_BadRecordMac = 20,
        XSSL_AlertType_RecordOverflow = 22,
        XSSL_AlertType_DecompressionFailure = 30,
        XSSL_AlertType_HandshakeFailure = 40,
        XSSL_AlertType_NoCertificate = 41,
        XSSL_AlertType_BadCertificate = 42,
        XSSL_AlertType_UnsupportedCertificate = 43,
        XSSL_AlertType_CertificateRevoked = 44,
        XSSL_AlertType_CertificateExpired = 45,
        XSSL_AlertType_CertificateUnknown = 46,
        XSSL_AlertType_IllegalParameter = 47,
        XSSL_AlertType_UnknownCa = 48,
        XSSL_AlertType_AccessDenied = 49,
        XSSL_AlertType_DecodeError = 50,
        XSSL_AlertType_DecryptError = 51,
        XSSL_AlertType_ExportRestriction = 60,
        XSSL_AlertType_ProtocolVersion = 70,
        XSSL_AlertType_InsufficientSecurity = 71,
        XSSL_AlertType_InternalError = 80,
        XSSL_AlertType_InappropriateFallback = 86,
        XSSL_AlertType_UserCancelled = 90,
        XSSL_AlertType_NoRenegotiation = 100,
        XSSL_AlertType_MissingExtension = 109,
        XSSL_AlertType_UnsupportedExtension = 110,
        XSSL_AlertType_CertificateUnobtainable = 111,
        XSSL_AlertType_UnrecognizedName = 112,
        XSSL_AlertType_BadCertificateStatusResponse = 113,
        XSSL_AlertType_BadCertificateHashValue = 114,
        XSSL_AlertType_UnknownPskIdentity = 115,
        XSSL_AlertType_CertificateRequired = 116,
        XSSL_AlertType_NoApplicationProtocol = 120,
        XSSL_AlertType_UnknownAlertMessage = 255
    } XSslAlertType;

    /* =========================================================================
     * 八、SslOption - SSL选项（参考Qt QSsl::SslOption）
     * ========================================================================= */

    typedef enum XSslOption {
        XSSL_OptionDisableEmptyFragments = 0x01,           /**< 禁用空片段插入 */
        XSSL_OptionDisableSessionTickets = 0x02,           /**< 禁用SSL会话票证扩展 */
        XSSL_OptionDisableCompression = 0x04,              /**< 禁用SSL压缩扩展 */
        XSSL_OptionDisableServerNameIndication = 0x08,     /**< 禁用SNI扩展 */
        XSSL_OptionDisableLegacyRenegotiation = 0x10,      /**< 禁用旧式重协商 */
        XSSL_OptionDisableSessionSharing = 0x20,           /**< 禁用会话共享 */
        XSSL_OptionDisableSessionPersistence = 0x40,       /**< 禁用会话持久化 */
        XSSL_OptionDisableServerCipherPreference = 0x80    /**< 禁用服务器密码偏好 */
    } XSslOption;

    typedef int XSslOptions; /**< SSL选项位掩码 */

    /* =========================================================================
     * 九、ImplementedClass - 实现的类（Qt 6.1+）
     * ========================================================================= */

    typedef enum XSslImplementedClass {
        XSSL_ImplementedClass_Key = 0,              /**< XSslKey */
        XSSL_ImplementedClass_Certificate = 1,      /**< XSslCertificate */
        XSSL_ImplementedClass_Socket = 2,           /**< XSslSocket */
        XSSL_ImplementedClass_DiffieHellman = 3,    /**< XSslDiffieHellmanParameters */
        XSSL_ImplementedClass_EllipticCurve = 4,    /**< XSslEllipticCurve */
        XSSL_ImplementedClass_Dtls = 5,             /**< XDtls */
        XSSL_ImplementedClass_DtlsCookie = 6        /**< XDtlsClientVerifier */
    } XSslImplementedClass;

    /* =========================================================================
     * 十、SupportedFeature - 支持的特性（Qt 6.1+）
     * ========================================================================= */

    typedef enum XSslSupportedFeature {
        XSSL_SupportedFeature_CertificateVerification = 0,  /**< 证书验证 */
        XSSL_SupportedFeature_ClientSideAlpn = 1,           /**< 客户端ALPN */
        XSSL_SupportedFeature_ServerSideAlpn = 2,           /**< 服务端ALPN */
        XSSL_SupportedFeature_Ocsp = 3,                      /**< OCSP装订 */
        XSSL_SupportedFeature_Psk = 4,                       /**< 预共享密钥 */
        XSSL_SupportedFeature_SessionTicket = 5,             /**< 会话票证 */
        XSSL_SupportedFeature_Alerts = 6                     /**< 警告消息 */
    } XSslSupportedFeature;

    /** @brief TLS 应用层协议协商状态，对齐 QSslConfiguration。 */
    typedef enum XSslNextProtocolNegotiationStatus {
        XSSL_NextProtocolNegotiationNone = 0,       /**< 未配置 ALPN。 */
        XSSL_NextProtocolNegotiationNegotiated,     /**< 已协商出应用协议。 */
        XSSL_NextProtocolNegotiationUnsupported     /**< 没有共同应用协议。 */
    } XSslNextProtocolNegotiationStatus;

    /* =========================================================================
     * 十一、SslError - SSL错误码（参考Qt QSslError）
     * ========================================================================= */

    typedef enum XSslError {
        XSSL_NoError = 0,
        XSSL_UnableToGetIssuerCertificate,
        XSSL_UnableToDecryptCertificateSignature,
        XSSL_UnableToDecodeIssuerPublicKey,
        XSSL_CertificateSignatureFailed,
        XSSL_CertificateNotYetValid,
        XSSL_CertificateExpired,
        XSSL_InvalidNotBeforeField,
        XSSL_InvalidNotAfterField,
        XSSL_SelfSignedCertificate,
        XSSL_SelfSignedCertificateInChain,
        XSSL_UnableToGetLocalIssuerCertificate,
        XSSL_UnableToVerifyFirstCertificate,
        XSSL_CertificateRevoked,
        XSSL_InvalidCaCertificate,
        XSSL_PathLengthExceeded,
        XSSL_InvalidPurpose,
        XSSL_CertificateUntrusted,
        XSSL_CertificateRejected,
        XSSL_SubjectIssuerMismatch,
        XSSL_AuthorityIssuerSerialNumberMismatch,
        XSSL_NoPeerCertificate,
        XSSL_HostNameMismatch,
        XSSL_UnspecifiedError,
        XSSL_NoCertificatesError,
        XSSL_CertificateRejectedDueToSslVersion,
        XSSL_InvalidSslVersion,
        XSSL_HandshakeFailed
    } XSslError;

    /* =========================================================================
     * 十二、平台初始化与后端查询
     * ========================================================================= */

     /**
      * @brief 初始化SSL子系统
      * @return 成功返回true
      */
    bool XSsl_platform_init(void);

    /**
     * @brief 清理SSL子系统
     */
    void XSsl_platform_deinit(void);

    /**
     * @brief 获取后端名称
     * @return 后端名称字符串（如"openssl"、"mbedtls"）
     */
    const char* XSsl_backendName(void);

    /**
     * @brief 检查后端是否实现指定类
     * @param cls 类类型
     * @return 已实现返回true
     */
    bool XSsl_isClassImplemented(XSslImplementedClass cls);

    /**
     * @brief 检查后端是否支持指定特性
     * @param feature 特性类型
     * @return 支持返回true
     */
    bool XSsl_isFeatureSupported(XSslSupportedFeature feature);

    /**
     * @brief 获取支持的协议
     * @return 协议位掩码
     */
    XSslProtocol XSsl_supportedProtocols(void);

    /**
     * @brief 获取默认协议
     * @return 默认协议（通常为SecureProtocols）
     */
    XSslProtocol XSsl_defaultProtocol(void);

    /* =========================================================================
     * 十三、SSL上下文（内部使用）
     * ========================================================================= */

    typedef struct XSslContext XSslContext;

    /**
     * @brief 创建SSL上下文
     * @param protocol 协议版本
     * @return SSL上下文指针
     */
    XSslContext* XSsl_contextCreate(XSslProtocol protocol);

    /**
     * @brief 销毁SSL上下文
     * @param ctx SSL上下文
     */
    void XSsl_contextDestroy(XSslContext* ctx);

    /* =========================================================================
     * 十四、证书操作
     * ========================================================================= */

    typedef struct XSslCertificate XSslCertificate;

    /**
     * @brief 从PEM数据创建证书
     * @param data PEM格式数据
     * @param len 数据长度
     * @return 证书对象
     */
    XSslCertificate* XSsl_certificateFromPem(const char* data, size_t len);

    /**
     * @brief 从DER数据创建证书
     * @param data DER格式数据
     * @param len 数据长度
     * @return 证书对象
     */
    XSslCertificate* XSsl_certificateFromDer(const uint8_t* data, size_t len);

    /**
     * @brief 从文件加载证书
     * @param path 文件路径
     * @param format 编码格式
     * @return 证书对象
     */
    XSslCertificate* XSsl_certificateLoad(const char* path, XSslEncodingFormat format);

    /**
     * @brief 销毁证书
     * @param cert 证书对象
     */
    void XSsl_certificateDestroy(XSslCertificate* cert);

    /* =========================================================================
     * 十五、密钥操作
     * ========================================================================= */

    typedef struct XSslKey XSslKey;

    /**
     * @brief 从PEM数据创建密钥
     * @param data PEM格式数据
     * @param len 数据长度
     * @param algorithm 密钥算法
     * @param type 密钥类型
     * @param passphrase 密码短语（可为NULL）
     * @return 密钥对象
     */
    XSslKey* XSsl_keyFromPem(const char* data, size_t len, XSslKeyAlgorithm algorithm,
        XSslKeyType type, const char* passphrase);

    /**
     * @brief 从DER数据创建密钥
     * @param data DER格式数据
     * @param len 数据长度
     * @param algorithm 密钥算法
     * @param type 密钥类型
     * @return 密钥对象
     */
    XSslKey* XSsl_keyFromDer(const uint8_t* data, size_t len, XSslKeyAlgorithm algorithm,
        XSslKeyType type);

    /**
     * @brief 销毁密钥
     * @param key 密钥对象
     */
    void XSsl_keyDestroy(XSslKey* key);

    /* =========================================================================
     * 十六、随机数生成
     * ========================================================================= */

     /**
      * @brief 生成加密安全的随机字节
      * @param buf 输出缓冲区
      * @param len 字节数
      * @return 成功返回true
      */
    bool XSsl_randomBytes(uint8_t* buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* XSSL_PLATFORM_H */
