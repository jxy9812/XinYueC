
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
#include "HsfLinkHistory.h"
#include "HsfLinkConfig.h"

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
#include "XBsonArray.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XByteArray.h"
#include "XDateTime.h"
#include "XObject.h"
#include "XVarList.h"
#include "XMemory.h"
#include "XString.h"
#include "XPrintf.h"
#include "XSystem.h"   /* XSystem_environment：泵点击诊断开关（XHSF_PUMP_TRACE）。 */
#include <string.h>

/* ==================== 模块状态 ==================== */

/** @brief 单类通知的订阅槽容量（登录窗/远端 provider/未来页各占一槽）。 */
#define HSF_LC_SUB_SLOTS  4

/**
 * @brief 遥测补拍通道（自愈节流的下标）。
 * @details 每类数据各自记账「上次向服务端要一拍的时刻」：同一个 tick 里可能
 *          同时补拍模拟量 + 浊度 + 泵（水质页三主题同订），共用一个时间戳
 *          会让后两类被前一类挤掉。流量计按侧独立（两侧是两台独立设备）。
 */
typedef enum HsfLcSnapshotChan {
    HsfLcSnapshotCh_Ai = 0,          /**< 模拟量（全量快照）。 */
    HsfLcSnapshotCh_FlowMeterA,      /**< 电磁流量计 A 侧。 */
    HsfLcSnapshotCh_FlowMeterB,      /**< 电磁流量计 B 侧。 */
    HsfLcSnapshotCh_Turbidity,       /**< 浊度仪组。 */
    HsfLcSnapshotCh_Pump,            /**< 进水泵组。 */
    HsfLcSnapshotCh_Vfd,             /**< 变频器遥测。 */
    HsfLcSnapshotCh_Count
} HsfLcSnapshotChan;

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
    /* 状态栏提示（单槽；主窗建窗时注册一次，跨连接复用——不像 ready/fail
       那样属于登录窗的一次性语义）。2026-11-09：自动重连被服务端瞬时
       拒绝时用它告诉操作员「账户可能已在线，正在重试」。 */
    HsfLinkNoticeFn m_noticeCb;  void* m_noticeUser;
    /* 变频器遥测快照。 */
#if XVFD_ON
    XVfdStatus m_vfdStatus;             /**< 最近遥测（解码产物）。 */
#endif
    HsfHistoryPage m_history;          /**< 最近一次历史分页结果（0x0118）。 */
    HsfLinkLogPage m_logPage;          /**< 最近一次运行日志分页结果（0x0119）。 */
    HsfConfig m_config;                /**< 最近一次远端配置快照（0x0116）。 */
    int m_cfgSeq;                      /**< 配置**写**应答序号（0=从未收到）。 */
    int m_cfgGetSeq;                   /**< 配置快照序号（GET 应答递增）。 */
    bool m_cfgOk;                      /**< 最近一次配置写是否成功。 */
    char m_cfgErr[96];                 /**< 最近一次配置写失败原因（UTF-8）。 */
    char m_serverAddr[96];              /**< 服务端地址（远端设置页显示用）。 */
    bool m_vfdValid;                    /**< 收到过至少一拍。 */
    uint64_t m_vfdLastMs;               /**< 最近一次解析成功的本地时刻（毫秒）。 */
    /* 模拟量遥测快照。 */
    int32_t m_aiUa[HSF_LINK_AI_CH_MAX]; /**< 工程值（μA 定点）。 */
    int32_t m_aiRaw[HSF_LINK_AI_CH_MAX]; /**< 原始寄存器（毫伏）。 */
    int32_t m_aiEng[HSF_LINK_AI_CH_MAX]; /**< 标定工程量（×1000；无标定=0）。 */
    bool    m_aiEngValid[HSF_LINK_AI_CH_MAX]; /**< 该通道是否有工程量。 */
    char    m_aiEngUnit[HSF_LINK_AI_CH_MAX][12]; /**< 工程量单位（"MPa"…）。 */
    char    m_aiEngNote[HSF_LINK_AI_CH_MAX][48]; /**< 越限说明（空=正常）。 */
    uint32_t m_aiMask;                  /**< 有效位掩码。 */
    int m_aiCount;                      /**< 通道数。 */
    bool m_aiValid;                     /**< 收到过至少一拍。 */
    uint64_t m_aiLastMs;                /**< 最近一次解析成功的本地时刻（毫秒）。 */
    char m_aiName[HSF_LINK_AI_CH_MAX][32]; /**< 通道名（服务端全量下发）。 */
    /* 温湿度遥测快照（按侧）。 */
    HsfLinkTempHum m_tempHumState[HSF_LINK_SIDE_COUNT]; /**< 每侧最近快照。 */
    /* 温湿度快照补拍节流（自愈用）：最近一次「向服务端要一拍」的时刻。 */
    uint64_t m_lastTempHumReqMs;              /**< 最近一次快照请求时刻（毫秒）。 */
    /* 其余遥测的补拍节流（按通道，见 HsfLcSnapshotChan）。 */
    uint64_t m_lastSnapReqMs[HsfLcSnapshotCh_Count];
    /* 电磁流量计遥测快照（按侧）。 */
    HsfLinkFlowMeter m_flowMeterState[HSF_LINK_SIDE_COUNT]; /**< 每侧最近快照。 */
    int m_flowMeterSide;               /**< 服务端报告的流量计所在侧（活动侧）。 */
    /* 数字量遥测快照。 */
    HsfLinkDigital m_digital;               /**< 最近数字量位图。 */
    HsfLcSub m_digitalCb[HSF_LC_SUB_SLOTS]; /**< 数字量更新订阅槽。 */
    /* 电磁阀遥测快照。 */
    HsfLinkValves m_valves;                 /**< 最近 6 路阀快照。 */
    HsfLcSub m_valveCb[HSF_LC_SUB_SLOTS];   /**< 电磁阀更新订阅槽。 */
    HsfLcSub m_turbCb[HSF_LC_SUB_SLOTS];    /**< 浊度仪更新订阅槽。 */
    HsfLcSub m_pumpCb[HSF_LC_SUB_SLOTS];    /**< 泵更新订阅槽。 */
    HsfLinkTurbiditySet m_turbidity;       /**< 浊度仪组快照。 */
    HsfLinkPumps m_pumps;                  /**< 泵组快照。 */
    HsfLinkValveAlarmFn m_valveAlarmCb;     /**< 阀告警回调（单槽；NULL=无）。 */
    void* m_valveAlarmUser;                 /**< 阀告警回调上下文（借用）。 */
    /* 最近控制应答。 */
    bool m_lastCmdOk;
    char m_lastCmdErr[96];              /**< UTF-8 失败原因。 */
    bool m_suppressNextErr;             /**< 抑制下一条命令应答的错误提示
                                        *   （启动前预置给定那一步）。 */
    int m_lastCmdSeq;                   /**< 应答序号（每拍自增；0=从未收到）。 */
    int m_lastValveIndex;               /**< 最近阀控制的路号（cmdResult 取键用）。 */
    char m_lastValveCmdResult[16];      /**< 最近阀控制的处置结果（issued/repeated/
                                          *   reversed/ignored/external；空=无）。 */
    /* 服务端运行模式（RUN_MODE 应答）。 */
    bool m_modeKnown;                   /**< 是否收到过模式应答。 */
    int  m_modeAuto;                    /**< 1=自动（拒绝远程控制）0=手动。 */
    int  m_autoRunning;                 /**< 自动控制器是否在跑：1=运行中 0=已停止
                                        *   -1=未知（服务端未上报该键）。 */
    bool m_autoRunningKnown;            /**< 是否收到过 autoRunning 键。 */
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
static void hsfLcNotifyTempHum(void);
static bool hsfLcParseValves(const uint8_t* data, uint32_t len);
static void hsfLcParseValveCmdResult(const uint8_t* data, uint32_t len,
                                      int index);
static void hsfLcNotifyValves(void);
static bool hsfLcParseLogPage(const uint8_t* data, uint32_t len);

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
static bool hsfLcParseAi(const uint8_t* data, uint32_t len)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonValue* v;
    int n = 0;
    int i;
    bool full = false;
    if (!doc) return false;
    if (XBsonDocument_from_bytes(doc, data, len)) {
        /* [只接受显示侧 2026-11-08，与 VFD 同一处缺陷] 服务端按「主题 × 侧」
         * 各推一帧（AI 100ms × A/B = 20 帧/秒），而此前无论哪一侧都写同一份
         * m_ai* 状态并通知订阅者 ⇒ 模拟量页被刷 20 次/秒、两路读数不同时还会
         * 在 A/B 之间跳变。页面只显示 A 侧，非显示侧直接丢弃。 */
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_SIDE);
        if (v && (int)XBsonValue_toInt32(v, HSF_SIDE_A) != HSF_SIDE_A) {
            XClassDelete((XClass*)doc);
            return false;
        }
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
            /* 原始寄存器（毫伏）：底部脚注"原始寄存器：CH1=2985mV…"用。 */
            snprintf(key, sizeof(key), HSF_KEY_AI_RAW_FMT, i);
            v = XBsonDocument_value_keyUtf8(doc, key);
            s_lc.m_aiRaw[i] = v ? XBsonValue_toInt32(v, 0) : 0;
            /* ---- 工程量四键（e/eu/en/ew，与 Python 侧同键同义）---- */
            snprintf(key, sizeof(key), HSF_KEY_AI_ENG_FMT, i);
            v = XBsonDocument_value_keyUtf8(doc, key);
            if (v) {
                s_lc.m_aiEng[i] = XBsonValue_toInt32(v, 0);
                s_lc.m_aiEngValid[i] = true;
                snprintf(key, sizeof(key), HSF_KEY_AI_ENG_UNIT_FMT, i);
                v = XBsonDocument_value_keyUtf8(doc, key);
                if (v) {
                    const XString* str = XBsonValue_toString(v);
                    const char* u8 = str ? XString_toUtf8(str) : NULL;
                    if (u8)
                        snprintf(s_lc.m_aiEngUnit[i],
                                 sizeof(s_lc.m_aiEngUnit[i]), "%s", u8);
                }
                /* 标定名优先（en{i} 是设备铭牌口径）。 */
                snprintf(key, sizeof(key), HSF_KEY_AI_ENG_NAME_FMT, i);
                v = XBsonDocument_value_keyUtf8(doc, key);
                if (v) {
                    const XString* str = XBsonValue_toString(v);
                    const char* u8 = str ? XString_toUtf8(str) : NULL;
                    if (u8)
                        snprintf(s_lc.m_aiName[i], sizeof(s_lc.m_aiName[i]),
                                 "%s", u8);
                }
                snprintf(key, sizeof(key), HSF_KEY_AI_ENG_NOTE_FMT, i);
                v = XBsonDocument_value_keyUtf8(doc, key);
                if (v) {
                    const XString* str = XBsonValue_toString(v);
                    const char* u8 = str ? XString_toUtf8(str) : NULL;
                    if (u8)
                        snprintf(s_lc.m_aiEngNote[i],
                                 sizeof(s_lc.m_aiEngNote[i]), "%s", u8);
                }
            }
            if (full) {
                snprintf(key, sizeof(key), HSF_KEY_AI_NAME_FMT, i);
                v = XBsonDocument_value_keyUtf8(doc, key);
                if (v) {
                    const XString* str = XBsonValue_toString(v);
                    const char* u8 = str ? XString_toUtf8(str) : NULL;
                    if (u8 && !s_lc.m_aiEngValid[i])
                        snprintf(s_lc.m_aiName[i], sizeof(s_lc.m_aiName[i]),
                                 "%s", u8);
                }
            }
        }
        s_lc.m_aiCount = n;
        s_lc.m_aiValid = true;
        s_lc.m_aiLastMs = XDateTime_currentMSecsSinceEpoch();
    } else {
        /* [2026-11-09 畸形载荷口径] 此前无论解析成败都 return true，畸形帧
         * 会被当成「解析成功」通知订阅者——订阅者读到的是上一拍的旧值，白
         * 画一帧；快照应答路径也没法把畸形载荷当失败留痕。改为按失败返回
         * （与温湿度/流量计/泵/浊度的解析器同口径：载荷不合法 = 没有新数据，
         * 不写 model 也不通知）。 */
        XClassDelete((XClass*)doc);
        return false;
    }
    XClassDelete((XClass*)doc);
    return true;
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
/**
 * @brief 解析浊度仪组帧（count + v{i}_name/ntu/raw/online）。
 * @details NTU 是**线上定点整数 ×100**（协议层口径，与 Python 侧同值）；
 *          客户端显示时 ÷100（原始值另存 m_raw 供排查）。
 */
static bool hsfLcParseTurbidity(const uint8_t* data, uint32_t len)
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
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_TURB_COUNT);
    count = v ? XBsonValue_toInt32(v, 0) : 0;
    if (count < 0) count = 0;
    if (count > HSF_TURBIDITY_COUNT) count = HSF_TURBIDITY_COUNT;
    s_lc.m_turbidity.m_count = count;
    for (i = 0; i < count; ++i) {
        HsfLinkTurbidity* it = &s_lc.m_turbidity.m_items[i];
        char key[40];
        snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, i,
                 HSF_KEY_TURB_NAME);
        v = XBsonDocument_value_keyUtf8(doc, key);
        if (v) {
            const XString* sv = XBsonValue_toString(v);
            const char* u8 = sv ? XString_toUtf8(sv) : NULL;
            if (u8) snprintf(it->m_name, sizeof(it->m_name), "%s", u8);
        }
        snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, i,
                 HSF_KEY_TURB_NTU);
        v = XBsonDocument_value_keyUtf8(doc, key);
        if (v) it->m_ntu100 = XBsonValue_toInt32(v, 0);
        snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, i,
                 HSF_KEY_TURB_RAW);
        v = XBsonDocument_value_keyUtf8(doc, key);
        if (v) it->m_raw = XBsonValue_toInt32(v, 0);
        snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, i,
                 HSF_KEY_TURB_ONLINE);
        v = XBsonDocument_value_keyUtf8(doc, key);
        it->m_online = v ? XBsonValue_toBool(v, false) : false;
        /* 温度（2026-11-10）：**缺键 = 该机型没有温度**，不是 0℃——
         * 站1 WZT-701A 只有 2 个寄存器、无温度；站3 ZHEYI-5100ZDY 一帧
         * 0x0000×4 同刻带出。显示时 m_temp100 ÷100。 */
        it->m_tempValid = false;
        it->m_temp100 = 0;
        snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, i,
                 HSF_KEY_TURB_TEMP);
        v = XBsonDocument_value_keyUtf8(doc, key);
        if (v) {
            it->m_temp100 = XBsonValue_toInt32(v, 0);
            it->m_tempValid = true;
        }
    }
    s_lc.m_turbidity.m_received = true;
    s_lc.m_turbidity.m_lastMs = XDateTime_currentMSecsSinceEpoch();
    XClassDelete((XClass*)doc);
    return true;
}

/**
 * @brief 解析泵组帧（count + v{i}_index/name/on/ready/valid/text）。
 * @note  泵无反馈：m_on 是命令状态（我方最后写的值），不代表泵在转。
 */
static bool hsfLcParsePumps(const uint8_t* data, uint32_t len)
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
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_PUMP_COUNT);
    count = v ? XBsonValue_toInt32(v, 0) : 0;
    if (count < 0) count = 0;
    if (count > HSF_PUMP_COUNT) count = HSF_PUMP_COUNT;
    s_lc.m_pumps.m_count = count;
    for (i = 0; i < count; ++i) {
        HsfLinkPump* it = &s_lc.m_pumps.m_items[i];
        char key[40];
        snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, i,
                 HSF_KEY_PUMP_NAME);
        v = XBsonDocument_value_keyUtf8(doc, key);
        if (v) {
            const XString* sv = XBsonValue_toString(v);
            const char* u8 = sv ? XString_toUtf8(sv) : NULL;
            if (u8) snprintf(it->m_name, sizeof(it->m_name), "%s", u8);
        }
        snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, i,
                 HSF_KEY_PUMP_ON);
        v = XBsonDocument_value_keyUtf8(doc, key);
        it->m_on = v ? XBsonValue_toBool(v, false) : false;
        snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, i,
                 HSF_KEY_PUMP_READY);
        v = XBsonDocument_value_keyUtf8(doc, key);
        it->m_ready = v ? XBsonValue_toBool(v, false) : false;
        snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, i,
                 HSF_KEY_VALVE_VALID);
        v = XBsonDocument_value_keyUtf8(doc, key);
        it->m_valid = v ? XBsonValue_toBool(v, false) : false;
        snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, i,
                 HSF_KEY_PUMP_TEXT);
        v = XBsonDocument_value_keyUtf8(doc, key);
        if (v) {
            const XString* sv = XBsonValue_toString(v);
            const char* u8 = sv ? XString_toUtf8(sv) : NULL;
            if (u8) snprintf(it->m_text, sizeof(it->m_text), "%s", u8);
        }
    }
    s_lc.m_pumps.m_received = true;
    s_lc.m_pumps.m_lastMs = XDateTime_currentMSecsSinceEpoch();
    XClassDelete((XClass*)doc);
    return true;
}

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
    /* [活动侧 2026-11-10] 记下服务端报告的侧号：客户端据此知道设备挂在
     * 哪条总线上（服务端的事实），自己不用猜现场接线。 */
    s_lc.m_flowMeterSide = side;
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

/* ==================== 设备健康/类别文案（首页/报警页共用） ==================== */

/** @brief 设备健康状态中文（与服务端 HsfDeviceHealth 对齐）。 */
const char* HsfLinkClient_healthText(int health)
{
    switch (health) {
    case 0: return "正常";                     /* 正常 */
    case 1: return "告警";                     /* 告警 */
    case 2: return "数据过期"; /* 数据过期 */
    case 3: return "离线";                     /* 离线 */
    case 4: return "故障";                     /* 故障 */
    default: return "未知";                    /* 未知 */
    }
}

/** @brief 设备类别中文（与 HsfDeviceKind / Python KIND_TEXT 对齐）。 */
const char* HsfLinkClient_kindText(int kind)
{
    switch (kind) {
    case 0: return "变频器";        /* 变频器 */
    case 1: return "温湿度";        /* 温湿度 */
    case 2: return "流量计";        /* 流量计 */
    case 3: return "模拟量";        /* 模拟量 */
    case 4: return "485总线";                  /* 485总线 */
    case 5: return "数字量";        /* 数字量 */
    case 6: return "电磁阀组";  /* 电磁阀组 */
    case 7: return "水质";                  /* 水质 */
    default: return "未知";                    /* 未知 */
    }
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
        /* [地址] 连接地址文案（服务端下发；空=该设备无连接概念）。 */
        snprintf(key, sizeof(key), "d%d_addr", i);
        hsfLcCopyValue(XBsonDocument_value_keyUtf8(doc, key),
                       d->m_addr, sizeof(d->m_addr));
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
    case HsfLinkTopic_Turbidity: return HSF_TOPIC_TURBIDITY_STATUS;
    case HsfLinkTopic_Pump:      return HSF_TOPIC_PUMP_STATUS;
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
    /* [两个常驻主题各自独立发送] 原实现用 || 串联：温湿度那一句失败会
     * 短路掉健康主题的订阅（健康卡随即永久无数据，且日志只有一行"常驻
     * 主题订阅失败"分不清是谁）。拆成两句、各记各的失败原因。 */
    if (!XLinkSession_subscribe(session, HSF_TOPIC_TEMPHUM_STATUS))
        XPrintf("HsfLinkClient: 常驻主题 0x%08X（温湿度）订阅失败\n",
                (unsigned)HSF_TOPIC_TEMPHUM_STATUS);
    if (!XLinkSession_subscribe(session, HSF_TOPIC_DEVICE_HEALTH))
        XPrintf("HsfLinkClient: 常驻主题 0x%08X（设备健康）订阅失败\n",
                (unsigned)HSF_TOPIC_DEVICE_HEALTH);
    /* 连接就绪时按已累计的页面引用补订（建窗期累计的引用 / 重连场景
     * 页面仍显示着）。sent 标志随链路作废，此处统一重发。
     * [逐主题失败必须留痕] 补订失败时 m_topicSent 保持 false，后续
     * HsfLinkClient_ensureSubscriptions（页面激活时调用）才有据可补；
     * 无日志的话实测症状就是"某几张卡一直 --，控制台毫无线索"。 */
    for (i = 0; i < (int)HsfLinkTopic_Count; ++i) {
        uint32_t id;
        s_lc.m_topicSent[i] = false;
        if (s_lc.m_topicRef[i] <= 0) continue;
        id = hsfLcTopicId((HsfLinkTopic)i);
        if (id && XLinkSession_subscribe(session, id))
            s_lc.m_topicSent[i] = true;
        else
            XPrintf("HsfLinkClient: 页面主题 0x%08X 订阅失败（引用=%d）\n",
                    id, s_lc.m_topicRef[i]);
    }
}

/**
 * @brief 幂等补订：把「引用>0 但订阅帧没发出去」的主题补发一次。
 * @details [为什么需要] 页面集合没变时 hsf_pageTopicSwitch 走"无差异"
 *          快路径直接返回；若上一次订阅帧因链路抖动/未就绪发送失败，
 *          该主题就再也补不回来——界面表现为"回到首页后某几张卡永久
 *          --"（实测复现：page 7 → 主界面，流量计卡停在「-- 无数据」）。
 *          本函数**只补订、绝不改引用计数**（引用归 subscribe/unsubscribe
 *          记账），故可在每次页面激活时无条件调用；已订阅的主题由
 *          XLinkSession_subscribe 幂等短路，不发重复帧、不占表槽。
 * @note  与 hsfLcSubscribeTopics 的区别：那个是"链路新建"语义（把
 *        m_topicSent 一律作废后重发）；本函数是"链路仍在"语义，**只补
 *        m_topicSent 为 false 的**，不会动已经生效的订阅。
 * @return 链路就绪且补订流程已执行返回 true；未就绪返回 false（引用
 *         保留，重连就绪时 hsfLcSubscribeTopics 会统一补）。
 */
bool HsfLinkClient_ensureSubscriptions(void)
{
    XLinkSession* session;
    int i;
    if (!s_lc.m_client || !XLinkClient_isReady(s_lc.m_client)) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    /* 常驻主题两兄弟也一起兜（幂等，已订阅即短路）。 */
    (void)XLinkSession_subscribe(session, HSF_TOPIC_TEMPHUM_STATUS);
    (void)XLinkSession_subscribe(session, HSF_TOPIC_DEVICE_HEALTH);
    for (i = 0; i < (int)HsfLinkTopic_Count; ++i) {
        uint32_t id;
        if (s_lc.m_topicRef[i] <= 0 || s_lc.m_topicSent[i]) continue;
        id = hsfLcTopicId((HsfLinkTopic)i);
        if (!id) continue;
        if (XLinkSession_subscribe(session, id))
            s_lc.m_topicSent[i] = true;
        else
            XPrintf("HsfLinkClient: 补订主题 0x%08X 失败（引用=%d，"
                    "下次切页/重连重试）\n", id, s_lc.m_topicRef[i]);
    }
    return true;
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
        /* [失败不回收引用] 引用表达的是"某页当前正显示"（见 close 注释：
         * 引用属页面生命周期，不属连接生命周期），发送成败不该改动它——
         * 原实现回滚引用，而页面侧 m_subTopics 位掩码仍保留该主题位，两边
         * 就此失步：下次切回同一页位掩码无差异 ⇒ 不再发订阅帧，该主题
         * 永久无推送（界面表现为某几张卡一直 "--"，切页也不恢复）。
         * 保留引用后，HsfLinkClient_ensureSubscriptions（每次页面激活
         * 调用）与重连后的 hsfLcSubscribeTopics 都能据此重试。 */
        XPrintf("HsfLinkClient: 主题 0x%08X 订阅帧发送失败（引用=%d，"
                "切页/重连时重试）\n", id, s_lc.m_topicRef[topic]);
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
 * @brief 载荷是否为「否定包络」（有 ok 键且为 false）——命令的失败应答。
 * @param data BSON 文档字节；len 字节数。
 * @param outErr 失败原因输出（UTF-8；可为 NULL）；无 err 键时写空串。
 * @param errCap outErr 容量。
 * @return 是失败包络返回 true。
 * @details [为什么必须单独认这个形状] 快照命令**成功**时的应答是裸快照文档
 *          （根本没有 ok 键）；只有「进水泵组未装配」「权限不足」这类拒绝才
 *          回 {"ok":false,"err":...}。若直接交给快照解析器，失败包络会被
 *          当成「0 个条目」写进 model（界面从「有一台泵」变成「一台都没有」，
 *          看着像数据丢了）。**注意**不能复用 hsfLcParseEnvelope：它对缺键
 *          也给 ok=false，而快照文档本来就没有 ok 键。
 */
static bool hsfLcIsErrorEnvelope(const uint8_t* data, uint32_t len,
                                 char* outErr, size_t errCap)
{
    XBsonDocument* doc;
    XBsonValue* v;
    bool isErr = false;

    if (outErr && errCap > 0) outErr[0] = '\0';
    if (!data || len == 0) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (XBsonDocument_from_bytes(doc, data, len)) {
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_OK);
        if (v && !XBsonValue_toBool(v, true)) {
            isErr = true;
            v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_ERR);
            if (v && outErr && errCap > 0)
                hsfLcCopyValue(v, outErr, errCap);
        }
    }
    XClassDelete((XClass*)doc);
    return isErr;
}

/**
 * @brief 快照应答可用性闸：失败包络（{"ok":false,"err":...}）直接留痕并拒绝。
 * @param msgCode 业务命令码（日志用）。
 * @param data BSON 文档字节；len 字节数。
 * @return 载荷是快照（可继续解析）返回 true；是失败包络返回 false（已打日志）。
 * @details [为什么每条快照应答都要过这道闸] 五条快照命令的成功应答都是**裸
 *          快照文档**（没有 ok 键），而「权限不足」「设备未装配」「未知命令」
 *          这类拒绝回的是负包络。若把负包络交给快照解析器：
 *            · 模拟量 → n 缺失 ⇒ 通道数被清成 0（界面直接「无数据」，看起来
 *              像设备掉线，实际只是这条请求被拒）；
 *            · 浊度 → count 缺失 ⇒ 台数清零（同理）；
 *            · 泵/流量计/变频器 → 只丢 valid 或保持旧值，危害小但同样不可取。
 *          故统一在解析前认这个形状：失败就**不写 model**，只留一行日志
 *          （含命令码与长度，现场能据此判断是权限问题还是设备未装配）。
 */
static bool hsfLcSnapshotPayloadOk(uint16_t msgCode, const uint8_t* data,
                                   uint32_t len)
{
    char err[96];
    if (!hsfLcIsErrorEnvelope(data, len, err, sizeof(err))) return true;
    XPrintf("HsfLinkClient: 快照应答为失败包络（cmd=0x%04X len=%u）：%s\n",
            (unsigned)msgCode, (unsigned)len, err[0] ? err : "未知原因");
    return false;
}

/**
 * @brief 模式包络回填（RUN_MODE 与 AUTO_CONTROL 同形应答共用）。
 * @param doc 已解析的应答文档（借用；NULL 安全）。
 * @param ok 应答是否成功（成功才回填模式/运行位，与既有 RUN_MODE 口径一致）。
 * @param outErr 失败原因输出（UTF-8；可 NULL 表示不记录）。
 * @param errCap outErr 容量。
 * @details 服务端 0x0105（RUN_MODE）与 0x0115（AUTO_CONTROL）都用
 *          hsfAppMakeModeEnvelope 回包：{ok, err?, mode, auto, autoRunning}。
 *          两个命令的处理逻辑必须同源，否则「自动运行被拒后界面还显示旧的
 *          运行位」这类漂移会只在一侧出现。
 */
static void hsfLcApplyModeDoc(XBsonDocument* doc, bool ok,
                              char* outErr, size_t errCap)
{
    XBsonValue* v;
    if (!doc) return;
    if (ok) {
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_MODE);
        if (v) {
            s_lc.m_modeAuto = (XBsonValue_toInt32(v, 0) == 1) ? 1 : 0;
            s_lc.m_modeKnown = true;
        }
        /* [自动运行状态] "自动模式"与"控制器真的在跑"是两件事：
         * 服务端 0x0105 应答带 auto / autoRunning 两个键。缺失时
         * 保持"未知"（三态：未知/已停止/运行中）——不能把缺失当
         * false（那会把"服务端没上报"显示成"已停止"）。 */
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_AUTO);
        if (v) {
            s_lc.m_autoRunning = -1;   /* 重置为未知 */
            s_lc.m_autoRunningKnown = false;
        }
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_AUTO_RUNNING);
        if (v) {
            s_lc.m_autoRunning = XBsonValue_toBool(v, false) ? 1 : 0;
            s_lc.m_autoRunningKnown = true;
        }
        return;
    }
    if (outErr && errCap > 0) {
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_ERR);
        if (v) hsfLcCopyValue(v, outErr, errCap);
    }
}

/**
 * @brief 业务帧分发（会话 pump 栈上同步执行；上下文=模块单例）。
 */
static void hsfLcDispatch(XLinkSession* session, const XLinkFrameHeader* frame,
                          const uint8_t* payload)
{
    /* [只要求 frame 非空] 分发全程不读 session（Response/Publish 两个分支只用
     * frame 与 payload）：白盒测试钩子 HsfLinkClient_testDispatchResponse 在
     * 未连线（没有会话对象）时也要能喂应答帧，故不把会话当必要条件。 */
    (void)session;
    if (!frame) return;
    switch (frame->m_msgClass) {
    case XLinkMsgClass_Response:
        if (frame->m_msgCode == HSF_CMD_HISTORY_PAGE) {
            /* [失败包络要留痕 2026-11-10] 落盘关闭时服务端回的是
             * {"ok":false,"err":"落盘未启用：…"}（不是分页文档）。此前
             * 直接当分页文档解析 ⇒ total=0、rows 空 ⇒ 界面「共 0 条」+
             * 停在「读取中」，现场像「表格数据没了」，看不到真正原因。
             * 现在先认包络：失败就把 err 原文带进结果结构，页面显示它。 */
            char err[sizeof(s_lc.m_history.m_error)];
            if (hsfLcIsErrorEnvelope(payload, frame->m_payloadLen, err,
                                      sizeof(err))) {
                s_lc.m_history.m_valid = false;
                s_lc.m_history.m_count = 0;
                s_lc.m_history.m_total = 0;
                snprintf(s_lc.m_history.m_error,
                         sizeof(s_lc.m_history.m_error), "%s",
                         err[0] ? err : "服务端拒绝了历史查询");
                XPrintf("HsfLinkClient: 历史查询被拒：%s\n",
                        s_lc.m_history.m_error);
                break;
            }
            s_lc.m_history.m_error[0] = '\0';
            (void)HsfHistoryPage_parse(payload, frame->m_payloadLen,
                                       &s_lc.m_history);
            break;
        }
        if (frame->m_msgCode == HSF_CMD_LOG_PAGE) {
            /* 日志分页应答：解析进独立槽（与 0x0118 的 m_history 互不覆盖，
             * 两个页面各看各的，不存在历史页那种"单一应答槽串扰"问题）。 */
            (void)hsfLcParseLogPage(payload, frame->m_payloadLen);
            break;
        }
        if (frame->m_msgCode == HSF_CMD_CONFIG_GET) {
            (void)HsfConfig_parse(payload, frame->m_payloadLen, &s_lc.m_config);
            ++s_lc.m_cfgGetSeq;       /* 新快照 → 页面才回填控件（避免每拍覆盖用户输入）。 */
            break;
        }
        if (frame->m_msgCode == HSF_CMD_CONFIG_SET) {
            /* [写应答] 与 Python 同口径：ok/err 回显到状态行（
             * 周期可能被服务端夹取过，页面以随后的 GET 快照为准）。 */
            bool ok = false;
            s_lc.m_cfgErr[0] = '\0';
            hsfLcParseEnvelope(payload, frame->m_payloadLen, &ok,
                               s_lc.m_cfgErr, sizeof(s_lc.m_cfgErr));
            s_lc.m_cfgOk = ok;
            ++s_lc.m_cfgSeq;
            break;
        }
        if (frame->m_msgCode == HSF_CMD_DEVICE_HEALTH) {
            hsfLcOnHealthPayload(payload, frame->m_payloadLen);
            break;
        }
        if (frame->m_msgCode == HSF_CMD_TEMPHUM_SNAPSHOT) {
            /* [温湿度快照应答 2026-11-09 根修] 这条应答此前**无人处理**：
             * HsfLinkClient_tempHumSnapshotRequest() 把请求发出去了，服务端
             * 也回了（键表与推送帧完全同构：side/temp/hum/tempValid/humValid），
             * 但客户端的 Response 分支里没有这一项 ⇒ 应答被**静默丢弃**，
             * model 永远不更新 ⇒ 「立即要一拍」这条自愈路是死的。
             * 现象：服务端周期推送一旦停摆（订阅退订/推送失步/服务端节拍
             * 停摆），客户端温湿度只在「订阅即补发」那一帧里有值，3.5s 后
             * 新鲜度判负 ⇒ 标题栏恒显「--℃ / --%」。此处补上应答解析：
             * 走与推送帧同一个解析器（hsfLcParseTempHum）刷新 model，
             * 再广播订阅者（标题栏/首页卡片同拍刷新）。 */
            int side = HSF_SIDE_A;
            if (!hsfLcSnapshotPayloadOk(frame->m_msgCode, payload,
                                        frame->m_payloadLen))
                break;
            if (hsfLcParseTempHum(payload, frame->m_payloadLen, &side)) {
                hsfLcNotifyTempHum();
            }
            break;
        }
        if (frame->m_msgCode == HSF_CMD_AI_SNAPSHOT) {
            /* [模拟量快照应答 2026-11-09 根修] 与温湿度同一条系统性缺陷：
             * HsfLinkClient 连上就发 HSF_CMD_AI_SNAPSHOT 要一次**全量**（名字
             * 只在全量帧里下发），服务端 hsfAppHandleAiSnapshot→hsfAppEncodeAi
             * (true) 回的帧与 HSF_TOPIC_AI_STATUS 推送帧完全同构（n/v{u}/raw/
             * e/eu/en/ew/an + full），但 Response 分支此前没有这一项 ⇒ 应答
             * 被静默丢弃：首拍名字/工程量拿不到，推送停摆时也没有兜底通道。
             * 解析走与推送同一个 hsfLcParseAi（含"只接受显示侧"过滤），
             * 之后广播订阅者（模拟量页/首页卡片同拍刷新）。 */
            int si;
            if (!hsfLcSnapshotPayloadOk(frame->m_msgCode, payload,
                                        frame->m_payloadLen))
                break;
            if (!hsfLcParseAi(payload, frame->m_payloadLen)) {
                XPrintf("HsfLinkClient: 模拟量快照应答解析失败"
                        "（cmd=0x%04X len=%u），保持上次快照\n",
                        (unsigned)frame->m_msgCode,
                        (unsigned)frame->m_payloadLen);
                break;
            }
            for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                if (s_lc.m_ai[si].m_cb)
                    s_lc.m_ai[si].m_cb(s_lc.m_ai[si].m_user);
            }
            break;
        }
        if (frame->m_msgCode == HSF_CMD_FLOWMETER_SNAPSHOT) {
            /* [流量计快照应答 2026-11-09 根修] 请求由
             * HsfLinkClient_flowMeterSnapshotRequest（流量计页/首页自愈）发出，
             * 应答 = hsfAppInsertSide + hsfAppFillFlowMeter 的裸快照文档
             * （side/flow/speed/forwardTotal/reverseTotal/flowUnit/totalUnit/
             * alarms/valid），与 HSF_TOPIC_FLOWMETER_STATUS 推送帧同构。
             * 此前无应答分支 ⇒ 补拍请求发得出去、回包被丢，页面只能等奖下一
             * 拍推送（推送停摆时永远等不到）。 */
            int side = HSF_SIDE_A;
            int si;
            if (!hsfLcSnapshotPayloadOk(frame->m_msgCode, payload,
                                        frame->m_payloadLen))
                break;
            if (!hsfLcParseFlowMeter(payload, frame->m_payloadLen, &side)) {
                XPrintf("HsfLinkClient: 流量计快照应答解析失败"
                        "（cmd=0x%04X len=%u），保持上次快照\n",
                        (unsigned)frame->m_msgCode,
                        (unsigned)frame->m_payloadLen);
                break;
            }
            for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                if (s_lc.m_flowMeter[si].m_cb)
                    s_lc.m_flowMeter[si].m_cb(s_lc.m_flowMeter[si].m_user);
            }
            break;
        }
        if (frame->m_msgCode == HSF_CMD_TURBIDITY_SNAPSHOT) {
            /* [浊度仪快照应答 2026-11-09 根修] 应答 = side + 浊度仪组帧
             * （count + v{i}_name/ntu/raw/online），与 HSF_TOPIC_TURBIDITY_STATUS
             * 推送帧同构；解析器忽略 side（两台仪器是同一组成套设备）。 */
            int si;
            if (!hsfLcSnapshotPayloadOk(frame->m_msgCode, payload,
                                        frame->m_payloadLen))
                break;
            if (!hsfLcParseTurbidity(payload, frame->m_payloadLen)) {
                XPrintf("HsfLinkClient: 浊度仪快照应答解析失败"
                        "（cmd=0x%04X len=%u），保持上次快照\n",
                        (unsigned)frame->m_msgCode,
                        (unsigned)frame->m_payloadLen);
                break;
            }
            for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                if (s_lc.m_turbCb[si].m_cb)
                    s_lc.m_turbCb[si].m_cb(s_lc.m_turbCb[si].m_user);
            }
            break;
        }
        if (frame->m_msgCode == HSF_CMD_PUMP_SNAPSHOT) {
            /* [泵快照应答 2026-11-09 根修] 应答通常是 hsfAppEncodePump 的裸
             * 快照（count + v{i}_index/name/on/ready/valid/text），键表与
             * HSF_TOPIC_PUMP_STATUS 推送帧同构；但**未装配/权限不足**时服务端
             * 回的是 {"ok":false,"err":...} 负包络（hsfLcSnapshotPayloadOk 挡）。 */
            int si;
            if (!hsfLcSnapshotPayloadOk(frame->m_msgCode, payload,
                                        frame->m_payloadLen))
                break;
            if (!hsfLcParsePumps(payload, frame->m_payloadLen)) {
                XPrintf("HsfLinkClient: 泵快照应答解析失败"
                        "（cmd=0x%04X len=%u），保持上次快照\n",
                        (unsigned)frame->m_msgCode,
                        (unsigned)frame->m_payloadLen);
                break;
            }
            for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                if (s_lc.m_pumpCb[si].m_cb)
                    s_lc.m_pumpCb[si].m_cb(s_lc.m_pumpCb[si].m_user);
            }
            break;
        }
        if (frame->m_msgCode == HSF_CMD_VFD_SNAPSHOT) {
            /* [变频器快照应答 2026-11-09 根修] 请求由
             * HsfLinkClient_vfdSnapshotRequest（provider 就绪桥/自愈）发出，
             * 应答 = XVfdCodec 遥测键表 + side + ratedRpm/ratedHz 的裸快照，
             * 与 HSF_TOPIC_VFD_STATUS 推送帧同构（多出的两个额定点键由页面
             * 量程用，解码器忽略未知键）。此前无应答分支 ⇒ 就绪后那一拍
             * 快照是死的，变频器页只能等 100ms 周期推送。 */
#if XVFD_ON
            int side = HSF_SIDE_A;
            int si;
            if (!hsfLcSnapshotPayloadOk(frame->m_msgCode, payload,
                                        frame->m_payloadLen))
                break;
            if (!XVfdCodec_statusFromBson_2(&s_lc.m_vfdStatus, &side,
                                            payload, frame->m_payloadLen)) {
                XPrintf("HsfLinkClient: 变频器快照应答解析失败"
                        "（cmd=0x%04X len=%u），保持上次快照\n",
                        (unsigned)frame->m_msgCode,
                        (unsigned)frame->m_payloadLen);
                break;
            }
            if (side != HSF_SIDE_A) break;   /* 界面只显示 A 侧（同推送口径）。 */
            s_lc.m_vfdValid = true;
            s_lc.m_vfdLastMs = XDateTime_currentMSecsSinceEpoch();
            for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                if (s_lc.m_vfd[si].m_cb)
                    s_lc.m_vfd[si].m_cb(s_lc.m_vfd[si].m_user);
            }
#else
            XPrintf("HsfLinkClient: 收到变频器快照应答但本构建未启用 VFD"
                    "（cmd=0x%04X len=%u）\n", (unsigned)frame->m_msgCode,
                    (unsigned)frame->m_payloadLen);
#endif
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
                hsfLcApplyModeDoc(doc, ok, s_lc.m_modeErr,
                                  sizeof(s_lc.m_modeErr));
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
            const bool suppress = s_lc.m_suppressNextErr;
            s_lc.m_lastCmdOk = false;
            s_lc.m_suppressNextErr = false;   /* 抑制只作用于下一条应答。 */
            hsfLcParseEnvelope(payload, frame->m_payloadLen, &ok,
                               s_lc.m_lastCmdErr, sizeof(s_lc.m_lastCmdErr));
            s_lc.m_lastCmdOk = ok;
            ++s_lc.m_lastCmdSeq;        /* 增量观测序号（成功/失败都推进）。 */
            /* [静默失败] 启动前的"预置给定"在停机态会被服务端拒（停机禁
             * 给定闸），Python 客户端把这一步设为静默（两条状态互相冲掉，
             * 只留最终那条启动结果）。此处同样不把失败写进错误文本——页面
             * 只看"错误文本非空"决定是否提示，故清空即静默。 */
            if (suppress && !ok) s_lc.m_lastCmdErr[0] = '\0';
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
            /* [整帧应用] 控制应答带整帧阀快照（count + v{i}_*）——立刻应用
             * 并通知页面刷新卡片，不必等下一拍周期推送（对齐 Python 侧
             * 2026-11-05「点第一下没反应」的修复：应答里的快照是命令之后的
             * 真实状态）。 */
            hsfLcParseValveCmdResult(payload, frame->m_payloadLen,
                                      s_lc.m_lastValveIndex);
            if (hsfLcParseValves(payload, frame->m_payloadLen))
                hsfLcNotifyValves();
            if (s_lc.m_cmdCb)
                s_lc.m_cmdCb(s_lc.m_cmdUser, HSF_CMD_VALVE_CONTROL, ok,
                             ok ? "" : s_lc.m_lastCmdErr);
        }
        else if (frame->m_msgCode == HSF_CMD_PUMP_CONTROL) {
            /* [泵控制应答 2026-11-09 根修] **不是被通用路径覆盖的**——
             * 查过 hsfLcDispatch：本函数的 Response 分支全部按 m_msgCode
             * 逐条分派，**没有任何按 frame->m_requestId 的统一 ok/err 包络
             * 处理**（会话层的 XLinkSession_requestFinished 信号本文件也没接）。
             * ⇒ 0x0114 的应答此前同样被静默丢弃：泵命令的成败/拒绝原因既
             * 不写 m_lastCmdOk/m_lastCmdErr（页面轮询 commandSeq 拿不到推进），
             * 也不触发 cmdCb。与 VALVE_CONTROL 同口径：成功时顺便应用应答里
             * 带的整帧泵快照（服务端把 ok 与快照并成一帧），不必等下一拍推送。 */
            bool ok = false;
            s_lc.m_lastCmdOk = false;
            hsfLcParseEnvelope(payload, frame->m_payloadLen, &ok,
                               s_lc.m_lastCmdErr, sizeof(s_lc.m_lastCmdErr));
            s_lc.m_lastCmdOk = ok;
            ++s_lc.m_lastCmdSeq;
            if (ok) {
                int si;
                if (hsfLcParsePumps(payload, frame->m_payloadLen)) {
                    for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                        if (s_lc.m_pumpCb[si].m_cb)
                            s_lc.m_pumpCb[si].m_cb(s_lc.m_pumpCb[si].m_user);
                    }
                }
            }
            if (s_lc.m_cmdCb)
                s_lc.m_cmdCb(s_lc.m_cmdUser, HSF_CMD_PUMP_CONTROL, ok,
                             ok ? "" : s_lc.m_lastCmdErr);
        }
        else if (frame->m_msgCode == HSF_CMD_AUTO_CONTROL) {
            /* [自动运行控制应答 2026-11-09 根修] 同上：无按 requestId 的通用
             * 命令结果路径 ⇒ 0x0115 应答此前被丢弃。远端设置页（
             * HsfSettingsRemote）正是靠 HsfLinkClient_commandSeq()/lastCommandError()
             * 轮询来把「启动/停止/急停/复位」的成败显示到状态行的——没有这条
             * 分支，序列号永不推进，失败原因永远浮不上来（界面只停在
             * 「正在启动自动运行…」）。
             * 应答形状 = 模式包络 {ok,err?,mode,auto,autoRunning}
             * （hsfAppMakeModeEnvelope），与 HSF_CMD_RUN_MODE 完全同形，故
             * 模式/运行位回填复用 hsfLcApplyModeDoc（同一份逻辑，不另写一套）。
             * 失败原因写 m_lastCmdErr **不写** m_modeErr：后者是「切换模式
             * 失败」的专属文案，混进来会让状态行把自动运行失败显示成
             * 「切换失败」。 */
            XBsonDocument* doc = XBsonDocument_create();
            bool ok = false;
            int si;
            s_lc.m_lastCmdOk = false;
            s_lc.m_lastCmdErr[0] = '\0';
            if (doc && XBsonDocument_from_bytes(doc, payload, frame->m_payloadLen)) {
                XBsonValue* v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_OK);
                if (v) ok = XBsonValue_toBool(v, false);
                hsfLcApplyModeDoc(doc, ok, s_lc.m_lastCmdErr,
                                  sizeof(s_lc.m_lastCmdErr));
            } else {
                XPrintf("HsfLinkClient: 自动运行应答解析失败"
                        "（cmd=0x%04X len=%u）\n", (unsigned)frame->m_msgCode,
                        (unsigned)frame->m_payloadLen);
            }
            if (doc) XClassDelete((XClass*)doc);
            s_lc.m_lastCmdOk = ok;
            ++s_lc.m_lastCmdSeq;      /* 失败也推进：页面据此弹错误文案。 */
            for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                if (s_lc.m_modeCb[si].m_cb)
                    s_lc.m_modeCb[si].m_cb(s_lc.m_modeCb[si].m_user);
            }
            if (s_lc.m_cmdCb)
                s_lc.m_cmdCb(s_lc.m_cmdUser, HSF_CMD_AUTO_CONTROL, ok,
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
            int side = HSF_SIDE_A;
            /* [只接受显示侧 2026-11-08] 服务端按「主题 × 侧」各推一帧（VFD
             * 100ms × A/B 两侧 = 20 帧/秒）。此前两份都写进同一个
             * m_vfdStatus 并逐帧通知订阅者，导致：
             *   (a) 界面数值在 A/B 之间每 100ms 交替（两侧读数不同时肉眼
             *       可见跳变）——这是**正确性**问题；
             *   (b) 仪表/横幅/趋势图每秒被刷 20 次，实测 VFD 页 28fps、
             *       帧区域均值 133K px。
             * 界面（含标题栏）显示 A 侧（HSF_SIDE_A），非显示侧的帧直接
             * 丢弃：不写状态、不通知 ⇒ 帧数减半且不再跳变。
             * [为什么不按侧分别存] 页面只用一侧，per-side 存储只会多占内存
             * 与分支；将来要做 A/B 同屏对比时再改。 */
            if (!XVfdCodec_statusFromBson_2(&s_lc.m_vfdStatus, &side,
                                            body, bodyLen))
                break;
            if (side != HSF_SIDE_A) break;
            s_lc.m_vfdValid = true;
            s_lc.m_vfdLastMs = XDateTime_currentMSecsSinceEpoch();
            for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                if (s_lc.m_vfd[si].m_cb)
                    s_lc.m_vfd[si].m_cb(s_lc.m_vfd[si].m_user);
            }
#endif
        } else if (topicId == HSF_TOPIC_DEVICE_HEALTH) {
            hsfLcOnHealthPayload(body, bodyLen);
        } else if (topicId == HSF_TOPIC_AI_STATUS) {
            int si;
            /* 解析函数内已做显示侧过滤：非显示侧返回 false，不通知订阅者。 */
            if (!hsfLcParseAi(body, bodyLen)) break;
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
        } else if (topicId == HSF_TOPIC_TURBIDITY_STATUS) {
            int si;
            if (!hsfLcParseTurbidity(body, bodyLen)) break;
            for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                if (s_lc.m_turbCb[si].m_cb)
                    s_lc.m_turbCb[si].m_cb(s_lc.m_turbCb[si].m_user);
            }
        } else if (topicId == HSF_TOPIC_PUMP_STATUS) {
            int si;
            if (!hsfLcParsePumps(body, bodyLen)) break;
            for (si = 0; si < HSF_LC_SUB_SLOTS; ++si) {
                if (s_lc.m_pumpCb[si].m_cb)
                    s_lc.m_pumpCb[si].m_cb(s_lc.m_pumpCb[si].m_user);
            }
        }
        break;
    }
    default:
        break;
    }
}

/**
 * @brief 白盒测试入口：把一条 **Response** 业务帧喂给业务分发。
 * @param msgCode 业务命令码（HSF_CMD_*；例如 HSF_CMD_AI_SNAPSHOT）。
 * @param payload 载荷字节（BSON；借用）。
 * @param len 载荷长度。
 * @return 参数非法（len>0 但 payload 为 NULL）返回 false，其余返回 true。
 * @details [为什么需要它] 五条快照命令的应答分支是「请求-应答」通道，离屏
 *          （无服务端）根本收不到真实应答；靠界面截图验不出「应答被解析并
 *          刷新了 model」。这里直接把构造好的应答帧喂进与线上**完全相同**的
 *          分发函数（hsfLcDispatch），测试即可断言 model 被更新、订阅者被
 *          通知。分发不依赖会话对象（见函数入口注记），未连线也可调用。
 * @warning 仅供测试（Test/XProtocolTest/hsf_regression_test.c §I）使用；
 *          生产路径请勿调用（它会绕过会话层的 pending/去重）。
 */
bool HsfLinkClient_testDispatchResponse(uint16_t msgCode,
                                        const uint8_t* payload, uint32_t len)
{
    XLinkFrameHeader frame;

    if (!payload && len > 0) return false;
    memset(&frame, 0, sizeof(frame));
    frame.m_msgClass = XLinkMsgClass_Response;
    frame.m_msgCode = msgCode;
    frame.m_payloadLen = len;
    hsfLcDispatch(NULL, &frame, payload);
    return true;
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
        /* 模拟量首拍全量（名字/工程量只在全量帧里下发）；走公开接口，
         * 自愈补拍（HsfLinkClient_aiEnsureFresh）用的是同一个发包函数。 */
        (void)HsfLinkClient_aiSnapshotRequest();
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
/**
 * @brief 认证失败：区分「永久失败（弹登录窗/收手）」与「瞬时拒绝（重试中）」
 * @details [2026-11-09 根修] 框架把认证失败分为永久（BadUser/BadPassword/
 *          Locked/NoCredential）与瞬时（Rejected：同账并发/会话上限，
 *          Timeout/Protocol：链路噪声）。瞬时拒绝在「已登录过」的重连路径上
 *          **不能**触发 fail 回调——那是登录窗语义，主窗开着时只会让状态栏
 *          显示一句 "登录失败" 而界面继续等；框架此时仍在退避重试。
 *          这里改为状态栏提示（notice），文案点明「账户可能已在线」，
 *          数据会在旧会话被服务端回收后自动恢复。
 *          首连（从未就绪）被拒仍走 fail 回调：登录窗留在原地给出准确原因。
 */
static void hsfLcOnAuthFailed(XObject* sender, XVarList* args)
{
    int reason = -1;
    int status = -1;
    int i;
    bool transient;
    (void)sender;
    if (args) {
        XVarList_start(args);
        reason = XVarList_arg(args, int);
        status = XVarList_arg(args, int);
    }
    XPrintf("HsfLinkClient: 认证失败（reason=%d status=%d everReady=%d）\n",
            reason, status, s_lc.m_everReady ? 1 : 0);

    /* 与框架 XLinkClientAuthFailure 同源：4=Rejected，6=Timeout，8=Protocol。 */
    transient = (reason == 4 || reason == 6 || reason == 8);
    if (transient && s_lc.m_everReady) {
        const char* text = (reason == 4)
            ? "服务端拒绝（该账户可能已在别处登录），正在重试…"
            : "认证暂时失败（链路超时/协议异常），正在重试…";
        if (s_lc.m_noticeCb)
            s_lc.m_noticeCb(s_lc.m_noticeUser, text);
        return;    /* 框架已按 XLINK_AUTH_REJECT_RETRY_LIMIT 退避重连 */
    }

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
    s_lc.m_vfdLastMs = 0;          /* 新连线：新鲜度与补拍节流从头计。 */
    memset(s_lc.m_aiUa, 0, sizeof(s_lc.m_aiUa));
    s_lc.m_aiMask = 0;
    s_lc.m_aiCount = 0;
    s_lc.m_aiValid = false;
    s_lc.m_aiLastMs = 0;
    memset(s_lc.m_tempHumState, 0, sizeof(s_lc.m_tempHumState));
    s_lc.m_lastTempHumReqMs = 0;   /* 新连线：补拍节流重新起算。 */
    memset(s_lc.m_flowMeterState, 0, sizeof(s_lc.m_flowMeterState));
    memset(s_lc.m_lastSnapReqMs, 0, sizeof(s_lc.m_lastSnapReqMs));
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
    /* [记下服务端地址] 远端设置页要显示"连的是哪台"（现场多台机器时
     * 光看角色看不出连的是谁）。 */
    snprintf(s_lc.m_serverAddr, sizeof(s_lc.m_serverAddr), "%s:%u", host,
             (unsigned)port);
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

bool HsfLinkClient_isReconnecting(void)
{
    return s_lc.m_client
        && XLinkClient_state(s_lc.m_client) == XLinkClient_ReconnectingState;
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
 * @brief 四档角色→权限位图的**客户端镜像**（服务端 XPermPolicy 同源）。
 * @details 下行下标严格等于 XLinkAuthRole 值（0/1/2/3），**不是权限档序**。
 *          与服务端 XPermission.c 的 s_permDefaults 逐位对齐：
 *            匿名=查看 / 普通=查看+下载 / 管理员=全权 / 操作者=+控制。
 * @warning 只影响界面可达性；授权以服务端为准。
 */
static uint32_t hsfLcRolePerms(int role)
{
    switch (role) {
    case (int)XLinkAuthRole_Anonymous: return HSF_PERM_VIEW;
    case (int)XLinkAuthRole_User:      return HSF_PERM_VIEW | HSF_PERM_DOWNLOAD;
    case (int)XLinkAuthRole_Admin:     return HSF_PERM_ALL;
    case (int)XLinkAuthRole_Operator:
        return HSF_PERM_VIEW | HSF_PERM_DOWNLOAD | HSF_PERM_DEVICE_CONTROL;
    default:                           return 0;
    }
}

bool HsfLinkClient_can(uint32_t perm)
{
    if (perm == 0) return true;
    return (hsfLcRolePerms(HsfLinkClient_role()) & perm) == perm;
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

void HsfLinkClient_setNoticeCallback(HsfLinkNoticeFn cb, void* userData)
{
    /* 单槽：主窗建窗时注册一次；NULL 摘除。跨连接复用（不像 ready/fail
       属于登录窗的一次性语义），故 hsfLcResetConnectionState 不动它。 */
    s_lc.m_noticeCb = cb;
    s_lc.m_noticeUser = cb ? userData : NULL;
}

int HsfLinkClient_lastAuthFailure(void)
{
    if (!s_lc.m_client) return 0;
    return XLinkClient_lastAuthFailure(s_lc.m_client);
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

void HsfLinkClient_setValveAlarmCallback(HsfLinkValveAlarmFn cb, void* userData)
{
    s_lc.m_valveAlarmCb = cb;
    s_lc.m_valveAlarmUser = userData;
}

const HsfLinkFlowMeter* HsfLinkClient_flowMeter(int side)
{
    if (side < 0 || side >= HSF_LINK_SIDE_COUNT) return NULL;
    return &s_lc.m_flowMeterState[side];
}

int HsfLinkClient_flowMeterSide(void)
{
    return s_lc.m_flowMeterSide;
}

const HsfLinkFlowMeter* HsfLinkClient_flowMeterActive(void)
{
    return HsfLinkClient_flowMeter(s_lc.m_flowMeterSide);
}

bool HsfLinkClient_flowMeterActiveFresh(void)
{
    return HsfLinkClient_flowMeterFresh(s_lc.m_flowMeterSide);
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

bool HsfLinkClient_aiEngineering(int index, int32_t* outValue1000,
                                 char* outUnit, int unitCap,
                                 const char** outNote)
{
    if (index < 0 || index >= HSF_LINK_AI_CH_MAX) return false;
    if (!s_lc.m_aiEngValid[index]) return false;
    if (outValue1000) *outValue1000 = s_lc.m_aiEng[index];
    if (outUnit && unitCap > 0)
        snprintf(outUnit, (size_t)unitCap, "%s", s_lc.m_aiEngUnit[index]);
    if (outNote) *outNote = s_lc.m_aiEngNote[index][0] ? s_lc.m_aiEngNote[index] : NULL;
    return true;
}

int32_t HsfLinkClient_aiRawMv(int index)
{
    if (index < 0 || index >= HSF_LINK_AI_CH_MAX) return 0;
    return s_lc.m_aiRaw[index];
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

/**
 * @brief 广播温湿度更新给全部订阅槽。
 * @note  推送帧与**快照应答**共用：两条路径都刷新同一份 model
 *        （m_tempHumState），订阅者拿到的口径完全一致。
 */
static void hsfLcNotifyTempHum(void)
{
    int i;
    for (i = 0; i < HSF_LC_SUB_SLOTS; ++i) {
        if (s_lc.m_tempHum[i].m_cb)
            s_lc.m_tempHum[i].m_cb(s_lc.m_tempHum[i].m_user);
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
 * @details [先认告警再认状态] 帧带 "ac" 键即告警帧（a{i}_* 键空间）——
 *          告警帧逐条回调且**绝不动阀状态模型**（告警帧无 count，旧实现
 *          落到这里 count=0 ⇒ 六路按钮全死直到下一拍状态帧，即 Python
 *          客户端注释里的同一口实锅）。无 ac 才按状态帧解析。
 * @return 解析成功返回 true（含告警帧）。
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
    /* ---- 告警帧分支（ac 键判型）---- */
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_ALARM_COUNT);
    if (v) {
        int n = XBsonValue_toInt32(v, 0);
        if (n < 0) n = 0;
        if (n > 8) n = 8;   /* 一帧至多合并 8 条；服务端一拍 6 路全超时=6。 */
        for (i = 0; i < n; ++i) {
            HsfLinkValveAlarm alarm;
            char key[32];
            XBsonValue* av;
            memset(&alarm, 0, sizeof(alarm));
            alarm.m_index = -1;
            snprintf(key, sizeof(key), "a%d_%s", i, HSF_KEY_ALARM_VALVE_INDEX);
            av = XBsonDocument_value_keyUtf8(doc, key);
            if (av) alarm.m_index = XBsonValue_toInt32(av, -1);
            snprintf(key, sizeof(key), "a%d_%s", i, HSF_KEY_ALARM_VALVE_NAME);
            av = XBsonDocument_value_keyUtf8(doc, key);
            if (av) {
                const XString* s = XBsonValue_toString(av);
                const char* u = s ? XString_toUtf8(s) : NULL;
                if (u) snprintf(alarm.m_name, sizeof(alarm.m_name), "%s", u);
            }
            snprintf(key, sizeof(key), "a%d_%s", i, HSF_KEY_ALARM_KIND);
            av = XBsonDocument_value_keyUtf8(doc, key);
            if (av) alarm.m_kind = XBsonValue_toInt32(av, 0);
            snprintf(key, sizeof(key), "a%d_%s", i, HSF_KEY_ALARM_TEXT);
            av = XBsonDocument_value_keyUtf8(doc, key);
            if (av) {
                const XString* s = XBsonValue_toString(av);
                const char* u = s ? XString_toUtf8(s) : NULL;
                if (u) snprintf(alarm.m_text, sizeof(alarm.m_text), "%s", u);
            }
            snprintf(key, sizeof(key), "a%d_%s", i, HSF_KEY_ALARM_AT);
            av = XBsonDocument_value_keyUtf8(doc, key);
            if (av) alarm.m_atMs = XBsonValue_toInt64(av, 0);
            if (s_lc.m_valveAlarmCb) s_lc.m_valveAlarmCb(s_lc.m_valveAlarmUser,
                                                         &alarm);
        }
        XClassDelete((XClass*)doc);
        return true;
    }
    /* ---- 状态帧分支 ---- */
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

const char* HsfLinkClient_lastValveCmdResult(void)
{
    return s_lc.m_lastValveCmdResult;
}

bool HsfLinkClient_valvesFresh(void)
{
    if (!s_lc.m_valves.m_received) return false;
    return (XDateTime_currentMSecsSinceEpoch() - s_lc.m_valves.m_lastMs) <=
           (uint64_t)HSF_LINK_VALVE_STALE_MS;
}

/**
 * @brief 从阀控制应答里取处置结果 v{i}_cmdResult。
 * @param data 应答载荷；len 长度。
 * @param index 路号（发送时记录）。
 * @note [为什么客户端要读它] 三分支幂等（issued/repeated/reversed/ignored）
 *       的区别只有服务端知道；若应答只回 ok=true，用户无法区分"真动了"
 *       和"本来就在那儿"，会以为按钮坏了（Python 侧同一口径）。
 */
static void hsfLcParseValveCmdResult(const uint8_t* data, uint32_t len,
                                      int index)
{
    XBsonDocument* doc;
    XBsonValue* v;
    char key[40];

    s_lc.m_lastValveCmdResult[0] = '\0';
    if (!data || len == 0 || index < 0 || index >= HSF_VALVE_COUNT) return;
    doc = XBsonDocument_create();
    if (!doc) return;
    if (!XBsonDocument_from_bytes(doc, data, len)) {
        XClassDelete((XClass*)doc);
        return;
    }
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, index,
             HSF_KEY_VALVE_CMD_RESULT);
    v = XBsonDocument_value_keyUtf8(doc, key);
    if (v) {
        const XString* s = XBsonValue_toString(v);
        const char* u8 = s ? XString_toUtf8(s) : NULL;
        if (u8) {
            snprintf(s_lc.m_lastValveCmdResult,
                     sizeof(s_lc.m_lastValveCmdResult), "%s", u8);
        }
    }
    XClassDelete((XClass*)doc);
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
    s_lc.m_lastValveIndex = index;   /* 应答的 cmdResult 按该路取。 */
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

bool HsfLinkClient_flowMeterSnapshotRequest(void)
{
    XLinkSession* session;
    uint32_t reqId = 0;

    /* [载荷不带 side 2026-11-10] 设备挂在哪条 485 上是**服务端的装配
     * 事实**：服务端按自己的装配位置回答，并在应答/推送帧里带上 side。
     * 客户端早先在请求里点名侧号（现场流量计在 B 侧 ⇒ 写死 B）⇒ 现场
     * 换接线就得改客户端；现在客户端只管要数据。 */
    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    return XLinkSession_sendRequest_ex(session, HSF_CMD_FLOWMETER_SNAPSHOT,
                                       XLinkDataType_Bson, NULL, 0, &reqId);
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

/**
 * @brief 温湿度自愈：数据不新鲜（或收到但无有效值）时向服务端补要一拍。
 * @param side 侧号（0=A 1=B）；越界返回 false。
 * @return 本次确实发出了补拍请求返回 true；数据本就新鲜/未就绪/节流中返回 false。
 * @details [为什么需要] 周期推送是「推送即丢」的**单向**通道：服务端节拍
 *          停摆、推送失步队列满被拒、或订阅登记丢失时，客户端侧收不到任何
 *          帧，而**服务端的数据其实一直有**（现场：history 行 temp=26.5
 *          恒有值，标题栏却恒显「--℃ / --%」）。只靠推送，界面只能停在
 *          「无数据」。这里用**已有的快照命令**（HSF_CMD_TEMPHUM_SNAPSHOT）
 *          做一条**请求-应答**的兜底通道：应答与推送帧同构，解析后刷新同一
 *          份 model，标题栏/首页卡片同拍恢复。
 * @note  节流：两次补拍请求至少间隔 HSF_LINK_TEMPHUM_RESYNC_MS，避免
 *        服务端无温湿度站时每秒打一次请求（应答照样回来并把该侧标成
 *        「收到但无效」，界面显示「--」是正确口径，不会空转刷屏）。
 */
bool HsfLinkClient_tempHumEnsureFresh(int side)
{
    const HsfLinkTempHum* th;
    uint64_t now;

    if (side < 0 || side >= HSF_LINK_SIDE_COUNT) return false;
    if (!HsfLinkClient_isReady()) return false;
    th = &s_lc.m_tempHumState[side];
    /* 已收到且**距预取阈值还有余量**、且有有效值 ⇒ 无需补拍。
     * [预取 2026-11-09] 判据不等到 HSF_LINK_TEMPHUM_STALE_MS 才补：那样
     * 界面会先落成「--」再等应答写回，推送停摆期间每 3.5s 闪一次「--」
     * （实测 25s 闪 7 次）。差值与理由见 HSF_LINK_TEMPHUM_PREFETCH_MS。 */
    if (th->m_received && th->m_lastMs != 0) {
        now = XDateTime_currentMSecsSinceEpoch();
        if (now < th->m_lastMs ||
            (now - th->m_lastMs) <= (uint64_t)HSF_LINK_TEMPHUM_PREFETCH_MS) {
            if (th->m_tempValid || th->m_humValid) return false;
        }
    }
    now = XDateTime_currentMSecsSinceEpoch();
    if (s_lc.m_lastTempHumReqMs != 0 &&
        now >= s_lc.m_lastTempHumReqMs &&
        (now - s_lc.m_lastTempHumReqMs) < (uint64_t)HSF_LINK_TEMPHUM_RESYNC_MS)
        return false;
    s_lc.m_lastTempHumReqMs = now;
    return HsfLinkClient_tempHumSnapshotRequest(side);
}

/* ==================== 遥测自愈补拍（快照命令兜底通道） ==================== */

/**
 * @brief 是否该向服务端再要一拍（从未收到，或剩余新鲜度不足预取阈值）。
 * @param received 收到过至少一拍。
 * @param lastMs 最近一次收到/解析成功的本地时刻（毫秒；0=从未）。
 * @param prefetchMs 预取阈值（毫秒）。
 * @return 需要补拍返回 true。
 * @details 判据用**预取阈值**而不是陈旧阈值：等到数据真的过期（界面已落成
 *          「--」）才补，会让应答回来之前先闪一次无数据（温湿度实测口径，
 *          见 HSF_LINK_TEMPHUM_PREFETCH_MS 注释）。
 */
static bool hsfLcNeedsRefetch(bool received, uint64_t lastMs, uint32_t prefetchMs)
{
    uint64_t now;
    if (!received || lastMs == 0) return true;
    now = XDateTime_currentMSecsSinceEpoch();
    if (now < lastMs) return false;              /* 时钟回退：当作刚收到。 */
    return (now - lastMs) > (uint64_t)prefetchMs;
}

/**
 * @brief 补拍节流闸：同一通道最短 HSF_LINK_SNAPSHOT_RESYNC_MS 才发一次。
 * @param ch 补拍通道。
 * @return 允许本次发送返回 true（并记下时刻）；节流中返回 false。
 */
static bool hsfLcSnapshotAllowed(HsfLcSnapshotChan ch)
{
    uint64_t now;
    if ((int)ch < 0 || ch >= HsfLcSnapshotCh_Count) return false;
    now = XDateTime_currentMSecsSinceEpoch();
    if (s_lc.m_lastSnapReqMs[ch] != 0 && now >= s_lc.m_lastSnapReqMs[ch] &&
        (now - s_lc.m_lastSnapReqMs[ch]) <
            (uint64_t)HSF_LINK_SNAPSHOT_RESYNC_MS)
        return false;
    s_lc.m_lastSnapReqMs[ch] = now;
    return true;
}

bool HsfLinkClient_aiSnapshotRequest(void)
{
    XLinkSession* session;
    uint32_t reqId = 0;
    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    return XLinkSession_sendRequest_ex(session, HSF_CMD_AI_SNAPSHOT,
                                       XLinkDataType_Bson, NULL, 0, &reqId);
}

bool HsfLinkClient_aiFresh(void)
{
    uint64_t now;
    if (!s_lc.m_aiValid || s_lc.m_aiLastMs == 0) return false;
    now = XDateTime_currentMSecsSinceEpoch();
    if (now < s_lc.m_aiLastMs) return true;      /* 时钟回退：宽容放行。 */
    return (now - s_lc.m_aiLastMs) <= (uint64_t)HSF_LINK_AI_STALE_MS;
}

bool HsfLinkClient_aiEnsureFresh(void)
{
    if (!HsfLinkClient_isReady()) return false;
    if (!hsfLcNeedsRefetch(s_lc.m_aiValid, s_lc.m_aiLastMs,
                           HSF_LINK_SNAPSHOT_PREFETCH_MS))
        return false;
    if (!hsfLcSnapshotAllowed(HsfLcSnapshotCh_Ai)) return false;
    return HsfLinkClient_aiSnapshotRequest();
}

bool HsfLinkClient_flowMeterEnsureFresh(void)
{
    const HsfLinkFlowMeter* snap;
    HsfLcSnapshotChan ch;
    int side;
    if (!HsfLinkClient_isReady()) return false;
    /* 活动侧＝服务端报告的侧（客户端不猜现场接线）。 */
    side = s_lc.m_flowMeterSide;
    if (side < 0 || side >= HSF_LINK_SIDE_COUNT) return false;
    snap = &s_lc.m_flowMeterState[side];
    if (!hsfLcNeedsRefetch(snap->m_received, snap->m_lastMs,
                           HSF_LINK_SNAPSHOT_PREFETCH_MS))
        return false;
    ch = (side == HSF_SIDE_A) ? HsfLcSnapshotCh_FlowMeterA
                              : HsfLcSnapshotCh_FlowMeterB;
    if (!hsfLcSnapshotAllowed(ch)) return false;
    return HsfLinkClient_flowMeterSnapshotRequest();
}

bool HsfLinkClient_turbidityEnsureFresh(void)
{
    if (!HsfLinkClient_isReady()) return false;
    if (!hsfLcNeedsRefetch(s_lc.m_turbidity.m_received,
                           s_lc.m_turbidity.m_lastMs,
                           HSF_LINK_SNAPSHOT_PREFETCH_MS))
        return false;
    if (!hsfLcSnapshotAllowed(HsfLcSnapshotCh_Turbidity)) return false;
    return HsfLinkClient_turbiditySnapshotRequest();
}

bool HsfLinkClient_pumpEnsureFresh(void)
{
    if (!HsfLinkClient_isReady()) return false;
    if (!hsfLcNeedsRefetch(s_lc.m_pumps.m_received, s_lc.m_pumps.m_lastMs,
                           HSF_LINK_SNAPSHOT_PREFETCH_MS))
        return false;
    if (!hsfLcSnapshotAllowed(HsfLcSnapshotCh_Pump)) return false;
    return HsfLinkClient_pumpSnapshotRequest();
}

bool HsfLinkClient_vfdEnsureFresh(void)
{
    if (!HsfLinkClient_isReady()) return false;
    if (!hsfLcNeedsRefetch(s_lc.m_vfdValid, s_lc.m_vfdLastMs,
                           HSF_LINK_SNAPSHOT_PREFETCH_MS))
        return false;
    if (!hsfLcSnapshotAllowed(HsfLcSnapshotCh_Vfd)) return false;
    return HsfLinkClient_vfdSnapshotRequest();
}

void HsfLinkClient_ensureFreshTopics(uint32_t topicMask)
{
    if (!HsfLinkClient_isReady()) return;
    /* 温湿度不在本表：它是常驻订阅，补拍入口是
     * HsfLinkClient_tempHumEnsureFresh（标题栏 1Hz 刷新里逐拍调用），
     * 此处再调一遍只会被它自己的节流挡掉，故不重复。 */
    if (topicMask & (1u << (int)HsfLinkTopic_Vfd))
        (void)HsfLinkClient_vfdEnsureFresh();
    if (topicMask & (1u << (int)HsfLinkTopic_Ai))
        (void)HsfLinkClient_aiEnsureFresh();
    if (topicMask & (1u << (int)HsfLinkTopic_FlowMeter)) {
        /* 流量计只有一台：补拍走活动侧（服务端报告的侧），客户端不点名。
         * 早先这里是「A、B 各补一次」（按现场接线写死两侧），现在侧号由
         * 服务端说了算 ⇒ 现场把流量计换到另一条 485，客户端零改动。 */
        (void)HsfLinkClient_flowMeterEnsureFresh();
    }
    if (topicMask & (1u << (int)HsfLinkTopic_Turbidity))
        (void)HsfLinkClient_turbidityEnsureFresh();
    if (topicMask & (1u << (int)HsfLinkTopic_Pump))
        (void)HsfLinkClient_pumpEnsureFresh();
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

bool HsfLinkClient_vfdJogRelease(void)
{
    /* [点动释放] 专用 op=101（HSF_OP_JOG_RELEASE）：松手即停，不等看门狗
     * 超时（600ms）。旧实现发普通 Stop——也能停，但语义不清（服务端会把
     * 它当"减速停机"命令），且与 Python 客户端的 {"op":101} 不一致。 */
    XVfdControlRequest req;
    memset(&req, 0, sizeof(req));
    req.m_op = (XVfdOperation)HSF_OP_JOG_RELEASE;
    return HsfLinkClient_vfdControl(&req);
}

void HsfLinkClient_suppressNextCommandError(void)
{
    s_lc.m_suppressNextErr = true;
}

/* ==================== 浊度仪 / 泵 ==================== */

const HsfLinkTurbiditySet* HsfLinkClient_turbidity(void)
{
    return &s_lc.m_turbidity;
}

bool HsfLinkClient_turbidityFresh(void)
{
    if (!s_lc.m_turbidity.m_received) return false;
    return (XDateTime_currentMSecsSinceEpoch() - s_lc.m_turbidity.m_lastMs) <=
           HSF_LINK_TURBIDITY_STALE_MS;
}

bool HsfLinkClient_turbiditySnapshotRequest(void)
{
    XLinkSession* session;
    uint32_t reqId = 0;
    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    return XLinkSession_sendRequest_ex(session, HSF_CMD_TURBIDITY_SNAPSHOT,
                                      XLinkDataType_Bson, NULL, 0, &reqId);
}

void HsfLinkClient_setTurbidityCallback(HsfLinkNotifyFn cb, void* userData)
{
    if (cb) hsfLcAddSub(s_lc.m_turbCb, cb, userData);
}

const HsfLinkPumps* HsfLinkClient_pumps(void)
{
    return &s_lc.m_pumps;
}

bool HsfLinkClient_pumpsFresh(void)
{
    if (!s_lc.m_pumps.m_received) return false;
    return (XDateTime_currentMSecsSinceEpoch() - s_lc.m_pumps.m_lastMs) <=
           HSF_LINK_PUMP_STALE_MS;
}

bool HsfLinkClient_pumpSnapshotRequest(void)
{
    XLinkSession* session;
    uint32_t reqId = 0;
    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    return XLinkSession_sendRequest_ex(session, HSF_CMD_PUMP_SNAPSHOT,
                                      XLinkDataType_Bson, NULL, 0, &reqId);
}

bool HsfLinkClient_pumpControl(int index, bool on)
{
    XLinkSession* session;
    XBsonDocument* doc;
    XByteArray* bytes;
    uint32_t reqId = 0;
    bool ok;
    const bool trace = XSystem_environment("XHSF_PUMP_TRACE") != NULL;

    /* [泵点击诊断] 三个前置闸都会静默 return false，现场「点了没反应」时
     * 看不到任何痕迹。开关式日志各留一行即可区分「链路未就绪」「索引
     * 越界」「会话为空」。 */
    if (trace)
        XPrintf("[pump] control(index=%d on=%d) ready=%d\n", index,
                on ? 1 : 0, HsfLinkClient_isReady() ? 1 : 0);
    if (!HsfLinkClient_isReady()) return false;
    if (index < 0 || index >= HSF_PUMP_COUNT) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_PUMP_INDEX, (int32_t)index);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_PUMP_ON, on);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    if (!bytes) return false;
    ok = XLinkSession_sendRequest_ex(session, HSF_CMD_PUMP_CONTROL,
                                     XLinkDataType_Bson,
                                     (const uint8_t*)XByteArray_constData(bytes),
                                     (uint32_t)XByteArray_size_base(bytes),
                                     &reqId);
    XClassDelete((XClass*)bytes);
    return ok;
}

void HsfLinkClient_setPumpCallback(HsfLinkNotifyFn cb, void* userData)
{
    if (cb) hsfLcAddSub(s_lc.m_pumpCb, cb, userData);
}

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

/** @brief 历史分页实际发包（时间窗/关键字均下推服务端）。 */
static bool hsfLcHistoryQuery(int offset, int limit, bool exportMode,
                              const char* kw,
                              const uint32_t* kwCols, int kwColCount,
                              int64_t loTs, int64_t hiTs)
{
    XLinkSession* session;
    XBsonDocument* doc;
    XByteArray* bytes;
    uint32_t reqId = 0;
    bool ok = false;

    if (!HsfLinkClient_isReady()) return false;
    if (offset < 0) offset = 0;
    if (limit <= 0) limit = 20;
    if (limit > HSF_HISTORY_PAGE_MAX) limit = HSF_HISTORY_PAGE_MAX;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;

    /* [窗口锚点 beforeTs] 首屏把自己的"现在"发上去，翻页原样回传——整段分页
     * 钉在同一批数据上；否则库在持续入库，翻到第 2 页时新行已顶进来，
     * offset 整体偏移→相邻两页出现重复行。
     * [export 标记] 导出意图由服务端验 Download 权限（界面灰按钮拦不住
     * 直接发帧的人）；普通浏览不带该键。
     * [kw/kwCols] 关键字下推服务端（与 Python 同键名），保证"总数"与
     * "页内容"共用同一条 WHERE。 */
    doc = XBsonDocument_create();
    if (!doc) return false;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_HIST_OFFSET, offset);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_HIST_LIMIT, limit);
    XBsonDocument_insert_keyUtf8_int64(doc, HSF_KEY_HIST_BEFORE_TS,
                                      XDateTime_currentSecsSinceEpoch() * 1000LL);
    /* [时间窗] 客户端选定的起止时刻下推服务端（HIST_KEY_LO_TS/HI_TS）；
     * 未设的一端不发键（服务端按"库内最早/现在"处理）。 */
    if (loTs > 0)
        XBsonDocument_insert_keyUtf8_int64(doc, HSF_KEY_HIST_LO_TS, loTs);
    if (hiTs > 0)
        XBsonDocument_insert_keyUtf8_int64(doc, HSF_KEY_HIST_HI_TS, hiTs);
    if (exportMode)
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_HIST_EXPORT, true);
    if (kw && kw[0]) {
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_HIST_KW, kw);
        if (kwCols && kwColCount > 0) {
            XBsonArray* arr = XBsonArray_create();
            int i;
            if (arr) {
                for (i = 0; i < kwColCount; ++i) {
                    XBsonValue* v = XBsonValue_create_int32((int32_t)kwCols[i]);
                    if (v) {
                        /* [move 后释放源壳 2026-11-08] append_move 只转内容，
                         * XBsonValue 堆壳归调用方（框架 XBsonArray.c 同款收尾；
                         * 公开测试 XJsonBsonCoverageTest 也是 move 后 delete）。
                         * 原先不释放 ⇒ 每次历史查询漏 N 个 32 字节壳，界面
                         * 历史页轮询时随运行时长累积。 */
                        XBsonArray_append_move_base(arr, v);
                        XBsonValue_delete(v);
                    }
                }
                XBsonDocument_insert_keyUtf8_array_move(doc, HSF_KEY_HIST_KW_COLS,
                                                        arr);
                /* 同口径：insert_keyUtf8_array_move 只转移内容，源数组壳由
                 * 调用方释放（否则每次带关键字查询漏一个 XBsonArray）。 */
                XClassDelete((XClass*)arr);
            }
        }
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    if (!bytes) return false;
    ok = XLinkSession_sendRequest_ex(session, HSF_CMD_HISTORY_PAGE,
                                    XLinkDataType_Bson,
                                    (const uint8_t*)XByteArray_constData(bytes),
                                    (uint32_t)XByteArray_size_base(bytes),
                                    &reqId);
    XClassDelete((XClass*)bytes);
    return ok;
}

bool HsfLinkClient_historyQuery(int offset, int limit, bool exportMode,
                                const char* kw,
                                const uint32_t* kwCols, int kwColCount)
{
    return hsfLcHistoryQuery(offset, limit, exportMode, kw, kwCols, kwColCount,
                             0, 0);
}

bool HsfLinkClient_historyQueryWindow(int offset, int limit, bool exportMode,
                                      const char* kw,
                                      const uint32_t* kwCols, int kwColCount,
                                      int64_t loTs, int64_t hiTs)
{
    return hsfLcHistoryQuery(offset, limit, exportMode, kw, kwCols, kwColCount,
                             loTs, hiTs);
}

bool HsfLinkClient_historyPageEx(int offset, int limit, bool exportMode)
{
    return HsfLinkClient_historyQuery(offset, limit, exportMode, NULL, NULL, 0);
}

bool HsfLinkClient_historyPage(int offset, int limit)
{
    return HsfLinkClient_historyPageEx(offset, limit, false);
}

const HsfHistoryPage* HsfLinkClient_history(void)
{
    return &s_lc.m_history;
}

/* ==================== 运行日志分页（0x0119） ==================== */

/**
 * @brief 解析 0x0119 应答到日志页结果（行结构 {ts,category,text}）。
 * @details [m_seq 跨解析保留] 与 HsfHistoryPage_parse 同款：整结构清零
 *          重填，序号单独拎出来递增（页面按"序号变过"才处置）。
 * @return 解析到有效应答返回 true（行数可为 0——空窗口也是有效应答）。
 */
static bool hsfLcParseLogPage(const uint8_t* data, uint32_t len)
{
    XBsonDocument* doc;
    XBsonValue* v;
    const XBsonArray* rows;
    int i;
    uint32_t seq = s_lc.m_logPage.m_seq;

    memset(&s_lc.m_logPage, 0, sizeof(s_lc.m_logPage));
    s_lc.m_logPage.m_seq = seq;
    if (!data || len == 0) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (!XBsonDocument_from_bytes(doc, data, len)) {
        XClassDelete((XClass*)doc);
        return false;
    }
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_OK);
    /* [ok 键必查] 未开库/解析失败等否定应答也是应答：置 valid 让页面
     * 别空等（页面按 m_err 文本提示）。 */
    s_lc.m_logPage.m_ok = v ? XBsonValue_toBool(v, false) : false;
    if (!s_lc.m_logPage.m_ok) {
        const XBsonValue* ev = XBsonDocument_value_keyUtf8(doc, HSF_KEY_ERR);
        if (ev) {
            const XString* s = XBsonValue_toString(ev);
            const char* u8 = s ? XString_toUtf8(s) : NULL;
            if (u8) snprintf(s_lc.m_logPage.m_err, sizeof(s_lc.m_logPage.m_err),
                             "%s", u8);
        }
        s_lc.m_logPage.m_valid = true;
        ++s_lc.m_logPage.m_seq;
        XClassDelete((XClass*)doc);
        return true;
    }
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_LOG_TOTAL);
    if (v) s_lc.m_logPage.m_total = (int)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_LOG_OFFSET);
    if (v) s_lc.m_logPage.m_offset = (int)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_LOG_LIMIT);
    if (v) s_lc.m_logPage.m_limit = (int)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_LOG_ROWS);
    rows = v ? XBsonValue_toArray(v) : NULL;
    if (rows) {
        const int64_t n = XBsonArray_count_base(rows);
        for (i = 0; i < (int)n && i < HSF_LOG_PAGE_MAX; ++i) {
            const XBsonValue* rowVal = XBsonArray_at_base(rows, (int64_t)i);
            const XBsonDocument* row = rowVal ? XBsonValue_toDocument(rowVal) : NULL;
            HsfLinkLogRow* dst = &s_lc.m_logPage.m_rows[s_lc.m_logPage.m_count];
            XBsonValue* cell;
            if (!row) continue;
            cell = XBsonDocument_value_keyUtf8(row, HSF_KEY_LOG_TS);
            if (cell) dst->m_ts = XBsonValue_toInt64(cell, 0);
            cell = XBsonDocument_value_keyUtf8(row, HSF_KEY_LOG_CATEGORY);
            if (cell) {
                const XString* s = XBsonValue_toString(cell);
                const char* u8 = s ? XString_toUtf8(s) : NULL;
                if (u8) snprintf(dst->m_category, sizeof(dst->m_category), "%s", u8);
            }
            cell = XBsonDocument_value_keyUtf8(row, HSF_KEY_LOG_TEXT);
            if (cell) {
                const XString* s = XBsonValue_toString(cell);
                const char* u8 = s ? XString_toUtf8(s) : NULL;
                if (u8) snprintf(dst->m_text, sizeof(dst->m_text), "%s", u8);
            }
            ++s_lc.m_logPage.m_count;
        }
    }
    s_lc.m_logPage.m_valid = true;
    ++s_lc.m_logPage.m_seq;
    XClassDelete((XClass*)doc);
    return true;
}

bool HsfLinkClient_logQueryWindow(int offset, int limit, int64_t beforeTs,
                                  int64_t loTs, int64_t hiTs,
                                  const char* category, const char* kw)
{
    XLinkSession* session;
    XBsonDocument* doc;
    XByteArray* bytes;
    uint32_t reqId = 0;
    bool ok = false;

    if (!HsfLinkClient_isReady()) return false;
    if (offset < 0) offset = 0;
    if (limit <= 0) limit = HSF_LOG_PAGE_DEFAULT;
    if (limit > HSF_LOG_PAGE_MAX) limit = HSF_LOG_PAGE_MAX;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;

    /* [窗口锚点 beforeTs] 与 0x0118 同理：首屏把自己的"现在"发上去，
     * 翻页原样回传——logs 表同样在持续入库，不带锚点翻页会被新行顶偏。 */
    doc = XBsonDocument_create();
    if (!doc) return false;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_LOG_OFFSET, offset);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_LOG_LIMIT, limit);
    XBsonDocument_insert_keyUtf8_int64(doc, HSF_KEY_LOG_BEFORE_TS,
                                       beforeTs > 0
                                           ? beforeTs
                                           : XDateTime_currentSecsSinceEpoch() * 1000LL);
    if (loTs > 0)
        XBsonDocument_insert_keyUtf8_int64(doc, HSF_KEY_LOG_LO_TS, loTs);
    if (hiTs > 0)
        XBsonDocument_insert_keyUtf8_int64(doc, HSF_KEY_LOG_HI_TS, hiTs);
    if (category && category[0])
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_LOG_CATEGORY, category);
    if (kw && kw[0])
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_LOG_KW, kw);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    if (!bytes) return false;
    ok = XLinkSession_sendRequest_ex(session, HSF_CMD_LOG_PAGE,
                                     XLinkDataType_Bson,
                                     (const uint8_t*)XByteArray_constData(bytes),
                                     (uint32_t)XByteArray_size_base(bytes),
                                     &reqId);
    XClassDelete((XClass*)bytes);
    return ok;
}

const HsfLinkLogPage* HsfLinkClient_logPage(void)
{
    return &s_lc.m_logPage;
}

/* ==================== 远端配置（0x0116 / 0x0117）+ 自动运行（0x0115） ==================== */

bool HsfLinkClient_configGet(void)
{
    XLinkSession* session;
    uint32_t reqId = 0;
    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    return XLinkSession_sendRequest_ex(session, HSF_CMD_CONFIG_GET,
                                       XLinkDataType_Bson, NULL, 0, &reqId);
}

bool HsfLinkClient_configSetStorage(bool enabled, int sampleMs)
{
    /* 兼容旧入口：只改开关/周期，不碰路径/上限/策略（-1/NULL=不发该键）。 */
    return HsfLinkClient_configSetStorageEx(enabled, NULL, sampleMs, -1, NULL, -1);
}

bool HsfLinkClient_configSetStorageEx(bool enabled, const char* path,
                                      int sampleMs, int maxMb,
                                      const char* policy, int keepFiles)
{
    XLinkSession* session;
    XBsonDocument* doc;
    XByteArray* bytes;
    uint32_t reqId = 0;
    bool ok = false;

    if (!HsfLinkClient_isReady()) return false;
    if (sampleMs < 1000) sampleMs = 1000;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    XBsonDocument_insert_keyUtf8_utf8(doc, HSF_CFG_SECTION_KEY,
                                      HSF_CFG_SECTION_STORAGE);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_CFGK_STORAGE_ENABLED, enabled);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_STORAGE_SAMPLE_MS,
                                       sampleMs);
    if (path)
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_CFGK_STORAGE_PATH, path);
    if (maxMb >= 0)
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_STORAGE_MAX_MB, maxMb);
    if (policy && policy[0])
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_CFGK_STORAGE_FULL_POLICY,
                                          policy);
    if (keepFiles >= 0)
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_STORAGE_KEEP_FILES,
                                           keepFiles);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    if (!bytes) return false;
    ok = XLinkSession_sendRequest_ex(session, HSF_CMD_CONFIG_SET,
                                    XLinkDataType_Bson,
                                    (const uint8_t*)XByteArray_constData(bytes),
                                    (uint32_t)XByteArray_size_base(bytes),
                                    &reqId);
    XClassDelete((XClass*)bytes);
    return ok;
}

bool HsfLinkClient_configSetCollect(const HsfConfigCollect* collect)
{
    XLinkSession* session;
    XBsonDocument* doc;
    XByteArray* bytes;
    uint32_t reqId = 0;
    bool ok = false;

    if (!HsfLinkClient_isReady() || !collect) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    XBsonDocument_insert_keyUtf8_utf8(doc, HSF_CFG_SECTION_KEY,
                                      HSF_CFG_SECTION_COLLECT);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_CFGK_VFD_ENABLED,
                                      collect->m_vfdEnabled);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_CFGK_TEMPHUM_ENABLED,
                                      collect->m_temphumEnabled);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_CFGK_FLOWMETER_ENABLED,
                                      collect->m_flowmeterEnabled);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_CFGK_TURBIDITY_ENABLED,
                                      collect->m_turbidityEnabled);
    /* [逐台开关 2026-11-10] 两台浊度是独立设备，各发各的；旧服务端不认
     * 这两个键时会回落共同键 ⇒ 向后兼容（两份 proto 同名常量）。 */
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_CFGK_TURBIDITY1_ENABLED,
                                      collect->m_turbidity1Enabled);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_CFGK_TURBIDITY2_ENABLED,
                                      collect->m_turbidity2Enabled);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_CFGK_ANALOG_ENABLED,
                                      collect->m_analogEnabled);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_CFGK_DIGITAL_ENABLED,
                                      collect->m_digitalEnabled);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_VFD_POLL_MS,
                                        collect->m_vfdPollMs);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_TEMPHUM_POLL_MS,
                                        collect->m_temphumPollMs);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_TURBIDITY_POLL_MS,
                                        collect->m_turbidityPollMs);
    /* [逐台 2026-11-10] 两台浊度 + 流量计各自独立下发。 */
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_TURBIDITY1_POLL_MS,
                                        collect->m_turbidity1PollMs);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_TURBIDITY2_POLL_MS,
                                        collect->m_turbidity2PollMs);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_FLOWMETER_POLL_MS,
                                        collect->m_flowmeterPollMs);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_ANALOG_POLL_MS,
                                        collect->m_analogPollMs);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_DIGITAL_POLL_MS,
                                        collect->m_digitalPollMs);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    if (!bytes) return false;
    ok = XLinkSession_sendRequest_ex(session, HSF_CMD_CONFIG_SET,
                                    XLinkDataType_Bson,
                                    (const uint8_t*)XByteArray_constData(bytes),
                                    (uint32_t)XByteArray_size_base(bytes),
                                    &reqId);
    XClassDelete((XClass*)bytes);
    return ok;
}

bool HsfLinkClient_configSetLogin(const HsfConfigLogin* login)
{
    XLinkSession* session;
    XBsonDocument* doc;
    XByteArray* bytes;
    uint32_t reqId = 0;
    bool ok = false;

    if (!HsfLinkClient_isReady() || !login) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    XBsonDocument_insert_keyUtf8_utf8(doc, HSF_CFG_SECTION_KEY,
                                      HSF_CFG_SECTION_LOGIN);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_CFGK_LOGIN_ALLOW_ANON,
                                      login->m_allowAnonymous);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_CFGK_LOGIN_ALLOW_CONCUR,
                                      login->m_allowConcurrent);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_LOGIN_MAX_SESSIONS,
                                       login->m_maxSessions);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    if (!bytes) return false;
    ok = XLinkSession_sendRequest_ex(session, HSF_CMD_CONFIG_SET,
                                    XLinkDataType_Bson,
                                    (const uint8_t*)XByteArray_constData(bytes),
                                    (uint32_t)XByteArray_size_base(bytes),
                                    &reqId);
    XClassDelete((XClass*)bytes);
    return ok;
}

bool HsfLinkClient_configOk(void)
{
    return s_lc.m_cfgOk;
}

const char* HsfLinkClient_configError(void)
{
    return s_lc.m_cfgErr;
}

int HsfLinkClient_configSeq(void)
{
    return s_lc.m_cfgSeq;
}

int HsfLinkClient_configGetSeq(void)
{
    return s_lc.m_cfgGetSeq;
}

const HsfConfig* HsfLinkClient_config(void)
{
    return &s_lc.m_config;
}

const char* HsfLinkClient_serverAddress(void)
{
    return s_lc.m_serverAddr;
}

const char* HsfLinkClient_roleName(void)
{
    /* 四档中文名与 XPermPolicy_roleName 同口径；角色值不单调，必须逐个
     * 枚举（旧实现把 1 写成"管理员"、2 写成"运行员"，与 XAccountRole
     * 正好相反——root（2）显示成运行员，现场据此误判权限）。 */
    switch (HsfLinkClient_role()) {
    case (int)XLinkAuthRole_Admin:    return "管理员";
    case (int)XLinkAuthRole_Operator: return "操作者";
    case (int)XLinkAuthRole_User:     return "普通用户";
    default:                          return "匿名";
    }
}

bool HsfLinkClient_autoControl(int op)
{
    XLinkSession* session;
    XBsonDocument* doc;
    XByteArray* bytes;
    uint32_t reqId = 0;
    bool ok = false;

    if (!HsfLinkClient_isReady()) return false;
    session = XLinkClient_session(s_lc.m_client);
    if (!session) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    XBsonDocument_insert_keyUtf8_int32(doc, "op", op);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    if (!bytes) return false;
    ok = XLinkSession_sendRequest_ex(session, HSF_CMD_AUTO_CONTROL,
                                    XLinkDataType_Bson,
                                    (const uint8_t*)XByteArray_constData(bytes),
                                    (uint32_t)XByteArray_size_base(bytes),
                                    &reqId);
    XClassDelete((XClass*)bytes);
    return ok;
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

bool HsfLinkClient_autoRunning(int* outRunning)
{
    if (!s_lc.m_autoRunningKnown) {
        if (outRunning) *outRunning = -1;
        return false;
    }
    if (outRunning) *outRunning = s_lc.m_autoRunning;
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

const char* HsfLinkClient_runModeError(void)
{
    /* 同一缓冲，零拷贝借用：成功应答已在 hsfLcOnFrame（RUN_MODE 分支）
     * 解析前清空，故返回空串即「没有失败原因」。 */
    return HsfLinkClient_lastModeError();
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
