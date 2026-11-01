/**
 * @file       XUpgrade_test_keys.h
 * @brief      ECDSA P-256 测试钥对（仅测试用，生产钥替换点见下）。
 * @details    供 e2e 造签名 manifest（设计文档 §4.2/§16 测试载体）：私钥在
 *             服务端/测试工具侧对 canonical(manifest) 做 ECDSA-P256-SHA256
 *             签名，设备侧经 XUpgrade_setTrustKey 注入对应公钥（SEC1 未压缩
 *             65 字节，即 XUPGRADE_TRUST_KEY_SEC1_SIZE）。钥材料生成于
 *             2026-10-06，生成时已当场验证：签名/验签回环通过、篡改被拒、
 *             PEM 重导入与本十六进制一致。
 * @warning    本钥对仅用于测试与桌面演练，禁止用于生产固件与生产签名。
 *             生产钥替换点（二选一，缺省内嵌）：1) 编译期替换
 *             Src/XCode/XUpgrade/XUpgrade_trust_pubkey.h 的内嵌公钥（发布
 *             流程换钥=重编译）；2) 产品初始化经 XUpgrade_setTrustKey 注入
 *             产品钥。公钥轮换走双钥过渡期运维方案（§17.7，M4）。
 */

#ifndef XUPGRADE_TEST_KEYS_H
#define XUPGRADE_TEST_KEYS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "XUpgrade.h" /* XUPGRADE_ON 开关与 XUPGRADE_TRUST_KEY_SEC1_SIZE（自包含，禁止依赖包含顺序） */

#if XUPGRADE_ON

/* ========================================================================== */
/*                            十六进制串形式（工具侧）                          */
/* ========================================================================== */

/** @brief 测试私钥标量（32 字节大端十六进制；mbedtls mbedtls_ecp_read_private 等价物）。 */
#define XUPGRADE_TEST_KEY_PRIV_HEX "dd209fd065497ac9a77f21626331cebe204e4ca332a774ef7436cae915ba1a1c"
/** @brief 测试公钥 X 坐标（32 字节大端十六进制）。 */
#define XUPGRADE_TEST_KEY_PUB_X_HEX "9f41bb6adce5d07a949dc43056be09f5a7a9a5cafac792f0c8bb51f3c5ee52d5"
/** @brief 测试公钥 Y 坐标（32 字节大端十六进制）。 */
#define XUPGRADE_TEST_KEY_PUB_Y_HEX "8f6b559aab646bfe647d26a0396f9d65dd38b00187875ccdc4e55f0f4cd85292"
/** @brief 测试公钥 SEC1 未压缩点（0x04||X||Y 共 65 字节十六进制；与 XUpgrade_setTrustKey 注入格式一致）。 */
#define XUPGRADE_TEST_KEY_PUB_SEC1_HEX "049f41bb6adce5d07a949dc43056be09f5a7a9a5cafac792f0c8bb51f3c5ee52d58f6b559aab646bfe647d26a0396f9d65dd38b00187875ccdc4e55f0f4cd85292"

/* ========================================================================== */
/*                            字节数组形式（设备侧）                            */
/* ========================================================================== */

/** @brief 测试私钥标量（大端，mbedtls mpi 读入即用）。仅测试编译单元包含本头。 */
static const uint8_t XUpgrade_testPrivateKey[32] = {
    0xdd, 0x20, 0x9f, 0xd0, 0x65, 0x49, 0x7a, 0xc9,
    0xa7, 0x7f, 0x21, 0x62, 0x63, 0x31, 0xce, 0xbe,
    0x20, 0x4e, 0x4c, 0xa3, 0x32, 0xa7, 0x74, 0xef,
    0x74, 0x36, 0xca, 0xe9, 0x15, 0xba, 0x1a, 0x1c
};

/** @brief 测试公钥 SEC1 未压缩点（0x04||X||Y，65 字节；XUpgrade_setTrustKey 直接可用）。 */
static const uint8_t XUpgrade_testPublicKeySec1[XUPGRADE_TRUST_KEY_SEC1_SIZE] = {
    0x04, 0x9f, 0x41, 0xbb, 0x6a, 0xdc, 0xe5, 0xd0,
    0x7a, 0x94, 0x9d, 0xc4, 0x30, 0x56, 0xbe, 0x09,
    0xf5, 0xa7, 0xa9, 0xa5, 0xca, 0xfa, 0xc7, 0x92,
    0xf0, 0xc8, 0xbb, 0x51, 0xf3, 0xc5, 0xee, 0x52,
    0xd5, 0x8f, 0x6b, 0x55, 0x9a, 0xab, 0x64, 0x6b,
    0xfe, 0x64, 0x7d, 0x26, 0xa0, 0x39, 0x6f, 0x9d,
    0x65, 0xdd, 0x38, 0xb0, 0x01, 0x87, 0x87, 0x5c,
    0xcd, 0xc4, 0xe5, 0x5f, 0x0f, 0x4c, 0xd8, 0x52,
    0x92
};

/* ========================================================================== */
/*                            PEM 形式（服务端/openssl 工具链）                 */
/* ========================================================================== */

/** @brief 测试公钥 PEM（SubjectPublicKeyInfo；服务端打包工具验签用）。 */
#define XUPGRADE_TEST_KEY_PUB_PEM \
    "-----BEGIN PUBLIC KEY-----\n" \
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEn0G7atzl0HqUncQwVr4J9aeppcr6\n" \
    "x5LwyLtR88XuUtWPa1Waq2Rr/mR9JqA5b51l3TiwAYeHXM3E5V8PTNhSkg==\n" \
    "-----END PUBLIC KEY-----\n"


/** @brief 测试私钥 PEM（PKCS#8；服务端打包工具签名用）。 */
#define XUPGRADE_TEST_KEY_PRIV_PEM \
    "-----BEGIN PRIVATE KEY-----\n" \
    "MIGHAgEAMBMGByqGSM49AgEGCCqGSM49AwEHBG0wawIBAQQg3SCf0GVJesmnfyFi\n" \
    "YzHOviBOTKMyp3TvdDbK6RW6GhyhRANCAASfQbtq3OXQepSdxDBWvgn1p6mlyvrH\n" \
    "kvDIu1Hzxe5S1Y9rVZqrZGv+ZH0moDlvnWXdOLABh4dczcTlXw9M2FKS\n" \
    "-----END PRIVATE KEY-----\n"


#endif /* XUPGRADE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XUPGRADE_TEST_KEYS_H */
