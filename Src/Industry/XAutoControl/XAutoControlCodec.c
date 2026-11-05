/**
 * @file       XAutoControlCodec.c
 * @brief      自动运行控制器状态/报警编解码实现：运行快照与告警 ↔ JSON/BSON。
 * @details    [实现捷径] 与 XSolenoidValveCodec / XVfdCodec 同款：JSON 与
 *             BSON 共用一张键映射表，编码先建 BSON 文档再经
 *             XBsonDocument_toJson 出 JSON；解码 JSON 先归一到 BSON 再走
 *             同一读取路径（XJsonDocument → XBsonDocument_fromJsonObject）。
 */
#include "XAutoControlCodec.h"

#if XINDUSTRY_ON && XAUTO_CONTROL_ON

#include "XBson.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XJson.h"
#include "XJsonDocument.h"
#include "XJsonObject.h"
#include "XMemory.h"
#include "XString.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================== */
/*                      键映射表                                              */
/* ========================================================================== */

/** @brief XAutoControlField → 线上键名（表驱动；数值稳定可直接上线）。 */
typedef struct XAutoControlCodecEntry {
    XAutoControlField m_field;  /**< 快照字段键。 */
    const char* m_key;          /**< 线上键名（静态常量）。 */
} XAutoControlCodecEntry;

static const XAutoControlCodecEntry s_autoCtlFieldTable[] = {
    { XAutoControlField_Running,         "running" },
    { XAutoControlField_Phase,           "phase" },
    { XAutoControlField_Actions,         "actions" },
    { XAutoControlField_LastError,       "lastError" },
    { XAutoControlField_StepIntervalMs,  "stepIntervalMs" },
    { XAutoControlField_PendingAlarms,   "pendingAlarms" },
    { XAutoControlField_State,           "state" }
};

#define XAUTOCTL_CODEC_FIELD_COUNT \
    (int)(sizeof(s_autoCtlFieldTable) / sizeof(s_autoCtlFieldTable[0]))

/** @brief 全键现成列表。 */
static const XAutoControlField s_autoCtlAllKeys[] = {
    XAutoControlField_Running,
    XAutoControlField_Phase,
    XAutoControlField_Actions,
    XAutoControlField_LastError,
    XAutoControlField_StepIntervalMs,
    XAutoControlField_PendingAlarms,
    XAutoControlField_State
};

/* 报警事件帧键（恒全量，无键裁剪）。 */
#define XAUTOCTL_CODEC_ALARM_KIND "kind"    /**< 报警类型（int32）。 */
#define XAUTOCTL_CODEC_ALARM_AT   "atMs"    /**< 发生时刻（int64 单调毫秒）。 */
#define XAUTOCTL_CODEC_ALARM_TEXT "text"    /**< 报警文字（utf8）。 */

/* ========================================================================== */
/*                      内部工具                                              */
/* ========================================================================== */

/**
 * @brief 把一个字段写入 BSON 文档（值取自快照）。
 * @return 写入成功返回 true；字段未知返回 false。
 */
static bool xautoCtlCodecWriteField(XBsonDocument* doc,
                                    const XAutoControlCodecEntry* entry,
                                    const XAutoControlStatus* st)
{
    switch (entry->m_field) {
    case XAutoControlField_Running:
        return XBsonDocument_insert_keyUtf8_bool(doc, entry->m_key, st->m_running);
    case XAutoControlField_Phase:
        return XBsonDocument_insert_keyUtf8_utf8(doc, entry->m_key, st->m_phase);
    case XAutoControlField_Actions:
        return XBsonDocument_insert_keyUtf8_int64(doc, entry->m_key, st->m_actions);
    case XAutoControlField_LastError:
        return XBsonDocument_insert_keyUtf8_utf8(doc, entry->m_key, st->m_lastError);
    case XAutoControlField_StepIntervalMs:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key,
                                                  (int32_t)st->m_stepIntervalMs);
    case XAutoControlField_PendingAlarms:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key,
                                                  st->m_pendingAlarms);
    case XAutoControlField_State:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key, st->m_state);
    default:
        return false;
    }
}

/**
 * @brief 快照 → BSON 文档（按键列表裁剪；编码公共前半段）。
 * @return 新建文档；分配失败返回 NULL。@warning 调用方 XClassDelete。
 */
static XBsonDocument* xautoCtlCodecBuildStatusDoc(const XAutoControlStatus* st,
                                                  const XAutoControlField* keys,
                                                  int count)
{
    XBsonDocument* doc = XBsonDocument_create();
    XAutoControlStatus zero;
    const XAutoControlStatus* src = st;
    int i;
    if (!doc) return NULL;
    if (!src) {
        memset(&zero, 0, sizeof(zero));
        src = &zero;
    }
    for (i = 0; keys && i < count; ++i) {
        int t;
        for (t = 0; t < XAUTOCTL_CODEC_FIELD_COUNT; ++t) {
            if (s_autoCtlFieldTable[t].m_field == keys[i]) {
                if (!xautoCtlCodecWriteField(doc, &s_autoCtlFieldTable[t], src)) {
                    XClassDelete((XClass*)doc);
                    return NULL;
                }
                break;
            }
        }
        /* 未知键静默跳过（前向兼容）。 */
    }
    return doc;
}

/**
 * @brief BSON 文档 → 快照（解码公共后半段）。
 */
static void xautoCtlCodecFillStatus(XAutoControlStatus* out,
                                    const XBsonDocument* doc)
{
    XBsonValue* v;
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "running");
    if (v) out->m_running = XBsonValue_toBool(v, false);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "phase");
    if (v) {
        const XString* s = XBsonValue_toString(v);
        if (s) {
            const char* utf8 = XString_toUtf8(s);
            if (utf8)
                snprintf(out->m_phase, sizeof(out->m_phase), "%s", utf8);
        }
    }
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "actions");
    if (v) out->m_actions = XBsonValue_toInt64(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "lastError");
    if (v) {
        const XString* s = XBsonValue_toString(v);
        if (s) {
            const char* utf8 = XString_toUtf8(s);
            if (utf8)
                snprintf(out->m_lastError, sizeof(out->m_lastError), "%s", utf8);
        }
    }
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "stepIntervalMs");
    if (v) out->m_stepIntervalMs = (uint32_t)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "pendingAlarms");
    if (v) out->m_pendingAlarms = XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "state");
    if (v) out->m_state = XBsonValue_toInt32(v, 0);
}

/**
 * @brief JSON 文本字节 → BSON 文档（解码归一捷径，XVfdCodec 同款）。
 */
static XBsonDocument* xautoCtlCodecJsonToDoc(const uint8_t* data, size_t size)
{
    XByteArray* bytes;
    XJsonDocument* json;
    XJsonObject* obj;
    XBsonDocument* doc = NULL;
    bytes = XByteArray_create();
    if (!bytes) return NULL;
    if (!XByteArray_push_back_2(bytes, data, size)) {
        XClassDelete((XClass*)bytes);
        return NULL;
    }
    json = XJsonDocument_fromJson(bytes);
    XClassDelete((XClass*)bytes);
    if (!json) return NULL;
    obj = XJsonDocument_object(json);
    if (obj)
        doc = XBsonDocument_fromJsonObject(obj);
    XClassDelete((XClass*)json);
    return doc;
}

/**
 * @brief 报警 → BSON 文档（编码公共前半段；NULL 视为全零报警）。
 */
static XBsonDocument* xautoCtlCodecBuildAlarmDoc(const XAutoControlAlarm* alarm)
{
    XBsonDocument* doc = XBsonDocument_create();
    if (!doc) return NULL;
    if (!XBsonDocument_insert_keyUtf8_int32(doc, XAUTOCTL_CODEC_ALARM_KIND,
                                            alarm ? alarm->m_kind : 0) ||
        !XBsonDocument_insert_keyUtf8_int64(doc, XAUTOCTL_CODEC_ALARM_AT,
                                            alarm ? (int64_t)alarm->m_atMs : 0) ||
        !XBsonDocument_insert_keyUtf8_utf8(doc, XAUTOCTL_CODEC_ALARM_TEXT,
                                           alarm ? alarm->m_text : "")) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    return doc;
}

/**
 * @brief BSON 文档 → 报警（解码公共后半段）。
 */
static void xautoCtlCodecFillAlarm(XAutoControlAlarm* out,
                                   const XBsonDocument* doc)
{
    XBsonValue* v;
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, XAUTOCTL_CODEC_ALARM_KIND);
    if (v) out->m_kind = XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, XAUTOCTL_CODEC_ALARM_AT);
    if (v) out->m_atMs = (uint64_t)XBsonValue_toInt64(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, XAUTOCTL_CODEC_ALARM_TEXT);
    if (v) {
        const XString* s = XBsonValue_toString(v);
        if (s) {
            const char* utf8 = XString_toUtf8(s);
            if (utf8)
                snprintf(out->m_text, sizeof(out->m_text), "%s", utf8);
        }
    }
}

/* ========================================================================== */
/*                      状态编码（控制器 → 载荷）                              */
/* ========================================================================== */

XByteArray* XAutoControlCodec_statusToBson(const XAutoControlStatus* st,
                                           const XAutoControlField* keys, int count)
{
    XBsonDocument* doc = xautoCtlCodecBuildStatusDoc(st, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

XByteArray* XAutoControlCodec_statusToJson(const XAutoControlStatus* st,
                                           const XAutoControlField* keys, int count)
{
    XBsonDocument* doc = xautoCtlCodecBuildStatusDoc(st, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toJson(doc, XJsonDocument_Compact);
    XClassDelete((XClass*)doc);
    return bytes;
}

const XAutoControlField* XAutoControlCodec_allKeys(int* outCount)
{
    if (!outCount) return NULL;
    *outCount = (int)(sizeof(s_autoCtlAllKeys) / sizeof(s_autoCtlAllKeys[0]));
    return s_autoCtlAllKeys;
}

/* ========================================================================== */
/*                      状态解码（载荷 → 快照）                                */
/* ========================================================================== */

bool XAutoControlCodec_statusFromBson(XAutoControlStatus* out,
                                      const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (!XBsonDocument_from_bytes(doc, data, size)) {
        XClassDelete((XClass*)doc);
        return false;
    }
    xautoCtlCodecFillStatus(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

bool XAutoControlCodec_statusFromJson(XAutoControlStatus* out,
                                      const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = xautoCtlCodecJsonToDoc(data, size);
    if (!doc) return false;
    xautoCtlCodecFillStatus(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

/* ========================================================================== */
/*                      报警编解码（事件帧）                                   */
/* ========================================================================== */

XByteArray* XAutoControlCodec_alarmToBson(const XAutoControlAlarm* alarm)
{
    XBsonDocument* doc = xautoCtlCodecBuildAlarmDoc(alarm);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

XByteArray* XAutoControlCodec_alarmToJson(const XAutoControlAlarm* alarm)
{
    XBsonDocument* doc = xautoCtlCodecBuildAlarmDoc(alarm);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toJson(doc, XJsonDocument_Compact);
    XClassDelete((XClass*)doc);
    return bytes;
}

bool XAutoControlCodec_alarmFromBson(XAutoControlAlarm* out,
                                     const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (!XBsonDocument_from_bytes(doc, data, size)) {
        XClassDelete((XClass*)doc);
        return false;
    }
    xautoCtlCodecFillAlarm(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

bool XAutoControlCodec_alarmFromJson(XAutoControlAlarm* out,
                                     const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = xautoCtlCodecJsonToDoc(data, size);
    if (!doc) return false;
    xautoCtlCodecFillAlarm(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

#endif /* XINDUSTRY_ON && XAUTO_CONTROL_ON */
