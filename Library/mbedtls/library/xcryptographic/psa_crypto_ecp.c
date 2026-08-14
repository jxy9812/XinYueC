/*
 *  PSA ECP layer on top of Mbed TLS crypto
 */
/*
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
 */

#include "tf_psa_crypto_common.h"

#if defined(MBEDTLS_PSA_CRYPTO_C)

#include <psa/crypto.h>
#include "psa_crypto_core.h"
#include "psa_crypto_ecp.h"
#include "psa_crypto_random_impl.h"
#include "psa_util_internal.h"

#include <stdlib.h>
#include <string.h>
#include "mbedtls/platform.h"

#include <mbedtls/private/ecdsa.h>
#include <mbedtls/private/ecp.h>
#include <mbedtls/private/error_common.h>

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC)
#include "XCryptographic.h"
#include <limits.h>
#endif

#if defined(MBEDTLS_ECDH_VARIANT_EVEREST_ENABLED)
#include "mbedtls/private/everest/x25519.h"
#endif

#if defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_BASIC) || \
    defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_IMPORT) || \
    defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_EXPORT) || \
    defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_PUBLIC_KEY) || \
    defined(MBEDTLS_PSA_BUILTIN_ALG_ECDSA) || \
    defined(MBEDTLS_PSA_BUILTIN_ALG_DETERMINISTIC_ECDSA) || \
    defined(MBEDTLS_PSA_BUILTIN_ALG_ECDH)
/* Helper function to verify if the provided EC's family and key bit size are valid.
 *
 * Note: "bits" parameter is used both as input and output and it might be updated
 *       in case provided input value is not multiple of 8 ("sloppy" bits).
 */
static int check_ecc_parameters(psa_ecc_family_t family, size_t *bits)
{
    switch (family) {
        case PSA_ECC_FAMILY_SECP_R1:
            switch (*bits) {
                case 192:
                case 224:
                case 256:
                case 384:
                case 521:
                    return PSA_SUCCESS;
                case 528:
                    *bits = 521;
                    return PSA_SUCCESS;
            }
            break;

        case PSA_ECC_FAMILY_BRAINPOOL_P_R1:
            switch (*bits) {
                case 256:
                case 384:
                case 512:
                    return PSA_SUCCESS;
            }
            break;

        case PSA_ECC_FAMILY_MONTGOMERY:
            switch (*bits) {
                case 448:
                case 255:
                    return PSA_SUCCESS;
                case 256:
                    *bits = 255;
                    return PSA_SUCCESS;
            }
            break;

        case PSA_ECC_FAMILY_SECP_K1:
            switch (*bits) {
                case 192:
                case 256:
                    return PSA_SUCCESS;
            }
            break;
    }

    return PSA_ERROR_INVALID_ARGUMENT;
}

#if !defined(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC)
psa_status_t mbedtls_psa_ecp_load_representation(
    psa_key_type_t type, size_t curve_bits,
    const uint8_t *data, size_t data_length,
    mbedtls_ecp_keypair **p_ecp)
{
    mbedtls_ecp_group_id grp_id = MBEDTLS_ECP_DP_NONE;
    psa_status_t status;
    mbedtls_ecp_keypair *ecp = NULL;
    size_t curve_bytes = data_length;
    int explicit_bits = (curve_bits != 0);

    if (PSA_KEY_TYPE_IS_PUBLIC_KEY(type) &&
        PSA_KEY_TYPE_ECC_GET_FAMILY(type) != PSA_ECC_FAMILY_MONTGOMERY) {
        /* A Weierstrass public key is represented as:
         * - The byte 0x04;
         * - `x_P` as a `ceiling(m/8)`-byte string, big-endian;
         * - `y_P` as a `ceiling(m/8)`-byte string, big-endian.
         * So its data length is 2m+1 where m is the curve size in bits.
         */
        if ((data_length & 1) == 0) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        curve_bytes = data_length / 2;

        /* Montgomery public keys are represented in compressed format, meaning
         * their curve_bytes is equal to the amount of input. */

        /* Private keys are represented in uncompressed private random integer
         * format, meaning their curve_bytes is equal to the amount of input. */
    }

    if (explicit_bits) {
        /* With an explicit bit-size, the data must have the matching length. */
        if (curve_bytes != PSA_BITS_TO_BYTES(curve_bits)) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
    } else {
        /* We need to infer the bit-size from the data. Since the only
         * information we have is the length in bytes, the value of curve_bits
         * at this stage is rounded up to the nearest multiple of 8. */
        curve_bits = PSA_BYTES_TO_BITS(curve_bytes);
    }

    /* Allocate and initialize a key representation. */
    ecp = mbedtls_calloc(1, sizeof(mbedtls_ecp_keypair));
    if (ecp == NULL) {
        return PSA_ERROR_INSUFFICIENT_MEMORY;
    }
    mbedtls_ecp_keypair_init(ecp);

    status = check_ecc_parameters(PSA_KEY_TYPE_ECC_GET_FAMILY(type), &curve_bits);
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    /* Load the group. */
    grp_id = mbedtls_ecc_group_from_psa(PSA_KEY_TYPE_ECC_GET_FAMILY(type),
                                        curve_bits);
    if (grp_id == MBEDTLS_ECP_DP_NONE) {
        status = PSA_ERROR_NOT_SUPPORTED;
        goto exit;
    }

    status = mbedtls_to_psa_error(
        mbedtls_ecp_group_load(&ecp->grp, grp_id));
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    /* Load the key material. */
    if (PSA_KEY_TYPE_IS_PUBLIC_KEY(type)) {
        /* Load the public value. */
        status = mbedtls_to_psa_error(
            mbedtls_ecp_point_read_binary(&ecp->grp, &ecp->Q,
                                          data,
                                          data_length));
        if (status != PSA_SUCCESS) {
            goto exit;
        }

        /* Check that the point is on the curve. */
        status = mbedtls_to_psa_error(
            mbedtls_ecp_check_pubkey(&ecp->grp, &ecp->Q));
        if (status != PSA_SUCCESS) {
            goto exit;
        }
    } else {
        /* Load and validate the secret value. */
        status = mbedtls_to_psa_error(
            mbedtls_ecp_read_key(ecp->grp.id,
                                 ecp,
                                 data,
                                 data_length));
        if (status != PSA_SUCCESS) {
            goto exit;
        }
    }

    *p_ecp = ecp;
exit:
    if (status != PSA_SUCCESS) {
        mbedtls_ecp_keypair_free(ecp);
        mbedtls_free(ecp);
    }

    return status;
}
#endif /* !defined(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC) */

#endif /* defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_BASIC) || ... */

#if defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_IMPORT) || \
    defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_EXPORT) || \
    defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_PUBLIC_KEY)

psa_status_t mbedtls_psa_ecp_import_key(
    const psa_key_attributes_t *attributes,
    const uint8_t *data, size_t data_length,
    uint8_t *key_buffer, size_t key_buffer_size,
    size_t *key_buffer_length, size_t *bits)
{

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC)
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_MONTGOMERY &&
        attributes->bits == 448 && data_length == 56 && key_buffer_size >= data_length) {
        XCryptographic_Key key;
        bool valid = PSA_KEY_TYPE_IS_PUBLIC_KEY(attributes->type) ?
                     XCryptographic_ecdhImportPublicKey(
                         XCryptographic_EcdhAlgorithm_X448,
                         XByteArrayView_create_data(data, 56), &key) :
                     XCryptographic_ecdhImportPrivateKey(
                         XCryptographic_EcdhAlgorithm_X448,
                         XByteArrayView_create_data(data, 56), &key);
        if (!valid) return PSA_ERROR_INVALID_ARGUMENT;
        memcpy(key_buffer, data, data_length);
        *key_buffer_length = data_length;
        *bits = 448;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_MONTGOMERY &&
        (attributes->bits == 255 || attributes->bits == 256) &&
        data_length == 32 && key_buffer_size >= data_length) {
        XCryptographic_Key key;
        bool valid = PSA_KEY_TYPE_IS_PUBLIC_KEY(attributes->type) ?
                     XCryptographic_ecdhImportPublicKey(
                         XCryptographic_EcdhAlgorithm_X25519,
                         XByteArrayView_create_data(data, 32), &key) :
                     XCryptographic_ecdhImportPrivateKey(
                         XCryptographic_EcdhAlgorithm_X25519,
                         XByteArrayView_create_data(data, 32), &key);
        if (!valid) return PSA_ERROR_INVALID_ARGUMENT;
        memcpy(key_buffer, data, data_length);
        *key_buffer_length = data_length;
        *bits = 255;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 521 && data_length <= INT64_MAX &&
        key_buffer_size >= data_length) {
        XCryptographic_Key key;
        bool valid = PSA_KEY_TYPE_IS_PUBLIC_KEY(attributes->type) ?
                     XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_NistP521,
                         XByteArrayView_create_data(data, (int64_t)data_length), &key) :
                     XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_NistP521,
                         XByteArrayView_create_data(data, (int64_t)data_length), &key);
        if (!valid) return PSA_ERROR_INVALID_ARGUMENT;
        memcpy(key_buffer, data, data_length);
        *key_buffer_length = data_length;
        *bits = 521;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits == 512 && data_length <= INT64_MAX && key_buffer_size >= data_length) {
        XCryptographic_Key key;
        bool valid = PSA_KEY_TYPE_IS_PUBLIC_KEY(attributes->type) ?
                     XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_BrainpoolP512r1,
                         XByteArrayView_create_data(data, (int64_t)data_length), &key) :
                     XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_BrainpoolP512r1,
                         XByteArrayView_create_data(data, (int64_t)data_length), &key);
        if (!valid) return PSA_ERROR_INVALID_ARGUMENT;
        memcpy(key_buffer, data, data_length);
        *key_buffer_length = data_length;
        *bits = 512;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits == 384 && data_length <= INT64_MAX && key_buffer_size >= data_length) {
        XCryptographic_Key key;
        bool valid = PSA_KEY_TYPE_IS_PUBLIC_KEY(attributes->type) ?
                     XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_BrainpoolP384r1, XByteArrayView_create_data(data,(int64_t)data_length),&key) :
                     XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_BrainpoolP384r1, XByteArrayView_create_data(data,(int64_t)data_length),&key);
        if (!valid) return PSA_ERROR_INVALID_ARGUMENT;
        memcpy(key_buffer,data,data_length); *key_buffer_length=data_length; *bits=384; XCryptographic_destroyKey(&key); return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 384 && data_length <= INT64_MAX &&
        key_buffer_size >= data_length) {
        XCryptographic_Key key;
        bool valid = PSA_KEY_TYPE_IS_PUBLIC_KEY(attributes->type) ?
                     XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_NistP384,
                         XByteArrayView_create_data(data, (int64_t)data_length), &key) :
                     XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_NistP384,
                         XByteArrayView_create_data(data, (int64_t)data_length), &key);
        if (!valid) return PSA_ERROR_INVALID_ARGUMENT;
        memcpy(key_buffer, data, data_length);
        *key_buffer_length = data_length;
        *bits = 384;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 256 && data_length <= INT64_MAX &&
        key_buffer_size >= data_length) {
        XCryptographic_Key key;
        bool valid = PSA_KEY_TYPE_IS_PUBLIC_KEY(attributes->type) ?
                     XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_NistP256,
                         XByteArrayView_create_data(data, (int64_t)data_length), &key) :
                     XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_NistP256,
                         XByteArrayView_create_data(data, (int64_t)data_length), &key);
        if (!valid) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        memcpy(key_buffer, data, data_length);
        *key_buffer_length = data_length;
        *bits = 256;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_K1 &&
        attributes->bits == 256 && data_length <= INT64_MAX &&
        key_buffer_size >= data_length) {
        XCryptographic_Key key;
        bool valid = PSA_KEY_TYPE_IS_PUBLIC_KEY(attributes->type) ?
                     XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_Secp256k1,
                         XByteArrayView_create_data(data, (int64_t)data_length), &key) :
                     XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_Secp256k1,
                         XByteArrayView_create_data(data, (int64_t)data_length), &key);
        if (!valid) return PSA_ERROR_INVALID_ARGUMENT;
        memcpy(key_buffer, data, data_length);
        *key_buffer_length = data_length;
        *bits = 256;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits == 256 && data_length <= INT64_MAX &&
        key_buffer_size >= data_length) {
        XCryptographic_Key key;
        bool valid = PSA_KEY_TYPE_IS_PUBLIC_KEY(attributes->type) ?
                     XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_BrainpoolP256r1,
                         XByteArrayView_create_data(data, (int64_t)data_length), &key) :
                     XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_BrainpoolP256r1,
                         XByteArrayView_create_data(data, (int64_t)data_length), &key);
        if (!valid) return PSA_ERROR_INVALID_ARGUMENT;
        memcpy(key_buffer, data, data_length);
        *key_buffer_length = data_length;
        *bits = 256;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    return PSA_ERROR_NOT_SUPPORTED;
#else
    psa_status_t status;
    mbedtls_ecp_keypair *ecp = NULL;
    /* Parse input */
    status = mbedtls_psa_ecp_load_representation(attributes->type,
                                                 attributes->bits,
                                                 data,
                                                 data_length,
                                                 &ecp);
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) ==
        PSA_ECC_FAMILY_MONTGOMERY) {
        *bits = ecp->grp.nbits + 1;
    } else {
        *bits = ecp->grp.nbits;
    }

    /* Re-export the data to PSA export format. There is currently no support
     * for other input formats then the export format, so this is a 1-1
     * copy operation. */
    status = mbedtls_psa_ecp_export_key(attributes->type,
                                        ecp,
                                        key_buffer,
                                        key_buffer_size,
                                        key_buffer_length);
exit:
    /* Always free the PK object (will also free contained ECP context) */
    mbedtls_ecp_keypair_free(ecp);
    mbedtls_free(ecp);

    return status;
#endif
}

#if !defined(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC)
psa_status_t mbedtls_psa_ecp_export_key(psa_key_type_t type,
                                        mbedtls_ecp_keypair *ecp,
                                        uint8_t *data,
                                        size_t data_size,
                                        size_t *data_length)
{
    psa_status_t status;

    if (PSA_KEY_TYPE_IS_PUBLIC_KEY(type)) {
        /* Check whether the public part is loaded */
        if (mbedtls_ecp_is_zero(&ecp->Q)) {
            /* Calculate the public key */
            status = mbedtls_to_psa_error(
                mbedtls_ecp_mul(&ecp->grp, &ecp->Q, &ecp->d, &ecp->grp.G,
                                mbedtls_psa_get_random,
                                MBEDTLS_PSA_RANDOM_STATE));
            if (status != PSA_SUCCESS) {
                return status;
            }
        }

        status = mbedtls_to_psa_error(
            mbedtls_ecp_point_write_binary(&ecp->grp, &ecp->Q,
                                           MBEDTLS_ECP_PF_UNCOMPRESSED,
                                           data_length,
                                           data,
                                           data_size));
        if (status != PSA_SUCCESS) {
            memset(data, 0, data_size);
        }

        return status;
    } else {
        status = mbedtls_to_psa_error(
            mbedtls_ecp_write_key_ext(ecp, data_length, data, data_size));
        return status;
    }
}
#endif

psa_status_t mbedtls_psa_ecp_export_public_key(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    uint8_t *data, size_t data_size, size_t *data_length)
{

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC)
    if (PSA_KEY_TYPE_IS_ECC_KEY_PAIR(attributes->type) &&
        PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_MONTGOMERY &&
        attributes->bits == 448 && key_buffer_size == 56 && data_size >= 56) {
        XCryptographic_Key key;
        if (!XCryptographic_ecdhImportPrivateKey(
                XCryptographic_EcdhAlgorithm_X448,
                XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        memcpy(data, key.publicKey, 56);
        *data_length = 56;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_IS_ECC_KEY_PAIR(attributes->type) &&
        PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 521 && key_buffer_size == 66 && data_size >= 133) {
        XCryptographic_Key key;
        if (!XCryptographic_ecdhImportPrivateKey(
                XCryptographic_EcdhAlgorithm_NistP521,
                XByteArrayView_create_data(key_buffer, key_buffer_size), &key))
            return PSA_ERROR_INVALID_ARGUMENT;
        memcpy(data, key.publicKey, 133);
        *data_length = 133;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_IS_ECC_KEY_PAIR(attributes->type) &&
        PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits == 512 && key_buffer_size == 64 && data_size >= 129) {
        XCryptographic_Key key;
        if (!XCryptographic_ecdhImportPrivateKey(
                XCryptographic_EcdhAlgorithm_BrainpoolP512r1,
                XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        memcpy(data, key.publicKey, 129);
        *data_length = 129;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_IS_ECC_KEY_PAIR(attributes->type) &&
        PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits == 384 && key_buffer_size == 48 && data_size >= 97) {
        XCryptographic_Key key;
        if (!XCryptographic_ecdhImportPrivateKey(XCryptographic_EcdhAlgorithm_BrainpoolP384r1,
                XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) return PSA_ERROR_INVALID_ARGUMENT;
        memcpy(data, key.publicKey, 97); *data_length = 97; XCryptographic_destroyKey(&key); return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_IS_ECC_KEY_PAIR(attributes->type) &&
        PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 384 && key_buffer_size == 48 && data_size >= 97) {
        XCryptographic_Key key;
        if (!XCryptographic_ecdhImportPrivateKey(
                XCryptographic_EcdhAlgorithm_NistP384,
                XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        memcpy(data, key.publicKey, 97);
        *data_length = 97;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_IS_ECC_KEY_PAIR(attributes->type) &&
        PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 256 && key_buffer_size == 32 && data_size >= 65) {
        XCryptographic_Key key;
        if (!XCryptographic_ecdhImportPrivateKey(
                XCryptographic_EcdhAlgorithm_NistP256,
                XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        memcpy(data, key.publicKey, 65);
        *data_length = 65;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_IS_ECC_KEY_PAIR(attributes->type) &&
        PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_K1 &&
        attributes->bits == 256 && key_buffer_size == 32 && data_size >= 65) {
        XCryptographic_Key key;
        if (!XCryptographic_ecdhImportPrivateKey(
                XCryptographic_EcdhAlgorithm_Secp256k1,
                XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        memcpy(data, key.publicKey, 65);
        *data_length = 65;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_IS_ECC_KEY_PAIR(attributes->type) &&
        PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits == 256 && key_buffer_size == 32 && data_size >= 65) {
        XCryptographic_Key key;
        if (!XCryptographic_ecdhImportPrivateKey(
                XCryptographic_EcdhAlgorithm_BrainpoolP256r1,
                XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        memcpy(data, key.publicKey, 65);
        *data_length = 65;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_IS_ECC_KEY_PAIR(attributes->type) &&
        PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_MONTGOMERY &&
        (attributes->bits == 255 || attributes->bits == 256) &&
        key_buffer_size == 32 && data_size >= 32) {
        XCryptographic_Key key;
        if (!XCryptographic_ecdhImportPrivateKey(
                XCryptographic_EcdhAlgorithm_X25519,
                XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
            return PSA_ERROR_INVALID_ARGUMENT;
        }
        memcpy(data, key.publicKey, 32);
        *data_length = 32;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    return PSA_ERROR_NOT_SUPPORTED;
#else
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    mbedtls_ecp_keypair *ecp = NULL;
    status = mbedtls_psa_ecp_load_representation(
        attributes->type, attributes->bits,
        key_buffer, key_buffer_size, &ecp);
    if (status != PSA_SUCCESS) {
        return status;
    }

    status = mbedtls_psa_ecp_export_key(
        PSA_KEY_TYPE_ECC_PUBLIC_KEY(
            PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type)),
        ecp, data, data_size, data_length);

    mbedtls_ecp_keypair_free(ecp);
    mbedtls_free(ecp);

    return status;
#endif
}
#endif /* defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_IMPORT) ||
        * defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_EXPORT) ||
        * defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_PUBLIC_KEY) */

#if defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_GENERATE)
psa_status_t mbedtls_psa_ecp_generate_key(
    const psa_key_attributes_t *attributes,
    uint8_t *key_buffer, size_t key_buffer_size, size_t *key_buffer_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC)
    psa_ecc_family_t family = PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type);
    XCryptographic_Key key;
    XCryptographic_EcdhAlgorithm algorithm = XCryptographic_EcdhAlgorithm_None;
    size_t private_size = 32;
    bool use_xcryptographic = false;

    if (attributes->bits == 521 && family == PSA_ECC_FAMILY_SECP_R1) {
        algorithm = XCryptographic_EcdhAlgorithm_NistP521;
        private_size = 66;
        use_xcryptographic = true;
    } else if (attributes->bits == 512 && family == PSA_ECC_FAMILY_BRAINPOOL_P_R1) {
        algorithm = XCryptographic_EcdhAlgorithm_BrainpoolP512r1;
        private_size = 64;
        use_xcryptographic = true;
    } else if (attributes->bits == 448 && family == PSA_ECC_FAMILY_MONTGOMERY) {
        algorithm = XCryptographic_EcdhAlgorithm_X448;
        private_size = 56;
        use_xcryptographic = true;
    } else if (attributes->bits == 384 && family == PSA_ECC_FAMILY_BRAINPOOL_P_R1) {
        algorithm = XCryptographic_EcdhAlgorithm_BrainpoolP384r1;
        private_size = 48; use_xcryptographic = true;
    } else if (attributes->bits == 384 && family == PSA_ECC_FAMILY_SECP_R1) {
        algorithm = XCryptographic_EcdhAlgorithm_NistP384;
        private_size = 48;
        use_xcryptographic = true;
    } else if ((attributes->bits == 256 &&
                (family == PSA_ECC_FAMILY_SECP_R1 || family == PSA_ECC_FAMILY_SECP_K1 ||
                 family == PSA_ECC_FAMILY_BRAINPOOL_P_R1)) ||
               ((attributes->bits == 255 || attributes->bits == 256) &&
                family == PSA_ECC_FAMILY_MONTGOMERY)) {
        algorithm = family == PSA_ECC_FAMILY_MONTGOMERY ?
                    XCryptographic_EcdhAlgorithm_X25519 :
                    (family == PSA_ECC_FAMILY_SECP_K1 ?
                     XCryptographic_EcdhAlgorithm_Secp256k1 :
                     (family == PSA_ECC_FAMILY_BRAINPOOL_P_R1 ?
                      XCryptographic_EcdhAlgorithm_BrainpoolP256r1 :
                      XCryptographic_EcdhAlgorithm_NistP256));
        use_xcryptographic = true;
    }
    if (use_xcryptographic && key_buffer_size >= private_size &&
        !XCryptographic_ecdhGenerateKey(algorithm, &key)) {
            return PSA_ERROR_INSUFFICIENT_ENTROPY;
    }
    if (use_xcryptographic && key_buffer_size >= private_size) {
        memcpy(key_buffer, key.privateKey, private_size);
        *key_buffer_length = private_size;
        XCryptographic_destroyKey(&key);
        return PSA_SUCCESS;
    }
    return PSA_ERROR_NOT_SUPPORTED;
#else
    psa_ecc_family_t curve = PSA_KEY_TYPE_ECC_GET_FAMILY(
        attributes->type);
    mbedtls_ecp_group_id grp_id =
        mbedtls_ecc_group_from_psa(curve, attributes->bits);
    if (grp_id == MBEDTLS_ECP_DP_NONE) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    mbedtls_ecp_keypair ecp;
    mbedtls_ecp_keypair_init(&ecp);
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;

    ret = mbedtls_ecp_group_load(&ecp.grp, grp_id);
    if (ret != 0) {
        goto exit;
    }

    ret = mbedtls_ecp_gen_privkey(&ecp.grp, &ecp.d,
                                  mbedtls_psa_get_random,
                                  MBEDTLS_PSA_RANDOM_STATE);
    if (ret != 0) {
        goto exit;
    }

    ret = mbedtls_ecp_write_key_ext(&ecp, key_buffer_length,
                                    key_buffer, key_buffer_size);

exit:
    mbedtls_ecp_keypair_free(&ecp);
    return mbedtls_to_psa_error(ret);
#endif
}
#endif /* MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_GENERATE */

/****************************************************************/
/* ECDSA sign/verify */
/****************************************************************/

#if defined(MBEDTLS_PSA_BUILTIN_ALG_ECDSA) || \
    defined(MBEDTLS_PSA_BUILTIN_ALG_DETERMINISTIC_ECDSA)
psa_status_t mbedtls_psa_ecdsa_sign_hash(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg, const uint8_t *hash, size_t hash_length,
    uint8_t *signature, size_t signature_size, size_t *signature_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC)
    XCryptographic_Key key;
    XByteArrayView signature_view;
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type)==PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits==384 && key_buffer_size==48 && hash_length<=INT64_MAX && signature_size>=96 &&
        XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_BrainpoolP384r1, XByteArrayView_create_data(key_buffer,48),&key)) {
        signature_view=PSA_ALG_ECDSA_IS_DETERMINISTIC(alg) ?
            XCryptographic_ecdsaSignHashInto((char*)signature,signature_size,key,XByteArrayView_create_data(hash,(int64_t)hash_length), true) :
            XCryptographic_ecdsaSignHashInto((char*)signature,signature_size,key,XByteArrayView_create_data(hash,(int64_t)hash_length), false);
        XCryptographic_destroyKey(&key); if (!signature_view.m_data) return PSA_ERROR_INSUFFICIENT_MEMORY;
        *signature_length=(size_t)signature_view.m_size; return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits == 512 && key_buffer_size == 64 && hash_length <= INT64_MAX &&
        signature_size >= 128 &&
        XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_BrainpoolP512r1,
            XByteArrayView_create_data(key_buffer, 64), &key)) {
        signature_view = PSA_ALG_ECDSA_IS_DETERMINISTIC(alg) ?
            XCryptographic_ecdsaSignHashInto(
                (char *)signature, signature_size, key,
                XByteArrayView_create_data(hash, (int64_t)hash_length), true) :
            XCryptographic_ecdsaSignHashInto(
                (char *)signature, signature_size, key,
                XByteArrayView_create_data(hash, (int64_t)hash_length), false);
        XCryptographic_destroyKey(&key);
        if (!signature_view.m_data) return PSA_ERROR_INSUFFICIENT_MEMORY;
        *signature_length = (size_t)signature_view.m_size;
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 521 && key_buffer_size == 66 && hash_length <= INT64_MAX &&
        signature_size >= 132 &&
        XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_NistP521,
            XByteArrayView_create_data(key_buffer, 66), &key)) {
        if (PSA_ALG_ECDSA_IS_DETERMINISTIC(alg)) {
            signature_view = XCryptographic_ecdsaSignHashInto(
                (char *)signature, signature_size, key,
                XByteArrayView_create_data(hash, (int64_t)hash_length), true);
        } else {
            signature_view = XCryptographic_ecdsaSignHashInto(
                (char *)signature, signature_size, key,
                XByteArrayView_create_data(hash, (int64_t)hash_length), false);
        }
        XCryptographic_destroyKey(&key);
        if (!signature_view.m_data) return PSA_ERROR_INSUFFICIENT_MEMORY;
        *signature_length = (size_t)signature_view.m_size;
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 384 && key_buffer_size == 48 && hash_length <= INT64_MAX &&
        signature_size >= 96 &&
        XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_NistP384,
            XByteArrayView_create_data(key_buffer, 48), &key)) {
        if (PSA_ALG_ECDSA_IS_DETERMINISTIC(alg)) {
            signature_view = XCryptographic_ecdsaSignHashInto(
                (char *)signature, signature_size, key,
                XByteArrayView_create_data(hash, (int64_t)hash_length), true);
        } else {
            signature_view = XCryptographic_ecdsaSignHashInto(
                (char *)signature, signature_size, key,
                XByteArrayView_create_data(hash, (int64_t)hash_length), false);
        }
        XCryptographic_destroyKey(&key);
        if (!signature_view.m_data) return PSA_ERROR_INSUFFICIENT_MEMORY;
        *signature_length = (size_t)signature_view.m_size;
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 256 &&
        key_buffer_size == 32 && hash_length <= INT64_MAX && signature_size >= 64 &&
        XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_NistP256,
            XByteArrayView_create_data(key_buffer, 32), &key)) {
        if (PSA_ALG_ECDSA_IS_DETERMINISTIC(alg)) {
            signature_view = XCryptographic_ecdsaSignHashInto(
                (char *)signature, signature_size, key,
                XByteArrayView_create_data(hash, (int64_t)hash_length), true);
        } else {
            signature_view = XCryptographic_ecdsaSignHashInto(
                (char *)signature, signature_size, key,
                XByteArrayView_create_data(hash, (int64_t)hash_length), false);
        }
        XCryptographic_destroyKey(&key);
        if (!signature_view.m_data) return PSA_ERROR_INSUFFICIENT_MEMORY;
        *signature_length = (size_t)signature_view.m_size;
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_K1 &&
        attributes->bits == 256 && key_buffer_size == 32 && hash_length <= INT64_MAX &&
        signature_size >= 64 &&
        XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_Secp256k1,
            XByteArrayView_create_data(key_buffer, 32), &key)) {
        if (PSA_ALG_ECDSA_IS_DETERMINISTIC(alg)) {
            signature_view = XCryptographic_ecdsaSignHashInto(
                (char *)signature, signature_size, key,
                XByteArrayView_create_data(hash, (int64_t)hash_length), true);
        } else {
            signature_view = XCryptographic_ecdsaSignHashInto(
                (char *)signature, signature_size, key,
                XByteArrayView_create_data(hash, (int64_t)hash_length), false);
        }
        XCryptographic_destroyKey(&key);
        if (!signature_view.m_data) return PSA_ERROR_INSUFFICIENT_MEMORY;
        *signature_length = (size_t)signature_view.m_size;
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits == 256 && key_buffer_size == 32 && hash_length <= INT64_MAX &&
        signature_size >= 64 &&
        XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_BrainpoolP256r1,
            XByteArrayView_create_data(key_buffer, 32), &key)) {
        if (PSA_ALG_ECDSA_IS_DETERMINISTIC(alg)) {
            signature_view = XCryptographic_ecdsaSignHashInto(
                (char *)signature, signature_size, key,
                XByteArrayView_create_data(hash, (int64_t)hash_length), true);
        } else {
            signature_view = XCryptographic_ecdsaSignHashInto(
                (char *)signature, signature_size, key,
                XByteArrayView_create_data(hash, (int64_t)hash_length), false);
        }
        XCryptographic_destroyKey(&key);
        if (!signature_view.m_data) return PSA_ERROR_INSUFFICIENT_MEMORY;
        *signature_length = (size_t)signature_view.m_size;
        return PSA_SUCCESS;
    }
    return PSA_ERROR_NOT_SUPPORTED;
#else
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    mbedtls_ecp_keypair *ecp = NULL;
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    size_t curve_bytes;
    mbedtls_mpi r, s;

    status = mbedtls_psa_ecp_load_representation(attributes->type,
                                                 attributes->bits,
                                                 key_buffer,
                                                 key_buffer_size,
                                                 &ecp);
    if (status != PSA_SUCCESS) {
        return status;
    }

    curve_bytes = PSA_BITS_TO_BYTES(ecp->grp.pbits);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    if (signature_size < 2 * curve_bytes) {
        ret = MBEDTLS_ERR_ECP_BUFFER_TOO_SMALL;
        goto cleanup;
    }

    if (PSA_ALG_ECDSA_IS_DETERMINISTIC(alg)) {
#if defined(MBEDTLS_PSA_BUILTIN_ALG_DETERMINISTIC_ECDSA)
        psa_algorithm_t hash_alg = PSA_ALG_SIGN_GET_HASH(alg);
        mbedtls_md_type_t md_alg = mbedtls_md_type_from_psa_alg(hash_alg);
        MBEDTLS_MPI_CHK(mbedtls_ecdsa_sign_det_ext(
                            &ecp->grp, &r, &s,
                            &ecp->d, hash,
                            hash_length, md_alg,
                            mbedtls_psa_get_random,
                            MBEDTLS_PSA_RANDOM_STATE));
#else
        ret = MBEDTLS_ERR_ECP_FEATURE_UNAVAILABLE;
        goto cleanup;
#endif /* defined(MBEDTLS_PSA_BUILTIN_ALG_DETERMINISTIC_ECDSA) */
    } else {
        (void) alg;
        MBEDTLS_MPI_CHK(mbedtls_ecdsa_sign(&ecp->grp, &r, &s, &ecp->d,
                                           hash, hash_length,
                                           mbedtls_psa_get_random,
                                           MBEDTLS_PSA_RANDOM_STATE));
    }

    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&r,
                                             signature,
                                             curve_bytes));
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(&s,
                                             signature + curve_bytes,
                                             curve_bytes));
cleanup:
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    if (ret == 0) {
        *signature_length = 2 * curve_bytes;
    }

    mbedtls_ecp_keypair_free(ecp);
    mbedtls_free(ecp);

    return mbedtls_to_psa_error(ret);
#endif
}

#if !defined(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC)
psa_status_t mbedtls_psa_ecp_load_public_part(mbedtls_ecp_keypair *ecp)
{
    int ret = 0;

    /* Check whether the public part is loaded. If not, load it. */
    if (mbedtls_ecp_is_zero(&ecp->Q)) {
        ret = mbedtls_ecp_mul(&ecp->grp, &ecp->Q,
                              &ecp->d, &ecp->grp.G,
                              mbedtls_psa_get_random,
                              MBEDTLS_PSA_RANDOM_STATE);
    }

    return mbedtls_to_psa_error(ret);
}
#endif

psa_status_t mbedtls_psa_ecdsa_verify_hash(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg, const uint8_t *hash, size_t hash_length,
    const uint8_t *signature, size_t signature_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC)
    XCryptographic_Key key;
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type)==PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits==384 && key_buffer_size==97 && hash_length<=INT64_MAX && signature_length<=INT64_MAX &&
        XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_BrainpoolP384r1, XByteArrayView_create_data(key_buffer,key_buffer_size),&key)) {
        bool valid=XCryptographic_ecdsaVerifyHash(key,XByteArrayView_create_data(hash,(int64_t)hash_length),XByteArrayView_create_data(signature,(int64_t)signature_length));
        XCryptographic_destroyKey(&key); return valid ? PSA_SUCCESS : PSA_ERROR_INVALID_SIGNATURE;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits == 512 && key_buffer_size == 129 && hash_length <= INT64_MAX &&
        signature_length <= INT64_MAX &&
        XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_BrainpoolP512r1,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        bool valid = XCryptographic_ecdsaVerifyHash(
            key, XByteArrayView_create_data(hash, (int64_t)hash_length),
            XByteArrayView_create_data(signature, (int64_t)signature_length));
        XCryptographic_destroyKey(&key);
        return valid ? PSA_SUCCESS : PSA_ERROR_INVALID_SIGNATURE;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 521 && key_buffer_size == 133 && hash_length <= INT64_MAX &&
        signature_length <= INT64_MAX &&
        XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_NistP521,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        bool valid = XCryptographic_ecdsaVerifyHash(
            key, XByteArrayView_create_data(hash, (int64_t)hash_length),
            XByteArrayView_create_data(signature, (int64_t)signature_length));
        XCryptographic_destroyKey(&key);
        return valid ? PSA_SUCCESS : PSA_ERROR_INVALID_SIGNATURE;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 384 && key_buffer_size == 97 && hash_length <= INT64_MAX &&
        signature_length <= INT64_MAX &&
        XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_NistP384,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        bool valid = XCryptographic_ecdsaVerifyHash(
            key, XByteArrayView_create_data(hash, (int64_t)hash_length),
            XByteArrayView_create_data(signature, (int64_t)signature_length));
        XCryptographic_destroyKey(&key);
        return valid ? PSA_SUCCESS : PSA_ERROR_INVALID_SIGNATURE;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 256 && key_buffer_size == 65 && hash_length <= INT64_MAX &&
        signature_length <= INT64_MAX &&
        XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_NistP256,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        bool valid = XCryptographic_ecdsaVerifyHash(
            key, XByteArrayView_create_data(hash, (int64_t)hash_length),
            XByteArrayView_create_data(signature, (int64_t)signature_length));
        XCryptographic_destroyKey(&key);
        return valid ? PSA_SUCCESS : PSA_ERROR_INVALID_SIGNATURE;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_K1 &&
        attributes->bits == 256 && key_buffer_size == 65 && hash_length <= INT64_MAX &&
        signature_length <= INT64_MAX &&
        XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_Secp256k1,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        bool valid = XCryptographic_ecdsaVerifyHash(
            key, XByteArrayView_create_data(hash, (int64_t)hash_length),
            XByteArrayView_create_data(signature, (int64_t)signature_length));
        XCryptographic_destroyKey(&key);
        return valid ? PSA_SUCCESS : PSA_ERROR_INVALID_SIGNATURE;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits == 256 && key_buffer_size == 65 && hash_length <= INT64_MAX &&
        signature_length <= INT64_MAX &&
        XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_BrainpoolP256r1,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        bool valid = XCryptographic_ecdsaVerifyHash(
            key, XByteArrayView_create_data(hash, (int64_t)hash_length),
            XByteArrayView_create_data(signature, (int64_t)signature_length));
        XCryptographic_destroyKey(&key);
        return valid ? PSA_SUCCESS : PSA_ERROR_INVALID_SIGNATURE;
    }
    return PSA_ERROR_NOT_SUPPORTED;
#else
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    mbedtls_ecp_keypair *ecp = NULL;
    size_t curve_bytes;
    mbedtls_mpi r, s;

    (void) alg;

    status = mbedtls_psa_ecp_load_representation(attributes->type,
                                                 attributes->bits,
                                                 key_buffer,
                                                 key_buffer_size,
                                                 &ecp);
    if (status != PSA_SUCCESS) {
        return status;
    }

    curve_bytes = PSA_BITS_TO_BYTES(ecp->grp.pbits);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    if (signature_length != 2 * curve_bytes) {
        status = PSA_ERROR_INVALID_SIGNATURE;
        goto cleanup;
    }

    status = mbedtls_to_psa_error(mbedtls_mpi_read_binary(&r,
                                                          signature,
                                                          curve_bytes));
    if (status != PSA_SUCCESS) {
        goto cleanup;
    }

    status = mbedtls_to_psa_error(mbedtls_mpi_read_binary(&s,
                                                          signature + curve_bytes,
                                                          curve_bytes));
    if (status != PSA_SUCCESS) {
        goto cleanup;
    }

    status = mbedtls_psa_ecp_load_public_part(ecp);
    if (status != PSA_SUCCESS) {
        goto cleanup;
    }

    status = mbedtls_to_psa_error(mbedtls_ecdsa_verify(&ecp->grp, hash,
                                                       hash_length, &ecp->Q,
                                                       &r, &s));
cleanup:
    mbedtls_mpi_free(&r);
    mbedtls_mpi_free(&s);
    mbedtls_ecp_keypair_free(ecp);
    mbedtls_free(ecp);

    return status;
#endif
}

#endif /* defined(MBEDTLS_PSA_BUILTIN_ALG_ECDSA) || \
        * defined(MBEDTLS_PSA_BUILTIN_ALG_DETERMINISTIC_ECDSA) */

/****************************************************************/
/* ECDH Key Agreement */
/****************************************************************/

#if defined(MBEDTLS_PSA_BUILTIN_ALG_ECDH)
#if !defined(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC)
static psa_status_t ecdh_write_secret(const mbedtls_ecp_group *grp,
                                      const mbedtls_ecp_point *secret,
                                      uint8_t *shared_secret, size_t shared_secret_size,
                                      size_t *shared_secret_length)
{
    *shared_secret_length = PSA_BITS_TO_BYTES(grp->pbits);
    if (shared_secret_size < *shared_secret_length) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }

    return mbedtls_to_psa_error(
        mbedtls_ecp_get_type(grp) == MBEDTLS_ECP_TYPE_MONTGOMERY ?
        mbedtls_mpi_write_binary_le(&secret->X, shared_secret, *shared_secret_length) :
        mbedtls_mpi_write_binary(&secret->X, shared_secret, *shared_secret_length));
}
#endif /* !defined(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC) */

#if defined(MBEDTLS_ECDH_VARIANT_EVEREST_ENABLED)
static psa_status_t ecdh_everest_shared_secret(
    const uint8_t *key_buffer, size_t key_buffer_size,
    const uint8_t *peer_key, size_t peer_key_length,
    uint8_t *shared_secret, size_t shared_secret_size,
    size_t *shared_secret_length)
{
    /* This static function is only called when we know the curve is x25519,
     * so we know key_buffer_size is correct unless the keystore is corrupted.
     * However even in that case we don't want the consequence to be a memory
     * error, so check anyway. This cannot be covered by tests though. */
    if (key_buffer_size != MBEDTLS_X25519_KEY_SIZE_BYTES) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    /* peer_key_length comes from the outside and could be incorrect */
    if (peer_key_length != MBEDTLS_X25519_KEY_SIZE_BYTES) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

    *shared_secret_length = MBEDTLS_X25519_KEY_SIZE_BYTES;
    if (shared_secret_size < *shared_secret_length) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }

    mbedtls_x25519_scalarmult(shared_secret, key_buffer, peer_key);

    return PSA_SUCCESS;
}
#endif /* MBEDTLS_ECDH_VARIANT_EVEREST_ENABLED */

psa_status_t mbedtls_psa_key_agreement_ecdh(
    const psa_key_attributes_t *attributes,
    const uint8_t *key_buffer, size_t key_buffer_size,
    psa_algorithm_t alg, const uint8_t *peer_key, size_t peer_key_length,
    uint8_t *shared_secret, size_t shared_secret_size,
    size_t *shared_secret_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_ECC)
    XCryptographic_Key key;
    XByteArrayView result;
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_MONTGOMERY &&
        attributes->bits == 448 && key_buffer_size == 56 && peer_key_length == 56 &&
        shared_secret_size >= 56 &&
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_X448,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        result = XCryptographic_ecdhAgreeInto(
            (char *)shared_secret, shared_secret_size, key,
            XByteArrayView_create_data(peer_key, (int64_t)peer_key_length));
        XCryptographic_destroyKey(&key);
        if (!result.m_data) return PSA_ERROR_INVALID_ARGUMENT;
        *shared_secret_length = (size_t)result.m_size;
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_MONTGOMERY &&
        (attributes->bits == 255 || attributes->bits == 256) &&
        key_buffer_size == 32 && peer_key_length == 32 &&
        shared_secret_size >= 32 &&
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_X25519,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        result = XCryptographic_ecdhAgreeInto(
            (char *)shared_secret, shared_secret_size, key,
            XByteArrayView_create_data(peer_key, (int64_t)peer_key_length));
        XCryptographic_destroyKey(&key);
        if (!result.m_data) return PSA_ERROR_INVALID_ARGUMENT;
        *shared_secret_length = (size_t)result.m_size;
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits == 256 && key_buffer_size == 32 && peer_key_length == 65 &&
        shared_secret_size >= 32 &&
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_BrainpoolP256r1,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        result = XCryptographic_ecdhAgreeInto(
            (char *)shared_secret, shared_secret_size, key,
            XByteArrayView_create_data(peer_key, (int64_t)peer_key_length));
        XCryptographic_destroyKey(&key);
        if (!result.m_data) return PSA_ERROR_INVALID_ARGUMENT;
        *shared_secret_length = (size_t)result.m_size;
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_K1 &&
        attributes->bits == 256 && key_buffer_size == 32 && peer_key_length == 65 &&
        shared_secret_size >= 32 &&
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_Secp256k1,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        result = XCryptographic_ecdhAgreeInto(
            (char *)shared_secret, shared_secret_size, key,
            XByteArrayView_create_data(peer_key, (int64_t)peer_key_length));
        XCryptographic_destroyKey(&key);
        if (!result.m_data) return PSA_ERROR_INVALID_ARGUMENT;
        *shared_secret_length = (size_t)result.m_size;
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 521 && key_buffer_size == 66 && peer_key_length == 133 &&
        shared_secret_size >= 66 &&
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_NistP521,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        result = XCryptographic_ecdhAgreeInto(
            (char *)shared_secret, shared_secret_size, key,
            XByteArrayView_create_data(peer_key, (int64_t)peer_key_length));
        XCryptographic_destroyKey(&key);
        if (!result.m_data) return PSA_ERROR_INVALID_ARGUMENT;
        *shared_secret_length = (size_t)result.m_size;
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits == 512 && key_buffer_size == 64 && peer_key_length == 129 &&
        shared_secret_size >= 64 &&
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_BrainpoolP512r1,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        result = XCryptographic_ecdhAgreeInto(
            (char *)shared_secret, shared_secret_size, key,
            XByteArrayView_create_data(peer_key, (int64_t)peer_key_length));
        XCryptographic_destroyKey(&key);
        if (!result.m_data) return PSA_ERROR_INVALID_ARGUMENT;
        *shared_secret_length = (size_t)result.m_size;
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_BRAINPOOL_P_R1 &&
        attributes->bits == 384 && key_buffer_size == 48 && peer_key_length == 97 &&
        shared_secret_size >= 48 &&
        XCryptographic_ecdhImportPrivateKey(XCryptographic_EcdhAlgorithm_BrainpoolP384r1,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        result = XCryptographic_ecdhAgreeInto((char *)shared_secret, shared_secret_size, key,
            XByteArrayView_create_data(peer_key, (int64_t)peer_key_length));
        XCryptographic_destroyKey(&key); if (!result.m_data) return PSA_ERROR_INVALID_ARGUMENT;
        *shared_secret_length = (size_t)result.m_size; return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 384 && key_buffer_size == 48 && peer_key_length == 97 &&
        shared_secret_size >= 48 &&
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_NistP384,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        result = XCryptographic_ecdhAgreeInto(
            (char *)shared_secret, shared_secret_size, key,
            XByteArrayView_create_data(peer_key, (int64_t)peer_key_length));
        XCryptographic_destroyKey(&key);
        if (!result.m_data) return PSA_ERROR_INVALID_ARGUMENT;
        *shared_secret_length = (size_t)result.m_size;
        return PSA_SUCCESS;
    }
    if (PSA_KEY_TYPE_ECC_GET_FAMILY(attributes->type) == PSA_ECC_FAMILY_SECP_R1 &&
        attributes->bits == 256 && key_buffer_size == 32 && peer_key_length == 65 &&
        shared_secret_size >= 32 &&
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_NistP256,
            XByteArrayView_create_data(key_buffer, key_buffer_size), &key)) {
        result = XCryptographic_ecdhAgreeInto(
            (char *)shared_secret, shared_secret_size, key,
            XByteArrayView_create_data(peer_key, (int64_t)peer_key_length));
        XCryptographic_destroyKey(&key);
        if (!result.m_data) return PSA_ERROR_INVALID_ARGUMENT;
        *shared_secret_length = (size_t)result.m_size;
        return PSA_SUCCESS;
    }
    return PSA_ERROR_NOT_SUPPORTED;
#else
    mbedtls_ecp_keypair *our_key = NULL;
    mbedtls_ecp_keypair *their_key = NULL;
    mbedtls_ecp_point secret;
    mbedtls_ecp_point_init(&secret);

    psa_status_t status;
    if (!PSA_KEY_TYPE_IS_ECC_KEY_PAIR(attributes->type) ||
        !PSA_ALG_IS_ECDH(alg)) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }

#if defined(MBEDTLS_ECDH_VARIANT_EVEREST_ENABLED)
    if (attributes->type == PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY) &&
        attributes->bits == 255) {
        return ecdh_everest_shared_secret(key_buffer, key_buffer_size,
                                          peer_key, peer_key_length,
                                          shared_secret, shared_secret_size,
                                          shared_secret_length);
    }
#endif /* MBEDTLS_ECDH_VARIANT_EVEREST_ENABLED */

    status = mbedtls_psa_ecp_load_representation(
        attributes->type,
        attributes->bits,
        key_buffer,
        key_buffer_size,
        &our_key);
    if (status != PSA_SUCCESS) {
        return status;
    }

    size_t bits = 0;
    psa_ecc_family_t curve = mbedtls_ecc_group_to_psa(our_key->grp.id, &bits);

    status = mbedtls_psa_ecp_load_representation(
        PSA_KEY_TYPE_ECC_PUBLIC_KEY(curve),
        bits,
        peer_key,
        peer_key_length,
        &their_key);
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    status = mbedtls_to_psa_error(
        mbedtls_ecp_mul(&our_key->grp, &secret, &our_key->d, &their_key->Q,
                        mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE));
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    status = ecdh_write_secret(&our_key->grp, &secret,
                               shared_secret, shared_secret_size, shared_secret_length);

exit:
    if (status != PSA_SUCCESS) {
        mbedtls_platform_zeroize(shared_secret, shared_secret_size);
    }
    mbedtls_ecp_point_free(&secret);
    mbedtls_ecp_keypair_free(their_key);
    mbedtls_free(their_key);
    mbedtls_ecp_keypair_free(our_key);
    mbedtls_free(our_key);
    return status;
#endif
}
#endif /* MBEDTLS_PSA_BUILTIN_ALG_ECDH */

/****************************************************************/
/* Interruptible ECC Key Generation */
/****************************************************************/

#if defined(MBEDTLS_ECP_RESTARTABLE) && \
    defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_GENERATE)

uint32_t mbedtls_psa_generate_key_iop_get_num_ops(
    mbedtls_psa_generate_key_iop_t *operation)
{
    return operation->num_ops;
}

psa_status_t mbedtls_psa_ecp_generate_key_iop_setup(
    mbedtls_psa_generate_key_iop_t *operation,
    const psa_key_attributes_t *attributes)
{
    int status = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;

    mbedtls_ecp_keypair_init(&operation->ecp);

    psa_ecc_family_t curve = PSA_KEY_TYPE_ECC_GET_FAMILY(
        psa_get_key_type(attributes));
    mbedtls_ecp_group_id grp_id =
        mbedtls_ecc_group_from_psa(curve, psa_get_key_bits(attributes));
    if (grp_id == MBEDTLS_ECP_DP_NONE) {
        return PSA_ERROR_NOT_SUPPORTED;
    }

    status = mbedtls_ecp_group_load(&operation->ecp.grp, grp_id);

    return mbedtls_to_psa_error(status);
}

psa_status_t mbedtls_psa_ecp_generate_key_iop_complete(
    mbedtls_psa_generate_key_iop_t *operation,
    uint8_t *key_output,
    size_t key_output_size,
    size_t *key_len)
{
    *key_len = 0;
    int status = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;

    *key_len = PSA_BITS_TO_BYTES(operation->ecp.grp.nbits);

    if (*key_len > key_output_size) {
        return PSA_ERROR_BUFFER_TOO_SMALL;
    }

    status = mbedtls_ecp_gen_privkey(&operation->ecp.grp, &operation->ecp.d,
                                     mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE);

    if (status != 0) {
        return mbedtls_to_psa_error(status);
    }

    /* Our implementation of key generation only generates the private key
       which doesn't invlolve any ECC arithmetic operations so number of ops
       is less than 1 but we round up to 1 to differentiate between num ops of
       0 which means no work has been done this facilitates testing. */
    operation->num_ops = 1;

    status = mbedtls_mpi_write_binary(&operation->ecp.d, key_output, key_output_size);

    return mbedtls_to_psa_error(status);
}

psa_status_t mbedtls_psa_ecp_generate_key_iop_abort(
    mbedtls_psa_generate_key_iop_t *operation)
{
    mbedtls_ecp_keypair_free(&operation->ecp);
    operation->num_ops = 0;
    return PSA_SUCCESS;
}

#endif /* MBEDTLS_ECP_RESTARTABLE && MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_GENERATE */

#if defined(MBEDTLS_ECP_RESTARTABLE) && \
    (defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_IMPORT) || \
    defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_EXPORT) || \
    defined(MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_PUBLIC_KEY))

uint32_t mbedtls_psa_ecp_export_public_key_iop_get_num_ops(
    mbedtls_psa_export_public_key_iop_t *operation)
{
    return operation->num_ops;
}

psa_status_t mbedtls_psa_ecp_export_public_key_iop_setup(
    mbedtls_psa_export_public_key_iop_t *operation,
    uint8_t *key,
    size_t key_len,
    const psa_key_attributes_t *key_attributes)
{
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;

    status = mbedtls_psa_ecp_load_representation(
        psa_get_key_type(key_attributes),
        psa_get_key_bits(key_attributes),
        key,
        key_len,
        &operation->key);
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    mbedtls_ecp_restart_init(&operation->restart_ctx);
    operation->num_ops = 0;

exit:
    return status;
}

psa_status_t mbedtls_psa_ecp_export_public_key_iop_complete(
    mbedtls_psa_export_public_key_iop_t *operation,
    uint8_t *pub_key,
    size_t pub_key_size,
    size_t *pub_key_len)
{
    int ret = 0;

    if (mbedtls_ecp_is_zero(&operation->key->Q)) {
        mbedtls_psa_interruptible_set_max_ops(psa_interruptible_get_max_ops());

        ret = mbedtls_ecp_mul_restartable(&operation->key->grp, &operation->key->Q,
                                          &operation->key->d, &operation->key->grp.G,
                                          mbedtls_psa_get_random, MBEDTLS_PSA_RANDOM_STATE,
                                          &operation->restart_ctx);
        operation->num_ops += operation->restart_ctx.ops_done;
    }

    if (ret == 0) {
        ret = mbedtls_ecp_write_public_key(operation->key,
                                           MBEDTLS_ECP_PF_UNCOMPRESSED, pub_key_len,
                                           pub_key, pub_key_size);
    }

    return mbedtls_to_psa_error(ret);
}

psa_status_t mbedtls_psa_ecp_export_public_key_iop_abort(
    mbedtls_psa_export_public_key_iop_t *operation)
{
    mbedtls_ecp_keypair_free(operation->key);
    mbedtls_free(operation->key);
    mbedtls_ecp_restart_free(&operation->restart_ctx);
    operation->num_ops = 0;
    return PSA_SUCCESS;
}

#endif /* MBEDTLS_ECP_RESTARTABLE && \
          (MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_IMPORT ||
           MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_KEY_PAIR_EXPORT || \
           MBEDTLS_PSA_BUILTIN_KEY_TYPE_ECC_PUBLIC_KEY) */

/****************************************************************/
/* Interruptible ECC Key Agreement */
/****************************************************************/

#if defined(MBEDTLS_PSA_BUILTIN_ALG_ECDH) && defined(MBEDTLS_ECP_RESTARTABLE)

uint32_t mbedtls_psa_key_agreement_iop_get_num_ops(
    mbedtls_psa_key_agreement_interruptible_operation_t *operation)
{
    return operation->num_ops;
}

psa_status_t mbedtls_psa_key_agreement_iop_setup(
    mbedtls_psa_key_agreement_interruptible_operation_t *operation,
    const psa_key_attributes_t *private_key_attributes,
    const uint8_t *private_key_buffer,
    size_t private_key_buffer_len,
    const uint8_t *peer_key,
    size_t peer_key_length)
{
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;

    /* We need to clear number of ops here in case there was a previous
       complete operation which doesn't reset it after finsishing. */
    operation->num_ops = 0;

    psa_key_type_t private_key_type = psa_get_key_type(private_key_attributes);
    if (!PSA_KEY_TYPE_IS_ECC_KEY_PAIR(private_key_type)) {
        status = PSA_ERROR_INVALID_ARGUMENT;
        goto exit;
    }

    status = mbedtls_psa_ecp_load_representation(
        psa_get_key_type(private_key_attributes),
        psa_get_key_bits(private_key_attributes),
        private_key_buffer,
        private_key_buffer_len,
        &operation->our_key);
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    status = mbedtls_psa_ecp_load_representation(
        PSA_KEY_TYPE_PUBLIC_KEY_OF_KEY_PAIR(private_key_type),
        psa_get_key_bits(private_key_attributes),
        peer_key,
        peer_key_length,
        &operation->their_key);
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    /* mbedtls_psa_ecp_load_representation() calls mbedtls_ecp_check_pubkey() which
       takes MBEDTLS_ECP_OPS_CHK amount of ops. */
    operation->num_ops += MBEDTLS_ECP_OPS_CHK;

exit:
    return status;
}

psa_status_t mbedtls_psa_key_agreement_iop_complete(
    mbedtls_psa_key_agreement_interruptible_operation_t *operation,
    uint8_t *shared_secret,
    size_t shared_secret_size,
    size_t *shared_secret_length)
{
    psa_status_t status = PSA_ERROR_CORRUPTION_DETECTED;
    mbedtls_ecp_point secret;

    mbedtls_ecp_point_init(&secret);

    mbedtls_psa_interruptible_set_max_ops(psa_interruptible_get_max_ops());

    status = mbedtls_to_psa_error(
        mbedtls_ecp_mul_restartable(&operation->our_key->grp,
                                    &secret,
                                    &operation->our_key->d,
                                    &operation->their_key->Q,
                                    mbedtls_psa_get_random,
                                    MBEDTLS_PSA_RANDOM_STATE,
                                    &operation->rs));
    operation->num_ops += operation->rs.ops_done;
    if (status != PSA_SUCCESS) {
        goto exit;
    }

    status = ecdh_write_secret(&operation->our_key->grp, &secret,
                               shared_secret, shared_secret_size, shared_secret_length);

exit:
    mbedtls_ecp_point_free(&secret);

    return status;
}

psa_status_t mbedtls_psa_key_agreement_iop_abort(
    mbedtls_psa_key_agreement_interruptible_operation_t *operation)
{
    mbedtls_ecp_keypair_free(operation->our_key);
    mbedtls_free(operation->our_key);
    operation->our_key = NULL;

    mbedtls_ecp_keypair_free(operation->their_key);
    mbedtls_free(operation->their_key);
    operation->their_key = NULL;

    mbedtls_ecp_restart_free(&operation->rs);
    operation->num_ops = 0;

    return PSA_SUCCESS;
}

#endif /* MBEDTLS_PSA_BUILTIN_ALG_ECDH */

#endif /* MBEDTLS_PSA_CRYPTO_C */
