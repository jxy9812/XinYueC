
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
#include "io/HsfServerIoSim.h"
#include "HsfServerStore.h"
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
#include "XTurbidityMeter.h" /* XTurbidityMeter_staleMs（陈旧阈值回读）。 */
#include "XFlowMeter.h"   /* XFlowMeterAlarm_System（流量计告警位定义）。 */
#include "XBson.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XBsonArray.h"
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
#include "XSystem.h"    /* XSystem_environment（压测诊断开关）。 */
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
/** @brief SQLite 落盘层（历史采样 + 告警事件；storage/enabled 控制）。 */
static HsfServerStore s_store;

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

/* 权限门辅助函数原型（定义在各命令处理器之后；处理器内先用先声明）。 */
static const char* hsfAppPermDeniedText(uint32_t perm);
static bool hsfAppSessionCan(XLinkServer* srv, XLinkSession* session,
                             uint32_t perm);
static uint32_t hsfAppCommandPerm(uint16_t cmd);
/* 关键运维事件（终端留痕 + logs 表入库；定义在 hsfAppRespond 之后，
 * HsfServerApp_setRunMode 等早于定义点的调用经本原型前置）。 */
static void hsfAppEventLog(const char* category, const char* text);

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
    char     m_addr[64];    /**< 连接地址文案（给人看；空=无连接概念）。 */
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
static bool hsfAppFillValvesDoc(XBsonDocument* doc);

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
/**
 * @brief 485 设备地址文案："COM17 · 115200 · 站 2"。
 * @details [为什么三个都要写] 现场排故障时这三个缺一不可：口接错、波特率
 *          不匹配、站号撞车，现象都是"通讯不上"，只看 IP 或只看"某口"
 *          根本判不出来。地址是操作员唯一能看到它们的地方（对齐 Python
 *          侧 _addr_rtu）。
 */
static void hsfAddrRtu(char* buf, int cap, const char* port, int baud,
                        int unit)
{
    if (!buf || cap <= 0) return;
    snprintf(buf, (size_t)cap, "%s \xC2\xB7 %d \xC2\xB7 \xE7\xAB\x99 %d",
             port ? port : "?", baud, unit);
}

/** @brief TCP 设备地址文案："192.168.0.13:502"。 */
static void hsfAddrTcp(char* buf, int cap, const char* host, int port)
{
    if (!buf || cap <= 0) return;
    snprintf(buf, (size_t)cap, "%s:%d", host ? host : "?", port);
}

static void hsfHealthAdd(HsfAppHealthState* st, const char* name, int kind,
                         int side, int health, int error, bool online,
                         int runState, const char* text, const char* addr)
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
    hsfHealthCopy(e->m_addr, sizeof(e->m_addr), addr ? addr : "");
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
            /* [与 Python 对齐] 首页设备表只列**真实设备**，不含 485 总线
             * 条目（Python collect_health 同样不列）：总线是链路不是设备，
             * 混进设备表会让"设备数量"与现场对不上。 */
            (void)name;
            (void)sideName;
            continue;
        }

        /* ---- 变频器 ---- */
        vfd = HsfServerIo_vfdDevice(side);
        if (vfd) {
            const XVfdStatus* vs = XVfdDevice_status_const(vfd);
            bool online = HsfServerIo_vfdOnline(side);
            int health = HsfDeviceHealth_Offline;
            int err = 0;
            const char* text = "";
            char addr[64];
            hsfAddrRtu(addr, (int)sizeof(addr),
                       (side == HsfSide_A) ? HSF_BUS_A_PORT : HSF_BUS_B_PORT,
                       (side == HsfSide_A) ? HSF_BUS_A_BAUDRATE : HSF_BUS_B_BAUDRATE,
                       HSF_VFD_UNIT_ID);
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
            /* [名字按工艺用途] 现场驱动的是搅拌泵，操作员按"搅拌泵"找设备；
             * 类别仍是 Vfd（"变频器"）——名字给操作员、类别给维修，两者不混
             * （对齐 Python 侧 collect_health 的"搅拌泵"+KIND_VFD）。 */
            hsfHealthAdd(st, "搅拌泵", HsfDeviceKind_Vfd, (int)side, health,
                         err, online,
                         (online && vs) ? (int)vs->m_runState : 0, text,
                         addr);
        }

        /* ---- 温湿度 ---- */
        th = HsfServerIo_tempHum(side);
        if (th) {
            int32_t t = 0, h = 0;
            bool tv = false, hv = false;
            char addr[64];
            (void)HsfServerIoTempHum_values(th, &t, &h, &tv, &hv);
            hsfAddrRtu(addr, (int)sizeof(addr),
                       (side == HsfSide_A) ? HSF_BUS_A_PORT : HSF_BUS_B_PORT,
                       (side == HsfSide_A) ? HSF_BUS_A_BAUDRATE : HSF_BUS_B_BAUDRATE,
                       HSF_TEMPHUM_UNIT_ID);
            hsfHealthAdd(st, "温湿度", HsfDeviceKind_TempHum, (int)side,
                         tv || hv ? HsfDeviceHealth_Ok : HsfDeviceHealth_Stale,
                         0, tv || hv, 0, tv || hv ? "" : "无有效数据", addr);
        }

        /* ---- 流量计 ---- */
        fm = HsfServerIo_flowMeter(side);
        if (fm) {
            double flow = 0, speed = 0, fwd = 0, rev = 0;
            int fu = 0, tu = 0, alarms = 0;
            bool valid = false;
            char addr[64];
            (void)HsfServerIoFlowMeter_values(fm, &flow, &speed, &fwd, &rev,
                                              &fu, &tu, &alarms, &valid);
            hsfAddrRtu(addr, (int)sizeof(addr),
                       (side == HsfSide_A) ? HSF_BUS_A_PORT : HSF_BUS_B_PORT,
                       (side == HsfSide_A) ? HSF_BUS_A_BAUDRATE : HSF_BUS_B_BAUDRATE,
                       HSF_FLOWMETER_UNIT_ID);
            /* [告警口径对齐 Python 2026-11-10] 四个报警位打包成一个掩码
             * （0=上限 1=下限 2=空管 3=系统），只有**系统报警**算设备告警；
             * 其余三位是工况信息——「空管」在泵停/无流时必然置位，早先
             * 「alarms != 0 ⇒ 故障」把首页显示成「电磁流量计 故障」
             * （板端实测 alarms=4 即空管位，流量 0.0 属正常停机态）。
             * Python 侧同口径：仅系统报警 ⇒ HEALTH_WARNING、err=0、
             * 文本「流量计系统报警」。位定义见 XFlowMeter_alarms()。 */
            const bool systemAlarm =
                (alarms & (int)XFlowMeterAlarm_System) != 0;
            hsfHealthAdd(st, "电磁流量计", HsfDeviceKind_FlowMeter, (int)side,
                         HsfServerIoFlowMeter_health(alarms, valid),
                         0, valid || alarms != 0, 0,
                         systemAlarm ? "流量计系统报警"
                                     : (valid ? "" : "无有效数据"),
                         addr);
        }

        /* ---- 浊度仪 ×2（逐台一条）----
         * [现场接法] 两条 485 总线：
         *   · 485-A：变频器 + 温湿度
         *   · 485-B：流量计 + 浊度仪 ×2（站 1/2，与流量计同一条总线；
         *     2026-11-09 板端普查定案，站号定义在 HsfServerConfig.h）
         * 全系统**总共就 2 台浊度仪**，配置里也只有 B 侧挂了
         * （HSF_BUS_B_HAS_TURBIDITY=1 / HSF_BUS_A_HAS_TURBIDITY=0）。
         *
         * [设备名不带 -A/-B] 侧号是**总线**的属性，不是设备的属性。现场
         * 只有两台表，名字就该是"浊度仪1/浊度仪2"；早先每台都拼侧标，
         * 加上 A 侧未装配还硬凑两条占位，首页变成 4 台（截图反馈：
         * 「浊度仪怎么多了两个我总共就两个」）。
         * 侧标只保留在"485-A/B 总线"那两条上 —— 那是两条真实存在的
         * 物理链路，操作员排查接线时确实需要区分。
         *
         * [只在该侧真的挂了才出条目] 与变频器/温湿度/流量计同一口径：
         * 该侧没挂就不出条目，别用"未装配"占位条目冒充设备。
         * [类别必须是"水质"而不是"温湿度"] 类别要表达"这是什么设备"。
         * [逐台而不是汇总] 两台独立接线，一台坏不能看起来像整组坏。 */
        {
            const bool sideHasTurbidity =
                (side == HsfSide_A) ? (HSF_BUS_A_HAS_TURBIDITY != 0)
                                    : (HSF_BUS_B_HAS_TURBIDITY != 0);
            int t;
            for (t = 0; sideHasTurbidity && t < HSF_TURBIDITY_COUNT; ++t) {
                const HsfServerIoTurbidity* tm = HsfServerIo_turbidity(side, t);
                char addr[64];
                snprintf(name, sizeof(name), "浊度仪%d", t + 1);
                hsfAddrRtu(addr, (int)sizeof(addr),
                           (side == HsfSide_A) ? HSF_BUS_A_PORT : HSF_BUS_B_PORT,
                           (side == HsfSide_A) ? HSF_BUS_A_BAUDRATE : HSF_BUS_B_BAUDRATE,
                           (t == 0) ? HSF_TURBIDITY1_UNIT_ID
                                    : HSF_TURBIDITY2_UNIT_ID);
                if (!tm) {
                    hsfHealthAdd(st, name, HSF_DEVICE_KIND_WATER, (int)side,
                                 HsfDeviceHealth_Offline, 0, false, 0, "未装配",
                                 addr);
                    continue;
                }
                {
                    bool online = HsfServerIoTurbidity_isOnline(tm);
                    hsfHealthAdd(st, name, HSF_DEVICE_KIND_WATER, (int)side,
                                 online ? HsfDeviceHealth_Ok
                                        : HsfDeviceHealth_Stale,
                                 0, online, 0, online ? "" : "无有效数据", addr);
                }
            }
        }
    }

    /* ---- 进水泵（数字量模块上的一路 DO，归类为数字量） ----
     * [与 Python 侧 pumps.health_summary 同口径] 无数字量模块→离线；
     * 未配置点位→离线；有配置但回显无效→陈旧；否则正常。
     * 泵没有独立通讯口，side 记数字量模块所在侧(A)，客户端按
     * (kind=DIGITAL, side=0) 找到它。
     * [顺序 2026-11-10 对齐 Python] 设备表行序与 Python 服务端一致
     * （进水泵 → 模拟量模块 → 按钮组 → 数字量 → 电动球阀组）：两端首页
     * 设备表逐行对齐，现场对台数/对行不必换算。 */
    {
        const HsfServerIoPump* pumps = HsfServerIo_pumps();
        if (pumps && HsfServerIoPump_count(pumps) > 0) {
            const HsfServerIoPumpItem* item = HsfServerIoPump_item(pumps, 0);
            int health = HsfDeviceHealth_Ok;
            bool online = true;
            const char* text = "";
            char addr[64];
            /* [泵的地址 = 它挂在哪个数字量模块的哪一路 DO] 泵没有独立通讯口
             * （接触器线圈接在模块 DO 上），报"站号"会让人去总线上找一个
             * 不存在的从站；报模块地址 + DO 口，现场照着量那一路就对了。 */
            snprintf(addr, sizeof(addr), "%s:%d \xC2\xB7 DO%d",
                     HSF_DIG_HOST, HSF_DIG_TCP_PORT, HSF_PUMP_DO(0));
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
                         (int)HsfSide_A, health, 0, online, 0, text, addr);
        }
    }

    /* ---- 按钮组（现场操作柱）----
     * [为什么必须有一行] 按钮组是**现场设备**（电箱上的操作柱），首页
     * 设备表按台数清点；缺这一行 ⇒ C 端 9 台、Python 端 10 台，现场对
     * 数量时没法解释。与 Python 侧 app.py 的 _button_health 同口径：
     *   · 数字量模块离线 ⇒ 离线（按键与灯均不可用）；
     *   · 急停触点无信号（DI=0）⇒ **故障**（被按下或断线，禁止启动）；
     *   · 否则按启动灯给"运行中/停止"。
     * [急停无信号为什么算故障] 急停是安全件：无信号时不能启动，报故障
     * 才能让人看见（Python 侧同口径：estop 无信号 ⇒ HEALTH_FAULT）。 */
    {
        uint32_t di = 0;
        uint32_t doMask = 0;
        bool valid = false;
        int health = HsfDeviceHealth_Ok;
        bool online = true;
        const char* text = "";
        char addr[96];
        snprintf(addr, sizeof(addr),
                 "%s:%d \xC2\xB7 DI%d~DI%d \xC2\xB7 DO%d~DO%d",
                 HSF_DIG_HOST, HSF_DIG_TCP_PORT,
                 HSF_BTN_ESTOP_DI, HSF_BTN_RESET_DI,
                 HSF_BTN_START_LAMP_DO, HSF_BTN_RESET_LAMP_DO);
        if (!HsfServerIo_digitalReady()) {
            health = HsfDeviceHealth_Offline;
            online = false;
            text = "数字量模块离线，按键与灯均不可用";
        } else {
            (void)HsfServerIo_digitalMasks(&di, &doMask, &valid);
            if (!valid) {
                health = HsfDeviceHealth_Stale;
                text = "无有效数据";
            } else if (!(di & (1u << HSF_BTN_ESTOP_DI))) {
                health = HsfDeviceHealth_Fault;
                text = "急停无信号（按下或断线）：禁止启动";
            } else if (doMask & (1u << HSF_BTN_START_LAMP_DO)) {
                text = "运行中（启动灯亮、停止灯灭）";
            } else {
                text = "停止（停止灯亮、启动灯灭）";
            }
        }
        hsfHealthAdd(st, "按钮组", HSF_DEVICE_KIND_DIGITAL,
                     (int)HsfSide_A, health, 0, online, 0, text, addr);
    }

    /* ---- 模拟量模块（TCP，单台） ----
     * [位置 2026-11-10 对齐 Python] 设备表行序与 Python 服务端逐行一致
     * （…进水泵 → 按钮组 → 模拟量模块 → 数字量 → 电动球阀组）：两端首页
     * 设备表逐行对齐，现场对台数/对行不必换算。 */
    {
        double ma[8];
        bool valid[8];
        char addr[64];
        int n = HsfServerIo_aiValues(ma, valid, 8);
        hsfAddrTcp(addr, (int)sizeof(addr), HSF_AI_HOST, HSF_AI_TCP_PORT);
        if (n > 0) {
            bool any = false;
            int i;
            for (i = 0; i < n; ++i)
                if (valid[i]) { any = true; break; }
            hsfHealthAdd(st, "模拟量模块", HsfDeviceKind_AnalogIo, (int)HsfSide_A,
                         any ? HsfDeviceHealth_Ok : HsfDeviceHealth_Stale,
                         0, any, 0, any ? "" : "无有效数据", addr);
        }
    }

    /* ---- 数字量模块（C 版为聚合后的单站；当前现场 .13 单台 16 点） ---- */
    {
        uint32_t di = 0;
        uint32_t doMask = 0;
        bool valid = false;
        char addr[64];
        hsfAddrTcp(addr, (int)sizeof(addr), HSF_DIG_HOST, HSF_DIG_TCP_PORT);
        if (HsfServerIo_digitalReady()) {
            (void)HsfServerIo_digitalMasks(&di, &doMask, &valid);
            hsfHealthAdd(st, HSF_DIG_MODULE_NAME, HSF_DEVICE_KIND_DIGITAL,
                         (int)HsfSide_A,
                         valid ? HsfDeviceHealth_Ok : HsfDeviceHealth_Stale,
                         0, valid, 0, valid ? "" : "无有效数据", addr);
        } else {
            hsfHealthAdd(st, HSF_DIG_MODULE_NAME, HSF_DEVICE_KIND_DIGITAL,
                         (int)HsfSide_A, HsfDeviceHealth_Offline,
                         0, false, 0, "未装配", addr);
        }
    }

    /* ---- 电磁阀组（整组一条，含故障路数摘要） ---- */
    {
        const HsfServerIoValve* valves = HsfServerIo_valves();
        if (valves && HsfServerIoValve_count(valves) > 0) {
            int health = HsfDeviceHealth_Ok;
            bool online = true;
            char text[96] = "";
            char addr[64];
            /* [阀组地址 = 数字量模块 + 它占的 DO 段] 阀组没有自己的通讯口，
             * 现场照着模块地址与 DO 号去量线即可（对齐 Python _valve_addr）。 */
            snprintf(addr, sizeof(addr), "%s:%d \xC2\xB7 DO%d~DO%d",
                     HSF_DIG_HOST, HSF_DIG_TCP_PORT,
                     HSF_VALVE_DO(0), HSF_VALVE_DO(HSF_VALVE_COUNT - 1));
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
            hsfHealthAdd(st, "电动球阀组", HSF_DEVICE_KIND_VALVE,
                         (int)HsfSide_A, health, 0, online, 0, text, addr);
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
        /* [地址] 连接地址文案（串口/波特率/站号 或 IP:端口 或 模块+DO 口）。
         * 空地址不发（与 Python 侧同口径：没有连接概念的汇总条目不占位）。 */
        if (e->m_addr[0]) {
            snprintf(key, sizeof(key), "d%d_addr", i);
            XBsonDocument_insert_keyUtf8_utf8(doc, key, e->m_addr);
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
 * @brief 停全部受控设备（变频器 / 进水泵 / 电磁阀）。
 * @param reason 日志里写明原因（"切到自动" / "切回手动"）。
 * @details [为什么要有这个动作 —— 2026-11-10 用户口径]
 *          「切换模式后要关闭所有设备，只有按了启动按钮后设备才允许工作，
 *          这是自动模式的运行逻辑」：
 *            · 切到自动：自动模式**拒掉全部远程控制**、控制器又要等人按
 *              "启动"才跑 —— 设备若还按手动命令转着，现场看到的是"切了
 *              自动，泵还在转，而且谁也停不了它"（远程控制已被关掉）。
 *            · 切到手动：自动逻辑退出，设备不能继续按自动命令跑。
 *          逐台独立处理、一台失败不挡其它台（设备离线是常态，不能因为
 *          一台掉线就不停其余的）。
 * @note 与 Python 侧 AutoController.stop_all_devices 同口径（同一动作
 *       序列：变频器减速停机 → 逐台停泵 → 逐路关阀）。
 */
static void hsfAppStopAllDevices(const char* reason)
{
    HsfSide side;
    int i;
    int count;
    char err[96];

    /* 1) 变频器：受控停机（减速停，不用自由停车——现场泵是搅拌设备，
     *    瞬断对机械冲击更大）。 */
    for (side = HsfSide_A; side < HsfSide_Count; ++side) {
        XVfdDevice* dev = HsfServerIo_vfdDevice(side);
        if (dev) {
            XVfdControlRequest req;
            memset(&req, 0, sizeof(req));
            req.m_op = XVfdOperation_Stop;
            (void)XVfdCodec_applyControl(dev, &req);
        }
    }
    /* 2) 进水泵：逐台停（现场无反馈点，停即输出断开）。 */
    count = HsfServerIoPump_count(HsfServerIo_pumps());
    for (i = 0; i < count; ++i) {
        (void)HsfServerIo_pumpSet(i, false, err, (int)sizeof(err));
    }
    /* 3) 电磁阀：逐路关。 */
    count = HsfServerIoValve_count(HsfServerIo_valves());
    for (i = 0; i < count; ++i) {
        (void)HsfServerIo_valveSet(i, false, err, (int)sizeof(err));
    }
    {
        char msg[160];
        snprintf(msg, sizeof(msg), "切换模式：设备全停（%s）",
                 reason ? reason : "");
        hsfAppEventLog("auto", msg);
    }
}

/**
 * @brief 把所有已装配变频器的控制闸同步到当前模式。
 * @param switched 本次是**模式真的发生了切换**（不是幂等重放）。
 * @details 自动 → 关闸（设备层也拒绝写入，双保险）；手动 → 开闸。
 *          控制闸是 XVfdDevice 的单一咽喉，覆盖全品牌全命令模型。
 *          [自动控制器单向安全] 退自动 ⇒ 强制停控制器（切回手动还在自动跑
 *          = 手动模式下设备被偷偷驱动，硬性安全问题）；进自动 ⇒ **不**启动
 *          （模式开关不是设备启动按钮，见 s_autoCtl 说明）。
 *          [切换 ⇒ 设备全停] 见 hsfAppStopAllDevices 的说明；幂等重放
 *          （同一模式再下一遍）**不**动设备：手动模式下设备正由人操作，
 *          一句重复的模式指令把人家手动开的泵停掉是事故。
 */
static void hsfAppApplyModeToDevices(bool switched)
{
    HsfSide side;
    bool enable = (s_runMode == HsfRunMode_Manual);
    for (side = HsfSide_A; side < HsfSide_Count; ++side) {
        XVfdDevice* dev = HsfServerIo_vfdDevice(side);
        if (dev) XVfdDevice_setControlEnabled(dev, enable);
    }
    if (s_autoCtlReady && !enable && XAutoController_running(&s_autoCtl)) {
        XAutoController_stop(&s_autoCtl, (uint64_t)XDateTime_currentMSecsSinceEpoch());
        hsfAppEventLog("auto", "退出自动模式，自动运行已强制停止");
    }
    if (switched) {
        hsfAppStopAllDevices(s_runMode == HsfRunMode_Auto ? "切到自动"
                                                          : "切回手动");
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
        /* 幂等：闸同步一次，**不动设备**（手动模式下设备正由人操作）。 */
        hsfAppApplyModeToDevices(false);
        return;
    }
    s_runMode = mode;
    /* 真切换 ⇒ 关闭所有设备（双向；2026-11-10 用户口径）。 */
    hsfAppApplyModeToDevices(true);
    HsfServerSettings_setInt("run/mode", (int)mode);
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "运行模式切到 %s（远程控制%s）",
                 mode == HsfRunMode_Auto ? "自动" : "手动",
                 mode == HsfRunMode_Auto ? "已禁用" : "已启用");
        hsfAppEventLog("auto", buf);
    }
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

/**
 * @brief 关键运维事件：XPrintf 终端留痕 + logs 表入库（一句话一个事件点）。
 * @param category 类别（utf8；入库口径见 HsfServerStore.h HsfLogRow：
 *                 conn/ctrl/alarm/auto/auth）。
 * @param text 事件文本（utf8；调用方 snprintf 拼好，末尾不带换行）。
 * @details [定点入库] 只收运维要翻的事件（会话接入/断开、控制受理/拒绝、
 *          阀告警、自动运行启停/急停、模式切换、登录成功/被拒）；周期性
 *          日志（遥测推送/采样/推送节拍）**不**进库——logs 表要能按页翻，
 *          不能被流水账冲掉。库未开=只打终端日志（与 Python
 *          _log_with_store 的"库未开=空操作旁路"同口径；入库失败也不
 *          影响命令路径）。
 */
static void hsfAppEventLog(const char* category, const char* text)
{
    XPrintf("HsfServerApp: %s\n", text);
    (void)HsfServerStore_insertLog(&s_store, 0, category, text);
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
    /* [停机禁给定] 给定类操作（频率/转速）只在**运行中**受理——判据是
     * 设备层解码出的遥测状态（m_running），不是"上次发过启动命令"。
     * 与 Python 侧 app.py 的 _VFD_SETPOINT_OPS 闸同口径同文案：宁可拒绝
     * 一次调速，也不能在不确定的状态下改运行参数（命令可能没送达、可能
     * 被拒、可能现场已按面板停止）。 */
    if ((req.m_op == XVfdOperation_SetFrequency ||
         req.m_op == XVfdOperation_SetSpeed)) {
        const XVfdStatus* st = XVfdDevice_status_const(dev);
        if (!st || !st->m_running) {
            hsfAppRespond(session, frame,
                          hsfAppMakeEnvelope(false,
                                             "变频器处于停机状态，不能下发给定（请先启动后再调速）"));
            return;
        }
    }
    /* [运行中禁点动] 电机在正常启停语义下运行时，点动会瞬间换向/抢命令字，
     * 物理上不成立 ⇒ 拒绝（与 Python app.py 同口径同文案）。例外：本侧
     * **点动保持中**（s_jogWatch[side].m_active）放行——点动手持每 200ms
     * 重发心跳帧，此时设备必然"运行中"，把心跳拦掉会被 600ms 看门狗误停
     * （心跳帧的受理口径见下方 Jog 分支注释）。 */
    if (req.m_op == XVfdOperation_Jog) {
        const XVfdStatus* stJog = XVfdDevice_status_const(dev);
        if (stJog && stJog->m_running && !s_jogWatch[side].m_active) {
            hsfAppRespond(session, frame,
                          hsfAppMakeEnvelope(false,
                                             "变频器运行中，禁止点动（请先停机再点动）"));
            return;
        }
    }
    /* [点动手持] 101 = 点动释放（自定义 op，不走 XVfdOperation 值域）。 */
    if ((int)req.m_op == HSF_OP_JOG_RELEASE) {
        hsfAppJogRelease(side);
        hsfAppRespond(session, frame, hsfAppMakeEnvelope(true, NULL));
        return;
    }
    /* [启动/点动捎带给定 → 先写给定再发命令字] 对齐 Python 服务端
     * apply_control（OP_START 分支：if given_hz > 0 → set_frequency(...)
     * 再 send_command）：给定**捎带在启动命令里**下发，服务端内部顺序执行
     * 「写 1000H → 写 2000H」。
     * [为什么必须这样] 服务端为安全起见拒绝**停机状态下的独立给定**
     * （上面的「停机禁给定」闸，拦的是 op=SetFrequency 的独立请求）。
     * 客户端若先单独发 SET 再发 START，SET 必被拒 ⇒ 启动按变频器里残留的
     * 旧给定跑（现场实测：界面设 3Hz、实际跑 25Hz = 界面在骗人）。
     * 捎带则不受该闸限制（它不是独立给定请求），且先后顺序确定。
     * [转速给定] 转速模式下 Python 客户端带的是 rpm（{"op":4,"rpm":…}），
     * C 端客户端带的是换算后的 hz；两种都要落到同一条"先写给定再发命令"
     * 的流程 ⇒ 这里把 rpm 换算成 Hz（换算口径与设备层 SetSpeedRpm 同源）。 */
    if ((req.m_op == XVfdOperation_Start || req.m_op == XVfdOperation_Jog) &&
        !(req.m_valueHz > 0.0) && req.m_valueRpm > 0.0) {
        req.m_valueHz = XVfdDevice_rpmToHz(dev, (int)req.m_valueRpm);
    }
    /* [带给定启动合成一个动作 2026-11-10 板端实机根因] 早先这里是两次调用
     *   （先 XVfdDevice_setFrequency 再 XVfdCodec_applyControl）：**第一笔
     *   写把设备级单飞闸占住，第二笔立刻被拒成「设备忙（上一动作在途）」**
     *   ⇒ C 客户端点「启动」恒失败（Python 服务端没有这道设备闸所以能控）。
     *   现在合成一个多阶段动作（先写给定、回执后再写命令字），单飞保护不变。
     *   [为什么不先判 m_valueHz>0 再决定调哪个] 无给定也要走同一个入口：
     *   它内部会跳过给定阶段，直接发命令字（口径只需一处）。 */
    if (req.m_op == XVfdOperation_Start || req.m_op == XVfdOperation_Jog) {
        ok = XVfdDevice_startWithGiven(dev, req.m_op, req.m_reverse,
                                       req.m_valueHz);
    } else {
        ok = XVfdCodec_applyControl(dev, &req);
    }
    if (!ok) {
        /* [把原因带出来 2026-11-10] 此前这里只回笼统的「设备拒绝（在途动作/
         *   控制闸/能力不符）」——现场看不到到底哪一闸拦的（板端实机复现：
         *   点「启动」被拒，日志无任何线索）。设备层现在留有**具体**原因
         *   （XVfdDevice_lastErrorText，如「命令源未切到通讯（P0.02=2）」），
         *   没有文本时退回错误枚举的通用说法。与 Python 侧「apply_control
         *   返回中文原因」的粒度对齐。 */
        const char* why = XVfdDevice_lastErrorText(dev);
        if (why && why[0]) {
            char msg[256];
            snprintf(msg, sizeof(msg), "设备拒绝：%s", why);
            err = msg;
        } else {
            err = "设备拒绝（在途动作/控制闸/能力不符）";
        }
    }
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
    /* [启停/急停入库] 自动运行的动作态变化是运维首要事件（急停尤其）；
     * 每次请求都留痕（幂等重复也在文本里标明"已是该状态"），不丢线索。 */
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "自动运行请求%s（%s）", opName,
                 changed ? "已执行" : "已是该状态（幂等）");
        hsfAppEventLog("auto", buf);
    }
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
 * @details [额定点随快照下发（S7，2026-11-09）] 应答补 ratedRpm/ratedHz
 *          两键（HSF_KEY_VFD_RATED_RPM/HZ，与 Python app.py _h_vfd_snapshot
 *          同键同源）：客户端把"转速模式"的输入量程与换算对齐，不必再
 *          写死一份 1440/50——两份必然漂移。C 侧取值与换算基准同源：
 *            ratedRpm = XVfdDevice_ratedRpm()（未配置缺省 1440，与 Python
 *                       DEFAULT_RATED_RPM 同值）；
 *            ratedHz  = XVfdDevice_maxFrequency()（转速↔频率换算基准，
 *                       未读得按 50 兜底，与 XVfdDevice_rpmToHz 内部基准
 *                       同一口径；对应 Python cfg.serial.vfd_rated_hz）。
 *          变频器状态由 XVfdCodec 编码、本端不重复那张键表，故沿用
 *          「解码 → 插键 → 重编码」补键方式（与 hsfAppWithSide 同款，
 *          一帧几十字节，成本可忽略）。
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
    XBsonDocument* doc;
    XByteArray* out;
    double ratedRpm;
    double ratedHz;

    if (!bytes) {
        hsfAppRespond(session, frame, NULL);
        return;
    }
    doc = XBsonDocument_create();
    if (!doc ||
        !XBsonDocument_from_bytes(doc, (const uint8_t*)XByteArray_constData(bytes),
                                  (size_t)XByteArray_size_base(bytes))) {
        if (doc) XClassDelete((XClass*)doc);
        XClassDelete((XClass*)bytes);
        hsfAppRespond(session, frame, NULL);
        return;
    }
    XClassDelete((XClass*)bytes);
    hsfAppInsertSide(doc, side);
    /* [缺省值对齐 Python] 设备未装配时也回缺省额定点（1440/50），客户端
     * 输入框量程不至于为 0；数据本身带 valid 位，不影响遥测判读。 */
    ratedRpm = dev ? XVfdDevice_ratedRpm(dev) : 1440.0;
    ratedHz = (dev && XVfdDevice_maxFrequency(dev) > 0.0)
                  ? XVfdDevice_maxFrequency(dev) : 50.0;
    XBsonDocument_insert_keyUtf8_double(doc, HSF_KEY_VFD_RATED_RPM, ratedRpm);
    XBsonDocument_insert_keyUtf8_double(doc, HSF_KEY_VFD_RATED_HZ, ratedHz);
    out = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    hsfAppRespond(session, frame, out);
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
        /* [陈旧阈值回读 2026-11-10] 报**设备层当前生效**的阈值（随采集
         * 周期联动，见 HsfServerIo.c 的 hsfIoStaleMs），而不是写死宏——
         * 否则周期改大后这里仍报 5s，与实际判定口径不符（排查时会被带偏）。 */
        {
            XTempHumTransmitter* thDev = HsfServerIoTempHum_device(th);
            XBsonDocument_insert_keyUtf8_int32(
                doc, "staleMs",
                thDev ? (int32_t)XTempHumTransmitter_staleMs(thDev)
                      : (int32_t)HSF_TEMPHUM_STALE_MS);
        }
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
    /* [陈旧阈值回读 2026-11-10] 报设备层当前生效阈值（随采集周期联动）。 */
    {
        XFlowMeter* fmDev = HsfServerIoFlowMeter_device(fm);
        XBsonDocument_insert_keyUtf8_int32(
            doc, "staleMs",
            fmDev ? (int32_t)XFlowMeter_staleMs(fmDev)
                  : (int32_t)HSF_FLOWMETER_STALE_MS);
    }
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
        double tempC = 0.0;
        uint32_t raw = 0;
        bool online = false;
        bool hasTemp = false;

        /* 名字恒带出（未装配也给出，客户端据此占位显示）。 */
        snprintf(name, sizeof(name), "浊度仪%d", i + 1);
        snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, i, HSF_KEY_TURB_NAME);
        XBsonDocument_insert_keyUtf8_utf8(doc, key, name);

        if (tm) {
            online = HsfServerIoTurbidity_isOnline(tm);
            (void)HsfServerIoTurbidity_ntu(tm, &ntu);
            (void)HsfServerIoTurbidity_raw(tm, &raw);
            /* 温度只有"机型带温度（role 2/3）且这一拍解出来"才有 */
            hasTemp = HsfServerIoTurbidity_tempC(tm, &tempC);
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
            /* 温度随帧下发（2026-11-10）：缺键 = 该机型没有温度（不是 0℃） */
            if (hasTemp) {
                snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, i,
                         HSF_KEY_TURB_TEMP);
                XBsonDocument_insert_keyUtf8_int32(doc, key,
                    (int32_t)(tempC * 100.0 + (tempC >= 0.0 ? 0.5 : -0.5)));
            }
        }
    }
    /* [陈旧阈值回读 2026-11-10] 两台浊度各自按自己的采集周期算阈值
     * （逐台可调），故报**两台的较小者**（客户端按一个阈值理解）。 */
    {
        uint32_t stale = HSF_TURBIDITY_STALE_MS;
        int t;
        for (t = 0; t < HSF_TURBIDITY_COUNT; ++t) {
            const HsfServerIoTurbidity* tb = HsfServerIo_turbidity(side, t);
            XTurbidityMeter* dev;
            uint32_t v;
            if (!tb) continue;
            dev = HsfServerIoTurbidity_device(tb);
            if (!dev) continue;
            v = XTurbidityMeter_staleMs(dev);
            if (v && (stale == HSF_TURBIDITY_STALE_MS || v < stale))
                stale = v;
        }
        XBsonDocument_insert_keyUtf8_int32(doc, "staleMs", (int32_t)stale);
    }
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
    const HsfSide requested = hsfAppSideFromPayload(payload, frame->m_payloadLen);
    /* [侧号由服务端解析 2026-11-10] 请求里的 side 只是提示；浊度仪挂在
     * 哪侧是服务端装配事实 ⇒ 按实际装配侧取数，响应的 side 也填实际侧。 */
    const HsfSide side = HsfServerIo_turbiditySide(requested);
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
    const HsfSide requested = hsfAppSideFromPayload(payload, frame->m_payloadLen);
    /* [侧号由服务端解析 2026-11-10] 同浊度：请求侧没装就按实际装配侧答。 */
    const HsfSide side = HsfServerIo_flowMeterSide(requested);
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
 * @brief 模拟量通道工程量换算（标定表 + 断线/超量程说明）——**唯一实现**。
 * @details 遥测编码（e/eu/en/ew 四键）与历史采样（press_mpa/press_note）共用：
 *          两处各算一遍会让"界面压差"与"库里压差/越限说明"不一致；
 *          Python 侧两者同源（cfg.analog.scales[0].engineering）。
 * @param ch 通道号。
 * @param ma 电流（mA）。
 * @param noteBuf 越限说明缓冲（可 NULL）。
 * @param noteCap 缓冲容量。
 * @param outValue 工程量输出。
 * @param outUnit 单位输出（借用静态串）。
 * @param outName 设备名输出（借用静态串）。
 * @param outNote 越限说明输出（借用 noteBuf；无越限=NULL）。
 * @return 该通道已标定返回 true；未标定/越界返回 false。
 */
static bool hsfAppAiEngineering(int ch, double ma, char* noteBuf, int noteCap,
                                double* outValue, const char** outUnit,
                                const char** outName, const char** outNote)
{
    static const struct {
        const char* m_name;
        const char* m_unit;
        double m_fullScale;
        double m_lowMa;
        double m_highMa;
    } kScales[HSF_ANALOG_CHANNELS] = HSF_AI_SCALES;
    double value;

    if (outValue) *outValue = 0.0;
    if (outUnit) *outUnit = NULL;
    if (outName) *outName = NULL;
    if (outNote) *outNote = NULL;
    if (ch < 0 || ch >= HSF_ANALOG_CHANNELS) return false;
    if (!kScales[ch].m_unit || kScales[ch].m_fullScale <= 0.0) return false;
    if (outUnit) *outUnit = kScales[ch].m_unit;
    if (outName) *outName = kScales[ch].m_name;
    value = (ma - kScales[ch].m_lowMa) /
            (kScales[ch].m_highMa - kScales[ch].m_lowMa) *
            kScales[ch].m_fullScale;
    if (ma < kScales[ch].m_lowMa - HSF_AI_ZERO_TOL_MA) {
        value = 0.0;
        if (noteBuf && noteCap > 0) {
            snprintf(noteBuf, (size_t)noteCap, "低于 %.1fmA（断线/欠量程）",
                     kScales[ch].m_lowMa);
            if (outNote) *outNote = noteBuf;
        }
    } else if (ma > kScales[ch].m_highMa + HSF_AI_ZERO_TOL_MA) {
        value = kScales[ch].m_fullScale;
        if (noteBuf && noteCap > 0) {
            snprintf(noteBuf, (size_t)noteCap, "高于 %.1fmA（超量程）",
                     kScales[ch].m_highMa);
            if (outNote) *outNote = noteBuf;
        }
    } else {
        if (value < 0.0) value = 0.0;
        if (value > kScales[ch].m_fullScale) value = kScales[ch].m_fullScale;
    }
    if (outValue) *outValue = value;
    return true;
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
        /* [名字只给有标定的通道] 现场只有通道 0 接了 EJA110A 压差变送器；
         * 其余通道是通用 4~20mA 输入，没有设备名字——发"模拟量N"是编造。
         * 无标定通道客户端按"通道 N"显示（与 Python 侧同口径）。 */
        static const char* const kNames[HSF_ANALOG_CHANNELS] = HSF_AI_NAMES;
        for (i = 0; i < n && i < HSF_ANALOG_CHANNELS; ++i) {
            char key[12];
            if (!kNames[i]) continue;
            snprintf(key, sizeof(key), HSF_KEY_AI_NAME_FMT, i);
            XBsonDocument_insert_keyUtf8_utf8(doc, key, kNames[i]);
        }
    }
    /* ---- 工程量四键（e/eu/en/ew，与 Python 侧同键同义）----
     * 标定表在服务端（HSF_AI_SCALES）：量程是设备铭牌口径，与接线同性质，
     * 放客户端意味着每台机器都要配一遍、改一次要升客户端。
     * 换算：p = (I - lowMa) / (highMa - lowMa) × fullScale。
     * 零点容差 0.05mA：活零点实测 3.996mA 属正常抖动，不是断线。 */
    {
        for (i = 0; i < n && i < HSF_ANALOG_CHANNELS; ++i) {
            char key[12];
            char noteBuf[64];
            double value;
            const char* note = NULL;
            const char* unit = NULL;
            const char* name = NULL;
            if (!valid[i]) continue;
            if (!hsfAppAiEngineering(i, ma[i], noteBuf, (int)sizeof(noteBuf),
                                     &value, &unit, &name, &note))
                continue;
            snprintf(key, sizeof(key), HSF_KEY_AI_ENG_FMT, i);
            XBsonDocument_insert_keyUtf8_int32(doc, key,
                                               (int32_t)(value * 1000.0 + 0.5));
            snprintf(key, sizeof(key), HSF_KEY_AI_ENG_UNIT_FMT, i);
            XBsonDocument_insert_keyUtf8_utf8(doc, key, unit);
            if (name) {
                snprintf(key, sizeof(key), HSF_KEY_AI_ENG_NAME_FMT, i);
                XBsonDocument_insert_keyUtf8_utf8(doc, key, name);
            }
            if (note) {
                snprintf(key, sizeof(key), HSF_KEY_AI_ENG_NOTE_FMT, i);
                XBsonDocument_insert_keyUtf8_utf8(doc, key, note);
            }
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
        {
            char buf[96];
            snprintf(buf, sizeof(buf), "控制命令 0x%04X 被拒（自动运行中：远程控制已被禁用）",
                     (unsigned)frame->m_msgCode);
            hsfAppEventLog("ctrl", buf);
        }
        return false;
    }
    /* [权限策略闸] 角色位图判定（零遍历）替代原硬比较；拒绝留痕由
     * XPermPolicy 的 permissionDenied 信号统一打日志（装配见
     * hsfAppAccessCtlSetup）。语义对齐四档：操作者/管理员持有
     * DeviceControl，匿名/普通用户不持有。 */
    if (!XPermPolicy_check(&s_perm,
                           (XAccountRole)XLinkServer_sessionRole(srv, session),
                           (uint32_t)XAccessPerm_DeviceControl)) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, hsfAppPermDeniedText(
                          (uint32_t)XAccessPerm_DeviceControl)));
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
 * @brief 数字量输出控制（口径与 Python _h_digital_control 统一，S6 方案 A）。
 * @details 载荷优先级：mask/values 在场 ⇒ 整组写（bit n=通道 n 目标值，
 *          只写有变化的点，与 Python write_group 同口径）；否则 do/index
 *          在场 ⇒ 单点写（dev 缺省 0，index 与 do 同义——旧客户端兼容）；
 *          都没有 ⇒ 拒（"控制载荷缺 do 或 mask 字段"）。
 *          dev=模块号：C 端单站聚合（HSF_DEV 恒 0），非 0 拒（越界文案
 *          与 Python 同款）。
 *          应答 {ok, err?, dev, di, do}：dev=受理的模块号、do=该站当前
 *          DO 位图（与 Python extra 回显同键）；di 为 C 端附加键（老客户端
 *          的响应刷新沿用，Python 端忽略未知键）。
 */
static void hsfAppHandleDigitalControl(XLinkSession* session,
                                       const XLinkFrameHeader* frame,
                                       const uint8_t* payload, XLinkServer* srv)
{
    XBsonDocument* doc;
    XBsonValue* v;
    int devIdx = 0;
    int channel = -1;
    int64_t mask = 0;
    bool hasGroup = false;
    bool hasSingle = false;
    bool on = false;
    bool ok = false;
    uint32_t di = 0;
    uint32_t doMask = 0;
    bool valid = false;
    char errBuf[192];
    char logBuf[192];
    int channels;

    errBuf[0] = '\0';   /* 失败原因聚合（整组路径填充；单点路径见下方赋值）。 */

    if (!hsfAppControlGate(session, frame, srv)) return;

    doc = XBsonDocument_create();
    if (!doc || !XBsonDocument_from_bytes(doc, payload, frame->m_payloadLen)) {
        if (doc) XClassDelete((XClass*)doc);
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "控制载荷解析失败"));
        return;
    }
    /* [dev] 缺省 0（旧 {"index","on"} 载荷不带本键，兼容口径）。 */
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_DIG_DEV);
    if (v) devIdx = (int)XBsonValue_toInt32(v, 0);
    /* [整组优先] mask 与 values 同义，mask 优先（Python 同序）。
     * [按 int64 取] bit31 在场时 Python 的 int 超 int32 编码为 BSON int64，
     * toInt32 对 int64 变体返回缺省 0——整组写会静默变全 0，必须 toInt64。 */
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_DIG_MASK);
    if (v) { mask = XBsonValue_toInt64(v, 0); hasGroup = true; }
    if (!hasGroup) {
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_DIG_VALUES);
        if (v) { mask = XBsonValue_toInt64(v, 0); hasGroup = true; }
    }
    /* [单点] do 与旧 index 同义，do 优先。 */
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_DIG_DO);
    if (v) { channel = (int)XBsonValue_toInt32(v, -1); hasSingle = true; }
    if (!hasSingle) {
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_DIG_INDEX);
        if (v) { channel = (int)XBsonValue_toInt32(v, -1); hasSingle = true; }
    }
    v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_DIG_ON);
    if (v) on = XBsonValue_toBool(v, false);
    XClassDelete((XClass*)doc);

    /* [模块号闸] C 端是单站聚合（"dev"恒 0）；越界文案与 Python 同款
     * （"数字量模块号越界（0~N）"，单站 N=0）。 */
    if (devIdx < 0 || devIdx > 0) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "数字量模块号越界（0~0）"));
        return;
    }
    channels = HsfServerIo_digitalCount();
    /* [站就绪闸] 站未装配时整组写会变成"无事可写"的假成功（单点写会被
     * 通道越界/写入失败拦住，整组写不会）——统一前置拒绝。 */
    if (!HsfServerIo_digitalReady()) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "数字量站未装配"));
        return;
    }
    if (!hasGroup && (!hasSingle || channel < 0 || channel >= channels)) {
        /* 两种 reject 合一：整组形态不带单点键不算错，走下面的缺 do 分支；
         * 单点形态通道越界才报通道越界（文案对齐 Python write_do）。 */
        if (hasSingle) {
            char buf[96];
            snprintf(buf, sizeof(buf), "DO 通道号越界（0~%d）",
                     channels > 0 ? channels - 1 : 0);
            hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, buf));
        } else {
            hsfAppRespond(session, frame,
                          hsfAppMakeEnvelope(false, "控制载荷缺 do 或 mask 字段"));
        }
        return;
    }

    if (hasGroup) {
        /* [整组写] 只写有变化的点（Python write_group 同口径）：先取当前
         * 位图做基线，逐位 FC05。位图键语义与 Python 一致（bit n=目标值）。
         * 失败聚合（最多报 4 条，"；"连接——Python 同款式）。 */
        int i;
        int failed = 0;
        HsfServerIo_digitalMasks(&di, &doMask, &valid);
        if (!valid) doMask = 0;   /* 无回显基线按全 0（与 Python 上电态一致）。 */
        errBuf[0] = '\0';
        ok = true;
        for (i = 0; i < channels && i < 32; ++i) {
            const bool target = ((((uint64_t)mask) >> i) & 1u) != 0u;
            const bool cur = ((doMask >> i) & 1u) != 0u;
            bool w;
            if (target == cur) continue;
            w = HsfServerIo_digitalWrite(i, target);
            if (!w) {
                ok = false;
                if (failed < 4) {
                    char seg[32];
                    snprintf(seg, sizeof(seg), "%sDO%d: 写入失败",
                             failed ? "；" : "", i);
                    if (strlen(errBuf) + strlen(seg) < sizeof(errBuf))
                        strcat(errBuf, seg);
                }
                ++failed;
            }
        }
    } else {
        ok = HsfServerIo_digitalWrite(channel, on);
        if (!ok)
            snprintf(errBuf, sizeof(errBuf), "%s", "写入被拒");
    }

    /* [受理/拒绝入库] DO 是执行器输出，误动作要可追溯：结果（含失败
     * 原因）定点进 logs 表 ctrl 类。 */
    HsfServerIo_digitalMasks(&di, &doMask, &valid);
    if (hasGroup) {
        snprintf(logBuf, sizeof(logBuf),
                 "数字量整组写 dev=%d mask=0x%08llX：%s%s",
                 devIdx, (unsigned long long)((uint64_t)mask & 0xFFFFFFFFull),
                 ok ? "已受理" : "被拒",
                 ok ? "" : (errBuf[0] ? errBuf : "（写入失败）"));
    } else {
        snprintf(logBuf, sizeof(logBuf),
                 "数字量控制 dev=%d 通道%d→%d：%s",
                 devIdx, channel, on ? 1 : 0, ok ? "已受理" : "被拒");
    }
    hsfAppEventLog("ctrl", logBuf);

    /* 应答带最新位图 + 模块号（Python 同键）：客户端不必等下一拍推送
     * 就能把按钮态对齐。 */
    {
        XBsonDocument* resp = XBsonDocument_create();
        XByteArray* bytes;
        if (resp) {
            XBsonDocument_insert_keyUtf8_bool(resp, HSF_KEY_OK, ok);
            if (!ok && errBuf[0])
                XBsonDocument_insert_keyUtf8_utf8(resp, HSF_KEY_ERR, errBuf);
            XBsonDocument_insert_keyUtf8_int32(resp, HSF_KEY_DIG_DEV, devIdx);
            XBsonDocument_insert_keyUtf8_int32(resp, HSF_KEY_DIG_DI, (int32_t)di);
            XBsonDocument_insert_keyUtf8_int32(resp, HSF_KEY_DIG_DO, (int32_t)doMask);
            bytes = XBsonDocument_toBson(resp);
            XClassDelete((XClass*)resp);
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
                      hsfAppMakeEnvelope(false, "电动球阀组未装配"));
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
    /* [应答 = ok/err + 整帧快照合并在同一帧] 与 Python _h_valve_control
     * 同构（extra=to_doc()），受理失败（离线/未配置）同样带快照；只有
     * 模式/角色/解析闸走裸包络（hsfAppControlGate 已处理）。
     * [为什么必须带快照] Python 侧实锅（2026-11-05"第一下点击没反应"）：
     * 客户端拿应答里的最新状态刷新卡片，只回 {ok:true} 就得等 ≤100ms 的
     * 周期推送，弱网下观感是"按钮没反应"。 */
    {
        XBsonDocument* resp = XBsonDocument_create();
        XByteArray* bytes = NULL;
        if (resp) {
            (void)hsfAppFillValvesDoc(resp);   /* 未装配时文档只含 ok/err。 */
            XBsonDocument_insert_keyUtf8_bool(resp, HSF_KEY_OK, ok);
            if (!ok && err[0])
                XBsonDocument_insert_keyUtf8_utf8(resp, HSF_KEY_ERR, err);
            bytes = XBsonDocument_toBson(resp);
            XClassDelete((XClass*)resp);
        }
        if (!bytes) {
            bytes = ok ? hsfAppMakeEnvelope(true, NULL)
                       : hsfAppMakeEnvelope(false,
                                            err[0] ? err : "阀门控制被拒");
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
 * @brief 历史分页查询（HSF_CMD_HISTORY_PAGE）。
 * @details 载荷 {offset,limit,beforeTs?,loTs?,hiTs?}；应答
 *          {ok,total,offset,limit,cols,rows,dbRows,latestTs,earliestTs,
 *            windowStartTs,windowEndTs}——**键名与 Python 侧 proto.HIST_KEY_*
 *          完全一致**，故 Python 客户端可以直接查 C 服务端的历史（反向
 *          亦然）。
 *          [窗口锚点] beforeTs 是"截至这一刻"的墙钟毫秒：库在持续入库
 *          （5s 一行）时，翻页若不带锚点，新行会把 offset 顶偏 ⇒ 相邻
 *          两页出现重复行、旧行被跳过。客户端首屏把自己的"现在"发上来，
 *          翻页原样回传。
 *          [权限] 浏览（缺省）属只读面；**导出/下载**（载荷 export=true）
 *          需 XAccessPerm_Download（普通用户+）——否则客户端不灰按钮也
 *          绕不过服务端（直发帧同样被拒）。
 */
static void hsfAppHandleHistoryPage(XLinkSession* session,
                                    const XLinkFrameHeader* frame,
                                    const uint8_t* payload)
{
    XBsonDocument* doc;
    XBsonArray* rowsArr;
    XBsonArray* colsArr;
    XByteArray* bytes;
    HsfHistoryQueryRow rows[HSF_HISTORY_PAGE_MAX];
    int64_t loMs = 0;
    int64_t hiMs = 0;
    int64_t dbRows = 0;
    int64_t latestTs = 0;
    int64_t earliestTs = 0;
    int windowTotal = 0;
    int offset = 0;
    int limit = HSF_HISTORY_PAGE_DEFAULT;
    int n = 0;
    int i;
    int c;
    bool wantExport = false;
    char kwBuf[160];
    uint32_t kwCols[HSF_HISTORY_DISPLAY_COLS];
    int kwColCount = 0;

    kwBuf[0] = '\0';

    if (!HsfServerStore_isOpen(&s_store)) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "落盘未启用：服务端没有历史数据可查"));
        return;
    }
    doc = XBsonDocument_create();
    if (!doc || !XBsonDocument_from_bytes(doc, payload, frame->m_payloadLen)) {
        if (doc) XClassDelete((XClass*)doc);
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "分页载荷解析失败"));
        return;
    }
    {
        XBsonValue* v;
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_HIST_OFFSET);
        if (v) offset = (int)XBsonValue_toInt32(v, 0);
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_HIST_LIMIT);
        if (v) limit = (int)XBsonValue_toInt32(v, 0);
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_HIST_LO_TS);
        if (v) loMs = XBsonValue_toInt64(v, 0);
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_HIST_HI_TS);
        if (v) hiMs = XBsonValue_toInt64(v, 0);
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_HIST_BEFORE_TS);
        if (v) {
            const int64_t before = XBsonValue_toInt64(v, 0);
            /* [窗口取交集] 与 Python 同口径：窗口上界 = min(hiTs, beforeTs)。
             * 旧实现直接覆盖 ⇒ 两个键同时给时更早的上界会丢。 */
            if (before > 0 && (hiMs <= 0 || before < hiMs)) hiMs = before;
        }
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_HIST_EXPORT);
        if (v) wantExport = XBsonValue_toBool(v, false);
        /* [关键字下推] 与 Python HIST_KEY_KW / KW_COLS 同名同义。 */
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_HIST_KW);
        if (v) {
            const XString* s = XBsonValue_toString(v);
            if (s) {
                const char* u = XString_toUtf8(s);
                if (u) snprintf(kwBuf, sizeof(kwBuf), "%s", u);
            }
        }
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_HIST_KW_COLS);
        if (v) {
            const XBsonArray* arr = XBsonValue_toArray(v);
            if (arr) {
                const int64_t cnt = XBsonArray_count_base(arr);
                int64_t k;
                for (k = 0; k < cnt && kwColCount < HSF_HISTORY_DISPLAY_COLS; ++k) {
                    const XBsonValue* cell = XBsonArray_at_base(arr, k);
                    const int idx = cell ? XBsonValue_toInt32(cell, -1) : -1;
                    if (idx >= 0 && idx < HSF_HISTORY_DISPLAY_COLS)
                        kwCols[kwColCount++] = (uint32_t)idx;
                }
            }
        }
    }
    XClassDelete((XClass*)doc);
    /* [下载权限闸] 导出意图必须持有 Download；浏览不受此限。 */
    if (wantExport &&
        !XPermPolicy_check(&s_perm,
                           (XAccountRole)XLinkServer_sessionRole(s_srv, session),
                           (uint32_t)XAccessPerm_Download)) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, hsfAppPermDeniedText(
                          (uint32_t)XAccessPerm_Download)));
        return;
    }
    if (offset < 0) offset = 0;
    /* [窗口终点缺省=现在] 与 Python 同口径：beforeTs 不给时窗口上界取
     * 当前墙钟，客户端据此显示"截至现在"；旧实现回 0（界面无法回显）。 */
    if (hiMs <= 0)
        hiMs = (int64_t)XDateTime_currentSecsSinceEpoch() * 1000LL;
    if (limit < HSF_HISTORY_PAGE_MIN) limit = HSF_HISTORY_PAGE_MIN;
    if (limit > HSF_HISTORY_PAGE_MAX) limit = HSF_HISTORY_PAGE_MAX;

    /* 窗口起点/终点回填：hiMs 已是 min(hiTs, beforeTs) 的生效上界，
     * 与 Python windowEndTs 同口径。 */
    n = HsfServerStore_queryHistory(&s_store, offset, limit, loMs, hiMs,
                                    kwBuf, kwCols, kwColCount,
                                    rows, HSF_HISTORY_PAGE_MAX,
                                    &windowTotal, &dbRows,
                                    &latestTs, &earliestTs);

    /* 应答文档：包络 + 分页数据。 */
    doc = XBsonDocument_create();
    if (!doc) {
        hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "应答文档创建失败"));
        return;
    }
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, true);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_HIST_TOTAL, windowTotal);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_HIST_OFFSET, offset);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_HIST_LIMIT, limit);
    XBsonDocument_insert_keyUtf8_int64(doc, HSF_KEY_HIST_DB_ROWS, dbRows);
    XBsonDocument_insert_keyUtf8_int64(doc, HSF_KEY_HIST_LATEST_TS, latestTs);
    XBsonDocument_insert_keyUtf8_int64(doc, HSF_KEY_HIST_EARLIEST_TS, earliestTs);
    XBsonDocument_insert_keyUtf8_int64(doc, HSF_KEY_HIST_WINDOW_START,
                                      (int64_t)loMs);
    XBsonDocument_insert_keyUtf8_int64(doc, HSF_KEY_HIST_WINDOW_END,
                                      (int64_t)hiMs);
    /* 列名数组（与 Python store.HISTORY_COLUMNS 同序）。 */
    colsArr = XBsonArray_create();
    if (colsArr) {
        static const char* const kCols[HSF_HISTORY_COLUMNS] = {
            "ts", "mode", "vfd_state", "freq_hz", "current_a", "rpm",
            "turb1", "turb2", "flow", "flow_unit", "flow_total",
            "total_unit", "press_mpa", "press_note", "temp", "hum",
            "di", "do", "water_temp"
        };
        for (c = 0; c < HSF_HISTORY_COLUMNS; ++c) {
            XBsonValue* val = XBsonValue_create(XBSON_TYPE_STRING);
            if (!val) continue;
            XBsonValue_setString_utf8(val, kCols[c]);
            /* [为什么用 move 变体] 拷贝语义的 append 会对元素走
             * XBsonValue_copy：字符串/子文档/数组各自 create_copy，而
             * XBsonArray/XBsonDocument 的 create_copy 是 COW（共享存储 +
             * 引用计数，XContainer_virtual.c:117-122），仍有一轮
             * addRef 与结构拷贝成本；move 只做结构移动，省掉这一轮。
             * 两条路径都必须随后释放源对象：copy 路径释放原值（引用已由
             * 元素持有），move 路径释放空的 XBsonValue 堆壳（AGENTS.md
             * 「`_move` 后空壳仍由调用方按分配方式释放」；框架 XBsonArray.c
             * 内部也是 move 后 delete）。
             * [move 后释放源壳 2026-11-08] 原先只 append 不释放 ⇒ 每次
             * HISTORY_PAGE 泄漏 18 个 32 字节壳，且随行数放大（每行另有
             * 18 个单元格壳 + 1 个行值壳）。注意这不是 210KB 的主项：
             * 主项是 XSqlRecord 副本（见 HsfServerStore.c 的
             * XClassDelete(rec) 修复）。 */
            XBsonArray_append_move_base(colsArr, val);
            XBsonValue_delete(val);
        }
        XBsonDocument_insert_keyUtf8_array_move(doc, HSF_KEY_HIST_COLS, colsArr);
        /* 同上：insert_keyUtf8_array_move 只转移内容，源数组壳必须由调用方
         * 释放（公开测试 XJsonBsonCoverageTest.c:1534-1535 即此口径）。 */
        XClassDelete((XClass*)colsArr);
    }
    /* 行数组：每行一个数组（18 格；NULL 列发 BSON null）。 */
    rowsArr = XBsonArray_create();
    if (rowsArr) {
        for (i = 0; i < n; ++i) {
            XBsonArray* rowArr = XBsonArray_create();
            if (!rowArr) break;
            for (c = 0; c < HSF_HISTORY_COLUMNS; ++c) {
                const HsfHistoryCell* cell = &rows[i].m_cells[c];
                XBsonValue* val = NULL;
                if (cell->m_isNull) {
                    val = XBsonValue_create_null();
                } else if (c == 0) {
                    val = XBsonValue_create_int64(strtoll(cell->m_text, NULL, 10));
                } else if (c == 1 || c == 2 || c == 9 || c == 11 || c == 13) {
                    val = XBsonValue_create(XBSON_TYPE_STRING);
                    if (val) XBsonValue_setString_utf8(val, cell->m_text);
                } else if (c == 16 || c == 17) {
                    val = XBsonValue_create_int64(
                        (int64_t)strtoull(cell->m_text, NULL, 10));
                } else {
                    val = XBsonValue_create_double(atof(cell->m_text));
                }
                if (!val) continue;
                XBsonArray_append_move_base(rowArr, val);
                XBsonValue_delete(val);   /* move 后释放源壳，见上方注记。 */
            }
            {
                /* [COW 共享 + 引用计数，不是深拷贝] 实现依据：
                 * XBsonValue_setArray → XBsonArray_create_copy → XClassCopy
                 * → VXClass_copy（XContainer_virtual.c:117-122）：COW 容器只
                 * 共享 m_data 并 XSharedData_addRef，不复制元素；同理
                 * XBsonDocument_create_copy 也是 COW。
                 * 所以本块**不是**历史页每轮 7,493 字节/行的来源；实测
                 * +182,648(limit=20) / +14,360(limit=1) 是「XSqlRecord 副本
                 * + 单格 XVariant 副本」的合计；修 _const 后为
                 * +153,688 / +11,328，余下的 XSqlRecord 副本见
                 * HsfServerStore.c 的 XClassDelete(rec) 修复。
                 * 这里用 _ref 仅省一次 addRef 引用计数（微优化、行为等价）：
                 * rowArr 所有权整体交给 rowVal，本函数不得再 XClassDelete(rowArr)。
                 * [为什么仍要释放 rowVal 壳] append_move 只把 value 的**内容**
                 * 移进数组元素，壳本身仍归调用方。 */
                XBsonValue* rowVal = XBsonValue_create(XBSON_TYPE_ARRAY);
                if (rowVal) {
                    XBsonValue_setArray_ref(rowVal, rowArr);
                    rowArr = NULL;              /* 所有权已转移 */
                    XBsonArray_append_move_base(rowsArr, rowVal);
                    XBsonValue_delete(rowVal);
                }
            }
            if (rowArr) XClassDelete((XClass*)rowArr);
        }
        XBsonDocument_insert_keyUtf8_array_move(doc, HSF_KEY_HIST_ROWS, rowsArr);
        /* 同 colsArr：move 只转移内容，源数组壳由调用方释放。 */
        XClassDelete((XClass*)rowsArr);
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    hsfAppRespond(session, frame, bytes);
}

/**
 * @brief 运行日志分页查询（HSF_CMD_LOG_PAGE=0x0119）。
 * @details 载荷 {limit,offset,beforeTs?,loTs?,hiTs?,category?,kw?}（键名
 *          与 0x0118 同风格，见 XhsfLinkProto.h HSF_KEY_LOG_*）；应答
 *          {ok, rows:[{ts,category,text}…], total, count, offset, limit}。
 *          [数据源] HsfServerStore 的 logs 表——由 hsfAppEventLog 定点入库
 *          （conn/ctrl/alarm/auto/auth 五类关键事件，不是全量日志流）。
 *          [窗口锚点/关键字] 语义与 0x0118 完全一致：上界 = min(hiTs,
 *          beforeTs)（缺省=现在，库在持续入库 ⇒ 翻页必须钉锚）；kw 下推
 *          服务端，total 与 rows 共用同一条 WHERE。offset/limit 原样回显
 *          （客户端在途应答归属判定用，与 0x0118 同机制）。
 *          [权限] 只读面，hsfAppCommandPerm 缺省已放 ViewTelemetry，此处
 *          不再细分（无导出形态）。
 */
static void hsfAppHandleLogPage(XLinkSession* session,
                                const XLinkFrameHeader* frame,
                                const uint8_t* payload)
{
    XBsonDocument* doc;
    XBsonArray* rowsArr;
    XByteArray* bytes;
    HsfLogRow rows[HSF_LOG_PAGE_MAX];
    int64_t loMs = 0;
    int64_t hiMs = 0;
    int total = 0;
    int offset = 0;
    int limit = HSF_LOG_PAGE_DEFAULT;
    int n = 0;
    int i;
    char categoryBuf[24];
    char kwBuf[160];

    categoryBuf[0] = '\0';
    kwBuf[0] = '\0';

    if (!HsfServerStore_isOpen(&s_store)) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "落盘未启用：服务端没有日志可查"));
        return;
    }
    doc = XBsonDocument_create();
    if (!doc || !XBsonDocument_from_bytes(doc, payload, frame->m_payloadLen)) {
        if (doc) XClassDelete((XClass*)doc);
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, "分页载荷解析失败"));
        return;
    }
    {
        XBsonValue* v;
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_LOG_OFFSET);
        if (v) offset = (int)XBsonValue_toInt32(v, 0);
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_LOG_LIMIT);
        if (v) limit = (int)XBsonValue_toInt32(v, 0);
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_LOG_LO_TS);
        if (v) loMs = XBsonValue_toInt64(v, 0);
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_LOG_HI_TS);
        if (v) hiMs = XBsonValue_toInt64(v, 0);
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_LOG_BEFORE_TS);
        if (v) {
            const int64_t before = XBsonValue_toInt64(v, 0);
            /* [窗口取交集] 与 0x0118 同口径：上界 = min(hiTs, beforeTs)。 */
            if (before > 0 && (hiMs <= 0 || before < hiMs)) hiMs = before;
        }
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_LOG_CATEGORY);
        if (v) {
            const XString* s = XBsonValue_toString(v);
            if (s) {
                const char* u = XString_toUtf8(s);
                if (u) snprintf(categoryBuf, sizeof(categoryBuf), "%s", u);
            }
        }
        v = XBsonDocument_value_keyUtf8(doc, HSF_KEY_LOG_KW);
        if (v) {
            const XString* s = XBsonValue_toString(v);
            if (s) {
                const char* u = XString_toUtf8(s);
                if (u) snprintf(kwBuf, sizeof(kwBuf), "%s", u);
            }
        }
    }
    XClassDelete((XClass*)doc);
    if (offset < 0) offset = 0;
    /* [窗口终点缺省=现在] 与 0x0118 同口径（客户端据此回显"截至现在"）。 */
    if (hiMs <= 0)
        hiMs = (int64_t)XDateTime_currentSecsSinceEpoch() * 1000LL;
    if (limit < 1) limit = 1;
    if (limit > HSF_LOG_PAGE_MAX) limit = HSF_LOG_PAGE_MAX;

    n = HsfServerStore_queryLogs(&s_store, offset, limit, loMs, hiMs,
                                 categoryBuf, kwBuf,
                                 rows, HSF_LOG_PAGE_MAX, &total);

    /* 应答文档：包络 + 行对象数组（区别于 0x0118 的值数组——行结构
     * {ts,category,text} 字段名即语义，客户端解析直读键）。 */
    doc = XBsonDocument_create();
    if (!doc) {
        hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "应答文档创建失败"));
        return;
    }
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, true);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_LOG_TOTAL, total);
    /* [count 2026-11-09 补契约] 页内行数（宏表定义了就回，C/Py 两端文档
     * 都写了该键；客户端实际用 len(rows)，此键供观测对账）。 */
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_LOG_COUNT, n);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_LOG_OFFSET, offset);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_LOG_LIMIT, limit);
    rowsArr = XBsonArray_create();
    if (rowsArr) {
        for (i = 0; i < n; ++i) {
            XBsonDocument* rowDoc = XBsonDocument_create();
            XBsonValue* rowVal;
            if (!rowDoc) break;
            XBsonDocument_insert_keyUtf8_int64(rowDoc, HSF_KEY_LOG_TS,
                                              rows[i].m_ts);
            XBsonDocument_insert_keyUtf8_utf8(rowDoc, HSF_KEY_LOG_CATEGORY,
                                              rows[i].m_category);
            XBsonDocument_insert_keyUtf8_utf8(rowDoc, HSF_KEY_LOG_TEXT,
                                              rows[i].m_text);
            rowVal = XBsonValue_create(XBSON_TYPE_DOCUMENT);
            if (!rowVal) {
                XClassDelete((XClass*)rowDoc);
                continue;
            }
            /* [COW 共享] setDocument 走 create_copy＝COW（共享存储 + 引用
             * 计数，见 XContainer_virtual），行文档所有权仍在调用方。 */
            XBsonValue_setDocument(rowVal, rowDoc);
            XBsonArray_append_move_base(rowsArr, rowVal);
            XBsonValue_delete(rowVal);   /* move 后释放源壳。 */
            XClassDelete((XClass*)rowDoc);
        }
        XBsonDocument_insert_keyUtf8_array_move(doc, HSF_KEY_LOG_ROWS, rowsArr);
        XClassDelete((XClass*)rowsArr);   /* move 后源数组壳由调用方释放。 */
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    hsfAppRespond(session, frame, bytes);
}

/**
 * @brief 采集节整型参数：缺省取配置头文件的宏，INI 有覆盖则用覆盖值。
 * @details [为什么要覆盖层] C 端的采集参数是**编译期常量**（写在
 *          HsfServerConfig.h，与现场接线同性质）。远端配置（0x0117）能改的
 *          就是这一层：写进 INI 的 collect/<键>，**重启服务端后生效**。
 *          没有覆盖时行为与从前完全一致（还是宏的值）。
 */
static int hsfCollectInt(const char* key, int def)
{
    char iniKey[96];
    snprintf(iniKey, sizeof(iniKey), "collect/%s", key);
    if (!HsfServerSettings_contains(iniKey)) return def;
    return HsfServerSettings_getInt(iniKey, def);
}

/** @brief 采集节开关（同 hsfCollectInt；0=禁用）。 */
static bool hsfCollectEnabled(const char* key, int def)
{
    return hsfCollectInt(key, def) != 0;
}

/**
 * @brief 远端配置读取（HSF_CMD_CONFIG_GET）：采集节 + 落盘节 + 容量实况。
 * @details 应答键与 Python 侧 proto.CFGK_* 一致 ⇒ 两端客户端可互换。
 *          [采集节报的是"真值"] C 端的采集参数是编译期常量（写在
 *          HsfServerConfig.h，与现场接线同性质），故这里报的就是当前
 *          生效值；若 INI 里有 collect/* 覆盖（远端配置改过、重启后生效），
 *          报覆盖后的值。
 *          [路 A/B 的耦合] C 端的 485 路 A（变频器+温湿度）与路 B
 *          （浊度仪+流量计）各自**共用一条总线**，故同一路的两个设备开关
 *          只能同生死 ⇒ vfdEnabled 与 temphumEnabled 报同一个值
 *          （如实反映"路 A 装没装"，不谎称能单独关一台）。
 *          [浊度是例外 2026-11-10] 两台浊度仪虽是同一路 B，但它们是两台
 *          **独立设备**（用户定案）：各自有 turbidity1Enabled/2Enabled，
 *          停用只停该从站的轮询，另一台照采；turbidityEnabled 仍回"任一
 *          启用"，作旧客户端的兼容缺省。
 */
static void hsfAppHandleConfigGet(XLinkSession* session,
                                  const XLinkFrameHeader* frame)
{
    XBsonDocument* doc;
    XBsonDocument* collect;
    XBsonDocument* storage;
    XBsonDocument* stats;

    doc = XBsonDocument_create();
    if (!doc) {
        hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "应答文档创建失败"));
        return;
    }
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, true);

    /* ---- 采集节 ---- */
    collect = XBsonDocument_create();
    if (collect) {
        HsfCollectConfig eff;
        const bool sideA = hsfCollectEnabled("vfdEnabled", HSF_BUS_A_ENABLED);
        const bool sideB = hsfCollectEnabled("turbidityEnabled", HSF_BUS_B_ENABLED);
        HsfServerIo_collectConfig(&eff);
        XBsonDocument_insert_keyUtf8_bool(collect, HSF_CFGK_VFD_ENABLED, sideA);
        XBsonDocument_insert_keyUtf8_bool(collect, HSF_CFGK_TEMPHUM_ENABLED, sideA);
        /* [旧键 = 任一启用] 旧客户端把它当"浊度这路开没开"；逐台键见下。 */
        XBsonDocument_insert_keyUtf8_bool(collect, HSF_CFGK_TURBIDITY_ENABLED,
                                          eff.turbidity1Enabled
                                              || eff.turbidity2Enabled);
        XBsonDocument_insert_keyUtf8_bool(collect, HSF_CFGK_FLOWMETER_ENABLED, sideB);
        XBsonDocument_insert_keyUtf8_bool(collect, HSF_CFGK_ANALOG_ENABLED,
                                         hsfCollectEnabled("analogEnabled", 1));
        XBsonDocument_insert_keyUtf8_bool(collect, HSF_CFGK_DIGITAL_ENABLED,
                                         hsfCollectEnabled("digitalEnabled",
                                                           HSF_DIG_ENABLED));
        /* [回显真值 2026-11-10] 从 HsfServerIo 取**当前生效**的节拍（INI 已在
         * 装配期/HOT 应用时落到 Hub）——不再直接回显 INI 字面值，避免"界面
         * 显示新值、实际还是旧节拍"的假象。 */
        {
            /* [逐台开关 2026-11-10] 两台浊度是独立设备，开关各报各的. */
            XBsonDocument_insert_keyUtf8_bool(collect,
                                              HSF_CFGK_TURBIDITY1_ENABLED,
                                              eff.turbidity1Enabled);
            XBsonDocument_insert_keyUtf8_bool(collect,
                                              HSF_CFGK_TURBIDITY2_ENABLED,
                                              eff.turbidity2Enabled);
            XBsonDocument_insert_keyUtf8_int32(collect, HSF_CFGK_VFD_POLL_MS,
                                               eff.vfdPollMs);
            XBsonDocument_insert_keyUtf8_int32(collect, HSF_CFGK_TEMPHUM_POLL_MS,
                                               eff.temphumPollMs);
            XBsonDocument_insert_keyUtf8_int32(collect, HSF_CFGK_TURBIDITY_POLL_MS,
                                               eff.turbidityPollMs);
            XBsonDocument_insert_keyUtf8_int32(collect, HSF_CFGK_TURBIDITY1_POLL_MS,
                                               eff.turbidity1PollMs);
            XBsonDocument_insert_keyUtf8_int32(collect, HSF_CFGK_TURBIDITY2_POLL_MS,
                                               eff.turbidity2PollMs);
            XBsonDocument_insert_keyUtf8_int32(collect, HSF_CFGK_FLOWMETER_POLL_MS,
                                               eff.flowmeterPollMs);
            XBsonDocument_insert_keyUtf8_int32(collect, HSF_CFGK_ANALOG_POLL_MS,
                                               eff.analogPollMs);
            XBsonDocument_insert_keyUtf8_int32(collect, HSF_CFGK_DIGITAL_POLL_MS,
                                               eff.digitalPollMs);
        }
        XBsonDocument_insert_keyUtf8_document(doc, HSF_CFG_SECTION_COLLECT,
                                               collect);
        /* [config-get 泄漏根修 2026-11-08] 所有权口径（按实现核对）：
         * insert_keyUtf8_document → XBsonValue_setDocument →
         * XBsonDocument_create_copy → XClassCopy → VXClass_copy
         * （XContainer_virtual.c:117-122）：COW 容器只共享 m_data 并
         * XSharedData_addRef，**不是元素级深拷贝**。插入只增加共享块引用，
         * 源 collect 的初始引用仍归调用方；原先只插入不释放 ⇒ doc 删除后
         * 共享块仍剩 1 个引用，整棵子文档（元素向量 + 键串）常驻堆上。
         * 实测：服务端堆每调用一次 CONFIG_GET +13,580 字节（C 客户端每次
         * 连接发一次 ⇒ 每轮固定漏 13,580），Python 探针不发该命令时 +0。
         * 后续若想省掉引用计数操作可改用 insert_keyUtf8_document_move/_ref
         * 变体（本轮只补所有权）。 */
        XClassDelete((XClass*)collect);
    }

    /* ---- 落盘节 ---- */
    storage = XBsonDocument_create();
    if (storage) {
        char path[256];
        HsfServerSettings_getString("storage/path", "", path, (int)sizeof(path));
        XBsonDocument_insert_keyUtf8_bool(storage, HSF_CFGK_STORAGE_ENABLED,
                                         HsfServerStore_enabled(&s_store));
        XBsonDocument_insert_keyUtf8_utf8(storage, HSF_CFGK_STORAGE_PATH, path);
        XBsonDocument_insert_keyUtf8_int32(storage, HSF_CFGK_STORAGE_SAMPLE_MS,
                                           HsfServerStore_sampleMs(&s_store));
        XBsonDocument_insert_keyUtf8_int32(storage, HSF_CFGK_STORAGE_MAX_MB,
                                           HsfServerSettings_getInt(
                                               "storage/max_mb", 0));
        {
            /* [策略键与 Python 同词] ring/stop/rotate；旧 INI
             * （keepOldest）与"键不存在读到 0"一律归一为 ring。 */
            char policy[32];
            HsfServerSettings_getString("storage/full_policy", "ring",
                                        policy, (int)sizeof(policy));
            if (strcmp(policy, "stop") != 0 &&
                strcmp(policy, "rotate") != 0 &&
                strcmp(policy, "ring") != 0)
                snprintf(policy, sizeof(policy), "ring");
            XBsonDocument_insert_keyUtf8_utf8(storage,
                HSF_CFGK_STORAGE_FULL_POLICY, policy);
        }
        {
            int keepFiles = HsfServerSettings_getInt("storage/keep_files", 3);
            if (keepFiles < 1) keepFiles = 3;   /* 缺键/脏值回缺省。 */
            XBsonDocument_insert_keyUtf8_int32(storage,
                HSF_CFGK_STORAGE_KEEP_FILES, keepFiles);
        }
        XBsonDocument_insert_keyUtf8_document(doc, HSF_CFG_SECTION_STORAGE,
                                               storage);
        /* 同上：COW 共享 + 引用计数（非元素级深拷贝），插入只 addRef，
         * 源 storage 的初始引用仍需调用方释放，否则每轮 CONFIG_GET 常驻
         * 一棵落盘节子文档。 */
        XClassDelete((XClass*)storage);
    }
    {
        const char* dbPath = HsfServerStore_dbPath(&s_store);
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_CFG_STORAGE_DB_PATH,
                                         dbPath ? dbPath : "");
    }
    /* ---- 登录/账户策略节（2026-11-08 新增；管理员可在远端设置里改）---- */
    {
        XBsonDocument* login = XBsonDocument_create();
        if (login) {
            XBsonDocument_insert_keyUtf8_bool(
                login, HSF_CFGK_LOGIN_ALLOW_ANON,
                XLoginPolicy_anonymousAllowed(&s_loginPolicy));
            XBsonDocument_insert_keyUtf8_bool(
                login, HSF_CFGK_LOGIN_ALLOW_CONCUR,
                XLoginPolicy_concurrentAllowed(&s_loginPolicy));
            XBsonDocument_insert_keyUtf8_int32(
                login, HSF_CFGK_LOGIN_MAX_SESSIONS,
                (int32_t)XLoginPolicy_maxSessions(&s_loginPolicy));
            XBsonDocument_insert_keyUtf8_document(doc, HSF_CFG_SECTION_LOGIN,
                                                  login);
            /* COW 共享语义同 collect/storage：插入只 addRef，源仍需释放。 */
            XClassDelete((XClass*)login);
        }
    }
    /* ---- 容量实况（能报多少报多少；C 端没有的分段/丢弃统计就报 0）---- */
    stats = XBsonDocument_create();
    if (stats) {
        int64_t used = HsfServerStore_usedBytes(&s_store);
        int64_t rows = HsfServerStore_rowCount(&s_store);
        const int maxMb = HsfServerSettings_getInt("storage/max_mb", 0);
        XBsonDocument_insert_keyUtf8_int64(stats, "usedBytes", used);
        XBsonDocument_insert_keyUtf8_double(stats, "usedMb",
                                           (double)used / (1024.0 * 1024.0));
        XBsonDocument_insert_keyUtf8_int64(stats, "contentBytes", used);
        XBsonDocument_insert_keyUtf8_double(stats, "contentMb",
                                           (double)used / (1024.0 * 1024.0));
        XBsonDocument_insert_keyUtf8_int32(stats, "maxMb", maxMb);
        XBsonDocument_insert_keyUtf8_int32(stats, "maxKb", maxMb * 1024);
        {
            char policy[32];
            HsfServerSettings_getString("storage/full_policy", "ring",
                                        policy, (int)sizeof(policy));
            if (strcmp(policy, "stop") != 0 &&
                strcmp(policy, "rotate") != 0 &&
                strcmp(policy, "ring") != 0)
                snprintf(policy, sizeof(policy), "ring");
            XBsonDocument_insert_keyUtf8_utf8(stats, "policy", policy);
        }
        {
            int keepFiles = HsfServerSettings_getInt("storage/keep_files", 3);
            if (keepFiles < 1) keepFiles = 3;
            XBsonDocument_insert_keyUtf8_int32(stats, "keepFiles", keepFiles);
        }
        XBsonDocument_insert_keyUtf8_bool(stats, "paused", false);
        XBsonDocument_insert_keyUtf8_utf8(stats, "pauseReason", "");
        XBsonDocument_insert_keyUtf8_int64(stats, "droppedRows", 0);
        XBsonDocument_insert_keyUtf8_int64(stats, "rows", rows);
        XBsonDocument_insert_keyUtf8_int32(stats, "segments", 1);
        XBsonDocument_insert_keyUtf8_bool(stats, "enabled",
                                         HsfServerStore_enabled(&s_store));
        XBsonDocument_insert_keyUtf8_utf8(stats, "dbPath",
                                         HsfServerStore_dbPath(&s_store)
                                             ? HsfServerStore_dbPath(&s_store) : "");
        XBsonDocument_insert_keyUtf8_document(doc, HSF_CFG_STORAGE_STATS, stats);
        /* 同上：COW 共享 + addRef，源 stats 的初始引用必须由调用方释放；
         * 容量实况子文档元素最多，单次占比最大。 */
        XClassDelete((XClass*)stats);
    }

    {
        XByteArray* bytes = XBsonDocument_toBson(doc);
        XClassDelete((XClass*)doc);
        hsfAppRespond(session, frame, bytes);
    }
}

/**
 * @brief 远端配置写入（HSF_CMD_CONFIG_SET）：需 root。
 * @details 载荷 {section:"collect"|"storage", …键}。
 *          [落盘节运行期生效] enabled/sample_ms 立即生效（重装 store 配置）；
 *          path 落盘但**下次启动**才换库（换库要重开连接，运行期换会丢数据）。
 *          [采集节持久化] C 端采集参数是编译期常量（写在 HsfServerConfig.h，
 *          与现场接线同性质），故这里只把值**落进 INI**，重启服务端后生效；
 *          应答里明说"重启后生效"，不假装已经改好。
 */
static void hsfAppHandleConfigSet(XLinkSession* session,
                                  const XLinkFrameHeader* frame,
                                  const uint8_t* payload, XLinkServer* srv)
{
    XBsonDocument* doc;
    XBsonValue* v;
    char section[32];
    bool any = false;

    /* [权限] 改服务端配置=管理面动作（AccountAdmin，管理员专属）；
     * 操作者只能控设备，不得改采集/落盘策略。但**不看自动模式闸**——改配置
     * 不动任何输出点，自动运行中允许调采集频率/开关（监视不受模式限制）。 */
    if (!XPermPolicy_check(&s_perm,
                           (XAccountRole)XLinkServer_sessionRole(srv, session),
                           (uint32_t)XAccessPerm_AccountAdmin)) {
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(false, hsfAppPermDeniedText(
                          (uint32_t)XAccessPerm_AccountAdmin)));
        return;
    }
    doc = XBsonDocument_create();
    if (!doc || !XBsonDocument_from_bytes(doc, payload, frame->m_payloadLen)) {
        if (doc) XClassDelete((XClass*)doc);
        hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "配置载荷解析失败"));
        return;
    }
    v = XBsonDocument_value_keyUtf8(doc, HSF_CFG_SECTION_KEY);
    if (v) {
        /* [XBsonValue_toString 返回借用的 const XString*] 用 XVariant_toString
         * 会拿到 NULL（BSON 字符串的变体类型与 XString_fromVariant 的期望
         * 不匹配）⇒ section 留着未初始化内容，后面 strcmp 全不匹配（实测
         * 报"未知配置段"）。另外 XString_toUtf8 是**借用**的内部缓冲，
         * 必须拷进本地缓冲再用。 */
        const XString* sec = XBsonValue_toString(v);
        if (sec) snprintf(section, sizeof(section), "%s", XString_toUtf8(sec));
    }
    if (!section || !section[0]) {
        XClassDelete((XClass*)doc);
        hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "载荷缺少 section"));
        return;
    }

    if (strcmp(section, HSF_CFG_SECTION_STORAGE) == 0) {
        /* 落盘节：写 INI + 运行期生效。 */
        v = XBsonDocument_value_keyUtf8(doc, HSF_CFGK_STORAGE_ENABLED);
        if (v) {
            HsfServerSettings_setInt("storage/enabled",
                                     XBsonValue_toBool(v, false) ? 1 : 0);
            any = true;
        }
        v = XBsonDocument_value_keyUtf8(doc, HSF_CFGK_STORAGE_SAMPLE_MS);
        if (v) {
            /* [XBsonValue 取值必须用 XBson 层自己的口] XBsonDocument_
             * value_keyUtf8 返回的是 **XBsonValue***，拿它当 XVariant* 用
             * （XVariant_toInt）是**类型错乱** ⇒ 取值恒 0（实测写入的采样
             * 周期被当成 0 夹到 1000，读回也是 1000）。 */
            int ms = (int)XBsonValue_toInt32(v, 0);
            if (ms <= 0) ms = (int)XBsonValue_toInt64(v, 0);
            if (ms < 1000) ms = 1000;
            HsfServerSettings_setInt("storage/sample_ms", ms);
            any = true;
        }
        v = XBsonDocument_value_keyUtf8(doc, HSF_CFGK_STORAGE_PATH);
        if (v) {
            const XString* pstr = XBsonValue_toString(v);
            HsfServerSettings_setString("storage/path",
                                       pstr ? XString_toUtf8(pstr) : "");
            any = true;
        }
        v = XBsonDocument_value_keyUtf8(doc, HSF_CFGK_STORAGE_MAX_MB);
        if (v) {
            HsfServerSettings_setInt("storage/max_mb", (int)XBsonValue_toInt32(v, 0));
            any = true;
        }
        v = XBsonDocument_value_keyUtf8(doc, HSF_CFGK_STORAGE_KEEP_FILES);
        if (v) {
            HsfServerSettings_setInt("storage/keep_files", (int)XBsonValue_toInt32(v, 0));
            any = true;
        }
        v = XBsonDocument_value_keyUtf8(doc, HSF_CFGK_STORAGE_FULL_POLICY);
        if (v) {
            /* [策略键与 Python 同词] ring/stop/rotate；旧值 keepOldest 映射 ring。 */
            const XString* ps = XBsonValue_toString(v);
            const char* pu = ps ? XString_toUtf8(ps) : "";
            if (pu && (strcmp(pu, "ring") == 0 ||
                       strcmp(pu, "stop") == 0 ||
                       strcmp(pu, "rotate") == 0)) {
                HsfServerSettings_setString("storage/full_policy", pu);
                any = true;
            } else if (pu && strcmp(pu, "keepOldest") == 0) {
                HsfServerSettings_setString("storage/full_policy", "ring");
                any = true;
            } else {
                XClassDelete((XClass*)doc);
                hsfAppRespond(session, frame,
                              hsfAppMakeEnvelope(false,
                                  "未知的落盘策略（ring/stop/rotate）"));
                return;
            }
        }
        XClassDelete((XClass*)doc);
        if (!any) {
            hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "载荷里没有配置键"));
            return;
        }
        /* 运行期生效（enabled/sample_ms 重读配置；path 下次启动换库）。 */
        (void)HsfServerStore_configure(&s_store, HsfServerSettings_dataDir());
        XPrintf("HsfServerApp: 远程配置：落盘节已更新（运行期生效）\n");
        hsfAppRespond(session, frame,
                      hsfAppMakeEnvelope(true, "落盘配置已保存（库路径下次启动生效）"));
        return;
    }

    if (strcmp(section, HSF_CFG_SECTION_COLLECT) == 0) {
        /* 采集节：C 端是编译期常量 ⇒ 只落 INI，重启后生效。 */
        static const char* const kBoolKeys[] = {
            HSF_CFGK_VFD_ENABLED, HSF_CFGK_TEMPHUM_ENABLED,
            HSF_CFGK_FLOWMETER_ENABLED, HSF_CFGK_TURBIDITY_ENABLED,
            /* [逐台开关 2026-11-10] 两台浊度各设各的。 */
            HSF_CFGK_TURBIDITY1_ENABLED, HSF_CFGK_TURBIDITY2_ENABLED,
            HSF_CFGK_ANALOG_ENABLED, HSF_CFGK_DIGITAL_ENABLED
        };
        static const char* const kIntKeys[] = {
            HSF_CFGK_VFD_POLL_MS, HSF_CFGK_TEMPHUM_POLL_MS,
            HSF_CFGK_TURBIDITY_POLL_MS,
            /* [逐台 2026-11-10] 两台浊度 + 流量计各自可设。 */
            HSF_CFGK_TURBIDITY1_POLL_MS, HSF_CFGK_TURBIDITY2_POLL_MS,
            HSF_CFGK_FLOWMETER_POLL_MS,
            HSF_CFGK_ANALOG_POLL_MS, HSF_CFGK_DIGITAL_POLL_MS
        };
        int i;
        for (i = 0; i < (int)(sizeof(kBoolKeys) / sizeof(kBoolKeys[0])); ++i) {
            v = XBsonDocument_value_keyUtf8(doc, kBoolKeys[i]);
            if (v) {
                /* 落 INI 的 collect/<键>（键名与协议同名，便于回读）。 */
                char iniKey[96];
                snprintf(iniKey, sizeof(iniKey), "collect/%s", kBoolKeys[i]);
                HsfServerSettings_setInt(iniKey, XBsonValue_toBool(v, false) ? 1 : 0);
                /* [旧客户端只发共同键] 它没有逐台概念 ⇒ 视作"两台一起"。必须
                 * 同步写逐台键：否则逐台键一旦存在，共同键就改不动实际门控
                 * （hsfIoCollectFromIni 优先逐台），表现为"改了没反应"。 */
                if (strcmp(kBoolKeys[i], HSF_CFGK_TURBIDITY_ENABLED) == 0) {
                    const int on = XBsonValue_toBool(v, false) ? 1 : 0;
                    HsfServerSettings_setInt("collect/turbidity1Enabled", on);
                    HsfServerSettings_setInt("collect/turbidity2Enabled", on);
                }
                any = true;
            }
        }
        for (i = 0; i < (int)(sizeof(kIntKeys) / sizeof(kIntKeys[0])); ++i) {
            v = XBsonDocument_value_keyUtf8(doc, kIntKeys[i]);
            if (v) {
                char iniKey[96];
                snprintf(iniKey, sizeof(iniKey), "collect/%s", kIntKeys[i]);
                HsfServerSettings_setInt(iniKey, (int)XBsonValue_toInt32(v, 0));
                any = true;
            }
        }
        XClassDelete((XClass*)doc);
        if (!any) {
            hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "载荷里没有配置键"));
            return;
        }
        /* [运行期生效 2026-11-10 根修] 此前这里只写 INI 并回「重启后生效」，
         * 而**没有任何代码消费 collect/***（装配期只读编译期宏）⇒ 远端设置
         * 改采集时间永远无效，连重启都不生效——用户反馈「某些设备的采集时间
         * 貌似设置时无效」的真正根因。现在写完 INI 立刻应用到 Hub 的从站最小
         * 发送间隔 / 设备节拍 / 两个 TCP 站的轮询周期（HsfServerIo），与
         * Python 端「下一拍生效」终于同口径。 */
        {
            const bool applied = HsfServerIo_applyCollectConfig(NULL);
            XPrintf("HsfServerApp: 远程配置：采集节%s\n",
                    applied ? "已应用（运行期生效）"
                            : "已保存（无装配成功的从站，下次启动生效）");
            hsfAppRespond(session, frame,
                          hsfAppMakeEnvelope(true,
                                             applied
                                                 ? "采集配置已应用（运行期立即生效）"
                                                 : "采集配置已保存（无在线从站，下次启动生效）"));
        }
        return;
    }

    if (strcmp(section, HSF_CFG_SECTION_LOGIN) == 0) {
        /* 登录/账户策略节（2026-11-08）：**立即对下一次登录生效**并落盘
         * hsf_login_policy.json；存量会话不受影响（关并发不会踢人）。
         * 会话上限夹到 0~256（0=不限）。 */
        v = XBsonDocument_value_keyUtf8(doc, HSF_CFGK_LOGIN_ALLOW_ANON);
        if (v) {
            (void)XLoginPolicy_setAllowAnonymous(&s_loginPolicy,
                                                XBsonValue_toBool(v, false));
            any = true;
        }
        v = XBsonDocument_value_keyUtf8(doc, HSF_CFGK_LOGIN_ALLOW_CONCUR);
        if (v) {
            (void)XLoginPolicy_setAllowConcurrent(&s_loginPolicy,
                                                  XBsonValue_toBool(v, false));
            any = true;
        }
        v = XBsonDocument_value_keyUtf8(doc, HSF_CFGK_LOGIN_MAX_SESSIONS);
        if (v) {
            int32_t maxSessions = XBsonValue_toInt32(v, 0);
            if (maxSessions < 0) maxSessions = 0;
            if (maxSessions > 256) maxSessions = 256;
            (void)XLoginPolicy_setMaxSessions(&s_loginPolicy,
                                              (uint32_t)maxSessions);
            any = true;
        }
        XClassDelete((XClass*)doc);
        if (!any) {
            hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "载荷里没有配置键"));
            return;
        }
        {
            /* 落盘失败不谎报成功：策略已运行期生效，但重启会回旧值。 */
            const bool saved = XLoginPolicy_save(&s_loginPolicy);
            XPrintf("HsfServerApp: 远程配置：登录策略已更新（匿名=%d 同账并发=%d "
                    "上限=%llu，落盘=%s）\n",
                    XLoginPolicy_anonymousAllowed(&s_loginPolicy) ? 1 : 0,
                    XLoginPolicy_concurrentAllowed(&s_loginPolicy) ? 1 : 0,
                    (unsigned long long)XLoginPolicy_maxSessions(&s_loginPolicy),
                    saved ? "是" : "否");
            hsfAppRespond(session, frame,
                          hsfAppMakeEnvelope(saved, saved
                              ? "登录策略已更新（下次登录生效）"
                              : "登录策略已生效，但落盘失败（重启后回旧值）"));
        }
        return;
    }

    XClassDelete((XClass*)doc);
    hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "未知配置段"));
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

/**
 * @brief 权限点不足的提示原文（与 Python 服务端 app.py 同口径）。
 */
static const char* hsfAppPermDeniedText(uint32_t perm)
{
    switch (perm) {
    case (uint32_t)XAccessPerm_Download:
        return "权限不足：下载数据需要普通用户及以上账户";
    case (uint32_t)XAccessPerm_DeviceControl:
        return "权限不足：设备控制需要操作者或管理员账户";
    case (uint32_t)XAccessPerm_ModeSwitch:
        return "权限不足：切换运行模式需要管理员账户";
    case (uint32_t)XAccessPerm_AutoControl:
        return "权限不足：自动运行控制需要管理员账户";
    case (uint32_t)XAccessPerm_AccountAdmin:
        return "权限不足：修改服务端配置需要管理员账户";
    case (uint32_t)XAccessPerm_ViewTelemetry:
    default:
        return "权限不足：查看数据需要已认证会话";
    }
}

/**
 * @brief 会话权限判定（角色 → XPermPolicy 位图；拒绝时策略自身留痕）。
 */
static bool hsfAppSessionCan(XLinkServer* srv, XLinkSession* session,
                             uint32_t perm)
{
    if (!srv || !session) return false;
    return XPermPolicy_check(&s_perm,
                             (XAccountRole)XLinkServer_sessionRole(srv, session),
                             perm);
}

/**
 * @brief 命令 → 所需权限点（**服务端强制门**，0=由处理函数细分）。
 * @details [为什么集中一张表] 权限要"实际生效"而不是靠客户端灰按钮：
 *          所有请求帧在进 switch 之前先过这道闸，新增命令忘记加闸时
 *          默认落到 View（只读），不会被越权写入。
 *          RUN_MODE 是"查询/设置同号"命令、HISTORY_PAGE 是"浏览/导出
 *          同号"命令，只能先按只读放行，再由处理函数细分（设置需
 *          ModeSwitch、导出需 Download）。
 */
static uint32_t hsfAppCommandPerm(uint16_t cmd)
{
    switch (cmd) {
    case HSF_CMD_VFD_CONTROL:
    case HSF_CMD_DIGITAL_CONTROL:
    case HSF_CMD_VALVE_CONTROL:
    case HSF_CMD_PUMP_CONTROL:
        return (uint32_t)XAccessPerm_DeviceControl;
    case HSF_CMD_AUTO_CONTROL:
        return (uint32_t)XAccessPerm_AutoControl;
    case HSF_CMD_CONFIG_SET:
        return (uint32_t)XAccessPerm_AccountAdmin;
    case HSF_CMD_LOG_PAGE:
    default:
        /* 全部读面（快照/健康/历史/日志/配置读/RUN_MODE 查询）。 */
        return (uint32_t)XAccessPerm_ViewTelemetry;
    }
}

/**
 * @brief 推送失步补发（会话 TX 队列「队满拒帧→重新排空」时被回调）。
 * @details [为什么必须补发] 拥塞窗内被整帧拒绝的 Publish 没有重发兜底：
 *          周期遥测下一拍会带来新值，但**变值事件帧**（设备健康变化、阀
 *          告警、单次状态跃迁）只发一次，丢一帧对端就永久停在旧状态——
 *          现场即「界面数据冻结、连接显示正常、操作没反应」。此处按该
 *          会话的订阅表把全部遥测主题补发一遍全量快照（与订阅即补发
 *          同一条路径 hsfAppPublishTopicNow），不断链、不重置会话。
 *          [踢线] 补发后若消费端仍然过慢、队列长期不排空，会话层的
 *          失步踢线（XLINK_TX_DESYNC_KICK_MS）负责淘汰，客户端重连+
 *          重订阅完成再同步——两级自愈各管一段。
 */
static void hsfAppTxResync(XLinkSession* session, void* userData)
{
    static const uint32_t kTopics[] = {
        HSF_TOPIC_VFD_STATUS,
        HSF_TOPIC_AI_STATUS,
        HSF_TOPIC_TEMPHUM_STATUS,
        HSF_TOPIC_FLOWMETER_STATUS,
        HSF_TOPIC_DIGITAL_STATUS,
        HSF_TOPIC_VALVE_STATUS,
        HSF_TOPIC_TURBIDITY_STATUS,
        HSF_TOPIC_PUMP_STATUS,
        HSF_TOPIC_DEVICE_HEALTH,
    };
    int sent = 0;
    size_t i;
    (void)userData;

    if (!session || !s_srv) return;
    for (i = 0; i < sizeof(kTopics) / sizeof(kTopics[0]); ++i) {
        if (!XLinkSession_isSubscribed(session, kTopics[i])) continue;
        if (kTopics[i] == HSF_TOPIC_DEVICE_HEALTH) {
            /* 健康是事件驱动主题：与订阅补发同路径，回一帧当前全量。 */
            XByteArray* bytes;
            hsfHealthCollect(&s_health);
            s_health.m_primed = true;
            bytes = hsfHealthToBson(&s_health);
            if (bytes) {
                if (XLinkSession_sendPublish_ex(session, HSF_TOPIC_DEVICE_HEALTH,
                                                XLinkDataType_Bson,
                                                (const uint8_t*)XByteArray_constData(bytes),
                                                (uint32_t)XByteArray_size_base(bytes)))
                    ++sent;
                XClassDelete((XClass*)bytes);
            }
            continue;
        }
        hsfAppPublishTopicNow(s_srv, session, kTopics[i]);
        ++sent;
    }
    XPrintf("HsfServerApp: 推送失步补发（%d 主题，会话已排空）\n", sent);
}

static void hsfAppDispatch(XLinkSession* session, const XLinkFrameHeader* frame,
                           const uint8_t* payload)
{
    XLinkServer* srv = s_srv;
    if (!session || !frame) return;
    switch (frame->m_msgClass) {
    case XLinkMsgClass_Request: {
        const uint32_t need = hsfAppCommandPerm(frame->m_msgCode);
        if (need && !hsfAppSessionCan(srv, session, need)) {
            hsfAppRespond(session, frame,
                          hsfAppMakeEnvelope(false, hsfAppPermDeniedText(need)));
            return;
        }
    }
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
        case HSF_CMD_HISTORY_PAGE:
            hsfAppHandleHistoryPage(session, frame, payload);
            break;
        case HSF_CMD_LOG_PAGE:
            hsfAppHandleLogPage(session, frame, payload);
            break;
        case HSF_CMD_CONFIG_GET:
            hsfAppHandleConfigGet(session, frame);
            break;
        case HSF_CMD_CONFIG_SET:
            hsfAppHandleConfigSet(session, frame, payload, srv);
            break;
        default:
            /* 未知命令：回否定应答，客户端可感知（不发则对端空等）。 */
            hsfAppRespond(session, frame, hsfAppMakeEnvelope(false, "未知命令"));
            break;
        }
        break;
    case XLinkMsgClass_Subscribe: {
        /* [读面权限闸] 订阅=查看，需 ViewTelemetry（匿名持有；策略收紧后
         * 在此自动生效，不必逐主题补闸）。 */
        if (!hsfAppSessionCan(srv, session,
                              (uint32_t)XAccessPerm_ViewTelemetry)) {
            XPrintf("HsfServerApp: 订阅被拒（角色%d）\n",
                    (int)XLinkServer_sessionRole(srv, session));
            break;
        }
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
 *          缺省策略（匿名关/同账并发关/上限 8），语义零漂移。
 *          [拒绝码：Rejected，不是 BadUser 2026-11-09 根修]
 *          策略闸拒绝的是**服务端瞬时状态**（同账并发/会话数上限/
 *          KickOld 未实现），凭据本身可能是对的。原先返回 BadUser 会被
 *          框架的反枚举路径改写成「假挑战 → 必然不匹配 → BadPassword」，
 *          客户端把「root 已在别处登录」误判成「口令错误」而**永久停止
 *          自动重连**：一次 6s 链路抖动（旧会话尚未回收）就让界面永久
 *          全 `--`，人工重登才好（实锅）。改返回 Rejected 后框架直接下发
 *          真实拒绝码，客户端按瞬时失败退避重试，旧会话一被回收即自动
 *          恢复；用户不存在/锁定仍走 BadUser/Locked 的假挑战路径，反枚举
 *          口径不变。
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
        return XLinkSessionAuthLookup_Rejected;
    }
    if (kick) {
        /* 本批无单会话关闭 API：按拒绝处理（见函数头说明）。 */
        XPrintf("HsfServerApp: KickOld 模式暂不支持（无单会话关闭 API），"
                "按拒绝处理：%s\n", user);
        return XLinkSessionAuthLookup_Rejected;
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
    /* [登录成功入库] 与登录被拒（hsfAppOnLoginRejected）同一 auth 类，
     * 账户审计在日志页一张时间线上看得全。 */
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "登录成功：%s", user);
        hsfAppEventLog("auth", buf);
    }
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
        {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "会话接入（当前 %d 路，dev=%p sockState=%d peerPort=%u）",
                     XLinkServer_sessionCount(self), (void*)dev, st,
                     dev ? (unsigned)XAbstractSocket_peerPort((XAbstractSocket*)dev) : 0u);
            hsfAppEventLog("conn", buf);
        }
    } else {
        {
            char buf[64];
            snprintf(buf, sizeof(buf), "会话断开（当前 %d 路）",
                     XLinkServer_sessionCount(self));
            hsfAppEventLog("conn", buf);
        }
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
        /* [陈旧阈值回读 2026-11-10] 报**设备层当前生效**的阈值（随采集
         * 周期联动，见 HsfServerIo.c 的 hsfIoStaleMs），而不是写死宏——
         * 否则周期改大后这里仍报 5s，与实际判定口径不符（排查时会被带偏）。 */
        {
            XTempHumTransmitter* thDev = HsfServerIoTempHum_device(th);
            XBsonDocument_insert_keyUtf8_int32(
                doc, "staleMs",
                thDev ? (int32_t)XTempHumTransmitter_staleMs(thDev)
                      : (int32_t)HSF_TEMPHUM_STALE_MS);
        }
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
        const HsfServerIoTempHum* th;
        if (!hsfAppPushDue(&s_pushGates[HSF_PUSH_GATE_TEMPHUM][si], nowMs))
            continue;
        /* [未装配的侧不推空帧 2026-11-09 根修]
         * 同一主题被两条侧轮流推时，客户端只保留**最后一帧**（Python 端
         * TempHumState 就是这样），所以"这一侧没装温湿度站"推出去的空帧
         * 会把另一侧的真数据盖掉 ⇒ 界面恒为 "--"（现场实测：Python 客户端
         * 温湿度恒无数据；C 客户端按侧取快照所以当时看着正常）。
         * 口径与变频器一致（hsfAppTickVfd：未就绪的侧跳过）——没装设备的
         * 侧不占这个主题。 */
        th = HsfServerIo_tempHum(side);
        if (!th) continue;
        hsfAppPublishTempHum(self, side, th);
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
        int ti;
        bool hasTurb = false;
        if (!hsfAppPushDue(&s_pushGates[HSF_PUSH_GATE_TURBIDITY][si], nowMs))
            continue;
        /* [未装配的侧不推空帧 2026-11-09] 同温湿度主题：客户端只保留
         * 最后一帧，未装配侧的空帧（online=false）会把真数据盖掉 ⇒ 界面
         * 恒"--"。口径同 hsfAppTickVfd（未就绪的侧跳过）。 */
        for (ti = 0; ti < HSF_TURBIDITY_COUNT; ++ti) {
            if (HsfServerIo_turbidity((HsfSide)si, ti)) { hasTurb = true; break; }
        }
        if (!hasTurb) continue;
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
        const HsfServerIoFlowMeter* fm;
        if (!hsfAppPushDue(&s_pushGates[HSF_PUSH_GATE_FLOWMETER][si], nowMs))
            continue;
        /* [未装配的侧不推空帧 2026-11-09] 同温湿度主题：未装配侧的空帧
         * 会覆盖另一侧的真数据 ⇒ 客户端恒"无数据"。 */
        fm = HsfServerIo_flowMeter((HsfSide)si);
        if (!fm) continue;
        doc = XBsonDocument_create();
        if (!doc) continue;
        hsfAppInsertSide(doc, (HsfSide)si);
        hsfAppFillFlowMeter(doc, fm);
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
    /* [无条件停机 —— 对齐 Python 侧 _jog_release] 看门狗态与设备真实状态
     * 可能失步：服务端重启过、或点动帧在途被丢（首帧没被受理时看门狗
     * 压根没置位），此时客户端松手发来的释放帧会因"未在点动态"被跳过
     * ⇒ 设备仍在点动、客户端却已放手 ⇒ 电机停不下来。
     * 松手是**安全动作**：多发这一帧的代价只是几十毫秒的减速停机，
     * 漏发的代价是电机一直跑。 */
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
        /* [侧号由服务端解析 2026-11-10] 补发也按**实际装配侧**——原先
         * 硬编码 A 侧，而现场流量计在 B 侧 ⇒ 客户端首连拿到的是空帧
         * （valid=false），首屏显示"无数据"。 */
        const HsfSide fmSide = HsfServerIo_flowMeterSide(HsfSide_A);
        if (doc) {
            hsfAppInsertSide(doc, fmSide);
            hsfAppFillFlowMeter(doc, HsfServerIo_flowMeter(fmSide));
            bytes = XBsonDocument_toBson(doc);
            XClassDelete((XClass*)doc);
        }
    } else if ((topic & 0xFFFFu) == (HSF_TOPIC_TURBIDITY_STATUS & 0xFFFFu)) {
        XBsonDocument* doc = XBsonDocument_create();
        /* [侧号由服务端解析 2026-11-10] 同上：浊度仪在 B 侧。 */
        const HsfSide tbSide = HsfServerIo_turbiditySide(HsfSide_A);
        if (doc) {
            hsfAppInsertSide(doc, tbSide);
            hsfAppFillTurbidity(doc, tbSide);
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
 * @brief 生成数字量通道名（宏缺省 + 阀/泵/按钮组点位覆盖）。
 * @details 与 Python 侧 `_digital_channel_names` / `_pump_channel_names` /
 *          `_button_channel_names` 同口径，**覆盖顺序也必须同序**：
 *          1) 宏里通用名（"输入1..32"/"输出1..32"）；
 *          2) 阀 DO="<name> 控制"、开到位 DI="<name> 开到位"、关到位 DI=
 *             "<name> 关到位"；
 *          3) 泵 DO="<name>"（不加后缀）；
 *          4) 按钮组 DI12~15 / DO7~9（点位取 HSF_BTN_* 宏）。
 *          后段覆盖前段，段序即优先级；服务端是名字的唯一事实源，
 *          客户端只按帧更新。
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
    /* [按钮组：现场操作柱的 4 触点 + 3 灯] 放在**最后**覆盖——顺序与
     * Python 侧 app.py 的登记链同构：_digital_channel_names（通用名/阀）→
     * _pump_channel_names（泵 DO6）→ _button_channel_names（DI12~15 /
     * DO7~9）。后写的赢，顺序一变就会互相覆盖，勿调换。
     * 名字措辞与 Python 逐字一致：DI 侧带动作语义（"急停（常闭）"——该点
     * 表达的是"回路是否正常"），DO 侧是灯（"启动按钮灯"）。
     * 点位一律用 HSF_BTN_* 宏（HsfServerConfig.h），不写死数字；
     * 越界（count 小于点位号）不写，避免污染数组外。 */
    {
        static const struct {
            int m_ch;
            const char* m_text;
        } kBtnDi[] = {
            { HSF_BTN_ESTOP_DI,      "急停（常闭）" },
            { HSF_BTN_START_DI,      "启动按钮" },
            { HSF_BTN_STOP_DI,       "停止按钮" },
            { HSF_BTN_RESET_DI,      "复位按钮" },
        };
        static const struct {
            int m_ch;
            const char* m_text;
        } kBtnDo[] = {
            { HSF_BTN_START_LAMP_DO, "启动按钮灯" },
            { HSF_BTN_STOP_LAMP_DO,  "停止按钮灯" },
            { HSF_BTN_RESET_LAMP_DO, "复位按钮灯" },
        };
        int k;
        for (k = 0; k < (int)(sizeof(kBtnDi) / sizeof(kBtnDi[0])); ++k) {
            int ch = kBtnDi[k].m_ch;
            if (ch >= 0 && ch < count)
                snprintf(s_diBuf[ch], sizeof(s_diBuf[ch]), "%s",
                         kBtnDi[k].m_text);
        }
        for (k = 0; k < (int)(sizeof(kBtnDo) / sizeof(kBtnDo[0])); ++k) {
            int ch = kBtnDo[k].m_ch;
            if (ch >= 0 && ch < count)
                snprintf(s_doBuf[ch], sizeof(s_doBuf[ch]), "%s",
                         kBtnDo[k].m_text);
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
    /* [name/staleMs 对齐 Python 2026-11-10] Python 侧 encode_digital 恒发
     * 这两个键（模块名 + 陈旧阈值）；C 端原先不发 ⇒ Python 客户端连 C
     * 服务端时数字量模块名取不到、也没法判陈旧。键名逐字对齐。 */
    XBsonDocument_insert_keyUtf8_utf8(doc, "name", HSF_DIG_MODULE_NAME);
    XBsonDocument_insert_keyUtf8_int32(doc, "staleMs",
                                       (int32_t)HSF_DIG_TIMEOUT_MS);
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
/**
 * @brief 把整组阀快照键填进调用方的文档（count + v{i}_* 前缀键表）。
 * @details [为什么抽内核] 控制应答要把快照与 ok/err 合并在**同一帧**里
 *          （Python 侧 extra 同构），周期推送则单独成帧——两处共用同一份
 *          键表逻辑，避免两处漂移。
 * @return 填充成功返回 true；阀组未装配返回 false（调用方文档保持原样）。
 */
static bool hsfAppFillValvesDoc(XBsonDocument* doc)
{
    const HsfServerIoValve* bank = HsfServerIo_valves();
    int count;
    int i;

    if (!bank || !doc) return false;
    count = HsfServerIoValve_count(bank);
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
        if (it->m_cmdResult[0]) {
            /* [处置结果] 与 Python to_doc 同口径：只在有值时下发，客户端
             * 据此区分"动了/本来就在那儿/在途重发/外部接管"。 */
            snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_VALVE_CMD_RESULT);
            XBsonDocument_insert_keyUtf8_utf8(doc, key, it->m_cmdResult);
        }
        if (it->m_text[0]) {
            snprintf(key, sizeof(key), "v%d_%s", i, HSF_KEY_VALVE_TEXT);
            XBsonDocument_insert_keyUtf8_utf8(doc, key, it->m_text);
        }
    }
    return true;
}

static XByteArray* hsfAppEncodeValves(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    XByteArray* bytes;
    if (!doc) return NULL;
    if (!hsfAppFillValvesDoc(doc)) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief 阀告警推送：本拍取走的告警合并成**一帧**随阀状态主题下发。
 * @details [为什么随 HSF_TOPIC_VALVE_STATUS 推] Python 生态
 *          （server_py/client_py/webui_py）把告警帧推在阀状态主题上——
 *          载荷带 "ac" 键即告警帧，键空间 a{i}_* 与周期状态的 v{i}_* 不
 *          相交，客户端按"先认告警再认状态"判型；且 Python 客户端只订阅
 *          0x00010007 ⇒ C 服务端推独立告警主题 0x00010008 的帧在 Python
 *          界面上被**静默丢弃**（bridge.TOPIC_KIND 查不到即 return）。旧
 *          "独立主题全推"的初衷由 publish_ex 的订阅语义兜不住（无人订阅
 *          即 0 投递）。主题对齐后键布局也必须对齐：{ac:N, a0_vi/vn/k/t/
 *          at/to?...}（Python _publish_valve_alarms 同构，一拍多路超时
 *          合并一帧）。
 *
 *          [为什么要"取走"而不是"读状态"] 告警每拍都可能成立（超时判据是
 *          `now - cmdAt > timeout`），若按状态推就会每 200ms 一帧把客户端刷屏。
 *          取走语义保证一条告警只出一次帧。
 */
static void hsfAppPushValveAlarms(XLinkServer* self, uint64_t now)
{
    const HsfServerIoValve* bank = HsfServerIo_valves();
    XBsonDocument* doc;
    XByteArray* bytes;
    int count;
    int i;
    int taken = 0;

    (void)now;
    count = bank ? HsfServerIoValve_count(bank) : 0;
    if (count <= 0) return;

    doc = XBsonDocument_create();
    if (!doc) return;

    for (i = 0; i < count; ++i) {
        int kind = 0;
        char text[160];
        const HsfServerIoValveItem* it;
        char key[32];
        int n;

        if (!HsfServerIo_valveTakeAlarm(i, &kind, text, (int)sizeof(text)))
            continue;
        it = HsfServerIoValve_item(bank, i);
        if (!it) continue;
        n = taken;
        ++taken;
        snprintf(key, sizeof(key), "a%d_%s", n, HSF_KEY_ALARM_VALVE_INDEX);
        XBsonDocument_insert_keyUtf8_int32(doc, key, i);
        snprintf(key, sizeof(key), "a%d_%s", n, HSF_KEY_ALARM_VALVE_NAME);
        XBsonDocument_insert_keyUtf8_utf8(doc, key, it->m_name);
        snprintf(key, sizeof(key), "a%d_%s", n, HSF_KEY_ALARM_KIND);
        XBsonDocument_insert_keyUtf8_int32(doc, key, kind);
        snprintf(key, sizeof(key), "a%d_%s", n, HSF_KEY_ALARM_TEXT);
        XBsonDocument_insert_keyUtf8_utf8(doc, key, text);
        snprintf(key, sizeof(key), "a%d_%s", n, HSF_KEY_ALARM_AT);
        XBsonDocument_insert_keyUtf8_int64(doc, key, (int64_t)now);
        if (kind == HsfValveAlarmKind_Timeout) {
            snprintf(key, sizeof(key), "a%d_%s", n, HSF_KEY_ALARM_TIMEOUT_S);
            XBsonDocument_insert_keyUtf8_double(
                doc, key,
                (double)HsfServerIoValve_moveTimeoutMs(bank) / 1000.0);
        }
        /* [告警入库] 与 Python insert_alarm("valve", name, text) 同表同分类。 */
        HsfServerStore_insertAlarm(&s_store, "valve", it->m_name, text);
        /* [logs 旁路] alarms 表供报警页、logs 表供日志页——同一事件两处
         * 可查（日志页 category="alarm" 筛得到）。hsfAppEventLog 同时承担
         * 终端留痕（不再单打一条 XPrintf，防双份）。 */
        {
            char buf[224];
            snprintf(buf, sizeof(buf), "阀告警 路%d(%s) 类型%d：%s",
                     i, it->m_name, kind, text);
            hsfAppEventLog("alarm", buf);
        }
    }

    if (taken == 0) {
        XClassDelete((XClass*)doc);
        return;
    }
    /* [ac 键最后写] 有该键即告警帧；条数 = 实际取走的条数。 */
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_ALARM_COUNT, taken);

    bytes = XBsonDocument_toBson(doc);
    if (bytes) {
        XLinkServer_publish_ex(self, HSF_TOPIC_VALVE_STATUS,
                               XLinkDataType_Bson,
                               (const uint8_t*)XByteArray_constData(bytes),
                               (uint32_t)XByteArray_size_base(bytes));
        XClassDelete((XClass*)bytes);
    }
    XClassDelete((XClass*)doc);
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
        /* [告警入库] 与 Python insert_alarm("auto", ...) 同分类口径。 */
        HsfServerStore_insertAlarm(&s_store, "auto", "自动运行", alarm.m_text);
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

/* ==================== 历史落盘：采样装配 ==================== */

/** @brief 变频器状态文案（与 Python RUN_STATE_TEXT 同表；离线返回 NULL）。 */
static const char* hsfAppVfdStateText(const XVfdStatus* st, char* buf, int cap)
{
    if (!st || !st->m_valid) return NULL;
    if (st->m_faulted) {
        snprintf(buf, (size_t)cap, "故障 0x%04X", (unsigned)st->m_faultCode);
        return buf;
    }
    switch (st->m_runState) {
    case XVfdRunState_Stopped: return "停机";
    case XVfdRunState_RunningForward: return "正转运行";
    case XVfdRunState_RunningReverse: return "反转运行";
    case XVfdRunState_JogForward:
    case XVfdRunState_JogReverse: return "点动运行";
    case XVfdRunState_Fault: return "故障";
    default: return "未知";
    }
}

/**
 * @brief 历史采样入库（每 HSF_STORE_SAMPLE_MS 一行；与 Python _store_loop
 *        同口径：失败不致命、离线列记 NULL）。
 * @details [与推送同源] 各值直接读装配层缓存（与遥测编码同批设备对象、
 *          同一 tick），保证"库里一行 = 界面一拍"可互相印证。
 *          [ts 用墙钟] history.ts 是 epoch 毫秒（趋势图按真实时刻画），
 *          超时/限流类的**时长**测量仍走单调钟——两套口径各司其职。
 */
/**
 * @brief 流量计单位码表（与 Python app.FLOW_UNIT_TEXT / TOTAL_UNIT_TEXT 同值）。
 * @details [为什么要全表] 旧实现只映射 5=m³/h、1=m³，其余码写 NULL ⇒ 界面显示
 *          "25.00 "（单位空）。Python 侧是完整码表 + 未知码 "?"，两端对
 *          同一台流量计会显示出不同文案。
 */
static const char* const s_flowUnitText[15] = {
    "L/s", "L/min", "L/h", "m³/s", "m³/min", "m³/h", "kg/s",
    "kg/min", "kg/h", "t/s", "t/min", "t/h", "g/s", "g/min", "g/h"
};
static const char* const s_totalUnitText[6] = {
    "L", "m³", "kg", "t", "g", "kL"
};

/* [历史行热身门 2026-11-09] 逐字段记「是否见过有效值」。
 *
 * [为什么需要] 服务端刚起的 1~2 秒里，485 的变频器还没上报 ⇒ 采到的行
 * 变频器四列全是 NULL ⇒ 界面整行半屏是 "--"（现场截图确认：每次重启
 * 后紧跟着的那一行如此）。NULL＝无数据的口径本身是对的（设备真掉线时
 * 就该记 NULL），但"**还没开始报**"与"**报过又掉**"要分开：后者该留
 * 无数据行（有信息量：那段确实没数据），前者只是启动瞬态，不该进库。
 *
 * [口径] 某字段还没见过有效值之前，带该字段空洞的行**不写**；见过之后
 * 照写（掉线留痕）。这样设备长期离线也不会让整张历史表停摆。
 */
#define HSF_HIST_WARM_VFD    0x01u   /**< 变频器（状态/频率/电流/转速）。 */
#define HSF_HIST_WARM_TH     0x02u   /**< 温湿度。 */
#define HSF_HIST_WARM_TURB   0x04u   /**< 浊度 ×2。 */
#define HSF_HIST_WARM_FLOW   0x08u   /**< 流量计（含正累计）。 */
#define HSF_HIST_WARM_PRESS  0x10u   /**< 压差。 */
#define HSF_HIST_WARM_DIO    0x20u   /**< 数字量位图。 */
static uint32_t s_histWarmMask = 0;   /**< 已见过有效值的字段位（见上）。 */

static void hsfAppTickHistory(void)
{
    HsfHistoryRow row;
    uint64_t now;
    char vfdText[48];
    char pressNoteBuf[64];   /**< 压差越限说明（row 借用，必须在 insert 之前存活）。 */
    double pressValue = 0.0;

    if (!HsfServerStore_isOpen(&s_store)) return;
    now = (uint64_t)XDateTime_currentMSecsSinceEpoch();
    if (s_store.m_lastSampleMs != 0 &&
        now - s_store.m_lastSampleMs <
            (uint64_t)HsfServerStore_sampleMs(&s_store)) {
        return;
    }
    s_store.m_lastSampleMs = now;

    memset(&row, 0, sizeof(row));
    row.tsMs = (int64_t)XDateTime_currentSecsSinceEpoch() * 1000LL;
    /* [模式文案与 Python 同口径] 自动档且控制器未就绪="自动·未知"
     * （旧实现只有运行中/停止两态，会把"还不知道"说成"停止"）。 */
    if (s_runMode == HsfRunMode_Auto) {
        if (!s_autoCtlReady)
            row.mode = "自动·未知";
        else
            row.mode = XAutoController_running(&s_autoCtl) ? "自动·运行中"
                                                          : "自动·停止";
    } else {
        row.mode = "手动";
    }

    /* ---- 变频器（A 侧）---- */
    {
        XVfdDevice* dev = HsfServerIo_vfdDevice(HsfSide_A);
        const XVfdStatus* st = dev ? XVfdDevice_status_const(dev) : NULL;
        if (st && st->m_valid) {
            row.vfdState = hsfAppVfdStateText(st, vfdText, (int)sizeof(vfdText));
            row.freqHz = st->m_outputFreqHz;
            row.freqHzValid = true;
            row.currentA = st->m_outputCurrentA;
            row.currentAValid = true;
            /* [转速与遥测同源] Python 历史取报文里的 speedRpm（无效则 NULL）；
             * 本侧旧实现无条件用 hzToRpm 换算，会让"库里转速"与"界面转速"
             * 在报文缺该字段时不一致。按有效位掩码取，缺字段即 NULL。 */
            if (st->m_validMask & XVfdField_SpeedRpm) {
                row.rpm = st->m_speedRpm;
            } else {
                row.rpm = XVfdDevice_hzToRpm(dev, st->m_outputFreqHz);
            }
            row.rpmValid = true;
        }
    }
    /* ---- 温湿度（电箱）---- */
    {
        const HsfServerIoTempHum* th = HsfServerIo_tempHum(HsfSide_A);
        int32_t t10 = 0;
        int32_t h10 = 0;
        bool tv = false;
        bool hv = false;
        if (th && HsfServerIoTempHum_values(th, &t10, &h10, &tv, &hv)) {
            if (tv) {
                row.temp = (double)t10 / 10.0;
                row.tempValid = true;
            }
            if (hv) {
                row.hum = (double)h10 / 10.0;
                row.humValid = true;
            }
        }
    }
    /* ---- 浊度仪两台（路 B）---- */
    {
        double ntu = 0.0;
        const HsfServerIoTurbidity* t1 = HsfServerIo_turbidity(HsfSide_B, 0);
        const HsfServerIoTurbidity* t2 = HsfServerIo_turbidity(HsfSide_B, 1);
        if (t1 && HsfServerIoTurbidity_isOnline(t1) &&
            HsfServerIoTurbidity_ntu(t1, &ntu)) {
            /* [0 NTU 是有效值] 旧实现 `ntu > 0` 把"读数恰好 0"当成无数据
             * ⇒ 清水工况整列变成 "--"。Python 以在线有效为准，保留 0。 */
            row.turb1 = ntu;
            row.turb1Valid = true;
        }
        if (t2 && HsfServerIoTurbidity_isOnline(t2) &&
            HsfServerIoTurbidity_ntu(t2, &ntu)) {
            row.turb2 = ntu;
            row.turb2Valid = true;
        }
        /* ---- 水温（浊度仪2 随帧带出的，2026-11-10）----
         * [只有带温度的机型才有] 浙仪 ZHEYI-5100ZDY 一帧 0x0000×4 同刻读出
         * 浊度+温度；WZT-701A 只有 2 个寄存器。tempC() 返回 false 时**不写**
         * ⇒ 该列入 NULL、界面显示 "--"，不能记 0℃（0℃ 是真实读数值）。
         * [优先第 2 台，退化到"任意带温度的那台"] 站号可以改（现场改过
         * 1/2/3），按"谁带温度就用谁"取，避免站号一换这列就永久空。 */
        {
            double tC = 0.0;
            if (t2 && HsfServerIoTurbidity_tempC(t2, &tC)) {
                row.waterTemp = tC;
                row.waterTempValid = true;
            } else if (t1 && HsfServerIoTurbidity_tempC(t1, &tC)) {
                row.waterTemp = tC;
                row.waterTempValid = true;
            }
        }
    }
    /* ---- 流量计（路 B）---- */
    {
        const HsfServerIoFlowMeter* fm = HsfServerIo_flowMeter(HsfSide_B);
        double flow = 0.0;
        double fwdTotal = 0.0;
        int flowUnit = 0;
        int totalUnit = 0;
        bool valid = false;
        if (fm && HsfServerIoFlowMeter_values(fm, &flow, NULL, &fwdTotal,
                                              NULL, &flowUnit, &totalUnit,
                                              NULL, &valid) &&
            valid) {
            row.flow = flow;
            row.flowValid = true;
            row.flowTotal = fwdTotal;
            row.flowTotalValid = true;
            /* [单位码全表] 与 Python FLOW_UNIT_TEXT/TOTAL_UNIT_TEXT 同值；
             * 未知码给 "?"（不写 NULL——NULL 会被界面当"没数据"）。 */
            row.flowUnit = (flowUnit >= 0 && flowUnit < 15)
                               ? s_flowUnitText[flowUnit] : "?";
            row.totalUnit = (totalUnit >= 0 && totalUnit < 6)
                                ? s_totalUnitText[totalUnit] : "?";
        }
    }
    /* ---- 压差（AI 通道 0；标定表/越限说明由 hsfAppAiEngineering 统一换算）----
     * [与 Python 同源] Python 历史取 cfg.analog.scales[0].engineering(ma)：
     * 值 + 越限说明一起入库；旧实现硬编码 0~1MPa 且从不写说明，导致
     * "界面压差"与"库里压差/异常行"对不上。 */
    {
        double ma[HSF_AI_CHANNELS];
        bool valid[HSF_AI_CHANNELS];
        int n = HsfServerIo_aiValues(ma, valid, HSF_AI_CHANNELS);
        if (n > 0 && valid[0]) {
            const char* note = NULL;
            pressNoteBuf[0] = '\0';
            if (hsfAppAiEngineering(0, ma[0], pressNoteBuf,
                                    (int)sizeof(pressNoteBuf), &pressValue,
                                    NULL, NULL, &note)) {
                row.pressMpa = pressValue;
                row.pressValid = true;
                /* 无越限也写空串（与 Python `note or ""` 同口径）：
                 * NULL=通道未标定，""=已标定但正常。 */
                row.pressNote = note ? note : "";
            }
        }
    }
    /* ---- 数字量位图（位宽随实际点数；聚合时最多 32 点）---- */
    {
        uint32_t di = 0;
        uint32_t doMask = 0;
        bool valid = false;
        if (HsfServerIo_digitalMasks(&di, &doMask, &valid) && valid) {
            row.di = di;
            row.diValid = true;
            row.doMask = doMask;
            row.doValid = true;
        }
    }

    /* [热身门] 还没见过有效值的字段 ⇒ 本行不写（启动瞬态不留空洞行）。 */
    {
        uint32_t need = 0;
        if (!row.vfdState || !row.freqHzValid || !row.currentAValid ||
            !row.rpmValid) {
            need |= HSF_HIST_WARM_VFD;
        }
        if (!row.tempValid || !row.humValid) need |= HSF_HIST_WARM_TH;
        if (!row.turb1Valid || !row.turb2Valid) need |= HSF_HIST_WARM_TURB;
        if (!row.flowValid || !row.flowTotalValid) need |= HSF_HIST_WARM_FLOW;
        if (!row.pressValid) need |= HSF_HIST_WARM_PRESS;
        if (!row.diValid || !row.doValid) need |= HSF_HIST_WARM_DIO;
        if (need & ~s_histWarmMask) {
            /* 还有"从未见过有效值"的字段 ⇒ 这一行是启动瞬态，不入库。 */
            return;
        }
        /* 本行全值 ⇒ 该字段组算热身完成。 */
        if (!(need & HSF_HIST_WARM_VFD)) s_histWarmMask |= HSF_HIST_WARM_VFD;
        if (!(need & HSF_HIST_WARM_TH)) s_histWarmMask |= HSF_HIST_WARM_TH;
        if (!(need & HSF_HIST_WARM_TURB)) s_histWarmMask |= HSF_HIST_WARM_TURB;
        if (!(need & HSF_HIST_WARM_FLOW)) s_histWarmMask |= HSF_HIST_WARM_FLOW;
        if (!(need & HSF_HIST_WARM_PRESS)) s_histWarmMask |= HSF_HIST_WARM_PRESS;
        if (!(need & HSF_HIST_WARM_DIO)) s_histWarmMask |= HSF_HIST_WARM_DIO;
    }

    if (!HsfServerStore_insertHistory(&s_store, &row)) {
        /* 失败不致命（磁盘满/路径不可写不该拖垮监控）；不刷屏，留给
         * store 层的单次 SQL 失败日志。 */
    }
}

/**
 * @brief 诊断：周期性打印系统堆存量（`HSF_HEAP_TRACE=1`）。
 * @details 压测判泄漏用（AGENTS 口径：泄漏看**堆计数**，不看工作集）。
 *          5 秒一次，附当前会话数，便于把「堆随连接数线性涨」与
 *          「每轮会话后堆回不到基线」区分开。未设环境变量时零开销。
 */
static void hsfAppHeapTrace(void)
{
    static int traceOn = -1;
    static int64_t lastMs = 0;
    int64_t nowMs;
    if (traceOn < 0)
        traceOn = XSystem_environment("HSF_HEAP_TRACE") ? 1 : 0;
    if (!traceOn) return;
    nowMs = XDateTime_currentMSecsSinceEpoch();
    if (lastMs != 0 && nowMs - lastMs < 5000) return;
    lastMs = nowMs;
    XPrintf("[heap] sys=%lld sessions=%u t=%lld\n",
            (long long)XMemory_statistics_2(XMEMORY_TYPE_SYSTEM).systemBytes,
            (unsigned)(s_srv ? XLinkServer_sessionCount(s_srv) : 0),
            (long long)nowMs);
}

static void hsfAppTick(XLinkServer* self, void* userData)
{
    /* 模拟心跳拍：推进时间驱动的模型状态（变频器爬坡/流量积分）。
     * [为什么挂在服务端 tick 上] 模拟现场不能因为"没人读"就冻结——现场
     * 不会因为上位机不在轮询就静止。未启用 --sim 时为空操作。 */
    HsfServerIoSim_tick();

    /* 注入链路自愈（485 两路 + 模拟量站 + 数字量站）：设备失联就重连、
     * 在线但数据停滞就重挂轮询定时器。**只对注入/模拟客户端生效**，
     * 物理串口/真机 TCP 装配下是空转（m_on=false）。
     * [为什么放在推数据之前] 同一拍里的编码/健康巡检要拿到自愈后的状态，
     * 否则故障那拍会先推一帧"无数据"再恢复，界面闪一下。 */
    (void)HsfServerIo_watchdog((uint64_t)XDateTime_currentMSecsSinceEpoch());

    /* 压测诊断：堆计数节拍（env 门控，未设零开销）。 */
    hsfAppHeapTrace();

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
    hsfAppTickHistory();            /* 历史采样入库（周期内自节流）。 */
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
    /* [登录被拒入库] 与 hsfAppOnAuthChanged 的成功留痕同一 auth 类。 */
    {
        char buf[160];
        snprintf(buf, sizeof(buf), "登录拒绝：%s（%s）",
                 name ? name : "", reason ? reason : "");
        hsfAppEventLog("auth", buf);
    }
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
    XLinkServer_setResyncCallback(s_srv, hsfAppTxResync, NULL);
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
    /* 启动恢复：**不是模式切换**（没有"切"这个动作），故不做全停——
     * 刚起进程时设备本来就没被本进程驱动过。 */
    hsfAppApplyModeToDevices(false);
    hsfAppAutoCtlSetup();
    XPrintf("HsfServerApp: 运行模式 = %s（自动运行 %s）\n",
            s_runMode == HsfRunMode_Auto ? "自动（远程控制禁用）" : "手动",
            XAutoController_running(&s_autoCtl) ? "运行中" : "停止");

    /* [历史落盘] 打开失败不致命（对齐 Python store.py 失败口径）：服务
     * 照跑，只是不落盘。采样周期/路径见 cfg/hsf_server.ini 的 [storage]。 */
    HsfServerStore_init(&s_store);
    if (HsfServerStore_configure(&s_store, HsfServerSettings_dataDir())) {
        HsfServerStore_open(&s_store);
    } else {
        XPrintf("HsfServerApp: 数据落盘已禁用（storage/enabled=0）\n");
    }

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
    HsfServerStore_clear(&s_store);   /* 关库（幂等；未启用为空操作）。 */
    XLinkServer_close(s_srv);
    XClassDelete((XClass*)s_srv);
    s_srv = NULL;
}

int HsfServerApp_sessionCount(void)
{
    return s_srv ? XLinkServer_sessionCount(s_srv) : 0;
}
