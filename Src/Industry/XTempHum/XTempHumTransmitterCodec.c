
/**
 * @file       XTempHumTransmitterCodec.c
 * @brief      温湿度变送器状态编解码实现。
 * @details    与 XSolenoidValveCodec 同款捷径：JSON 与 BSON 共用一张键映射
 *             表，编码先建 BSON 文档再经 XBsonDocument_toJson 出 JSON；解码
 *             JSON 先归一到 BSON 再走同一读取路径（键名两格式一致）。
 */
#include "XTempHumTransmitterCodec.h"

#if XINDUSTRY_ON && XTEMP_HUM_ON

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

/** @brief XTempHumTransmitterField → 线上键名（表驱动）。 */
typedef struct XTempHumCodecEntry {
    XTempHumTransmitterField m_field;   /**< 快照字段键。 */
    const char* m_key;                  /**< 线上键名（静态常量）。 */
} XTempHumCodecEntry;

static const XTempHumCodecEntry s_tempHumFieldTable[] = {
    { XTempHumTransmitterField_TempValue, "temp" },
    { XTempHumTransmitterField_HumValue,  "hum" },
    { XTempHumTransmitterField_TempValid, "tempValid" },
    { XTempHumTransmitterField_HumValid,  "humValid" },
    { XTempHumTransmitterField_StaleMs,   "staleMs" }
};

#define XTEMPHUM_CODEC_FIELD_COUNT \
    (int)(sizeof(s_tempHumFieldTable) / sizeof(s_tempHumFieldTable[0]))

/** @brief 全键现成列表。 */
static const XTempHumTransmitterField s_tempHumAllKeys[] = {
    XTempHumTransmitterField_TempValue,
    XTempHumTransmitterField_HumValue,
    XTempHumTransmitterField_TempValid,
    XTempHumTransmitterField_HumValid,
    XTempHumTransmitterField_StaleMs
};

/* ==================== 内部工具 ==================== */

/**
 * @brief 用变送器当前状态构建快照（NULL 视为全零）。
 */
static void temphumCodecMakeSnapshot(XTempHumTransmitterSnapshot* out,
                                     const XTempHumTransmitter* transmitter)
{
    memset(out, 0, sizeof(*out));
    if (!transmitter) return;
    /* 值恒带出（无效时为 0），质量位由设备判定——解码方按需展示。 */
    (void)XTempHumTransmitter_value(transmitter, XTempHumChannel_Temperature,
                                    &out->m_tempEng);
    (void)XTempHumTransmitter_value(transmitter, XTempHumChannel_Humidity,
                                    &out->m_humEng);
    out->m_tempValid = XTempHumTransmitter_isValid(transmitter,
                                                   XTempHumChannel_Temperature);
    out->m_humValid = XTempHumTransmitter_isValid(transmitter,
                                                  XTempHumChannel_Humidity);
    out->m_staleMs = XTempHumTransmitter_staleMs(transmitter);
}

/**
 * @brief 把一个字段写入 BSON 文档（值取自快照）。
 * @return 写入成功返回 true；字段未知返回 false。
 */
static bool temphumCodecWriteField(XBsonDocument* doc,
                                   const XTempHumCodecEntry* entry,
                                   const XTempHumTransmitterSnapshot* snap)
{
    switch (entry->m_field) {
    case XTempHumTransmitterField_TempValue:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key, snap->m_tempEng);
    case XTempHumTransmitterField_HumValue:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key, snap->m_humEng);
    case XTempHumTransmitterField_TempValid:
        return XBsonDocument_insert_keyUtf8_bool(doc, entry->m_key, snap->m_tempValid);
    case XTempHumTransmitterField_HumValid:
        return XBsonDocument_insert_keyUtf8_bool(doc, entry->m_key, snap->m_humValid);
    case XTempHumTransmitterField_StaleMs:
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
static XBsonDocument* temphumCodecBuildStateDoc(const XTempHumTransmitter* transmitter,
                                                const XTempHumTransmitterField* keys,
                                                int count)
{
    XTempHumTransmitterSnapshot snap;
    XBsonDocument* doc = XBsonDocument_create();
    int i;
    if (!doc) return NULL;
    temphumCodecMakeSnapshot(&snap, transmitter);
    for (i = 0; keys && i < count; ++i) {
        int t;
        for (t = 0; t < XTEMPHUM_CODEC_FIELD_COUNT; ++t) {
            if (s_tempHumFieldTable[t].m_field == keys[i]) {
                if (!temphumCodecWriteField(doc, &s_tempHumFieldTable[t], &snap)) {
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
static void temphumCodecFillSnapshot(XTempHumTransmitterSnapshot* out,
                                     const XBsonDocument* doc)
{
    XBsonValue* v;
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "temp");
    if (v) out->m_tempEng = XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "hum");
    if (v) out->m_humEng = XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "tempValid");
    if (v) out->m_tempValid = XBsonValue_toBool(v, false);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "humValid");
    if (v) out->m_humValid = XBsonValue_toBool(v, false);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "staleMs");
    if (v) out->m_staleMs = (uint32_t)XBsonValue_toInt64(v, 0);
}

/**
 * @brief JSON 字节 → BSON 文档（解码公共前半段）。
 * @return 新建 BSON 文档；失败返回 NULL。@warning 调用方 XClassDelete。
 */
static XBsonDocument* temphumCodecJsonToDoc(const uint8_t* data, size_t size)
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

XByteArray* XTempHumTransmitterCodec_stateToBson(const XTempHumTransmitter* transmitter,
                                                 const XTempHumTransmitterField* keys,
                                                 int count)
{
    XBsonDocument* doc = temphumCodecBuildStateDoc(transmitter, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

XByteArray* XTempHumTransmitterCodec_stateToJson(const XTempHumTransmitter* transmitter,
                                                 const XTempHumTransmitterField* keys,
                                                 int count)
{
    XBsonDocument* doc = temphumCodecBuildStateDoc(transmitter, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toJson(doc, XJsonDocument_Compact);
    XClassDelete((XClass*)doc);
    return bytes;
}

const XTempHumTransmitterField* XTempHumTransmitterCodec_allKeys(int* outCount)
{
    if (!outCount) return NULL;
    *outCount = (int)(sizeof(s_tempHumAllKeys) / sizeof(s_tempHumAllKeys[0]));
    return s_tempHumAllKeys;
}

bool XTempHumTransmitterCodec_stateFromBson(XTempHumTransmitterSnapshot* out,
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
    temphumCodecFillSnapshot(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

bool XTempHumTransmitterCodec_stateFromJson(XTempHumTransmitterSnapshot* out,
                                            const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = temphumCodecJsonToDoc(data, size);
    if (!doc) return false;
    temphumCodecFillSnapshot(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

#endif /* XINDUSTRY_ON && XTEMP_HUM_ON */
