/**
 * @file XCryptographic_mldsa87.c
 * @brief mbedTLS 可选的 ML-DSA-87 XCryptographic 后端。
 *
 * 第三方 mldsa-native 源码只在 mbedTLS 后端中以单编译单元方式编译；
 * XCryptographic 目录只保留公共 API 声明，不包含 mbedTLS 或第三方源码。
 */

#include "XCryptographic.h"
#include "XCryptographic_config.h"

#include <string.h>

#if XCRYPTOGRAPHIC_MLDSA87_ON

#define MLD_CONFIG_FILE "XCryptographic_mldsa87_config.h"
#define MLD_CONFIG_PARAMETER_SET 87

/* 只在本编译单元内可见 mldsa-native 的接口和实现。 */
#include "third_party/mldsa-native/mldsa_native.h"
#include "third_party/mldsa-native/mldsa_native.c"

static bool xcryptographic_mldsa87_contextPrefix(
    XByteArrayView context, uint8_t prefix[257], size_t* prefixSize)
{
    if (!prefixSize || context.m_size < 0 || context.m_size > 255u ||
        (context.m_size != 0u && !context.m_data)) {
        return false;
    }

    prefix[0] = 0u;
    prefix[1] = (uint8_t)context.m_size;
    if (context.m_size != 0u) {
        memcpy(prefix + 2u, context.m_data, context.m_size);
    }
    *prefixSize = context.m_size + 2u;
    return true;
}

bool XCryptographic_mldsa87KeyPair(
    XByteArrayView seed, uint8_t* publicKey, size_t publicKeySize,
    uint8_t* privateKey, size_t privateKeySize)
{
    if (seed.m_size != XCRYPTOGRAPHIC_MLDSA_SEED_SIZE || !seed.m_data ||
        !publicKey || publicKeySize < XCRYPTOGRAPHIC_MLDSA87_PUBLIC_KEY_SIZE ||
        !privateKey || privateKeySize < XCRYPTOGRAPHIC_MLDSA87_PRIVATE_KEY_SIZE) {
        return false;
    }
    return XCryptographic_mldsa87_keypair_internal(publicKey, privateKey,
                                                   seed.m_data) == 0;
}

bool XCryptographic_mldsa87Sign(
    XByteArrayView message, XByteArrayView context, XByteArrayView randomness,
    XByteArrayView privateKey, uint8_t* signature, size_t signatureSize)
{
    uint8_t prefix[257];
    size_t prefixSize = 0u;
    size_t produced = 0u;
    if (message.m_size < 0 ||
        (message.m_size != 0u && !message.m_data) ||
        !xcryptographic_mldsa87_contextPrefix(context, prefix, &prefixSize) ||
        randomness.m_size != XCRYPTOGRAPHIC_MLDSA_SEED_SIZE || !randomness.m_data ||
        privateKey.m_size != XCRYPTOGRAPHIC_MLDSA87_PRIVATE_KEY_SIZE ||
        !privateKey.m_data || !signature ||
        signatureSize < XCRYPTOGRAPHIC_MLDSA87_SIGNATURE_SIZE) {
        return false;
    }

    return XCryptographic_mldsa87_signature_internal(
               signature, &produced, message.m_data, message.m_size,
               prefix, prefixSize, randomness.m_data, privateKey.m_data, 0) == 0 &&
           produced == XCRYPTOGRAPHIC_MLDSA87_SIGNATURE_SIZE;
}

bool XCryptographic_mldsa87Verify(
    XByteArrayView message, XByteArrayView context, XByteArrayView signature,
    XByteArrayView publicKey)
{
    uint8_t prefix[257];
    size_t prefixSize = 0u;
    if (message.m_size < 0 ||
        (message.m_size != 0u && !message.m_data) ||
        !xcryptographic_mldsa87_contextPrefix(context, prefix, &prefixSize) ||
        signature.m_size != XCRYPTOGRAPHIC_MLDSA87_SIGNATURE_SIZE ||
        !signature.m_data ||
        publicKey.m_size != XCRYPTOGRAPHIC_MLDSA87_PUBLIC_KEY_SIZE ||
        !publicKey.m_data) {
        return false;
    }

    return XCryptographic_mldsa87_verify_internal(
               signature.m_data, signature.m_size, message.m_data, message.m_size,
               prefix, prefixSize, publicKey.m_data, 0) == 0;
}

#else

bool XCryptographic_mldsa87KeyPair(
    XByteArrayView seed, uint8_t* publicKey, size_t publicKeySize,
    uint8_t* privateKey, size_t privateKeySize)
{
    (void)seed;
    (void)publicKey;
    (void)publicKeySize;
    (void)privateKey;
    (void)privateKeySize;
    return false;
}

bool XCryptographic_mldsa87Sign(
    XByteArrayView message, XByteArrayView context, XByteArrayView randomness,
    XByteArrayView privateKey, uint8_t* signature, size_t signatureSize)
{
    (void)message;
    (void)context;
    (void)randomness;
    (void)privateKey;
    (void)signature;
    (void)signatureSize;
    return false;
}

bool XCryptographic_mldsa87Verify(
    XByteArrayView message, XByteArrayView context, XByteArrayView signature,
    XByteArrayView publicKey)
{
    (void)message;
    (void)context;
    (void)signature;
    (void)publicKey;
    return false;
}

#endif
