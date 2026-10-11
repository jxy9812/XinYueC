/**
 * @file       XSsl_mbedtls_p256.c
 * @brief      XSsl mbedtls 后端的 ECDSA P-256/SHA-256 薄适配实现（PSA 客户端 API）。
 * @details    接口契约见同目录 XSsl_mbedtls_p256.h。上游 mbedtls 的旧
 *             ecp.c/ecdsa.c 数学源在本库构建中已被裁出
 *             （MBEDTLS_USE_XCRYPTOGRAPHIC_ECC，见 Library/mbedtls/CMakeLists.txt），
 *             本适配全部经 PSA 客户端 API（psa_import_key/psa_verify_hash/
 *             psa_sign_hash）路由到 XCryptographic ECC 后端；PSA 核心的内存
 *             与随机数已由 XSsl_mbedtls.c 分别桥接到 XMemory 与
 *             XRandomGenerator，本文件不再触碰分配器。DER 与 PSA raw r||s
 *             的双向转换在文件内以纯字节操作实现，签名编解码严格最小长度
 *             校验，防拼接/越界。
 */

#include "XSsl_mbedtls_p256.h"

#ifdef XSSL_USE_MBEDTLS

#include <string.h>

#include "mbedtls/platform_util.h" /* mbedtls_platform_zeroize：用后清零钥材料 */
#include "psa/crypto.h"

/* ========================================================================== */
/*                     DER 与 PSA raw（r||s）双向转换（内部）                   */
/* ========================================================================== */

/** @brief P-256 模长（字节），即 r/s 各自的 raw 定长宽度。 */
#define XSSL_P256_RAW_HALF 32u

/**
 * @brief 解析 DER ECDSA-Signature 为 PSA raw r||s（各 32 字节右对齐）。
 * @param der DER 字节序列；借用，不能为 NULL。
 * @param derLen der 字节数；[8, XSSL_P256_DER_SIG_MAX]。
 * @param outRaw 输出 64 字节缓冲区；调用方提供存储空间。
 * @return 解析成功返回 true；结构/长度非法返回 false。
 * @note 严格校验：总长恰尽、SEQUENCE/INTEGER 标签、定长字段、整数去除前导
 *       零后有效长度 [1,32]（全零 r/s 交由 PSA 侧范围检查拒绝）。
 */
static bool xssl_p256_derToRaw(const uint8_t* der, size_t derLen, uint8_t* outRaw)
{
    size_t pos = 0;
    size_t seqLen;
    size_t rLen;
    size_t sLen;
    size_t rStart;
    size_t sStart;
    size_t rValueLen;
    size_t sValueLen;

    if (!der || !outRaw || derLen < 8u || derLen > (size_t)XSSL_P256_DER_SIG_MAX) {
        return false;
    }
    if (der[pos] != 0x30u) {
        return false;                       /* 非 SEQUENCE */
    }
    ++pos;
    seqLen = (size_t)der[pos];
    ++pos;
    if (seqLen != derLen - 2u) {
        return false;                       /* SEQUENCE 长度必须恰尽 */
    }
    if (der[pos] != 0x02u) {
        return false;                       /* r 非 INTEGER */
    }
    ++pos;
    rLen = (size_t)der[pos];
    ++pos;
    if (rLen == 0u || rLen > XSSL_P256_RAW_HALF + 1u || pos + rLen + 2u > derLen) {
        return false;                       /* r 长度越界或挤压 s 的位置 */
    }
    if (der[pos] == 0x00u && rLen > 1u && der[pos + 1u] < 0x80u) {
        return false;                       /* 非最小编码的多余前导零 */
    }
    rStart = pos;
    pos += rLen;
    if (der[pos] != 0x02u) {
        return false;                       /* s 非 INTEGER */
    }
    ++pos;
    sLen = (size_t)der[pos];
    ++pos;
    if (sLen == 0u || sLen > XSSL_P256_RAW_HALF + 1u || pos + sLen != derLen) {
        return false;                       /* s 长度越界或未恰尽 */
    }
    if (der[pos] == 0x00u && sLen > 1u && der[pos + 1u] < 0x80u) {
        return false;
    }
    sStart = pos;
    /* 去前导零后有效长度须落回 [1,32]，右对齐拷入 raw 半段 */
    rValueLen = rLen;
    while (rValueLen > 0u && der[rStart + (rLen - rValueLen)] == 0x00u) {
        --rValueLen;
    }
    sValueLen = sLen;
    while (sValueLen > 0u && der[sStart + (sLen - sValueLen)] == 0x00u) {
        --sValueLen;
    }
    if (rValueLen == 0u || rValueLen > XSSL_P256_RAW_HALF ||
        sValueLen == 0u || sValueLen > XSSL_P256_RAW_HALF) {
        return false;
    }
    memset(outRaw, 0, 2u * XSSL_P256_RAW_HALF);
    memcpy(outRaw + (XSSL_P256_RAW_HALF - rValueLen),
           der + rStart + (rLen - rValueLen), rValueLen);
    memcpy(outRaw + 2u * XSSL_P256_RAW_HALF - sValueLen,
           der + sStart + (sLen - sValueLen), sValueLen);
    return true;
}

/**
 * @brief 将 PSA raw r||s（64 字节）编码为 DER ECDSA-Signature。
 * @param raw 64 字节 raw 签名；借用，不能为 NULL。
 * @param outDer 输出缓冲区；调用方提供存储空间，容量不得小于 72。
 * @param cap 输出缓冲区容量。
 * @param outLen 输出 DER 字节数；不能为 NULL。
 * @return 编码成功返回 true；容量不足返回 false（outLen 不写入）。
 * @note 按最小 DER 规则按需补符号前导零（r/s 最高位为 1 时补 0x00）。
 */
static bool xssl_p256_rawToDer(const uint8_t* raw, uint8_t* outDer, size_t cap, size_t* outLen)
{
    size_t rLen = XSSL_P256_RAW_HALF;
    size_t sLen = XSSL_P256_RAW_HALF;
    size_t pos = 0;
    while (rLen > 1u && raw[rLen - XSSL_P256_RAW_HALF] == 0x00u) {
        --rLen;                             /* 去前导零（至少保留 1 字节） */
    }
    while (sLen > 1u && raw[XSSL_P256_RAW_HALF + XSSL_P256_RAW_HALF - sLen] == 0x00u) {
        --sLen;
    }
    {
        size_t rPad = (raw[XSSL_P256_RAW_HALF - rLen] & 0x80u) != 0u ? 1u : 0u;
        size_t sPad = (raw[2u * XSSL_P256_RAW_HALF - sLen] & 0x80u) != 0u ? 1u : 0u;
        size_t total = 2u + 2u + rLen + rPad + 2u + sLen + sPad;
        if (!raw || !outDer || !outLen || cap < total) {
            return false;
        }
        outDer[pos] = 0x30u;                /* SEQUENCE */
        ++pos;
        outDer[pos] = (uint8_t)(total - 2u);
        ++pos;
        outDer[pos] = 0x02u;                /* r INTEGER */
        ++pos;
        outDer[pos] = (uint8_t)(rLen + rPad);
        ++pos;
        if (rPad != 0u) {
            outDer[pos] = 0x00u;
            ++pos;
        }
        memcpy(outDer + pos, raw + (XSSL_P256_RAW_HALF - rLen), rLen);
        pos += rLen;
        outDer[pos] = 0x02u;                /* s INTEGER */
        ++pos;
        outDer[pos] = (uint8_t)(sLen + sPad);
        ++pos;
        if (sPad != 0u) {
            outDer[pos] = 0x00u;
            ++pos;
        }
        memcpy(outDer + pos, raw + (2u * XSSL_P256_RAW_HALF - sLen), sLen);
        pos += sLen;
        *outLen = pos;
    }
    return true;
}

/* ========================================================================== */
/*                                  公共实现                                   */
/* ========================================================================== */

bool XSsl_p256VerifySha256Der(const uint8_t* publicKeySec1, size_t publicKeyLen,
                              const uint8_t* sha256Digest, size_t sha256Len,
                              const uint8_t* derSignature, size_t signatureLen)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key = 0;
    uint8_t raw[2u * XSSL_P256_RAW_HALF];
    psa_status_t status;

    if (!publicKeySec1 || publicKeyLen != (size_t)XSSL_P256_SEC1_UNCOMPRESSED_SIZE ||
        publicKeySec1[0] != 0x04u ||
        !sha256Digest || sha256Len != (size_t)XSSL_P256_SHA256_SIZE ||
        !derSignature || signatureLen == 0u || signatureLen > (size_t)XSSL_P256_DER_SIG_MAX) {
        return false;
    }
    if (!xssl_p256_derToRaw(derSignature, signatureLen, raw)) {
        return false;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        mbedtls_platform_zeroize(raw, sizeof(raw));
        return false;
    }
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, publicKeySec1, publicKeyLen, &key);
    if (status != PSA_SUCCESS) {
        mbedtls_platform_zeroize(raw, sizeof(raw));
        return false;                       /* 公钥点非法等，一律按校验不通过 */
    }
    status = psa_verify_hash(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                             sha256Digest, sha256Len, raw, sizeof(raw));
    (void)psa_destroy_key(key);
    mbedtls_platform_zeroize(raw, sizeof(raw));
    return status == PSA_SUCCESS;
}

bool XSsl_p256SignHashDer(const uint8_t* privateKeyBe, size_t privateKeyLen,
                          const uint8_t* sha256Digest, size_t sha256Len,
                          uint8_t* outDerSignature, size_t cap, size_t* outLen)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key = 0;
    uint8_t raw[2u * XSSL_P256_RAW_HALF];
    size_t rawLen = 0;
    psa_status_t status;
    bool ok = false;

    if (!privateKeyBe || privateKeyLen != (size_t)XSSL_P256_PRIV_SCALAR_SIZE ||
        !sha256Digest || sha256Len != (size_t)XSSL_P256_SHA256_SIZE ||
        !outDerSignature || cap < (size_t)XSSL_P256_DER_SIG_MAX || !outLen) {
        return false;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, privateKeyBe, privateKeyLen, &key);
    if (status != PSA_SUCCESS) {
        return false;                       /* 私钥标量不在 [1, n-1] 等 */
    }
    status = psa_sign_hash(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                           sha256Digest, sha256Len, raw, sizeof(raw), &rawLen);
    (void)psa_destroy_key(key);
    if (status == PSA_SUCCESS && rawLen == sizeof(raw)) {
        ok = xssl_p256_rawToDer(raw, outDerSignature, cap, outLen);
    }
    mbedtls_platform_zeroize(raw, sizeof(raw));
    return ok;
}

bool XSsl_p256DerivePublicKey(const uint8_t* privateKeyBe, size_t privateKeyLen,
                              uint8_t* outPublicKeySec1, size_t cap, size_t* outLen)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key = 0;
    psa_status_t status;

    if (!privateKeyBe || privateKeyLen != (size_t)XSSL_P256_PRIV_SCALAR_SIZE ||
        !outPublicKeySec1 || cap < (size_t)XSSL_P256_SEC1_UNCOMPRESSED_SIZE || !outLen) {
        return false;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    /* EXPORT 用途仅为 psa_export_public_key 所需；公钥本身非敏感材料。 */
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, privateKeyBe, privateKeyLen, &key);
    if (status != PSA_SUCCESS) {
        return false;                       /* 私钥标量不在 [1, n-1] 等 */
    }
    /* PSA 的 ECC 公钥导出格式即 SEC1 未压缩点 0x04||X||Y。 */
    status = psa_export_public_key(key, outPublicKeySec1, cap, outLen);
    (void)psa_destroy_key(key);
    return status == PSA_SUCCESS;
}

bool XSsl_p256GenerateKeyPair(uint8_t* outPrivateKeyBe, size_t privCap, size_t* outPrivLen,
                              uint8_t* outPublicKeySec1, size_t pubCap, size_t* outPubLen)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key = 0;
    uint8_t pubBuf[XSSL_P256_SEC1_UNCOMPRESSED_SIZE];
    size_t privOut = 0;
    size_t pubLen = 0;
    psa_status_t status;

    if (!outPrivateKeyBe || privCap < (size_t)XSSL_P256_PRIV_SCALAR_SIZE || !outPrivLen ||
        !outPublicKeySec1 || pubCap < (size_t)XSSL_P256_SEC1_UNCOMPRESSED_SIZE || !outPubLen) {
        return false;
    }
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    status = psa_generate_key(&attributes, &key);
    if (status != PSA_SUCCESS) {
        return false;
    }
    /* ECC keypair 的 psa_export_key 输出即 32 字节大端标量。 */
    status = psa_export_key(key, outPrivateKeyBe, privCap, &privOut);
    if (status == PSA_SUCCESS) {
        status = psa_export_public_key(key, pubBuf, sizeof(pubBuf), &pubLen);
    }
    (void)psa_destroy_key(key);
    if (status == PSA_SUCCESS &&
        privOut == (size_t)XSSL_P256_PRIV_SCALAR_SIZE &&
        pubLen == (size_t)XSSL_P256_SEC1_UNCOMPRESSED_SIZE) {
        memcpy(outPublicKeySec1, pubBuf, pubLen);
        *outPrivLen = privOut;
        *outPubLen = pubLen;
        return true;
    }
    /* 任何一步失败：调用方私钥缓冲整体清零，不留下半截钥材料。 */
    mbedtls_platform_zeroize(outPrivateKeyBe, privCap);
    *outPrivLen = 0;
    return false;
}

#endif /* XSSL_USE_MBEDTLS */
