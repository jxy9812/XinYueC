/**
 * @file XSslTest.c
 * @brief XSsl、XSslSocket 与 mbedTLS XCryptographic 后端离线回归测试。
 */

#ifndef MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#define MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS
#endif
#include "XSslTest.h"

#if DEMOTEST

#include "XSsl_platform.h"
#include "XSsl_session.h"
#include "XSslSocket.h"
#include "XAction.h"
#include "XByteArray.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XTestMenu.h"
#include "XPrintf.h"
#include "XThread.h"
#include "XTcpServer.h"
#include "XVector.h"
#include <mbedtls/private/ctr_drbg.h>
#include <mbedtls/private/hmac_drbg.h>
#include <mbedtls/lms.h>
#include <mbedtls/nist_kw.h>
#include <mbedtls/md.h>
#include <psa/crypto.h>
#include <limits.h>
#include <string.h>

#define XSSL_TEST_REQUIRE(condition, name, reason) \
    do { if (!(condition)) { XPrintf("[FAIL] XSsl: %s: %s\n", name, reason); return false; } } while (0)
#define XSSL_TEST_PASS(name) XPrintf("[PASS] XSsl: %s\n", name)
#define XSSL_TEST_PIPE_CAPACITY 65536u

typedef struct XSslTestPipe {
    uint8_t data[XSSL_TEST_PIPE_CAPACITY];
    size_t begin;
    size_t end;
} XSslTestPipe;

typedef struct XSslTestBio {
    XSslTestPipe* input;
    XSslTestPipe* output;
} XSslTestBio;

typedef struct XSslTestSocketFactory {
    XSslCertificate* certificate;
    XSslKey* private_key;
} XSslTestSocketFactory;

static const char xssl_test_certificate[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBfjCCASOgAwIBAgIUcmqiFpJI30FI7ibAG7UQLLw0JXEwCgYIKoZIzj0EAwIw\n"
    "FDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDgxMjEwMzc0OFoXDTI2MDgxMzEw\n"
    "Mzc0OFowFDESMBAGA1UEAwwJbG9jYWxob3N0MFkwEwYHKoZIzj0CAQYIKoZIzj0D\n"
    "AQcDQgAEkhHWHZJ/NCgz8OHzLzqbng5jo5V+VmAPMtlCQasfxHlXLXam6N7LPYZd\n"
    "QzyxN7bm4u1DEc++2lfLL9fk+YgHoqNTMFEwHQYDVR0OBBYEFGPItLV+0FMSR76J\n"
    "ruwTufruSMKJMB8GA1UdIwQYMBaAFGPItLV+0FMSR76JruwTufruSMKJMA8GA1Ud\n"
    "EwEB/wQFMAMBAf8wCgYIKoZIzj0EAwIDSQAwRgIhAKWshEteA5UU4A4+8n5RYhLE\n"
    "Ivohx6AEAfKq15LCHHUHAiEAhZXXbl1K/9xaB7gDkMvH8+TzVaxMQKjs1lZMGpRo\n"
    "G+o=\n"
    "-----END CERTIFICATE-----\n";

static const char xssl_test_private_key[] =
    "-----BEGIN EC PRIVATE KEY-----\n"
    "MHcCAQEEIMbnbIRsnsUbi1OpLo0zqSS0SrOQHF1DcKTcHdMxdyFJoAoGCCqGSM49\n"
    "AwEHoUQDQgAEkhHWHZJ/NCgz8OHzLzqbng5jo5V+VmAPMtlCQasfxHlXLXam6N7L\n"
    "PYZdQzyxN7bm4u1DEc++2lfLL9fk+YgHog==\n"
    "-----END EC PRIVATE KEY-----\n";

static int xssl_test_bio_send(void* user_data, const uint8_t* data, size_t size)
{
    XSslTestBio* bio = (XSslTestBio*)user_data;
    XSslTestPipe* output;
    size_t used;
    if (!bio || (!data && size != 0) || size > INT_MAX) return XSSL_BIO_ERROR;
    output = bio->output;
    if (!output) return XSSL_BIO_ERROR;
    used = output->end - output->begin;
    if (output->end + size > sizeof(output->data) && output->begin != 0) {
        memmove(output->data, output->data + output->begin, used);
        output->begin = 0;
        output->end = used;
    }
    if (size > sizeof(output->data) - output->end) return XSSL_BIO_WANT_WRITE;
    if (size != 0) memcpy(output->data + output->end, data, size);
    output->end += size;
    return (int)size;
}

static int xssl_test_bio_recv(void* user_data, uint8_t* data, size_t size)
{
    XSslTestBio* bio = (XSslTestBio*)user_data;
    XSslTestPipe* input;
    size_t available;
    if (!bio || !data || size == 0) return XSSL_BIO_ERROR;
    input = bio->input;
    if (!input) return XSSL_BIO_ERROR;
    available = input->end - input->begin;
    if (available == 0) return XSSL_BIO_WANT_READ;
    if (available > size) available = size;
    memcpy(data, input->data + input->begin, available);
    input->begin += available;
    if (input->begin == input->end) input->begin = input->end = 0;
    return (int)available;
}

static XTcpSocket* xssl_test_socket_factory(void* context)
{
    XSslTestSocketFactory* factory = (XSslTestSocketFactory*)context;
    XSslSocket* socket = XSslSocket_create();
    XString* cipher_suites;
    if (!socket || !factory) return (XTcpSocket*)socket;
    cipher_suites = XString_create_utf8("ECDHE-ECDSA-AES128-GCM-SHA256");
    XSslSocket_setProtocol(socket, XSSL_TlsV1_2);
    XSslSocket_setPeerVerifyMode(socket, XSSL_VerifyNone);
    if (cipher_suites) {
        (void)XSslSocket_setCipherSuites(socket, cipher_suites);
        XString_delete_base((XClass*)cipher_suites);
    }
    XSslSocket_setLocalCertificate(socket, factory->certificate);
    XSslSocket_setPrivateKey(socket, factory->private_key);
    return (XTcpSocket*)socket;
}

static bool xssl_test_psa_aead(psa_key_type_t key_type, psa_algorithm_t algorithm,
                               const uint8_t* key_data, size_t key_size,
                               const uint8_t* nonce, size_t nonce_size,
                               const uint8_t* associated_data, size_t associated_data_size,
                               const uint8_t* plain_text, size_t plain_text_size,
                               const uint8_t* expected_encrypted, size_t expected_encrypted_size,
                               const char* name)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t encrypted[128];
    uint8_t decrypted[128];
    size_t encrypted_size = 0;
    size_t decrypted_size = 0;
    psa_status_t status;
    XSSL_TEST_REQUIRE(plain_text_size + 16 <= sizeof(encrypted), name, "测试缓冲区不足");
    psa_set_key_type(&attributes, key_type);
    psa_set_key_bits(&attributes, key_size * 8u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, algorithm);
    status = psa_import_key(&attributes, key_data, key_size, &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, name, "PSA 密钥导入失败");
    status = psa_aead_encrypt(key, algorithm, nonce, nonce_size,
                              associated_data, associated_data_size,
                              plain_text, plain_text_size,
                              encrypted, sizeof(encrypted), &encrypted_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && encrypted_size == plain_text_size + 16u,
                      name, "PSA 认证加密失败");
    XSSL_TEST_REQUIRE(!expected_encrypted ||
                          (encrypted_size == expected_encrypted_size &&
                           memcmp(encrypted, expected_encrypted, encrypted_size) == 0),
                      name, "PSA 认证加密参考向量不匹配");
    memset(decrypted, 0, sizeof(decrypted));
    status = psa_aead_decrypt(key, algorithm, nonce, nonce_size,
                              associated_data, associated_data_size,
                              encrypted, encrypted_size,
                              decrypted, sizeof(decrypted), &decrypted_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && decrypted_size == plain_text_size &&
                      memcmp(decrypted, plain_text, plain_text_size) == 0,
                      name, "PSA 认证解密结果错误");
    encrypted[encrypted_size - 1] ^= 1u;
    memset(decrypted, 0xa5, sizeof(decrypted));
    status = psa_aead_decrypt(key, algorithm, nonce, nonce_size,
                              associated_data, associated_data_size,
                              encrypted, encrypted_size,
                              decrypted, sizeof(decrypted), &decrypted_size);
    XSSL_TEST_REQUIRE(status == PSA_ERROR_INVALID_SIGNATURE && decrypted[0] == 0 &&
                      decrypted_size == 0,
                      name, "认证失败的 PSA 输出清零语义错误");
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS, name, "PSA 密钥销毁失败");
    XSSL_TEST_PASS(name);
    return true;
}

static bool xssl_test_psa_aead_backend(void)
{
    static const uint8_t aes_key[16] = { 0 };
    static const uint8_t gcm_nonce[12] = { 0 };
    static const uint8_t gcm_plain_text[16] = { 0 };
    static const uint8_t ccm_nonce[13] = {
        0x00, 0x00, 0x00, 0x03, 0x02, 0x01, 0x00,
        0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5
    };
    static const uint8_t associated_data[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    static const uint8_t ccm_plain_text[23] = {
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e
    };
    static const uint8_t chacha_key[32] = {
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f
    };
    static const uint8_t chacha_nonce[12] = {
        0x07, 0x00, 0x00, 0x00, 0x40, 0x41, 0x42, 0x43,
        0x44, 0x45, 0x46, 0x47
    };
    static const uint8_t chacha_plain_text[] = "XCryptographic PSA AEAD";
    static const uint8_t aria_gcm_expected[32] = {
        0x52, 0xa9, 0xa4, 0xcc, 0x4f, 0xb1, 0xef, 0x00,
        0xa7, 0x2f, 0xf8, 0x75, 0x83, 0xd4, 0x4e, 0x5c,
        0x55, 0x8b, 0x5d, 0xc8, 0x13, 0x12, 0xa9, 0x93,
        0x5f, 0x28, 0x66, 0xf0, 0xbd, 0x77, 0xa6, 0x73
    };
    static const uint8_t aria_ccm_expected[39] = {
        0x82, 0x56, 0x2e, 0xf7, 0x11, 0x9b, 0x65, 0x7c,
        0x2f, 0x54, 0xb3, 0x5e, 0xda, 0x4d, 0xfc, 0x7e,
        0xa5, 0x92, 0xa6, 0x3a, 0x9b, 0x84, 0x3f,
        0x2c, 0xe8, 0xb5, 0xed, 0xf6, 0xc6, 0xdb, 0x59,
        0x1a, 0x62, 0x16, 0x11, 0x98, 0x4e, 0xc8, 0xe5
    };
    static const uint8_t camellia_gcm_expected[32] = {
        0xde, 0xfe, 0x3e, 0x0b, 0x5c, 0x54, 0xc9, 0x4b,
        0x4f, 0x2a, 0x0f, 0x5a, 0x46, 0xf6, 0x21, 0x0d,
        0xf6, 0x72, 0xb9, 0x4d, 0x19, 0x22, 0x66, 0xc7,
        0xc8, 0xc8, 0xdb, 0xb4, 0x27, 0xcc, 0x98, 0x9a
    };
    static const uint8_t camellia_ccm_expected[39] = {
        0xac, 0xbf, 0x4e, 0xa7, 0xca, 0xd2, 0xc2, 0x91,
        0x64, 0xa6, 0x7b, 0xa8, 0xa0, 0x38, 0x15, 0x48,
        0x94, 0xd8, 0x42, 0x5d, 0xdc, 0x29, 0x9f,
        0x78, 0x1b, 0x61, 0x8b, 0x30, 0xdb, 0x77, 0x99,
        0x75, 0x55, 0x8f, 0xe1, 0x46, 0xc5, 0x4b, 0x04
    };

    XSSL_TEST_REQUIRE(XSsl_platform_init(), "PSA AEAD backend init", "XSsl 平台初始化失败");
    return xssl_test_psa_aead(PSA_KEY_TYPE_AES, PSA_ALG_GCM,
                              aes_key, sizeof(aes_key), gcm_nonce, sizeof(gcm_nonce),
                              NULL, 0, gcm_plain_text, sizeof(gcm_plain_text), NULL, 0,
                              "PSA AES-GCM uses XCryptographic") &&
           xssl_test_psa_aead(PSA_KEY_TYPE_AES, PSA_ALG_CCM,
                              aes_key, sizeof(aes_key), ccm_nonce, sizeof(ccm_nonce),
                              associated_data, sizeof(associated_data),
                              ccm_plain_text, sizeof(ccm_plain_text), NULL, 0,
                              "PSA AES-CCM uses XCryptographic") &&
           xssl_test_psa_aead(PSA_KEY_TYPE_CHACHA20, PSA_ALG_CHACHA20_POLY1305,
                              chacha_key, sizeof(chacha_key), chacha_nonce, sizeof(chacha_nonce),
                              associated_data, sizeof(associated_data),
                              chacha_plain_text, sizeof(chacha_plain_text) - 1u,
                              NULL, 0, "PSA ChaCha20-Poly1305 uses XCryptographic") &&
           xssl_test_psa_aead(PSA_KEY_TYPE_ARIA, PSA_ALG_GCM,
                              aes_key, sizeof(aes_key), gcm_nonce, sizeof(gcm_nonce),
                              NULL, 0, gcm_plain_text, sizeof(gcm_plain_text),
                              aria_gcm_expected, sizeof(aria_gcm_expected),
                              "PSA ARIA-GCM uses XCryptographic") &&
           xssl_test_psa_aead(PSA_KEY_TYPE_ARIA, PSA_ALG_CCM,
                              aes_key, sizeof(aes_key), ccm_nonce, sizeof(ccm_nonce),
                              associated_data, sizeof(associated_data),
                              ccm_plain_text, sizeof(ccm_plain_text),
                              aria_ccm_expected, sizeof(aria_ccm_expected),
                              "PSA ARIA-CCM uses XCryptographic") &&
           xssl_test_psa_aead(PSA_KEY_TYPE_CAMELLIA, PSA_ALG_GCM,
                              aes_key, sizeof(aes_key), gcm_nonce, sizeof(gcm_nonce),
                              NULL, 0, gcm_plain_text, sizeof(gcm_plain_text),
                              camellia_gcm_expected, sizeof(camellia_gcm_expected),
                              "PSA Camellia-GCM uses XCryptographic") &&
           xssl_test_psa_aead(PSA_KEY_TYPE_CAMELLIA, PSA_ALG_CCM,
                              aes_key, sizeof(aes_key), ccm_nonce, sizeof(ccm_nonce),
                              associated_data, sizeof(associated_data),
                              ccm_plain_text, sizeof(ccm_plain_text),
                              camellia_ccm_expected, sizeof(camellia_ccm_expected),
                              "PSA Camellia-CCM uses XCryptographic");
}

static bool xssl_test_psa_aead_multipart(void)
{
    static const uint8_t aes_key[16] = { 0 };
    static const uint8_t chacha_key[32] = {
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f
    };
    static const uint8_t gcm_nonce[12] = { 0 };
    static const uint8_t chacha_nonce[12] = { 7, 0, 0, 0, 0x40, 0x41, 0x42, 0x43,
                                               0x44, 0x45, 0x46, 0x47 };
    static const uint8_t ccm_nonce[13] = {
        0, 0, 0, 3, 2, 1, 0, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5
    };
    static const uint8_t aad[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    static const uint8_t plain[] = "multipart AEAD XCryptographic";
    struct {
        psa_key_type_t type;
        psa_algorithm_t alg;
        const uint8_t *key;
        size_t keySize;
        const uint8_t *nonce;
        size_t nonceSize;
        const char *name;
    } cases[] = {
        { PSA_KEY_TYPE_AES, PSA_ALG_GCM, aes_key, sizeof(aes_key), gcm_nonce, sizeof(gcm_nonce),
          "PSA multipart AES-GCM uses XCryptographic" },
        { PSA_KEY_TYPE_AES, PSA_ALG_CCM, aes_key, sizeof(aes_key), ccm_nonce, sizeof(ccm_nonce),
          "PSA multipart AES-CCM uses XCryptographic" },
        { PSA_KEY_TYPE_ARIA, PSA_ALG_GCM, aes_key, sizeof(aes_key), gcm_nonce, sizeof(gcm_nonce),
          "PSA multipart ARIA-GCM uses XCryptographic" },
        { PSA_KEY_TYPE_ARIA, PSA_ALG_CCM, aes_key, sizeof(aes_key), ccm_nonce, sizeof(ccm_nonce),
          "PSA multipart ARIA-CCM uses XCryptographic" },
        { PSA_KEY_TYPE_CAMELLIA, PSA_ALG_GCM, aes_key, sizeof(aes_key), gcm_nonce, sizeof(gcm_nonce),
          "PSA multipart Camellia-GCM uses XCryptographic" },
        { PSA_KEY_TYPE_CAMELLIA, PSA_ALG_CCM, aes_key, sizeof(aes_key), ccm_nonce, sizeof(ccm_nonce),
          "PSA multipart Camellia-CCM uses XCryptographic" },
        { PSA_KEY_TYPE_CHACHA20, PSA_ALG_CHACHA20_POLY1305, chacha_key, sizeof(chacha_key),
          chacha_nonce, sizeof(chacha_nonce), "PSA multipart ChaCha20-Poly1305 uses XCryptographic" }
    };
    size_t caseIndex;
    for (caseIndex = 0; caseIndex < sizeof(cases) / sizeof(cases[0]); ++caseIndex) {
        psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
        mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
        psa_aead_operation_t encryptOperation = PSA_AEAD_OPERATION_INIT;
        psa_aead_operation_t decryptOperation = PSA_AEAD_OPERATION_INIT;
        uint8_t expected[128] = { 0 };
        uint8_t ciphertext[128] = { 0 };
        uint8_t decrypted[128] = { 0 };
        uint8_t tag[16] = { 0 };
        uint8_t badTag[16] = { 0 };
        size_t expectedSize = 0, outputSize = 0, updateSize = 0, tagSize = 0;
        psa_status_t status;

        psa_set_key_type(&attributes, cases[caseIndex].type);
        psa_set_key_bits(&attributes, cases[caseIndex].keySize * 8u);
        psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
        psa_set_key_algorithm(&attributes, cases[caseIndex].alg);
        status = psa_import_key(&attributes, cases[caseIndex].key, cases[caseIndex].keySize, &key);
        psa_reset_key_attributes(&attributes);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, cases[caseIndex].name, "PSA 密钥导入失败");
        status = psa_aead_encrypt(key, cases[caseIndex].alg, cases[caseIndex].nonce,
                                  cases[caseIndex].nonceSize, aad, sizeof(aad), plain,
                                  sizeof(plain) - 1u, expected, sizeof(expected), &expectedSize);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, cases[caseIndex].name, "一次性向量生成失败");

        status = psa_aead_encrypt_setup(&encryptOperation, key, cases[caseIndex].alg);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, cases[caseIndex].name, "分段加密 setup 失败");
        status = psa_aead_set_nonce(&encryptOperation, cases[caseIndex].nonce, cases[caseIndex].nonceSize);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, cases[caseIndex].name, "分段加密 nonce 失败");
        if (cases[caseIndex].alg == PSA_ALG_CCM) {
            status = psa_aead_set_lengths(&encryptOperation, sizeof(aad), sizeof(plain) - 1u);
            XSSL_TEST_REQUIRE(status == PSA_SUCCESS, cases[caseIndex].name, "分段 CCM 长度失败");
        }
        status = psa_aead_update_ad(&encryptOperation, aad, 3);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, cases[caseIndex].name, "分段附加数据 1 失败");
        status = psa_aead_update_ad(&encryptOperation, aad + 3, sizeof(aad) - 3u);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, cases[caseIndex].name, "分段附加数据 2 失败");
        status = psa_aead_update(&encryptOperation, NULL, 0, ciphertext, sizeof(ciphertext), &updateSize);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS && updateSize == 0,
                          cases[caseIndex].name, "零长度分段数据失败");
        status = psa_aead_update(&encryptOperation, plain, 7, ciphertext, sizeof(ciphertext), &updateSize);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS && updateSize == 7, cases[caseIndex].name, "分段数据 1 失败");
        status = psa_aead_update(&encryptOperation, plain + 7, sizeof(plain) - 1u - 7u,
                                 ciphertext + 7, sizeof(ciphertext) - 7u, &updateSize);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS && updateSize == sizeof(plain) - 1u - 7u,
                          cases[caseIndex].name, "分段数据 2 失败");
        status = psa_aead_finish(&encryptOperation, ciphertext + sizeof(plain) - 1u,
                                 sizeof(ciphertext) - (sizeof(plain) - 1u), &outputSize,
                                 tag, sizeof(tag), &tagSize);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS && outputSize == 0 && tagSize == 16 &&
                          memcmp(ciphertext, expected, sizeof(plain) - 1u) == 0 &&
                          memcmp(tag, expected + sizeof(plain) - 1u, 16) == 0,
                          cases[caseIndex].name, "分段加密结果不匹配");

        status = psa_aead_decrypt_setup(&decryptOperation, key, cases[caseIndex].alg);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, cases[caseIndex].name, "分段解密 setup 失败");
        status = psa_aead_set_nonce(&decryptOperation, cases[caseIndex].nonce, cases[caseIndex].nonceSize);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, cases[caseIndex].name, "分段解密 nonce 失败");
        if (cases[caseIndex].alg == PSA_ALG_CCM) {
            status = psa_aead_set_lengths(&decryptOperation, sizeof(aad), sizeof(plain) - 1u);
            XSSL_TEST_REQUIRE(status == PSA_SUCCESS, cases[caseIndex].name, "分段解密 CCM 长度失败");
        }
        status = psa_aead_update_ad(&decryptOperation, aad, 4);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, cases[caseIndex].name, "分段解密附加数据 1 失败");
        status = psa_aead_update_ad(&decryptOperation, aad + 4, sizeof(aad) - 4u);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, cases[caseIndex].name, "分段解密附加数据 2 失败");
        status = psa_aead_update(&decryptOperation, ciphertext, 5, decrypted, sizeof(decrypted), &updateSize);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS && updateSize == 5, cases[caseIndex].name, "分段解密数据 1 失败");
        status = psa_aead_update(&decryptOperation, ciphertext + 5, sizeof(plain) - 1u - 5u,
                                 decrypted + 5, sizeof(decrypted) - 5u, &updateSize);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS && updateSize == sizeof(plain) - 1u - 5u,
                          cases[caseIndex].name, "分段解密数据 2 失败");
        status = psa_aead_verify(&decryptOperation, decrypted + sizeof(plain) - 1u,
                                 sizeof(decrypted) - (sizeof(plain) - 1u), &outputSize,
                                 tag, tagSize);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS && outputSize == 0 &&
                          memcmp(decrypted, plain, sizeof(plain) - 1u) == 0,
                          cases[caseIndex].name, "分段解密结果错误");

        memcpy(badTag, tag, tagSize);
        badTag[0] ^= 1u;
        decryptOperation = psa_aead_operation_init();
        status = psa_aead_decrypt_setup(&decryptOperation, key, cases[caseIndex].alg);
        status = status == PSA_SUCCESS ? psa_aead_set_nonce(&decryptOperation, cases[caseIndex].nonce,
                                                             cases[caseIndex].nonceSize) : status;
        if (cases[caseIndex].alg == PSA_ALG_CCM && status == PSA_SUCCESS)
            status = psa_aead_set_lengths(&decryptOperation, sizeof(aad), sizeof(plain) - 1u);
        if (status == PSA_SUCCESS) status = psa_aead_update_ad(&decryptOperation, aad, sizeof(aad));
        if (status == PSA_SUCCESS) status = psa_aead_update(&decryptOperation, ciphertext,
                                                            sizeof(plain) - 1u, decrypted,
                                                            sizeof(decrypted), &updateSize);
        if (status == PSA_SUCCESS) status = psa_aead_verify(&decryptOperation, decrypted + sizeof(plain) - 1u,
                                                             sizeof(decrypted) - (sizeof(plain) - 1u),
                                                             &outputSize, badTag, tagSize);
        XSSL_TEST_REQUIRE(status == PSA_ERROR_INVALID_SIGNATURE,
                          cases[caseIndex].name, "错误标签未拒绝");
        XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS, cases[caseIndex].name, "PSA 密钥销毁失败");
        XSSL_TEST_PASS(cases[caseIndex].name);
    }

    {
        psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
        mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
        psa_aead_operation_t operation = PSA_AEAD_OPERATION_INIT;
        uint8_t output[16];
        size_t outputSize = 0;
        psa_status_t status;

        psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
        psa_set_key_bits(&attributes, sizeof(aes_key) * 8u);
        psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT);
        psa_set_key_algorithm(&attributes, PSA_ALG_GCM);
        status = psa_import_key(&attributes, aes_key, sizeof(aes_key), &key);
        psa_reset_key_attributes(&attributes);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA multipart AEAD states", "密钥导入失败");
        status = psa_aead_encrypt_setup(&operation, key, PSA_ALG_GCM);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA multipart AEAD states", "setup 失败");
        status = psa_aead_update(&operation, plain, 1, output, sizeof(output), &outputSize);
        XSSL_TEST_REQUIRE(status == PSA_ERROR_BAD_STATE, "PSA multipart AEAD states", "未设 nonce 状态未拒绝");
        operation = psa_aead_operation_init();
        status = psa_aead_encrypt_setup(&operation, key, PSA_ALG_GCM);
        status = status == PSA_SUCCESS ? psa_aead_set_nonce(&operation, gcm_nonce, sizeof(gcm_nonce)) : status;
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA multipart AEAD states", "缓冲区测试初始化失败");
        status = psa_aead_update(&operation, plain, 1, output, 0, &outputSize);
        XSSL_TEST_REQUIRE(status == PSA_ERROR_BUFFER_TOO_SMALL,
                          "PSA multipart AEAD states", "过小输出缓冲区未拒绝");
        status = psa_aead_update(&operation, plain, 1, output, sizeof(output), &outputSize);
        XSSL_TEST_REQUIRE(status == PSA_ERROR_BAD_STATE,
                          "PSA multipart AEAD states", "失败操作未中止");
        XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS,
                          "PSA multipart AEAD states", "PSA 密钥销毁失败");
        XSSL_TEST_PASS("PSA multipart AEAD state/error handling");
    }
    return true;
}

static bool xssl_test_psa_ctr_backend(void)
{
    static const uint8_t key_data[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const uint8_t iv[16] = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
    };
    static const uint8_t plain_text[64] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
    };
    static const uint8_t expected[64] = {
        0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
        0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
        0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff,
        0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff,
        0x5a, 0xe4, 0xdf, 0x3e, 0xdb, 0xd5, 0xd3, 0x5e,
        0x5b, 0x4f, 0x09, 0x02, 0x0d, 0xb0, 0x3e, 0xab,
        0x1e, 0x03, 0x1d, 0xda, 0x2f, 0xbe, 0x03, 0xd1,
        0x79, 0x21, 0x70, 0xa0, 0xf3, 0x00, 0x9c, 0xee
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t output[sizeof(plain_text)];
    size_t output_size = 0;
    size_t size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, sizeof(key_data) * 8u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_CTR);
    status = psa_import_key(&attributes, key_data, sizeof(key_data), &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CTR setup", "密钥导入失败");
    status = psa_cipher_encrypt_setup(&operation, key, PSA_ALG_CTR);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CTR streaming setup", "加密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, iv, sizeof(iv));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CTR IV", "计数器设置失败");
    status = psa_cipher_update(&operation, plain_text, 19, output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 19, "PSA AES-CTR streaming update", "第一段加密失败");
    output_size = size;
    status = psa_cipher_update(&operation, plain_text + output_size,
                               sizeof(plain_text) - output_size,
                               output + output_size, sizeof(output) - output_size, &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == sizeof(plain_text) - output_size,
                      "PSA AES-CTR streaming update", "第二段加密失败");
    output_size += size;
    status = psa_cipher_finish(&operation, output + output_size,
                               sizeof(output) - output_size, &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0 && output_size == sizeof(expected) &&
                      memcmp(output, expected, sizeof(expected)) == 0,
                      "PSA AES-CTR uses XCryptographic", "NIST SP 800-38A 向量不匹配");
    (void) psa_cipher_abort(&operation);

    operation = (psa_cipher_operation_t)PSA_CIPHER_OPERATION_INIT;
    status = psa_cipher_decrypt_setup(&operation, key, PSA_ALG_CTR);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CTR decrypt setup", "解密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, iv, sizeof(iv));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CTR decrypt IV", "解密计数器设置失败");
    status = psa_cipher_update(&operation, expected, sizeof(expected), output,
                               sizeof(output), &output_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && output_size == sizeof(plain_text) &&
                      memcmp(output, plain_text, sizeof(plain_text)) == 0,
                      "PSA AES-CTR decrypt uses XCryptographic", "解密结果错误");
    (void) psa_cipher_abort(&operation);
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS, "PSA AES-CTR setup", "密钥销毁失败");
    XSSL_TEST_PASS("PSA AES-CTR uses XCryptographic");
    return true;
}

static bool xssl_test_psa_ecb_cbc_backend(void)
{
    static const uint8_t key_data[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const uint8_t iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t plain_text[32] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51
    };
    static const uint8_t ecb_expected[32] = {
        0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
        0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97,
        0xf5, 0xd3, 0xd5, 0x85, 0x03, 0xb9, 0x69, 0x9d,
        0xe7, 0x85, 0x89, 0x5a, 0x96, 0xfd, 0xba, 0xaf
    };
    static const uint8_t cbc_expected[32] = {
        0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
        0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
        0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee,
        0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t output[sizeof(plain_text)];
    size_t output_size = 0;
    size_t size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, sizeof(key_data) * 8u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECB_NO_PADDING);
    status = psa_import_key(&attributes, key_data, sizeof(key_data), &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-ECB setup", "密钥导入失败");
    status = psa_cipher_encrypt_setup(&operation, key, PSA_ALG_ECB_NO_PADDING);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-ECB streaming setup", "加密上下文初始化失败");
    status = psa_cipher_update(&operation, plain_text, sizeof(plain_text), output,
                               sizeof(output), &output_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && output_size == sizeof(output) &&
                      memcmp(output, ecb_expected, sizeof(output)) == 0,
                      "PSA AES-ECB uses XCryptographic", "ECB 向量不匹配");
    status = psa_cipher_finish(&operation, output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0, "PSA AES-ECB finish", "ECB 结束失败");
    (void) psa_cipher_abort(&operation);
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS, "PSA AES-ECB setup", "密钥销毁失败");
    key = MBEDTLS_SVC_KEY_ID_INIT;

    operation = (psa_cipher_operation_t)PSA_CIPHER_OPERATION_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, sizeof(key_data) * 8u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_CBC_NO_PADDING);
    status = psa_import_key(&attributes, key_data, sizeof(key_data), &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CBC setup", "密钥导入失败");
    status = psa_cipher_encrypt_setup(&operation, key, PSA_ALG_CBC_NO_PADDING);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CBC streaming setup", "加密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, iv, sizeof(iv));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CBC IV", "CBC IV 设置失败");
    status = psa_cipher_update(&operation, plain_text, 19, output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 16, "PSA AES-CBC streaming update", "CBC 第一段输出长度错误");
    output_size = size;
    status = psa_cipher_update(&operation, plain_text + 19, sizeof(plain_text) - 19,
                               output + output_size, sizeof(output) - output_size, &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 16, "PSA AES-CBC streaming update", "CBC 第二段输出长度错误");
    output_size += size;
    status = psa_cipher_finish(&operation, output + output_size,
                               sizeof(output) - output_size, &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0 && output_size == sizeof(output) &&
                      memcmp(output, cbc_expected, sizeof(output)) == 0,
                      "PSA AES-CBC uses XCryptographic", "CBC 向量不匹配");
    (void) psa_cipher_abort(&operation);

    operation = (psa_cipher_operation_t)PSA_CIPHER_OPERATION_INIT;
    status = psa_cipher_decrypt_setup(&operation, key, PSA_ALG_CBC_NO_PADDING);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CBC decrypt setup", "解密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, iv, sizeof(iv));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CBC decrypt IV", "解密 IV 设置失败");
    status = psa_cipher_update(&operation, cbc_expected, sizeof(cbc_expected), output,
                               sizeof(output), &output_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && output_size == sizeof(output) &&
                      memcmp(output, plain_text, sizeof(output)) == 0,
                      "PSA AES-CBC decrypt uses XCryptographic", "CBC 解密向量不匹配");
    (void) psa_cipher_abort(&operation);
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS, "PSA AES CBC/ECB setup", "密钥销毁失败");
    XSSL_TEST_PASS("PSA AES-ECB/CBC uses XCryptographic");
    return true;
}

static bool xssl_test_psa_xts_backend(void)
{
    static const uint8_t key_data[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    static const uint8_t key_data_256[64] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
    };
    static const uint8_t data_unit[16] = { 0 };
    static const uint8_t plain_text[17] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10
    };
    static const uint8_t expected_cipher[17] = {
        0xac, 0x2a, 0xe6, 0x0d, 0x18, 0xa7, 0xeb, 0xfd,
        0x1f, 0x08, 0x4d, 0x9b, 0x59, 0x37, 0x25, 0xc1, 0x74
    };
    static const uint8_t expected_cipher_256[17] = {
        0xd1, 0x86, 0xc8, 0x8a, 0x41, 0x78, 0x93, 0x18,
        0xd4, 0xe8, 0xe5, 0xd0, 0x44, 0x48, 0x55, 0xc2, 0xdc
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t output[sizeof(plain_text)];
    size_t output_size = 0;
    size_t size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, sizeof(key_data) * 8u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_XTS);
    status = psa_import_key(&attributes, key_data, sizeof(key_data), &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-XTS setup", "组合密钥导入失败");

    status = psa_cipher_encrypt_setup(&operation, key, PSA_ALG_XTS);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-XTS encrypt setup", "加密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, data_unit, sizeof(data_unit));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-XTS data unit", "数据单元设置失败");
    status = psa_cipher_update(&operation, plain_text, 7, output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0,
                      "PSA AES-XTS split update", "第一段不应输出数据");
    status = psa_cipher_update(&operation, plain_text + 7, sizeof(plain_text) - 7,
                               output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0,
                      "PSA AES-XTS tail update", "尾部数据应保留至 finish");
    status = psa_cipher_finish(&operation, output, sizeof(output), &output_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && output_size == sizeof(output) &&
                      memcmp(output, expected_cipher, sizeof(output)) == 0,
                      "PSA AES-XTS uses XCryptographic", "末块窃取向量不匹配");
    (void) psa_cipher_abort(&operation);

    operation = (psa_cipher_operation_t)PSA_CIPHER_OPERATION_INIT;
    status = psa_cipher_decrypt_setup(&operation, key, PSA_ALG_XTS);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-XTS decrypt setup", "解密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, data_unit, sizeof(data_unit));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-XTS decrypt data unit", "解密数据单元设置失败");
    status = psa_cipher_update(&operation, expected_cipher, sizeof(expected_cipher),
                               output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0,
                      "PSA AES-XTS decrypt update", "解密尾部数据应保留至 finish");
    status = psa_cipher_finish(&operation, output, sizeof(output), &output_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && output_size == sizeof(output) &&
                      memcmp(output, plain_text, sizeof(output)) == 0,
                      "PSA AES-XTS decrypt uses XCryptographic", "末块窃取解密不匹配");
    (void) psa_cipher_abort(&operation);

    operation = (psa_cipher_operation_t)PSA_CIPHER_OPERATION_INIT;
    status = psa_cipher_encrypt_setup(&operation, key, PSA_ALG_XTS);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-XTS short-data setup", "短数据单元测试初始化失败");
    status = psa_cipher_set_iv(&operation, data_unit, sizeof(data_unit));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-XTS short-data unit", "短数据单元设置失败");
    status = psa_cipher_update(&operation, plain_text, 15, output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0,
                      "PSA AES-XTS short-data update", "短数据更新不应输出数据");
    status = psa_cipher_finish(&operation, output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_ERROR_INVALID_ARGUMENT && size == 0,
                      "PSA AES-XTS short-data rejection", "不足一块的数据单元未被拒绝");
    (void) psa_cipher_abort(&operation);
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS,
                      "PSA AES-XTS cleanup", "密钥销毁失败");

    key = MBEDTLS_SVC_KEY_ID_INIT;
    operation = (psa_cipher_operation_t)PSA_CIPHER_OPERATION_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, sizeof(key_data_256) * 8u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_XTS);
    status = psa_import_key(&attributes, key_data_256, sizeof(key_data_256), &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-256-XTS setup", "512 位组合密钥导入失败");
    status = psa_cipher_encrypt_setup(&operation, key, PSA_ALG_XTS);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-256-XTS encrypt setup", "加密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, data_unit, sizeof(data_unit));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-256-XTS data unit", "数据单元设置失败");
    status = psa_cipher_update(&operation, plain_text, sizeof(plain_text),
                               output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0,
                      "PSA AES-256-XTS update", "末块窃取数据应保留至 finish");
    status = psa_cipher_finish(&operation, output, sizeof(output), &output_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && output_size == sizeof(output) &&
                      memcmp(output, expected_cipher_256, sizeof(output)) == 0,
                      "PSA AES-256-XTS uses XCryptographic", "512 位组合密钥向量不匹配");
    (void) psa_cipher_abort(&operation);
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS,
                      "PSA AES-256-XTS cleanup", "512 位组合密钥销毁失败");
    XSSL_TEST_PASS("PSA AES-XTS uses XCryptographic");
    return true;
}

static bool xssl_test_hmac_drbg_backend(void)
{
    XSSL_TEST_REQUIRE(mbedtls_hmac_drbg_self_test(0) == 0,
                      "HMAC-DRBG", "NIST SP 800-90A vector mismatch");
    XSSL_TEST_PASS("HMAC-DRBG uses XCryptographic HMAC");
    return true;
}

static bool xssl_test_ctr_drbg_backend(void)
{
    XSSL_TEST_REQUIRE(mbedtls_ctr_drbg_self_test(0) == 0,
                      "CTR-DRBG", "NIST SP 800-90A vector mismatch");
    XSSL_TEST_PASS("CTR-DRBG uses XCryptographic AES-ECB");
    return true;
}

static bool xssl_test_lms_backend(void)
{
    static const uint8_t identifier[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    static const uint8_t message[] = "XinYueC LMS RFC8554 layout test";
    static const uint8_t root[32] = {
        0x39, 0xcd, 0xc4, 0x80, 0x6c, 0x10, 0x18, 0x91,
        0xed, 0xc6, 0x05, 0x55, 0xc2, 0x7e, 0xde, 0xec,
        0xf7, 0xc1, 0xf4, 0x0d, 0xa8, 0x27, 0x3b, 0x47,
        0x6f, 0x0c, 0xf0, 0x99, 0x30, 0x16, 0xb5, 0xbc
    };
    uint8_t public_key[56];
    uint8_t exported_key[56];
    uint8_t lmots_signature[1124];
    uint8_t signature[1452];
    mbedtls_lms_public_t context;
    size_t exported_size = 0;
    size_t index;

    memset(lmots_signature, 0, sizeof(lmots_signature));
    lmots_signature[3] = 4;
    for (index = 32; index < sizeof(lmots_signature); ++index) {
        lmots_signature[index] = (uint8_t)(index * 37u + 11u);
    }
    memset(signature, 0, sizeof(signature));
    signature[3] = 42;
    memcpy(signature + 4, lmots_signature, sizeof(lmots_signature));
    signature[1131] = 6;
    for (index = 0; index < 10 * 32; ++index) {
        size_t height = index / 32;
        signature[1132 + index] = (uint8_t)(height * 29u +
                                              (index % 32) * 7u + 3u);
    }
    memset(public_key, 0, sizeof(public_key));
    public_key[3] = 6;
    public_key[7] = 4;
    memcpy(public_key + 8, identifier, sizeof(identifier));
    memcpy(public_key + 24, root, sizeof(root));

    mbedtls_lms_public_init(&context);
    XSSL_TEST_REQUIRE(mbedtls_lms_import_public_key(&context, public_key,
                                                     sizeof(public_key)) == 0 &&
                      mbedtls_lms_export_public_key(&context, exported_key,
                                                    sizeof(exported_key),
                                                    &exported_size) == 0 &&
                      exported_size == sizeof(exported_key) &&
                      memcmp(exported_key, public_key, sizeof(public_key)) == 0 &&
                      mbedtls_lms_verify(&context, message, sizeof(message) - 1,
                                         signature, sizeof(signature)) == 0,
                      "LMS", "XCryptographic LMS 适配验签失败");
    signature[1200] ^= 1u;
    XSSL_TEST_REQUIRE(mbedtls_lms_verify(&context, message, sizeof(message) - 1,
                                         signature, sizeof(signature)) != 0,
                      "LMS", "篡改认证路径未被拒绝");
    mbedtls_lms_public_free(&context);
    XSSL_TEST_PASS("LMS/LM-OTS uses XCryptographic");
    return true;
}

static bool xssl_test_psa_cbc_pkcs7_backend(void)
{
    static const uint8_t key_data[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const uint8_t iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t plain_text[21] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e
    };
    static const uint8_t expected_cipher[32] = {
        0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
        0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
        0x40, 0x05, 0x39, 0x32, 0xeb, 0xc8, 0x0b, 0x58,
        0x11, 0x83, 0x69, 0x06, 0x25, 0x52, 0xa0, 0x1d
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t output[sizeof(expected_cipher)];
    size_t output_size = 0;
    size_t size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, sizeof(key_data) * 8u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_CBC_PKCS7);
    status = psa_import_key(&attributes, key_data, sizeof(key_data), &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CBC-PKCS7 setup", "密钥导入失败");

    status = psa_cipher_encrypt_setup(&operation, key, PSA_ALG_CBC_PKCS7);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CBC-PKCS7 encrypt setup", "加密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, iv, sizeof(iv));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CBC-PKCS7 IV", "CBC-PKCS7 IV 设置失败");
    status = psa_cipher_update(&operation, plain_text, 7, output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0, "PSA AES-CBC-PKCS7 streaming update", "第一段输出长度错误");
    status = psa_cipher_update(&operation, plain_text + 7, sizeof(plain_text) - 7,
                               output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 16, "PSA AES-CBC-PKCS7 streaming update", "第二段输出长度错误");
    output_size = size;
    status = psa_cipher_finish(&operation, output + output_size,
                               sizeof(output) - output_size, &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 16 && output_size + size == sizeof(output) &&
                      memcmp(output, expected_cipher, sizeof(output)) == 0,
                      "PSA AES-CBC-PKCS7 encrypt uses XCryptographic", "CBC-PKCS7 加密向量不匹配");
    (void) psa_cipher_abort(&operation);

    operation = (psa_cipher_operation_t)PSA_CIPHER_OPERATION_INIT;
    status = psa_cipher_decrypt_setup(&operation, key, PSA_ALG_CBC_PKCS7);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CBC-PKCS7 decrypt setup", "解密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, iv, sizeof(iv));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CBC-PKCS7 decrypt IV", "解密 IV 设置失败");
    status = psa_cipher_update(&operation, expected_cipher, sizeof(expected_cipher), output,
                               sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 16, "PSA AES-CBC-PKCS7 decrypt update", "解密更新失败");
    output_size = size;
    status = psa_cipher_finish(&operation, output + output_size,
                               sizeof(output) - output_size, &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == sizeof(plain_text) - 16u &&
                      output_size + size == sizeof(plain_text) &&
                      memcmp(output, plain_text, sizeof(plain_text)) == 0,
                      "PSA AES-CBC-PKCS7 decrypt uses XCryptographic", "CBC-PKCS7 解密向量不匹配");
    (void) psa_cipher_abort(&operation);
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS, "PSA AES-CBC-PKCS7 cleanup", "密钥销毁失败");
    XSSL_TEST_PASS("PSA AES-CBC-PKCS7 uses XCryptographic");
    return true;
}

static bool xssl_test_psa_cfb_ofb_backend(void)
{
    static const uint8_t key_data[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const uint8_t iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t plain_text[32] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51
    };
    static const uint8_t cfb_expected[32] = {
        0x3b, 0x3f, 0xd9, 0x2e, 0xb7, 0x2d, 0xad, 0x20,
        0x33, 0x34, 0x49, 0xf8, 0xe8, 0x3c, 0xfb, 0x4a,
        0xc8, 0xa6, 0x45, 0x37, 0xa0, 0xb3, 0xa9, 0x3f,
        0xcd, 0xe3, 0xcd, 0xad, 0x9f, 0x1c, 0xe5, 0x8b
    };
    static const uint8_t ofb_expected[32] = {
        0x3b, 0x3f, 0xd9, 0x2e, 0xb7, 0x2d, 0xad, 0x20,
        0x33, 0x34, 0x49, 0xf8, 0xe8, 0x3c, 0xfb, 0x4a,
        0x77, 0x89, 0x50, 0x8d, 0x16, 0x91, 0x8f, 0x03,
        0xf5, 0x3c, 0x52, 0xda, 0xc5, 0x4e, 0xd8, 0x25
    };
    psa_algorithm_t algorithms[] = { PSA_ALG_CFB, PSA_ALG_OFB };
    const uint8_t* expected[] = { cfb_expected, ofb_expected };
    size_t index;

    for (index = 0; index < sizeof(algorithms) / sizeof(algorithms[0]); ++index) {
        psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
        psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
        mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
        uint8_t output[sizeof(plain_text)];
        size_t output_size = 0;
        size_t size = 0;
        psa_status_t status;

        psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
        psa_set_key_bits(&attributes, sizeof(key_data) * 8u);
        psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
        psa_set_key_algorithm(&attributes, algorithms[index]);
        status = psa_import_key(&attributes, key_data, sizeof(key_data), &key);
        psa_reset_key_attributes(&attributes);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CFB/OFB setup", "密钥导入失败");
        status = psa_cipher_encrypt_setup(&operation, key, algorithms[index]);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CFB/OFB encrypt setup", "加密上下文初始化失败");
        status = psa_cipher_set_iv(&operation, iv, sizeof(iv));
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CFB/OFB IV", "初始向量设置失败");
        status = psa_cipher_update(&operation, plain_text, 21, output, sizeof(output), &size);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 21,
                          "PSA AES-CFB/OFB streaming", "第一段输出长度错误");
        output_size = size;
        status = psa_cipher_update(&operation, plain_text + 21, sizeof(plain_text) - 21,
                                   output + output_size, sizeof(output) - output_size, &size);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == sizeof(plain_text) - output_size,
                          "PSA AES-CFB/OFB streaming", "第二段输出长度错误");
        output_size += size;
        status = psa_cipher_finish(&operation, output + output_size,
                                   sizeof(output) - output_size, &size);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0 && output_size == sizeof(output) &&
                          memcmp(output, expected[index], sizeof(output)) == 0,
                          "PSA AES-CFB/OFB uses XCryptographic", "加密向量不匹配");
        (void) psa_cipher_abort(&operation);
        operation = (psa_cipher_operation_t)PSA_CIPHER_OPERATION_INIT;
        status = psa_cipher_decrypt_setup(&operation, key, algorithms[index]);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CFB/OFB decrypt setup", "解密上下文初始化失败");
        status = psa_cipher_set_iv(&operation, iv, sizeof(iv));
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CFB/OFB decrypt IV", "解密初始向量设置失败");
        status = psa_cipher_update(&operation, expected[index], sizeof(output), output,
                                   sizeof(output), &output_size);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS && output_size == sizeof(output) &&
                          memcmp(output, plain_text, sizeof(output)) == 0,
                          "PSA AES-CFB/OFB decrypt uses XCryptographic", "解密向量不匹配");
        (void) psa_cipher_abort(&operation);
        XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS,
                          "PSA AES-CFB/OFB setup", "密钥销毁失败");
    }
    XSSL_TEST_PASS("PSA AES-CFB/OFB uses XCryptographic");
    return true;
}

static bool xssl_test_psa_chacha20_backend(void)
{
    static const uint8_t key_data[32] = { 0 };
    static const uint8_t nonce[12] = { 0 };
    static const uint8_t plain_text[64] = { 0 };
    static const uint8_t expected[64] = {
        0x76, 0xb8, 0xe0, 0xad, 0xa0, 0xf1, 0x3d, 0x90,
        0x40, 0x5d, 0x6a, 0xe5, 0x53, 0x86, 0xbd, 0x28,
        0xbd, 0xd2, 0x19, 0xb8, 0xa0, 0x8d, 0xed, 0x1a,
        0xa8, 0x36, 0xef, 0xcc, 0x8b, 0x77, 0x0d, 0xc7,
        0xda, 0x41, 0x59, 0x7c, 0x51, 0x57, 0x48, 0x8d,
        0x77, 0x24, 0xe0, 0x3f, 0xb8, 0xd8, 0x4a, 0x37,
        0x6a, 0x43, 0xb8, 0xf4, 0x15, 0x18, 0xa1, 0x1c,
        0xc3, 0x87, 0xb6, 0x69, 0xb2, 0xee, 0x65, 0x86
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t output[sizeof(plain_text)];
    size_t output_size = 0;
    size_t size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_CHACHA20);
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_STREAM_CIPHER);
    status = psa_import_key(&attributes, key_data, sizeof(key_data), &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ChaCha20 setup", "密钥导入失败");
    status = psa_cipher_encrypt_setup(&operation, key, PSA_ALG_STREAM_CIPHER);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ChaCha20 streaming setup", "加密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, nonce, sizeof(nonce));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ChaCha20 IV", "nonce 设置失败");
    status = psa_cipher_update(&operation, plain_text, 17, output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 17, "PSA ChaCha20 streaming", "第一段输出长度错误");
    output_size = size;
    status = psa_cipher_update(&operation, plain_text + output_size, sizeof(plain_text) - output_size,
                               output + output_size, sizeof(output) - output_size, &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == sizeof(plain_text) - output_size,
                      "PSA ChaCha20 streaming", "第二段输出长度错误");
    output_size += size;
    status = psa_cipher_finish(&operation, output + output_size,
                               sizeof(output) - output_size, &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0 && output_size == sizeof(output) &&
                      memcmp(output, expected, sizeof(output)) == 0,
                      "PSA ChaCha20 uses XCryptographic", "RFC 7539 向量不匹配");
    (void) psa_cipher_abort(&operation);
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS, "PSA ChaCha20 setup", "密钥销毁失败");
    XSSL_TEST_PASS("PSA ChaCha20 uses XCryptographic");
    return true;
}

static bool xssl_test_psa_chacha20_edges(void)
{
    static const uint8_t key_data[32] = { 0 };
    static const uint8_t nonce[12] = { 0 };
    static const uint8_t plain_text[96] = { 0 };
    static const uint8_t expected[96] = {
        0x76, 0xb8, 0xe0, 0xad, 0xa0, 0xf1, 0x3d, 0x90,
        0x40, 0x5d, 0x6a, 0xe5, 0x53, 0x86, 0xbd, 0x28,
        0xbd, 0xd2, 0x19, 0xb8, 0xa0, 0x8d, 0xed, 0x1a,
        0xa8, 0x36, 0xef, 0xcc, 0x8b, 0x77, 0x0d, 0xc7,
        0xda, 0x41, 0x59, 0x7c, 0x51, 0x57, 0x48, 0x8d,
        0x77, 0x24, 0xe0, 0x3f, 0xb8, 0xd8, 0x4a, 0x37,
        0x6a, 0x43, 0xb8, 0xf4, 0x15, 0x18, 0xa1, 0x1c,
        0xc3, 0x87, 0xb6, 0x69, 0xb2, 0xee, 0x65, 0x86,
        0x9f, 0x07, 0xe7, 0xbe, 0x55, 0x51, 0x38, 0x7a,
        0x98, 0xba, 0x97, 0x7c, 0x73, 0x2d, 0x08, 0x0d,
        0xcb, 0x0f, 0x29, 0xa0, 0x48, 0xe3, 0x65, 0x69,
        0x12, 0xc6, 0x53, 0x3e, 0x32, 0xee, 0x7a, 0xed
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
    uint8_t output[sizeof(plain_text) + 16];
    size_t size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_CHACHA20);
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_STREAM_CIPHER);
    status = psa_import_key(&attributes, key_data, sizeof(key_data), &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ChaCha20 edges setup", "密钥导入失败");

    /* 未调用 set_iv 时，PSA 核心应返回 BAD_STATE。 */
    status = psa_cipher_encrypt_setup(&operation, key, PSA_ALG_STREAM_CIPHER);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ChaCha20 missing-IV setup", "加密上下文初始化失败");
    status = psa_cipher_update(&operation, plain_text, 1, output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_ERROR_BAD_STATE, "PSA ChaCha20 missing set_iv", "未设置 IV 时应返回 BAD_STATE");
    (void) psa_cipher_abort(&operation);

    /* 零长度输入应成功返回且不影响后续数据流。 */
    status = psa_cipher_encrypt_setup(&operation, key, PSA_ALG_STREAM_CIPHER);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ChaCha20 zero-length setup", "加密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, nonce, sizeof(nonce));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ChaCha20 zero-length IV", "nonce 设置失败");
    status = psa_cipher_update(&operation, plain_text, 0, output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0, "PSA ChaCha20 zero-length update", "零长度输入行为错误");
    status = psa_cipher_update(&operation, plain_text, 1, output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 1, "PSA ChaCha20 zero-length then data", "零长度输入后继续更新失败");
    (void) psa_cipher_abort(&operation);

    /* 输出缓冲区过小时应返回 BUFFER_TOO_SMALL，而不是 INVALID_ARGUMENT。 */
    status = psa_cipher_encrypt_setup(&operation, key, PSA_ALG_STREAM_CIPHER);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ChaCha20 buffer setup", "加密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, nonce, sizeof(nonce));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ChaCha20 buffer IV", "nonce 设置失败");
    status = psa_cipher_update(&operation, plain_text, 8, output, 4, &size);
    XSSL_TEST_REQUIRE(status == PSA_ERROR_BUFFER_TOO_SMALL, "PSA ChaCha20 buffer too small", "输出缓冲区过小应返回 BUFFER_TOO_SMALL");
    (void) psa_cipher_abort(&operation);

    /* 恰好在 64 字节 ChaCha20 块边界处拆分，然后清理。 */
    status = psa_cipher_encrypt_setup(&operation, key, PSA_ALG_STREAM_CIPHER);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ChaCha20 boundary setup", "加密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, nonce, sizeof(nonce));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ChaCha20 boundary IV", "nonce 设置失败");
    status = psa_cipher_update(&operation, plain_text, 64, output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 64, "PSA ChaCha20 boundary update1", "第一段输出长度错误");
    status = psa_cipher_update(&operation, plain_text + 64, sizeof(plain_text) - 64,
                               output + 64, sizeof(output) - 64, &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == sizeof(plain_text) - 64,
                      "PSA ChaCha20 boundary update2", "第二段输出长度错误");
    status = psa_cipher_finish(&operation, output + sizeof(plain_text),
                               sizeof(output) - sizeof(plain_text), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0 &&
                      memcmp(output, expected, sizeof(expected)) == 0,
                      "PSA ChaCha20 boundary vector", "跨 64 字节边界向量不匹配");
    status = psa_cipher_abort(&operation);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ChaCha20 cleanup", "abort 清理失败");

    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS, "PSA ChaCha20 edges setup", "密钥销毁失败");
    XSSL_TEST_PASS("PSA ChaCha20 edge cases");
    return true;
}

static bool xssl_test_psa_ccm_star_no_tag_backend(void)
{
    static const uint8_t key_data[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const uint8_t nonce[13] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c
    };
    static const uint8_t plain_text[64] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
    };
    static const uint8_t expected[64] = {
        0x4e, 0xb6, 0x70, 0x73, 0xf4, 0x4d, 0x26, 0x88,
        0x66, 0x42, 0x21, 0x8d, 0xbf, 0xa9, 0x2f, 0x22,
        0x18, 0xb1, 0x22, 0x63, 0x7a, 0x50, 0xb0, 0x0e,
        0xcf, 0x27, 0x28, 0x78, 0xca, 0xe1, 0x5f, 0x87,
        0x99, 0x82, 0x4d, 0x86, 0x4e, 0xb7, 0x0c, 0x41,
        0xea, 0x9d, 0x2e, 0x6f, 0x4f, 0x0a, 0xc5, 0x81,
        0x1f, 0x44, 0x4a, 0x73, 0x39, 0x3f, 0xf3, 0x24,
        0x36, 0xb0, 0x15, 0x3c, 0xeb, 0x5f, 0xea, 0x64
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t output[sizeof(plain_text)];
    size_t output_size = 0;
    size_t size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, sizeof(key_data) * 8u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_CCM_STAR_NO_TAG);
    status = psa_import_key(&attributes, key_data, sizeof(key_data), &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA CCM*-no-tag setup", "密钥导入失败");
    status = psa_cipher_encrypt_setup(&operation, key, PSA_ALG_CCM_STAR_NO_TAG);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA CCM*-no-tag streaming setup", "加密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, nonce, sizeof(nonce));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA CCM*-no-tag IV", "nonce 设置失败");
    status = psa_cipher_update(&operation, plain_text, 17, output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 17, "PSA CCM*-no-tag streaming", "第一段输出长度错误");
    output_size = size;
    status = psa_cipher_update(&operation, plain_text + output_size, sizeof(plain_text) - output_size,
                               output + output_size, sizeof(output) - output_size, &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == sizeof(plain_text) - output_size,
                      "PSA CCM*-no-tag streaming", "第二段输出长度错误");
    output_size += size;
    status = psa_cipher_finish(&operation, output + output_size,
                               sizeof(output) - output_size, &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0 && output_size == sizeof(expected) &&
                      memcmp(output, expected, sizeof(expected)) == 0,
                      "PSA CCM*-no-tag uses XCryptographic", "CCM*-无标签向量不匹配");
    (void) psa_cipher_abort(&operation);

    operation = (psa_cipher_operation_t)PSA_CIPHER_OPERATION_INIT;
    status = psa_cipher_decrypt_setup(&operation, key, PSA_ALG_CCM_STAR_NO_TAG);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA CCM*-no-tag decrypt setup", "解密上下文初始化失败");
    status = psa_cipher_set_iv(&operation, nonce, sizeof(nonce));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA CCM*-no-tag decrypt IV", "解密 nonce 设置失败");
    status = psa_cipher_update(&operation, expected, sizeof(expected), output,
                               sizeof(output), &output_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && output_size == sizeof(plain_text) &&
                      memcmp(output, plain_text, sizeof(plain_text)) == 0,
                      "PSA CCM*-no-tag decrypt uses XCryptographic", "解密结果错误");
    (void) psa_cipher_abort(&operation);
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS, "PSA CCM*-no-tag setup", "密钥销毁失败");
    XSSL_TEST_PASS("PSA CCM*-no-tag uses XCryptographic");
    return true;
}

static bool xssl_test_psa_p256_backend(void)
{
    static const uint8_t hash[32] = {
        0x9f, 0x86, 0xd0, 0x81, 0x88, 0x4c, 0x7d, 0x65,
        0x9a, 0x2f, 0xea, 0xa0, 0xc5, 0x5a, 0xd0, 0x15,
        0xa3, 0xbf, 0x4f, 0x1b, 0x2b, 0x0b, 0x82, 0x2c,
        0xd1, 0x5d, 0x6c, 0x15, 0xb0, 0xf0, 0x0a, 0x08
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t first = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t second = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t signing = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t verification = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t first_public[65];
    uint8_t second_public[65];
    uint8_t first_secret[32];
    uint8_t second_secret[32];
    uint8_t signature[64];
    size_t first_public_size = 0;
    size_t second_public_size = 0;
    size_t first_secret_size = 0;
    size_t second_secret_size = 0;
    size_t signature_size = 0;
    psa_status_t status;
    psa_algorithm_t ecdsa = PSA_ALG_ECDSA(PSA_ALG_SHA_256);

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_generate_key(&attributes, &first);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-256 ECDH setup", "第一把密钥生成失败");
    status = psa_generate_key(&attributes, &second);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-256 ECDH setup", "第二把密钥生成失败");
    psa_reset_key_attributes(&attributes);
    status = psa_export_public_key(first, first_public, sizeof(first_public), &first_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_public_size == sizeof(first_public),
                      "PSA P-256 ECDH public key", "第一把公钥导出失败");
    status = psa_export_public_key(second, second_public, sizeof(second_public), &second_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_public_size == sizeof(second_public),
                      "PSA P-256 ECDH public key", "第二把公钥导出失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, first, second_public, second_public_size,
                                   first_secret, sizeof(first_secret), &first_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_secret_size == sizeof(first_secret),
                      "PSA P-256 ECDH uses XCryptographic", "第一方向共享密钥失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, second, first_public, first_public_size,
                                   second_secret, sizeof(second_secret), &second_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_secret_size == sizeof(second_secret) &&
                      memcmp(first_secret, second_secret, sizeof(first_secret)) == 0,
                      "PSA P-256 ECDH uses XCryptographic", "双方共享密钥不一致");
    XSSL_TEST_REQUIRE(psa_destroy_key(first) == PSA_SUCCESS && psa_destroy_key(second) == PSA_SUCCESS,
                      "PSA P-256 ECDH setup", "ECDH 密钥销毁失败");

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, ecdsa);
    status = psa_generate_key(&attributes, &signing);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-256 ECDSA setup", "签名密钥生成失败");
    status = psa_sign_hash(signing, ecdsa, hash, sizeof(hash), signature,
                           sizeof(signature), &signature_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && signature_size == sizeof(signature),
                      "PSA P-256 ECDSA uses XCryptographic", "摘要签名失败");
    status = psa_export_public_key(signing, first_public, sizeof(first_public), &first_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_public_size == sizeof(first_public),
                      "PSA P-256 ECDSA public key", "签名公钥导出失败");
    XSSL_TEST_REQUIRE(psa_destroy_key(signing) == PSA_SUCCESS,
                      "PSA P-256 ECDSA setup", "签名密钥销毁失败");
    signing = MBEDTLS_SVC_KEY_ID_INIT;
    psa_reset_key_attributes(&attributes);
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes, ecdsa);
    status = psa_import_key(&attributes, first_public, first_public_size, &verification);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-256 ECDSA setup", "验签公钥导入失败");
    status = psa_verify_hash(verification, ecdsa, hash, sizeof(hash), signature, signature_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-256 ECDSA uses XCryptographic", "公钥验签失败");
    signature[0] ^= 1u;
    status = psa_verify_hash(verification, ecdsa, hash, sizeof(hash), signature, signature_size);
    XSSL_TEST_REQUIRE(status == PSA_ERROR_INVALID_SIGNATURE,
                      "PSA P-256 ECDSA rejection", "篡改签名未被拒绝");
    XSSL_TEST_REQUIRE(psa_destroy_key(verification) == PSA_SUCCESS,
                      "PSA P-256 ECDSA setup", "验签密钥销毁失败");
    XSSL_TEST_PASS("PSA P-256 ECDH/ECDSA uses XCryptographic");
    return true;
}

static bool xssl_test_psa_rsa_backend(void)
{
    static const uint8_t hash[32] = {
        0x9f, 0x86, 0xd0, 0x81, 0x88, 0x4c, 0x7d, 0x65,
        0x9a, 0x2f, 0xea, 0xa0, 0xc5, 0x5a, 0xd0, 0x15,
        0xa3, 0xbf, 0x4f, 0x1b, 0x2b, 0x0b, 0x82, 0x2c,
        0xd1, 0x5d, 0x6c, 0x15, 0xb0, 0xf0, 0x0a, 0x08
    };
    static const uint8_t message[] = "XCryptographic RSA PSA 回归消息";
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t signing = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t decrypting = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t oaep_signing = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t oaep_decrypting = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t verify_pub = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t encrypt_pub = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t pss_verify_pub = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t oaep_encrypt_pub = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t priv_der[2048];
    uint8_t pub_der[512];
    uint8_t cipher[256];
    uint8_t plain[256];
    uint8_t signature[256];
    size_t priv_size = 0;
    size_t pub_size = 0;
    size_t cipher_size = 0;
    size_t plain_size = 0;
    size_t sig_size = 0;
    psa_status_t status;

    /* 生成 1024 位 RSA 密钥对，导出并通过 PSA 重新导入 */
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_KEY_PAIR);
    psa_set_key_bits(&attributes, 1024);
    psa_set_key_usage_flags(&attributes,
                            PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_RSA_PKCS1V15_CRYPT);
    status = psa_generate_key(&attributes, &signing);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA RSA setup", "RSA 密钥对生成失败");
    status = psa_export_key(signing, priv_der, sizeof(priv_der), &priv_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && priv_size > 0,
                      "PSA RSA export/import", "私钥导出失败");
    status = psa_export_public_key(signing, pub_der, sizeof(pub_der), &pub_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && pub_size > 0,
                      "PSA RSA public key", "公钥导出失败");
    XSSL_TEST_REQUIRE(psa_destroy_key(signing) == PSA_SUCCESS,
                      "PSA RSA setup", "生成密钥销毁失败");
    signing = MBEDTLS_SVC_KEY_ID_INIT;

    /* PKCS1 私钥：解密 */
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_KEY_PAIR);
    psa_set_key_bits(&attributes, 1024);
    psa_set_key_usage_flags(&attributes,
                            PSA_KEY_USAGE_DECRYPT | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_RSA_PKCS1V15_CRYPT);
    status = psa_import_key(&attributes, priv_der, priv_size, &decrypting);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA RSA export/import", "私钥导入失败");

    /* PKCS1 私钥：签名 */
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_KEY_PAIR);
    psa_set_key_bits(&attributes, 1024);
    psa_set_key_usage_flags(&attributes,
                            PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes,
                          PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, priv_der, priv_size, &signing);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA RSA export/import", "签名私钥导入失败");

    /* OAEP 私钥：解密 */
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_KEY_PAIR);
    psa_set_key_bits(&attributes, 1024);
    psa_set_key_usage_flags(&attributes,
                            PSA_KEY_USAGE_DECRYPT | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_RSA_OAEP(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, priv_der, priv_size, &oaep_decrypting);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA RSA OAEP", "OAEP 私钥导入失败");

    /* OAEP 私钥：PSS 签名 */
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_KEY_PAIR);
    psa_set_key_bits(&attributes, 1024);
    psa_set_key_usage_flags(&attributes,
                            PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_RSA_PSS(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, priv_der, priv_size, &oaep_signing);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA RSA PSS", "PSS 私钥导入失败");

    /* 公钥：PKCS1 验签 */
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_PUBLIC_KEY);
    psa_set_key_bits(&attributes, 1024);
    psa_set_key_usage_flags(&attributes,
                            PSA_KEY_USAGE_VERIFY_HASH | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes,
                          PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, pub_der, pub_size, &verify_pub);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA RSA public key", "验签公钥导入失败");

    /* 公钥：PKCS1 加密 */
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_PUBLIC_KEY);
    psa_set_key_bits(&attributes, 1024);
    psa_set_key_usage_flags(&attributes,
                            PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_RSA_PKCS1V15_CRYPT);
    status = psa_import_key(&attributes, pub_der, pub_size, &encrypt_pub);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA RSA public key", "加密公钥导入失败");

    /* 公钥：PSS 验签 */
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_PUBLIC_KEY);
    psa_set_key_bits(&attributes, 1024);
    psa_set_key_usage_flags(&attributes,
                            PSA_KEY_USAGE_VERIFY_HASH | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_RSA_PSS(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, pub_der, pub_size, &pss_verify_pub);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA RSA PSS", "PSS 验签公钥导入失败");

    /* 公钥：OAEP 加密 */
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_PUBLIC_KEY);
    psa_set_key_bits(&attributes, 1024);
    psa_set_key_usage_flags(&attributes,
                            PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, PSA_ALG_RSA_OAEP(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, pub_der, pub_size, &oaep_encrypt_pub);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA RSA OAEP", "OAEP 加密公钥导入失败");

    /* PKCS1 v1.5 加解密 */
    status = psa_asymmetric_encrypt(encrypt_pub, PSA_ALG_RSA_PKCS1V15_CRYPT,
                                    message, sizeof(message) - 1,
                                    NULL, 0, cipher, sizeof(cipher), &cipher_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && cipher_size == 128,
                      "PSA RSA encrypt/decrypt", "PKCS1 加密失败");
    status = psa_asymmetric_decrypt(decrypting, PSA_ALG_RSA_PKCS1V15_CRYPT,
                                    cipher, cipher_size, NULL, 0,
                                    plain, sizeof(plain), &plain_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && plain_size == sizeof(message) - 1 &&
                      memcmp(plain, message, plain_size) == 0,
                      "PSA RSA encrypt/decrypt", "PKCS1 解密失败");

    /* OAEP 加解密 */
    status = psa_asymmetric_encrypt(oaep_encrypt_pub,
                                    PSA_ALG_RSA_OAEP(PSA_ALG_SHA_256),
                                    message, sizeof(message) - 1,
                                    NULL, 0, cipher, sizeof(cipher), &cipher_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && cipher_size == 128,
                      "PSA RSA OAEP", "OAEP 加密失败");
    status = psa_asymmetric_decrypt(oaep_decrypting,
                                    PSA_ALG_RSA_OAEP(PSA_ALG_SHA_256),
                                    cipher, cipher_size, NULL, 0,
                                    plain, sizeof(plain), &plain_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && plain_size == sizeof(message) - 1 &&
                      memcmp(plain, message, plain_size) == 0,
                      "PSA RSA OAEP", "OAEP 解密失败");

    /* PKCS1 v1.5 签名/验签 */
    status = psa_sign_hash(signing, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256),
                           hash, sizeof(hash), signature, sizeof(signature), &sig_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && sig_size == 128,
                      "PSA RSA PKCS1 sign/verify", "PKCS1 签名失败");
    status = psa_verify_hash(verify_pub, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256),
                             hash, sizeof(hash), signature, sig_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA RSA PKCS1 sign/verify", "PKCS1 公钥验签失败");
    signature[0] ^= 1u;
    status = psa_verify_hash(verify_pub, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256),
                             hash, sizeof(hash), signature, sig_size);
    XSSL_TEST_REQUIRE(status == PSA_ERROR_INVALID_SIGNATURE,
                      "PSA RSA PKCS1 rejection", "篡改签名未被拒绝");
    signature[0] ^= 1u;

    /* PSS 签名/验签 */
    status = psa_sign_hash(oaep_signing, PSA_ALG_RSA_PSS(PSA_ALG_SHA_256),
                           hash, sizeof(hash), signature, sizeof(signature), &sig_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && sig_size == 128,
                      "PSA RSA PSS sign/verify", "PSS 签名失败");
    status = psa_verify_hash(pss_verify_pub, PSA_ALG_RSA_PSS(PSA_ALG_SHA_256),
                             hash, sizeof(hash), signature, sig_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA RSA PSS sign/verify", "PSS 公钥验签失败");

    XSSL_TEST_REQUIRE(psa_destroy_key(signing) == PSA_SUCCESS &&
                      psa_destroy_key(decrypting) == PSA_SUCCESS &&
                      psa_destroy_key(oaep_signing) == PSA_SUCCESS &&
                      psa_destroy_key(oaep_decrypting) == PSA_SUCCESS &&
                      psa_destroy_key(verify_pub) == PSA_SUCCESS &&
                      psa_destroy_key(encrypt_pub) == PSA_SUCCESS &&
                      psa_destroy_key(pss_verify_pub) == PSA_SUCCESS &&
                      psa_destroy_key(oaep_encrypt_pub) == PSA_SUCCESS,
                      "PSA RSA setup", "RSA 密钥销毁失败");
    XSSL_TEST_PASS("PSA RSA uses XCryptographic");
    return true;
}

static bool xssl_test_psa_x25519_backend(void)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t first = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t second = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t first_public[32];
    uint8_t second_public[32];
    uint8_t first_secret[32];
    uint8_t second_secret[32];
    size_t first_public_size = 0;
    size_t second_public_size = 0;
    size_t first_secret_size = 0;
    size_t second_secret_size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
    psa_set_key_bits(&attributes, 255);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_generate_key(&attributes, &first);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA X25519 setup", "第一把密钥生成失败");
    status = psa_generate_key(&attributes, &second);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA X25519 setup", "第二把密钥生成失败");
    psa_reset_key_attributes(&attributes);
    status = psa_export_public_key(first, first_public, sizeof(first_public), &first_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_public_size == sizeof(first_public),
                      "PSA X25519 public key", "第一把公钥导出失败");
    status = psa_export_public_key(second, second_public, sizeof(second_public), &second_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_public_size == sizeof(second_public),
                      "PSA X25519 public key", "第二把公钥导出失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, first, second_public, second_public_size,
                                   first_secret, sizeof(first_secret), &first_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_secret_size == sizeof(first_secret),
                      "PSA X25519 uses XCryptographic", "第一方向共享密钥失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, second, first_public, first_public_size,
                                   second_secret, sizeof(second_secret), &second_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_secret_size == sizeof(second_secret) &&
                      memcmp(first_secret, second_secret, sizeof(first_secret)) == 0,
                      "PSA X25519 uses XCryptographic", "双方共享密钥不一致");
    XSSL_TEST_REQUIRE(psa_destroy_key(first) == PSA_SUCCESS && psa_destroy_key(second) == PSA_SUCCESS,
                      "PSA X25519 setup", "密钥销毁失败");
    XSSL_TEST_PASS("PSA X25519 uses XCryptographic");
    return true;
}

static bool xssl_test_psa_x448_backend(void)
{
    static const uint8_t private_four[56] = { [0] = 4 };
    static const uint8_t public_four[56] = {
        0x3f,0x48,0x2c,0x8a,0x9f,0x19,0xb0,0x1e,0x6c,0x46,0xee,0x97,0x11,0xd9,0xdc,0x14,
        0xfd,0x4b,0xf6,0x7a,0xf3,0x07,0x65,0xc2,0xae,0x2b,0x84,0x6a,0x4d,0x23,0xa8,0xcd,
        0x0d,0xb8,0x97,0x08,0x62,0x39,0x49,0x2c,0xaf,0x35,0x0b,0x51,0xf8,0x33,0x86,0x8b,
        0x9b,0xc2,0xb3,0xbc,0xa9,0xcf,0x41,0x13
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t first = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t second = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t imported = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t first_public[56], second_public[56], imported_public[56];
    uint8_t first_secret[56], second_secret[56];
    size_t first_public_size = 0, second_public_size = 0, imported_public_size = 0;
    size_t first_secret_size = 0, second_secret_size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
    psa_set_key_bits(&attributes, 448);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_generate_key(&attributes, &first);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA X448 setup", "第一把密钥生成失败");
    status = psa_generate_key(&attributes, &second);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA X448 setup", "第二把密钥生成失败");
    psa_reset_key_attributes(&attributes);
    status = psa_export_public_key(first, first_public, sizeof(first_public), &first_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_public_size == sizeof(first_public),
                      "PSA X448 public key", "第一把公钥导出失败");
    status = psa_export_public_key(second, second_public, sizeof(second_public), &second_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_public_size == sizeof(second_public),
                      "PSA X448 public key", "第二把公钥导出失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, first, second_public, second_public_size,
                                   first_secret, sizeof(first_secret), &first_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_secret_size == sizeof(first_secret),
                      "PSA X448 uses XCryptographic", "第一方向共享密钥失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, second, first_public, first_public_size,
                                   second_secret, sizeof(second_secret), &second_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_secret_size == sizeof(second_secret) &&
                      memcmp(first_secret, second_secret, sizeof(first_secret)) == 0,
                      "PSA X448 uses XCryptographic", "双方共享密钥不一致");
    XSSL_TEST_REQUIRE(psa_destroy_key(first) == PSA_SUCCESS && psa_destroy_key(second) == PSA_SUCCESS,
                      "PSA X448 setup", "密钥销毁失败");

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_MONTGOMERY));
    psa_set_key_bits(&attributes, 448);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_import_key(&attributes, private_four, sizeof(private_four), &imported);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA X448 import", "固定私钥导入失败");
    status = psa_export_public_key(imported, imported_public, sizeof(imported_public),
                                   &imported_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && imported_public_size == sizeof(imported_public) &&
                      memcmp(imported_public, public_four, sizeof(public_four)) == 0,
                      "PSA X448 import", "导入私钥的 RFC 7748 公钥不匹配");
    XSSL_TEST_REQUIRE(psa_destroy_key(imported) == PSA_SUCCESS,
                      "PSA X448 import", "导入密钥销毁失败");
    XSSL_TEST_PASS("PSA X448 uses XCryptographic");
    return true;
}

static bool xssl_test_psa_secp256k1_backend(void)
{
    static const uint8_t private_one[32] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
    };
    static const uint8_t base_public[65] = {
        0x04,
        0x79,0xbe,0x66,0x7e,0xf9,0xdc,0xbb,0xac,0x55,0xa0,0x62,0x95,0xce,0x87,0x0b,0x07,
        0x02,0x9b,0xfc,0xdb,0x2d,0xce,0x28,0xd9,0x59,0xf2,0x81,0x5b,0x16,0xf8,0x17,0x98,
        0x48,0x3a,0xda,0x77,0x26,0xa3,0xc4,0x65,0x5d,0xa4,0xfb,0xfc,0x0e,0x11,0x08,0xa8,
        0xfd,0x17,0xb4,0x48,0xa6,0x85,0x54,0x19,0x9c,0x47,0xd0,0x8f,0xfb,0x10,0xd4,0xb8
    };
    static const uint8_t hash[32] = {
        0x9f,0x86,0xd0,0x81,0x88,0x4c,0x7d,0x65,0x9a,0x2f,0xea,0xa0,0xc5,0x5a,0xd0,0x15,
        0xa3,0xbf,0x4f,0x1b,0x2b,0x0b,0x82,0x2c,0xd1,0x5d,0x6c,0x15,0xb0,0xf0,0x0a,0x08
    };
    static const uint8_t openssl_signature[64] = {
        0x26,0x3c,0x0b,0x76,0x37,0x4c,0x55,0x06,0xeb,0xb2,0xf5,0x74,0x00,0xef,0x05,0xf7,
        0x20,0x45,0xd7,0x6c,0x09,0x03,0x92,0x90,0xf8,0xd4,0x49,0xa5,0xa8,0x82,0x16,0x60,
        0x3f,0x3d,0x08,0x59,0x3c,0xd2,0xe0,0x21,0x45,0xf2,0x1d,0x29,0x4b,0x4c,0xec,0xe7,
        0x99,0xef,0x4f,0xd8,0xee,0x86,0xd3,0x09,0xf0,0x5a,0x77,0x5f,0xe6,0xb7,0xed,0x6d
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t first = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t second = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t imported = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t signing = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t verification = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t first_public[65];
    uint8_t second_public[65];
    uint8_t imported_public[65];
    uint8_t first_secret[32];
    uint8_t second_secret[32];
    uint8_t signature[64];
    size_t first_public_size = 0;
    size_t second_public_size = 0;
    size_t imported_public_size = 0;
    size_t first_secret_size = 0;
    size_t second_secret_size = 0;
    size_t signature_size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_K1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_generate_key(&attributes, &first);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA secp256k1 ECDH setup", "第一把密钥生成失败");
    status = psa_generate_key(&attributes, &second);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA secp256k1 ECDH setup", "第二把密钥生成失败");
    psa_reset_key_attributes(&attributes);
    status = psa_export_public_key(first, first_public, sizeof(first_public), &first_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_public_size == sizeof(first_public),
                      "PSA secp256k1 ECDH public key", "第一把公钥导出失败");
    status = psa_export_public_key(second, second_public, sizeof(second_public), &second_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_public_size == sizeof(second_public),
                      "PSA secp256k1 ECDH public key", "第二把公钥导出失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, first, second_public, second_public_size,
                                   first_secret, sizeof(first_secret), &first_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_secret_size == sizeof(first_secret),
                      "PSA secp256k1 ECDH uses XCryptographic", "第一方向共享密钥失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, second, first_public, first_public_size,
                                   second_secret, sizeof(second_secret), &second_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_secret_size == sizeof(second_secret) &&
                      memcmp(first_secret, second_secret, sizeof(first_secret)) == 0,
                      "PSA secp256k1 ECDH uses XCryptographic", "双方共享密钥不一致");
    XSSL_TEST_REQUIRE(psa_destroy_key(first) == PSA_SUCCESS && psa_destroy_key(second) == PSA_SUCCESS,
                      "PSA secp256k1 ECDH setup", "ECDH 密钥销毁失败");

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_K1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_import_key(&attributes, private_one, sizeof(private_one), &imported);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA secp256k1 ECDH import", "私钥导入失败");
    status = psa_export_public_key(imported, imported_public, sizeof(imported_public),
                                   &imported_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && imported_public_size == sizeof(imported_public) &&
                      memcmp(imported_public, base_public, sizeof(base_public)) == 0,
                      "PSA secp256k1 ECDH import", "导入私钥的公钥不匹配基点");
    XSSL_TEST_REQUIRE(psa_destroy_key(imported) == PSA_SUCCESS,
                      "PSA secp256k1 ECDH import", "导入密钥销毁失败");
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_K1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, private_one, sizeof(private_one), &signing);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA secp256k1 ECDSA setup", "签名私钥导入失败");
    status = psa_sign_hash(signing, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash, sizeof(hash),
                           signature, sizeof(signature), &signature_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && signature_size == sizeof(signature),
                      "PSA secp256k1 ECDSA uses XCryptographic", "摘要签名失败");
    XSSL_TEST_REQUIRE(psa_destroy_key(signing) == PSA_SUCCESS,
                      "PSA secp256k1 ECDSA setup", "签名密钥销毁失败");
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_K1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, base_public, sizeof(base_public), &verification);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA secp256k1 ECDSA setup", "验签公钥导入失败");
    status = psa_verify_hash(verification, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash, sizeof(hash),
                             signature, signature_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS,
                      "PSA secp256k1 ECDSA uses XCryptographic", "XCryptographic 签名验签失败");
    status = psa_verify_hash(verification, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash, sizeof(hash),
                             openssl_signature, sizeof(openssl_signature));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS,
                      "PSA secp256k1 ECDSA OpenSSL vector", "OpenSSL 签名验签失败");
    signature[0] ^= 1u;
    status = psa_verify_hash(verification, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash, sizeof(hash),
                             signature, signature_size);
    XSSL_TEST_REQUIRE(status == PSA_ERROR_INVALID_SIGNATURE,
                      "PSA secp256k1 ECDSA rejection", "篡改签名未被拒绝");
    XSSL_TEST_REQUIRE(psa_destroy_key(verification) == PSA_SUCCESS,
                      "PSA secp256k1 ECDSA setup", "验签密钥销毁失败");
    XSSL_TEST_PASS("PSA secp256k1 ECDH/ECDSA uses XCryptographic");
    return true;
}

static bool xssl_test_psa_p384_backend(void)
{
    static const uint8_t private_one[48] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
    };
    static const uint8_t base_public[97] = {
        0x04,
        0xaa,0x87,0xca,0x22,0xbe,0x8b,0x05,0x37,0x8e,0xb1,0xc7,0x1e,0xf3,0x20,0xad,0x74,
        0x6e,0x1d,0x3b,0x62,0x8b,0xa7,0x9b,0x98,0x59,0xf7,0x41,0xe0,0x82,0x54,0x2a,0x38,
        0x55,0x02,0xf2,0x5d,0xbf,0x55,0x29,0x6c,0x3a,0x54,0x5e,0x38,0x72,0x76,0x0a,0xb7,
        0x36,0x17,0xde,0x4a,0x96,0x26,0x2c,0x6f,0x5d,0x9e,0x98,0xbf,0x92,0x92,0xdc,0x29,
        0xf8,0xf4,0x1d,0xbd,0x28,0x9a,0x14,0x7c,0xe9,0xda,0x31,0x13,0xb5,0xf0,0xb8,0xc0,
        0x0a,0x60,0xb1,0xce,0x1d,0x7e,0x81,0x9d,0x7a,0x43,0x1d,0x7c,0x90,0xea,0x0e,0x5f
    };
    static const uint8_t hash[48] = {
        0x9f,0x86,0xd0,0x81,0x88,0x4c,0x7d,0x65,0x9a,0x2f,0xea,0xa0,0xc5,0x5a,0xd0,0x15,
        0xa3,0xbf,0x4f,0x1b,0x2b,0x0b,0x82,0x2c,0xd1,0x5d,0x6c,0x15,0xb0,0xf0,0x0a,0x08,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
    };
    static const uint8_t openssl_signature[96] = {
        0x2a,0x5d,0x85,0x00,0x37,0xcd,0x09,0x23,0xbe,0x89,0xc1,0xb8,0x8a,0xab,0x2f,0xf4,
        0xd6,0xae,0x0a,0xc0,0x5f,0x76,0xc9,0xcf,0xb1,0xae,0xeb,0x19,0x76,0x9d,0x2c,0x65,
        0x22,0x2e,0x8f,0xd4,0x79,0x68,0xfe,0x5b,0xee,0x73,0xbc,0x97,0x87,0xb7,0xc4,0x91,
        0x14,0xac,0xc5,0x18,0x66,0x3e,0xe6,0x6f,0x37,0xdf,0xd3,0x63,0x8a,0x2d,0x6b,0x81,
        0xab,0xa8,0xf6,0x76,0xe8,0x3e,0xd1,0x88,0x1a,0x88,0x1b,0x38,0x8d,0x7b,0xa7,0xc8,
        0xf4,0x55,0xb7,0x9d,0xb9,0x82,0xec,0xad,0x10,0xc3,0x96,0x30,0x91,0x3d,0x6f,0xf0
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t first = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t second = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t imported = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t signing = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t verification = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t first_public[97];
    uint8_t second_public[97];
    uint8_t imported_public[97];
    uint8_t first_secret[48];
    uint8_t second_secret[48];
    uint8_t signature[96];
    size_t first_public_size = 0;
    size_t second_public_size = 0;
    size_t imported_public_size = 0;
    size_t first_secret_size = 0;
    size_t second_secret_size = 0;
    size_t signature_size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 384);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_generate_key(&attributes, &first);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-384 ECDH setup", "第一把密钥生成失败");
    status = psa_generate_key(&attributes, &second);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-384 ECDH setup", "第二把密钥生成失败");
    psa_reset_key_attributes(&attributes);
    status = psa_export_public_key(first, first_public, sizeof(first_public), &first_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_public_size == sizeof(first_public),
                      "PSA P-384 ECDH public key", "第一把公钥导出失败");
    status = psa_export_public_key(second, second_public, sizeof(second_public), &second_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_public_size == sizeof(second_public),
                      "PSA P-384 ECDH public key", "第二把公钥导出失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, first, second_public, second_public_size,
                                   first_secret, sizeof(first_secret), &first_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_secret_size == sizeof(first_secret),
                      "PSA P-384 ECDH uses XCryptographic", "第一方向共享密钥失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, second, first_public, first_public_size,
                                   second_secret, sizeof(second_secret), &second_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_secret_size == sizeof(second_secret) &&
                      memcmp(first_secret, second_secret, sizeof(first_secret)) == 0,
                      "PSA P-384 ECDH uses XCryptographic", "双方共享密钥不一致");
    XSSL_TEST_REQUIRE(psa_destroy_key(first) == PSA_SUCCESS && psa_destroy_key(second) == PSA_SUCCESS,
                      "PSA P-384 ECDH setup", "ECDH 密钥销毁失败");

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 384);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_import_key(&attributes, private_one, sizeof(private_one), &imported);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-384 ECDH import", "私钥导入失败");
    status = psa_export_public_key(imported, imported_public, sizeof(imported_public),
                                   &imported_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && imported_public_size == sizeof(imported_public) &&
                      memcmp(imported_public, base_public, sizeof(base_public)) == 0,
                      "PSA P-384 ECDH import", "导入私钥的公钥不匹配标准基点");
    XSSL_TEST_REQUIRE(psa_destroy_key(imported) == PSA_SUCCESS,
                      "PSA P-384 ECDH import", "导入密钥销毁失败");
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 384);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_384));
    status = psa_import_key(&attributes, private_one, sizeof(private_one), &signing);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-384 ECDSA setup", "签名私钥导入失败");
    status = psa_sign_hash(signing, PSA_ALG_ECDSA(PSA_ALG_SHA_384), hash, sizeof(hash),
                           signature, sizeof(signature), &signature_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && signature_size == sizeof(signature),
                      "PSA P-384 ECDSA uses XCryptographic", "摘要签名失败");
    XSSL_TEST_REQUIRE(psa_destroy_key(signing) == PSA_SUCCESS,
                      "PSA P-384 ECDSA setup", "签名密钥销毁失败");
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 384);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_384));
    status = psa_import_key(&attributes, base_public, sizeof(base_public), &verification);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-384 ECDSA setup", "验签公钥导入失败");
    status = psa_verify_hash(verification, PSA_ALG_ECDSA(PSA_ALG_SHA_384), hash, sizeof(hash),
                             signature, signature_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS,
                      "PSA P-384 ECDSA uses XCryptographic", "XCryptographic 签名验签失败");
    status = psa_verify_hash(verification, PSA_ALG_ECDSA(PSA_ALG_SHA_384), hash, sizeof(hash),
                             openssl_signature, sizeof(openssl_signature));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS,
                      "PSA P-384 ECDSA OpenSSL vector", "外部签名验签失败");
    signature[0] ^= 1u;
    status = psa_verify_hash(verification, PSA_ALG_ECDSA(PSA_ALG_SHA_384), hash, sizeof(hash),
                             signature, signature_size);
    XSSL_TEST_REQUIRE(status == PSA_ERROR_INVALID_SIGNATURE,
                      "PSA P-384 ECDSA rejection", "篡改签名未被拒绝");
    XSSL_TEST_REQUIRE(psa_destroy_key(verification) == PSA_SUCCESS,
                      "PSA P-384 ECDSA setup", "验签密钥销毁失败");
    XSSL_TEST_PASS("PSA P-384 ECDH/ECDSA uses XCryptographic");
    return true;
}

static bool xssl_test_psa_p521_backend(void)
{
    static const uint8_t private_one[66] = { [65] = 1 };
    static const uint8_t base_public[133] = {
        0x04,
        0x00,0xc6,0x85,0x8e,0x06,0xb7,0x04,0x04,0xe9,0xcd,0x9e,0x3e,0xcb,0x66,0x23,0x95,
        0xb4,0x42,0x9c,0x64,0x81,0x39,0x05,0x3f,0xb5,0x21,0xf8,0x28,0xaf,0x60,0x6b,0x4d,
        0x3d,0xba,0xa1,0x4b,0x5e,0x77,0xef,0xe7,0x59,0x28,0xfe,0x1d,0xc1,0x27,0xa2,0xff,
        0xa8,0xde,0x33,0x48,0xb3,0xc1,0x85,0x6a,0x42,0x9b,0xf9,0x7e,0x7e,0x31,0xc2,0xe5,
        0xbd,0x66,
        0x01,0x18,0x39,0x29,0x6a,0x78,0x9a,0x3b,0xc0,0x04,0x5c,0x8a,0x5f,0xb4,0x2c,0x7d,
        0x1b,0xd9,0x98,0xf5,0x44,0x49,0x57,0x9b,0x44,0x68,0x17,0xaf,0xbd,0x17,0x27,0x3e,
        0x66,0x2c,0x97,0xee,0x72,0x99,0x5e,0xf4,0x26,0x40,0xc5,0x50,0xb9,0x01,0x3f,0xad,
        0x07,0x61,0x35,0x3c,0x70,0x86,0xa2,0x72,0xc2,0x40,0x88,0xbe,0x94,0x76,0x9f,0xd1,
        0x66,0x50
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t first = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t second = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t imported = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t signing = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t verification = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t first_public[133];
    uint8_t second_public[133];
    uint8_t imported_public[133];
    uint8_t first_secret[66];
    uint8_t second_secret[66];
    uint8_t hash[64];
    uint8_t signature[132];
    size_t first_public_size = 0;
    size_t second_public_size = 0;
    size_t imported_public_size = 0;
    size_t first_secret_size = 0;
    size_t second_secret_size = 0;
    size_t signature_size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 521);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_generate_key(&attributes, &first);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-521 ECDH setup", "第一把密钥生成失败");
    status = psa_generate_key(&attributes, &second);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-521 ECDH setup", "第二把密钥生成失败");
    psa_reset_key_attributes(&attributes);
    status = psa_export_public_key(first, first_public, sizeof(first_public), &first_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_public_size == sizeof(first_public),
                      "PSA P-521 ECDH public key", "第一把公钥导出失败");
    status = psa_export_public_key(second, second_public, sizeof(second_public), &second_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_public_size == sizeof(second_public),
                      "PSA P-521 ECDH public key", "第二把公钥导出失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, first, second_public, second_public_size,
                                   first_secret, sizeof(first_secret), &first_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_secret_size == sizeof(first_secret),
                      "PSA P-521 ECDH uses XCryptographic", "第一方向共享密钥失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, second, first_public, first_public_size,
                                   second_secret, sizeof(second_secret), &second_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_secret_size == sizeof(second_secret) &&
                      memcmp(first_secret, second_secret, sizeof(first_secret)) == 0,
                      "PSA P-521 ECDH uses XCryptographic", "双方共享密钥不一致");
    XSSL_TEST_REQUIRE(psa_destroy_key(first) == PSA_SUCCESS && psa_destroy_key(second) == PSA_SUCCESS,
                      "PSA P-521 ECDH setup", "ECDH 密钥销毁失败");

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 521);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_import_key(&attributes, private_one, sizeof(private_one), &imported);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-521 ECDH import", "私钥导入失败");
    status = psa_export_public_key(imported, imported_public, sizeof(imported_public),
                                   &imported_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && imported_public_size == sizeof(imported_public) &&
                      memcmp(imported_public, base_public, sizeof(base_public)) == 0,
                      "PSA P-521 ECDH import", "导入私钥的公钥不匹配标准基点");
    XSSL_TEST_REQUIRE(psa_destroy_key(imported) == PSA_SUCCESS,
                      "PSA P-521 ECDH import", "导入密钥销毁失败");
    memset(hash, 0x5a, sizeof(hash));
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 521);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_512));
    status = psa_import_key(&attributes, private_one, sizeof(private_one), &signing);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-521 ECDSA setup", "签名私钥导入失败");
    status = psa_sign_hash(signing, PSA_ALG_ECDSA(PSA_ALG_SHA_512), hash, sizeof(hash),
                           signature, sizeof(signature), &signature_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && signature_size == sizeof(signature),
                      "PSA P-521 ECDSA uses XCryptographic", "摘要签名失败");
    XSSL_TEST_REQUIRE(psa_destroy_key(signing) == PSA_SUCCESS,
                      "PSA P-521 ECDSA setup", "签名密钥销毁失败");
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 521);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_512));
    status = psa_import_key(&attributes, base_public, sizeof(base_public), &verification);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA P-521 ECDSA setup", "验签公钥导入失败");
    status = psa_verify_hash(verification, PSA_ALG_ECDSA(PSA_ALG_SHA_512), hash, sizeof(hash),
                             signature, signature_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS,
                      "PSA P-521 ECDSA uses XCryptographic", "签名验签失败");
    signature[0] ^= 1u;
    status = psa_verify_hash(verification, PSA_ALG_ECDSA(PSA_ALG_SHA_512), hash, sizeof(hash),
                             signature, signature_size);
    XSSL_TEST_REQUIRE(status == PSA_ERROR_INVALID_SIGNATURE,
                      "PSA P-521 ECDSA rejection", "篡改签名未被拒绝");
    XSSL_TEST_REQUIRE(psa_destroy_key(verification) == PSA_SUCCESS,
                      "PSA P-521 ECDSA setup", "验签密钥销毁失败");
    XSSL_TEST_PASS("PSA P-521 ECDH/ECDSA uses XCryptographic");
    return true;
}

static bool xssl_test_psa_brainpool_p256r1_backend(void)
{
    static const uint8_t private_one[32] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
    };
    static const uint8_t base_public[65] = {
        0x04,
        0x8b,0xd2,0xae,0xb9,0xcb,0x7e,0x57,0xcb,0x2c,0x4b,0x48,0x2f,0xfc,0x81,0xb7,0xaf,
        0xb9,0xde,0x27,0xe1,0xe3,0xbd,0x23,0xc2,0x3a,0x44,0x53,0xbd,0x9a,0xce,0x32,0x62,
        0x54,0x7e,0xf8,0x35,0xc3,0xda,0xc4,0xfd,0x97,0xf8,0x46,0x1a,0x14,0x61,0x1d,0xc9,
        0xc2,0x77,0x45,0x13,0x2d,0xed,0x8e,0x54,0x5c,0x1d,0x54,0xc7,0x2f,0x04,0x69,0x97
    };
    static const uint8_t hash[32] = {
        0x9f,0x86,0xd0,0x81,0x88,0x4c,0x7d,0x65,0x9a,0x2f,0xea,0xa0,0xc5,0x5a,0xd0,0x15,
        0xa3,0xbf,0x4f,0x1b,0x2b,0x0b,0x82,0x2c,0xd1,0x5d,0x6c,0x15,0xb0,0xf0,0x0a,0x08
    };
    static const uint8_t openssl_signature[64] = {
        0x67,0x58,0x0f,0x42,0x53,0xa2,0x07,0x00,0xb9,0xd9,0x0c,0xce,0x2a,0xc8,0x04,0xa4,
        0x7b,0xaf,0x3c,0xb6,0xa4,0x02,0xf8,0x68,0x77,0xc5,0xb2,0xad,0xd9,0xbb,0x7a,0x37,
        0x40,0xaf,0x09,0x6c,0x15,0x2c,0x08,0xf1,0xfa,0xa6,0x0b,0xaa,0x85,0x7e,0x30,0xe7,
        0x38,0x0c,0x4a,0x32,0x04,0xf8,0x36,0x9d,0x3c,0x8e,0x9b,0x95,0x0d,0x7f,0xff,0xc4
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t first = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t second = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t imported = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t signing = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t verification = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t first_public[65];
    uint8_t second_public[65];
    uint8_t imported_public[65];
    uint8_t first_secret[32];
    uint8_t second_secret[32];
    uint8_t signature[64];
    size_t first_public_size = 0;
    size_t second_public_size = 0;
    size_t imported_public_size = 0;
    size_t first_secret_size = 0;
    size_t second_secret_size = 0;
    size_t signature_size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_BRAINPOOL_P_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_generate_key(&attributes, &first);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA brainpoolP256r1 ECDH setup", "第一把密钥生成失败");
    status = psa_generate_key(&attributes, &second);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA brainpoolP256r1 ECDH setup", "第二把密钥生成失败");
    psa_reset_key_attributes(&attributes);
    status = psa_export_public_key(first, first_public, sizeof(first_public), &first_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_public_size == sizeof(first_public),
                      "PSA brainpoolP256r1 ECDH public key", "第一把公钥导出失败");
    status = psa_export_public_key(second, second_public, sizeof(second_public), &second_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_public_size == sizeof(second_public),
                      "PSA brainpoolP256r1 ECDH public key", "第二把公钥导出失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, first, second_public, second_public_size,
                                   first_secret, sizeof(first_secret), &first_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_secret_size == sizeof(first_secret),
                      "PSA brainpoolP256r1 ECDH uses XCryptographic", "第一方向共享密钥失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, second, first_public, first_public_size,
                                   second_secret, sizeof(second_secret), &second_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_secret_size == sizeof(second_secret) &&
                      memcmp(first_secret, second_secret, sizeof(first_secret)) == 0,
                      "PSA brainpoolP256r1 ECDH uses XCryptographic", "双方共享密钥不一致");
    XSSL_TEST_REQUIRE(psa_destroy_key(first) == PSA_SUCCESS && psa_destroy_key(second) == PSA_SUCCESS,
                      "PSA brainpoolP256r1 ECDH setup", "ECDH 密钥销毁失败");

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_BRAINPOOL_P_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_import_key(&attributes, private_one, sizeof(private_one), &imported);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA brainpoolP256r1 ECDH import", "私钥导入失败");
    status = psa_export_public_key(imported, imported_public, sizeof(imported_public),
                                   &imported_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && imported_public_size == sizeof(imported_public) &&
                      memcmp(imported_public, base_public, sizeof(base_public)) == 0,
                      "PSA brainpoolP256r1 ECDH import", "导入私钥的公钥不匹配 RFC 5639 基点");
    XSSL_TEST_REQUIRE(psa_destroy_key(imported) == PSA_SUCCESS,
                      "PSA brainpoolP256r1 ECDH import", "导入密钥销毁失败");
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_BRAINPOOL_P_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, private_one, sizeof(private_one), &signing);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA brainpoolP256r1 ECDSA setup", "签名私钥导入失败");
    status = psa_sign_hash(signing, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash, sizeof(hash),
                           signature, sizeof(signature), &signature_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && signature_size == sizeof(signature),
                      "PSA brainpoolP256r1 ECDSA uses XCryptographic", "摘要签名失败");
    XSSL_TEST_REQUIRE(psa_destroy_key(signing) == PSA_SUCCESS,
                      "PSA brainpoolP256r1 ECDSA setup", "签名密钥销毁失败");
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_BRAINPOOL_P_R1));
    psa_set_key_bits(&attributes, 256);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    status = psa_import_key(&attributes, base_public, sizeof(base_public), &verification);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA brainpoolP256r1 ECDSA setup", "验签公钥导入失败");
    status = psa_verify_hash(verification, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash, sizeof(hash),
                             signature, signature_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS,
                      "PSA brainpoolP256r1 ECDSA uses XCryptographic", "XCryptographic 签名验签失败");
    status = psa_verify_hash(verification, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash, sizeof(hash),
                             openssl_signature, sizeof(openssl_signature));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS,
                      "PSA brainpoolP256r1 ECDSA OpenSSL vector", "OpenSSL 签名验签失败");
    signature[0] ^= 1u;
    status = psa_verify_hash(verification, PSA_ALG_ECDSA(PSA_ALG_SHA_256), hash, sizeof(hash),
                             signature, signature_size);
    XSSL_TEST_REQUIRE(status == PSA_ERROR_INVALID_SIGNATURE,
                      "PSA brainpoolP256r1 ECDSA rejection", "篡改签名未被拒绝");
    XSSL_TEST_REQUIRE(psa_destroy_key(verification) == PSA_SUCCESS,
                      "PSA brainpoolP256r1 ECDSA setup", "验签密钥销毁失败");
    XSSL_TEST_PASS("PSA brainpoolP256r1 ECDH/ECDSA uses XCryptographic");
    return true;
}

static bool xssl_test_psa_brainpool_p384r1_backend(void)
{
    static const uint8_t private_one[48] = { [47] = 1 };
    static const uint8_t base_public[97] = {
        0x04,
        0x1d,0x1c,0x64,0xf0,0x68,0xcf,0x45,0xff,0xa2,0xa6,0x3a,0x81,0xb7,0xc1,0x3f,0x6b,
        0x88,0x47,0xa3,0xe7,0x7e,0xf1,0x4f,0xe3,0xdb,0x7f,0xca,0xfe,0x0c,0xbd,0x10,0xe8,
        0xe8,0x26,0xe0,0x34,0x36,0xd6,0x46,0xaa,0xef,0x87,0xb2,0xe2,0x47,0xd4,0xaf,0x1e,
        0x8a,0xbe,0x1d,0x75,0x20,0xf9,0xc2,0xa4,0x5c,0xb1,0xeb,0x8e,0x95,0xcf,0xd5,0x52,
        0x62,0xb7,0x0b,0x29,0xfe,0xec,0x58,0x64,0xe1,0x9c,0x05,0x4f,0xf9,0x91,0x29,0x28,
        0x0e,0x46,0x46,0x21,0x77,0x91,0x81,0x11,0x42,0x82,0x03,0x41,0x26,0x3c,0x53,0x15
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t first=MBEDTLS_SVC_KEY_ID_INIT, second=MBEDTLS_SVC_KEY_ID_INIT, imported=MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t signing=MBEDTLS_SVC_KEY_ID_INIT, verification=MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t first_public[97], second_public[97], imported_public[97], first_secret[48], second_secret[48];
    uint8_t hash[48], signature[96];
    size_t first_public_size=0, second_public_size=0, imported_public_size=0, first_secret_size=0, second_secret_size=0;
    size_t signature_size=0;
    psa_status_t status;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_BRAINPOOL_P_R1));
    psa_set_key_bits(&attributes,384); psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_DERIVE); psa_set_key_algorithm(&attributes,PSA_ALG_ECDH);
    status=psa_generate_key(&attributes,&first); XSSL_TEST_REQUIRE(status==PSA_SUCCESS,"PSA brainpoolP384r1 ECDH setup","第一把密钥生成失败");
    status=psa_generate_key(&attributes,&second); XSSL_TEST_REQUIRE(status==PSA_SUCCESS,"PSA brainpoolP384r1 ECDH setup","第二把密钥生成失败"); psa_reset_key_attributes(&attributes);
    status=psa_export_public_key(first,first_public,sizeof(first_public),&first_public_size); XSSL_TEST_REQUIRE(status==PSA_SUCCESS&&first_public_size==97,"PSA brainpoolP384r1 public key","第一把公钥导出失败");
    status=psa_export_public_key(second,second_public,sizeof(second_public),&second_public_size); XSSL_TEST_REQUIRE(status==PSA_SUCCESS&&second_public_size==97,"PSA brainpoolP384r1 public key","第二把公钥导出失败");
    status=psa_raw_key_agreement(PSA_ALG_ECDH,first,second_public,second_public_size,first_secret,sizeof(first_secret),&first_secret_size); XSSL_TEST_REQUIRE(status==PSA_SUCCESS&&first_secret_size==48,"PSA brainpoolP384r1 ECDH uses XCryptographic","第一方向共享密钥失败");
    status=psa_raw_key_agreement(PSA_ALG_ECDH,second,first_public,first_public_size,second_secret,sizeof(second_secret),&second_secret_size); XSSL_TEST_REQUIRE(status==PSA_SUCCESS&&second_secret_size==48&&memcmp(first_secret,second_secret,48)==0,"PSA brainpoolP384r1 ECDH uses XCryptographic","双方共享密钥不一致");
    XSSL_TEST_REQUIRE(psa_destroy_key(first)==PSA_SUCCESS&&psa_destroy_key(second)==PSA_SUCCESS,"PSA brainpoolP384r1 ECDH setup","ECDH 密钥销毁失败");
    psa_set_key_type(&attributes,PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_BRAINPOOL_P_R1)); psa_set_key_bits(&attributes,384); psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_DERIVE); psa_set_key_algorithm(&attributes,PSA_ALG_ECDH);
    status=psa_import_key(&attributes,private_one,sizeof(private_one),&imported); psa_reset_key_attributes(&attributes); XSSL_TEST_REQUIRE(status==PSA_SUCCESS,"PSA brainpoolP384r1 ECDH import","私钥导入失败");
    status=psa_export_public_key(imported,imported_public,sizeof(imported_public),&imported_public_size); XSSL_TEST_REQUIRE(status==PSA_SUCCESS&&imported_public_size==97&&memcmp(imported_public,base_public,97)==0,"PSA brainpoolP384r1 ECDH import","导入私钥公钥不匹配 RFC 5639 基点");
    XSSL_TEST_REQUIRE(psa_destroy_key(imported)==PSA_SUCCESS,"PSA brainpoolP384r1 ECDH import","导入密钥销毁失败");
    memset(hash,0x5a,sizeof(hash));
    psa_set_key_type(&attributes,PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_BRAINPOOL_P_R1)); psa_set_key_bits(&attributes,384); psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_SIGN_HASH); psa_set_key_algorithm(&attributes,PSA_ALG_ECDSA(PSA_ALG_SHA_384));
    status=psa_import_key(&attributes,private_one,sizeof(private_one),&signing); psa_reset_key_attributes(&attributes); XSSL_TEST_REQUIRE(status==PSA_SUCCESS,"PSA brainpoolP384r1 ECDSA setup","签名私钥导入失败");
    status=psa_sign_hash(signing,PSA_ALG_ECDSA(PSA_ALG_SHA_384),hash,sizeof(hash),signature,sizeof(signature),&signature_size); XSSL_TEST_REQUIRE(status==PSA_SUCCESS&&signature_size==96,"PSA brainpoolP384r1 ECDSA uses XCryptographic","摘要签名失败");
    XSSL_TEST_REQUIRE(psa_destroy_key(signing)==PSA_SUCCESS,"PSA brainpoolP384r1 ECDSA setup","签名密钥销毁失败");
    psa_set_key_type(&attributes,PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_BRAINPOOL_P_R1)); psa_set_key_bits(&attributes,384); psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_VERIFY_HASH); psa_set_key_algorithm(&attributes,PSA_ALG_ECDSA(PSA_ALG_SHA_384));
    status=psa_import_key(&attributes,base_public,sizeof(base_public),&verification); psa_reset_key_attributes(&attributes); XSSL_TEST_REQUIRE(status==PSA_SUCCESS,"PSA brainpoolP384r1 ECDSA setup","验签公钥导入失败");
    status=psa_verify_hash(verification,PSA_ALG_ECDSA(PSA_ALG_SHA_384),hash,sizeof(hash),signature,signature_size); XSSL_TEST_REQUIRE(status==PSA_SUCCESS,"PSA brainpoolP384r1 ECDSA uses XCryptographic","签名验签失败");
    signature[0]^=1u; status=psa_verify_hash(verification,PSA_ALG_ECDSA(PSA_ALG_SHA_384),hash,sizeof(hash),signature,signature_size); XSSL_TEST_REQUIRE(status==PSA_ERROR_INVALID_SIGNATURE,"PSA brainpoolP384r1 ECDSA rejection","篡改签名未被拒绝");
    XSSL_TEST_REQUIRE(psa_destroy_key(verification)==PSA_SUCCESS,"PSA brainpoolP384r1 ECDSA setup","验签密钥销毁失败");
    XSSL_TEST_PASS("PSA brainpoolP384r1 ECDH/ECDSA uses XCryptographic"); return true;
}

static bool xssl_test_psa_brainpool_p512r1_backend(void)
{
    static const uint8_t private_one[64] = { [63] = 1 };
    static const uint8_t base_public[129] = {
        0x04,
        0x81,0xae,0xe4,0xbd,0xd8,0x2e,0xd9,0x64,0x5a,0x21,0x32,0x2e,0x9c,0x4c,0x6a,0x93,
        0x85,0xed,0x9f,0x70,0xb5,0xd9,0x16,0xc1,0xb4,0x3b,0x62,0xee,0xf4,0xd0,0x09,0x8e,
        0xff,0x3b,0x1f,0x78,0xe2,0xd0,0xd4,0x8d,0x50,0xd1,0x68,0x7b,0x93,0xb9,0x7d,0x5f,
        0x7c,0x6d,0x50,0x47,0x40,0x6a,0x5e,0x68,0x8b,0x35,0x22,0x09,0xbc,0xb9,0xf8,0x22,
        0x7d,0xde,0x38,0x5d,0x56,0x63,0x32,0xec,0xc0,0xea,0xbf,0xa9,0xcf,0x78,0x22,0xfd,
        0xf2,0x09,0xf7,0x00,0x24,0xa5,0x7b,0x1a,0xa0,0x00,0xc5,0x5b,0x88,0x1f,0x81,0x11,
        0xb2,0xdc,0xde,0x49,0x4a,0x5f,0x48,0x5e,0x5b,0xca,0x4b,0xd8,0x8a,0x27,0x63,0xae,
        0xd1,0xca,0x2b,0x2f,0xa8,0xf0,0x54,0x06,0x78,0xcd,0x1e,0x0f,0x3a,0xd8,0x08,0x92
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t first = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t second = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t imported = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t signing = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t verification = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t first_public[129], second_public[129], imported_public[129];
    uint8_t first_secret[64], second_secret[64], hash[64], signature[128];
    size_t first_public_size = 0, second_public_size = 0, imported_public_size = 0;
    size_t first_secret_size = 0, second_secret_size = 0, signature_size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_BRAINPOOL_P_R1));
    psa_set_key_bits(&attributes, 512);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_generate_key(&attributes, &first);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA brainpoolP512r1 ECDH setup", "第一把密钥生成失败");
    status = psa_generate_key(&attributes, &second);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA brainpoolP512r1 ECDH setup", "第二把密钥生成失败");
    psa_reset_key_attributes(&attributes);
    status = psa_export_public_key(first, first_public, sizeof(first_public), &first_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_public_size == sizeof(first_public),
                      "PSA brainpoolP512r1 public key", "第一把公钥导出失败");
    status = psa_export_public_key(second, second_public, sizeof(second_public), &second_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_public_size == sizeof(second_public),
                      "PSA brainpoolP512r1 public key", "第二把公钥导出失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, first, second_public, second_public_size,
                                   first_secret, sizeof(first_secret), &first_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_secret_size == sizeof(first_secret),
                      "PSA brainpoolP512r1 ECDH uses XCryptographic", "第一方向共享密钥失败");
    status = psa_raw_key_agreement(PSA_ALG_ECDH, second, first_public, first_public_size,
                                   second_secret, sizeof(second_secret), &second_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_secret_size == sizeof(second_secret) &&
                      memcmp(first_secret, second_secret, sizeof(first_secret)) == 0,
                      "PSA brainpoolP512r1 ECDH uses XCryptographic", "双方共享密钥不一致");
    XSSL_TEST_REQUIRE(psa_destroy_key(first) == PSA_SUCCESS && psa_destroy_key(second) == PSA_SUCCESS,
                      "PSA brainpoolP512r1 ECDH setup", "ECDH 密钥销毁失败");

    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_BRAINPOOL_P_R1));
    psa_set_key_bits(&attributes, 512);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
    status = psa_import_key(&attributes, private_one, sizeof(private_one), &imported);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA brainpoolP512r1 ECDH import", "私钥导入失败");
    status = psa_export_public_key(imported, imported_public, sizeof(imported_public),
                                   &imported_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && imported_public_size == sizeof(imported_public) &&
                      memcmp(imported_public, base_public, sizeof(base_public)) == 0,
                      "PSA brainpoolP512r1 ECDH import", "导入私钥的公钥不匹配 RFC 5639 基点");
    XSSL_TEST_REQUIRE(psa_destroy_key(imported) == PSA_SUCCESS,
                      "PSA brainpoolP512r1 ECDH import", "导入密钥销毁失败");

    memset(hash, 0x5a, sizeof(hash));
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_BRAINPOOL_P_R1));
    psa_set_key_bits(&attributes, 512);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_512));
    status = psa_import_key(&attributes, private_one, sizeof(private_one), &signing);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA brainpoolP512r1 ECDSA setup", "签名私钥导入失败");
    status = psa_sign_hash(signing, PSA_ALG_ECDSA(PSA_ALG_SHA_512), hash, sizeof(hash),
                           signature, sizeof(signature), &signature_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && signature_size == sizeof(signature),
                      "PSA brainpoolP512r1 ECDSA uses XCryptographic", "摘要签名失败");
    XSSL_TEST_REQUIRE(psa_destroy_key(signing) == PSA_SUCCESS,
                      "PSA brainpoolP512r1 ECDSA setup", "签名密钥销毁失败");
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_BRAINPOOL_P_R1));
    psa_set_key_bits(&attributes, 512);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_512));
    status = psa_import_key(&attributes, base_public, sizeof(base_public), &verification);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA brainpoolP512r1 ECDSA setup", "验签公钥导入失败");
    status = psa_verify_hash(verification, PSA_ALG_ECDSA(PSA_ALG_SHA_512), hash, sizeof(hash),
                             signature, signature_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS,
                      "PSA brainpoolP512r1 ECDSA uses XCryptographic", "签名验签失败");
    signature[0] ^= 1u;
    status = psa_verify_hash(verification, PSA_ALG_ECDSA(PSA_ALG_SHA_512), hash, sizeof(hash),
                             signature, signature_size);
    XSSL_TEST_REQUIRE(status == PSA_ERROR_INVALID_SIGNATURE,
                      "PSA brainpoolP512r1 ECDSA rejection", "篡改签名未被拒绝");
    XSSL_TEST_REQUIRE(psa_destroy_key(verification) == PSA_SUCCESS,
                      "PSA brainpoolP512r1 ECDSA setup", "验签密钥销毁失败");
    XSSL_TEST_PASS("PSA brainpoolP512r1 ECDH/ECDSA uses XCryptographic");
    return true;
}

static bool xssl_test_psa_ffdh_backend(void)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t first = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t second = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t first_public[256];
    uint8_t second_public[256];
    uint8_t first_secret[256];
    uint8_t second_secret[256];
    size_t first_public_size = 0;
    size_t second_public_size = 0;
    size_t first_secret_size = 0;
    size_t second_secret_size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_DH_KEY_PAIR(PSA_DH_FAMILY_RFC7919));
    psa_set_key_bits(&attributes, 2048);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&attributes, PSA_ALG_FFDH);
    status = psa_generate_key(&attributes, &first);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA FFDH setup", "第一把 FFDH 密钥生成失败");
    status = psa_generate_key(&attributes, &second);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA FFDH setup", "第二把 FFDH 密钥生成失败");
    psa_reset_key_attributes(&attributes);
    status = psa_export_public_key(first, first_public, sizeof(first_public), &first_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_public_size == sizeof(first_public),
                      "PSA FFDH public key", "第一把公钥导出失败");
    status = psa_export_public_key(second, second_public, sizeof(second_public), &second_public_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_public_size == sizeof(second_public),
                      "PSA FFDH public key", "第二把公钥导出失败");
    status = psa_raw_key_agreement(PSA_ALG_FFDH, first, second_public, second_public_size,
                                   first_secret, sizeof(first_secret), &first_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && first_secret_size == sizeof(first_secret),
                      "PSA FFDH uses XCryptographic", "A 方向共享密钥失败");
    status = psa_raw_key_agreement(PSA_ALG_FFDH, second, first_public, first_public_size,
                                   second_secret, sizeof(second_secret), &second_secret_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && second_secret_size == sizeof(second_secret) &&
                      memcmp(first_secret, second_secret, sizeof(first_secret)) == 0,
                      "PSA FFDH uses XCryptographic", "双方共享密钥不一致");
    XSSL_TEST_REQUIRE(psa_destroy_key(first) == PSA_SUCCESS && psa_destroy_key(second) == PSA_SUCCESS,
                      "PSA FFDH setup", "密钥销毁失败");
    XSSL_TEST_PASS("PSA FFDH uses XCryptographic");
    return true;
}

static bool xssl_test_psa_ecjpake_backend(void)
{
    static const uint8_t password[] = { 'p', 'a', 's', 's', 'w', 'o', 'r', 'd' };
    static const psa_pake_step_t round_one_steps[] = {
        PSA_PAKE_STEP_KEY_SHARE, PSA_PAKE_STEP_ZK_PUBLIC, PSA_PAKE_STEP_ZK_PROOF,
        PSA_PAKE_STEP_KEY_SHARE, PSA_PAKE_STEP_ZK_PUBLIC, PSA_PAKE_STEP_ZK_PROOF
    };
    static const psa_pake_step_t round_two_steps[] = {
        PSA_PAKE_STEP_KEY_SHARE, PSA_PAKE_STEP_ZK_PUBLIC, PSA_PAKE_STEP_ZK_PROOF
    };
    psa_key_attributes_t password_attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_attributes_t shared_attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_pake_cipher_suite_t cipher_suite = psa_pake_cipher_suite_init();
    psa_pake_operation_t client = PSA_PAKE_OPERATION_INIT;
    psa_pake_operation_t server = PSA_PAKE_OPERATION_INIT;
    mbedtls_svc_key_id_t password_key = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t client_shared_key = MBEDTLS_SVC_KEY_ID_INIT;
    mbedtls_svc_key_id_t server_shared_key = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t client_output[6][65];
    uint8_t server_output[6][65];
    uint8_t client_second_output[3][65];
    uint8_t server_second_output[3][65];
    uint8_t client_shared[65];
    uint8_t server_shared[65];
    size_t client_output_lengths[6];
    size_t server_output_lengths[6];
    size_t client_second_output_lengths[3];
    size_t server_second_output_lengths[3];
    size_t client_shared_length = 0;
    size_t server_shared_length = 0;
    psa_status_t status;
    size_t index;

    psa_set_key_type(&password_attributes, PSA_KEY_TYPE_PASSWORD);
    psa_set_key_usage_flags(&password_attributes, PSA_KEY_USAGE_DERIVE);
    psa_set_key_algorithm(&password_attributes, PSA_ALG_JPAKE(PSA_ALG_SHA_256));
    status = psa_import_key(&password_attributes, password, sizeof(password), &password_key);
    psa_reset_key_attributes(&password_attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE password", "口令密钥导入失败");

    psa_pake_cs_set_algorithm(&cipher_suite, PSA_ALG_JPAKE(PSA_ALG_SHA_256));
    psa_pake_cs_set_primitive(&cipher_suite,
                              PSA_PAKE_PRIMITIVE(PSA_PAKE_PRIMITIVE_TYPE_ECC,
                                                 PSA_ECC_FAMILY_SECP_R1, 256));
    status = psa_pake_setup(&client, password_key, &cipher_suite);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE client setup", "客户端 PAKE 初始化失败");
    status = psa_pake_setup(&server, password_key, &cipher_suite);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE server setup", "服务端 PAKE 初始化失败");
    status = psa_pake_set_user(&client, (const uint8_t *)"client", 6);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE client user", "客户端 user 设置失败");
    status = psa_pake_set_peer(&client, (const uint8_t *)"server", 6);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE client peer", "客户端 peer 设置失败");
    status = psa_pake_set_user(&server, (const uint8_t *)"server", 6);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE server user", "服务端 user 设置失败");
    status = psa_pake_set_peer(&server, (const uint8_t *)"client", 6);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE server peer", "服务端 peer 设置失败");

    for (index = 0; index < 6; ++index) {
        status = psa_pake_output(&client, round_one_steps[index], client_output[index],
                                 sizeof(client_output[index]), &client_output_lengths[index]);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE client round one output", "客户端第一轮输出失败");
        status = psa_pake_output(&server, round_one_steps[index], server_output[index],
                                 sizeof(server_output[index]), &server_output_lengths[index]);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE server round one output", "服务端第一轮输出失败");
    }
    for (index = 0; index < 6; ++index) {
        status = psa_pake_input(&client, round_one_steps[index], server_output[index],
                                server_output_lengths[index]);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE client round one input", "客户端第一轮输入失败");
        status = psa_pake_input(&server, round_one_steps[index], client_output[index],
                                client_output_lengths[index]);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE server round one input", "服务端第一轮输入失败");
    }
    for (index = 0; index < 3; ++index) {
        status = psa_pake_output(&client, round_two_steps[index], client_second_output[index],
                                 sizeof(client_second_output[index]), &client_second_output_lengths[index]);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE client round two output", "客户端第二轮输出失败");
        status = psa_pake_output(&server, round_two_steps[index], server_second_output[index],
                                 sizeof(server_second_output[index]), &server_second_output_lengths[index]);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE server round two output", "服务端第二轮输出失败");
    }
    for (index = 0; index < 3; ++index) {
        status = psa_pake_input(&client, round_two_steps[index], server_second_output[index],
                                server_second_output_lengths[index]);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE client round two input", "客户端第二轮输入失败");
        status = psa_pake_input(&server, round_two_steps[index], client_second_output[index],
                                client_second_output_lengths[index]);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE server round two input", "服务端第二轮输入失败");
    }

    psa_set_key_type(&shared_attributes, PSA_KEY_TYPE_DERIVE);
    psa_set_key_usage_flags(&shared_attributes, PSA_KEY_USAGE_EXPORT);
    status = psa_pake_get_shared_key(&client, &shared_attributes, &client_shared_key);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE client shared key", "客户端共享密钥生成失败");
    status = psa_pake_get_shared_key(&server, &shared_attributes, &server_shared_key);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE server shared key", "服务端共享密钥生成失败");
    status = psa_export_key(client_shared_key, client_shared, sizeof(client_shared),
                            &client_shared_length);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE client export", "客户端共享密钥导出失败");
    status = psa_export_key(server_shared_key, server_shared, sizeof(server_shared),
                            &server_shared_length);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && client_shared_length == server_shared_length &&
                          client_shared_length == 65 && memcmp(client_shared, server_shared, 65) == 0,
                      "PSA ECJPAKE server export", "双方共享密钥不一致");

    (void) psa_destroy_key(client_shared_key);
    (void) psa_destroy_key(server_shared_key);
    (void) psa_destroy_key(password_key);
    psa_reset_key_attributes(&shared_attributes);
    XSSL_TEST_PASS("PSA ECJPAKE uses XCryptographic");
    return true;
}

static bool xssl_test_psa_tls12_ecjpake_pms_backend(void)
{
    static const uint8_t shared_point[65] = {
        0x04,
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f
    };
    static const uint8_t expected_pms[32] = {
        0x63, 0x0d, 0xcd, 0x29, 0x66, 0xc4, 0x33, 0x66,
        0x91, 0x12, 0x54, 0x48, 0xbb, 0xb2, 0x5b, 0x4f,
        0xf4, 0x12, 0xa4, 0x9c, 0x73, 0x2d, 0xb2, 0xc8,
        0xab, 0xc1, 0xb8, 0x58, 0x1b, 0xd7, 0x10, 0xdd
    };
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    uint8_t pms[sizeof(expected_pms)];
    psa_status_t status;

    status = psa_key_derivation_setup(&operation, PSA_ALG_TLS12_ECJPAKE_TO_PMS);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE-to-PMS setup", "派生初始化失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET,
                                            shared_point, sizeof(shared_point));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA ECJPAKE-to-PMS input", "共享点输入失败");
    status = psa_key_derivation_output_bytes(&operation, pms, sizeof(pms));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(pms, expected_pms, sizeof(pms)) == 0,
                      "PSA ECJPAKE-to-PMS uses XCryptographic", "SHA-256 派生向量不匹配");
    (void) psa_key_derivation_abort(&operation);
    XSSL_TEST_PASS("PSA ECJPAKE-to-PMS uses XCryptographic");
    return true;
}

static bool xssl_test_psa_kdf_backend(void)
{
    static const uint8_t input_key_material[22] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
    };
    static const uint8_t salt[13] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c
    };
    static const uint8_t info[10] = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9
    };
    static const uint8_t hkdf_expected[42] = {
        0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a,
        0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
        0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c,
        0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf,
        0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18,
        0x58, 0x65
    };
    static const uint8_t pbkdf2_expected[32] = {
        0x12, 0x0f, 0xb6, 0xcf, 0xfc, 0xf8, 0xb3, 0x2c,
        0x43, 0xe7, 0x22, 0x52, 0x56, 0xc4, 0xf8, 0x37,
        0xa8, 0x65, 0x48, 0xc9, 0x2c, 0xcc, 0x35, 0x48,
        0x08, 0x05, 0x98, 0x7c, 0xb7, 0x0b, 0xe1, 0x7b
    };
    static const uint8_t pbkdf2_sha1_expected[20] = {
        0xea, 0x6c, 0x01, 0x4d, 0xc7, 0x2d, 0x6f, 0x8c,
        0xcd, 0x1e, 0xd9, 0x2a, 0xce, 0x1d, 0x41, 0xf0,
        0xd8, 0xde, 0x89, 0x57
    };
    static const uint8_t pbkdf2_sha512_expected[64] = {
        0xe1, 0xd9, 0xc1, 0x6a, 0xa6, 0x81, 0x70, 0x8a,
        0x45, 0xf5, 0xc7, 0xc4, 0xe2, 0x15, 0xce, 0xb6,
        0x6e, 0x01, 0x1a, 0x2e, 0x9f, 0x00, 0x40, 0x71,
        0x3f, 0x18, 0xae, 0xfd, 0xb8, 0x66, 0xd5, 0x3c,
        0xf7, 0x6c, 0xab, 0x28, 0x68, 0xa3, 0x9b, 0x9f,
        0x78, 0x40, 0xed, 0xce, 0x4f, 0xef, 0x5a, 0x82,
        0xbe, 0x67, 0x33, 0x5c, 0x77, 0xa6, 0x06, 0x8e,
        0x04, 0x11, 0x27, 0x54, 0xf2, 0x7c, 0xcf, 0x4e
    };
    static const uint8_t pbkdf2_sha3_256_expected[32] = {
        0x4c, 0x91, 0x5b, 0xae, 0xdd, 0x17, 0x73, 0x38,
        0x3e, 0x77, 0xfc, 0xfe, 0x38, 0x11, 0x4c, 0xa7,
        0x51, 0x40, 0x10, 0xad, 0xec, 0x24, 0xb4, 0x72,
        0x90, 0xec, 0x17, 0x02, 0x08, 0x42, 0x3f, 0x76
    };
    static const uint8_t pbkdf2_long_password[80] = {
        'p','p','p','p','p','p','p','p','p','p','p','p','p','p','p','p',
        'p','p','p','p','p','p','p','p','p','p','p','p','p','p','p','p',
        'p','p','p','p','p','p','p','p','p','p','p','p','p','p','p','p',
        'p','p','p','p','p','p','p','p','p','p','p','p','p','p','p','p',
        'p','p','p','p','p','p','p','p','p','p','p','p','p','p','p','p',
    };
    static const uint8_t pbkdf2_long_password_expected[32] = {
        0x23, 0x55, 0x2d, 0xe5, 0x73, 0xd1, 0x2e, 0x83,
        0x92, 0x85, 0xf9, 0x9d, 0xe7, 0x5d, 0x70, 0x96,
        0xfd, 0x46, 0xc2, 0x42, 0x4d, 0xbb, 0xa9, 0x0e,
        0xca, 0xda, 0x9d, 0x93, 0xfe, 0x8f, 0x6b, 0x16
    };
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    uint8_t output[64];
    psa_status_t status;

    status = psa_key_derivation_setup(&operation, PSA_ALG_HKDF(PSA_ALG_SHA_256));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF setup", "HKDF 初始化失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                            salt, sizeof(salt));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF salt", "HKDF salt 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET,
                                            input_key_material, sizeof(input_key_material));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF secret", "HKDF 密钥材料输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_INFO,
                                            info, sizeof(info));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF info", "HKDF info 输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, sizeof(hkdf_expected));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && memcmp(output, hkdf_expected, sizeof(hkdf_expected)) == 0,
                      "PSA HKDF-SHA-256 uses XCryptographic", "RFC 5869 向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    operation = (psa_key_derivation_operation_t)PSA_KEY_DERIVATION_OPERATION_INIT;
    status = psa_key_derivation_setup(&operation, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2 long-password setup", "长密码 PBKDF2 初始化失败");
    status = psa_key_derivation_input_integer(&operation, PSA_KEY_DERIVATION_INPUT_COST, 2);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2 long-password cost", "长密码 PBKDF2 迭代次数输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                            (const uint8_t*)"salt", 4);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2 long-password salt", "长密码 PBKDF2 salt 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                            pbkdf2_long_password, sizeof(pbkdf2_long_password));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2 long-password password", "长密码 PBKDF2 密码输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, sizeof(pbkdf2_long_password_expected));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, pbkdf2_long_password_expected,
                             sizeof(pbkdf2_long_password_expected)) == 0,
                      "PSA PBKDF2 long-password uses XCryptographic", "长密码预处理向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    operation = (psa_key_derivation_operation_t)PSA_KEY_DERIVATION_OPERATION_INIT;
    status = psa_key_derivation_setup(&operation, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2 setup", "PBKDF2 初始化失败");
    status = psa_key_derivation_input_integer(&operation, PSA_KEY_DERIVATION_INPUT_COST, 1);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2 cost", "PBKDF2 迭代次数输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                            (const uint8_t*)"salt", 4);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2 salt", "PBKDF2 salt 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                            (const uint8_t*)"password", 8);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2 password", "PBKDF2 密码输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, sizeof(pbkdf2_expected));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, pbkdf2_expected, sizeof(pbkdf2_expected)) == 0,
                      "PSA PBKDF2-HMAC-SHA-256 uses XCryptographic", "RFC 8018 向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    operation = (psa_key_derivation_operation_t)PSA_KEY_DERIVATION_OPERATION_INIT;
    status = psa_key_derivation_setup(&operation, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_1));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-HMAC-SHA-1 setup", "PBKDF2-SHA-1 初始化失败");
    status = psa_key_derivation_input_integer(&operation, PSA_KEY_DERIVATION_INPUT_COST, 2);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-HMAC-SHA-1 cost", "PBKDF2-SHA-1 迭代次数输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                            (const uint8_t*)"salt", 4);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-HMAC-SHA-1 salt", "PBKDF2-SHA-1 salt 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                            (const uint8_t*)"password", 8);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-HMAC-SHA-1 password", "PBKDF2-SHA-1 密码输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, sizeof(pbkdf2_sha1_expected));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, pbkdf2_sha1_expected, sizeof(pbkdf2_sha1_expected)) == 0,
                      "PSA PBKDF2-HMAC-SHA-1 uses XCryptographic", "PBKDF2-SHA-1 向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    operation = (psa_key_derivation_operation_t)PSA_KEY_DERIVATION_OPERATION_INIT;
    status = psa_key_derivation_setup(&operation, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_512));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-HMAC-SHA-512 setup", "PBKDF2-SHA-512 初始化失败");
    status = psa_key_derivation_input_integer(&operation, PSA_KEY_DERIVATION_INPUT_COST, 2);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-HMAC-SHA-512 cost", "PBKDF2-SHA-512 迭代次数输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                            (const uint8_t*)"salt", 4);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-HMAC-SHA-512 salt", "PBKDF2-SHA-512 salt 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                            (const uint8_t*)"password", 8);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-HMAC-SHA-512 password", "PBKDF2-SHA-512 密码输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, 17);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-HMAC-SHA-512 first", "PBKDF2-SHA-512 第一段输出失败");
    status = psa_key_derivation_output_bytes(&operation, output + 17,
                                             sizeof(pbkdf2_sha512_expected) - 17);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, pbkdf2_sha512_expected, sizeof(pbkdf2_sha512_expected)) == 0,
                      "PSA PBKDF2-HMAC-SHA-512 uses XCryptographic", "PBKDF2-SHA-512 分段向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    operation = (psa_key_derivation_operation_t)PSA_KEY_DERIVATION_OPERATION_INIT;
    status = psa_key_derivation_setup(&operation, PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA3_256));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-HMAC-SHA3-256 setup", "PBKDF2-SHA3-256 初始化失败");
    status = psa_key_derivation_input_integer(&operation, PSA_KEY_DERIVATION_INPUT_COST, 2);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-HMAC-SHA3-256 cost", "PBKDF2-SHA3-256 迭代次数输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                            (const uint8_t*)"salt", 4);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-HMAC-SHA3-256 salt", "PBKDF2-SHA3-256 salt 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                            (const uint8_t*)"password", 8);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-HMAC-SHA3-256 password", "PBKDF2-SHA3-256 密码输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, sizeof(pbkdf2_sha3_256_expected));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, pbkdf2_sha3_256_expected, sizeof(pbkdf2_sha3_256_expected)) == 0,
                      "PSA PBKDF2-HMAC-SHA3-256 uses XCryptographic", "PBKDF2-SHA3-256 向量不匹配");
    (void) psa_key_derivation_abort(&operation);
    XSSL_TEST_PASS("PSA HKDF/PBKDF2 HMAC-SHA-256 uses XCryptographic");
    return true;
}

static bool xssl_test_psa_kdf_new_backend(void)
{
    static const uint8_t input_key_material[22] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
    };
    static const uint8_t salt[13] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
        0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c
    };
    static const uint8_t info[10] = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9
    };
    static const uint8_t hkdf_expected[42] = {
        0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a,
        0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
        0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c,
        0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf,
        0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18,
        0x58, 0x65
    };
    static const uint8_t hkdf_sha384_expected[42] = {
        0x9b, 0x50, 0x97, 0xa8, 0x60, 0x38, 0xb8, 0x05,
        0x30, 0x90, 0x76, 0xa4, 0x4b, 0x3a, 0x9f, 0x38,
        0x06, 0x3e, 0x25, 0xb5, 0x16, 0xdc, 0xbf, 0x36,
        0x9f, 0x39, 0x4c, 0xfa, 0xb4, 0x36, 0x85, 0xf7,
        0x48, 0xb6, 0x45, 0x77, 0x63, 0xe4, 0xf0, 0x20,
        0x4f, 0xc5
    };
    static const uint8_t hkdf_extract_expected[32] = {
        0x07, 0x77, 0x09, 0x36, 0x2c, 0x2e, 0x32, 0xdf,
        0x0d, 0xdc, 0x3f, 0x0d, 0xc4, 0x7b, 0xba, 0x63,
        0x90, 0xb6, 0xc7, 0x3b, 0xb5, 0x0f, 0x9c, 0x31,
        0x22, 0xec, 0x84, 0x4a, 0xd7, 0xc2, 0xb3, 0xe5
    };
    static const uint8_t hkdf_extract_sha384_expected[48] = {
        0x70, 0x4b, 0x39, 0x99, 0x07, 0x79, 0xce, 0x1d,
        0xc5, 0x48, 0x05, 0x2c, 0x7d, 0xc3, 0x9f, 0x30,
        0x35, 0x70, 0xdd, 0x13, 0xfb, 0x39, 0xf7, 0xac,
        0xc5, 0x64, 0x68, 0x0b, 0xef, 0x80, 0xe8, 0xde,
        0xc7, 0x0e, 0xe9, 0xa7, 0xe1, 0xf3, 0xe2, 0x93,
        0xef, 0x68, 0xec, 0xeb, 0x07, 0x2a, 0x5a, 0xde
    };
    static const uint8_t tls12_expected[48] = {
        0x7e, 0xd4, 0x2a, 0x23, 0xa1, 0x33, 0xad, 0x37,
        0x9b, 0x99, 0x19, 0x6a, 0x86, 0xdb, 0x88, 0x7c,
        0xf5, 0x95, 0xd9, 0xad, 0xa5, 0x66, 0x1e, 0xc1,
        0x18, 0x66, 0x91, 0x65, 0x9b, 0xf8, 0x7a, 0x7a,
        0x8d, 0x16, 0xf4, 0x32, 0x0d, 0x6b, 0x5e, 0x9c,
        0xc3, 0xab, 0x31, 0x8d, 0xbe, 0x40, 0x0a, 0x45
    };
    static const uint8_t tls12_sha384_expected[48] = {
        0xe6, 0xa6, 0xf1, 0x99, 0xa9, 0xf1, 0x6f, 0x9a,
        0x56, 0x65, 0x70, 0xcd, 0x0d, 0x8d, 0xa7, 0x1e,
        0x04, 0xb5, 0x51, 0xe2, 0x4d, 0xcb, 0x65, 0x20,
        0x70, 0x61, 0xad, 0xcd, 0xa3, 0xae, 0x99, 0xc3,
        0xc6, 0x8f, 0xb6, 0xcf, 0x59, 0x90, 0xae, 0x76,
        0x58, 0xdb, 0xd8, 0x3b, 0xe3, 0xe7, 0x2c, 0x96
    };
    static const uint8_t psk_pure_expected[48] = {
        0xe1, 0x4f, 0x0d, 0xb7, 0x4f, 0xaf, 0xda, 0xbc,
        0xb8, 0xae, 0x2f, 0xed, 0x0b, 0x6d, 0x46, 0x03,
        0x72, 0x7b, 0x1a, 0x59, 0x60, 0x4e, 0x36, 0x61,
        0xc2, 0x54, 0xd2, 0xd6, 0xe7, 0x76, 0xb0, 0x8c,
        0xac, 0xf4, 0xa7, 0x22, 0xe3, 0xe7, 0x41, 0x3a,
        0xd0, 0x8b, 0xcb, 0xc4, 0x8a, 0xc1, 0x97, 0x8d
    };
    static const uint8_t psk_mixed_expected[48] = {
        0xc4, 0x43, 0x70, 0x00, 0x72, 0xa9, 0x56, 0x64,
        0x41, 0x43, 0x70, 0x80, 0xee, 0x8d, 0x18, 0x2e,
        0xb1, 0x69, 0x38, 0xae, 0x93, 0xf6, 0xa1, 0xcc,
        0x1f, 0x53, 0x5e, 0xff, 0x37, 0x5c, 0x14, 0x1a,
        0xff, 0x5b, 0xc0, 0x44, 0x66, 0x9e, 0x8c, 0x88,
        0x20, 0x7b, 0x72, 0x4d, 0x75, 0x71, 0x87, 0x3f
    };
    static const uint8_t pbkdf2_password[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t pbkdf2_expected[32] = {
        0x4e, 0x91, 0xe3, 0xc9, 0x6d, 0x4d, 0x0c, 0xac,
        0x33, 0x29, 0x41, 0xec, 0x57, 0x8b, 0xd4, 0x8f,
        0xf5, 0x69, 0x17, 0x49, 0x35, 0xfc, 0x3f, 0x8f,
        0x7f, 0xe4, 0x3e, 0x6c, 0xcc, 0x72, 0xfe, 0x3f
    };
    psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;
    uint8_t output[64];
    psa_status_t status;

    status = psa_key_derivation_setup(&operation, PSA_ALG_HKDF_EXTRACT(PSA_ALG_SHA_256));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF-extract setup", "HKDF-extract 初始化失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                            salt, sizeof(salt));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF-extract salt", "HKDF-extract salt 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET,
                                            input_key_material, sizeof(input_key_material));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF-extract secret", "HKDF-extract 密钥材料输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, sizeof(hkdf_extract_expected));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, hkdf_extract_expected, sizeof(hkdf_extract_expected)) == 0,
                      "PSA HKDF-extract uses XCryptographic", "HKDF-extract SHA-256 向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    operation = (psa_key_derivation_operation_t)PSA_KEY_DERIVATION_OPERATION_INIT;
    status = psa_key_derivation_setup(&operation, PSA_ALG_HKDF_EXTRACT(PSA_ALG_SHA_384));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF-extract-SHA-384 setup", "HKDF-extract-SHA-384 初始化失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                            salt, sizeof(salt));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF-extract-SHA-384 salt", "HKDF-extract-SHA-384 salt 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET,
                                            input_key_material, sizeof(input_key_material));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF-extract-SHA-384 secret", "HKDF-extract-SHA-384 密钥材料输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, sizeof(hkdf_extract_sha384_expected));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, hkdf_extract_sha384_expected,
                             sizeof(hkdf_extract_sha384_expected)) == 0,
                      "PSA HKDF-extract-SHA-384 uses XCryptographic", "HKDF-extract SHA-384 向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    operation = (psa_key_derivation_operation_t)PSA_KEY_DERIVATION_OPERATION_INIT;

    /* HKDF-SHA-256 分段读取扩展，覆盖 XCryptographic 流式处理路径。 */
    status = psa_key_derivation_setup(&operation, PSA_ALG_HKDF(PSA_ALG_SHA_256));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF split setup", "HKDF 初始化失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                            salt, sizeof(salt));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF split salt", "HKDF salt 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET,
                                            input_key_material, sizeof(input_key_material));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF split secret", "HKDF 密钥材料输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_INFO,
                                            info, sizeof(info));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF split info", "HKDF info 输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, 16);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF split first", "HKDF 分段输出第一段失败");
    status = psa_key_derivation_output_bytes(&operation, output + 16, sizeof(hkdf_expected) - 16);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, hkdf_expected, sizeof(hkdf_expected)) == 0,
                      "PSA HKDF-SHA-256 split uses XCryptographic", "RFC 5869 分段向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    /* HKDF-SHA-384 分段读取，覆盖通用 HMAC 后端路径。 */
    operation = (psa_key_derivation_operation_t)PSA_KEY_DERIVATION_OPERATION_INIT;
    status = psa_key_derivation_setup(&operation, PSA_ALG_HKDF(PSA_ALG_SHA_384));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF-SHA-384 setup", "HKDF-SHA-384 初始化失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                            salt, sizeof(salt));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF-SHA-384 salt", "HKDF-SHA-384 salt 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET,
                                            input_key_material, sizeof(input_key_material));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF-SHA-384 secret", "HKDF-SHA-384 密钥材料输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_INFO,
                                            info, sizeof(info));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF-SHA-384 info", "HKDF-SHA-384 info 输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, 17);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HKDF-SHA-384 first", "HKDF-SHA-384 第一段输出失败");
    status = psa_key_derivation_output_bytes(&operation, output + 17,
                                             sizeof(hkdf_sha384_expected) - 17);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, hkdf_sha384_expected, sizeof(hkdf_sha384_expected)) == 0,
                      "PSA HKDF-SHA-384 uses XCryptographic", "HKDF-SHA-384 向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    /* TLS 1.2 PRF-SHA-256 分段读取。 */
    operation = (psa_key_derivation_operation_t)PSA_KEY_DERIVATION_OPERATION_INIT;
    status = psa_key_derivation_setup(&operation, PSA_ALG_TLS12_PRF(PSA_ALG_SHA_256));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PRF setup", "TLS1.2 PRF 初始化失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SEED,
                                            (const uint8_t*)"seed", 4);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PRF seed", "TLS1.2 PRF seed 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET,
                                            (const uint8_t*)"secret", 6);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PRF secret", "TLS1.2 PRF secret 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_LABEL,
                                            (const uint8_t*)"label", 5);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PRF label", "TLS1.2 PRF label 输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, 20);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PRF first", "TLS1.2 PRF 第一段输出失败");
    status = psa_key_derivation_output_bytes(&operation, output + 20, sizeof(tls12_expected) - 20);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, tls12_expected, sizeof(tls12_expected)) == 0,
                      "PSA TLS1.2 PRF-SHA-256 uses XCryptographic", "TLS1.2 PRF 向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    /* TLS 1.2 PRF-SHA-384 分段读取，覆盖适配层中的散列选择。 */
    operation = (psa_key_derivation_operation_t)PSA_KEY_DERIVATION_OPERATION_INIT;
    status = psa_key_derivation_setup(&operation, PSA_ALG_TLS12_PRF(PSA_ALG_SHA_384));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PRF-SHA-384 setup", "TLS1.2 PRF-SHA-384 初始化失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SEED,
                                            (const uint8_t*)"seed", 4);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PRF-SHA-384 seed", "TLS1.2 PRF-SHA-384 seed 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET,
                                            (const uint8_t*)"secret", 6);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PRF-SHA-384 secret", "TLS1.2 PRF-SHA-384 secret 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_LABEL,
                                            (const uint8_t*)"label", 5);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PRF-SHA-384 label", "TLS1.2 PRF-SHA-384 label 输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, 19);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PRF-SHA-384 first", "TLS1.2 PRF-SHA-384 第一段输出失败");
    status = psa_key_derivation_output_bytes(&operation, output + 19,
                                             sizeof(tls12_sha384_expected) - 19);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, tls12_sha384_expected, sizeof(tls12_sha384_expected)) == 0,
                      "PSA TLS1.2 PRF-SHA-384 uses XCryptographic", "TLS1.2 PRF-SHA-384 向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    /* TLS 1.2 PSK 转主密钥：纯 PSK。 */
    operation = (psa_key_derivation_operation_t)PSA_KEY_DERIVATION_OPERATION_INIT;
    status = psa_key_derivation_setup(&operation, PSA_ALG_TLS12_PSK_TO_MS(PSA_ALG_SHA_256));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PSK_TO_MS pure setup", "PSK-to-MS 初始化失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SEED,
                                            (const uint8_t*)"seed", 4);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PSK_TO_MS pure seed", "PSK-to-MS seed 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET,
                                            (const uint8_t*)"abc", 3);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PSK_TO_MS pure psk", "PSK-to-MS PSK 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_LABEL,
                                            (const uint8_t*)"master secret", 13);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PSK_TO_MS pure label", "PSK-to-MS label 输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, sizeof(psk_pure_expected));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, psk_pure_expected, sizeof(psk_pure_expected)) == 0,
                      "PSA TLS1.2 PSK-to-MS pure uses XCryptographic", "纯 PSK 主密钥向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    /* TLS 1.2 PSK 转主密钥：混合 PSK（含 ECDHE 其它秘密）。 */
    operation = (psa_key_derivation_operation_t)PSA_KEY_DERIVATION_OPERATION_INIT;
    status = psa_key_derivation_setup(&operation, PSA_ALG_TLS12_PSK_TO_MS(PSA_ALG_SHA_256));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PSK_TO_MS mixed setup", "PSK-to-MS 初始化失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SEED,
                                            (const uint8_t*)"seed", 4);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PSK_TO_MS mixed seed", "PSK-to-MS seed 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_OTHER_SECRET,
                                            (const uint8_t*)"\x00\x11\x22\x33", 4);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PSK_TO_MS mixed other", "PSK-to-MS 对端秘密输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SECRET,
                                            (const uint8_t*)"abc", 3);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PSK_TO_MS mixed psk", "PSK-to-MS PSK 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_LABEL,
                                            (const uint8_t*)"master secret", 13);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA TLS12_PSK_TO_MS mixed label", "PSK-to-MS label 输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, sizeof(psk_mixed_expected));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, psk_mixed_expected, sizeof(psk_mixed_expected)) == 0,
                      "PSA TLS1.2 PSK-to-MS mixed uses XCryptographic", "混合 PSK 主密钥向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    /* PBKDF2-AES-CMAC-PRF-128。 */
    operation = (psa_key_derivation_operation_t)PSA_KEY_DERIVATION_OPERATION_INIT;
    status = psa_key_derivation_setup(&operation, PSA_ALG_PBKDF2_AES_CMAC_PRF_128);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-AES-CMAC setup", "PBKDF2-AES-CMAC 初始化失败");
    status = psa_key_derivation_input_integer(&operation, PSA_KEY_DERIVATION_INPUT_COST, 5);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-AES-CMAC cost", "PBKDF2-AES-CMAC 迭代次数输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_SALT,
                                            (const uint8_t*)"salt", 4);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-AES-CMAC salt", "PBKDF2-AES-CMAC salt 输入失败");
    status = psa_key_derivation_input_bytes(&operation, PSA_KEY_DERIVATION_INPUT_PASSWORD,
                                            pbkdf2_password, sizeof(pbkdf2_password));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA PBKDF2-AES-CMAC password", "PBKDF2-AES-CMAC 密码输入失败");
    status = psa_key_derivation_output_bytes(&operation, output, sizeof(pbkdf2_expected));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, pbkdf2_expected, sizeof(pbkdf2_expected)) == 0,
                      "PSA PBKDF2-AES-CMAC-PRF-128 uses XCryptographic", "PBKDF2-AES-CMAC 向量不匹配");
    (void) psa_key_derivation_abort(&operation);

    XSSL_TEST_PASS("PSA HKDF-split/TLS1.2-PRF/PSK-to-MS/PBKDF2-AES-CMAC uses XCryptographic");
    return true;
}

static bool xssl_test_psa_xof_backend(void)
{
    static const uint8_t abcExpected256[32] = {
        0x48, 0x33, 0x66, 0x60, 0x13, 0x60, 0xa8, 0x77,
        0x1c, 0x68, 0x63, 0x08, 0x0c, 0xc4, 0x11, 0x4d,
        0x8d, 0xb4, 0x45, 0x30, 0xf8, 0xf1, 0xe1, 0xee,
        0x4f, 0x94, 0xea, 0x37, 0xe7, 0x8b, 0x57, 0x39
    };
    static const uint8_t emptyExpected128[32] = {
        0x7f, 0x9c, 0x2b, 0xa4, 0xe8, 0x8f, 0x82, 0x7d,
        0x61, 0x60, 0x45, 0x50, 0x76, 0x05, 0x85, 0x3e,
        0xd7, 0x3b, 0x80, 0x93, 0xf6, 0xef, 0xbc, 0x88,
        0xeb, 0x1a, 0x6e, 0xac, 0xfa, 0x66, 0xef, 0x26
    };
    static const uint8_t helloExpected[64] = {
        0x3a, 0x91, 0x59, 0xf0, 0x71, 0xe4, 0xdd, 0x1c,
        0x8c, 0x4f, 0x96, 0x86, 0x07, 0xc3, 0x09, 0x42,
        0xe1, 0x20, 0xd8, 0x15, 0x6b, 0x8b, 0x1e, 0x72,
        0xe0, 0xd3, 0x76, 0xe8, 0x87, 0x1c, 0xb8, 0xb8,
        0x99, 0x07, 0x26, 0x65, 0x67, 0x4f, 0x26, 0xcc,
        0x49, 0x4a, 0x4b, 0xcf, 0x02, 0x7c, 0x58, 0x26,
        0x7e, 0x8e, 0xe2, 0xda, 0x60, 0xe9, 0x42, 0x75,
        0x9d, 0xe8, 0x6d, 0x26, 0x70, 0xbb, 0xa1, 0xaa
    };
    psa_xof_operation_t operation = PSA_XOF_OPERATION_INIT;
    uint8_t output[64];
    psa_status_t status;

    status = psa_xof_setup(&operation, PSA_ALG_SHAKE128);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHAKE128 setup", "SHAKE-128 初始化失败");
    status = psa_xof_update(&operation, (const uint8_t*)"hello world", 11);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHAKE128 update", "SHAKE-128 输入失败");
    status = psa_xof_output(&operation, output, 40);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHAKE128 output first", "SHAKE-128 第一段输出失败");
    status = psa_xof_output(&operation, output + 40, sizeof(output) - 40);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, helloExpected, sizeof(helloExpected)) == 0,
                      "PSA SHAKE128 split output uses XCryptographic", "SHAKE-128 分段输出向量不匹配");
    status = psa_xof_abort(&operation);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHAKE128 abort", "SHAKE-128 abort 失败");

    operation = (psa_xof_operation_t)PSA_XOF_OPERATION_INIT;
    status = psa_xof_setup(&operation, PSA_ALG_SHAKE128);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHAKE128 empty setup", "SHAKE-128 初始化失败");
    status = psa_xof_update(&operation, NULL, 0);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHAKE128 empty update", "SHAKE-128 空输入失败");
    status = psa_xof_output(&operation, output, sizeof(emptyExpected128));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, emptyExpected128, sizeof(emptyExpected128)) == 0,
                      "PSA SHAKE128 empty vector uses XCryptographic", "SHAKE-128 空输入向量不匹配");
    status = psa_xof_abort(&operation);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHAKE128 empty abort", "SHAKE-128 abort 失败");

    operation = (psa_xof_operation_t)PSA_XOF_OPERATION_INIT;
    status = psa_xof_setup(&operation, PSA_ALG_SHAKE256);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHAKE256 setup", "SHAKE-256 初始化失败");
    status = psa_xof_update(&operation, (const uint8_t*)"abc", 3);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHAKE256 update", "SHAKE-256 输入失败");
    status = psa_xof_output(&operation, output, sizeof(abcExpected256));
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                      memcmp(output, abcExpected256, sizeof(abcExpected256)) == 0,
                      "PSA SHAKE256 abc vector uses XCryptographic", "SHAKE-256 abc 向量不匹配");
    status = psa_xof_abort(&operation);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHAKE256 abort", "SHAKE-256 abort 失败");
    XSSL_TEST_PASS("PSA SHAKE128/256 uses XCryptographic");
    return true;
}

static bool xssl_test_psa_hash_backend(void)
{
    typedef struct XSslHashVector {
        psa_algorithm_t algorithm;
        size_t length;
        const uint8_t *expected;
    } XSslHashVector;
    static const uint8_t md5_expected[16] = {
        0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
        0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72
    };
    static const uint8_t sha1_expected[20] = {
        0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a,
        0xba, 0x3e, 0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c,
        0x9c, 0xd0, 0xd8, 0x9d
    };
    static const uint8_t sha224_expected[28] = {
        0x23, 0x09, 0x7d, 0x22, 0x34, 0x05, 0xd8, 0x22,
        0x86, 0x42, 0xa4, 0x77, 0xbd, 0xa2, 0x55, 0xb3,
        0x2a, 0xad, 0xbc, 0xe4, 0xbd, 0xa0, 0xb3, 0xf7,
        0xe3, 0x6c, 0x9d, 0xa7
    };
    static const uint8_t expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    static const uint8_t sha384_expected[48] = {
        0xcb, 0x00, 0x75, 0x3f, 0x45, 0xa3, 0x5e, 0x8b,
        0xb5, 0xa0, 0x3d, 0x69, 0x9a, 0xc6, 0x50, 0x07,
        0x27, 0x2c, 0x32, 0xab, 0x0e, 0xde, 0xd1, 0x63,
        0x1a, 0x8b, 0x60, 0x5a, 0x43, 0xff, 0x5b, 0xed,
        0x80, 0x86, 0x07, 0x2b, 0xa1, 0xe7, 0xcc, 0x23,
        0x58, 0xba, 0xec, 0xa1, 0x34, 0xc8, 0x25, 0xa7
    };
    static const uint8_t sha512_expected[64] = {
        0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba,
        0xcc, 0x41, 0x73, 0x49, 0xae, 0x20, 0x41, 0x31,
        0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
        0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a,
        0x21, 0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1, 0xa8,
        0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
        0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e,
        0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f
    };
    static const uint8_t sha3_224_expected[28] = {
        0xe6, 0x42, 0x82, 0x4c, 0x3f, 0x8c, 0xf2, 0x4a,
        0xd0, 0x92, 0x34, 0xee, 0x7d, 0x3c, 0x76, 0x6f,
        0xc9, 0xa3, 0xa5, 0x16, 0x8d, 0x0c, 0x94, 0xad,
        0x73, 0xb4, 0x6f, 0xdf
    };
    static const uint8_t sha3_256_expected[32] = {
        0x3a, 0x98, 0x5d, 0xa7, 0x4f, 0xe2, 0x25, 0xb2,
        0x04, 0x5c, 0x17, 0x2d, 0x6b, 0xd3, 0x90, 0xbd,
        0x85, 0x5f, 0x08, 0x6e, 0x3e, 0x9d, 0x52, 0x5b,
        0x46, 0xbf, 0xe2, 0x45, 0x11, 0x43, 0x15, 0x32
    };
    static const uint8_t sha3_384_expected[48] = {
        0xec, 0x01, 0x49, 0x82, 0x88, 0x51, 0x6f, 0xc9,
        0x26, 0x45, 0x9f, 0x58, 0xe2, 0xc6, 0xad, 0x8d,
        0xf9, 0xb4, 0x73, 0xcb, 0x0f, 0xc0, 0x8c, 0x25,
        0x96, 0xda, 0x7c, 0xf0, 0xe4, 0x9b, 0xe4, 0xb2,
        0x98, 0xd8, 0x8c, 0xea, 0x92, 0x7a, 0xc7, 0xf5,
        0x39, 0xf1, 0xed, 0xf2, 0x28, 0x37, 0x6d, 0x25
    };
    static const uint8_t sha3_512_expected[64] = {
        0xb7, 0x51, 0x85, 0x0b, 0x1a, 0x57, 0x16, 0x8a,
        0x56, 0x93, 0xcd, 0x92, 0x4b, 0x6b, 0x09, 0x6e,
        0x08, 0xf6, 0x21, 0x82, 0x74, 0x44, 0xf7, 0x0d,
        0x88, 0x4f, 0x5d, 0x02, 0x40, 0xd2, 0x71, 0x2e,
        0x10, 0xe1, 0x16, 0xe9, 0x19, 0x2a, 0xf3, 0xc9,
        0x1a, 0x7e, 0xc5, 0x76, 0x47, 0xe3, 0x93, 0x40,
        0x57, 0x34, 0x0b, 0x4c, 0xf4, 0x08, 0xd5, 0xa5,
        0x65, 0x92, 0xf8, 0x27, 0x4e, 0xec, 0x53, 0xf0
    };
    static const uint8_t ripemd160_expected[20] = {
        0x8e, 0xb2, 0x08, 0xf7, 0xe0, 0x5d, 0x98, 0x7a,
        0x9b, 0x04, 0x4a, 0x8e, 0x98, 0xc6, 0xb0, 0x87,
        0xf1, 0x5a, 0x0b, 0xfc
    };
    static const XSslHashVector vectors[] = {
        { PSA_ALG_MD5, sizeof(md5_expected), md5_expected },
        { PSA_ALG_SHA_1, sizeof(sha1_expected), sha1_expected },
        { PSA_ALG_SHA_224, sizeof(sha224_expected), sha224_expected },
        { PSA_ALG_SHA_256, sizeof(expected), expected },
        { PSA_ALG_SHA_384, sizeof(sha384_expected), sha384_expected },
        { PSA_ALG_SHA_512, sizeof(sha512_expected), sha512_expected },
        { PSA_ALG_SHA3_224, sizeof(sha3_224_expected), sha3_224_expected },
        { PSA_ALG_SHA3_256, sizeof(sha3_256_expected), sha3_256_expected },
        { PSA_ALG_SHA3_384, sizeof(sha3_384_expected), sha3_384_expected },
        { PSA_ALG_SHA3_512, sizeof(sha3_512_expected), sha3_512_expected },
        { PSA_ALG_RIPEMD160, sizeof(ripemd160_expected), ripemd160_expected }
    };
    uint8_t digest[sizeof(sha512_expected)];
    size_t digest_size = 0;
    size_t index;
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    psa_hash_operation_t clone = PSA_HASH_OPERATION_INIT;
    psa_status_t status;

    for (index = 0; index < sizeof(vectors) / sizeof(vectors[0]); ++index) {
        status = psa_hash_compute(vectors[index].algorithm,
                                           (const uint8_t*)"abc", 3,
                                           digest, sizeof(digest), &digest_size);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                          digest_size == vectors[index].length &&
                          memcmp(digest, vectors[index].expected, vectors[index].length) == 0,
                          "PSA hash family uses XCryptographic", "摘要标准向量不匹配");
    }
    status = psa_hash_compute(PSA_ALG_SHA_256,
                              (const uint8_t*)"abc", 3,
                              digest, sizeof(digest), &digest_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && digest_size == sizeof(expected) &&
                      memcmp(digest, expected, sizeof(expected)) == 0,
                      "PSA SHA-256 uses XCryptographic", "摘要标准向量不匹配");
    status = psa_hash_setup(&operation, PSA_ALG_SHA_256);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHA-256 streaming setup", "流式摘要初始化失败");
    status = psa_hash_update(&operation, (const uint8_t*)"a", 1);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHA-256 streaming update", "第一段摘要更新失败");
    status = psa_hash_clone(&operation, &clone);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHA-256 streaming clone", "摘要上下文克隆失败");
    status = psa_hash_update(&operation, (const uint8_t*)"bc", 2);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHA-256 streaming update", "第二段摘要更新失败");
    status = psa_hash_update(&clone, (const uint8_t*)"bc", 2);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA SHA-256 streaming clone", "克隆上下文更新失败");
    digest_size = 0;
    status = psa_hash_finish(&operation, digest, sizeof(digest), &digest_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && digest_size == sizeof(expected) &&
                      memcmp(digest, expected, sizeof(expected)) == 0,
                      "PSA SHA-256 streaming uses XCryptographic", "流式摘要结果错误");
    digest_size = 0;
    status = psa_hash_finish(&clone, digest, sizeof(digest), &digest_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && digest_size == sizeof(expected) &&
                      memcmp(digest, expected, sizeof(expected)) == 0,
                      "PSA SHA-256 streaming clone", "克隆摘要结果错误");
    (void) psa_hash_abort(&operation);
    (void) psa_hash_abort(&clone);
    XSSL_TEST_PASS("PSA SHA-256 uses XCryptographic");
    return true;
}

static bool xssl_test_psa_mac_backend(void)
{
    typedef struct XSslHmacVector {
        psa_algorithm_t algorithm;
        size_t length;
        const uint8_t *expected;
    } XSslHmacVector;
    static const uint8_t hmac_key[20] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
    };
    static const uint8_t hmac_md5_expected[16] = {
        0x5c, 0xce, 0xc3, 0x4e, 0xa9, 0x65, 0x63, 0x92,
        0x45, 0x7f, 0xa1, 0xac, 0x27, 0xf0, 0x8f, 0xbc
    };
    static const uint8_t hmac_sha1_expected[20] = {
        0xb6, 0x17, 0x31, 0x86, 0x55, 0x05, 0x72, 0x64,
        0xe2, 0x8b, 0xc0, 0xb6, 0xfb, 0x37, 0x8c, 0x8e,
        0xf1, 0x46, 0xbe, 0x00
    };
    static const uint8_t hmac_sha224_expected[28] = {
        0x89, 0x6f, 0xb1, 0x12, 0x8a, 0xbb, 0xdf, 0x19,
        0x68, 0x32, 0x10, 0x7c, 0xd4, 0x9d, 0xf3, 0x3f,
        0x47, 0xb4, 0xb1, 0x16, 0x99, 0x12, 0xba, 0x4f,
        0x53, 0x68, 0x4b, 0x22
    };
    static const uint8_t hmac_expected[32] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
        0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
        0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
    };
    static const uint8_t hmac_sha384_expected[48] = {
        0xaf, 0xd0, 0x39, 0x44, 0xd8, 0x48, 0x95, 0x62,
        0x6b, 0x08, 0x25, 0xf4, 0xab, 0x46, 0x90, 0x7f,
        0x15, 0xf9, 0xda, 0xdb, 0xe4, 0x10, 0x1e, 0xc6,
        0x82, 0xaa, 0x03, 0x4c, 0x7c, 0xeb, 0xc5, 0x9c,
        0xfa, 0xea, 0x9e, 0xa9, 0x07, 0x6e, 0xde, 0x7f,
        0x4a, 0xf1, 0x52, 0xe8, 0xb2, 0xfa, 0x9c, 0xb6
    };
    static const uint8_t hmac_sha512_expected[64] = {
        0x87, 0xaa, 0x7c, 0xde, 0xa5, 0xef, 0x61, 0x9d,
        0x4f, 0xf0, 0xb4, 0x24, 0x1a, 0x1d, 0x6c, 0xb0,
        0x23, 0x79, 0xf4, 0xe2, 0xce, 0x4e, 0xc2, 0x78,
        0x7a, 0xd0, 0xb3, 0x05, 0x45, 0xe1, 0x7c, 0xde,
        0xda, 0xa8, 0x33, 0xb7, 0xd6, 0xb8, 0xa7, 0x02,
        0x03, 0x8b, 0x27, 0x4e, 0xae, 0xa3, 0xf4, 0xe4,
        0xbe, 0x9d, 0x91, 0x4e, 0xeb, 0x61, 0xf1, 0x70,
        0x2e, 0x69, 0x6c, 0x20, 0x3a, 0x12, 0x68, 0x54
    };
    static const uint8_t hmac_sha3_256_expected[32] = {
        0xba, 0x85, 0x19, 0x23, 0x10, 0xdf, 0xfa, 0x96,
        0xe2, 0xa3, 0xa4, 0x0e, 0x69, 0x77, 0x43, 0x51,
        0x14, 0x0b, 0xb7, 0x18, 0x5e, 0x12, 0x02, 0xcd,
        0xcc, 0x91, 0x75, 0x89, 0xf9, 0x5e, 0x16, 0xbb
    };
    static const uint8_t hmac_ripemd160_expected[20] = {
        0x24, 0xcb, 0x4b, 0xd6, 0x7d, 0x20, 0xfc, 0x1a,
        0x5d, 0x2e, 0xd7, 0x73, 0x2d, 0xcc, 0x39, 0x37,
        0x7f, 0x0a, 0x56, 0x68
    };
    static const XSslHmacVector hmac_vectors[] = {
        { PSA_ALG_HMAC(PSA_ALG_MD5), sizeof(hmac_md5_expected), hmac_md5_expected },
        { PSA_ALG_HMAC(PSA_ALG_SHA_1), sizeof(hmac_sha1_expected), hmac_sha1_expected },
        { PSA_ALG_HMAC(PSA_ALG_SHA_224), sizeof(hmac_sha224_expected), hmac_sha224_expected },
        { PSA_ALG_HMAC(PSA_ALG_SHA_256), sizeof(hmac_expected), hmac_expected },
        { PSA_ALG_HMAC(PSA_ALG_SHA_384), sizeof(hmac_sha384_expected), hmac_sha384_expected },
        { PSA_ALG_HMAC(PSA_ALG_SHA_512), sizeof(hmac_sha512_expected), hmac_sha512_expected },
        { PSA_ALG_HMAC(PSA_ALG_SHA3_256), sizeof(hmac_sha3_256_expected), hmac_sha3_256_expected },
        { PSA_ALG_HMAC(PSA_ALG_RIPEMD160), sizeof(hmac_ripemd160_expected), hmac_ripemd160_expected }
    };
    static const uint8_t cmac_key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const uint8_t cmac_message[16] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
    };
    static const uint8_t cmac_expected[16] = {
        0x07, 0x0a, 0x16, 0xb4, 0x6b, 0x4d, 0x41, 0x44,
        0xf7, 0x9b, 0xdd, 0x9d, 0xd0, 0x4a, 0x28, 0x7c
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    psa_algorithm_t algorithm = PSA_ALG_HMAC(PSA_ALG_SHA_256);
    psa_mac_operation_t operation = PSA_MAC_OPERATION_INIT;
    uint8_t mac[PSA_MAC_MAX_SIZE];
    size_t mac_size = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, sizeof(hmac_key) * 8u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attributes, algorithm);
    status = psa_import_key(&attributes, hmac_key, sizeof(hmac_key), &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HMAC-SHA-256 setup", "密钥导入失败");
    status = psa_mac_compute(key, algorithm, (const uint8_t*)"Hi There", 8,
                             mac, sizeof(mac), &mac_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && mac_size == sizeof(hmac_expected) &&
                      memcmp(mac, hmac_expected, sizeof(hmac_expected)) == 0,
                      "PSA HMAC-SHA-256 uses XCryptographic", "RFC 4231 向量不匹配");
    status = psa_mac_sign_setup(&operation, key, algorithm);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HMAC-SHA-256 streaming setup", "流式 MAC 初始化失败");
    status = psa_mac_update(&operation, (const uint8_t*)"Hi ", 3);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HMAC-SHA-256 streaming update", "第一段 MAC 更新失败");
    status = psa_mac_update(&operation, (const uint8_t*)"There", 5);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HMAC-SHA-256 streaming update", "第二段 MAC 更新失败");
    mac_size = 0;
    status = psa_mac_sign_finish(&operation, mac, sizeof(mac), &mac_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && mac_size == sizeof(hmac_expected) &&
                      memcmp(mac, hmac_expected, sizeof(hmac_expected)) == 0,
                      "PSA HMAC-SHA-256 streaming uses XCryptographic", "流式 MAC 结果错误");
    (void) psa_mac_abort(&operation);
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS,
                      "PSA HMAC-SHA-256 setup", "密钥销毁失败");
    XSSL_TEST_PASS("PSA HMAC-SHA-256 uses XCryptographic");

    for (size_t index = 0; index < sizeof(hmac_vectors) / sizeof(hmac_vectors[0]); ++index) {
        key = MBEDTLS_SVC_KEY_ID_INIT;
        psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
        psa_set_key_bits(&attributes, sizeof(hmac_key) * 8u);
        psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE);
        psa_set_key_algorithm(&attributes, hmac_vectors[index].algorithm);
        status = psa_import_key(&attributes, hmac_key, sizeof(hmac_key), &key);
        psa_reset_key_attributes(&attributes);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA HMAC hash-family setup", "摘要族密钥导入失败");
        status = psa_mac_compute(key, hmac_vectors[index].algorithm,
                                 (const uint8_t*)"Hi There", 8,
                                 mac, sizeof(mac), &mac_size);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS &&
                          mac_size == hmac_vectors[index].length &&
                          memcmp(mac, hmac_vectors[index].expected, mac_size) == 0,
                          "PSA HMAC hash-family uses XCryptographic", "HMAC 摘要族向量不匹配");
        XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS,
                          "PSA HMAC hash-family cleanup", "摘要族密钥销毁失败");
    }
    XSSL_TEST_PASS("PSA HMAC hash-family uses XCryptographic");

    key = MBEDTLS_SVC_KEY_ID_INIT;
    algorithm = PSA_ALG_CMAC;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, sizeof(cmac_key) * 8u);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attributes, algorithm);
    status = psa_import_key(&attributes, cmac_key, sizeof(cmac_key), &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CMAC setup", "密钥导入失败");
    status = psa_mac_compute(key, algorithm, cmac_message, sizeof(cmac_message),
                             mac, sizeof(mac), &mac_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && mac_size == sizeof(cmac_expected) &&
                      memcmp(mac, cmac_expected, sizeof(cmac_expected)) == 0,
                      "PSA AES-CMAC uses XCryptographic", "RFC 4493 向量不匹配");
    operation = (psa_mac_operation_t)PSA_MAC_OPERATION_INIT;
    status = psa_mac_sign_setup(&operation, key, algorithm);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CMAC streaming setup", "流式 CMAC 初始化失败");
    status = psa_mac_update(&operation, cmac_message, 7);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CMAC streaming update", "第一段 CMAC 更新失败");
    status = psa_mac_update(&operation, cmac_message + 7, sizeof(cmac_message) - 7);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-CMAC streaming update", "第二段 CMAC 更新失败");
    mac_size = 0;
    status = psa_mac_sign_finish(&operation, mac, sizeof(mac), &mac_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && mac_size == sizeof(cmac_expected) &&
                      memcmp(mac, cmac_expected, sizeof(cmac_expected)) == 0,
                      "PSA AES-CMAC streaming uses XCryptographic", "流式 CMAC 结果错误");
    (void) psa_mac_abort(&operation);
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS,
                      "PSA AES-CMAC setup", "密钥销毁失败");
    XSSL_TEST_PASS("PSA AES-CMAC uses XCryptographic");
    return true;
}

static bool xssl_test_session_tls12(void)
{
    XSslCertificate* certificate;
    XSslKey* private_key;
    XSslSession* client;
    XSslSession* server;
    XSslTestPipe client_to_server = { { 0 }, 0, 0 };
    XSslTestPipe server_to_client = { { 0 }, 0, 0 };
    XSslTestBio client_bio = { &server_to_client, &client_to_server };
    XSslTestBio server_bio = { &client_to_server, &server_to_client };
    static const uint8_t request[] = "client to server through XSsl";
    static const uint8_t response[] = "server to client through XSsl";
    uint8_t output[128];
    size_t step;
    bool complete = false;

    certificate = XSsl_certificateFromPem(xssl_test_certificate, strlen(xssl_test_certificate));
    private_key = XSsl_keyFromPem(xssl_test_private_key, strlen(xssl_test_private_key),
                                  XSSL_KeyAlgorithm_Ec, XSSL_PrivateKey, NULL);
    client = XSsl_sessionCreate(XSSL_TlsV1_2, false);
    server = XSsl_sessionCreate(XSSL_TlsV1_2, true);
    XSSL_TEST_REQUIRE(certificate && private_key && client && server,
                      "XSsl TLS 1.2 setup", "证书、私钥或会话创建失败");
    XSSL_TEST_REQUIRE(XSsl_sessionSetCipherSuites(client, "ECDHE-ECDSA-AES128-GCM-SHA256") &&
                      XSsl_sessionSetCipherSuites(server, "ECDHE-ECDSA-AES128-GCM-SHA256"),
                      "XSsl TLS 1.2 cipher", "无法设置 AES-GCM 密码套件");
    XSsl_sessionSetPeerVerify(client, XSSL_VerifyNone);
    XSsl_sessionSetPeerVerify(server, XSSL_VerifyNone);
    XSSL_TEST_REQUIRE(XSsl_sessionSetCertificate(server, certificate, private_key),
                      "XSsl TLS 1.2 certificate", "无法设置服务端证书");
    XSsl_sessionSetBio(client, &client_bio, xssl_test_bio_send, xssl_test_bio_recv);
    XSsl_sessionSetBio(server, &server_bio, xssl_test_bio_send, xssl_test_bio_recv);
    for (step = 0; step < 1000; ++step) {
        int client_status = XSsl_sessionHandshake(client);
        int server_status = XSsl_sessionHandshake(server);
        XSSL_TEST_REQUIRE(client_status != XSSL_S_ERROR && client_status != XSSL_S_CLOSED &&
                          server_status != XSSL_S_ERROR && server_status != XSSL_S_CLOSED,
                          "XSsl TLS 1.2 handshake", "内存 BIO 握手失败");
        if (XSsl_sessionIsEncrypted(client) && XSsl_sessionIsEncrypted(server)) {
            complete = true;
            break;
        }
    }
    XSSL_TEST_REQUIRE(complete && strcmp(XSsl_sessionProtocolString(client), "TLSv1.2") == 0 &&
                      XSsl_sessionCipherName(client) != NULL,
                      "XSsl TLS 1.2 handshake", "握手未完成或协商结果错误");
    XSSL_TEST_REQUIRE(XSsl_sessionWrite(client, request, sizeof(request) - 1u) ==
                      (int)(sizeof(request) - 1u),
                      "XSsl client record", "客户端记录加密失败");
    for (step = 0; step < 16; ++step) {
        int received = XSsl_sessionRead(server, output, sizeof(output));
        if (received > 0) {
            XSSL_TEST_REQUIRE((size_t)received == sizeof(request) - 1u &&
                              memcmp(output, request, sizeof(request) - 1u) == 0,
                              "XSsl client record", "服务端解密数据错误");
            break;
        }
        XSSL_TEST_REQUIRE(received != XSSL_S_ERROR, "XSsl client record", "服务端读取记录失败");
        if (step == 15) XSSL_TEST_REQUIRE(false, "XSsl client record", "服务端未收到应用数据");
    }
    XSSL_TEST_REQUIRE(XSsl_sessionWrite(server, response, sizeof(response) - 1u) ==
                      (int)(sizeof(response) - 1u),
                      "XSsl server record", "服务端记录加密失败");
    for (step = 0; step < 16; ++step) {
        int received = XSsl_sessionRead(client, output, sizeof(output));
        if (received > 0) {
            XSSL_TEST_REQUIRE((size_t)received == sizeof(response) - 1u &&
                              memcmp(output, response, sizeof(response) - 1u) == 0,
                              "XSsl server record", "客户端解密数据错误");
            break;
        }
        XSSL_TEST_REQUIRE(received != XSSL_S_ERROR, "XSsl server record", "客户端读取记录失败");
        if (step == 15) XSSL_TEST_REQUIRE(false, "XSsl server record", "客户端未收到应用数据");
    }
    XSsl_sessionDestroy(client);
    XSsl_sessionDestroy(server);
    XSsl_keyDestroy(private_key);
    XSsl_certificateDestroy(certificate);
    XSSL_TEST_PASS("TLS 1.2 memory-BIO handshake and AES-GCM records");
    return true;
}

static void xssl_test_delete_protocols(XVector* protocols)
{
    size_t index;
    if (!protocols) return;
    for (index = 0; index < XContainer_size_base((const XContainer*)protocols); ++index) {
        XByteArray** value = (XByteArray**)XVector_at_base(protocols, (int64_t)index);
        if (value && *value) XClass_delete_base((XClass*)*value);
    }
    XVector_delete_base((XClass*)protocols);
}

static bool xssl_test_socket_configuration(void)
{
    XSslSocket* socket = XSslSocket_create();
    XString* cipher_suites = XString_create_utf8("ECDHE-ECDSA-AES128-GCM-SHA256");
    XVector* protocols = XVector_create(sizeof(XByteArray*));
    XVector* copied_protocols;
    XByteArray* h2 = XByteArray_create_utf8("h2");
    XByteArray* http11 = XByteArray_create_utf8("http/1.1");
    XSSL_TEST_REQUIRE(socket && cipher_suites && protocols && h2 && http11,
                      "XSslSocket create", "对象创建失败");
    XSSL_TEST_REQUIRE(strcmp(XSsl_backendName(), "mbedtls") == 0 &&
                      XSslSocket_protocol(socket) == XSSL_SecureProtocols,
                      "XSslSocket backend", "后端或默认协议错误");
    XSslSocket_setProtocol(socket, XSSL_TlsV1_2);
    XSslSocket_setPeerVerifyMode(socket, XSSL_VerifyNone);
    XSSL_TEST_REQUIRE(XSslSocket_protocol(socket) == XSSL_TlsV1_2 &&
                      XSslSocket_peerVerifyMode(socket) == XSSL_VerifyNone &&
                      XSslSocket_setCipherSuites(socket, cipher_suites) &&
                      XVector_push_back_1_base(protocols, &h2) &&
                      XVector_push_back_1_base(protocols, &http11) &&
                      XSslSocket_setAllowedNextProtocols(socket, protocols),
                      "XSslSocket configuration", "配置接口失败");
    copied_protocols = XSslSocket_allowedNextProtocols(socket);
    XSSL_TEST_REQUIRE(copied_protocols && XContainer_size_base((const XContainer*)copied_protocols) == 2u &&
                      XSslSocket_nextProtocolNegotiationStatus(socket) == XSSL_NextProtocolNegotiationUnsupported,
                      "XSslSocket ALPN", "ALPN 配置或深拷贝错误");
    xssl_test_delete_protocols(copied_protocols);
    XString_delete_base((XClass*)cipher_suites);
    xssl_test_delete_protocols(protocols);
    XClass_delete_base((XClass*)socket);
    XSSL_TEST_PASS("XSslSocket configuration and lifecycle");
    return true;
}

static bool xssl_test_socket_tls12_loopback(void)
{
    XSslCertificate* certificate = NULL;
    XSslKey* private_key = NULL;
    XTcpServer* listener = NULL;
    XSslSocket* client = NULL;
    XSslSocket* server = NULL;
    XString* host_name = NULL;
    XString* cipher_suites = NULL;
    XSslTestSocketFactory factory;
    static const char request[] = "client XSslSocket request";
    static const char response[] = "server XSslSocket response";
    char output[128];
    uint64_t deadline;
    bool ok = false;

    certificate = XSsl_certificateFromPem(xssl_test_certificate, strlen(xssl_test_certificate));
    private_key = XSsl_keyFromPem(xssl_test_private_key, strlen(xssl_test_private_key),
                                  XSSL_KeyAlgorithm_Ec, XSSL_PrivateKey, NULL);
    listener = XTcpServer_create();
    client = XSslSocket_create();
    host_name = XString_create_utf8("127.0.0.1");
    cipher_suites = XString_create_utf8("ECDHE-ECDSA-AES128-GCM-SHA256");
    if (!certificate || !private_key || !listener || !client || !host_name || !cipher_suites)
        goto cleanup;
    factory.certificate = certificate;
    factory.private_key = private_key;
    XTcpServer_setIncomingSocketFactory(listener, xssl_test_socket_factory, &factory);
    if (!XTcpServer_listen(listener, NULL, 0))
        goto cleanup;
    XSslSocket_setProtocol(client, XSSL_TlsV1_2);
    XSslSocket_setPeerVerifyMode(client, XSSL_VerifyNone);
    if (!XSslSocket_setCipherSuites(client, cipher_suites))
        goto cleanup;
    XSslSocket_connectToHostEncrypted(client, host_name, XTcpServer_serverPort(listener));
    deadline = XDateTime_currentMSecsSinceEpoch() + 10000u;
    while (!XTcpServer_hasPendingConnections_base(listener) &&
           XDateTime_currentMSecsSinceEpoch() < deadline) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }
    if (!XTcpServer_hasPendingConnections_base(listener))
        goto cleanup;
    server = (XSslSocket*)XTcpServer_nextPendingConnection_base(listener);
    if (!server)
        goto cleanup;
    XSslSocket_startServerEncryption(server);
    deadline = XDateTime_currentMSecsSinceEpoch() + 10000u;
    while ((!XSslSocket_waitForEncrypted(client, 5) ||
            !XSslSocket_waitForEncrypted(server, 5)) &&
           XDateTime_currentMSecsSinceEpoch() < deadline) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(1);
    }
    if (!XSslSocket_sessionCipher(client) || !XSslSocket_sessionCipher(server))
        goto cleanup;
    if (XSslSocket_write_1((XIODevice*)client, request, (int64_t)strlen(request)) !=
        (int64_t)strlen(request))
        goto cleanup;
    deadline = XDateTime_currentMSecsSinceEpoch() + 5000u;
    while (XDateTime_currentMSecsSinceEpoch() < deadline) {
        int64_t received;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        received = XSslSocket_read_1((XIODevice*)server, output, (int64_t)sizeof(output));
        if (received > 0) {
            if ((size_t)received != strlen(request) || memcmp(output, request, strlen(request)) != 0)
                goto cleanup;
            break;
        }
        XThread_msleep(1);
    }
    if (XDateTime_currentMSecsSinceEpoch() >= deadline)
        goto cleanup;
    if (XSslSocket_write_1((XIODevice*)server, response, (int64_t)strlen(response)) !=
        (int64_t)strlen(response))
        goto cleanup;
    deadline = XDateTime_currentMSecsSinceEpoch() + 5000u;
    while (XDateTime_currentMSecsSinceEpoch() < deadline) {
        int64_t received;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        received = XSslSocket_read_1((XIODevice*)client, output, (int64_t)sizeof(output));
        if (received > 0) {
            if ((size_t)received != strlen(response) || memcmp(output, response, strlen(response)) != 0)
                goto cleanup;
            ok = true;
            break;
        }
        XThread_msleep(1);
    }
cleanup:
    if (client) {
        XSslSocket_disconnectFromHost_base((XAbstractSocket*)client);
        XClass_delete_base((XClass*)client);
    }
    if (listener) {
        XTcpServer_close(listener);
        XClass_delete_base((XClass*)listener);
    }
    if (cipher_suites) XString_delete_base((XClass*)cipher_suites);
    if (host_name) XString_delete_base((XClass*)host_name);
    if (private_key) XSsl_keyDestroy(private_key);
    if (certificate) XSsl_certificateDestroy(certificate);
    XSSL_TEST_REQUIRE(ok, "XSslSocket local TLS 1.2", "本地回环握手或双向读写失败");
    XSSL_TEST_PASS("XSslSocket local TLS 1.2 handshake and records");
    return true;
}

static bool xssl_test_psa_block_roundtrip(
    psa_key_type_t key_type, size_t key_size, psa_algorithm_t algorithm,
    const uint8_t* key_data, const uint8_t* iv, size_t iv_size,
    const uint8_t* plain_text, size_t plain_text_size,
    const uint8_t* expected, size_t first_out_len, const char* name)
{
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t output[64];
    size_t output_size = 0;
    size_t size = 0;
    size_t first_len = first_out_len;
    psa_status_t status;

    if (plain_text_size > sizeof(output)) return false;
    psa_set_key_type(&attributes, key_type);
    psa_set_key_bits(&attributes, (uint16_t)(key_size * 8u));
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, algorithm);
    status = psa_import_key(&attributes, key_data, key_size, &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, name, "PSA 密钥导入失败");

    /* 加密：先 17 字节、再剩余部分，最后 finish。 */
    status = psa_cipher_encrypt_setup(&operation, key, algorithm);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, name, "PSA 加密上下文初始化失败");
    if (iv && iv_size != 0) {
        status = psa_cipher_set_iv(&operation, iv, iv_size);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, name, "PSA IV 设置失败");
    }
    if (first_len > plain_text_size) first_len = plain_text_size;
    status = psa_cipher_update(&operation, plain_text, first_len,
                               output, sizeof(output), &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == first_len, name, "PSA 第一段加密失败");
    output_size = size;
    status = psa_cipher_update(&operation, plain_text + first_len,
                               plain_text_size - first_len,
                               output + output_size, sizeof(output) - output_size, &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == plain_text_size - first_len,
                      name, "PSA 第二段加密失败");
    output_size += size;
    status = psa_cipher_finish(&operation, output + output_size,
                               sizeof(output) - output_size, &size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && size == 0 && output_size == plain_text_size &&
                      memcmp(output, expected, plain_text_size) == 0,
                      name, "PSA 加密向量不匹配");
    (void) psa_cipher_abort(&operation);

    /* 解密：在一段内完成。 */
    operation = (psa_cipher_operation_t)PSA_CIPHER_OPERATION_INIT;
    status = psa_cipher_decrypt_setup(&operation, key, algorithm);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, name, "PSA 解密上下文初始化失败");
    if (iv && iv_size != 0) {
        status = psa_cipher_set_iv(&operation, iv, iv_size);
        XSSL_TEST_REQUIRE(status == PSA_SUCCESS, name, "PSA 解密 IV 设置失败");
    }
    memset(output, 0, sizeof(output));
    status = psa_cipher_update(&operation, expected, plain_text_size,
                               output, sizeof(output), &output_size);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && output_size == plain_text_size &&
                      memcmp(output, plain_text, plain_text_size) == 0,
                      name, "PSA 解密向量不匹配");
    (void) psa_cipher_abort(&operation);
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS, name, "PSA 密钥销毁失败");
    XSSL_TEST_PASS(name);
    return true;
}

static bool xssl_test_psa_key_wrap_backend(void)
{
    static const uint8_t kw_kek[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t kw_plain_text[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    static const uint8_t kw_expected[24] = {
        0x1f, 0xa6, 0x8b, 0x0a, 0x81, 0x12, 0xb4, 0x47,
        0xae, 0xf3, 0x4b, 0xd8, 0xfb, 0x5a, 0x7b, 0x82,
        0x9d, 0x3e, 0x86, 0x23, 0x71, 0xd2, 0xcf, 0xe5
    };
    static const uint8_t kwp_kek[24] = {
        0x58, 0x40, 0xdf, 0x6e, 0x29, 0xb0, 0x2a, 0xf1,
        0xab, 0x49, 0x3b, 0x70, 0x5b, 0xf1, 0x6e, 0xa1,
        0xae, 0x83, 0x38, 0xf4, 0xdc, 0xc1, 0x76, 0xa8
    };
    static const uint8_t kwp_plain_text[20] = {
        0xc3, 0x7b, 0x7e, 0x64, 0x92, 0x58, 0x43, 0x40,
        0xbe, 0xd1, 0x22, 0x07, 0x80, 0x89, 0x41, 0x15,
        0x50, 0x68, 0xf7, 0x38
    };
    static const uint8_t kwp_expected[32] = {
        0x13, 0x8b, 0xde, 0xaa, 0x9b, 0x8f, 0xa7, 0xfc,
        0x61, 0xf9, 0x77, 0x42, 0xe7, 0x22, 0x48, 0xee,
        0x5a, 0xe6, 0xae, 0x53, 0x60, 0xd1, 0xae, 0x6a,
        0x5f, 0x54, 0xf3, 0x73, 0xfa, 0x54, 0x3b, 0x6a
    };
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    uint8_t output[32];
    size_t output_length = 0;
    psa_status_t status;

    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECB_NO_PADDING);
    status = psa_import_key(&attributes, kw_kek, sizeof(kw_kek), &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-KW setup", "AES-KW 密钥导入失败");
    status = mbedtls_nist_kw_wrap(key, MBEDTLS_KW_MODE_KW,
                                  kw_plain_text, sizeof(kw_plain_text),
                                  output, sizeof(output), &output_length);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && output_length == sizeof(kw_expected) &&
                      memcmp(output, kw_expected, sizeof(kw_expected)) == 0,
                      "PSA AES-KW uses XCryptographic", "AES-KW 封装向量不匹配");
    status = mbedtls_nist_kw_unwrap(key, MBEDTLS_KW_MODE_KW,
                                    kw_expected, sizeof(kw_expected),
                                    output, sizeof(output), &output_length);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && output_length == sizeof(kw_plain_text) &&
                      memcmp(output, kw_plain_text, sizeof(kw_plain_text)) == 0,
                      "PSA AES-KW unwrap uses XCryptographic", "AES-KW 解封结果不匹配");
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS,
                      "PSA AES-KW cleanup", "AES-KW 密钥销毁失败");

    attributes = (psa_key_attributes_t) PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 192);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECB_NO_PADDING);
    status = psa_import_key(&attributes, kwp_kek, sizeof(kwp_kek), &key);
    psa_reset_key_attributes(&attributes);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS, "PSA AES-KWP setup", "AES-KWP 密钥导入失败");
    status = mbedtls_nist_kw_wrap(key, MBEDTLS_KW_MODE_KWP,
                                  kwp_plain_text, sizeof(kwp_plain_text),
                                  output, sizeof(output), &output_length);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && output_length == sizeof(kwp_expected) &&
                      memcmp(output, kwp_expected, sizeof(kwp_expected)) == 0,
                      "PSA AES-KWP uses XCryptographic", "AES-KWP 封装向量不匹配");
    status = mbedtls_nist_kw_unwrap(key, MBEDTLS_KW_MODE_KWP,
                                    kwp_expected, sizeof(kwp_expected),
                                    output, sizeof(output), &output_length);
    XSSL_TEST_REQUIRE(status == PSA_SUCCESS && output_length == sizeof(kwp_plain_text) &&
                      memcmp(output, kwp_plain_text, sizeof(kwp_plain_text)) == 0,
                      "PSA AES-KWP unwrap uses XCryptographic", "AES-KWP 解封结果不匹配");
    XSSL_TEST_REQUIRE(psa_destroy_key(key) == PSA_SUCCESS,
                      "PSA AES-KWP cleanup", "AES-KWP 密钥销毁失败");
    XSSL_TEST_PASS("PSA AES-KW/KWP uses XCryptographic");
    return true;
}

static bool xssl_test_psa_aria_camellia_backend(void)
{
    /* ARIA：ECB/CBC/CFB/OFB 向量（与 XCryptographic 原语测试一致）。 */
    static const uint8_t aria_ecb_key[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    static const uint8_t aria_ecb_pt[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    static const uint8_t aria_ecb_ct[3][16] = {
        { 0xd7, 0x18, 0xfb, 0xd6, 0xab, 0x64, 0x4c, 0x73,
          0x9d, 0xa9, 0x5f, 0x3b, 0xe6, 0x45, 0x17, 0x78 },
        { 0x26, 0x44, 0x9c, 0x18, 0x05, 0xdb, 0xe7, 0xaa,
          0x25, 0xa4, 0x68, 0xce, 0x26, 0x3a, 0x9e, 0x79 },
        { 0xf9, 0x2b, 0xd7, 0xc7, 0x9f, 0xb7, 0x2e, 0x2f,
          0x2b, 0x8f, 0x80, 0xc1, 0x97, 0x2d, 0x24, 0xfc }
    };
    static const uint8_t aria_mode_key[32] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    static const uint8_t aria_mode_iv[16] = {
        0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78,
        0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0
    };
    static const uint8_t aria_mode_pt[48] = {
        0x11, 0x11, 0x11, 0x11, 0xaa, 0xaa, 0xaa, 0xaa,
        0x11, 0x11, 0x11, 0x11, 0xbb, 0xbb, 0xbb, 0xbb,
        0x11, 0x11, 0x11, 0x11, 0xcc, 0xcc, 0xcc, 0xcc,
        0x11, 0x11, 0x11, 0x11, 0xdd, 0xdd, 0xdd, 0xdd,
        0x22, 0x22, 0x22, 0x22, 0xaa, 0xaa, 0xaa, 0xaa,
        0x22, 0x22, 0x22, 0x22, 0xbb, 0xbb, 0xbb, 0xbb
    };
    static const uint8_t aria_cbc_ct[3][48] = {
        { 0x49, 0xd6, 0x18, 0x60, 0xb1, 0x49, 0x09, 0x10,
          0x9c, 0xef, 0x0d, 0x22, 0xa9, 0x26, 0x81, 0x34,
          0xfa, 0xdf, 0x9f, 0xb2, 0x31, 0x51, 0xe9, 0x64,
          0x5f, 0xba, 0x75, 0x01, 0x8b, 0xdb, 0x15, 0x38,
          0xb5, 0x33, 0x34, 0x63, 0x4b, 0xbf, 0x7d, 0x4c,
          0xd4, 0xb5, 0x37, 0x70, 0x33, 0x06, 0x0c, 0x15 },
        { 0xaf, 0xe6, 0xcf, 0x23, 0x97, 0x4b, 0x53, 0x3c,
          0x67, 0x2a, 0x82, 0x62, 0x64, 0xea, 0x78, 0x5f,
          0x4e, 0x4f, 0x7f, 0x78, 0x0d, 0xc7, 0xf3, 0xf1,
          0xe0, 0x96, 0x2b, 0x80, 0x90, 0x23, 0x86, 0xd5,
          0x14, 0xe9, 0xc3, 0xe7, 0x72, 0x59, 0xde, 0x92,
          0xdd, 0x11, 0x02, 0xff, 0xab, 0x08, 0x6c, 0x1e },
        { 0x52, 0x3a, 0x8a, 0x80, 0x6a, 0xe6, 0x21, 0xf1,
          0x55, 0xfd, 0xd2, 0x8d, 0xbc, 0x34, 0xe1, 0xab,
          0x7b, 0x9b, 0x42, 0x43, 0x2a, 0xd8, 0xb2, 0xef,
          0xb9, 0x6e, 0x23, 0xb1, 0x3f, 0x0a, 0x6e, 0x52,
          0xf3, 0x61, 0x85, 0xd5, 0x0a, 0xd0, 0x02, 0xc5,
          0xf6, 0x01, 0xbe, 0xe5, 0x49, 0x3f, 0x11, 0x8b }
    };
    static const uint8_t aria_cfb_ct[3][48] = {
        { 0x37, 0x20, 0xe5, 0x3b, 0xa7, 0xd6, 0x15, 0x38,
          0x34, 0x06, 0xb0, 0x9f, 0x0a, 0x05, 0xa2, 0x00,
          0xc0, 0x7c, 0x21, 0xe6, 0x37, 0x0f, 0x41, 0x3a,
          0x5d, 0x13, 0x25, 0x00, 0xa6, 0x82, 0x85, 0x01,
          0x7c, 0x61, 0xb4, 0x34, 0xc7, 0xb7, 0xca, 0x96,
          0x85, 0xa5, 0x10, 0x71, 0x86, 0x1e, 0x4d, 0x4b },
        { 0x41, 0x71, 0xf7, 0x19, 0x2b, 0xf4, 0x49, 0x54,
          0x94, 0xd2, 0x73, 0x61, 0x29, 0x64, 0x0f, 0x5c,
          0x4d, 0x87, 0xa9, 0xa2, 0x13, 0x66, 0x4c, 0x94,
          0x48, 0x47, 0x7c, 0x6e, 0xcc, 0x20, 0x13, 0x59,
          0x8d, 0x97, 0x66, 0x95, 0x2d, 0xd8, 0xc3, 0x86,
          0x8f, 0x17, 0xe3, 0x6e, 0xf6, 0x6f, 0xd8, 0x4b },
        { 0x26, 0x83, 0x47, 0x05, 0xb0, 0xf2, 0xc0, 0xe2,
          0x58, 0x8d, 0x4a, 0x7f, 0x09, 0x00, 0x96, 0x35,
          0xf2, 0x8b, 0xb9, 0x3d, 0x8c, 0x31, 0xf8, 0x70,
          0xec, 0x1e, 0x0b, 0xdb, 0x08, 0x2b, 0x66, 0xfa,
          0x40, 0x2d, 0xd9, 0xc2, 0x02, 0xbe, 0x30, 0x0c,
          0x45, 0x17, 0xd1, 0x96, 0xb1, 0x4d, 0x4c, 0xe1 }
    };
    static const uint8_t aria_ofb_ct[3][48] = {
        { 0x37, 0x20, 0xe5, 0x3b, 0xa7, 0xd6, 0x15, 0x38,
          0x34, 0x06, 0xb0, 0x9f, 0x0a, 0x05, 0xa2, 0x00,
          0x00, 0x63, 0x06, 0x3f, 0x05, 0x60, 0x08, 0x34,
          0x83, 0xfa, 0xeb, 0x04, 0x1c, 0x8a, 0xde, 0xce,
          0xf3, 0x0c, 0xf8, 0x0c, 0xef, 0xb0, 0x02, 0xa0,
          0xd2, 0x80, 0x75, 0x91, 0x68, 0xec, 0x01, 0xdb },
        { 0x41, 0x71, 0xf7, 0x19, 0x2b, 0xf4, 0x49, 0x54,
          0x94, 0xd2, 0x73, 0x61, 0x29, 0x64, 0x0f, 0x5c,
          0xc2, 0x24, 0xd2, 0x6d, 0x36, 0x4b, 0x5a, 0x06,
          0xdd, 0xde, 0x13, 0xd0, 0xf1, 0xe7, 0x4f, 0xaa,
          0x84, 0x6d, 0xe3, 0x54, 0xc6, 0x3c, 0xda, 0x77,
          0x46, 0x9d, 0x1a, 0x2d, 0x42, 0x5c, 0x47, 0xff },
        { 0x26, 0x83, 0x47, 0x05, 0xb0, 0xf2, 0xc0, 0xe2,
          0x58, 0x8d, 0x4a, 0x7f, 0x09, 0x00, 0x96, 0x35,
          0x84, 0xc2, 0x56, 0x81, 0x5c, 0x42, 0x92, 0xb5,
          0x9f, 0x8d, 0x3f, 0x96, 0x6a, 0x75, 0xb5, 0x23,
          0x45, 0xb4, 0xf5, 0xf9, 0x8c, 0x78, 0x5d, 0x3f,
          0x36, 0x8a, 0x8d, 0x5f, 0xf8, 0x9b, 0x7f, 0x95 }
    };

    /* Camellia：ECB/CBC/CFB/OFB 向量。 */
    static const uint8_t cam_ecb_key[3][32] = {
        { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
          0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10 },
        { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
          0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
          0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77 },
        { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
          0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
          0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
          0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff }
    };
    static const uint8_t cam_ecb_pt[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
    };
    static const uint8_t cam_ecb_ct[3][16] = {
        { 0x67, 0x67, 0x31, 0x38, 0x54, 0x96, 0x69, 0x73,
          0x08, 0x57, 0x06, 0x56, 0x48, 0xea, 0xbe, 0x43 },
        { 0xb4, 0x99, 0x34, 0x01, 0xb3, 0xe9, 0x96, 0xf8,
          0x4e, 0xe5, 0xce, 0xe7, 0xd7, 0x9b, 0x09, 0xb9 },
        { 0x9a, 0xcc, 0x23, 0x7d, 0xff, 0x16, 0xd7, 0x6c,
          0x20, 0xef, 0x7c, 0x91, 0x9e, 0x3a, 0x75, 0x09 }
    };
    static const uint8_t cam_cbc_key[3][32] = {
        { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
          0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c },
        { 0x8e, 0x73, 0xb0, 0xf7, 0xda, 0x0e, 0x64, 0x52,
          0xc8, 0x10, 0xf3, 0x2b, 0x80, 0x90, 0x79, 0xe5,
          0x62, 0xf8, 0xea, 0xd2, 0x52, 0x2c, 0x6b, 0x7b },
        { 0x60, 0x3d, 0xeb, 0x10, 0x15, 0xca, 0x71, 0xbe,
          0x2b, 0x73, 0xae, 0xf0, 0x85, 0x7d, 0x77, 0x81,
          0x1f, 0x35, 0x2c, 0x07, 0x3b, 0x61, 0x08, 0xd7,
          0x2d, 0x98, 0x10, 0xa3, 0x09, 0x14, 0xdf, 0xf4 }
    };
    static const uint8_t cam_cbc_iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t cam_cbc_pt[3][16] = {
        { 0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
          0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a },
        { 0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
          0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51 },
        { 0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
          0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef }
    };
    static const uint8_t cam_cbc_ct[3][3][16] = {
        { { 0x16, 0x07, 0xcf, 0x49, 0x4b, 0x36, 0xbb, 0xf0,
            0x0d, 0xae, 0xb0, 0xb5, 0x03, 0xc8, 0x31, 0xab },
          { 0xa2, 0xf2, 0xcf, 0x67, 0x16, 0x29, 0xef, 0x78,
            0x40, 0xc5, 0xa5, 0xdf, 0xb5, 0x07, 0x48, 0x87 },
          { 0x0f, 0x06, 0x16, 0x50, 0x08, 0xcf, 0x8b, 0x8b,
            0x5a, 0x63, 0x58, 0x63, 0x62, 0x54, 0x3e, 0x54 } },
        { { 0x2a, 0x48, 0x30, 0xab, 0x5a, 0xc4, 0xa1, 0xa2,
            0x40, 0x59, 0x55, 0xfd, 0x21, 0x95, 0xcf, 0x93 },
          { 0x5d, 0x5a, 0x86, 0x9b, 0xd1, 0x4c, 0xe5, 0x42,
            0x64, 0xf8, 0x92, 0xa6, 0xdd, 0x2e, 0xc3, 0xd5 },
          { 0x37, 0xd3, 0x59, 0xc3, 0x34, 0x98, 0x36, 0xd8,
            0x84, 0xe3, 0x10, 0xad, 0xdf, 0x68, 0xc4, 0x49 } },
        { { 0xe6, 0xcf, 0xa3, 0x5f, 0xc0, 0x2b, 0x13, 0x4a,
            0x4d, 0x2c, 0x0b, 0x67, 0x37, 0xac, 0x3e, 0xda },
          { 0x36, 0xcb, 0xeb, 0x73, 0xbd, 0x50, 0x4b, 0x40,
            0x70, 0xb1, 0xb7, 0xde, 0x2b, 0x21, 0xeb, 0x50 },
          { 0xe3, 0x1a, 0x60, 0x55, 0x29, 0x7d, 0x96, 0xca,
            0x33, 0x30, 0xcd, 0xf1, 0xb1, 0x86, 0x0a, 0x83 } }
    };
    static const uint8_t cam_cfb_ct[3][48] = {
        { 0x14, 0xf7, 0x64, 0x61, 0x87, 0x81, 0x7e, 0xb5,
          0x86, 0x59, 0x91, 0x46, 0xb8, 0x2b, 0xd7, 0x19,
          0xa5, 0x3d, 0x28, 0xbb, 0x82, 0xdf, 0x74, 0x11,
          0x03, 0xea, 0x4f, 0x92, 0x1a, 0x44, 0x88, 0x0b,
          0x9c, 0x21, 0x57, 0xa6, 0x64, 0x62, 0x6d, 0x1d,
          0xef, 0x9e, 0xa4, 0x20, 0xfd, 0xe6, 0x9b, 0x96 },
        { 0xc8, 0x32, 0xbb, 0x97, 0x80, 0x67, 0x7d, 0xaa,
          0x82, 0xd9, 0xb6, 0x86, 0x0d, 0xcd, 0x56, 0x5e,
          0x86, 0xf8, 0x49, 0x16, 0x27, 0x90, 0x6d, 0x78,
          0x0c, 0x7a, 0x6d, 0x46, 0xea, 0x33, 0x1f, 0x98,
          0x69, 0x51, 0x1c, 0xce, 0x59, 0x4c, 0xf7, 0x10,
          0xcb, 0x98, 0xbb, 0x63, 0xd7, 0x22, 0x1f, 0x01 },
        { 0xcf, 0x61, 0x07, 0xbb, 0x0c, 0xea, 0x7d, 0x7f,
          0xb1, 0xbd, 0x31, 0xf5, 0xe7, 0xb0, 0x6c, 0x93,
          0x89, 0xbe, 0xdb, 0x4c, 0xcd, 0xd8, 0x64, 0xea,
          0x11, 0xba, 0x4c, 0xbe, 0x84, 0x9b, 0x5e, 0x2b,
          0x55, 0x5f, 0xc3, 0xf3, 0x4b, 0xdd, 0x2d, 0x54,
          0xc6, 0x2d, 0x9e, 0x3b, 0xf3, 0x38, 0xc1, 0xc4 }
    };
    static const uint8_t cam_ofb_ct[3][48] = {
        { 0x14, 0xf7, 0x64, 0x61, 0x87, 0x81, 0x7e, 0xb5,
          0x86, 0x59, 0x91, 0x46, 0xb8, 0x2b, 0xd7, 0x19,
          0x97, 0x32, 0x91, 0x71, 0x6c, 0x4d, 0x82, 0xd0,
          0x1a, 0x07, 0x9e, 0x6d, 0xf7, 0x00, 0xe6, 0xeb,
          0x0e, 0xf0, 0x60, 0x3e, 0x2e, 0xe5, 0x34, 0xc1,
          0x74, 0xf4, 0x4a, 0x86, 0x78, 0xa0, 0x1f, 0x5b },
        { 0xc8, 0x32, 0xbb, 0x97, 0x80, 0x67, 0x7d, 0xaa,
          0x82, 0xd9, 0xb6, 0x86, 0x0d, 0xcd, 0x56, 0x5e,
          0x3c, 0x2d, 0x4d, 0xd9, 0x17, 0x84, 0x4e, 0x91,
          0x9c, 0x63, 0x7a, 0x3a, 0xb9, 0x38, 0xb4, 0x51,
          0xe5, 0xa9, 0xfb, 0x94, 0x8b, 0xc3, 0xa7, 0x57,
          0x05, 0x7d, 0x3d, 0x8e, 0xf3, 0x04, 0xef, 0x43 },
        { 0xcf, 0x61, 0x07, 0xbb, 0x0c, 0xea, 0x7d, 0x7f,
          0xb1, 0xbd, 0x31, 0xf5, 0xe7, 0xb0, 0x6c, 0x93,
          0x85, 0x52, 0x1d, 0xb2, 0xf6, 0xbb, 0x67, 0x7f,
          0x1e, 0xb2, 0x24, 0x46, 0x58, 0x41, 0x83, 0x40,
          0x23, 0x27, 0x26, 0x85, 0xae, 0x60, 0x49, 0xc7,
          0x88, 0x11, 0x4b, 0x3c, 0x21, 0xca, 0x20, 0x5c }
    };
    size_t key_len;
    size_t index;
    bool ok = true;
    char name[96];

    for (key_len = 0; key_len < 3; ++key_len) {
        size_t bits = 16 + key_len * 8;
        snprintf(name, sizeof(name), "PSA ARIA-ECB (%-3u) uses XCryptographic", (unsigned)bits);
        ok = xssl_test_psa_block_roundtrip(
                 PSA_KEY_TYPE_ARIA, bits, PSA_ALG_ECB_NO_PADDING,
                 aria_ecb_key, NULL, 0, aria_ecb_pt, 16, aria_ecb_ct[key_len], 16, name) && ok;
        snprintf(name, sizeof(name), "PSA ARIA-CBC (%-3u) uses XCryptographic", (unsigned)bits);
        ok = xssl_test_psa_block_roundtrip(
                 PSA_KEY_TYPE_ARIA, bits, PSA_ALG_CBC_NO_PADDING,
                 aria_mode_key, aria_mode_iv, sizeof(aria_mode_iv),
                 aria_mode_pt, sizeof(aria_mode_pt), aria_cbc_ct[key_len], 16, name) && ok;
        snprintf(name, sizeof(name), "PSA ARIA-CFB (%-3u) uses XCryptographic", (unsigned)bits);
        ok = xssl_test_psa_block_roundtrip(
                 PSA_KEY_TYPE_ARIA, bits, PSA_ALG_CFB,
                 aria_mode_key, aria_mode_iv, sizeof(aria_mode_iv),
                 aria_mode_pt, sizeof(aria_mode_pt), aria_cfb_ct[key_len], 17, name) && ok;
        snprintf(name, sizeof(name), "PSA ARIA-OFB (%-3u) uses XCryptographic", (unsigned)bits);
        ok = xssl_test_psa_block_roundtrip(
                 PSA_KEY_TYPE_ARIA, bits, PSA_ALG_OFB,
                 aria_mode_key, aria_mode_iv, sizeof(aria_mode_iv),
                 aria_mode_pt, sizeof(aria_mode_pt), aria_ofb_ct[key_len], 17, name) && ok;

        snprintf(name, sizeof(name), "PSA Camellia-ECB (%-3u) uses XCryptographic", (unsigned)bits);
        ok = xssl_test_psa_block_roundtrip(
                 PSA_KEY_TYPE_CAMELLIA, bits, PSA_ALG_ECB_NO_PADDING,
                 cam_ecb_key[key_len], NULL, 0, cam_ecb_pt, 16, cam_ecb_ct[key_len], 16, name) && ok;
    }

    for (key_len = 0; key_len < 3; ++key_len) {
        size_t bits = 16 + key_len * 8;
        uint8_t cam_pt[48];
        uint8_t cam_cbc_out[48];
        uint8_t cam_cfb_out[48];
        uint8_t cam_ofb_out[48];
        memcpy(cam_pt, cam_cbc_pt[0], 16);
        memcpy(cam_pt + 16, cam_cbc_pt[1], 16);
        memcpy(cam_pt + 32, cam_cbc_pt[2], 16);
        memcpy(cam_cbc_out, cam_cbc_ct[key_len][0], 16);
        memcpy(cam_cbc_out + 16, cam_cbc_ct[key_len][1], 16);
        memcpy(cam_cbc_out + 32, cam_cbc_ct[key_len][2], 16);
        memcpy(cam_cfb_out, cam_cfb_ct[key_len], 48);
        memcpy(cam_ofb_out, cam_ofb_ct[key_len], 48);
        snprintf(name, sizeof(name), "PSA Camellia-CBC (%-3u) uses XCryptographic", (unsigned)bits);
        ok = xssl_test_psa_block_roundtrip(
                 PSA_KEY_TYPE_CAMELLIA, bits, PSA_ALG_CBC_NO_PADDING,
                 cam_cbc_key[key_len], cam_cbc_iv, sizeof(cam_cbc_iv),
                 cam_pt, sizeof(cam_pt), cam_cbc_out, 16, name) && ok;
        snprintf(name, sizeof(name), "PSA Camellia-CFB (%-3u) uses XCryptographic", (unsigned)bits);
        ok = xssl_test_psa_block_roundtrip(
                 PSA_KEY_TYPE_CAMELLIA, bits, PSA_ALG_CFB,
                 cam_cbc_key[key_len], cam_cbc_iv, sizeof(cam_cbc_iv),
                 cam_pt, sizeof(cam_pt), cam_cfb_out, 17, name) && ok;
        snprintf(name, sizeof(name), "PSA Camellia-OFB (%-3u) uses XCryptographic", (unsigned)bits);
        ok = xssl_test_psa_block_roundtrip(
                 PSA_KEY_TYPE_CAMELLIA, bits, PSA_ALG_OFB,
                 cam_cbc_key[key_len], cam_cbc_iv, sizeof(cam_cbc_iv),
                 cam_pt, sizeof(cam_pt), cam_ofb_out, 17, name) && ok;
    }
    XSSL_TEST_PASS("PSA ARIA/Camellia block ciphers use XCryptographic");
    return ok;
}

static bool xssl_test_legacy_md_backend(void)
{
    const mbedtls_md_info_t *info;
    mbedtls_md_context_t digest;
    mbedtls_md_context_t clone;
    unsigned char out[64];
    unsigned char out2[64];
    static const unsigned char msg[] = "abc";
    static const unsigned char sha256_expected[] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
        0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    static const unsigned char hmac_key[] = {
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
        0x0b,0x0b,0x0b,0x0b
    };
    static const unsigned char hmac_msg[] = "Hi There";
    static const unsigned char hmac_expected[] = {
        0xb0,0x34,0x4c,0x61,0xd8,0xdb,0x38,0x53,
        0x5c,0xa8,0xaf,0xce,0xaf,0x0b,0xf1,0x2b,
        0x88,0x1d,0xc2,0x00,0xc9,0x83,0x3d,0xa7,
        0x26,0xe9,0x37,0x6c,0x2e,0x32,0xcf,0xf7
    };
    int ret;

    mbedtls_md_init(&digest);
    mbedtls_md_init(&clone);

    info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    XSSL_TEST_REQUIRE(info != NULL, "legacy MD", "SHA-256 info missing");

    ret = mbedtls_md_setup(&digest, info, 0);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD setup", "setup failed");
    ret = mbedtls_md_starts(&digest);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD starts", "starts failed");
    ret = mbedtls_md_update(&digest, msg, 1);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD update", "update 1 failed");
    ret = mbedtls_md_update(&digest, msg + 1, 2);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD update", "update 2 failed");
    ret = mbedtls_md_finish(&digest, out);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD finish", "finish failed");
    XSSL_TEST_REQUIRE(memcmp(out, sha256_expected, sizeof(sha256_expected)) == 0,
                      "legacy MD SHA-256 abc", "digest mismatch");

    ret = mbedtls_md(info, msg, sizeof(msg) - 1, out2);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD one-shot", "one-shot failed");
    XSSL_TEST_REQUIRE(memcmp(out2, sha256_expected, sizeof(sha256_expected)) == 0,
                      "legacy MD one-shot SHA-256 abc", "digest mismatch");

    ret = mbedtls_md_setup(&clone, info, 0);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD clone setup", "clone setup failed");
    ret = mbedtls_md_starts(&digest);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD reset", "reset failed");
    ret = mbedtls_md_update(&digest, msg, sizeof(msg) - 1);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD clone update", "update failed");
    ret = mbedtls_md_clone(&clone, &digest);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD clone", "clone failed");
    ret = mbedtls_md_finish(&digest, out);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD finish 2", "finish failed");
    ret = mbedtls_md_finish(&clone, out2);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD clone finish", "clone finish failed");
    XSSL_TEST_REQUIRE(memcmp(out, out2, sizeof(sha256_expected)) == 0,
                      "legacy MD clone result", "clone mismatch");

    ret = mbedtls_md_setup(&digest, info, 0);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD HMAC setup", "setup failed");
    ret = mbedtls_md_hmac_setup(&digest, info);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD hmac_setup", "hmac_setup failed");
    ret = mbedtls_md_hmac_starts(&digest, hmac_key, sizeof(hmac_key));
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD hmac_starts", "hmac_starts failed");
    ret = mbedtls_md_hmac_update(&digest, hmac_msg, sizeof(hmac_msg) - 1);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD hmac_update", "hmac_update failed");
    ret = mbedtls_md_hmac_finish(&digest, out);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD hmac_finish", "hmac_finish failed");
    XSSL_TEST_REQUIRE(memcmp(out, hmac_expected, sizeof(hmac_expected)) == 0,
                      "legacy MD HMAC RFC 4231", "hmac mismatch");
    ret = mbedtls_md_hmac_reset(&digest);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD hmac_reset", "hmac_reset failed");
    ret = mbedtls_md_hmac_update(&digest, hmac_msg, sizeof(hmac_msg) - 1);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD hmac_reset update", "hmac_reset update failed");
    ret = mbedtls_md_hmac_finish(&digest, out2);
    XSSL_TEST_REQUIRE(ret == 0, "legacy MD hmac_reset finish", "hmac_reset finish failed");
    XSSL_TEST_REQUIRE(memcmp(out, out2, sizeof(hmac_expected)) == 0,
                      "legacy MD HMAC reset reuse", "hmac reset mismatch");

    mbedtls_md_free(&digest);
    mbedtls_md_free(&clone);
    XSSL_TEST_PASS("legacy MD/HMAC uses XCryptographic");
    return true;
}

bool XSslTest_runAll(void)
{
    bool ok = xssl_test_legacy_md_backend();
    ok = xssl_test_psa_aead_backend() && ok;
    ok = xssl_test_psa_aead_multipart() && ok;
    ok = xssl_test_psa_ctr_backend() && ok;
    ok = xssl_test_psa_ecb_cbc_backend() && ok;
    ok = xssl_test_psa_xts_backend() && ok;
    ok = xssl_test_hmac_drbg_backend() && ok;
    ok = xssl_test_ctr_drbg_backend() && ok;
    ok = xssl_test_lms_backend() && ok;
    ok = xssl_test_psa_cbc_pkcs7_backend() && ok;
    ok = xssl_test_psa_cfb_ofb_backend() && ok;
    ok = xssl_test_psa_key_wrap_backend() && ok;
    ok = xssl_test_psa_aria_camellia_backend() && ok;
    ok = xssl_test_psa_chacha20_backend() && ok;
    ok = xssl_test_psa_chacha20_edges() && ok;
    ok = xssl_test_psa_ccm_star_no_tag_backend() && ok;
    ok = xssl_test_psa_p256_backend() && ok;
    ok = xssl_test_psa_rsa_backend() && ok;
    ok = xssl_test_psa_x25519_backend() && ok;
    ok = xssl_test_psa_x448_backend() && ok;
    ok = xssl_test_psa_secp256k1_backend() && ok;
    ok = xssl_test_psa_p384_backend() && ok;
    ok = xssl_test_psa_p521_backend() && ok;
    ok = xssl_test_psa_brainpool_p256r1_backend() && ok;
    ok = xssl_test_psa_brainpool_p384r1_backend() && ok;
    ok = xssl_test_psa_brainpool_p512r1_backend() && ok;
    ok = xssl_test_psa_ffdh_backend() && ok;
    ok = xssl_test_psa_ecjpake_backend() && ok;
    ok = xssl_test_psa_tls12_ecjpake_pms_backend() && ok;
    ok = xssl_test_psa_kdf_backend() && ok;
    ok = xssl_test_psa_kdf_new_backend() && ok;
    ok = xssl_test_psa_xof_backend() && ok;
    ok = xssl_test_psa_hash_backend() && ok;
    ok = xssl_test_psa_mac_backend() && ok;
    ok = xssl_test_session_tls12() && ok;
    ok = xssl_test_socket_configuration() && ok;
    ok = xssl_test_socket_tls12_loopback() && ok;
    XPrintf("[RESULT] XSsl tests: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static void xssl_test_run_all_wrapper(XVariant* data)
{
    (void)data;
    XSslTest_runAll();
}

void XTestMenu_XSslTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XSsl(离线回归)");
    XAction* action;
    if (!menu) return;
    XTestMenu_addMenu(root, menu);
    action = XTestMenu_addAction(menu, "XSsl/XSslSocket 全部测试");
    if (action) XTestMenu_setActionFunction(action, xssl_test_run_all_wrapper);
}

#endif /* DEMOTEST */
