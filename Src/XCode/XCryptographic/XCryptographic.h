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

#include "XClass.h"
#include "XCryptographic_config.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============== 前向声明 ===============

typedef struct XByteArray XByteArray;
typedef struct XString XString;
typedef struct XIODevice XIODevice;

#include "XByteArrayView.h"   ///< 使用项目中真正的XByteArrayView（非拥有型只读视图）

// =============== 算法枚举（对齐Qt 6.8）==============

/**
 * @brief 加密哈希算法类型
 * @note 值与Qt 6.8 QCryptographicHash::Algorithm 保持一致
 */
typedef enum XCryptographicHash_Algorithm {
    XCryptographicHash_Md4 = 0,             ///< MD4 (128位)
    XCryptographicHash_Md5 = 1,             ///< MD5 (128位)
    XCryptographicHash_Sha1 = 2,            ///< SHA-1 (160位)
    XCryptographicHash_Sha224 = 3,          ///< SHA-224 (224位)
    XCryptographicHash_Sha256 = 4,          ///< SHA-256 (256位)
    XCryptographicHash_Sha384 = 5,          ///< SHA-384 (384位)
    XCryptographicHash_Sha512 = 6,          ///< SHA-512 (512位)
    XCryptographicHash_Keccak_224 = 7,      ///< Keccak-224 (Qt 5.9.2+)
    XCryptographicHash_Keccak_256 = 8,      ///< Keccak-256
    XCryptographicHash_Keccak_384 = 9,      ///< Keccak-384
    XCryptographicHash_Keccak_512 = 10,     ///< Keccak-512
    XCryptographicHash_RealSha3_224 = 11,   ///< SHA3-224 (原始SHA3)
    XCryptographicHash_RealSha3_256 = 12,   ///< SHA3-256
    XCryptographicHash_RealSha3_384 = 13,   ///< SHA3-384
    XCryptographicHash_RealSha3_512 = 14,   ///< SHA3-512
    // 别名（Qt兼容：Sha3_* 实际是 RealSha3_*）
    XCryptographicHash_Sha3_224 = XCryptographicHash_RealSha3_224,
    XCryptographicHash_Sha3_256 = XCryptographicHash_RealSha3_256,
    XCryptographicHash_Sha3_384 = XCryptographicHash_RealSha3_384,
    XCryptographicHash_Sha3_512 = XCryptographicHash_RealSha3_512,
    XCryptographicHash_Blake2b_160 = 15,    ///< BLAKE2b-160 (Qt 6.0+)
    XCryptographicHash_Blake2b_256 = 16,    ///< BLAKE2b-256
    XCryptographicHash_Blake2b_384 = 17,    ///< BLAKE2b-384
    XCryptographicHash_Blake2b_512 = 18,    ///< BLAKE2b-512
    XCryptographicHash_Blake2s_128 = 19,    ///< BLAKE2s-128
    XCryptographicHash_Blake2s_160 = 20,    ///< BLAKE2s-160
    XCryptographicHash_Blake2s_224 = 21,    ///< BLAKE2s-224
    XCryptographicHash_Blake2s_256 = 22,    ///< BLAKE2s-256
    XCryptographicHash_Ripemd160 = 23,      ///< RIPEMD-160 算法
    XCryptographicHash_NumAlgorithms = 24   ///< 算法数量
} XCryptographicHash_Algorithm;

// =============== 虚函数表定义 ===============

/**
 * @brief XCryptographicHash虚函数表枚举
 * @note 继承XClass，重载Deinit用于清理资源
 */
XCLASS_DEFINE_BEGING(XCryptographicHash)
XCLASS_DEFINE_EXTEND_END(XCryptographicHash, XClass)

/**
 * @brief 初始化XCryptographicHash类的虚函数表
 * @return 指向初始化完成的XVtable的指针
 */
XVtable* XCryptographicHash_class_init(void);

// =============== 核心结构体 ===============

/**
 * @brief 加密哈希上下文（继承XClass）
 * @note 内部使用XByteArray作为缓冲区
 */
typedef struct XCryptographicHash {
    XClass base;                                    ///< 基类
    XCryptographicHash_Algorithm algorithm;         ///< 哈希算法
    XByteArray* buffer;                             ///< 内部缓冲区（XByteArray）
    XByteArray* resultCache;                        ///< 结果缓存（resultView用）
    uint64_t totalLen;                              ///< 总数据长度
    bool finalized;                                 ///< 是否已计算最终结果
} XCryptographicHash;

// =============== 构造/析构函数 ===============

/**
 * @brief 在堆上创建哈希上下文
 * @param method 哈希算法
 * @return 哈希上下文指针，失败返回NULL
 */
XCryptographicHash* XCryptographicHash_create(XCryptographicHash_Algorithm method);
void XCryptographicHash_init(XCryptographicHash* hash, XCryptographicHash_Algorithm method);
#define XCryptographicHash_copy_base XClass_copy_base

/**
* @brief 移动操作的基础实现
* @details 复用基类XClass的移动逻辑，作为容器移动的默认实现
*/
#define XCryptographicHash_move_base XClass_move_base

/**
* @brief 销毁操作的基础实现
* @details 复用基类XClass的销毁逻辑，作为容器销毁的默认实现
*/
#define XCryptographicHash_deinit_base XClass_deinit_base

/**
* @brief 内存释放的基础实现
* @details 复用基类XClass的内存释放逻辑，作为容器内存释放的默认实现
*/
#define XCryptographicHash_delete_base XClass_delete_base
// =============== 数据输入 ===============

/**
 * @brief 添加数据到哈希计算
 * @param hash 哈希上下文
 * @param data 数据指针
 * @param len 数据长度
 */
void XCryptographicHash_addData(XCryptographicHash* hash, const char* data, size_t len);

/**
 * @brief 添加字节数组视图到哈希计算
 * @param hash 哈希上下文
 * @param bytes 字节数组视图
 */
void XCryptographicHash_addData_2(XCryptographicHash* hash, XByteArrayView bytes);

/**
 * @brief 添加字节数组到哈希计算
 * @param hash 哈希上下文
 * @param data 字节数组
 */
void XCryptographicHash_addData_3(XCryptographicHash* hash, const XByteArray* data);

/**
 * @brief 从IODevice读取数据并添加到哈希计算
 * @param hash 哈希上下文
 * @param device IO设备
 * @return 成功返回true
 */
bool XCryptographicHash_addData_4(XCryptographicHash* hash, XIODevice* device);

/**
 * @brief 重置哈希上下文（可重新计算）
 * @param hash 哈希上下文
 */
void XCryptographicHash_reset(XCryptographicHash* hash);

// =============== 结果获取 ===============

/**
 * @brief 获取哈希结果（返回新的XByteArray）
 * @param hash 哈希上下文
 * @return 哈希结果字节数组（调用者需删除）
 */
XByteArray* XCryptographicHash_result(XCryptographicHash* hash);

/**
 * @brief 获取哈希结果视图（不分配内存，指向内部缓存）
 * @param hash 哈希上下文
 * @return 哈希结果视图（仅在hash未修改时有效）
 * @note Qt 6.3+ resultView()
 */
XByteArrayView XCryptographicHash_resultView(XCryptographicHash* hash);

// =============== 算法信息 ===============

/**
 * @brief 获取当前使用的算法
 * @param hash 哈希上下文
 * @return 哈希算法
 */
XCryptographicHash_Algorithm XCryptographicHash_algorithm(const XCryptographicHash* hash);

// =============== 静态便捷方法 ===============

/**
 * @brief 一次性计算哈希
 * @param data 数据指针
 * @param len 数据长度
 * @param method 哈希算法
 * @return 哈希结果字节数组（调用者需删除）
 */
XByteArray* XCryptographicHash_hash(const char* data, size_t len, XCryptographicHash_Algorithm method);

/**
 * @brief 一次性计算哈希（字节数组视图）
 * @param data 字节数组视图
 * @param method 哈希算法
 * @return 哈希结果字节数组（调用者需删除）
 */
XByteArray* XCryptographicHash_hash_2(XByteArrayView data, XCryptographicHash_Algorithm method);

/**
 * @brief 一次性计算哈希（字节数组）
 * @param data 字节数组
 * @param method 哈希算法
 * @return 哈希结果字节数组（调用者需删除）
 */
XByteArray* XCryptographicHash_hash_3(const XByteArray* data, XCryptographicHash_Algorithm method);

/**
 * @brief 将哈希结果写入提供的缓冲区（无内存分配，Qt 6.8+）
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param data 数据指针
 * @param dataLen 数据长度
 * @param method 哈希算法
 * @return 成功返回有效视图，缓冲区不足返回空视图
 */
XByteArrayView XCryptographicHash_hashInto(
    char* buffer, size_t bufferSize,
    const char* data, size_t dataLen,
    XCryptographicHash_Algorithm method
);

/**
 * @brief 将多个数据的哈希结果写入提供的缓冲区（Qt 6.8+）
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param dataArray 数据数组
 * @param count 数据数量
 * @param method 哈希算法
 * @return 成功返回有效视图，缓冲区不足返回空视图
 */
XByteArrayView XCryptographicHash_hashInto_1(
    char* buffer, size_t bufferSize,
    const XByteArrayView* dataArray, size_t count,
    XCryptographicHash_Algorithm method
);

/** @brief RFC 8554 LM-OTS SHA256_N32_W8 public-key candidate. */
bool XCryptographic_lmotsCalculatePublicKeyCandidate(
    XByteArrayView keyIdentifier, uint32_t leafIdentifier,
    XByteArrayView message, XByteArrayView signature,
    uint8_t* output, size_t outputSize);

/** @brief RFC 8554 LMS SHA256_M32_H10/LMOTS-SHA256_N32_W8 verification. */
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
 * @param privateKey 输出私钥缓冲区，至少 4896 字节。
 */
bool XCryptographic_mldsa87KeyPair(
    XByteArrayView seed, uint8_t* publicKey, size_t publicKeySize,
    uint8_t* privateKey, size_t privateKeySize);

/**
 * @brief 使用给定随机性和可选上下文对消息进行 ML-DSA-87 签名。
 * @param randomness 32 字节确定性签名随机输入。
 * @param context FIPS 204 上下文，长度不得超过 255 字节。
 */
bool XCryptographic_mldsa87Sign(
    XByteArrayView message, XByteArrayView context, XByteArrayView randomness,
    XByteArrayView privateKey, uint8_t* signature, size_t signatureSize);

/** @brief 验证 ML-DSA-87 签名，context 必须与签名时一致。 */
bool XCryptographic_mldsa87Verify(
    XByteArrayView message, XByteArrayView context, XByteArrayView signature,
    XByteArrayView publicKey);

/**
 * @brief 获取算法输出长度（字节）
 * @param method 哈希算法
 * @return 输出长度（字节）
 */
int XCryptographicHash_hashLength(XCryptographicHash_Algorithm method);

/**
 * @brief 检查算法是否支持
 * @param method 哈希算法
 * @return 支持返回true
 */
bool XCryptographicHash_supportsAlgorithm(XCryptographicHash_Algorithm method);

/**
 * @brief 获取算法名称
 * @param method 哈希算法
 * @return 算法名称字符串
 */
const char* XCryptographicHash_algorithmName(XCryptographicHash_Algorithm method);

// =============== HMAC 函数（对齐Qt QMessageAuthenticationCode）===============

/**
 * @brief 计算HMAC（Hash-based Message Authentication Code）
 * @param key 密钥数据
 * @param keyLen 密钥长度
 * @param message 消息数据
 * @param msgLen 消息长度
 * @param method 哈希算法
 * @return HMAC结果字节数组（调用者需删除）
 * @note 对齐Qt QMessageAuthenticationCode::hash()
 */
XByteArray* XCryptographicHash_hmac(
    const char* key, size_t keyLen,
    const char* message, size_t msgLen,
    XCryptographicHash_Algorithm method
);

/**
 * @brief 计算HMAC（字节数组视图版本）
 * @param key 密钥视图
 * @param message 消息视图
 * @param method 哈希算法
 * @return HMAC结果字节数组（调用者需删除）
 */
XByteArray* XCryptographicHash_hmac_2(
    XByteArrayView key,
    XByteArrayView message,
    XCryptographicHash_Algorithm method
);

/**
 * @brief 计算HMAC（XByteArray版本）
 * @param key 密钥字节数组
 * @param message 消息字节数组
 * @param method 哈希算法
 * @return HMAC结果字节数组（调用者需删除）
 */
XByteArray* XCryptographicHash_hmac_3(
    const XByteArray* key,
    const XByteArray* message,
    XCryptographicHash_Algorithm method
);

/**
 * @brief 将HMAC结果写入提供的缓冲区（无内存分配）
 * @param buffer 输出缓冲区
 * @param bufferSize 缓冲区大小
 * @param key 密钥数据
 * @param keyLen 密钥长度
 * @param message 消息数据
 * @param msgLen 消息长度
 * @param method 哈希算法
 * @return 成功返回有效视图，缓冲区不足返回空视图
 */
XByteArrayView XCryptographicHash_hmacInto(
    char* buffer, size_t bufferSize,
    const char* key, size_t keyLen,
    const char* message, size_t msgLen,
    XCryptographicHash_Algorithm method
);

/**
 * @brief 流式 HMAC 上下文。
 * @note 值类型；使用 XCryptographic_hmacSetup 初始化，完成后调用
 *       XCryptographic_hmacAbort 清理。
 */
typedef struct XCryptographic_HmacOperation {
    XCryptographicHash_Algorithm algorithm;
    XCryptographicHash* inner;
    uint8_t opad[144];
    size_t blockSize;
    bool active;
} XCryptographic_HmacOperation;

/** @brief 初始化流式 HMAC 操作。 */
bool XCryptographic_hmacSetup(XCryptographic_HmacOperation* operation,
                              XByteArrayView key,
                              XCryptographicHash_Algorithm algorithm);

/** @brief 向流式 HMAC 操作追加消息数据。 */
bool XCryptographic_hmacUpdate(XCryptographic_HmacOperation* operation,
                               XByteArrayView message);

/** @brief 完成流式 HMAC 操作并写入摘要。 */
XByteArrayView XCryptographic_hmacFinishInto(XCryptographic_HmacOperation* operation,
                                             char* buffer, size_t bufferSize);

/** @brief 清理流式 HMAC 操作及其中的密钥派生状态。 */
void XCryptographic_hmacAbort(XCryptographic_HmacOperation* operation);

// =============== 通用密码原语 ===============

/**
 * @brief 密钥用途
 * @note 密钥内容只在 XCryptographic 的对称加密、密钥交换和签名函数之间传递。
 */
typedef enum XCryptographic_KeyType {
    XCryptographic_KeyType_None = 0,
    XCryptographic_KeyType_AesCtr,
    XCryptographic_KeyType_AesGcm,
    XCryptographic_KeyType_AesCcm,
    XCryptographic_KeyType_AriaGcm,
    XCryptographic_KeyType_AriaCcm,
    XCryptographic_KeyType_CamelliaGcm,
    XCryptographic_KeyType_CamelliaCcm,
    XCryptographic_KeyType_ChaCha20Poly1305,
    XCryptographic_KeyType_X25519,
    XCryptographic_KeyType_X448,
    XCryptographic_KeyType_EcdhNistP256,
    XCryptographic_KeyType_EcdhNistP384,
    XCryptographic_KeyType_EcdhNistP521,
    XCryptographic_KeyType_EcdhSecp256k1,
    XCryptographic_KeyType_EcdhBrainpoolP256r1,
    XCryptographic_KeyType_EcdhBrainpoolP384r1,
    XCryptographic_KeyType_EcdhBrainpoolP512r1,
    XCryptographic_KeyType_EcdsaNistP256Private,
    XCryptographic_KeyType_EcdsaNistP256Public,
    XCryptographic_KeyType_EcdsaNistP384Private,
    XCryptographic_KeyType_EcdsaNistP384Public,
    XCryptographic_KeyType_EcdsaNistP521Private,
    XCryptographic_KeyType_EcdsaNistP521Public,
    XCryptographic_KeyType_EcdsaSecp256k1Private,
    XCryptographic_KeyType_EcdsaSecp256k1Public,
    XCryptographic_KeyType_EcdsaBrainpoolP256r1Private,
    XCryptographic_KeyType_EcdsaBrainpoolP256r1Public,
    XCryptographic_KeyType_EcdsaBrainpoolP384r1Private,
    XCryptographic_KeyType_EcdsaBrainpoolP384r1Public,
    XCryptographic_KeyType_EcdsaBrainpoolP512r1Private,
    XCryptographic_KeyType_EcdsaBrainpoolP512r1Public
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
    XCryptographic_EcdhAlgorithm_None = 0,
    XCryptographic_EcdhAlgorithm_X25519,
    XCryptographic_EcdhAlgorithm_X448,
    XCryptographic_EcdhAlgorithm_NistP256,
    XCryptographic_EcdhAlgorithm_NistP384,
    XCryptographic_EcdhAlgorithm_NistP521,
    XCryptographic_EcdhAlgorithm_Secp256k1,
    XCryptographic_EcdhAlgorithm_BrainpoolP256r1,
    XCryptographic_EcdhAlgorithm_BrainpoolP384r1,
    XCryptographic_EcdhAlgorithm_BrainpoolP512r1
} XCryptographic_EcdhAlgorithm;

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
 * @brief 处理 AES-CTR 流的一段输入
 * @return 成功返回指向 buffer 的有效视图，失败返回空视图
 */
XByteArrayView XCryptographic_aesCtrUpdateInto(
    XCryptographic_CipherOperation* operation,
    char* buffer, size_t bufferSize, XByteArrayView data);
/** @brief 将 AES-CTR 处理结果写入新建的 XByteArray；调用者负责删除返回值。 */
XByteArray* XCryptographic_aesCtrUpdate(
    XCryptographic_CipherOperation* operation, XByteArrayView data);
/** @brief 清理 AES-CTR 流上下文中的密钥派生数据。 */
void XCryptographic_aesCtrAbort(XCryptographic_CipherOperation* operation);

/**
 * @brief 生成 X25519/X448、NIST P-256/P-384/P-521、secp256k1 或 brainpool ECDH 临时密钥
 * @param algorithm 密钥协商算法
 * @param result 输出密钥对象
 * @return 成功返回 true
 */
bool XCryptographic_ecdhGenerateKey(XCryptographic_EcdhAlgorithm algorithm,
                                        XCryptographic_Key* result);
/**
 * @brief 导出 ECDH 或 ECDSA 公钥
 * @return 公钥字节数组；失败返回 NULL，调用者负责删除返回值
 */
XByteArray* XCryptographic_exportPublicKey(XCryptographic_Key key);
/** @brief 将公钥写入提供的缓冲区（无内存分配）。 */
XByteArrayView XCryptographic_exportPublicKeyInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key);
/**
 * @brief 计算 ECDH 共享密钥
 * @return 共享密钥字节数组；失败返回 NULL，调用者负责删除返回值
 */
XByteArray* XCryptographic_ecdhAgree(XCryptographic_Key privateKey,
                                         XByteArrayView peerPublicKey);
/** @brief 将 ECDH 共享密钥写入提供的缓冲区（无内存分配）。 */
XByteArrayView XCryptographic_ecdhAgreeInto(
    char* buffer, size_t bufferSize, XCryptographic_Key privateKey,
    XByteArrayView peerPublicKey);

/** @brief 从 32 字节 P-256 私钥标量导入 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaP256ImportPrivateKey(XByteArrayView key,
                                                  XCryptographic_Key* result);
/** @brief 生成 P-256 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaP256GenerateKey(XCryptographic_Key* result);
/** @brief 导出 P-256 ECDSA 私钥标量。 */
XByteArray* XCryptographic_ecdsaP256ExportPrivateKey(XCryptographic_Key key);
/** @brief 将 P-256 ECDSA 私钥标量写入提供的缓冲区。 */
XByteArrayView XCryptographic_ecdsaP256ExportPrivateKeyInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key);
/** @brief 导入 65 字节 P-256 ECDSA 公钥用于验签。 */
bool XCryptographic_ecdsaP256ImportPublicKey(XByteArrayView key,
                                                 XCryptographic_Key* result);
/** @brief 对 SHA-256 摘要进行 P-256 ECDSA 签名，输出为 64 字节 r||s。 */
XByteArray* XCryptographic_ecdsaP256SignHash(XCryptographic_Key key,
                                                  XByteArrayView hash);
/** @brief 将 P-256 ECDSA 签名写入提供的缓冲区，结果为 64 字节 r||s。 */
XByteArrayView XCryptographic_ecdsaP256SignHashInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 验证 64 字节 r||s 格式的 P-256 ECDSA 签名。 */
bool XCryptographic_ecdsaP256VerifyHash(XCryptographic_Key key,
                                            XByteArrayView hash,
                                            XByteArrayView signature);
/** @brief 从 48 字节 P-384 私钥标量导入 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaP384ImportPrivateKey(XByteArrayView key,
                                              XCryptographic_Key* result);
/** @brief 生成 P-384 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaP384GenerateKey(XCryptographic_Key* result);
/** @brief 导入 97 字节 P-384 ECDSA 公钥用于验签。 */
bool XCryptographic_ecdsaP384ImportPublicKey(XByteArrayView key,
                                             XCryptographic_Key* result);
/** @brief 对摘要进行 P-384 ECDSA 签名，输出为 96 字节 r||s。 */
XByteArrayView XCryptographic_ecdsaP384SignHashInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 对摘要进行确定性 P-384 ECDSA 签名，输出为 96 字节 r||s。 */
XByteArrayView XCryptographic_ecdsaP384SignHashDeterministicInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 验证 96 字节 r||s 格式的 P-384 ECDSA 签名。 */
bool XCryptographic_ecdsaP384VerifyHash(XCryptographic_Key key,
                                        XByteArrayView hash,
                                        XByteArrayView signature);
/** @brief 从 66 字节 P-521 私钥标量导入 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaP521ImportPrivateKey(XByteArrayView key,
                                              XCryptographic_Key* result);
/** @brief 生成 P-521 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaP521GenerateKey(XCryptographic_Key* result);
/** @brief 导入 133 字节 P-521 ECDSA 公钥用于验签。 */
bool XCryptographic_ecdsaP521ImportPublicKey(XByteArrayView key,
                                             XCryptographic_Key* result);
/** @brief 对摘要进行 P-521 ECDSA 签名，输出为 132 字节 r||s。 */
XByteArrayView XCryptographic_ecdsaP521SignHashInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 对摘要进行确定性 P-521 ECDSA 签名，输出为 132 字节 r||s。 */
XByteArrayView XCryptographic_ecdsaP521SignHashDeterministicInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 验证 132 字节 r||s 格式的 P-521 ECDSA 签名。 */
bool XCryptographic_ecdsaP521VerifyHash(XCryptographic_Key key,
                                        XByteArrayView hash,
                                        XByteArrayView signature);
/** @brief 从 48 字节 brainpoolP384r1 私钥标量导入 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaBrainpoolP384r1ImportPrivateKey(
    XByteArrayView key, XCryptographic_Key* result);
/** @brief 生成 brainpoolP384r1 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaBrainpoolP384r1GenerateKey(XCryptographic_Key* result);
/** @brief 导入 97 字节 brainpoolP384r1 ECDSA 公钥用于验签。 */
bool XCryptographic_ecdsaBrainpoolP384r1ImportPublicKey(
    XByteArrayView key, XCryptographic_Key* result);
/** @brief 对摘要进行 brainpoolP384r1 ECDSA 签名，输出为 96 字节 r||s。 */
XByteArrayView XCryptographic_ecdsaBrainpoolP384r1SignHashInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 对摘要进行确定性 brainpoolP384r1 ECDSA 签名。 */
XByteArrayView XCryptographic_ecdsaBrainpoolP384r1SignHashDeterministicInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 验证 96 字节 r||s 格式的 brainpoolP384r1 ECDSA 签名。 */
bool XCryptographic_ecdsaBrainpoolP384r1VerifyHash(
    XCryptographic_Key key, XByteArrayView hash, XByteArrayView signature);
/** @brief 从 64 字节 brainpoolP512r1 私钥标量导入 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaBrainpoolP512r1ImportPrivateKey(
    XByteArrayView key, XCryptographic_Key* result);
/** @brief 生成 brainpoolP512r1 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaBrainpoolP512r1GenerateKey(XCryptographic_Key* result);
/** @brief 导入 129 字节 brainpoolP512r1 ECDSA 公钥用于验签。 */
bool XCryptographic_ecdsaBrainpoolP512r1ImportPublicKey(
    XByteArrayView key, XCryptographic_Key* result);
/** @brief 对摘要进行 brainpoolP512r1 ECDSA 签名，输出为 128 字节 r||s。 */
XByteArrayView XCryptographic_ecdsaBrainpoolP512r1SignHashInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 对摘要进行确定性 brainpoolP512r1 ECDSA 签名。 */
XByteArrayView XCryptographic_ecdsaBrainpoolP512r1SignHashDeterministicInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 验证 128 字节 r||s 格式的 brainpoolP512r1 ECDSA 签名。 */
bool XCryptographic_ecdsaBrainpoolP512r1VerifyHash(
    XCryptographic_Key key, XByteArrayView hash, XByteArrayView signature);
/** @brief 从 32 字节 secp256k1 私钥标量导入 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaSecp256k1ImportPrivateKey(XByteArrayView key,
                                                    XCryptographic_Key* result);
/** @brief 生成 secp256k1 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaSecp256k1GenerateKey(XCryptographic_Key* result);
/** @brief 导入 65 字节 secp256k1 ECDSA 公钥用于验签。 */
bool XCryptographic_ecdsaSecp256k1ImportPublicKey(XByteArrayView key,
                                                   XCryptographic_Key* result);
/** @brief 对摘要进行 secp256k1 ECDSA 签名，输出为 64 字节 r||s。 */
XByteArrayView XCryptographic_ecdsaSecp256k1SignHashInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 对摘要进行确定性 secp256k1 ECDSA 签名，输出为 64 字节 r||s。 */
XByteArrayView XCryptographic_ecdsaSecp256k1SignHashDeterministicInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 验证 64 字节 r||s 格式的 secp256k1 ECDSA 签名。 */
bool XCryptographic_ecdsaSecp256k1VerifyHash(XCryptographic_Key key,
                                              XByteArrayView hash,
                                              XByteArrayView signature);
/** @brief 从 32 字节 brainpoolP256r1 私钥标量导入 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaBrainpoolP256r1ImportPrivateKey(
    XByteArrayView key, XCryptographic_Key* result);
/** @brief 生成 brainpoolP256r1 ECDSA 签名密钥。 */
bool XCryptographic_ecdsaBrainpoolP256r1GenerateKey(XCryptographic_Key* result);
/** @brief 导入 65 字节 brainpoolP256r1 ECDSA 公钥用于验签。 */
bool XCryptographic_ecdsaBrainpoolP256r1ImportPublicKey(
    XByteArrayView key, XCryptographic_Key* result);
/** @brief 对摘要进行 brainpoolP256r1 ECDSA 签名，输出为 64 字节 r||s。 */
XByteArrayView XCryptographic_ecdsaBrainpoolP256r1SignHashInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 对摘要进行确定性 brainpoolP256r1 ECDSA 签名，输出为 64 字节 r||s。 */
XByteArrayView XCryptographic_ecdsaBrainpoolP256r1SignHashDeterministicInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 验证 64 字节 r||s 格式的 brainpoolP256r1 ECDSA 签名。 */
bool XCryptographic_ecdsaBrainpoolP256r1VerifyHash(
    XCryptographic_Key key, XByteArrayView hash, XByteArrayView signature);
/** @brief 清零由本模块导入或生成的密钥。 */
void XCryptographic_destroyKey(XCryptographic_Key* key);



// =============== SHAKE128/256 XOF ===============

/** @brief 可扩展输出函数（XOF）算法。 */
typedef enum XCryptographic_XofAlgorithm {
    XCryptographic_XofAlgorithm_Shake128 = 0,
    XCryptographic_XofAlgorithm_Shake256
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

/** @brief 初始化 SHAKE128/256 XOF 操作。 */
bool XCryptographic_xofSetup(XCryptographic_XofOperation* operation,
                             XCryptographic_XofAlgorithm algorithm);
/** @brief 向 XOF 操作添加一段输入。 */
bool XCryptographic_xofUpdateInto(XCryptographic_XofOperation* operation,
                                  XByteArrayView data);
/** @brief 从 XOF 操作输出指定长度的字节。 */
XByteArrayView XCryptographic_xofOutputInto(
    XCryptographic_XofOperation* operation, char* buffer, size_t bufferSize,
    size_t outputSize);
/** @brief 清理 XOF 上下文。 */
void XCryptographic_xofAbort(XCryptographic_XofOperation* operation);

// =============== 分组密码（AES/ARIA/Camellia） ===============

/** @brief 分组密码算法。 */
typedef enum XCryptographic_BlockCipherAlgorithm {
    XCryptographic_BlockCipherAlgorithm_Aes = 0,
    XCryptographic_BlockCipherAlgorithm_Aria,
    XCryptographic_BlockCipherAlgorithm_Camellia
} XCryptographic_BlockCipherAlgorithm;

/** @brief 分组密码工作模式。 */
typedef enum XCryptographic_BlockCipherMode {
    XCryptographic_BlockCipherMode_EcbNoPadding = 0,
    XCryptographic_BlockCipherMode_CbcNoPadding,
    XCryptographic_BlockCipherMode_CbcPkcs7,
    XCryptographic_BlockCipherMode_Cfb,
    XCryptographic_BlockCipherMode_Ofb,
    XCryptographic_BlockCipherMode_Xts
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

/** @brief 初始化分组密码流。 */
bool XCryptographic_blockCipherSetup(
    XCryptographic_BlockCipherOperation* operation,
    XCryptographic_BlockCipherAlgorithm algorithm, XByteArrayView key,
    XCryptographic_BlockCipherMode mode, bool encrypt);
/** @brief 初始化 AES 分组密码流。 */
bool XCryptographic_aesBlockCipherSetup(
    XCryptographic_BlockCipherOperation* operation, XByteArrayView key,
    XCryptographic_BlockCipherMode mode, bool encrypt);
/** @brief 设置分组密码初始向量（ECB 不需要）。 */
bool XCryptographic_blockCipherSetIv(
    XCryptographic_BlockCipherOperation* operation, XByteArrayView iv);
/** @brief 设置 AES 分组密码初始向量。 */
bool XCryptographic_aesBlockCipherSetIv(
    XCryptographic_BlockCipherOperation* operation, XByteArrayView iv);
/** @brief 处理分组密码流的一段输入。 */
XByteArrayView XCryptographic_blockCipherUpdateInto(
    XCryptographic_BlockCipherOperation* operation, char* buffer,
    size_t bufferSize, XByteArrayView data);
/** @brief 处理 AES 分组密码流的一段输入。 */
XByteArrayView XCryptographic_aesBlockCipherUpdateInto(
    XCryptographic_BlockCipherOperation* operation, char* buffer,
    size_t bufferSize, XByteArrayView data);
/** @brief 完成分组密码流并输出末尾块/明文。 */
XByteArrayView XCryptographic_blockCipherFinishInto(
    XCryptographic_BlockCipherOperation* operation, char* buffer,
    size_t bufferSize);
/** @brief 完成 AES 分组密码流并输出末尾块/明文。 */
XByteArrayView XCryptographic_aesBlockCipherFinishInto(
    XCryptographic_BlockCipherOperation* operation, char* buffer,
    size_t bufferSize);
/** @brief 清理分组密码上下文。 */
void XCryptographic_blockCipherAbort(XCryptographic_BlockCipherOperation* operation);
/** @brief 清理 AES 分组密码上下文。 */
void XCryptographic_aesBlockCipherAbort(XCryptographic_BlockCipherOperation* operation);

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

/** @brief 初始化 ChaCha20 流。 */
bool XCryptographic_chacha20Setup(XCryptographic_ChaCha20Operation* operation,
                                  XByteArrayView key, XByteArrayView nonce);
/** @brief 处理 ChaCha20 流的一段输入。 */
XByteArrayView XCryptographic_chacha20UpdateInto(
    XCryptographic_ChaCha20Operation* operation, char* buffer,
    size_t bufferSize, XByteArrayView data);
/** @brief 清理 ChaCha20 上下文。 */
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

/** @brief 一次性计算 AES-CMAC。 */
XByteArrayView XCryptographic_aesCmacInto(
    XByteArrayView key, XByteArrayView message, char* buffer, size_t bufferSize);
/** @brief 一次性计算 AES-CMAC（返回新对象）。 */
XByteArray* XCryptographic_aesCmac(XByteArrayView key, XByteArrayView message);
/** @brief 初始化 AES-CMAC 流。 */
bool XCryptographic_aesCmacSetup(XCryptographic_CmacOperation* operation,
                                 XByteArrayView key);
/** @brief 向 AES-CMAC 流添加一段数据。 */
bool XCryptographic_aesCmacUpdate(XCryptographic_CmacOperation* operation,
                                  XByteArrayView message);
/** @brief 完成 AES-CMAC 流并输出 16 字节标签。 */
XByteArrayView XCryptographic_aesCmacFinishInto(
    XCryptographic_CmacOperation* operation, char* buffer, size_t bufferSize);
/** @brief 清理 AES-CMAC 上下文。 */
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

/** @brief 初始化 AES CCM*-无标签流。 */
bool XCryptographic_aesCcmStarNoTagSetup(
    XCryptographic_CcmStarNoTagOperation* operation,
    XByteArrayView key, XByteArrayView nonce);
/** @brief 处理 AES CCM*-无标签流的一段输入。 */
XByteArrayView XCryptographic_aesCcmStarNoTagUpdateInto(
    XCryptographic_CcmStarNoTagOperation* operation, char* buffer,
    size_t bufferSize, XByteArrayView data);
/** @brief 清理 CCM*-无标签上下文。 */
void XCryptographic_aesCcmStarNoTagAbort(
    XCryptographic_CcmStarNoTagOperation* operation);

// =============== 认证加密（AEAD） ===============

/** @brief AEAD 算法。 */
typedef enum XCryptographic_AeadAlgorithm {
    XCryptographic_AeadAlgorithm_AesGcm = 0,
    XCryptographic_AeadAlgorithm_AesCcm,
    XCryptographic_AeadAlgorithm_AriaGcm,
    XCryptographic_AeadAlgorithm_AriaCcm,
    XCryptographic_AeadAlgorithm_CamelliaGcm,
    XCryptographic_AeadAlgorithm_CamelliaCcm,
    XCryptographic_AeadAlgorithm_ChaCha20Poly1305
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

/** @brief 导入 AEAD 对称密钥。 */
bool XCryptographic_aeadImportKey(XCryptographic_AeadAlgorithm algorithm,
                                  XByteArrayView key, XCryptographic_Key* result);
/** @brief 初始化 AEAD 分段运算。 */
bool XCryptographic_aeadSetup(XCryptographic_AeadOperation* operation,
                              XCryptographic_AeadAlgorithm algorithm,
                              XCryptographic_Key key, bool encrypt);
/** @brief 设置 AEAD nonce。 */
bool XCryptographic_aeadSetNonce(XCryptographic_AeadOperation* operation,
                                 XByteArrayView nonce);
/** @brief 设置附加数据和明文长度（CCM 必需）。 */
bool XCryptographic_aeadSetLengths(XCryptographic_AeadOperation* operation,
                                   size_t associatedDataLength,
                                   size_t dataLength);
/** @brief 设置认证标签长度。 */
bool XCryptographic_aeadSetTagSize(XCryptographic_AeadOperation* operation,
                                   size_t tagSize);
/** @brief 输入一段附加认证数据。 */
bool XCryptographic_aeadUpdateAssociatedData(XCryptographic_AeadOperation* operation,
                                             XByteArrayView associatedData);
/** @brief 加密或解密一段消息。 */
XByteArrayView XCryptographic_aeadUpdateInto(XCryptographic_AeadOperation* operation,
                                             char* buffer, size_t bufferSize,
                                             XByteArrayView data);
/** @brief 完成分段运算并输出标签。 */
bool XCryptographic_aeadFinishInto(XCryptographic_AeadOperation* operation,
                                   char* output, size_t outputSize, size_t* outputLength,
                                   char* tag, size_t tagSize, size_t* tagLength);
/** @brief 清理 AEAD 分段上下文。 */
void XCryptographic_aeadAbort(XCryptographic_AeadOperation* operation);
/** @brief AEAD 加密，输出为密文||标签。 */
XByteArrayView XCryptographic_aeadEncryptInto(
    XCryptographic_Key key, XByteArrayView nonce,
    XByteArrayView associatedData, XByteArrayView plainText,
    size_t tagSize, char* buffer, size_t bufferSize);
/** @brief AEAD 加密（返回新对象）。 */
XByteArray* XCryptographic_aeadEncrypt(
    XCryptographic_Key key, XByteArrayView nonce,
    XByteArrayView associatedData, XByteArrayView plainText,
    size_t tagSize);
/** @brief AEAD 解密并验证标签，输出明文。 */
XByteArrayView XCryptographic_aeadDecryptInto(
    XCryptographic_Key key, XByteArrayView nonce,
    XByteArrayView associatedData, XByteArrayView encryptedData,
    size_t tagSize, char* buffer, size_t bufferSize);
/** @brief AEAD 解密（返回新对象）。 */
XByteArray* XCryptographic_aeadDecrypt(
    XCryptographic_Key key, XByteArrayView nonce,
    XByteArrayView associatedData, XByteArrayView encryptedData,
    size_t tagSize);

// =============== 密钥派生（KDF） ===============

/** @brief 基于指定 HMAC 摘要的 HKDF 一次性派生（RFC 5869）。 */
XByteArrayView XCryptographic_hkdfInto(
    XByteArrayView salt, XByteArrayView inputKeyMaterial,
    XByteArrayView info, XCryptographicHash_Algorithm hashAlgorithm,
    size_t outputSize, char* buffer, size_t bufferSize);
/** @brief 基于指定 HMAC 摘要的 HKDF 提取阶段。 */
XByteArrayView XCryptographic_hkdfExtractInto(
    XByteArrayView salt, XByteArrayView inputKeyMaterial,
    XCryptographicHash_Algorithm hashAlgorithm, char* buffer, size_t bufferSize);
/** @brief 基于指定 HMAC 摘要的 HKDF 展开阶段。 */
XByteArrayView XCryptographic_hkdfExpandInto(
    XByteArrayView prk, XByteArrayView info,
    XCryptographicHash_Algorithm hashAlgorithm, size_t outputSize,
    char* buffer, size_t bufferSize);
/** @brief 基于 SHA-256 的 HKDF 一次性派生（RFC 5869）。 */
XByteArrayView XCryptographic_hkdfSha256Into(
    XByteArrayView salt, XByteArrayView inputKeyMaterial,
    XByteArrayView info, size_t outputSize,
    char* buffer, size_t bufferSize);
XByteArray* XCryptographic_hkdfSha256(
    XByteArrayView salt, XByteArrayView inputKeyMaterial,
    XByteArrayView info, size_t outputSize);
/** @brief 基于 SHA-256 的 HKDF 提取阶段。 */
XByteArrayView XCryptographic_hkdfExtractSha256Into(
    XByteArrayView salt, XByteArrayView inputKeyMaterial,
    char* buffer, size_t bufferSize);
/** @brief 基于 SHA-256 的 HKDF 展开阶段。 */
XByteArrayView XCryptographic_hkdfExpandSha256Into(
    XByteArrayView prk, XByteArrayView info, size_t outputSize,
    char* buffer, size_t bufferSize);
/** @brief 基于指定 HMAC 摘要的 PBKDF2 一次性派生（RFC 8018）。 */
XByteArrayView XCryptographic_pbkdf2HmacInto(
    XByteArrayView password, XByteArrayView salt, uint32_t iterations,
    XCryptographicHash_Algorithm hashAlgorithm, size_t outputSize,
    char* buffer, size_t bufferSize);
/** @brief 基于 HMAC-SHA-256 的 PBKDF2 一次性派生兼容接口。 */
XByteArrayView XCryptographic_pbkdf2HmacSha256Into(
    XByteArrayView password, XByteArrayView salt, uint32_t iterations,
    size_t outputSize, char* buffer, size_t bufferSize);
XByteArray* XCryptographic_pbkdf2HmacSha256(
    XByteArrayView password, XByteArrayView salt, uint32_t iterations,
    size_t outputSize);
/** @brief 基于指定 HMAC 摘要的 TLS 1.2 PRF（RFC 5246）。 */
XByteArrayView XCryptographic_tls12PrfInto(
    XByteArrayView secret, XByteArrayView label, XByteArrayView seed,
    XCryptographicHash_Algorithm hashAlgorithm,
    size_t outputSize, char* buffer, size_t bufferSize);
/** @brief TLS 1.2 PRF（SHA-256，RFC 5246）。 */
XByteArrayView XCryptographic_tls12PrfSha256Into(
    XByteArrayView secret, XByteArrayView label, XByteArrayView seed,
    size_t outputSize, char* buffer, size_t bufferSize);
/** @brief 基于指定 HMAC 摘要的 TLS 1.2 PSK-to-MS（RFC 4279）。 */
XByteArrayView XCryptographic_tls12PskToMsInto(
    XByteArrayView psk, XByteArrayView otherSecret,
    XByteArrayView label, XByteArrayView seed,
    XCryptographicHash_Algorithm hashAlgorithm,
    size_t outputSize, char* buffer, size_t bufferSize);
/** @brief TLS 1.2 PSK-to-MS（SHA-256，RFC 4279）。 */
XByteArrayView XCryptographic_tls12PskToMsSha256Into(
    XByteArrayView psk, XByteArrayView otherSecret,
    XByteArrayView label, XByteArrayView seed,
    size_t outputSize, char* buffer, size_t bufferSize);
/** @brief 基于 AES-CMAC-PRF-128 的 PBKDF2（RFC 4615）。 */
XByteArrayView XCryptographic_pbkdf2AesCmacPrf128Into(
    XByteArrayView password, XByteArrayView salt, uint32_t iterations,
    size_t outputSize, char* buffer, size_t bufferSize);

// =============== AES 密钥封装（RFC 3394/RFC 5649） ===============

/** @brief AES 密钥封装模式。 */
typedef enum XCryptographic_KeyWrapMode {
    XCryptographic_KeyWrapMode_Kw = 0,
    XCryptographic_KeyWrapMode_Kwp = 1
} XCryptographic_KeyWrapMode;

/** @brief RFC 3394/RFC 5649 AES 密钥封装。 */
bool XCryptographic_aesKeyWrapInto(
    XCryptographic_KeyWrapMode mode, XByteArrayView kek,
    XByteArrayView input, char* buffer, size_t bufferSize,
    size_t* outputSize);
/** @brief RFC 3394/RFC 5649 AES 密钥解封。 */
bool XCryptographic_aesKeyUnwrapInto(
    XCryptographic_KeyWrapMode mode, XByteArrayView kek,
    XByteArrayView input, char* buffer, size_t bufferSize,
    size_t* outputSize);

// =============== 有限域 Diffie-Hellman（FFDH） ===============

/** @brief 生成 FFDH 私钥（RFC 7919）。 */
bool XCryptographic_ffdhGeneratePrivateKeyInto(char* buffer, size_t bufferSize);
/** @brief 从 FFDH 私钥导出公钥。 */
XByteArrayView XCryptographic_ffdhExportPublicKeyInto(
    char* buffer, size_t bufferSize,
    const char* privateKey, size_t privateKeyLen);
/** @brief 计算 FFDH 共享密钥。 */
XByteArrayView XCryptographic_ffdhAgreeInto(
    char* buffer, size_t bufferSize,
    const char* privateKey, size_t privateKeyLen,
    const char* peerPublicKey, size_t peerPublicKeyLen);

// =============== ECDH 密钥导入 ===============

/** @brief 从私钥标量导入 ECDH 密钥并计算公钥。 */
bool XCryptographic_ecdhImportPrivateKey(
    XCryptographic_EcdhAlgorithm algorithm, XByteArrayView privateKey,
    XCryptographic_Key* result);
/** @brief 导入 ECDH 对端公钥。 */
bool XCryptographic_ecdhImportPublicKey(
    XCryptographic_EcdhAlgorithm algorithm, XByteArrayView publicKey,
    XCryptographic_Key* result);

// =============== 确定性 ECDSA（RFC 6979） ===============

/** @brief 对摘要进行确定性 P-256 ECDSA 签名，输出 64 字节 r||s。 */
XByteArrayView XCryptographic_ecdsaP256SignHashDeterministicInto(
    char* buffer, size_t bufferSize, XCryptographic_Key key,
    XByteArrayView hash);
/** @brief 对摘要进行确定性 P-256 ECDSA 签名（返回新对象）。 */
XByteArray* XCryptographic_ecdsaP256SignHashDeterministic(
    XCryptographic_Key key, XByteArrayView hash);

// =============== ECJPAKE ===============

/** @brief ECJPAKE 协议角色。 */
typedef enum XCryptographic_EcjPakeRole {
    XCryptographic_EcjPakeRole_Client = 0,
    XCryptographic_EcjPakeRole_Server
} XCryptographic_EcjPakeRole;

/** @brief ECJPAKE 不透明协议上下文，仅支持 secp256r1 与 SHA-256。 */
typedef struct XCryptographic_EcjPakeContext XCryptographic_EcjPakeContext;

/** @brief 使用口令创建 ECJPAKE 协议上下文。 */
XCryptographic_EcjPakeContext* XCryptographic_ecjPakeCreate(
    XCryptographic_EcjPakeRole role, XByteArrayView password);
/** @brief 清理 ECJPAKE 协议上下文及其中的敏感状态。 */
void XCryptographic_ecjPakeDestroy(XCryptographic_EcjPakeContext* context);
/** @brief 生成 ECJPAKE 第一轮 TLS 格式消息。 */
XByteArrayView XCryptographic_ecjPakeWriteRoundOneInto(
    XCryptographic_EcjPakeContext* context, char* buffer, size_t bufferSize);
/** @brief 验证并处理 ECJPAKE 对端第一轮 TLS 格式消息。 */
bool XCryptographic_ecjPakeReadRoundOne(
    XCryptographic_EcjPakeContext* context, XByteArrayView message);
/** @brief 生成 ECJPAKE 第二轮 TLS 格式消息。 */
XByteArrayView XCryptographic_ecjPakeWriteRoundTwoInto(
    XCryptographic_EcjPakeContext* context, char* buffer, size_t bufferSize);
/** @brief 验证并处理 ECJPAKE 对端第二轮 TLS 格式消息。 */
bool XCryptographic_ecjPakeReadRoundTwo(
    XCryptographic_EcjPakeContext* context, XByteArrayView message);
/** @brief 导出 65 字节无压缩 ECJPAKE 共享点。 */
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

/** @brief 从 DER 私钥导入 RSA 密钥。 */
XCryptographic_RsaKey* XCryptographic_rsaImportPrivateKey(XByteArrayView der);
/** @brief 从 DER 公钥导入 RSA 密钥。 */
XCryptographic_RsaKey* XCryptographic_rsaImportPublicKey(XByteArrayView der);
/** @brief 销毁 RSA 密钥。 */
void XCryptographic_rsaDestroyKey(XCryptographic_RsaKey* key);
/** @brief 返回 RSA 模数字节数。 */
size_t XCryptographic_rsaKeyBytes(const XCryptographic_RsaKey* key);
/** @brief 判断 RSA 密钥是否含私钥。 */
bool XCryptographic_rsaIsPrivateKey(const XCryptographic_RsaKey* key);
/** @brief 导出 RSA 公钥 DER。 */
XByteArrayView XCryptographic_rsaExportPublicKeyInto(
    char* buffer, size_t bufferSize, const XCryptographic_RsaKey* key);
/** @brief 导出 RSA 私钥 DER。 */
XByteArrayView XCryptographic_rsaExportPrivateKeyInto(
    char* buffer, size_t bufferSize, const XCryptographic_RsaKey* key);
/** @brief 导出 RSA 私钥 DER（返回新对象）。 */
XByteArray* XCryptographic_rsaExportPrivateKey(const XCryptographic_RsaKey* key);
/** @brief 生成 RSA 密钥对。 */
XCryptographic_RsaKey* XCryptographic_rsaGenerateKey(
    unsigned bits, uint32_t exponent);
/** @brief RSA 公钥加密。 */
XByteArrayView XCryptographic_rsaEncryptInto(
    char* buffer, size_t bufferSize, const XCryptographic_RsaKey* key,
    XCryptographic_RsaPadding padding, XCryptographicHash_Algorithm hash_alg,
    XByteArrayView label, XByteArrayView input);
/** @brief RSA 公钥加密（返回新对象）。 */
XByteArray* XCryptographic_rsaEncrypt(
    const XCryptographic_RsaKey* key, XCryptographic_RsaPadding padding,
    XCryptographicHash_Algorithm hash_alg, XByteArrayView label,
    XByteArrayView input);
/** @brief RSA 私钥解密。 */
XByteArrayView XCryptographic_rsaDecryptInto(
    char* buffer, size_t bufferSize, const XCryptographic_RsaKey* key,
    XCryptographic_RsaPadding padding, XCryptographicHash_Algorithm hash_alg,
    XByteArrayView label, XByteArrayView input, size_t* output_len);
/** @brief RSA 私钥解密（返回新对象）。 */
XByteArray* XCryptographic_rsaDecrypt(
    const XCryptographic_RsaKey* key, XCryptographic_RsaPadding padding,
    XCryptographicHash_Algorithm hash_alg, XByteArrayView label,
    XByteArrayView input);
/** @brief RSA 私钥签名摘要。 */
XByteArrayView XCryptographic_rsaSignHashInto(
    char* buffer, size_t bufferSize, const XCryptographic_RsaKey* key,
    XCryptographic_RsaPadding padding, XCryptographicHash_Algorithm hash_alg,
    XByteArrayView hash);
/** @brief RSA 私钥签名摘要（返回新对象）。 */
XByteArray* XCryptographic_rsaSignHash(
    const XCryptographic_RsaKey* key, XCryptographic_RsaPadding padding,
    XCryptographicHash_Algorithm hash_alg, XByteArrayView hash);
/** @brief RSA 公钥验证签名。 */
bool XCryptographic_rsaVerifyHash(
    const XCryptographic_RsaKey* key, XCryptographic_RsaPadding padding,
    XCryptographicHash_Algorithm hash_alg, XByteArrayView hash,
    XByteArrayView signature);

#ifdef __cplusplus
}
#endif

#endif /* XCRYPTOGRAPHIC_H */
