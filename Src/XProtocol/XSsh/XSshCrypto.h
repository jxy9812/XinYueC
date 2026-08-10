/** @file XSshCrypto.h
 * @brief XSsh 客户端与服务端共享的算法能力表。
 * @details
 * 本文件是 XSsh 的内部公共单元。客户端和服务端均通过
 * XCryptographic_config.h 的同一组裁剪宏生成算法名称列表并完成协商，
 * 避免两端出现不同的默认套件或密钥长度。
 */

#ifndef XSSHCRYPTO_H
#define XSSHCRYPTO_H

#include "XSsh_config.h"
#include "XCryptographic_config.h"
#include "XCryptographic.h"

#if XPROTOCOL_ON && XSSH_ON

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef enum XSshKexAlgorithm {
    XSshKexAlgorithm_None = 0,
    XSshKexAlgorithm_Curve25519Sha256,
    XSshKexAlgorithm_EcdhSha2Nistp256
} XSshKexAlgorithm;

typedef enum XSshCipherAlgorithm {
    XSshCipherAlgorithm_None = 0,
    XSshCipherAlgorithm_Aes256Ctr,
    XSshCipherAlgorithm_Aes192Ctr,
    XSshCipherAlgorithm_Aes128Ctr
} XSshCipherAlgorithm;

typedef struct XSshNamedAlgorithm {
    const char* name;
    int value;
} XSshNamedAlgorithm;

static const XSshNamedAlgorithm xssh_kex_algorithms[] = {
#if XCRYPTOGRAPHIC_X25519_ON && XCRYPTOGRAPHIC_SHA256_ON
    { "curve25519-sha256", XSshKexAlgorithm_Curve25519Sha256 },
#endif
#if XCRYPTOGRAPHIC_ECDH_NISTP256_ON && XCRYPTOGRAPHIC_SHA256_ON
    { "ecdh-sha2-nistp256", XSshKexAlgorithm_EcdhSha2Nistp256 },
#endif
    { NULL, XSshKexAlgorithm_None }
};

static const XSshNamedAlgorithm xssh_hostkey_algorithms[] = {
#if XCRYPTOGRAPHIC_ECDSA_NISTP256_ON && XCRYPTOGRAPHIC_SHA256_ON
    { "ecdsa-sha2-nistp256", 1 },
#endif
    { NULL, 0 }
};

static const XSshNamedAlgorithm xssh_cipher_algorithms[] = {
#if XCRYPTOGRAPHIC_AES256_CTR_ON
    { "aes256-ctr", XSshCipherAlgorithm_Aes256Ctr },
#endif
#if XCRYPTOGRAPHIC_AES192_CTR_ON
    { "aes192-ctr", XSshCipherAlgorithm_Aes192Ctr },
#endif
#if XCRYPTOGRAPHIC_AES128_CTR_ON
    { "aes128-ctr", XSshCipherAlgorithm_Aes128Ctr },
#endif
    { NULL, XSshCipherAlgorithm_None }
};

static const XSshNamedAlgorithm xssh_mac_algorithms[] = {
#if XCRYPTOGRAPHIC_HMAC_SHA256_ON
    { "hmac-sha2-256", 1 },
#endif
    { NULL, 0 }
};

/** @brief 按客户端名称列表顺序选择首个双方支持的算法。 */
static bool xssh_select_client_algorithm(const uint8_t* list, size_t len,
                                         const XSshNamedAlgorithm* supported,
                                         int* value)
{
    size_t start = 0;
    if (!list || !supported || !value) return false;
    while (start < len) {
        size_t end = start;
        size_t i;
        while (end < len && list[end] != ',') ++end;
        for (i = 0; supported[i].name; ++i) {
            size_t nameLen = strlen(supported[i].name);
            if (end - start == nameLen &&
                memcmp(list + start, supported[i].name, nameLen) == 0) {
                *value = supported[i].value;
                return true;
            }
        }
        if (end == len) break;
        start = end + 1u;
    }
    return false;
}

static bool xssh_name_list_contains(const uint8_t* list, size_t len, const char* name)
{
    size_t nameLen = strlen(name);
    size_t start = 0;
    if (!list || !name) return false;
    while (start <= len) {
        size_t i = start;
        while (i < len && list[i] != ',') ++i;
        if (i - start == nameLen && memcmp(list + start, name, nameLen) == 0)
            return true;
        if (i >= len) break;
        start = i + 1u;
    }
    return false;
}

static bool xssh_build_algorithm_list(const XSshNamedAlgorithm* algorithms,
                                      uint8_t* out, size_t capacity,
                                      size_t* outLen)
{
    size_t off = 0;
    size_t i;
    if (!algorithms || !out || !outLen) return false;
    for (i = 0; algorithms[i].name; ++i) {
        size_t nameLen = strlen(algorithms[i].name);
        if (off && off >= capacity) return false;
        if (off) out[off++] = ',';
        if (nameLen > capacity - off) return false;
        memcpy(out + off, algorithms[i].name, nameLen);
        off += nameLen;
    }
    if (off == 0) return false;
    *outLen = off;
    return true;
}

static size_t xssh_cipher_key_size(XSshCipherAlgorithm cipher)
{
    switch (cipher) {
    case XSshCipherAlgorithm_Aes256Ctr: return 32;
    case XSshCipherAlgorithm_Aes192Ctr: return 24;
    case XSshCipherAlgorithm_Aes128Ctr: return 16;
    default: return 0;
    }
}

static bool xssh_hash_sha256(const void* data, size_t dataLen, uint8_t output[32])
{
    XByteArrayView result;
    if (!XCRYPTOGRAPHIC_SHA256_ON || !output || (!data && dataLen)) return false;
    result = XCryptographicHash_hashInto((char*)output, 32, (const char*)data,
                                         dataLen, XCryptographicHash_Sha256);
    return result.m_data == output && result.m_size == 32;
}

static bool xssh_hmac_sha256(const void* key, size_t keyLen,
                             const void* message, size_t messageLen,
                             uint8_t output[32])
{
    XByteArrayView result;
    if (!XCRYPTOGRAPHIC_HMAC_SHA256_ON || !output || (!key && keyLen) ||
        (!message && messageLen)) return false;
    result = XCryptographicHash_hmacInto((char*)output, 32, (const char*)key,
                                         keyLen, (const char*)message, messageLen,
                                         XCryptographicHash_Sha256);
    return result.m_data == output && result.m_size == 32;
}

#endif /* XPROTOCOL_ON && XSSH_ON */

#endif /* XSSHCRYPTO_H */
