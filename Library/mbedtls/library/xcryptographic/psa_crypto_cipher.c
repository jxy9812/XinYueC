/*
 *  PSA cipher driver entry points
 */
/*
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
 */

#include "tf_psa_crypto_common.h"

#if defined(MBEDTLS_PSA_CRYPTO_C)

#include "psa_crypto_cipher.h"
#include "psa_crypto_core.h"
#include "psa_crypto_random_impl.h"
#include "constant_time_internal.h"

#include "mbedtls/private/cipher.h"
#include "mbedtls/private/error_common.h"

#include <string.h>

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CTR) || \
    defined(MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER) || \
    defined(MBEDTLS_USE_XCRYPTOGRAPHIC_XTS) || \
    defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20) || \
    defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG)
#include "XCryptographic.h"
#include "XMemory.h"
#include <limits.h>
#endif

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CTR)

typedef struct xcryptographic_ctr_context {
    XCryptographic_Key key;
    XCryptographic_CipherOperation operation;
} xcryptographic_ctr_context;

static xcryptographic_ctr_context *xcryptographic_ctr_context_get(
    const mbedtls_psa_cipher_operation_t *operation)
{
    xcryptographic_ctr_context *context = NULL;

    memcpy(&context, &operation->ctx.cipher, sizeof(context));
    return context;
}

static void xcryptographic_ctr_context_set(
    mbedtls_psa_cipher_operation_t *operation,
    xcryptographic_ctr_context *context)
{
    memset(&operation->ctx.cipher, 0, sizeof(operation->ctx.cipher));
    memcpy(&operation->ctx.cipher, &context, sizeof(context));
}

static bool xcryptographic_ctr_uses_operation(
    const mbedtls_psa_cipher_operation_t *operation)
{
    return operation->alg == PSA_ALG_CTR &&
           xcryptographic_ctr_context_get(operation) != NULL;
}
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CTR */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER) || \
    defined(MBEDTLS_USE_XCRYPTOGRAPHIC_XTS)
typedef struct xcryptographic_block_cipher_context {
    XCryptographic_BlockCipherOperation operation;
} xcryptographic_block_cipher_context;

static xcryptographic_block_cipher_context *xcryptographic_block_cipher_context_get(
    const mbedtls_psa_cipher_operation_t *operation)
{
    xcryptographic_block_cipher_context *context = NULL;

    memcpy(&context, &operation->ctx.cipher, sizeof(context));
    return context;
}

static void xcryptographic_block_cipher_context_set(
    mbedtls_psa_cipher_operation_t *operation,
    xcryptographic_block_cipher_context *context)
{
    memset(&operation->ctx.cipher, 0, sizeof(operation->ctx.cipher));
    memcpy(&operation->ctx.cipher, &context, sizeof(context));
}

static bool xcryptographic_block_cipher_uses_operation(
    const mbedtls_psa_cipher_operation_t *operation)
{
    return (operation->alg == PSA_ALG_ECB_NO_PADDING ||
            operation->alg == PSA_ALG_CBC_NO_PADDING ||
            operation->alg == PSA_ALG_CBC_PKCS7 ||
            operation->alg == PSA_ALG_CFB ||
            operation->alg == PSA_ALG_OFB ||
            operation->alg == PSA_ALG_XTS) &&
           xcryptographic_block_cipher_context_get(operation) != NULL;
}
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER || MBEDTLS_USE_XCRYPTOGRAPHIC_XTS */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG)
typedef struct xcryptographic_ccm_star_no_tag_context {
    XCryptographic_CcmStarNoTagOperation operation;
} xcryptographic_ccm_star_no_tag_context;

static xcryptographic_ccm_star_no_tag_context *xcryptographic_ccm_star_no_tag_context_get(
    const mbedtls_psa_cipher_operation_t *operation)
{
    xcryptographic_ccm_star_no_tag_context *context = NULL;

    memcpy(&context, &operation->ctx.cipher, sizeof(context));
    return context;
}

static void xcryptographic_ccm_star_no_tag_context_set(
    mbedtls_psa_cipher_operation_t *operation,
    xcryptographic_ccm_star_no_tag_context *context)
{
    memset(&operation->ctx.cipher, 0, sizeof(operation->ctx.cipher));
    memcpy(&operation->ctx.cipher, &context, sizeof(context));
}

static bool xcryptographic_ccm_star_no_tag_uses_operation(
    const mbedtls_psa_cipher_operation_t *operation)
{
    return operation->alg == PSA_ALG_CCM_STAR_NO_TAG &&
           xcryptographic_ccm_star_no_tag_context_get(operation) != NULL;
}
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20)
typedef struct xcryptographic_chacha20_context {
    XCryptographic_ChaCha20Operation operation;
} xcryptographic_chacha20_context;

static xcryptographic_chacha20_context *xcryptographic_chacha20_context_get(
    const mbedtls_psa_cipher_operation_t *operation)
{
    xcryptographic_chacha20_context *context = NULL;

    memcpy(&context, &operation->ctx.cipher, sizeof(context));
    return context;
}

static void xcryptographic_chacha20_context_set(
    mbedtls_psa_cipher_operation_t *operation,
    xcryptographic_chacha20_context *context)
{
    memset(&operation->ctx.cipher, 0, sizeof(operation->ctx.cipher));
    memcpy(&operation->ctx.cipher, &context, sizeof(context));
}

static bool xcryptographic_chacha20_uses_operation(
    const mbedtls_psa_cipher_operation_t *operation)
{
    return operation->alg == PSA_ALG_STREAM_CIPHER &&
           xcryptographic_chacha20_context_get(operation) != NULL;
}
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20 */

#if defined(MBEDTLS_PSA_BUILTIN_CIPHER) || \
    defined(MBEDTLS_PSA_BUILTIN_AEAD) || \
    defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_AES) || \
    defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ARIA) || \
    defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_CAMELLIA) || \
    defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_CHACHA20)
/* mbedtls_cipher_values_from_psa() below only checks if the proper build symbols
 * are enabled, but it does not provide any compatibility check between them
 * (i.e. if the specified key works with the specified algorithm). This helper
 * function is meant to provide this support.
 * mbedtls_cipher_info_from_psa() might be used for the same purpose, but it
 * requires CIPHER_C to be enabled.
 */
static psa_status_t mbedtls_cipher_validate_values(
    psa_algorithm_t alg,
    psa_key_type_t key_type)
{
    /* Reduce code size - hinting to the compiler about what it can assume allows the compiler to
       eliminate bits of the logic below. */
#if !defined(PSA_WANT_KEY_TYPE_AES)
    MBEDTLS_ASSUME(key_type != PSA_KEY_TYPE_AES);
#endif
#if !defined(PSA_WANT_KEY_TYPE_ARIA)
    MBEDTLS_ASSUME(key_type != PSA_KEY_TYPE_ARIA);
#endif
#if !defined(PSA_WANT_KEY_TYPE_CAMELLIA)
    MBEDTLS_ASSUME(key_type != PSA_KEY_TYPE_CAMELLIA);
#endif
#if !defined(PSA_WANT_KEY_TYPE_CHACHA20)
    MBEDTLS_ASSUME(key_type != PSA_KEY_TYPE_CHACHA20);
#endif
#if !defined(PSA_WANT_ALG_CCM)
    MBEDTLS_ASSUME(alg != PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 0));
#endif
#if !defined(PSA_WANT_ALG_GCM)
    MBEDTLS_ASSUME(alg != PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_GCM, 0));
#endif
#if !defined(PSA_WANT_ALG_STREAM_CIPHER)
    MBEDTLS_ASSUME(alg != PSA_ALG_STREAM_CIPHER);
#endif
#if !defined(PSA_WANT_ALG_CHACHA20_POLY1305)
    MBEDTLS_ASSUME(alg != PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CHACHA20_POLY1305, 0));
#endif
#if !defined(PSA_WANT_ALG_CCM_STAR_NO_TAG)
    MBEDTLS_ASSUME(alg != PSA_ALG_CCM_STAR_NO_TAG);
#endif
#if !defined(PSA_WANT_ALG_CTR)
    MBEDTLS_ASSUME(alg != PSA_ALG_CTR);
#endif
#if !defined(PSA_WANT_ALG_CFB)
    MBEDTLS_ASSUME(alg != PSA_ALG_CFB);
#endif
#if !defined(PSA_WANT_ALG_OFB)
    MBEDTLS_ASSUME(alg != PSA_ALG_OFB);
#endif
#if !defined(PSA_WANT_ALG_ECB_NO_PADDING)
    MBEDTLS_ASSUME(alg != PSA_ALG_ECB_NO_PADDING);
#endif
#if !defined(PSA_WANT_ALG_CBC_NO_PADDING)
    MBEDTLS_ASSUME(alg != PSA_ALG_CBC_NO_PADDING);
#endif
#if !defined(PSA_WANT_ALG_CBC_PKCS7)
    MBEDTLS_ASSUME(alg != PSA_ALG_CBC_PKCS7);
#endif
#if !defined(PSA_WANT_ALG_CMAC)
    MBEDTLS_ASSUME(alg != PSA_ALG_CMAC);
#endif

    if (alg == PSA_ALG_STREAM_CIPHER ||
        alg == PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CHACHA20_POLY1305, 0)) {
        if (key_type == PSA_KEY_TYPE_CHACHA20) {
            return PSA_SUCCESS;
        }
    }

    if (alg == PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 0) ||
        alg == PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_GCM, 0) ||
        alg == PSA_ALG_CCM_STAR_NO_TAG) {
        if (key_type == PSA_KEY_TYPE_AES ||
            key_type == PSA_KEY_TYPE_ARIA ||
            key_type == PSA_KEY_TYPE_CAMELLIA) {
            return PSA_SUCCESS;
        }
    }

    if (alg == PSA_ALG_CTR ||
        alg == PSA_ALG_CFB ||
        alg == PSA_ALG_OFB ||
        alg == PSA_ALG_XTS ||
        alg == PSA_ALG_ECB_NO_PADDING ||
        alg == PSA_ALG_CBC_NO_PADDING ||
        alg == PSA_ALG_CBC_PKCS7 ||
        alg == PSA_ALG_CMAC) {
        if (key_type == PSA_KEY_TYPE_AES ||
            key_type == PSA_KEY_TYPE_ARIA ||
            key_type == PSA_KEY_TYPE_CAMELLIA) {
            return PSA_SUCCESS;
        }
    }

    return PSA_ERROR_NOT_SUPPORTED;
}

psa_status_t mbedtls_cipher_values_from_psa(
    psa_algorithm_t alg,
    psa_key_type_t key_type,
    mbedtls_cipher_mode_t *mode,
    mbedtls_cipher_id_t *cipher_id)
{
    mbedtls_cipher_id_t cipher_id_tmp;
    if (PSA_ALG_IS_AEAD(alg)) {
        alg = PSA_ALG_AEAD_WITH_SHORTENED_TAG(alg, 0);
    }

    if (PSA_ALG_IS_CIPHER(alg) || PSA_ALG_IS_AEAD(alg)) {
        switch (alg) {
#if defined(MBEDTLS_PSA_BUILTIN_ALG_STREAM_CIPHER)
            case PSA_ALG_STREAM_CIPHER:
                *mode = MBEDTLS_MODE_STREAM;
                break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CTR)
            case PSA_ALG_CTR:
                *mode = MBEDTLS_MODE_CTR;
                break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CFB)
            case PSA_ALG_CFB:
                *mode = MBEDTLS_MODE_CFB;
                break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_OFB)
            case PSA_ALG_OFB:
                *mode = MBEDTLS_MODE_OFB;
                break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_ECB_NO_PADDING)
            case PSA_ALG_ECB_NO_PADDING:
                *mode = MBEDTLS_MODE_ECB;
                break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CBC_NO_PADDING)
            case PSA_ALG_CBC_NO_PADDING:
                *mode = MBEDTLS_MODE_CBC;
                break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CBC_PKCS7)
            case PSA_ALG_CBC_PKCS7:
                *mode = MBEDTLS_MODE_CBC;
                break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CCM_STAR_NO_TAG)
            case PSA_ALG_CCM_STAR_NO_TAG:
                *mode = MBEDTLS_MODE_CCM_STAR_NO_TAG;
                break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CCM)
            case PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 0):
                *mode = MBEDTLS_MODE_CCM;
                break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_GCM)
            case PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_GCM, 0):
                *mode = MBEDTLS_MODE_GCM;
                break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305)
            case PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CHACHA20_POLY1305, 0):
                *mode = MBEDTLS_MODE_CHACHAPOLY;
                break;
#endif
            default:
                return PSA_ERROR_NOT_SUPPORTED;
        }
    } else if (alg == PSA_ALG_CMAC) {
        *mode = MBEDTLS_MODE_ECB;
    } else {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    switch (key_type) {
#if defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_AES)
        case PSA_KEY_TYPE_AES:
            cipher_id_tmp = MBEDTLS_CIPHER_ID_AES;
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ARIA)
        case PSA_KEY_TYPE_ARIA:
            cipher_id_tmp = MBEDTLS_CIPHER_ID_ARIA;
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_CAMELLIA)
        case PSA_KEY_TYPE_CAMELLIA:
            cipher_id_tmp = MBEDTLS_CIPHER_ID_CAMELLIA;
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_CHACHA20)
        case PSA_KEY_TYPE_CHACHA20:
            cipher_id_tmp = MBEDTLS_CIPHER_ID_CHACHA20;
            break;
#endif
        default:
            return PSA_ERROR_NOT_SUPPORTED;
    }
    if (cipher_id != NULL) {
        *cipher_id = cipher_id_tmp;
    }

    return mbedtls_cipher_validate_values(alg, key_type);
}
#else
/* When MBEDTLS_PSA_BUILTIN_CIPHER, MBEDTLS_PSA_BUILTIN_AEAD,
 * MBEDTLS_PSA_BUILTIN_KEY_TYPE_AES, MBEDTLS_PSA_BUILTIN_KEY_TYPE_ARIA,
 * MBEDTLS_PSA_BUILTIN_KEY_TYPE_CAMELLIA and MBEDTLS_PSA_BUILTIN_CIPHER are
 * not defined, the function mbedtls_cipher_values_from_psa() can only ever
 * return PSA_ERROR_NOT_SUPPORTED. In that configuration, the compiler may
 * report an error such as:
 *     "code will never be executed [-Werror,-Wunreachable-code]"
 * on the line:
 *     if (cipher_id != NULL) {
 *
 * Since under these conditions the function can only return
 * PSA_ERROR_NOT_SUPPORTED and still pulls in a non-trivial amount of code,
 * provide a reduced version that simply returns PSA_ERROR_NOT_SUPPORTED.
 *
 * Note that when all the conditions above are met, this function is used
 * by mbedtls_cipher_info_from_psa(), if built-in CMAC is additionally enabled.
 */
psa_status_t mbedtls_cipher_values_from_psa(
    psa_algorithm_t alg,
    psa_key_type_t key_type,
    mbedtls_cipher_mode_t *mode,
    mbedtls_cipher_id_t *cipher_id)
{
    (void) alg;
    (void) key_type;
    (void) mode;
    (void) cipher_id;

    return PSA_ERROR_NOT_SUPPORTED;
}
#endif /* MBEDTLS_PSA_BUILTIN_CIPHER) ||
          MBEDTLS_PSA_BUILTIN_AEAD ||
          MBEDTLS_PSA_BUILTIN_KEY_TYPE_AES ||
          MBEDTLS_PSA_BUILTIN_KEY_TYPE_ARIA ||
          MBEDTLS_PSA_BUILTIN_KEY_TYPE_CAMELLIA ||
          MBEDTLS_PSA_BUILTIN_KEY_TYPE_CHACHA20 */

#if defined(MBEDTLS_CIPHER_C)
const mbedtls_cipher_info_t *mbedtls_cipher_info_from_psa(
    psa_algorithm_t alg,
    psa_key_type_t key_type,
    size_t key_bits,
    mbedtls_cipher_id_t *cipher_id)
{
    mbedtls_cipher_mode_t mode;
    psa_status_t status;
    mbedtls_cipher_id_t cipher_id_tmp = MBEDTLS_CIPHER_ID_NONE;

    status = mbedtls_cipher_values_from_psa(alg, key_type, &mode, &cipher_id_tmp);
    if (status != PSA_SUCCESS) {
        return NULL;
    }
    if (cipher_id != NULL) {
        *cipher_id = cipher_id_tmp;
    }

    return mbedtls_cipher_info_from_values(cipher_id_tmp, (int) key_bits, mode);
}
#endif /* MBEDTLS_CIPHER_C */

#if defined(MBEDTLS_PSA_BUILTIN_CIPHER)

static psa_status_t psa_cipher_setup(
    mbedtls_psa_cipher_operation_t *operation,
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg,
    mbedtls_operation_t cipher_operation)
{
    int ret = 0;
    size_t key_bits;
    const mbedtls_cipher_info_t *cipher_info = NULL;
    psa_key_type_t key_type = attributes->type;

    (void) key_buffer_size;

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CTR)
    if (alg == PSA_ALG_CTR && key_type == PSA_KEY_TYPE_AES) {
        xcryptographic_ctr_context *context;
        XByteArrayView key_view;

        if (key_buffer_size > INT64_MAX) {
            return PSA_ERROR_NOT_SUPPORTED;
        }
        key_view = XByteArrayView_create_data(key_buffer, (int64_t)key_buffer_size);
        context = (xcryptographic_ctr_context *)XMalloc_System(sizeof(*context));
        if (!context) {
            return PSA_ERROR_INSUFFICIENT_MEMORY;
        }
        memset(context, 0, sizeof(*context));
        if (!XCryptographic_aesCtrImportKey(key_view, &context->key)) {
            XFree_System(context);
            return PSA_ERROR_NOT_SUPPORTED;
        }
        operation->alg = alg;
        operation->block_length = PSA_BLOCK_CIPHER_BLOCK_LENGTH(key_type);
        operation->iv_length = PSA_CIPHER_IV_LENGTH(key_type, alg);
        xcryptographic_ctr_context_set(operation, context);
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CTR */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_XTS)
    if (alg == PSA_ALG_XTS && key_type == PSA_KEY_TYPE_AES &&
        (key_buffer_size == 32 || key_buffer_size == 64)) {
        xcryptographic_block_cipher_context *context =
            (xcryptographic_block_cipher_context *)XMalloc_System(sizeof(*context));
        if (!context) return PSA_ERROR_INSUFFICIENT_MEMORY;
        memset(context, 0, sizeof(*context));
        if (!XCryptographic_blockCipherSetup(&context->operation, XCryptographic_BlockCipherAlgorithm_Aes,
                XByteArrayView_create_data(key_buffer, (int64_t)key_buffer_size),
                XCryptographic_BlockCipherMode_Xts,
                cipher_operation == MBEDTLS_ENCRYPT)) {
            XFree_System(context);
            return PSA_ERROR_NOT_SUPPORTED;
        }
        operation->alg = alg;
        operation->block_length = 16;
        operation->iv_length = 16;
        xcryptographic_block_cipher_context_set(operation, context);
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_XTS */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER)
    if ((alg == PSA_ALG_ECB_NO_PADDING || alg == PSA_ALG_CBC_NO_PADDING ||
         alg == PSA_ALG_CBC_PKCS7 || alg == PSA_ALG_CFB || alg == PSA_ALG_OFB) &&
        (key_type == PSA_KEY_TYPE_AES || key_type == PSA_KEY_TYPE_ARIA ||
         key_type == PSA_KEY_TYPE_CAMELLIA) && key_buffer_size <= INT64_MAX) {
        xcryptographic_block_cipher_context *context;
        XCryptographic_BlockCipherMode mode = alg == PSA_ALG_ECB_NO_PADDING ?
            XCryptographic_BlockCipherMode_EcbNoPadding :
            alg == PSA_ALG_CBC_NO_PADDING ? XCryptographic_BlockCipherMode_CbcNoPadding :
            alg == PSA_ALG_CBC_PKCS7 ? XCryptographic_BlockCipherMode_CbcPkcs7 :
            alg == PSA_ALG_CFB ? XCryptographic_BlockCipherMode_Cfb :
            XCryptographic_BlockCipherMode_Ofb;
        XCryptographic_BlockCipherAlgorithm algorithm =
            key_type == PSA_KEY_TYPE_ARIA ? XCryptographic_BlockCipherAlgorithm_Aria :
            key_type == PSA_KEY_TYPE_CAMELLIA ? XCryptographic_BlockCipherAlgorithm_Camellia :
            XCryptographic_BlockCipherAlgorithm_Aes;

        context = (xcryptographic_block_cipher_context *)XMalloc_System(sizeof(*context));
        if (!context) return PSA_ERROR_INSUFFICIENT_MEMORY;
        memset(context, 0, sizeof(*context));
        if (!XCryptographic_blockCipherSetup(
                &context->operation, algorithm,
                XByteArrayView_create_data(key_buffer, (int64_t)key_buffer_size),
                mode, cipher_operation == MBEDTLS_ENCRYPT)) {
            XFree_System(context);
            return PSA_ERROR_NOT_SUPPORTED;
        }
        operation->alg = alg;
        operation->block_length = 16;
        operation->iv_length = alg == PSA_ALG_ECB_NO_PADDING ? 0 : 16;
        xcryptographic_block_cipher_context_set(operation, context);
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG)
    if (alg == PSA_ALG_CCM_STAR_NO_TAG && key_type == PSA_KEY_TYPE_AES &&
        (key_buffer_size == 16 || key_buffer_size == 24 || key_buffer_size == 32)) {
        xcryptographic_ccm_star_no_tag_context *context =
            (xcryptographic_ccm_star_no_tag_context *)XMalloc_System(sizeof(*context));
        if (!context) return PSA_ERROR_INSUFFICIENT_MEMORY;
        memset(context, 0, sizeof(*context));
        operation->alg = alg;
        operation->block_length = 1;
        operation->iv_length = 13;
        xcryptographic_ccm_star_no_tag_context_set(operation, context);
        if (!XCryptographic_aesCcmStarNoTagSetup(
                &context->operation,
                XByteArrayView_create_data(key_buffer, (int64_t)key_buffer_size),
                XByteArrayView_create_data((const uint8_t[13]){ 0 }, 13))) {
            XFree_System(context);
            xcryptographic_ccm_star_no_tag_context_set(operation, NULL);
            operation->alg = 0;
            return PSA_ERROR_NOT_SUPPORTED;
        }
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20)
    if (alg == PSA_ALG_STREAM_CIPHER && key_type == PSA_KEY_TYPE_CHACHA20 &&
        key_buffer_size == 32) {
        xcryptographic_chacha20_context *context =
            (xcryptographic_chacha20_context *)XMalloc_System(sizeof(*context));
        if (!context) return PSA_ERROR_INSUFFICIENT_MEMORY;
        memset(context, 0, sizeof(*context));
        operation->alg = alg;
        operation->block_length = 1;
        operation->iv_length = 12;
        xcryptographic_chacha20_context_set(operation, context);
        if (!XCryptographic_chacha20Setup(
                &context->operation,
                XByteArrayView_create_data(key_buffer, (int64_t)key_buffer_size),
                XByteArrayView_create_data((const uint8_t[12]){ 0 }, 12))) {
            XFree_System(context);
            xcryptographic_chacha20_context_set(operation, NULL);
            operation->alg = 0;
            return PSA_ERROR_NOT_SUPPORTED;
        }
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20 */

    mbedtls_cipher_init(&operation->ctx.cipher);

    operation->alg = alg;
    key_bits = attributes->bits;
    cipher_info = mbedtls_cipher_info_from_psa(alg, key_type,
                                               key_bits, NULL);
    if (cipher_info == NULL) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    ret = mbedtls_cipher_setup(&operation->ctx.cipher, cipher_info);
    if (ret != 0) {
        goto exit;
    }

    {
        ret = mbedtls_cipher_setkey(&operation->ctx.cipher, key_buffer,
                                    (int) key_bits, cipher_operation);
    }
    if (ret != 0) {
        goto exit;
    }

#if defined(MBEDTLS_PSA_BUILTIN_ALG_CBC_NO_PADDING) || \
    defined(MBEDTLS_PSA_BUILTIN_ALG_CBC_PKCS7)
    switch (alg) {
        case PSA_ALG_CBC_NO_PADDING:
            ret = mbedtls_cipher_set_padding_mode(&operation->ctx.cipher,
                                                  MBEDTLS_PADDING_NONE);
            break;
        case PSA_ALG_CBC_PKCS7:
            ret = mbedtls_cipher_set_padding_mode(&operation->ctx.cipher,
                                                  MBEDTLS_PADDING_PKCS7);
            break;
        default:
            /* The algorithm doesn't involve padding. */
            ret = 0;
            break;
    }
    if (ret != 0) {
        goto exit;
    }
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CBC_NO_PADDING ||
          MBEDTLS_PSA_BUILTIN_ALG_CBC_PKCS7 */

    operation->block_length = (PSA_ALG_IS_STREAM_CIPHER(alg) ? 1 :
                               PSA_BLOCK_CIPHER_BLOCK_LENGTH(key_type));
    operation->iv_length = PSA_CIPHER_IV_LENGTH(key_type, alg);

exit:
    return mbedtls_to_psa_error(ret);
}

psa_status_t mbedtls_psa_cipher_encrypt_setup(
    mbedtls_psa_cipher_operation_t *operation,
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg)
{
    return psa_cipher_setup(operation, attributes,
                            key_buffer, key_buffer_size,
                            alg, MBEDTLS_ENCRYPT);
}

psa_status_t mbedtls_psa_cipher_decrypt_setup(
    mbedtls_psa_cipher_operation_t *operation,
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg)
{
    return psa_cipher_setup(operation, attributes,
                            key_buffer, key_buffer_size,
                            alg, MBEDTLS_DECRYPT);
}

psa_status_t mbedtls_psa_cipher_set_iv(
    mbedtls_psa_cipher_operation_t *operation,
    const uint8_t *iv, size_t iv_length)
{
    if (iv_length != operation->iv_length) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CTR)
    if (xcryptographic_ctr_uses_operation(operation)) {
        xcryptographic_ctr_context *context = xcryptographic_ctr_context_get(operation);
        if (!iv || iv_length > INT64_MAX) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        return XCryptographic_aesCtrSetup(
                   &context->operation, context->key, true,
                   XByteArrayView_create_data(iv, (int64_t)iv_length)) ?
               PSA_SUCCESS : PSA_ERROR_BAD_STATE;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CTR */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER) || \
    defined(MBEDTLS_USE_XCRYPTOGRAPHIC_XTS)
    if (xcryptographic_block_cipher_uses_operation(operation)) {
        xcryptographic_block_cipher_context *context =
            xcryptographic_block_cipher_context_get(operation);
        if (operation->alg == PSA_ALG_ECB_NO_PADDING || !iv || iv_length > INT64_MAX) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        return XCryptographic_blockCipherSetIv(
                   &context->operation,
                   XByteArrayView_create_data(iv, (int64_t)iv_length)) ?
               PSA_SUCCESS : PSA_ERROR_INVALID_ARGUMENT;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER || MBEDTLS_USE_XCRYPTOGRAPHIC_XTS */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG)
    if (xcryptographic_ccm_star_no_tag_uses_operation(operation)) {
        xcryptographic_ccm_star_no_tag_context *context =
            xcryptographic_ccm_star_no_tag_context_get(operation);
        uint8_t key[32];
        uint8_t key_len;
        bool valid;
        if (!iv || iv_length != 13) return PSA_ERROR_INVALID_ARGUMENT;
        key_len = context->operation.keyLen;
        memcpy(key, context->operation.key, sizeof(key));
        XCryptographic_aesCcmStarNoTagAbort(&context->operation);
        valid = XCryptographic_aesCcmStarNoTagSetup(
                   &context->operation,
                   XByteArrayView_create_data(key, key_len),
                   XByteArrayView_create_data(iv, 13)) ?
               true : false;
        memset(key, 0, sizeof(key));
        return valid ? PSA_SUCCESS : PSA_ERROR_BAD_STATE;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20)
    if (xcryptographic_chacha20_uses_operation(operation)) {
        xcryptographic_chacha20_context *context =
            xcryptographic_chacha20_context_get(operation);
        uint8_t key[32];
        bool valid;
        if (!iv || iv_length != 12) return PSA_ERROR_INVALID_ARGUMENT;
        memcpy(key, context->operation.key, sizeof(key));
        XCryptographic_chacha20Abort(&context->operation);
        valid = XCryptographic_chacha20Setup(
                   &context->operation,
                   XByteArrayView_create_data(key, sizeof(key)),
                   XByteArrayView_create_data(iv, 12)) ?
               true : false;
        memset(key, 0, sizeof(key));
        return valid ? PSA_SUCCESS : PSA_ERROR_BAD_STATE;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20 */

    return mbedtls_to_psa_error(
        mbedtls_cipher_set_iv(&operation->ctx.cipher,
                              iv, iv_length));
}

#if defined(MBEDTLS_PSA_BUILTIN_ALG_ECB_NO_PADDING)
/** Process input for which the algorithm is set to ECB mode.
 *
 * This requires manual processing, since the PSA API is defined as being
 * able to process arbitrary-length calls to psa_cipher_update() with ECB mode,
 * but the underlying mbedtls_cipher_update only takes full blocks.
 *
 * \param ctx           The mbedtls cipher context to use. It must have been
 *                      set up for ECB.
 * \param[in] input     The input plaintext or ciphertext to process.
 * \param input_length  The number of bytes to process from \p input.
 *                      This does not need to be aligned to a block boundary.
 *                      If there is a partial block at the end of the input,
 *                      it is stored in \p ctx for future processing.
 * \param output        The buffer where the output is written. It must be
 *                      at least `BS * floor((p + input_length) / BS)` bytes
 *                      long, where `p` is the number of bytes in the
 *                      unprocessed partial block in \p ctx (with
 *                      `0 <= p <= BS - 1`) and `BS` is the block size.
 * \param output_length On success, the number of bytes written to \p output.
 *                      \c 0 on error.
 *
 * \return #PSA_SUCCESS or an error from a hardware accelerator
 */
static psa_status_t psa_cipher_update_ecb(
    mbedtls_cipher_context_t *ctx,
    const uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t *output_length)
{
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    size_t block_size = mbedtls_cipher_info_get_block_size(ctx->cipher_info);
    size_t internal_output_length = 0;
    *output_length = 0;

    if (input_length == 0) {
        status = PSA_SUCCESS;
        goto exit;
    }

    if (ctx->unprocessed_len > 0) {
        /* Fill up to block size, and run the block if there's a full one. */
        size_t bytes_to_copy = block_size - ctx->unprocessed_len;

        if (input_length < bytes_to_copy) {
            bytes_to_copy = input_length;
        }

        memcpy(&(ctx->unprocessed_data[ctx->unprocessed_len]),
               input, bytes_to_copy);
        input_length -= bytes_to_copy;
        input += bytes_to_copy;
        ctx->unprocessed_len += bytes_to_copy;

        if (ctx->unprocessed_len == block_size) {
            status = mbedtls_to_psa_error(
                mbedtls_cipher_update(ctx,
                                      ctx->unprocessed_data,
                                      block_size,
                                      output, &internal_output_length));

            if (status != PSA_SUCCESS) {
                goto exit;
            }

            output += internal_output_length;
            *output_length += internal_output_length;
            ctx->unprocessed_len = 0;
        }
    }

    while (input_length >= block_size) {
        /* Run all full blocks we have, one by one */
        status = mbedtls_to_psa_error(
            mbedtls_cipher_update(ctx, input,
                                  block_size,
                                  output, &internal_output_length));

        if (status != PSA_SUCCESS) {
            goto exit;
        }

        input_length -= block_size;
        input += block_size;

        output += internal_output_length;
        *output_length += internal_output_length;
    }

    if (input_length > 0) {
        /* Save unprocessed bytes for later processing */
        memcpy(&(ctx->unprocessed_data[ctx->unprocessed_len]),
               input, input_length);
        ctx->unprocessed_len += input_length;
    }

    status = PSA_SUCCESS;

exit:
    return status;
}
#endif /* MBEDTLS_PSA_BUILTIN_ALG_ECB_NO_PADDING */

psa_status_t mbedtls_psa_cipher_update(
    mbedtls_psa_cipher_operation_t *operation,
    const uint8_t *input, size_t input_length,
    uint8_t *output, size_t output_size, size_t *output_length)
{
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    size_t expected_output_size;

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CTR)
    if (xcryptographic_ctr_uses_operation(operation)) {
        xcryptographic_ctr_context *context = xcryptographic_ctr_context_get(operation);
        XByteArrayView result;

        if ((!input && input_length != 0) || input_length > INT64_MAX) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        if (input_length == 0) {
            *output_length = 0;
            return PSA_SUCCESS;
        }
        if (!output || output_size < input_length) {
            return PSA_ERROR_BUFFER_TOO_SMALL;
        }
        result = XCryptographic_aesCtrUpdateInto(
            &context->operation, (char *)output, output_size,
            XByteArrayView_create_data(input, (int64_t)input_length));
        if (!result.m_data || (size_t)result.m_size != input_length) {
            return PSA_ERROR_BAD_STATE;
        }
        *output_length = input_length;
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CTR */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER) || \
    defined(MBEDTLS_USE_XCRYPTOGRAPHIC_XTS)
    if (xcryptographic_block_cipher_uses_operation(operation)) {
        xcryptographic_block_cipher_context *context =
            xcryptographic_block_cipher_context_get(operation);
        XByteArrayView result;
        size_t expected = (context->operation.pendingSize + input_length) / 16u * 16u;

        if ((!input && input_length != 0) || input_length > INT64_MAX) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        if (operation->alg == PSA_ALG_XTS) {
            size_t total = context->operation.pendingSize + input_length;
            size_t keep = 16u + (total % 16u != 0 ? total % 16u : 0u);
            expected = total > keep ? ((total - keep) / 16u) * 16u : 0u;
        }
        if (output_size < expected) return PSA_ERROR_BUFFER_TOO_SMALL;
        result = XCryptographic_blockCipherUpdateInto(
            &context->operation, (char *)output, output_size,
            XByteArrayView_create_data(input, (int64_t)input_length));
        if (!result.m_data) return PSA_ERROR_BAD_STATE;
        *output_length = (size_t)result.m_size;
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER || MBEDTLS_USE_XCRYPTOGRAPHIC_XTS */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG)
    if (xcryptographic_ccm_star_no_tag_uses_operation(operation)) {
        xcryptographic_ccm_star_no_tag_context *context =
            xcryptographic_ccm_star_no_tag_context_get(operation);
        XByteArrayView result;
        if (input_length > INT64_MAX || (!input && input_length != 0)) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        if (input_length == 0) {
            *output_length = 0;
            return PSA_SUCCESS;
        }
        if (!output || output_size < input_length) {
            return PSA_ERROR_BUFFER_TOO_SMALL;
        }
        result = XCryptographic_aesCcmStarNoTagUpdateInto(
            &context->operation, (char *)output, output_size,
            XByteArrayView_create_data(input, (int64_t)input_length));
        if (!result.m_data) return PSA_ERROR_BAD_STATE;
        *output_length = input_length;
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20)
    if (xcryptographic_chacha20_uses_operation(operation)) {
        xcryptographic_chacha20_context *context =
            xcryptographic_chacha20_context_get(operation);
        XByteArrayView result;
        if (input_length > INT64_MAX || (!input && input_length != 0)) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        if (input_length == 0) {
            *output_length = 0;
            return PSA_SUCCESS;
        }
        if (!output || output_size < input_length) {
            return PSA_ERROR_BUFFER_TOO_SMALL;
        }
        result = XCryptographic_chacha20UpdateInto(
            &context->operation, (char *)output, output_size,
            XByteArrayView_create_data(input, (int64_t)input_length));
        if (!result.m_data) return PSA_ERROR_BAD_STATE;
        *output_length = input_length;
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20 */

    if (!PSA_ALG_IS_STREAM_CIPHER(operation->alg)) {
        /* Take the unprocessed partial block left over from previous
         * update calls, if any, plus the input to this call. Remove
         * the last partial block, if any. You get the data that will be
         * output in this call. */
        expected_output_size =
            (operation->ctx.cipher.unprocessed_len + input_length)
            / operation->block_length * operation->block_length;
    } else {
        expected_output_size = input_length;
    }

    if (output_size < expected_output_size) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }

#if defined(MBEDTLS_PSA_BUILTIN_ALG_ECB_NO_PADDING)
    if (operation->alg == PSA_ALG_ECB_NO_PADDING) {
        /* mbedtls_cipher_update has an API inconsistency: it will only
         * process a single block at a time in ECB mode. Abstract away that
         * inconsistency here to match the PSA API behaviour. */
        status = psa_cipher_update_ecb(&operation->ctx.cipher,
                                       input,
                                       input_length,
                                       output,
                                       output_length);
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_ECB_NO_PADDING */
    if (input_length == 0) {
        /* There is no input, nothing to be done */
        *output_length = 0;
        status = PSA_SUCCESS;
    } else {
        status = mbedtls_to_psa_error(
            mbedtls_cipher_update(&operation->ctx.cipher, input,
                                  input_length, output, output_length));

        if (*output_length > output_size) {
            return PSA_ERROR_CORRUPTION_DETECTED;
        }
    }

    return status;
}

psa_status_t mbedtls_psa_cipher_finish(
    mbedtls_psa_cipher_operation_t *operation,
    uint8_t *output, size_t output_size, size_t *output_length)
{
    psa_status_t status = PSA_ERROR_GENERIC_ERROR;
    size_t invalid_padding = 0;

    /* We will copy output_size bytes from temp_output_buffer to the
     * output buffer. We can't use *output_length to determine how
     * much to copy because we must not leak that value through timing
     * when doing decryption with unpadding. But the underlying function
     * is not guaranteed to write beyond *output_length. To ensure we don't
     * leak the former content of the stack to the caller, wipe that
     * former content. */
    uint8_t temp_output_buffer[MBEDTLS_MAX_BLOCK_LENGTH] = { 0 };

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CTR)
    if (xcryptographic_ctr_uses_operation(operation)) {
        (void) output;
        (void) output_size;
        *output_length = 0;
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CTR */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER) || \
    defined(MBEDTLS_USE_XCRYPTOGRAPHIC_XTS)
    if (xcryptographic_block_cipher_uses_operation(operation)) {
        xcryptographic_block_cipher_context *context =
            xcryptographic_block_cipher_context_get(operation);
        XByteArrayView result;
        if ((operation->alg == PSA_ALG_CBC_PKCS7 || operation->alg == PSA_ALG_XTS) &&
            output_size < context->operation.pendingSize) {
            return PSA_ERROR_BUFFER_TOO_SMALL;
        }
        result = XCryptographic_blockCipherFinishInto(
            &context->operation, (char *)output, output_size);
        if (!result.m_data) return PSA_ERROR_INVALID_ARGUMENT;
        *output_length = (size_t)result.m_size;
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER || MBEDTLS_USE_XCRYPTOGRAPHIC_XTS */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG)
    if (xcryptographic_ccm_star_no_tag_uses_operation(operation)) {
        xcryptographic_ccm_star_no_tag_context *context =
            xcryptographic_ccm_star_no_tag_context_get(operation);
        XCryptographic_aesCcmStarNoTagAbort(&context->operation);
        *output_length = 0;
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20)
    if (xcryptographic_chacha20_uses_operation(operation)) {
        *output_length = 0;
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20 */
    if (output_size > sizeof(temp_output_buffer)) {
        output_size = sizeof(temp_output_buffer);
    }

    if (operation->ctx.cipher.unprocessed_len != 0) {
        if (operation->alg == PSA_ALG_ECB_NO_PADDING ||
            operation->alg == PSA_ALG_CBC_NO_PADDING) {
            status = PSA_ERROR_INVALID_ARGUMENT;
            goto exit;
        }
    }

    status = mbedtls_to_psa_error(
        mbedtls_cipher_finish_padded(&operation->ctx.cipher,
                                     temp_output_buffer,
                                     output_length,
                                     &invalid_padding));
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    if (output_size == 0) {
        ; /* Nothing to copy. Note that output may be NULL in this case. */
    } else {
        /* Do not use the value of *output_length to determine how much
         * to copy. When decrypting a padded cipher, the output length is
         * sensitive, and leaking it could allow a padding oracle attack. */
        memcpy(output, temp_output_buffer, output_size);
    }

    status = mbedtls_ct_error_if_else_0(invalid_padding,
                                        PSA_ERROR_INVALID_PADDING);
    mbedtls_ct_condition_t buffer_too_small =
        mbedtls_ct_uint_lt(output_size, *output_length);
    status = mbedtls_ct_error_if(buffer_too_small,
                                 PSA_ERROR_BUFFER_TOO_SMALL,
                                 status);

exit:
    mbedtls_platform_zeroize(temp_output_buffer,
                             sizeof(temp_output_buffer));
    return status;
}

psa_status_t mbedtls_psa_cipher_abort(
    mbedtls_psa_cipher_operation_t *operation)
{
    /* Sanity check (shouldn't happen: operation->alg should
     * always have been initialized to a valid value). */
    if (!PSA_ALG_IS_CIPHER(operation->alg)) {
        return PSA_ERROR_BAD_STATE;
    }

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CTR)
    if (xcryptographic_ctr_uses_operation(operation)) {
        xcryptographic_ctr_context *context = xcryptographic_ctr_context_get(operation);
        XCryptographic_aesCtrAbort(&context->operation);
        XCryptographic_destroyKey(&context->key);
        XFree_System(context);
        xcryptographic_ctr_context_set(operation, NULL);
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CTR */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER) || \
    defined(MBEDTLS_USE_XCRYPTOGRAPHIC_XTS)
    if (xcryptographic_block_cipher_uses_operation(operation)) {
        xcryptographic_block_cipher_context *context =
            xcryptographic_block_cipher_context_get(operation);
        XCryptographic_blockCipherAbort(&context->operation);
        XFree_System(context);
        xcryptographic_block_cipher_context_set(operation, NULL);
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_BLOCK_CIPHER || MBEDTLS_USE_XCRYPTOGRAPHIC_XTS */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG)
    if (xcryptographic_ccm_star_no_tag_uses_operation(operation)) {
        xcryptographic_ccm_star_no_tag_context *context =
            xcryptographic_ccm_star_no_tag_context_get(operation);
        XCryptographic_aesCcmStarNoTagAbort(&context->operation);
        XFree_System(context);
        xcryptographic_ccm_star_no_tag_context_set(operation, NULL);
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CCM_STAR_NO_TAG */

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20)
    if (xcryptographic_chacha20_uses_operation(operation)) {
        xcryptographic_chacha20_context *context =
            xcryptographic_chacha20_context_get(operation);
        XCryptographic_chacha20Abort(&context->operation);
        XFree_System(context);
        xcryptographic_chacha20_context_set(operation, NULL);
        return PSA_SUCCESS;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_CHACHA20 */

    mbedtls_cipher_free(&operation->ctx.cipher);

    return PSA_SUCCESS;
}

psa_status_t mbedtls_psa_cipher_encrypt(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer,
    size_t key_buffer_size,
    psa_algorithm_t alg,
    const uint8_t *iv,
    size_t iv_length,
    const uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t output_size,
    size_t *output_length)
{
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    mbedtls_psa_cipher_operation_t operation = MBEDTLS_PSA_CIPHER_OPERATION_INIT;
    size_t update_output_length, finish_output_length;

    status = mbedtls_psa_cipher_encrypt_setup(&operation, attributes,
                                              key_buffer, key_buffer_size,
                                              alg);
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    if (iv_length > 0) {
        status = mbedtls_psa_cipher_set_iv(&operation, iv, iv_length);
        if (status != PSA_SUCCESS) {
            goto exit;
        }
    }

    status = mbedtls_psa_cipher_update(&operation, input, input_length,
                                       output, output_size,
                                       &update_output_length);
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    status = mbedtls_psa_cipher_finish(
        &operation,
        mbedtls_buffer_offset(output, update_output_length),
        output_size - update_output_length, &finish_output_length);
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    *output_length = update_output_length + finish_output_length;

exit:
    if (status == PSA_SUCCESS) {
        status = mbedtls_psa_cipher_abort(&operation);
    } else {
        mbedtls_psa_cipher_abort(&operation);
    }

    return status;
}

psa_status_t mbedtls_psa_cipher_decrypt(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer,
    size_t key_buffer_size,
    psa_algorithm_t alg,
    const uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t output_size,
    size_t *output_length)
{
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    mbedtls_psa_cipher_operation_t operation = MBEDTLS_PSA_CIPHER_OPERATION_INIT;
    size_t olength, accumulated_length;

    status = mbedtls_psa_cipher_decrypt_setup(&operation, attributes,
                                              key_buffer, key_buffer_size,
                                              alg);
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    if (operation.iv_length > 0) {
        status = mbedtls_psa_cipher_set_iv(&operation,
                                           input, operation.iv_length);
        if (status != PSA_SUCCESS) {
            goto exit;
        }
    }

    status = mbedtls_psa_cipher_update(
        &operation,
        mbedtls_buffer_offset_const(input, operation.iv_length),
        input_length - operation.iv_length,
        output, output_size, &olength);
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    accumulated_length = olength;

    status = mbedtls_psa_cipher_finish(
        &operation,
        mbedtls_buffer_offset(output, accumulated_length),
        output_size - accumulated_length, &olength);

    *output_length = accumulated_length + olength;

exit:
    /* C99 doesn't allow a declaration to follow a label */;
    psa_status_t abort_status = mbedtls_psa_cipher_abort(&operation);
    /* Normally abort shouldn't fail unless the operation is in a bad
     * state, in which case we'd expect finish to fail with the same error.
     * So it doesn't matter much which call's error code we pick when both
     * fail. However, in unauthenticated decryption specifically, the
     * distinction between PSA_SUCCESS and PSA_ERROR_INVALID_PADDING is
     * security-sensitive (risk of a padding oracle attack), so here we
     * must not have a code path that depends on the value of status. */
    if (abort_status != PSA_SUCCESS) {
        status = abort_status;
    }

    return status;
}
#endif /* MBEDTLS_PSA_BUILTIN_CIPHER */

#endif /* MBEDTLS_PSA_CRYPTO_C */
