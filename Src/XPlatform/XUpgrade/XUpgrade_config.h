/** @file XUpgrade_config.h
 * @brief 跨平台自升级模块配置文件
 *
 * 通过此配置文件可以裁剪 XUpgrade 模块内部的各个子功能：
 *   1. XUPGRADE_HTTP_ON        - XHttp 下载通道（XNetworkAccessManager，Range 断点续传）
 *   2. XUPGRADE_VERIFY_ON      - 包完整性校验（.pak 整包/条目 SHA-256）
 *   3. XUPGRADE_VERIFY_P256_ON - manifest ECDSA P-256 验签（canonical 序列化 + 信任公钥注入）
 *   4. XUPGRADE_FTP_ON         - XFtp 下载通道（预留）
 *   5. XUPGRADE_AB_ON          - 嵌入式 A/B 存储后端（XUpgradeStoreAb + Drive 分区原语 + bootloader 契约）
 *   6. XUPGRADE_AB_SIM_ON      - 宿主模拟 A/B 分区后端（测试/桌面演练；真实板级经 Drive 注入时无需此宏）
 *   7. XUPGRADE_GRAY_ON        - 灰度发布命中判定（allow/deny 名单 + SHA256(salt|deviceKey) 分流）
 *   8. XUPGRADE_DELTA_ON       - delta 增量升级（M4 预留，bspatch）
 *   9. XUPGRADE_UI_HINTS_ON    - UI 提示事件（无 UI 嵌入式构建置 0）
 *
 * 模块总开关 XUPGRADE_ON 可在 CXinYueConfig.h 汇总或编译选项覆盖，此处仅提供
 * 默认值。所有宏一律带 XUPGRADE_ 全前缀，避免短名宏在 #if 中静默为 0 的陷阱。
 * 契约扩展口径：P-256 验签、灰度判定、forced/silent 策略收口与 A/B 后端契约
 * （含宿主模拟）全部编入默认构建；目录后端仍为缺省存储。
 */

#ifndef XUPGRADE_CONFIG_H
#define XUPGRADE_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 引入全局配置，确保 XUPGRADE_ON 总开关已定义 */
#include "CXinYueConfig.h"

#ifndef XUPGRADE_ON
#define XUPGRADE_ON 1
#endif

#if XUPGRADE_ON

/* ========================================================================== */
/*                        子功能开关                                        */
/* ========================================================================== */

/** @brief XHttp 下载通道（XNetworkAccessManager；Range 断点续传）。 */
#ifndef XUPGRADE_HTTP_ON
#define XUPGRADE_HTTP_ON 1
#endif

/** @brief 包完整性校验（.pak 整包/条目 SHA-256；仅内部调试可关，发布恒 1）。 */
#ifndef XUPGRADE_VERIFY_ON
#define XUPGRADE_VERIFY_ON 1
#endif

/** @brief manifest ECDSA P-256 验签（canonical 序列化在 XUpgradeManifest 侧，
 *         验签入口 XUpgradeVerify_verifyP256；信任公钥缺省编译期内嵌
 *         XUpgrade_trust_pubkey.h，可经 XUpgrade_setTrustKey 注入测试/产品钥，
 *         设计文档 §4.2。仅内部调试可关，发布恒 1）。 */
#ifndef XUPGRADE_VERIFY_P256_ON
#define XUPGRADE_VERIFY_P256_ON 1
#endif

/** @brief XFtp 下载通道（预留）。 */
#ifndef XUPGRADE_FTP_ON
#define XUPGRADE_FTP_ON 0
#endif

/** @brief 嵌入式 A/B 存储后端（XUpgradeStoreAb + Drive 分区原语 + bootloader 元数据块，§9）。 */
#ifndef XUPGRADE_AB_ON
#define XUPGRADE_AB_ON 0
#endif

/** @brief 宿主模拟 A/B 分区后端（默认 1：与 XUPGRADE_AB_ON=1 组合编入文件持久化
 *         的模拟分区原语，供桌面 e2e/断电演练，见 XUpgradeStoreAb_installSimBackend）。
 *         真实板级经 XUpgradeStore_setPartitionBackend 注入 Drive 回调，置 0 即
 *         裁出模拟实现；本宏单独置 1 而不编入 AB 后端时无效果。 */
#ifndef XUPGRADE_AB_SIM_ON
#define XUPGRADE_AB_SIM_ON 1
#endif

/** @brief 灰度发布命中判定（allow/deny 名单 + SHA256(salt|deviceKey) 分流，
 *         契约判定入口 XUpgradeManifest_grayHit，设计文档 §11）。 */
#ifndef XUPGRADE_GRAY_ON
#define XUPGRADE_GRAY_ON 1
#endif

/** @brief delta 增量升级（M4 预留：bsdiff/bspatch）。 */
#ifndef XUPGRADE_DELTA_ON
#define XUPGRADE_DELTA_ON 0
#endif

/** @brief UI 提示事件（无 UI 嵌入式构建置 0；置 0 时引擎照常后台运行，仅不推 UI 提示语义事件）。 */
#ifndef XUPGRADE_UI_HINTS_ON
#define XUPGRADE_UI_HINTS_ON 1
#endif

#endif /* XUPGRADE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XUPGRADE_CONFIG_H */
