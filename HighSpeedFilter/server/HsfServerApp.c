
/**
 * @file       HsfServerApp.c
 * @brief      服务端 XLink 接入与业务路由实现。
 * @details    [鉴权] XLink 内建挑战应答（口令明文不过网不落盘）；账户库
 *             JSON 落数据目录（对齐客户端 cfg/ 口径），首次启动无账户时
 *             自动创建 admin/admin（只读角色）与 root/root（管理员角色）。
 *             [门禁] 控制类命令在分发回调里以 XLinkServer_sessionRole
 *             强制：仅 XLinkAuthRole_Admin（root）可写，其余角色得到
 *             {"ok":false,"err":"权限不足"}。
 *             [遥测] 服务端唯一 tick 槽按 HSF_TELEMETRY_MS 周期推送三个
 *             主题（变频器/模拟量/温湿度），变频器与温湿度按侧各推一份；
 *             无人订阅时 publish 静默为 0，不视为错误。
 *             [两侧] 请求载荷带 "side" 键（0=A 1=B，缺省 A），响应与推送
 *             都回带 side，客户端可区分两路 485 的数据。
 */
#include "HsfServerApp.h"

#include "HsfServerIo.h"
#include "HsfServerSettings.h"
#include "HsfServerConfig.h"
#include "XhsfLinkProto.h"

#include "XLinkServer.h"
#include "XLinkSession.h"
#include "XLinkProto.h"
#include "XLinkUserStore.h"
#include "XModbusDataUnit.h"
#include "XModbusHub.h"
#include "XVfdCodec.h"
#include "XVfdDevice.h"
#include "XTempHumTransmitter.h"
#include "XTempHumTransmitterCodec.h"
#include "XBson.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XIODevice.h"   /* XIODevice_fd（会话接入日志打印底层 fd）。 */
#include "XAbstractSocket.h"    /* XAbstractSocket_state（连接态诊断）。 */
#include "XString.h"
#include "XMemory.h"
#include "XDateTime.h"
#include "XPrintf.h"
#include <stdio.h>
#include <string.h>

/* ==================== 模块状态 ==================== */

static XLinkServer* s_srv = NULL;       /**< XLink 服务端实例。 */

/**
 * @brief 当前运行模式（手动/自动）。
 * @details 自动模式下拒绝全部 XLink 控制命令（遥测/快照不受影响）。
 *          持久化在 HsfServerSettings（键 "run/mode"），重启保持。
 *          默认手动：老版本配置文件无该键时行为不变。
 */
static HsfRunMode s_runMode = HsfRunMode_Manual;

/* ==================== 设备健康（事件驱动推送） ==================== */

/** @brief 健康条目上限（A/B 两侧 × 4 类设备 + 总线）。 */
#define HSF_HEALTH_MAX 16

/**
 * @brief 单条设备健康快照。
 * @details 值语义，可直接逐字节比对判断"是否变化"（无指针成员）。
 */
typedef struct HsfAppHealthEntry {
    char     m_name[32];    /**< 设备显示名（UTF-8）。 */
    int      m_kind;        /**< HsfDeviceKind。 */
    int      m_side;        /**< HsfSide。 */
    int      m_health;      /**< HsfDeviceHealth。 */
    int      m_error;       /**< 错误码/故障码；0=无。 */
    bool     m_online;      /**< 通讯在线。 */
    int      m_runState;    /**< 运行态（XVfdRunState；非设备类为 0）。 */
    char     m_text[48];    /**< 错误说明（可空）。 */
} HsfAppHealthEntry;

/**
 * @brief 健康汇总状态（模块单例）。
 */
typedef struct HsfAppHealthState {
    HsfAppHealthEntry m_entries[HSF_HEALTH_MAX]; /**< 当前快照。 */
    int  m_count;        /**< 当前条目数。 */
    int  m_lastCount;    /**< 上次推送时的条目数（比对用）。 */
    HsfAppHealthEntry m_last[HSF_HEALTH_MAX];    /**< 上次推送的快照。 */
    int  m_seq;          /**< 变化序号（单调递增）。 */
    bool m_primed;       /**< 是否已建立基线（首拍不推，只建基线）。 */
} HsfAppHealthState;

static HsfAppHealthState s_health;

/* ==================== 点动看门狗（手持心跳） ==================== */

/**
 * @brief 单侧点动看门狗状态。
 * @details m_active 表示"服务端认为设备正在点动"；m_lastMs 为该状态下最近
 *          一次收到 Jog 帧的时刻（单调毫秒）。tick 里超时即自动停机。
 */
typedef struct HsfAppJogWatch {
    bool     m_active;   /**< 是否处于点动保持态。 */
    bool     m_reverse;  /**< 点动方向。 */
    uint64_t m_lastMs;   /**< 最近一次收到点动帧的时刻。 */
} HsfAppJogWatch;

static HsfAppJogWatch s_jogWatch[HsfSide_Count];

/* 前置声明（控制处理里用到，实现在 tick 段前）。 */
static void hsfAppJogTouch(HsfSide side, bool reverse);
static void hsfAppJogRelease(HsfSide side);
static void hsfAppPublishTopicNow(XLinkServer* srv, XLinkSession* session,
                                  uint32_t topic);

/* ==================== 设备健康：采集 / 比对 / 推送 ==================== */

/** @brief 安全字符串拷贝（定长，恒 0 结尾）。 */
static void hsfHealthCopy(char* dst, size_t cap, const char* src)
{
    size_t n;
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    n = strlen(src);
    if (n >= cap) n = cap - 1;
    XMemcpy(dst, src, n);
    dst[n] = '\0';
}

/**
 * @brief 追加一条设备健康条目。
 */
static void hsfHealthAdd(HsfAppHealthState* st, const char* name, int kind,
                         int side, int health, int error, bool online,
                         int runState, const char* text)
{
    HsfAppHealthEntry* e;
    if (!st || st->m_count >= HSF_HEALTH_MAX) return;
    e = &st->m_entries[st->m_count++];
    XMemset(e, 0, sizeof(*e));
    hsfHealthCopy(e->m_name, sizeof(e->m_name), name);
    e->m_kind = kind;
    e->m_side = side;
    e->m_health = health;
    e->m_error = error;
    e->m_online = online;
    e->m_runState = runState;
    hsfHealthCopy(e->m_text, sizeof(e->m_text), text);
}

/**
 * @brief 采集全量设备健康快照（只读缓存/标志，不发起 IO）。
 */
static void hsfHealthCollect(HsfAppHealthState* st)
{
    HsfSide side;
    if (!st) return;
    st->m_count = 0;
    for (side = HsfSide_A; side < HsfSide_Count; ++side) {
        const char* sideName = (side == HsfSide_A) ? "A" : "B";
        XVfdDevice* vfd;
        const HsfServerIoTempHum* th;
        const HsfServerIoFlowMeter* fm;
        char name[32];

        if (!HsfServerIo_sideReady(side)) {
            snprintf(name, sizeof(name), "485-%s 总线", sideName);
            hsfHealthAdd(st, name, HsfDeviceKind_Bus, (int)side,
                         HsfDeviceHealth_Offline, 0, false, 0, "总线未装配");
            continue;
        }

        /* ---- 485 总线本身 ---- */
        {
            bool online = HsfServerIo_busOnline(side);
            snprintf(name, sizeof(name), "485-%s 总线", sideName);
            hsfHealthAdd(st, name, HsfDeviceKind_Bus, (int)side,
                         online ? HsfDeviceHealth_Ok : HsfDeviceHealth_Offline,
                         0, online, 0, online ? "" : "串口断开");
        }

        /* ---- 变频器 ---- */
        vfd = HsfServerIo_vfdDevice(side);
        if (vfd) {
            const XVfdStatus* vs = XVfdDevice_status_const(vfd);
            bool online = HsfServerIo_vfdOnline(side);
            int health = HsfDeviceHealth_Offline;
            int err = 0;
            const char* text = "";
            if (online && vs && vs->m_valid) {
                if (vs->m_faulted) {
                    health = HsfDeviceHealth_Fault;
                    err = (int)vs->m_faultCode;
                    text = HsfServerIo_vfdFaultText(vs->m_faultCode);
                    if (!text) text = "变频器故障";
                } else {
                    health = HsfDeviceHealth_Ok;
                }
            } else if (online) {
                health = HsfDeviceHealth_Stale;
                text = "无有效遥测";
            } else {
                text = "从站失联";
            }
            snprintf(name, sizeof(name), "变频器-%s", sideName);
            hsfHealthAdd(st, name, HsfDeviceKind_Vfd, (int)side, health,
                         err, online,
                         (online && vs) ? (int)vs->m_runState : 0, text);
        }

        /* ---- 温湿度 ---- */
        th = HsfServerIo_tempHum(side);
        if (th) {
            int32_t t = 0, h = 0;
            bool tv = false, hv = false;
            (void)HsfServerIoTempHum_values(th, &t, &h, &tv, &hv);
            snprintf(name, sizeof(name), "温湿度-%s", sideName);
            hsfHealthAdd(st, name, HsfDeviceKind_TempHum, (int)side,
                         tv || hv ? HsfDeviceHealth_Ok : HsfDeviceHealth_Stale,
                         0, tv || hv, 0, tv || hv ? "" : "无有效数据");
        }

        /* ---- 流量计 ---- */
        fm = HsfServerIo_flowMeter(side);
        if (fm) {
            double flow = 0, speed = 0, fwd = 0, rev = 0;
            int fu = 0, tu = 0, alarms = 0;
            bool valid = false;
            (void)HsfServerIoFlowMeter_values(fm, &flow, &speed, &fwd, &rev,
                                              &fu, &tu, &alarms, &valid);
            snprintf(name, sizeof(name), "流量计-%s", sideName);
            hsfHealthAdd(st, name, HsfDeviceKind_FlowMeter, (int)side,
                         alarms != 0 ? HsfDeviceHealth_Fault
                                     : (valid ? HsfDeviceHealth_Ok
                                              : HsfDeviceHealth_Stale),
                         alarms, valid || alarms != 0, 0,
                         alarms != 0 ? "流量计告警" : (valid ? "" : "无有效数据"));
        }
    }

    /* ---- 模拟量模块（TCP，单台） ---- */
    {
        double ma[8];
        bool valid[8];
        int n = HsfServerIo_aiValues(ma, valid, 8);
        if (n > 0) {
            bool any = false;
            int i;
            for (i = 0; i < n; ++i)
                if (valid[i]) { any = true; break; }
            hsfHealthAdd(st, "模拟量模块", HsfDeviceKind_AnalogIo, (int)HsfSide_A,
                         any ? HsfDeviceHealth_Ok : HsfDeviceHealth_Stale,
                         0, any, 0, any ? "" : "无有效数据");
        }
    }
}

/**
 * @brief 两快照是否一致（条目数 + 逐项内存比对）。
 * @return 一致返回 true。
 */
static bool hsfHealthSame(const HsfAppHealthState* a, const HsfAppHealthState* b)
{
    int i;
    if (a->m_count != b->m_count) return false;
    for (i = 0; i < a->m_count; ++i) {
        if (XMemcmp(&a->m_entries[i], &b->m_entries[i],
                    sizeof(HsfAppHealthEntry)) != 0)
            return false;
    }
    return true;
}

/**
 * @brief 当前健康快照 → BSON 载荷。
 * @param st 快照。
 * @return 新建 XByteArray；失败 NULL。@warning 调用方 XClassDelete。
 */
static XByteArray* hsfHealthToBson(const HsfAppHealthState* st)
{
    XBsonDocument* doc;
    XByteArray* bytes;
    int i;
    if (!st) return NULL;
    doc = XBsonDocument_create();
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_DEV_COUNT, st->m_count);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_DEV_SEQ, st->m_seq);
    XBsonDocument_insert_keyUtf8_int64(doc, HSF_KEY_DEV_TS,
                                       (int64_t)XDateTime_currentMSecsSinceEpoch());
    for (i = 0; i < st->m_count; ++i) {
        const HsfAppHealthEntry* e = &st->m_entries[i];
        char key[24];
        snprintf(key, sizeof(key), "d%d_name", i);
        XBsonDocument_insert_keyUtf8_utf8(doc, key, e->m_name);
        snprintf(key, sizeof(key), "d%d_kind", i);
        XBsonDocument_insert_keyUtf8_int32(doc, key, e->m_kind);
        snprintf(key, sizeof(key), "d%d_side", i);
        XBsonDocument_insert_keyUtf8_int32(doc, key, e->m_side);
        snprintf(key, sizeof(key), "d%d_health", i);
        XBsonDocument_insert_keyUtf8_int32(doc, key, e->m_health);
        snprintf(key, sizeof(key), "d%d_error", i);
        XBsonDocument_insert_keyUtf8_int32(doc, key, e->m_error);
        snprintf(key, sizeof(key), "d%d_online", i);
        XBsonDocument_insert_keyUtf8_bool(doc, key, e->m_online);
        snprintf(key, sizeof(key), "d%d_runState", i);
        XBsonDocument_insert_keyUtf8_int32(doc, key, e->m_runState);
        if (e->m_text[0]) {
            snprintf(key, sizeof(key), "d%d_text", i);
            XBsonDocument_insert_keyUtf8_utf8(doc, key, e->m_text);
        }
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief 推送当前快照（无人订阅时静默为 0，不算错误）。
 */
static void hsfHealthPublish(XLinkServer* self)
{
    XByteArray* bytes = hsfHealthToBson(&s_health);
    if (!bytes || !self) {
        if (bytes) XClassDelete((XClass*)bytes);
        return;
    }
    XLinkServer_publish_ex(self, HSF_TOPIC_DEVICE_HEALTH, XLinkDataType_Bson,
                           (const uint8_t*)XByteArray_constData(bytes),
                           (uint32_t)XByteArray_size_base(bytes));
    XClassDelete((XClass*)bytes);
}

/**
 * @brief 健康巡检拍：采集 → 比对 → 变化才推（首拍只建基线）。
 * @note  事件驱动：状态不变**不推**，避免无意义流量。
 */
static void hsfHealthTick(XLinkServer* self)
{
    hsfHealthCollect(&s_health);
    if (!s_health.m_primed) {
        /* 首拍：只建基线（客户端订阅时会补发，见 hsfAppPublishHealthNow）。 */
        XMemcpy(s_health.m_last, s_health.m_entries, sizeof(s_health.m_last));
        s_health.m_lastCount = s_health.m_count;
        s_health.m_primed = true;
        return;
    }
    {
        /* 与上次快照比对（只在栈上拼出上次视图，不复制整表）。 */
        HsfAppHealthState prev;
        XMemset(&prev, 0, sizeof(prev));
        XMemcpy(prev.m_entries, s_health.m_last, sizeof(prev.m_entries));
        prev.m_count = s_health.m_lastCount;
        if (hsfHealthSame(&s_health, &prev))
            return;             /* 无变化：不推。 */
    }
    ++s_health.m_seq;
    XMemcpy(s_health.m_last, s_health.m_entries, sizeof(s_health.m_last));
    s_health.m_lastCount = s_health.m_count;
    hsfHealthPublish(self);
    XPrintf("HsfServerApp: 设备健康变化（seq=%d，%d 台）\n",
            s_health.m_seq, s_health.m_count);
}

/**
 * @brief 立即补发一份当前健康快照（客户端首连/首订阅用）。
 * @note  先采集再发，保证客户端拿到的是"此刻"而不是上一拍。
 */
static void hsfAppPublishHealthNow(XLinkServer* self)
{
    if (!self) return;
    hsfHealthCollect(&s_health);
    XMemcpy(s_health.m_last, s_health.m_entries, sizeof(s_health.m_last));
    s_health.m_lastCount = s_health.m_count;
    s_health.m_primed = true;
    hsfHealthPublish(self);
}


/* ==================== 载荷辅助 ==================== */

/**
 * @brief 从 BSON 载荷取侧号。
 * @param payload BSON 文档字节；可为 NULL。
 * @param size 字节数。
 * @return 侧号；无 side 键、解析失败或越界一律按 A 侧（兼容单侧老客户端）。
 */
static HsfSide hsfAppSideFromPayload(const uint8_t* payload, uint32_t size)
{
    XBsonDocument* doc;
    XBsonValue* v;
    int32_t side = (int32_t)HSF_SIDE_A;

    if (!payload || size == 0) return HsfSide_A;
    doc = XBsonDocument_create();
    if (!doc) return HsfSide_A;
    if (XBsonDocument_from_bytes(doc, payload, (size_t)size)) {
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_SIDE);
        if (v) side = XBsonValue_toInt32(v, (int32_t)HSF_SIDE_A);
    }
    XClassDelete((XClass*)doc);
    return (side == (int32_t)HSF_SIDE_B) ? HsfSide_B : HsfSide_A;
}

/**
 * @brief 在已有 BSON 文档里追加 side 字段（两端共用主题时按字段区分）。
 * @return 写入成功返回 true。
 */
static bool hsfAppInsertSide(XBsonDocument* doc, HsfSide side)
{
    if (!doc) return false;
    return XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_SIDE, (int32_t)side);
}

/**
 * @brief 给一段现成 BSON 载荷补上 side 键，返回新载荷。
 * @param bytes 源 BSON（**所有权移交**：无论成败都被释放）。
 * @param side 侧号。
 * @return 新 XByteArray（调用方负释放）；解析/编码失败返回 NULL。
 * @note 变频器状态由 XVfdCodec 编码，本服务端不重复实现那张键表，故用
 *       「解码 → 插键 → 重编码」的方式补 side（一帧几十字节，成本可忽略）。
 */
static XByteArray* hsfAppWithSide(XByteArray* bytes, HsfSide side)
{
    XBsonDocument* doc;
    XByteArray* out;

    if (!bytes) return NULL;
    doc = XBsonDocument_create();
    if (!doc) {
        XClassDelete((XClass*)bytes);
        return NULL;
    }
    if (!XBsonDocument_from_bytes(doc, (const uint8_t*)XByteArray_constData(bytes),
                                  (size_t)XByteArray_size_base(bytes))) {
        XClassDelete((XClass*)doc);
        XClassDelete((XClass*)bytes);
        return NULL;
    }
    XClassDelete((XClass*)bytes);
    hsfAppInsertSide(doc, side);
    out = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return out;
}

/* ==================== 应答包络 ==================== */

/**
 * @brief 构建应答包络 {"ok":bool,"err":utf8} 并编为 BSON 字节。
 * @return 新建 XByteArray；分配失败返回 NULL。@warning 调用方 XClassDelete。
 */
static XByteArray* hsfAppMakeEnvelope(bool ok, const char* errUtf8)
{
    XBsonDocument* doc = XBsonDocument_create();
    XByteArray* bytes;
    if (!doc) return NULL;
    if (!XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, ok) ||
        (errUtf8 && !XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_ERR, errUtf8))) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief 把所有已装配变频器的控制闸同步到当前模式。
 * @details 自动 → 关闸（设备层也拒绝写入，双保险）；手动 → 开闸。
 *          控制闸是 XVfdDevice 的单一咽喉，覆盖全品牌全命令模型。
 */
static void hsfAppApplyModeToDevices(void)
{
    HsfSide side;
    bool enable = (s_runMode == HsfRunMode_Manual);
    for (side = HsfSide_A; side < HsfSide_Count; ++side) {
        XVfdDevice* dev = HsfServerIo_vfdDevice(side);
        if (dev) XVfdDevice_setControlEnabled(dev, enable);
    }
}

/**
 * @brief 读取当前运行模式。
 */
HsfRunMode HsfServerApp_runMode(void)
{
    return s_runMode;
}

/**
 * @brief 设置运行模式并落盘。
 * @param mode 目标模式；非法值忽略。
 */
void HsfServerApp_setRunMode(HsfRunMode mode)
{
    if (mode != HsfRunMode_Manual && mode != HsfRunMode_Auto) return;
    if (s_runMode == mode) {
        hsfAppApplyModeToDevices();      /* 幂等：闸同步一次。 */
        return;
    }
    s_runMode = mode;
    hsfAppApplyModeToDevices();
    HsfServerSettings_setInt("run/mode", (int)mode);
    XPrintf("HsfServerApp: 运行模式切到 %s（远程控制%s）\n",
            mode == HsfRunMode_Auto ? "自动" : "手动",
            mode == HsfRunMode_Auto ? "已禁用" : "已启用");
}

/**
 * @brief 应答包络 + 模式字段（{"ok","err","mode","auto"}）。
 */
static XByteArray* hsfAppMakeModeEnvelope(bool ok, const char* errUtf8)
{
    XBsonDocument* doc = XBsonDocument_create();
    XByteArray* bytes;
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, ok);
    if (errUtf8)
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_ERR, errUtf8);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_MODE, (int32_t)s_runMode);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO,
                                      s_runMode == HsfRunMode_Auto);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief 发送应答（BSON 载荷；bytes 为 NULL 时发空应答）。
 */
static void hsfAppRespond(XLinkSession* session, const XLinkFrameHeader* frame,
                          XByteArray* bytes)
{
    if (!session || !frame) {
        if (bytes) XClassDelete((XClass*)bytes);
        return;
    }
    XLinkSession_sendResponse_ex(session, frame->m_requestId, frame->m_msgCode,
                                 XLinkDataType_Bson,
                                 bytes ? (const uint8_t*)XByteArray_constData(bytes) : NULL,
                                 bytes ? (uint32_t)XByteArray_size_base(bytes) : 0);
    if (bytes) XClassDelete((XClass*)bytes);
}

/* ==================== 命令处理 ==================== */

/**
 * @brief 变频器控制命令：角色门禁 → 解码 → 分派到指定侧的设备。
 */
static void hsfAppHandleVfdControl(XLinkSession* session,
                                   const XLinkFrameHeader* frame,
                                   const uint8_t* payload, XLinkServer* srv)
{
    XVfdControlRequest req;
    XVfdDevice* dev;
    HsfSide side;
    bool ok = false;
    const char* err = NULL;

    /* [自动模式闸] 最前置：自动运行中一律拒绝远程控制（角色门禁之后
     * 也不放开——两个条件必须同时满足才允许写入）。遥测/快照不经过本
     * 函数，故自动模式仍然可以正常监视。 */
    if (s_runMode == HsfRunMode_Auto) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false,
                                         "自动运行中：远程控制已被禁用（现场切换到手动后可用）"));
        return;
    }
    if (XLinkServer_sessionRole(srv, session) < (uint8_t)HSF_ROLE_CONTROL) {
        hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "权限不足：控制命令需要 root 账户"));
        return;
    }
    side = hsfAppSideFromPayload(payload, frame->m_payloadLen);
    if (!XVfdCodec_controlFromBson(&req, payload, frame->m_payloadLen)) {
        hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "控制载荷解析失败"));
        return;
    }
    dev = HsfServerIo_vfdDevice(side);
    if (!dev) {
        hsfAppRespond(session, frame, hsfAppMakeEnvelope(false,
                        side == HsfSide_A ? "变频器未就绪（A 侧）"
                                          : "变频器未就绪（B 侧）"));
        return;
    }
    /* [点动手持] 101 = 点动释放（自定义 op，不走 XVfdOperation 值域）。 */
    if ((int)req.m_op == HSF_OP_JOG_RELEASE) {
        hsfAppJogRelease(side);
        hsfAppRespond(session, frame, hsfAppMakeEnvelope(true, NULL));
        return;
    }
    ok = XVfdCodec_applyControl(dev, &req);
    if (!ok) err = "设备拒绝（在途动作/控制闸/能力不符）";
    /* [点动手持] 心跳刷新判据：**不要求本次写入成功**。
     * 点动是持续意图，同一命令字反复下发时设备层单飞会把后续帧判 Busy
     * （上一笔写落定未回），若此时不刷新看门狗，心跳就被判"丢失"而误停机
     * （实测：客户端发 34 帧、服务端只认 2 帧 → 600ms 后自动停）。
     * 只要命令被受理过（设备在位、控制闸开、命令源正确），就视为心跳；
     * 首次进入点动态时仍成功下发一次命令字。 */
    if (req.m_op == XVfdOperation_Jog) {
        /* 直接查设备忙碌位（比 lastError 可靠：后者会被后续调用覆盖）。 */
        bool busy = XVfdDevice_isBusy(dev);
        if (ok || busy) {
            hsfAppJogTouch(side, req.m_reverse);
            if (!ok) { ok = true; err = NULL; }   /* 心跳帧回 ok，客户端不必重试 */
        }
    }
    hsfAppRespond(session, frame, hsfAppMakeEnvelope(ok, err));
}

/**
 * @brief 运行模式：查询 / 设置。
 * @details 载荷 {"mode":int} 有 mode 键=设置（需 root），无=查询。
 *          查询对所有已认证角色开放（客户端要能显示当前模式）。
 */
static void hsfAppHandleRunMode(XLinkSession* session,
                                const XLinkFrameHeader* frame,
                                const uint8_t* payload, XLinkServer* srv)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonValue* v;
    bool wantSet = false;
    int32_t newMode = 0;

    if (!doc) return;
    if (XBsonDocument_from_bytes(doc, payload, frame->m_payloadLen)) {
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_MODE);
        if (v) {
            wantSet = true;
            newMode = XBsonValue_toInt32(v, 0);
        }
    }
    XClassDelete((XClass*)doc);

    if (!wantSet) {                      /* 查询。 */
        hsfAppRespond(session, frame, hsfAppMakeModeEnvelope(true, NULL));
        return;
    }
    if (XLinkServer_sessionRole(srv, session) < (uint8_t)HSF_ROLE_CONTROL) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false,
                                         "权限不足：切换运行模式需要 root 账户"));
        return;
    }
    if (newMode != (int32_t)HsfRunMode_Manual &&
        newMode != (int32_t)HsfRunMode_Auto) {
        hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "非法模式值"));
        return;
    }
    HsfServerApp_setRunMode((HsfRunMode)newMode);
    hsfAppRespond(session, frame, hsfAppMakeModeEnvelope(true, NULL));
}

/**
 * @brief 设备健康：返回当前全量快照（供客户端断线重连对账）。
 */
static void hsfAppHandleDeviceHealth(XLinkSession* session,
                                     const XLinkFrameHeader* frame)
{
    XByteArray* bytes;
    hsfHealthCollect(&s_health);
    s_health.m_primed = true;
    bytes = hsfHealthToBson(&s_health);
    hsfAppRespond(session, frame, bytes);
}

/**
 * @brief 变频器遥测快照命令：编码指定侧当前状态回包。
 */
static void hsfAppHandleVfdSnapshot(XLinkSession* session,
                                    const XLinkFrameHeader* frame,
                                    const uint8_t* payload)
{
    const HsfSide side = hsfAppSideFromPayload(payload, frame->m_payloadLen);
    const XVfdDevice* dev = (const XVfdDevice*)HsfServerIo_vfdDevice(side);
    const XVfdStatus* st = dev ? XVfdDevice_status_const(dev) : NULL;
    int keyCount = 0;
    const uint64_t* keys = XVfdCodec_telemetryKeys(&keyCount);
    XByteArray* bytes = XVfdCodec_statusToBson(st, keys, keyCount);
    bytes = hsfAppWithSide(bytes, side);
    hsfAppRespond(session, frame, bytes);
}

/**
 * @brief 温湿度遥测快照命令：{"side","temp","hum","tempValid","humValid","staleMs"}。
 * @note 温度单位 0.1℃（可负）、湿度 0.1%RH；服务端不再换算，客户端按
 *       0.1 单位显示（键名口径与 XTempHumTransmitterCodec 一致）。
 */
static void hsfAppHandleTempHumSnapshot(XLinkSession* session,
                                        const XLinkFrameHeader* frame,
                                        const uint8_t* payload)
{
    const HsfSide side = hsfAppSideFromPayload(payload, frame->m_payloadLen);
    const HsfServerIoTempHum* th = HsfServerIo_tempHum(side);
    XBsonDocument* doc = XBsonDocument_create();
    XByteArray* bytes;
    if (!doc) {
        hsfAppRespond(session, frame, NULL);
        return;
    }
    hsfAppInsertSide(doc, side);
    if (th) {
        int32_t temp = 0;
        int32_t hum = 0;
        bool tv = false;
        bool hv = false;
        /* 未收到数据的通道不写值键：客户端据此区分「0」与「无数据」。 */
        if (HsfServerIoTempHum_values(th, &temp, &hum, &tv, &hv)) {
            if (tv) XBsonDocument_insert_keyUtf8_int32(doc, "temp", temp);
            if (hv) XBsonDocument_insert_keyUtf8_int32(doc, "hum", hum);
        }
        XBsonDocument_insert_keyUtf8_bool(doc, "tempValid", tv);
        XBsonDocument_insert_keyUtf8_bool(doc, "humValid", hv);
        XBsonDocument_insert_keyUtf8_int32(doc, "staleMs",
                                          (int32_t)HSF_TEMPHUM_STALE_MS);
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    hsfAppRespond(session, frame, bytes);
}

/**
 * @brief 把流量计快照填进 BSON 文档（推送与快照命令共用同一份口径）。
 * @param doc 目标文档（不能为 NULL）。
 * @param fm 该侧流量计站（可为 NULL=未装配，只写 valid=false）。
 * @note 键名与 XFlowMeterCodec 的线上口径对齐（flow/speed/forwardTotal/
 *       reverseTotal/flowUnit/totalUnit/alarms），另加 valid。
 *       物理量用 double 写入——流量是 IEEE754 单精度解出的值，累积量可到
 *       千万级，定点整数装不下。
 */
static void hsfAppFillFlowMeter(XBsonDocument* doc, const HsfServerIoFlowMeter* fm)
{
    double flow = 0.0;
    double speed = 0.0;
    double fwdTotal = 0.0;
    double revTotal = 0.0;
    int flowUnit = 0;
    int totalUnit = 0;
    int alarms = 0;
    bool valid = false;

    if (!doc) return;
    if (fm) {
        (void)HsfServerIoFlowMeter_values(fm, &flow, &speed, &fwdTotal, &revTotal,
                                          &flowUnit, &totalUnit, &alarms, &valid);
        if (valid) {
            XBsonDocument_insert_keyUtf8_double(doc, "flow", flow);
            XBsonDocument_insert_keyUtf8_double(doc, "speed", speed);
            XBsonDocument_insert_keyUtf8_double(doc, "forwardTotal", fwdTotal);
            XBsonDocument_insert_keyUtf8_double(doc, "reverseTotal", revTotal);
            XBsonDocument_insert_keyUtf8_int32(doc, "flowUnit", flowUnit);
            XBsonDocument_insert_keyUtf8_int32(doc, "totalUnit", totalUnit);
            XBsonDocument_insert_keyUtf8_int32(doc, "alarms", alarms);
        }
    }
    XBsonDocument_insert_keyUtf8_bool(doc, "valid", valid);
    XBsonDocument_insert_keyUtf8_int32(doc, "staleMs",
                                      (int32_t)HSF_FLOWMETER_STALE_MS);
}

/**
 * @brief 电磁流量计快照命令：{"side", "flow", "speed", ... , "valid"}。
 */
static void hsfAppHandleFlowMeterSnapshot(XLinkSession* session,
                                          const XLinkFrameHeader* frame,
                                          const uint8_t* payload)
{
    const HsfSide side = hsfAppSideFromPayload(payload, frame->m_payloadLen);
    XBsonDocument* doc = XBsonDocument_create();
    XByteArray* bytes;
    if (!doc) {
        hsfAppRespond(session, frame, NULL);
        return;
    }
    hsfAppInsertSide(doc, side);
    hsfAppFillFlowMeter(doc, HsfServerIo_flowMeter(side));
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    hsfAppRespond(session, frame, bytes);
}
static void hsfAppHandleAiSnapshot(XLinkSession* session,
                                   const XLinkFrameHeader* frame)
{
    XBsonDocument* doc = XBsonDocument_create();
    XByteArray* bytes = NULL;
    double ma[HSF_AI_CHANNELS];
    int32_t raw[HSF_AI_CHANNELS];
    bool valid[HSF_AI_CHANNELS];
    int n;
    int i;
    uint32_t vmask = 0;
    if (!doc) {
        hsfAppRespond(session, frame, NULL);
        return;
    }
    n = HsfServerIo_aiValues(ma, valid, HSF_AI_CHANNELS);
    (void)HsfServerIo_aiRawValues(raw, NULL, HSF_AI_CHANNELS);
    for (i = 0; i < n; ++i) {
        char key[12];
        if (valid[i]) vmask |= (1u << i);
        snprintf(key, sizeof(key), "r%d", i);
        XBsonDocument_insert_keyUtf8_int32(doc, key, raw[i]);
        snprintf(key, sizeof(key), "u%d", i);
        XBsonDocument_insert_keyUtf8_int32(doc, key,
                                           (int32_t)(ma[i] * 1000.0 + 0.5));
    }
    XBsonDocument_insert_keyUtf8_int32(doc, "n", n);
    XBsonDocument_insert_keyUtf8_int32(doc, "v", (int32_t)vmask);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    hsfAppRespond(session, frame, bytes);
}

/**
 * @brief 分发回调：XLink 全部业务帧的统一入口（在会话 pump 栈上同步执行）。
 */
static void hsfAppDispatch(XLinkSession* session, const XLinkFrameHeader* frame,
                           const uint8_t* payload)
{
    XLinkServer* srv = s_srv;
    if (!session || !frame) return;
    switch (frame->m_msgClass) {
    case XLinkMsgClass_Request:
        switch (frame->m_msgCode) {
        case HSF_CMD_VFD_CONTROL:
            hsfAppHandleVfdControl(session, frame, payload, srv);
            break;
        case HSF_CMD_VFD_SNAPSHOT:
            hsfAppHandleVfdSnapshot(session, frame, payload);
            break;
        case HSF_CMD_AI_SNAPSHOT:
            hsfAppHandleAiSnapshot(session, frame);
            break;
        case HSF_CMD_RUN_MODE:
            hsfAppHandleRunMode(session, frame, payload, srv);
            break;
        case HSF_CMD_DEVICE_HEALTH:
            hsfAppHandleDeviceHealth(session, frame);
            break;
        case HSF_CMD_TEMPHUM_SNAPSHOT:
            hsfAppHandleTempHumSnapshot(session, frame, payload);
            break;
        case HSF_CMD_FLOWMETER_SNAPSHOT:
            hsfAppHandleFlowMeterSnapshot(session, frame, payload);
            break;
        default:
            /* 未知命令：回否定应答，客户端可感知（不发则对端空等）。 */
            hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "未知命令"));
            break;
        }
        break;
    case XLinkMsgClass_Subscribe: {
        /* 订阅登记由会话层自动完成。**所有遥测主题都"订阅即补发一次"**：
         * 客户端切到某页才订阅（隐藏即退订），若只等周期推送，首帧要等
         * 一个推送周期（温湿度曾 1s）→ 页面刚打开显示"--"，观感"没数据"。
         * 补发用该会话定向发送，只影响本次订阅者。
         * @note  [2026-11-04 实锅] 必须以 **payload 里的 4 字节 topicId** 为
         *       准：m_msgCode 只有 16 位（协议层主题号 0x000100NN 放不下），
         *       用它补发会把主题号截成 0x00NN 发出去，客户端按完整主题号
         *       匹配 → 整帧被丢弃（首帧补发"发了但收不到"）。 */
        uint32_t topic = 0;
        if (frame->m_payloadLen >= 4)
            XMemory_read_data(payload, XBYTE_ORDER_LITTLE_ENDIAN,
                              (uint8_t*)&topic, sizeof(topic));
        if (!topic) topic = (uint32_t)(frame->m_msgCode & 0xFFFFu);
        XPrintf("HsfServerApp: 会话订阅主题 0x%08X\n", topic);
        if (session && s_srv) {
            if (topic == HSF_TOPIC_DEVICE_HEALTH) {
                XByteArray* bytes;
                hsfHealthCollect(&s_health);
                s_health.m_primed = true;
                bytes = hsfHealthToBson(&s_health);
                if (bytes) {
                    XLinkSession_sendPublish_ex(session, HSF_TOPIC_DEVICE_HEALTH,
                                                XLinkDataType_Bson,
                                                (const uint8_t*)XByteArray_constData(bytes),
                                                (uint32_t)XByteArray_size_base(bytes));
                    XClassDelete((XClass*)bytes);
                }
            } else {
                /* 其余遥测：走统一的"立即推一拍给该会话"路径。 */
                hsfAppPublishTopicNow(s_srv, session, topic);
            }
        }
        break;
    }
    default:
        break;
    }
}

/**
 * @brief 会话增删通知（连接建立/断线的可见性）。
 */
/* ==================== 会话策略（root 单会话 / admin 多会话） ==================== */

/**
 * @brief 用户名比较（const char* 口径；栈上 XString 复用框架字符串族）。
 */
static bool hsfAppUserIs(const char* user, const char* name)
{
    bool eq;
    XString_Init_Utf8(tmp, user);   /* 宏自带声明：tmp 即 XString*。 */
    eq = XString_equals_utf8(tmp, name, XChar_CaseSensitive);
    XClassDeinit((XClass*)tmp);
    return eq;
}

/**
 * @brief 查是否存在其他已认证的 root 会话。
 * @param except 排除的会话（发起本次认证的会话自身；可为 NULL）。
 * @return 存在返回 true。
 * @note 经服务端会话表逐个比对已认证用户名；人机界面连接数小，O(n) 足够。
 *       极小竞态窗（两个 root 同时处于认证中）不设防——认证在毫秒级完成。
 */
static bool hsfAppRootSessionActive(const XLinkSession* except)
{
    int i;
    int n = s_srv ? XLinkServer_sessionCount(s_srv) : 0;
    for (i = 0; i < n; ++i) {
        XLinkSession* s = XLinkServer_sessionAt(s_srv, i);
        const char* user;
        if (!s || (const XLinkSession*)s == except) continue;
        user = XLinkServer_sessionUser_const(s_srv, s);
        if (hsfAppUserIs(user, HSF_DEFAULT_ROOT_NAME)) return true;
    }
    return false;
}

/**
 * @brief 服务端凭据查表（替换框架缺省查表，附 root 单会话策略）。
 * @param userData 注册时传入的会话自身（活跃判定时排除自己）。
 * @return 见 XLinkSessionAuthLookupResult。
 * @note 策略门禁：root 只允许一个在线会话（防并行修改）；admin（只读）
 *       不限。拒绝走 BadUser——框架按认证失败发 AuthResult(Rejected)+
 *       Goodbye(AuthFailed)，客户端收到干净的 authFailed（登录窗留在
 *       原地，不会闪进主界面）。查表在会话 pump 栈上同步执行；查表语义
 *       复刻框架缺省版（经公开 userAt 读账户库记录，避免触碰私有状态）。
 */
static int hsfAppAuthLookup(void* userData, const char* user,
                            uint8_t saltOut[XLINK_AUTH_SALT_BYTES],
                            uint32_t* iterationsOut,
                            uint8_t hashOut[XLINK_AUTH_NONCE_BYTES],
                            uint8_t* roleOut)
{
    XLinkSession* self = (XLinkSession*)userData;
    uint32_t i;
    uint32_t n;

    if (hsfAppUserIs(user, HSF_DEFAULT_ROOT_NAME) && hsfAppRootSessionActive(self)) {
        XPrintf("HsfServerApp: root 单会话策略拒绝重复登录\n");
        return XLinkSessionAuthLookup_BadUser;
    }
    if (!user || !user[0] || !s_srv)
        return XLinkSessionAuthLookup_BadUser;
    n = XLinkServer_userCount(s_srv);
    for (i = 0; i < n; ++i) {
        const XLinkUserRecord* r = XLinkServer_userAt(s_srv, i);
        if (!r || !r->m_name) continue;
        if (!XString_equals_utf8(r->m_name, user, XChar_CaseSensitive)) continue;
        if (r->m_flags & XLINK_USER_FLAG_LOCKED)
            return XLinkSessionAuthLookup_Locked;
        if (!(r->m_flags & XLINK_USER_FLAG_HAS_PASSWORD))
            return XLinkSessionAuthLookup_BadUser;
        memcpy(saltOut, r->m_salt, XLINK_AUTH_SALT_BYTES);
        *iterationsOut = r->m_iterations;
        memcpy(hashOut, r->m_hash, XLINK_AUTH_NONCE_BYTES);
        *roleOut = r->m_role;
        return XLinkSessionAuthLookup_Ok;
    }
    return XLinkSessionAuthLookup_BadUser;
}

static void hsfAppNotify(XLinkServer* self, XLinkSession* session,
                         XLinkServerSessionEvent event)
{
    (void)self;
    if (event == XLinkServerSessionEvent_Added && session) {
        /* 替换框架缺省查表（accept 时挂的）为带 root 单会话策略的版本；
         * userData 传会话自身，供活跃判定排除自己。 */
        XLinkSession_setAuthLookup(session, hsfAppAuthLookup, session);
    }
    if (event == XLinkServerSessionEvent_Added && session) {
        /* 客户端首次连接：立即补发一份设备健康快照（免等待下一拍）。
         * 注：此时尚未订阅本主题，故用单会话直发（broadcast 之外的
         * 定向发送），保证"首连即拿到当前状态"。 */
        XByteArray* bytes;
        hsfHealthCollect(&s_health);
        s_health.m_primed = true;
        bytes = hsfHealthToBson(&s_health);
        if (bytes) {
            XLinkSession_sendPublish_ex(session, HSF_TOPIC_DEVICE_HEALTH,
                                        XLinkDataType_Bson,
                                        (const uint8_t*)XByteArray_constData(bytes),
                                        (uint32_t)XByteArray_size_base(bytes));
            XClassDelete((XClass*)bytes);
        }
    }
    /* 接入时带上会话设备指针 + socket 状态：排查「一次物理连接被建成
     * 多个会话」时，光看路数无法区分「客户端真的重连」与「服务端把同一条
     * 连接反复 accept」。
     * @note 不要打印 `XIODevice_fd`：那是文件类设备才填的字段，socket
     *       走 `socketDeviceFd`（XDevice 设备句柄），其 `m_fd` 恒为
     *       XFD_INVALID——拿它当"连接有效性"判据会把真实连接全判成空壳
     *       （本战役踩过这个坑）。这里改打 socket 连接状态。 */
    if (event == XLinkServerSessionEvent_Added) {
        XIODevice* dev = XLinkSession_device(session);
        int st = dev ? (int)XAbstractSocket_state((XAbstractSocket*)dev) : -1;
        /* 对端端口是分辨"真重连"与"同一条连接被重复 accept"的唯一硬证据：
         * 同端口 = 同一条物理连接。取自 socket 自身的 peer 信息。 */
        XPrintf("HsfServerApp: 会话接入（当前 %d 路，dev=%p sockState=%d "
                "peerPort=%u）\n",
                XLinkServer_sessionCount(self), (void*)dev, st,
                dev ? (unsigned)XAbstractSocket_peerPort((XAbstractSocket*)dev) : 0u);
    } else {
        XPrintf("HsfServerApp: 会话断开（当前 %d 路）\n",
                XLinkServer_sessionCount(self));
    }
}


/* ==================== 周期遥测 ==================== */

/** @brief 按主题分频的推送节拍簿记（每主题上次推送时刻）。 */
typedef struct HsfAppPushGate {
    uint64_t m_lastMs;      /**< 上次推送时刻（单调毫秒；0=从未推）。 */
    uint32_t m_intervalMs;  /**< 本主题推送间隔（毫秒；0=不推）。 */
} HsfAppPushGate;

/**
 * @brief 推送闸数组：按「主题 × 侧」各一把。
 * @details 一个主题内有多少侧就推多少帧，故闸必须按侧独立——单闸管一个
 *          主题会让多侧共享计时，实际每侧间隔被侧数除短（2026-11-04 实锅：
 *          温湿度两路侧共用一闸，实测 473ms ≈ 配置 1000ms ÷ 2）。
 */
enum {
    HSF_PUSH_GATE_VFD = 0,
    HSF_PUSH_GATE_TEMPHUM,
    HSF_PUSH_GATE_FLOWMETER,
    HSF_PUSH_GATE_AI,
    HSF_PUSH_GATE_COUNT
};

/** @brief 闸表：[主题][侧]。模拟量无侧维度，只用 [AI][0]。 */
static HsfAppPushGate s_pushGates[HSF_PUSH_GATE_COUNT][2];

/**
 * @brief 本拍该主题是否到期。
 * @param g 推送闸；nowMs 当前时刻。
 * @return 到期（该推）返回 true。
 * @note 间隔 0 视为"跟随基准节拍"——tick 本身就是基准节拍，故恒到期。
 */
static bool hsfAppPushDue(HsfAppPushGate* g, uint64_t nowMs)
{
    if (!g) return false;
    if (g->m_intervalMs == 0) return true;
    if (g->m_lastMs != 0 && nowMs >= g->m_lastMs &&
        (nowMs - g->m_lastMs) < (uint64_t)g->m_intervalMs)
        return false;
    g->m_lastMs = nowMs;
    return true;
}

/**
 * @brief 初始化推送闸（start 时按配置宏设间隔）。
 * @note  间隔 0 = 跟随基准节拍（tick 本身即基准），非 0 按本闸独立分频。
 */
static void hsfAppPushGatesInit(void)
{
    int t;
    int si;
    const uint32_t vfdMs = (HSF_VFD_PUSH_MS > 0) ? (uint32_t)HSF_VFD_PUSH_MS : 0u;
    const uint32_t thMs = (HSF_TEMPHUM_PUSH_MS > 0)
                              ? (uint32_t)HSF_TEMPHUM_PUSH_MS : 0u;
    const uint32_t fmMs = (HSF_FLOWMETER_PUSH_MS > 0)
                              ? (uint32_t)HSF_FLOWMETER_PUSH_MS : 0u;
    const uint32_t aiMs = (HSF_AI_PUSH_MS > 0) ? (uint32_t)HSF_AI_PUSH_MS : 0u;

    for (t = 0; t < HSF_PUSH_GATE_COUNT; ++t) {
        for (si = 0; si < 2; ++si) {
            s_pushGates[t][si].m_lastMs = 0;
            s_pushGates[t][si].m_intervalMs = 0;
        }
    }
    for (si = 0; si < 2; ++si) {
        s_pushGates[HSF_PUSH_GATE_VFD][si].m_intervalMs = vfdMs;
        s_pushGates[HSF_PUSH_GATE_TEMPHUM][si].m_intervalMs = thMs;
        s_pushGates[HSF_PUSH_GATE_FLOWMETER][si].m_intervalMs = fmMs;
    }
    s_pushGates[HSF_PUSH_GATE_AI][0].m_intervalMs = aiMs;
    (void)t;
}

/**
 * @brief 推一份温湿度快照（含 side）到温湿度主题。
 * @param self 服务端；side 侧号；th 该侧温湿度站（NULL=未装配，推空快照）。
 */
/**
 * @brief 编码温湿度快照为 BSON 载荷（订阅补发与周期推送共用）。
 * @return 新建 XByteArray；未装配/编码失败返回 NULL。@warning 调用方释放。
 */
static XByteArray* hsfAppEncodeTempHum(HsfSide side,
                                      const HsfServerIoTempHum* th)
{
    XBsonDocument* doc = XBsonDocument_create();
    XByteArray* bytes;
    if (!doc) return NULL;
    hsfAppInsertSide(doc, side);
    if (th) {
        int32_t temp = 0;
        int32_t hum = 0;
        bool tv = false;
        bool hv = false;
        if (HsfServerIoTempHum_values(th, &temp, &hum, &tv, &hv)) {
            if (tv) XBsonDocument_insert_keyUtf8_int32(doc, "temp", temp);
            if (hv) XBsonDocument_insert_keyUtf8_int32(doc, "hum", hum);
        }
        XBsonDocument_insert_keyUtf8_bool(doc, "tempValid", tv);
        XBsonDocument_insert_keyUtf8_bool(doc, "humValid", hv);
        XBsonDocument_insert_keyUtf8_int32(doc, "staleMs",
                                          (int32_t)HSF_TEMPHUM_STALE_MS);
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief 发布温湿度主题（周期推送）。
 */
static void hsfAppPublishTempHum(XLinkServer* self, HsfSide side,
                                 const HsfServerIoTempHum* th)
{
    XByteArray* bytes = hsfAppEncodeTempHum(side, th);
    if (!bytes || !self) {
        if (bytes) XClassDelete((XClass*)bytes);
        return;
    }
    XLinkServer_publish_ex(self, HSF_TOPIC_TEMPHUM_STATUS, XLinkDataType_Bson,
                           (const uint8_t*)XByteArray_constData(bytes),
                           (uint32_t)XByteArray_size_base(bytes));
    XClassDelete((XClass*)bytes);
}

/**
 * @brief 发布温湿度主题（全部侧各推一份，各自闸独立计时）。
 */
static void hsfAppTickTempHum(XLinkServer* self)
{
    int si;
    const uint64_t nowMs = XDateTime_currentMSecsSinceEpoch();
    for (si = 0; si < (int)HsfSide_Count; ++si) {
        const HsfSide side = (HsfSide)si;
        if (!hsfAppPushDue(&s_pushGates[HSF_PUSH_GATE_TEMPHUM][si], nowMs))
            continue;
        hsfAppPublishTempHum(self, side, HsfServerIo_tempHum(side));
    }
}

/**
 * @brief 发布流量计主题（全部侧各推一份，各自闸独立计时）。
 */
static void hsfAppTickFlowMeter(XLinkServer* self)
{
    int si;
    const uint64_t nowMs = XDateTime_currentMSecsSinceEpoch();
    for (si = 0; si < (int)HsfSide_Count; ++si) {
        XBsonDocument* doc;
        XByteArray* bytes;
        if (!hsfAppPushDue(&s_pushGates[HSF_PUSH_GATE_FLOWMETER][si], nowMs))
            continue;
        doc = XBsonDocument_create();
        if (!doc) continue;
        hsfAppInsertSide(doc, (HsfSide)si);
        hsfAppFillFlowMeter(doc, HsfServerIo_flowMeter((HsfSide)si));
        bytes = XBsonDocument_toBson(doc);
        XClassDelete((XClass*)doc);
        if (bytes) {
            XLinkServer_publish_ex(self, HSF_TOPIC_FLOWMETER_STATUS,
                                   XLinkDataType_Bson,
                                   (const uint8_t*)XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes));
            XClassDelete((XClass*)bytes);
        }
    }
}

/**
 * @brief 发布变频器主题（全部侧各推一份；未就绪的侧跳过）。
 */
static void hsfAppTickVfd(XLinkServer* self)
{
    int si;
    const uint64_t nowMs = XDateTime_currentMSecsSinceEpoch();
    for (si = 0; si < (int)HsfSide_Count; ++si) {
        const XVfdDevice* dev;
        const XVfdStatus* st;
        if (!hsfAppPushDue(&s_pushGates[HSF_PUSH_GATE_VFD][si], nowMs))
            continue;
        dev = (const XVfdDevice*)HsfServerIo_vfdDevice((HsfSide)si);
        st = dev ? XVfdDevice_status_const(dev) : NULL;
        if (st && st->m_valid) {
            int keyCount = 0;
            const XVfdStatusField* keys = XVfdCodec_telemetryKeys(&keyCount);
            XByteArray* bytes = XVfdCodec_statusToBson(st, keys, keyCount);
            bytes = hsfAppWithSide(bytes, (HsfSide)si);
            if (bytes) {
                XLinkServer_publish_ex(self, HSF_TOPIC_VFD_STATUS,
                                       XLinkDataType_Bson,
                                       (const uint8_t*)XByteArray_constData(bytes),
                                       (uint32_t)XByteArray_size_base(bytes));
                XClassDelete((XClass*)bytes);
            }
        }
    }
}

/**
 * @brief 发布模拟量主题（与侧无关）。
 */
static void hsfAppTickAi(XLinkServer* self)
{
    XBsonDocument* doc = XBsonDocument_create();
    if (!doc) return;
    {
        double ma[HSF_AI_CHANNELS];
        int32_t raw[HSF_AI_CHANNELS];
        bool valid[HSF_AI_CHANNELS];
        int n = HsfServerIo_aiValues(ma, valid, HSF_AI_CHANNELS);
        int i;
        uint32_t vmask = 0;
        XByteArray* bytes;
        (void)HsfServerIo_aiRawValues(raw, NULL, HSF_AI_CHANNELS);
        for (i = 0; i < n; ++i) {
            char key[12];
            if (valid[i]) vmask |= (1u << i);
            snprintf(key, sizeof(key), "r%d", i);
            XBsonDocument_insert_keyUtf8_int32(doc, key, raw[i]);
            snprintf(key, sizeof(key), "u%d", i);
            XBsonDocument_insert_keyUtf8_int32(doc, key,
                                               (int32_t)(ma[i] * 1000.0 + 0.5));
        }
        XBsonDocument_insert_keyUtf8_int32(doc, "n", n);
        XBsonDocument_insert_keyUtf8_int32(doc, "v", (int32_t)vmask);
        bytes = XBsonDocument_toBson(doc);
        if (bytes) {
            XLinkServer_publish_ex(self, HSF_TOPIC_AI_STATUS, XLinkDataType_Bson,
                                   (const uint8_t*)XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes));
            XClassDelete((XClass*)bytes);
        }
    }
    XClassDelete((XClass*)doc);
}

/**
 * @brief 遥测推送拍：三个主题各按自己的间隔到期才推（每侧独立计时）。
 * @note  tick 周期 = HSF_TELEMETRY_MS（基准节拍）。主题间隔为 0 表示跟随
 *        基准（每拍都推）；非 0 时按本主题**每侧**独立分频——慢变量
 *        （温湿度 1s）不必按 200ms 推同一份值，也不会因为有两侧而翻倍。
 *        只投递给**订阅了该主题**的客户端；无人订阅时 publish 静默返回 0。
 */
/**
 * @brief 收到点动帧：刷新看门狗（首次进入点动态）。
 * @param side 侧号；reverse 方向。
 */
static void hsfAppJogTouch(HsfSide side, bool reverse)
{
    HsfAppJogWatch* w;
    if (side < 0 || side >= HsfSide_Count) return;
    w = &s_jogWatch[side];
    if (!w->m_active) {
        XPrintf("HsfServerApp: 点动开始（侧 %s%s）\n",
                side == HsfSide_A ? "A" : "B", reverse ? " 反转" : " 正转");
    }
    w->m_active = true;
    w->m_reverse = reverse;
    w->m_lastMs = XDateTime_currentMSecsSinceEpoch();
}

/**
 * @brief 点动释放：立刻停机并清看门狗（客户端松手）。
 * @param side 侧号。
 */
static void hsfAppJogRelease(HsfSide side)
{
    HsfAppJogWatch* w;
    XVfdDevice* dev;
    if (side < 0 || side >= HsfSide_Count) return;
    w = &s_jogWatch[side];
    if (!w->m_active) return;
    w->m_active = false;
    dev = HsfServerIo_vfdDevice(side);
    if (dev) {
        /* 点动释放用减速停机（F3 点动停止），保持与手动点动一致的减速曲线。 */
        (void)XVfdDevice_stop(dev);
        XPrintf("HsfServerApp: 点动释放（侧 %s）→ 停机\n",
                side == HsfSide_A ? "A" : "B");
    }
}

/**
 * @brief 点动看门狗巡检：超时未收到点动帧 → 自动停机。
 * @note  防"链路断开后设备一直点动"：这是安全兜底，不是正常路径。
 */
static void hsfAppJogWatchdog(void)
{
    HsfSide side;
    uint64_t now = XDateTime_currentMSecsSinceEpoch();
    for (side = HsfSide_A; side < HsfSide_Count; ++side) {
        HsfAppJogWatch* w = &s_jogWatch[side];
        XVfdDevice* dev;
        if (!w->m_active) continue;
        if (now - w->m_lastMs <= (uint64_t)HSF_JOG_TIMEOUT_MS) continue;
        w->m_active = false;
        dev = HsfServerIo_vfdDevice(side);
        if (dev) (void)XVfdDevice_stop(dev);
        XPrintf("HsfServerApp: 点动心跳超时（侧 %s，%llums 无帧）→ 自动停机\n",
                side == HsfSide_A ? "A" : "B",
                (unsigned long long)(now - w->m_lastMs));
    }
}

/* ==================== 订阅即补发：实现 ==================== */

/**
 * @brief 向单会话发一拍指定主题（订阅时调用，免等周期）。
 * @details 编码路径与周期推送完全一致，差别只在"发给谁"：周期走
 *          XLinkServer_publish_ex（按订阅关系投递），这里走
 *          XLinkSession_sendPublish_ex（只发该会话）。不刷新推送门控，
 *          故不影响周期节拍。
 * @param srv 服务端；session 目标会话；topic topicId 低 16 位。
 */
static void hsfAppPublishTopicNow(XLinkServer* srv, XLinkSession* session,
                                  uint32_t topic)
{
    XByteArray* bytes = NULL;
    if (!srv || !session) return;

    if ((topic & 0xFFFFu) == (HSF_TOPIC_VFD_STATUS & 0xFFFFu)) {
        const XVfdDevice* dev = (const XVfdDevice*)HsfServerIo_vfdDevice(HsfSide_A);
        const XVfdStatus* st = dev ? XVfdDevice_status_const(dev) : NULL;
        if (st && st->m_valid) {
            int keyCount = 0;
            const XVfdStatusField* keys = XVfdCodec_telemetryKeys(&keyCount);
            bytes = XVfdCodec_statusToBson(st, keys, keyCount);
            bytes = hsfAppWithSide(bytes, HsfSide_A);
        }
    } else if ((topic & 0xFFFFu) == (HSF_TOPIC_TEMPHUM_STATUS & 0xFFFFu)) {
        bytes = hsfAppEncodeTempHum(HsfSide_A, HsfServerIo_tempHum(HsfSide_A));
    } else if ((topic & 0xFFFFu) == (HSF_TOPIC_FLOWMETER_STATUS & 0xFFFFu)) {
        XBsonDocument* doc = XBsonDocument_create();
        if (doc) {
            hsfAppInsertSide(doc, HsfSide_A);
            hsfAppFillFlowMeter(doc, HsfServerIo_flowMeter(HsfSide_A));
            bytes = XBsonDocument_toBson(doc);
            XClassDelete((XClass*)doc);
        }
    } else if ((topic & 0xFFFFu) == (HSF_TOPIC_AI_STATUS & 0xFFFFu)) {
        XBsonDocument* doc = XBsonDocument_create();
        if (doc) {
            double ma[HSF_AI_CHANNELS];
            int32_t raw[HSF_AI_CHANNELS];
            bool valid[HSF_AI_CHANNELS];
            int n = HsfServerIo_aiValues(ma, valid, HSF_AI_CHANNELS);
            int i;
            uint32_t vmask = 0;
            (void)HsfServerIo_aiRawValues(raw, NULL, HSF_AI_CHANNELS);
            for (i = 0; i < n; ++i) {
                char key[12];
                if (valid[i]) vmask |= (1u << i);
                snprintf(key, sizeof(key), "r%d", i);
                XBsonDocument_insert_keyUtf8_int32(doc, key, raw[i]);
                snprintf(key, sizeof(key), "u%d", i);
                XBsonDocument_insert_keyUtf8_int32(doc, key,
                                                   (int32_t)(ma[i] * 1000.0 + 0.5));
            }
            XBsonDocument_insert_keyUtf8_int32(doc, "n", n);
            XBsonDocument_insert_keyUtf8_int32(doc, "v", (int32_t)vmask);
            bytes = XBsonDocument_toBson(doc);
            XClassDelete((XClass*)doc);
        }
    }
    if (!bytes) return;
    XLinkSession_sendPublish_ex(session, topic, XLinkDataType_Bson,
                                (const uint8_t*)XByteArray_constData(bytes),
                                (uint32_t)XByteArray_size_base(bytes));
    XClassDelete((XClass*)bytes);
}

static void hsfAppTick(XLinkServer* self, void* userData)
{
    /* 设备健康：事件驱动巡检（变化才推）。 */
    hsfHealthTick(self);

    /* 点动手持看门狗：超时自动停机（安全兜底）。 */
    hsfAppJogWatchdog();

    (void)userData;

    hsfAppTickVfd(self);
    hsfAppTickTempHum(self);
    hsfAppTickFlowMeter(self);
    /* 模拟量无侧维度，单闸。 */
    if (hsfAppPushDue(&s_pushGates[HSF_PUSH_GATE_AI][0],
                      XDateTime_currentMSecsSinceEpoch()))
        hsfAppTickAi(self);
}

/* ==================== 账户库 ==================== */

/**
 * @brief 装配账户库：落数据目录 JSON；空库时创建缺省账户并落盘。
 * @return 账户库可用返回 true。
 */
static bool hsfAppSetupUsers(XLinkServer* srv)
{
    char path[600];
    const char* dir = HsfServerSettings_dataDir();
    if (!dir || !dir[0]) dir = ".";
    snprintf(path, sizeof(path), "%s/hsf_users.json", dir);
    if (!XLinkServer_setUserStorePath(srv, path)) {
        XPrintf("HsfServerApp: 账户库加载失败（%s）\n", path);
        return false;
    }
    if (XLinkServer_userCount(srv) == 0) {
        /* 首次启动：admin=只读（User 角色），root=管理员（可控制）。 */
        if (!XLinkServer_addUser(srv, HSF_DEFAULT_USER_NAME, HSF_DEFAULT_USER_PASS,
                                 (uint8_t)XLinkAuthRole_User) ||
            !XLinkServer_addUser(srv, HSF_DEFAULT_ROOT_NAME, HSF_DEFAULT_ROOT_PASS,
                                 (uint8_t)XLinkAuthRole_Admin)) {
            XPrintf("HsfServerApp: 缺省账户创建失败\n");
            return false;
        }
        if (!XLinkServer_saveUserStore(srv)) {
            XPrintf("HsfServerApp: 账户库落盘失败（%s）——本次仍可用\n", path);
        } else {
            XPrintf("HsfServerApp: 缺省账户已创建（%s）：admin/admin 只读、"
                    "root/root 可改\n", path);
        }
    } else {
        XPrintf("HsfServerApp: 账户库已加载（%s，%d 账户）\n", path,
                XLinkServer_userCount(srv));
    }
    return true;
}

/* ==================== 公开 API ==================== */

bool HsfServerApp_start(uint16_t port)
{
    if (s_srv) HsfServerApp_stop();
    if (port == 0) port = (uint16_t)HSF_XLINK_DEFAULT_PORT;

    s_srv = XLinkServer_create();
    if (!s_srv) {
        XPrintf("HsfServerApp: XLinkServer 创建失败\n");
        return false;
    }
    XLinkServer_setDispatchCallback(s_srv, hsfAppDispatch, NULL);
    XLinkServer_setNotifyCallback(s_srv, hsfAppNotify);
    XLinkServer_setTickCallback(s_srv, hsfAppTick, NULL);
    XLinkServer_setTickInterval(s_srv, HSF_TELEMETRY_MS);
    hsfAppPushGatesInit();      /* 三主题按各自间隔分频（0=跟随基准节拍）。 */
    if (!hsfAppSetupUsers(s_srv)) {
        XClassDelete((XClass*)s_srv);
        s_srv = NULL;
        return false;
    }
    XLinkServer_setAuthEnabled(s_srv, true);
    if (!XLinkServer_listen(s_srv, NULL, port)) {
        XPrintf("HsfServerApp: 监听 %u 端口失败\n", (unsigned)port);
        XClassDelete((XClass*)s_srv);
        s_srv = NULL;
        return false;
    }
    /* 恢复持久化的运行模式，并同步到设备控制闸。 */
    s_runMode = (HsfServerSettings_getInt("run/mode", 0) == (int)HsfRunMode_Auto)
                    ? HsfRunMode_Auto : HsfRunMode_Manual;
    hsfAppApplyModeToDevices();
    XPrintf("HsfServerApp: 运行模式 = %s\n",
            s_runMode == HsfRunMode_Auto ? "自动（远程控制禁用）" : "手动");

    XPrintf("HsfServerApp: XLink 服务端就绪（端口 %u，鉴权开，"
            "遥测 %dms）\n", (unsigned)port, HSF_TELEMETRY_MS);
    return true;
}

void HsfServerApp_stop(void)
{
    if (!s_srv) return;
    XLinkServer_close(s_srv);
    XClassDelete((XClass*)s_srv);
    s_srv = NULL;
}

int HsfServerApp_sessionCount(void)
{
    return s_srv ? XLinkServer_sessionCount(s_srv) : 0;
}
