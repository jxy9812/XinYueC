
/**
 * @file       XFlowMeterCodec.c
 * @brief      电磁流量计状态编解码实现。
 * @details    与 XDiffPressureGaugeCodec / XTempHumTransmitterCodec 同款捷径：
 *             JSON 与 BSON 共用一张键映射表，编码先建 BSON 文档再经
 *             XBsonDocument_toJson 出 JSON；解码 JSON 先归一到 BSON 再走
 *             同一读取路径（键名两格式一致）。
 */
#include "XFlowMeterCodec.h"

#if XINDUSTRY_ON && XFLOW_METER_ON

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

/** @brief XFlowMeterField → 线上键名（表驱动）。 */
typedef struct XFlowMeterCodecEntry {
    XFlowMeterField m_field;            /**< 快照字段键。 */
    const char* m_key;                  /**< 线上键名（静态常量）。 */
} XFlowMeterCodecEntry;

static const XFlowMeterCodecEntry s_flowMeterFieldTable[] = {
    { XFlowMeterField_InstantFlow,  "flow" },
    { XFlowMeterField_InstantSpeed, "speed" },
    { XFlowMeterField_FlowPercent,  "percent" },
    { XFlowMeterField_Conductivity, "conductivity" },
    { XFlowMeterField_ForwardTotal, "forwardTotal" },
    { XFlowMeterField_ReverseTotal, "reverseTotal" },
    { XFlowMeterField_FlowUnit,     "flowUnit" },
    { XFlowMeterField_TotalUnit,    "totalUnit" },
    { XFlowMeterField_Alarms,       "alarms" },
    { XFlowMeterField_Valid,        "valid" },
    { XFlowMeterField_StaleMs,      "staleMs" }
};

#define XFLOWMETER_CODEC_FIELD_COUNT \
    (int)(sizeof(s_flowMeterFieldTable) / sizeof(s_flowMeterFieldTable[0]))

/** @brief 全键现成列表。 */
static const XFlowMeterField s_flowMeterAllKeys[] = {
    XFlowMeterField_InstantFlow,
    XFlowMeterField_InstantSpeed,
    XFlowMeterField_FlowPercent,
    XFlowMeterField_Conductivity,
    XFlowMeterField_ForwardTotal,
    XFlowMeterField_ReverseTotal,
    XFlowMeterField_FlowUnit,
    XFlowMeterField_TotalUnit,
    XFlowMeterField_Alarms,
    XFlowMeterField_Valid,
    XFlowMeterField_StaleMs
};

/* ==================== 内部工具 ==================== */

/**
 * @brief 用流量计当前状态构建快照（NULL 视为全零）。
 */
static void flowCodecMakeSnapshot(XFlowMeterSnapshot* out, const XFlowMeter* meter)
{
    memset(out, 0, sizeof(*out));
    if (!meter) return;
    /* 各量恒带出（无效时为 0），质量位由设备判定——解码方按需展示。 */
    (void)XFlowMeter_value(meter, XFlowMeterQuantity_InstantFlow, &out->m_instantFlow);
    (void)XFlowMeter_value(meter, XFlowMeterQuantity_InstantSpeed, &out->m_instantSpeed);
    (void)XFlowMeter_value(meter, XFlowMeterQuantity_FlowPercent, &out->m_flowPercent);
    (void)XFlowMeter_value(meter, XFlowMeterQuantity_Conductivity, &out->m_conductivity);
    (void)XFlowMeter_value(meter, XFlowMeterQuantity_ForwardTotal, &out->m_forwardTotal);
    (void)XFlowMeter_value(meter, XFlowMeterQuantity_ReverseTotal, &out->m_reverseTotal);
    XFlowMeter_unitCodes(meter, &out->m_flowUnitCode, &out->m_totalUnitCode);
    out->m_alarmBits = XFlowMeter_alarms(meter);
    out->m_valid = XFlowMeter_isValid(meter);
    out->m_staleMs = XFlowMeter_staleMs(meter);
}

/**
 * @brief 把一个字段写入 BSON 文档（值取自快照）。
 * @return 写入成功返回 true；字段未知返回 false。
 */
static bool flowCodecWriteField(XBsonDocument* doc, const XFlowMeterCodecEntry* entry,
                                const XFlowMeterSnapshot* snap)
{
    switch (entry->m_field) {
    case XFlowMeterField_InstantFlow:
        return XBsonDocument_insert_keyUtf8_double(doc, entry->m_key, snap->m_instantFlow);
    case XFlowMeterField_InstantSpeed:
        return XBsonDocument_insert_keyUtf8_double(doc, entry->m_key, snap->m_instantSpeed);
    case XFlowMeterField_FlowPercent:
        return XBsonDocument_insert_keyUtf8_double(doc, entry->m_key, snap->m_flowPercent);
    case XFlowMeterField_Conductivity:
        return XBsonDocument_insert_keyUtf8_double(doc, entry->m_key, snap->m_conductivity);
    case XFlowMeterField_ForwardTotal:
        return XBsonDocument_insert_keyUtf8_double(doc, entry->m_key, snap->m_forwardTotal);
    case XFlowMeterField_ReverseTotal:
        return XBsonDocument_insert_keyUtf8_double(doc, entry->m_key, snap->m_reverseTotal);
    case XFlowMeterField_FlowUnit:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key,
                                                  (int32_t)snap->m_flowUnitCode);
    case XFlowMeterField_TotalUnit:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key,
                                                  (int32_t)snap->m_totalUnitCode);
    case XFlowMeterField_Alarms:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key,
                                                  (int32_t)snap->m_alarmBits);
    case XFlowMeterField_Valid:
        return XBsonDocument_insert_keyUtf8_bool(doc, entry->m_key, snap->m_valid);
    case XFlowMeterField_StaleMs:
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
static XBsonDocument* flowCodecBuildStateDoc(const XFlowMeter* meter,
                                             const XFlowMeterField* keys, int count)
{
    XFlowMeterSnapshot snap;
    XBsonDocument* doc = XBsonDocument_create();
    int i;
    if (!doc) return NULL;
    flowCodecMakeSnapshot(&snap, meter);
    for (i = 0; keys && i < count; ++i) {
        int t;
        for (t = 0; t < XFLOWMETER_CODEC_FIELD_COUNT; ++t) {
            if (s_flowMeterFieldTable[t].m_field == keys[i]) {
                if (!flowCodecWriteField(doc, &s_flowMeterFieldTable[t], &snap)) {
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
 * @details [2026-11-04 实锅] 旧版 XBsonValue_toDouble 仅在类型为 DOUBLE 时
 *          返回值。JSON 文本里不带小数点的数（如整数累积量 19088743）解析后
 *          成为整型，转成 BSON 便是 INT32/INT64，经 toDouble 读回落在默认值
 *          0 上——整数字段的 double 往返会静默丢值。现 toDouble 已与
 *          XJsonValue_toDouble 统一为整数拓宽；保留显式分支兼容旧构建。
 * @param v 值对象；NULL 返回 0。
 * @return 数值；不可解释时返回 0。
 */
static double flowCodecValueToDouble(const XBsonValue* v)
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
static void flowCodecFillSnapshot(XFlowMeterSnapshot* out, const XBsonDocument* doc)
{
    XBsonValue* v;
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "flow");
    if (v) out->m_instantFlow = flowCodecValueToDouble(v);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "speed");
    if (v) out->m_instantSpeed = flowCodecValueToDouble(v);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "percent");
    if (v) out->m_flowPercent = flowCodecValueToDouble(v);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "conductivity");
    if (v) out->m_conductivity = flowCodecValueToDouble(v);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "forwardTotal");
    if (v) out->m_forwardTotal = flowCodecValueToDouble(v);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "reverseTotal");
    if (v) out->m_reverseTotal = flowCodecValueToDouble(v);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "flowUnit");
    if (v) out->m_flowUnitCode = (uint16_t)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "totalUnit");
    if (v) out->m_totalUnitCode = (uint16_t)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "alarms");
    if (v) out->m_alarmBits = (uint8_t)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "valid");
    if (v) out->m_valid = XBsonValue_toBool(v, false);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "staleMs");
    if (v) out->m_staleMs = (uint32_t)XBsonValue_toInt64(v, 0);
}

/**
 * @brief JSON 字节 → BSON 文档（解码公共前半段）。
 * @return 新建 BSON 文档；失败返回 NULL。@warning 调用方 XClassDelete。
 */
static XBsonDocument* flowCodecJsonToDoc(const uint8_t* data, size_t size)
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

XByteArray* XFlowMeterCodec_stateToBson(const XFlowMeter* meter,
                                        const XFlowMeterField* keys, int count)
{
    XBsonDocument* doc = flowCodecBuildStateDoc(meter, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

XByteArray* XFlowMeterCodec_stateToJson(const XFlowMeter* meter,
                                        const XFlowMeterField* keys, int count)
{
    XBsonDocument* doc = flowCodecBuildStateDoc(meter, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toJson(doc, XJsonDocument_Compact);
    XClassDelete((XClass*)doc);
    return bytes;
}

const XFlowMeterField* XFlowMeterCodec_allKeys(int* outCount)
{
    if (!outCount) return NULL;
    *outCount = (int)(sizeof(s_flowMeterAllKeys) / sizeof(s_flowMeterAllKeys[0]));
    return s_flowMeterAllKeys;
}

bool XFlowMeterCodec_stateFromBson(XFlowMeterSnapshot* out,
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
    flowCodecFillSnapshot(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

bool XFlowMeterCodec_stateFromJson(XFlowMeterSnapshot* out,
                                   const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = flowCodecJsonToDoc(data, size);
    if (!doc) return false;
    flowCodecFillSnapshot(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

#endif /* XINDUSTRY_ON && XFLOW_METER_ON */
