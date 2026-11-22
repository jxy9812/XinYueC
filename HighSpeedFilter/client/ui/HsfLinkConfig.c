/**
 * @file       HsfLinkConfig.c
 * @brief      远端配置应答解析实现。
 * @details    [键名与 Python 端一致] 服务端按 proto.CFGK_* 下发，这里按
 *             同名字段读；读不到就留缺省（不猜、不报错——配置是"缺了也能
 *             显示"的宽容结构）。
 */
#include "HsfLinkConfig.h"

#include "XBson.h"
#include "XBsonArray.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XString.h"
#include <stdio.h>
#include <string.h>

/** @brief 自动模式设备 id → HsfConfigAutoMode.m_devices 下标；未知返回 -1。
 *  @details 稳定 id 形如 valve1..valve6 / stirPump / inletPump
 *           （HSF_AUTO_DEVICE_VALVE_FMT / HSF_AUTO_DEVICE_ID_*；与服务端
 *           HsfServerAutoMode、Python proto.AUTO_DEVICES 同一口径）。
 *           未知 id 静默跳过（前向兼容——服务端将来增删设备清单时旧客户端
 *           不炸，对齐 Python `_populate_auto` 的「未知设备 id 丢弃」）。 */
static int hsfCfgAutoDevIndex(const char* id)
{
    int i;
    char buf[16];
    if (!id || !id[0]) return -1;
    if (strcmp(id, HSF_AUTO_DEVICE_ID_STIR_PUMP) == 0)  return 6;
    if (strcmp(id, HSF_AUTO_DEVICE_ID_INLET_PUMP) == 0) return 7;
    for (i = 0; i < 6; ++i) {
        snprintf(buf, sizeof(buf), HSF_AUTO_DEVICE_VALVE_FMT, i + 1);
        if (strcmp(id, buf) == 0) return i;
    }
    return -1;
}

/** @brief 解析单模式的 devices 数组（字符串 id 列表 → 勾选位）。 */
static void hsfCfgAutoParseDevices(const XBsonDocument* modeDoc,
                                   bool* devices)
{
    XBsonValue* v;
    const XBsonArray* arr;
    size_t n;
    size_t k;
    int i;
    for (i = 0; i < HSF_AUTO_DEVICE_COUNT; ++i) devices[i] = false;
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)modeDoc, "devices");
    if (!v) return;
    arr = XBsonValue_toArray(v);
    if (!arr) return;
    n = XBsonArray_size_base((XBsonArray*)arr);
    for (k = 0; k < n; ++k) {
        const XBsonValue* el = XBsonArray_at_base((XBsonArray*)arr, (int64_t)k);
        const XString* s;
        const char* u;
        int idx;
        if (!el) continue;
        s = XBsonValue_toString((XBsonValue*)el);
        if (!s) continue;
        u = XString_toUtf8(s);
        if (!u) continue;
        idx = hsfCfgAutoDevIndex(u);
        if (idx >= 0 && idx < HSF_AUTO_DEVICE_COUNT)
            devices[idx] = true;
    }
}

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
    /* [编译期断言] 模式 id 表与协议常量绑死（MSVC C 模式负数组长度写法）。 */
    typedef char HsfCfgAutoModeTableAssert[(HSF_AUTO_MODE_COUNT == 3) ? 1 : -1];
    /** 模式 id 表（下标 = HsfConfigAuto.m_modes 稳定顺序；proto 同名同序）。 */
    static const char* const kModeIds[HSF_AUTO_MODE_COUNT] = {
        HSF_AUTO_MODE_ID_PRODUCE, HSF_AUTO_MODE_ID_BACKWASH,
        HSF_AUTO_MODE_ID_RINSE
    };

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
            /* [节存在性 2026-11-12] 局部应答不带 login ⇒ 不许界面按零值
             * 铺回（否则点一次「应用采集配置」就把登录策略卡冲回缺省）。 */
            out->m_loginPresent = true;
            out->m_login.m_allowAnonymous =
                hsfCfgSubBool(login, "loginAllowAnonymous", false);
            out->m_login.m_allowConcurrent =
                hsfCfgSubBool(login, "loginAllowConcurrent", false);
            out->m_login.m_maxSessions =
                hsfCfgSubInt(login, "loginMaxSessions", 8);
        }
    }
    {
        /* 运行保护节（2026-11-15；旧服务端没有该节 ⇒ 保持缺省）。 */
        const XBsonDocument* protect = hsfCfgSubDoc(doc, "protect");
        if (protect) {
            out->m_protectPresent = true;
            /* [2026-11-15 审计·对齐 Python] 缺键回落**协议缺省**（0 =
             * 关闭该保护），不是 60000：原来回落 60000 ⇒ 老服务端没这个
             * 键时，界面显示"60 秒"（像配好了）⇒ 用户点「应用」就把保护
             * 打开成 60 秒 ⇒ 现场莫名开始停线。Python 侧
             * HSF_PROTECT_INLET_NOFLOW_TIMEOUT_MS_DEF 也是 0。 */
            out->m_protect.m_inletNoFlowTimeoutMs =
                hsfCfgSubInt(protect, "inletNoFlowTimeoutMs",
                             HSF_PROTECT_INLET_NOFLOW_TIMEOUT_MS_DEF);
            out->m_protect.m_inletNoFlowStop =
                hsfCfgSubBool(protect, "inletNoFlowStop",
                              HSF_PROTECT_INLET_NOFLOW_STOP_DEF);
            /* [压差过大保护 2026-11-15] 缺键（老服务端）⇒ 保持缺省。 */
            out->m_protect.m_pressHighEnabled =
                hsfCfgSubBool(protect, "pressHighEnabled",
                              HSF_PROTECT_PRESS_HIGH_ENABLED_DEF);
            out->m_protect.m_pressHighLimitMpa =
                hsfCfgSubDouble(protect, "pressHighLimitMpa",
                                HSF_PROTECT_PRESS_HIGH_LIMIT_MPA_DEF);
            out->m_protect.m_pressHighStop =
                hsfCfgSubBool(protect, "pressHighStop",
                              HSF_PROTECT_PRESS_HIGH_STOP_DEF);
        }
    }

    if (collect) {
        /* [节存在性 2026-11-12] 同 login/storage：缺 collect 节的局部
         * 应答不得让界面把采集卡冲回零值。 */
        out->m_collectPresent = true;
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
        out->m_storage.m_sampleMs = hsfCfgSubInt(storage, "sampleMs",
                                                  HSF_STORAGE_SAMPLE_MS_DEF);
        out->m_storage.m_maxMb = hsfCfgSubInt(storage, "maxMb",
                                               HSF_STORAGE_MAX_MB_DEF);
        hsfCfgSubText(storage, "fullPolicy", out->m_storage.m_fullPolicy,
                      (int)sizeof(out->m_storage.m_fullPolicy));
        out->m_storage.m_keepFiles = hsfCfgSubInt(storage, "keepFiles", 3);
        /* [入库触发 2026-11-12] 缺键回缺省 always（老服务端不静默改语义）。 */
        hsfCfgSubText(storage, "historyTrigger",
                      out->m_storage.m_historyTrigger,
                      (int)sizeof(out->m_storage.m_historyTrigger));
        if (strcmp(out->m_storage.m_historyTrigger, HSF_HISTORY_TRIGGER_AUTO) != 0)
            snprintf(out->m_storage.m_historyTrigger,
                     sizeof(out->m_storage.m_historyTrigger), "%s",
                     HSF_HISTORY_TRIGGER_ALWAYS);
    }
    /* 自动模式配置节（2026-11-11；旧服务端/局部应答没有该节 ⇒ m_autoPresent
     * 保持 false，界面**不**按零值铺回控件——与落盘节同一纪律）。 */
    {
        const XBsonDocument* autoSec = hsfCfgSubDoc(doc, HSF_CFG_SECTION_AUTO);
        const XBsonDocument* modes =
            autoSec ? hsfCfgSubDoc(autoSec, "modes") : NULL;
        if (autoSec && modes) {
            int m;
            out->m_auto.m_autoPresent = true;
            for (m = 0; m < HSF_AUTO_MODE_COUNT; ++m) {
                const XBsonDocument* one = hsfCfgSubDoc(modes, kModeIds[m]);
                HsfConfigAutoMode* dst = &out->m_auto.m_modes[m];
                /* [缺键回落缺省] 单个模式缺文档/缺键按出厂缺省铺（rpm=
                 * 铭牌额定点、正转、全不勾）——与服务端 applyModeDoc 的
                 * 「缺 devices 键=保持现值」口径在"整节缺失"层面的近似：
                 * GET 应答是全量快照，服务端总是三模式齐发。 */
                dst->m_stirRpm = HSF_AUTO_STIR_RPM_DEF;
                dst->m_stirReverse = false;
                dst->m_resetValvesOnStop =
                    (bool)HSF_AUTO_MODE_RESET_VALVES_ON_STOP_DEF;
                dst->m_resetValvesOnEstop =
                    (bool)HSF_AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF;
                if (!one) continue;
                hsfCfgAutoParseDevices(one, dst->m_devices);
                dst->m_stirRpm = hsfCfgSubInt(one, "stirRpm",
                                              HSF_AUTO_STIR_RPM_DEF);
                if (dst->m_stirRpm < HSF_AUTO_STIR_RPM_MIN)
                    dst->m_stirRpm = HSF_AUTO_STIR_RPM_MIN;
                if (dst->m_stirRpm > HSF_AUTO_STIR_RPM_MAX)
                    dst->m_stirRpm = HSF_AUTO_STIR_RPM_MAX;
                dst->m_stirReverse = hsfCfgSubBool(one, "stirReverse", false);
                dst->m_resetValvesOnStop = hsfCfgSubBool(one,
                    HSF_AUTO_MODE_RESET_VALVES_ON_STOP,
                    HSF_AUTO_MODE_RESET_VALVES_ON_STOP_DEF);
                dst->m_resetValvesOnEstop = hsfCfgSubBool(one,
                    HSF_AUTO_MODE_RESET_VALVES_ON_ESTOP,
                    HSF_AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF);
            }
            /* [自动循环子节 2026-11-12] 与 modes 平级；缺子节保持缺省。 */
            {
                const XBsonDocument* cyc = hsfCfgSubDoc(autoSec,
                                                        HSF_CFGK_AUTO_CYCLE);
                HsfConfigAutoCycle* c = &out->m_auto.m_cycle;
                c->m_enabled = HSF_AUTO_CYCLE_ENABLED_DEF;
                c->m_pressThreshold = HSF_AUTO_CYCLE_PRESS_THRESHOLD_DEF;
                snprintf(c->m_pressCompare, sizeof(c->m_pressCompare), "%s",
                         HSF_AUTO_CYCLE_PRESS_COMPARE_DEF);
                c->m_timerFallback = HSF_AUTO_CYCLE_TIMER_FALLBACK_DEF;
                c->m_timerMinutes = HSF_AUTO_CYCLE_TIMER_MINUTES_DEF;
                c->m_backwashMinutes = HSF_AUTO_CYCLE_BACKWASH_MINUTES_DEF;
                c->m_rinseMinutes = HSF_AUTO_CYCLE_RINSE_MINUTES_DEF;
                if (cyc) {
                    out->m_auto.m_cyclePresent = true;
                    c->m_enabled = hsfCfgSubBool(cyc,
                        HSF_CFGK_AUTO_CYCLE_ENABLED, c->m_enabled);
                    c->m_pressThreshold = hsfCfgSubDouble(cyc,
                        HSF_CFGK_AUTO_CYCLE_PRESS_THRESHOLD,
                        c->m_pressThreshold);
                    if (c->m_pressThreshold < 0.0) c->m_pressThreshold = 0.0;
                    if (c->m_pressThreshold > HSF_AUTO_CYCLE_PRESS_THRESHOLD_MAX)
                        c->m_pressThreshold = HSF_AUTO_CYCLE_PRESS_THRESHOLD_MAX;
                    hsfCfgSubText(cyc, HSF_CFGK_AUTO_CYCLE_PRESS_COMPARE,
                                  c->m_pressCompare,
                                  (int)sizeof(c->m_pressCompare));
                    if (strcmp(c->m_pressCompare, HSF_AUTO_PRESS_COMPARE_GT) != 0 &&
                        strcmp(c->m_pressCompare, HSF_AUTO_PRESS_COMPARE_LT) != 0)
                        snprintf(c->m_pressCompare, sizeof(c->m_pressCompare),
                                 "%s", HSF_AUTO_CYCLE_PRESS_COMPARE_DEF);
                    c->m_timerFallback = hsfCfgSubBool(cyc,
                        HSF_CFGK_AUTO_CYCLE_TIMER_FALLBACK, c->m_timerFallback);
                    c->m_timerMinutes = hsfCfgSubInt(cyc,
                        HSF_CFGK_AUTO_CYCLE_TIMER_MINUTES, c->m_timerMinutes);
                    c->m_backwashMinutes = hsfCfgSubInt(cyc,
                        HSF_CFGK_AUTO_CYCLE_BACKWASH_MINUTES, c->m_backwashMinutes);
                    c->m_rinseMinutes = hsfCfgSubInt(cyc,
                        HSF_CFGK_AUTO_CYCLE_RINSE_MINUTES, c->m_rinseMinutes);
                }
                if (!out->m_auto.m_autoPresent)
                    out->m_auto.m_autoPresent = true;
            }
        }
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
    /* [统计节存在性 2026-11-12] 库路径或统计任一存在即置位（Python 同口径：
     * `if "storageDbPath" in doc or "storageStats" in doc`）——界面据此决定
     * 要不要刷新「当前库文件 / 用量」两行（点一次登录策略不该把它们清空）。 */
    if (stats || v)
        out->m_statsPresent = true;
    out->m_valid = true;
    XClassDelete((XClass*)doc);
    return true;
}
