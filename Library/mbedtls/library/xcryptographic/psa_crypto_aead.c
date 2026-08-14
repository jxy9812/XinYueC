/*
 *  PSA AEAD entry points
 */
/*
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
 */

#include "tf_psa_crypto_common.h"

#if defined(MBEDTLS_PSA_CRYPTO_C)

#include "psa_crypto_aead.h"
#include "psa_crypto_core.h"
#include "psa_crypto_cipher.h"

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
#include "XCryptographic.h"
#include "XMemory.h"
#include <limits.h>
#endif

#include <string.h>
#include "mbedtls/platform.h"

#include "mbedtls/private/ccm.h"
#include "mbedtls/private/chachapoly.h"
#include "mbedtls/private/cipher.h"
#include "mbedtls/private/gcm.h"
#include "mbedtls/private/error_common.h"

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
static XCryptographic_AeadOperation *xcryptographic_aead_context_get(
    const mbedtls_psa_aead_operation_t *operation)
{
    XCryptographic_AeadOperation *context = NULL;
    memcpy(&context, &operation->ctx.xcryptographic, sizeof(context));
    return context;
}

static void xcryptographic_aead_context_set(mbedtls_psa_aead_operation_t *operation,
                                            XCryptographic_AeadOperation *context)
{
    memset(&operation->ctx, 0, sizeof(operation->ctx));
    memcpy(&operation->ctx.xcryptographic, &context, sizeof(context));
}
#endif

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
/*
 * TLS record protection and session tickets use the PSA single-part AEAD
 * API. Keep that public PSA surface intact while sourcing the actual AEAD
 * implementation from XinYueC's XCryptographic module.
 */
static psa_status_t xcryptographic_aead_algorithm(
    psa_key_type_t key_type, psa_algorithm_t alg,
    XCryptographic_AeadAlgorithm *result, size_t *tag_length)
{
    if (PSA_ALG_AEAD_WITH_SHORTENED_TAG(alg, 0) ==
        PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_GCM, 0)) {
        if (key_type == PSA_KEY_TYPE_AES)
            *result = XCryptographic_AeadAlgorithm_AesGcm;
        else if (key_type == PSA_KEY_TYPE_ARIA)
            *result = XCryptographic_AeadAlgorithm_AriaGcm;
        else if (key_type == PSA_KEY_TYPE_CAMELLIA)
            *result = XCryptographic_AeadAlgorithm_CamelliaGcm;
        else
            return PSA_ERROR_NOT_SUPPORTED;
    } else if (PSA_ALG_AEAD_WITH_SHORTENED_TAG(alg, 0) ==
               PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 0)) {
        if (key_type == PSA_KEY_TYPE_AES)
            *result = XCryptographic_AeadAlgorithm_AesCcm;
        else if (key_type == PSA_KEY_TYPE_ARIA)
            *result = XCryptographic_AeadAlgorithm_AriaCcm;
        else if (key_type == PSA_KEY_TYPE_CAMELLIA)
            *result = XCryptographic_AeadAlgorithm_CamelliaCcm;
        else
            return PSA_ERROR_NOT_SUPPORTED;
    } else if (alg == PSA_ALG_CHACHA20_POLY1305) {
        if (key_type != PSA_KEY_TYPE_CHACHA20)
            return PSA_ERROR_NOT_SUPPORTED;
        *result = XCryptographic_AeadAlgorithm_ChaCha20Poly1305;
    } else {
        return PSA_ERROR_NOT_SUPPORTED;
    }
    *tag_length = PSA_ALG_AEAD_GET_TAG_LENGTH(alg);
    return PSA_SUCCESS;
}

static bool xcryptographic_aead_view(const uint8_t *data, size_t size,
                                     XByteArrayView *result)
{
    if ((!data && size != 0) || size > INT64_MAX) {
        return false;
    }
    *result = XByteArrayView_create_data(data, (int64_t) size);
    return true;
}

static psa_status_t xcryptographic_aead_encrypt(
    psa_key_type_t key_type, const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg,
    const uint8_t *nonce, size_t nonce_length,
    const uint8_t *additional_data, size_t additional_data_length,
    const uint8_t *plaintext, size_t plaintext_length,
    uint8_t *ciphertext, size_t ciphertext_size, size_t *ciphertext_length)
{
    XCryptographic_AeadAlgorithm algorithm;
    XCryptographic_Key key;
    XByteArrayView key_view, nonce_view, additional_data_view, plaintext_view;
    XByteArrayView encrypted;
    size_t tag_length;
    psa_status_t status;

    if (!ciphertext_length || !ciphertext) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    status = xcryptographic_aead_algorithm(key_type, alg, &algorithm, &tag_length);
    if (status != PSA_SUCCESS) {
        return status;
    }
    if (plaintext_length > SIZE_MAX - tag_length ||
        ciphertext_size < plaintext_length + tag_length ||
        !xcryptographic_aead_view(key_buffer, key_buffer_size, &key_view) ||
        !xcryptographic_aead_view(nonce, nonce_length, &nonce_view) ||
        !xcryptographic_aead_view(additional_data, additional_data_length, &additional_data_view) ||
        !xcryptographic_aead_view(plaintext, plaintext_length, &plaintext_view) ||
        !XCryptographic_aeadImportKey(algorithm, key_view, &key)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    encrypted = XCryptographic_aeadEncryptInto(
        key, nonce_view, additional_data_view, plaintext_view, tag_length,
        (char *) ciphertext, ciphertext_size);
    XCryptographic_destroyKey(&key);
    if (!encrypted.m_data) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    *ciphertext_length = (size_t) encrypted.m_size;
    return PSA_SUCCESS;
}

static psa_status_t xcryptographic_aead_decrypt(
    psa_key_type_t key_type, const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg,
    const uint8_t *nonce, size_t nonce_length,
    const uint8_t *additional_data, size_t additional_data_length,
    const uint8_t *ciphertext, size_t ciphertext_length,
    uint8_t *plaintext, size_t plaintext_size, size_t *plaintext_length)
{
    XCryptographic_AeadAlgorithm algorithm;
    XCryptographic_Key key;
    XByteArrayView key_view, nonce_view, additional_data_view, ciphertext_view;
    XByteArrayView decrypted;
    size_t tag_length;
    psa_status_t status;

    if (!plaintext_length || !plaintext) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    status = xcryptographic_aead_algorithm(key_type, alg, &algorithm, &tag_length);
    if (status != PSA_SUCCESS) {
        return status;
    }
    if (ciphertext_length < tag_length || plaintext_size < ciphertext_length - tag_length ||
        !xcryptographic_aead_view(key_buffer, key_buffer_size, &key_view) ||
        !xcryptographic_aead_view(nonce, nonce_length, &nonce_view) ||
        !xcryptographic_aead_view(additional_data, additional_data_length, &additional_data_view) ||
        !xcryptographic_aead_view(ciphertext, ciphertext_length, &ciphertext_view) ||
        !XCryptographic_aeadImportKey(algorithm, key_view, &key)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    decrypted = XCryptographic_aeadDecryptInto(
        key, nonce_view, additional_data_view, ciphertext_view, tag_length,
        (char *) plaintext, plaintext_size);
    XCryptographic_destroyKey(&key);
    if (!decrypted.m_data) {
        return PSA_ERROR_INVALID_SIGNATURE;
    }
    *plaintext_length = (size_t) decrypted.m_size;
    return PSA_SUCCESS;
}
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD */

#if defined(MBEDTLS_PSA_BUILTIN_AEAD)
static psa_status_t psa_aead_setup(
    mbedtls_psa_aead_operation_t *operation,
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer,
    size_t key_buffer_size,
    psa_algorithm_t alg)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
    XCryptographic_AeadAlgorithm algorithm;
    XCryptographic_AeadOperation *context;
    XCryptographic_Key key;
    XByteArrayView keyView;
    size_t tagLength;
    psa_status_t status;

    status = xcryptographic_aead_algorithm(attributes->type, alg, &algorithm, &tagLength);
    if (status != PSA_SUCCESS) return status;
    keyView = XByteArrayView_create_data(key_buffer, (int64_t)key_buffer_size);
    if (!XCryptographic_aeadImportKey(algorithm, keyView, &key)) return PSA_ERROR_INVALID_ARGUMENT;
    context = (XCryptographic_AeadOperation *)XMalloc_System(sizeof(*context));
    if (!context) {
        XCryptographic_destroyKey(&key);
        return PSA_ERROR_INSUFFICIENT_MEMORY;
    }
    if (!XCryptographic_aeadSetup(context, algorithm, key, false) ||
        !XCryptographic_aeadSetTagSize(context, tagLength)) {
        XCryptographic_aeadAbort(context);
        XFree_System(context);
        XCryptographic_destroyKey(&key);
        return PSA_ERROR_NOT_SUPPORTED;
    }
    XCryptographic_destroyKey(&key);
    xcryptographic_aead_context_set(operation, context);
    operation->alg = PSA_ALG_AEAD_WITH_SHORTENED_TAG(alg, 0);
    operation->key_type = attributes->type;
    operation->tag_length = tagLength;
    return PSA_SUCCESS;
#else
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    mbedtls_cipher_id_t cipher_id;
    mbedtls_cipher_mode_t mode;
    (void) key_buffer_size;

    status = mbedtls_cipher_values_from_psa(alg, attributes->type,
                                            &mode, &cipher_id);
    if (status != PSA_SUCCESS) {
        return status;
    }

    switch (PSA_ALG_AEAD_WITH_SHORTENED_TAG(alg, 0)) {
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CCM)
        case PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 0):
            operation->alg = PSA_ALG_CCM;
            /* CCM allows the following tag lengths: 4, 6, 8, 10, 12, 14, 16.
             * The call to mbedtls_ccm_encrypt_and_tag or
             * mbedtls_ccm_auth_decrypt will validate the tag length. */
            if (PSA_BLOCK_CIPHER_BLOCK_LENGTH(attributes->type) != 16) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }

            mbedtls_ccm_init(&operation->ctx.ccm);
            status = mbedtls_to_psa_error(
                mbedtls_ccm_setkey(&operation->ctx.ccm, cipher_id,
                                   key_buffer, (unsigned int) attributes->bits));
            if (status != PSA_SUCCESS) {
                return status;
            }
            break;
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CCM */

#if defined(MBEDTLS_PSA_BUILTIN_ALG_GCM)
        case PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_GCM, 0):
            operation->alg = PSA_ALG_GCM;
            /* GCM allows the following tag lengths: 4, 8, 12, 13, 14, 15, 16.
             * The call to mbedtls_gcm_crypt_and_tag or
             * mbedtls_gcm_auth_decrypt will validate the tag length. */
            if (PSA_BLOCK_CIPHER_BLOCK_LENGTH(attributes->type) != 16) {
                return PSA_ERROR_INVALID_ARGUMENT;
            }

            mbedtls_gcm_init(&operation->ctx.gcm);
            status = mbedtls_to_psa_error(
                mbedtls_gcm_setkey(&operation->ctx.gcm, cipher_id,
                                   key_buffer, (unsigned int) attributes->bits));
            if (status != PSA_SUCCESS) {
                return status;
            }
            break;
#endif /* MBEDTLS_PSA_BUILTIN_ALG_GCM */

#if defined(MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305)
        case PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CHACHA20_POLY1305, 0):
            operation->alg = PSA_ALG_CHACHA20_POLY1305;
            /* We only support the default tag length. */
            if (alg != PSA_ALG_CHACHA20_POLY1305) {
                return PSA_ERROR_NOT_SUPPORTED;
            }

            mbedtls_chachapoly_init(&operation->ctx.chachapoly);
            status = mbedtls_to_psa_error(
                mbedtls_chachapoly_setkey(&operation->ctx.chachapoly,
                                          key_buffer));
            if (status != PSA_SUCCESS) {
                return status;
            }
            break;
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305 */

        default:
            (void) status;
            (void) key_buffer;
            return PSA_ERROR_NOT_SUPPORTED;
    }

    operation->key_type = psa_get_key_type(attributes);

    operation->tag_length = PSA_ALG_AEAD_GET_TAG_LENGTH(alg);

    return PSA_SUCCESS;
#endif
}

psa_status_t mbedtls_psa_aead_encrypt(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg,
    const uint8_t *nonce, size_t nonce_length,
    const uint8_t *additional_data, size_t additional_data_length,
    const uint8_t *plaintext, size_t plaintext_length,
    uint8_t *ciphertext, size_t ciphertext_size, size_t *ciphertext_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
    return xcryptographic_aead_encrypt(attributes->type, key_buffer, key_buffer_size, alg,
                                       nonce, nonce_length,
                                       additional_data, additional_data_length,
                                       plaintext, plaintext_length,
                                       ciphertext, ciphertext_size, ciphertext_length);
#else
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    mbedtls_psa_aead_operation_t operation = MBEDTLS_PSA_AEAD_OPERATION_INIT;
    uint8_t *tag;

    status = psa_aead_setup(&operation, attributes, key_buffer,
                            key_buffer_size, alg);

    if (status != PSA_SUCCESS) {
        goto exit;
    }

    /* For all currently supported modes, the tag is at the end of the
     * ciphertext. */
    if (ciphertext_size < (plaintext_length + operation.tag_length)) {
        status = PSA_ERROR_BUFFER_TOO_SMALL;
        goto exit;
    }
    tag = ciphertext + plaintext_length;

#if defined(MBEDTLS_PSA_BUILTIN_ALG_CCM)
    if (operation.alg == PSA_ALG_CCM) {
        status = mbedtls_to_psa_error(
            mbedtls_ccm_encrypt_and_tag(&operation.ctx.ccm,
                                        plaintext_length,
                                        nonce, nonce_length,
                                        additional_data,
                                        additional_data_length,
                                        plaintext, ciphertext,
                                        tag, operation.tag_length));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_GCM)
    if (operation.alg == PSA_ALG_GCM) {
        status = mbedtls_to_psa_error(
            mbedtls_gcm_crypt_and_tag(&operation.ctx.gcm,
                                      MBEDTLS_GCM_ENCRYPT,
                                      plaintext_length,
                                      nonce, nonce_length,
                                      additional_data, additional_data_length,
                                      plaintext, ciphertext,
                                      operation.tag_length, tag));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_GCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305)
    if (operation.alg == PSA_ALG_CHACHA20_POLY1305) {
        if (operation.tag_length != 16) {
            status = PSA_ERROR_NOT_SUPPORTED;
            goto exit;
        }
        status = mbedtls_to_psa_error(
            mbedtls_chachapoly_encrypt_and_tag(&operation.ctx.chachapoly,
                                               plaintext_length,
                                               nonce,
                                               additional_data,
                                               additional_data_length,
                                               plaintext,
                                               ciphertext,
                                               tag));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305 */
    {
        (void) tag;
        (void) nonce;
        (void) nonce_length;
        (void) additional_data;
        (void) additional_data_length;
        (void) plaintext;
        return PSA_ERROR_NOT_SUPPORTED;
    }

    if (status == PSA_SUCCESS) {
        *ciphertext_length = plaintext_length + operation.tag_length;
    }

exit:
    mbedtls_psa_aead_abort(&operation);

    return status;
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD */
}

/* Locate the tag in a ciphertext buffer containing the encrypted data
 * followed by the tag. Return the length of the part preceding the tag in
 * *plaintext_length. This is the size of the plaintext in modes where
 * the encrypted data has the same size as the plaintext, such as
 * CCM and GCM. */
static psa_status_t psa_aead_unpadded_locate_tag(size_t tag_length,
                                                 const uint8_t *ciphertext,
                                                 size_t ciphertext_length,
                                                 size_t plaintext_size,
                                                 const uint8_t **p_tag)
{
    size_t payload_length;
    if (tag_length > ciphertext_length) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    payload_length = ciphertext_length - tag_length;
    if (payload_length > plaintext_size) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }
    *p_tag = ciphertext + payload_length;
    return PSA_SUCCESS;
}

psa_status_t mbedtls_psa_aead_decrypt(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg,
    const uint8_t *nonce, size_t nonce_length,
    const uint8_t *additional_data, size_t additional_data_length,
    const uint8_t *ciphertext, size_t ciphertext_length,
    uint8_t *plaintext, size_t plaintext_size, size_t *plaintext_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
    return xcryptographic_aead_decrypt(attributes->type, key_buffer, key_buffer_size, alg,
                                       nonce, nonce_length,
                                       additional_data, additional_data_length,
                                       ciphertext, ciphertext_length,
                                       plaintext, plaintext_size, plaintext_length);
#else
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    mbedtls_psa_aead_operation_t operation = MBEDTLS_PSA_AEAD_OPERATION_INIT;
    const uint8_t *tag = NULL;

    status = psa_aead_setup(&operation, attributes, key_buffer,
                            key_buffer_size, alg);

    if (status != PSA_SUCCESS) {
        goto exit;
    }

    status = psa_aead_unpadded_locate_tag(operation.tag_length,
                                          ciphertext, ciphertext_length,
                                          plaintext_size, &tag);
    if (status != PSA_SUCCESS) {
        goto exit;
    }

#if defined(MBEDTLS_PSA_BUILTIN_ALG_CCM)
    if (operation.alg == PSA_ALG_CCM) {
        status = mbedtls_to_psa_error(
            mbedtls_ccm_auth_decrypt(&operation.ctx.ccm,
                                     ciphertext_length - operation.tag_length,
                                     nonce, nonce_length,
                                     additional_data,
                                     additional_data_length,
                                     ciphertext, plaintext,
                                     tag, operation.tag_length));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_GCM)
    if (operation.alg == PSA_ALG_GCM) {
        status = mbedtls_to_psa_error(
            mbedtls_gcm_auth_decrypt(&operation.ctx.gcm,
                                     ciphertext_length - operation.tag_length,
                                     nonce, nonce_length,
                                     additional_data,
                                     additional_data_length,
                                     tag, operation.tag_length,
                                     ciphertext, plaintext));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_GCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305)
    if (operation.alg == PSA_ALG_CHACHA20_POLY1305) {
        if (operation.tag_length != 16) {
            status = PSA_ERROR_NOT_SUPPORTED;
            goto exit;
        }
        status = mbedtls_to_psa_error(
            mbedtls_chachapoly_auth_decrypt(&operation.ctx.chachapoly,
                                            ciphertext_length - operation.tag_length,
                                            nonce,
                                            additional_data,
                                            additional_data_length,
                                            tag,
                                            ciphertext,
                                            plaintext));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305 */
    {
        (void) nonce;
        (void) nonce_length;
        (void) additional_data;
        (void) additional_data_length;
        (void) plaintext;
        return PSA_ERROR_NOT_SUPPORTED;
    }

    if (status == PSA_SUCCESS) {
        *plaintext_length = ciphertext_length - operation.tag_length;
    }

exit:
    mbedtls_psa_aead_abort(&operation);

    return status;
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD */
}

/* Set the key and algorithm for a multipart authenticated encryption
 * operation. */
psa_status_t mbedtls_psa_aead_encrypt_setup(
    mbedtls_psa_aead_operation_t *operation,
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer,
    size_t key_buffer_size,
    psa_algorithm_t alg)
{
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;

    status = psa_aead_setup(operation, attributes, key_buffer,
                            key_buffer_size, alg);

    if (status == PSA_SUCCESS) {
        operation->is_encrypt = 1;
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
        xcryptographic_aead_context_get(operation)->encrypt = true;
#endif
    }

    return status;
}

/* Set the key and algorithm for a multipart authenticated decryption
 * operation. */
psa_status_t mbedtls_psa_aead_decrypt_setup(
    mbedtls_psa_aead_operation_t *operation,
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer,
    size_t key_buffer_size,
    psa_algorithm_t alg)
{
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;

    status = psa_aead_setup(operation, attributes, key_buffer,
                            key_buffer_size, alg);

    if (status == PSA_SUCCESS) {
        operation->is_encrypt = 0;
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
        xcryptographic_aead_context_get(operation)->encrypt = false;
#endif
    }

    return status;
}

/* Set a nonce for the multipart AEAD operation*/
psa_status_t mbedtls_psa_aead_set_nonce(
    mbedtls_psa_aead_operation_t *operation,
    const uint8_t *nonce,
    size_t nonce_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
    XCryptographic_AeadOperation *context = xcryptographic_aead_context_get(operation);
    if (!context || !XCryptographic_aeadSetNonce(context,
            XByteArrayView_create_data(nonce, (int64_t)nonce_length)))
        return PSA_ERROR_INVALID_ARGUMENT;
    return PSA_SUCCESS;
#else
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;

#if defined(MBEDTLS_PSA_BUILTIN_ALG_GCM)
    if (operation->alg == PSA_ALG_GCM) {
        status = mbedtls_to_psa_error(
            mbedtls_gcm_starts(&operation->ctx.gcm,
                               operation->is_encrypt ?
                               MBEDTLS_GCM_ENCRYPT : MBEDTLS_GCM_DECRYPT,
                               nonce,
                               nonce_length));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_GCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CCM)
    if (operation->alg == PSA_ALG_CCM) {
        status = mbedtls_to_psa_error(
            mbedtls_ccm_starts(&operation->ctx.ccm,
                               operation->is_encrypt ?
                               MBEDTLS_CCM_ENCRYPT : MBEDTLS_CCM_DECRYPT,
                               nonce,
                               nonce_length));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305)
    if (operation->alg == PSA_ALG_CHACHA20_POLY1305) {
        /* Note - ChaChaPoly allows an 8 byte nonce, but we would have to
         * allocate a buffer in the operation, copy the nonce to it and pad
         * it, so for now check the nonce is 12 bytes, as
         * mbedtls_chachapoly_starts() assumes it can read 12 bytes from the
         * passed in buffer. */
        if (nonce_length != 12) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }

        status = mbedtls_to_psa_error(
            mbedtls_chachapoly_starts(&operation->ctx.chachapoly,
                                      nonce,
                                      operation->is_encrypt ?
                                      MBEDTLS_CHACHAPOLY_ENCRYPT :
                                      MBEDTLS_CHACHAPOLY_DECRYPT));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305 */
    {
        (void) operation;
        (void) nonce;
        (void) nonce_length;

        return PSA_ERROR_NOT_SUPPORTED;
    }

    return status;
#endif
}

/* Declare the lengths of the message and additional data for AEAD. */
psa_status_t mbedtls_psa_aead_set_lengths(
    mbedtls_psa_aead_operation_t *operation,
    size_t ad_length,
    size_t plaintext_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
    XCryptographic_AeadOperation *context = xcryptographic_aead_context_get(operation);
    if (!context || !XCryptographic_aeadSetLengths(context, ad_length, plaintext_length))
        return PSA_ERROR_INVALID_ARGUMENT;
    return PSA_SUCCESS;
#else
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CCM)
    if (operation->alg == PSA_ALG_CCM) {
        return mbedtls_to_psa_error(
            mbedtls_ccm_set_lengths(&operation->ctx.ccm,
                                    ad_length,
                                    plaintext_length,
                                    operation->tag_length));

    }
#else /* MBEDTLS_PSA_BUILTIN_ALG_CCM */
    (void) operation;
    (void) ad_length;
    (void) plaintext_length;
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CCM */

    return PSA_SUCCESS;
#endif
}

/* Pass additional data to an active multipart AEAD operation. */
psa_status_t mbedtls_psa_aead_update_ad(
    mbedtls_psa_aead_operation_t *operation,
    const uint8_t *input,
    size_t input_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
    XCryptographic_AeadOperation *context = xcryptographic_aead_context_get(operation);
    if (!context || !XCryptographic_aeadUpdateAssociatedData(context,
            XByteArrayView_create_data(input, (int64_t)input_length)))
        return PSA_ERROR_INVALID_ARGUMENT;
    return PSA_SUCCESS;
#else
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;

#if defined(MBEDTLS_PSA_BUILTIN_ALG_GCM)
    if (operation->alg == PSA_ALG_GCM) {
        status = mbedtls_to_psa_error(
            mbedtls_gcm_update_ad(&operation->ctx.gcm, input, input_length));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_GCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CCM)
    if (operation->alg == PSA_ALG_CCM) {
        status = mbedtls_to_psa_error(
            mbedtls_ccm_update_ad(&operation->ctx.ccm, input, input_length));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305)
    if (operation->alg == PSA_ALG_CHACHA20_POLY1305) {
        status = mbedtls_to_psa_error(
            mbedtls_chachapoly_update_aad(&operation->ctx.chachapoly,
                                          input,
                                          input_length));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305 */
    {
        (void) operation;
        (void) input;
        (void) input_length;

        return PSA_ERROR_NOT_SUPPORTED;
    }

    return status;
#endif
}

/* Encrypt or decrypt a message fragment in an active multipart AEAD
 * operation.*/
psa_status_t mbedtls_psa_aead_update(
    mbedtls_psa_aead_operation_t *operation,
    const uint8_t *input,
    size_t input_length,
    uint8_t *output,
    size_t output_size,
    size_t *output_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
    XCryptographic_AeadOperation *context = xcryptographic_aead_context_get(operation);
    XByteArrayView result;
    if (!context || !output_length)
        return PSA_ERROR_INVALID_ARGUMENT;
    result = XCryptographic_aeadUpdateInto(context, (char *)output, output_size,
        XByteArrayView_create_data(input, (int64_t)input_length));
    if (!result.m_data && input_length != 0) return PSA_ERROR_BUFFER_TOO_SMALL;
    *output_length = input_length;
    return PSA_SUCCESS;
#else
    size_t update_output_length;
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;

    update_output_length = input_length;

#if defined(MBEDTLS_PSA_BUILTIN_ALG_GCM)
    if (operation->alg == PSA_ALG_GCM) {
        status =  mbedtls_to_psa_error(
            mbedtls_gcm_update(&operation->ctx.gcm,
                               input, input_length,
                               output, output_size,
                               &update_output_length));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_GCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CCM)
    if (operation->alg == PSA_ALG_CCM) {
        if (output_size < input_length) {
            return PSA_ERROR_BUFFER_TOO_SMALL;
        }

        status = mbedtls_to_psa_error(
            mbedtls_ccm_update(&operation->ctx.ccm,
                               input, input_length,
                               output, output_size,
                               &update_output_length));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305)
    if (operation->alg == PSA_ALG_CHACHA20_POLY1305) {
        if (output_size < input_length) {
            return PSA_ERROR_BUFFER_TOO_SMALL;
        }

        status = mbedtls_to_psa_error(
            mbedtls_chachapoly_update(&operation->ctx.chachapoly,
                                      input_length,
                                      input,
                                      output));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305 */
    {
        (void) operation;
        (void) input;
        (void) output;
        (void) output_size;

        return PSA_ERROR_NOT_SUPPORTED;
    }

    if (status == PSA_SUCCESS) {
        *output_length = update_output_length;
    }

    return status;
#endif
}

/* Finish encrypting a message in a multipart AEAD operation. */
psa_status_t mbedtls_psa_aead_finish(
    mbedtls_psa_aead_operation_t *operation,
    uint8_t *ciphertext,
    size_t ciphertext_size,
    size_t *ciphertext_length,
    uint8_t *tag,
    size_t tag_size,
    size_t *tag_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
    XCryptographic_AeadOperation *context = xcryptographic_aead_context_get(operation);
    size_t outputLength = 0;
    if (!context || !ciphertext_length || !tag_length ||
        !XCryptographic_aeadFinishInto(context, (char *)ciphertext, ciphertext_size,
            &outputLength, (char *)tag, tag_size, tag_length))
        return PSA_ERROR_INVALID_ARGUMENT;
    *ciphertext_length = outputLength;
    return PSA_SUCCESS;
#else
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    size_t finish_output_size = 0;

    if (tag_size < operation->tag_length) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }

#if defined(MBEDTLS_PSA_BUILTIN_ALG_GCM)
    if (operation->alg == PSA_ALG_GCM) {
        status =  mbedtls_to_psa_error(
            mbedtls_gcm_finish(&operation->ctx.gcm,
                               ciphertext, ciphertext_size, ciphertext_length,
                               tag, operation->tag_length));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_GCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CCM)
    if (operation->alg == PSA_ALG_CCM) {
        /* tag must be big enough to store a tag of size passed into set
         * lengths. */
        if (tag_size < operation->tag_length) {
            return PSA_ERROR_BUFFER_TOO_SMALL;
        }

        status = mbedtls_to_psa_error(
            mbedtls_ccm_finish(&operation->ctx.ccm,
                               tag, operation->tag_length));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305)
    if (operation->alg == PSA_ALG_CHACHA20_POLY1305) {
        /* Belt and braces. Although the above tag_size check should have
         * already done this, if we later start supporting smaller tag sizes
         * for chachapoly, then passing a tag buffer smaller than 16 into here
         * could cause a buffer overflow, so better safe than sorry. */
        if (tag_size < 16) {
            return PSA_ERROR_BUFFER_TOO_SMALL;
        }

        status = mbedtls_to_psa_error(
            mbedtls_chachapoly_finish(&operation->ctx.chachapoly,
                                      tag));
    } else
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305 */
    {
        (void) ciphertext;
        (void) ciphertext_size;
        (void) ciphertext_length;
        (void) tag;
        (void) tag_size;
        (void) tag_length;

        return PSA_ERROR_NOT_SUPPORTED;
    }

    if (status == PSA_SUCCESS) {
        /* This will be zero for all supported algorithms currently, but left
         * here for future support. */
        *ciphertext_length = finish_output_size;
        *tag_length = operation->tag_length;
    }

    return status;
#endif
}

/* Abort an AEAD operation */
psa_status_t mbedtls_psa_aead_abort(
    mbedtls_psa_aead_operation_t *operation)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_AEAD)
    XCryptographic_AeadOperation *context = xcryptographic_aead_context_get(operation);
    if (context) {
        XCryptographic_aeadAbort(context);
        XFree_System(context);
    }
    xcryptographic_aead_context_set(operation, NULL);
    operation->alg = 0;
    operation->is_encrypt = 0;
    return PSA_SUCCESS;
#else
    switch (operation->alg) {
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CCM)
        case PSA_ALG_CCM:
            mbedtls_ccm_free(&operation->ctx.ccm);
            break;
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_GCM)
        case PSA_ALG_GCM:
            mbedtls_gcm_free(&operation->ctx.gcm);
            break;
#endif /* MBEDTLS_PSA_BUILTIN_ALG_GCM */
#if defined(MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305)
        case PSA_ALG_CHACHA20_POLY1305:
            mbedtls_chachapoly_free(&operation->ctx.chachapoly);
            break;
#endif /* MBEDTLS_PSA_BUILTIN_ALG_CHACHA20_POLY1305 */
    }

    operation->is_encrypt = 0;

    return PSA_SUCCESS;
#endif
}

#endif /* MBEDTLS_PSA_BUILTIN_AEAD */

#endif /* MBEDTLS_PSA_CRYPTO_C */
