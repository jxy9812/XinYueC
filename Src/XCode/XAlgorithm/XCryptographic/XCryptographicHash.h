/**
 * @file XCryptographicHash.h
 * @brief XCryptographic 哈希、HMAC 与高速哈希公共 API。
 */

#ifndef XCRYPTOGRAPHICHASH_H
#define XCRYPTOGRAPHICHASH_H

#include "XByteArrayView.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XByteArray XByteArray;
typedef struct XIODevice XIODevice;

/**
 * @brief 哈希算法类型。
 * @details 0~23 保持 Qt 6.8 QCryptographicHash 的兼容顺序；24~49 为
 *          原 XHashFunc 的高速哈希算法。高速哈希适合容器和缓存，
 *          不提供密码学安全性。
 */
typedef enum XCryptographicHash_Algorithm {
    XCryptographicHash_Md4 = 0,                   /**< MD4。 */
    XCryptographicHash_Md5 = 1,                   /**< MD5。 */
    XCryptographicHash_Sha1 = 2,                  /**< SHA-1。 */
    XCryptographicHash_Sha224 = 3,                /**< SHA-224。 */
    XCryptographicHash_Sha256 = 4,                /**< SHA-256。 */
    XCryptographicHash_Sha384 = 5,                /**< SHA-384。 */
    XCryptographicHash_Sha512 = 6,                /**< SHA-512。 */
    XCryptographicHash_Keccak_224 = 7,            /**< Keccak-224。 */
    XCryptographicHash_Keccak_256 = 8,            /**< Keccak-256。 */
    XCryptographicHash_Keccak_384 = 9,            /**< Keccak-384。 */
    XCryptographicHash_Keccak_512 = 10,           /**< Keccak-512。 */
    XCryptographicHash_RealSha3_224 = 11,         /**< 标准 SHA3-224。 */
    XCryptographicHash_RealSha3_256 = 12,         /**< 标准 SHA3-256。 */
    XCryptographicHash_RealSha3_384 = 13,         /**< 标准 SHA3-384。 */
    XCryptographicHash_RealSha3_512 = 14,         /**< 标准 SHA3-512。 */
    XCryptographicHash_Sha3_224 = XCryptographicHash_RealSha3_224, /**< SHA3-224 别名。 */
    XCryptographicHash_Sha3_256 = XCryptographicHash_RealSha3_256, /**< SHA3-256 别名。 */
    XCryptographicHash_Sha3_384 = XCryptographicHash_RealSha3_384, /**< SHA3-384 别名。 */
    XCryptographicHash_Sha3_512 = XCryptographicHash_RealSha3_512, /**< SHA3-512 别名。 */
    XCryptographicHash_Blake2b_160 = 15,          /**< BLAKE2b-160。 */
    XCryptographicHash_Blake2b_256 = 16,          /**< BLAKE2b-256。 */
    XCryptographicHash_Blake2b_384 = 17,          /**< BLAKE2b-384。 */
    XCryptographicHash_Blake2b_512 = 18,          /**< BLAKE2b-512。 */
    XCryptographicHash_Blake2s_128 = 19,          /**< BLAKE2s-128。 */
    XCryptographicHash_Blake2s_160 = 20,          /**< BLAKE2s-160。 */
    XCryptographicHash_Blake2s_224 = 21,          /**< BLAKE2s-224。 */
    XCryptographicHash_Blake2s_256 = 22,          /**< BLAKE2s-256。 */
    XCryptographicHash_Ripemd160 = 23,            /**< RIPEMD-160。 */
    XCryptographicHash_Murmur3_32 = 24,           /**< MurmurHash3-32，非密码学哈希。 */
    XCryptographicHash_Fnv1a_64 = 25,             /**< FNV-1a-64，非密码学哈希。 */
    XCryptographicHash_Djb2 = 26,                 /**< DJB2，非密码学哈希。 */
    XCryptographicHash_Pearson = 27,              /**< Pearson，非密码学哈希。 */
    XCryptographicHash_Lookup3 = 28,              /**< lookup3，非密码学哈希。 */
    XCryptographicHash_CityHash64 = 29,           /**< CityHash64，非密码学哈希。 */
    XCryptographicHash_FarmHash64 = 30,           /**< FarmHash64，非密码学哈希。 */
    XCryptographicHash_HighwayHash64 = 31,        /**< HighwayHash64，非密码学哈希。 */
    XCryptographicHash_XxHash64 = 32,             /**< xxHash64，非密码学哈希。 */
    XCryptographicHash_WyHash = 33,               /**< wyhash，非密码学哈希。 */
    XCryptographicHash_T1ha2 = 34,                /**< t1ha2，非密码学哈希。 */
    XCryptographicHash_SpookyHash64 = 35,         /**< SpookyHash64，非密码学哈希。 */
    XCryptographicHash_SipHash24 = 36,            /**< SipHash-2-4，需要 128 位密钥。 */
    XCryptographicHash_MetroHash64 = 37,          /**< MetroHash64，非密码学哈希。 */
    XCryptographicHash_MumHash = 38,              /**< MumHash，非密码学哈希。 */
    XCryptographicHash_FastHash64 = 39,           /**< FastHash64，非密码学哈希。 */
    XCryptographicHash_ThomasWang64 = 40,         /**< Thomas Wang 64 位哈希。 */
    XCryptographicHash_OneAtATime = 41,           /**< Jenkins one-at-a-time 哈希。 */
    XCryptographicHash_SuperFastHash = 42,        /**< SuperFastHash，非密码学哈希。 */
    XCryptographicHash_ElfHash = 43,              /**< ELF 哈希，非密码学哈希。 */
    XCryptographicHash_ApHash = 44,               /**< AP 哈希，非密码学哈希。 */
    XCryptographicHash_JsHash = 45,               /**< JS 哈希，非密码学哈希。 */
    XCryptographicHash_RsHash = 46,               /**< RS 哈希，非密码学哈希。 */
    XCryptographicHash_PjwHash = 47,              /**< PJW 哈希，非密码学哈希。 */
    XCryptographicHash_BkdrHash = 48,             /**< BKDR 哈希，非密码学哈希。 */
    XCryptographicHash_SdbmHash = 49,             /**< SDBM 哈希，非密码学哈希。 */
    XCryptographicHash_NumAlgorithms = 50         /**< 算法数量哨兵，不是有效算法。 */
} XCryptographicHash_Algorithm;

/**
 * @brief 轻量流式哈希上下文。
 * @details 上下文拥有内部输入缓存和 resultView 缓存；这些缓存以及 HMAC
 *          内部哈希上下文均由 XCRYPTOGRAPHIC_HASH_MEMORY_POOL_TYPE 指定
 *          的内存池分配。
 *          该结构为值类型，但只能通过本头文件提供的生命周期函数操作。
 */
typedef struct XCryptographicHash {
    uint64_t algorithm : 6;
    uint64_t finalized : 1;
    uint64_t initialized : 1;
    uint64_t totalLen : 56;
    void* buffer;
    size_t bufferSize;
    size_t bufferCapacity;
    void* resultCache;
    size_t resultCacheSize;
    size_t resultCacheCapacity;
} XCryptographicHash;

/**
 * @brief 创建并初始化流式哈希上下文。
 * @param method 要使用的哈希算法；SipHash-2-4 需要密钥，不能用于此接口。
 * @return 成功返回新上下文，算法不支持或内存不足返回 NULL；调用者负责
 *         使用 XCryptographicHash_delete 释放返回对象。
 */
XCryptographicHash* XCryptographicHash_create(XCryptographicHash_Algorithm method);
/**
 * @brief 初始化调用者提供的哈希上下文存储。
 * @param hash 待初始化的上下文，不能为 NULL。
 * @param method 要使用的无密钥哈希算法。
 * @note 若 hash 已初始化，其旧缓存会被重置且不会泄漏；栈对象最后应调用
 *       XCryptographicHash_deinit。
 */
void XCryptographicHash_init(XCryptographicHash* hash,
                             XCryptographicHash_Algorithm method);
/**
 * @brief 深拷贝哈希上下文及其输入/结果缓存。
 * @param dest 目标上下文；必须已由 init 初始化或为可安全清理的对象。
 * @param src 源上下文；不能为 NULL，且必须已初始化。
 */
void XCryptographicHash_copy(XCryptographicHash* dest,
                             const XCryptographicHash* src);
/**
 * @brief 将源上下文的所有权移动到目标上下文。
 * @param dest 目标上下文；其原有缓存会被释放。
 * @param src 源上下文；成功后被清零并不可继续使用，除非重新 init。
 */
void XCryptographicHash_move(XCryptographicHash* dest, XCryptographicHash* src);
/**
 * @brief 释放上下文缓存并将上下文置为未初始化状态。
 * @param hash 要清理的上下文；传 NULL 无操作。
 */
void XCryptographicHash_deinit(XCryptographicHash* hash);
/**
 * @brief 销毁由 create 创建的上下文。
 * @param hash 要销毁的上下文；传 NULL 无操作。
 */
void XCryptographicHash_delete(XCryptographicHash* hash);

/**
 * @brief 向流式哈希追加一段原始字节。
 * @param hash 已初始化且未完成的上下文。
 * @param data 输入数据；len 为 0 时可为 NULL，否则不能为 NULL。
 * @param len 输入字节数。
 */
void XCryptographicHash_addData(XCryptographicHash* hash,
                                const char* data, size_t len);
/**
 * @brief 以只读字节视图向上下文追加数据。
 * @param hash 已初始化且未完成的上下文。
 * @param bytes 输入字节视图；空视图不追加数据。
 */
void XCryptographicHash_addData_2(XCryptographicHash* hash, XByteArrayView bytes);
/**
 * @brief 以 XByteArray 内容向上下文追加数据。
 * @param hash 已初始化且未完成的上下文。
 * @param data 输入数组；函数只读取其内容，不取得所有权。
 */
void XCryptographicHash_addData_3(XCryptographicHash* hash,
                                  const XByteArray* data);
/**
 * @brief 读取设备剩余内容并追加到上下文。
 * @param hash 已初始化且未完成的上下文。
 * @param device 可读设备；函数不取得设备所有权。
 * @return 成功读取并追加返回 true，否则返回 false。
 */
bool XCryptographicHash_addData_4(XCryptographicHash* hash, XIODevice* device);
/**
 * @brief 清空已追加数据和结果缓存，保留算法设置以便重新计算。
 * @param hash 要重置的上下文。
 */
void XCryptographicHash_reset(XCryptographicHash* hash);
/**
 * @brief 计算当前输入并返回新建的结果数组。
 * @param hash 已初始化的上下文；计算后上下文标记为 finalized。
 * @return 新建的摘要数组；调用者负责使用 XByteArray_delete_base 释放，
 *         参数无效、算法不支持或分配失败返回 NULL。
 */
XByteArray* XCryptographicHash_result(XCryptographicHash* hash);
/**
 * @brief 计算当前输入并返回内部结果缓存的只读视图。
 * @param hash 已初始化的上下文；计算后上下文标记为 finalized。
 * @return 指向上下文内部缓存的摘要视图；下一次 resultView、reset 或 deinit
 *         后视图可能失效，失败返回空视图。
 */
XByteArrayView XCryptographicHash_resultView(XCryptographicHash* hash);
/**
 * @brief 查询上下文使用的算法。
 * @param hash 已初始化的上下文。
 * @return 算法枚举；hash 无效时返回 XCryptographicHash_Md5。
 */
XCryptographicHash_Algorithm XCryptographicHash_algorithm(
    const XCryptographicHash* hash);

/**
 * @brief 一次性计算原始字节摘要。
 * @param data 输入数据；len 为 0 时仍需提供非 NULL 指针以区分无效参数。
 * @param len 输入字节数。
 * @param method 哈希算法；SipHash-2-4 不能用于无密钥摘要接口。
 * @return 新建摘要数组，调用者负责释放；失败返回 NULL。
 */
XByteArray* XCryptographicHash_hash(const char* data, size_t len,
                                    XCryptographicHash_Algorithm method);
/**
 * @brief 一次性计算字节视图摘要。
 * @param data 输入字节视图。
 * @param method 哈希算法。
 * @return 新建摘要数组，调用者负责释放；失败返回 NULL。
 */
XByteArray* XCryptographicHash_hash_2(XByteArrayView data,
                                      XCryptographicHash_Algorithm method);
/**
 * @brief 一次性计算 XByteArray 摘要。
 * @param data 输入数组；函数只读取其内容。
 * @param method 哈希算法。
 * @return 新建摘要数组，调用者负责释放；失败返回 NULL。
 */
XByteArray* XCryptographicHash_hash_3(const XByteArray* data,
                                      XCryptographicHash_Algorithm method);
/**
 * @brief 将一次性摘要写入调用者提供的缓冲区。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出缓冲区容量，至少为 hashLength(method)。
 * @param data 输入数据；dataLen 为 0 时仍需提供非 NULL 指针。
 * @param dataLen 输入字节数。
 * @param method 哈希算法。
 * @return 指向 buffer 的只读视图；缓冲区不足或参数无效返回空视图。
 */
XByteArrayView XCryptographicHash_hashInto(
    char* buffer, size_t bufferSize, const char* data, size_t dataLen,
    XCryptographicHash_Algorithm method);
/**
 * @brief 将多段输入拼接后写入调用者提供的缓冲区。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出缓冲区容量，至少为 hashLength(method)。
 * @param dataArray 输入视图数组；数组元素按顺序参与计算。
 * @param count 输入视图数量，可为 0。
 * @param method 哈希算法。
 * @return 指向 buffer 的只读视图；缓冲区不足或参数无效返回空视图。
 */
XByteArrayView XCryptographicHash_hashInto_1(
    char* buffer, size_t bufferSize,
    const XByteArrayView* dataArray, size_t count,
    XCryptographicHash_Algorithm method);

/**
 * @brief 查询算法摘要长度。
 * @param method 哈希算法。
 * @return 摘要长度（字节）；枚举值无效返回 0。
 */
int XCryptographicHash_hashLength(XCryptographicHash_Algorithm method);
/**
 * @brief 判断算法能否用于无密钥流式/一次性摘要。
 * @param method 待查询算法。
 * @return 支持返回 true；无效算法或需要密钥的 SipHash-2-4 返回 false。
 */
bool XCryptographicHash_supportsAlgorithm(XCryptographicHash_Algorithm method);
/**
 * @brief 获取算法的稳定文本名称。
 * @param method 待查询算法。
 * @return 静态只读名称字符串；枚举值无效返回 "Unknown"。
 */
const char* XCryptographicHash_algorithmName(XCryptographicHash_Algorithm method);

/**
 * @brief 无状态哈希回调类型，适用于 XHashMap/XHashSet。
 * @param key 待哈希数据；len 为 0 时可为 NULL。
 * @param len 输入字节数。
 * @return 64 位哈希值；32 位算法的结果按无符号整数提升。
 */
typedef uint64_t (*XCryptographicHashFunction)(const void* key, size_t len);
/**
 * @brief 获取指定快速哈希算法的无状态回调。
 * @param method 快速哈希算法。
 * @return 可直接调用的回调；不适用的算法（包括 SipHash-2-4）返回 NULL。
 */
XCryptographicHashFunction XCryptographicHash_function(
    XCryptographicHash_Algorithm method);

/**
 * @brief 直接返回高速哈希的整数值，适用于缓存键和容器索引。
 * @param data 待哈希数据；len 为 0 时可为 NULL。
 * @param len 输入字节数。
 * @param method 快速哈希算法。
 * @return 无状态哈希值；算法无效、不是快速哈希或需要密钥时返回 0。
 */
uint64_t XCryptographicHash_value(const void* data, size_t len,
                                  XCryptographicHash_Algorithm method);

/**
 * @brief 计算带 128 位密钥的 SipHash-2-4。
 * @param data 待哈希数据；len 为 0 时可为 NULL。
 * @param len 输入字节数。
 * @param key0 SipHash 密钥低 64 位。
 * @param key1 SipHash 密钥高 64 位。
 * @return 64 位 SipHash-2-4 结果；参数无效时返回 0。
 */
uint64_t XCryptographicHash_siphash24(const uint8_t* data, size_t len,
                                      uint64_t key0, uint64_t key1);

/**
 * @brief 对 Keccak-f[1600] 状态执行一次置换。
 * @param state 25 个小端 64 位 lane 组成的状态，函数原地修改。
 */
void XCryptographicHash_keccakPermute(uint64_t state[25]);

/**
 * @brief 一次性计算 HMAC 并返回新建结果数组。
 * @param key HMAC 密钥字节。
 * @param keyLen 密钥长度。
 * @param message 待认证消息；msgLen 为 0 时可为 NULL。
 * @param msgLen 消息长度。
 * @param method 底层哈希算法；必须支持 HMAC 且不能是快速哈希。
 * @return 新建 HMAC 摘要，调用者负责释放；失败返回 NULL。
 */
XByteArray* XCryptographicHash_hmac(
    const char* key, size_t keyLen, const char* message, size_t msgLen,
    XCryptographicHash_Algorithm method);
/**
 * @brief 使用两个字节视图一次性计算 HMAC。
 * @param key HMAC 密钥视图。
 * @param message 待认证消息视图。
 * @param method 底层哈希算法。
 * @return 新建 HMAC 摘要，调用者负责释放；失败返回 NULL。
 */
XByteArray* XCryptographicHash_hmac_2(
    XByteArrayView key, XByteArrayView message,
    XCryptographicHash_Algorithm method);
/**
 * @brief 使用两个 XByteArray 一次性计算 HMAC。
 * @param key HMAC 密钥数组，只读。
 * @param message 待认证消息数组，只读。
 * @param method 底层哈希算法。
 * @return 新建 HMAC 摘要，调用者负责释放；失败返回 NULL。
 */
XByteArray* XCryptographicHash_hmac_3(
    const XByteArray* key, const XByteArray* message,
    XCryptographicHash_Algorithm method);
/**
 * @brief 将一次性 HMAC 写入调用者提供的缓冲区。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出缓冲区容量，至少为 hashLength(method)。
 * @param key HMAC 密钥字节。
 * @param keyLen 密钥长度。
 * @param message 待认证消息；msgLen 为 0 时可为 NULL。
 * @param msgLen 消息长度。
 * @param method 底层哈希算法。
 * @return 指向 buffer 的结果视图；失败返回空视图。
 */
XByteArrayView XCryptographicHash_hmacInto(
    char* buffer, size_t bufferSize,
    const char* key, size_t keyLen,
    const char* message, size_t msgLen,
    XCryptographicHash_Algorithm method);

/**
 * @brief HMAC 流式运算上下文。
 * @note 值类型；使用 hmacSetup 初始化，完成后使用 hmacAbort 清理。
 */
typedef struct XCryptographic_HmacOperation {
    XCryptographicHash_Algorithm algorithm;
    XCryptographicHash* inner;
    uint8_t opad[144];
    size_t blockSize;
    bool active;
} XCryptographic_HmacOperation;

/**
 * @brief 初始化 HMAC 流式上下文并处理密钥。
 * @param operation 输出上下文。
 * @param key HMAC 密钥视图。
 * @param algorithm 底层哈希算法。
 * @return 初始化成功返回 true，参数无效、算法不支持或内存不足返回 false。
 */
bool XCryptographic_hmacSetup(XCryptographic_HmacOperation* operation,
                              XByteArrayView key,
                              XCryptographicHash_Algorithm algorithm);
/**
 * @brief 向 HMAC 上下文追加消息。
 * @param operation 已初始化且未完成的 HMAC 上下文。
 * @param message 待认证消息视图，可为空视图。
 * @return 成功返回 true；上下文状态或参数无效返回 false。
 */
bool XCryptographic_hmacUpdate(XCryptographic_HmacOperation* operation,
                               XByteArrayView message);
/**
 * @brief 完成 HMAC 并将标签写入调用者缓冲区。
 * @param operation 已初始化的 HMAC 上下文；成功或失败后都会结束该上下文。
 * @param buffer 输出标签缓冲区。
 * @param bufferSize 输出容量，至少为底层摘要长度。
 * @return 指向 buffer 的标签视图；失败返回空视图。
 */
XByteArrayView XCryptographic_hmacFinishInto(
    XCryptographic_HmacOperation* operation, char* buffer, size_t bufferSize);
/**
 * @brief 中止 HMAC 运算并清零/释放敏感状态。
 * @param operation 要清理的上下文；传 NULL 无操作。
 */
void XCryptographic_hmacAbort(XCryptographic_HmacOperation* operation);

#ifdef __cplusplus
}
#endif

#endif /* XCRYPTOGRAPHICHASH_H */
