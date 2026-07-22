/**
 * @file XCryptographicHash.h
 * @brief 加密哈希算法类（对齐Qt 6.8 QCryptographicHash）
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

#ifndef XCRYPTOGRAPHICHASH_H
#define XCRYPTOGRAPHICHASH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XClass.h"
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
    XCryptographicHash_NumAlgorithms = 23   ///< 算法数量
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

#ifdef __cplusplus
}
#endif

#endif // XCRYPTOGRAPHICHASH_H
