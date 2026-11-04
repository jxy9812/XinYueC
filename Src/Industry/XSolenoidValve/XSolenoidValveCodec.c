
/**
 * @file       XSolenoidValveCodec.c
 * @brief      电磁阀状态/控制编解码实现：阀位快照与开/关命令 ↔ JSON/BSON。
 * @details    [实现捷径] 与 XVfdCodec 同款：JSON 与 BSON 共用一张键映射表，
 *             编码先建 BSON 文档再经 XBsonDocument_toJson 出 JSON；解码
 *             JSON 先归一到 BSON 再走同一读取路径。控制载荷识别按
 *             「先 BSON 后 JSON」尝试（两格式键名一致，语义零分歧）。
 */
#include "XSolenoidValveCodec.h"

#if XINDUSTRY_ON && XSOLENOID_VALVE_ON

#include "XBson.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XJson.h"
#include "XJsonDocument.h"
#include "XJsonObject.h"
#include "XMemory.h"
#include "XString.h"
#include <string.h>

/* ========================================================================== */
/*                      键映射表                                              */
/* ========================================================================== */

/** @brief XSolenoidValveField → 线上键名 + 取值函数（表驱动）。 */
typedef struct XSolenoidValveCodecEntry {
    XSolenoidValveField m_field;    /**< 快照字段键。 */
    const char* m_key;              /**< 线上键名（静态常量）。 */
} XSolenoidValveCodecEntry;

static const XSolenoidValveCodecEntry s_valveFieldTable[] = {
    { XSolenoidValveField_State,       "state" },
    { XSolenoidValveField_Open,        "open" },
    { XSolenoidValveField_Closed,      "closed" },
    { XSolenoidValveField_FeedbackMode, "mode" },
    { XSolenoidValveField_TimeoutMs,   "timeoutMs" }
};

#define XSOLENOID_CODEC_FIELD_COUNT \
    (int)(sizeof(s_valveFieldTable) / sizeof(s_valveFieldTable[0]))

/** @brief 全键现成列表。 */
static const XSolenoidValveField s_valveAllKeys[] = {
    XSolenoidValveField_State,
    XSolenoidValveField_Open,
    XSolenoidValveField_Closed,
    XSolenoidValveField_FeedbackMode,
    XSolenoidValveField_TimeoutMs
};

/* ========================================================================== */
/*                      内部工具                                              */
/* ========================================================================== */

/**
 * @brief 用阀当前状态构建快照（valve 为 NULL 时全零）。
 */
static void xsolenoidCodecMakeSnapshot(XSolenoidValveSnapshot* out,
                                       const XSolenoidValve* valve)
{
    memset(out, 0, sizeof(*out));
    if (!valve) return;
    out->m_state = (uint8_t)XSolenoidValve_state(valve);
    out->m_open = XSolenoidValve_isFullyOpen(valve);
    out->m_closed = XSolenoidValve_isFullyClosed(valve);
    out->m_feedbackMode = (uint8_t)XSolenoidValve_feedbackMode(valve);
    out->m_timeoutMs = XSolenoidValve_timeoutMs(valve);
}

/**
 * @brief 把一个字段写入 BSON 文档（值取自快照）。
 * @return 写入成功返回 true；字段未知返回 false。
 */
static bool xsolenoidCodecWriteField(XBsonDocument* doc,
                                     const XSolenoidValveCodecEntry* entry,
                                     const XSolenoidValveSnapshot* snap)
{
    switch (entry->m_field) {
    case XSolenoidValveField_State:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key,
                                                  (int32_t)snap->m_state);
    case XSolenoidValveField_Open:
        return XBsonDocument_insert_keyUtf8_bool(doc, entry->m_key, snap->m_open);
    case XSolenoidValveField_Closed:
        return XBsonDocument_insert_keyUtf8_bool(doc, entry->m_key, snap->m_closed);
    case XSolenoidValveField_FeedbackMode:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key,
                                                  (int32_t)snap->m_feedbackMode);
    case XSolenoidValveField_TimeoutMs:
        return XBsonDocument_insert_keyUtf8_int32(doc, entry->m_key,
                                                  (int32_t)snap->m_timeoutMs);
    default:
        return false;
    }
}

/**
 * @brief 快照 → BSON 文档（按键列表裁剪；编码公共前半段）。
 * @return 新建文档；分配失败返回 NULL。@warning 调用方 XClassDelete。
 */
static XBsonDocument* xsolenoidCodecBuildStateDoc(const XSolenoidValve* valve,
                                                  const XSolenoidValveField* keys,
                                                  int count)
{
    XSolenoidValveSnapshot snap;
    XBsonDocument* doc = XBsonDocument_create();
    int i;
    if (!doc) return NULL;
    xsolenoidCodecMakeSnapshot(&snap, valve);
    for (i = 0; keys && i < count; ++i) {
        int t;
        for (t = 0; t < XSOLENOID_CODEC_FIELD_COUNT; ++t) {
            if (s_valveFieldTable[t].m_field == keys[i]) {
                if (!xsolenoidCodecWriteField(doc, &s_valveFieldTable[t], &snap)) {
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
static void xsolenoidCodecFillSnapshot(XSolenoidValveSnapshot* out,
                                       const XBsonDocument* doc)
{
    XBsonValue* v;
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "state");
    if (v) out->m_state = (uint8_t)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "open");
    if (v) out->m_open = XBsonValue_toBool(v, false);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "closed");
    if (v) out->m_closed = XBsonValue_toBool(v, false);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "mode");
    if (v) out->m_feedbackMode = (uint8_t)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "timeoutMs");
    if (v) out->m_timeoutMs = (uint32_t)XBsonValue_toInt64(v, 0);
}

/**
 * @brief 控制文档 → 方向（读 "op"："open"/"close" 或 1/2）。
 * @return 识别成功填充 outOpen 返回 true；否则 false。
 */
static bool xsolenoidCodecReadOp(const XBsonDocument* doc, bool* outOpen)
{
    XBsonValue* op = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "op");
    if (!op) return false;
    if (XBsonValue_type(op) == XBSON_TYPE_STRING) {
        const XString* s = XBsonValue_toString(op);
        if (s && XString_equals_utf8(s, "open", XChar_CaseSensitive)) {
            *outOpen = true;
            return true;
        }
        if (s && XString_equals_utf8(s, "close", XChar_CaseSensitive)) {
            *outOpen = false;
            return true;
        }
        return false;
    }
    {
        int32_t v = XBsonValue_toInt32(op, 0);
        if (v == 1) { *outOpen = true; return true; }
        if (v == 2) { *outOpen = false; return true; }
    }
    return false;
}

/* ========================================================================== */
/*                      状态编码（阀 → 载荷）                                  */
/* ========================================================================== */

XByteArray* XSolenoidValveCodec_stateToBson(const XSolenoidValve* valve,
                                            const XSolenoidValveField* keys, int count)
{
    XBsonDocument* doc = xsolenoidCodecBuildStateDoc(valve, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

XByteArray* XSolenoidValveCodec_stateToJson(const XSolenoidValve* valve,
                                            const XSolenoidValveField* keys, int count)
{
    XBsonDocument* doc = xsolenoidCodecBuildStateDoc(valve, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toJson(doc, XJsonDocument_Compact);
    XClassDelete((XClass*)doc);
    return bytes;
}

const XSolenoidValveField* XSolenoidValveCodec_allKeys(int* outCount)
{
    if (!outCount) return NULL;
    *outCount = (int)(sizeof(s_valveAllKeys) / sizeof(s_valveAllKeys[0]));
    return s_valveAllKeys;
}

/* ========================================================================== */
/*                      状态解码（载荷 → 快照）                                */
/* ========================================================================== */

bool XSolenoidValveCodec_stateFromBson(XSolenoidValveSnapshot* out,
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
    xsolenoidCodecFillSnapshot(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

bool XSolenoidValveCodec_stateFromJson(XSolenoidValveSnapshot* out,
                                       const uint8_t* data, size_t size)
{
    XByteArray* bytes;
    XJsonDocument* json;
    XJsonObject* obj;
    XBsonDocument* doc;
    bool ok = false;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    bytes = XByteArray_create();
    if (!bytes) return false;
    if (!XByteArray_push_back_2(bytes, data, size)) {
        XClassDelete((XClass*)bytes);
        return false;
    }
    json = XJsonDocument_fromJson(bytes);
    XClassDelete((XClass*)bytes);
    if (!json) return false;
    obj = XJsonDocument_object(json);
    if (obj) {
        doc = XBsonDocument_fromJsonObject(obj);
        if (doc) {
            xsolenoidCodecFillSnapshot(out, doc);
            XClassDelete((XClass*)doc);
            ok = true;
        }
    }
    XClassDelete((XClass*)json);
    return ok;
}

/* ========================================================================== */
/*                      控制编解码与执行                                       */
/* ========================================================================== */

/**
 * @brief 控制文档构建（BSON/JSON 共用）。
 * @return 新建文档；分配失败返回 NULL。@warning 调用方 XClassDelete。
 */
static XBsonDocument* xsolenoidCodecBuildControlDoc(bool open)
{
    XBsonDocument* doc = XBsonDocument_create();
    if (!doc) return NULL;
    if (!XBsonDocument_insert_keyUtf8_utf8(doc, "op", open ? "open" : "close")) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    return doc;
}

XByteArray* XSolenoidValveCodec_controlToBson(bool open)
{
    XBsonDocument* doc = xsolenoidCodecBuildControlDoc(open);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

XByteArray* XSolenoidValveCodec_controlToJson(bool open)
{
    XBsonDocument* doc = xsolenoidCodecBuildControlDoc(open);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toJson(doc, XJsonDocument_Compact);
    XClassDelete((XClass*)doc);
    return bytes;
}

bool XSolenoidValveCodec_applyControl(XSolenoidValve* valve,
                                      const uint8_t* data, size_t size,
                                      bool* outOpen)
{
    XBsonDocument* doc;
    bool open = false;
    bool ok = false;
    if (!valve || !data || size == 0) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    /* 先按 BSON 识别；失败再按 JSON 归一识别（键名两格式一致）。 */
    if (XBsonDocument_from_bytes(doc, data, size) &&
        xsolenoidCodecReadOp(doc, &open)) {
        ok = true;
    } else {
        XByteArray* bytes = XByteArray_create();
        if (bytes) {
            if (XByteArray_push_back_2(bytes, data, size)) {
                XJsonDocument* json = XJsonDocument_fromJson(bytes);
                if (json) {
                    XJsonObject* obj = XJsonDocument_object(json);
                    if (obj) {
                        XBsonDocument* conv = XBsonDocument_fromJsonObject(obj);
                        if (conv) {
                            if (xsolenoidCodecReadOp(conv, &open)) ok = true;
                            XClassDelete((XClass*)conv);
                        }
                    }
                    XClassDelete((XClass*)json);
                }
            }
            XClassDelete((XClass*)bytes);
        }
    }
    XClassDelete((XClass*)doc);
    if (!ok) return false;
    if (outOpen) *outOpen = open;
    return open ? XSolenoidValve_open(valve) : XSolenoidValve_close(valve);
}

#endif /* XINDUSTRY_ON && XSOLENOID_VALVE_ON */
