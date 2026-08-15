/**
 * @file XCryptographic.h
 * @brief 加密算法与哈希类（哈希部分对齐 Qt 6.8 QCryptographicHash）
 *
 * 支持的算法：MD4, MD5, SHA-1, SHA-224/256/384/512, SHA3, Keccak, Blake2
 *
 * 使用方式：
 * @code
 * // 方式1：一次性计算
 * XByteArray* hash = XCryptographicHash_hash(data, XCryptographicHash_Md5);
 *
 * // 方式2：流式计算
 * XCryptographicHash* ctx = XCryptographicHash_create(XCryptographicHash_Sha256);
 * XCryptographicHash_addData(ctx, data1, len1);
 * XCryptographicHash_addData_1(ctx, byteArray);
 * XByteArray* result = XCryptographicHash_result(ctx);
 * XCryptographicHash_delete(ctx);
 *
 * // 方式3：hashInto（无内存分配）
 * char buffer[32];
 * XByteArrayView view = XCryptographicHash_hashInto(buffer, sizeof(buffer), data, len, XCryptographicHash_Sha256);
 * @endcode
 */

#ifndef XCRYPTOGRAPHIC_H
#define XCRYPTOGRAPHIC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XCryptographic_config.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============== 前向声明 ===============

typedef struct XByteArray XByteArray;
typedef struct XString XString;
typedef struct XIODevice XIODevice;

#include "XByteArrayView.h"   ///< 使用项目中真正的XByteArrayView（非拥有型只读视图）

#include "XCryptographicHash.h"

/**
 * @brief 根据 RFC 8554 计算 LM-OTS SHA256_N32_W8 公钥候选值。
 * @param keyIdentifier 16 字节 LMS/LM-OTS 树标识符。
 * @param leafIdentifier 叶节点编号。
 * @param message 待签名消息视图。
 * @param signature 1124 字节 LM-OTS 签名视图。
 * @param output 输出 32 字节公钥候选值的缓冲区。
 * @param outputSize output 容量，至少为 32 字节。
 * @return 输入格式和校验通过并成功写入返回 true，否则返回 false。
 */
bool XCryptographic_lmotsCalculatePublicKeyCandidate(
    XByteArrayView keyIdentifier, uint32_t leafIdentifier,
    XByteArrayView message, XByteArrayView signature,
    uint8_t* output, size_t outputSize);

/**
 * @brief 根据 RFC 8554 验证 LMS SHA256_M32_H10/LMOTS-SHA256_N32_W8 签名。
 * @param keyIdentifier 16 字节 LMS 树标识符。
 * @param root 32 字节 LMS 公钥根节点。
 * @param message 被验证的原始消息视图。
 * @param signature 1452 字节 LMS 签名视图。
 * @return 签名有效返回 true，格式错误或验证失败返回 false。
 */
bool XCryptographic_lmsVerify(
    XByteArrayView keyIdentifier, XByteArrayView root,
    XByteArrayView message, XByteArrayView signature);

// =============== ML-DSA-87（FIPS 204） ===============

/**
 * @note ML-DSA-87 的可选实现由 Library/mbedtls 后端提供；本头文件只
 *       保留 XCryptographic 的稳定 API 声明，不携带第三方算法源码。
 */

/** ML-DSA-87 公钥长度（字节）。 */
#define XCRYPTOGRAPHIC_MLDSA87_PUBLIC_KEY_SIZE 2592u
/** ML-DSA-87 私钥长度（字节）。 */
#define XCRYPTOGRAPHIC_MLDSA87_PRIVATE_KEY_SIZE 4896u
/** ML-DSA-87 签名长度（字节）。 */
#define XCRYPTOGRAPHIC_MLDSA87_SIGNATURE_SIZE 4627u
/** ML-DSA 内部密钥生成和确定性签名所需的种子长度（字节）。 */
#define XCRYPTOGRAPHIC_MLDSA_SEED_SIZE 32u

/**
 * @brief 使用 32 字节种子生成 ML-DSA-87 密钥对。
 * @param seed 32 字节密钥生成种子。
 * @param publicKey 输出公钥缓冲区，至少 2592 字节。
 * @param publicKeySize publicKey 缓冲区容量。
 * @param privateKey 输出私钥缓冲区，至少 4896 字节。
 * @param privateKeySize privateKey 缓冲区容量。
 * @return 成功生成并写入密钥对返回 true，否则返回 false。
 */
bool XCryptographic_mldsa87KeyPair(
    XByteArrayView seed, uint8_t* publicKey, size_t publicKeySize,
    uint8_t* privateKey, size_t privateKeySize);

/**
 * @brief 使用给定随机性和可选上下文对消息进行 ML-DSA-87 签名。
 * @param message 待签名消息视图。
 * @param context FIPS 204 上下文，长度不得超过 255 字节。
 * @param randomness 32 字节确定性签名随机输入。
 * @param privateKey ML-DSA-87 私钥，必须为 4896 字节。
 * @param signature 输出签名缓冲区。
 * @param signatureSize signature 容量，至少为 4627 字节。
 * @return 成功写入签名返回 true，参数无效或实现被关闭返回 false。
 */
bool XCryptographic_mldsa87Sign(
    XByteArrayView message, XByteArrayView context, XByteArrayView randomness,
    XByteArrayView privateKey, uint8_t* signature, size_t signatureSize);

/**
 * @brief 验证 ML-DSA-87 签名，context 必须与签名时一致。
 * @param message 待验证消息视图。
 * @param context FIPS 204 上下文，必须与签名时完全一致。
 * @param signature ML-DSA-87 签名，必须为 4627 字节。
 * @param publicKey ML-DSA-87 公钥，必须为 2592 字节。
 * @return 签名有效返回 true，否则返回 false。
 */
bool XCryptographic_mldsa87Verify(
    XByteArrayView message, XByteArrayView context, XByteArrayView signature,
    XByteArrayView publicKey);


// =============== 通用密码原语 ===============

/**
 * @brief 密钥用途
 * @note 密钥内容只在 XCryptographic 的对称加密、密钥交换和签名函数之间传递。
 */
typedef enum XCryptographic_KeyType {
    XCryptographic_KeyType_None = 0,                         /**< 未指定密钥类型。 */
    XCryptographic_KeyType_AesCtr,                           /**< AES-CTR 对称密钥。 */
    XCryptographic_KeyType_AesGcm,                            /**< AES-GCM 对称密钥。 */
    XCryptographic_KeyType_AesCcm,                            /**< AES-CCM 对称密钥。 */
    XCryptographic_KeyType_AriaGcm,                           /**< ARIA-GCM 对称密钥。 */
    XCryptographic_KeyType_AriaCcm,                           /**< ARIA-CCM 对称密钥。 */
    XCryptographic_KeyType_CamelliaGcm,                       /**< Camellia-GCM 对称密钥。 */
    XCryptographic_KeyType_CamelliaCcm,                       /**< Camellia-CCM 对称密钥。 */
    XCryptographic_KeyType_ChaCha20Poly1305,                  /**< ChaCha20-Poly1305 密钥。 */
    XCryptographic_KeyType_X25519,                            /**< X25519 私钥或公钥材料。 */
    XCryptographic_KeyType_X448,                              /**< X448 私钥或公钥材料。 */
    XCryptographic_KeyType_EcdhNistP256,                      /**< NIST P-256 ECDH 密钥。 */
    XCryptographic_KeyType_EcdhNistP384,                      /**< NIST P-384 ECDH 密钥。 */
    XCryptographic_KeyType_EcdhNistP521,                      /**< NIST P-521 ECDH 密钥。 */
    XCryptographic_KeyType_EcdhSecp256k1,                     /**< secp256k1 ECDH 密钥。 */
    XCryptographic_KeyType_EcdhBrainpoolP256r1,               /**< brainpoolP256r1 ECDH 密钥。 */
    XCryptographic_KeyType_EcdhBrainpoolP384r1,               /**< brainpoolP384r1 ECDH 密钥。 */
    XCryptographic_KeyType_EcdhBrainpoolP512r1,               /**< brainpoolP512r1 ECDH 密钥。 */
    XCryptographic_KeyType_EcdsaNistP256Private,              /**< NIST P-256 ECDSA 私钥。 */
    XCryptographic_KeyType_EcdsaNistP256Public,               /**< NIST P-256 ECDSA 公钥。 */
    XCryptographic_KeyType_EcdsaNistP384Private,              /**< NIST P-384 ECDSA 私钥。 */
    XCryptographic_KeyType_EcdsaNistP384Public,               /**< NIST P-384 ECDSA 公钥。 */
    XCryptographic_KeyType_EcdsaNistP521Private,              /**< NIST P-521 ECDSA 私钥。 */
    XCryptographic_KeyType_EcdsaNistP521Public,               /**< NIST P-521 ECDSA 公钥。 */
    XCryptographic_KeyType_EcdsaSecp256k1Private,             /**< secp256k1 ECDSA 私钥。 */
    XCryptographic_KeyType_EcdsaSecp256k1Public,              /**< secp256k1 ECDSA 公钥。 */
    XCryptographic_KeyType_EcdsaBrainpoolP256r1Private,       /**< brainpoolP256r1 ECDSA 私钥。 */
    XCryptographic_KeyType_EcdsaBrainpoolP256r1Public,        /**< brainpoolP256r1 ECDSA 公钥。 */
    XCryptographic_KeyType_EcdsaBrainpoolP384r1Private,       /**< brainpoolP384r1 ECDSA 私钥。 */
    XCryptographic_KeyType_EcdsaBrainpoolP384r1Public,        /**< brainpoolP384r1 ECDSA 公钥。 */
    XCryptographic_KeyType_EcdsaBrainpoolP512r1Private,       /**< brainpoolP512r1 ECDSA 私钥。 */
    XCryptographic_KeyType_EcdsaBrainpoolP512r1Public         /**< brainpoolP512r1 ECDSA 公钥。 */
} XCryptographic_KeyType;

/**
 * @brief 通用密码密钥
 * @note 值类型，调用 XCryptographic_destroyKey 后不可再使用。
 */
typedef struct XCryptographic_Key {
    XCryptographic_KeyType type;      ///< 密钥用途
    uint8_t privateKey[66];               ///< 私钥或 ECDH 标量，覆盖至 P-521
    uint8_t publicKey[133];               ///< 公钥，覆盖至 P-521 非压缩点
    size_t publicKeyLen;                  ///< 公钥长度
    uint8_t symmetricKey[32];             ///< AES 对称密钥
    size_t symmetricKeyLen;               ///< AES 密钥长度
} XCryptographic_Key;

/**
 * @brief AES-CTR 流式运算上下文
 * @note 值类型；先使用 XCryptographic_aesCtrSetup 初始化，
 *       使用完成后调用 XCryptographic_aesCtrAbort 清理。
 */
typedef struct XCryptographic_CipherOperation {
    uint8_t roundKeys[240];
    uint8_t counter[16];
    uint8_t stream[16];
    size_t streamOffset;
    uint8_t rounds;
    bool active;
} XCryptographic_CipherOperation;

/** @brief ECDH 密钥协商算法。 */
typedef enum XCryptographic_EcdhAlgorithm {
    XCryptographic_EcdhAlgorithm_None = 0,             /**< 未指定 ECDH 算法。 */
    XCryptographic_EcdhAlgorithm_X25519,               /**< Curve25519 X25519。 */
    XCryptographic_EcdhAlgorithm_X448,                 /**< Curve448 X448。 */
    XCryptographic_EcdhAlgorithm_NistP256,             /**< NIST P-256。 */
    XCryptographic_EcdhAlgorithm_NistP384,             /**< NIST P-384。 */
    XCryptographic_EcdhAlgorithm_NistP521,             /**< NIST P-521。 */
    XCryptographic_EcdhAlgorithm_Secp256k1,            /**< secp256k1。 */
    XCryptographic_EcdhAlgorithm_BrainpoolP256r1,      /**< brainpoolP256r1。 */
    XCryptographic_EcdhAlgorithm_BrainpoolP384r1,      /**< brainpoolP384r1。 */
    XCryptographic_EcdhAlgorithm_BrainpoolP512r1       /**< brainpoolP512r1。 */
} XCryptographic_EcdhAlgorithm;

/** @brief ECDSA 曲线算法。 */
typedef enum XCryptographic_EcdsaAlgorithm {
    XCryptographic_EcdsaAlgorithm_None = 0,             /**< 未指定 ECDSA 曲线。 */
    XCryptographic_EcdsaAlgorithm_NistP256,             /**< NIST P-256。 */
    XCryptographic_EcdsaAlgorithm_NistP384,             /**< NIST P-384。 */
    XCryptographic_EcdsaAlgorithm_NistP521,             /**< NIST P-521。 */
    XCryptographic_EcdsaAlgorithm_BrainpoolP256r1,      /**< brainpoolP256r1。 */
    XCryptographic_EcdsaAlgorithm_BrainpoolP384r1,      /**< brainpoolP384r1。 */
    XCryptographic_EcdsaAlgorithm_BrainpoolP512r1,      /**< brainpoolP512r1。 */
    XCryptographic_EcdsaAlgorithm_Secp256k1             /**< secp256k1。 */
} XCryptographic_EcdsaAlgorithm;

/**
 * @brief 导入 AES-CTR 密钥
 * @param key AES-128/192/256 原始密钥视图（长度为 16、24 或 32）
 * @param result 输出密钥对象
 * @return 成功返回 true；算法被关闭或参数无效返回 false
 */
bool XCryptographic_aesCtrImportKey(XByteArrayView key,
                                        XCryptographic_Key* result);
/**
 * @brief 初始化 AES-CTR 加密或解密流
 * @param operation 输出流式上下文
 * @param key AES-CTR 密钥
 * @param encrypt true 表示加密；CTR 模式下两种方向使用相同变换
 * @param iv 16 字节初始计数器视图
 * @return 成功返回 true
 */
bool XCryptographic_aesCtrSetup(XCryptographic_CipherOperation* operation,
                                    XCryptographic_Key key, bool encrypt,
                                    XByteArrayView iv);
/**
 * @brief 处理 AES-CTR 流的一段输入。
 * @param operation 已由 XCryptographic_aesCtrSetup 初始化的上下文。
 * @param buffer 输出缓冲区，允许与 data 指向相同存储（实现支持时）。
 * @param bufferSize 输出缓冲区容量，至少为 data.m_size。
 * @param data 待加密或解密数据视图。
 * @return 指向 buffer 的结果视图；缓冲区不足或上下文无效返回空视图。
 */
XByteArrayView XCryptographic_aesCtrUpdateInto(
    XCryptographic_CipherOperation* operation,
    char* buffer, size_t bufferSize, XByteArrayView data);
/**
 * @brief 处理 AES-CTR 流的一段输入并返回新建数组。
 * @param operation 已初始化的 AES-CTR 上下文。
 * @param data 待加密或解密数据视图。
 * @return 新建结果数组，调用者负责释放；失败返回 NULL。
 */
XByteArray* XCryptographic_aesCtrUpdate(
    XCryptographic_CipherOperation* operation, XByteArrayView data);
/**
 * @brief 清理 AES-CTR 流上下文中的密钥派生数据。
 * @param operation 要清理的上下文；传 NULL 无操作。
 */
void XCryptographic_aesCtrAbort(XCryptographic_CipherOperation* operation);

/**
 * @brief 生成 X25519/X448、NIST P-256/P-384/P-521、secp256k1 或 brainpool ECDH 临时密钥。
 * @param algorithm 密钥协商算法。
 * @param result 输出密钥对象，成功后由调用者使用 destroyKey 清理。
 * @return 成功返回 true；算法关闭、参数无效或随机源失败返回 false。
 */
bool XCryptographic_ecdhGenerateKey(XCryptographic_EcdhAlgorithm algorithm,
                                        XCryptographic_Key* result);
/**
 * @brief 导出 ECDH 或 ECDSA 公钥。
 * @param key 含公钥材料的密钥对象，按值传入且不会被修改。
 * @return 新建公钥字节数组；失败返回 NULL，调用者负责释放。
 */
XByteArray* XCryptographic_exportPublicKey(XCryptographic_Key key);
/**
 * @brief 将公钥写入提供的缓冲区（无内存分配）。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出缓冲区容量。
 * @param key 含公钥材料的密钥对象。
 * @return 指向 buffer 的公钥视图；容量不足或密钥无效返回空视图。
 */
XByteArrayView XCryptographic_exportPublicKeyInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key);
/**
 * @brief 计算 ECDH 共享密钥。
 * @param privateKey 本地私钥对象。
 * @param peerPublicKey 对端公钥编码视图。
 * @return 新建共享密钥数组；失败返回 NULL，调用者负责释放。
 */
XByteArray* XCryptographic_ecdhAgree(XCryptographic_Key privateKey,
                                         XByteArrayView peerPublicKey);
/**
 * @brief 将 ECDH 共享密钥写入提供的缓冲区（无内存分配）。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出缓冲区容量。
 * @param privateKey 本地私钥对象。
 * @param peerPublicKey 对端公钥编码视图。
 * @return 指向 buffer 的共享密钥视图；失败返回空视图。
 */
XByteArrayView XCryptographic_ecdhAgreeInto(
    char* buffer, size_t bufferSize, XCryptographic_Key privateKey,
    XByteArrayView peerPublicKey);

/**
 * @brief 按曲线导入 ECDSA 私钥。
 * @param algorithm 椭圆曲线算法。
 * @param key 私钥标量编码视图。
 * @param result 输出密钥对象。
 * @return 成功返回 true；编码、曲线或参数无效返回 false。
 */
bool XCryptographic_ecdsaImportPrivateKey(
    XCryptographic_EcdsaAlgorithm algorithm, XByteArrayView key,
    XCryptographic_Key* result);
/**
 * @brief 按曲线生成 ECDSA 私钥。
 * @param algorithm 椭圆曲线算法。
 * @param result 输出密钥对象。
 * @return 成功返回 true；算法关闭或随机源失败返回 false。
 */
bool XCryptographic_ecdsaGenerateKey(
    XCryptographic_EcdsaAlgorithm algorithm, XCryptographic_Key* result);
/**
 * @brief 导出 ECDSA 私钥标量。
 * @param key 含私钥材料的密钥对象。
 * @return 新建私钥标量数组；失败返回 NULL，调用者负责释放。
 */
XByteArray* XCryptographic_ecdsaExportPrivateKey(XCryptographic_Key key);
/**
 * @brief 将 ECDSA 私钥标量写入提供的缓冲区。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出缓冲区容量。
 * @param key 含私钥材料的密钥对象。
 * @return 指向 buffer 的私钥视图；失败返回空视图。
 */
XByteArrayView XCryptographic_ecdsaExportPrivateKeyInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key);
/**
 * @brief 按曲线导入 ECDSA 公钥。
 * @param algorithm 椭圆曲线算法。
 * @param key 公钥编码视图。
 * @param result 输出密钥对象。
 * @return 成功返回 true；编码、曲线或参数无效返回 false。
 */
bool XCryptographic_ecdsaImportPublicKey(
    XCryptographic_EcdsaAlgorithm algorithm, XByteArrayView key,
    XCryptographic_Key* result);
/**
 * @brief 对摘要进行 ECDSA 签名。
 * @param key 含 ECDSA 私钥的密钥对象。
 * @param hash 待签名摘要视图。
 * @param deterministic true 使用 RFC 6979 确定性签名，false 使用随机数。
 * @return 新建签名数组；失败返回 NULL，调用者负责释放。
 */
XByteArray* XCryptographic_ecdsaSignHash(
    XCryptographic_Key key, XByteArrayView hash, bool deterministic);
/**
 * @brief 将 ECDSA 签名写入提供的缓冲区。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出缓冲区容量。
 * @param key 含 ECDSA 私钥的密钥对象。
 * @param hash 待签名摘要视图。
 * @param deterministic 是否使用 RFC 6979 确定性签名。
 * @return 指向 buffer 的签名视图；失败返回空视图。
 */
XByteArrayView XCryptographic_ecdsaSignHashInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash, bool deterministic);
/**
 * @brief 验证 ECDSA 摘要签名。
 * @param key 含 ECDSA 公钥的密钥对象。
 * @param hash 被验证摘要视图。
 * @param signature DER 编码的 ECDSA 签名视图。
 * @return 签名有效返回 true，否则返回 false。
 */
bool XCryptographic_ecdsaVerifyHash(
    XCryptographic_Key key, XByteArrayView hash, XByteArrayView signature);

/**
 * @brief 清零由本模块导入或生成的密钥。
 * @param key 要清理的密钥对象；传 NULL 无操作。
 */
void XCryptographic_destroyKey(XCryptographic_Key* key);



// =============== SHAKE128/256 XOF ===============

/** @brief 可扩展输出函数（XOF）算法。 */
typedef enum XCryptographic_XofAlgorithm {
    XCryptographic_XofAlgorithm_Shake128 = 0, /**< SHAKE128。 */
    XCryptographic_XofAlgorithm_Shake256      /**< SHAKE256。 */
} XCryptographic_XofAlgorithm;

/**
 * @brief SHAKE XOF 流式上下文
 * @note 值类型；先使用 XCryptographic_xofSetup 初始化，
 *       使用完成后调用 XCryptographic_xofAbort 清理。
 */
typedef struct XCryptographic_XofOperation {
    XCryptographic_XofAlgorithm algorithm;  ///< 使用的算法
    uint64_t state[25];                     ///< Keccak 状态
    uint8_t block[200];                     ///< 吸收/挤出缓冲区
    size_t rate;                            ///< 当前算法速率（字节）
    size_t blockLen;                        ///< 已吸收的块内字节数
    size_t outputOffset;                    ///< 挤出阶段输出偏移
    uint8_t delimitedSuffix;                ///< SHAKE 分隔符 0x1F
    bool finalized;                         ///< 是否已进入挤出阶段
    bool active;                            ///< 操作是否已初始化
} XCryptographic_XofOperation;

/**
 * @brief 初始化 SHAKE128/256 XOF 操作。
 * @param operation 输出的 XOF 上下文。
 * @param algorithm SHAKE128 或 SHAKE256。
 * @return 初始化成功返回 true；参数无效或算法关闭返回 false。
 */
bool XCryptographic_xofSetup(XCryptographic_XofOperation* operation,
                             XCryptographic_XofAlgorithm algorithm);
/**
 * @brief 向 XOF 操作添加一段输入。
 * @param operation 已初始化且尚未输出的 XOF 上下文。
 * @param data 输入字节视图。
 * @return 成功返回 true；上下文已进入输出阶段或参数无效返回 false。
 */
bool XCryptographic_xofUpdateInto(XCryptographic_XofOperation* operation,
                                  XByteArrayView data);
/**
 * @brief 从 XOF 操作输出指定长度的字节。
 * @param operation 已初始化的 XOF 上下文。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出缓冲区容量，至少为 outputSize。
 * @param outputSize 请求输出的字节数。
 * @return 指向 buffer 的输出视图；容量不足或上下文无效返回空视图。
 */
XByteArrayView XCryptographic_xofOutputInto(
    XCryptographic_XofOperation* operation, char* buffer, size_t bufferSize,
    size_t outputSize);
/**
 * @brief 清理 XOF 上下文并清零状态。
 * @param operation 要清理的上下文；传 NULL 无操作。
 */
void XCryptographic_xofAbort(XCryptographic_XofOperation* operation);

// =============== 分组密码（AES/ARIA/Camellia） ===============

/** @brief 分组密码算法。 */
typedef enum XCryptographic_BlockCipherAlgorithm {
    XCryptographic_BlockCipherAlgorithm_Aes = 0, /**< AES。 */
    XCryptographic_BlockCipherAlgorithm_Aria,     /**< ARIA。 */
    XCryptographic_BlockCipherAlgorithm_Camellia  /**< Camellia。 */
} XCryptographic_BlockCipherAlgorithm;

/** @brief 分组密码工作模式。 */
typedef enum XCryptographic_BlockCipherMode {
    XCryptographic_BlockCipherMode_EcbNoPadding = 0, /**< ECB，无填充。 */
    XCryptographic_BlockCipherMode_CbcNoPadding,      /**< CBC，无填充。 */
    XCryptographic_BlockCipherMode_CbcPkcs7,          /**< CBC，PKCS#7 填充。 */
    XCryptographic_BlockCipherMode_Cfb,               /**< CFB。 */
    XCryptographic_BlockCipherMode_Ofb,               /**< OFB。 */
    XCryptographic_BlockCipherMode_Xts                /**< XTS。 */
} XCryptographic_BlockCipherMode;

/**
 * @brief 分组密码流式上下文
 * @note 值类型；先使用 XCryptographic_blockCipherSetup 初始化，
 *       CBC/CFB/OFB 再调用 XCryptographic_blockCipherSetIv 设置初始向量，
 *       使用完成后调用 XCryptographic_blockCipherAbort 清理。
 */
typedef struct XCryptographic_BlockCipherOperation {
    XCryptographic_BlockCipherAlgorithm algorithm;  ///< 使用的算法
    XCryptographic_BlockCipherMode mode;            ///< 工作模式
    bool encrypt;                                   ///< 是否加密
    bool active;                                    ///< 是否已初始化
    uint8_t roundKeys[240];                         ///< AES 轮密钥
    uint8_t xtsRoundKeys[240];                      ///< AES-XTS tweak 密钥轮密钥
    uint32_t keySchedule[68];                       ///< ARIA/Camellia 加密轮密钥
    uint32_t decryptKeySchedule[68];                ///< ARIA/Camellia 解密轮密钥
    uint8_t rounds;                                 ///< 轮数
    uint8_t xtsRounds;                              ///< AES-XTS tweak 密钥轮数
    uint8_t iv[16];                                 ///< 当前 IV/链
    uint8_t tweak[16];                              ///< AES-XTS 当前 tweak
    uint8_t stream[16];                             ///< CFB/OFB 流缓冲
    size_t streamOffset;                            ///< CFB/OFB 流偏移
    uint8_t pending[32];                            ///< 待处理块缓冲（XTS 保留末块窃取输入）
    size_t pendingSize;                             ///< 待处理字节数
} XCryptographic_BlockCipherOperation;

/**
 * @brief 初始化 AES、ARIA 或 Camellia 分组密码流。
 * @param operation 输出分组密码上下文。
 * @param algorithm 分组密码算法。
 * @param key 原始密钥视图，长度由算法和密钥规格决定。
 * @param mode 工作模式；XTS 需要组合密钥。
 * @param encrypt true 表示加密，false 表示解密。
 * @return 初始化成功返回 true；密钥、模式或参数无效返回 false。
 */
bool XCryptographic_blockCipherSetup(
    XCryptographic_BlockCipherOperation* operation,
    XCryptographic_BlockCipherAlgorithm algorithm, XByteArrayView key,
    XCryptographic_BlockCipherMode mode, bool encrypt);
/**
 * @brief 设置分组密码初始向量（ECB 不需要）。
 * @param operation 已初始化的分组密码上下文。
 * @param iv 初始向量；CBC/CFB/OFB/XTS 通常为 16 字节。
 * @return 设置成功返回 true，模式或向量长度不匹配返回 false。
 */
bool XCryptographic_blockCipherSetIv(
    XCryptographic_BlockCipherOperation* operation, XByteArrayView iv);
/**
 * @brief 处理分组密码流的一段输入。
 * @param operation 已初始化且未完成的上下文。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出缓冲区容量。
 * @param data 输入数据视图；无填充模式必须满足分组长度约束。
 * @return 指向 buffer 的已处理数据视图；输出空间不足或状态无效返回空视图。
 */
XByteArrayView XCryptographic_blockCipherUpdateInto(
    XCryptographic_BlockCipherOperation* operation, char* buffer,
    size_t bufferSize, XByteArrayView data);
/**
 * @brief 完成分组密码流并输出末尾块/明文。
 * @param operation 已初始化的上下文。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出缓冲区容量。
 * @return 指向 buffer 的最终输出视图；填充错误或容量不足返回空视图。
 */
XByteArrayView XCryptographic_blockCipherFinishInto(
    XCryptographic_BlockCipherOperation* operation, char* buffer,
    size_t bufferSize);
/**
 * @brief 清理分组密码上下文并清零密钥材料。
 * @param operation 要清理的上下文；传 NULL 无操作。
 */
void XCryptographic_blockCipherAbort(XCryptographic_BlockCipherOperation* operation);

// =============== ChaCha20 流密码 ===============

/**
 * @brief 原始 ChaCha20 流式上下文
 * @note 值类型；先使用 XCryptographic_chacha20Setup 初始化，
 *       使用完成后调用 XCryptographic_chacha20Abort 清理。
 */
typedef struct XCryptographic_ChaCha20Operation {
    uint8_t key[32];             ///< 32 字节密钥
    uint8_t nonce[12];           ///< 12 字节随机数
    uint32_t counter;            ///< 块计数器
    uint8_t stream[64];          ///< 当前密钥流块
    size_t streamOffset;         ///< 密钥流偏移
    bool active;                 ///< 是否已初始化
} XCryptographic_ChaCha20Operation;

/**
 * @brief 初始化 ChaCha20 流。
 * @param operation 输出 ChaCha20 上下文。
 * @param key 32 字节 ChaCha20 密钥。
 * @param nonce 12 字节 nonce。
 * @return 初始化成功返回 true；参数长度错误返回 false。
 */
bool XCryptographic_chacha20Setup(XCryptographic_ChaCha20Operation* operation,
                                  XByteArrayView key, XByteArrayView nonce);
/**
 * @brief 处理 ChaCha20 流的一段输入。
 * @param operation 已初始化的 ChaCha20 上下文。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出容量，至少为 data.m_size。
 * @param data 输入明文或密文视图。
 * @return 指向 buffer 的处理结果视图；失败返回空视图。
 */
XByteArrayView XCryptographic_chacha20UpdateInto(
    XCryptographic_ChaCha20Operation* operation, char* buffer,
    size_t bufferSize, XByteArrayView data);
/**
 * @brief 清理 ChaCha20 上下文并清零密钥材料。
 * @param operation 要清理的上下文；传 NULL 无操作。
 */
void XCryptographic_chacha20Abort(XCryptographic_ChaCha20Operation* operation);

// =============== AES-CMAC ===============

/**
 * @brief AES-CMAC 流式上下文
 * @note 值类型；先使用 XCryptographic_aesCmacSetup 初始化，
 *       使用完成后调用 XCryptographic_aesCmacAbort 清理。
 */
typedef struct XCryptographic_CmacOperation {
    uint8_t roundKeys[240];     ///< AES 轮密钥
    uint8_t rounds;             ///< 轮数
    uint8_t firstSubkey[16];    ///< K1 子密钥
    uint8_t secondSubkey[16];   ///< K2 子密钥
    uint8_t state[16];          ///< 累计状态
    uint8_t pending[16];        ///< 待处理块缓冲
    size_t pendingSize;         ///< 待处理字节数
    bool active;                ///< 是否已初始化
} XCryptographic_CmacOperation;

/**
 * @brief 一次性计算 AES-CMAC 并写入调用者缓冲区。
 * @param key AES 密钥视图，长度为 16、24 或 32 字节。
 * @param message 待认证消息视图。
 * @param buffer 输出 16 字节标签的缓冲区。
 * @param bufferSize 输出容量，至少为 16 字节。
 * @return 指向 buffer 的 16 字节标签视图；失败返回空视图。
 */
XByteArrayView XCryptographic_aesCmacInto(
    XByteArrayView key, XByteArrayView message, char* buffer, size_t bufferSize);
/**
 * @brief 一次性计算 AES-CMAC 并返回新对象。
 * @param key AES 密钥视图，长度为 16、24 或 32 字节。
 * @param message 待认证消息视图。
 * @return 新建 16 字节标签数组；失败返回 NULL，调用者负责释放。
 */
XByteArray* XCryptographic_aesCmac(XByteArrayView key, XByteArrayView message);
/**
 * @brief 初始化 AES-CMAC 流。
 * @param operation 输出 CMAC 上下文。
 * @param key AES 密钥视图，长度为 16、24 或 32 字节。
 * @return 初始化成功返回 true，否则返回 false。
 */
bool XCryptographic_aesCmacSetup(XCryptographic_CmacOperation* operation,
                                 XByteArrayView key);
/**
 * @brief 向 AES-CMAC 流添加一段数据。
 * @param operation 已初始化且未完成的 CMAC 上下文。
 * @param message 待认证消息视图。
 * @return 成功返回 true；上下文无效或已完成返回 false。
 */
bool XCryptographic_aesCmacUpdate(XCryptographic_CmacOperation* operation,
                                  XByteArrayView message);
/**
 * @brief 完成 AES-CMAC 流并输出 16 字节标签。
 * @param operation 已初始化的 CMAC 上下文；调用后进入完成状态。
 * @param buffer 输出标签缓冲区。
 * @param bufferSize 输出容量，至少为 16 字节。
 * @return 指向 buffer 的标签视图；失败返回空视图。
 */
XByteArrayView XCryptographic_aesCmacFinishInto(
    XCryptographic_CmacOperation* operation, char* buffer, size_t bufferSize);
/**
 * @brief 清理 AES-CMAC 上下文并清零密钥材料。
 * @param operation 要清理的上下文；传 NULL 无操作。
 */
void XCryptographic_aesCmacAbort(XCryptographic_CmacOperation* operation);

// =============== CCM* 无标签流 ===============

/**
 * @brief AES CCM*-无标签流式上下文
 * @note 值类型；先使用 XCryptographic_aesCcmStarNoTagSetup 初始化，
 *       使用完成后调用 XCryptographic_aesCcmStarNoTagAbort 清理。
 */
typedef struct XCryptographic_CcmStarNoTagOperation {
    uint8_t roundKeys[240];     ///< AES 轮密钥
    uint8_t key[32];            ///< 原始密钥
    uint8_t keyLen;             ///< 密钥长度
    uint8_t rounds;             ///< 轮数
    uint8_t counter[16];        ///< 计数器块
    uint8_t stream[16];         ///< 密钥流缓冲
    size_t streamOffset;        ///< 密钥流偏移
    uint32_t blockCounter;      ///< 块计数器
    bool active;                ///< 是否已初始化
} XCryptographic_CcmStarNoTagOperation;

/**
 * @brief 初始化 AES CCM*-无标签流。
 * @param operation 输出 CCM* 上下文。
 * @param key AES-128/192/256 密钥视图。
 * @param nonce CCM* nonce 视图，长度由协议约束。
 * @return 初始化成功返回 true，否则返回 false。
 */
bool XCryptographic_aesCcmStarNoTagSetup(
    XCryptographic_CcmStarNoTagOperation* operation,
    XByteArrayView key, XByteArrayView nonce);
/**
 * @brief 处理 AES CCM*-无标签流的一段输入。
 * @param operation 已初始化的 CCM* 上下文。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出容量，至少为 data.m_size。
 * @param data 输入明文或密文视图。
 * @return 指向 buffer 的结果视图；失败返回空视图。
 */
XByteArrayView XCryptographic_aesCcmStarNoTagUpdateInto(
    XCryptographic_CcmStarNoTagOperation* operation, char* buffer,
    size_t bufferSize, XByteArrayView data);
/**
 * @brief 清理 CCM*-无标签上下文并清零密钥材料。
 * @param operation 要清理的上下文；传 NULL 无操作。
 */
void XCryptographic_aesCcmStarNoTagAbort(
    XCryptographic_CcmStarNoTagOperation* operation);

// =============== 认证加密（AEAD） ===============

/** @brief AEAD 算法。 */
typedef enum XCryptographic_AeadAlgorithm {
    XCryptographic_AeadAlgorithm_AesGcm = 0,       /**< AES-GCM。 */
    XCryptographic_AeadAlgorithm_AesCcm,           /**< AES-CCM。 */
    XCryptographic_AeadAlgorithm_AriaGcm,          /**< ARIA-GCM。 */
    XCryptographic_AeadAlgorithm_AriaCcm,          /**< ARIA-CCM。 */
    XCryptographic_AeadAlgorithm_CamelliaGcm,      /**< Camellia-GCM。 */
    XCryptographic_AeadAlgorithm_CamelliaCcm,      /**< Camellia-CCM。 */
    XCryptographic_AeadAlgorithm_ChaCha20Poly1305  /**< ChaCha20-Poly1305。 */
} XCryptographic_AeadAlgorithm;

/** @brief Poly1305 流式状态。 */
typedef struct XCryptographic_Poly1305 {
    uint32_t r[4];
    uint32_t h[5];
    uint32_t pad[4];
    uint8_t buffer[16];
    size_t bufferLen;
} XCryptographic_Poly1305;

/**
 * @brief AEAD 分段运算上下文。
 * @note 值类型；使用 XCryptographic_aeadSetup 初始化，完成后调用
 *       XCryptographic_aeadAbort 清理。
 */
typedef struct XCryptographic_AeadOperation {
    XCryptographic_AeadAlgorithm algorithm;
    XCryptographic_Key key;
    XCryptographic_BlockCipherOperation blockCipher;
    XCryptographic_ChaCha20Operation chacha20;
    XCryptographic_Poly1305 poly1305;
    uint8_t nonce[16];
    size_t nonceLen;
    size_t tagSize;
    size_t associatedDataLength;
    size_t dataLength;
    size_t expectedAssociatedDataLength;
    size_t expectedDataLength;
    uint8_t gcmHashSubkey[16];
    uint8_t gcmInitialCounter[16];
    uint8_t gcmCounter[16];
    uint8_t gcmAuth[16];
    uint8_t gcmPending[16];
    size_t gcmPendingLen;
    uint8_t gcmStream[16];
    size_t gcmStreamOffset;
    uint8_t ccmState[16];
    uint8_t ccmCounter[16];
    uint8_t ccmStream[16];
    uint8_t ccmPending[16];
    size_t ccmPendingLen;
    size_t ccmStreamOffset;
    size_t ccmBlockCounter;
    uint8_t ccmAdPrefix[10];
    size_t ccmAdPrefixLen;
    size_t ccmAdPrefixOffset;
    bool encrypt;
    bool active;
    bool lengthsSet;
    bool associatedDataFinalized;
} XCryptographic_AeadOperation;

/**
 * @brief 导入 AEAD 对称密钥。
 * @param algorithm AEAD 算法。
 * @param key 原始对称密钥视图。
 * @param result 输出密钥对象。
 * @return 成功返回 true；算法、密钥长度或参数无效返回 false。
 */
bool XCryptographic_aeadImportKey(XCryptographic_AeadAlgorithm algorithm,
                                  XByteArrayView key, XCryptographic_Key* result);
/**
 * @brief 初始化 AEAD 分段运算。
 * @param operation 输出 AEAD 上下文。
 * @param algorithm AEAD 算法。
 * @param key 已导入的 AEAD 密钥对象。
 * @param encrypt true 执行加密，false 执行解密。
 * @return 初始化成功返回 true，否则返回 false。
 */
bool XCryptographic_aeadSetup(XCryptographic_AeadOperation* operation,
                              XCryptographic_AeadAlgorithm algorithm,
                              XCryptographic_Key key, bool encrypt);
/**
 * @brief 设置 AEAD nonce。
 * @param operation 已初始化的 AEAD 上下文。
 * @param nonce nonce/IV 视图，长度由算法约束。
 * @return 设置成功返回 true，否则返回 false。
 */
bool XCryptographic_aeadSetNonce(XCryptographic_AeadOperation* operation,
                                 XByteArrayView nonce);
/**
 * @brief 设置附加数据和明文长度（CCM 必需）。
 * @param operation 已初始化的 AEAD 上下文。
 * @param associatedDataLength 计划输入的附加认证数据总长度。
 * @param dataLength 计划输入的明文/密文总长度。
 * @return 设置成功返回 true，否则返回 false。
 */
bool XCryptographic_aeadSetLengths(XCryptographic_AeadOperation* operation,
                                   size_t associatedDataLength,
                                   size_t dataLength);
/**
 * @brief 设置认证标签长度。
 * @param operation 已初始化的 AEAD 上下文。
 * @param tagSize 标签字节数，必须符合算法允许的范围。
 * @return 设置成功返回 true，否则返回 false。
 */
bool XCryptographic_aeadSetTagSize(XCryptographic_AeadOperation* operation,
                                   size_t tagSize);
/**
 * @brief 输入一段附加认证数据。
 * @param operation 已初始化且尚未输入消息的 AEAD 上下文。
 * @param associatedData 附加认证数据视图。
 * @return 成功返回 true；顺序、长度或状态错误返回 false。
 */
bool XCryptographic_aeadUpdateAssociatedData(XCryptographic_AeadOperation* operation,
                                             XByteArrayView associatedData);
/**
 * @brief 加密或解密一段消息。
 * @param operation 已初始化的 AEAD 上下文。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出容量，至少为 data.m_size。
 * @param data 输入明文或密文视图。
 * @return 指向 buffer 的处理结果视图；失败返回空视图。
 */
XByteArrayView XCryptographic_aeadUpdateInto(XCryptographic_AeadOperation* operation,
                                             char* buffer, size_t bufferSize,
                                             XByteArrayView data);
/**
 * @brief 完成 AEAD 分段运算并输出剩余数据和认证标签。
 * @param operation 已初始化的 AEAD 上下文。
 * @param output 输出剩余明文/密文的缓冲区。
 * @param outputSize output 容量。
 * @param outputLength 实际写入 output 的字节数，可为 NULL。
 * @param tag 输出认证标签的缓冲区。
 * @param tagSize tag 容量。
 * @param tagLength 实际写入标签的字节数，可为 NULL。
 * @return 成功完成返回 true；容量不足、状态错误或解密认证失败返回 false。
 */
bool XCryptographic_aeadFinishInto(XCryptographic_AeadOperation* operation,
                                   char* output, size_t outputSize, size_t* outputLength,
                                   char* tag, size_t tagSize, size_t* tagLength);
/**
 * @brief 清理 AEAD 分段上下文并清零密钥材料。
 * @param operation 要清理的上下文；传 NULL 无操作。
 */
void XCryptographic_aeadAbort(XCryptographic_AeadOperation* operation);
/**
 * @brief 一次性 AEAD 加密，输出为 ciphertext || tag。
 * @param key 已导入的 AEAD 密钥。
 * @param nonce nonce/IV 视图。
 * @param associatedData 附加认证数据视图。
 * @param plainText 待加密明文视图。
 * @param tagSize 认证标签长度。
 * @param buffer 输出密文和标签的缓冲区。
 * @param bufferSize 输出容量，至少为 plainText.m_size + tagSize。
 * @return 指向 buffer 的密文||标签视图；失败返回空视图。
 */
XByteArrayView XCryptographic_aeadEncryptInto(
    XCryptographic_Key key, XByteArrayView nonce,
    XByteArrayView associatedData, XByteArrayView plainText,
    size_t tagSize, char* buffer, size_t bufferSize);
/**
 * @brief 一次性 AEAD 加密并返回新对象。
 * @param key 已导入的 AEAD 密钥。
 * @param nonce nonce/IV 视图。
 * @param associatedData 附加认证数据视图。
 * @param plainText 待加密明文视图。
 * @param tagSize 认证标签长度。
 * @return 新建的密文||标签数组；失败返回 NULL，调用者负责释放。
 */
XByteArray* XCryptographic_aeadEncrypt(
    XCryptographic_Key key, XByteArrayView nonce,
    XByteArrayView associatedData, XByteArrayView plainText,
    size_t tagSize);
/**
 * @brief 一次性 AEAD 解密并验证标签。
 * @param key 已导入的 AEAD 密钥。
 * @param nonce nonce/IV 视图。
 * @param associatedData 附加认证数据视图。
 * @param encryptedData ciphertext || tag 视图。
 * @param tagSize 末尾认证标签长度。
 * @param buffer 输出明文缓冲区。
 * @param bufferSize 输出容量，至少为密文长度减去标签长度。
 * @return 指向 buffer 的明文视图；认证失败或参数无效返回空视图。
 */
XByteArrayView XCryptographic_aeadDecryptInto(
    XCryptographic_Key key, XByteArrayView nonce,
    XByteArrayView associatedData, XByteArrayView encryptedData,
    size_t tagSize, char* buffer, size_t bufferSize);
/**
 * @brief 一次性 AEAD 解密并返回新对象。
 * @param key 已导入的 AEAD 密钥。
 * @param nonce nonce/IV 视图。
 * @param associatedData 附加认证数据视图。
 * @param encryptedData ciphertext || tag 视图。
 * @param tagSize 末尾认证标签长度。
 * @return 新建明文数组；认证失败或参数无效返回 NULL，调用者负责释放。
 */
XByteArray* XCryptographic_aeadDecrypt(
    XCryptographic_Key key, XByteArrayView nonce,
    XByteArrayView associatedData, XByteArrayView encryptedData,
    size_t tagSize);

// =============== 密钥派生（KDF） ===============

/**
 * @brief 基于指定 HMAC 摘要的 HKDF 一次性派生（RFC 5869）。
 * @param salt HKDF salt，可为空视图。
 * @param inputKeyMaterial 输入密钥材料。
 * @param info 应用上下文信息，可为空视图。
 * @param hashAlgorithm HMAC 使用的哈希算法。
 * @param outputSize 请求派生字节数。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出容量，至少为 outputSize。
 * @return 指向 buffer 的派生密钥视图；失败返回空视图。
 */
XByteArrayView XCryptographic_hkdfInto(
    XByteArrayView salt, XByteArrayView inputKeyMaterial,
    XByteArrayView info, XCryptographicHash_Algorithm hashAlgorithm,
    size_t outputSize, char* buffer, size_t bufferSize);
/**
 * @brief 执行 HKDF 提取阶段（RFC 5869）。
 * @param salt HKDF salt，可为空视图。
 * @param inputKeyMaterial 输入密钥材料。
 * @param hashAlgorithm HMAC 使用的哈希算法。
 * @param buffer 输出 PRK 缓冲区，容量至少为摘要长度。
 * @param bufferSize 输出容量。
 * @return 指向 buffer 的 PRK 视图；失败返回空视图。
 */
XByteArrayView XCryptographic_hkdfExtractInto(
    XByteArrayView salt, XByteArrayView inputKeyMaterial,
    XCryptographicHash_Algorithm hashAlgorithm, char* buffer, size_t bufferSize);
/**
 * @brief 执行 HKDF 展开阶段（RFC 5869）。
 * @param prk HKDF 提取阶段得到的伪随机密钥。
 * @param info 应用上下文信息，可为空视图。
 * @param hashAlgorithm HMAC 使用的哈希算法。
 * @param outputSize 请求派生字节数。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出容量，至少为 outputSize。
 * @return 指向 buffer 的派生结果视图；失败返回空视图。
 */
XByteArrayView XCryptographic_hkdfExpandInto(
    XByteArrayView prk, XByteArrayView info,
    XCryptographicHash_Algorithm hashAlgorithm, size_t outputSize,
    char* buffer, size_t bufferSize);
/**
 * @brief 基于指定 HMAC 摘要的 PBKDF2 一次性派生（RFC 8018）。
 * @param password 密码视图。
 * @param salt PBKDF2 salt 视图。
 * @param iterations 迭代次数，必须大于 0。
 * @param hashAlgorithm HMAC 使用的哈希算法。
 * @param outputSize 请求派生字节数。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出容量，至少为 outputSize。
 * @return 指向 buffer 的派生结果视图；失败返回空视图。
 */
XByteArrayView XCryptographic_pbkdf2HmacInto(
    XByteArrayView password, XByteArrayView salt, uint32_t iterations,
    XCryptographicHash_Algorithm hashAlgorithm, size_t outputSize,
    char* buffer, size_t bufferSize);
/**
 * @brief 基于指定 HMAC 摘要的 TLS 1.2 PRF（RFC 5246）。
 * @param secret PRF secret。
 * @param label PRF label。
 * @param seed PRF seed。
 * @param hashAlgorithm HMAC 使用的哈希算法。
 * @param outputSize 请求输出字节数。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出容量。
 * @return 指向 buffer 的 PRF 输出视图；失败返回空视图。
 */
XByteArrayView XCryptographic_tls12PrfInto(
    XByteArrayView secret, XByteArrayView label, XByteArrayView seed,
    XCryptographicHash_Algorithm hashAlgorithm,
    size_t outputSize, char* buffer, size_t bufferSize);
/**
 * @brief 基于指定 HMAC 摘要计算 TLS 1.2 PSK-to-MS（RFC 4279）。
 * @param psk 预共享密钥。
 * @param otherSecret 其他 secret；无其他 secret 时传空视图。
 * @param label PRF label。
 * @param seed PRF seed。
 * @param hashAlgorithm HMAC 使用的哈希算法。
 * @param outputSize 请求输出字节数。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出容量。
 * @return 指向 buffer 的主密钥视图；失败返回空视图。
 */
XByteArrayView XCryptographic_tls12PskToMsInto(
    XByteArrayView psk, XByteArrayView otherSecret,
    XByteArrayView label, XByteArrayView seed,
    XCryptographicHash_Algorithm hashAlgorithm,
    size_t outputSize, char* buffer, size_t bufferSize);
/**
 * @brief 基于 AES-CMAC-PRF-128 的 PBKDF2（RFC 4615）。
 * @param password 密码视图。
 * @param salt salt 视图。
 * @param iterations 迭代次数，必须大于 0。
 * @param outputSize 请求派生字节数。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出容量。
 * @return 指向 buffer 的派生结果视图；失败返回空视图。
 */
XByteArrayView XCryptographic_pbkdf2AesCmacPrf128Into(
    XByteArrayView password, XByteArrayView salt, uint32_t iterations,
    size_t outputSize, char* buffer, size_t bufferSize);

// =============== AES 密钥封装（RFC 3394/RFC 5649） ===============

/** @brief AES 密钥封装模式。 */
typedef enum XCryptographic_KeyWrapMode {
    XCryptographic_KeyWrapMode_Kw = 0, /**< RFC 3394，输入长度为 8 的倍数。 */
    XCryptographic_KeyWrapMode_Kwp = 1 /**< RFC 5649，支持非 8 倍数长度。 */
} XCryptographic_KeyWrapMode;

/**
 * @brief RFC 3394/RFC 5649 AES 密钥封装。
 * @param mode KW（RFC 3394）或 KWP（RFC 5649）模式。
 * @param kek 密钥封装密钥，长度为 16、24 或 32 字节。
 * @param input 待封装密钥数据。
 * @param buffer 输出封装结果缓冲区。
 * @param bufferSize 输出容量。
 * @param outputSize 实际写入的封装结果长度输出指针，不能为 NULL。
 * @return 成功返回 true；参数、长度或输出容量无效返回 false。
 */
bool XCryptographic_aesKeyWrapInto(
    XCryptographic_KeyWrapMode mode, XByteArrayView kek,
    XByteArrayView input, char* buffer, size_t bufferSize,
    size_t* outputSize);
/**
 * @brief RFC 3394/RFC 5649 AES 密钥解封。
 * @param mode KW（RFC 3394）或 KWP（RFC 5649）模式。
 * @param kek 密钥封装密钥，长度为 16、24 或 32 字节。
 * @param input 待解封密文视图。
 * @param buffer 输出原始密钥数据缓冲区。
 * @param bufferSize 输出容量。
 * @param outputSize 实际写入的明文长度输出指针，不能为 NULL。
 * @return 完整性校验通过并成功解封返回 true，否则返回 false。
 */
bool XCryptographic_aesKeyUnwrapInto(
    XCryptographic_KeyWrapMode mode, XByteArrayView kek,
    XByteArrayView input, char* buffer, size_t bufferSize,
    size_t* outputSize);

// =============== 有限域 Diffie-Hellman（FFDH） ===============

/**
 * @brief 生成 FFDH 私钥（RFC 7919 FFDHE2048）。
 * @param buffer 输出私钥缓冲区。
 * @param bufferSize 输出容量，至少为 FFDHE2048 私钥长度。
 * @return 成功生成并写入返回 true，否则返回 false。
 */
bool XCryptographic_ffdhGeneratePrivateKeyInto(char* buffer, size_t bufferSize);
/**
 * @brief 从 FFDH 私钥导出公钥。
 * @param buffer 输出公钥缓冲区。
 * @param bufferSize 输出容量。
 * @param privateKey 私钥大端编码。
 * @param privateKeyLen 私钥字节数。
 * @return 指向 buffer 的公钥视图；失败返回空视图。
 */
XByteArrayView XCryptographic_ffdhExportPublicKeyInto(
    char* buffer, size_t bufferSize,
    const char* privateKey, size_t privateKeyLen);
/**
 * @brief 计算 FFDH 共享密钥。
 * @param buffer 输出共享密钥缓冲区。
 * @param bufferSize 输出容量。
 * @param privateKey 本地私钥大端编码。
 * @param privateKeyLen 本地私钥字节数。
 * @param peerPublicKey 对端公钥大端编码。
 * @param peerPublicKeyLen 对端公钥字节数。
 * @return 指向 buffer 的共享密钥视图；失败返回空视图。
 */
XByteArrayView XCryptographic_ffdhAgreeInto(
    char* buffer, size_t bufferSize,
    const char* privateKey, size_t privateKeyLen,
    const char* peerPublicKey, size_t peerPublicKeyLen);

// =============== ECDH 密钥导入 ===============

/**
 * @brief 从私钥标量导入 ECDH 密钥并计算公钥。
 * @param algorithm ECDH 曲线或 Montgomery 算法。
 * @param privateKey 私钥标量编码视图，长度必须符合 algorithm。
 * @param result 输出密钥对象；成功后可用于 XCryptographic_ecdhAgree。
 * @return 导入成功返回 true；算法关闭、编码或参数无效返回 false。
 */
bool XCryptographic_ecdhImportPrivateKey(
    XCryptographic_EcdhAlgorithm algorithm, XByteArrayView privateKey,
    XCryptographic_Key* result);
/**
 * @brief 导入 ECDH 对端公钥。
 * @param algorithm ECDH 曲线或 Montgomery 算法。
 * @param publicKey 对端公钥编码视图，长度和编码必须符合 algorithm。
 * @param result 输出密钥对象；成功后包含可用于协商的公钥材料。
 * @return 导入成功返回 true；算法关闭、编码或参数无效返回 false。
 */
bool XCryptographic_ecdhImportPublicKey(
    XCryptographic_EcdhAlgorithm algorithm, XByteArrayView publicKey,
    XCryptographic_Key* result);

// =============== 确定性 ECDSA（RFC 6979） ===============

/*
 * Deterministic signing is selected by the `deterministic` argument of the
 * generic XCryptographic_ecdsaSignHash* APIs above. Only the generic
 * XCryptographic_ecdsaSignHash* APIs are public.
 */

// =============== ECJPAKE ===============

/** @brief ECJPAKE 协议角色。 */
typedef enum XCryptographic_EcjPakeRole {
    XCryptographic_EcjPakeRole_Client = 0, /**< ECJPAKE 客户端。 */
    XCryptographic_EcjPakeRole_Server      /**< ECJPAKE 服务端。 */
} XCryptographic_EcjPakeRole;

/** @brief ECJPAKE 不透明协议上下文，仅支持 secp256r1 与 SHA-256。 */
typedef struct XCryptographic_EcjPakeContext XCryptographic_EcjPakeContext;

/**
 * @brief 使用口令创建 ECJPAKE 协议上下文。
 * @param role 本端协议角色。
 * @param password 预共享口令视图，函数会复制并保护其内容。
 * @return 新建不透明上下文；失败返回 NULL，调用者负责 destroy。
 */
XCryptographic_EcjPakeContext* XCryptographic_ecjPakeCreate(
    XCryptographic_EcjPakeRole role, XByteArrayView password);
/**
 * @brief 清理 ECJPAKE 协议上下文及其中的敏感状态。
 * @param context 要销毁的上下文；传 NULL 无操作。
 */
void XCryptographic_ecjPakeDestroy(XCryptographic_EcjPakeContext* context);
/**
 * @brief 生成 ECJPAKE 第一轮 TLS 格式消息。
 * @param context 已创建且处于第一轮发送状态的上下文。
 * @param buffer 输出消息缓冲区。
 * @param bufferSize 输出容量。
 * @return 指向 buffer 的消息视图；状态或容量错误返回空视图。
 */
XByteArrayView XCryptographic_ecjPakeWriteRoundOneInto(
    XCryptographic_EcjPakeContext* context, char* buffer, size_t bufferSize);
/**
 * @brief 验证并处理 ECJPAKE 对端第一轮 TLS 格式消息。
 * @param context 本端 ECJPAKE 上下文。
 * @param message 对端第一轮 TLS 格式消息。
 * @return 校验通过并接受消息返回 true，否则返回 false。
 */
bool XCryptographic_ecjPakeReadRoundOne(
    XCryptographic_EcjPakeContext* context, XByteArrayView message);
/**
 * @brief 生成 ECJPAKE 第二轮 TLS 格式消息。
 * @param context 已接受对端第一轮消息的上下文。
 * @param buffer 输出消息缓冲区。
 * @param bufferSize 输出容量。
 * @return 指向 buffer 的消息视图；状态或容量错误返回空视图。
 */
XByteArrayView XCryptographic_ecjPakeWriteRoundTwoInto(
    XCryptographic_EcjPakeContext* context, char* buffer, size_t bufferSize);
/**
 * @brief 验证并处理 ECJPAKE 对端第二轮 TLS 格式消息。
 * @param context 本端 ECJPAKE 上下文。
 * @param message 对端第二轮 TLS 格式消息。
 * @return 校验通过并接受消息返回 true，否则返回 false。
 */
bool XCryptographic_ecjPakeReadRoundTwo(
    XCryptographic_EcjPakeContext* context, XByteArrayView message);
/**
 * @brief 导出 65 字节无压缩 ECJPAKE 共享点。
 * @param context 已完成两轮交互的上下文。
 * @param buffer 输出共享点缓冲区。
 * @param bufferSize 输出容量，至少为 65 字节。
 * @return 指向 buffer 的共享点视图；上下文未完成或容量不足返回空视图。
 */
XByteArrayView XCryptographic_ecjPakeWriteSharedKeyInto(
    XCryptographic_EcjPakeContext* context, char* buffer, size_t bufferSize);

// =============== RSA ===============

/** @brief RSA 密钥不透明对象。 */
typedef struct XCryptographic_RsaKey XCryptographic_RsaKey;

/** @brief RSA 填充方式。 */
typedef enum XCryptographic_RsaPadding {
    XCryptographic_RsaPadding_Pkcs1 = 0,  ///< PKCS#1 v1.5 填充
    XCryptographic_RsaPadding_Oaep,       ///< OAEP 填充
    XCryptographic_RsaPadding_Pss         ///< PSS 填充
} XCryptographic_RsaPadding;

/**
 * @brief 从 DER 私钥导入 RSA 密钥。
 * @param der PKCS#1 或兼容格式的 DER 私钥视图。
 * @return 新建 RSA 密钥对象；失败返回 NULL，调用者负责销毁。
 */
XCryptographic_RsaKey* XCryptographic_rsaImportPrivateKey(XByteArrayView der);
/**
 * @brief 从 DER 公钥导入 RSA 密钥。
 * @param der PKCS#1/SubjectPublicKeyInfo DER 公钥视图。
 * @return 新建 RSA 密钥对象；失败返回 NULL，调用者负责销毁。
 */
XCryptographic_RsaKey* XCryptographic_rsaImportPublicKey(XByteArrayView der);
/**
 * @brief 销毁 RSA 密钥并清零敏感材料。
 * @param key 要销毁的 RSA 密钥；传 NULL 无操作。
 */
void XCryptographic_rsaDestroyKey(XCryptographic_RsaKey* key);
/**
 * @brief 返回 RSA 模数字节数。
 * @param key RSA 密钥对象。
 * @return 模数长度（字节）；key 无效返回 0。
 */
size_t XCryptographic_rsaKeyBytes(const XCryptographic_RsaKey* key);
/**
 * @brief 判断 RSA 密钥是否含私钥。
 * @param key RSA 密钥对象。
 * @return 含可用于私钥操作的材料返回 true，否则返回 false。
 */
bool XCryptographic_rsaIsPrivateKey(const XCryptographic_RsaKey* key);
/**
 * @brief 导出 RSA 公钥 DER。
 * @param buffer 输出 DER 缓冲区。
 * @param bufferSize 输出容量。
 * @param key RSA 密钥对象。
 * @return 指向 buffer 的 DER 视图；失败返回空视图。
 */
XByteArrayView XCryptographic_rsaExportPublicKeyInto(
    char* buffer, size_t bufferSize, const XCryptographic_RsaKey* key);
/**
 * @brief 导出 RSA 私钥 DER。
 * @param buffer 输出 DER 缓冲区。
 * @param bufferSize 输出容量。
 * @param key 含私钥的 RSA 密钥对象。
 * @return 指向 buffer 的 DER 视图；key 不含私钥或容量不足返回空视图。
 */
XByteArrayView XCryptographic_rsaExportPrivateKeyInto(
    char* buffer, size_t bufferSize, const XCryptographic_RsaKey* key);
/**
 * @brief 导出 RSA 私钥 DER（返回新对象）。
 * @param key 含私钥的 RSA 密钥对象。
 * @return 新建 DER 数组；失败返回 NULL，调用者负责释放。
 */
XByteArray* XCryptographic_rsaExportPrivateKey(const XCryptographic_RsaKey* key);
/**
 * @brief 生成 RSA 密钥对。
 * @param bits RSA 模数位数。
 * @param exponent 公钥指数，通常为 65537。
 * @return 新建 RSA 私钥对象；失败返回 NULL，调用者负责销毁。
 */
XCryptographic_RsaKey* XCryptographic_rsaGenerateKey(
    unsigned bits, uint32_t exponent);
/**
 * @brief RSA 公钥加密。
 * @param buffer 输出密文缓冲区，容量至少为 RSA 模数字节数。
 * @param bufferSize 输出容量。
 * @param key RSA 公钥或含公钥的密钥对象。
 * @param padding PKCS#1 v1.5 或 OAEP；PSS 不用于加密。
 * @param hash_alg OAEP 使用的哈希算法，PKCS#1 v1.5 可按实现约束传入。
 * @param label OAEP label；PKCS#1 v1.5 时应为空视图。
 * @param input 待加密明文。
 * @return 指向 buffer 的密文视图；失败返回空视图。
 */
XByteArrayView XCryptographic_rsaEncryptInto(
    char* buffer, size_t bufferSize, const XCryptographic_RsaKey* key,
    XCryptographic_RsaPadding padding, XCryptographicHash_Algorithm hash_alg,
    XByteArrayView label, XByteArrayView input);
/**
 * @brief RSA 公钥加密（返回新对象）。
 * @param key RSA 公钥或含公钥的密钥对象。
 * @param padding 加密填充模式。
 * @param hash_alg OAEP 使用的哈希算法。
 * @param label OAEP label。
 * @param input 待加密明文。
 * @return 新建密文数组；失败返回 NULL，调用者负责释放。
 */
XByteArray* XCryptographic_rsaEncrypt(
    const XCryptographic_RsaKey* key, XCryptographic_RsaPadding padding,
    XCryptographicHash_Algorithm hash_alg, XByteArrayView label,
    XByteArrayView input);
/**
 * @brief RSA 私钥解密。
 * @param buffer 输出明文缓冲区。
 * @param bufferSize 输出容量。
 * @param key 含私钥的 RSA 密钥对象。
 * @param padding 解密填充模式。
 * @param hash_alg OAEP 使用的哈希算法。
 * @param label OAEP label，必须与加密时一致。
 * @param input RSA 密文视图。
 * @param output_len 实际明文长度输出指针，可为 NULL。
 * @return 指向 buffer 的明文视图；解密或填充校验失败返回空视图。
 */
XByteArrayView XCryptographic_rsaDecryptInto(
    char* buffer, size_t bufferSize, const XCryptographic_RsaKey* key,
    XCryptographic_RsaPadding padding, XCryptographicHash_Algorithm hash_alg,
    XByteArrayView label, XByteArrayView input, size_t* output_len);
/**
 * @brief RSA 私钥解密（返回新对象）。
 * @param key 含私钥的 RSA 密钥对象。
 * @param padding 解密填充模式。
 * @param hash_alg OAEP 使用的哈希算法。
 * @param label OAEP label，必须与加密时一致。
 * @param input RSA 密文视图。
 * @return 新建明文数组；失败返回 NULL，调用者负责释放。
 */
XByteArray* XCryptographic_rsaDecrypt(
    const XCryptographic_RsaKey* key, XCryptographic_RsaPadding padding,
    XCryptographicHash_Algorithm hash_alg, XByteArrayView label,
    XByteArrayView input);
/**
 * @brief RSA 私钥签名摘要。
 * @param buffer 输出签名缓冲区，容量至少为 RSA 模数字节数。
 * @param bufferSize 输出容量。
 * @param key 含私钥的 RSA 密钥对象。
 * @param padding PKCS#1 v1.5 或 PSS 签名填充。
 * @param hash_alg 摘要算法。
 * @param hash 待签名摘要视图。
 * @return 指向 buffer 的签名视图；失败返回空视图。
 */
XByteArrayView XCryptographic_rsaSignHashInto(
    char* buffer, size_t bufferSize, const XCryptographic_RsaKey* key,
    XCryptographic_RsaPadding padding, XCryptographicHash_Algorithm hash_alg,
    XByteArrayView hash);
/**
 * @brief RSA 私钥签名摘要（返回新对象）。
 * @param key 含私钥的 RSA 密钥对象。
 * @param padding PKCS#1 v1.5 或 PSS 签名填充。
 * @param hash_alg 摘要算法。
 * @param hash 待签名摘要视图。
 * @return 新建签名数组；失败返回 NULL，调用者负责释放。
 */
XByteArray* XCryptographic_rsaSignHash(
    const XCryptographic_RsaKey* key, XCryptographic_RsaPadding padding,
    XCryptographicHash_Algorithm hash_alg, XByteArrayView hash);
/**
 * @brief RSA 公钥验证签名。
 * @param key RSA 公钥或含公钥的密钥对象。
 * @param padding PKCS#1 v1.5 或 PSS 签名填充。
 * @param hash_alg 摘要算法。
 * @param hash 被验证摘要视图。
 * @param signature RSA 签名视图。
 * @return 签名有效返回 true，否则返回 false。
 */
bool XCryptographic_rsaVerifyHash(
    const XCryptographic_RsaKey* key, XCryptographic_RsaPadding padding,
    XCryptographicHash_Algorithm hash_alg, XByteArrayView hash,
    XByteArrayView signature);

#ifdef __cplusplus
}
#endif

#endif /* XCRYPTOGRAPHIC_H */
