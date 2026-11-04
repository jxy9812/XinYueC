
/**
 * @file       XDiffPressureGaugeCodec.c
 * @brief      压差计状态编解码实现。
 * @details    与 XSolenoidValveCodec / XTempHumTransmitterCodec 同款捷径：
 *             JSON 与 BSON 共用一张键映射表，编码先建 BSON 文档再经
 *             XBsonDocument_toJson 出 JSON；解码 JSON 先归一到 BSON 再走
 *             同一读取路径（键名两格式一致）。
 */
#include "XDiffPressureGaugeCodec.h"

#if XINDUSTRY_ON && XPRESS_DIFF_ON

#include "XBson.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XJson.h"
#include "XJsonDocument.h"
#include "XJsonObject.h"
#include "XMemory.h"
#include "XString.h"
#include <string.h>

/* ==================== 键映射表 ==================== */

/** @brief XDiffPressureGaugeField → 线上键名（表驱动）。 */
typedef struct XDiffPressureCodecEntry {
    XDiffPressureGaugeField m_field;    /**< 快照字段键。 */
    const char* m_key;                  /**< 线上键名（静态常量）。 */
} XDiffPressureCodecEntry;

static const XDiffPressureCodecEntry s_diffPressureFieldTable[] = {
    { XDiffPressureGaugeField_Value,   "value" },
    { XDiffPressureGaugeField_Valid,   "valid" },
    { XDiffPressureGaugeField_StaleMs, "staleMs" }
};

#define XDIFFPRESSURE_CODEC_FIELD_COUNT \
    (int)(sizeof(s_diffPressureFieldTable) / sizeof(s_diffPressureFieldTable[0]))

/** @brief 全键现成列表。 */
static const XDiffPressureGaugeField s_diffPressureAllKeys[] = {
    XDiffPressureGaugeField_Value,
    XDiffPressureGaugeField_Valid,
    XDiffPressureGaugeField_StaleMs
};

/* ==================== 内部工具 ==================== */

/**
 * @brief 用压差计当前状态构建快照（NULL 视为全零）。
 */
static void diffCodecMakeSnapshot(XDiffPressureGaugeSnapshot* out,
                                  const XDiffPressureGauge* gauge)
{
    memset(out, 0, sizeof(*out));
    if (!gauge) return;
    /* 值恒带出（无效时为 0），质量位由设备判定——解码方按需展示。 */
    (void)XDiffPressureGauge_value(gauge, &out->m_valueEng);
    out->m_valid = XDiffPressureGauge_isValid(gauge);
    out->m_staleMs = XDiffPressureGauge_staleMs(gauge);
}

/**
 * @brief 把一个字段写入 BSON 文档（值取自快照）。
 * @return 写入成功返回 true；字段未知返回 false。
 */
static bool diffCodecWriteField(XBsonDocument* doc,
                                const XDiffPressureCodecEntry* entry,
                                const XDiffPressureGaugeSnapshot* snap)
{
    switch (entry->m_field) {
    case XDiffPressureGaugeField_Value:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key, snap->m_valueEng);
    case XDiffPressureGaugeField_Valid:
        return XBsonDocument_insert_keyUtf8_bool(doc, entry->m_key, snap->m_valid);
    case XDiffPressureGaugeField_StaleMs:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key,
                                                  (int32_t)snap->m_staleMs);
    default:
        return false;
    }
}

/**
 * @brief 快照 → BSON 文档（按键列表裁剪；编码公共前半段）。
 * @return 新建文档；分配失败返回 NULL。@warning 调用方 XClassDelete。
 */
static XBsonDocument* diffCodecBuildStateDoc(const XDiffPressureGauge* gauge,
                                             const XDiffPressureGaugeField* keys,
                                             int count)
{
    XDiffPressureGaugeSnapshot snap;
    XBsonDocument* doc = XBsonDocument_create();
    int i;
    if (!doc) return NULL;
    diffCodecMakeSnapshot(&snap, gauge);
    for (i = 0; keys && i < count; ++i) {
        int t;
        for (t = 0; t < XDIFFPRESSURE_CODEC_FIELD_COUNT; ++t) {
            if (s_diffPressureFieldTable[t].m_field == keys[i]) {
                if (!diffCodecWriteField(doc, &s_diffPressureFieldTable[t], &snap)) {
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
static void diffCodecFillSnapshot(XDiffPressureGaugeSnapshot* out,
                                  const XBsonDocument* doc)
{
    XBsonValue* v;
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "value");
    if (v) out->m_valueEng = XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "valid");
    if (v) out->m_valid = XBsonValue_toBool(v, false);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "staleMs");
    if (v) out->m_staleMs = (uint32_t)XBsonValue_toInt64(v, 0);
}

/**
 * @brief JSON 字节 → BSON 文档（解码公共前半段）。
 * @return 新建 BSON 文档；失败返回 NULL。@warning 调用方 XClassDelete。
 */
static XBsonDocument* diffCodecJsonToDoc(const uint8_t* data, size_t size)
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

/* ==================== 公开 API ==================== */

XByteArray* XDiffPressureGaugeCodec_stateToBson(const XDiffPressureGauge* gauge,
                                                const XDiffPressureGaugeField* keys,
                                                int count)
{
    XBsonDocument* doc = diffCodecBuildStateDoc(gauge, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

XByteArray* XDiffPressureGaugeCodec_stateToJson(const XDiffPressureGauge* gauge,
                                                const XDiffPressureGaugeField* keys,
                                                int count)
{
    XBsonDocument* doc = diffCodecBuildStateDoc(gauge, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toJson(doc, XJsonDocument_Compact);
    XClassDelete((XClass*)doc);
    return bytes;
}

const XDiffPressureGaugeField* XDiffPressureGaugeCodec_allKeys(int* outCount)
{
    if (!outCount) return NULL;
    *outCount = (int)(sizeof(s_diffPressureAllKeys) / sizeof(s_diffPressureAllKeys[0]));
    return s_diffPressureAllKeys;
}

bool XDiffPressureGaugeCodec_stateFromBson(XDiffPressureGaugeSnapshot* out,
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
    diffCodecFillSnapshot(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

bool XDiffPressureGaugeCodec_stateFromJson(XDiffPressureGaugeSnapshot* out,
                                           const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = diffCodecJsonToDoc(data, size);
    if (!doc) return false;
    diffCodecFillSnapshot(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

#endif /* XINDUSTRY_ON && XPRESS_DIFF_ON */
