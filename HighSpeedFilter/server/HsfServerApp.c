
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
#include "XAutoControl.h"
#include "XAutoControlCodec.h"
#include "XAccount.h"
#include "XPermission.h"
#include "XLoginPolicy.h"
#include "XAccessControlCodec.h"
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

/* ==================== 自动运行控制（XAutoController 装配） ==================== */

/**
 * @brief 自动运行控制器（Src/Industry/XAutoControl 模块类实例）。
 * @details [与模式位的关系——两个"自动"不是一回事]
 *            s_runMode              = 模式闸：自动=远程控制被禁（授权态）；
 *            s_autoCtl 运行位        = 控制器：自动逻辑真的在驱动设备（动作态）。
 *          [安全口径，与 Python 侧 app.py 逐条同口径]
 *            - 进自动**不**自动启动控制器（模式开关不是设备启动按钮）；
 *            - 退自动**强制停**控制器（单向安全动作，见
 *              hsfAppApplyModeToDevices）；
 *            - 重启恢复模式位后控制器**一律保持停止**（没人按过启动，
 *              设备不能自己动）；
 *            - 真正启动只走 HSF_CMD_AUTO_CONTROL（需 AutoControl 权限 +
 *              已处自动模式）。
 *          [本版本无工艺逻辑] 控制器是空骨架：start/stop/急停锁存/报警/
 *          记账全链路真实生效，step 放行不做任何动作（安全默认）。设备
 *          访问归使用方适配层——将来填工艺逻辑时注册
 *          hsfAppAutoStepHandler（其 ud 上下文自带 HsfServerIo 访问），
 *          装配面不变。
 */
static XAutoController s_autoCtl;

/** @brief 控制器是否已初始化（HsfServerApp_start 置位；stop 复位）。 */
static bool s_autoCtlReady = false;

/* ==================== 访问控制（XAccount/XPermPolicy/XLoginPolicy） ==================== */

/**
 * @brief 权限策略（角色→权限位图；判定零遍历）。
 * @details 各控制命令的闸从此查：控制面=DeviceControl、切模式=ModeSwitch、
 *          自动运行=AutoControl。拒绝留痕开（permissionDenied → 日志）。
 */
static XPermPolicy s_perm;

/**
 * @brief 登录策略（匿名登录/同账并发/会话上限）。
 * @details [收编既有 root 单会话特判] 原 hsfAppAuthLookup 里"root 只许一个
 *          在线会话"是硬编码特例；收编后=缺省策略（匿名关/同账并发关/
 *          上限 8），语义零漂移。
 */
static XLoginPolicy s_loginPolicy;

/**
 * @brief 账户管理器（XLinkUserStore 同格式记录的管理面：CRUD/锁定策略/
 *        缺省账户引导）。
 * @details [记录主源] 认证查表从它读（hsfAppAuthLookup）；XLinkServer 的
 *          文件副本仅作启动兜底。变更落盘经它 save（原子写），跨重启保持。
 */
static XAccountManager s_accounts;

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
/**
 * @brief 数字量快照推送间隔（毫秒；0 = 跟随 HSF_TELEMETRY_MS）。
 * @details 交互面：DO 点动要立刻看到回显，跟随基准 100ms 节拍。
 */
#define HSF_DIGITAL_PUSH_MS     0

/**
 * @brief 电磁阀快照推送间隔（毫秒；0 = 跟随基准）。
 * @details 阀状态本身是秒级过程（动作 1~3s），但"点下去→动作中"的反馈
 *          必须立即出现，故同样跟随基准节拍。
 */
#define HSF_VALVE_PUSH_MS       0

/**
 * @brief 进水泵快照推送间隔（毫秒；500 = 独立分频，对齐 Python
 *        PUMP_PUSH_MS）。
 * @details 泵没有"动作中"过程（切换即时），500ms 足够跟手；独立闸而非
 *          跟随 100ms 基准——泵帧通常只有 1 台，降频省总线（与 Python
 *          侧 app.PUMP_PUSH_MS=500 同口径）。
 */
#define HSF_PUMP_PUSH_MS        500

/**
 * @brief 通道名是否已变更（true ⇒ 下一拍主动下发一帧只含名字的快照）。
 * @details 名字是**部署参数**（写在 HsfServerConfig.h 宏里），运行期不会变；
 *          本标志是给"现场改宏重新部署后不重启客户端也能更新"以及将来
 *          做名字热改预留的通路。当前由 HsfServerApp_notifyDigitalNamesChanged
 *          置位。
 */
static bool s_digitalNamesDirty = false;

/** @brief 数字量/电磁阀/进水泵快照编码（定义在周期遥测段；此处前向声明）。 */
static XByteArray* hsfAppEncodeDigital(bool full);
static XByteArray* hsfAppEncodeDigitalNames(void);
static XByteArray* hsfAppEncodeValves(void);
static XByteArray* hsfAppEncodePump(void);

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

        /* ---- 浊度仪 ×2（逐台一条）----
         * [类别必须是"水质"而不是"温湿度"] 曾经误用 HsfDeviceKind_TempHum，
         * 首页"类型"列把两台浊度仪显示成"温湿度"——数据正确但类别完全误导
         * （现场会以为那是电箱里的温湿度变送器）。类别必须表达"这是什么
         * 设备"，复用一个不相关的值等于在界面上撒谎。
         * [逐台而不是汇总] 两台是独立接线（站号 1/3），一台没接时另一台
         * 必须照常报 OK——汇总成一条会让"一台坏"看起来像"整组坏"。 */
        {
            int t;
            for (t = 0; t < HSF_TURBIDITY_COUNT; ++t) {
                const HsfServerIoTurbidity* tm = HsfServerIo_turbidity(side, t);
                snprintf(name, sizeof(name), "浊度仪%d-%s", t + 1, sideName);
                if (!tm) {
                    hsfHealthAdd(st, name, HSF_DEVICE_KIND_WATER, (int)side,
                                 HsfDeviceHealth_Offline, 0, false, 0, "未装配");
                    continue;
                }
                {
                    bool online = HsfServerIoTurbidity_isOnline(tm);
                    hsfHealthAdd(st, name, HSF_DEVICE_KIND_WATER, (int)side,
                                 online ? HsfDeviceHealth_Ok
                                        : HsfDeviceHealth_Stale,
                                 0, online, 0, online ? "" : "无有效数据");
                }
            }
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

    /* ---- 进水泵（数字量模块上的一路 DO，归类为数字量） ----
     * [与 Python 侧 pumps.health_summary 同口径] 无数字量模块→离线；
     * 未配置点位→离线；有配置但回显无效→陈旧；否则正常。
     * 泵没有独立通讯口，side 记数字量模块所在侧(A)，客户端按
     * (kind=DIGITAL, side=0) 找到它。 */
    {
        const HsfServerIoPump* pumps = HsfServerIo_pumps();
        if (pumps && HsfServerIoPump_count(pumps) > 0) {
            const HsfServerIoPumpItem* item = HsfServerIoPump_item(pumps, 0);
            int health = HsfDeviceHealth_Ok;
            bool online = true;
            const char* text = "";
            if (!HsfServerIo_digitalReady()) {
                health = HsfDeviceHealth_Offline;
                online = false;
                text = "数字量模块未装配";
            } else if (!HsfServerIoPump_ready(pumps)) {
                health = HsfDeviceHealth_Offline;
                online = false;
                text = "未配置点位";
            } else if (item && !item->m_valid) {
                health = HsfDeviceHealth_Stale;
                text = "无有效回显";
            }
            hsfHealthAdd(st, "进水泵", HSF_DEVICE_KIND_DIGITAL,
                         (int)HsfSide_A, health, 0, online, 0, text);
        }
    }

    /* ---- 数字量模块（C 版为 .12/.13 聚合后的单站） ---- */
    {
        uint32_t di = 0;
        uint32_t doMask = 0;
        bool valid = false;
        if (HsfServerIo_digitalReady()) {
            (void)HsfServerIo_digitalMasks(&di, &doMask, &valid);
            hsfHealthAdd(st, "数字量模块", HSF_DEVICE_KIND_DIGITAL,
                         (int)HsfSide_A,
                         valid ? HsfDeviceHealth_Ok : HsfDeviceHealth_Stale,
                         0, valid, 0, valid ? "" : "无有效数据");
        } else {
            hsfHealthAdd(st, "数字量模块", HSF_DEVICE_KIND_DIGITAL,
                         (int)HsfSide_A, HsfDeviceHealth_Offline,
                         0, false, 0, "未装配");
        }
    }

    /* ---- 电磁阀组（整组一条，含故障路数摘要） ---- */
    {
        const HsfServerIoValve* valves = HsfServerIo_valves();
        if (valves && HsfServerIoValve_count(valves) > 0) {
            int health = HsfDeviceHealth_Ok;
            bool online = true;
            char text[96] = "";
            if (!HsfServerIo_digitalReady()) {
                health = HsfDeviceHealth_Offline;
                online = false;
                snprintf(text, sizeof(text), "数字量模块未装配");
            } else if (!HsfServerIoValve_ready(valves)) {
                health = HsfDeviceHealth_Offline;
                online = false;
                snprintf(text, sizeof(text), "未配置点位");
            } else {
                int faults = HsfServerIoValve_faultCount(valves);
                if (faults > 0) {
                    health = HsfDeviceHealth_Fault;
                    snprintf(text, sizeof(text), "%d 路故障", faults);
                } else {
                    bool anyValid = false;
                    int i;
                    for (i = 0; i < HsfServerIoValve_count(valves); ++i) {
                        const HsfServerIoValveItem* it =
                            HsfServerIoValve_item(valves, i);
                        if (it && it->m_valid) { anyValid = true; break; }
                    }
                    if (!anyValid) {
                        health = HsfDeviceHealth_Stale;
                        snprintf(text, sizeof(text), "无有效反馈");
                    }
                }
            }
            hsfHealthAdd(st, "电磁阀组", HSF_DEVICE_KIND_VALVE,
                         (int)HsfSide_A, health, 0, online, 0, text);
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
 *          [自动控制器单向安全] 退自动 ⇒ 强制停控制器（切回手动还在自动跑
 *          = 手动模式下设备被偷偷驱动，硬性安全问题）；进自动 ⇒ **不**启动
 *          （模式开关不是设备启动按钮，见 s_autoCtl 说明）。
 */
static void hsfAppApplyModeToDevices(void)
{
    HsfSide side;
    bool enable = (s_runMode == HsfRunMode_Manual);
    for (side = HsfSide_A; side < HsfSide_Count; ++side) {
        XVfdDevice* dev = HsfServerIo_vfdDevice(side);
        if (dev) XVfdDevice_setControlEnabled(dev, enable);
    }
    if (s_autoCtlReady && !enable && XAutoController_running(&s_autoCtl)) {
        XAutoController_stop(&s_autoCtl, (uint64_t)XDateTime_currentMSecsSinceEpoch());
        XPrintf("HsfServerApp: 退出自动模式，自动运行已强制停止\n");
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
/**
 * @brief 通知"通道名已变更"：下一拍主动下发一帧只含名字的快照。
 * @note  名字本质是部署参数（HsfServerConfig.h 宏），运行期通常不变；
 *        本接口给"改了宏重新部署后不重启客户端"以及将来热改名预留通路。
 */
void HsfServerApp_notifyChannelNamesChanged(void)
{
    s_digitalNamesDirty = true;
}

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
 * @brief 应答包络 + 模式字段（{"ok","err","mode","auto","autoRunning"}）。
 * @details autoRunning 是**控制器**的真实状态（与模式位分开回显）：客户端
 *          拿到的就是切换/启停之后控制器此刻的运行位，不必等下一帧推送，
 *          也不允许界面从模式位推断运行位（没启动时假"在跑"比故障更难查）。
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
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO_RUNNING,
                                      s_autoCtlReady &&
                                      XAutoController_running(&s_autoCtl));
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
    if (!XPermPolicy_check(&s_perm,
                           (XAccountRole)XLinkServer_sessionRole(srv, session),
                           (uint32_t)XAccessPerm_DeviceControl)) {
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
    /* [权限策略闸] 切模式需要 ModeSwitch（只有 Admin 持有，等价旧口径）。 */
    if (!XPermPolicy_check(&s_perm,
                           (XAccountRole)XLinkServer_sessionRole(srv, session),
                           (uint32_t)XAccessPerm_ModeSwitch)) {
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

/* ==================== 自动运行控制（XAutoController 装配与命令） ==================== */

/**
 * @brief 装配自动运行控制器（HsfServerApp_start 调用一次）。
 * @note  [本版本无 stepHandler] 控制器是空骨架：start/stop/急停锁存/报警/
 *        记账全链路真实生效，step 放行后不做任何动作（安全默认，见
 *        XAutoControl.h）。设备访问归使用方适配层——将来填工艺逻辑时注册
 *        hsfAppAutoStepHandler（其 ud 上下文自带 HsfServerIo 访问：DI/DO
 *        位图直读、写 DO 走 HsfServerIo_digitalWrite、动作失败经
 *        noteAction+raiseAlarm 回灌），装配面不变。
 */
static void hsfAppAutoCtlSetup(void)
{
    if (!s_autoCtlReady) {
        XAutoController_init(&s_autoCtl);
        s_autoCtlReady = true;
    }
    XPrintf("HsfServerApp: 自动运行控制器已装配（骨架：无工艺逻辑，不产生动作）\n");
}

/**
 * @brief 自动运行控制命令（HSF_CMD_AUTO_CONTROL，载荷 {"op":int}）。
 * @details 闸门顺序与 Python 侧 _h_auto_control 逐条同口径，四操作字分发
 *          （HSF_AUTO_OP_*；兼容老载荷 {"on":bool}）：
 *            1) 角色门禁——**停机/急停/复位也要求 root**（自动运行状态属于
 *               运行决策，不开放只读角色）；
 *            2) 启动必须在自动模式（**停止/急停/复位永远放行**——停机类
 *               动作不被任何闸门挡住）；拒绝时照样回显模式与运行位，客户端
 *               能立刻纠正界面而不是再猜；
 *            3) 急停锁存语义由 XAutoController 保证（复位前拒绝启动；
 *               复位只回停止态、不重新启动）。
 *          应答 = 模式包络（mode/auto/autoRunning 为执行**之后**的值）。
 */
static void hsfAppHandleAutoControl(XLinkSession* session,
                                    const XLinkFrameHeader* frame,
                                    const uint8_t* payload, XLinkServer* srv)
{
    XBsonDocument* doc;
    XBsonValue* v;
    bool haveOp = false;
    int32_t op = 0;
    bool wantOn = false;      /* 兼容老载荷 {"on":bool}。 */
    bool changed = false;
    const char* opName = "";

    /* [权限策略闸] 四板斧全部要求 AutoControl（含停止/急停/复位——自动
     * 运行状态属于运行决策，不开放只读角色，与 Python 侧同口径）。 */
    if (!XPermPolicy_check(&s_perm,
                           (XAccountRole)XLinkServer_sessionRole(srv, session),
                           (uint32_t)XAccessPerm_AutoControl)) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false,
                                         "权限不足：自动运行控制需要 root 账户"));
        return;
    }
    if (!s_autoCtlReady) {
        hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "自动控制器未装配"));
        return;
    }
    doc = XBsonDocument_create();
    if (doc && XBsonDocument_from_bytes(doc, payload, frame->m_payloadLen)) {
        v = XBsonDocument_value_keyUtf8(doc, "op");
        if (v) {
            op = XBsonValue_toInt32(v, 0);
            haveOp = true;
        }
        v = XBsonDocument_value_keyUtf8(doc, "on");
        if (v) wantOn = XBsonValue_toBool(v, false);
    }
    if (doc) XClassDelete((XClass*)doc);

    /* op 键缺失时按老口径折算（true≈启动 / false≈停止）。 */
    if (!haveOp) op = wantOn ? HSF_AUTO_OP_START : HSF_AUTO_OP_STOP;

    switch (op) {
    case HSF_AUTO_OP_START:
        opName = "启动";
        if (s_runMode != HsfRunMode_Auto) {
            hsfAppRespond(session, frame,
                          hsfAppMakeModeEnvelope(false,
                              "当前是手动模式：请先切到自动模式，再启动自动运行"));
            return;
        }
        changed = XAutoController_start(&s_autoCtl,
                                        (uint64_t)XDateTime_currentMSecsSinceEpoch());
        break;
    case HSF_AUTO_OP_STOP:
        opName = "停止";
        changed = XAutoController_stop(&s_autoCtl,
                                       (uint64_t)XDateTime_currentMSecsSinceEpoch());
        break;
    case HSF_AUTO_OP_ESTOP:
        opName = "急停";
        changed = XAutoController_eStop(&s_autoCtl,
                                        (uint64_t)XDateTime_currentMSecsSinceEpoch());
        break;
    case HSF_AUTO_OP_RESET:
        opName = "复位";
        changed = XAutoController_reset(&s_autoCtl,
                                        (uint64_t)XDateTime_currentMSecsSinceEpoch());
        break;
    default:
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "非法 op 操作字（1启动/2停止/3急停/4复位）"));
        return;
    }
    XPrintf("HsfServerApp: 自动运行请求%s（%s）\n", opName,
            changed ? "已执行" : "已是该状态（幂等）");
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
 * @brief 把两台浊度仪的快照填进 BSON 文档（推送与快照命令共用同一份口径）。
 * @param doc 目标文档（不能为 NULL）。
 * @param side 侧号。
 * @note 键表与 Python 侧 encode_turbidity 逐键一致：
 *         {"count", "v0_name","v0_ntu","v0_raw","v0_online", ... , "staleMs"}
 *       其中 ntu 是 **NTU×100 的整数**（Python 侧 proto.KEY_TURB_NTU 同口径），
 *       客户端 ÷100 显示两位小数。
 *
 *       [为什么不发浮点] 与 Python 侧既定口径对齐是第一位的：同一个 BSON 帧
 *       要能被两边的客户端读成同一个数。设备编解码层 XTurbidityMeterCodec 的
 *       "ntu" 是**无损原值**（double），服务于进程内/记录回放；本函数是
 *       **线上协议层**，按跨进程展示口径发定点整数。两者同名不同口径是刻意
 *       的分工——混用会差 100 倍且不报错。
 *
 *       [raw 必须发] 换算口径一旦有疑问，raw 是唯一能反推真相的量。现场拿到
 *       raw 就能判断是"字序错了"还是"格式错了"，不必猜。
 */
static void hsfAppFillTurbidity(XBsonDocument* doc, HsfSide side)
{
    int i;
    if (!doc) return;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_TURB_COUNT,
                                       (int32_t)HSF_TURBIDITY_COUNT);
    for (i = 0; i < HSF_TURBIDITY_COUNT; ++i) {
        const HsfServerIoTurbidity* tm = HsfServerIo_turbidity(side, i);
        char key[64];
        char name[32];
        double ntu = 0.0;
        uint32_t raw = 0;
        bool online = false;

        /* 名字恒带出（未装配也给出，客户端据此占位显示）。 */
        snprintf(name, sizeof(name), "浊度仪%d", i + 1);
        snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, i, HSF_KEY_TURB_NAME);
        XBsonDocument_insert_keyUtf8_utf8(doc, key, name);

        if (tm) {
            online = HsfServerIoTurbidity_isOnline(tm);
            (void)HsfServerIoTurbidity_ntu(tm, &ntu);
            (void)HsfServerIoTurbidity_raw(tm, &raw);
        }
        snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, i, HSF_KEY_TURB_ONLINE);
        XBsonDocument_insert_keyUtf8_bool(doc, key, online);
        /* 在线才写值：离线时给旧值会让界面显示"一个数"却不知所措。
         * 客户端按 online=false 显示"--"并提示从站失联。 */
        if (online) {
            snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, i,
                     HSF_KEY_TURB_NTU);
            /* ×100 取整：四舍五入而非截断（9.995 → 1000 而不是 999）。 */
            XBsonDocument_insert_keyUtf8_int32(doc, key,
                (int32_t)(ntu * 100.0 + (ntu >= 0.0 ? 0.5 : -0.5)));
            snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, i,
                     HSF_KEY_TURB_RAW);
            XBsonDocument_insert_keyUtf8_int64(doc, key, (int64_t)raw);
        }
    }
    XBsonDocument_insert_keyUtf8_int32(doc, "staleMs",
                                       (int32_t)HSF_TURBIDITY_STALE_MS);
}

/**
 * @brief 浊度仪快照命令：{"count","v0_*","v1_*","staleMs"}。
 * @details 浊度仪是**只读设备**——标定/偏移/清洗都在仪表面板上做，对外通讯
 *          只暴露测量值（手册 4.3.10）。故只有快照命令，没有控制命令。
 */
static void hsfAppHandleTurbiditySnapshot(XLinkSession* session,
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
    hsfAppFillTurbidity(doc, side);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    hsfAppRespond(session, frame, bytes);
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
/**
 * @brief 编码模拟量快照（full=true 时附全部通道名）。
 * @param full 是否全量（含名字）；false=只带数值/掩码（周期推送用）。
 * @return 新建字节数组；失败 NULL。@warning 调用方释放。
 * @note  [名字的唯一事实源] 通道名来自 HsfServerConfig.h 的 HSF_AI_NAMES 宏；
 *        客户端首次拉全量拿到名字，之后只收数值。
 */
static XByteArray* hsfAppEncodeAi(bool full)
{
    XBsonDocument* doc = XBsonDocument_create();
    XByteArray* bytes;
    double ma[HSF_AI_CHANNELS];
    int32_t raw[HSF_AI_CHANNELS];
    bool valid[HSF_AI_CHANNELS];
    int n;
    int i;
    uint32_t vmask = 0;

    if (!doc) return NULL;
    n = HsfServerIo_aiValues(ma, valid, HSF_AI_CHANNELS);
    (void)HsfServerIo_aiRawValues(raw, NULL, HSF_AI_CHANNELS);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_FULL, full);
    for (i = 0; i < n; ++i) {
        char key[12];
        if (valid[i]) vmask |= (1u << i);
        snprintf(key, sizeof(key), HSF_KEY_AI_RAW_FMT, i);
        XBsonDocument_insert_keyUtf8_int32(doc, key, raw[i]);
        snprintf(key, sizeof(key), HSF_KEY_AI_UA_FMT, i);
        XBsonDocument_insert_keyUtf8_int32(doc, key,
                                           (int32_t)(ma[i] * 1000.0 + 0.5));
    }
    if (full) {
        static const char* const kNames[HSF_ANALOG_CHANNELS] = HSF_AI_NAMES;
        for (i = 0; i < n && i < HSF_ANALOG_CHANNELS; ++i) {
            char key[12];
            if (!kNames[i]) continue;
            snprintf(key, sizeof(key), HSF_KEY_AI_NAME_FMT, i);
            XBsonDocument_insert_keyUtf8_utf8(doc, key, kNames[i]);
        }
    }
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_AI_COUNT, n);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_AI_VALIDMASK, (int32_t)vmask);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

static void hsfAppHandleAiSnapshot(XLinkSession* session,
                                   const XLinkFrameHeader* frame)
{
    /* 客户端首次拉全量（含通道名）；之后周期推送只带数值。 */
    XByteArray* bytes = hsfAppEncodeAi(true);
    if (!bytes) {
        hsfAppRespond(session, frame, NULL);
        return;
    }
    hsfAppRespond(session, frame, bytes);
}

/**
 * @brief 分发回调：XLink 全部业务帧的统一入口（在会话 pump 栈上同步执行）。
 */
/**
 * @brief 数字量/电磁阀控制命令的公共前置闸（模式 → 角色）。
 * @return 通过返回 true；被拒时已发否定应答并返回 false。
 */
static bool hsfAppControlGate(XLinkSession* session, const XLinkFrameHeader* frame,
                              XLinkServer* srv)
{
    if (s_srv && HsfServerApp_runMode() == HsfRunMode_Auto) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false,
                          "\xE8\x87\xAA\xE5\x8A\xA8\xE8\xBF\x90\xE8\xA1\x8C\xE4\xB8\xAD"
                          "\xEF\xBC\x9A\xE8\xBF\x9C\xE7\xA8\x8B\xE6\x8E\xA7\xE5\x88\xB6"
                          "\xE5\xB7\xB2\xE8\xA2\xAB\xE7\xA6\x81\xE7\x94\xA8"));
        /* 自动运行中：远程控制已被禁用 */
        return false;
    }
    /* [权限策略闸] 角色位图判定（零遍历）替代原硬比较；拒绝留痕由
     * XPermPolicy 的 permissionDenied 信号统一打日志（装配见
     * hsfAppAccessCtlSetup）。语义与旧口径等价：只有 Admin 持有
     * DeviceControl。 */
    if (!XPermPolicy_check(&s_perm,
                           (XAccountRole)XLinkServer_sessionRole(srv, session),
                           (uint32_t)XAccessPerm_DeviceControl)) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false,
                          "\xE6\x9D\x83\xE9\x99\x90\xE4\xB8\x8D\xE8\xB6\xB3"
                          "\xEF\xBC\x9A\xE6\x8E\xA7\xE5\x88\xB6\xE5\x91\xBD\xE4\xBB\xA4"
                          "\xE9\x9C\x80\xE8\xA6\x81 root \xE8\xB4\xA6\xE6\x88\xB7"));
        /* 权限不足：控制命令需要 root 账户 */
        return false;
    }
    return true;
}

/**
 * @brief 数字量快照请求（Response = 位图 BSON；未装配回否定应答）。
 */
static void hsfAppHandleDigitalSnapshot(XLinkSession* session,
                                        const XLinkFrameHeader* frame)
{
    XByteArray* bytes = hsfAppEncodeDigital(true);   /* 客户端首次拉全量（含名字）。 */
    if (!bytes) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false,
                          "\xE6\x95\xB0\xE5\xAD\x97\xE9\x87\x8F\xE7\xAB\x99"
                          "\xE6\x9C\xAA\xE8\xA3\x85\xE9\x85\x8D")); /* 数字量站未装配 */
        return;
    }
    XLinkSession_sendResponse_ex(session, frame->m_requestId, frame->m_msgCode,
                                 XLinkDataType_Bson,
                                 (const uint8_t*)XByteArray_constData(bytes),
                                 (uint32_t)XByteArray_size_base(bytes));
    XClassDelete((XClass*)bytes);
}

/**
 * @brief 数字量输出控制（{"index":int,"on":bool}）。
 */
static void hsfAppHandleDigitalControl(XLinkSession* session,
                                       const XLinkFrameHeader* frame,
                                       const uint8_t* payload, XLinkServer* srv)
{
    XBsonDocument* doc;
    XBsonValue* v;
    int index = -1;
    bool on = false;
    bool ok;

    if (!hsfAppControlGate(session, frame, srv)) return;

    doc = XBsonDocument_create();
    if (!doc || !XBsonDocument_from_bytes(doc, payload, frame->m_payloadLen)) {
        if (doc) XClassDelete((XClass*)doc);
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "控制载荷解析失败"));
        return;
    }
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_DIG_INDEX);
    if (v) index = XBsonValue_toInt32(v, -1);
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_DIG_ON);
    if (v) on = XBsonValue_toBool(v, false);
    XClassDelete((XClass*)doc);

    if (index < 0 || index >= HsfServerIo_digitalCount()) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "通道号越界"));
        return;
    }
    ok = HsfServerIo_digitalWrite(index, on);
    /* 应答带上最新位图：客户端不必等下一拍推送就能把按钮态对齐。 */
    {
        XByteArray* bytes = hsfAppEncodeDigital(false);  /* 控制应答只带状态。 */
        if (bytes) {
            XBsonDocument* resp = XBsonDocument_create();
            if (resp) {
                uint32_t di = 0;
                uint32_t doMask = 0;
                bool valid = false;
                HsfServerIo_digitalMasks(&di, &doMask, &valid);
                XBsonDocument_insert_keyUtf8_bool(resp, HSF_KEY_OK, ok);
                XBsonDocument_insert_keyUtf8_int32(resp, HSF_KEY_DIG_DI, (int32_t)di);
                XBsonDocument_insert_keyUtf8_int32(resp, HSF_KEY_DIG_DO, (int32_t)doMask);
                XClassDelete((XClass*)bytes);
                bytes = XBsonDocument_toBson(resp);
                XClassDelete((XClass*)resp);
            }
            if (bytes) {
                XLinkSession_sendResponse_ex(session, frame->m_requestId,
                                             frame->m_msgCode, XLinkDataType_Bson,
                                             (const uint8_t*)XByteArray_constData(bytes),
                                             (uint32_t)XByteArray_size_base(bytes));
                XClassDelete((XClass*)bytes);
                return;
            }
        }
    }
    hsfAppRespond(session, frame, hsfAppMakeEnvelope(ok, ok ? NULL : "写入被拒"));
}

/**
 * @brief 电磁阀快照请求（Response = 6 路 BSON）。
 */
static void hsfAppHandleValveSnapshot(XLinkSession* session,
                                      const XLinkFrameHeader* frame)
{
    XByteArray* bytes = hsfAppEncodeValves();
    if (!bytes) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "电磁阀组未装配"));
        return;
    }
    XLinkSession_sendResponse_ex(session, frame->m_requestId, frame->m_msgCode,
                                 XLinkDataType_Bson,
                                 (const uint8_t*)XByteArray_constData(bytes),
                                 (uint32_t)XByteArray_size_base(bytes));
    XClassDelete((XClass*)bytes);
}

/**
 * @brief 电磁阀控制（{"index":int,"on":bool}）。
 * @note  受理 ≠ 到位：应答带最新快照，客户端据此立刻更新卡片；真正的
 *        "已开/已关"由反馈判读后经周期推送补上。
 */
static void hsfAppHandleValveControl(XLinkSession* session,
                                     const XLinkFrameHeader* frame,
                                     const uint8_t* payload, XLinkServer* srv)
{
    XBsonDocument* doc;
    XBsonValue* v;
    int index = -1;
    bool wantOpen = false;
    char err[96];
    bool ok;
    XByteArray* snap;

    if (!hsfAppControlGate(session, frame, srv)) return;

    doc = XBsonDocument_create();
    if (!doc || !XBsonDocument_from_bytes(doc, payload, frame->m_payloadLen)) {
        if (doc) XClassDelete((XClass*)doc);
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "控制载荷解析失败"));
        return;
    }
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_VALVE_INDEX);
    if (v) index = XBsonValue_toInt32(v, -1);
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_VALVE_ON);
    if (v) wantOpen = XBsonValue_toBool(v, false);
    XClassDelete((XClass*)doc);

    err[0] = '\0';
    ok = HsfServerIo_valveSet(index, wantOpen, err, (int)sizeof(err));
    if (!ok) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, err[0] ? err : "阀门控制被拒"));
        return;
    }

    /* 应答 = 包络 + 最新整帧快照（一次刷新全部 6 路，避免逐路拼）。 */
    {
        XBsonDocument* resp = XBsonDocument_create();
        XByteArray* bytes = NULL;
        if (resp) {
            XBsonDocument_insert_keyUtf8_bool(resp, HSF_KEY_OK, true);
            bytes = XBsonDocument_toBson(resp);
            XClassDelete((XClass*)resp);
        }
        if (!bytes) bytes = hsfAppMakeEnvelope(true, NULL);
        snap = hsfAppEncodeValves();
        if (snap && bytes) {
            /* 简化：先回包络，随后把快照作为第二段用同一 requestId 发送会
             * 破坏 Request/Response 配对，故快照只走周期推送。此处仅回
             * 受理结果；客户端收到 ok 后等下一拍（≤100ms）拿新状态。 */
            XClassDelete((XClass*)snap);
        } else if (snap) {
            XClassDelete((XClass*)snap);
        }
        if (bytes) {
            XLinkSession_sendResponse_ex(session, frame->m_requestId,
                                         frame->m_msgCode, XLinkDataType_Bson,
                                         (const uint8_t*)XByteArray_constData(bytes),
                                         (uint32_t)XByteArray_size_base(bytes));
            XClassDelete((XClass*)bytes);
        }
    }
}

/**
 * @brief 进水泵快照编码（count + v{i}_ 前缀键；与 Python _pump_doc 同表）。
 * @return 新建 XByteArray；泵组未装配/编码失败返回 NULL。@warning 调用方释放。
 */
static XByteArray* hsfAppEncodePump(void)
{
    const HsfServerIoPump* bank = HsfServerIo_pumps();
    XBsonDocument* doc;
    XByteArray* bytes;
    int count;
    int i;

    if (!bank) return NULL;
    count = HsfServerIoPump_count(bank);
    doc = XBsonDocument_create();
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_PUMP_COUNT, count);
    for (i = 0; i < count; ++i) {
        const HsfServerIoPumpItem* it = HsfServerIoPump_item(bank, i);
        char key[32];
        if (!it) continue;
        snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_PUMP_NAME);
        XBsonDocument_insert_keyUtf8_utf8(doc, key, it->m_name);
        snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_PUMP_ON);
        XBsonDocument_insert_keyUtf8_bool(doc, key, it->m_on);
        snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_PUMP_READY);
        XBsonDocument_insert_keyUtf8_bool(doc, key, it->m_do >= 0);
        snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_VALVE_VALID);
        XBsonDocument_insert_keyUtf8_bool(doc, key, it->m_valid);
        if (it->m_text[0]) {
            snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_PUMP_TEXT);
            XBsonDocument_insert_keyUtf8_utf8(doc, key, it->m_text);
        }
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief 进水泵快照请求（Response = 泵状态 BSON；未装配回否定应答）。
 */
static void hsfAppHandlePumpSnapshot(XLinkSession* session,
                                     const XLinkFrameHeader* frame)
{
    XByteArray* bytes = hsfAppEncodePump();
    if (!bytes) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "进水泵组未装配"));
        return;
    }
    XLinkSession_sendResponse_ex(session, frame->m_requestId, frame->m_msgCode,
                                 XLinkDataType_Bson,
                                 (const uint8_t*)XByteArray_constData(bytes),
                                 (uint32_t)XByteArray_size_base(bytes));
    XClassDelete((XClass*)bytes);
}

/**
 * @brief 进水泵控制（{"index":int,"on":bool}）。
 * @details 应答 = 包络 + 最新整帧快照键（复用一套解析，客户端不等下一拍
 *          就刷新卡片）；幂等"已处于该状态"按成功受理（err 文本随帧带
 *          err 键，客户端可提示但不判失败）。
 */
static void hsfAppHandlePumpControl(XLinkSession* session,
                                    const XLinkFrameHeader* frame,
                                    const uint8_t* payload, XLinkServer* srv)
{
    XBsonDocument* doc;
    XBsonValue* v;
    int index = -1;
    bool on = false;
    char err[96];
    bool ok;
    XByteArray* respBytes = NULL;

    if (!hsfAppControlGate(session, frame, srv)) return;

    doc = XBsonDocument_create();
    if (!doc || !XBsonDocument_from_bytes(doc, payload, frame->m_payloadLen)) {
        if (doc) XClassDelete((XClass*)doc);
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "控制载荷解析失败"));
        return;
    }
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_PUMP_INDEX);
    if (v) index = XBsonValue_toInt32(v, -1);
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_PUMP_ON);
    if (v) on = XBsonValue_toBool(v, false);
    XClassDelete((XClass*)doc);

    err[0] = '\0';
    ok = HsfServerIo_pumpSet(index, on, err, (int)sizeof(err));
    if (!ok) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, err[0] ? err : "进水泵控制被拒"));
        return;
    }

    /* 应答 = {ok, err?(幂等提示)} + 泵快照键（同一帧，客户端一次解析）。
     * [快照并帧] 快照编解码往返一次，逐元素拷进应答文档（BSON 无文档级
     * merge；元素迭代是公开 API，键名直接取自元素）。 */
    {
        XBsonDocument* resp = XBsonDocument_create();
        XByteArray* snap = hsfAppEncodePump();
        XBsonDocument* snapDoc = NULL;
        if (snap) {
            snapDoc = XBsonDocument_create();
            if (snapDoc) {
                if (!XBsonDocument_from_bytes(
                        snapDoc, (const uint8_t*)XByteArray_constData(snap),
                        XByteArray_size_base(snap))) {
                    XClassDelete((XClass*)snapDoc);
                    snapDoc = NULL;
                }
            }
            XClassDelete((XClass*)snap);
        }
        if (resp) {
            int64_t e;
            int64_t snapCount = snapDoc ? XBsonDocument_size_base(snapDoc) : 0;
            XBsonDocument_insert_keyUtf8_bool(resp, HSF_KEY_OK, true);
            if (err[0])
                XBsonDocument_insert_keyUtf8_utf8(resp, HSF_KEY_ERR, err);
            for (e = 0; e < snapCount; ++e) {
                const XBsonElement* el = XBsonDocument_at_const(snapDoc, e);
                const XString* k;
                const XBsonValue* val;
                if (!el) continue;
                k = XBsonElement_key_const(el);
                val = XBsonElement_value_const(el);
                if (k && val)
                    XBsonDocument_insert_keyUtf8_value(
                        resp, XString_constData(k), (XBsonValue*)val);
            }
            respBytes = XBsonDocument_toBson(resp);
            XClassDelete((XClass*)resp);
        }
        if (snapDoc) XClassDelete((XClass*)snapDoc);
    }
    if (!respBytes)
        respBytes = hsfAppMakeEnvelope(true, err[0] ? err : NULL);
    if (respBytes) {
        XLinkSession_sendResponse_ex(session, frame->m_requestId,
                                     frame->m_msgCode, XLinkDataType_Bson,
                                     (const uint8_t*)XByteArray_constData(respBytes),
                                     (uint32_t)XByteArray_size_base(respBytes));
        XClassDelete((XClass*)respBytes);
    }
}

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
        case HSF_CMD_TURBIDITY_SNAPSHOT:
            hsfAppHandleTurbiditySnapshot(session, frame, payload);
            break;
        case HSF_CMD_DIGITAL_SNAPSHOT:
            hsfAppHandleDigitalSnapshot(session, frame);
            break;
        case HSF_CMD_DIGITAL_CONTROL:
            hsfAppHandleDigitalControl(session, frame, payload, srv);
            break;
        case HSF_CMD_VALVE_SNAPSHOT:
            hsfAppHandleValveSnapshot(session, frame);
            break;
        case HSF_CMD_VALVE_CONTROL:
            hsfAppHandleValveControl(session, frame, payload, srv);
            break;
        case HSF_CMD_AUTO_CONTROL:
            hsfAppHandleAutoControl(session, frame, payload, srv);
            break;
        case HSF_CMD_PUMP_SNAPSHOT:
            hsfAppHandlePumpSnapshot(session, frame);
            break;
        case HSF_CMD_PUMP_CONTROL:
            hsfAppHandlePumpControl(session, frame, payload, srv);
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
                /* 其余遥测：走统一的"立即推一拍给该会话"路径
                 * （数字量/模拟量在该路径里按 full=true 补发，见
                 *  hsfAppPublishTopicNow）。 */
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
 * @brief 统计某账户当前在线会话数（排除 except）。
 * @note 登录策略判定的"同账并发"输入；人机界面连接数小，O(n) 足够。
 */
static uint32_t hsfAppSameAccountSessions(const XLinkSession* except,
                                          const char* user)
{
    uint32_t n = 0;
    int i;
    int total = s_srv ? XLinkServer_sessionCount(s_srv) : 0;
    if (!user || !user[0]) return 0;
    for (i = 0; i < total; ++i) {
        XLinkSession* s = XLinkServer_sessionAt(s_srv, i);
        const char* u;
        if (!s || (const XLinkSession*)s == except) continue;
        u = XLinkServer_sessionUser_const(s_srv, s);
        if (u && hsfAppUserIs(u, user)) ++n;
    }
    return n;
}

/**
 * @brief 服务端凭据查表（替换框架缺省查表；账户管理器为主源 + 登录策略闸）。
 * @param userData 注册时传入的会话自身（同账会话计数时排除自己）。
 * @return 见 XLinkSessionAuthLookupResult。
 * @details [记录主源=账户管理器] XAccountManager 持有同一格式的账户库，
 *          且承载运行期策略（失败锁定/到期解锁）。查表改从它读——XLink
 *          自带的文件副本只作启动兜底；否则 noteFailure 记的锁定/到期
 *          解锁不会反映到认证路径（两份副本漂移是最危险的静默故障）。
 *          [登录策略闸] 原"root 单会话"硬编码特例收编为 XLoginPolicy
 *          缺省策略（匿名关/同账并发关/上限 8），语义零漂移。拒绝走
 *          BadUser——框架按认证失败发 AuthResult(Rejected)+Goodbye
 *          (AuthFailed)，客户端收到干净的 authFailed（登录窗留在原地）。
 *          [KickOld 说明] 本批链路层无"服务端关单个会话"API，KickOld
 *          暂按拒绝处理并打日志（策略缺省 DenyNew，行为不受影响）。
 *          查表在会话 pump 栈上同步执行。
 */
static int hsfAppAuthLookup(void* userData, const char* user,
                            uint8_t saltOut[XLINK_AUTH_SALT_BYTES],
                            uint32_t* iterationsOut,
                            uint8_t hashOut[XLINK_AUTH_NONCE_BYTES],
                            uint8_t* roleOut)
{
    XLinkSession* self = (XLinkSession*)userData;
    const XLinkUserRecord* r;
    uint64_t now = (uint64_t)XDateTime_currentMSecsSinceEpoch();
    char reason[XLOGIN_POLICY_REASON_MAX];
    bool kick = false;
    uint32_t active;
    uint32_t same;

    (void)self;
    if (!user || !user[0])
        return XLinkSessionAuthLookup_BadUser;

    /* [到期解锁的惰性判定] 自动锁定到期此刻放行（顺便把持久锁定位清掉）。 */
    (void)XAccountManager_isLockedNow(&s_accounts, user, now);

    /* [登录策略闸] 总数上限 → 同账并发（匿名会话不走本查表——查表只处理
     * "报了用户名"的认证；匿名准入在链路层的匿名会话口径，本批全关）。 */
    active = (uint32_t)(s_srv ? XLinkServer_sessionCount(s_srv) : 0);
    same = hsfAppSameAccountSessions(self, user);
    if (!XLoginPolicy_admitSession(&s_loginPolicy, false, active, same,
                                   reason, (int)sizeof(reason), &kick)) {
        XPrintf("HsfServerApp: 登录被策略拒绝（%s：%s）\n", user,
                reason[0] ? reason : "未说明");
        return XLinkSessionAuthLookup_BadUser;
    }
    if (kick) {
        /* 本批无单会话关闭 API：按拒绝处理（见函数头说明）。 */
        XPrintf("HsfServerApp: KickOld 模式暂不支持（无单会话关闭 API），"
                "按拒绝处理：%s\n", user);
        return XLinkSessionAuthLookup_BadUser;
    }

    /* [凭据材料] 从账户管理器读（盐/迭代/摘要/角色，与 XLink 格式逐字节
     * 同构）；锁定态已含上面 isLockedNow 的到期判定结果。 */
    r = XAccountManager_find(&s_accounts, user);
    if (!r)
        return XLinkSessionAuthLookup_BadUser;
    if (XLinkUserStore_isLocked(r))
        return XLinkSessionAuthLookup_Locked;
    if (!XLinkUserStore_hasPassword(r))
        return XLinkSessionAuthLookup_BadUser;
    memcpy(saltOut, r->m_salt, XLINK_AUTH_SALT_BYTES);
    *iterationsOut = r->m_iterations;
    memcpy(hashOut, r->m_hash, XLINK_AUTH_NONCE_BYTES);
    *roleOut = r->m_role;
    return XLinkSessionAuthLookup_Ok;
}

/**
 * @brief 认证结果回灌槽：成功→清失败计数；失败→失败计数（达阈值自动锁定）。
 * @details 挂在每会话 authChanged 上（notify Added 时 connect）。用户名取
 *          会话的已认证名——失败时该名为空（认证没过），拿不到名字就不记
 *          （防止给空名/不存在的名字建计数表项；"用户名不存在"的失败本就
 *          不该喂给账户层）。
 */
static void hsfAppOnAuthChanged(XObject* sender, XVarList* args)
{
    XLinkSession* session = (XLinkSession*)sender;
    bool passed;
    const char* user;

    if (!args) return;
    XVarList_start(args);
    passed = XVarList_arg(args, bool);
    (void)XVarList_arg(args, uint8_t);   /* status */
    (void)XVarList_arg(args, uint8_t);   /* role */
    if (!passed || !s_srv || !session) return;
    user = XLinkServer_sessionUser_const(s_srv, session);
    if (!user || !user[0]) return;
    XAccountManager_noteSuccess(&s_accounts, user);
}

static void hsfAppNotify(XLinkServer* self, XLinkSession* session,
                         XLinkServerSessionEvent event)
{
    (void)self;
    if (event == XLinkServerSessionEvent_Added && session) {
        /* 替换框架缺省查表（accept 时挂的）为"账户管理器主源 + 登录策略
         * 闸"版本；userData 传会话自身，供同账会话计数排除自己。 */
        XLinkSession_setAuthLookup(session, hsfAppAuthLookup, session);
        /* 认证结果回灌：成功→清账户失败计数（防误锁定）。 */
        XObject_connect_2((XObject*)session,
                          XSignal(XLinkSession_authChanged_signal),
                          hsfAppOnAuthChanged);
    }
    /* [2026-11-05 协议顺序根修] 原先在此处（Added = 连接已接受、**尚未
     * 握手**）就直发一份设备健康快照。这违反"握手前不发业务帧"的顺序：
     * 客户端拿到第一帧是 Publish（class=3）而不是 HelloAck，握手逻辑
     * 直接判协议异常（外部按标准顺序写的客户端 100% 复现）。
     * 现在改为**只在收到订阅后补发**——订阅本身只在握手+认证之后才会
     * 发出，顺序天然正确；订阅补发路径见 dispatch 的 Subscribe 分支
     * （健康快照在那里单独处理，其余主题走 hsfAppPublishTopicNow）。 */
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
    HSF_PUSH_GATE_TURBIDITY,
    HSF_PUSH_GATE_AI,
    HSF_PUSH_GATE_DIGITAL,   /**< 数字量位图（无侧维度，用 [0]）。 */
    HSF_PUSH_GATE_VALVE,     /**< 电磁阀 6 路快照（无侧维度，用 [0]）。 */
    HSF_PUSH_GATE_PUMP,      /**< 进水泵快照（无侧维度，用 [0]）。 */
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
    const uint32_t tbMs = (HSF_TURBIDITY_PUSH_MS > 0)
                              ? (uint32_t)HSF_TURBIDITY_PUSH_MS : 0u;
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
        s_pushGates[HSF_PUSH_GATE_TURBIDITY][si].m_intervalMs = tbMs;
    }
    s_pushGates[HSF_PUSH_GATE_AI][0].m_intervalMs = aiMs;
    /* 数字量与电磁阀：跟随基准节拍（100ms）。这两块都是"人点了要立刻
     * 看到回显"的交互面，单独降频会让按下去的反馈显得迟滞。 */
    s_pushGates[HSF_PUSH_GATE_DIGITAL][0].m_intervalMs =
        (HSF_DIGITAL_PUSH_MS > 0) ? (uint32_t)HSF_DIGITAL_PUSH_MS : 0u;
    s_pushGates[HSF_PUSH_GATE_VALVE][0].m_intervalMs =
        (HSF_VALVE_PUSH_MS > 0) ? (uint32_t)HSF_VALVE_PUSH_MS : 0u;
    s_pushGates[HSF_PUSH_GATE_PUMP][0].m_intervalMs =
        (HSF_PUMP_PUSH_MS > 0) ? (uint32_t)HSF_PUMP_PUSH_MS : 0u;
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
 * @brief 发布浊度仪主题（全部侧各推一份，各自闸独立计时）。
 * @note 两台合成一帧：它们总是成对显示、成对刷新（见 HSF_KEY_TURB_COUNT 说明）。
 */
static void hsfAppTickTurbidity(XLinkServer* self)
{
    int si;
    const uint64_t nowMs = XDateTime_currentMSecsSinceEpoch();
    for (si = 0; si < (int)HsfSide_Count; ++si) {
        XBsonDocument* doc;
        XByteArray* bytes;
        if (!hsfAppPushDue(&s_pushGates[HSF_PUSH_GATE_TURBIDITY][si], nowMs))
            continue;
        doc = XBsonDocument_create();
        if (!doc) continue;
        hsfAppInsertSide(doc, (HsfSide)si);
        hsfAppFillTurbidity(doc, (HsfSide)si);
        bytes = XBsonDocument_toBson(doc);
        XClassDelete((XClass*)doc);
        if (bytes) {
            XLinkServer_publish_ex(self, HSF_TOPIC_TURBIDITY_STATUS,
                                   XLinkDataType_Bson,
                                   (const uint8_t*)XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes));
            XClassDelete((XClass*)bytes);
        }
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
    /* 周期推送只带数值（名字已由全量快照下发过）。 */
    XByteArray* bytes = hsfAppEncodeAi(false);
    if (!bytes) return;
    XLinkServer_publish_ex(self, HSF_TOPIC_AI_STATUS, XLinkDataType_Bson,
                           (const uint8_t*)XByteArray_constData(bytes),
                           (uint32_t)XByteArray_size_base(bytes));
    XClassDelete((XClass*)bytes);
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
    } else if ((topic & 0xFFFFu) == (HSF_TOPIC_TURBIDITY_STATUS & 0xFFFFu)) {
        XBsonDocument* doc = XBsonDocument_create();
        if (doc) {
            hsfAppInsertSide(doc, HsfSide_A);
            hsfAppFillTurbidity(doc, HsfSide_A);
            bytes = XBsonDocument_toBson(doc);
            XClassDelete((XClass*)doc);
        }
    } else if ((topic & 0xFFFFu) == (HSF_TOPIC_AI_STATUS & 0xFFFFu)) {
        /* 订阅补发：给**全量**（客户端刚订阅，还没拿过名字）。 */
        bytes = hsfAppEncodeAi(true);
    } else if ((topic & 0xFFFFu) == (HSF_TOPIC_DIGITAL_STATUS & 0xFFFFu)) {
        /* 订阅补发：给**全量**（含全部通道名）。客户端首连即拿全量，
         * 之后周期推送只带状态——这就是"名字只由服务端给"的落地方式。 */
        bytes = hsfAppEncodeDigital(true);
    } else if ((topic & 0xFFFFu) == (HSF_TOPIC_VALVE_STATUS & 0xFFFFu)) {
        bytes = hsfAppEncodeValves();
    }
    if (!bytes) return;
    XLinkSession_sendPublish_ex(session, topic, XLinkDataType_Bson,
                                (const uint8_t*)XByteArray_constData(bytes),
                                (uint32_t)XByteArray_size_base(bytes));
    XClassDelete((XClass*)bytes);
}

/**
 * @brief 生成数字量通道名（宏缺省 + 阀/泵点位覆盖）。
 * @details 与 Python 侧 `_digital_channel_names` / `_pump_channel_names` 同口径：
 *          阀 DO="<name> 控制"、开到位 DI="<name> 开到位"、关到位 DI=
 *          "<name> 关到位"；泵 DO="<name>"（不加后缀）。
 *          服务端是名字的唯一事实源，客户端只按帧更新。
 * @param diNames 输出 DI 名字数组（容量 HSF_DIG_CHANNELS_MAX）。
 * @param doNames 输出 DO 名字数组。
 * @param count 本次快照点数（≤ HSF_DIG_CHANNELS_MAX）。
 * @note 单线程事件循环内调用；静态缓冲避免每拍分配。
 */
static void hsfAppBuildDigitalNames(const char** diNames, const char** doNames,
                                    int count)
{
    static const char* const kDi[HSF_DIG_CHANNELS_MAX] = HSF_DIG_DI_NAMES;
    static const char* const kDo[HSF_DIG_CHANNELS_MAX] = HSF_DIG_DO_NAMES;
    static char s_diBuf[HSF_DIG_CHANNELS_MAX][96];
    static char s_doBuf[HSF_DIG_CHANNELS_MAX][96];
    const HsfServerIoValve* valves = HsfServerIo_valves();
    const HsfServerIoPump* pumps = HsfServerIo_pumps();
    int i;

    if (count > HSF_DIG_CHANNELS_MAX) count = HSF_DIG_CHANNELS_MAX;
    for (i = 0; i < count; ++i) {
        snprintf(s_diBuf[i], sizeof(s_diBuf[i]), "%s", kDi[i] ? kDi[i] : "");
        snprintf(s_doBuf[i], sizeof(s_doBuf[i]), "%s", kDo[i] ? kDo[i] : "");
        diNames[i] = s_diBuf[i];
        doNames[i] = s_doBuf[i];
    }
    if (valves) {
        int n = HsfServerIoValve_count(valves);
        for (i = 0; i < n; ++i) {
            const HsfServerIoValveItem* it = HsfServerIoValve_item(valves, i);
            if (!it) continue;
            if (it->m_do >= 0 && it->m_do < count)
                snprintf(s_doBuf[it->m_do], sizeof(s_doBuf[it->m_do]),
                         "%s 控制", it->m_name);
            if (it->m_openDi >= 0 && it->m_openDi < count)
                snprintf(s_diBuf[it->m_openDi], sizeof(s_diBuf[it->m_openDi]),
                         "%s 开到位", it->m_name);
            if (it->m_closeDi >= 0 && it->m_closeDi < count)
                snprintf(s_diBuf[it->m_closeDi], sizeof(s_diBuf[it->m_closeDi]),
                         "%s 关到位", it->m_name);
        }
    }
    if (pumps) {
        int n = HsfServerIoPump_count(pumps);
        for (i = 0; i < n; ++i) {
            const HsfServerIoPumpItem* it = HsfServerIoPump_item(pumps, i);
            if (it && it->m_do >= 0 && it->m_do < count)
                snprintf(s_doBuf[it->m_do], sizeof(s_doBuf[it->m_do]),
                         "%s", it->m_name);
        }
    }
}

/**
 * @brief 编码数字量快照（16DI/16DO 位图）。
 * @return 新建字节数组；未装配返回 NULL。@warning 调用方释放。
 */
static XByteArray* hsfAppEncodeDigital(bool full)
{
    XBsonDocument* doc;
    XByteArray* bytes;
    uint32_t di = 0;
    uint32_t doMask = 0;
    bool valid = false;
    int count;

    if (!HsfServerIo_digitalReady()) return NULL;
    HsfServerIo_digitalMasks(&di, &doMask, &valid);
    count = HsfServerIo_digitalCount();
    doc = XBsonDocument_create();
    if (!doc) return NULL;
    /* [full 标志] 客户端靠它区分"全量重建点表"与"只更新状态"，
     * 不靠"有没有名字键"猜（猜错会漏更新或重复建点）。 */
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_FULL, full);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_DIG_DEV, 0);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_DIG_DI, (int32_t)di);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_DIG_DO, (int32_t)doMask);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_DIG_COUNT, count);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_VALVE_VALID, valid);
    if (full) {
        /* 全量：把服务端通道名一并下发（名字的唯一事实源在服务端）。 */
        const char* diNames[HSF_DIG_CHANNELS_MAX];
        const char* doNames[HSF_DIG_CHANNELS_MAX];
        int i;
        hsfAppBuildDigitalNames(diNames, doNames, count);
        for (i = 0; i < count && i < HSF_DIG_CHANNELS_MAX; ++i) {
            char key[24];
            snprintf(key, sizeof(key), HSF_KEY_DIG_DI_NAME_FMT, i);
            if (diNames[i][0])
                XBsonDocument_insert_keyUtf8_utf8(doc, key, diNames[i]);
            snprintf(key, sizeof(key), HSF_KEY_DIG_DO_NAME_FMT, i);
            if (doNames[i][0])
                XBsonDocument_insert_keyUtf8_utf8(doc, key, doNames[i]);
        }
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief 名字专用帧（只含 count + 全部名字；用于名字变更主动下发）。
 * @note  客户端按"有名字键就更新名字"处理，与 full 帧的差别只是没有状态。
 */
static XByteArray* hsfAppEncodeDigitalNames(void)
{
    XBsonDocument* doc;
    XByteArray* bytes;
    const char* diNames[HSF_DIG_CHANNELS_MAX];
    const char* doNames[HSF_DIG_CHANNELS_MAX];
    int count;
    int i;

    if (!HsfServerIo_digitalReady()) return NULL;
    count = HsfServerIo_digitalCount();
    doc = XBsonDocument_create();
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_FULL, true);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_DIG_COUNT, count);
    hsfAppBuildDigitalNames(diNames, doNames, count);
    for (i = 0; i < count && i < HSF_DIG_CHANNELS_MAX; ++i) {
        char key[24];
        snprintf(key, sizeof(key), HSF_KEY_DIG_DI_NAME_FMT, i);
        if (diNames[i][0])
            XBsonDocument_insert_keyUtf8_utf8(doc, key, diNames[i]);
        snprintf(key, sizeof(key), HSF_KEY_DIG_DO_NAME_FMT, i);
        if (doNames[i][0])
            XBsonDocument_insert_keyUtf8_utf8(doc, key, doNames[i]);
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief 编码电磁阀 6 路快照（v<i>_* 前缀键表）。
 * @return 新建字节数组；阀组未装配返回 NULL。@warning 调用方释放。
 *
 * @note [键名格式] 与 Python 侧 ValveBank.to_doc 逐字一致：
 *       count + v<i>_name / v<i>_openCmd / v<i>_openFb / v<i>_closeFb /
 *       v<i>_state / v<i>_ready / v<i>_valid / v<i>_text。
 */
static XByteArray* hsfAppEncodeValves(void)
{
    const HsfServerIoValve* bank = HsfServerIo_valves();
    XBsonDocument* doc;
    XByteArray* bytes;
    int count;
    int i;

    if (!bank) return NULL;
    count = HsfServerIoValve_count(bank);
    doc = XBsonDocument_create();
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_VALVE_COUNT, count);
    for (i = 0; i < count; ++i) {
        const HsfServerIoValveItem* it = HsfServerIoValve_item(bank, i);
        bool ready;
        char key[32];
        if (!it) continue;
        ready = it->m_do >= 0 && it->m_openDi >= 0 && it->m_closeDi >= 0;
        snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_VALVE_NAME);
        XBsonDocument_insert_keyUtf8_utf8(doc, key, it->m_name);
        snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_VALVE_OPEN_CMD);
        XBsonDocument_insert_keyUtf8_bool(doc, key, it->m_openCmd);
        snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_VALVE_OPEN_FB);
        XBsonDocument_insert_keyUtf8_bool(doc, key, it->m_openFb);
        snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_VALVE_CLOSE_FB);
        XBsonDocument_insert_keyUtf8_bool(doc, key, it->m_closeFb);
        snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_VALVE_STATE);
        XBsonDocument_insert_keyUtf8_int32(doc, key, it->m_state);
        snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_VALVE_READY);
        XBsonDocument_insert_keyUtf8_bool(doc, key, ready);
        snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_VALVE_VALID);
        XBsonDocument_insert_keyUtf8_bool(doc, key, it->m_valid);
        if (it->m_text[0]) {
            snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_VALVE_TEXT);
            XBsonDocument_insert_keyUtf8_utf8(doc, key, it->m_text);
        }
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief 阀告警推送：把本拍新产生的告警逐条推给**所有已连接会话**。
 * @details [为什么全推而不按订阅] 阀告警是**事件**，不是遥测。只看数字量页/
 *          主界面的客户端没订阅阀主题，但"某路阀超时未到位"这类信息必须让它
 *          看到——现场最常见的失误就是告警藏在没人订阅的主题里。故这里不判
 *          订阅，直接广播（XLinkServer_publish_ex 按订阅投递；无人订阅时静默
 *          返回 0，不视为错误）。
 *
 *          [为什么要"取走"而不是"读状态"] 告警每拍都可能成立（超时判据是
 *          `now - cmdAt > timeout`），若按状态推就会每 200ms 一帧把客户端刷屏。
 *          取走语义保证一条告警只出一次帧。
 */
static void hsfAppPushValveAlarms(XLinkServer* self, uint64_t now)
{
    int i;
    int count = HsfServerIoValve_count(HsfServerIo_valves());

    for (i = 0; i < count; ++i) {
        int kind = 0;
        char text[160];
        const HsfServerIoValveItem* it;
        XBsonDocument* doc;

        if (!HsfServerIo_valveTakeAlarm(i, &kind, text, (int)sizeof(text)))
            continue;
        it = HsfServerIoValve_item(HsfServerIo_valves(), i);
        if (!it) continue;

        doc = XBsonDocument_create();
        if (!doc) continue;
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_ALARM_COUNT, 1);
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_ALARM_VALVE_INDEX, i);
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_ALARM_VALVE_NAME,
                                          it->m_name);
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_ALARM_KIND, kind);
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_ALARM_TEXT, text);
        XBsonDocument_insert_keyUtf8_int64(doc, HSF_KEY_ALARM_AT,
                                           (int64_t)now);
        if (kind == HsfValveAlarmKind_Timeout) {
            XBsonDocument_insert_keyUtf8_double(
                doc, HSF_KEY_ALARM_TIMEOUT_S,
                (double)HsfServerIoValve_moveTimeoutMs(HsfServerIo_valves())
                    / 1000.0);
        }
        {
            XByteArray* bytes = XBsonDocument_toBson(doc);
            if (bytes) {
                XLinkServer_publish_ex(self, HSF_TOPIC_VALVE_ALARM,
                                       XLinkDataType_Bson,
                                       (const uint8_t*)XByteArray_constData(bytes),
                                       (uint32_t)XByteArray_size_base(bytes));
                XClassDelete((XClass*)bytes);
            }
        }
        XClassDelete((XClass*)doc);
        XPrintf("HsfServerApp: 阀告警 路%d(%s) 类型%d：%s\n",
                i, it->m_name, kind, text);
    }
}

/**
 * @brief 数字量/电磁阀推送拍（每拍刷新阀组状态再推两主题）。
 * @note  阀状态依赖 DI 反馈 ⇒ **先 poll 再编码**，保证同一拍数据自洽
 *        （与 Python 侧 tick 的"先 refresh_valve_sources 再 encode"同款）。
 */
static void hsfAppTickDigitalValve(XLinkServer* self)
{
    uint64_t now = XDateTime_currentMSecsSinceEpoch();

    /* 阀组状态判读（内部读 DI 位图；未装配时把 valid 置 false）。 */
    HsfServerIo_valvePoll(now);

    /* 先推告警（事件，一条一次），再推周期状态。 */
    hsfAppPushValveAlarms(self, now);

    /* 名字变更：主动下发一帧只含名字的全量帧（见 HsfServerApp_namesChanged）。 */
    if (s_digitalNamesDirty) {
        XByteArray* nb = hsfAppEncodeDigitalNames();
        if (nb) {
            XLinkServer_publish_ex(self, HSF_TOPIC_DIGITAL_STATUS,
                                   XLinkDataType_Bson,
                                   (const uint8_t*)XByteArray_constData(nb),
                                   (uint32_t)XByteArray_size_base(nb));
            XClassDelete((XClass*)nb);
            s_digitalNamesDirty = false;
            XPrintf("HsfServerApp: 通道名已变更，已主动下发\n");
        }
    }
    if (hsfAppPushDue(&s_pushGates[HSF_PUSH_GATE_DIGITAL][0], now)) {
        XByteArray* bytes = hsfAppEncodeDigital(false);  /* 周期推送只带状态。 */
        if (bytes) {
            XLinkServer_publish_ex(self, HSF_TOPIC_DIGITAL_STATUS,
                                   XLinkDataType_Bson,
                                   (const uint8_t*)XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes));
            XClassDelete((XClass*)bytes);
        }
    }
    if (hsfAppPushDue(&s_pushGates[HSF_PUSH_GATE_VALVE][0], now)) {
        XByteArray* bytes = hsfAppEncodeValves();
        if (bytes) {
            XLinkServer_publish_ex(self, HSF_TOPIC_VALVE_STATUS,
                                   XLinkDataType_Bson,
                                   (const uint8_t*)XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes));
            XClassDelete((XClass*)bytes);
        }
    }
}

/**
 * @brief 自动运行告警抽取：取走控制器在队报警，逐条广播给订阅会话。
 * @details [为什么走 XLink 推送] 自动运行的异常（动作失败/设备不可用/
 *          逻辑自定义）远端必须能看见——远程管理的闭环是"订阅告警主题 +
 *          AUTO_CONTROL 启停"。取走即消费：一条报警只出一次帧，与阀告警
 *          同款防刷屏口径。无人订阅时 publish 静默为 0，事件不丢（帧内容
 *          已打日志留痕）。
 */
static void hsfAppPushAutoAlarms(XLinkServer* self, uint64_t now)
{
    XAutoControlAlarm alarm;
    while (XAutoController_takeAlarm(&s_autoCtl, &alarm)) {
        XByteArray* bytes = XAutoControlCodec_alarmToBson(&alarm);
        if (bytes) {
            XLinkServer_publish_ex(self, HSF_TOPIC_AUTO_ALARM,
                                   XLinkDataType_Bson,
                                   (const uint8_t*)XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes));
            XClassDelete((XClass*)bytes);
        }
        XPrintf("HsfServerApp: 自动运行报警 类型%d @%llu：%s\n",
                alarm.m_kind, (unsigned long long)alarm.m_atMs, alarm.m_text);
    }
    (void)now;
}

/**
 * @brief 进水泵推送拍（先 poll（回显校准/外部改动接管）再按闸推送）。
 * @note  外部改动留痕：poll 发现回显翻转时 m_text 带一次性说明，本拍打
 *        日志（泵没有告警面——无反馈就没有可比对的真相源，见
 *        HsfServerIoPump.h）。
 */
static void hsfAppTickPump(XLinkServer* self)
{
    const HsfServerIoPump* bank = HsfServerIo_pumps();
    int count = HsfServerIoPump_count(bank);
    int i;
    uint64_t now = XDateTime_currentMSecsSinceEpoch();

    /* 先 poll：DO 回显校准 + 外部改动接管（未装配时内部置 valid=false）。 */
    HsfServerIo_pumpPoll();

    /* 外部改动留痕（一次性文本出现即打；下一拍状态一致后清掉不再刷屏）。 */
    for (i = 0; i < count; ++i) {
        const HsfServerIoPumpItem* it = HsfServerIoPump_item(bank, i);
        if (it && it->m_text[0])
            XPrintf("HsfServerApp: 进水泵 %s（%d）%s\n", it->m_name, i, it->m_text);
    }

    if (hsfAppPushDue(&s_pushGates[HSF_PUSH_GATE_PUMP][0], now)) {
        XByteArray* bytes = hsfAppEncodePump();
        if (bytes) {
            XLinkServer_publish_ex(self, HSF_TOPIC_PUMP_STATUS,
                                   XLinkDataType_Bson,
                                   (const uint8_t*)XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes));
            XClassDelete((XClass*)bytes);
        }
    }
}

static void hsfAppTick(XLinkServer* self, void* userData)
{
    /* 设备健康：事件驱动巡检（变化才推）。 */
    hsfHealthTick(self);

    /* 点动手持看门狗：超时自动停机（安全兜底）。 */
    hsfAppJogWatchdog();

    (void)userData;

    /* 自动运行控制器：只在自动模式下步进（手动模式 step 闸门也会拦，
     * 这里显式判一次省得空转）；告警抽取与模式无关（停机也可能有在队
     * 报警要清空——stop 保留在队事件，见 XAutoController_stop 契约）。 */
    if (s_autoCtlReady) {
        uint64_t nowAuto = (uint64_t)XDateTime_currentMSecsSinceEpoch();
        if (s_runMode == HsfRunMode_Auto)
            XAutoController_step(&s_autoCtl, nowAuto);
        hsfAppPushAutoAlarms(self, nowAuto);
    }

    hsfAppTickVfd(self);
    hsfAppTickTempHum(self);
    hsfAppTickFlowMeter(self);
    hsfAppTickTurbidity(self);
    hsfAppTickDigitalValve(self);   /* 数字量 + 电磁阀（先判读后编码）。 */
    hsfAppTickPump(self);           /* 进水泵（先 poll 后编码）。 */
    /* 模拟量无侧维度，单闸。 */
    if (hsfAppPushDue(&s_pushGates[HSF_PUSH_GATE_AI][0],
                      XDateTime_currentMSecsSinceEpoch()))
        hsfAppTickAi(self);
}

/* ==================== 账户库 ==================== */

/**
 * @brief 权限拒绝留痕槽（XPermPolicy.permissionDenied → 日志）。
 * @details 约束 5"拒绝必须留痕"的服务端落点：本批落日志（含角色/权限/
 *          时刻），推远端走扩展位。s_perm 是静态对象，NULL 会话照记。
 */
static void hsfAppOnPermDenied(XObject* sender, XVarList* args)
{
    int role = -1;
    int32_t perm = 0;
    int64_t atMs = 0;
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    role = XVarList_arg(args, int);
    perm = XVarList_arg(args, int32_t);
    atMs = XVarList_arg(args, int64_t);
    XPrintf("HsfServerApp: 权限拒绝 角色%d 权限0x%X @%llu\n",
            role, (unsigned)perm, (unsigned long long)atMs);
}

/**
 * @brief 登录被拒留痕槽（XLoginPolicy.loginRejected → 日志）。
 * @details 与权限拒绝同一审计口径；原因已由 authLookup 打过带用户名的
 *          日志，这里只补一条事件级留痕（方便与 access 日志对账）。
 */
static void hsfAppOnLoginRejected(XObject* sender, XVarList* args)
{
    const char* name = "";
    const char* reason = "";
    int64_t atMs = 0;
    (void)sender;
    if (!args) return;
    XVarList_start(args);
    name = XVarList_arg(args, const char*);
    reason = XVarList_arg(args, const char*);
    atMs = XVarList_arg(args, int64_t);
    XPrintf("HsfServerApp: 登录拒绝留痕（%s：%s @%llu）\n",
            name ? name : "", reason ? reason : "",
            (unsigned long long)atMs);
}

/**
 * @brief 装配访问控制三件（账户/权限/登录策略）+ 账户库引导落盘。
 * @param srv XLink 服务端（账户文件路径同步给它作启动兜底；可为 NULL）。
 * @return 账户库可用返回 true（失败仍继续：登录链路可先联调）。
 * @details 装配顺序：
 *            1) 账户库路径 = <数据目录>/hsf_users.json（与旧版同文件，
 *               格式逐字节同构，现有部署零迁移）；
 *            2) load → 空库 ensureDefaults（admin/admin 只读 + root/root
 *               管理，只建一次）→ save（原子写）；
 *            3) XLinkServer_setUserStorePath 同步文件（框架内部缺省查表的
 *               兜底源；实际认证走 hsfAppAuthLookup=账户管理器主源）；
 *            4) 权限策略缺省表（None=无/User=只读/Admin=全权），拒绝留痕
 *               开（→ 日志）；
 *            5) 登录策略缺省（匿名关/同账并发关/上限 8/DenyNew）——即旧
 *               "root 单会话"语义收编；策略落盘 hsf_login_policy.json。
 *          [本批边界] 远端账户/策略管理命令面（CMD_ACCOUNT_* / 策略下发）
 *          为扩展位未实现；本地变更直接改 JSON 或后续批次上命令面。
 */
static bool hsfAppAccessCtlSetup(XLinkServer* srv)
{
    char path[600];
    char policyPath[600];
    const char* dir = HsfServerSettings_dataDir();
    bool ok;

    if (!dir || !dir[0]) dir = ".";
    snprintf(path, sizeof(path), "%s/hsf_users.json", dir);
    snprintf(policyPath, sizeof(policyPath), "%s/hsf_login_policy.json", dir);

    /* ---- 账户管理器 ---- */
    XAccountManager_init(&s_accounts);
    XAccountManager_setPath(&s_accounts, path);
    ok = XAccountManager_load(&s_accounts);
    if (!ok) {
        XPrintf("HsfServerApp: 账户库加载失败（%s）——按空库继续\n\n", path);
    }
    if (XAccountManager_ensureDefaults(&s_accounts)) {
        XPrintf("HsfServerApp: 缺省账户已创建（%s）：admin/admin 只读、"
                "root/root 可改（请尽快改密）\n\n", path);
    } else {
        XPrintf("HsfServerApp: 账户库已加载（%s，%u 账户）\n\n", path,
                (unsigned)XAccountManager_count(&s_accounts));
    }
    if (!XAccountManager_save(&s_accounts)) {
        XPrintf("HsfServerApp: 账户库落盘失败（%s）——本次仍可用\n\n", path);
    }
    if (srv) XLinkServer_setUserStorePath(srv, path);

    /* ---- 权限策略：缺省表 + 拒绝留痕 ---- */
    XPermPolicy_init(&s_perm);
    XObject_connect_2((XObject*)&s_perm,
                      XSignal(XPermPolicy_permissionDenied_signal),
                      hsfAppOnPermDenied);

    /* ---- 登录策略：缺省（匿名关/并发关/上限8/DenyNew）+ 留痕 + 落盘 ---- */
    XLoginPolicy_init(&s_loginPolicy);
    XLoginPolicy_setPath(&s_loginPolicy, policyPath);
    XLoginPolicy_load(&s_loginPolicy);       /* 不存在=保持缺省。 */
    XObject_connect_2((XObject*)&s_loginPolicy,
                      XSignal(XLoginPolicy_loginRejected_signal),
                      hsfAppOnLoginRejected);
    XPrintf("HsfServerApp: 登录策略（匿名=%d 同账并发=%d 上限=%u 模式=%d，%s）\n\n",
            XLoginPolicy_anonymousAllowed(&s_loginPolicy) ? 1 : 0,
            XLoginPolicy_concurrentAllowed(&s_loginPolicy) ? 1 : 0,
            (unsigned)XLoginPolicy_maxSessions(&s_loginPolicy),
            (int)XLoginPolicy_concurrencyMode(&s_loginPolicy),
            policyPath);
    return ok;
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
    if (!hsfAppAccessCtlSetup(s_srv)) {
        XPrintf("HsfServerApp: 账户库不可用（继续启动，登录链路可先联调）\n");
    }
    XLinkServer_setAuthEnabled(s_srv, true);
    if (!XLinkServer_listen(s_srv, NULL, port)) {
        XPrintf("HsfServerApp: 监听 %u 端口失败\n", (unsigned)port);
        XClassDelete((XClass*)s_srv);
        s_srv = NULL;
        return false;
    }
    /* 恢复持久化的运行模式，并同步到设备控制闸。控制器**一律保持停止**：
     * 重启 = 没人按过启动，设备不能自己动（模式位只是授权态）。 */
    s_runMode = (HsfServerSettings_getInt("run/mode", 0) == (int)HsfRunMode_Auto)
                    ? HsfRunMode_Auto : HsfRunMode_Manual;
    hsfAppApplyModeToDevices();
    hsfAppAutoCtlSetup();
    XPrintf("HsfServerApp: 运行模式 = %s（自动运行 %s）\n",
            s_runMode == HsfRunMode_Auto ? "自动（远程控制禁用）" : "手动",
            XAutoController_running(&s_autoCtl) ? "运行中" : "停止");

    XPrintf("HsfServerApp: XLink 服务端就绪（端口 %u，鉴权开，"
            "遥测 %dms）\n", (unsigned)port, HSF_TELEMETRY_MS);
    return true;
}

void HsfServerApp_stop(void)
{
    if (!s_srv) return;
    /* 控制器强制停（服务端关闭不该留下"还在自动跑"的状态），随后整体
     * 回到未装配态——下次 start 重新走 hsfAppAutoCtlSetup。 */
    if (s_autoCtlReady) {
        XAutoController_stop(&s_autoCtl,
                             (uint64_t)XDateTime_currentMSecsSinceEpoch());
        XAutoController_setStepHandler(&s_autoCtl, NULL, NULL);
    }
    XLinkServer_close(s_srv);
    XClassDelete((XClass*)s_srv);
    s_srv = NULL;
}

int HsfServerApp_sessionCount(void)
{
    return s_srv ? XLinkServer_sessionCount(s_srv) : 0;
}
