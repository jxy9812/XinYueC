/**
 * @file       HsfLinkConfig.c
 * @brief      远端配置应答解析实现。
 * @details    [键名与 Python 端一致] 服务端按 proto.CFGK_* 下发，这里按
 *             同名字段读；读不到就留缺省（不猜、不报错——配置是"缺了也能
 *             显示"的宽容结构）。
 */
#include "HsfLinkConfig.h"

#include "XBson.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XString.h"
#include <stdio.h>
#include <string.h>

/** @brief 取子文档（collect/storage/storageStats）。 */
static const XBsonDocument* hsfCfgSubDoc(const XBsonDocument* doc,
                                          const char* key)
{
    XBsonValue* v;
    const XBsonDocument* sub;
    if (!doc || !key) return NULL;
    v = XBsonDocument_value_keyUtf8(doc, key);
    if (!v) return NULL;
    sub = XBsonValue_toDocument(v);
    return sub;
}

/** @brief 取子文档里的布尔键。 */
static bool hsfCfgSubBool(const XBsonDocument* sub, const char* key,
                           bool def)
{
    XBsonValue* v;
    if (!sub || !key) return def;
    v = XBsonDocument_value_keyUtf8(sub, key);
    if (!v) return def;
    return XBsonValue_toBool(v, def);
}

/** @brief 取子文档里的整型键。 */
static int hsfCfgSubInt(const XBsonDocument* sub, const char* key, int def)
{
    XBsonValue* v;
    if (!sub || !key) return def;
    v = XBsonDocument_value_keyUtf8(sub, key);
    if (!v) return def;
    return (int)XBsonValue_toInt32(v, def);
}

/** @brief 取子文档里的 64 位整型键。 */
static int64_t hsfCfgSubInt64(const XBsonDocument* sub, const char* key,
                               int64_t def)
{
    XBsonValue* v;
    if (!sub || !key) return def;
    v = XBsonDocument_value_keyUtf8(sub, key);
    if (!v) return def;
    return XBsonValue_toInt64(v, def);
}

/** @brief 取子文档里的浮点键（usedMb 等服务端直接给的定数）。 */
static double hsfCfgSubDouble(const XBsonDocument* sub, const char* key,
                               double def)
{
    XBsonValue* v;
    if (!sub || !key) return def;
    v = XBsonDocument_value_keyUtf8(sub, key);
    if (!v) return def;
    return XBsonValue_toDouble(v, def);
}

/** @brief 取子文档里的文本键。 */
static void hsfCfgSubText(const XBsonDocument* sub, const char* key,
                          char* out, int cap)
{
    XBsonValue* v;
    const XString* s;
    if (out && cap > 0) out[0] = '\0';
    if (!sub || !key || !out || cap <= 0) return;
    v = XBsonDocument_value_keyUtf8(sub, key);
    if (!v) return;
    s = XBsonValue_toString(v);
    if (s) {
        const char* u = XString_toUtf8(s);
        /* [空串不能交给 %s] 空 BSON 字符串的 XString 内部缓冲可能为 NULL，
         * XString_toUtf8 返回 NULL 时 snprintf("%s", NULL) 会写成 "(null)"
         * （实测：Python 服务端 path="" → 路径框显示 "(null)"）。 */
        if (u) snprintf(out, (size_t)cap, "%s", u);
    }
}

bool HsfConfig_parse(const uint8_t* data, uint32_t len, HsfConfig* out)
{
    XBsonDocument* doc;
    const XBsonDocument* collect;
    const XBsonDocument* storage;
    const XBsonDocument* stats;
    XBsonValue* v;

    if (!out) return false;
    memset(out, 0, sizeof(*out));
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (!XBsonDocument_from_bytes(doc, data, len)) {
        XClassDelete((XClass*)doc);
        return false;
    }

    collect = hsfCfgSubDoc(doc, "collect");
    storage = hsfCfgSubDoc(doc, "storage");
    stats = hsfCfgSubDoc(doc, "storageStats");
    {
        /* 登录策略节（2026-11-08；旧服务端没有该节 ⇒ 保持缺省，界面
         * 按缺省显示且不误报）。 */
        const XBsonDocument* login = hsfCfgSubDoc(doc, "login");
        if (login) {
            out->m_login.m_allowAnonymous =
                hsfCfgSubBool(login, "loginAllowAnonymous", false);
            out->m_login.m_allowConcurrent =
                hsfCfgSubBool(login, "loginAllowConcurrent", false);
            out->m_login.m_maxSessions =
                hsfCfgSubInt(login, "loginMaxSessions", 8);
        }
    }

    if (collect) {
        out->m_collect.m_vfdEnabled =
            hsfCfgSubBool(collect, "vfdEnabled", true);
        out->m_collect.m_temphumEnabled =
            hsfCfgSubBool(collect, "temphumEnabled", true);
        out->m_collect.m_flowmeterEnabled =
            hsfCfgSubBool(collect, "flowmeterEnabled", true);
        out->m_collect.m_turbidityEnabled =
            hsfCfgSubBool(collect, "turbidityEnabled", true);
        /* [逐台开关 2026-11-10] 两台浊度是独立设备：缺逐台键回落共同键
         * （旧服务端只报 turbidityEnabled）。 */
        out->m_collect.m_turbidity1Enabled =
            hsfCfgSubBool(collect, "turbidity1Enabled",
                          out->m_collect.m_turbidityEnabled);
        out->m_collect.m_turbidity2Enabled =
            hsfCfgSubBool(collect, "turbidity2Enabled",
                          out->m_collect.m_turbidityEnabled);
        out->m_collect.m_analogEnabled =
            hsfCfgSubBool(collect, "analogEnabled", true);
        out->m_collect.m_digitalEnabled =
            hsfCfgSubBool(collect, "digitalEnabled", true);
        out->m_collect.m_vfdPollMs =
            hsfCfgSubInt(collect, "vfdPollMs", 0);
        out->m_collect.m_temphumPollMs =
            hsfCfgSubInt(collect, "temphumPollMs", 500);
        out->m_collect.m_turbidityPollMs =
            hsfCfgSubInt(collect, "turbidityPollMs", 500);
        /* [逐台 2026-11-10] 缺键回落"两台共同缺省"，与 C 服务端同序。 */
        out->m_collect.m_turbidity1PollMs =
            hsfCfgSubInt(collect, "turbidity1PollMs",
                         out->m_collect.m_turbidityPollMs);
        out->m_collect.m_turbidity2PollMs =
            hsfCfgSubInt(collect, "turbidity2PollMs",
                         out->m_collect.m_turbidityPollMs);
        out->m_collect.m_flowmeterPollMs =
            hsfCfgSubInt(collect, "flowmeterPollMs", 500);
        out->m_collect.m_analogPollMs =
            hsfCfgSubInt(collect, "analogPollMs", 200);
        out->m_collect.m_digitalPollMs =
            hsfCfgSubInt(collect, "digitalPollMs", 200);
    }
    if (storage) {
        /* [落盘节确实存在 2026-11-10] 记一笔：没有这一节时 m_storage 的
         * 字段是结构体零值（enabled=false），界面若把它当「服务端说落盘
         * 关闭」写回去就会静默关掉落盘 ⇒ 历史表格空（见 HsfConfig 的
         * m_storagePresent 注记）。 */
        out->m_storagePresent = true;
        out->m_storage.m_enabled = hsfCfgSubBool(storage, "enabled", true);
        hsfCfgSubText(storage, "path", out->m_storage.m_path,
                      (int)sizeof(out->m_storage.m_path));
        out->m_storage.m_sampleMs = hsfCfgSubInt(storage, "sampleMs", 5000);
        out->m_storage.m_maxMb = hsfCfgSubInt(storage, "maxMb", 0);
        hsfCfgSubText(storage, "fullPolicy", out->m_storage.m_fullPolicy,
                      (int)sizeof(out->m_storage.m_fullPolicy));
        out->m_storage.m_keepFiles = hsfCfgSubInt(storage, "keepFiles", 3);
    }
    if (stats) {
        out->m_stats.m_usedBytes = hsfCfgSubInt64(stats, "usedBytes", 0);
        out->m_stats.m_contentBytes = hsfCfgSubInt64(stats, "contentBytes", 0);
        out->m_stats.m_usedMb = hsfCfgSubDouble(stats, "usedMb",
            (double)out->m_stats.m_usedBytes / (1024.0 * 1024.0));
        out->m_stats.m_maxMb = hsfCfgSubInt(stats, "maxMb", 0);
        /* [64 位字段用 Int64 取] 服务端把 rows/droppedRows 写成 int64；
         * XBsonValue_toInt32 对 int64 变体不降型（实测回到 0），
         * 界面会显示"行数 0"。 */
        out->m_stats.m_rows = (int)hsfCfgSubInt64(stats, "rows", 0);
        out->m_stats.m_segments = (int)hsfCfgSubInt64(stats, "segments", 0);
        out->m_stats.m_droppedRows =
            (int)hsfCfgSubInt64(stats, "droppedRows", 0);
        out->m_stats.m_paused = hsfCfgSubBool(stats, "paused", false);
        hsfCfgSubText(stats, "policy", out->m_stats.m_policy,
                      (int)sizeof(out->m_stats.m_policy));
        hsfCfgSubText(stats, "pauseReason", out->m_stats.m_pauseReason,
                      (int)sizeof(out->m_stats.m_pauseReason));
    }
    v = XBsonDocument_value_keyUtf8(doc, "storageDbPath");
    if (v) {
        const XString* s = XBsonValue_toString(v);
        const char* u = s ? XString_toUtf8(s) : NULL;
        if (u) snprintf(out->m_dbPath, sizeof(out->m_dbPath), "%s", u);
    }
    out->m_valid = true;
    XClassDelete((XClass*)doc);
    return true;
}
