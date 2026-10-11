/**
 * @file       XUpgrade_trust_pubkey.h
 * @brief      manifest 验签的编译期内嵌缺省信任公钥（SEC1 未压缩点，设计文档 §4.2）。
 * @details    信任锚（§4.2）：发布换钥=重编译替换本表，或产品初始化经
 *             XUpgrade_setTrustKey 注入（二选一，缺省使用本表）。当前内嵌为
 *             测试钥占位——与 Src/XCode/XUpgrade/XUpgrade_test_keys.h 的公钥
 *             SEC1 逐字节一致（生成记录见该头文件头注释）；本头只含公钥，
 *             测试私钥仅存在于仅测试编译单元包含的 XUpgrade_test_keys.h，
 *             不进入产品路径。本头仅被 XUpgradeVerify.c 包含。
 * @warning    生产发布前必须替换本表为产品公钥（0x04||X||Y，65 字节大端），
 *             或在产品初始化阶段注入产品钥；测试钥占位不得随产品发布。
 */

#ifndef XUPGRADE_TRUST_PUBKEY_H
#define XUPGRADE_TRUST_PUBKEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ============================================================================
 *  发布门禁（@warning 的编译期强制，防测试钥随产品发布）
 *  - 发布/产品构建：在构建定义 XUPGRADE_PRODUCT_BUILD，且替换本表为产品钥后
 *    定义 XUPGRADE_TRUST_KEY_PRODUCT（任一缺失即编译失败）；
 *  - 开发/测试构建：两者都不定义，缺省测试钥占位照常可用（缺省口径）。
 *  验签门已接入引擎检查链（§4.2），缺省信任锚=仓库测试钥（对应私钥同仓），
 *  不换钥发布=任何拿到仓库者可签发升级包。
 * ========================================================================== */
#if defined(XUPGRADE_PRODUCT_BUILD) && !defined(XUPGRADE_TRUST_KEY_PRODUCT)
#error "XUpgrade_trust_pubkey.h: 信任公钥仍为仓库测试钥占位——产品发布前必须替换为产品公钥并定义 XUPGRADE_TRUST_KEY_PRODUCT（§4.2 / 文件头 @warning）"
#endif

/** @brief 内嵌缺省信任公钥长度（字节），恒为 SEC1 未压缩点 65 字节。 */
#define XUPGRADE_TRUST_PUBKEY_SEC1_SIZE 65u

/**
 * @brief 内嵌缺省信任公钥（SEC1 未压缩点 0x04||X||Y，大端）。
 * @note 当前值为测试钥占位（替换点见文件头注释）；被 XUpgradeVerify.c 用作
 *       XUpgrade_setTrustKey 未注入时的缺省信任锚。
 */
static const uint8_t XUpgrade_trustPubkeySec1[XUPGRADE_TRUST_PUBKEY_SEC1_SIZE] = {
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

#ifdef __cplusplus
}
#endif

#endif /* XUPGRADE_TRUST_PUBKEY_H */
