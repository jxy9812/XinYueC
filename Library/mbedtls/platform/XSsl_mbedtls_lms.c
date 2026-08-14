/* 基于 XCryptographic 的 mbedTLS LMS/LM-OTS 兼容适配层。 */
#include "tf_psa_crypto_common.h"

#if defined(MBEDTLS_LMS_C) && defined(MBEDTLS_USE_XCRYPTOGRAPHIC_LMS) && \
    !defined(MBEDTLS_LMS_PRIVATE)

#include "XCryptographic.h"
#include "mbedtls/lms.h"
#include "mbedtls/platform_util.h"
#include "lmots.h"
#include <limits.h>
#include <string.h>

#define LMS_PUBLIC_KEY_TYPE_OFFSET 0u
#define LMS_PUBLIC_KEY_OTSTYPE_OFFSET 4u
#define LMS_PUBLIC_KEY_I_OFFSET 8u
#define LMS_PUBLIC_KEY_ROOT_OFFSET 24u
#define LMS_SIG_Q_OFFSET 0u
#define LMS_SIG_OTS_OFFSET 4u
#define LMS_SIG_TYPE_OFFSET 1128u
#define LMS_SIG_PATH_OFFSET 1132u

static bool lms_view(const unsigned char *data, size_t size,
                     XByteArrayView *view)
{
    if (!view || size > (size_t) INT64_MAX || (!data && size != 0)) {
        return false;
    }
    *view = XByteArrayView_create_data(data, (int64_t) size);
    return true;
}

void mbedtls_lmots_public_init(mbedtls_lmots_public_t *ctx)
{
    if (ctx) memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_lmots_public_free(mbedtls_lmots_public_t *ctx)
{
    if (ctx) mbedtls_platform_zeroize(ctx, sizeof(*ctx));
}

int mbedtls_lmots_import_public_key(mbedtls_lmots_public_t *ctx,
                                    const unsigned char *key, size_t key_size)
{
    if (!ctx || !key || key_size != MBEDTLS_LMOTS_PUBLIC_KEY_LEN(
            MBEDTLS_LMOTS_SHA256_N32_W8) ||
        key[0] != 0 || key[1] != 0 || key[2] != 0 || key[3] != 4) {
        return MBEDTLS_ERR_LMS_BAD_INPUT_DATA;
    }
    ctx->params.type = MBEDTLS_LMOTS_SHA256_N32_W8;
    memcpy(ctx->params.I_key_identifier, key + 4, 16);
    memcpy(ctx->params.q_leaf_identifier, key + 20, 4);
    memcpy(ctx->public_key, key + 24, 32);
    ctx->have_public_key = 1;
    return 0;
}

int mbedtls_lmots_export_public_key(const mbedtls_lmots_public_t *ctx,
                                    unsigned char *key, size_t key_size,
                                    size_t *key_len)
{
    if (!ctx || !ctx->have_public_key) return MBEDTLS_ERR_LMS_BAD_INPUT_DATA;
    if (!key || key_size < MBEDTLS_LMOTS_PUBLIC_KEY_LEN(ctx->params.type)) {
        return MBEDTLS_ERR_LMS_BUFFER_TOO_SMALL;
    }
    key[0] = 0; key[1] = 0; key[2] = 0; key[3] = 4;
    memcpy(key + 4, ctx->params.I_key_identifier, 16);
    memcpy(key + 20, ctx->params.q_leaf_identifier, 4);
    memcpy(key + 24, ctx->public_key, 32);
    if (key_len) *key_len = 56;
    return 0;
}

int mbedtls_lmots_calculate_public_key_candidate(
    const mbedtls_lmots_parameters_t *params, const unsigned char *msg,
    size_t msg_size, const unsigned char *sig, size_t sig_size,
    unsigned char *out, size_t out_size, size_t *out_len)
{
    XByteArrayView identifier, message, signature;
    if (!params || params->type != MBEDTLS_LMOTS_SHA256_N32_W8 ||
        !lms_view(params->I_key_identifier, 16, &identifier) ||
        !lms_view(msg, msg_size, &message) || !lms_view(sig, sig_size, &signature) ||
        !out || out_size < 32) return MBEDTLS_ERR_LMS_BAD_INPUT_DATA;
    if (!XCryptographic_lmotsCalculatePublicKeyCandidate(
            identifier, ((uint32_t)params->q_leaf_identifier[0] << 24) |
            ((uint32_t)params->q_leaf_identifier[1] << 16) |
            ((uint32_t)params->q_leaf_identifier[2] << 8) |
            params->q_leaf_identifier[3], message, signature, out, out_size)) {
        return MBEDTLS_ERR_LMS_VERIFY_FAILED;
    }
    if (out_len) *out_len = 32;
    return 0;
}

int mbedtls_lmots_verify(const mbedtls_lmots_public_t *ctx,
                         const unsigned char *msg, size_t msg_size,
                         const unsigned char *sig, size_t sig_size)
{
    unsigned char candidate[32];
    size_t candidate_len;
    int ret;
    if (!ctx || !ctx->have_public_key) return MBEDTLS_ERR_LMS_BAD_INPUT_DATA;
    ret = mbedtls_lmots_calculate_public_key_candidate(
        &ctx->params, msg, msg_size, sig, sig_size,
        candidate, sizeof(candidate), &candidate_len);
    if (ret != 0 || candidate_len != 32 ||
        memcmp(candidate, ctx->public_key, 32) != 0) {
        return MBEDTLS_ERR_LMS_VERIFY_FAILED;
    }
    return 0;
}

void mbedtls_lms_public_init(mbedtls_lms_public_t *ctx)
{
    if (ctx) memset(ctx, 0, sizeof(*ctx));
}

void mbedtls_lms_public_free(mbedtls_lms_public_t *ctx)
{
    if (ctx) mbedtls_platform_zeroize(ctx, sizeof(*ctx));
}

int mbedtls_lms_import_public_key(mbedtls_lms_public_t *ctx,
                                  const unsigned char *key, size_t key_size)
{
    if (!ctx || !key || key_size != MBEDTLS_LMS_PUBLIC_KEY_LEN(
            MBEDTLS_LMS_SHA256_M32_H10) ||
        key[0] != 0 || key[1] != 0 || key[2] != 0 || key[3] != 6 ||
        key[4] != 0 || key[5] != 0 || key[6] != 0 || key[7] != 4) {
        return MBEDTLS_ERR_LMS_BAD_INPUT_DATA;
    }
    ctx->params.type = MBEDTLS_LMS_SHA256_M32_H10;
    ctx->params.otstype = MBEDTLS_LMOTS_SHA256_N32_W8;
    memcpy(ctx->params.I_key_identifier, key + LMS_PUBLIC_KEY_I_OFFSET, 16);
    memcpy(ctx->T_1_pub_key, key + LMS_PUBLIC_KEY_ROOT_OFFSET, 32);
    ctx->have_public_key = 1;
    return 0;
}

int mbedtls_lms_export_public_key(const mbedtls_lms_public_t *ctx,
                                  unsigned char *key, size_t key_size,
                                  size_t *key_len)
{
    if (!ctx || !ctx->have_public_key) return MBEDTLS_ERR_LMS_BAD_INPUT_DATA;
    if (!key || key_size < MBEDTLS_LMS_PUBLIC_KEY_LEN(ctx->params.type)) {
        return MBEDTLS_ERR_LMS_BUFFER_TOO_SMALL;
    }
    key[0] = 0; key[1] = 0; key[2] = 0; key[3] = 6;
    key[4] = 0; key[5] = 0; key[6] = 0; key[7] = 4;
    memcpy(key + LMS_PUBLIC_KEY_I_OFFSET, ctx->params.I_key_identifier, 16);
    memcpy(key + LMS_PUBLIC_KEY_ROOT_OFFSET, ctx->T_1_pub_key, 32);
    if (key_len) *key_len = 56;
    return 0;
}

int mbedtls_lms_verify(const mbedtls_lms_public_t *ctx,
                       const unsigned char *msg, size_t msg_size,
                       const unsigned char *sig, size_t sig_size)
{
    XByteArrayView identifier, root, message, signature;
    if (!ctx || !ctx->have_public_key ||
        ctx->params.type != MBEDTLS_LMS_SHA256_M32_H10 ||
        ctx->params.otstype != MBEDTLS_LMOTS_SHA256_N32_W8 ||
        !lms_view(ctx->params.I_key_identifier, 16, &identifier) ||
        !lms_view(ctx->T_1_pub_key, 32, &root) ||
        !lms_view(msg, msg_size, &message) || !lms_view(sig, sig_size, &signature)) {
        return MBEDTLS_ERR_LMS_BAD_INPUT_DATA;
    }
    return XCryptographic_lmsVerify(identifier, root, message, signature) ?
           0 : MBEDTLS_ERR_LMS_VERIFY_FAILED;
}

#endif /* MBEDTLS_LMS_C && MBEDTLS_USE_XCRYPTOGRAPHIC_LMS && !MBEDTLS_LMS_PRIVATE */
