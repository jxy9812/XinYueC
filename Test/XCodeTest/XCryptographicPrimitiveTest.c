/**
 * @file XCryptographicPrimitiveTest.c
 * @brief XCryptographic 独立密码原语标准向量测试。
 */

#include "XCryptographicPrimitiveTest.h"

#if DEMOTEST

#include "XAction.h"
#include "XByteArray.h"
#include "XCryptographic.h"
#include "XMenu.h"
#include "XPrintf.h"
#include <string.h>

#define XCRYPTO_PRIMITIVE_PASS(name) XPrintf("[PASS] XCryptographic: %s\n", name)
#define XCRYPTO_PRIMITIVE_FAIL(name, reason) \
    XPrintf("[FAIL] XCryptographic: %s: %s\n", name, reason)
#define XCRYPTO_PRIMITIVE_REQUIRE(condition, name, reason) \
    do { if (!(condition)) { XCRYPTO_PRIMITIVE_FAIL(name, reason); return false; } } while (0)
#define XCRYPTO_PRIMITIVE_SIZE(array) XByteArray_size_base((XContainer*)(array))
#define XCRYPTO_PRIMITIVE_DELETE(array) XByteArray_delete_base((XClass*)(array))

static XByteArrayView xcryptographic_test_view(const uint8_t* data, size_t size)
{
    return XByteArrayView_create_data(data, (int64_t)size);
}

static bool XCryptographicPrimitive_test_aes_gcm(void)
{
    static const uint8_t keyBytes[16] = { 0 };
    static const uint8_t nonce[12] = { 0 };
    static const uint8_t plainText[16] = { 0 };
    static const uint8_t expected[] = {
        0x03, 0x88, 0xda, 0xce, 0x60, 0xb6, 0xa3, 0x92,
        0xf3, 0x28, 0xc2, 0xb9, 0x71, 0xb2, 0xfe, 0x78,
        0xab, 0x6e, 0x47, 0xd4, 0x2c, 0xec, 0x13, 0xbd,
        0xf5, 0x3a, 0x67, 0xb2, 0x12, 0x57, 0xbd, 0xdf
    };
    XCryptographic_Key key;
    XByteArray* encrypted;
    XByteArray* decrypted;
    uint8_t unchanged[16];
    XByteArrayView result;
    XByteArrayView empty = xcryptographic_test_view(NULL, 0);

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_aeadImportKey(XCryptographic_AeadAlgorithm_AesGcm,
                                     xcryptographic_test_view(keyBytes, sizeof(keyBytes)), &key),
        "AES-GCM key import", "AES-128-GCM 密钥导入失败");
    encrypted = XCryptographic_aeadEncrypt(
        key, xcryptographic_test_view(nonce, sizeof(nonce)), empty,
        xcryptographic_test_view(plainText, sizeof(plainText)), 16);
    XCRYPTO_PRIMITIVE_REQUIRE(encrypted != NULL && XCRYPTO_PRIMITIVE_SIZE(encrypted) == sizeof(expected),
                              "AES-GCM encrypt", "加密失败或长度错误");
    XCRYPTO_PRIMITIVE_REQUIRE(memcmp(XByteArray_data(encrypted), expected, sizeof(expected)) == 0,
                              "AES-GCM NIST vector", "NIST SP 800-38D 向量不匹配");
    decrypted = XCryptographic_aeadDecrypt(
        key, xcryptographic_test_view(nonce, sizeof(nonce)), empty,
        XByteArrayView_create_bytearray(encrypted), 16);
    XCRYPTO_PRIMITIVE_REQUIRE(decrypted != NULL && XCRYPTO_PRIMITIVE_SIZE(decrypted) == sizeof(plainText) &&
                                  memcmp(XByteArray_data(decrypted), plainText, sizeof(plainText)) == 0,
                              "AES-GCM decrypt", "验签解密结果错误");

    ((uint8_t*)XByteArray_data(encrypted))[sizeof(expected) - 1] ^= 1u;
    memset(unchanged, 0xa5, sizeof(unchanged));
    result = XCryptographic_aeadDecryptInto(
        key, xcryptographic_test_view(nonce, sizeof(nonce)), empty,
        XByteArrayView_create_bytearray(encrypted), 16,
        (char*)unchanged, sizeof(unchanged));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data == NULL &&
                                  memcmp(unchanged, (const uint8_t[16]){
                                      0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5,
                                      0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5
                                  }, sizeof(unchanged)) == 0,
                              "AES-GCM authentication failure", "认证失败时写入了明文");
    XCryptographic_destroyKey(&key);
    XCRYPTO_PRIMITIVE_DELETE(decrypted);
    XCRYPTO_PRIMITIVE_DELETE(encrypted);
    XCRYPTO_PRIMITIVE_PASS("AES-128-GCM NIST vector/authentication failure");
    return true;
}

static bool XCryptographicPrimitive_test_aes_gcm_nonstandard_nonce(void)
{
    static const uint8_t keyBytes[16] = { 0 };
    static const uint8_t nonce[8] = { 0 };
    static const uint8_t plainText[16] = { 0 };
    static const uint8_t expected[] = {
        0x7d, 0x70, 0xbb, 0x45, 0xc2, 0x18, 0xc8, 0xc9,
        0x8b, 0x26, 0x35, 0xf5, 0xee, 0xf0, 0x9c, 0xe9,
        0xd4, 0xf5, 0xc8, 0x3d, 0x34, 0xf8, 0xd3, 0x68,
        0xd5, 0x75, 0xac, 0x88, 0x1d, 0x97, 0x88, 0x30
    };
    XCryptographic_Key key;
    XByteArray* encrypted;

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_aeadImportKey(XCryptographic_AeadAlgorithm_AesGcm,
                                     xcryptographic_test_view(keyBytes, sizeof(keyBytes)), &key),
        "AES-GCM nonstandard nonce key import", "AES-128-GCM 密钥导入失败");
    encrypted = XCryptographic_aeadEncrypt(
        key, xcryptographic_test_view(nonce, sizeof(nonce)),
        xcryptographic_test_view(NULL, 0),
        xcryptographic_test_view(plainText, sizeof(plainText)), 16);
    XCRYPTO_PRIMITIVE_REQUIRE(encrypted != NULL && XCRYPTO_PRIMITIVE_SIZE(encrypted) == sizeof(expected) &&
                                  memcmp(XByteArray_data(encrypted), expected, sizeof(expected)) == 0,
                              "AES-GCM nonstandard nonce", "非 12 字节 nonce 向量不匹配");
    XCryptographic_destroyKey(&key);
    XCRYPTO_PRIMITIVE_DELETE(encrypted);
    XCRYPTO_PRIMITIVE_PASS("AES-128-GCM nonstandard nonce vector");
    return true;
}

static bool XCryptographicPrimitive_test_aes_gcm_key_sizes(void)
{
    static const uint8_t nonce[12] = { 0 };
    static const uint8_t plainText[16] = { 0 };
    static const uint8_t key192[24] = { 0 };
    static const uint8_t expected192[] = {
        0x98, 0xe7, 0x24, 0x7c, 0x07, 0xf0, 0xfe, 0x41,
        0x1c, 0x26, 0x7e, 0x43, 0x84, 0xb0, 0xf6, 0x00,
        0x2f, 0xf5, 0x8d, 0x80, 0x03, 0x39, 0x27, 0xab,
        0x8e, 0xf4, 0xd4, 0x58, 0x75, 0x14, 0xf0, 0xfb
    };
    static const uint8_t key256[32] = { 0 };
    static const uint8_t expected256[] = {
        0xce, 0xa7, 0x40, 0x3d, 0x4d, 0x60, 0x6b, 0x6e,
        0x07, 0x4e, 0xc5, 0xd3, 0xba, 0xf3, 0x9d, 0x18,
        0xd0, 0xd1, 0xc8, 0xa7, 0x99, 0x99, 0x6b, 0xf0,
        0x26, 0x5b, 0x98, 0xb5, 0xd4, 0x8a, 0xb9, 0x19
    };
    XCryptographic_Key key;
    XByteArray* encrypted;

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_aeadImportKey(XCryptographic_AeadAlgorithm_AesGcm,
                                     xcryptographic_test_view(key192, sizeof(key192)), &key),
        "AES-192-GCM key import", "AES-192-GCM 密钥导入失败");
    encrypted = XCryptographic_aeadEncrypt(key, xcryptographic_test_view(nonce, sizeof(nonce)),
                                           xcryptographic_test_view(NULL, 0),
                                           xcryptographic_test_view(plainText, sizeof(plainText)), 16);
    XCRYPTO_PRIMITIVE_REQUIRE(encrypted != NULL && XCRYPTO_PRIMITIVE_SIZE(encrypted) == sizeof(expected192) &&
                                  memcmp(XByteArray_data(encrypted), expected192, sizeof(expected192)) == 0,
                              "AES-192-GCM vector", "AES-192-GCM 向量不匹配");
    XCRYPTO_PRIMITIVE_DELETE(encrypted);
    XCryptographic_destroyKey(&key);

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_aeadImportKey(XCryptographic_AeadAlgorithm_AesGcm,
                                     xcryptographic_test_view(key256, sizeof(key256)), &key),
        "AES-256-GCM key import", "AES-256-GCM 密钥导入失败");
    encrypted = XCryptographic_aeadEncrypt(key, xcryptographic_test_view(nonce, sizeof(nonce)),
                                           xcryptographic_test_view(NULL, 0),
                                           xcryptographic_test_view(plainText, sizeof(plainText)), 16);
    XCRYPTO_PRIMITIVE_REQUIRE(encrypted != NULL && XCRYPTO_PRIMITIVE_SIZE(encrypted) == sizeof(expected256) &&
                                  memcmp(XByteArray_data(encrypted), expected256, sizeof(expected256)) == 0,
                              "AES-256-GCM vector", "AES-256-GCM 向量不匹配");
    XCRYPTO_PRIMITIVE_DELETE(encrypted);
    XCryptographic_destroyKey(&key);
    XCRYPTO_PRIMITIVE_PASS("AES-192/256-GCM key-size vectors");
    return true;
}

static bool XCryptographicPrimitive_test_aria_camellia_aead(void)
{
    static const uint8_t key[16] = { 0 };
    static const uint8_t gcm_nonce[12] = { 0 };
    static const uint8_t gcm_plain_text[16] = { 0 };
    static const uint8_t ccm_nonce[13] = {
        0x00, 0x00, 0x00, 0x03, 0x02, 0x01, 0x00,
        0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5
    };
    static const uint8_t aad[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    static const uint8_t ccm_plain_text[23] = {
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e
    };
    static const uint8_t expected[4][39] = {
        { 0x52, 0xa9, 0xa4, 0xcc, 0x4f, 0xb1, 0xef, 0x00,
          0xa7, 0x2f, 0xf8, 0x75, 0x83, 0xd4, 0x4e, 0x5c,
          0x55, 0x8b, 0x5d, 0xc8, 0x13, 0x12, 0xa9, 0x93,
          0x5f, 0x28, 0x66, 0xf0, 0xbd, 0x77, 0xa6, 0x73 },
        { 0x82, 0x56, 0x2e, 0xf7, 0x11, 0x9b, 0x65, 0x7c,
          0x2f, 0x54, 0xb3, 0x5e, 0xda, 0x4d, 0xfc, 0x7e,
          0xa5, 0x92, 0xa6, 0x3a, 0x9b, 0x84, 0x3f,
          0x2c, 0xe8, 0xb5, 0xed, 0xf6, 0xc6, 0xdb, 0x59,
          0x1a, 0x62, 0x16, 0x11, 0x98, 0x4e, 0xc8, 0xe5 },
        { 0xde, 0xfe, 0x3e, 0x0b, 0x5c, 0x54, 0xc9, 0x4b,
          0x4f, 0x2a, 0x0f, 0x5a, 0x46, 0xf6, 0x21, 0x0d,
          0xf6, 0x72, 0xb9, 0x4d, 0x19, 0x22, 0x66, 0xc7,
          0xc8, 0xc8, 0xdb, 0xb4, 0x27, 0xcc, 0x98, 0x9a },
        { 0xac, 0xbf, 0x4e, 0xa7, 0xca, 0xd2, 0xc2, 0x91,
          0x64, 0xa6, 0x7b, 0xa8, 0xa0, 0x38, 0x15, 0x48,
          0x94, 0xd8, 0x42, 0x5d, 0xdc, 0x29, 0x9f,
          0x78, 0x1b, 0x61, 0x8b, 0x30, 0xdb, 0x77, 0x99,
          0x75, 0x55, 0x8f, 0xe1, 0x46, 0xc5, 0x4b, 0x04 }
    };
    static const XCryptographic_AeadAlgorithm algorithms[] = {
        XCryptographic_AeadAlgorithm_AriaGcm,
        XCryptographic_AeadAlgorithm_AriaCcm,
        XCryptographic_AeadAlgorithm_CamelliaGcm,
        XCryptographic_AeadAlgorithm_CamelliaCcm
    };
    XCryptographic_Key cryptographic_key;
    XByteArrayView encrypted;
    XByteArrayView decrypted;
    uint8_t encrypted_buffer[64];
    uint8_t decrypted_buffer[32];
    size_t index;

    for (index = 0; index < sizeof(algorithms) / sizeof(algorithms[0]); ++index) {
        const bool is_gcm = (index & 1u) == 0;
        const uint8_t* nonce = is_gcm ? gcm_nonce : ccm_nonce;
        size_t nonce_size = is_gcm ? sizeof(gcm_nonce) : sizeof(ccm_nonce);
        const uint8_t* plain_text = is_gcm ? gcm_plain_text : ccm_plain_text;
        size_t plain_text_size = is_gcm ? sizeof(gcm_plain_text) : sizeof(ccm_plain_text);
        const uint8_t* associated_data = is_gcm ? NULL : aad;
        size_t associated_data_size = is_gcm ? 0 : sizeof(aad);
        size_t expected_size = plain_text_size + 16u;

        XCRYPTO_PRIMITIVE_REQUIRE(
            XCryptographic_aeadImportKey(algorithms[index],
                                         xcryptographic_test_view(key, sizeof(key)),
                                         &cryptographic_key),
            "ARIA/Camellia AEAD key import", "认证加密密钥导入失败");
        encrypted = XCryptographic_aeadEncryptInto(
            cryptographic_key, xcryptographic_test_view(nonce, nonce_size),
            xcryptographic_test_view(associated_data, associated_data_size),
            xcryptographic_test_view(plain_text, plain_text_size), 16,
            (char*)encrypted_buffer, sizeof(encrypted_buffer));
        XCRYPTO_PRIMITIVE_REQUIRE(encrypted.m_data != NULL &&
                                      encrypted.m_size == (int64_t)expected_size &&
                                      memcmp(encrypted_buffer, expected[index], expected_size) == 0,
                                  "ARIA/Camellia AEAD reference vector",
                                  "GCM/CCM 参考向量不匹配");
        decrypted = XCryptographic_aeadDecryptInto(
            cryptographic_key, xcryptographic_test_view(nonce, nonce_size),
            xcryptographic_test_view(associated_data, associated_data_size),
            encrypted, 16, (char*)decrypted_buffer, sizeof(decrypted_buffer));
        XCRYPTO_PRIMITIVE_REQUIRE(decrypted.m_data != NULL &&
                                      decrypted.m_size == (int64_t)plain_text_size &&
                                      memcmp(decrypted_buffer, plain_text, plain_text_size) == 0,
                                  "ARIA/Camellia AEAD decrypt", "认证解密结果不一致");
        encrypted_buffer[expected_size - 1u] ^= 1u;
        memset(decrypted_buffer, 0xa5, sizeof(decrypted_buffer));
        decrypted = XCryptographic_aeadDecryptInto(
            cryptographic_key, xcryptographic_test_view(nonce, nonce_size),
            xcryptographic_test_view(associated_data, associated_data_size),
            xcryptographic_test_view(encrypted_buffer, expected_size), 16,
            (char*)decrypted_buffer, sizeof(decrypted_buffer));
        XCRYPTO_PRIMITIVE_REQUIRE(decrypted.m_data == NULL &&
                                      decrypted_buffer[0] == 0xa5,
                                  "ARIA/Camellia AEAD authentication failure",
                                  "认证失败时写入了明文");
        XCryptographic_destroyKey(&cryptographic_key);
    }
    XCRYPTO_PRIMITIVE_PASS("ARIA/Camellia GCM/CCM reference vectors");
    return true;
}

static bool XCryptographicPrimitive_test_ecjpake(void)
{
    static const uint8_t password[] = { 'p', 'a', 's', 's', 'w', 'o', 'r', 'd' };
    static const uint8_t wrong_password[] = { 'w', 'r', 'o', 'n', 'g' };
    XCryptographic_EcjPakeContext *client = NULL;
    XCryptographic_EcjPakeContext *server = NULL;
    XCryptographic_EcjPakeContext *wrong_server = NULL;
    XCryptographic_EcjPakeContext *wrong_client = NULL;
    uint8_t client_round_one[512], server_round_one[512];
    uint8_t client_round_two[512], server_round_two[512];
    uint8_t client_shared[65], server_shared[65], wrong_shared[65];
    uint8_t wrong_server_shared[65];
    uint8_t wrong_client_round_one[512], wrong_server_round_one[512];
    uint8_t wrong_client_round_two[512], wrong_server_round_two[512];
    XByteArrayView client_round_one_view, server_round_one_view;
    XByteArrayView client_round_two_view, server_round_two_view;
    XByteArrayView client_shared_view, server_shared_view;

    client = XCryptographic_ecjPakeCreate(
        XCryptographic_EcjPakeRole_Client,
        xcryptographic_test_view(password, sizeof(password)));
    server = XCryptographic_ecjPakeCreate(
        XCryptographic_EcjPakeRole_Server,
        xcryptographic_test_view(password, sizeof(password)));
    wrong_server = XCryptographic_ecjPakeCreate(
        XCryptographic_EcjPakeRole_Server,
        xcryptographic_test_view(wrong_password, sizeof(wrong_password)));
    wrong_client = XCryptographic_ecjPakeCreate(
        XCryptographic_EcjPakeRole_Client,
        xcryptographic_test_view(wrong_password, sizeof(wrong_password)));
    XCRYPTO_PRIMITIVE_REQUIRE(client && server && wrong_server && wrong_client,
                              "ECJPAKE create", "ECJPAKE 上下文创建失败");

    client_round_one_view = XCryptographic_ecjPakeWriteRoundOneInto(client,
                                                                    (char *)client_round_one,
                                                                    sizeof(client_round_one));
    server_round_one_view = XCryptographic_ecjPakeWriteRoundOneInto(server,
                                                                    (char *)server_round_one,
                                                                    sizeof(server_round_one));
    XCRYPTO_PRIMITIVE_REQUIRE(client_round_one_view.m_data && server_round_one_view.m_data,
                              "ECJPAKE round one write", "ECJPAKE 第一轮生成失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecjPakeReadRoundOne(server, client_round_one_view) &&
        XCryptographic_ecjPakeReadRoundOne(client, server_round_one_view),
        "ECJPAKE round one read", "ECJPAKE 第一轮验证失败");
    client_round_two_view = XCryptographic_ecjPakeWriteRoundTwoInto(client,
                                                                    (char *)client_round_two,
                                                                    sizeof(client_round_two));
    server_round_two_view = XCryptographic_ecjPakeWriteRoundTwoInto(server,
                                                                    (char *)server_round_two,
                                                                    sizeof(server_round_two));
    XCRYPTO_PRIMITIVE_REQUIRE(client_round_two_view.m_data && server_round_two_view.m_data,
                              "ECJPAKE round two write", "ECJPAKE 第二轮生成失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecjPakeReadRoundTwo(server, client_round_two_view) &&
        XCryptographic_ecjPakeReadRoundTwo(client, server_round_two_view),
        "ECJPAKE round two read", "ECJPAKE 第二轮验证失败");
    client_shared_view = XCryptographic_ecjPakeWriteSharedKeyInto(client,
                                                                  (char *)client_shared,
                                                                  sizeof(client_shared));
    server_shared_view = XCryptographic_ecjPakeWriteSharedKeyInto(server,
                                                                  (char *)server_shared,
                                                                  sizeof(server_shared));
    XCRYPTO_PRIMITIVE_REQUIRE(client_shared_view.m_data && server_shared_view.m_data &&
                                  client_shared_view.m_size == 65 &&
                                  server_shared_view.m_size == 65,
                              "ECJPAKE shared point", "ECJPAKE 共享点导出失败");
    XCRYPTO_PRIMITIVE_REQUIRE(memcmp(client_shared, server_shared, 65) == 0,
                              "ECJPAKE shared point match", "相同密码未得到相同共享点");

    /* 使用不同密码再完整执行一次交换。 */
    {
        XByteArrayView wrong_client_round_one_view =
            XCryptographic_ecjPakeWriteRoundOneInto(wrong_client,
                                                    (char *)wrong_client_round_one,
                                                    sizeof(wrong_client_round_one));
        XByteArrayView wrong_server_round_one_view =
            XCryptographic_ecjPakeWriteRoundOneInto(wrong_server,
                                                    (char *)wrong_server_round_one,
                                                    sizeof(wrong_server_round_one));
        XByteArrayView wrong_client_round_two_view;
        XByteArrayView wrong_server_round_two_view;
        XCRYPTO_PRIMITIVE_REQUIRE(wrong_client_round_one_view.m_data &&
                                      wrong_server_round_one_view.m_data &&
                                      XCryptographic_ecjPakeReadRoundOne(wrong_server,
                                                                         wrong_client_round_one_view) &&
                                      XCryptographic_ecjPakeReadRoundOne(wrong_client,
                                                                         wrong_server_round_one_view),
                                  "ECJPAKE wrong-password round one",
                                  "不同密码交换第一轮失败");
        wrong_client_round_two_view = XCryptographic_ecjPakeWriteRoundTwoInto(
            wrong_client, (char *)wrong_client_round_two, sizeof(wrong_client_round_two));
        wrong_server_round_two_view = XCryptographic_ecjPakeWriteRoundTwoInto(
            wrong_server, (char *)wrong_server_round_two, sizeof(wrong_server_round_two));
        XCRYPTO_PRIMITIVE_REQUIRE(wrong_client_round_two_view.m_data &&
                                      wrong_server_round_two_view.m_data &&
                                      XCryptographic_ecjPakeReadRoundTwo(wrong_server,
                                                                         wrong_client_round_two_view) &&
                                      XCryptographic_ecjPakeReadRoundTwo(wrong_client,
                                                                         wrong_server_round_two_view),
                                  "ECJPAKE wrong-password round two",
                                  "不同密码交换第二轮失败");
    }
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecjPakeWriteSharedKeyInto(wrong_client, (char *)wrong_shared,
                                                 sizeof(wrong_shared)).m_data &&
            XCryptographic_ecjPakeWriteSharedKeyInto(wrong_server, (char *)wrong_server_shared,
                                                     sizeof(wrong_server_shared)).m_data &&
            memcmp(wrong_shared, wrong_server_shared, 65) == 0 &&
            memcmp(client_shared, wrong_shared, 65) != 0,
        "ECJPAKE password separation", "不同密码得到相同共享点");

    XCryptographic_ecjPakeDestroy(client);
    XCryptographic_ecjPakeDestroy(server);
    XCryptographic_ecjPakeDestroy(wrong_server);
    XCryptographic_ecjPakeDestroy(wrong_client);
    XCRYPTO_PRIMITIVE_PASS("ECJPAKE secp256r1/SHA-256 two-round exchange");
    return true;
}

static bool XCryptographicPrimitive_test_hkdf_sha256(void)
{
    static const uint8_t inputKeyMaterial[22] = {
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
    static const uint8_t expected[42] = {
        0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a,
        0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
        0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c,
        0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf,
        0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18,
        0x58, 0x65
    };
    uint8_t output[sizeof(expected)];
    XByteArrayView result = XCryptographic_hkdfInto(
        xcryptographic_test_view(salt, sizeof(salt)),
        xcryptographic_test_view(inputKeyMaterial, sizeof(inputKeyMaterial)),
        xcryptographic_test_view(info, sizeof(info)), XCryptographicHash_Sha256, sizeof(output),
        (char*)output, sizeof(output));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == (int64_t)sizeof(expected) &&
                                  memcmp(output, expected, sizeof(expected)) == 0,
                              "HKDF-SHA-256 RFC 5869 vector", "RFC 5869 测试向量不匹配");
    XCRYPTO_PRIMITIVE_PASS("HKDF-SHA-256 RFC 5869 vector");
    return true;
}

static bool XCryptographicPrimitive_test_aes_ccm(void)
{
    static const uint8_t keyBytes[16] = {
        0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
        0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf
    };
    static const uint8_t nonce[13] = {
        0x00, 0x00, 0x00, 0x03, 0x02, 0x01, 0x00,
        0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5
    };
    static const uint8_t associatedData[8] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
    };
    static const uint8_t plainText[23] = {
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e
    };
    static const uint8_t expected[] = {
        0x58, 0x8c, 0x97, 0x9a, 0x61, 0xc6, 0x63, 0xd2,
        0xf0, 0x66, 0xd0, 0xc2, 0xc0, 0xf9, 0x89, 0x80,
        0x6d, 0x5f, 0x6b, 0x61, 0xda, 0xc3, 0x84,
        0x17, 0xe8, 0xd1, 0x2c, 0xfd, 0xf9, 0x26, 0xe0
    };
    XCryptographic_Key key;
    XByteArray* encrypted;
    XByteArray* decrypted;
    uint8_t unchanged[sizeof(plainText)];
    XByteArrayView result;
    size_t i;
    bool untouched = true;

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_aeadImportKey(XCryptographic_AeadAlgorithm_AesCcm,
                                     xcryptographic_test_view(keyBytes, sizeof(keyBytes)), &key),
        "AES-CCM key import", "AES-128-CCM 密钥导入失败");
    encrypted = XCryptographic_aeadEncrypt(
        key, xcryptographic_test_view(nonce, sizeof(nonce)),
        xcryptographic_test_view(associatedData, sizeof(associatedData)),
        xcryptographic_test_view(plainText, sizeof(plainText)), 8);
    XCRYPTO_PRIMITIVE_REQUIRE(encrypted != NULL && XCRYPTO_PRIMITIVE_SIZE(encrypted) == sizeof(expected) &&
                                  memcmp(XByteArray_data(encrypted), expected, sizeof(expected)) == 0,
                              "AES-CCM RFC 3610 vector", "RFC 3610 向量不匹配");
    decrypted = XCryptographic_aeadDecrypt(
        key, xcryptographic_test_view(nonce, sizeof(nonce)),
        xcryptographic_test_view(associatedData, sizeof(associatedData)),
        XByteArrayView_create_bytearray(encrypted), 8);
    XCRYPTO_PRIMITIVE_REQUIRE(decrypted != NULL && XCRYPTO_PRIMITIVE_SIZE(decrypted) == sizeof(plainText) &&
                                  memcmp(XByteArray_data(decrypted), plainText, sizeof(plainText)) == 0,
                              "AES-CCM decrypt", "验签解密结果错误");

    ((uint8_t*)XByteArray_data(encrypted))[sizeof(expected) - 1] ^= 1u;
    memset(unchanged, 0xa5, sizeof(unchanged));
    result = XCryptographic_aeadDecryptInto(
        key, xcryptographic_test_view(nonce, sizeof(nonce)),
        xcryptographic_test_view(associatedData, sizeof(associatedData)),
        XByteArrayView_create_bytearray(encrypted), 8,
        (char*)unchanged, sizeof(unchanged));
    for (i = 0; i < sizeof(unchanged); ++i) {
        if (unchanged[i] != 0xa5) untouched = false;
    }
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data == NULL && untouched,
                              "AES-CCM authentication failure", "认证失败时写入了明文");
    XCryptographic_destroyKey(&key);
    XCRYPTO_PRIMITIVE_DELETE(decrypted);
    XCRYPTO_PRIMITIVE_DELETE(encrypted);
    XCRYPTO_PRIMITIVE_PASS("AES-128-CCM RFC 3610 vector");
    return true;
}

static bool XCryptographicPrimitive_test_chacha20_poly1305(void)
{
    static const uint8_t keyBytes[32] = {
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f
    };
    static const uint8_t nonce[12] = {
        0x07, 0x00, 0x00, 0x00, 0x40, 0x41, 0x42, 0x43,
        0x44, 0x45, 0x46, 0x47
    };
    static const uint8_t associatedData[12] = {
        0x50, 0x51, 0x52, 0x53, 0xc0, 0xc1, 0xc2, 0xc3,
        0xc4, 0xc5, 0xc6, 0xc7
    };
    static const uint8_t plainText[] = {
        0x4c, 0x61, 0x64, 0x69, 0x65, 0x73, 0x20, 0x61,
        0x6e, 0x64, 0x20, 0x47, 0x65, 0x6e, 0x74, 0x6c,
        0x65, 0x6d, 0x65, 0x6e, 0x20, 0x6f, 0x66, 0x20,
        0x74, 0x68, 0x65, 0x20, 0x63, 0x6c, 0x61, 0x73,
        0x73, 0x20, 0x6f, 0x66, 0x20, 0x27, 0x39, 0x39,
        0x3a, 0x20, 0x49, 0x66, 0x20, 0x49, 0x20, 0x63,
        0x6f, 0x75, 0x6c, 0x64, 0x20, 0x6f, 0x66, 0x66,
        0x65, 0x72, 0x20, 0x79, 0x6f, 0x75, 0x20, 0x6f,
        0x6e, 0x6c, 0x79, 0x20, 0x6f, 0x6e, 0x65, 0x20,
        0x74, 0x69, 0x70, 0x20, 0x66, 0x6f, 0x72, 0x20,
        0x74, 0x68, 0x65, 0x20, 0x66, 0x75, 0x74, 0x75,
        0x72, 0x65, 0x2c, 0x20, 0x73, 0x75, 0x6e, 0x73,
        0x63, 0x72, 0x65, 0x65, 0x6e, 0x20, 0x77, 0x6f,
        0x75, 0x6c, 0x64, 0x20, 0x62, 0x65, 0x20, 0x69,
        0x74, 0x2e
    };
    static const uint8_t expected[] = {
        0xd3, 0x1a, 0x8d, 0x34, 0x64, 0x8e, 0x60, 0xdb,
        0x7b, 0x86, 0xaf, 0xbc, 0x53, 0xef, 0x7e, 0xc2,
        0xa4, 0xad, 0xed, 0x51, 0x29, 0x6e, 0x08, 0xfe,
        0xa9, 0xe2, 0xb5, 0xa7, 0x36, 0xee, 0x62, 0xd6,
        0x3d, 0xbe, 0xa4, 0x5e, 0x8c, 0xa9, 0x67, 0x12,
        0x82, 0xfa, 0xfb, 0x69, 0xda, 0x92, 0x72, 0x8b,
        0x1a, 0x71, 0xde, 0x0a, 0x9e, 0x06, 0x0b, 0x29,
        0x05, 0xd6, 0xa5, 0xb6, 0x7e, 0xcd, 0x3b, 0x36,
        0x92, 0xdd, 0xbd, 0x7f, 0x2d, 0x77, 0x8b, 0x8c,
        0x98, 0x03, 0xae, 0xe3, 0x28, 0x09, 0x1b, 0x58,
        0xfa, 0xb3, 0x24, 0xe4, 0xfa, 0xd6, 0x75, 0x94,
        0x55, 0x85, 0x80, 0x8b, 0x48, 0x31, 0xd7, 0xbc,
        0x3f, 0xf4, 0xde, 0xf0, 0x8e, 0x4b, 0x7a, 0x9d,
        0xe5, 0x76, 0xd2, 0x65, 0x86, 0xce, 0xc6, 0x4b,
        0x61, 0x16, 0x1a, 0xe1, 0x0b, 0x59, 0x4f, 0x09,
        0xe2, 0x6a, 0x7e, 0x90, 0x2e, 0xcb, 0xd0, 0x60,
        0x06, 0x91
    };
    XCryptographic_Key key;
    XByteArray* encrypted;
    XByteArray* decrypted;
    uint8_t unchanged[sizeof(plainText)];
    XByteArrayView result;
    size_t i;
    bool untouched = true;
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_aeadImportKey(XCryptographic_AeadAlgorithm_ChaCha20Poly1305,
                                     xcryptographic_test_view(keyBytes, sizeof(keyBytes)), &key),
        "ChaCha20-Poly1305 key import", "ChaCha20-Poly1305 密钥导入失败");
    encrypted = XCryptographic_aeadEncrypt(key, xcryptographic_test_view(nonce, sizeof(nonce)),
                                           xcryptographic_test_view(associatedData, sizeof(associatedData)),
                                           xcryptographic_test_view(plainText, sizeof(plainText)), 16);
    XCRYPTO_PRIMITIVE_REQUIRE(encrypted != NULL && XCRYPTO_PRIMITIVE_SIZE(encrypted) == sizeof(expected) &&
                                  memcmp(XByteArray_data(encrypted), expected, sizeof(expected)) == 0,
                              "ChaCha20-Poly1305 RFC 8439 vector", "RFC 8439 向量不匹配");
    decrypted = XCryptographic_aeadDecrypt(key, xcryptographic_test_view(nonce, sizeof(nonce)),
                                           xcryptographic_test_view(associatedData, sizeof(associatedData)),
                                           XByteArrayView_create_bytearray(encrypted), 16);
    XCRYPTO_PRIMITIVE_REQUIRE(decrypted != NULL && XCRYPTO_PRIMITIVE_SIZE(decrypted) == sizeof(plainText) &&
                                  memcmp(XByteArray_data(decrypted), plainText, sizeof(plainText)) == 0,
                              "ChaCha20-Poly1305 decrypt", "ChaCha20-Poly1305 解密错误");
    ((uint8_t*)XByteArray_data(encrypted))[sizeof(expected) - 1] ^= 1u;
    memset(unchanged, 0xa5, sizeof(unchanged));
    result = XCryptographic_aeadDecryptInto(
        key, xcryptographic_test_view(nonce, sizeof(nonce)),
        xcryptographic_test_view(associatedData, sizeof(associatedData)),
        XByteArrayView_create_bytearray(encrypted), 16,
        (char*)unchanged, sizeof(unchanged));
    for (i = 0; i < sizeof(unchanged); ++i) {
        if (unchanged[i] != 0xa5) untouched = false;
    }
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data == NULL && untouched,
                              "ChaCha20-Poly1305 authentication failure", "认证失败时写入了明文");
    XCryptographic_destroyKey(&key);
    XCRYPTO_PRIMITIVE_DELETE(decrypted);
    XCRYPTO_PRIMITIVE_DELETE(encrypted);
    XCRYPTO_PRIMITIVE_PASS("ChaCha20-Poly1305 RFC 8439 vector");
    return true;
}

static bool XCryptographicPrimitive_test_ripemd160(void)
{
    static const uint8_t expected[20] = {
        0x8e, 0xb2, 0x08, 0xf7, 0xe0, 0x5d, 0x98, 0x7a, 0x9b, 0x04,
        0x4a, 0x8e, 0x98, 0xc6, 0xb0, 0x87, 0xf1, 0x5a, 0x0b, 0xfc
    };
    uint8_t output[sizeof(expected)];
    XByteArrayView result = XCryptographicHash_hashInto(
        (char*)output, sizeof(output), "abc", 3, XCryptographicHash_Ripemd160);
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == (int64_t)sizeof(expected) &&
                                  memcmp(output, expected, sizeof(expected)) == 0,
                              "RIPEMD-160 abc vector", "RIPEMD-160 测试向量不匹配");
    XCRYPTO_PRIMITIVE_PASS("RIPEMD-160 abc vector");
    return true;
}

static bool XCryptographicPrimitive_test_aes_cmac(void)
{
    static const uint8_t keyBytes[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const uint8_t message[16] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
    };
    static const uint8_t expected[16] = {
        0x07, 0x0a, 0x16, 0xb4, 0x6b, 0x4d, 0x41, 0x44,
        0xf7, 0x9b, 0xdd, 0x9d, 0xd0, 0x4a, 0x28, 0x7c
    };
    uint8_t output[sizeof(expected)];
    XByteArrayView result = XCryptographic_aesCmacInto(
        xcryptographic_test_view(keyBytes, sizeof(keyBytes)),
        xcryptographic_test_view(message, sizeof(message)),
        (char*)output, sizeof(output));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == (int64_t)sizeof(expected) &&
                                  memcmp(output, expected, sizeof(expected)) == 0,
                              "AES-CMAC RFC 4493 vector", "AES-CMAC 测试向量不匹配");
    XCRYPTO_PRIMITIVE_PASS("AES-CMAC RFC 4493 vector");
    return true;
}

static bool XCryptographicPrimitive_test_aes_ecb_cbc(void)
{
    static const uint8_t keyBytes[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const uint8_t iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t plainText[64] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
    };
    static const uint8_t ecbExpected[64] = {
        0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
        0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97,
        0xf5, 0xd3, 0xd5, 0x85, 0x03, 0xb9, 0x69, 0x9d,
        0xe7, 0x85, 0x89, 0x5a, 0x96, 0xfd, 0xba, 0xaf,
        0x43, 0xb1, 0xcd, 0x7f, 0x59, 0x8e, 0xce, 0x23,
        0x88, 0x1b, 0x00, 0xe3, 0xed, 0x03, 0x06, 0x88,
        0x7b, 0x0c, 0x78, 0x5e, 0x27, 0xe8, 0xad, 0x3f,
        0x82, 0x23, 0x20, 0x71, 0x04, 0x72, 0x5d, 0xd4
    };
    static const uint8_t cbcExpected[64] = {
        0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
        0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
        0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee,
        0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2,
        0x73, 0xbe, 0xd6, 0xb8, 0xe3, 0xc1, 0x74, 0x3b,
        0x71, 0x16, 0xe6, 0x9e, 0x22, 0x22, 0x95, 0x16,
        0x3f, 0xf1, 0xca, 0xa1, 0x68, 0x1f, 0xac, 0x09,
        0x12, 0x0e, 0xca, 0x30, 0x75, 0x86, 0xe1, 0xa7
    };
    XCryptographic_BlockCipherOperation operation;
    uint8_t output[sizeof(plainText)];
    XByteArrayView result;

    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes, xcryptographic_test_view(keyBytes, sizeof(keyBytes)),
                                  XCryptographic_BlockCipherMode_EcbNoPadding, true),
                              "AES-ECB setup", "AES-ECB 初始化失败");
    result = XCryptographic_blockCipherUpdateInto(
        &operation, (char*)output, sizeof(output), xcryptographic_test_view(plainText, sizeof(plainText)));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == (int64_t)sizeof(output) &&
                              XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                              memcmp(output, ecbExpected, sizeof(output)) == 0,
                              "AES-ECB NIST vector", "ECB 加密向量不匹配");
    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes, xcryptographic_test_view(keyBytes, sizeof(keyBytes)),
                                  XCryptographic_BlockCipherMode_EcbNoPadding, false),
                              "AES-ECB decrypt setup", "AES-ECB 解密初始化失败");
    result = XCryptographic_blockCipherUpdateInto(
        &operation, (char*)output, sizeof(output), xcryptographic_test_view(ecbExpected, sizeof(ecbExpected)));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == (int64_t)sizeof(output) &&
                              XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                              memcmp(output, plainText, sizeof(output)) == 0,
                              "AES-ECB decrypt", "ECB 解密向量不匹配");
    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes, xcryptographic_test_view(keyBytes, sizeof(keyBytes)),
                                  XCryptographic_BlockCipherMode_CbcNoPadding, true) &&
                              XCryptographic_blockCipherSetIv(&operation, xcryptographic_test_view(iv, sizeof(iv))),
                              "AES-CBC setup", "AES-CBC 初始化失败");
    result = XCryptographic_blockCipherUpdateInto(
        &operation, (char*)output, sizeof(output), xcryptographic_test_view(plainText, 21));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16,
                              "AES-CBC streaming", "CBC 第一段输出长度错误");
    result = XCryptographic_blockCipherUpdateInto(
        &operation, (char*)output + 16, sizeof(output) - 16,
        xcryptographic_test_view(plainText + 21, sizeof(plainText) - 21));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 48 &&
                              XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                              memcmp(output, cbcExpected, sizeof(output)) == 0,
                              "AES-CBC NIST vector", "CBC 加密向量不匹配");
    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes, xcryptographic_test_view(keyBytes, sizeof(keyBytes)),
                                  XCryptographic_BlockCipherMode_CbcNoPadding, false) &&
                              XCryptographic_blockCipherSetIv(&operation, xcryptographic_test_view(iv, sizeof(iv))),
                              "AES-CBC decrypt setup", "AES-CBC 解密初始化失败");
    result = XCryptographic_blockCipherUpdateInto(
        &operation, (char*)output, sizeof(output), xcryptographic_test_view(cbcExpected, sizeof(cbcExpected)));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == (int64_t)sizeof(output) &&
                              XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                              memcmp(output, plainText, sizeof(output)) == 0,
                              "AES-CBC decrypt", "CBC 解密向量不匹配");
    XCRYPTO_PRIMITIVE_PASS("AES-ECB/CBC NIST SP 800-38A vectors");
    return true;
}

static bool XCryptographicPrimitive_test_aes_xts(void)
{
    static const uint8_t key32[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    static const uint8_t key32_p1619[32] = {
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22
    };
    static const uint8_t p1619_plain[32] = {
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44,
        0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x44
    };
    static const uint8_t p1619_tweak[16] = {
        0x33, 0x33, 0x33, 0x33, 0x33, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    static const uint8_t p1619_cipher[32] = {
        0xc4, 0x54, 0x18, 0x5e, 0x6a, 0x16, 0x93, 0x6e,
        0x39, 0x33, 0x40, 0x38, 0xac, 0xef, 0x83, 0x8b,
        0xfb, 0x18, 0x6f, 0xff, 0x74, 0x80, 0xad, 0xc4,
        0x28, 0x93, 0x82, 0xec, 0xd6, 0xd3, 0x94, 0xf0
    };
    static const uint8_t partial_plain[17] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10
    };
    static const uint8_t partial_cipher[17] = {
        0xac, 0x2a, 0xe6, 0x0d, 0x18, 0xa7, 0xeb, 0xfd,
        0x1f, 0x08, 0x4d, 0x9b, 0x59, 0x37, 0x25, 0xc1, 0x74
    };
    XCryptographic_BlockCipherOperation operation;
    uint8_t output[32];
    XByteArrayView result;

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes,
            xcryptographic_test_view(key32_p1619, sizeof(key32_p1619)),
            XCryptographic_BlockCipherMode_Xts, true) &&
        XCryptographic_blockCipherSetIv(&operation,
            xcryptographic_test_view(p1619_tweak, sizeof(p1619_tweak))),
        "AES-XTS IEEE P1619 setup", "AES-XTS 初始化失败");
    result = XCryptographic_blockCipherUpdateInto(&operation, (char*)output,
        sizeof(output), xcryptographic_test_view(p1619_plain, 11));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 0,
        "AES-XTS segmented update", "AES-XTS 分段更新输出长度错误");
    result = XCryptographic_blockCipherUpdateInto(&operation, (char*)output,
        sizeof(output), xcryptographic_test_view(p1619_plain + 11, 21));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16,
        "AES-XTS full-block update", "AES-XTS 整块更新输出长度错误");
    result = XCryptographic_blockCipherFinishInto(&operation, (char*)output + 16,
        sizeof(output) - 16);
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16 &&
        memcmp(output, p1619_cipher, sizeof(p1619_cipher)) == 0,
        "AES-XTS IEEE P1619 vector", "AES-XTS 整块向量不匹配");

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes,
            xcryptographic_test_view(key32, sizeof(key32)),
            XCryptographic_BlockCipherMode_Xts, true) &&
        XCryptographic_blockCipherSetIv(&operation,
            xcryptographic_test_view((const uint8_t[16]){ 0 }, 16)),
        "AES-XTS ciphertext-stealing setup", "AES-XTS 窃取模式初始化失败");
    result = XCryptographic_blockCipherUpdateInto(&operation, (char*)output,
        sizeof(output), xcryptographic_test_view(partial_plain, 7));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 0,
        "AES-XTS ciphertext-stealing split", "AES-XTS 窃取模式第一段输出错误");
    result = XCryptographic_blockCipherUpdateInto(&operation, (char*)output,
        sizeof(output), xcryptographic_test_view(partial_plain + 7, 10));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 0,
        "AES-XTS ciphertext-stealing tail", "AES-XTS 窃取模式尾段输出错误");
    result = XCryptographic_blockCipherFinishInto(&operation, (char*)output,
        sizeof(output));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 17 &&
        memcmp(output, partial_cipher, sizeof(partial_cipher)) == 0,
        "AES-XTS ciphertext stealing vector", "AES-XTS 窃取向量不匹配");

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes,
            xcryptographic_test_view(key32, sizeof(key32)),
            XCryptographic_BlockCipherMode_Xts, false) &&
        XCryptographic_blockCipherSetIv(&operation,
            xcryptographic_test_view((const uint8_t[16]){ 0 }, 16)),
        "AES-XTS decrypt setup", "AES-XTS 解密初始化失败");
    result = XCryptographic_blockCipherUpdateInto(&operation, (char*)output,
        sizeof(output), xcryptographic_test_view(partial_cipher, sizeof(partial_cipher)));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 0,
        "AES-XTS decrypt update", "AES-XTS 解密更新输出错误");
    result = XCryptographic_blockCipherFinishInto(&operation, (char*)output,
        sizeof(output));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 17 &&
        memcmp(output, partial_plain, sizeof(partial_plain)) == 0,
        "AES-XTS decrypt vector", "AES-XTS 窃取解密结果不匹配");
    XCRYPTO_PRIMITIVE_PASS("AES-XTS IEEE P1619 and ciphertext stealing vectors");
    return true;
}

static bool XCryptographicPrimitive_test_aes_key_wrap(void)
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
    static const uint8_t kwp_short_plain_text[7] = {
        0x46, 0x6f, 0x72, 0x50, 0x61, 0x73, 0x69
    };
    static const uint8_t kwp_short_expected[16] = {
        0xaf, 0xbe, 0xb0, 0xf0, 0x7d, 0xfb, 0xf5, 0x41,
        0x92, 0x00, 0xf2, 0xcc, 0xb5, 0x0b, 0xb2, 0x4f
    };
    uint8_t wrapped[32];
    uint8_t unwrapped[32];
    size_t output_size = 0;

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_aesKeyWrapInto(
            XCryptographic_KeyWrapMode_Kw,
            xcryptographic_test_view(kw_kek, sizeof(kw_kek)),
            xcryptographic_test_view(kw_plain_text, sizeof(kw_plain_text)),
            (char *) wrapped, sizeof(wrapped), &output_size) &&
        output_size == sizeof(kw_expected) &&
        memcmp(wrapped, kw_expected, sizeof(kw_expected)) == 0,
        "AES-KW RFC 3394 vector", "AES-KW 封装向量不匹配");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_aesKeyUnwrapInto(
            XCryptographic_KeyWrapMode_Kw,
            xcryptographic_test_view(kw_kek, sizeof(kw_kek)),
            xcryptographic_test_view(kw_expected, sizeof(kw_expected)),
            (char *) unwrapped, sizeof(unwrapped), &output_size) &&
        output_size == sizeof(kw_plain_text) &&
        memcmp(unwrapped, kw_plain_text, sizeof(kw_plain_text)) == 0,
        "AES-KW RFC 3394 unwrap", "AES-KW 解封结果不匹配");

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_aesKeyWrapInto(
            XCryptographic_KeyWrapMode_Kwp,
            xcryptographic_test_view(kwp_kek, sizeof(kwp_kek)),
            xcryptographic_test_view(kwp_plain_text, sizeof(kwp_plain_text)),
            (char *) wrapped, sizeof(wrapped), &output_size) &&
        output_size == sizeof(kwp_expected) &&
        memcmp(wrapped, kwp_expected, sizeof(kwp_expected)) == 0,
        "AES-KWP RFC 5649 vector", "AES-KWP 封装向量不匹配");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_aesKeyUnwrapInto(
            XCryptographic_KeyWrapMode_Kwp,
            xcryptographic_test_view(kwp_kek, sizeof(kwp_kek)),
            xcryptographic_test_view(kwp_expected, sizeof(kwp_expected)),
            (char *) unwrapped, sizeof(unwrapped), &output_size) &&
        output_size == sizeof(kwp_plain_text) &&
        memcmp(unwrapped, kwp_plain_text, sizeof(kwp_plain_text)) == 0,
        "AES-KWP RFC 5649 unwrap", "AES-KWP 解封结果不匹配");

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_aesKeyWrapInto(
            XCryptographic_KeyWrapMode_Kwp,
            xcryptographic_test_view(kwp_kek, sizeof(kwp_kek)),
            xcryptographic_test_view(kwp_short_plain_text, sizeof(kwp_short_plain_text)),
            (char *) wrapped, sizeof(wrapped), &output_size) &&
        output_size == sizeof(kwp_short_expected) &&
        memcmp(wrapped, kwp_short_expected, sizeof(kwp_short_expected)) == 0,
        "AES-KWP RFC 5649 single-block vector", "AES-KWP 单块封装向量不匹配");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_aesKeyUnwrapInto(
            XCryptographic_KeyWrapMode_Kwp,
            xcryptographic_test_view(kwp_kek, sizeof(kwp_kek)),
            xcryptographic_test_view(kwp_short_expected, sizeof(kwp_short_expected)),
            (char *) unwrapped, sizeof(unwrapped), &output_size) &&
        output_size == sizeof(kwp_short_plain_text) &&
        memcmp(unwrapped, kwp_short_plain_text, sizeof(kwp_short_plain_text)) == 0,
        "AES-KWP RFC 5649 single-block unwrap", "AES-KWP 单块解封结果不匹配");

    memcpy(wrapped, kwp_expected, sizeof(kwp_expected));
    wrapped[0] ^= 0x01;
    memset(unwrapped, 0xa5, sizeof(unwrapped));
    XCRYPTO_PRIMITIVE_REQUIRE(
        !XCryptographic_aesKeyUnwrapInto(
            XCryptographic_KeyWrapMode_Kwp,
            xcryptographic_test_view(kwp_kek, sizeof(kwp_kek)),
            xcryptographic_test_view(wrapped, sizeof(kwp_expected)),
            (char *) unwrapped, sizeof(unwrapped), &output_size) &&
        output_size == 0 && unwrapped[0] == 0,
        "AES-KWP integrity rejection", "篡改密文未被拒绝");
    XCRYPTO_PRIMITIVE_PASS("AES-KW RFC 3394 and AES-KWP RFC 5649 vectors");
    return true;
}

static bool XCryptographicPrimitive_test_aes_cfb_ofb(void)
{
    static const uint8_t keyBytes[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const uint8_t iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t plainText[64] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
    };
    static const uint8_t cfbExpected[64] = {
        0x3b, 0x3f, 0xd9, 0x2e, 0xb7, 0x2d, 0xad, 0x20,
        0x33, 0x34, 0x49, 0xf8, 0xe8, 0x3c, 0xfb, 0x4a,
        0xc8, 0xa6, 0x45, 0x37, 0xa0, 0xb3, 0xa9, 0x3f,
        0xcd, 0xe3, 0xcd, 0xad, 0x9f, 0x1c, 0xe5, 0x8b,
        0x26, 0x75, 0x1f, 0x67, 0xa3, 0xcb, 0xb1, 0x40,
        0xb1, 0x80, 0x8c, 0xf1, 0x87, 0xa4, 0xf4, 0xdf,
        0xc0, 0x4b, 0x05, 0x35, 0x7c, 0x5d, 0x1c, 0x0e,
        0xea, 0xc4, 0xc6, 0x6f, 0x9f, 0xf7, 0xf2, 0xe6
    };
    static const uint8_t ofbExpected[64] = {
        0x3b, 0x3f, 0xd9, 0x2e, 0xb7, 0x2d, 0xad, 0x20,
        0x33, 0x34, 0x49, 0xf8, 0xe8, 0x3c, 0xfb, 0x4a,
        0x77, 0x89, 0x50, 0x8d, 0x16, 0x91, 0x8f, 0x03,
        0xf5, 0x3c, 0x52, 0xda, 0xc5, 0x4e, 0xd8, 0x25,
        0x97, 0x40, 0x05, 0x1e, 0x9c, 0x5f, 0xec, 0xf6,
        0x43, 0x44, 0xf7, 0xa8, 0x22, 0x60, 0xed, 0xcc,
        0x30, 0x4c, 0x65, 0x28, 0xf6, 0x59, 0xc7, 0x78,
        0x66, 0xa5, 0x10, 0xd9, 0xc1, 0xd6, 0xae, 0x5e
    };
    XCryptographic_BlockCipherOperation operation;
    uint8_t output[sizeof(plainText)];
    XByteArrayView result;
    XCryptographic_BlockCipherMode modes[] = {
        XCryptographic_BlockCipherMode_Cfb, XCryptographic_BlockCipherMode_Ofb
    };
    const uint8_t* expected[] = { cfbExpected, ofbExpected };
    size_t index;

    for (index = 0; index < sizeof(modes) / sizeof(modes[0]); ++index) {
        XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes, xcryptographic_test_view(keyBytes, sizeof(keyBytes)),
                                      modes[index], true) &&
                                  XCryptographic_blockCipherSetIv(
                                      &operation, xcryptographic_test_view(iv, sizeof(iv))),
                                  "AES-CFB/OFB setup", "流密码模式初始化失败");
        result = XCryptographic_blockCipherUpdateInto(
            &operation, (char*)output, sizeof(output), xcryptographic_test_view(plainText, 23));
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 23,
                                  "AES-CFB/OFB streaming", "第一段输出长度错误");
        result = XCryptographic_blockCipherUpdateInto(
            &operation, (char*)output + 23, sizeof(output) - 23,
            xcryptographic_test_view(plainText + 23, sizeof(plainText) - 23));
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 41 &&
                                  XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                                  memcmp(output, expected[index], sizeof(output)) == 0,
                                  "AES-CFB/OFB NIST vector", "流密码模式加密向量不匹配");
        XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes, xcryptographic_test_view(keyBytes, sizeof(keyBytes)),
                                      modes[index], false) &&
                                  XCryptographic_blockCipherSetIv(
                                      &operation, xcryptographic_test_view(iv, sizeof(iv))),
                                  "AES-CFB/OFB decrypt setup", "流密码模式解密初始化失败");
        result = XCryptographic_blockCipherUpdateInto(
            &operation, (char*)output, sizeof(output),
            xcryptographic_test_view(expected[index], sizeof(output)));
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == (int64_t)sizeof(output) &&
                                  XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                                  memcmp(output, plainText, sizeof(output)) == 0,
                                  "AES-CFB/OFB decrypt", "流密码模式解密向量不匹配");
    }
    XCRYPTO_PRIMITIVE_PASS("AES-CFB/OFB NIST SP 800-38A vectors");
    return true;
}

static bool XCryptographicPrimitive_test_aes_cbc_pkcs7(void)
{
    static const uint8_t keyBytes[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const uint8_t iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t plainText21[21] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e
    };
    static const uint8_t expected21[32] = {
        0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
        0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
        0x40, 0x05, 0x39, 0x32, 0xeb, 0xc8, 0x0b, 0x58,
        0x11, 0x83, 0x69, 0x06, 0x25, 0x52, 0xa0, 0x1d
    };
    static const uint8_t plainText64[64] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
        0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
        0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
        0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17,
        0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10
    };
    static const uint8_t expected80[80] = {
        0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
        0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
        0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee,
        0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2,
        0x73, 0xbe, 0xd6, 0xb8, 0xe3, 0xc1, 0x74, 0x3b,
        0x71, 0x16, 0xe6, 0x9e, 0x22, 0x22, 0x95, 0x16,
        0x3f, 0xf1, 0xca, 0xa1, 0x68, 0x1f, 0xac, 0x09,
        0x12, 0x0e, 0xca, 0x30, 0x75, 0x86, 0xe1, 0xa7,
        0x8c, 0xb8, 0x28, 0x07, 0x23, 0x0e, 0x13, 0x21,
        0xd3, 0xfa, 0xe0, 0x0d, 0x18, 0xcc, 0x20, 0x12
    };
    XCryptographic_BlockCipherOperation operation;
    uint8_t output[sizeof(expected80)];
    XByteArrayView result;
    size_t index;

    /* 21 字节明文：填充 11 字节后输出 32 字节。 */
    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes, xcryptographic_test_view(keyBytes, sizeof(keyBytes)),
                                  XCryptographic_BlockCipherMode_CbcPkcs7, true) &&
                              XCryptographic_blockCipherSetIv(
                                  &operation, xcryptographic_test_view(iv, sizeof(iv))),
                              "AES-CBC-PKCS7 encrypt setup", "CBC-PKCS7 加密初始化失败");
    result = XCryptographic_blockCipherUpdateInto(
        &operation, (char*)output, sizeof(output), xcryptographic_test_view(plainText21, 7));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 0,
                              "AES-CBC-PKCS7 streaming", "第一段输出长度错误");
    result = XCryptographic_blockCipherUpdateInto(
        &operation, (char*)output, sizeof(output), xcryptographic_test_view(plainText21 + 7, 14));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16,
                              "AES-CBC-PKCS7 streaming", "第二段输出长度错误");
    result = XCryptographic_blockCipherFinishInto(&operation, (char*)output + 16, sizeof(output) - 16);
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16 &&
                              memcmp(output, expected21, sizeof(expected21)) == 0,
                              "AES-CBC-PKCS7 NIST vector", "CBC-PKCS7 加密向量不匹配");

    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes, xcryptographic_test_view(keyBytes, sizeof(keyBytes)),
                                  XCryptographic_BlockCipherMode_CbcPkcs7, false) &&
                              XCryptographic_blockCipherSetIv(
                                  &operation, xcryptographic_test_view(iv, sizeof(iv))),
                              "AES-CBC-PKCS7 decrypt setup", "CBC-PKCS7 解密初始化失败");
    result = XCryptographic_blockCipherUpdateInto(
        &operation, (char*)output, sizeof(output), xcryptographic_test_view(expected21, sizeof(expected21)));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16,
                              "AES-CBC-PKCS7 decrypt update", "CBC-PKCS7 解密更新失败");
    result = XCryptographic_blockCipherFinishInto(&operation, (char*)output + 16, sizeof(output) - 16);
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 5 &&
                              memcmp(output, plainText21, sizeof(plainText21)) == 0,
                              "AES-CBC-PKCS7 decrypt", "CBC-PKCS7 解密向量不匹配");

    /* 64 字节整块明文：PKCS7 仍补一整块，输出 80 字节。 */
    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes, xcryptographic_test_view(keyBytes, sizeof(keyBytes)),
                                  XCryptographic_BlockCipherMode_CbcPkcs7, true) &&
                              XCryptographic_blockCipherSetIv(
                                  &operation, xcryptographic_test_view(iv, sizeof(iv))),
                              "AES-CBC-PKCS7 full-block encrypt setup", "CBC-PKCS7 整块加密初始化失败");
    result = XCryptographic_blockCipherUpdateInto(
        &operation, (char*)output, sizeof(output), xcryptographic_test_view(plainText64, sizeof(plainText64)));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 64,
                              "AES-CBC-PKCS7 full-block update", "CBC-PKCS7 整块更新失败");
    result = XCryptographic_blockCipherFinishInto(&operation, (char*)output + 64, sizeof(output) - 64);
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16 &&
                              memcmp(output, expected80, sizeof(expected80)) == 0,
                              "AES-CBC-PKCS7 full-block vector", "CBC-PKCS7 整块加密向量不匹配");

    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes, xcryptographic_test_view(keyBytes, sizeof(keyBytes)),
                                  XCryptographic_BlockCipherMode_CbcPkcs7, false) &&
                              XCryptographic_blockCipherSetIv(
                                  &operation, xcryptographic_test_view(iv, sizeof(iv))),
                              "AES-CBC-PKCS7 full-block decrypt setup", "CBC-PKCS7 整块解密初始化失败");
    result = XCryptographic_blockCipherUpdateInto(
        &operation, (char*)output, sizeof(output), xcryptographic_test_view(expected80, sizeof(expected80)));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 64,
                              "AES-CBC-PKCS7 full-block decrypt update", "CBC-PKCS7 整块解密更新失败");
    result = XCryptographic_blockCipherFinishInto(&operation, (char*)output + 64, sizeof(output) - 64);
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 0 &&
                              memcmp(output, plainText64, sizeof(plainText64)) == 0,
                              "AES-CBC-PKCS7 full-block decrypt", "CBC-PKCS7 整块解密向量不匹配");

    /* 填充校验：篡改最后一个填充字节应被拒绝。 */
    {
        uint8_t bad[sizeof(expected21)];
        memcpy(bad, expected21, sizeof(bad));
        bad[31] ^= 1u;
        XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(&operation, XCryptographic_BlockCipherAlgorithm_Aes, xcryptographic_test_view(keyBytes, sizeof(keyBytes)),
                                      XCryptographic_BlockCipherMode_CbcPkcs7, false) &&
                                  XCryptographic_blockCipherSetIv(
                                      &operation, xcryptographic_test_view(iv, sizeof(iv))),
                                  "AES-CBC-PKCS7 invalid padding setup", "CBC-PKCS7 解密初始化失败");
        result = XCryptographic_blockCipherUpdateInto(
            &operation, (char*)output, sizeof(output), xcryptographic_test_view(bad, sizeof(bad)));
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16,
                                  "AES-CBC-PKCS7 invalid padding update", "CBC-PKCS7 解密更新失败");
        result = XCryptographic_blockCipherFinishInto(&operation, (char*)output, sizeof(output));
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data == NULL,
                                  "AES-CBC-PKCS7 invalid padding", "错误填充未被拒绝");
    }
    for (index = 0; index < sizeof(output); ++index) output[index] = 0;
    XCRYPTO_PRIMITIVE_PASS("AES-CBC-PKCS7 NIST SP 800-38A + PKCS7 vectors");
    return true;
}

static bool XCryptographicPrimitive_test_aria_block_cipher(void)
{
    static const uint8_t ecbKey[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    static const uint8_t ecbPt[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    static const uint8_t ecbCt[3][16] = {
        { 0xd7, 0x18, 0xfb, 0xd6, 0xab, 0x64, 0x4c, 0x73,
          0x9d, 0xa9, 0x5f, 0x3b, 0xe6, 0x45, 0x17, 0x78 },
        { 0x26, 0x44, 0x9c, 0x18, 0x05, 0xdb, 0xe7, 0xaa,
          0x25, 0xa4, 0x68, 0xce, 0x26, 0x3a, 0x9e, 0x79 },
        { 0xf9, 0x2b, 0xd7, 0xc7, 0x9f, 0xb7, 0x2e, 0x2f,
          0x2b, 0x8f, 0x80, 0xc1, 0x97, 0x2d, 0x24, 0xfc }
    };
    static const uint8_t modeKey[32] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff
    };
    static const uint8_t modeIv[16] = {
        0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78,
        0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0
    };
    static const uint8_t modePt[48] = {
        0x11, 0x11, 0x11, 0x11, 0xaa, 0xaa, 0xaa, 0xaa,
        0x11, 0x11, 0x11, 0x11, 0xbb, 0xbb, 0xbb, 0xbb,
        0x11, 0x11, 0x11, 0x11, 0xcc, 0xcc, 0xcc, 0xcc,
        0x11, 0x11, 0x11, 0x11, 0xdd, 0xdd, 0xdd, 0xdd,
        0x22, 0x22, 0x22, 0x22, 0xaa, 0xaa, 0xaa, 0xaa,
        0x22, 0x22, 0x22, 0x22, 0xbb, 0xbb, 0xbb, 0xbb
    };
    static const uint8_t cbcCt[3][48] = {
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
    static const uint8_t cfbCt[3][48] = {
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
    static const uint8_t ofbCt[3][48] = {
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

    XCryptographic_BlockCipherOperation operation;
    uint8_t output[48];
    XByteArrayView result;
    size_t keyLen;
    size_t index;

    for (keyLen = 0; keyLen < 3; ++keyLen) {
        size_t bits = 16 + keyLen * 8;
        XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(
                                      &operation,
                                      XCryptographic_BlockCipherAlgorithm_Aria,
                                      xcryptographic_test_view(ecbKey, bits),
                                      XCryptographic_BlockCipherMode_EcbNoPadding,
                                      true),
                                  "ARIA-ECB setup", "ARIA-ECB 初始化失败");
        result = XCryptographic_blockCipherUpdateInto(
            &operation, (char*)output, sizeof(output), xcryptographic_test_view(ecbPt, sizeof(ecbPt)));
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16 &&
                                  XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                                  memcmp(output, ecbCt[keyLen], 16) == 0,
                                  "ARIA-ECB RFC 5794 vector", "ARIA-ECB 加密向量不匹配");
        XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(
                                      &operation,
                                      XCryptographic_BlockCipherAlgorithm_Aria,
                                      xcryptographic_test_view(ecbKey, bits),
                                      XCryptographic_BlockCipherMode_EcbNoPadding,
                                      false),
                                  "ARIA-ECB decrypt setup", "ARIA-ECB 解密初始化失败");
        result = XCryptographic_blockCipherUpdateInto(
            &operation, (char*)output, sizeof(output), xcryptographic_test_view(ecbCt[keyLen], 16));
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16 &&
                                  XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                                  memcmp(output, ecbPt, sizeof(ecbPt)) == 0,
                                  "ARIA-ECB decrypt", "ARIA-ECB 解密向量不匹配");
    }

    for (keyLen = 0; keyLen < 3; ++keyLen) {
        size_t bits = 16 + keyLen * 8;
        XCryptographic_BlockCipherMode modes[] = {
            XCryptographic_BlockCipherMode_CbcNoPadding,
            XCryptographic_BlockCipherMode_Cfb,
            XCryptographic_BlockCipherMode_Ofb
        };
        const uint8_t* expected[] = { cbcCt[keyLen], cfbCt[keyLen], ofbCt[keyLen] };
        for (index = 0; index < 3; ++index) {
            size_t firstLen = (modes[index] == XCryptographic_BlockCipherMode_Cfb ||
                               modes[index] == XCryptographic_BlockCipherMode_Ofb) ? 17u : 16u;
            size_t secondLen = (modes[index] == XCryptographic_BlockCipherMode_Cfb ||
                               modes[index] == XCryptographic_BlockCipherMode_Ofb) ? 31u : 32u;
            XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(
                                          &operation,
                                          XCryptographic_BlockCipherAlgorithm_Aria,
                                          xcryptographic_test_view(modeKey, bits),
                                          modes[index], true) &&
                                      XCryptographic_blockCipherSetIv(
                                          &operation, xcryptographic_test_view(modeIv, sizeof(modeIv))),
                                      "ARIA-CBC/CFB setup", "ARIA 分组模式初始化失败");
            result = XCryptographic_blockCipherUpdateInto(
                &operation, (char*)output, sizeof(output), xcryptographic_test_view(modePt, 17));
            XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == (int64_t)firstLen,
                                      "ARIA-CBC/CFB streaming", "ARIA 第一段输出长度错误");
            result = XCryptographic_blockCipherUpdateInto(
                &operation, (char*)output + firstLen, sizeof(output) - firstLen,
                xcryptographic_test_view(modePt + 17, sizeof(modePt) - 17));
            XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == (int64_t)secondLen &&
                                      XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                                      memcmp(output, expected[index], sizeof(modePt)) == 0,
                                      "ARIA-CBC/CFB vector", "ARIA 分组模式向量不匹配");
            XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(
                                          &operation,
                                          XCryptographic_BlockCipherAlgorithm_Aria,
                                          xcryptographic_test_view(modeKey, bits),
                                          modes[index], false) &&
                                      XCryptographic_blockCipherSetIv(
                                          &operation, xcryptographic_test_view(modeIv, sizeof(modeIv))),
                                      "ARIA-CBC/CFB decrypt setup", "ARIA 解密初始化失败");
            result = XCryptographic_blockCipherUpdateInto(
                &operation, (char*)output, sizeof(output),
                xcryptographic_test_view(expected[index], sizeof(modePt)));
            XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == (int64_t)sizeof(modePt) &&
                                      XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                                      memcmp(output, modePt, sizeof(modePt)) == 0,
                                      "ARIA-CBC/CFB decrypt", "ARIA 分组模式解密向量不匹配");
        }
    }
    XCRYPTO_PRIMITIVE_PASS("ARIA ECB/CBC/CFB/OFB RFC 5794 + ARIA test vectors");
    return true;
}

static bool XCryptographicPrimitive_test_camellia_block_cipher(void)
{
    static const uint8_t ecbKey[3][32] = {
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
    static const uint8_t ecbZeroKey[3][32] = { { 0 }, { 0 }, { 0 } };
    static const uint8_t ecbPt[2][16] = {
        { 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
          0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10 },
        { 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }
    };
    static const uint8_t ecbCt[3][2][16] = {
        { { 0x67, 0x67, 0x31, 0x38, 0x54, 0x96, 0x69, 0x73,
            0x08, 0x57, 0x06, 0x56, 0x48, 0xea, 0xbe, 0x43 },
          { 0x38, 0x3c, 0x6c, 0x2a, 0xab, 0xef, 0x7f, 0xde,
            0x25, 0xcd, 0x47, 0x0b, 0xf7, 0x74, 0xa3, 0x31 } },
        { { 0xb4, 0x99, 0x34, 0x01, 0xb3, 0xe9, 0x96, 0xf8,
            0x4e, 0xe5, 0xce, 0xe7, 0xd7, 0x9b, 0x09, 0xb9 },
          { 0xd1, 0x76, 0x3f, 0xc0, 0x19, 0xd7, 0x7c, 0xc9,
            0x30, 0xbf, 0xf2, 0xa5, 0x6f, 0x7c, 0x93, 0x64 } },
        { { 0x9a, 0xcc, 0x23, 0x7d, 0xff, 0x16, 0xd7, 0x6c,
            0x20, 0xef, 0x7c, 0x91, 0x9e, 0x3a, 0x75, 0x09 },
          { 0x05, 0x03, 0xfb, 0x10, 0xab, 0x24, 0x1e, 0x7c,
            0xf4, 0x5d, 0x8c, 0xde, 0xee, 0x47, 0x43, 0x35 } }
    };
    static const uint8_t cbcKey[3][32] = {
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
    static const uint8_t cbcIv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t cbcPt[3][16] = {
        { 0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
          0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a },
        { 0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
          0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51 },
        { 0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11,
          0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef }
    };
    static const uint8_t cbcCt[3][3][16] = {
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
    static const uint8_t cfbCt[3][48] = {
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
    static const uint8_t ofbCt[3][48] = {
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
    XCryptographic_BlockCipherOperation operation;
    uint8_t output[48];
    XByteArrayView result;
    size_t keyLen;
    size_t i;
    size_t index_slot;

    for (keyLen = 0; keyLen < 3; ++keyLen) {
        size_t bits = 16 + keyLen * 8;
        for (i = 0; i < 2; ++i) {
            XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(
                                          &operation,
                                          XCryptographic_BlockCipherAlgorithm_Camellia,
                                          xcryptographic_test_view(i == 0 ? ecbKey[keyLen] : ecbZeroKey[keyLen], bits),
                                          XCryptographic_BlockCipherMode_EcbNoPadding,
                                          true),
                                      "Camellia-ECB setup", "Camellia-ECB 初始化失败");
            result = XCryptographic_blockCipherUpdateInto(
                &operation, (char*)output, sizeof(output), xcryptographic_test_view(ecbPt[i], 16));
            XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16 &&
                                      XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                                      memcmp(output, ecbCt[keyLen][i], 16) == 0,
                                      "Camellia-ECB vector", "Camellia-ECB 加密向量不匹配");
            XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(
                                          &operation,
                                          XCryptographic_BlockCipherAlgorithm_Camellia,
                                          xcryptographic_test_view(i == 0 ? ecbKey[keyLen] : ecbZeroKey[keyLen], bits),
                                          XCryptographic_BlockCipherMode_EcbNoPadding,
                                          false),
                                      "Camellia-ECB decrypt setup", "Camellia-ECB 解密初始化失败");
            result = XCryptographic_blockCipherUpdateInto(
                &operation, (char*)output, sizeof(output), xcryptographic_test_view(ecbCt[keyLen][i], 16));
            XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16 &&
                                      XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                                      memcmp(output, ecbPt[i], 16) == 0,
                                      "Camellia-ECB decrypt", "Camellia-ECB 解密向量不匹配");
        }
    }

    for (keyLen = 0; keyLen < 3; ++keyLen) {
        size_t bits = 16 + keyLen * 8;
        XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(
                                      &operation,
                                      XCryptographic_BlockCipherAlgorithm_Camellia,
                                      xcryptographic_test_view(cbcKey[keyLen], bits),
                                      XCryptographic_BlockCipherMode_CbcNoPadding,
                                      true) &&
                                  XCryptographic_blockCipherSetIv(
                                      &operation, xcryptographic_test_view(cbcIv, sizeof(cbcIv))),
                                  "Camellia-CBC setup", "Camellia-CBC 初始化失败");
        result = XCryptographic_blockCipherUpdateInto(
            &operation, (char*)output, sizeof(output),
            xcryptographic_test_view(cbcPt[0], sizeof(cbcPt[0])));
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16 &&
                                  memcmp(output, cbcCt[keyLen][0], 16) == 0,
                                  "Camellia-CBC block1", "Camellia-CBC 第 1 块不匹配");
        result = XCryptographic_blockCipherUpdateInto(
            &operation, (char*)output + 16, sizeof(output) - 16,
            xcryptographic_test_view(cbcPt[1], sizeof(cbcPt[1])));
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16 &&
                                  memcmp(output + 16, cbcCt[keyLen][1], 16) == 0,
                                  "Camellia-CBC block2", "Camellia-CBC 第 2 块不匹配");
        result = XCryptographic_blockCipherUpdateInto(
            &operation, (char*)output + 32, sizeof(output) - 32,
            xcryptographic_test_view(cbcPt[2], sizeof(cbcPt[2])));
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 16 &&
                                  XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                                  memcmp(output, cbcCt[keyLen], sizeof(cbcPt)) == 0,
                                  "Camellia-CBC vector", "Camellia-CBC 加密向量不匹配");
        XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(
                                      &operation,
                                      XCryptographic_BlockCipherAlgorithm_Camellia,
                                      xcryptographic_test_view(cbcKey[keyLen], bits),
                                      XCryptographic_BlockCipherMode_CbcNoPadding,
                                      false) &&
                                  XCryptographic_blockCipherSetIv(
                                      &operation, xcryptographic_test_view(cbcIv, sizeof(cbcIv))),
                                  "Camellia-CBC decrypt setup", "Camellia-CBC 解密初始化失败");
        result = XCryptographic_blockCipherUpdateInto(
            &operation, (char*)output, sizeof(output),
            xcryptographic_test_view(cbcCt[keyLen][0], sizeof(cbcPt)));
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == (int64_t)sizeof(cbcPt) &&
                                  XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                                  memcmp(output, cbcPt, sizeof(cbcPt)) == 0,
                                  "Camellia-CBC decrypt", "Camellia-CBC 解密向量不匹配");
    }
    for (keyLen = 0; keyLen < 3; ++keyLen) {
        size_t bits = 16 + keyLen * 8;
        XCryptographic_BlockCipherMode modes[] = {
            XCryptographic_BlockCipherMode_Cfb,
            XCryptographic_BlockCipherMode_Ofb
        };
        const uint8_t* expected[] = { cfbCt[keyLen], ofbCt[keyLen] };
        uint8_t camPt[48];
        memcpy(camPt, cbcPt[0], 16);
        memcpy(camPt + 16, cbcPt[1], 16);
        memcpy(camPt + 32, cbcPt[2], 16);
        for (index_slot = 0; index_slot < 2; ++index_slot) {
            size_t firstLen = 17u;
            size_t secondLen = 31u;
            XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(
                                          &operation,
                                          XCryptographic_BlockCipherAlgorithm_Camellia,
                                          xcryptographic_test_view(cbcKey[keyLen], bits),
                                          modes[index_slot], true) &&
                                      XCryptographic_blockCipherSetIv(
                                          &operation, xcryptographic_test_view(cbcIv, sizeof(cbcIv))),
                                      "Camellia-CFB/OFB setup", "Camellia CFB/OFB 初始化失败");
            result = XCryptographic_blockCipherUpdateInto(
                &operation, (char*)output, sizeof(output), xcryptographic_test_view(camPt, 17));
            XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == (int64_t)firstLen,
                                      "Camellia-CFB/OFB streaming", "Camellia 第一段输出长度错误");
            result = XCryptographic_blockCipherUpdateInto(
                &operation, (char*)output + firstLen, sizeof(output) - firstLen,
                xcryptographic_test_view(camPt + 17, 31));
            XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == (int64_t)secondLen &&
                                      XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                                      memcmp(output, expected[index_slot], sizeof(camPt)) == 0,
                                      "Camellia-CFB/OFB vector", "Camellia CFB/OFB 向量不匹配");
            XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_blockCipherSetup(
                                          &operation,
                                          XCryptographic_BlockCipherAlgorithm_Camellia,
                                          xcryptographic_test_view(cbcKey[keyLen], bits),
                                          modes[index_slot], false) &&
                                      XCryptographic_blockCipherSetIv(
                                          &operation, xcryptographic_test_view(cbcIv, sizeof(cbcIv))),
                                      "Camellia-CFB/OFB decrypt setup", "Camellia CFB/OFB 解密初始化失败");
            result = XCryptographic_blockCipherUpdateInto(
                &operation, (char*)output, sizeof(output),
                xcryptographic_test_view(expected[index_slot], sizeof(camPt)));
            XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == (int64_t)sizeof(camPt) &&
                                      XCryptographic_blockCipherFinishInto(&operation, NULL, 0).m_data &&
                                      memcmp(output, camPt, sizeof(camPt)) == 0,
                                      "Camellia-CFB/OFB decrypt", "Camellia CFB/OFB 解密向量不匹配");
        }
    }
    XCRYPTO_PRIMITIVE_PASS("Camellia ECB/CBC/CFB/OFB vectors");
    return true;
}

static bool XCryptographicPrimitive_test_aes_ccm_star_no_tag(void)
{
    static const uint8_t keyBytes[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };
    static const uint8_t nonce[13] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c
    };
    static const uint8_t plainText[64] = {
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
        0x36, 0xb0, 0x15, 0x3c, 0xeb, 0x5f, 0xea, 0x64,
    };
    XCryptographic_CcmStarNoTagOperation operation;
    uint8_t output[sizeof(expected)];
    XByteArrayView result;

    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_aesCcmStarNoTagSetup(
                                  &operation, xcryptographic_test_view(keyBytes, sizeof(keyBytes)),
                                  xcryptographic_test_view(nonce, sizeof(nonce))),
                              "AES-CCM*-no-tag setup", "CCM*-无标签初始化失败");
    result = XCryptographic_aesCcmStarNoTagUpdateInto(
        &operation, (char*)output, sizeof(output), xcryptographic_test_view(plainText, 17));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 17,
                              "AES-CCM*-no-tag streaming", "第一段输出长度错误");
    result = XCryptographic_aesCcmStarNoTagUpdateInto(
        &operation, (char*)output + 17, sizeof(output) - 17,
        xcryptographic_test_view(plainText + 17, sizeof(plainText) - 17));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 47 &&
                              memcmp(output, expected, sizeof(expected)) == 0,
                              "AES-CCM*-no-tag vector", "CCM*-无标签加密向量不匹配");
    XCryptographic_aesCcmStarNoTagAbort(&operation);
    XCRYPTO_PRIMITIVE_PASS("AES-CCM*-no-tag keystream vector (AES-128)");
    return true;
}

static bool XCryptographicPrimitive_test_chacha20(void)
{
    static const uint8_t key[32] = { 0 };
    static const uint8_t nonce[12] = { 0 };
    static const uint8_t plainText[96] = { 0 };
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
    XCryptographic_ChaCha20Operation operation;
    uint8_t output[sizeof(plainText)];
    XByteArrayView result;
    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_chacha20Setup(
                                  &operation, xcryptographic_test_view(key, sizeof(key)),
                                  xcryptographic_test_view(nonce, sizeof(nonce))),
                              "ChaCha20 setup", "ChaCha20 初始化失败");
    result = XCryptographic_chacha20UpdateInto(
        &operation, (char*)output, sizeof(output), xcryptographic_test_view(plainText, 17));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 17,
                              "ChaCha20 streaming", "第一段输出长度错误");
    result = XCryptographic_chacha20UpdateInto(
        &operation, (char*)output + 17, sizeof(output) - 17,
        xcryptographic_test_view(plainText + 17, sizeof(plainText) - 17));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 79 &&
                              memcmp(output, expected, sizeof(output)) == 0,
                              "ChaCha20 RFC 7539 vector", "ChaCha20 测试向量不匹配");
    XCryptographic_chacha20Abort(&operation);

    /* 恰好在 64 字节 ChaCha20 块边界处拆分。 */
    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_chacha20Setup(
                                  &operation, xcryptographic_test_view(key, sizeof(key)),
                                  xcryptographic_test_view(nonce, sizeof(nonce))),
                              "ChaCha20 boundary setup", "ChaCha20 初始化失败");
    result = XCryptographic_chacha20UpdateInto(
        &operation, (char*)output, sizeof(output), xcryptographic_test_view(plainText, 64));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 64,
                              "ChaCha20 boundary update1", "第一段输出长度错误");
    result = XCryptographic_chacha20UpdateInto(
        &operation, (char*)output + 64, sizeof(output) - 64,
        xcryptographic_test_view(plainText + 64, sizeof(plainText) - 64));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data && result.m_size == 32 &&
                              memcmp(output, expected, sizeof(output)) == 0,
                              "ChaCha20 boundary split", "跨 64 字节边界向量不匹配");
    XCryptographic_chacha20Abort(&operation);
    XCRYPTO_PRIMITIVE_PASS("ChaCha20 RFC 7539 vector");
    return true;
}

static bool XCryptographicPrimitive_test_pbkdf2_hmac_sha256(void)
{
    static const uint8_t expected[32] = {
        0x12, 0x0f, 0xb6, 0xcf, 0xfc, 0xf8, 0xb3, 0x2c,
        0x43, 0xe7, 0x22, 0x52, 0x56, 0xc4, 0xf8, 0x37,
        0xa8, 0x65, 0x48, 0xc9, 0x2c, 0xcc, 0x35, 0x48,
        0x08, 0x05, 0x98, 0x7c, 0xb7, 0x0b, 0xe1, 0x7b
    };
    uint8_t output[sizeof(expected)];
    XByteArrayView result = XCryptographic_pbkdf2HmacInto(
        xcryptographic_test_view((const uint8_t*)"password", 8),
        xcryptographic_test_view((const uint8_t*)"salt", 4), 1, XCryptographicHash_Sha256,
        sizeof(output), (char*)output, sizeof(output));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == (int64_t)sizeof(expected) &&
                                  memcmp(output, expected, sizeof(expected)) == 0,
                              "PBKDF2-HMAC-SHA-256 vector", "PBKDF2-HMAC-SHA-256 测试向量不匹配");
    {
        static const uint8_t sha512_expected[64] = {
            0xe1, 0xd9, 0xc1, 0x6a, 0xa6, 0x81, 0x70, 0x8a,
            0x45, 0xf5, 0xc7, 0xc4, 0xe2, 0x15, 0xce, 0xb6,
            0x6e, 0x01, 0x1a, 0x2e, 0x9f, 0x00, 0x40, 0x71,
            0x3f, 0x18, 0xae, 0xfd, 0xb8, 0x66, 0xd5, 0x3c,
            0xf7, 0x6c, 0xab, 0x28, 0x68, 0xa3, 0x9b, 0x9f,
            0x78, 0x40, 0xed, 0xce, 0x4f, 0xef, 0x5a, 0x82,
            0xbe, 0x67, 0x33, 0x5c, 0x77, 0xa6, 0x06, 0x8e,
            0x04, 0x11, 0x27, 0x54, 0xf2, 0x7c, 0xcf, 0x4e
        };
        uint8_t sha512_output[sizeof(sha512_expected)];
        result = XCryptographic_pbkdf2HmacInto(
            xcryptographic_test_view((const uint8_t*)"password", 8),
            xcryptographic_test_view((const uint8_t*)"salt", 4), 2,
            XCryptographicHash_Sha512, sizeof(sha512_output),
            (char *) sha512_output, sizeof(sha512_output));
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL &&
                                  memcmp(sha512_output, sha512_expected,
                                         sizeof(sha512_expected)) == 0,
                                  "PBKDF2-HMAC-SHA-512 vector", "PBKDF2-HMAC-SHA-512 测试向量不匹配");
    }
    XCRYPTO_PRIMITIVE_PASS("PBKDF2-HMAC-SHA-256 vector");
    return true;
}

static bool XCryptographicPrimitive_test_shake(void)
{
    static const uint8_t emptyExpected128[] = {
        0x7f, 0x9c, 0x2b, 0xa4, 0xe8, 0x8f, 0x82, 0x7d,
        0x61, 0x60, 0x45, 0x50, 0x76, 0x05, 0x85, 0x3e,
        0xd7, 0x3b, 0x80, 0x93, 0xf6, 0xef, 0xbc, 0x88,
        0xeb, 0x1a, 0x6e, 0xac, 0xfa, 0x66, 0xef, 0x26
    };
    static const uint8_t abcExpected128[] = {
        0x58, 0x81, 0x09, 0x2d, 0xd8, 0x18, 0xbf, 0x5c,
        0xf8, 0xa3, 0xdd, 0xb7, 0x93, 0xfb, 0xcb, 0xa7,
        0x40, 0x97, 0xd5, 0xc5, 0x26, 0xa6, 0xd3, 0x5f,
        0x97, 0xb8, 0x33, 0x51, 0x94, 0x0f, 0x2c, 0xc8
    };
    static const uint8_t abcExpected256[] = {
        0x48, 0x33, 0x66, 0x60, 0x13, 0x60, 0xa8, 0x77,
        0x1c, 0x68, 0x63, 0x08, 0x0c, 0xc4, 0x11, 0x4d,
        0x8d, 0xb4, 0x45, 0x30, 0xf8, 0xf1, 0xe1, 0xee,
        0x4f, 0x94, 0xea, 0x37, 0xe7, 0x8b, 0x57, 0x39
    };
    XCryptographic_XofOperation operation;
    uint8_t output[64];
    XByteArrayView result;
    static const uint8_t abc[3] = { 'a', 'b', 'c' };

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_xofSetup(&operation, XCryptographic_XofAlgorithm_Shake128),
        "SHAKE128 setup", "SHAKE-128 初始化失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_xofUpdateInto(&operation, xcryptographic_test_view(NULL, 0)),
        "SHAKE128 empty update", "空输入更新失败");
    result = XCryptographic_xofOutputInto(&operation, (char*)output, sizeof(output), 32);
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 32 &&
                                  memcmp(output, emptyExpected128, sizeof(emptyExpected128)) == 0,
                              "SHAKE128 empty vector", "SHAKE-128 空输入向量不匹配");
    XCryptographic_xofAbort(&operation);

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_xofSetup(&operation, XCryptographic_XofAlgorithm_Shake128),
        "SHAKE128 abc setup", "SHAKE-128 初始化失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_xofUpdateInto(&operation, xcryptographic_test_view(abc, sizeof(abc))),
        "SHAKE128 abc update", "SHAKE-128 输入失败");
    result = XCryptographic_xofOutputInto(&operation, (char*)output, sizeof(output), 32);
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 32 &&
                                  memcmp(output, abcExpected128, sizeof(abcExpected128)) == 0,
                              "SHAKE128 abc vector", "SHAKE-128 abc 向量不匹配");
    XCryptographic_xofAbort(&operation);

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_xofSetup(&operation, XCryptographic_XofAlgorithm_Shake256),
        "SHAKE256 abc setup", "SHAKE-256 初始化失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_xofUpdateInto(&operation, xcryptographic_test_view(abc, sizeof(abc))),
        "SHAKE256 abc update", "SHAKE-256 输入失败");
    result = XCryptographic_xofOutputInto(&operation, (char*)output, sizeof(output), 32);
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 32 &&
                                  memcmp(output, abcExpected256, sizeof(abcExpected256)) == 0,
                              "SHAKE256 abc vector", "SHAKE-256 abc 向量不匹配");
    XCryptographic_xofAbort(&operation);

    /* 跨块挤出：SHAKE-128("hello world") 输出 64 字节，分两次挤出验证连续性。 */
    {
        static const uint8_t expected[] = {
            0x3a, 0x91, 0x59, 0xf0, 0x71, 0xe4, 0xdd, 0x1c,
            0x8c, 0x4f, 0x96, 0x86, 0x07, 0xc3, 0x09, 0x42,
            0xe1, 0x20, 0xd8, 0x15, 0x6b, 0x8b, 0x1e, 0x72,
            0xe0, 0xd3, 0x76, 0xe8, 0x87, 0x1c, 0xb8, 0xb8,
            0x99, 0x07, 0x26, 0x65, 0x67, 0x4f, 0x26, 0xcc,
            0x49, 0x4a, 0x4b, 0xcf, 0x02, 0x7c, 0x58, 0x26,
            0x7e, 0x8e, 0xe2, 0xda, 0x60, 0xe9, 0x42, 0x75,
            0x9d, 0xe8, 0x6d, 0x26, 0x70, 0xbb, 0xa1, 0xaa
        };
        static const uint8_t hello[] = "hello world";
        XCRYPTO_PRIMITIVE_REQUIRE(
            XCryptographic_xofSetup(&operation, XCryptographic_XofAlgorithm_Shake128),
            "SHAKE128 split setup", "SHAKE-128 初始化失败");
        XCRYPTO_PRIMITIVE_REQUIRE(
            XCryptographic_xofUpdateInto(&operation, xcryptographic_test_view(hello, 11)),
            "SHAKE128 split update", "SHAKE-128 输入失败");
        result = XCryptographic_xofOutputInto(&operation, (char*)output, sizeof(output), 40);
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 40,
                                  "SHAKE128 split first", "分段输出第一段失败");
        result = XCryptographic_xofOutputInto(&operation, (char*)output + 40, sizeof(output) - 40, 24);
        XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 24 &&
                                      memcmp(output, expected, sizeof(expected)) == 0,
                                  "SHAKE128 split vector", "SHAKE-128 分段输出向量不匹配");
        XCryptographic_xofAbort(&operation);
    }
    XCRYPTO_PRIMITIVE_PASS("SHAKE-128/256 vectors and split squeeze");
    return true;
}

static bool XCryptographicPrimitive_test_kdf_batch(void)
{
    static const uint8_t ikm[22] = {
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
    static const uint8_t expectedPrk[32] = {
        0x07, 0x77, 0x09, 0x36, 0x2c, 0x2e, 0x32, 0xdf,
        0x0d, 0xdc, 0x3f, 0x0d, 0xc4, 0x7b, 0xba, 0x63,
        0x90, 0xb6, 0xc7, 0x3b, 0xb5, 0x0f, 0x9c, 0x31,
        0x22, 0xec, 0x84, 0x4a, 0xd7, 0xc2, 0xb3, 0xe5
    };
    static const uint8_t expectedOkM[42] = {
        0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a,
        0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
        0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c,
        0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf,
        0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18,
        0x58, 0x65
    };
    static const uint8_t expectedTls12Prf[48] = {
        0x7e, 0xd4, 0x2a, 0x23, 0xa1, 0x33, 0xad, 0x37,
        0x9b, 0x99, 0x19, 0x6a, 0x86, 0xdb, 0x88, 0x7c,
        0xf5, 0x95, 0xd9, 0xad, 0xa5, 0x66, 0x1e, 0xc1,
        0x18, 0x66, 0x91, 0x65, 0x9b, 0xf8, 0x7a, 0x7a,
        0x8d, 0x16, 0xf4, 0x32, 0x0d, 0x6b, 0x5e, 0x9c,
        0xc3, 0xab, 0x31, 0x8d, 0xbe, 0x40, 0x0a, 0x45
    };
    static const uint8_t expectedPskPure[48] = {
        0xe1, 0x4f, 0x0d, 0xb7, 0x4f, 0xaf, 0xda, 0xbc,
        0xb8, 0xae, 0x2f, 0xed, 0x0b, 0x6d, 0x46, 0x03,
        0x72, 0x7b, 0x1a, 0x59, 0x60, 0x4e, 0x36, 0x61,
        0xc2, 0x54, 0xd2, 0xd6, 0xe7, 0x76, 0xb0, 0x8c,
        0xac, 0xf4, 0xa7, 0x22, 0xe3, 0xe7, 0x41, 0x3a,
        0xd0, 0x8b, 0xcb, 0xc4, 0x8a, 0xc1, 0x97, 0x8d
    };
    static const uint8_t expectedPskMixed[48] = {
        0xc4, 0x43, 0x70, 0x00, 0x72, 0xa9, 0x56, 0x64,
        0x41, 0x43, 0x70, 0x80, 0xee, 0x8d, 0x18, 0x2e,
        0xb1, 0x69, 0x38, 0xae, 0x93, 0xf6, 0xa1, 0xcc,
        0x1f, 0x53, 0x5e, 0xff, 0x37, 0x5c, 0x14, 0x1a,
        0xff, 0x5b, 0xc0, 0x44, 0x66, 0x9e, 0x8c, 0x88,
        0x20, 0x7b, 0x72, 0x4d, 0x75, 0x71, 0x87, 0x3f
    };
    static const uint8_t pbkdf2Password[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f
    };
    static const uint8_t expectedPbkdf2[32] = {
        0x4e, 0x91, 0xe3, 0xc9, 0x6d, 0x4d, 0x0c, 0xac,
        0x33, 0x29, 0x41, 0xec, 0x57, 0x8b, 0xd4, 0x8f,
        0xf5, 0x69, 0x17, 0x49, 0x35, 0xfc, 0x3f, 0x8f,
        0x7f, 0xe4, 0x3e, 0x6c, 0xcc, 0x72, 0xfe, 0x3f
    };
    uint8_t prk[32];
    uint8_t okm[sizeof(expectedOkM)];
    uint8_t tls12[48];
    uint8_t psk[48];
    uint8_t pbkdf2[32];
    XByteArrayView result;

    result = XCryptographic_hkdfExtractInto(
        xcryptographic_test_view(salt, sizeof(salt)),
        xcryptographic_test_view(ikm, sizeof(ikm)), XCryptographicHash_Sha256,
        (char*)prk, sizeof(prk));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 32 &&
                                  memcmp(prk, expectedPrk, sizeof(expectedPrk)) == 0,
                              "HKDF-extract SHA-256 RFC 5869 PRK", "HKDF 提取阶段向量不匹配");

    result = XCryptographic_hkdfExpandInto(
        xcryptographic_test_view(expectedPrk, sizeof(expectedPrk)),
        xcryptographic_test_view(info, sizeof(info)), XCryptographicHash_Sha256, sizeof(okm),
        (char*)okm, sizeof(okm));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == (int64_t)sizeof(okm) &&
                                  memcmp(okm, expectedOkM, sizeof(expectedOkM)) == 0,
                              "HKDF-expand SHA-256 RFC 5869 OKM", "HKDF 展开阶段向量不匹配");

    result = XCryptographic_tls12PrfInto(
        xcryptographic_test_view((const uint8_t*)"secret", 6),
        xcryptographic_test_view((const uint8_t*)"label", 5),
        xcryptographic_test_view((const uint8_t*)"seed", 4), XCryptographicHash_Sha256,
        sizeof(tls12), (char*)tls12, sizeof(tls12));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 48 &&
                                  memcmp(tls12, expectedTls12Prf, sizeof(expectedTls12Prf)) == 0,
                              "TLS1.2 PRF SHA-256 vector", "TLS1.2 PRF 向量不匹配");

    result = XCryptographic_tls12PskToMsInto(
        xcryptographic_test_view((const uint8_t*)"abc", 3),
        xcryptographic_test_view(NULL, 0),
        xcryptographic_test_view((const uint8_t*)"master secret", 13),
        xcryptographic_test_view((const uint8_t*)"seed", 4), XCryptographicHash_Sha256,
        sizeof(psk), (char*)psk, sizeof(psk));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 48 &&
                                  memcmp(psk, expectedPskPure, sizeof(expectedPskPure)) == 0,
                              "TLS1.2 PSK-to-MS pure vector", "TLS1.2 纯 PSK 主密钥向量不匹配");

    result = XCryptographic_tls12PskToMsInto(
        xcryptographic_test_view((const uint8_t*)"abc", 3),
        xcryptographic_test_view((const uint8_t*)"\x00\x11\x22\x33", 4),
        xcryptographic_test_view((const uint8_t*)"master secret", 13),
        xcryptographic_test_view((const uint8_t*)"seed", 4), XCryptographicHash_Sha256,
        sizeof(psk), (char*)psk, sizeof(psk));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 48 &&
                                  memcmp(psk, expectedPskMixed, sizeof(expectedPskMixed)) == 0,
                              "TLS1.2 PSK-to-MS mixed vector", "TLS1.2 混合 PSK 主密钥向量不匹配");

    result = XCryptographic_pbkdf2AesCmacPrf128Into(
        xcryptographic_test_view(pbkdf2Password, sizeof(pbkdf2Password)),
        xcryptographic_test_view((const uint8_t*)"salt", 4), 5,
        sizeof(pbkdf2), (char*)pbkdf2, sizeof(pbkdf2));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 32 &&
                                  memcmp(pbkdf2, expectedPbkdf2, sizeof(expectedPbkdf2)) == 0,
                              "PBKDF2-AES-CMAC-PRF-128 vector", "PBKDF2-AES-CMAC 向量不匹配");

    XCRYPTO_PRIMITIVE_PASS("HKDF-ext/expand, TLS1.2 PRF/PSK-to-MS, PBKDF2-AES-CMAC vectors");
    return true;
}

static bool XCryptographicPrimitive_test_hmac_stream(void)
{
    static const uint8_t key[20] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
    };
    static const uint8_t expected[48] = {
        0xaf, 0xd0, 0x39, 0x44, 0xd8, 0x48, 0x95, 0x62,
        0x6b, 0x08, 0x25, 0xf4, 0xab, 0x46, 0x90, 0x7f,
        0x15, 0xf9, 0xda, 0xdb, 0xe4, 0x10, 0x1e, 0xc6,
        0x82, 0xaa, 0x03, 0x4c, 0x7c, 0xeb, 0xc5, 0x9c,
        0xfa, 0xea, 0x9e, 0xa9, 0x07, 0x6e, 0xde, 0x7f,
        0x4a, 0xf1, 0x52, 0xe8, 0xb2, 0xfa, 0x9c, 0xb6
    };
    XCryptographic_HmacOperation operation;
    uint8_t output[sizeof(expected)];
    XByteArrayView result;

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_hmacSetup(&operation, xcryptographic_test_view(key, sizeof(key)),
                                 XCryptographicHash_Sha384),
        "HMAC-SHA-384 streaming setup", "HMAC 流式初始化失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_hmacUpdate(&operation, xcryptographic_test_view((const uint8_t*)"Hi ", 3)) &&
        XCryptographic_hmacUpdate(&operation, xcryptographic_test_view((const uint8_t*)"There", 5)),
        "HMAC-SHA-384 streaming update", "HMAC 流式更新失败");
    result = XCryptographic_hmacFinishInto(&operation, (char*)output, sizeof(output));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL &&
                              memcmp(output, expected, sizeof(expected)) == 0,
                              "HMAC-SHA-384 streaming vector", "RFC 4231 向量不匹配");
    XCryptographic_hmacAbort(&operation);
    XCRYPTO_PRIMITIVE_PASS("HMAC-SHA-384 streaming RFC 4231 vector");
    return true;
}

static bool XCryptographicPrimitive_test_ffdh(void)
{
    /* RFC 7919 ffdhe2048：X = 0x0102...1f20 时，期望值 = 2^X mod p。 */
    static const uint8_t privateKey[256] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
        0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
        0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c,
        0x1d, 0x1e, 0x1f, 0x20,
    };
    static const uint8_t expectedPublic[256] = {
        0x0e, 0xef, 0x0c, 0x0e, 0xae, 0x9c, 0x65, 0xa3, 0x33, 0x2c, 0xdc, 0x74, 0x2b, 0x58, 0x56, 0x1d,
        0x36, 0x2c, 0x0a, 0xf5, 0x26, 0xf8, 0xad, 0x52, 0x8b, 0x19, 0xff, 0x39, 0xc9, 0x14, 0x34, 0x31,
        0x2f, 0x83, 0x30, 0x2e, 0xd9, 0xdc, 0x6f, 0x2b, 0x84, 0xb2, 0x50, 0x48, 0x2d, 0xbd, 0x80, 0x96,
        0x21, 0x54, 0xf8, 0xa6, 0xd6, 0x83, 0x74, 0x1b, 0xf8, 0xbf, 0x4f, 0x3f, 0xde, 0xe2, 0x2f, 0x80,
        0x54, 0x1a, 0x77, 0x55, 0x3d, 0x1a, 0xe7, 0xa0, 0x96, 0xc5, 0x21, 0xf8, 0x69, 0x98, 0x7a, 0xe5,
        0xec, 0xa1, 0x09, 0x0d, 0x93, 0xe7, 0xbd, 0xea, 0x0b, 0x43, 0x49, 0xf1, 0x7f, 0xee, 0x34, 0xf4,
        0x77, 0x5b, 0xfb, 0x38, 0x0f, 0x42, 0x6d, 0xec, 0x93, 0x7b, 0x25, 0x82, 0x56, 0x80, 0xc6, 0xe2,
        0xb8, 0xc4, 0xc9, 0xee, 0xde, 0x64, 0xb9, 0x1f, 0x5c, 0xdc, 0x02, 0xba, 0x78, 0xe7, 0x59, 0x08,
        0xd4, 0x7c, 0xd2, 0x7f, 0xda, 0x4b, 0x51, 0x88, 0xc2, 0x2b, 0xaf, 0x2a, 0xcd, 0x1e, 0x86, 0x1e,
        0xd0, 0x86, 0x4e, 0x9e, 0xb9, 0x34, 0x66, 0x40, 0xd3, 0x54, 0xea, 0xda, 0x9f, 0x71, 0x56, 0xa6,
        0x01, 0x38, 0x7b, 0xa5, 0xf1, 0xb8, 0x60, 0x97, 0x33, 0x70, 0x78, 0x03, 0xa5, 0xac, 0x86, 0x6f,
        0xc1, 0xbb, 0xe5, 0x4b, 0x15, 0xa9, 0x99, 0x34, 0x72, 0x7b, 0xc5, 0xc1, 0x57, 0xf8, 0x93, 0xd8,
        0x67, 0xa8, 0x9a, 0xbf, 0x0c, 0xe3, 0xdd, 0x0c, 0xf3, 0xd7, 0xce, 0x00, 0x07, 0x3d, 0xb8, 0xb3,
        0xbb, 0xae, 0x14, 0x86, 0xfa, 0xed, 0xc4, 0x62, 0xf7, 0xed, 0xeb, 0x3d, 0xc4, 0x4b, 0xa3, 0x03,
        0x36, 0xde, 0x3d, 0x4d, 0xbf, 0xb4, 0xf1, 0x4f, 0x14, 0x3c, 0x46, 0xb0, 0xba, 0x93, 0xf3, 0x16,
        0xf5, 0x0c, 0x33, 0x77, 0xb9, 0xa3, 0x8a, 0x27, 0xae, 0x04, 0x8f, 0x56, 0x05, 0xa4, 0xdc, 0xbf,
    };
    uint8_t publicKey[256];
    uint8_t privA[256];
    uint8_t privB[256];
    uint8_t pubA[256];
    uint8_t pubB[256];
    uint8_t secretA[256];
    uint8_t secretB[256];
    uint8_t invalidPublic[256];
    XByteArrayView result;

    result = XCryptographic_ffdhExportPublicKeyInto(
        (char*)publicKey, sizeof(publicKey),
        (const char*)privateKey, sizeof(privateKey));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 256 &&
                                  memcmp(publicKey, expectedPublic, sizeof(expectedPublic)) == 0,
                              "FFDH ffdhe2048 export G^X mod P", "RFC 7919 公钥向量不匹配");

    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_ffdhGeneratePrivateKeyInto((char*)privA, sizeof(privA)) &&
                                  XCryptographic_ffdhGeneratePrivateKeyInto((char*)privB, sizeof(privB)),
                              "FFDH private key generation", "RFC 7919 私钥生成失败");

    result = XCryptographic_ffdhExportPublicKeyInto(
        (char*)pubA, sizeof(pubA), (const char*)privA, sizeof(privA));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 256,
                              "FFDH export public A", "公钥 A 导出失败");
    result = XCryptographic_ffdhExportPublicKeyInto(
        (char*)pubB, sizeof(pubB), (const char*)privB, sizeof(privB));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 256,
                              "FFDH export public B", "公钥 B 导出失败");

    result = XCryptographic_ffdhAgreeInto(
        (char*)secretA, sizeof(secretA),
        (const char*)privA, sizeof(privA),
        (const char*)pubB, sizeof(pubB));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 256,
                              "FFDH agree A<-B", "A 方向共享密钥失败");
    result = XCryptographic_ffdhAgreeInto(
        (char*)secretB, sizeof(secretB),
        (const char*)privB, sizeof(privB),
        (const char*)pubA, sizeof(pubA));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data != NULL && result.m_size == 256 &&
                                  memcmp(secretA, secretB, sizeof(secretA)) == 0,
                              "FFDH agree both directions", "双方共享密钥不一致");

    memset(invalidPublic, 0, sizeof(invalidPublic));
    invalidPublic[255] = 1;
    result = XCryptographic_ffdhAgreeInto(
        (char*)secretA, sizeof(secretA),
        (const char*)privA, sizeof(privA),
        (const char*)invalidPublic, sizeof(invalidPublic));
    XCRYPTO_PRIMITIVE_REQUIRE(result.m_data == NULL,
                              "FFDH reject low-order peer 1", "应拒绝对端公钥 1");

    XCRYPTO_PRIMITIVE_PASS("FFDH RFC 7919 export/agree vectors");
    return true;
}

static bool XCryptographicPrimitive_test_rsa(void)
{
    static const uint8_t rsaTestPrivDer[] = {
        0x30, 0x82, 0x02, 0x5d, 0x02, 0x01, 0x00, 0x02, 0x81, 0x81, 0x00, 0xc3,
        0x92, 0x59, 0xd0, 0x8d, 0xed, 0x5c, 0xf3, 0x69, 0xc3, 0x5e, 0x86, 0x32,
        0xb5, 0xf0, 0xb1, 0xad, 0x6f, 0x1e, 0x1d, 0x7d, 0x27, 0x7e, 0x26, 0x7e,
        0x5e, 0xe5, 0x20, 0x17, 0x81, 0x60, 0xea, 0xb7, 0xe5, 0x85, 0xd5, 0x06,
        0x2a, 0x1d, 0x60, 0xb5, 0x81, 0x92, 0x76, 0x10, 0x9b, 0x83, 0x6a, 0x83,
        0xb6, 0x7c, 0x36, 0xdf, 0x2d, 0xc5, 0x38, 0xaf, 0xf7, 0xe6, 0x99, 0xe1,
        0x15, 0x04, 0xb6, 0x5d, 0x6b, 0x67, 0x7f, 0x53, 0x83, 0x72, 0xd4, 0x02,
        0xf5, 0x0f, 0x4b, 0x47, 0x43, 0x63, 0x84, 0x3e, 0xd1, 0xc5, 0xc6, 0x99,
        0xf8, 0x6b, 0x54, 0x1b, 0xda, 0x46, 0xaa, 0x7d, 0xaa, 0xf2, 0x2b, 0x34,
        0x07, 0xfd, 0x5d, 0xf1, 0xe8, 0x7c, 0x18, 0xa0, 0x0c, 0x60, 0xde, 0x93,
        0xd2, 0x32, 0xb8, 0x34, 0xc4, 0xd3, 0xdb, 0xea, 0xc2, 0x12, 0xef, 0x64,
        0xad, 0xc1, 0x01, 0xd3, 0xd9, 0xa8, 0x8d, 0x02, 0x03, 0x01, 0x00, 0x01,
        0x02, 0x81, 0x81, 0x00, 0xb7, 0x12, 0xf8, 0x1a, 0x9f, 0xd1, 0x73, 0xf2,
        0xb4, 0xad, 0xdb, 0x7d, 0x5a, 0x59, 0x30, 0xa0, 0xd2, 0xce, 0xb2, 0xed,
        0x3d, 0xec, 0x4b, 0x4d, 0xf3, 0x7c, 0x17, 0x96, 0x8c, 0x0a, 0x63, 0xd4,
        0x35, 0x23, 0x99, 0xbd, 0x89, 0x50, 0xc1, 0x41, 0x77, 0x87, 0x7c, 0xb8,
        0x22, 0xe1, 0xc1, 0x0a, 0x63, 0x93, 0xdf, 0x01, 0x6a, 0xb8, 0x28, 0xe7,
        0xe0, 0xe9, 0xfa, 0x27, 0x50, 0x29, 0x61, 0xc7, 0x44, 0x1a, 0x18, 0x06,
        0x6a, 0x25, 0x92, 0xe0, 0x2c, 0xef, 0x56, 0x96, 0x06, 0x06, 0xdf, 0xe6,
        0x07, 0x6a, 0x47, 0xb1, 0x34, 0xa2, 0x24, 0x9f, 0x30, 0x2b, 0x70, 0x8b,
        0xc3, 0x2b, 0xf7, 0xc0, 0xd3, 0x8e, 0x35, 0xcb, 0xfb, 0x17, 0x9b, 0x94,
        0x43, 0x73, 0xa6, 0x3e, 0xf4, 0x52, 0xdb, 0x37, 0xdc, 0xf8, 0x27, 0xdc,
        0x4d, 0x39, 0x01, 0x2c, 0xb9, 0xb1, 0xb3, 0xbd, 0x56, 0xb8, 0x06, 0xa1,
        0x02, 0x41, 0x00, 0xf6, 0x72, 0xa7, 0x7f, 0x0d, 0x4a, 0x27, 0xf0, 0xe7,
        0xaa, 0x17, 0xef, 0xeb, 0x77, 0xa6, 0xdc, 0xed, 0xf8, 0x95, 0x08, 0x71,
        0x57, 0xc2, 0x6a, 0x10, 0x0a, 0xf9, 0x7c, 0xe9, 0xa0, 0xcc, 0xf4, 0x14,
        0x97, 0x4d, 0x43, 0x34, 0x96, 0xc0, 0x4c, 0x2b, 0x20, 0x23, 0xf0, 0x0c,
        0xe5, 0xa0, 0x20, 0x4a, 0x73, 0x63, 0xee, 0x44, 0xd4, 0x43, 0x76, 0x12,
        0xa7, 0x81, 0x26, 0xb4, 0x8a, 0xb4, 0x49, 0x02, 0x41, 0x00, 0xcb, 0x26,
        0xe2, 0x6c, 0xc8, 0xc8, 0xac, 0xf6, 0x6d, 0x7c, 0xc8, 0x9f, 0xa7, 0x2b,
        0xa0, 0x06, 0x29, 0xcf, 0x64, 0x61, 0x38, 0x3d, 0xac, 0xb5, 0xcc, 0x7d,
        0x05, 0xc0, 0x25, 0x83, 0xb9, 0xcb, 0x69, 0x78, 0x73, 0xbd, 0x3f, 0x52,
        0x66, 0x00, 0x54, 0x4a, 0x9f, 0xf9, 0xd6, 0x56, 0x9e, 0x7d, 0xca, 0xe1,
        0x3a, 0xe5, 0xda, 0x59, 0xe4, 0xd4, 0xe8, 0x17, 0x3a, 0x18, 0xe5, 0x2c,
        0xca, 0x25, 0x02, 0x41, 0x00, 0xa3, 0xd4, 0xbe, 0x72, 0x60, 0xb4, 0x4e,
        0x6f, 0x00, 0xa2, 0x7b, 0x7d, 0x3b, 0xec, 0x73, 0xd9, 0xe4, 0xbc, 0xde,
        0xde, 0x18, 0xf2, 0xfd, 0x44, 0x22, 0xdc, 0x18, 0xd4, 0xa8, 0x3f, 0x04,
        0x60, 0xb2, 0x1b, 0x8e, 0xfa, 0x41, 0x48, 0x82, 0x17, 0x60, 0x87, 0xe2,
        0x3c, 0x1f, 0x66, 0xbb, 0x17, 0x1c, 0x47, 0x2f, 0x44, 0x63, 0x2f, 0x34,
        0x95, 0x96, 0x7b, 0x12, 0x09, 0x57, 0xf5, 0xe9, 0x39, 0x02, 0x40, 0x56,
        0x7e, 0x6c, 0xc3, 0x02, 0x4d, 0xa6, 0x8e, 0x99, 0x19, 0x1a, 0xd6, 0x16,
        0xb7, 0xd5, 0x3f, 0x2a, 0x87, 0xf9, 0x66, 0x07, 0x2b, 0x03, 0x20, 0xb8,
        0x3a, 0xb6, 0xbb, 0x13, 0x7d, 0xdd, 0x1a, 0x05, 0x02, 0xda, 0xcc, 0x45,
        0x6c, 0x90, 0xaf, 0x2d, 0x34, 0x44, 0x9e, 0x7b, 0xaa, 0x8f, 0x7a, 0x61,
        0x69, 0xb9, 0xc8, 0xe9, 0x49, 0x82, 0xcb, 0x3f, 0x31, 0xbc, 0x73, 0xa3,
        0x07, 0x89, 0xdd, 0x02, 0x40, 0x0a, 0xcb, 0xee, 0xea, 0x5b, 0x19, 0x91,
        0x28, 0x8c, 0x56, 0xc7, 0x81, 0x4b, 0x52, 0xfa, 0x5f, 0xf5, 0xed, 0xd7,
        0xa7, 0x3f, 0xaa, 0x23, 0x54, 0x39, 0x50, 0xd1, 0x97, 0x45, 0xdd, 0xd6,
        0x0d, 0xba, 0xec, 0x2c, 0xaf, 0x7e, 0x76, 0x5d, 0x17, 0x47, 0xec, 0xd7,
        0x25, 0x5e, 0xc1, 0xe8, 0x89, 0x5f, 0x66, 0xa0, 0x8c, 0x3a, 0xe1, 0x69,
        0x6b, 0xea, 0x44, 0xe5, 0x08, 0x58, 0x31, 0x5c, 0x2e
    };
    static const uint8_t rsaTestPubDer[] = {
        0x30, 0x81, 0x89, 0x02, 0x81, 0x81, 0x00, 0xc3, 0x92, 0x59, 0xd0, 0x8d,
        0xed, 0x5c, 0xf3, 0x69, 0xc3, 0x5e, 0x86, 0x32, 0xb5, 0xf0, 0xb1, 0xad,
        0x6f, 0x1e, 0x1d, 0x7d, 0x27, 0x7e, 0x26, 0x7e, 0x5e, 0xe5, 0x20, 0x17,
        0x81, 0x60, 0xea, 0xb7, 0xe5, 0x85, 0xd5, 0x06, 0x2a, 0x1d, 0x60, 0xb5,
        0x81, 0x92, 0x76, 0x10, 0x9b, 0x83, 0x6a, 0x83, 0xb6, 0x7c, 0x36, 0xdf,
        0x2d, 0xc5, 0x38, 0xaf, 0xf7, 0xe6, 0x99, 0xe1, 0x15, 0x04, 0xb6, 0x5d,
        0x6b, 0x67, 0x7f, 0x53, 0x83, 0x72, 0xd4, 0x02, 0xf5, 0x0f, 0x4b, 0x47,
        0x43, 0x63, 0x84, 0x3e, 0xd1, 0xc5, 0xc6, 0x99, 0xf8, 0x6b, 0x54, 0x1b,
        0xda, 0x46, 0xaa, 0x7d, 0xaa, 0xf2, 0x2b, 0x34, 0x07, 0xfd, 0x5d, 0xf1,
        0xe8, 0x7c, 0x18, 0xa0, 0x0c, 0x60, 0xde, 0x93, 0xd2, 0x32, 0xb8, 0x34,
        0xc4, 0xd3, 0xdb, 0xea, 0xc2, 0x12, 0xef, 0x64, 0xad, 0xc1, 0x01, 0xd3,
        0xd9, 0xa8, 0x8d, 0x02, 0x03, 0x01, 0x00, 0x01
    };
    static const uint8_t rsaMsg[] = "XCryptographic RSA 下沉测试消息";
    static const uint8_t rsaHash[32] = {
        0xba, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
    };
    XCryptographic_RsaKey *priv = NULL;
    XCryptographic_RsaKey *pub = NULL;
    XByteArrayView derView;
    uint8_t pubDer[256];
    uint8_t ct[128];
    uint8_t pt[128];
    uint8_t sig[128];
    XByteArrayView ctView, ptView, sigView;
    XByteArrayView msgView = xcryptographic_test_view(rsaMsg, sizeof(rsaMsg) - 1);
    XByteArrayView hashView = xcryptographic_test_view(rsaHash, sizeof(rsaHash));
    XByteArrayView inputView = xcryptographic_test_view(rsaMsg, sizeof(rsaMsg) - 1);
    XByteArrayView emptyLabel = { NULL, 0 };
    size_t ptLen = 0;

    derView = xcryptographic_test_view(rsaTestPrivDer, sizeof(rsaTestPrivDer));
    priv = XCryptographic_rsaImportPrivateKey(derView);
    XCRYPTO_PRIMITIVE_REQUIRE(priv != NULL,
                              "RSA import private key", "导入 RSA 私钥失败");
    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_rsaIsPrivateKey(priv) &&
                                  XCryptographic_rsaKeyBytes(priv) == 128,
                              "RSA private key info", "私钥属性不正确");

    /* 导出公钥 DER 并导入 */
    derView = XCryptographic_rsaExportPublicKeyInto((char*)pubDer, sizeof(pubDer), priv);
    XCRYPTO_PRIMITIVE_REQUIRE(derView.m_data != NULL && derView.m_size > 0,
                              "RSA export public key", "导出公钥失败");
    derView = xcryptographic_test_view(pubDer, (size_t)derView.m_size);
    pub = XCryptographic_rsaImportPublicKey(derView);
    XCRYPTO_PRIMITIVE_REQUIRE(pub != NULL,
                              "RSA import public key", "导入 RSA 公钥失败");
    XCRYPTO_PRIMITIVE_REQUIRE(!XCryptographic_rsaIsPrivateKey(pub) &&
                                  XCryptographic_rsaKeyBytes(pub) == 128,
                              "RSA public key info", "公钥属性不正确");

    /* 导出私钥 DER 并重新导入，验证序列化往返 */
    {
        uint8_t privDerBuf[2048];
        XByteArrayView privDerView;
        XCryptographic_RsaKey *priv2 = NULL;
        privDerView = XCryptographic_rsaExportPrivateKeyInto(
            (char *)privDerBuf, sizeof(privDerBuf), priv);
        XCRYPTO_PRIMITIVE_REQUIRE(privDerView.m_data != NULL &&
                                      privDerView.m_size > 0,
                                  "RSA export private key", "导出私钥失败");
        priv2 = XCryptographic_rsaImportPrivateKey(privDerView);
        XCRYPTO_PRIMITIVE_REQUIRE(priv2 != NULL &&
                                      XCryptographic_rsaIsPrivateKey(priv2) &&
                                      XCryptographic_rsaKeyBytes(priv2) == 128,
                                  "RSA export/import private key",
                                  "私钥序列化往返失败");
        XCryptographic_rsaDestroyKey(priv2);
    }

    /* OAEP 加解密往返 */
    ctView = XCryptographic_rsaEncryptInto((char*)ct, sizeof(ct), pub,
                                           XCryptographic_RsaPadding_Oaep,
                                           XCryptographicHash_Sha256,
                                           emptyLabel, inputView);
    XCRYPTO_PRIMITIVE_REQUIRE(ctView.m_data != NULL && ctView.m_size == 128,
                              "RSA OAEP encrypt", "OAEP 加密失败");
    ptView = XCryptographic_rsaDecryptInto((char*)pt, sizeof(pt), priv,
                                           XCryptographic_RsaPadding_Oaep,
                                           XCryptographicHash_Sha256,
                                           emptyLabel, ctView, &ptLen);
    XCRYPTO_PRIMITIVE_REQUIRE(ptView.m_data != NULL && ptLen == inputView.m_size &&
                                  memcmp(pt, rsaMsg, ptLen) == 0,
                              "RSA OAEP decrypt", "OAEP 解密结果不一致");

    /* PKCS#1 v1.5 签名/验签 */
    sigView = XCryptographic_rsaSignHashInto((char*)sig, sizeof(sig), priv,
                                             XCryptographic_RsaPadding_Pkcs1,
                                             XCryptographicHash_Sha256,
                                             hashView);
    XCRYPTO_PRIMITIVE_REQUIRE(sigView.m_data != NULL && sigView.m_size == 128,
                              "RSA PKCS1v15 sign", "PKCS1v15 签名失败");
    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_rsaVerifyHash(
                                  pub, XCryptographic_RsaPadding_Pkcs1,
                                  XCryptographicHash_Sha256,
                                  hashView, sigView),
                              "RSA PKCS1v15 verify", "PKCS1v15 验签失败");

    /* PSS 签名/验签 */
    sigView = XCryptographic_rsaSignHashInto((char*)sig, sizeof(sig), priv,
                                             XCryptographic_RsaPadding_Pss,
                                             XCryptographicHash_Sha256,
                                             hashView);
    XCRYPTO_PRIMITIVE_REQUIRE(sigView.m_data != NULL && sigView.m_size == 128,
                              "RSA PSS sign", "PSS 签名失败");
    XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_rsaVerifyHash(
                                  pub, XCryptographic_RsaPadding_Pss,
                                  XCryptographicHash_Sha256,
                                  hashView, sigView),
                              "RSA PSS verify", "PSS 验签失败");

    /* 默认回归保留一次随机密钥生成及往返，避免纯 C 大整数实现拖慢全套测试。 */
    {
        int iter;
        for (iter = 0; iter < 1; ++iter) {
            XCryptographic_RsaKey *gpriv = XCryptographic_rsaGenerateKey(1024, 65537);
            XByteArrayView gPubDer, gPrivDer, gCt, gPt;
            uint8_t gPubBuf[512], gPrivBuf[2048], gCtBuf[256], gPtBuf[256];
            XCryptographic_RsaKey *gpub = NULL;
            size_t gPtLen = 0;
            if (!gpriv) { XPrintf("[RSA压力] 密钥生成失败 iter=%d\n", iter); return false; }
            gPubDer = XCryptographic_rsaExportPublicKeyInto((char*)gPubBuf, sizeof(gPubBuf), gpriv);
            gPrivDer = XCryptographic_rsaExportPrivateKeyInto((char*)gPrivBuf, sizeof(gPrivBuf), gpriv);
            if (!gPubDer.m_data || !gPrivDer.m_data) {
                XPrintf("[RSA压力] 导出失败 iter=%d\n", iter);
                XCryptographic_rsaDestroyKey(gpriv);
                return false;
            }
            gPubDer = xcryptographic_test_view(gPubBuf, (size_t)gPubDer.m_size);
            gPrivDer = xcryptographic_test_view(gPrivBuf, (size_t)gPrivDer.m_size);
            gpub = XCryptographic_rsaImportPublicKey(gPubDer);
            {
                uint8_t gPub2Buf[512];
                XByteArrayView gPub2 = XCryptographic_rsaExportPublicKeyInto((char*)gPub2Buf, sizeof(gPub2Buf), gpub);
                if (!gPub2.m_data || gPub2.m_size != gPubDer.m_size ||
                    memcmp(gPub2Buf, gPubBuf, (size_t)gPub2.m_size) != 0) {
                    XPrintf("[RSA压力] 公钥再导出不一致 iter=%d\n", iter);
                    XCryptographic_rsaDestroyKey(gpriv);
                    XCryptographic_rsaDestroyKey(gpub);
                    return false;
                }
            }
            {
                XCryptographic_RsaKey *gpriv2 = XCryptographic_rsaImportPrivateKey(gPrivDer);
                if (!gpriv2) {
                    size_t ii;
                    XPrintf("[RSA压力] 私钥导入失败 iter=%d privlen=%u\n", iter, (unsigned)gPrivDer.m_size);
                    for (ii = 0; ii < (size_t)gPrivDer.m_size; ++ii) {
                        XPrintf("%02x", ((const uint8_t*)gPrivDer.m_data)[ii]);
                        if ((ii & 31) == 31) XPrintf("\n");
                    }
                    XPrintf("\n");
                    XCryptographic_rsaDestroyKey(gpriv);
                    if(gpub) XCryptographic_rsaDestroyKey(gpub);
                    return false;
                }
                XCryptographic_rsaDestroyKey(gpriv2);
            }
            gCt = XCryptographic_rsaEncryptInto((char*)gCtBuf, sizeof(gCtBuf), gpub,
                                                XCryptographic_RsaPadding_Pkcs1,
                                                XCryptographicHash_Sha256,
                                                emptyLabel, inputView);
            if (!gCt.m_data) { XPrintf("[RSA压力] 加密失败 iter=%d\n", iter); XCryptographic_rsaDestroyKey(gpriv); XCryptographic_rsaDestroyKey(gpub); return false; }
            gPt = XCryptographic_rsaDecryptInto((char*)gPtBuf, sizeof(gPtBuf), gpriv,
                                                XCryptographic_RsaPadding_Pkcs1,
                                                XCryptographicHash_Sha256,
                                                emptyLabel, gCt, &gPtLen);
            if (!gPt.m_data || gPtLen != inputView.m_size ||
                memcmp(gPtBuf, rsaMsg, gPtLen) != 0) {
                size_t ii;
                XPrintf("[RSA压力] 加解密往返失败 iter=%d pt=%d ptlen=%u msg_size=%u\n",
                        iter, gPt.m_data?1:0, (unsigned)gPtLen, (unsigned)inputView.m_size);
                for (ii = 0; ii < gPtLen && ii < 64; ++ii) XPrintf("%02x", ((const uint8_t*)gPt.m_data)[ii]);
                XPrintf("\n");
                XCryptographic_rsaDestroyKey(gpriv);
                XCryptographic_rsaDestroyKey(gpub);
                return false;
            }
            {
                uint8_t gSigBuf[256];
                XByteArrayView gSig;
                gSig = XCryptographic_rsaSignHashInto((char*)gSigBuf, sizeof(gSigBuf), gpriv,
                                                      XCryptographic_RsaPadding_Pkcs1,
                                                      XCryptographicHash_Sha256, hashView);
                if (!gSig.m_data || !XCryptographic_rsaVerifyHash(
                        gpub, XCryptographic_RsaPadding_Pkcs1,
                        XCryptographicHash_Sha256, hashView, gSig)) {
                    XPrintf("[RSA压力] 签验签失败 iter=%d\n", iter);
                    XCryptographic_rsaDestroyKey(gpriv);
                    XCryptographic_rsaDestroyKey(gpub);
                    return false;
                }
            }
            XCryptographic_rsaDestroyKey(gpriv);
            XCryptographic_rsaDestroyKey(gpub);
        }
    }

    XCryptographic_rsaDestroyKey(priv);
    XCryptographic_rsaDestroyKey(pub);
    XCRYPTO_PRIMITIVE_PASS("RSA PKCS1/OAEP/PSS 向量与往返");
    return true;
}

static bool XCryptographicPrimitive_test_ecdsa_deterministic(void)
{
    static const uint8_t priv[32] = {
        0xC9,0xAF,0xA9,0xD8,0x45,0xBA,0x75,0x16,0x6B,0x5C,0x21,0x57,0x67,0xB1,0xD6,0x93,
        0x4E,0x50,0xC3,0xDB,0x36,0xE8,0x9B,0x12,0x7B,0x8A,0x62,0x2B,0x12,0x0F,0x67,0x21
    };
    static const uint8_t hash[32] = {
        0xAF,0x2B,0xDB,0xE1,0xAA,0x9B,0x6E,0xC1,0xE2,0xAD,0xE1,0xD6,0x94,0xF4,0x1F,0xC7,
        0x1A,0x83,0x1D,0x02,0x68,0xE9,0x89,0x15,0x62,0x11,0x3D,0x8A,0x62,0xAD,0xD1,0xBF
    };
    static const uint8_t expR[32] = {
        0xEF,0xD4,0x8B,0x2A,0xAC,0xB6,0xA8,0xFD,0x11,0x40,0xDD,0x9C,0xD4,0x5E,0x81,0xD6,
        0x9D,0x2C,0x87,0x7B,0x56,0xAA,0xF9,0x91,0xC3,0x4D,0x0E,0xA8,0x4E,0xAF,0x37,0x16
    };
    static const uint8_t expS[32] = {
        0xF7,0xCB,0x1C,0x94,0x2D,0x65,0x7C,0x41,0xD4,0x36,0xC7,0xA1,0xB6,0xE2,0x9F,0x65,
        0xF3,0xE9,0x00,0xDB,0xB9,0xAF,0xF4,0x06,0x4D,0xC4,0xAB,0x2F,0x84,0x3A,0xCD,0xA8
    };
    XCryptographic_Key key;
    uint8_t sig[64];
    XByteArrayView privView = { priv, 32 };
    XByteArrayView hashView = { hash, 32 };
    XByteArrayView sigView;
    if (!XCryptographic_ecdsaImportPrivateKey(
            XCryptographic_EcdsaAlgorithm_NistP256, privView, &key))
        XCRYPTO_PRIMITIVE_REQUIRE(false, "ECDSA deterministic import", "导入确定性 ECDSA 密钥失败");
    sigView = XCryptographic_ecdsaSignHashInto(
        (char*)sig, sizeof(sig), key, hashView, true);
    XCRYPTO_PRIMITIVE_REQUIRE(sigView.m_data != NULL && sigView.m_size == 64,
                              "ECDSA deterministic sign", "RFC6979 确定性签名失败");
    XCRYPTO_PRIMITIVE_REQUIRE(memcmp(sig, expR, 32) == 0 && memcmp(sig + 32, expS, 32) == 0,
                              "ECDSA deterministic vector", "RFC6979 P-256 SHA-256 向量不匹配");
    {
        XCryptographic_Key pub;
        uint8_t pubBuf[65];
        XByteArrayView pubView = XCryptographic_exportPublicKeyInto(
            (char*)pubBuf, sizeof(pubBuf), key);
        XCRYPTO_PRIMITIVE_REQUIRE(pubView.m_data != NULL &&
                                  XCryptographic_ecdsaImportPublicKey(
                                      XCryptographic_EcdsaAlgorithm_NistP256, pubView, &pub),
                                  "ECDSA deterministic pubkey", "确定性 ECDSA 公钥导入失败");
        XCRYPTO_PRIMITIVE_REQUIRE(XCryptographic_ecdsaVerifyHash(
                                      pub, hashView, xcryptographic_test_view(sig, 64)),
                                  "ECDSA deterministic verify", "确定性 ECDSA 验签失败");
        XCryptographic_destroyKey(&pub);
    }
    XCryptographic_destroyKey(&key);
    XCRYPTO_PRIMITIVE_PASS("ECDSA deterministic RFC6979 vector");
    return true;
}

static bool XCryptographicPrimitive_test_secp256k1_ecdh(void)
{
    static const uint8_t private_one[32] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
    };
    static const uint8_t private_two[32] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2
    };
    static const uint8_t base_public[65] = {
        0x04,
        0x79,0xbe,0x66,0x7e,0xf9,0xdc,0xbb,0xac,0x55,0xa0,0x62,0x95,0xce,0x87,0x0b,0x07,
        0x02,0x9b,0xfc,0xdb,0x2d,0xce,0x28,0xd9,0x59,0xf2,0x81,0x5b,0x16,0xf8,0x17,0x98,
        0x48,0x3a,0xda,0x77,0x26,0xa3,0xc4,0x65,0x5d,0xa4,0xfb,0xfc,0x0e,0x11,0x08,0xa8,
        0xfd,0x17,0xb4,0x48,0xa6,0x85,0x54,0x19,0x9c,0x47,0xd0,0x8f,0xfb,0x10,0xd4,0xb8
    };
    static const uint8_t expected_shared[32] = {
        0xc6,0x04,0x7f,0x94,0x41,0xed,0x7d,0x6d,0x30,0x45,0x40,0x6e,0x95,0xc0,0x7c,0xd8,
        0x5c,0x77,0x8e,0x4b,0x8c,0xef,0x3c,0xa7,0xab,0xac,0x09,0xb9,0x5c,0x70,0x9e,0xe5
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
    XCryptographic_Key first;
    XCryptographic_Key second;
    uint8_t first_public[65];
    uint8_t second_public[65];
    uint8_t first_shared[32];
    uint8_t second_shared[32];
    uint8_t signature[64];
    XByteArrayView first_view;
    XByteArrayView second_view;

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_Secp256k1,
            xcryptographic_test_view(private_one, sizeof(private_one)), &first) &&
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_Secp256k1,
            xcryptographic_test_view(private_two, sizeof(private_two)), &second),
        "secp256k1 ECDH import", "secp256k1 私钥导入失败");
    first_view = XCryptographic_exportPublicKeyInto(
        (char*)first_public, sizeof(first_public), first);
    second_view = XCryptographic_exportPublicKeyInto(
        (char*)second_public, sizeof(second_public), second);
    XCRYPTO_PRIMITIVE_REQUIRE(first_view.m_data && first_view.m_size == 65 &&
                              memcmp(first_public, base_public, sizeof(base_public)) == 0,
                              "secp256k1 base point", "私钥 1 未导出规范基点");
    XCRYPTO_PRIMITIVE_REQUIRE(second_view.m_data && second_view.m_size == 65,
                              "secp256k1 public key", "私钥 2 公钥导出失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdhAgreeInto((char*)first_shared, sizeof(first_shared), first,
                                     second_view).m_data &&
        XCryptographic_ecdhAgreeInto((char*)second_shared, sizeof(second_shared), second,
                                     first_view).m_data &&
        memcmp(first_shared, second_shared, sizeof(first_shared)) == 0 &&
        memcmp(first_shared, expected_shared, sizeof(expected_shared)) == 0,
        "secp256k1 ECDH vector", "共享密钥不匹配 2G 的 x 坐标");
    second_public[1] ^= 1u;
    XCRYPTO_PRIMITIVE_REQUIRE(!XCryptographic_ecdhImportPublicKey(
                                  XCryptographic_EcdhAlgorithm_Secp256k1,
                                  xcryptographic_test_view(second_public, sizeof(second_public)),
                                  &(XCryptographic_Key){0}),
                              "secp256k1 public-key rejection", "非法曲线点未被拒绝");
    XCryptographic_destroyKey(&first);
    XCryptographic_destroyKey(&second);
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_Secp256k1,
            xcryptographic_test_view(private_one, sizeof(private_one)), &first) &&
        XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_Secp256k1,
            xcryptographic_test_view(base_public, sizeof(base_public)), &second),
        "secp256k1 ECDSA import", "secp256k1 ECDSA 密钥导入失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdsaVerifyHash(
            second, xcryptographic_test_view(hash, sizeof(hash)),
            xcryptographic_test_view(openssl_signature, sizeof(openssl_signature))),
        "secp256k1 ECDSA OpenSSL vector", "OpenSSL 签名未通过验签");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdsaSignHashInto(
            (char*)signature, sizeof(signature), first,
            xcryptographic_test_view(hash, sizeof(hash)), true).m_data &&
        XCryptographic_ecdsaVerifyHash(
            second, xcryptographic_test_view(hash, sizeof(hash)),
            xcryptographic_test_view(signature, sizeof(signature))),
        "secp256k1 deterministic ECDSA", "确定性签名或验签失败");
    signature[0] ^= 1u;
    XCRYPTO_PRIMITIVE_REQUIRE(!XCryptographic_ecdsaVerifyHash(
                                  second, xcryptographic_test_view(hash, sizeof(hash)),
                                  xcryptographic_test_view(signature, sizeof(signature))),
                              "secp256k1 ECDSA rejection", "篡改签名未被拒绝");
    XCryptographic_destroyKey(&first);
    XCryptographic_destroyKey(&second);
    XCRYPTO_PRIMITIVE_PASS("secp256k1 ECDH/ECDSA vectors");
    return true;
}

static bool XCryptographicPrimitive_test_brainpool_p256r1_ecdh(void)
{
    static const uint8_t private_one[32] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
    };
    static const uint8_t private_two[32] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2
    };
    static const uint8_t base_public[65] = {
        0x04,
        0x8b,0xd2,0xae,0xb9,0xcb,0x7e,0x57,0xcb,0x2c,0x4b,0x48,0x2f,0xfc,0x81,0xb7,0xaf,
        0xb9,0xde,0x27,0xe1,0xe3,0xbd,0x23,0xc2,0x3a,0x44,0x53,0xbd,0x9a,0xce,0x32,0x62,
        0x54,0x7e,0xf8,0x35,0xc3,0xda,0xc4,0xfd,0x97,0xf8,0x46,0x1a,0x14,0x61,0x1d,0xc9,
        0xc2,0x77,0x45,0x13,0x2d,0xed,0x8e,0x54,0x5c,0x1d,0x54,0xc7,0x2f,0x04,0x69,0x97
    };
    static const uint8_t expected_shared[32] = {
        0x74,0x3c,0xf1,0xb8,0xb5,0xcd,0x4f,0x2e,0xb5,0x5f,0x8a,0xa3,0x69,0x59,0x3a,0xc4,
        0x36,0xef,0x04,0x41,0x66,0x69,0x9e,0x37,0xd5,0x1a,0x14,0xc2,0xce,0x13,0xea,0x0e
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
    XCryptographic_Key first;
    XCryptographic_Key second;
    uint8_t first_public[65];
    uint8_t second_public[65];
    uint8_t first_shared[32];
    uint8_t second_shared[32];
    uint8_t signature[64];
    XByteArrayView first_view;
    XByteArrayView second_view;

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_BrainpoolP256r1,
            xcryptographic_test_view(private_one, sizeof(private_one)), &first) &&
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_BrainpoolP256r1,
            xcryptographic_test_view(private_two, sizeof(private_two)), &second),
        "brainpoolP256r1 ECDH import", "brainpoolP256r1 私钥导入失败");
    first_view = XCryptographic_exportPublicKeyInto(
        (char*)first_public, sizeof(first_public), first);
    second_view = XCryptographic_exportPublicKeyInto(
        (char*)second_public, sizeof(second_public), second);
    XCRYPTO_PRIMITIVE_REQUIRE(first_view.m_data && first_view.m_size == 65 &&
                              memcmp(first_public, base_public, sizeof(base_public)) == 0,
                              "brainpoolP256r1 base point", "私钥 1 未导出 RFC 5639 基点");
    XCRYPTO_PRIMITIVE_REQUIRE(second_view.m_data && second_view.m_size == 65,
                              "brainpoolP256r1 public key", "私钥 2 公钥导出失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdhAgreeInto((char*)first_shared, sizeof(first_shared), first,
                                     second_view).m_data &&
        XCryptographic_ecdhAgreeInto((char*)second_shared, sizeof(second_shared), second,
                                     first_view).m_data &&
        memcmp(first_shared, second_shared, sizeof(first_shared)) == 0 &&
        memcmp(first_shared, expected_shared, sizeof(expected_shared)) == 0,
        "brainpoolP256r1 ECDH vector", "共享密钥不匹配 OpenSSL 2G 的 x 坐标");
    second_public[1] ^= 1u;
    XCRYPTO_PRIMITIVE_REQUIRE(!XCryptographic_ecdhImportPublicKey(
                                  XCryptographic_EcdhAlgorithm_BrainpoolP256r1,
                                  xcryptographic_test_view(second_public, sizeof(second_public)),
                                  &(XCryptographic_Key){0}),
                              "brainpoolP256r1 public-key rejection", "非法曲线点未被拒绝");
    XCryptographic_destroyKey(&first);
    XCryptographic_destroyKey(&second);
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_BrainpoolP256r1,
            xcryptographic_test_view(private_one, sizeof(private_one)), &first) &&
        XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_BrainpoolP256r1,
            xcryptographic_test_view(base_public, sizeof(base_public)), &second),
        "brainpoolP256r1 ECDSA import", "brainpoolP256r1 ECDSA 密钥导入失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdsaVerifyHash(
            second, xcryptographic_test_view(hash, sizeof(hash)),
            xcryptographic_test_view(openssl_signature, sizeof(openssl_signature))),
        "brainpoolP256r1 ECDSA OpenSSL vector", "OpenSSL 签名未通过验签");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdsaSignHashInto(
            (char*)signature, sizeof(signature), first,
            xcryptographic_test_view(hash, sizeof(hash)), true).m_data &&
        XCryptographic_ecdsaVerifyHash(
            second, xcryptographic_test_view(hash, sizeof(hash)),
            xcryptographic_test_view(signature, sizeof(signature))),
        "brainpoolP256r1 deterministic ECDSA", "确定性签名或验签失败");
    signature[0] ^= 1u;
    XCRYPTO_PRIMITIVE_REQUIRE(!XCryptographic_ecdsaVerifyHash(
                                  second, xcryptographic_test_view(hash, sizeof(hash)),
                                  xcryptographic_test_view(signature, sizeof(signature))),
                              "brainpoolP256r1 ECDSA rejection", "篡改签名未被拒绝");
    XCryptographic_destroyKey(&first);
    XCryptographic_destroyKey(&second);
    XCRYPTO_PRIMITIVE_PASS("brainpoolP256r1 ECDH/ECDSA OpenSSL vectors");
    return true;
}

static bool XCryptographicPrimitive_test_p384_ecdh(void)
{
    static const uint8_t private_one[48] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
    };
    static const uint8_t private_two[48] = {
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2
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
    static const uint8_t twice_public[97] = {
        0x04,
        0x08,0xd9,0x99,0x05,0x7b,0xa3,0xd2,0xd9,0x69,0x26,0x00,0x45,0xc5,0x5b,0x97,0xf0,
        0x89,0x02,0x59,0x59,0xa6,0xf4,0x34,0xd6,0x51,0xd2,0x07,0xd1,0x9f,0xb9,0x6e,0x9e,
        0x4f,0xe0,0xe8,0x6e,0xbe,0x0e,0x64,0xf8,0x5b,0x96,0xa9,0xc7,0x52,0x95,0xdf,0x61,
        0x8e,0x80,0xf1,0xfa,0x5b,0x1b,0x3c,0xed,0xb7,0xbf,0xe8,0xdf,0xfd,0x6d,0xba,0x74,
        0xb2,0x75,0xd8,0x75,0xbc,0x6c,0xc4,0x3e,0x90,0x4e,0x50,0x5f,0x25,0x6a,0xb4,0x25,
        0x5f,0xfd,0x43,0xe9,0x4d,0x39,0xe2,0x2d,0x61,0x50,0x1e,0x70,0x0a,0x94,0x0e,0x80
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
    XCryptographic_Key first;
    XCryptographic_Key second;
    uint8_t first_public[97];
    uint8_t second_public[97];
    uint8_t first_shared[48];
    uint8_t second_shared[48];
    uint8_t signature[96];
    uint8_t deterministic_signature[96];
    XByteArrayView first_view;
    XByteArrayView second_view;

    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_NistP384,
            xcryptographic_test_view(private_one, sizeof(private_one)), &first) &&
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_NistP384,
            xcryptographic_test_view(private_two, sizeof(private_two)), &second),
        "P-384 ECDH import", "P-384 私钥导入失败");
    first_view = XCryptographic_exportPublicKeyInto(
        (char*)first_public, sizeof(first_public), first);
    second_view = XCryptographic_exportPublicKeyInto(
        (char*)second_public, sizeof(second_public), second);
    XCRYPTO_PRIMITIVE_REQUIRE(first_view.m_data && first_view.m_size == 97 &&
                              memcmp(first_public, base_public, sizeof(base_public)) == 0,
                              "P-384 base point", "私钥 1 未导出标准 P-384 基点");
    XCRYPTO_PRIMITIVE_REQUIRE(second_view.m_data && second_view.m_size == 97 &&
                              memcmp(second_public, twice_public, sizeof(twice_public)) == 0,
                              "P-384 double point", "私钥 2 未导出 OpenSSL 2G 点");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdhAgreeInto((char*)first_shared, sizeof(first_shared), first,
                                     second_view).m_data &&
        XCryptographic_ecdhAgreeInto((char*)second_shared, sizeof(second_shared), second,
                                     first_view).m_data &&
        memcmp(first_shared, second_shared, sizeof(first_shared)) == 0 &&
        memcmp(first_shared, twice_public + 1, sizeof(first_shared)) == 0,
        "P-384 ECDH vector", "共享密钥不匹配 OpenSSL 2G 的 x 坐标");
    second_public[1] ^= 1u;
    XCRYPTO_PRIMITIVE_REQUIRE(!XCryptographic_ecdhImportPublicKey(
                                  XCryptographic_EcdhAlgorithm_NistP384,
                                  xcryptographic_test_view(second_public, sizeof(second_public)),
                                  &(XCryptographic_Key){0}),
                              "P-384 public-key rejection", "非法曲线点未被拒绝");
    XCryptographic_destroyKey(&first);
    XCryptographic_destroyKey(&second);
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_NistP384,
            xcryptographic_test_view(private_one, sizeof(private_one)), &first) &&
        XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_NistP384,
            xcryptographic_test_view(base_public, sizeof(base_public)), &second),
        "P-384 ECDSA import", "P-384 ECDSA 密钥导入失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdsaVerifyHash(
            second, xcryptographic_test_view(hash, sizeof(hash)),
            xcryptographic_test_view(openssl_signature, sizeof(openssl_signature))),
        "P-384 ECDSA OpenSSL vector", "外部 P-384/SHA-384 签名未通过验签");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdsaSignHashInto(
            (char*)signature, sizeof(signature), first,
            xcryptographic_test_view(hash, sizeof(hash)), false).m_data &&
        XCryptographic_ecdsaVerifyHash(
            second, xcryptographic_test_view(hash, sizeof(hash)),
            xcryptographic_test_view(signature, sizeof(signature))),
        "P-384 random ECDSA", "随机签名或验签失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdsaSignHashInto(
            (char*)signature, sizeof(signature), first,
            xcryptographic_test_view(hash, sizeof(hash)), true).m_data &&
        XCryptographic_ecdsaSignHashInto(
            (char*)deterministic_signature, sizeof(deterministic_signature), first,
            xcryptographic_test_view(hash, sizeof(hash)), true).m_data &&
        memcmp(signature, deterministic_signature, sizeof(signature)) == 0 &&
        XCryptographic_ecdsaVerifyHash(
            second, xcryptographic_test_view(hash, sizeof(hash)),
            xcryptographic_test_view(signature, sizeof(signature))),
        "P-384 deterministic ECDSA", "RFC 6979 签名不可重复或验签失败");
    signature[0] ^= 1u;
    XCRYPTO_PRIMITIVE_REQUIRE(!XCryptographic_ecdsaVerifyHash(
                                  second, xcryptographic_test_view(hash, sizeof(hash)),
                                  xcryptographic_test_view(signature, sizeof(signature))),
                              "P-384 ECDSA rejection", "篡改签名未被拒绝");
    XCryptographic_destroyKey(&first);
    XCryptographic_destroyKey(&second);
    XCRYPTO_PRIMITIVE_PASS("P-384 ECDH/ECDSA OpenSSL vectors");
    return true;
}

static bool XCryptographicPrimitive_test_p521_ecdh(void)
{
    static const uint8_t private_one[66] = { [65] = 1 };
    static const uint8_t private_two[66] = { [65] = 2 };
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
    static const uint8_t twice_public[133] = {
        0x04,
        0x00,0x43,0x3c,0x21,0x90,0x24,0x27,0x7e,0x7e,0x68,0x2f,0xcb,0x28,0x81,0x48,0xc2,
        0x82,0x74,0x74,0x03,0x27,0x9b,0x1c,0xcc,0x06,0x35,0x2c,0x6e,0x55,0x05,0xd7,0x69,
        0xbe,0x97,0xb3,0xb2,0x04,0xda,0x6e,0xf5,0x55,0x07,0xaa,0x10,0x4a,0x3a,0x35,0xc5,
        0xaf,0x41,0xcf,0x2f,0xa3,0x64,0xd6,0x0f,0xd9,0x67,0xf4,0x3e,0x39,0x33,0xba,0x6d,0x78,0x3d,
        0x00,0xf4,0xbb,0x8c,0xc7,0xf8,0x6d,0xb2,0x67,0x00,0xa7,0xf3,0xec,0xee,0xee,0xd3,
        0xf0,0xb5,0xc6,0xb5,0x10,0x7c,0x4d,0xa9,0x77,0x40,0xab,0x21,0xa2,0x99,0x06,0xc4,
        0x2d,0xbb,0xb3,0xe3,0x77,0xde,0x9f,0x25,0x1f,0x6b,0x93,0x93,0x7f,0xa9,0x9a,0x32,
        0x48,0xf4,0xea,0xfc,0xbe,0x95,0xed,0xc0,0xf4,0xf7,0x1b,0xe3,0x56,0xd6,0x61,0xf4,
        0x1b,0x02
    };
    static const uint8_t deterministic_signature_expected[132] = {
        0x00,0xda,0xe3,0xed,0x25,0x84,0x40,0x8b,0xd4,0x41,0x05,0x51,0xfe,0xb0,0x35,0x5a,
        0xc5,0x4a,0x15,0x12,0x68,0x14,0x75,0xa6,0x83,0xbf,0xd7,0x88,0x3a,0xf7,0x39,0x1a,
        0xb7,0x69,0x7d,0x57,0x0e,0xf6,0xe6,0x44,0x29,0x72,0xb8,0xd5,0x3d,0xa3,0x17,0xf4,
        0x4e,0xdc,0x51,0x6b,0xc1,0x32,0x94,0x88,0xe0,0xd5,0x92,0xcf,0x0f,0xfa,0xe1,0x41,
        0x96,0x3d,0x00,0xed,0xbf,0xc5,0xaf,0x28,0x23,0x62,0x23,0x0a,0x46,0xd2,0xac,0x74,
        0xe3,0x83,0xdf,0xdd,0x10,0x6b,0x52,0x78,0xa4,0x71,0x64,0xeb,0xfc,0x9c,0xea,0x0e,
        0x61,0xfb,0xe8,0x97,0x05,0x4f,0x91,0x62,0xc9,0xd1,0xd2,0x4f,0x70,0xf2,0xbc,0x46,
        0x12,0x4c,0xcc,0x46,0x7c,0x4c,0xd0,0xec,0xd2,0x1d,0xc4,0x4b,0xc2,0x76,0x8a,0x9b,
        0xf9,0xcf,0xfe,0x6b
    };
    XCryptographic_Key first, second;
    uint8_t first_public[133], second_public[133];
    uint8_t first_shared[66], second_shared[66];
    uint8_t hash[64];
    uint8_t signature[132], deterministic_signature[132];
    XByteArrayView first_view, second_view;
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdhImportPrivateKey(XCryptographic_EcdhAlgorithm_NistP521,
            xcryptographic_test_view(private_one, sizeof(private_one)), &first) &&
        XCryptographic_ecdhImportPrivateKey(XCryptographic_EcdhAlgorithm_NistP521,
            xcryptographic_test_view(private_two, sizeof(private_two)), &second),
        "P-521 ECDH import", "P-521 私钥导入失败");
    first_view = XCryptographic_exportPublicKeyInto((char*)first_public, sizeof(first_public), first);
    second_view = XCryptographic_exportPublicKeyInto((char*)second_public, sizeof(second_public), second);
    XCRYPTO_PRIMITIVE_REQUIRE(first_view.m_data && memcmp(first_public, base_public, sizeof(base_public)) == 0,
                              "P-521 base point", "私钥 1 未导出标准 P-521 基点");
    XCRYPTO_PRIMITIVE_REQUIRE(second_view.m_data && memcmp(second_public, twice_public, sizeof(twice_public)) == 0,
                              "P-521 double point", "私钥 2 未导出 OpenSSL 2G 点");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdhAgreeInto((char*)first_shared, sizeof(first_shared), first, second_view).m_data &&
        XCryptographic_ecdhAgreeInto((char*)second_shared, sizeof(second_shared), second, first_view).m_data &&
        memcmp(first_shared, second_shared, sizeof(first_shared)) == 0 &&
        memcmp(first_shared, twice_public + 1, sizeof(first_shared)) == 0,
        "P-521 ECDH vector", "共享密钥不匹配 OpenSSL 2G 的 x 坐标");
    second_public[1] ^= 1u;
    XCRYPTO_PRIMITIVE_REQUIRE(!XCryptographic_ecdhImportPublicKey(
        XCryptographic_EcdhAlgorithm_NistP521,
        xcryptographic_test_view(second_public, sizeof(second_public)), &(XCryptographic_Key){0}),
        "P-521 public-key rejection", "非法曲线点未被拒绝");
    memset(hash, 0x5a, sizeof(hash));
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_NistP521,
            xcryptographic_test_view(private_one, sizeof(private_one)), &first) &&
        XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_NistP521,
            xcryptographic_test_view(base_public, sizeof(base_public)), &second) &&
        XCryptographic_ecdsaSignHashInto(
            (char*)signature, sizeof(signature), first,
            xcryptographic_test_view(hash, sizeof(hash)), false).m_data &&
        XCryptographic_ecdsaVerifyHash(
            second, xcryptographic_test_view(hash, sizeof(hash)),
            xcryptographic_test_view(signature, sizeof(signature))),
        "P-521 ECDSA", "随机签名或验签失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdsaSignHashInto(
            (char*)signature, sizeof(signature), first,
            xcryptographic_test_view(hash, sizeof(hash)), true).m_data &&
        XCryptographic_ecdsaSignHashInto(
            (char*)deterministic_signature, sizeof(deterministic_signature), first,
            xcryptographic_test_view(hash, sizeof(hash)), true).m_data &&
        memcmp(signature, deterministic_signature, sizeof(signature)) == 0 &&
        memcmp(signature, deterministic_signature_expected, sizeof(signature)) == 0 &&
        XCryptographic_ecdsaVerifyHash(
            second, xcryptographic_test_view(hash, sizeof(hash)),
            xcryptographic_test_view(signature, sizeof(signature))),
        "P-521 deterministic ECDSA", "RFC 6979 签名不可重复或验签失败");
    signature[0] ^= 1u;
    XCRYPTO_PRIMITIVE_REQUIRE(!XCryptographic_ecdsaVerifyHash(
                                  second, xcryptographic_test_view(hash, sizeof(hash)),
                                  xcryptographic_test_view(signature, sizeof(signature))),
                              "P-521 ECDSA rejection", "篡改签名未被拒绝");
    XCryptographic_destroyKey(&first);
    XCryptographic_destroyKey(&second);
    XCRYPTO_PRIMITIVE_PASS("P-521 ECDH OpenSSL vectors");
    return true;
}

static bool XCryptographicPrimitive_test_brainpool_p512r1_ecdh(void)
{
    static const uint8_t private_one[64] = { [63] = 1 };
    static const uint8_t private_two[64] = { [63] = 2 };
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
    static const uint8_t twice_public[129] = {
        0x04,
        0x9f,0x49,0x45,0xf6,0x80,0xed,0xf9,0x80,0x0a,0x63,0x28,0x57,0x58,0xf3,0x99,0xb3,
        0xd1,0x8d,0x81,0x41,0xb8,0xa1,0x80,0x64,0xa3,0x0d,0x30,0x35,0xf4,0xcb,0x65,0x81,
        0x95,0x78,0x77,0xf3,0xa8,0xf0,0xf7,0x25,0x97,0x11,0x6e,0x70,0x29,0x15,0xa4,0xf4,
        0xf6,0x98,0xf4,0x04,0x08,0x9a,0x4c,0xc5,0x08,0x04,0x47,0xde,0xf0,0x2f,0x48,0x50,
        0x6d,0x6b,0x4b,0x18,0x8b,0x69,0x9c,0x56,0x49,0x82,0x6b,0x71,0x62,0x92,0xf2,0x9d,
        0x14,0x9c,0xe1,0x23,0x8d,0x3f,0x1e,0x0f,0x5a,0x2c,0x36,0x6b,0x03,0xe5,0xd1,0xb2,
        0xfd,0xf9,0x9b,0xb1,0x70,0x9c,0x70,0x0f,0xa5,0xc3,0xb6,0x02,0xb0,0x96,0x0c,0xbf,
        0x63,0xa4,0x2e,0x41,0x81,0xfd,0x92,0x9c,0xe2,0x69,0xad,0x21,0xbe,0x59,0x2e,0x71
    };
    XCryptographic_Key first, second;
    uint8_t first_public[129], second_public[129], first_shared[64], second_shared[64];
    XByteArrayView first_view, second_view;
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_BrainpoolP512r1,
            xcryptographic_test_view(private_one, sizeof(private_one)), &first) &&
        XCryptographic_ecdhImportPrivateKey(
            XCryptographic_EcdhAlgorithm_BrainpoolP512r1,
            xcryptographic_test_view(private_two, sizeof(private_two)), &second),
        "brainpoolP512r1 ECDH import", "brainpoolP512r1 私钥导入失败");
    first_view = XCryptographic_exportPublicKeyInto((char*)first_public, sizeof(first_public), first);
    second_view = XCryptographic_exportPublicKeyInto((char*)second_public, sizeof(second_public), second);
    XCRYPTO_PRIMITIVE_REQUIRE(first_view.m_data &&
                              memcmp(first_public, base_public, sizeof(base_public)) == 0,
                              "brainpoolP512r1 base point", "私钥 1 未导出 RFC 5639 基点");
    XCRYPTO_PRIMITIVE_REQUIRE(second_view.m_data &&
                              memcmp(second_public, twice_public, sizeof(twice_public)) == 0,
                              "brainpoolP512r1 double point", "私钥 2 未导出独立库 2G 点");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdhAgreeInto((char*)first_shared, sizeof(first_shared), first, second_view).m_data &&
        XCryptographic_ecdhAgreeInto((char*)second_shared, sizeof(second_shared), second, first_view).m_data &&
        memcmp(first_shared, second_shared, sizeof(first_shared)) == 0 &&
        memcmp(first_shared, twice_public + 1, sizeof(first_shared)) == 0,
        "brainpoolP512r1 ECDH vector", "共享密钥不匹配 2G 的 x 坐标");
    second_public[1] ^= 1u;
    XCRYPTO_PRIMITIVE_REQUIRE(!XCryptographic_ecdhImportPublicKey(
        XCryptographic_EcdhAlgorithm_BrainpoolP512r1,
        xcryptographic_test_view(second_public, sizeof(second_public)), &(XCryptographic_Key){0}),
        "brainpoolP512r1 public-key rejection", "非法曲线点未被拒绝");
    {
        uint8_t hash[64];
        uint8_t signature[128];
        uint8_t deterministic_signature[128];
        memset(hash, 0x5a, sizeof(hash));
        XCRYPTO_PRIMITIVE_REQUIRE(
            XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_BrainpoolP512r1,
                xcryptographic_test_view(private_one, sizeof(private_one)), &first) &&
            XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_BrainpoolP512r1,
                xcryptographic_test_view(base_public, sizeof(base_public)), &second) &&
            XCryptographic_ecdsaSignHashInto(
                (char*)signature, sizeof(signature), first,
                xcryptographic_test_view(hash, sizeof(hash)), false).m_data &&
            XCryptographic_ecdsaVerifyHash(
                second, xcryptographic_test_view(hash, sizeof(hash)),
                xcryptographic_test_view(signature, sizeof(signature))),
            "brainpoolP512r1 ECDSA", "随机签名或验签失败");
        XCRYPTO_PRIMITIVE_REQUIRE(
            XCryptographic_ecdsaSignHashInto(
                (char*)signature, sizeof(signature), first,
                xcryptographic_test_view(hash, sizeof(hash)), true).m_data &&
            XCryptographic_ecdsaSignHashInto(
                (char*)deterministic_signature, sizeof(deterministic_signature), first,
                xcryptographic_test_view(hash, sizeof(hash)), true).m_data &&
            memcmp(signature, deterministic_signature, sizeof(signature)) == 0 &&
            XCryptographic_ecdsaVerifyHash(
                second, xcryptographic_test_view(hash, sizeof(hash)),
                xcryptographic_test_view(signature, sizeof(signature))),
            "brainpoolP512r1 deterministic ECDSA", "确定性签名或验签失败");
        signature[0] ^= 1u;
        XCRYPTO_PRIMITIVE_REQUIRE(!XCryptographic_ecdsaVerifyHash(
                                      second, xcryptographic_test_view(hash, sizeof(hash)),
                                      xcryptographic_test_view(signature, sizeof(signature))),
                                  "brainpoolP512r1 ECDSA rejection", "篡改签名未被拒绝");
    }
    XCryptographic_destroyKey(&first);
    XCryptographic_destroyKey(&second);
    XCRYPTO_PRIMITIVE_PASS("brainpoolP512r1 ECDH/ECDSA RFC 5639 vectors");
    return true;
}

static bool XCryptographicPrimitive_test_brainpool_p384_ecdh(void)
{
    static const uint8_t private_one[48] = { [47] = 1 };
    static const uint8_t private_two[48] = { [47] = 2 };
    static const uint8_t base_public[97] = {
        0x04,
        0x1d,0x1c,0x64,0xf0,0x68,0xcf,0x45,0xff,0xa2,0xa6,0x3a,0x81,0xb7,0xc1,0x3f,0x6b,
        0x88,0x47,0xa3,0xe7,0x7e,0xf1,0x4f,0xe3,0xdb,0x7f,0xca,0xfe,0x0c,0xbd,0x10,0xe8,
        0xe8,0x26,0xe0,0x34,0x36,0xd6,0x46,0xaa,0xef,0x87,0xb2,0xe2,0x47,0xd4,0xaf,0x1e,
        0x8a,0xbe,0x1d,0x75,0x20,0xf9,0xc2,0xa4,0x5c,0xb1,0xeb,0x8e,0x95,0xcf,0xd5,0x52,
        0x62,0xb7,0x0b,0x29,0xfe,0xec,0x58,0x64,0xe1,0x9c,0x05,0x4f,0xf9,0x91,0x29,0x28,
        0x0e,0x46,0x46,0x21,0x77,0x91,0x81,0x11,0x42,0x82,0x03,0x41,0x26,0x3c,0x53,0x15
    };
    XCryptographic_Key first, second;
    uint8_t first_public[97], second_public[97], first_shared[48], second_shared[48];
    XByteArrayView first_view, second_view;
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdhImportPrivateKey(XCryptographic_EcdhAlgorithm_BrainpoolP384r1,
            xcryptographic_test_view(private_one, sizeof(private_one)), &first) &&
        XCryptographic_ecdhImportPrivateKey(XCryptographic_EcdhAlgorithm_BrainpoolP384r1,
            xcryptographic_test_view(private_two, sizeof(private_two)), &second),
        "brainpoolP384r1 ECDH import", "私钥导入失败");
    first_view = XCryptographic_exportPublicKeyInto((char*)first_public, sizeof(first_public), first);
    second_view = XCryptographic_exportPublicKeyInto((char*)second_public, sizeof(second_public), second);
    XCRYPTO_PRIMITIVE_REQUIRE(first_view.m_data && memcmp(first_public, base_public, sizeof(base_public)) == 0,
                              "brainpoolP384r1 base point", "私钥 1 未导出 RFC 5639 基点");
    XCRYPTO_PRIMITIVE_REQUIRE(second_view.m_data &&
                              XCryptographic_ecdhAgreeInto((char*)first_shared, sizeof(first_shared), first, second_view).m_data &&
                              XCryptographic_ecdhAgreeInto((char*)second_shared, sizeof(second_shared), second, first_view).m_data &&
                              memcmp(first_shared, second_shared, sizeof(first_shared)) == 0,
                              "brainpoolP384r1 ECDH", "双方共享密钥不一致");
    second_public[1] ^= 1u;
    XCRYPTO_PRIMITIVE_REQUIRE(!XCryptographic_ecdhImportPublicKey(
                                  XCryptographic_EcdhAlgorithm_BrainpoolP384r1,
                                  xcryptographic_test_view(second_public, sizeof(second_public)),
                                  &(XCryptographic_Key){0}),
                              "brainpoolP384r1 public-key rejection", "非法曲线点未被拒绝");
    {
        uint8_t hash[48], signature[96], deterministic_signature[96];
        memset(hash, 0x5a, sizeof(hash));
        XCRYPTO_PRIMITIVE_REQUIRE(
            XCryptographic_ecdsaImportPrivateKey(XCryptographic_EcdsaAlgorithm_BrainpoolP384r1,
                xcryptographic_test_view(private_one, sizeof(private_one)), &first) &&
            XCryptographic_ecdsaImportPublicKey(XCryptographic_EcdsaAlgorithm_BrainpoolP384r1,
                xcryptographic_test_view(base_public, sizeof(base_public)), &second) &&
            XCryptographic_ecdsaSignHashInto(
                (char*)signature, sizeof(signature), first,
                xcryptographic_test_view(hash, sizeof(hash)), false).m_data &&
            XCryptographic_ecdsaVerifyHash(
                second, xcryptographic_test_view(hash, sizeof(hash)),
                xcryptographic_test_view(signature, sizeof(signature))),
            "brainpoolP384r1 ECDSA", "签名或验签失败");
        XCRYPTO_PRIMITIVE_REQUIRE(
            XCryptographic_ecdsaSignHashInto(
                (char*)signature, sizeof(signature), first,
                xcryptographic_test_view(hash, sizeof(hash)), true).m_data &&
            XCryptographic_ecdsaSignHashInto(
                (char*)deterministic_signature, sizeof(deterministic_signature), first,
                xcryptographic_test_view(hash, sizeof(hash)), true).m_data &&
            memcmp(signature, deterministic_signature, sizeof(signature)) == 0 &&
            XCryptographic_ecdsaVerifyHash(
                second, xcryptographic_test_view(hash, sizeof(hash)),
                xcryptographic_test_view(signature, sizeof(signature))),
            "brainpoolP384r1 deterministic ECDSA", "确定性签名或验签失败");
        signature[0] ^= 1u;
        XCRYPTO_PRIMITIVE_REQUIRE(!XCryptographic_ecdsaVerifyHash(
                                      second, xcryptographic_test_view(hash, sizeof(hash)),
                                      xcryptographic_test_view(signature, sizeof(signature))),
                                  "brainpoolP384r1 ECDSA rejection", "篡改签名未被拒绝");
    }
    XCryptographic_destroyKey(&first); XCryptographic_destroyKey(&second);
    XCRYPTO_PRIMITIVE_PASS("brainpoolP384r1 ECDH/ECDSA RFC 5639 vectors");
    return true;
}

static bool XCryptographicPrimitive_test_x448(void)
{
    static const uint8_t private_four[56] = { [0] = 4 };
    static const uint8_t private_eight[56] = { [0] = 8 };
    static const uint8_t public_four[56] = {
        0x3f,0x48,0x2c,0x8a,0x9f,0x19,0xb0,0x1e,0x6c,0x46,0xee,0x97,0x11,0xd9,0xdc,0x14,
        0xfd,0x4b,0xf6,0x7a,0xf3,0x07,0x65,0xc2,0xae,0x2b,0x84,0x6a,0x4d,0x23,0xa8,0xcd,
        0x0d,0xb8,0x97,0x08,0x62,0x39,0x49,0x2c,0xaf,0x35,0x0b,0x51,0xf8,0x33,0x86,0x8b,
        0x9b,0xc2,0xb3,0xbc,0xa9,0xcf,0x41,0x13
    };
    static const uint8_t public_eight[56] = {
        0xc6,0x65,0xf6,0x55,0xd4,0x84,0x77,0x0e,0x52,0x46,0x16,0xe7,0x98,0x98,0x76,0x48,
        0xa9,0xd3,0xdc,0x4d,0x0c,0x69,0x59,0x9f,0x6c,0xfc,0xcb,0x8a,0x0d,0x96,0x6a,0xd1,
        0xde,0x30,0xed,0xfc,0xdf,0xe8,0xec,0x12,0x05,0x1c,0x65,0x6b,0x04,0x63,0xe4,0xf8,
        0x1e,0x66,0x2f,0xfa,0x53,0x71,0xf9,0x6c
    };
    static const uint8_t shared_four_eight[56] = {
        0xa0,0x63,0x86,0x09,0x70,0x12,0x61,0x17,0xcb,0x98,0x99,0x4d,0x86,0x80,0x87,0x5e,
        0xa2,0xcb,0xaf,0x2c,0xc4,0x49,0x8a,0xe6,0x97,0x73,0x76,0x02,0x15,0xb1,0xa7,0xc4,
        0xb8,0xc3,0xd6,0x4e,0x53,0x73,0x60,0x91,0xd6,0xfb,0x99,0xfb,0x21,0xf0,0x77,0x7e,
        0xb0,0x07,0x2b,0x1a,0xd7,0xa3,0x1d,0x75
    };
    XCryptographic_Key first, second;
    uint8_t first_public[56], second_public[56], first_shared[56], second_shared[56];
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdhImportPrivateKey(XCryptographic_EcdhAlgorithm_X448,xcryptographic_test_view(private_four,sizeof(private_four)),&first) &&
        XCryptographic_ecdhImportPrivateKey(XCryptographic_EcdhAlgorithm_X448,xcryptographic_test_view(private_eight,sizeof(private_eight)),&second),
        "X448 import", "X448 私钥导入失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_exportPublicKeyInto((char*)first_public,sizeof(first_public),first).m_data &&
        XCryptographic_exportPublicKeyInto((char*)second_public,sizeof(second_public),second).m_data,
        "X448 public-key export", "X448 公钥导出失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        memcmp(first_public,public_four,sizeof(public_four))==0 &&
        memcmp(second_public,public_eight,sizeof(public_eight))==0,
        "X448 RFC 7748 public vectors", "X448 公钥向量不匹配");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_ecdhAgreeInto((char*)first_shared,sizeof(first_shared),first,xcryptographic_test_view(second_public,sizeof(second_public))).m_data &&
        XCryptographic_ecdhAgreeInto((char*)second_shared,sizeof(second_shared),second,xcryptographic_test_view(first_public,sizeof(first_public))).m_data &&
        memcmp(first_shared,second_shared,sizeof(first_shared))==0 &&
        memcmp(first_shared,shared_four_eight,sizeof(shared_four_eight))==0,
        "X448 ECDH", "X448 双方共享密钥不一致");
    XCryptographic_destroyKey(&first); XCryptographic_destroyKey(&second);
    XCRYPTO_PRIMITIVE_PASS("X448 RFC 7748 vectors"); return true;
}

static bool XCryptographicPrimitive_test_lms(void)
{
    static const uint8_t identifier[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };
    static const uint8_t message[] = "XinYueC LMS RFC8554 layout test";
    static const uint8_t expected_candidate[32] = {
        0x7b, 0xee, 0x87, 0x3b, 0x69, 0x63, 0x48, 0x74,
        0x9c, 0xc8, 0x8e, 0x20, 0x87, 0x3e, 0xef, 0xa7,
        0x16, 0xfa, 0x98, 0x92, 0x20, 0xc4, 0x1c, 0xd9,
        0x47, 0x1f, 0xee, 0xc4, 0xb7, 0x05, 0x41, 0x5b
    };
    static const uint8_t expected_root[32] = {
        0x39, 0xcd, 0xc4, 0x80, 0x6c, 0x10, 0x18, 0x91,
        0xed, 0xc6, 0x05, 0x55, 0xc2, 0x7e, 0xde, 0xec,
        0xf7, 0xc1, 0xf4, 0x0d, 0xa8, 0x27, 0x3b, 0x47,
        0x6f, 0x0c, 0xf0, 0x99, 0x30, 0x16, 0xb5, 0xbc
    };
    uint8_t lmots_signature[1124];
    uint8_t lms_signature[1452];
    uint8_t candidate[32];
    size_t index;

    memset(lmots_signature, 0, sizeof(lmots_signature));
    lmots_signature[3] = 4;
    for (index = 32; index < sizeof(lmots_signature); ++index) {
        lmots_signature[index] = (uint8_t)(index * 37u + 11u);
    }
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_lmotsCalculatePublicKeyCandidate(
            xcryptographic_test_view(identifier, sizeof(identifier)), 42,
            xcryptographic_test_view(message, sizeof(message) - 1),
            xcryptographic_test_view(lmots_signature, sizeof(lmots_signature)),
            candidate, sizeof(candidate)) &&
        memcmp(candidate, expected_candidate, sizeof(candidate)) == 0,
        "LM-OTS candidate", "RFC 8554 SHA256_N32_W8 候选公钥不匹配");

    memset(lms_signature, 0, sizeof(lms_signature));
    lms_signature[3] = 42;
    memcpy(lms_signature + 4, lmots_signature, sizeof(lmots_signature));
    lms_signature[1131] = 6;
    for (index = 0; index < 10 * 32; ++index) {
        size_t height = index / 32;
        lms_signature[1132 + index] = (uint8_t)(height * 29u +
                                                  (index % 32) * 7u + 3u);
    }
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_lmsVerify(
            xcryptographic_test_view(identifier, sizeof(identifier)),
            xcryptographic_test_view(expected_root, sizeof(expected_root)),
            xcryptographic_test_view(message, sizeof(message) - 1),
            xcryptographic_test_view(lms_signature, sizeof(lms_signature))),
        "LMS H10 verification", "RFC 8554 LMS SHA256_M32_H10 验签失败");
    lms_signature[1200] ^= 1u;
    XCRYPTO_PRIMITIVE_REQUIRE(
        !XCryptographic_lmsVerify(
            xcryptographic_test_view(identifier, sizeof(identifier)),
            xcryptographic_test_view(expected_root, sizeof(expected_root)),
            xcryptographic_test_view(message, sizeof(message) - 1),
            xcryptographic_test_view(lms_signature, sizeof(lms_signature))),
        "LMS rejection", "篡改认证路径未被拒绝");
    XCRYPTO_PRIMITIVE_PASS("LMS/LM-OTS RFC 8554 SHA-256 verification");
    return true;
}

static bool XCryptographicPrimitive_test_mldsa87(void)
{
#if XCRYPTOGRAPHIC_MLDSA87_ON
    static const uint8_t message[] = "XinYueC ML-DSA-87 FIPS 204 test";
    static const uint8_t context[] = { 0x58, 0x59, 0x43 };
    uint8_t seed[XCRYPTOGRAPHIC_MLDSA_SEED_SIZE];
    uint8_t randomness[XCRYPTOGRAPHIC_MLDSA_SEED_SIZE];
    uint8_t publicKey[XCRYPTOGRAPHIC_MLDSA87_PUBLIC_KEY_SIZE];
    uint8_t privateKey[XCRYPTOGRAPHIC_MLDSA87_PRIVATE_KEY_SIZE];
    uint8_t signature[XCRYPTOGRAPHIC_MLDSA87_SIGNATURE_SIZE];
    size_t index;

    for (index = 0; index < sizeof(seed); ++index) {
        seed[index] = (uint8_t)(index * 13u + 7u);
        randomness[index] = (uint8_t)(index * 29u + 3u);
    }
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_mldsa87KeyPair(
            xcryptographic_test_view(seed, sizeof(seed)), publicKey,
            sizeof(publicKey), privateKey, sizeof(privateKey)),
        "ML-DSA-87 key generation", "FIPS 204 密钥生成失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_mldsa87Sign(
            xcryptographic_test_view(message, sizeof(message) - 1u),
            xcryptographic_test_view(context, sizeof(context)),
            xcryptographic_test_view(randomness, sizeof(randomness)),
            xcryptographic_test_view(privateKey, sizeof(privateKey)),
            signature, sizeof(signature)),
        "ML-DSA-87 signing", "FIPS 204 确定性签名失败");
    XCRYPTO_PRIMITIVE_REQUIRE(
        XCryptographic_mldsa87Verify(
            xcryptographic_test_view(message, sizeof(message) - 1u),
            xcryptographic_test_view(context, sizeof(context)),
            xcryptographic_test_view(signature, sizeof(signature)),
            xcryptographic_test_view(publicKey, sizeof(publicKey))),
        "ML-DSA-87 verification", "FIPS 204 验签失败");
    signature[0] ^= 1u;
    XCRYPTO_PRIMITIVE_REQUIRE(
        !XCryptographic_mldsa87Verify(
            xcryptographic_test_view(message, sizeof(message) - 1u),
            xcryptographic_test_view(context, sizeof(context)),
            xcryptographic_test_view(signature, sizeof(signature)),
            xcryptographic_test_view(publicKey, sizeof(publicKey))),
        "ML-DSA-87 rejection", "篡改签名未被拒绝");
    XCRYPTO_PRIMITIVE_PASS("ML-DSA-87 FIPS 204 keygen/sign/verify");
#else
    XCRYPTO_PRIMITIVE_PASS("ML-DSA-87 disabled (optional feature)");
#endif
    return true;
}

bool XCryptographicPrimitiveTest_runAll(void)
{
    bool ok = true;
    ok = XCryptographicPrimitive_test_aes_gcm() && ok;
    ok = XCryptographicPrimitive_test_aes_gcm_nonstandard_nonce() && ok;
    ok = XCryptographicPrimitive_test_aes_gcm_key_sizes() && ok;
    ok = XCryptographicPrimitive_test_aria_camellia_aead() && ok;
    ok = XCryptographicPrimitive_test_ecjpake() && ok;
    ok = XCryptographicPrimitive_test_aes_ccm() && ok;
    ok = XCryptographicPrimitive_test_chacha20_poly1305() && ok;
    ok = XCryptographicPrimitive_test_hkdf_sha256() && ok;
    ok = XCryptographicPrimitive_test_ripemd160() && ok;
    ok = XCryptographicPrimitive_test_aes_cmac() && ok;
    ok = XCryptographicPrimitive_test_aes_ecb_cbc() && ok;
    ok = XCryptographicPrimitive_test_aes_key_wrap() && ok;
    ok = XCryptographicPrimitive_test_aes_cbc_pkcs7() && ok;
    ok = XCryptographicPrimitive_test_aes_xts() && ok;
    ok = XCryptographicPrimitive_test_aes_ccm_star_no_tag() && ok;
    ok = XCryptographicPrimitive_test_aes_cfb_ofb() && ok;
    ok = XCryptographicPrimitive_test_chacha20() && ok;
    ok = XCryptographicPrimitive_test_pbkdf2_hmac_sha256() && ok;
    ok = XCryptographicPrimitive_test_aria_block_cipher() && ok;
    ok = XCryptographicPrimitive_test_camellia_block_cipher() && ok;
    ok = XCryptographicPrimitive_test_shake() && ok;
    ok = XCryptographicPrimitive_test_kdf_batch() && ok;
    ok = XCryptographicPrimitive_test_hmac_stream() && ok;
    ok = XCryptographicPrimitive_test_ffdh() && ok;
    ok = XCryptographicPrimitive_test_rsa() && ok;
    ok = XCryptographicPrimitive_test_ecdsa_deterministic() && ok;
    ok = XCryptographicPrimitive_test_secp256k1_ecdh() && ok;
    ok = XCryptographicPrimitive_test_brainpool_p256r1_ecdh() && ok;
    ok = XCryptographicPrimitive_test_p384_ecdh() && ok;
    ok = XCryptographicPrimitive_test_p521_ecdh() && ok;
    ok = XCryptographicPrimitive_test_brainpool_p512r1_ecdh() && ok;
    ok = XCryptographicPrimitive_test_brainpool_p384_ecdh() && ok;
    ok = XCryptographicPrimitive_test_x448() && ok;
    ok = XCryptographicPrimitive_test_lms() && ok;
    ok = XCryptographicPrimitive_test_mldsa87() && ok;
    XPrintf("[RESULT] XCryptographic primitives: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static void XCryptographicPrimitive_test_all_wrapper(XVariant* data)
{
    (void)data;
    XCryptographicPrimitiveTest_runAll();
}

void XMenu_XCryptographicPrimitiveTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XCryptographic(密码原语)");
    XAction* action;
    if (!menu) return;
    XMenu_addMenu(root, menu);
    action = XMenu_addAction(menu, "AES-GCM/CCM/HKDF 全部测试");
    if (action) XAction_setAction(action, XCryptographicPrimitive_test_all_wrapper);
}

#endif /* DEMOTEST */
