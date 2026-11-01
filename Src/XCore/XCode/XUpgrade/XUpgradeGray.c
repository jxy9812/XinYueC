/**
 * @file       XUpgradeGray.c
 * @brief      升级策略引擎与灰度判定（设计文档 §10/§11）的端上判定层。
 * @details    灰度命中哈希算法的唯一实现是契约位 XUpgradeManifest_grayHit
 *             （XUpgradeManifest.c，算法入契约保证跨版本判定一致，§11）；
 *             本文件在其上维护判定分类（allow/deny 精确匹配→分流哈希）、
 *             §10 策略组合锚定（本地版本低于 min_version/policy_forced_below
 *              → forced，无视组件策略与用户跳过）与 silent 维护窗口纯判定，
 *             并收口 forced 宽限期解析（本地覆盖 > manifest forced_grace_hours
 *             > 0）。全部入口为可独立断言的纯逻辑：墙钟时刻由调用方经
 *             XDateTime_currentDateTime 取值后注入（风格指南 2026-09-27 裁定，
 *             维护窗口禁用单调毫秒），本 TU 不取时间、不做 IO、不分配内存。
 *             消费方为引擎（XUpgradeEngine.c）：模块内部契约为本文件定义的
 *             外部链接入口（无独立内部头，声明以本文件为准，引擎侧文件内
 *             前置声明保持签名一致）。
 */
#include "XUpgrade.h"
#include "xupgrade_shared.h" /* 模块内部共享小工具（xups_*） */

#include "XUpgradeManifest.h"
#include "XUpgradeIdentity.h"

#if XUPGRADE_ON

/* ========================================================================== */
/*                            内部工具（禁止 string.h 口径）                    */
/* ========================================================================== */

/* str_eq 已收敛至 xupgrade_shared.h（xups_str_eq）。 */

#if XUPGRADE_GRAY_ON

/** @brief 设备键是否精确命中灰度名单（空条目视为不命中，容量越界截断）。 */
static bool xupgray_list_hit(const char (*list)[XUPGRADE_SERIAL_MAX], uint32_t count,
                             const char* deviceKey)
{
    uint32_t i;
    if (!list || !deviceKey || deviceKey[0] == '\0') return false;
    for (i = 0; i < count && i < XUPGRADE_GRAY_LIST_MAX; ++i) {
        if (list[i][0] != '\0' && xups_str_eq(list[i], deviceKey)) return true;
    }
    return false;
}

#endif /* XUPGRADE_GRAY_ON */

/* ========================================================================== */
/*                            灰度判定（§11）                                   */
/* ========================================================================== */

bool XUpgradeGray_manifestHasGray(const XUpgradeManifest* manifest)
{
    if (!manifest) return false;
    /* manifest 结构无"gray 字段存在"标志位：以任一有效灰度字段非缺省值判定
     * （percent=0 且名单与盐全空 = 解析缺省 = 未配置灰度）。 */
    return manifest->m_gray.m_percent > 0u ||
           manifest->m_gray.m_allowCount > 0u ||
           manifest->m_gray.m_denyCount > 0u ||
           manifest->m_gray.m_salt[0] != '\0';
}

XUpgradeGrayDecision XUpgradeGray_evaluate(const XUpgradeManifest* manifest)
{
#if XUPGRADE_GRAY_ON
    char deviceKey[XUPGRADE_DEVICE_KEY_MAX];
    if (!manifest || !XUpgradeGray_manifestHasGray(manifest))
        return XUpgradeGrayDecision_NotEvaluated;
    /* 设备键回退链（SN→MAC→持久 UUID）全部不可得：灰度按未命中处理
     * （XUpgradeIdentity.h 契约口径），不做空键哈希。 */
    if (!XUpgradeIdentity_deviceKey(deviceKey, sizeof(deviceKey)))
        return XUpgradeGrayDecision_Miss;
    /* 判定顺序不得随版本变化（§11）：allow 精确命中→放行；deny 精确命中→
     * 拒绝；其余分流哈希（算法唯一实现 XUpgradeManifest_grayHit）。 */
    if (xupgray_list_hit(manifest->m_gray.m_allow, manifest->m_gray.m_allowCount, deviceKey))
        return XUpgradeGrayDecision_AllowList;
    if (xupgray_list_hit(manifest->m_gray.m_deny, manifest->m_gray.m_denyCount, deviceKey))
        return XUpgradeGrayDecision_DenyList;
    return XUpgradeManifest_grayHit(&manifest->m_gray, deviceKey)
               ? XUpgradeGrayDecision_Hit
               : XUpgradeGrayDecision_Miss;
#else
    (void)manifest;
    return XUpgradeGrayDecision_NotEvaluated; /* 灰度裁剪关闭：不判定、不拦截 */
#endif
}

bool XUpgradeGray_decisionPass(XUpgradeGrayDecision decision)
{
    return decision == XUpgradeGrayDecision_NotEvaluated ||
           decision == XUpgradeGrayDecision_AllowList ||
           decision == XUpgradeGrayDecision_Hit;
}

/* ========================================================================== */
/*                            策略组合判定（§10）                               */
/* ========================================================================== */

bool XUpgradeGray_forcedAnchored(const XUpgradeManifest* manifest, const char* localVersion)
{
    if (!manifest) return false;
    /* 锚定顺序与字段口径（§10 表行 1）：本地组件版本低于 min_version 或
     * policy_forced_below 即 forced（无视组件策略与用户跳过）；空串=未设置。
     * 本地版本缺失（新组件从未激活）按空串参与比较，低于锚线即 forced。 */
    if (manifest->m_minVersion[0] != '\0' &&
        XUpgradeManifest_compareVersions(localVersion, manifest->m_minVersion) < 0)
        return true;
    if (manifest->m_policyForcedBelow[0] != '\0' &&
        XUpgradeManifest_compareVersions(localVersion, manifest->m_policyForcedBelow) < 0)
        return true;
    return false;
}

uint32_t XUpgradeGray_resolveGraceHours(bool hasOverride, uint32_t overrideHours,
                                        bool hasManifest, const XUpgradeManifest* manifest)
{
    if (hasOverride) return overrideHours;
    if (hasManifest && manifest) return manifest->m_forcedGraceHours;
    return 0u; /* 无 manifest 上下文：0（staged 完成即自动 apply，§10） */
}

/* ========================================================================== */
/*                            silent 维护窗口（§10）                            */
/* ========================================================================== */

bool XUpgradeGray_windowContains(const XUpgradeApplyWindow* window, int hour, int minute)
{
    int start;
    int end;
    int now;
    if (!window || !window->m_enabled) return false;
    if (window->m_startHour > 23 || window->m_startMinute > 59 ||
        window->m_endHour > 23 || window->m_endMinute > 59)
        return false;
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return false;
    start = (int)window->m_startHour * 60 + (int)window->m_startMinute;
    end = (int)window->m_endHour * 60 + (int)window->m_endMinute;
    now = hour * 60 + minute;
    if (start == end) return false; /* 零长窗口：永不命中（错过顺延口径的退化态） */
    if (start < end) return now >= start && now < end; /* 同日窗口 [start, end) */
    return now >= start || now < end; /* 跨午夜窗口（如 22:30-05:00） */
}

#endif /* XUPGRADE_ON */
