/**
 * @file       XTurbidityMeterCodec.c
 * @brief      浊度仪状态编解码实现。
 * @details    与 XFlowMeterCodec 同款捷径：JSON 与 BSON 共用一张键映射表，
 *             编码先建 BSON 文档再经 XBsonDocument_toJson 出 JSON；解码 JSON
 *             先归一到 BSON 再走同一读取路径（键名两格式一致）。
 */
#include "XTurbidityMeterCodec.h"

#if XINDUSTRY_ON && XTURBIDITY_ON

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

/** @brief XTurbidityMeterField → 线上键名（表驱动）。 */
typedef struct XTurbidityMeterCodecEntry {
    XTurbidityMeterField m_field;       /**< 快照字段键。 */
    const char* m_key;                  /**< 线上键名（静态常量）。 */
} XTurbidityMeterCodecEntry;

static const XTurbidityMeterCodecEntry s_turbidityFieldTable[] = {
    { XTurbidityMeterField_Ntu,     "ntu" },
    { XTurbidityMeterField_RawBits, "rawBits" },
    { XTurbidityMeterField_Source,  "source" },
    { XTurbidityMeterField_Valid,   "valid" },
    { XTurbidityMeterField_StaleMs, "staleMs" }
};

#define XTURBIDITY_CODEC_FIELD_COUNT \
    (int)(sizeof(s_turbidityFieldTable) / sizeof(s_turbidityFieldTable[0]))

/** @brief 全键现成列表。 */
static const XTurbidityMeterField s_turbidityAllKeys[] = {
    XTurbidityMeterField_Ntu,
    XTurbidityMeterField_RawBits,
    XTurbidityMeterField_Source,
    XTurbidityMeterField_Valid,
    XTurbidityMeterField_StaleMs
};

/* ==================== 内部工具 ==================== */

/**
 * @brief 用浊度仪当前状态构建快照（NULL 视为全零）。
 */
static void turbidityCodecMakeSnapshot(XTurbidityMeterSnapshot* out,
                                       const XTurbidityMeter* meter)
{
    memset(out, 0, sizeof(*out));
    if (!meter) return;
    /* 值恒带出（无效时为 0），质量位由设备判定——解码方按需展示。 */
    (void)XTurbidityMeter_value(meter, &out->m_ntu);
    (void)XTurbidityMeter_rawWords(meter, &out->m_rawBits);
    out->m_source = (int32_t)XTurbidityMeter_sourceMode(meter);
    out->m_valid = XTurbidityMeter_isValid(meter);
    out->m_staleMs = XTurbidityMeter_staleMs(meter);
}

/**
 * @brief 把一个字段写入 BSON 文档（值取自快照）。
 * @return 写入成功返回 true；字段未知返回 false。
 */
static bool turbidityCodecWriteField(XBsonDocument* doc,
                                     const XTurbidityMeterCodecEntry* entry,
                                     const XTurbidityMeterSnapshot* snap)
{
    switch (entry->m_field) {
    case XTurbidityMeterField_Ntu:
        return XBsonDocument_insert_keyUtf8_double(doc, entry->m_key, snap->m_ntu);
    case XTurbidityMeterField_RawBits:
        /* uint32 装进 int64 保留全部 32 位（int32 会把高位当负数）。 */
        return XBsonDocument_insert_keyUtf8_int64(doc, entry->m_key,
                                                  (int64_t)snap->m_rawBits);
    case XTurbidityMeterField_Source:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key, snap->m_source);
    case XTurbidityMeterField_Valid:
        return XBsonDocument_insert_keyUtf8_bool(doc, entry->m_key, snap->m_valid);
    case XTurbidityMeterField_StaleMs:
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
static XBsonDocument* turbidityCodecBuildStateDoc(const XTurbidityMeter* meter,
                                                  const XTurbidityMeterField* keys,
                                                  int count)
{
    XTurbidityMeterSnapshot snap;
    XBsonDocument* doc = XBsonDocument_create();
    int i;
    if (!doc) return NULL;
    turbidityCodecMakeSnapshot(&snap, meter);
    for (i = 0; keys && i < count; ++i) {
        int t;
        for (t = 0; t < XTURBIDITY_CODEC_FIELD_COUNT; ++t) {
            if (s_turbidityFieldTable[t].m_field == keys[i]) {
                if (!turbidityCodecWriteField(doc, &s_turbidityFieldTable[t], &snap)) {
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
 * @brief 宽容读 double：BSON DOUBLE/INT32/INT64 三种类型都接受。
 * @details 与 XFlowMeterCodec 同款实锅：XBsonValue_toDouble 仅在类型为 DOUBLE
 *          时返回值，其它类型直接给默认值。JSON 文本里不带小数点的数（如
 *          "ntu": 12）解析后成为整型，转成 BSON 便是 INT32/INT64，经 toDouble
 *          读回落在默认值 0 上——整数字段的 double 往返会静默丢值。
 * @param v 值对象；NULL 返回 0。
 * @return 数值；不可解释时返回 0。
 */
static double turbidityCodecValueToDouble(const XBsonValue* v)
{
    if (!v) return 0.0;
    if (XBsonValue_isDouble(v)) return XBsonValue_toDouble(v, 0.0);
    if (XBsonValue_isInt32(v) || XBsonValue_isInt64(v))
        return (double)XBsonValue_toInt64(v, 0);
    return 0.0;
}

/**
 * @brief BSON 文档 → 快照（解码公共后半段）。
 */
static void turbidityCodecFillSnapshot(XTurbidityMeterSnapshot* out,
                                       const XBsonDocument* doc)
{
    XBsonValue* v;
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "ntu");
    if (v) out->m_ntu = turbidityCodecValueToDouble(v);
    /* rawBits 走 int64 读：uint32 全范围都在 int64 内，不会被当成负数。 */
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "rawBits");
    if (v) out->m_rawBits = (uint32_t)XBsonValue_toInt64(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "source");
    if (v) out->m_source = XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "valid");
    if (v) out->m_valid = XBsonValue_toBool(v, false);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "staleMs");
    if (v) out->m_staleMs = (uint32_t)XBsonValue_toInt64(v, 0);
}

/**
 * @brief JSON 字节 → BSON 文档（解码公共前半段）。
 * @return 新建 BSON 文档；失败返回 NULL。@warning 调用方 XClassDelete。
 */
static XBsonDocument* turbidityCodecJsonToDoc(const uint8_t* data, size_t size)
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

XByteArray* XTurbidityMeterCodec_stateToBson(const XTurbidityMeter* meter,
                                             const XTurbidityMeterField* keys,
                                             int count)
{
    XBsonDocument* doc = turbidityCodecBuildStateDoc(meter, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

XByteArray* XTurbidityMeterCodec_stateToJson(const XTurbidityMeter* meter,
                                             const XTurbidityMeterField* keys,
                                             int count)
{
    XBsonDocument* doc = turbidityCodecBuildStateDoc(meter, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toJson(doc, XJsonDocument_Compact);
    XClassDelete((XClass*)doc);
    return bytes;
}

const XTurbidityMeterField* XTurbidityMeterCodec_allKeys(int* outCount)
{
    if (!outCount) return NULL;
    *outCount = (int)(sizeof(s_turbidityAllKeys) / sizeof(s_turbidityAllKeys[0]));
    return s_turbidityAllKeys;
}

bool XTurbidityMeterCodec_stateFromBson(XTurbidityMeterSnapshot* out,
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
    turbidityCodecFillSnapshot(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

bool XTurbidityMeterCodec_stateFromJson(XTurbidityMeterSnapshot* out,
                                        const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = turbidityCodecJsonToDoc(data, size);
    if (!doc) return false;
    turbidityCodecFillSnapshot(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

#endif /* XINDUSTRY_ON && XTURBIDITY_ON */
