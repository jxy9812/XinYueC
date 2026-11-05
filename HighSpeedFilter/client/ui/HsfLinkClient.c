
/**
 * @file       HsfLinkClient.c
 * @brief      客户端 XLink 连接封装实现。
 * @details    [状态机] open → Connecting →（XLink 握手+挑战应答）→
 *             Connected+认证 → 订阅两个遥测主题 → ready 回调；断线自动
 *             重连（2s），重新就绪再触发 ready（重订阅照走）。认证失败
 *             走 fail 回调（XLink 客户端此时已停自动重连，须重新 open）。
 *             [接收] 分发回调三 arg 口径（XLinkSessionDispatchCallback），
 *             上下文走模块单例（回调无 userData 位）。
 */
#include "HsfLinkClient.h"

#include "HsfSettingsStore.h"
#include "XhsfLinkProto.h"

#include "XLinkClient.h"
#include "XLinkSession.h"
#include "XLinkProto.h"
#if XVFD_ON
#include "XVfdCodec.h"
#include "XVfdM512.h"
#endif
#include "XBson.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XObject.h"
#include "XVarList.h"
#include "XMemory.h"
#include "XString.h"
#include "XPrintf.h"
#include <string.h>

/* ==================== 模块状态 ==================== */

/** @brief 单类通知的订阅槽容量（登录窗/远端 provider/未来页各占一槽）。 */
#define HSF_LC_SUB_SLOTS  4

/** @brief 通知订阅（回调+令牌对；空槽 m_cb=NULL）。 */
typedef struct HsfLcSub {
    HsfLinkNotifyFn m_cb;               /**< 通知回调；NULL=空槽。 */
    void* m_user;                       /**< 注册令牌。 */
} HsfLcSub;

/** @brief 连接封装单例（open/close 管生命周期；回调注册即存函数指针）。 */
typedef struct HsfLinkClientState {
    XLinkClient* m_client;              /**< XLink 客户端（拥有）。 */
    bool m_everReady;                   /**< 曾就绪（区分首次连接失败与断线）。 */
    /* 订阅表（多订阅者：登录窗/远端 provider/未来 AI 页各占一槽）。 */
    HsfLcSub m_ready[HSF_LC_SUB_SLOTS];
    HsfLcSub m_fail[HSF_LC_SUB_SLOTS];
    HsfLcSub m_vfd[HSF_LC_SUB_SLOTS];
    HsfLcSub m_ai[HSF_LC_SUB_SLOTS];
    HsfLcSub m_tempHum[HSF_LC_SUB_SLOTS];   /**< 温湿度遥测订阅槽。 */
    HsfLcSub m_flowMeter[HSF_LC_SUB_SLOTS]; /**< 电磁流量计遥测订阅槽。 */
    HsfLinkCommandFn m_cmdCb;    void* m_cmdUser;   /**< 控制应答（单槽）。 */
    /* 变频器遥测快照。 */
#if XVFD_ON
    XVfdStatus m_vfdStatus;             /**< 最近遥测（解码产物）。 */
#endif
    bool m_vfdValid;                    /**< 收到过至少一拍。 */
    /* 模拟量遥测快照。 */
    int32_t m_aiUa[HSF_LINK_AI_CH_MAX]; /**< 工程值（μA 定点）。 */
    uint32_t m_aiMask;                  /**< 有效位掩码。 */
    int m_aiCount;                      /**< 通道数。 */
    bool m_aiValid;                     /**< 收到过至少一拍。 */
    char m_aiName[HSF_LINK_AI_CH_MAX][32]; /**< 通道名（服务端全量下发）。 */
    /* 温湿度遥测快照（按侧）。 */
    HsfLinkTempHum m_tempHumState[HSF_LINK_SIDE_COUNT]; /**< 每侧最近快照。 */
    /* 电磁流量计遥测快照（按侧）。 */
    HsfLinkFlowMeter m_flowMeterState[HSF_LINK_SIDE_COUNT]; /**< 每侧最近快照。 */
    /* 数字量遥测快照。 */
    HsfLinkDigital m_digital;               /**< 最近数字量位图。 */
    HsfLcSub m_digitalCb[HSF_LC_SUB_SLOTS]; /**< 数字量更新订阅槽。 */
    /* 电磁阀遥测快照。 */
    HsfLinkValves m_valves;                 /**< 最近 6 路阀快照。 */
    HsfLcSub m_valveCb[HSF_LC_SUB_SLOTS];   /**< 电磁阀更新订阅槽。 */
    /* 最近控制应答。 */
    bool m_lastCmdOk;
    char m_lastCmdErr[96];              /**< UTF-8 失败原因。 */
    int m_lastCmdSeq;                   /**< 应答序号（每拍自增；0=从未收到）。 */
    /* 服务端运行模式（RUN_MODE 应答）。 */
    bool m_modeKnown;                   /**< 是否收到过模式应答。 */
    int  m_modeAuto;                    /**< 1=自动（拒绝远程控制）0=手动。 */
    char m_modeErr[96];                 /**< 最近一次模式设置的失败原因。 */
    HsfLcSub m_modeCb[HSF_LC_SUB_SLOTS];/**< 模式变化订阅。 */
    /* 设备健康（事件驱动推送 + 查询应答）。 */
    HsfLinkDeviceHealth m_dev[HSF_LINK_DEV_MAX]; /**< 最近快照（定长数组）。 */
    int  m_devCount;                     /**< 条目数。 */
    int  m_devSeq;                       /**< 变化序号（服务端单调递增）。 */
    HsfLcSub m_healthCb[HSF_LC_SUB_SLOTS]; /**< 健康更新订阅。 */
    /* 按需订阅引用计数（页面显示期持有；归零才真正退订）。 */
    int m_topicRef[HsfLinkTopic_Count];  /**< 各主题订阅引用数。 */
    bool m_topicSent[HsfLinkTopic_Count];/**< 是否已向对端发出订阅帧（与
                                          *   引用计数分离：引用可能在建窗期
                                          *   累计而当时未连线，就绪后须补发）。 */
} HsfLinkClientState;

static HsfLinkClientState s_lc;         /**< 模块单例（零初始化）。 */

/* ---- 前向声明：分派函数在本文件前部，解析/广播实现在后部。 ---- */
static bool hsfLcParseDigital(const uint8_t* data, uint32_t len);
static void hsfLcNotifyDigital(void);
static bool hsfLcParseValves(const uint8_t* data, uint32_t len);
static void hsfLcNotifyValves(void);

/* ==================== 内部工具 ==================== */

/**
 * @brief 从 Response 载荷读应答包络 {"ok":bool,"err":utf8}。
 */
static void hsfLcParseEnvelope(const uint8_t* data, uint32_t len,
                               bool* outOk, char* outErr, size_t errCap)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonValue* v;
    outErr[0] = '\0';
    *outOk = false;
    if (!doc) return;
    if (XBsonDocument_from_bytes(doc, data, len)) {
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_OK);
        if (v) *outOk = XBsonValue_toBool(v, false);
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_ERR);
        if (v) {
            const XString* s = XBsonValue_toString(v);
            if (s) {
                const char* u8 = XString_toUtf8(s);
                if (u8) {
                    size_t n = strlen(u8);
                    if (n >= errCap) n = errCap - 1;
                    memcpy(outErr, u8, n);
                    outErr[n] = '\0';
                }
            }
        }
    }
    XClassDelete((XClass*)doc);
}

/**
 * @brief 解码模拟量遥测 {"n","v","u0..uN"} + 全量时的 "an0..anN"。
 * @note  [名字只由服务端给] 全量帧才带 an<i> 名字；增量帧只有数值，
 *        客户端沿用已缓存的名字。名字变更时服务端主动发全量帧。
 */
static void hsfLcParseAi(const uint8_t* data, uint32_t len)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonValue* v;
    int n = 0;
    int i;
    bool full = false;
    if (!doc) return;
    if (XBsonDocument_from_bytes(doc, data, len)) {
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_FULL);
        if (v) full = XBsonValue_toBool(v, false);
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_AI_COUNT);
        if (v) n = XBsonValue_toInt32(v, 0);
        if (n > HSF_LINK_AI_CH_MAX) n = HSF_LINK_AI_CH_MAX;
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_AI_VALIDMASK);
        s_lc.m_aiMask = v ? (uint32_t)XBsonValue_toInt32(v, 0) : 0;
        for (i = 0; i < n; ++i) {
            char key[16];
            snprintf(key, sizeof(key), HSF_KEY_AI_UA_FMT, i);
            v = XBsonDocument_value_keyUtf8(doc, key);
            s_lc.m_aiUa[i] = v ? XBsonValue_toInt32(v, 0) : 0;
            if (full) {
                snprintf(key, sizeof(key), HSF_KEY_AI_NAME_FMT, i);
                v = XBsonDocument_value_keyUtf8(doc, key);
                if (v) {
                    const XString* str = XBsonValue_toString(v);
                    const char* u8 = str ? XString_toUtf8(str) : NULL;
                    if (u8)
                        snprintf(s_lc.m_aiName[i], sizeof(s_lc.m_aiName[i]),
                                 "%s", u8);
                }
            }
        }
        s_lc.m_aiCount = n;
        s_lc.m_aiValid = true;
    }
    XClassDelete((XClass*)doc);
}

/**
 * @brief 解码温湿度遥测 {"side","temp","hum","tempValid","humValid"}。
 * @param data BSON 文档字节；len 字节数；outSide 输出侧号。
 * @return 解析成功返回 true（快照已就近更新）。
 * @note  值单位 0.1（温度可负）；键名与服务端 XTempHumTransmitterCodec 一致。
 *        无效通道不写值键，故缺键即「无数据」，保持上次值并把 valid 置 false。
 */
static bool hsfLcParseTempHum(const uint8_t* data, uint32_t len, int* outSide)
{
    XBsonDocument* doc;
    XBsonValue* v;
    HsfLinkTempHum* snap;
    int side = HSF_SIDE_A;

    if (!data || len == 0 || !outSide) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (!XBsonDocument_from_bytes(doc, data, len)) {
        XClassDelete((XClass*)doc);
        return false;
    }
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_SIDE);
    if (v) side = XBsonValue_toInt32(v, HSF_SIDE_A);
    if (side < 0 || side >= HSF_LINK_SIDE_COUNT) {
        XClassDelete((XClass*)doc);
        return false;
    }
    snap = &s_lc.m_tempHumState[side];
    v = XBsonDocument_value_keyUtf8(doc, "temp");
    if (v) snap->m_tempC10 = XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8(doc, "hum");
    if (v) snap->m_humPct10 = XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8(doc, "tempValid");
    snap->m_tempValid = v ? XBsonValue_toBool(v, false) : false;
    v = XBsonDocument_value_keyUtf8(doc, "humValid");
    snap->m_humValid = v ? XBsonValue_toBool(v, false) : false;
    snap->m_received = true;
    snap->m_lastMs = XDateTime_currentMSecsSinceEpoch();
    XClassDelete((XClass*)doc);
    *outSide = side;
    return true;
}

/**
 * @brief 宽容读 double：BSON DOUBLE/INT32/INT64 三种类型都接受。
 * @details [与 XFlowMeterCodec 同款修法] XBsonValue_toDouble 仅在类型为
 *          DOUBLE 时返回值，其它类型直接给默认值。JSON 文本里不带小数点
 *          的数（如整数累积量）解析后是整型，读回来会静默变 0。服务端推的
 *          是 BSON double，正常不会命中；但快照接口/别的客户端可能用整数，
 *          这里按类型分流兜底。
 * @param v 值对象；NULL 返回 0。
 * @return 数值；不可解释时返回 0。
 */
static double hsfLcValueToDouble(const XBsonValue* v)
{
    if (!v) return 0.0;
    if (XBsonValue_isDouble(v)) return XBsonValue_toDouble(v, 0.0);
    if (XBsonValue_isInt32(v) || XBsonValue_isInt64(v))
        return (double)XBsonValue_toInt64(v, 0);
    return 0.0;
}

/**
 * @brief 解码电磁流量计遥测 {"side","flow","speed","forwardTotal",
 *        "reverseTotal","flowUnit","totalUnit","alarms","valid"}。
 * @param data BSON 文档字节；len 字节数；outSide 输出侧号。
 * @return 解析成功返回 true（快照已就近更新）。
 * @note  键名与服务端 hsfAppFillFlowMeter 一致。valid=false 时不写物理量键，
 *        保持上次值并把 valid 置 false（界面按质量显隐）。
 */
static bool hsfLcParseFlowMeter(const uint8_t* data, uint32_t len, int* outSide)
{
    XBsonDocument* doc;
    XBsonValue* v;
    HsfLinkFlowMeter* snap;
    int side = HSF_SIDE_A;

    if (!data || len == 0 || !outSide) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (!XBsonDocument_from_bytes(doc, data, len)) {
        XClassDelete((XClass*)doc);
        return false;
    }
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_SIDE);
    if (v) side = XBsonValue_toInt32(v, HSF_SIDE_A);
    if (side < 0 || side >= HSF_LINK_SIDE_COUNT) {
        XClassDelete((XClass*)doc);
        return false;
    }
    snap = &s_lc.m_flowMeterState[side];
    v = XBsonDocument_value_keyUtf8(doc, "valid");
    snap->m_valid = v ? XBsonValue_toBool(v, false) : false;
    if (snap->m_valid) {
        v = XBsonDocument_value_keyUtf8(doc, "flow");
        if (v) snap->m_flow = hsfLcValueToDouble(v);
        v = XBsonDocument_value_keyUtf8(doc, "speed");
        if (v) snap->m_speed = hsfLcValueToDouble(v);
        v = XBsonDocument_value_keyUtf8(doc, "forwardTotal");
        if (v) snap->m_forwardTotal = hsfLcValueToDouble(v);
        v = XBsonDocument_value_keyUtf8(doc, "reverseTotal");
        if (v) snap->m_reverseTotal = hsfLcValueToDouble(v);
        v = XBsonDocument_value_keyUtf8(doc, "flowUnit");
        if (v) snap->m_flowUnit = XBsonValue_toInt32(v, 0xFF);
        v = XBsonDocument_value_keyUtf8(doc, "totalUnit");
        if (v) snap->m_totalUnit = XBsonValue_toInt32(v, 0xFF);
        v = XBsonDocument_value_keyUtf8(doc, "alarms");
        if (v) snap->m_alarms = XBsonValue_toInt32(v, 0);
    }
    snap->m_received = true;
    snap->m_lastMs = XDateTime_currentMSecsSinceEpoch();
    XClassDelete((XClass*)doc);
    *outSide = side;
    return true;
}

/**
 * @brief 订阅遥测主题（就绪后调；重连后重订阅走同一路径）。
 * @note  服务端只负责发布，客户端按需订阅——加主题只需在服务端发布侧
 *        增加一处、客户端这里加一句订阅，两端解耦（2026-11-04 口径）。
 */
/**
 * @brief 定长字符串安全拷贝（恒 0 结尾）。
 */
static void hsfLcCopy(char* dst, size_t cap, const char* src)
{
    size_t n;
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    n = strlen(src);
    if (n >= cap) n = cap - 1;
    XMemcpy(dst, src, n);
    dst[n] = '\0';
}

/**
 * @brief 从 BSON 值取 UTF-8 文本写入定长缓冲。
 */
static void hsfLcCopyValue(XBsonValue* v, char* dst, size_t cap)
{
    const XString* xs;
    if (!v || !dst) return;
    xs = XBsonValue_toString(v);
    if (!xs) return;
    hsfLcCopy(dst, cap, XString_toUtf8(xs));
}

/**
 * @brief 解析设备健康载荷（推送与查询应答同构）并通知订阅者。
 * @param data BSON 字节；len 长度。
 */
static void hsfLcOnHealthPayload(const uint8_t* data, uint32_t len)
{
    XBsonDocument* doc;
    XBsonValue* v;
    int n = 0;
    int i;
    int si;

    doc = XBsonDocument_create();
    if (!doc) return;
    if (!XBsonDocument_from_bytes(doc, data, len)) {
        XClassDelete((XClass*)doc);
        return;
    }
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_DEV_COUNT);
    if (v) n = XBsonValue_toInt32(v, 0);
    if (n < 0) n = 0;
    if (n > HSF_LINK_DEV_MAX) n = HSF_LINK_DEV_MAX;
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_DEV_SEQ);
    if (v) s_lc.m_devSeq = XBsonValue_toInt32(v, s_lc.m_devSeq);

    for (i = 0; i < n; ++i) {
        HsfLinkDeviceHealth* d = &s_lc.m_dev[i];
        char key[24];
        XMemset(d, 0, sizeof(*d));
        snprintf(key, sizeof(key), "d%d_name", i);
        hsfLcCopyValue(XBsonDocument_value_keyUtf8(doc, key),
                       d->m_name, sizeof(d->m_name));
        snprintf(key, sizeof(key), "d%d_kind", i);
        v = XBsonDocument_value_keyUtf8(doc, key);
        if (v) d->m_kind = XBsonValue_toInt32(v, 0);
        snprintf(key, sizeof(key), "d%d_side", i);
        v = XBsonDocument_value_keyUtf8(doc, key);
        if (v) d->m_side = XBsonValue_toInt32(v, 0);
        snprintf(key, sizeof(key), "d%d_health", i);
        v = XBsonDocument_value_keyUtf8(doc, key);
        if (v) d->m_health = XBsonValue_toInt32(v, 0);
        snprintf(key, sizeof(key), "d%d_error", i);
        v = XBsonDocument_value_keyUtf8(doc, key);
        if (v) d->m_error = XBsonValue_toInt32(v, 0);
        snprintf(key, sizeof(key), "d%d_runState", i);
        v = XBsonDocument_value_keyUtf8(doc, key);
        if (v) d->m_runState = XBsonValue_toInt32(v, 0);
        snprintf(key, sizeof(key), "d%d_online", i);
        v = XBsonDocument_value_keyUtf8(doc, key);
        if (v) d->m_online = XBsonValue_toBool(v, false);
        snprintf(key, sizeof(key), "d%d_text", i);
        hsfLcCopyValue(XBsonDocument_value_keyUtf8(doc, key),
                       d->m_text, sizeof(d->m_text));
    }
    s_lc.m_devCount = n;
    XClassDelete((XClass*)doc);

    for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
        if (s_lc.m_healthCb[si].m_cb)
            s_lc.m_healthCb[si].m_cb(s_lc.m_healthCb[si].m_user);
    }
}

/**
 * @brief 主题枚举 → 线上主题号。
 * @param topic 主题枚举；越界返回 0。
 */
static uint32_t hsfLcTopicId(HsfLinkTopic topic)
{
    switch (topic) {
    case HsfLinkTopic_Vfd:       return HSF_TOPIC_VFD_STATUS;
    case HsfLinkTopic_Ai:        return HSF_TOPIC_AI_STATUS;
    case HsfLinkTopic_TempHum:   return HSF_TOPIC_TEMPHUM_STATUS;
    case HsfLinkTopic_FlowMeter: return HSF_TOPIC_FLOWMETER_STATUS;
    case HsfLinkTopic_Digital:   return HSF_TOPIC_DIGITAL_STATUS;
    case HsfLinkTopic_Valve:     return HSF_TOPIC_VALVE_STATUS;
    default:                     return 0;
    }
}

/**
 * @brief 常驻主题订阅（健康/温湿度：顶栏与标题栏常显，不随页面显隐）。
 * @note  变频器/模拟量/流量计改由页面 Show/Hide 按需订阅退订（见
 *        HsfLinkClient_subscribe）；健康主题事件驱动、温湿度喂顶栏，
 *        故恒订阅。
 */
static void hsfLcSubscribeTopics(void)
{
    XLinkSession* session = s_lc.m_client ? XLinkClient_session(s_lc.m_client) : NULL;
    int i;
    if (!session) return;
    if (!XLinkSession_subscribe(session, HSF_TOPIC_TEMPHUM_STATUS) ||
        !XLinkSession_subscribe(session, HSF_TOPIC_DEVICE_HEALTH)) {
        XPrintf("HsfLinkClient: 常驻主题订阅失败\n");
    }
    /* 连接就绪时按已累计的页面引用补订（建窗期累计的引用 / 重连场景
     * 页面仍显示着）。sent 标志随链路作废，此处统一重发。 */
    for (i = 0; i < (int)HsfLinkTopic_Count; ++i) {
        s_lc.m_topicSent[i] = false;
        if (s_lc.m_topicRef[i] > 0
            && XLinkSession_subscribe(session, hsfLcTopicId((HsfLinkTopic)i)))
            s_lc.m_topicSent[i] = true;
    }
}

bool HsfLinkClient_subscribe(HsfLinkTopic topic)
{
    XLinkSession* session;
    uint32_t id;
    if ((int)topic < 0 || topic >= HsfLinkTopic_Count) return false;
    id = hsfLcTopicId(topic);
    if (!id) return false;
    session = s_lc.m_client ? XLinkClient_session(s_lc.m_client) : NULL;
    s_lc.m_topicRef[topic]++;
    /* 未连线：只累计引用；就绪时 hsfLcSubscribeTopics 按引用补订。
     * 返回 true=可受理——页面不该因"此刻正连线上"而把它当失败。 */
    if (!session || !XLinkClient_isReady(s_lc.m_client))
        return true;
    /* 已连线但**尚未发出过**订阅帧（引用可能在建窗期就累计了）：
     * 补发。判据用 m_topicSent 而非引用数——否则「建窗期累计引用、
     * 登录后切页」这条路会因 ref>0 直接 return，帧永远不发出去
     * （实测症状：切页后服务端收不到任何订阅）。 */
    if (s_lc.m_topicSent[topic])
        return true;
    if (!XLinkSession_subscribe(session, id)) {
        /* 发送失败：回滚，下次切页/重连可重试。 */
        s_lc.m_topicRef[topic]--;
        XPrintf("HsfLinkClient: 主题 0x%08X 订阅失败\n", id);
        return false;
    }
    s_lc.m_topicSent[topic] = true;
    return true;
}

void HsfLinkClient_unsubscribe(HsfLinkTopic topic)
{
    XLinkSession* session;
    uint32_t id;
    if ((int)topic < 0 || topic >= HsfLinkTopic_Count) return;
    if (s_lc.m_topicRef[topic] <= 0) return;
    if (--s_lc.m_topicRef[topic] > 0) return;
    /* 引用归零：本主题不再需要推送。已发出过订阅帧才需退订。 */
    if (!s_lc.m_topicSent[topic]) return;
    s_lc.m_topicSent[topic] = false;
    id = hsfLcTopicId(topic);
    session = s_lc.m_client ? XLinkClient_session(s_lc.m_client) : NULL;
    if (!session || !id) return;
    /* 未就绪即不发退订帧：本端会话已随链路作废（重连后是全新会话），
     * 对端也早已回收该订阅，无需再通知。 */
    if (!XLinkClient_isReady(s_lc.m_client)) return;
    (void)XLinkSession_unsubscribe(session, id);
}

/* ==================== XLink 回调 ==================== */

/**
 * @brief 业务帧分发（会话 pump 栈上同步执行；上下文=模块单例）。
 */
static void hsfLcDispatch(XLinkSession* session, const XLinkFrameHeader* frame,
                          const uint8_t* payload)
{
    if (!session || !frame) return;
    switch (frame->m_msgClass) {
    case XLinkMsgClass_Response:
        if (frame->m_msgCode == HSF_CMD_DEVICE_HEALTH) {
            hsfLcOnHealthPayload(payload, frame->m_payloadLen);
            break;
        }
        if (frame->m_msgCode == HSF_CMD_RUN_MODE) {
            XBsonDocument* doc = XBsonDocument_create();
            bool ok = false;
            int si;
            s_lc.m_modeErr[0] = '\0';
            if (doc && XBsonDocument_from_bytes(doc, payload, frame->m_payloadLen)) {
                XBsonValue* v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_OK);
                if (v) ok = XBsonValue_toBool(v, false);
                if (ok) {
                    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_MODE);
                    if (v) {
                        s_lc.m_modeAuto = (XBsonValue_toInt32(v, 0) == 1) ? 1 : 0;
                        s_lc.m_modeKnown = true;
                    }
                } else {
                    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_ERR);
                    if (v) {
                        const XString* es = XBsonValue_toString(v);
                        const char* eu = es ? XString_toUtf8(es) : NULL;
                        if (eu) {
                            size_t n = strlen(eu);
                            if (n >= sizeof(s_lc.m_modeErr)) n = sizeof(s_lc.m_modeErr) - 1;
                            XMemcpy(s_lc.m_modeErr, eu, n);
                            s_lc.m_modeErr[n] = '\0';
                        }
                    }
                }
            }
            if (doc) XClassDelete((XClass*)doc);
            for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                if (s_lc.m_modeCb[si].m_cb)
                    s_lc.m_modeCb[si].m_cb(s_lc.m_modeCb[si].m_user);
            }
            break;
        }
        if (frame->m_msgCode == HSF_CMD_VFD_CONTROL) {
            bool ok = false;
            s_lc.m_lastCmdOk = false;
            hsfLcParseEnvelope(payload, frame->m_payloadLen, &ok,
                               s_lc.m_lastCmdErr, sizeof(s_lc.m_lastCmdErr));
            s_lc.m_lastCmdOk = ok;
            ++s_lc.m_lastCmdSeq;        /* 增量观测序号（成功/失败都推进）。 */
            if (s_lc.m_cmdCb)
                s_lc.m_cmdCb(s_lc.m_cmdUser, HSF_CMD_VFD_CONTROL, ok,
                             ok ? "" : s_lc.m_lastCmdErr);
        }
        else if (frame->m_msgCode == HSF_CMD_DIGITAL_SNAPSHOT) {
            /* 快照应答：整帧就是位图文档（不是包络），直接走解析路径。 */
            if (hsfLcParseDigital(payload, frame->m_payloadLen)) hsfLcNotifyDigital();
        }
        else if (frame->m_msgCode == HSF_CMD_DIGITAL_CONTROL) {
            /* 应答 = {ok di do}：成功时顺带把位图对齐（不必等下一拍推送），
             * 失败时只取 err。包络与位图两条读取都在同一份载荷上做。 */
            bool ok = false;
            s_lc.m_lastCmdOk = false;
            hsfLcParseEnvelope(payload, frame->m_payloadLen, &ok,
                               s_lc.m_lastCmdErr, sizeof(s_lc.m_lastCmdErr));
            s_lc.m_lastCmdOk = ok;
            ++s_lc.m_lastCmdSeq;
            if (ok) {
                if (hsfLcParseDigital(payload, frame->m_payloadLen))
                    hsfLcNotifyDigital();
            }
            if (s_lc.m_cmdCb)
                s_lc.m_cmdCb(s_lc.m_cmdUser, HSF_CMD_DIGITAL_CONTROL, ok,
                             ok ? "" : s_lc.m_lastCmdErr);
        }
        else if (frame->m_msgCode == HSF_CMD_VALVE_SNAPSHOT) {
            if (hsfLcParseValves(payload, frame->m_payloadLen)) hsfLcNotifyValves();
        }
        else if (frame->m_msgCode == HSF_CMD_VALVE_CONTROL) {
            bool ok = false;
            s_lc.m_lastCmdOk = false;
            hsfLcParseEnvelope(payload, frame->m_payloadLen, &ok,
                               s_lc.m_lastCmdErr, sizeof(s_lc.m_lastCmdErr));
            s_lc.m_lastCmdOk = ok;
            ++s_lc.m_lastCmdSeq;
            if (s_lc.m_cmdCb)
                s_lc.m_cmdCb(s_lc.m_cmdUser, HSF_CMD_VALVE_CONTROL, ok,
                             ok ? "" : s_lc.m_lastCmdErr);
        }
        break;
    case XLinkMsgClass_Publish: {
        /* 线上载荷 = [u32 topicId(小端)][BSON 文档]。 */
        uint32_t topicId;
        const uint8_t* body;
        uint32_t bodyLen;
        if (frame->m_payloadLen < 4 || !payload) break;
        topicId = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                  ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
        body = payload + 4;
        bodyLen = frame->m_payloadLen - 4;
        if (topicId == HSF_TOPIC_VFD_STATUS) {
#if XVFD_ON
            int si;
            if (XVfdCodec_statusFromBson(&s_lc.m_vfdStatus, body, bodyLen)) {
                s_lc.m_vfdValid = true;
                for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                    if (s_lc.m_vfd[si].m_cb)
                        s_lc.m_vfd[si].m_cb(s_lc.m_vfd[si].m_user);
                }
            }
#endif
        } else if (topicId == HSF_TOPIC_DEVICE_HEALTH) {
            hsfLcOnHealthPayload(body, bodyLen);
        } else if (topicId == HSF_TOPIC_AI_STATUS) {
            int si;
            hsfLcParseAi(body, bodyLen);
            for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                if (s_lc.m_ai[si].m_cb)
                    s_lc.m_ai[si].m_cb(s_lc.m_ai[si].m_user);
            }
        } else if (topicId == HSF_TOPIC_TEMPHUM_STATUS) {
            int side;
            int si;
            if (!hsfLcParseTempHum(body, bodyLen, &side)) break;
            if (side < 0 || side >= HSF_LINK_SIDE_COUNT) break;
            for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                if (s_lc.m_tempHum[si].m_cb)
                    s_lc.m_tempHum[si].m_cb(s_lc.m_tempHum[si].m_user);
            }
        } else if (topicId == HSF_TOPIC_FLOWMETER_STATUS) {
            int side;
            int si;
            if (!hsfLcParseFlowMeter(body, bodyLen, &side)) break;
            if (side < 0 || side >= HSF_LINK_SIDE_COUNT) break;
            for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                if (s_lc.m_flowMeter[si].m_cb)
                    s_lc.m_flowMeter[si].m_cb(s_lc.m_flowMeter[si].m_user);
            }
        } else if (topicId == HSF_TOPIC_DIGITAL_STATUS) {
            if (hsfLcParseDigital(body, bodyLen)) hsfLcNotifyDigital();
        } else if (topicId == HSF_TOPIC_VALVE_STATUS) {
            if (hsfLcParseValves(body, bodyLen)) hsfLcNotifyValves();
        }
        break;
    }
    default:
        break;
    }
}

/**
 * @brief 连接状态翻转：就绪 → 订阅+ready；掉到未连接 → fail（仅首次）。
 */
static void hsfLcOnStateChanged(XObject* sender, XVarList* args)
{
    int state = -1;
    int i;
    if (!args) return;
    XVarList_start(args);
    state = XVarList_arg(args, int);
    (void)sender;
    if ((XLinkClientState)state == XLinkClient_ConnectedState &&
        s_lc.m_client && XLinkClient_isReady(s_lc.m_client)) {
        s_lc.m_everReady = true;
        hsfLcSubscribeTopics();
        /* [首次拉全量] 客户端不写死点数/通道名：一连上就向服务端要一次
         * 数字量与模拟量的全量快照（服务端在 full 帧里带全部名字）。
         * 之后只收状态/数值增量；名字改了由服务端主动下发。 */
        HsfLinkClient_digitalSnapshotRequest();
        {
            XLinkSession* sess = XLinkClient_session(s_lc.m_client);
            uint32_t rid = 0;
            if (sess)
                XLinkSession_sendRequest_ex(sess, HSF_CMD_AI_SNAPSHOT,
                                            XLinkDataType_Bson, NULL, 0, &rid);
        }
        XPrintf("HsfLinkClient: 登录成功（用户 %s）\n",
                XLinkClient_user_const(s_lc.m_client));
        for (i = 0; i < HSF_LC_SUB_SLOTS; ++i) {
            if (s_lc.m_ready[i].m_cb)
                s_lc.m_ready[i].m_cb(s_lc.m_ready[i].m_user);
        }
    } else if ((XLinkClientState)state == XLinkClient_UnconnectedState &&
               !s_lc.m_everReady) {
        /* 首连未成（拒绝/不可达/超时）：通知登录窗。 */
        XPrintf("HsfLinkClient: 连接失败（state=%d）\n", state);
        for (i = 0; i < HSF_LC_SUB_SLOTS; ++i) {
            if (s_lc.m_fail[i].m_cb)
                s_lc.m_fail[i].m_cb(s_lc.m_fail[i].m_user);
        }
    } else if ((XLinkClientState)state == XLinkClient_ReconnectingState) {
        /* [2026-11-05 断线不上界面根修] 原实现只处理 UnconnectedState，
         * **漏了 ReconnectingState(3)**。自动重连开启时，断链后 XLinkClient 转的是
         * ReconnectingState 而不是 Unconnected，于是客户端 UI 永远收不到
         * 「已断开」通知——实测：杀掉服务端后界面继续显示旧数据、
         * 状态栏仍是「已连接」，直到重连成功才被覆盖（工业界面
         * 最不能容忍的隐式故障）。两种断线态都必须通知上层。 */
        XPrintf("HsfLinkClient: 连接断开（自动重连中）\n");
        for (i = 0; i < HSF_LC_SUB_SLOTS; ++i) {
            if (s_lc.m_ready[i].m_cb)
                s_lc.m_ready[i].m_cb(s_lc.m_ready[i].m_user);
        }
    } else if ((XLinkClientState)state == XLinkClient_UnconnectedState) {
        XPrintf("HsfLinkClient: 连接断开（已收手）\n");
        for (i = 0; i < HSF_LC_SUB_SLOTS; ++i) {
            if (s_lc.m_ready[i].m_cb)
                s_lc.m_ready[i].m_cb(s_lc.m_ready[i].m_user);
        }
    }
}

/**
 * @brief 认证失败（用户名/密码错等；客户端已停自动重连）。
 */
static void hsfLcOnAuthFailed(XObject* sender, XVarList* args)
{
    int reason = -1;
    int status = -1;
    int i;
    (void)sender;
    if (args) {
        XVarList_start(args);
        reason = XVarList_arg(args, int);
        status = XVarList_arg(args, int);
    }
    XPrintf("HsfLinkClient: 认证失败（reason=%d status=%d）\n", reason, status);
    for (i = 0; i < HSF_LC_SUB_SLOTS; ++i) {
        if (s_lc.m_fail[i].m_cb)
            s_lc.m_fail[i].m_cb(s_lc.m_fail[i].m_user);
    }
}


/* ==================== 公开 API ==================== */

/**
 * @brief 清空连接态快照（保留订阅表与主题引用计数）。
 * @details [为何不 memset 整表] 订阅者是**界面侧**注册的（主窗温湿度/
 *          运行模式/健康、远端 provider、流量计、登录窗），其生命周期
 *          与连接无关：主窗先建、登录后才连线的排布下，open 里整表
 *          memset 会把主窗已挂的全部订阅静默抹掉（登录成功后主窗顶栏
 *          时钟/温湿度、运行模式钮、健康灯、变频器页遥测全线失联，
 *          2026-11-04 主窗前置改造实锅）。此处只清「上一根连线留下的
 *          解码快照」，订阅/引用计数由 subscribe/unsubscribe 自己记账，
 *          close 也照样摘（close 是拆除语义，注册方本就必须重挂）。
 */
static void hsfLcResetConnectionState(void)
{
    int i;
    /* 遥测解码快照（新连接尚无任何数据）。 */
#if XVFD_ON
    memset(&s_lc.m_vfdStatus, 0, sizeof(s_lc.m_vfdStatus));
#endif
    s_lc.m_vfdValid = false;
    memset(s_lc.m_aiUa, 0, sizeof(s_lc.m_aiUa));
    s_lc.m_aiMask = 0;
    s_lc.m_aiCount = 0;
    s_lc.m_aiValid = false;
    memset(s_lc.m_tempHumState, 0, sizeof(s_lc.m_tempHumState));
    memset(s_lc.m_flowMeterState, 0, sizeof(s_lc.m_flowMeterState));
    /* 应答观测。 */
    s_lc.m_lastCmdOk = false;
    s_lc.m_lastCmdErr[0] = '\0';
    s_lc.m_lastCmdSeq = 0;
    s_lc.m_modeKnown = false;
    s_lc.m_modeAuto = 0;
    s_lc.m_modeErr[0] = '\0';
    /* 设备健康。 */
    memset(s_lc.m_dev, 0, sizeof(s_lc.m_dev));
    s_lc.m_devCount = 0;
    s_lc.m_devSeq = 0;
    /* 首连判定：新连线重新计（登录窗靠它区分「首连失败」与「断线重连」）。 */
    s_lc.m_everReady = false;
    /* 回调槽自检：close 已摘过，这里对空槽再走一遍是零成本保险
     * （防止调用方绕过 close 直接调 open 时残留旧连线期的指针）。 */
    for (i = 0; i < HSF_LC_SUB_SLOTS; ++i) {
        s_lc.m_ready[i].m_cb = NULL;    s_lc.m_ready[i].m_user = NULL;
        s_lc.m_fail[i].m_cb = NULL;     s_lc.m_fail[i].m_user = NULL;
    }
    s_lc.m_cmdCb = NULL;
    s_lc.m_cmdUser = NULL;
}

bool HsfLinkClient_open(const char* host, uint16_t port,
                        const char* user, const char* password)
{
    if (!host || !host[0] || !user || !user[0]) return false;
    HsfLinkClient_close();
    /* 只清连接态与登录窗槽位；其它订阅者（主窗/provider/页面）保留。 */
    hsfLcResetConnectionState();

    s_lc.m_client = XLinkClient_create();
    if (!s_lc.m_client) return false;
    if (port == 0) port = (uint16_t)HSF_XLINK_DEFAULT_PORT;
    XLinkClient_setHostPort(s_lc.m_client, host, port);
    XLinkClient_setDispatchCallback(s_lc.m_client, hsfLcDispatch, NULL);
    XLinkClient_setCredential(s_lc.m_client, user, password);
    XLinkClient_setAutoReconnect(s_lc.m_client, true);
    XLinkClient_setReconnectInterval(s_lc.m_client, 2000);
    XObject_connect_2((XObject*)s_lc.m_client,
                      XSignal(XLinkClient_stateChanged_signal), hsfLcOnStateChanged);
    XObject_connect_2((XObject*)s_lc.m_client,
                      XSignal(XLinkClient_authFailed_signal), hsfLcOnAuthFailed);
    if (!XLinkClient_connectToServer(s_lc.m_client)) {
        XPrintf("HsfLinkClient: 连接发起失败（地址 %s:%u）\n", host,
                (unsigned)port);
        XClassDelete((XClass*)s_lc.m_client);
        s_lc.m_client = NULL;
        return false;
    }
    XPrintf("HsfLinkClient: 连接中 %s:%u（用户 %s）\n", host, (unsigned)port,
            user);
    return true;
}

void HsfLinkClient_close(void)
{
    int i;
    if (!s_lc.m_client) return;
    /* 先摘订阅再拆链：disconnect 会触发 stateChanged（Unconnected）
     * ——订阅槽若还挂着，远端 provider/页面会在拆除中的会话上反调
     * （发快照请求等），退出期段错误的根因。
     * [页面/主窗订阅保留 2026-11-04] ready/fail 由登录窗按次注册（重开
     * 登录须重挂），但 vfd/ai/tempHum/flowMeter/mode/health 是**界面侧
     * 常驻订阅**（主窗先建、登录后连线），拆除连接时清掉它们会让界面
     * 永久失联（重新 open 不会有人再注册一遍）。故这里只摘 ready/fail
     * 与命令应答（一次性语义），其余保留——连接不存在期间它们不会被
     * 回调（无会话即无分发），重连就绪后照常收数据。 */
    for (i = 0; i < HSF_LC_SUB_SLOTS; ++i) {
        s_lc.m_ready[i].m_cb = NULL;
        s_lc.m_ready[i].m_user = NULL;
        s_lc.m_fail[i].m_cb = NULL;
        s_lc.m_fail[i].m_user = NULL;
    }
    s_lc.m_cmdCb = NULL;
    s_lc.m_cmdUser = NULL;
    XLinkClient_disconnectFromServer(s_lc.m_client);
    XClassDelete((XClass*)s_lc.m_client);
    s_lc.m_client = NULL;
    /* 订阅引用**不清零**：引用表达的是"某页当前正显示"，属页面生命周期
     * 而非连接生命周期。保留后重新登录时 hsfLcSubscribeTopics 会按引用
     * 自动补订，显示中的页面不掉数据（隐藏页无引用，不会被误订）。 */
}

bool HsfLinkClient_isOpen(void)
{
    return s_lc.m_client != NULL;
}

bool HsfLinkClient_isReady(void)
{
    return s_lc.m_client && XLinkClient_isReady(s_lc.m_client);
}

const char* HsfLinkClient_user(void)
{
    return (s_lc.m_client && XLinkClient_isAuthenticated(s_lc.m_client))
               ? XLinkClient_user_const(s_lc.m_client) : "";
}

int HsfLinkClient_role(void)
{
    XLinkSession* session;
    if (!s_lc.m_client || !XLinkClient_isAuthenticated(s_lc.m_client)) return 0;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return 0;
    return (int)XLinkSession_authRole(session);
}

/**
 * @brief 登记一个订阅（找空槽；表满覆盖末槽并告警）。
 */
static void hsfLcAddSub(HsfLcSub* subs, HsfLinkNotifyFn cb, void* userData)
{
    int i;
    for (i = 0; i < HSF_LC_SUB_SLOTS; ++i) {
        if (!subs[i].m_cb) {
            subs[i].m_cb = cb;
            subs[i].m_user = userData;
            return;
        }
    }
    XPrintf("HsfLinkClient: 订阅槽已满，覆盖末槽\n");
    subs[HSF_LC_SUB_SLOTS - 1].m_cb = cb;
    subs[HSF_LC_SUB_SLOTS - 1].m_user = userData;
}

void HsfLinkClient_setReadyCallback(HsfLinkNotifyFn cb, void* userData)
{
    if (cb) hsfLcAddSub(s_lc.m_ready, cb, userData);
}

void HsfLinkClient_setFailCallback(HsfLinkNotifyFn cb, void* userData)
{
    if (cb) hsfLcAddSub(s_lc.m_fail, cb, userData);
}

void HsfLinkClient_setVfdCallback(HsfLinkNotifyFn cb, void* userData)
{
    if (cb) hsfLcAddSub(s_lc.m_vfd, cb, userData);
}

void HsfLinkClient_setAiCallback(HsfLinkNotifyFn cb, void* userData)
{
    if (cb) hsfLcAddSub(s_lc.m_ai, cb, userData);
}

void HsfLinkClient_setTempHumCallback(HsfLinkNotifyFn cb, void* userData)
{
    if (cb) hsfLcAddSub(s_lc.m_tempHum, cb, userData);
}

void HsfLinkClient_setFlowMeterCallback(HsfLinkNotifyFn cb, void* userData)
{
    if (cb) hsfLcAddSub(s_lc.m_flowMeter, cb, userData);
}

void HsfLinkClient_setDigitalCallback(HsfLinkNotifyFn cb, void* userData)
{
    if (cb) hsfLcAddSub(s_lc.m_digitalCb, cb, userData);
}

void HsfLinkClient_setValveCallback(HsfLinkNotifyFn cb, void* userData)
{
    if (cb) hsfLcAddSub(s_lc.m_valveCb, cb, userData);
}

const HsfLinkFlowMeter* HsfLinkClient_flowMeter(int side)
{
    if (side < 0 || side >= HSF_LINK_SIDE_COUNT) return NULL;
    return &s_lc.m_flowMeterState[side];
}

/* ==================== 数字量（16DI/16DO） ==================== */

/**
 * @brief 解析数字量快照 BSON 到模块状态（推送与应答共用同一条路径）。
 * @return 解析成功返回 true。
 */
static bool hsfLcParseDigital(const uint8_t* data, uint32_t len)
{
    XBsonDocument* doc;
    XBsonValue* v;
    bool full = false;
    int n = 0;
    int i;

    if (!data || len == 0) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (!XBsonDocument_from_bytes(doc, data, len)) {
        XClassDelete((XClass*)doc);
        return false;
    }
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_FULL);
    if (v) full = XBsonValue_toBool(v, false);
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_DIG_DI);
    if (v) s_lc.m_digital.m_di = (uint32_t)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_DIG_DO);
    if (v) s_lc.m_digital.m_do = (uint32_t)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_DIG_COUNT);
    if (v) n = XBsonValue_toInt32(v, 0);
    if (n > 0) {
        if (n > HSF_DIGITAL_CHANNELS) n = HSF_DIGITAL_CHANNELS;
        s_lc.m_digital.m_count = n;
    }
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_VALVE_VALID);
    if (v) s_lc.m_digital.m_valid = XBsonValue_toBool(v, false);
    /* [控制应答也走这里] 应答可能只带 ok+位图，没有 count/valid：
     * count 沿用旧值，valid 缺省按"有数据"处理，避免按钮态被误判失效。 */
    else s_lc.m_digital.m_valid = true;
    if (s_lc.m_digital.m_count <= 0)
        s_lc.m_digital.m_count = HSF_DIGITAL_CHANNELS;

    /* 名字：只在全量帧（或名字专用帧）里出现。增量帧不带名字，
     * 客户端沿用缓存——这就是"名字只由服务端给、改了服务端主动推"
     * 的实现。 */
    if (full) {
        for (i = 0; i < s_lc.m_digital.m_count; ++i) {
            char key[20];
            snprintf(key, sizeof(key), HSF_KEY_DIG_DI_NAME_FMT, i);
            v = XBsonDocument_value_keyUtf8(doc, key);
            if (v) {
                const XString* str = XBsonValue_toString(v);
                const char* u8 = str ? XString_toUtf8(str) : NULL;
                if (u8)
                    snprintf(s_lc.m_digital.m_diName[i],
                             sizeof(s_lc.m_digital.m_diName[i]), "%s", u8);
            }
            snprintf(key, sizeof(key), HSF_KEY_DIG_DO_NAME_FMT, i);
            v = XBsonDocument_value_keyUtf8(doc, key);
            if (v) {
                const XString* str = XBsonValue_toString(v);
                const char* u8 = str ? XString_toUtf8(str) : NULL;
                if (u8)
                    snprintf(s_lc.m_digital.m_doName[i],
                             sizeof(s_lc.m_digital.m_doName[i]), "%s", u8);
            }
        }
    }
    s_lc.m_digital.m_full = full;
    s_lc.m_digital.m_received = true;
    s_lc.m_digital.m_lastMs = XDateTime_currentMSecsSinceEpoch();
    XClassDelete((XClass*)doc);
    return true;
}

const char* HsfLinkClient_aiName(int index)
{
    if (index < 0 || index >= HSF_LINK_AI_CH_MAX) return NULL;
    return s_lc.m_aiName[index][0] ? s_lc.m_aiName[index] : NULL;
}

const char* HsfLinkClient_digitalName(int index, bool isDo)
{
    if (index < 0 || index >= HSF_DIGITAL_CHANNELS) return NULL;
    if (isDo)
        return s_lc.m_digital.m_doName[index][0] ? s_lc.m_digital.m_doName[index] : NULL;
    return s_lc.m_digital.m_diName[index][0] ? s_lc.m_digital.m_diName[index] : NULL;
}

/** @brief 广播数字量更新给全部订阅槽。 */
static void hsfLcNotifyDigital(void)
{
    int i;
    for (i = 0; i < HSF_LC_SUB_SLOTS; ++i) {
        if (s_lc.m_digitalCb[i].m_cb)
            s_lc.m_digitalCb[i].m_cb(s_lc.m_digitalCb[i].m_user);
    }
}

const HsfLinkDigital* HsfLinkClient_digital(void)
{
    return &s_lc.m_digital;
}

bool HsfLinkClient_digitalFresh(void)
{
    if (!s_lc.m_digital.m_received) return false;
    return (XDateTime_currentMSecsSinceEpoch() - s_lc.m_digital.m_lastMs) <=
           (uint64_t)HSF_LINK_DIGITAL_STALE_MS;
}

bool HsfLinkClient_digitalControl(int channel, bool on)
{
    XLinkSession* session;
    XBsonDocument* doc;
    XByteArray* bytes;
    uint32_t reqId = 0;
    bool ok;

    if (!HsfLinkClient_isReady()) return false;
    if (channel < 0) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_DIG_INDEX, (int32_t)channel);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_DIG_ON, on);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    if (!bytes) return false;
    ok = XLinkSession_sendRequest_ex(session, HSF_CMD_DIGITAL_CONTROL,
                                     XLinkDataType_Bson,
                                     (const uint8_t*)XByteArray_constData(bytes),
                                     (uint32_t)XByteArray_size_base(bytes),
                                     &reqId);
    XClassDelete((XClass*)bytes);
    return ok;
}

bool HsfLinkClient_digitalSnapshotRequest(void)
{
    XLinkSession* session;
    uint32_t reqId = 0;

    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    return XLinkSession_sendRequest_ex(session, HSF_CMD_DIGITAL_SNAPSHOT,
                                       XLinkDataType_Bson, NULL, 0, &reqId);
}

/* ==================== 电磁阀（6 路） ==================== */

/** @brief 从 BSON 文档里按 v<i>_<key> 前缀取子键。 */
static void hsfLcValveReadStr(XBsonDocument* doc, int index, const char* key,
                              char* out, size_t cap)
{
    char full[40];
    XBsonValue* v;
    if (!out || cap == 0) return;
    out[0] = '\0';
    snprintf(full, sizeof(full), HSF_KEY_VALVE_PREFIX_FMT, index, key);
    v = XBsonDocument_value_keyUtf8(doc, full);
    if (!v) return;
    {
        const XString* s = XBsonValue_toString(v);
        const char* u = s ? XString_toUtf8(s) : NULL;
        if (u) snprintf(out, cap, "%s", u);
    }
}

static bool hsfLcValveReadBool(XBsonDocument* doc, int index, const char* key,
                               bool def)
{
    char full[40];
    XBsonValue* v;
    snprintf(full, sizeof(full), HSF_KEY_VALVE_PREFIX_FMT, index, key);
    v = XBsonDocument_value_keyUtf8(doc, full);
    return v ? XBsonValue_toBool(v, def) : def;
}

static int hsfLcValveReadInt(XBsonDocument* doc, int index, const char* key,
                            int def)
{
    char full[40];
    XBsonValue* v;
    snprintf(full, sizeof(full), HSF_KEY_VALVE_PREFIX_FMT, index, key);
    v = XBsonDocument_value_keyUtf8(doc, full);
    return v ? XBsonValue_toInt32(v, def) : def;
}

/**
 * @brief 解析电磁阀 6 路快照 BSON 到模块状态。
 * @return 解析成功返回 true。
 */
static bool hsfLcParseValves(const uint8_t* data, uint32_t len)
{
    XBsonDocument* doc;
    XBsonValue* v;
    int count;
    int i;

    if (!data || len == 0) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (!XBsonDocument_from_bytes(doc, data, len)) {
        XClassDelete((XClass*)doc);
        return false;
    }
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_VALVE_COUNT);
    count = v ? XBsonValue_toInt32(v, 0) : 0;
    if (count < 0) count = 0;
    if (count > HSF_VALVE_COUNT) count = HSF_VALVE_COUNT;
    s_lc.m_valves.m_count = count;
    for (i = 0; i < count; ++i) {
        HsfLinkValveItem* it = &s_lc.m_valves.m_items[i];
        hsfLcValveReadStr(doc, i, HSF_KEY_VALVE_NAME, it->m_name,
                          sizeof(it->m_name));
        it->m_state = hsfLcValveReadInt(doc, i, HSF_KEY_VALVE_STATE, 0);
        it->m_openCmd = hsfLcValveReadBool(doc, i, HSF_KEY_VALVE_OPEN_CMD, false);
        it->m_openFb = hsfLcValveReadBool(doc, i, HSF_KEY_VALVE_OPEN_FB, false);
        it->m_closeFb = hsfLcValveReadBool(doc, i, HSF_KEY_VALVE_CLOSE_FB, false);
        it->m_ready = hsfLcValveReadBool(doc, i, HSF_KEY_VALVE_READY, false);
        it->m_valid = hsfLcValveReadBool(doc, i, HSF_KEY_VALVE_VALID, false);
        hsfLcValveReadStr(doc, i, HSF_KEY_VALVE_TEXT, it->m_text,
                          sizeof(it->m_text));
    }
    s_lc.m_valves.m_received = true;
    s_lc.m_valves.m_lastMs = XDateTime_currentMSecsSinceEpoch();
    XClassDelete((XClass*)doc);
    return true;
}

/** @brief 广播电磁阀更新给全部订阅槽。 */
static void hsfLcNotifyValves(void)
{
    int i;
    for (i = 0; i < HSF_LC_SUB_SLOTS; ++i) {
        if (s_lc.m_valveCb[i].m_cb)
            s_lc.m_valveCb[i].m_cb(s_lc.m_valveCb[i].m_user);
    }
}

const HsfLinkValves* HsfLinkClient_valves(void)
{
    return &s_lc.m_valves;
}

bool HsfLinkClient_valvesFresh(void)
{
    if (!s_lc.m_valves.m_received) return false;
    return (XDateTime_currentMSecsSinceEpoch() - s_lc.m_valves.m_lastMs) <=
           (uint64_t)HSF_LINK_VALVE_STALE_MS;
}

bool HsfLinkClient_valveControl(int index, bool wantOpen)
{
    XLinkSession* session;
    XBsonDocument* doc;
    XByteArray* bytes;
    uint32_t reqId = 0;
    bool ok;

    if (!HsfLinkClient_isReady()) return false;
    if (index < 0 || index >= HSF_VALVE_COUNT) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_VALVE_INDEX, (int32_t)index);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_VALVE_ON, wantOpen);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    if (!bytes) return false;
    ok = XLinkSession_sendRequest_ex(session, HSF_CMD_VALVE_CONTROL,
                                     XLinkDataType_Bson,
                                     (const uint8_t*)XByteArray_constData(bytes),
                                     (uint32_t)XByteArray_size_base(bytes),
                                     &reqId);
    XClassDelete((XClass*)bytes);
    return ok;
}

bool HsfLinkClient_valveSnapshotRequest(void)
{
    XLinkSession* session;
    uint32_t reqId = 0;

    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    return XLinkSession_sendRequest_ex(session, HSF_CMD_VALVE_SNAPSHOT,
                                       XLinkDataType_Bson, NULL, 0, &reqId);
}

bool HsfLinkClient_flowMeterFresh(int side)
{
    const HsfLinkFlowMeter* snap;
    if (side < 0 || side >= HSF_LINK_SIDE_COUNT) return false;
    snap = &s_lc.m_flowMeterState[side];
    if (!snap->m_received) return false;
    return (XDateTime_currentMSecsSinceEpoch() - snap->m_lastMs) <=
           (uint64_t)HSF_LINK_FLOWMETER_STALE_MS;
}

bool HsfLinkClient_flowMeterInject(const uint8_t* data, uint32_t size)
{
    int side = 0;
    int i;

    if (!data || size == 0) return false;
    if (!hsfLcParseFlowMeter(data, size, &side)) return false;
    if (side < 0 || side >= HSF_LINK_SIDE_COUNT) return false;
    /* 与真实推送同款：解析后广播给全部订阅者。 */
    for (i = 0; i < HSF_LC_SUB_SLOTS; ++i) {
        if (s_lc.m_flowMeter[i].m_cb)
            s_lc.m_flowMeter[i].m_cb(s_lc.m_flowMeter[i].m_user);
    }
    return true;
}

bool HsfLinkClient_flowMeterSnapshotRequest(int side)
{
    XLinkSession* session;
    XBsonDocument* doc;
    XByteArray* bytes;
    uint32_t reqId = 0;
    bool ok;

    if (side < 0 || side >= HSF_LINK_SIDE_COUNT) return false;
    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_SIDE, (int32_t)side);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    if (!bytes) return false;
    ok = XLinkSession_sendRequest_ex(session, HSF_CMD_FLOWMETER_SNAPSHOT,
                                     XLinkDataType_Bson,
                                     (const uint8_t*)XByteArray_constData(bytes),
                                     (uint32_t)XByteArray_size_base(bytes),
                                     &reqId);
    XClassDelete((XClass*)bytes);
    return ok;
}

void HsfLinkClient_setCommandCallback(HsfLinkCommandFn cb, void* userData)
{
    s_lc.m_cmdCb = cb;
    s_lc.m_cmdUser = userData;
}

#if XVFD_ON
const XVfdStatus* HsfLinkClient_vfdStatus(void)
{
    return &s_lc.m_vfdStatus;
}
#endif

bool HsfLinkClient_vfdValid(void)
{
    return s_lc.m_vfdValid;
}

int HsfLinkClient_aiValues(int32_t* outUa, uint32_t* outMask)
{
    int i;
    if (outMask) *outMask = s_lc.m_aiValid ? s_lc.m_aiMask : 0;
    if (!s_lc.m_aiValid) return 0;
    if (outUa) {
        for (i = 0; i < s_lc.m_aiCount; ++i) outUa[i] = s_lc.m_aiUa[i];
    }
    return s_lc.m_aiCount;
}

const HsfLinkTempHum* HsfLinkClient_tempHum(int side)
{
    if (side < 0 || side >= HSF_LINK_SIDE_COUNT) return NULL;
    return &s_lc.m_tempHumState[side];
}

bool HsfLinkClient_tempHumFresh(int side)
{
    const HsfLinkTempHum* th;
    uint64_t now;
    if (side < 0 || side >= HSF_LINK_SIDE_COUNT) return false;
    th = &s_lc.m_tempHumState[side];
    if (!th->m_received || th->m_lastMs == 0) return false;
    now = XDateTime_currentMSecsSinceEpoch();
    if (now < th->m_lastMs) return true;          /* 时钟回退：宽容放行。 */
    return (now - th->m_lastMs) <= (uint64_t)HSF_LINK_TEMPHUM_STALE_MS;
}

bool HsfLinkClient_tempHumSnapshotRequest(int side)
{
    XLinkSession* session;
    XBsonDocument* doc;
    XByteArray* bytes;
    uint32_t reqId = 0;
    bool ok;

    if (side < 0 || side >= HSF_LINK_SIDE_COUNT) return false;
    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_SIDE, (int32_t)side);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    if (!bytes) return false;
    ok = XLinkSession_sendRequest_ex(session, HSF_CMD_TEMPHUM_SNAPSHOT,
                                     XLinkDataType_Bson,
                                     (const uint8_t*)XByteArray_constData(bytes),
                                     (uint32_t)XByteArray_size_base(bytes),
                                     &reqId);
    XClassDelete((XClass*)bytes);
    return ok;
}

#if XVFD_ON
bool HsfLinkClient_vfdControl(const XVfdControlRequest* req)
{
    XLinkSession* session;
    XByteArray* bytes;
    uint32_t reqId = 0;
    bool ok;
    if (!req || !HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    bytes = XVfdCodec_controlToBson(req);
    if (!bytes) return false;
    ok = XLinkSession_sendRequest_ex(session, HSF_CMD_VFD_CONTROL,
                                     XLinkDataType_Bson,
                                     (const uint8_t*)XByteArray_constData(bytes),
                                     (uint32_t)XByteArray_size_base(bytes),
                                     &reqId);
    XClassDelete((XClass*)bytes);
    if (!ok) XPrintf("HsfLinkClient: 控制命令入队失败\n");
    return ok;
}
#endif

bool HsfLinkClient_vfdSnapshotRequest(void)
{
    XLinkSession* session;
    uint32_t reqId = 0;
    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    return XLinkSession_sendRequest_ex(session, HSF_CMD_VFD_SNAPSHOT,
                                       XLinkDataType_Bson, NULL, 0, &reqId);
}

const char* HsfLinkClient_lastCommandError(void)
{
    return s_lc.m_lastCmdErr;
}

int HsfLinkClient_commandSeq(void)
{
    return s_lc.m_lastCmdSeq;
}

bool HsfLinkClient_queryRunMode(void)
{
    XLinkSession* session;
    uint32_t reqId = 0;
    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    return XLinkSession_sendRequest_ex(session, HSF_CMD_RUN_MODE,
                                       XLinkDataType_Bson, NULL, 0, &reqId);
}

bool HsfLinkClient_setRunMode(bool autoMode)
{
    XLinkSession* session;
    XBsonDocument* doc;
    XByteArray* bytes;
    uint32_t reqId = 0;
    bool ok;
    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_MODE, autoMode ? 1 : 0);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    if (!bytes) return false;
    ok = XLinkSession_sendRequest_ex(session, HSF_CMD_RUN_MODE,
                                     XLinkDataType_Bson,
                                     (const uint8_t*)XByteArray_constData(bytes),
                                     (uint32_t)XByteArray_size_base(bytes), &reqId);
    XClassDelete((XClass*)bytes);
    return ok;
}

bool HsfLinkClient_runMode(int* outAuto)
{
    if (!s_lc.m_modeKnown) {
        if (outAuto) *outAuto = 0;
        return false;
    }
    if (outAuto) *outAuto = s_lc.m_modeAuto;
    return true;
}

void HsfLinkClient_setModeCallback(HsfLinkNotifyFn cb, void* userData)
{
    if (cb) hsfLcAddSub(s_lc.m_modeCb, cb, userData);
}

const char* HsfLinkClient_lastModeError(void)
{
    return s_lc.m_modeErr;
}

void HsfLinkClient_setHealthCallback(HsfLinkNotifyFn cb, void* userData)
{
    if (cb) hsfLcAddSub(s_lc.m_healthCb, cb, userData);
}

int HsfLinkClient_deviceHealth(HsfLinkDeviceHealth* out, int capacity,
                               int* outSeq)
{
    int n = s_lc.m_devCount;
    int i;
    if (outSeq) *outSeq = s_lc.m_devSeq;
    if (!out || capacity <= 0) return n;
    if (n > capacity) n = capacity;
    for (i = 0; i < n; ++i)
        out[i] = s_lc.m_dev[i];
    return n;
}

bool HsfLinkClient_queryDeviceHealth(void)
{
    XLinkSession* session;
    uint32_t reqId = 0;
    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    return XLinkSession_sendRequest_ex(session, HSF_CMD_DEVICE_HEALTH,
                                       XLinkDataType_Bson, NULL, 0, &reqId);
}

bool HsfLinkClient_healthHasProblem(int* outFault, int* outOffline)
{
    int i;
    int fault = 0;
    int offline = 0;
    for (i = 0; i < s_lc.m_devCount; ++i) {
        if (s_lc.m_dev[i].m_health == HsfDeviceHealth_Fault) ++fault;
        if (s_lc.m_dev[i].m_health == HsfDeviceHealth_Offline) ++offline;
    }
    if (outFault) *outFault = fault;
    if (outOffline) *outOffline = offline;
    return (fault > 0 || offline > 0);
}
