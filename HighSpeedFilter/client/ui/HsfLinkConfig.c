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
    if (s) snprintf(out, (size_t)cap, "%s", XString_toUtf8(s));
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

    if (collect) {
        out->m_collect.m_vfdEnabled =
            hsfCfgSubBool(collect, "vfdEnabled", true);
        out->m_collect.m_temphumEnabled =
            hsfCfgSubBool(collect, "temphumEnabled", true);
        out->m_collect.m_flowmeterEnabled =
            hsfCfgSubBool(collect, "flowmeterEnabled", true);
        out->m_collect.m_turbidityEnabled =
            hsfCfgSubBool(collect, "turbidityEnabled", true);
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
        out->m_collect.m_analogPollMs =
            hsfCfgSubInt(collect, "analogPollMs", 200);
        out->m_collect.m_digitalPollMs =
            hsfCfgSubInt(collect, "digitalPollMs", 200);
    }
    if (storage) {
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
        out->m_stats.m_maxMb = hsfCfgSubInt(stats, "maxMb", 0);
        out->m_stats.m_rows = hsfCfgSubInt(stats, "rows", 0);
        out->m_stats.m_segments = hsfCfgSubInt(stats, "segments", 0);
        out->m_stats.m_droppedRows = hsfCfgSubInt(stats, "droppedRows", 0);
        out->m_stats.m_paused = hsfCfgSubBool(stats, "paused", false);
    }
    v = XBsonDocument_value_keyUtf8(doc, "storageDbPath");
    if (v) {
        const XString* s = XBsonValue_toString(v);
        if (s) snprintf(out->m_dbPath, sizeof(out->m_dbPath), "%s",
                        XString_toUtf8(s));
    }
    out->m_valid = true;
    XClassDelete((XClass*)doc);
    return true;
}
