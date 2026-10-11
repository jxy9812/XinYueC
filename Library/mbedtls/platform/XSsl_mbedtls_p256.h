/**
 * @file       XSsl_mbedtls_p256.h
 * @brief      XSsl mbedtls 后端的 ECDSA P-256/SHA-256 薄适配（验签与造签原语）。
 * @details    第三方三层集成（风格指南「第三方库集成规范」）：上游 mbedtls 的
 *             旧 ecp/ecdsa 数学源在本库已裁出（MBEDTLS_USE_XCRYPTOGRAPHIC_ECC，
 *             Library/mbedtls/CMakeLists.txt），ECC 能力统一经 PSA 客户端 API
 *             路由到 XCryptographic 后端；本适配是该能力的库内门面——上层
 *             （XUpgrade 等业务模块）只使用本头的接口声明，禁止直连 mbedtls
 *             头。实现于 Library/mbedtls/platform/（同 XSsl_mbedtls.c 桥接层，
 *             随 mbedtls 静态库目标编译），内存经 XSsl_mbedtls.c 的
 *             mbedtls_platform_set_calloc_free 桥走 XMemory，随机数经外部熵桥
 *             走 XRandomGenerator。
 *
 *             编码契约（与 XUpgrade §4.2 签名链一致）：
 *             - 公钥 = SEC1 未压缩点 0x04||X||Y，65 字节大端；
 *             - 私钥 = 32 字节大端标量；
 *             - 摘要 = SHA-256 32 字节（调用方先算好，本层不重复哈希）；
 *             - 签名 = DER SEQUENCE{r INTEGER, s INTEGER}（不超过 72 字节，
 *               与 openssl 互操作默认一致；PSA 内部 raw r||s 形式由本层双向
 *               转换）。
 *
 *             XSsl_p256SignHashDer 仅供建包工具/e2e 测试造签使用（服务端
 *             签名工具的库内等价物），产品升级链路只调用
 *             XSsl_p256VerifySha256Der。
 */

#ifndef XSSL_MBEDTLS_P256_H
#define XSSL_MBEDTLS_P256_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief P-256 SEC1 未压缩公钥长度（0x04||X||Y，字节）。 */
#define XSSL_P256_SEC1_UNCOMPRESSED_SIZE 65u
/** @brief P-256 私钥标量长度（大端，字节）。 */
#define XSSL_P256_PRIV_SCALAR_SIZE 32u
/** @brief SHA-256 摘要长度（字节）。 */
#define XSSL_P256_SHA256_SIZE 32u
/** @brief P-256 ECDSA 签名 DER 编码最大长度（字节）。 */
#define XSSL_P256_DER_SIG_MAX 72u

/**
 * @brief 对 32 字节 SHA-256 摘要执行 ECDSA P-256 验签（DER 签名）。
 * @param publicKeySec1 SEC1 未压缩公钥；借用，不能为 NULL，首字节须为 0x04。
 * @param publicKeyLen 公钥字节数；必须为 XSSL_P256_SEC1_UNCOMPRESSED_SIZE。
 * @param sha256Digest 待验签摘要；借用，不能为 NULL。
 * @param sha256Len 摘要字节数；必须为 XSSL_P256_SHA256_SIZE。
 * @param derSignature DER 编码签名；借用，不能为 NULL。
 * @param signatureLen 签名字节数；[1, XSSL_P256_DER_SIG_MAX]。
 * @return 验签通过返回 true；参数非法、DER 非法、公钥点非法、签名不符或
 *         PSA 子系统不可用返回 false（不区分失败原因，调用方一律按校验
 *         不通过处理，不得放行）。
 * @note 内部每次调用前执行幂等的 psa_crypto_init，公钥以易失钥导入、用后
 *       销毁，不落盘、不占用持久键槽。可在多线程并发调用（PSA 客户端 API
 *       线程安全，初始化幂等）。
 */
bool XSsl_p256VerifySha256Der(const uint8_t* publicKeySec1, size_t publicKeyLen,
                              const uint8_t* sha256Digest, size_t sha256Len,
                              const uint8_t* derSignature, size_t signatureLen);

/**
 * @brief 以 P-256 私钥对 32 字节摘要做 ECDSA 签名并输出 DER 编码（造签原语）。
 * @param privateKeyBe 私钥标量；借用，不能为 NULL，大端。
 * @param privateKeyLen 私钥字节数；必须为 XSSL_P256_PRIV_SCALAR_SIZE。
 * @param sha256Digest 待签摘要；借用，不能为 NULL。
 * @param sha256Len 摘要字节数；必须为 XSSL_P256_SHA256_SIZE。
 * @param outDerSignature 输出 DER 签名缓冲区；调用方提供存储空间，不能为
 *                        NULL，容量不得小于 XSSL_P256_DER_SIG_MAX。
 * @param cap outDerSignature 容量（字节）。
 * @param outLen 输出实际 DER 签名字节数；不能为 NULL，失败时不写入。
 * @return 签名成功返回 true；参数非法、私钥非法（不在 [1, n-1]）或 PSA
 *         子系统/随机数不可用返回 false。
 * @warning 仅供建包工具与测试造签使用；私钥材料在返回前于栈上清零。
 */
bool XSsl_p256SignHashDer(const uint8_t* privateKeyBe, size_t privateKeyLen,
                          const uint8_t* sha256Digest, size_t sha256Len,
                          uint8_t* outDerSignature, size_t cap, size_t* outLen);

/**
 * @brief 由 P-256 私钥标量派生 SEC1 未压缩公钥（0x04||X||Y，65 字节）。
 * @param privateKeyBe 私钥标量；借用，不能为 NULL，大端。
 * @param privateKeyLen 私钥字节数；必须为 XSSL_P256_PRIV_SCALAR_SIZE。
 * @param outPublicKeySec1 输出公钥缓冲区；调用方提供存储空间，不能为 NULL，
 *                         容量不得小于 XSSL_P256_SEC1_UNCOMPRESSED_SIZE。
 * @param cap outPublicKeySec1 容量（字节）。
 * @param outLen 输出实际公钥字节数（恒为 XSSL_P256_SEC1_UNCOMPRESSED_SIZE）；
 *               不能为 NULL，失败时不写入。
 * @return 派生成功返回 true；参数非法、私钥非法（不在 [1, n-1]）或 PSA
 *         子系统不可用返回 false。私钥材料在返回前于栈上清零。
 * @note 供建包工具在 seal 自验与 verify 子命令中由私钥推导配对公钥，避免
 *       要求操作者同时保管公钥文件；不经软件点乘实现，经 PSA 导入
 *       （SIGN_HASH|EXPORT 用途）后 psa_export_public_key 导出。
 */
bool XSsl_p256DerivePublicKey(const uint8_t* privateKeyBe, size_t privateKeyLen,
                              uint8_t* outPublicKeySec1, size_t cap, size_t* outLen);

/**
 * @brief 生成一对新的 P-256 密钥（私钥标量 + SEC1 未压缩公钥）。
 * @param outPrivateKeyBe 输出私钥标量缓冲区；调用方提供存储空间，不能为
 *                        NULL，容量不得小于 XSSL_P256_PRIV_SCALAR_SIZE。
 * @param privCap outPrivateKeyBe 容量（字节）。
 * @param outPrivLen 输出实际私钥字节数（恒为 XSSL_P256_PRIV_SCALAR_SIZE）；
 *                   不能为 NULL，失败时不写入。
 * @param outPublicKeySec1 输出公钥缓冲区；调用方提供存储空间，不能为 NULL，
 *                         容量不得小于 XSSL_P256_SEC1_UNCOMPRESSED_SIZE。
 * @param pubCap outPublicKeySec1 容量（字节）。
 * @param outPubLen 输出实际公钥字节数（恒为 65）；不能为 NULL，失败时不写入。
 * @return 生成成功返回 true；参数非法或 PSA 子系统/随机数不可用返回 false。
 * @note 供建包工具 keygen 子命令生成产品钥对（替换内嵌测试钥，XUpgrade.md
 *       §17.7）。随机数经 XSsl_mbedtls.c 的外部熵桥取自 XRandomGenerator；
 *       私钥中间材料在返回前于栈上清零。
 */
bool XSsl_p256GenerateKeyPair(uint8_t* outPrivateKeyBe, size_t privCap, size_t* outPrivLen,
                              uint8_t* outPublicKeySec1, size_t pubCap, size_t* outPubLen);

#ifdef __cplusplus
}
#endif

#endif /* XSSL_MBEDTLS_P256_H */
