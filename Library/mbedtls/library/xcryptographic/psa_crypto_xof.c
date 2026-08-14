/*
 *  PSA XOF (extendable-output function) layer on top of software crypto
 *  Routed to XCryptographic when MBEDTLS_USE_XCRYPTOGRAPHIC_XOF is enabled.
 */
/*
 *  Copyright The Mbed TLS Contributors
 *  SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
 */

#include "tf_psa_crypto_common.h"

#if defined(MBEDTLS_PSA_CRYPTO_C)

#include <psa/crypto.h>
#include "psa_crypto_xof.h"

#if defined(MBEDTLS_PSA_BUILTIN_XOF)

#include <string.h>

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_XOF)
#include "XCryptographic.h"
#include "XMemory.h"
#endif

#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_XOF)

static XCryptographic_XofOperation *xcryptographic_xof_context_get(
    const mbedtls_psa_xof_operation_t *operation)
{
    XCryptographic_XofOperation *context = NULL;

    memcpy(&context, &operation->ctx, sizeof(context));
    return context;
}

static void xcryptographic_xof_context_set(
    mbedtls_psa_xof_operation_t *operation,
    XCryptographic_XofOperation *context)
{
    memset(&operation->ctx, 0, sizeof(operation->ctx));
    memcpy(&operation->ctx, &context, sizeof(context));
}

static psa_status_t xcryptographic_xof_algorithm(
    psa_algorithm_t alg, XCryptographic_XofAlgorithm *result)
{
    switch (alg) {
    case PSA_ALG_SHAKE128: *result = XCryptographic_XofAlgorithm_Shake128; return PSA_SUCCESS;
    case PSA_ALG_SHAKE256: *result = XCryptographic_XofAlgorithm_Shake256; return PSA_SUCCESS;
    default: return PSA_ALG_IS_XOF(alg) ? PSA_ERROR_NOT_SUPPORTED : PSA_ERROR_INVALID_ARGUMENT;
    }
}

#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_XOF */

psa_status_t mbedtls_psa_xof_abort(
    mbedtls_psa_xof_operation_t *operation)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_XOF)
    XCryptographic_XofOperation *context;

    if (operation->alg == 0) {
        return PSA_SUCCESS;
    }
    context = xcryptographic_xof_context_get(operation);
    if (context) {
        XCryptographic_xofAbort(context);
        XFree_System(context);
    }
    xcryptographic_xof_context_set(operation, NULL);
    operation->alg = 0;
    return PSA_SUCCESS;
#else
    switch (operation->alg) {
        case 0:
            /* The object has (apparently) been initialized but it is not
             * in use. It's ok to call abort on such an object, and there's
             * nothing to do. */
            break;

#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHAKE128)
        case PSA_ALG_SHAKE128:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHAKE256)
        case PSA_ALG_SHAKE256:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SOME_SHAKE)
            mbedtls_sha3_free(&operation->ctx.shake);
            break;
#endif

        default:
            return PSA_ERROR_BAD_STATE;
    }
    operation->alg = 0;
    return PSA_SUCCESS;
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_XOF */
}

psa_status_t mbedtls_psa_xof_setup(
    mbedtls_psa_xof_operation_t *operation,
    psa_algorithm_t alg)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_XOF)
    XCryptographic_XofAlgorithm algorithm;
    XCryptographic_XofOperation *context;
    psa_status_t status;

    /* A context must be freshly initialized before it can be set up. */
    if (operation->alg != 0) {
        return PSA_ERROR_BAD_STATE;
    }
    status = xcryptographic_xof_algorithm(alg, &algorithm);
    if (status != PSA_SUCCESS) {
        return status;
    }
    context = (XCryptographic_XofOperation *) XMalloc_System(sizeof(*context));
    if (!context) {
        return PSA_ERROR_INSUFFICIENT_MEMORY;
    }
    if (!XCryptographic_xofSetup(context, algorithm)) {
        XFree_System(context);
        return PSA_ERROR_NOT_SUPPORTED;
    }
    xcryptographic_xof_context_set(operation, context);
    operation->alg = alg;
    return PSA_SUCCESS;
#else
    /* A context must be freshly initialized before it can be set up. */
    if (operation->alg != 0) {
        return PSA_ERROR_BAD_STATE;
    }

    switch (alg) {
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHAKE128)
        case PSA_ALG_SHAKE128:
            mbedtls_sha3_starts(&operation->ctx.shake, MBEDTLS_SHA3_SHAKE128);
            break;
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHAKE256)
        case PSA_ALG_SHAKE256:
            mbedtls_sha3_starts(&operation->ctx.shake, MBEDTLS_SHA3_SHAKE256);
            break;
#endif

        default:
            return PSA_ALG_IS_XOF(alg) ?
                   PSA_ERROR_NOT_SUPPORTED :
                   PSA_ERROR_INVALID_ARGUMENT;
    }

    operation->alg = alg;
    return PSA_SUCCESS;
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_XOF */
}

psa_status_t mbedtls_psa_xof_set_context(
    mbedtls_psa_xof_operation_t *operation,
    const uint8_t *context, size_t context_length)
{
    switch (operation->alg) {
        case 0:
            return PSA_ERROR_BAD_STATE;

        default:
            (void) context;
            (void) context_length;
            return PSA_ERROR_INVALID_ARGUMENT;
    }
}

psa_status_t mbedtls_psa_xof_update(
    mbedtls_psa_xof_operation_t *operation,
    const uint8_t *input, size_t input_length)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_XOF)
    XCryptographic_XofOperation *context;
    XByteArrayView data;

    context = xcryptographic_xof_context_get(operation);
    if (!context || operation->alg == 0) {
        return PSA_ERROR_BAD_STATE;
    }
    data = XByteArrayView_create_data(input, (int64_t) input_length);
    if (!XCryptographic_xofUpdateInto(context, data)) {
        return PSA_ERROR_BAD_STATE;
    }
    return PSA_SUCCESS;
#else
    switch (operation->alg) {

#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHAKE128)
        case PSA_ALG_SHAKE128:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHAKE256)
        case PSA_ALG_SHAKE256:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SOME_SHAKE)
    mbedtls_sha3_update(&operation->ctx.shake, input, input_length);
    return PSA_SUCCESS;
#endif

        default:
            (void) input;
            (void) input_length;
            return PSA_ERROR_BAD_STATE;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_XOF */
}

psa_status_t mbedtls_psa_xof_output(
    mbedtls_psa_xof_operation_t *operation,
    uint8_t *output, size_t output_size)
{
#if defined(MBEDTLS_USE_XCRYPTOGRAPHIC_XOF)
    XCryptographic_XofOperation *context;
    XByteArrayView result;

    context = xcryptographic_xof_context_get(operation);
    if (!context || operation->alg == 0) {
        return PSA_ERROR_BAD_STATE;
    }
    if (output_size == 0) {
        return PSA_SUCCESS;
    }
    if (!output) {
        return PSA_ERROR_INVALID_ARGUMENT;
    }
    result = XCryptographic_xofOutputInto(context, (char *) output, output_size, output_size);
    if (!result.m_data) {
        return PSA_ERROR_INSUFFICIENT_MEMORY;
    }
    return PSA_SUCCESS;
#else
    /* TODO: fill output with something "safe" in case of error.
     * What would be safe here? */

    switch (operation->alg) {

#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHAKE128)
        case PSA_ALG_SHAKE128:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SHAKE256)
        case PSA_ALG_SHAKE256:
#endif
#if defined(MBEDTLS_PSA_BUILTIN_ALG_SOME_SHAKE)
    mbedtls_sha3_finish(&operation->ctx.shake, output, output_size);
    return PSA_SUCCESS;
#endif

        default:
            (void) output;
            (void) output_size;
            return PSA_ERROR_BAD_STATE;
    }
#endif /* MBEDTLS_USE_XCRYPTOGRAPHIC_XOF */
}

#endif /* MBEDTLS_PSA_BUILTIN_XOF */

#endif /* MBEDTLS_PSA_CRYPTO_C */
