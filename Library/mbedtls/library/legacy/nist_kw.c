/*
 *  NIST SP 800-38F key wrapping (KW/KWP) backed by XCryptographic.
 *
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
 */

#include "tf_psa_crypto_common.h"

#if defined(MBEDTLS_NIST_KW_C)

#include "mbedtls/nist_kw.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/private/error_common.h"
#include "mbedtls/constant_time.h"

#include <stdint.h>
#include <string.h>

#include "mbedtls/platform.h"
#include "psa/crypto.h"
#include "psa_crypto_core.h"
#include "psa_crypto_slot_management.h"
#include "XCryptographic.h"

#define KW_SEMIBLOCK_LENGTH    8

static int get_key_bytes(mbedtls_svc_key_id_t key,
                         unsigned char *key_buffer, size_t key_buffer_size,
                         size_t *key_length)
{
    psa_key_slot_t *slot = NULL;
    psa_key_attributes_t attributes;
    psa_key_type_t type;
    psa_status_t status;

    status = psa_get_and_lock_key_slot(key, &slot);
    if (status != PSA_SUCCESS) {
        return status;
    }
    status = psa_get_key_attributes(key, &attributes);
    if (status != PSA_SUCCESS) {
        (void) psa_unregister_read_under_mutex(slot);
        return status;
    }
    type = psa_get_key_type(&attributes);
    psa_reset_key_attributes(&attributes);
    if (type != PSA_KEY_TYPE_AES) {
        (void) psa_unregister_read_under_mutex(slot);
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    if (slot->key.bytes > key_buffer_size) {
        (void) psa_unregister_read_under_mutex(slot);
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }
    memcpy(key_buffer, slot->key.data, slot->key.bytes);
    *key_length = slot->key.bytes;
    return psa_unregister_read_under_mutex(slot);
}

psa_status_t mbedtls_nist_kw_wrap(mbedtls_svc_key_id_t key,
                                  mbedtls_nist_kw_mode_t mode,
                                  const unsigned char *input, size_t input_length,
                                  unsigned char *output, size_t output_size,
                                  size_t *output_length)
{
    unsigned char key_bytes[32];
    size_t key_length = 0;
    size_t wrapped_length = 0;
    psa_status_t status;
    XCryptographic_KeyWrapMode xmode;
    XByteArrayView kek;
    XByteArrayView in_view;

    *output_length = 0;
    if (!input || !output || !output_length) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    status = get_key_bytes(key, key_bytes, sizeof(key_bytes), &key_length);
    if (status != PSA_SUCCESS) {
        return status;
    }

    if (mode == MBEDTLS_KW_MODE_KW) {
        if (input_length < 16 || (input_length % KW_SEMIBLOCK_LENGTH) != 0) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        if (output_size < input_length + KW_SEMIBLOCK_LENGTH) {
            return PSA_ERROR_BUFFER_TOO_SMALL;
        }
        xmode = XCryptographic_KeyWrapMode_Kw;
    } else if (mode == MBEDTLS_KW_MODE_KWP) {
        if (input_length < 1 || output_size < input_length + KW_SEMIBLOCK_LENGTH) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        if (output_size < input_length + KW_SEMIBLOCK_LENGTH) {
            return PSA_ERROR_BUFFER_TOO_SMALL;
        }
        xmode = XCryptographic_KeyWrapMode_Kwp;
    } else {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    kek.m_data = (const char *)key_bytes;
    kek.m_size = key_length;
    in_view.m_data = (const char *)input;
    in_view.m_size = input_length;

    if (!XCryptographic_aesKeyWrapInto(xmode, kek, in_view,
                                       (char *)output, output_size,
                                       &wrapped_length)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    *output_length = wrapped_length;
    mbedtls_platform_zeroize(key_bytes, sizeof(key_bytes));
    return PSA_SUCCESS;
}

psa_status_t mbedtls_nist_kw_unwrap(mbedtls_svc_key_id_t key,
                                    mbedtls_nist_kw_mode_t mode,
                                    const unsigned char *input, size_t input_length,
                                    unsigned char *output, size_t output_size,
                                    size_t *output_length)
{
    unsigned char key_bytes[32];
    size_t key_length = 0;
    size_t unwrapped_length = 0;
    psa_status_t status;
    XCryptographic_KeyWrapMode xmode;
    XByteArrayView kek;
    XByteArrayView in_view;

    *output_length = 0;
    if (!input || !output || !output_length) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    status = get_key_bytes(key, key_bytes, sizeof(key_bytes), &key_length);
    if (status != PSA_SUCCESS) {
        return status;
    }

    if (mode == MBEDTLS_KW_MODE_KW) {
        if (input_length < 24 || (input_length % KW_SEMIBLOCK_LENGTH) != 0) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        xmode = XCryptographic_KeyWrapMode_Kw;
    } else if (mode == MBEDTLS_KW_MODE_KWP) {
        if (input_length < KW_SEMIBLOCK_LENGTH) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        xmode = XCryptographic_KeyWrapMode_Kwp;
    } else {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    if (output_size < input_length) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }

    kek.m_data = (const char *)key_bytes;
    kek.m_size = key_length;
    in_view.m_data = (const char *)input;
    in_view.m_size = input_length;

    if (!XCryptographic_aesKeyUnwrapInto(xmode, kek, in_view,
                                         (char *)output, output_size,
                                         &unwrapped_length)) {
        return PSA_ERROR_INVALID_SIGNATURE;
    }
    *output_length = unwrapped_length;
    mbedtls_platform_zeroize(key_bytes, sizeof(key_bytes));
    return PSA_SUCCESS;
}

#endif /* MBEDTLS_NIST_KW_C */
