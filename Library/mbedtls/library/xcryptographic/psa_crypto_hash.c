/*
 *  PSA hashing layer on top of Mbed TLS software crypto
 */
/*
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
 */

#include "tf_psa_crypto_common.h"

#if defined(MBEDTLS_PSA_CRYPTO_C)

#include <psa/crypto.h>
#include "psa_crypto_core.h"
#include "psa_crypto_hash.h"

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_HASH)
#include "XCryptographic.h"
#include <limits.h>
#endif

#include <mbedtls/private/error_common.h>
#include <string.h>

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_HASH)
static psa_status_t xcryptographic_hash_algorithm(
    psa_algorithm_t alg, XCryptographicHash_Algorithm *result)
{
    switch (alg) {
    case PSA_ALG_MD5:      *result = XCryptographicHash_Md5; return PSA_SUCCESS;
    case PSA_ALG_RIPEMD160:*result = XCryptographicHash_Ripemd160; return PSA_SUCCESS;
    case PSA_ALG_SHA_1:    *result = XCryptographicHash_Sha1; return PSA_SUCCESS;
    case PSA_ALG_SHA_224:  *result = XCryptographicHash_Sha224; return PSA_SUCCESS;
    case PSA_ALG_SHA_256:  *result = XCryptographicHash_Sha256; return PSA_SUCCESS;
    case PSA_ALG_SHA_384:  *result = XCryptographicHash_Sha384; return PSA_SUCCESS;
    case PSA_ALG_SHA_512:  *result = XCryptographicHash_Sha512; return PSA_SUCCESS;
    case PSA_ALG_SHA3_224: *result = XCryptographicHash_Sha3_224; return PSA_SUCCESS;
    case PSA_ALG_SHA3_256: *result = XCryptographicHash_Sha3_256; return PSA_SUCCESS;
    case PSA_ALG_SHA3_384: *result = XCryptographicHash_Sha3_384; return PSA_SUCCESS;
    case PSA_ALG_SHA3_512: *result = XCryptographicHash_Sha3_512; return PSA_SUCCESS;
    default: return PSA_ALG_IS_HASH(alg) ? PSA_ERROR_NOT_SUPPORTED : PSA_ERROR_INVALID_ARGUMENT;
    }
}

static psa_status_t xcryptographic_hash_compute(
    psa_algorithm_t alg, const uint8_t *input, size_t input_length,
    uint8_t *hash, size_t hash_size, size_t *hash_length)
{
    XCryptographicHash_Algorithm algorithm;
    XByteArrayView result;
    int size;
    psa_status_t status;
    if (!hash_length || (!input && input_length != 0) || !hash) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    *hash_length = 0;
    status = xcryptographic_hash_algorithm(alg, &algorithm);
    if (status != PSA_SUCCESS) {
        return status;
    }
    size = XCryptographicHash_hashLength(algorithm);
    if (size <= 0 || hash_size < (size_t)size) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }
    if (input_length > INT64_MAX) {
        return PSA_ERROR_NOT_SUPPORTED;
    }
    if (input_length == 0) {
        static const uint8_t empty_input = 0;
        result = XCryptographicHash_hashInto((char *)hash, hash_size,
                                             (const char *)&empty_input, 0, algorithm);
    } else {
        result = XCryptographicHash_hashInto((char *)hash, hash_size,
                                             (const char *)input, input_length, algorithm);
    }
    if (!result.m_data) {
        return PSA_ERROR_INSUFFICIENT_MEMORY;
    }
    *hash_length = (size_t)result.m_size;
    return PSA_SUCCESS;
}

/* Keep the public PSA operation layout stable while XCryptographic owns the
 * streaming context. The builtin context union has pointer alignment and
 * sufficient storage in every enabled mbedTLS configuration. */
static XCryptographicHash *xcryptographic_hash_context_get(
    const mbedtls_psa_hash_operation_t *operation)
{
    XCryptographicHash *context = NULL;

    memcpy(&context, &operation->ctx, sizeof(context));
    return context;
}

static void xcryptographic_hash_context_set(
    mbedtls_psa_hash_operation_t *operation,
    XCryptographicHash *context)
{
    memset(&operation->ctx, 0, sizeof(operation->ctx));
    memcpy(&operation->ctx, &context, sizeof(context));
}
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_HASH */

#if defined(MBEDTLS_PSA_BUILTIN_HASH)
psa_status_t mbedtls_psa_hash_abort(
    mbedtls_psa_hash_operation_t *operation)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_HASH)
    XCryptographicHash *context;

    if (operation->alg == 0) {
        return PSA_SUCCESS;
    }
    context = xcryptographic_hash_context_get(operation);
    if (!context) {
        return PSA_ERROR_BAD_STATE;
    }
    XCryptographicHash_delete(context);
    xcryptographic_hash_context_set(operation, NULL);
    operation->alg = 0;
    return PSA_SUCCESS;
#else
    switch (operation->alg) {
        case 0:
            /* The object has (apparently) been initialized but it is not
             * in use. It's ok to call abort on such an object, and there's
             * nothing to do. */
            break;
#if defined(MBEDTLS_PSA_BUILTIN_ALG_MD5)
        case PSA_ALG_MD5:
            mbedtls_md5_free(&operation->ctx.md5);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_RIPEMD160)
        case PSA_ALG_RIPEMD160:
            mbedtls_ripemd160_free(&operation->ctx.ripemd160);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_1)
        case PSA_ALG_SHA_1:
            mbedtls_sha1_free(&operation->ctx.sha1);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_224)
        case PSA_ALG_SHA_224:
            mbedtls_sha256_free(&operation->ctx.sha256);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_256)
        case PSA_ALG_SHA_256:
            mbedtls_sha256_free(&operation->ctx.sha256);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_384)
        case PSA_ALG_SHA_384:
            mbedtls_sha512_free(&operation->ctx.sha512);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_512)
        case PSA_ALG_SHA_512:
            mbedtls_sha512_free(&operation->ctx.sha512);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_224)
        case PSA_ALG_SHA3_224:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_256)
        case PSA_ALG_SHA3_256:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_384)
        case PSA_ALG_SHA3_384:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_512)
        case PSA_ALG_SHA3_512:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_SOME_HASH)
            mbedtls_sha3_free(&operation->ctx.sha3);
            break;
#endif
        default:
            return PSA_ERROR_BAD_STATE;
    }
    operation->alg = 0;
    return PSA_SUCCESS;
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_HASH */
}

psa_status_t mbedtls_psa_hash_setup(
    mbedtls_psa_hash_operation_t *operation,
    psa_algorithm_t alg)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_HASH)
    XCryptographicHash_Algorithm algorithm;
    XCryptographicHash *context;
    psa_status_t status;

    if (operation->alg != 0) {
        return PSA_ERROR_BAD_STATE;
    }
    status = xcryptographic_hash_algorithm(alg, &algorithm);
    if (status != PSA_SUCCESS) {
        return status;
    }
    context = XCryptographicHash_create(algorithm);
    if (!context) {
        return PSA_ERROR_INSUFFICIENT_MEMORY;
    }
    xcryptographic_hash_context_set(operation, context);
    operation->alg = alg;
    return PSA_SUCCESS;
#else
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;

    /* A context must be freshly initialized before it can be set up. */
    if (operation->alg != 0) {
        return PSA_ERROR_BAD_STATE;
    }

    switch (alg) {
#if defined(MBEDTLS_PSA_BUILTIN_ALG_MD5)
        case PSA_ALG_MD5:
            mbedtls_md5_init(&operation->ctx.md5);
            ret = mbedtls_md5_starts(&operation->ctx.md5);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_RIPEMD160)
        case PSA_ALG_RIPEMD160:
            mbedtls_ripemd160_init(&operation->ctx.ripemd160);
            ret = mbedtls_ripemd160_starts(&operation->ctx.ripemd160);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_1)
        case PSA_ALG_SHA_1:
            mbedtls_sha1_init(&operation->ctx.sha1);
            ret = mbedtls_sha1_starts(&operation->ctx.sha1);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_224)
        case PSA_ALG_SHA_224:
            mbedtls_sha256_init(&operation->ctx.sha256);
            ret = mbedtls_sha256_starts(&operation->ctx.sha256, 1);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_256)
        case PSA_ALG_SHA_256:
            mbedtls_sha256_init(&operation->ctx.sha256);
            ret = mbedtls_sha256_starts(&operation->ctx.sha256, 0);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_384)
        case PSA_ALG_SHA_384:
            mbedtls_sha512_init(&operation->ctx.sha512);
            ret = mbedtls_sha512_starts(&operation->ctx.sha512, 1);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_512)
        case PSA_ALG_SHA_512:
            mbedtls_sha512_init(&operation->ctx.sha512);
            ret = mbedtls_sha512_starts(&operation->ctx.sha512, 0);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_224)
        case PSA_ALG_SHA3_224:
            mbedtls_sha3_init(&operation->ctx.sha3);
            ret = mbedtls_sha3_starts(&operation->ctx.sha3, MBEDTLS_SHA3_224);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_256)
        case PSA_ALG_SHA3_256:
            mbedtls_sha3_init(&operation->ctx.sha3);
            ret = mbedtls_sha3_starts(&operation->ctx.sha3, MBEDTLS_SHA3_256);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_384)
        case PSA_ALG_SHA3_384:
            mbedtls_sha3_init(&operation->ctx.sha3);
            ret = mbedtls_sha3_starts(&operation->ctx.sha3, MBEDTLS_SHA3_384);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_512)
        case PSA_ALG_SHA3_512:
            mbedtls_sha3_init(&operation->ctx.sha3);
            ret = mbedtls_sha3_starts(&operation->ctx.sha3, MBEDTLS_SHA3_512);
            break;
#endif
        default:
            return PSA_ALG_IS_HASH(alg) ?
                   PSA_ERROR_NOT_SUPPORTED :
                   PSA_ERROR_INVALID_ARGUMENT;
    }
    if (ret == 0) {
        operation->alg = alg;
    } else {
        mbedtls_psa_hash_abort(operation);
    }
    return mbedtls_to_psa_error(ret);
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_HASH */
}

psa_status_t mbedtls_psa_hash_clone(
    const mbedtls_psa_hash_operation_t *source_operation,
    mbedtls_psa_hash_operation_t *target_operation)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_HASH)
    XCryptographicHash *source;
    XCryptographicHash *target;

    if (source_operation->alg == 0) {
        return PSA_ERROR_BAD_STATE;
    }
    source = xcryptographic_hash_context_get(source_operation);
    if (!source) {
        return PSA_ERROR_BAD_STATE;
    }
    target = XCryptographicHash_create(XCryptographicHash_algorithm(source));
    if (!target) {
        return PSA_ERROR_INSUFFICIENT_MEMORY;
    }
    XCryptographicHash_copy(target, source);
    xcryptographic_hash_context_set(target_operation, target);
    target_operation->alg = source_operation->alg;
    return PSA_SUCCESS;
#else
    switch (source_operation->alg) {
        case 0:
            return PSA_ERROR_BAD_STATE;
#if defined(MBEDTLS_PSA_BUILTIN_ALG_MD5)
        case PSA_ALG_MD5:
            mbedtls_md5_clone(&target_operation->ctx.md5,
                              &source_operation->ctx.md5);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_RIPEMD160)
        case PSA_ALG_RIPEMD160:
            mbedtls_ripemd160_clone(&target_operation->ctx.ripemd160,
                                    &source_operation->ctx.ripemd160);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_1)
        case PSA_ALG_SHA_1:
            mbedtls_sha1_clone(&target_operation->ctx.sha1,
                               &source_operation->ctx.sha1);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_224)
        case PSA_ALG_SHA_224:
            mbedtls_sha256_clone(&target_operation->ctx.sha256,
                                 &source_operation->ctx.sha256);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_256)
        case PSA_ALG_SHA_256:
            mbedtls_sha256_clone(&target_operation->ctx.sha256,
                                 &source_operation->ctx.sha256);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_384)
        case PSA_ALG_SHA_384:
            mbedtls_sha512_clone(&target_operation->ctx.sha512,
                                 &source_operation->ctx.sha512);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_512)
        case PSA_ALG_SHA_512:
            mbedtls_sha512_clone(&target_operation->ctx.sha512,
                                 &source_operation->ctx.sha512);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_224)
        case PSA_ALG_SHA3_224:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_256)
        case PSA_ALG_SHA3_256:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_384)
        case PSA_ALG_SHA3_384:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_512)
        case PSA_ALG_SHA3_512:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_SOME_HASH)
            mbedtls_sha3_clone(&target_operation->ctx.sha3,
                               &source_operation->ctx.sha3);
            break;
#endif
        default:
            (void) source_operation;
            (void) target_operation;
            return PSA_ERROR_NOT_SUPPORTED;
    }

    target_operation->alg = source_operation->alg;
    return PSA_SUCCESS;
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_HASH */
}

psa_status_t mbedtls_psa_hash_update(
    mbedtls_psa_hash_operation_t *operation,
    const uint8_t *input,
    size_t input_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_HASH)
    XCryptographicHash *context;
    static const uint8_t empty_input = 0;

    if (operation->alg == 0 || (!input && input_length != 0)) {
        return PSA_ERROR_BAD_STATE;
    }
    context = xcryptographic_hash_context_get(operation);
    if (!context) {
        return PSA_ERROR_BAD_STATE;
    }
    XCryptographicHash_addData(context,
                               (const char *) (input ? input : &empty_input),
                               input_length);
    return PSA_SUCCESS;
#else
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;

    switch (operation->alg) {
#if defined(MBEDTLS_PSA_BUILTIN_ALG_MD5)
        case PSA_ALG_MD5:
            ret = mbedtls_md5_update(&operation->ctx.md5,
                                     input, input_length);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_RIPEMD160)
        case PSA_ALG_RIPEMD160:
            ret = mbedtls_ripemd160_update(&operation->ctx.ripemd160,
                                           input, input_length);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_1)
        case PSA_ALG_SHA_1:
            ret = mbedtls_sha1_update(&operation->ctx.sha1,
                                      input, input_length);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_224)
        case PSA_ALG_SHA_224:
            ret = mbedtls_sha256_update(&operation->ctx.sha256,
                                        input, input_length);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_256)
        case PSA_ALG_SHA_256:
            ret = mbedtls_sha256_update(&operation->ctx.sha256,
                                        input, input_length);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_384)
        case PSA_ALG_SHA_384:
            ret = mbedtls_sha512_update(&operation->ctx.sha512,
                                        input, input_length);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_512)
        case PSA_ALG_SHA_512:
            ret = mbedtls_sha512_update(&operation->ctx.sha512,
                                        input, input_length);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_224)
        case PSA_ALG_SHA3_224:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_256)
        case PSA_ALG_SHA3_256:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_384)
        case PSA_ALG_SHA3_384:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_512)
        case PSA_ALG_SHA3_512:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_SOME_HASH)
    ret = mbedtls_sha3_update(&operation->ctx.sha3,
                              input, input_length);
    break;
#endif
        default:
            (void) input;
            (void) input_length;
            return PSA_ERROR_BAD_STATE;
    }

    return mbedtls_to_psa_error(ret);
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_HASH */
}

psa_status_t mbedtls_psa_hash_finish(
    mbedtls_psa_hash_operation_t *operation,
    uint8_t *hash,
    size_t hash_size,
    size_t *hash_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_HASH)
    XCryptographicHash *context;
    XByteArrayView result;
    size_t actual_hash_length;

    if (!hash_length) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    *hash_length = hash_size;
    if (hash_size != 0) {
        memset(hash, '!', hash_size);
    }
    if (operation->alg == 0) {
        return PSA_ERROR_BAD_STATE;
    }
    actual_hash_length = PSA_HASH_LENGTH(operation->alg);
    if (hash_size < actual_hash_length) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }
    context = xcryptographic_hash_context_get(operation);
    if (!context) {
        return PSA_ERROR_BAD_STATE;
    }
    result = XCryptographicHash_resultView(context);
    if (!result.m_data || (size_t) result.m_size != actual_hash_length) {
        return PSA_ERROR_INSUFFICIENT_MEMORY;
    }
    memcpy(hash, result.m_data, actual_hash_length);
    *hash_length = actual_hash_length;
    return PSA_SUCCESS;
#else
    psa_status_t status;
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    size_t actual_hash_length = PSA_HASH_LENGTH(operation->alg);

    /* Fill the output buffer with something that isn't a valid hash
     * (barring an attack on the hash and deliberately-crafted input),
     * in case the caller doesn't check the return status properly. */
    *hash_length = hash_size;
    /* If hash_size is 0 then hash may be NULL and then the
     * call to memset would have undefined behavior. */
    if (hash_size != 0) {
        memset(hash, '!', hash_size);
    }

    if (hash_size < actual_hash_length) {
        status = PSA_ERROR_BUFFER_TOO_SMALL;
        goto exit;
    }

    switch (operation->alg) {
#if defined(MBEDTLS_PSA_BUILTIN_ALG_MD5)
        case PSA_ALG_MD5:
            ret = mbedtls_md5_finish(&operation->ctx.md5, hash);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_RIPEMD160)
        case PSA_ALG_RIPEMD160:
            ret = mbedtls_ripemd160_finish(&operation->ctx.ripemd160, hash);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_1)
        case PSA_ALG_SHA_1:
            ret = mbedtls_sha1_finish(&operation->ctx.sha1, hash);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_224)
        case PSA_ALG_SHA_224:
            ret = mbedtls_sha256_finish(&operation->ctx.sha256, hash);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_256)
        case PSA_ALG_SHA_256:
            ret = mbedtls_sha256_finish(&operation->ctx.sha256, hash);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_384)
        case PSA_ALG_SHA_384:
            ret = mbedtls_sha512_finish(&operation->ctx.sha512, hash);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA_512)
        case PSA_ALG_SHA_512:
            ret = mbedtls_sha512_finish(&operation->ctx.sha512, hash);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_224)
        case PSA_ALG_SHA3_224:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_256)
        case PSA_ALG_SHA3_256:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_384)
        case PSA_ALG_SHA3_384:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_512)
        case PSA_ALG_SHA3_512:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHA3_SOME_HASH)
    ret = mbedtls_sha3_finish(&operation->ctx.sha3, hash, hash_size);
    break;
#endif
        default:
            (void) hash;
            return PSA_ERROR_BAD_STATE;
    }
    status = mbedtls_to_psa_error(ret);

exit:
    if (status == PSA_SUCCESS) {
        *hash_length = actual_hash_length;
    }
    return status;
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_HASH */
}

psa_status_t mbedtls_psa_hash_compute(
    psa_algorithm_t alg,
    const uint8_t *input,
    size_t input_length,
    uint8_t *hash,
    size_t hash_size,
    size_t *hash_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_HASH)
    return xcryptographic_hash_compute(alg, input, input_length,
                                       hash, hash_size, hash_length);
#else
    mbedtls_psa_hash_operation_t operation = MBEDTLS_PSA_HASH_OPERATION_INIT;
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    psa_status_t abort_status = PSA_ERROR_CORRUPTION_DETECTED;

    *hash_length = hash_size;
    status = mbedtls_psa_hash_setup(&operation, alg);
    if (status != PSA_SUCCESS) {
        goto exit;
    }
    status = mbedtls_psa_hash_update(&operation, input, input_length);
    if (status != PSA_SUCCESS) {
        goto exit;
    }
    status = mbedtls_psa_hash_finish(&operation, hash, hash_size, hash_length);
    if (status != PSA_SUCCESS) {
        goto exit;
    }

exit:
    abort_status = mbedtls_psa_hash_abort(&operation);
    if (status == PSA_SUCCESS) {
        return abort_status;
    } else {
        return status;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_HASH */
}
#endif /* MBEDTLS_PSA_BUILTIN_HASH */

#endif /* MBEDTLS_PSA_CRYPTO_C */
