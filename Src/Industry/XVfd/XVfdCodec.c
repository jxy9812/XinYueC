
/**
 * @file       XVfdCodec.c
 * @brief      变频器状态/控制编解码实现：XVfdStatus / XVfdControlRequest ↔ JSON/BSON。
 * @details    [键映射] 状态字段按 **validMask 位序号（0~39）** 映射到线上
 *             字符串键名（s_fieldTable 下标即位号）；不直接 switch
 *             XVfdStatusField 枚举——MSVC C 模式高位枚举常量截断会让
 *             case 值重复，位序号表跨平台稳定。
 *             [实现捷径] JSON 与 BSON 共用一张表：编码先建 BSON 文档，
 *             JSON 版经 XBsonDocument_toJson 输出；解码 JSON 版先经
 *             XJsonDocument → XBsonDocument_fromJsonObject 归一到 BSON
 *             再走同一条字段读取路径（键名两格式一致，语义零分歧）。
 */
#include "XVfdCodec.h"

#if XVFD_ON

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
/*                      键映射表（下标 = validMask 位号）                      */
/* ========================================================================== */

/** @brief 字段值类别（编码写什么 BSON 类型 / 解码怎么读）。 */
typedef enum XVfdCodecKind {
    XVfdCodecKind_Bool = 0,         /**< 布尔（BSON bool）。 */
    XVfdCodecKind_Int32,            /**< 有符号整型（枚举/计数器；BSON int32）。 */
    XVfdCodecKind_Uint32,           /**< 无符号整型（状态字/位图/故障码；BSON int32）。 */
    XVfdCodecKind_Double            /**< 工程量浮点（BSON double）。 */
} XVfdCodecKind;

/** @brief 位号 → 线上键名 + 值类别（与 XVfdDevice.h 的 XVfdStatusField 顺序一致）。 */
typedef struct XVfdCodecFieldEntry {
    const char* m_key;              /**< 线上键名（静态常量）。 */
    XVfdCodecKind m_kind;           /**< 值类别（当前仅作自述；读写路径按位号 switch）。 */
} XVfdCodecFieldEntry;

/* 字段位序（对照 XVfdStatusField 枚举，勿改动顺序）：
   0 RunState, 1 RawStateWord, 2 Running, 3 Faulted, 4 Warning, 5 Reverse,
   6 TargetReached, 7 Remote, 8 JogActive, 9 DcBraking, 10 SetFreqHz,
   11 OutputFreqHz, 12 SetFreqPercent, 13 OutputFreqPercent, 14 OutputVoltage,
   15 OutputCurrent, 16 BusVoltage, 17 MainsVoltage, 18 OutputPowerKw,
   19 OutputPowerPercent, 20 TorquePercent, 21 SpeedRpm, 22 FeedbackSpeedRpm,
   23 DriveTempC, 24 MotorTempC, 25 DriveThermalPercent, 26 MotorThermalPercent,
   27 DiBits, 28 DoBits, 29 Ai1, 30 Ai2, 31 Ai3, 32 PidSetpoint, 33 PidFeedback,
   34 CounterValue, 35 LengthValue, 36 RunHours, 37 PowerOnHours, 38 EnergyKwh,
   39 FaultCode */
static const XVfdCodecFieldEntry s_fieldTable[] = {
    { "runState",              XVfdCodecKind_Int32 },    /* 0 */
    { "rawStateWord",          XVfdCodecKind_Uint32 },   /* 1 */
    { "running",               XVfdCodecKind_Bool },     /* 2 */
    { "faulted",               XVfdCodecKind_Bool },     /* 3 */
    { "warning",               XVfdCodecKind_Bool },     /* 4 */
    { "reverse",               XVfdCodecKind_Bool },     /* 5 */
    { "targetReached",         XVfdCodecKind_Bool },     /* 6 */
    { "remote",                XVfdCodecKind_Bool },     /* 7 */
    { "jogActive",             XVfdCodecKind_Bool },     /* 8 */
    { "dcBraking",             XVfdCodecKind_Bool },     /* 9 */
    { "setFreqHz",             XVfdCodecKind_Double },   /* 10 */
    { "outputFreqHz",          XVfdCodecKind_Double },   /* 11 */
    { "setFreqPercent",        XVfdCodecKind_Double },   /* 12 */
    { "outputFreqPercent",     XVfdCodecKind_Double },   /* 13 */
    { "outputVoltageV",        XVfdCodecKind_Double },   /* 14 */
    { "outputCurrentA",        XVfdCodecKind_Double },   /* 15 */
    { "busVoltageV",           XVfdCodecKind_Double },   /* 16 */
    { "mainsVoltageV",         XVfdCodecKind_Double },   /* 17 */
    { "outputPowerKw",         XVfdCodecKind_Double },   /* 18 */
    { "outputPowerPercent",    XVfdCodecKind_Double },   /* 19 */
    { "torquePercent",         XVfdCodecKind_Double },   /* 20 */
    { "speedRpm",              XVfdCodecKind_Double },   /* 21 */
    { "feedbackSpeedRpm",      XVfdCodecKind_Double },   /* 22 */
    { "driveTempC",            XVfdCodecKind_Double },   /* 23 */
    { "motorTempC",            XVfdCodecKind_Double },   /* 24 */
    { "driveThermalPercent",   XVfdCodecKind_Double },   /* 25 */
    { "motorThermalPercent",   XVfdCodecKind_Double },   /* 26 */
    { "diBits",                XVfdCodecKind_Uint32 },   /* 27 */
    { "doBits",                XVfdCodecKind_Uint32 },   /* 28 */
    { "ai1",                   XVfdCodecKind_Double },   /* 29 */
    { "ai2",                   XVfdCodecKind_Double },   /* 30 */
    { "ai3",                   XVfdCodecKind_Double },   /* 31 */
    { "pidSetpoint",           XVfdCodecKind_Double },   /* 32 */
    { "pidFeedback",           XVfdCodecKind_Double },   /* 33 */
    { "counterValue",          XVfdCodecKind_Int32 },    /* 34 */
    { "lengthValue",           XVfdCodecKind_Double },   /* 35 */
    { "runHoursTotal",         XVfdCodecKind_Double },   /* 36 */
    { "powerOnHoursTotal",     XVfdCodecKind_Double },   /* 37 */
    { "energyKwhTotal",        XVfdCodecKind_Double },   /* 38 */
    { "faultCode",             XVfdCodecKind_Uint32 }    /* 39 */
};

#define XVFD_CODEC_BIT(n)   (1ULL << (n))
#define XVFD_CODEC_FIELD_COUNT  (int)(sizeof(s_fieldTable) / sizeof(s_fieldTable[0]))

/** @brief 「页面遥测」缺省键列表（渲染所需子集；显式移位防枚举截断）。 */
static const uint64_t s_telemetryKeys[] = {
    XVFD_CODEC_BIT(0),   /* RunState */
    XVFD_CODEC_BIT(2),   /* Running */
    XVFD_CODEC_BIT(3),   /* Faulted */
    XVFD_CODEC_BIT(5),   /* Reverse */
    XVFD_CODEC_BIT(8),   /* JogActive */
    XVFD_CODEC_BIT(7),   /* Remote */
    XVFD_CODEC_BIT(10),  /* SetFreqHz */
    XVFD_CODEC_BIT(11),  /* OutputFreqHz */
    XVFD_CODEC_BIT(14),  /* OutputVoltage */
    XVFD_CODEC_BIT(15),  /* OutputCurrent */
    XVFD_CODEC_BIT(16),  /* BusVoltage */
    XVFD_CODEC_BIT(18),  /* OutputPowerKw */
    XVFD_CODEC_BIT(20),  /* TorquePercent */
    XVFD_CODEC_BIT(21),  /* SpeedRpm */
    XVFD_CODEC_BIT(39)   /* FaultCode */
};

/* ========================================================================== */
/*                      编码辅助                                              */
/* ========================================================================== */

/**
 * @brief 把位号字段写入 BSON 文档（值取自快照）。
 * @param doc 目标文档；bit 字段位号（0~39）；st 快照。
 * @return 写入成功返回 true；位号越界返回 false。
 * @note switch 逐位号取值（表驱动无法对「字段 → 成员」寻址，C 无反射）。
 */
static bool xvfdCodecWriteField(XBsonDocument* doc, int bit, const XVfdStatus* st)
{
    const char* key = s_fieldTable[bit].m_key;
    switch (bit) {
    case 0:  return XBsonDocument_insert_keyUtf8_int32(doc, key, (int32_t)st->m_runState);
    case 1:  return XBsonDocument_insert_keyUtf8_int32(doc, key, (int32_t)st->m_rawStateWord);
    case 2:  return XBsonDocument_insert_keyUtf8_bool(doc, key, st->m_running);
    case 3:  return XBsonDocument_insert_keyUtf8_bool(doc, key, st->m_faulted);
    case 4:  return XBsonDocument_insert_keyUtf8_bool(doc, key, st->m_warning);
    case 5:  return XBsonDocument_insert_keyUtf8_bool(doc, key, st->m_reverse);
    case 6:  return XBsonDocument_insert_keyUtf8_bool(doc, key, st->m_targetReached);
    case 7:  return XBsonDocument_insert_keyUtf8_bool(doc, key, st->m_remote);
    case 8:  return XBsonDocument_insert_keyUtf8_bool(doc, key, st->m_jogActive);
    case 9:  return XBsonDocument_insert_keyUtf8_bool(doc, key, st->m_dcBraking);
    case 10: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_setFreqHz);
    case 11: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_outputFreqHz);
    case 12: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_setFreqPercent);
    case 13: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_outputFreqPercent);
    case 14: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_outputVoltageV);
    case 15: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_outputCurrentA);
    case 16: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_busVoltageV);
    case 17: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_mainsVoltageV);
    case 18: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_outputPowerKw);
    case 19: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_outputPowerPercent);
    case 20: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_torquePercent);
    case 21: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_speedRpm);
    case 22: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_feedbackSpeedRpm);
    case 23: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_driveTempC);
    case 24: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_motorTempC);
    case 25: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_driveThermalPercent);
    case 26: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_motorThermalPercent);
    case 27: return XBsonDocument_insert_keyUtf8_int32(doc, key, (int32_t)st->m_diBits);
    case 28: return XBsonDocument_insert_keyUtf8_int32(doc, key, (int32_t)st->m_doBits);
    case 29: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_ai1);
    case 30: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_ai2);
    case 31: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_ai3);
    case 32: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_pidSetpoint);
    case 33: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_pidFeedback);
    case 34: return XBsonDocument_insert_keyUtf8_int32(doc, key, st->m_counterValue);
    case 35: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_lengthValue);
    case 36: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_runHoursTotal);
    case 37: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_powerOnHoursTotal);
    case 38: return XBsonDocument_insert_keyUtf8_double(doc, key, st->m_energyKwhTotal);
    case 39: return XBsonDocument_insert_keyUtf8_int32(doc, key, (int32_t)st->m_faultCode);
    default: return false;
    }
}

/**
 * @brief 快照 → 「键列表裁剪」的 BSON 文档（JSON 版复用）。
 * @return 新建文档（恒含 "vm"；keys 为 NULL 时仅含 "vm"）；
 *         分配失败返回 NULL。@warning 调用方 XClassDelete。
 */
static XBsonDocument* xvfdCodecBuildStatusDoc(const XVfdStatus* status,
                                              const uint64_t* keys, int count)
{
    XBsonDocument* doc = XBsonDocument_create();
    uint64_t mask = status ? status->m_validMask : 0;
    uint64_t emitted = 0;       /* 实际编码出的位（vm 按它写，见下）。 */
    int i;
    int bitOf[XVFD_CODEC_FIELD_COUNT];
    int n = 0;
    if (!doc) return NULL;
    /* 预解析键列表 → 位号；vm 只写「键列表 ∩ 源掩码」——载荷里没有的
     * 字段位绝不上报，否则解码方会把零值当有效（"--" 判定失真）。 */
    for (i = 0; keys && i < count; ++i) {
        int t;
        int bit = -1;
        for (t = 0; t < XVFD_CODEC_FIELD_COUNT; ++t) {
            if (keys[i] == XVFD_CODEC_BIT(t)) { bit = t; break; }
        }
        if (bit < 0) continue;                  /* 未知键静默跳过（前向兼容）。 */
        if ((mask & keys[i]) && status) {
            bitOf[n++] = bit;
            emitted |= XVFD_CODEC_BIT(bit);
        }
    }
    if (!XBsonDocument_insert_keyUtf8_int64(doc, "vm", (int64_t)emitted)) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    if (emitted != 0 &&
        !XBsonDocument_insert_keyUtf8_int64(doc, "ts",
                                            (int64_t)status->m_timestampMs)) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    for (i = 0; i < n; ++i) {
        if (!xvfdCodecWriteField(doc, bitOf[i], status)) {
            XClassDelete((XClass*)doc);
            return NULL;
        }
    }
    return doc;
}

/* ========================================================================== */
/*                      状态编码（设备 → 载荷）                                */
/* ========================================================================== */

XByteArray* XVfdCodec_statusToBson(const XVfdStatus* status,
                                   const uint64_t* keys, int count)
{
    XBsonDocument* doc = xvfdCodecBuildStatusDoc(status, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

XByteArray* XVfdCodec_statusToJson(const XVfdStatus* status,
                                   const uint64_t* keys, int count)
{
    XBsonDocument* doc = xvfdCodecBuildStatusDoc(status, keys, count);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toJson(doc, XJsonDocument_Compact);
    XClassDelete((XClass*)doc);
    return bytes;
}

/* ========================================================================== */
/*                      状态解码（载荷 → 快照）                                */
/* ========================================================================== */

/**
 * @brief 单字段读取路径（BSON 值 → 快照成员；bit 为位号）。
 * @return 识别并写入返回 true；未知位号返回 false（调用方跳过）。
 */
static bool xvfdCodecReadField(XVfdStatus* st, int bit, XBsonValue* v)
{
    switch (bit) {
    case 0:  st->m_runState = (XVfdRunState)XBsonValue_toInt32(v, 0); return true;
    case 1:  st->m_rawStateWord = (uint16_t)XBsonValue_toInt32(v, 0); return true;
    case 2:  st->m_running = XBsonValue_toBool(v, false); return true;
    case 3:  st->m_faulted = XBsonValue_toBool(v, false); return true;
    case 4:  st->m_warning = XBsonValue_toBool(v, false); return true;
    case 5:  st->m_reverse = XBsonValue_toBool(v, false); return true;
    case 6:  st->m_targetReached = XBsonValue_toBool(v, false); return true;
    case 7:  st->m_remote = XBsonValue_toBool(v, false); return true;
    case 8:  st->m_jogActive = XBsonValue_toBool(v, false); return true;
    case 9:  st->m_dcBraking = XBsonValue_toBool(v, false); return true;
    case 10: st->m_setFreqHz = XBsonValue_toDouble(v, 0.0); return true;
    case 11: st->m_outputFreqHz = XBsonValue_toDouble(v, 0.0); return true;
    case 12: st->m_setFreqPercent = XBsonValue_toDouble(v, 0.0); return true;
    case 13: st->m_outputFreqPercent = XBsonValue_toDouble(v, 0.0); return true;
    case 14: st->m_outputVoltageV = XBsonValue_toDouble(v, 0.0); return true;
    case 15: st->m_outputCurrentA = XBsonValue_toDouble(v, 0.0); return true;
    case 16: st->m_busVoltageV = XBsonValue_toDouble(v, 0.0); return true;
    case 17: st->m_mainsVoltageV = XBsonValue_toDouble(v, 0.0); return true;
    case 18: st->m_outputPowerKw = XBsonValue_toDouble(v, 0.0); return true;
    case 19: st->m_outputPowerPercent = XBsonValue_toDouble(v, 0.0); return true;
    case 20: st->m_torquePercent = XBsonValue_toDouble(v, 0.0); return true;
    case 21: st->m_speedRpm = XBsonValue_toDouble(v, 0.0); return true;
    case 22: st->m_feedbackSpeedRpm = XBsonValue_toDouble(v, 0.0); return true;
    case 23: st->m_driveTempC = XBsonValue_toDouble(v, 0.0); return true;
    case 24: st->m_motorTempC = XBsonValue_toDouble(v, 0.0); return true;
    case 25: st->m_driveThermalPercent = XBsonValue_toDouble(v, 0.0); return true;
    case 26: st->m_motorThermalPercent = XBsonValue_toDouble(v, 0.0); return true;
    case 27: st->m_diBits = (uint16_t)XBsonValue_toInt32(v, 0); return true;
    case 28: st->m_doBits = (uint16_t)XBsonValue_toInt32(v, 0); return true;
    case 29: st->m_ai1 = XBsonValue_toDouble(v, 0.0); return true;
    case 30: st->m_ai2 = XBsonValue_toDouble(v, 0.0); return true;
    case 31: st->m_ai3 = XBsonValue_toDouble(v, 0.0); return true;
    case 32: st->m_pidSetpoint = XBsonValue_toDouble(v, 0.0); return true;
    case 33: st->m_pidFeedback = XBsonValue_toDouble(v, 0.0); return true;
    case 34: st->m_counterValue = XBsonValue_toInt32(v, 0); return true;
    case 35: st->m_lengthValue = XBsonValue_toDouble(v, 0.0); return true;
    case 36: st->m_runHoursTotal = XBsonValue_toDouble(v, 0.0); return true;
    case 37: st->m_powerOnHoursTotal = XBsonValue_toDouble(v, 0.0); return true;
    case 38: st->m_energyKwhTotal = XBsonValue_toDouble(v, 0.0); return true;
    case 39: st->m_faultCode = (uint16_t)XBsonValue_toInt32(v, 0); return true;
    default: return false;
    }
}

/**
 * @brief 从 BSON 文档填充快照（BSON/JSON 解码的公共后半段）。
 * @note 有效位 = "vm" ∪ 载荷实际出现的字段位（发送方漏置位也有值）。
 */
static void xvfdCodecFillStatus(XVfdStatus* out, const XBsonDocument* doc)
{
    XBsonValue* vm = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "vm");
    uint64_t mask = vm ? (uint64_t)XBsonValue_toInt64(vm, 0) : 0;
    int bit;
    for (bit = 0; bit < XVFD_CODEC_FIELD_COUNT; ++bit) {
        XBsonValue* v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc,
                                                    s_fieldTable[bit].m_key);
        if (v && xvfdCodecReadField(out, bit, v)) {
            mask |= XVFD_CODEC_BIT(bit);        /* 载荷里有即有效。 */
        }
    }
    {
        XBsonValue* ts = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "ts");
        if (ts) out->m_timestampMs = (uint64_t)XBsonValue_toInt64(ts, 0);
    }
    out->m_validMask = mask;
    out->m_valid = (mask != 0);
}

bool XVfdCodec_statusFromBson_2(XVfdStatus* out, int* outSide,
                                const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (outSide) *outSide = 0;          /* 缺省侧 A（协议缺键时的口径）。 */
    if (!data || size == 0) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (!XBsonDocument_from_bytes(doc, data, size)) {
        XClassDelete((XClass*)doc);
        return false;
    }
    if (outSide) {
        XBsonValue* sv = XBsonDocument_value_keyUtf8(doc, "side");
        if (sv) *outSide = (int)XBsonValue_toInt32(sv, 0);
    }
    xvfdCodecFillStatus(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

bool XVfdCodec_statusFromBson(XVfdStatus* out, const uint8_t* data, size_t size)
{
    return XVfdCodec_statusFromBson_2(out, NULL, data, size);
}

/**
 * @brief JSON 字节 → BSON 文档（解码公共前半段）。
 * @return 新建 BSON 文档；失败返回 NULL。@warning 调用方 XClassDelete。
 * @note  bytes/json/obj 逐层建、逐层收，失败路径不漏释放。
 */
static XBsonDocument* xvfdCodecJsonToDoc(const uint8_t* data, size_t size)
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

bool XVfdCodec_statusFromJson(XVfdStatus* out, const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = xvfdCodecJsonToDoc(data, size);
    if (!doc) return false;
    xvfdCodecFillStatus(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

/* ========================================================================== */
/*                      控制编解码与执行                                       */
/* ========================================================================== */

/** @brief 控制键名（线上约定）。 */
#define XVFD_CODEC_KEY_OP   "op"
#define XVFD_CODEC_KEY_REV  "rev"
#define XVFD_CODEC_KEY_HZ   "hz"
#define XVFD_CODEC_KEY_RPM  "rpm"
#define XVFD_CODEC_KEY_VAL  "val"
#define XVFD_CODEC_KEY_IDX  "idx"

/**
 * @brief 控制请求 → BSON 文档（JSON 版复用）。
 * @return 新建文档；分配失败返回 NULL。@warning 调用方 XClassDelete。
 */
static XBsonDocument* xvfdCodecBuildControlDoc(const XVfdControlRequest* req)
{
    XBsonDocument* doc = XBsonDocument_create();
    bool ok = true;
    if (!doc || !req) {
        if (doc) XClassDelete((XClass*)doc);
        return NULL;
    }
    ok = XBsonDocument_insert_keyUtf8_int32(doc, XVFD_CODEC_KEY_OP,
                                            (int32_t)req->m_op);
    if (ok) {
        switch (req->m_op) {
        case XVfdOperation_Start:
        case XVfdOperation_Jog:
            ok = XBsonDocument_insert_keyUtf8_bool(doc, XVFD_CODEC_KEY_REV,
                                                   req->m_reverse);
            /* [捎带给定 —— 对齐 Python 客户端 {"op":4,"rev":…,"hz":…}]
             * 启动/点动帧**必须带界面给定量**：客户端只发一条 Start，
             * 服务端在处理时「先写给定、再发命令字」。旧实现只写 rev、
             * 把 m_valueHz 丢了 ⇒ 启动/点动按变频器里**残留的旧给定**跑
             * （现场症状：界面设 3Hz、实际跑 25Hz = 界面在骗人）。
             * [转速模式] rpm 一并带上，服务端优先用 rpm 换算（与 Python
             * 侧 _resolve_given_hz 同口径）。 */
            if (ok && req->m_valueHz != 0.0)
                ok = XBsonDocument_insert_keyUtf8_double(doc, XVFD_CODEC_KEY_HZ,
                                                         req->m_valueHz);
            if (ok && req->m_valueRpm != 0.0)
                ok = XBsonDocument_insert_keyUtf8_double(doc, XVFD_CODEC_KEY_RPM,
                                                         req->m_valueRpm);
            break;
        case XVfdOperation_SetFrequency:
            ok = XBsonDocument_insert_keyUtf8_double(doc, XVFD_CODEC_KEY_HZ,
                                                     req->m_valueHz);
            break;
        case XVfdOperation_SetSpeed:
            ok = XBsonDocument_insert_keyUtf8_double(doc, XVFD_CODEC_KEY_RPM,
                                                     req->m_valueRpm);
            break;
        case XVfdOperation_SetPidTarget:
            ok = XBsonDocument_insert_keyUtf8_double(doc, XVFD_CODEC_KEY_VAL,
                                                     req->m_value);
            break;
        case XVfdOperation_SelectPresetSpeed:
            ok = XBsonDocument_insert_keyUtf8_int32(doc, XVFD_CODEC_KEY_IDX,
                                                    req->m_index);
            break;
        default:
            break;      /* Stop/FreeStop/QuickStop/ResetFault 等无附加参数。 */
        }
    }
    if (!ok) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    return doc;
}

XByteArray* XVfdCodec_controlToBson(const XVfdControlRequest* req)
{
    XBsonDocument* doc = xvfdCodecBuildControlDoc(req);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

XByteArray* XVfdCodec_controlToJson(const XVfdControlRequest* req)
{
    XBsonDocument* doc = xvfdCodecBuildControlDoc(req);
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toJson(doc, XJsonDocument_Compact);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief 从控制文档读出请求（BSON/JSON 解码的公共后半段）。
 * @return "op" 为已知动作返回 true；缺失或越界返回 false。
 */
static bool xvfdCodecFillControl(XVfdControlRequest* out, const XBsonDocument* doc)
{
    XBsonValue* op = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, XVFD_CODEC_KEY_OP);
    XBsonValue* v;
    int32_t opVal;
    if (!op) return false;
    opVal = XBsonValue_toInt32(op, 0);
    switch ((XVfdOperation)opVal) {
    case XVfdOperation_Start:
    case XVfdOperation_Stop:
    case XVfdOperation_FreeStop:
    case XVfdOperation_QuickStop:
    case XVfdOperation_Jog:
    case XVfdOperation_ResetFault:
    case XVfdOperation_SetFrequency:
    case XVfdOperation_SetSpeed:
    case XVfdOperation_SetPidTarget:
    case XVfdOperation_SelectPresetSpeed:
    case 101:   /* 点动释放（HSF_OP_JOG_RELEASE；自定义语义，见 XhsfLinkProto.h） */
        out->m_op = (XVfdOperation)opVal;
        break;
    default:
        return false;
    }
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, XVFD_CODEC_KEY_REV);
    if (v) out->m_reverse = XBsonValue_toBool(v, false);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, XVFD_CODEC_KEY_HZ);
    if (v) out->m_valueHz = XBsonValue_toDouble(v, 0.0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, XVFD_CODEC_KEY_RPM);
    if (v) out->m_valueRpm = XBsonValue_toDouble(v, 0.0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, XVFD_CODEC_KEY_VAL);
    if (v) out->m_value = XBsonValue_toDouble(v, 0.0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, XVFD_CODEC_KEY_IDX);
    if (v) out->m_index = XBsonValue_toInt32(v, 0);
    return true;
}

bool XVfdCodec_controlFromBson(XVfdControlRequest* out, const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    bool ok = false;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (XBsonDocument_from_bytes(doc, data, size)) {
        ok = xvfdCodecFillControl(out, doc);
    }
    XClassDelete((XClass*)doc);
    return ok;
}

bool XVfdCodec_controlFromJson(XVfdControlRequest* out, const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    bool ok = false;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = xvfdCodecJsonToDoc(data, size);
    if (!doc) return false;
    ok = xvfdCodecFillControl(out, doc);
    XClassDelete((XClass*)doc);
    return ok;
}

bool XVfdCodec_applyControl(XVfdDevice* dev, const XVfdControlRequest* req)
{
    if (!dev || !req) return false;
    switch (req->m_op) {
    case XVfdOperation_Start:       return XVfdDevice_start(dev, req->m_reverse);
    case XVfdOperation_Stop:        return XVfdDevice_stop(dev);
    case XVfdOperation_FreeStop:    return XVfdDevice_freeStop(dev);
    case XVfdOperation_QuickStop:   return XVfdDevice_quickStop(dev);
    case XVfdOperation_Jog:         return XVfdDevice_jog(dev, req->m_reverse);
    case XVfdOperation_ResetFault:  return XVfdDevice_resetFault(dev);
    case XVfdOperation_SetFrequency: return XVfdDevice_setFrequency(dev, req->m_valueHz);
    case XVfdOperation_SetSpeed:    return XVfdDevice_setSpeedRpm(dev, req->m_valueRpm);
    case XVfdOperation_SetPidTarget: return XVfdDevice_setPidTarget(dev, req->m_value);
    case XVfdOperation_SelectPresetSpeed: return XVfdDevice_selectPresetSpeed(dev, req->m_index);
    default:
        return false;   /* SetAccelDecel 需双值，载荷不承载；走参数写。 */
    }
}

/* ========================================================================== */
/*                      键列表助手                                            */
/* ========================================================================== */

const uint64_t* XVfdCodec_telemetryKeys(int* outCount)
{
    if (!outCount) return NULL;
    *outCount = (int)(sizeof(s_telemetryKeys) / sizeof(s_telemetryKeys[0]));
    return s_telemetryKeys;
}

#endif /* XVFD_ON */
