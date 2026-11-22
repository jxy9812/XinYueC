/**
 * @file       HsfServerAutoMode.c
 * @brief      服务端自动模式序列实现（三模式状态机 + 全停 + 急停/复位 + auto 配置节）。
 * @details    Python 事实源：server_py/hsfsrv/autocontrol.py（AutoController
 *             三模式序列 S1-S14）。本文件是它的 C 端镜像，逐相位逐闸对齐；
 *             框架件 XAutoController 承担运行闸门/急停锁存/步进节流/报警队列
 *             /动作记账，设备访问经 HsfServerIo 门面（阀/泵/数字量）与
 *             XVfdDevice/XVfdCodec（搅拌泵）。
 *             [决策只认反馈] 阀到位判 HsfServerIoValve_item().m_state
 *             （Fault/Open/Closed），绝不认 valveSet 返回值——那只是"命令受理"。
 *             [节流] 阀命令重发走 XAutoController_throttle（键固定短字面量）；
 *             [全停] 逐台独立、失败收集、部分失败补 1 号告警。
 *             [累计运行时长 2026-11-12] 服务端计（唯一事实源）：起跑时刻 +
 *             已结算累计，停止/急停/异常中止（阀超时等）时结算这一段，跨多次
 *             运行累计保留；正在跑时查询算到当前。键 = HSF_KEY_AUTO_RUN_MS
 *             （与 server_py/hsfsrv/proto.py 的 KEY_AUTO_RUN_MS 逐值同步）。
 */
#include "HsfServerAutoMode.h"

#include "HsfServerIo.h"          /* 阀/泵/数字量门面 + HsfServerConfig 点位宏。 */
#include "HsfServerSettings.h"    /* auto/<模式id> 配置落盘（getString/setString）。 */
#include "XVfdDevice.h"           /* XVfdDevice_startWithGiven/rpmToHz/lastErrorText。 */
#include "XVfdCodec.h"            /* XVfdControlRequest（减速停机用）。 */
#include "XAutoControl.h"         /* 框架件：运行闸/急停锁存/节流/报警队列。 */
#include "XBsonDocument.h"
#include "XBsonArray.h"
#include "XBsonValue.h"
#include "XJson.h"                /* XJsonDocument_Compact（配置 JSON 落盘格式）。 */
#include "XJsonDocument.h"        /* XJsonDocument_fromJson/object（配置恢复）。 */
#include "XJsonObject.h"          /* XJsonObject（fromJsonObject 入参）。 */
#include "XByteArray.h"
#include "XString.h"
#include "XDateTime.h"           /* XDateTime_currentMSecsSinceEpoch（告警时刻）。 */
#include "XPrintf.h"
#include <stdio.h>
#include <string.h>

/* ==================== 常量 ==================== */

/** @brief 阀相在「阀层动作超时」之外再等的宽限（毫秒；autocontrol.py
 *         VALVE_PHASE_GRACE_MS 同值——阀层自己按 move_timeout 判 Fault 并
 *         自带告警，本层多留余量再判序列超时，避免两处几乎同时报两条
 *         含义相近的告警）。 */
#define AM_VALVE_PHASE_GRACE_MS   2000u

/** @brief 阀命令失败的节流重发间隔（毫秒；VALVE_CMD_RETRY_MS 同值——下发
 *         失败（设备离线抖动）不放弃整个模式，等一会再试；连发会打爆总线）。 */
#define AM_VALVE_CMD_RETRY_MS     2000u

/** @brief 泵相写失败的宽限（毫秒）：泵写路径的前置在线/新鲜度判定是
 *         **单拍瞬时值**（HsfServerIoPump_poll 的 m_online = 本拍 DO 回读
 *         是否有效），总线一轮停滞的抖动窗会把"可写"误报成"离线"。
 *         与阀相同款纪律：失败先留在泵相逐拍重试（不放弃模式），持续
 *         失败过了宽限才按 ACTION_FAILED 中止（告警文字与类别跟一次性
 *         失败口径完全一致）。 */
#define AM_PUMP_PHASE_GRACE_MS    4000u

/** @brief 现场按键沿去抖缺省（毫秒；Python cfg.button_debounce_ms 缺省 300
 *         同值——数字量模块 200ms 一拍，按键物理时长通常更长，去抖防机械
 *         抖动造成连续两个沿）。[2026-11-13 可配化] 运行期值在
 *         s_btnDebounceMs（auto 节 buttons 子节），此宏只作缺省。 */
#define AM_BTN_DEBOUNCE_MS        300u

/** @brief 模式 id 缓冲容量（"produce"/"backwash"/"rinse" 足够）。 */
#define AM_MODE_ID_MAX            16

/** @brief 单模式配置 JSON 串缓冲（设备清单最多 8 个 id + 两个参数）。 */
#define AM_CFG_JSON_MAX           512

/** @brief 报警文字缓冲（与 XAutoControlAlarm.m_text[128] 同量级再留余）。 */
#define AM_TEXT_MAX               160

/** @brief 累计运行时长落盘间隔（毫秒；Python AutoController.RUN_PERSIST_MS
 *         同值 60000）——断电/崩溃最多丢这个窗口的时长。 */
#define AM_RUN_PERSIST_MS         60000u

/** @brief 累计运行时长持久化键（与 run/mode 同节同套路；Python 侧 =
 *         cfg 的 runtime 节键 auto_run_ms——config.py:1127
 *         auto_run_ms()/save_auto_run_ms()。**存字符串**：毫秒累计会超
 *         int32（≈24.8 天），int 承载会在 999+ 天口径下静默截断。 */
#define AM_RUN_MS_KEY             "run/auto_run_ms"

/* 设备下标（与 proto.py AUTO_DEVICES 同序：0..5 = valve1..6）。 */
#define AM_DEV_STIR               6
#define AM_DEV_INLET              7

/* 相位名（经 HSF_KEY_AUTO_PHASE 上线；与 Python PHASE_* 逐字同）。 */
static const char kPhaseIdle[]    = "idle";
static const char kPhaseValves[]  = "valves";
static const char kPhasePumps[]   = "pumps";
static const char kPhaseRunning[] = "running";
static const char kPhaseEstop[]   = "estop";

/* 模式表（id + 显示名；与 proto.py AUTO_MODES 同序同字面）。 */
static const char* const kModeIds[HSF_AUTO_MODE_COUNT] = {
    HSF_AUTO_MODE_ID_PRODUCE, HSF_AUTO_MODE_ID_BACKWASH, HSF_AUTO_MODE_ID_RINSE
};
static const char* const kModeNames[HSF_AUTO_MODE_COUNT] = {
    HSF_AUTO_MODE_NAME_PRODUCE, HSF_AUTO_MODE_NAME_BACKWASH, HSF_AUTO_MODE_NAME_RINSE
};

/**
 * @brief 粘性自动模式的持久化键（INI 落成 [run] auto_mode=）。
 * @details 与运行模式键 "run/mode" 同节同套路（Python 侧是 cfg 的 runtime
 *          节 "auto_mode" 键，同义不同载体）。
 */
#define AM_KEY_STICKY_MODE   "run/auto_mode"

/* ==================== 模块状态 ==================== */

static bool s_ready = false;               /**< 装配完成标志。 */
static bool s_ctlInit = false;             /**< 框架控制器已 init（同进程一次）。 */
static XAutoController s_ctl;              /**< 框架控制器（运行闸/急停锁存/报警）。 */
static HsfServerAutoModeLogFn s_log = NULL;/**< 运维日志钩子（NULL=静默）。 */

static bool s_faultLatched = false;        /**< 报警锁存（第二把锁；框架只管急停锁）。 */
static char s_mode[AM_MODE_ID_MAX];        /**< 粘性模式 id（""=从未选过）。 */

/**
 * @brief 本轮**运行中**的模式 id（与粘性 s_mode 分账）。
 * @details [为什么要分账 2026-11-20] s_mode 是「下次启动按哪个模式跑」的
 *          **粘性**（op=5 改的就是它），而运行期的配置读取、日志显示、
 *          状态回显要的是「这一轮正在跑哪个模式」——两者被挤在一个变量
 *          里时，运行中一条 op=5 会把正在跑的工序的配置整个换掉
 *          （阀复位开关/搅拌泵转速与方向/阀目标来源全变）⇒ 打断生产。
 *          Python 端早已分账（ctl.selected_mode 粘性 vs ctl.mode 运行中）
 *          ⇒ C 端同款：s_mode=粘性，s_runMode=运行中，组合模式的当前
 *          子阶段另有 s_cycleStage。
 */
static char s_runMode[AM_MODE_ID_MAX];     /**< 本轮运行中的模式 id（""=未在跑）。 */

/**
 * @brief 当前运行模式是不是**自动**（tick 每拍从 App 更新；缺省=false=手动）。
 * @details 用途有二：
 *   · 手动模式的按钮组启动灯反映"现场还有设备在动"（不是自动运行位）；
 *   · 手动模式的停止/急停**一律关阀复位**（用户口径 2026-11-15：手动模式
 *     下按停止/急停要把设备与球阀全部停下来；自动模式才看逐模式配置）。
 * [为什么存一份而不是每次问 App] 本模块的公开 API（requestStop/
 * requestEstop）由 App 在协议处理里调用，那条路径拿得到模式，但本模块
 * 内部（按键受理/急停闸）也要同一口径 ⇒ 统一用 tick 同步过来的这一份
 * （与 Python 侧 ctx.is_auto() 读 App 的 run_mode 同源同值）。
 */
static bool s_autoMode = false;

static HsfServerAutoModeCfg s_cfg[HSF_AUTO_MODE_COUNT]; /**< 三模式配置。 */

/** @brief 自动循环配置（auto 节 cycle 子节；缺省见 proto）。 */
static HsfServerAutoModeCycleCfg s_cycleCfg = {
    HSF_AUTO_CYCLE_ENABLED_DEF, HSF_AUTO_CYCLE_PRESS_THRESHOLD_DEF,
    HSF_AUTO_CYCLE_PRESS_COMPARE_DEF, HSF_AUTO_CYCLE_TIMER_FALLBACK_DEF,
    HSF_AUTO_CYCLE_TIMER_MINUTES_DEF, HSF_AUTO_CYCLE_BACKWASH_MINUTES_DEF,
    HSF_AUTO_CYCLE_RINSE_MINUTES_DEF
};

/** @brief 当前是否在组合模式（「自动」循环）中运行。 */
static bool s_cycleRoot = false;
/** @brief 当前子阶段 id（produce/backwash/rinse；非组合模式为空串）。 */
static char s_cycleStage[AM_MODE_ID_MAX];
/** @brief 当前子阶段起算时刻（**阀相**就起算，与 Python 同口径）。 */
static uint64_t s_cycleStageAt = 0;
/**
 * @brief 停止/急停时记住的子阶段（再启动从那一步继续；复位清空）。
 * @details 用户口径「停止/急停后从停止的状态继续运行，除非按了复位」——
 *          记忆在**服务端**（唯一事实源，客户端重启不丢）。
 */
static char s_cycleResumeStage[AM_MODE_ID_MAX];
/** @brief 压差取数回调（装配期由 App 注入；NULL=读不到）。 */
static HsfServerAutoModePressFn s_pressFn = NULL;

/* ---- 按键触点类型与去抖（auto 节 buttons 子节；2026-11-13 对齐 Python）----
 * Python：cfg.button_debounce_ms + cfg.button_items.normally_closed
 * （buttons.py：「触点类型由现场接线决定，不是软件常量」）。C 端原先是
 * 编译期宏 + amEdge 字面量实参，现场换接法必须改代码重编；现挂进 auto
 * 配置节（auto/buttons 一键 JSON；0x0117 同通道），SET 立即生效。
 * 缺省与 Python 缺省一致：去抖 300ms；启动/复位常开（上升沿）、停止常闭
 * （下降沿 1→0——"线断了=停止"的安全侧接线）。 */
static int s_btnDebounceMs = (int)AM_BTN_DEBOUNCE_MS;
static bool s_btnNcStart = false;   /**< start_di 常开（按下=上升沿）。 */
static bool s_btnNcStop = true;     /**< stop_di 常闭（按下=1→0 下降沿）。 */
static bool s_btnNcReset = false;   /**< reset_di 常开（按下=上升沿）。 */

/**
 * @brief 本次运行的循环配置**快照**（Python `self._cycle_cfg` 同口径）。
 * @details [为什么快照而不是直读] Python 在 start() 时拍一次快照 ⇒ 运行中
 *          改配置**下一次启动才生效**；直读会让「改一个分钟数」立刻改变
 *          当前阶段的截止时刻（行为与参照实现不一致）。
 */
static HsfServerAutoModeCycleCfg s_runCycle;

/* 前向声明（循环调度在步进回调里用，实现在「四板斧」段）。 */
static void amBuildValveTargets(uint64_t nowMs);
static void amCycleTick(uint64_t nowMs);
static bool amResetValvesOnStop(void);   /* 现场停止按钮的全停取模式配置。 */
static void amApplySwitchPolicies(const char* newMode); /* 换相策略（2026-11-14）。 */
/* [换相策略 2026-11-14] 泵相收尾要用（泵相在文件前部、实现在循环调度段）。 */
static void amStopStirPump(void);
static void amStopInletPumps(void);

/* ---- 阀相状态（start() 定型，stop/abort 作废） ---- */
static bool s_valveInTable[HSF_VALVE_COUNT]; /**< 该路是否进目标表（点位配齐）。 */
static bool s_valveOpen[HSF_VALVE_COUNT];    /**< 目标（true=开）。 */
static bool s_valveAcked[HSF_VALVE_COUNT];   /**< 命令已受理（失败按节流重发）。 */
static int  s_valveCount = 0;                /**< 目标表覆盖的路数口径（=阀组路数）。 */
static uint64_t s_phaseAtMs = 0;             /**< 本相位起始时刻。 */
static uint64_t s_valveDeadlineMs = 0;       /**< 阀相截止时刻（move_timeout+宽限）。 */
static bool s_pumpIssued = false;            /**< 泵相已完成下发（两台全受理）。 */
static bool s_pumpStirAcked = false;         /**< 搅拌泵启动已受理（重试去重）。 */
static bool s_pumpInletAcked = false;        /**< 进水泵启动已受理（重试去重）。 */
/* [换相策略 2026-11-14] 走「保持上一模式状态」的泵，且新模式**不选**它
 * ⇒ 换相途中它仍在转 ⇒ 泵相必须把它停掉（否则一直转）。 */
static bool s_carryStirPendingStop = false;
static bool s_carryInletPendingStop = false;
static uint64_t s_pumpDeadlineMs = 0;        /**< 泵相截止时刻（首败起算，过限中止）。 */
static char s_pumpFirstErr[96];              /**< 首次失败原因（中止告警引用）。 */

/* ---- 急停回路闸状态（_check_estop 同款） ---- */
static bool s_estopEverOk = false;    /**< 是否见到过急停回路正常（开机宽限判据）。 */
static bool s_estopBootWarned = false;/* 开机宽限一次性日志已打。 */

/* ---- 累计自动运行时长（2026-11-12；Python _run_accum_ms 同口径） ----
 * [为什么在服务端计] 客户端自己计时会因**断线/重启/换客户端**失真（本地
 * 计数从 0 开始，两端数字对不上）——服务端是唯一事实源，客户端只显示。
 * [只在运行期间累加] s_runAccumMs 是"已结算"的累计；正在跑的那一段用
 * s_runStartMs 到现在的差值补上（HsfServerAutoMode_runMs 算）。
 * 停止 / 急停 / 异常中止（阀超时等）时把这一段结算进累计（多次运行**累计**
 * 而非单次）。 */
static uint64_t s_runAccumMs = 0;     /**< 已结算的累计运行毫秒（跨运行保留）。 */
static uint64_t s_runStartMs = 0;     /**< 本次运行的起算时刻（0 = 不在跑）。 */
/** @brief 上次周期落盘时刻（运行相节流用；0=还没落过盘 ⇒ 首拍即落，
 *         与 Python ctx.throttle 的"首次放行"口径一致）。 */
static uint64_t s_runPersistAtMs = 0;

/* ---- 现场按键沿状态（上升/下降沿按触点类型；-1=未知） ---- */
static int s_prevStart = -1;
static int s_prevStop = -1;
static int s_prevReset = -1;
static uint64_t s_lastEdgeMs[3] = {0, 0, 0}; /**< 上次确认沿时刻（去抖；0=无）。 */

/* ---- 按钮组三盏灯（DO7~9；2026-11-12 对齐 Python buttons.py/_apply_lamps） ----
 * [分工] Python 侧 buttons.py 只管"采集与灯"、运行态由 autocontrol 决定后
 * set_running() 写回；C 端按钮沿受理（amHandleButtons）与急停闸
 * （amCheckEstop）本就归本模块，灯跟随也归这里（tick 每拍同步）。
 * [写判据] 回读 ≠ 目标 才写（Python _apply_lamps 同款）——没有独立的
 * "上次目标"缓存（C 端健康文案读的是实际 DO 电平，Python 的 _lamp_cache
 * 在 C 端无消费方，不留死账）。 */

static int s_stopAllSeq = 0;               /**< 全停批次序号（日志/告警文字用）。 */

/* ==================== 内部辅助 ==================== */

static void amLog(const char* category, const char* text)
{
    if (s_log) s_log(category, text);
}

/**
 * @brief 本轮**运行中**的模式 id（组合模式=当前子阶段）。
 * @details [粘性分账 2026-11-20] 运行期的一切（配置读取 / 日志显示 /
 *          状态回显）都必须用「正在跑的那个模式」，**不是**粘性 s_mode
 *          ——运行中用户在下拉改选（op=5）只改粘性（下次启动用），
 *          不得换掉正在跑的工序的配置。Python ctl.mode 同款。
 */
static const char* amRunModeId(void)
{
    if (s_cycleStage[0]) return s_cycleStage;
    if (s_runMode[0]) return s_runMode;
    return s_mode;
}

/** @brief 当前模式配置（组合模式取**当前子阶段**的配置）。 */
static const HsfServerAutoModeCfg* amCurCfg(void)
{
    const char* id = amRunModeId();
    const int idx = HsfServerAutoMode_modeIndex(id);
    return &s_cfg[idx >= 0 ? idx : 0];
}

/** @brief 组合模式下当前子阶段名（日志/告警用；非组合回空串）。 */
static const char* amStageId(void)
{
    return s_cycleRoot ? s_cycleStage : "";
}

/** @brief 取当前显示名（日志/告警文字用；未知回原 id 或"未选择"）。 */
static void amModeNameOf(const char* modeId, char* buf, int cap)
{
    const int idx = HsfServerAutoMode_modeIndex(modeId);
    if (idx >= 0) {
        snprintf(buf, (size_t)cap, "%s", kModeNames[idx]);
    } else if (modeId && modeId[0]) {
        snprintf(buf, (size_t)cap, "%s", modeId);
    } else {
        snprintf(buf, (size_t)cap, "%s", "未选择");
    }
}

/** @brief 阀路是否点位配齐（Python ValveItem.ready 同口径：三个口都有）。 */
static bool amValveReady(const HsfServerIoValve* bank, int i)
{
    const HsfServerIoValveItem* it = bank ? HsfServerIoValve_item(bank, i) : NULL;
    return it && it->m_do >= 0 && it->m_openDi >= 0 && it->m_closeDi >= 0;
}

/** @brief 取首个已装配的变频器设备（借用；两侧都没装返回 NULL）。 */
static XVfdDevice* amFindVfd(void)
{
    HsfSide side;
    for (side = HsfSide_A; side < HsfSide_Count; ++side) {
        XVfdDevice* dev = HsfServerIo_vfdDevice(side);
        if (dev) return dev;
    }
    return NULL;
}

/** @brief 取阀组动作超时（阀组未装配回编译期宏，与手动阀控同源）。 */
static uint64_t amValveMoveTimeoutMs(void)
{
    const HsfServerIoValve* bank = HsfServerIo_valves();
    const int ms = bank ? HsfServerIoValve_moveTimeoutMs(bank) : 0;
    return ms > 0 ? (uint64_t)ms : (uint64_t)HSF_VALVE_MOVE_TIMEOUT_MS;
}

/** @brief 作废模式序列中间状态（Python _clear_phase_state）。 */
static void amClearPhaseState(void)
{
    memset(s_valveInTable, 0, sizeof(s_valveInTable));
    memset(s_valveOpen, 0, sizeof(s_valveOpen));
    memset(s_valveAcked, 0, sizeof(s_valveAcked));
    s_valveCount = 0;
    s_phaseAtMs = 0;
    s_valveDeadlineMs = 0;
    s_pumpIssued = false;
    s_pumpStirAcked = false;
    s_pumpInletAcked = false;
    s_pumpDeadlineMs = 0;
    s_pumpFirstErr[0] = '\0';
    /* [换相策略 2026-11-14] 待停的 carry 泵作废（序列已中止/停止 ⇒ 不再有
     * "泵相收尾"来停它；停止路径另有全停逻辑兜底）。 */
    s_carryStirPendingStop = false;
    s_carryInletPendingStop = false;
    /* [循环状态清空 2026-11-12] 停止后不再处于任何子阶段（下一次启动从
     * 产水或记忆阶段重新开始）；累计运行时间不受影响（在 stop 里另行结算）。 */
    s_cycleRoot = false;
    s_cycleStage[0] = '\0';
    s_cycleStageAt = 0;
}

/**
 * @brief 累计运行时长落盘（Python ctx.persist_run_ms 同义；HsfServerApp
 *        侧的 run/mode 持久化同套路：写入即 sync，进程退出不丢）。
 * @param totalMs 累计毫秒（uint64 以十进制字符串落盘——int32 毫秒在
 *        ≈24.8 天溢出，累计口径必须字符串承载）。
 */
static void amPersistRunMs(uint64_t totalMs)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)totalMs);
    HsfServerSettings_setString(AM_RUN_MS_KEY, buf);
}

/**
 * @brief 启动恢复：从设置读回累计运行毫秒（缺键/空串/脏值 = 0）。
 * @details Python AutoController 构造时 load_run_ms() 同口径（2026-11-12
 *          用户定案「这个时间要服务端持久化」：服务端重启不归零）。
 */
static void amLoadRunMs(void)
{
    char buf[32];
    unsigned long long v = 0;
    HsfServerSettings_getString(AM_RUN_MS_KEY, "", buf, (int)sizeof(buf));
    if (buf[0])
        v = strtoull(buf, NULL, 10);
    s_runAccumMs = (uint64_t)v;
}

/**
 * @brief 把本次运行的一段并进累计（停止/急停/异常中止时调用；
 *        Python _settle_run_time 同口径）。
 * @param now 结算时刻（单调毫秒）。
 * @details 幂等：不在跑（s_runStartMs == 0）时是空操作；结算后清起算时刻，
 *          故同一段不会被重复计入。
 *          [持久化 2026-11-12] 结算即落盘——累计是**跨重启**的总量，
 *          不等周期窗口（Python 同款：_settle_run_time 末尾 persist）。
 */
static void amSettleRunTime(uint64_t now)
{
    if (s_runStartMs) {
        if (now > s_runStartMs) {
            s_runAccumMs += (now - s_runStartMs);
        }
    }
    s_runStartMs = 0;
    amPersistRunMs(s_runAccumMs);
}

/** @brief 灌一条阀门名（"阀N"，1 起——与 Python 阀相告警、C 端界面同口径）。 */
static void amValveName(int i, char* buf, int cap)
{
    snprintf(buf, (size_t)cap, "阀%d", i + 1);
}

/** @brief 安全追加（snprintf 返回值可能超过剩余容量，长度必须夹紧）。 */
static void amAppend(char* buf, int cap, int* len, const char* text)
{
    if (!buf || !text || !text[0] || cap <= 1 || *len >= cap - 1) return;
    {
        const int w = snprintf(buf + *len, (size_t)(cap - *len), "%s", text);
        if (w > 0) *len += w;
        if (*len > cap - 1) *len = cap - 1;
    }
}

/* ==================== 全停 ==================== */

int HsfServerAutoMode_stopAllDevicesEx(const char* reason, bool resetValves)
{
    char failed[640];
    char stopped[256];
    char msg[AM_TEXT_MAX];
    HsfSide side;
    int i;
    int count;
    const int seq = ++s_stopAllSeq;
    int failLen = 0;
    int stopLen = 0;
    int msgLen = 0;

    failed[0] = '\0';
    stopped[0] = '\0';
    snprintf(msg, sizeof(msg), "全停：开始（#%d", seq);
    msgLen = (int)strlen(msg);
    if (reason && reason[0]) {
        amAppend(msg, (int)sizeof(msg), &msgLen, "，");
        amAppend(msg, (int)sizeof(msg), &msgLen, reason);
    }
    amAppend(msg, (int)sizeof(msg), &msgLen, "）");
    amLog("auto", msg);

    /* 1) 变频器：受控停机（减速停，不用自由停车——现场泵是搅拌设备，
     *    瞬断对机械冲击更大）。逐侧独立。 */
    for (side = HsfSide_A; side < HsfSide_Count; ++side) {
        XVfdDevice* dev = HsfServerIo_vfdDevice(side);
        if (!dev) continue;
        {
            XVfdControlRequest req;    /* 栈上结构体交框架：先 memset。 */
            bool ok;
            char item[96];
            memset(&req, 0, sizeof(req));
            req.m_op = XVfdOperation_Stop;
            ok = XVfdCodec_applyControl(dev, &req);
            if (ok) {
                snprintf(item, sizeof(item), "变频器");
                amAppend(stopped, (int)sizeof(stopped), &stopLen, item);
            } else {
                const char* why = XVfdDevice_lastErrorText(dev);
                snprintf(item, sizeof(item), "变频器（%s）",
                         (why && why[0]) ? why : "设备拒绝");
                amAppend(failed, (int)sizeof(failed), &failLen, item);
            }
        }
    }
    /* 2) 进水泵：逐台停（无反馈点，停即输出断开）。逐台独立。 */
    {
        const HsfServerIoPump* bank = HsfServerIo_pumps();
        count = bank ? HsfServerIoPump_count(bank) : 0;
        for (i = 0; i < count; ++i) {
            const HsfServerIoPumpItem* it = HsfServerIoPump_item(bank, i);
            char err[96];
            char item[128];
            const bool ok = HsfServerIo_pumpSet(i, false, err, (int)sizeof(err));
            const char* name = (it && it->m_name[0]) ? it->m_name : "进水泵";
            if (ok) {
                snprintf(item, sizeof(item), "%s", name);
                amAppend(stopped, (int)sizeof(stopped), &stopLen, item);
            } else {
                snprintf(item, sizeof(item), "%s（%s）", name, err);
                amAppend(failed, (int)sizeof(failed), &failLen, item);
            }
        }
    }
    /* 3) 电动球阀：逐路关（不判到位，只发关命令）；未配点位的路不碰。
     *    [阀保持现状 2026-11-12] resetValves=false ⇒ **这一段跳过**：
     *    急停停的是泵与变频器，阀可能正停在中间位，叠一条“关”是叠加
     *    未知动作（用户口径「电动球阀可以不动」）。 */
    if (resetValves) {
        const HsfServerIoValve* bank = HsfServerIo_valves();
        count = bank ? HsfServerIoValve_count(bank) : 0;
        for (i = 0; i < count; ++i) {
            char err[96];
            char vname[16];
            char item[128];
            bool ok;
            if (!amValveReady(bank, i)) continue;
            amValveName(i, vname, (int)sizeof(vname));
            ok = HsfServerIo_valveSet(i, false, err, (int)sizeof(err));
            if (ok) {
                snprintf(item, sizeof(item), "%s", vname);
                amAppend(stopped, (int)sizeof(stopped), &stopLen, item);
            } else {
                snprintf(item, sizeof(item), "%s（%s）", vname, err);
                amAppend(failed, (int)sizeof(failed), &failLen, item);
            }
        }
    }

    snprintf(msg, sizeof(msg), "全停：完成（#%d）已停 %s", seq,
             stopped[0] ? stopped : "无设备");
    if (!resetValves) {
        msgLen = (int)strlen(msg);
        amAppend(msg, (int)sizeof(msg), &msgLen, "；阀保持现状");
    }
    if (failed[0]) {
        msgLen = (int)strlen(msg);
        amAppend(msg, (int)sizeof(msg), &msgLen, "；失败 ");
        amAppend(msg, (int)sizeof(msg), &msgLen, failed);
    }
    amLog("auto", msg);

    /* [全停失败必须让人看见] 停机失败是安全事件：补一条 1 号告警进队列
     * （调用方每拍 take → 推送/入库），远端与告警历史都能查到"哪次全停
     * 没停干净"。对齐 Python stop_all_devices 的 ActionFailed 同款口径。
     * [装配前护栏] 模块未装配（控制器未 init）时只停设备不灌报警——
     * 报警队列还没建，灌了就是写野状态。 */
    if (failed[0] && s_ready) {
        char alarmText[AM_TEXT_MAX];
        snprintf(alarmText, sizeof(alarmText), "全停部分失败（#%d）：%s",
                 seq, failed);
        XAutoController_raiseAlarm(&s_ctl, HSF_AUTO_ALARM_ACTION_FAILED,
                                   alarmText,
                                   (uint64_t)XDateTime_currentMSecsSinceEpoch());
    }
    return seq;
}

int HsfServerAutoMode_stopAllDevices(const char* reason)
{
    return HsfServerAutoMode_stopAllDevicesEx(reason, true);
}

/* ==================== 模式序列异常收场 ==================== */

/** @brief 报警 + 锁存 + 停控制器，回 idle 且**不动设备**（_abort_mode）。 */
static void amAbort(uint64_t now, int kind, const char* text)
{
    char msg[AM_TEXT_MAX];
    s_faultLatched = true;
    XAutoController_stop(&s_ctl, now);
    /* [累计运行时长结算 2026-11-12] 模式中止（阀超时等）同样结束"正在运行"
     * 这一段——中止后这一段时长是既成事实，不能丢。 */
    amSettleRunTime(now);
    amClearPhaseState();
    XAutoController_setPhase(&s_ctl, kPhaseIdle);
    XAutoController_raiseAlarm(&s_ctl, kind, text, now);
    snprintf(msg, sizeof(msg), "%s（已停止自动模式，需复位后才能再启动）", text);
    amLog("auto", msg);
}

/* ==================== 故障锁存（2026-11-15 用户定案） ====================
 *
 * [用户口径]「进了报警表的报警要锁存，并自动停止设备，必须手动复位才可以
 *  启动设备」。落到本层的三件事：
 *   ① raiseFault()：设备级故障（变频器故障等）⇒ 报警（进报警历史表）+
 *      **锁存** + **自动停设备**（VFD + 进水泵 + 电动球阀，按模式规则
 *      复位阀）；
 *   ② 锁存未解 ⇒ **手动模式的远程控制也被拒**（App 层各控制 handler 的
 *      闸，见 HsfServerApp.c hsfAppFaultGateText）；
 *   ③ clearFault()：**人工复位**才解锁（自动模式的复位按钮，或设备侧的
 *      「故障复位」——那是人按的，一样算人工复位）。
 *
 * [与 amAbort 的区别] 模式序列中止（阀超时）**不动设备**：阀可能停在中间
 * 位，乱发"关阀"是叠加未知动作；这里是**设备故障**，停机本身就是保护
 * 动作（等价现场按停止）。
 */

bool HsfServerAutoMode_raiseFault(const char* text, uint64_t nowMs,
                                  bool stopDevices, bool enqueueAlarm)
{
    char msg[AM_TEXT_MAX];
    if (!s_ready) return false;
    s_faultLatched = true;
    /* [enqueueAlarm=false] 调用方自己已经发过这条告警（如运行保护/变频器
     * 故障：它们要带 dev 键单独发）⇒ 别再排一条（否则一条变两条）。 */
    if (enqueueAlarm) {
        XAutoController_raiseAlarm(&s_ctl, HSF_AUTO_ALARM_DEVICE_FAULT,
                                   text ? text : "设备故障", nowMs);
    }
    snprintf(msg, sizeof(msg),
             "%s（已锁存：需手动复位后才能再启动设备）",
             text ? text : "设备故障");
    amLog("auto", msg);
    if (stopDevices) {
        /* [对齐 Python 2026-11-15·审计] 原来这里自己拼了两步：
         *   XAutoController_stop(...) + stopAllDevicesEx(text, **true**)
         * ⇒ ① **累计运行时长没结算**（Python 的 raise_fault → stop() 里
         *      有 _settle_run_time()；C 端这条路径漏了 ⇒ 故障停机后
         *      "自动运行 N天 HH时…" 少算这一段）；② 阀复位**写死 true**
         *      （无视逐模式配置 resetValvesOnStop；Python 读模式配置）。
         * ⇒ 改走与 Python 同一条路径：requestStop()（记循环子阶段 →
         *   结算运行时长 → 清相位 → 按**模式配置**决定阀是否复位 →
         *   全停设备）。
         */
        (void)HsfServerAutoMode_requestStop(text ? text : "设备故障", nowMs);
    }
    return true;
}

bool HsfServerAutoMode_clearFault(void)
{
    if (!s_ready || !s_faultLatched) return false;
    s_faultLatched = false;
    amLog("auto", "报警锁存已解除（设备故障复位）");
    return true;
}

/* ==================== 阀相 ==================== */

static void amStepValves(uint64_t now)
{
    const HsfServerIoValve* bank = HsfServerIo_valves();
    int i;

    /* 1) 命令下发：未受理的阀逐个投递（失败按 2000ms 节流重发）。 */
    for (i = 0; i < HSF_VALVE_COUNT; ++i) {
        char err[96];
        char key[16];
        bool ok;
        if (!s_valveInTable[i] || s_valveAcked[i]) continue;
        snprintf(key, sizeof(key), "autovalve%d", i);
        if (!XAutoController_throttle(&s_ctl, key, AM_VALVE_CMD_RETRY_MS, now))
            continue;
        ok = HsfServerIo_valveSet(i, s_valveOpen[i], err, (int)sizeof(err));
        XAutoController_noteAction(&s_ctl, ok, ok ? NULL : err);
        if (ok) s_valveAcked[i] = true;
        /* 失败不中止模式：下一拍节流重发（设备离线抖动是常态）。 */
    }

    /* 2) 到位校验：任何一路判故障（阀层超时/互斥反馈）⇒ 立即中止；
     *    还有没到位的 ⇒ 等下一拍，整体超过截止时刻 ⇒ 序列超时中止。
     * [等待口径 2026-11-13 用户定案] 未选阀打到关闭**是要等待的**
     * （确保未参与生产的阀确实关回才开泵）——现场未接线的阀给不出
     * 关到位 ⇒ 超时报警=保护行为。 */
    for (i = 0; i < HSF_VALVE_COUNT; ++i) {
        const HsfServerIoValveItem* it;
        int state;
        int target;
        char vname[16];
        char text[AM_TEXT_MAX];
        char mname[32];
        if (!s_valveInTable[i]) continue;
        it = bank ? HsfServerIoValve_item(bank, i) : NULL;
        state = it ? it->m_state : (int)HsfValveState_Unknown;
        if (state == (int)HsfValveState_Fault) {
            amValveName(i, vname, (int)sizeof(vname));
            amModeNameOf(amRunModeId(), mname, (int)sizeof(mname));
            snprintf(text, sizeof(text),
                     "模式[%s]：%s 动作超时未到位（判故障）", mname, vname);
            amAbort(now, HSF_AUTO_ALARM_VALVE_TIMEOUT, text);
            return;
        }
        target = s_valveOpen[i] ? (int)HsfValveState_Open
                                : (int)HsfValveState_Closed;
        if (state == target) continue;
        if (now > s_valveDeadlineMs) {
            amValveName(i, vname, (int)sizeof(vname));
            amModeNameOf(amRunModeId(), mname, (int)sizeof(mname));
            snprintf(text, sizeof(text), "模式[%s]：%s 动作超时未到位",
                     mname, vname);
            amAbort(now, HSF_AUTO_ALARM_VALVE_TIMEOUT, text);
            return;
        }
        return;                      /* 还有阀在途中，等下一拍。 */
    }

    /* 3) 全部到位 → 泵相。 */
    XAutoController_setPhase(&s_ctl, kPhasePumps);
    s_phaseAtMs = now;
    s_pumpIssued = false;
    s_pumpStirAcked = false;
    s_pumpInletAcked = false;
    s_pumpDeadlineMs = 0;
    s_pumpFirstErr[0] = '\0';
    {
        char mname[32];
        char msg[96];
        amModeNameOf(amRunModeId(), mname, (int)sizeof(mname));
        snprintf(msg, sizeof(msg), "自动模式[%s]：阀全部到位 → 启动泵", mname);
        amLog("auto", msg);
    }
}

/* ==================== 泵相 ==================== */

static void amStepPumps(uint64_t now)
{
    const HsfServerAutoModeCfg* cfg = amCurCfg();
    bool stirSelected = cfg->m_devices[AM_DEV_STIR];
    bool inletSelected = cfg->m_devices[AM_DEV_INLET];
    char mname[32];
    bool okAll = true;
    char firstErr[128];
    XVfdDevice* vfd = NULL;

    if (s_pumpIssued) return;            /* 两台全受理：进入运行后的幂等出口。 */
    amModeNameOf(amRunModeId(), mname, (int)sizeof(mname));
    firstErr[0] = '\0';

    /* [换相策略收尾 2026-11-14] carry 的泵在换相途中仍在转，而新模式
     * **不选**它 ⇒ 这里（泵相＝换相完成）必须把它停掉，否则它会一直转
     * （泵相只"启动选中的"，从不停"没选的"）。 */
    if (s_carryStirPendingStop && !stirSelected) {
        amStopStirPump();
        s_carryStirPendingStop = false;
    }
    if (s_carryInletPendingStop && !inletSelected) {
        amStopInletPumps();
        s_carryInletPendingStop = false;
    }

    /* 搅拌泵：按配置给定转速（rpm 折算 Hz）与方向；未受理才投递
     * （StartWithGiven 是多阶段单动作，受理后由完成回调推进第二阶段，
     * 重投会被设备级单飞闸拒成"设备忙"）。 */
    if (stirSelected && !s_pumpStirAcked) {
        XVfdControlRequest req;
        bool ok;
        vfd = amFindVfd();
        if (!vfd) {
            char text[AM_TEXT_MAX];
            snprintf(text, sizeof(text), "模式[%s]：搅拌泵（变频器）未装配", mname);
            amAbort(now, HSF_AUTO_ALARM_DEVICE_UNAVAIL, text);
            return;
        }
        memset(&req, 0, sizeof(req));
        req.m_op = XVfdOperation_Start;
        req.m_reverse = cfg->m_stirReverse;
        req.m_valueRpm = (double)cfg->m_stirRpm;
        /* [rpm 按 720/1440 占额定比折算给定] 换算与设备层 SetSpeedRpm 同源
         * （hz = rpm / ratedRpm × maxFreqHz），与手动"转速调速"同一口径。 */
        req.m_valueHz = XVfdDevice_rpmToHz(vfd, (double)cfg->m_stirRpm);
        ok = XVfdDevice_startWithGiven(vfd, XVfdOperation_Start,
                                       cfg->m_stirReverse, req.m_valueHz);
        XAutoController_noteAction(&s_ctl, ok, ok ? NULL : "搅拌泵启动被拒");
        if (ok) {
            s_pumpStirAcked = true;
        } else {
            const char* why = XVfdDevice_lastErrorText(vfd);
            snprintf(firstErr, sizeof(firstErr), "%s",
                     (why && why[0]) ? why : "设备拒绝（在途动作/控制闸/能力不符）");
            okAll = false;
        }
    }
    /* 进水泵：DO6 单点写；同样未受理才投递（pumpSet 幂等："已处于该状态"
     * 返回 true，重投不会重复继电器动作）。 */
    if (okAll && inletSelected && !s_pumpInletAcked) {
        char err[96];
        const bool ok = HsfServerIo_pumpSet(0, true, err, (int)sizeof(err));
        XAutoController_noteAction(&s_ctl, ok, ok ? NULL : err);
        if (ok) {
            s_pumpInletAcked = true;
        } else {
            snprintf(firstErr, sizeof(firstErr), "%s", err);
            okAll = false;
        }
    }
    if (okAll) {
        /* 全部受理（或无泵配置）⇒ 进入运行。 */
        s_pumpIssued = true;
        XAutoController_setPhase(&s_ctl, kPhaseRunning);
        amLog("auto", (stirSelected || inletSelected)
                          ? "自动模式设备全部启动，进入运行"
                          : "自动模式无泵配置，进入运行");
        return;
    }
    /* [失败先重试再中止] 泵写路径的前置在线判定是单拍瞬时值（见
     * AM_PUMP_PHASE_GRACE_MS 注），首败起算宽限、留在泵相逐拍重投；
     * 过限仍失败才按一次性失败的同一文字/类别中止。 */
    if (s_pumpDeadlineMs == 0) {
        s_pumpDeadlineMs = now + AM_PUMP_PHASE_GRACE_MS;
        snprintf(s_pumpFirstErr, sizeof(s_pumpFirstErr), "%s",
                 firstErr[0] ? firstErr : "未知");
    }
    if (now <= s_pumpDeadlineMs) return; /* 留在泵相，等下一拍重投。 */
    {
        char text[AM_TEXT_MAX];
        snprintf(text, sizeof(text), "模式[%s]：泵启动失败 %s", mname,
                 s_pumpFirstErr[0] ? s_pumpFirstErr : "未知");
        amAbort(now, HSF_AUTO_ALARM_ACTION_FAILED, text);
    }
}

/* ==================== 组合模式「自动」循环调度 ==================== */

/** @brief 停搅拌泵（变频器停机；逐侧）。
 *  @details 与 Python `_stop_stir` 同口径：只停，不带给定。 */
static void amStopStirPump(void)
{
    HsfSide side;
    for (side = HsfSide_A; side < HsfSide_Count; ++side) {
        XVfdDevice* dev = HsfServerIo_vfdDevice(side);
        XVfdControlRequest req;    /* 栈上结构体交框架：先 memset。 */
        if (!dev) continue;
        memset(&req, 0, sizeof(req));
        req.m_op = XVfdOperation_Stop;
        (void)XVfdCodec_applyControl(dev, &req);
    }
}

/** @brief 停全部进水泵（逐台；失败只记日志）。 */
static void amStopInletPumps(void)
{
    const HsfServerIoPump* bank = HsfServerIo_pumps();
    const int count = bank ? HsfServerIoPump_count(bank) : 0;
    char err[96];
    int i;
    for (i = 0; i < count; ++i) {
        if (!HsfServerIo_pumpSet(i, false, err, (int)sizeof(err))) {
            char msg[160];
            snprintf(msg, sizeof(msg), "自动循环：换相停泵（#%d）失败 %s",
                     i + 1, err);
            amLog("auto", msg);
        }
    }
}

/** @brief 启动搅拌泵（按目标模式的转速/方向）。
 *  @details 与泵相同款（amStepPumps 的搅拌泵分支）：先写频率给定再发
 *           命令字（否则变频器沿用旧给定跑）。 */
static void amStartStirPump(const HsfServerAutoModeCfg* cfg)
{
    XVfdDevice* vfd = amFindVfd();
    if (!vfd || !cfg) return;
    (void)XVfdDevice_startWithGiven(vfd, XVfdOperation_Start,
                                     cfg->m_stirReverse,
                                     XVfdDevice_rpmToHz(vfd,
                                         (double)cfg->m_stirRpm));
}

/** @brief 启动进水泵（1 台，index 0）。 */
static void amStartInletPump(void)
{
    char err[96];
    if (!HsfServerIo_pumpSet(0, true, err, (int)sizeof(err))) {
        char msg[160];
        snprintf(msg, sizeof(msg), "自动循环：换相启进水泵失败 %s", err);
        amLog("auto", msg);
    }
}

/** @brief 按**目标模式**的换相策略处置两个泵（只管**换相窗口内**）。
 *  @details 2026-11-14 用户定案（与 Python `_apply_switch_policies` 同口径）：
 *            - stopRestart：**保持之前的逻辑**——换相先停泵，阀到位后
 *              （泵相）再按新模式配置启动；
 *            - carry：**保持上一模式的状态**——换相途中该泵不动（物理现状
 *              即上一模式的状态）；
 *            - direct：**直接切到当前模式的状态**——换相立即按新模式的
 *              目标启停该泵（与阀门切换并行）。
 *            [carry 的收尾] carry 且新模式**不选**该泵时，它在换相途中仍在
 *            转；换相完成（泵相）必须把它停掉 ⇒ 记进 s_carry*PendingStop，
 *            由泵相收尾（见 amStepPumps）。 */
static void amApplySwitchPolicies(const char* newMode)
{
    const int idx = HsfServerAutoMode_modeIndex(newMode);
    const HsfServerAutoModeCfg* cfg = (idx >= 0) ? &s_cfg[idx] : NULL;
    const bool wantStir = cfg ? cfg->m_devices[AM_DEV_STIR] : false;
    const bool wantInlet = cfg ? cfg->m_devices[AM_DEV_INLET] : false;
    const char* stirPol = cfg ? cfg->m_stirSwitch : HSF_AUTO_SWITCH_POLICY_DEF;
    const char* inletPol = cfg ? cfg->m_inletSwitch : HSF_AUTO_SWITCH_POLICY_DEF;
    const bool stirKeep = (strcmp(stirPol, HSF_AUTO_SWITCH_POLICY_CARRY) == 0);
    const bool inletKeep = (strcmp(inletPol, HSF_AUTO_SWITCH_POLICY_CARRY) == 0);
    const bool stirDirect = (strcmp(stirPol, HSF_AUTO_SWITCH_POLICY_DIRECT) == 0);
    const bool inletDirect = (strcmp(inletPol, HSF_AUTO_SWITCH_POLICY_DIRECT) == 0);
    char msg[224];

    /* [carry 收尾] 保持运行但新模式不要它 ⇒ 泵相必须停。 */
    if (stirKeep && !wantStir) s_carryStirPendingStop = true;
    if (inletKeep && !wantInlet) s_carryInletPendingStop = true;

    snprintf(msg, sizeof(msg),
             "自动循环：换相策略 搅拌泵=%s 进水泵=%s（目标模式[%s]）",
             stirKeep ? "保持" : (stirDirect ? "直切" : "停"),
             inletKeep ? "保持" : (inletDirect ? "直切" : "停"),
             newMode ? newMode : "?");
    amLog("auto", msg);

    /* ---- 进水泵 ---- */
    if (inletKeep) {
        /* 不动它（保持上一模式的状态）。 */
    } else if (inletDirect) {
        if (wantInlet) {
            amStartInletPump();
        } else {
            amStopInletPumps();
        }
    } else {
        amStopInletPumps();          /* stopRestart：先停，泵相再启。 */
    }
    /* ---- 搅拌泵（变频器）---- */
    if (stirKeep) {
        /* 不动它（保持上一模式的状态）。 */
    } else if (stirDirect) {
        if (wantStir) {
            amStartStirPump(cfg);
        } else {
            amStopStirPump();
        }
    } else {
        amStopStirPump();            /* stopRestart：先停，泵相再启。 */
    }
}

/** @brief 循环换相：按**目标模式**的换相策略处置泵 → 按新模式重建阀目标
 *         （回阀相），累计运行不中断。
 *  @details [为什么不整体 stop/start] 停机清累计运行时间、相位打回 idle；
 *           换相是**同一个运行过程内**换子模式，累计要连续（Python 同口径）。
 *           [泵怎么办 2026-11-14] 换相窗口里阀要走 ~9s 行程，泵不一定该停
 *           ⇒ 逐泵可配（见 amApplySwitchPolicies）。 */
static void amCycleSwitch(const char* newMode, const char* reason, uint64_t now)
{
    char from[32];
    char to[32];
    char msg[224];
    amModeNameOf(s_cycleStage, from, (int)sizeof(from));
    amModeNameOf(newMode, to, (int)sizeof(to));
    amApplySwitchPolicies(newMode);
    snprintf(s_cycleStage, sizeof(s_cycleStage), "%s", newMode);
    s_cycleStageAt = now;
    amBuildValveTargets(now);
    snprintf(msg, sizeof(msg), "自动循环：%s → 切换到[%s]（%s）", from, to,
             (reason && reason[0]) ? reason : "无原因");
    amLog("auto", msg);
}

/** @brief 循环换相判定（只在 running 相、组合模式下跑）。
 *  @details 顺序固定 产水 →（压差达标 或 定时兜底）→ 反洗 →（定时）→
 *           顺洗 →（定时）→ 产水 ……；**压差读不到（None）时不切**
 *           （不按缺数据误判，只走定时兜底）。 */
static void amCycleTick(uint64_t nowMs)
{
    const uint64_t elapsed = (s_cycleStageAt && nowMs > s_cycleStageAt)
                                 ? (nowMs - s_cycleStageAt) : 0;
    char reason[160];
    if (strcmp(s_cycleStage, HSF_AUTO_MODE_ID_PRODUCE) == 0) {
        double mpa = 0.0;
        const bool have = s_pressFn ? s_pressFn(&mpa) : false;
        bool hit = false;
        const bool gt = (strcmp(s_runCycle.m_pressCompare,
                                HSF_AUTO_PRESS_COMPARE_GT) == 0);
        if (have) {
            hit = gt ? (mpa > s_runCycle.m_pressThreshold)
                     : (mpa < s_runCycle.m_pressThreshold);
        }
        if (hit) {
            snprintf(reason, sizeof(reason), "压差 %.3f MPa %s 阈值 %.3f",
                     mpa, gt ? ">" : "<", s_runCycle.m_pressThreshold);
            amCycleSwitch(HSF_AUTO_MODE_ID_BACKWASH, reason, nowMs);
            return;
        }
        if (s_runCycle.m_timerFallback &&
            elapsed >= (uint64_t)s_runCycle.m_timerMinutes * 60000u) {
            snprintf(reason, sizeof(reason), "定时兜底（产水 %d 分钟到期）",
                     s_runCycle.m_timerMinutes);
            amCycleSwitch(HSF_AUTO_MODE_ID_BACKWASH, reason, nowMs);
        }
        return;
    }
    if (strcmp(s_cycleStage, HSF_AUTO_MODE_ID_BACKWASH) == 0) {
        if (elapsed >= (uint64_t)s_runCycle.m_backwashMinutes * 60000u) {
            snprintf(reason, sizeof(reason), "反洗 %d 分钟到期",
                     s_runCycle.m_backwashMinutes);
            amCycleSwitch(HSF_AUTO_MODE_ID_RINSE, reason, nowMs);
        }
        return;
    }
    if (strcmp(s_cycleStage, HSF_AUTO_MODE_ID_RINSE) == 0) {
        if (elapsed >= (uint64_t)s_runCycle.m_rinseMinutes * 60000u) {
            snprintf(reason, sizeof(reason), "顺洗 %d 分钟到期",
                     s_runCycle.m_rinseMinutes);
            amCycleSwitch(HSF_AUTO_MODE_ID_PRODUCE, reason, nowMs);
        }
    }
}

/* ==================== 框架步进回调 ==================== */

static void amStepEntry(void* ud, struct XAutoController* ctl, uint64_t nowMs)
{
    const char* phase;
    (void)ud;
    (void)ctl;
    phase = XAutoController_phase(&s_ctl);
    if (strcmp(phase, kPhaseValves) == 0) {
        amStepValves(nowMs);
    } else if (strcmp(phase, kPhasePumps) == 0) {
        amStepPumps(nowMs);
    } else if (strcmp(phase, kPhaseRunning) == 0) {
        if (s_cycleRoot) {
            /* [组合模式「自动」 2026-11-12] 循环换相判定长在 running 相
             *（与 Python _step 的 running 分支同位置）。 */
            amCycleTick(nowMs);
        }
        /* [运行中周期性持久化累计时长 2026-11-12·对齐 Python] 每
         * AM_RUN_PERSIST_MS(60s) 落一次盘——断电/崩溃最多丢一个窗口的
         * 时长。所有模式都落（不只循环）；不在跑时不落（结算路径已落）。
         * 首拍即落（s_runPersistAtMs==0 ⇒ 与 Python throttle 首次放行
         * 同口径），其后每 60s 一拍。 */
        if (!s_runPersistAtMs ||
            (uint64_t)(nowMs - s_runPersistAtMs) >= AM_RUN_PERSIST_MS) {
            uint64_t total = s_runAccumMs;
            if (s_runStartMs && nowMs > s_runStartMs)
                total += (uint64_t)(nowMs - s_runStartMs);
            s_runPersistAtMs = nowMs;
            amPersistRunMs(total);
        }
    }
}

/* ==================== 现场按键沿（pollIdle 前半） ==================== */

/** @brief 按键沿判据（纯函数；判据的唯一事实源；回归锁直接驱动）。 */
bool HsfServerAutoMode_buttonEdgeRule(int prev, int level, bool nc)
{
    if (level < 0) return false;         /* 读不到：跨过未知不算沿。 */
    if (nc) {
        return (level == 0 && prev == 1); /* 常闭：不按=1，按下=0 ⇒ 1→0。 */
    }
    /* [2026-11-15 审计] 常开沿要求**上一次电平已知且为 0**：原判据
     * `prev != 1` 把「上一次是未知(-1)」也算成沿 ⇒ **第一帧读到 1 就
     * 当成"刚按下"** ⇒ 服务端一启动，若启动键此刻是 1（被按住、输入浮空
     * 读 1、模块首帧全 1）就**自动启动**（现场安全事故）。Python 侧
     * buttons.py 同步修（两端口径必须一致）。 */
    return (level == 1 && prev == 0);     /* 常开：不按=0，按下=1 ⇒ 0→1。 */
}

/** @brief 单键沿判定：level -1=未知（不产生假沿）；nc=true=常闭（下降沿）。 */
static bool amEdge(int* prev, int level, bool nc, int keySlot, uint64_t now)
{
    bool edge;
    if (level < 0) {                 /* 读不到：跨过未知不算沿。 */
        *prev = -1;
        return false;
    }
    edge = HsfServerAutoMode_buttonEdgeRule(*prev, level, nc);
    *prev = level;
    if (!edge) return false;
    if (s_lastEdgeMs[keySlot] != 0 &&
        now - s_lastEdgeMs[keySlot] < (uint64_t)(s_btnDebounceMs > 0
                                                     ? s_btnDebounceMs : 0))
        return false;                /* 同键去抖窗口内的第二次沿丢弃。 */
    s_lastEdgeMs[keySlot] = now;
    return true;
}

static void amHandleButtons(uint64_t now, bool autoMode)
{
    uint32_t di = 0;
    bool valid = false;
    bool have;
    int startLvl;
    int stopLvl;
    int resetLvl;
    bool startEdge;
    bool stopEdge;
    bool resetEdge;

    have = HsfServerIo_digitalDi(&di, &valid) && valid;
    startLvl = have ? (int)((di >> HSF_BTN_START_DI) & 1u) : -1;
    stopLvl = have ? (int)((di >> HSF_BTN_STOP_DI) & 1u) : -1;
    resetLvl = have ? (int)((di >> HSF_BTN_RESET_DI) & 1u) : -1;
    /* 触点类型（buttons.py 现场口径；2026-11-13 起可配）：缺省启动/复位
     * 常开（上升沿）、停止常闭（下降沿 1→0——"线断了=停止"，安全侧接线）；
     * 现场换接法改 auto/buttons 配置，不再改代码重编。 */
    startEdge = amEdge(&s_prevStart, startLvl, s_btnNcStart, 0, now);
    stopEdge = amEdge(&s_prevStop, stopLvl, s_btnNcStop, 1, now);
    resetEdge = amEdge(&s_prevReset, resetLvl, s_btnNcReset, 2, now);

    /* ---- 停止：无条件受理（停机类动作不被任何闸门挡住） ---- */
    if (stopEdge) {
        if (HsfServerAutoMode_running()) {
            (void)HsfServerAutoMode_requestStop("现场停止按钮", now);
        } else {
            /* 没在跑也照样停一次设备：现场按停止是"我要设备停"。
             * [阀复位走模式配置 2026-11-13·对齐 Python] Python 同场景两
             * 分支都传 _reset_valves_on_stop()（autocontrol.py:1162/1170）；
             * 原实现这里调恒参 wrapper（硬编码 resetValves=true）⇒ 配了
             * 「停止不复位阀」的模式在未运行态按停止仍被发关阀——对可能
             * 停在中间位的阀叠加未知动作，正是该开关要避免的。 */
            (void)HsfServerAutoMode_stopAllDevicesEx("现场停止按钮",
                                                     amResetValvesOnStop());
            amLog("auto", "自动运行：未在运行，现场停止按钮仍执行全停");
        }
    }
    /* ---- 复位：解除急停锁存与报警锁存（不启动） ---- */
    if (resetEdge) {
        (void)HsfServerAutoMode_requestReset(now);
    }
    /* ---- 启动：必须同时满足全部条件（含模式配置可跑），拒只记日志 ---- */
    if (startEdge) {
        char why[128];
        char whyMode[128];
        if (!HsfServerAutoMode_startPreconditions(autoMode, why,
                                                  (int)sizeof(why))) {
            char msg[160];
            snprintf(msg, sizeof(msg), "自动运行：现场启动按钮被拒 —— %s", why);
            amLog("auto", msg);
            return;
        }
        if (!HsfServerAutoMode_modeConfigError(s_mode, whyMode,
                                               (int)sizeof(whyMode))) {
            char msg[192];
            snprintf(msg, sizeof(msg), "自动运行：现场启动按钮被拒 —— %s",
                     whyMode);
            amLog("auto", msg);
            return;
        }
        {
            const bool changed = HsfServerAutoMode_start(NULL, now);
            amLog("auto", changed ? "自动运行：现场启动按钮受理（已启动）"
                                  : "自动运行：现场启动按钮受理（已在运行）");
        }
    }
}

/* ==================== 急停回路闸（pollIdle 后半） ==================== */

static void amCheckEstop(uint64_t now)
{
    bool configured = HsfServerIo_digitalReady();
    uint32_t di = 0;
    bool valid = false;
    int estop;                        /* 1=正常 0=无信号 -1=未知。 */
    const char* reason;

    if (!configured) {
        /* 没配急停回路：没有这道安全件，不能拿它当闸（同启动四闸）。 */
        if (strcmp(XAutoController_phase(&s_ctl), kPhaseEstop) == 0 &&
            !XAutoController_estopped(&s_ctl)) {
            XAutoController_setPhase(&s_ctl, kPhaseIdle);
        }
        return;
    }
    estop = (HsfServerIo_digitalDi(&di, &valid) && valid)
                ? (((di >> HSF_BTN_ESTOP_DI) & 1u) ? 1 : 0)
                : -1;
    if (estop == 1) {
        s_estopEverOk = true;
        if (strcmp(XAutoController_phase(&s_ctl), kPhaseEstop) == 0 &&
            !XAutoController_estopped(&s_ctl)) {
            XAutoController_setPhase(&s_ctl, kPhaseIdle);
        }
        return;
    }
    /* 0（无信号）与 -1（配了但读不到/离线）都按禁止处理。 */
    reason = (estop == 0) ? "急停无信号（按下或断线）"
                          : "急停状态未知（数字量模块离线）";
    if (!s_estopEverOk) {
        /* 情形②：还没见过第一帧，不锁存（控制器本来也起不来——启动四闸
         * 会拒）。只把相位记成"等待采集"，一次性日志留痕。 */
        if (!s_estopBootWarned) {
            char msg[160];
            s_estopBootWarned = true;
            snprintf(msg, sizeof(msg),
                     "自动控制：%s（首帧未到，暂不锁存；采到急停正常后才按"
                     "真实急停处理）", reason);
            amLog("auto", msg);
        }
        return;
    }
    if (!XAutoController_estopped(&s_ctl)) {
        char msg[160];
        snprintf(msg, sizeof(msg), "自动控制：%s ⇒ 停止自动运行并全停设备",
                 reason);
        amLog("auto", msg);
        /* [急停触发点] 4 号告警在框架 eStop 里自动入队，这里不重复 raise。 */
        (void)HsfServerAutoMode_requestEstop(reason, now);
    }
}

/* ==================== 公开 API：判据 ==================== */

HsfServerAutoModeEstopState HsfServerAutoMode_estopState(void)
{
    uint32_t di = 0;
    bool valid = false;

    if (!HsfServerIo_digitalReady()) {
        /* [只拦"配了"的现场] 没配急停回路就没有这道安全件，不能拿它当闸
         * （否则未装操作柱的现场永远起不来）。[C 端"配置"的定义] Python
         * 判 button_items 里有没有 estop_di 点位；C 端点位是编译期宏
         * （HSF_BTN_ESTOP_DI=12，挂在与阀同一台数字量模块上）——"配置了"
         * = 数字量站已装配。无数字量站的现场自然落到"未配置放行"分支，
         * 与 Python estop_configured=False 同效果。 */
        return HSF_ESTOP_CIRCUIT_UNCONFIGURED;
    }
    if (!(HsfServerIo_digitalDi(&di, &valid) && valid)) {
        return HSF_ESTOP_CIRCUIT_UNKNOWN;   /* 配了但读不到：回路可能断了。 */
    }
    /* 急停常闭：1=回路通（有信号）、0=按下或断线。 */
    return ((di >> HSF_BTN_ESTOP_DI) & 1u) ? HSF_ESTOP_CIRCUIT_OK
                                           : HSF_ESTOP_CIRCUIT_NO_SIGNAL;
}

bool HsfServerAutoMode_startPreconditions(bool autoMode, char* outErr, int cap)
{
    const char* why = NULL;

    if (!autoMode) {
        why = "当前是手动模式（按钮启停只在自动模式下生效）";
    } else if (s_faultLatched) {
        why = "有报警未复位，需先按复位再启动";
    } else if (XAutoController_estopped(&s_ctl)) {
        why = "急停已锁存，需先复位";
    } else {
        /* [三态单源] 急停回路判定与 VFD 控制急停前置闸共用 estopState
         *（2026-11-12 收拢；文案与 Python start_preconditions 逐字同）。 */
        switch (HsfServerAutoMode_estopState()) {
        case HSF_ESTOP_CIRCUIT_UNKNOWN:
            why = "急停状态未知（数字量模块离线）";
            break;
        case HSF_ESTOP_CIRCUIT_NO_SIGNAL:
            why = "急停无信号（按下或断线）";
            break;
        default:
            break;   /* 回路正常 / 未配置：放行。 */
        }
    }
    if (why && outErr && cap > 0) snprintf(outErr, (size_t)cap, "%s", why);
    return why == NULL;
}

bool HsfServerAutoMode_modeConfigError(const char* modeId, char* outErr, int cap)
{
    char why[128];
    const HsfServerAutoModeCfg* cfg;
    const HsfServerIoValve* bank;
    int i;
    int selected = 0;

    why[0] = '\0';
    if (!modeId || !modeId[0] ||
        HsfServerAutoMode_modeIndex(modeId) < 0) {
        if (modeId && strcmp(modeId, HSF_AUTO_MODE_ID_AUTO) == 0) {
            /* [组合模式 2026-11-12] 校验循环启用 + 三个子模式都配设备
             *（与 Python _cycle_config_error 同口径同文字）。 */
            int m;
            int missing = 0;
            if (!s_cycleCfg.m_enabled) {
                snprintf(why, sizeof(why), "%s",
                         "「自动」循环模式未启用（远端设置 → 自动循环配置）");
            } else {
                char miss[96];
                int missLen = 0;
                miss[0] = '\0';
                for (m = 0; m < HSF_AUTO_MODE_COUNT; ++m) {
                    int d;
                    int sel = 0;
                    for (d = 0; d < HSF_AUTO_DEVICE_COUNT; ++d)
                        if (s_cfg[m].m_devices[d]) ++sel;
                    if (sel > 0) continue;
                    ++missing;
                    amAppend(miss, (int)sizeof(miss), &missLen,
                             missLen ? "、" : "");
                    amAppend(miss, (int)sizeof(miss), &missLen, kModeNames[m]);
                }
                if (missing > 0) {
                    snprintf(why, sizeof(why),
                             "「自动」循环需要三个子模式都配设备，缺：%s", miss);
                }
            }
        } else {
            snprintf(why, sizeof(why), "%s",
                     "未选择自动模式（先在客户端主界面选模式）");
        }
    } else {
        cfg = &s_cfg[HsfServerAutoMode_modeIndex(modeId)];
        for (i = 0; i < HSF_AUTO_DEVICE_COUNT; ++i)
            if (cfg->m_devices[i]) ++selected;
        if (selected == 0) {
            snprintf(why, sizeof(why), "模式[%s]未配置设备（远端设置 → 自动模式配置）",
                     modeId);
        } else {
            bank = HsfServerIo_valves();
            for (i = 0; i < HSF_VALVE_COUNT && !why[0]; ++i) {
                if (!cfg->m_devices[i]) continue;
                if (!amValveReady(bank, i)) {
                    snprintf(why, sizeof(why), "阀%d 未配置点位，无法参与模式[%s]",
                             i + 1, modeId);
                }
            }
            if (!why[0] && cfg->m_devices[AM_DEV_STIR] && !amFindVfd()) {
                snprintf(why, sizeof(why), "搅拌泵（变频器）未装配，模式[%s]无法启动",
                         modeId);
            }
            if (!why[0] && cfg->m_devices[AM_DEV_INLET]) {
                const HsfServerIoPump* pumps = HsfServerIo_pumps();
                if (!pumps || HsfServerIoPump_count(pumps) <= 0) {
                    snprintf(why, sizeof(why), "进水泵未装配，模式[%s]无法启动",
                             modeId);
                }
            }
        }
    }
    if (why[0] && outErr && cap > 0) snprintf(outErr, (size_t)cap, "%s", why);
    return !why[0];
}

/* ==================== 公开 API：四板斧 ==================== */

/** @brief 按当前（子）模式配置重建阀相目标（启动与换相共用）。 */
static void amBuildValveTargets(uint64_t nowMs)
{
    const HsfServerAutoModeCfg* cfg = amCurCfg();
    const HsfServerIoValve* bank = HsfServerIo_valves();
    int i;
    s_valveCount = bank ? HsfServerIoValve_count(bank) : 0;
    memset(s_valveAcked, 0, sizeof(s_valveAcked));
    for (i = 0; i < HSF_VALVE_COUNT; ++i) {
        s_valveInTable[i] = i < s_valveCount && amValveReady(bank, i);
        s_valveOpen[i] = cfg->m_devices[i];
    }
    s_phaseAtMs = nowMs;
    s_valveDeadlineMs = nowMs + amValveMoveTimeoutMs() + AM_VALVE_PHASE_GRACE_MS;
    s_pumpIssued = false;
    XAutoController_setPhase(&s_ctl, kPhaseValves);
}

/** @brief 记住当前循环子阶段（停止/急停时调用；非组合模式不做）。
 *  @note  必须在 amClearPhaseState 之前调（那里会清 s_cycleRoot）。 */
static void amRememberCycleStage(void)
{
    if (!s_cycleRoot) return;
    if (s_cycleStage[0]) {
        snprintf(s_cycleResumeStage, sizeof(s_cycleResumeStage), "%s",
                 s_cycleStage);
    }
}

/** @brief 换相策略取值是否合法（三个取值之一；2026-11-14）。
 *  @details 与 Python proto.AUTO_SWITCH_POLICY_IDS 同集合。 */
static bool amSwitchPolicyValid(const char* p)
{
    return p && (strcmp(p, HSF_AUTO_SWITCH_POLICY_STOP_RESTART) == 0
              || strcmp(p, HSF_AUTO_SWITCH_POLICY_CARRY) == 0
              || strcmp(p, HSF_AUTO_SWITCH_POLICY_DIRECT) == 0);
}

/**
 * @brief 当前模式的「停止时是否关阀复位」（无配置按缺省 true）。
 * @details **手动模式一律 true**（2026-11-15 用户口径：手动模式下按停止
 *          也要把设备与球阀全部停下来）。手动模式没有工艺序列，阀不会正
 *          停在"某个必须保持的中间位"；逐模式配置只管自动模式那三种序列。
 *          与 Python 侧 AutoController._reset_valves_on_stop 同口径。
 */
static bool amResetValvesOnStop(void)
{
    const HsfServerAutoModeCfg* cfg = amCurCfg();
    if (!s_autoMode) return true;   /* 手动模式：强制关阀复位。 */
    return cfg ? cfg->m_resetValvesOnStop
               : (bool)HSF_AUTO_MODE_RESET_VALVES_ON_STOP_DEF;
}

/**
 * @brief 当前模式的「急停时是否关阀复位」（无配置按缺省 false）。
 * @details **手动模式一律 true**（2026-11-15 用户口径：手动模式下急停也
 *          要有效果，设备与球阀全部停下来）。自动模式维持逐模式配置（缺省
 *          false＝阀保持现状，见 2026-11-12 用户口径）。
 *          与 Python 侧 AutoController._reset_valves_on_estop 同口径。
 */
static bool amResetValvesOnEstop(void)
{
    const HsfServerAutoModeCfg* cfg = amCurCfg();
    if (!s_autoMode) return true;   /* 手动模式：强制关阀复位。 */
    return cfg ? cfg->m_resetValvesOnEstop
               : (bool)HSF_AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF;
}

bool HsfServerAutoMode_start(const char* modeId, uint64_t nowMs)
{
    if (!s_ready) return false;
    if (XAutoController_running(&s_ctl)) return false;   /* 幂等。 */
    if (modeId && modeId[0]) {
        snprintf(s_mode, sizeof(s_mode), "%s", modeId);
        /* [粘性分账 2026-11-20] 本轮运行的模式**快照**一份：运行期读配置/
         * 显示/回显都用它，不受运行中 op=5 改粘性的影响（Python
         * ctl.mode = mode 同款）。 */
        snprintf(s_runMode, sizeof(s_runMode), "%s", modeId);
    }
    if (!s_mode[0]) {
        amLog("auto", "自动运行：拒绝启动 —— 未选择自动模式（先在客户端主界面选模式）");
        return false;
    }
    /* 框架 start：急停锁存中返回 false（调用方已过启动四闸，双保险）。 */
    if (!XAutoController_start(&s_ctl, nowMs)) return false;

    /* [累计运行时长起算 2026-11-12] 只有自动运行**真的起来了**才计时
     * （"已启动"这一刻起算；此前累计保留，多次运行是累加）。 */
    s_runStartMs = nowMs;

    /* [组合模式「自动」 2026-11-12] 子阶段 = 记忆阶段（停止/急停过）否则
     * 从产水开始；阶段从**阀相**就起算（阀到位/泵启动都占用该模式时长）。 */
    if (strcmp(s_mode, HSF_AUTO_MODE_ID_AUTO) == 0) {
        const char* stage = s_cycleResumeStage[0] ? s_cycleResumeStage
                                                  : HSF_AUTO_MODE_ID_PRODUCE;
        snprintf(s_cycleStage, sizeof(s_cycleStage), "%s", stage);
        s_cycleRoot = true;
        s_cycleStageAt = nowMs;
        /* [运行期快照 2026-11-12·对齐 Python] 本次运行的循环参数在此拍快照。 */
        s_runCycle = s_cycleCfg;
    } else {
        s_cycleRoot = false;
        s_cycleStage[0] = '\0';
    }

    /* ---- 阀相目标：选中阀开、未选阀关；未配置点位的阀不进目标表 ---- */
    amBuildValveTargets(nowMs);
    {
        char mname[32];
        char smsg[80];
        char msg[192];
        amModeNameOf(s_mode, mname, (int)sizeof(mname));
        if (s_cycleRoot) {
            char sname[32];
            amModeNameOf(s_cycleStage, sname, (int)sizeof(sname));
            snprintf(smsg, sizeof(smsg), "，阶段[%s]%s", sname,
                     s_cycleResumeStage[0] ? "（从记忆阶段继续）" : "");
        } else {
            smsg[0] = '\0';
        }
        snprintf(msg, sizeof(msg), "自动运行：已启动（模式[%s]%s，进入阀相）",
                 mname, smsg);
        amLog("auto", msg);
    }
    return true;
}

/**
 * @brief 模式 id 是否合法（三子模式 + 组合循环 "auto"）。
 * @details 组合循环的**配置**校验不在这里做（留到启动时刻的
 *          modeConfigError）——选模式只是记粘性。
 */
bool HsfServerAutoMode_isKnownMode(const char* modeId)
{
    if (!modeId || !modeId[0]) return false;
    if (strcmp(modeId, HSF_AUTO_MODE_ID_AUTO) == 0) return true;
    return HsfServerAutoMode_modeIndex(modeId) >= 0;
}

bool HsfServerAutoMode_selectMode(const char* modeId)
{
    if (!s_ready) return false;
    /* [运行中也受理 2026-11-18·两端对齐] 见 HsfServerApp.c op=5 分支的
     * 说明：**三账分立**（粘性 s_mode / 运行中 s_runMode / 组合循环当前
     * 子阶段 s_cycleStage）⇒ 运行中改选只改**粘性**，不碰正在跑的工序
     * ⇒ 可以受理。 */
    if (!HsfServerAutoMode_isKnownMode(modeId)) return false;
    snprintf(s_mode, sizeof(s_mode), "%s", modeId);
    /* [落盘 2026-11-13·对齐 Python persist_mode] 物理启动按钮用这份粘性：
     * 只存内存 ⇒ 服务端重启即清空 ⇒ 现场按键被拒「未选择自动模式」。 */
    HsfServerSettings_setString(AM_KEY_STICKY_MODE, s_mode);
    {
        char mname[32];
        char msg[96];
        amModeNameOf(s_mode, mname, (int)sizeof(mname));
        snprintf(msg, sizeof(msg),
                 "自动运行：已选择模式[%s]（只记粘性，不启动）", mname);
        amLog("auto", msg);
    }
    return true;
}

bool HsfServerAutoMode_stopController(const char* reason, uint64_t nowMs)
{
    /* [记住循环子阶段 2026-11-12] 必须在 amClearPhaseState 之前记（那里
     * 会清 s_cycleRoot）——否则「停止后从原阶段继续」永远回落产水。 */
    amRememberCycleStage();
    const bool changed = XAutoController_stop(&s_ctl, nowMs);
    /* [累计运行时长结算 2026-11-12] 把本次运行的一段并进累计（停止/切模式
     * 都走这里；不在跑时是空操作）。 */
    amSettleRunTime(nowMs);
    amClearPhaseState();
    /* [急停锁存期相位保持 2026-11-13·对齐 Python] 框架 stop 在急停锁存中
     * 返回 false（不在 Running 态）且从不写相位——此时把相位打回 idle 会
     * 制造「相位=idle 但急停锁存仍在」的组合：客户端复位钮判据是
     * phase=estop ∨ autoFault（纯急停锁存下 autoFault=false）⇒ 置灰，
     * 远程复位不可达；启动闸又按锁存拒绝启动。Python stop() 同路径
     * （not running 早退分支）不碰 self.phase，相位保持 "estop"。
     * 故只在框架真的停掉了（changed）时才清相位。 */
    if (changed) XAutoController_setPhase(&s_ctl, kPhaseIdle);
    if (changed) {
        char msg[96];
        if (reason && reason[0]) {
            snprintf(msg, sizeof(msg), "自动运行：已停止（%s）", reason);
        } else {
            snprintf(msg, sizeof(msg), "自动运行：已停止");
        }
        amLog("auto", msg);
    }
    return changed;
}

bool HsfServerAutoMode_requestStop(const char* reason, uint64_t nowMs)
{
    const bool resetValves = amResetValvesOnStop();
    const bool changed = HsfServerAutoMode_stopController(
        reason && reason[0] ? reason : "停止", nowMs);
    /* [G4 修复] 停止 = 停控制器 + 全停设备：只停控制器会让设备继续按上一条
     * 命令动着，现场看到的是"我按了停止，泵还在转"。
     * [逐模式阀复位 2026-11-12] 阀是否关回看当前模式配置（缺省关回）。 */
    (void)HsfServerAutoMode_stopAllDevicesEx(
        reason && reason[0] ? reason : "停止", resetValves);
    return changed;
}

bool HsfServerAutoMode_requestEstop(const char* reason, uint64_t nowMs)
{
    /* [记住循环子阶段 2026-11-12] 急停也要记住（复位前从原阶段继续）。 */
    amRememberCycleStage();
    const bool resetValves = amResetValvesOnEstop();
    /* 框架 eStop：首次入急停返回 true 并自动入队 4 号告警；已锁存返回
     * false（幂等，不重复报警）。 */
    const bool first = XAutoController_eStop(&s_ctl, nowMs);
    /* [累计运行时长结算 2026-11-12] 急停同样结束"正在运行"这一段。 */
    amSettleRunTime(nowMs);
    XAutoController_setPhase(&s_ctl, kPhaseEstop);
    /* [阀保持现状 2026-11-12] 急停缺省不关阀（只看配置）。 */
    (void)HsfServerAutoMode_stopAllDevicesEx(
        reason && reason[0] ? reason : "急停", resetValves);
    if (first) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "自动运行：急停（已锁存，需复位后才能再次启动；阀%s）",
                 resetValves ? "复位关回" : "保持现状");
        amLog("auto", msg);
    }
    return first;
}

bool HsfServerAutoMode_requestReset(uint64_t nowMs)
{
    const bool wasEstop = XAutoController_estopped(&s_ctl);
    const bool hadFault = s_faultLatched;

    /* 框架 reset 只解急停锁存（非急停态返回 false）；报警锁存在本模块。 */
    (void)XAutoController_reset(&s_ctl, nowMs);
    s_faultLatched = false;
    /* [复位清阶段记忆 2026-11-12] 用户口径「除非按了复位」——复位后
     * 下次组合模式启动从**产水**重新开始，不接旧阶段。 */
    s_cycleResumeStage[0] = '\0';
    XAutoController_setPhase(&s_ctl, kPhaseIdle);
    if (wasEstop || hadFault) {
        char msg[96];
        snprintf(msg, sizeof(msg), "自动运行：已复位（%s解除，处于停止态）",
                 (wasEstop && hadFault) ? "急停锁存、报警锁存"
                     : (wasEstop ? "急停锁存" : "报警锁存"));
        amLog("auto", msg);
    }
    return wasEstop || hadFault;
}

/* ==================== 按钮组三盏灯 ==================== */

/**
 * @brief 手动模式的「在动」判定（启动灯口径；2026-11-15 用户口径）。
 * @details 现场还有设备在动即返回 true：
 *          · **搅拌泵**（变频器）：遥测有效 ∧（运行中 ∨ 输出频率 > 0）
 *            ——输出频率 > 0 覆盖"停机斜坡"（电机还在转但状态字已报停）；
 *          · **进水泵**：命令态为开（泵无反馈点，输出即状态）；
 *          · **任意一路电动球阀在开位**：开到位反馈为真，或最近一条命令
 *            是"开"（含正在开到位途中）。
 *
 * [为什么这样判"阀开着"] 阀动起来之后**两个到位反馈都不吸**（动作中，
 *   行程 ~9s）⇒ 单看反馈会漏掉"正在开到位途中"那 9s ⇒ 用"开到位 ∨ 开
 *   命令"兜住；下了关命令 openCmd 清零 ⇒ 灯灭。
 * [为什么不用"还没到关位"] 现场只有 1 号阀接了行程开关（其余 5 路无反馈）
 *   ⇒ "没到关位"会把 5 口没接线的阀判成"永远开着" ⇒ 灯常亮。
 *
 * [与 Python 端同口径] server_py/hsfsrv/autocontrol.py 的
 * AutoContext.manual_running() 逐条对应（VFD 遥测有效 ∧（运行中 ∨ 输出
 * 频率 > 0）；泵命令态；阀 = 开到位反馈 ∨ 开命令）。
 */
static bool amManualRunning(void)
{
    HsfSide side;
    int i;

    /* 1) 搅拌泵（变频器）：逐侧独立（两侧各有一台变频器）。 */
    for (side = HsfSide_A; side < HsfSide_Count; ++side) {
        XVfdDevice* dev = HsfServerIo_vfdDevice(side);
        const XVfdStatus* vs = dev ? XVfdDevice_status_const(dev) : NULL;
        if (!vs || !vs->m_valid) continue;
        if (vs->m_running || vs->m_outputFreqHz > 0.0) {
            return true;
        }
    }
    /* 2) 进水泵：命令态为开（泵无反馈点）。 */
    {
        const HsfServerIoPump* bank = HsfServerIo_pumps();
        const int count = bank ? HsfServerIoPump_count(bank) : 0;
        for (i = 0; i < count; ++i) {
            const HsfServerIoPumpItem* it = HsfServerIoPump_item(bank, i);
            if (it && it->m_on) {
                return true;
            }
        }
    }
    /* 3) 电动球阀：开到位 ∨ 已下过开命令。 */
    {
        const HsfServerIoValve* bank = HsfServerIo_valves();
        const int count = bank ? HsfServerIoValve_count(bank) : 0;
        for (i = 0; i < count; ++i) {
            const HsfServerIoValveItem* it = HsfServerIoValve_item(bank, i);
            if (!it) continue;
            if (it->m_openFb || it->m_openCmd) {
                return true;
            }
        }
    }
    return false;
}

/** @brief 灯显示口径的"在运行"（真在跑 ∧ 急停回路正常；急停异常一律显示停着）。 */
static bool amLampStateRunning(void)
{
    /* Python buttons.poll 第4步：estop_ok is not True ⇒ 一律显示"停着"
     *（观感安全侧；要不要锁存由 amCheckEstop/启动闸另行判定）。 */
    if (!XAutoController_running(&s_ctl)) return false;
    return HsfServerAutoMode_estopState() == HSF_ESTOP_CIRCUIT_OK;
}

/**
 * @brief 按钮组三盏灯跟随（每拍；**两种模式都写**，口径按模式分）。
 * @details 与 Python buttons.py `_apply_lamps` 同口径：
 *   · 目标：启动灯 DO7=**在动**；停止灯 DO8=**非在动**（与启动灯严格互斥）；
 *     复位灯 DO9=**需要复位时常亮**（报警锁存 ∨ 急停锁存 ⇒ 不按复位
 *       就一直亮，提示现场有未清除的锁存）；没有锁存时仍是**点动**
 *       镜像按键（按住才亮、松手就灭）。[2026-11-18 用户口径]
 *     · **自动模式**：在动 = 自动运行中 ∧ 急停回路正常（急停异常显示停着）。
 *     · **手动模式**：在动 = **现场还有设备在动**（amManualRunning：搅拌泵
 *       在转 / 进水泵在开 / 任一球阀在开位或正在开到位途中）——用户口径
 *       2026-11-15：手动模式下启动灯要如实反映设备在不在跑，不能停在
 *       旧状态（现场看到"启动灯亮"却什么设备都没在跑 ⇒ 误判）。
 *   · [回读校准] 每拍读 DO 实际电平，与目标不一致才写（幂等补写）——
 *     外部把 DO 清零后灯能自愈（板端 2026-11-06 实锅：只记目标值不回读
 *     ⇒ 数字量页清零后操作柱灯永不亮）。读不到（None）也写一次。
 */
static void amSyncStationLamps(bool autoMode)
{
    bool running;
    bool resetHeld = false;
    /* [需要复位时常亮 2026-11-18] 报警锁存 ∨ 急停锁存 ⇒ 复位灯常亮
     *（提示现场「要按复位」）；没有锁存时仍是点动镜像。 */
    bool needReset = false;
    const int lampDo[3] = { HSF_BTN_START_LAMP_DO, HSF_BTN_STOP_LAMP_DO,
                            HSF_BTN_RESET_LAMP_DO };
    uint32_t di = 0;
    bool diValid = false;
    uint32_t doMask = 0;
    bool doValid = false;
    int i;

    if (!HsfServerIo_digitalReady()) {
        return;   /* 没有数字量站 = 没有灯可写。 */
    }
    (void)HsfServerIo_digitalDi(&di, &diValid);
    /* 复位 DI15 常开：按住=1（Python pressed[reset]=(cur is True)）。 */
    resetHeld = diValid && ((di >> HSF_BTN_RESET_DI) & 1u);
    needReset = HsfServerAutoMode_faultLatched()
                 || XAutoController_estopped(&s_ctl);
    /* [在动的口径按模式分 2026-11-15] */
    running = autoMode ? amLampStateRunning() : amManualRunning();

    (void)HsfServerIo_digitalDo(&doMask, &doValid);
    for (i = 0; i < 3; ++i) {
        bool want;
        bool actual;
        switch (i) {
        case 0: want = running; break;    /**< DO7 启动灯 = 运行中。 */
        case 1: want = !running; break;   /**< DO8 停止灯 = 非运行（急停也亮）。 */
        default: want = (needReset || resetHeld); break;
                 /**< DO9 复位灯 = 需要复位时常亮，否则点动镜像。 */
        }
        actual = doValid && ((doMask >> lampDo[i]) & 1u);
        /* [回读 ≠ 目标 才写] 读不到（doValid=false）也写一次（按"不知道"
         * 处理；与 Python actual is None ⇒ 仍写 同口径）。 */
        if (!doValid || actual != want) {
            (void)HsfServerIo_digitalWrite(lampDo[i], want);
        }
    }
}

/* ==================== 每拍入口 ==================== */

void HsfServerAutoMode_tick(uint64_t nowMs, bool autoMode)
{
    if (!s_ready) return;
    /* [运行模式同步 2026-11-15] 本模块的灯口径与"停止/急停要不要关阀"都
     * 依赖当前模式（手动：一律关阀 + 启动灯反映设备在动）。 */
    s_autoMode = autoMode;
    amHandleButtons(nowMs, autoMode);       /* 按键沿受理（无条件）。 */
    amCheckEstop(nowMs);                    /* 急停回路闸（无条件）。 */
    XAutoController_step(&s_ctl, nowMs);    /* 仅运行中放行（框架 50ms 节流）。 */
    amSyncStationLamps(autoMode);           /* 灯跟随（两种模式都写）。 */
    /* [临时诊断 HSF_AUTO_DIAG=1 2026-11-12] 累计时长「运行中但 runMs=0」
     * 现场复现取证（5s 一条；复现后整段删除——AGENTS.md 探针零残留纪律）。 */
    if (getenv("HSF_AUTO_DIAG")) {
        static int diagCnt;
        if ((++diagCnt % 50) == 0)
            XPrintf("[auto-diag] now=%llu ready=%d running=%d phase=%s "
                    "accum=%llu start=%llu persistAt=%llu runMs=%llu\n",
                    (unsigned long long)nowMs,
                    (int)s_ready, (int)XAutoController_running(&s_ctl),
                    XAutoController_phase(&s_ctl),
                    (unsigned long long)s_runAccumMs,
                    (unsigned long long)s_runStartMs,
                    (unsigned long long)s_runPersistAtMs,
                    (unsigned long long)HsfServerAutoMode_runMs());
    }
}

/* ==================== 配置（auto 配置节） ==================== */

int HsfServerAutoMode_modeIndex(const char* modeId)
{
    int i;
    if (!modeId) return -1;
    for (i = 0; i < HSF_AUTO_MODE_COUNT; ++i)
        if (strcmp(kModeIds[i], modeId) == 0) return i;
    return -1;
}

const char* HsfServerAutoMode_modeName(int index)
{
    return (index >= 0 && index < HSF_AUTO_MODE_COUNT) ? kModeNames[index] : "";
}

int HsfServerAutoMode_deviceIndex(const char* deviceId)
{
    int i;
    char buf[16];
    if (!deviceId) return -1;
    for (i = 0; i < 6; ++i) {
        snprintf(buf, sizeof(buf), HSF_AUTO_DEVICE_VALVE_FMT, i + 1);
        if (strcmp(buf, deviceId) == 0) return i;
    }
    if (strcmp(HSF_AUTO_DEVICE_ID_STIR_PUMP, deviceId) == 0) return AM_DEV_STIR;
    if (strcmp(HSF_AUTO_DEVICE_ID_INLET_PUMP, deviceId) == 0) return AM_DEV_INLET;
    return -1;
}

/** @brief 把一份配置写成 JSON 串并落 INI（auto/<模式id>）。 */
static void amSaveModeCfg(int modeIdx)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonArray* arr;
    char key[32];
    int i;
    if (!doc) return;
    arr = XBsonArray_create();
    if (arr) {
        for (i = 0; i < HSF_AUTO_DEVICE_COUNT; ++i) {
            char devId[16];
            XBsonValue* val;
            if (!s_cfg[modeIdx].m_devices[i]) continue;
            if (i < 6) {
                snprintf(devId, sizeof(devId), HSF_AUTO_DEVICE_VALVE_FMT, i + 1);
            } else {
                snprintf(devId, sizeof(devId), "%s",
                         i == AM_DEV_STIR ? HSF_AUTO_DEVICE_ID_STIR_PUMP
                                          : HSF_AUTO_DEVICE_ID_INLET_PUMP);
            }
            val = XBsonValue_create(XBSON_TYPE_STRING);
            if (!val) continue;
            XBsonValue_setString_utf8(val, devId);
            XBsonArray_append_move_base(arr, val);
            XBsonValue_delete(val);      /* move 后释放源壳。 */
        }
        XBsonDocument_insert_keyUtf8_array_move(doc, "devices", arr);
        XClassDelete((XClass*)arr);      /* move 后源数组壳由调用方释放。 */
    }
    /* [stirRpm 落盘 2026-11-12 实锅修复] 此前漏插 stirRpm——落盘 JSON 只有
     * devices/stirReverse/复位两键 ⇒ 重启 load 缺键回缺省 1440（720/960
     * 等任何自设转速全丢，「重启恢复」成了半个恢复）。内存态回显
     * （configDoc）不带此伤，§S1 的「SET/GET 往返」锁不到它——补键的
     * 同时在 §S1 加「落盘→重启→回读」断言封口。 */
    XBsonDocument_insert_keyUtf8_int32(doc, "stirRpm", s_cfg[modeIdx].m_stirRpm);
    XBsonDocument_insert_keyUtf8_bool(doc, "stirReverse", s_cfg[modeIdx].m_stirReverse);
    /* [逐模式阀复位 2026-11-12] 两个开关随模式一起落盘。 */
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_AUTO_MODE_RESET_VALVES_ON_STOP,
                                      s_cfg[modeIdx].m_resetValvesOnStop);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_AUTO_MODE_RESET_VALVES_ON_ESTOP,
                                      s_cfg[modeIdx].m_resetValvesOnEstop);
    /* [逐模式换相策略 2026-11-14] 两个泵的换相处置策略随模式一起落盘。 */
    XBsonDocument_insert_keyUtf8_utf8(doc, HSF_AUTO_MODE_STIR_SWITCH,
                                        s_cfg[modeIdx].m_stirSwitch);
    XBsonDocument_insert_keyUtf8_utf8(doc, HSF_AUTO_MODE_INLET_SWITCH,
                                        s_cfg[modeIdx].m_inletSwitch);
    {
        XString* json = XBsonDocument_toJson_string(doc, XJsonDocument_Compact);
        if (json) {
            snprintf(key, sizeof(key), "auto/%s", kModeIds[modeIdx]);
            HsfServerSettings_setString(key, XString_toUtf8(json));
            XClassDelete((XClass*)json);
        }
    }
    XClassDelete((XClass*)doc);
}

bool HsfServerAutoMode_applyModeDoc(const char* modeId,
                                    const struct XBsonDocument* modeDoc,
                                    char* outErr, int cap)
{
    const int idx = HsfServerAutoMode_modeIndex(modeId);
    const XBsonDocument* doc = (const XBsonDocument*)modeDoc;
    const XBsonValue* v;
    char why[128];
    bool newDevices[HSF_AUTO_DEVICE_COUNT];
    bool haveDevices = false;
    HsfServerAutoModeCfg* target;

    why[0] = '\0';
    if (idx < 0) {
        snprintf(why, sizeof(why), "未知自动模式：%s", modeId ? modeId : "");
    }
    if (!why[0] && doc) {
        target = &s_cfg[idx];
        /* [devices 整表替换 2026-11-13·对齐 Python] Python app.py 是
         * `target["devices"] = clean`（**整表覆盖**：传子集 ⇒ 只剩子集、
         * 传空数组 ⇒ 清空）。原实现从现值起步只置 true（合并语义）⇒
         * 远端设置取消勾选不生效、空数组也清不掉——两种意图都被静默
         * 忽略。现从零起步：收到 devices 数组即整体替换（未知 id 丢弃、
         * 「有内容且全未知」仍拒）。 */
        memset(newDevices, 0, sizeof(newDevices));
        v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "devices");
        if (v) {
            const XBsonArray* arr = XBsonValue_toArray((XBsonValue*)v);
            if (!arr) {
                snprintf(why, sizeof(why), "模式 %s 的 devices 必须是数组", modeId);
            } else {
                const size_t n = XBsonArray_size_base((XBsonArray*)arr);
                size_t k;
                int known = 0;
                for (k = 0; k < n; ++k) {
                    const XBsonValue* el = XBsonArray_at_base((XBsonArray*)arr, k);
                    const XString* s;
                    int di;
                    if (!el) continue;
                    if (XBsonValue_type((XBsonValue*)el) != XBSON_TYPE_STRING) continue;
                    s = XBsonValue_toString((XBsonValue*)el);
                    if (!s) continue;
                    di = HsfServerAutoMode_deviceIndex(XString_toUtf8(s));
                    if (di >= 0) {
                        newDevices[di] = true;
                        ++known;
                    }
                }
                /* 未知 id 丢弃（前向兼容），但"有内容且全未知"拒——客户端
                 * 键形错了要让人看见，不能静默存一份空的。 */
                if (n > 0 && known == 0) {
                    snprintf(why, sizeof(why),
                             "模式 %s 的设备清单里没有可识别的设备 id", modeId);
                } else {
                    haveDevices = true;
                }
            }
        }
        if (!why[0]) {
            v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "stirRpm");
            if (v) {
                /* [按类型取值] toInt32/toInt64 只对同型存储可靠，INT32/INT64/
                 * DOUBLE 各走各的口（混用会读错联合体字段）。字符串数字 Python
                 * int() 能转、C 拒——客户端两端都发数字，无实际分歧面。 */
                const XBsonType t = XBsonValue_type((XBsonValue*)v);
                int rpm;
                if (t == XBSON_TYPE_INT32) {
                    rpm = (int)XBsonValue_toInt32((XBsonValue*)v, 0);
                } else if (t == XBSON_TYPE_INT64) {
                    rpm = (int)XBsonValue_toInt64((XBsonValue*)v, 0);
                } else if (t == XBSON_TYPE_DOUBLE) {
                    rpm = (int)XBsonValue_toDouble((XBsonValue*)v, 0.0);
                } else {
                    rpm = -1;
                }
                if (rpm < 0) {
                    snprintf(why, sizeof(why), "模式 %s 的 stirRpm 需要整数", modeId);
                } else {
                    if (rpm < HSF_AUTO_STIR_RPM_MIN) rpm = HSF_AUTO_STIR_RPM_MIN;
                    if (rpm > HSF_AUTO_STIR_RPM_MAX) rpm = HSF_AUTO_STIR_RPM_MAX;
                    target->m_stirRpm = rpm;
                }
            }
        }
        if (!why[0]) {
            v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, "stirReverse");
            if (v) target->m_stirReverse = XBsonValue_toBool((XBsonValue*)v, false);
            /* [逐模式阀复位 2026-11-12] 逐键可选（缺键 = 保持现值）。 */
            v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc,
                                            HSF_AUTO_MODE_RESET_VALVES_ON_STOP);
            if (v)
                target->m_resetValvesOnStop =
                    XBsonValue_toBool((XBsonValue*)v,
                                      HSF_AUTO_MODE_RESET_VALVES_ON_STOP_DEF);
            v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc,
                                            HSF_AUTO_MODE_RESET_VALVES_ON_ESTOP);
            if (v)
                target->m_resetValvesOnEstop =
                    XBsonValue_toBool((XBsonValue*)v,
                                      HSF_AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF);
            /* [逐模式换相策略 2026-11-14] 两个泵在换相窗口内怎么处理。
             * **fail-closed**：非法值明确拒（策略是安全相关的行为选择，
             * 静默回落缺省会让人以为配了别的策略）。文案与 Python
             * app.py 逐字同源。 */
            {
                const char* keys[2];
                int i;
                keys[0] = HSF_AUTO_MODE_STIR_SWITCH;
                keys[1] = HSF_AUTO_MODE_INLET_SWITCH;
                for (i = 0; i < 2 && !why[0]; ++i) {
                    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc,
                                                    keys[i]);
                    if (!v) continue;
                    if (XBsonValue_type((XBsonValue*)v) != XBSON_TYPE_STRING) {
                        snprintf(why, sizeof(why),
                                 "模式 %s 的 %s 只能是 stopRestart/carry/direct",
                                 modeId, keys[i]);
                        break;
                    }
                    {
                        const XString* sv = XBsonValue_toString((XBsonValue*)v);
                        const char* val = sv ? XString_toUtf8(sv) : "";
                        if (!amSwitchPolicyValid(val)) {
                            snprintf(why, sizeof(why),
                                     "模式 %s 的 %s 只能是 stopRestart/carry/direct",
                                     modeId, keys[i]);
                            break;
                        }
                        if (i == 0) {
                            snprintf(target->m_stirSwitch,
                                     sizeof(target->m_stirSwitch), "%s", val);
                        } else {
                            snprintf(target->m_inletSwitch,
                                     sizeof(target->m_inletSwitch), "%s", val);
                        }
                    }
                }
            }
        }
        if (!why[0] && haveDevices) {
            memcpy(target->m_devices, newDevices, sizeof(newDevices));
        }
        if (!why[0]) amSaveModeCfg(idx);   /* 立即生效 + 落盘。 */
    }
    if (why[0] && outErr && cap > 0) snprintf(outErr, (size_t)cap, "%s", why);
    return !why[0];
}

struct XBsonDocument* HsfServerAutoMode_configDoc(void)
{
    XBsonDocument* root = XBsonDocument_create();
    XBsonDocument* modes;
    int m;
    if (!root) return NULL;
    modes = XBsonDocument_create();
    if (!modes) {
        XClassDelete((XClass*)root);
        return NULL;
    }
    for (m = 0; m < HSF_AUTO_MODE_COUNT; ++m) {
        XBsonDocument* one = XBsonDocument_create();
        XBsonArray* arr;
        int i;
        if (!one) continue;
        arr = XBsonArray_create();
        if (arr) {
            for (i = 0; i < HSF_AUTO_DEVICE_COUNT; ++i) {
                char devId[16];
                XBsonValue* val;
                if (!s_cfg[m].m_devices[i]) continue;
                if (i < 6) {
                    snprintf(devId, sizeof(devId), HSF_AUTO_DEVICE_VALVE_FMT, i + 1);
                } else {
                    snprintf(devId, sizeof(devId), "%s",
                             i == AM_DEV_STIR ? HSF_AUTO_DEVICE_ID_STIR_PUMP
                                              : HSF_AUTO_DEVICE_ID_INLET_PUMP);
                }
                val = XBsonValue_create(XBSON_TYPE_STRING);
                if (!val) continue;
                XBsonValue_setString_utf8(val, devId);
                XBsonArray_append_move_base(arr, val);
                XBsonValue_delete(val);  /* move 后释放源壳。 */
            }
            XBsonDocument_insert_keyUtf8_array_move(one, "devices", arr);
            XClassDelete((XClass*)arr);  /* move 后源数组壳由调用方释放。 */
        }
        XBsonDocument_insert_keyUtf8_int32(one, "stirRpm",
                                           (int32_t)s_cfg[m].m_stirRpm);
        XBsonDocument_insert_keyUtf8_bool(one, "stirReverse", s_cfg[m].m_stirReverse);
        XBsonDocument_insert_keyUtf8_bool(one, HSF_AUTO_MODE_RESET_VALVES_ON_STOP,
                                           s_cfg[m].m_resetValvesOnStop);
        /* [逐模式换相策略 2026-11-14] 两个泵在换相窗口内的处置策略。 */
        XBsonDocument_insert_keyUtf8_utf8(one, HSF_AUTO_MODE_STIR_SWITCH,
                                            s_cfg[m].m_stirSwitch);
        XBsonDocument_insert_keyUtf8_utf8(one, HSF_AUTO_MODE_INLET_SWITCH,
                                            s_cfg[m].m_inletSwitch);
        XBsonDocument_insert_keyUtf8_bool(one, HSF_AUTO_MODE_RESET_VALVES_ON_ESTOP,
                                           s_cfg[m].m_resetValvesOnEstop);
        /* insert_*_document 是深拷贝（COW addRef），one 的初始引用仍归本函数。 */
        XBsonDocument_insert_keyUtf8_document(modes, kModeIds[m], one);
        XClassDelete((XClass*)one);
    }
    XBsonDocument_insert_keyUtf8_document(root, "modes", modes);
    XClassDelete((XClass*)modes);
    /* [自动循环子节 2026-11-12] 与 modes 平级（键形 = Python
     * _config_auto_doc 的 cycle）。 */
    {
        XBsonDocument* cycle = XBsonDocument_create();
        if (cycle) {
            XBsonDocument_insert_keyUtf8_bool(cycle,
                HSF_CFGK_AUTO_CYCLE_ENABLED, s_cycleCfg.m_enabled);
            XBsonDocument_insert_keyUtf8_double(cycle,
                HSF_CFGK_AUTO_CYCLE_PRESS_THRESHOLD, s_cycleCfg.m_pressThreshold);
            XBsonDocument_insert_keyUtf8_utf8(cycle,
                HSF_CFGK_AUTO_CYCLE_PRESS_COMPARE, s_cycleCfg.m_pressCompare);
            XBsonDocument_insert_keyUtf8_bool(cycle,
                HSF_CFGK_AUTO_CYCLE_TIMER_FALLBACK, s_cycleCfg.m_timerFallback);
            XBsonDocument_insert_keyUtf8_int32(cycle,
                HSF_CFGK_AUTO_CYCLE_TIMER_MINUTES, s_cycleCfg.m_timerMinutes);
            XBsonDocument_insert_keyUtf8_int32(cycle,
                HSF_CFGK_AUTO_CYCLE_BACKWASH_MINUTES, s_cycleCfg.m_backwashMinutes);
            XBsonDocument_insert_keyUtf8_int32(cycle,
                HSF_CFGK_AUTO_CYCLE_RINSE_MINUTES, s_cycleCfg.m_rinseMinutes);
            XBsonDocument_insert_keyUtf8_document(root, HSF_CFGK_AUTO_CYCLE,
                                                  cycle);
            XClassDelete((XClass*)cycle);
        }
    }
    /* [按键接线子节 2026-11-13] C 端扩展（Python 不经 0x0117 收发该子节，
     * 它走自己的 cfg 文件；客户端应答缺键不误改的纪律不受影响）。 */
    {
        XBsonDocument* buttons = XBsonDocument_create();
        if (buttons) {
            XBsonArray* nc = XBsonArray_create();
            if (nc) {
                static const char* const kIds[] = {
                    "start_di", "stop_di", "reset_di"
                };
                const bool flags[3] = { s_btnNcStart, s_btnNcStop, s_btnNcReset };
                int i;
                for (i = 0; i < 3; ++i) {
                    XBsonValue* val;
                    if (!flags[i]) continue;
                    val = XBsonValue_create(XBSON_TYPE_STRING);
                    if (!val) continue;
                    XBsonValue_setString_utf8(val, kIds[i]);
                    XBsonArray_append_move_base(nc, val);
                    XBsonValue_delete(val);   /* move 后释放源壳。 */
                }
                XBsonDocument_insert_keyUtf8_array_move(
                    buttons, HSF_CFGK_AUTO_BUTTONS_NC, nc);
                XClassDelete((XClass*)nc);   /* move 后源数组壳由调用方释放。 */
            }
            XBsonDocument_insert_keyUtf8_int32(
                buttons, HSF_CFGK_AUTO_BUTTONS_DEBOUNCE,
                (int32_t)s_btnDebounceMs);
            XBsonDocument_insert_keyUtf8_document(
                root, HSF_CFGK_AUTO_BUTTONS, buttons);
            XClassDelete((XClass*)buttons);
        }
    }
    return root;
}

/** @brief 从 JSON 串恢复一份模式配置（坏串/缺键保持现值，宽松口径）。 */
static void amLoadModeCfg(int modeIdx, const char* json)
{
    XByteArray* bytes;
    XJsonDocument* jsonDoc;
    XJsonObject* obj;
    XBsonDocument* doc;
    const XBsonValue* v;
    const XBsonArray* arr;
    size_t n;
    size_t k;
    HsfServerAutoModeCfg* target = &s_cfg[modeIdx];

    if (!json || !json[0]) return;
    bytes = XByteArray_create();
    if (!bytes) return;
    if (!XByteArray_push_back_2(bytes, json, strlen(json))) {
        XClassDelete((XClass*)bytes);
        return;
    }
    jsonDoc = XJsonDocument_fromJson(bytes);
    XClassDelete((XClass*)bytes);
    if (!jsonDoc) {
        char msg[96];
        snprintf(msg, sizeof(msg), "自动模式配置恢复失败：模式[%s] 的 INI 值不是"
                 "合法 JSON（保持缺省）", kModeIds[modeIdx]);
        amLog("auto", msg);
        return;
    }
    obj = XJsonDocument_object(jsonDoc);
    doc = obj ? XBsonDocument_fromJsonObject(obj) : NULL;
    XClassDelete((XClass*)jsonDoc);
    if (!doc) {
        char msg[96];
        snprintf(msg, sizeof(msg), "自动模式配置恢复失败：模式[%s] 的 INI 值"
                 "不是对象（保持缺省）", kModeIds[modeIdx]);
        amLog("auto", msg);
        return;
    }

    v = XBsonDocument_value_keyUtf8(doc, "devices");
    arr = v ? XBsonValue_toArray((XBsonValue*)v) : NULL;
    if (arr) {
        bool devices[HSF_AUTO_DEVICE_COUNT];
        memset(devices, 0, sizeof(devices));
        n = XBsonArray_size_base((XBsonArray*)arr);
        for (k = 0; k < n; ++k) {
            const XBsonValue* el = XBsonArray_at_base((XBsonArray*)arr, k);
            const XString* s;
            int di;
            if (!el) continue;
            if (XBsonValue_type((XBsonValue*)el) != XBSON_TYPE_STRING) continue;
            s = XBsonValue_toString((XBsonValue*)el);
            if (!s) continue;
            di = HsfServerAutoMode_deviceIndex(XString_toUtf8(s));
            if (di >= 0) devices[di] = true;
        }
        memcpy(target->m_devices, devices, sizeof(devices));
    }
    v = XBsonDocument_value_keyUtf8(doc, "stirRpm");
    if (v) {
        /* 按类型取值（同 applyModeDoc 的口径；宽松：脏值回缺省夹取）。 */
        const XBsonType t = XBsonValue_type((XBsonValue*)v);
        int rpm = HSF_AUTO_STIR_RPM_DEF;
        if (t == XBSON_TYPE_INT32) {
            rpm = (int)XBsonValue_toInt32((XBsonValue*)v,
                                          (int32_t)HSF_AUTO_STIR_RPM_DEF);
        } else if (t == XBSON_TYPE_INT64) {
            rpm = (int)XBsonValue_toInt64((XBsonValue*)v,
                                          (int64_t)HSF_AUTO_STIR_RPM_DEF);
        } else if (t == XBSON_TYPE_DOUBLE) {
            rpm = (int)XBsonValue_toDouble((XBsonValue*)v,
                                           (double)HSF_AUTO_STIR_RPM_DEF);
        }
        if (rpm < HSF_AUTO_STIR_RPM_MIN) rpm = HSF_AUTO_STIR_RPM_MIN;
        if (rpm > HSF_AUTO_STIR_RPM_MAX) rpm = HSF_AUTO_STIR_RPM_MAX;
        target->m_stirRpm = rpm;
    }
    v = XBsonDocument_value_keyUtf8(doc, "stirReverse");
    if (v) target->m_stirReverse = XBsonValue_toBool((XBsonValue*)v, false);
    /* [逐模式阀复位 2026-11-12] 缺键回缺省（旧配置文件兼容）。 */
    v = XBsonDocument_value_keyUtf8(doc, HSF_AUTO_MODE_RESET_VALVES_ON_STOP);
    target->m_resetValvesOnStop = v
        ? XBsonValue_toBool((XBsonValue*)v, HSF_AUTO_MODE_RESET_VALVES_ON_STOP_DEF)
        : (bool)HSF_AUTO_MODE_RESET_VALVES_ON_STOP_DEF;
    v = XBsonDocument_value_keyUtf8(doc, HSF_AUTO_MODE_RESET_VALVES_ON_ESTOP);
    target->m_resetValvesOnEstop = v
        ? XBsonValue_toBool((XBsonValue*)v, HSF_AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF)
        : (bool)HSF_AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF;
    /* [逐模式换相策略 2026-11-14] 缺键/非法值回缺省（旧配置文件兼容）。 */
    {
        const char* keys[2];
        int i;
        keys[0] = HSF_AUTO_MODE_STIR_SWITCH;
        keys[1] = HSF_AUTO_MODE_INLET_SWITCH;
        for (i = 0; i < 2; ++i) {
            char* dst = (i == 0) ? target->m_stirSwitch
                                 : target->m_inletSwitch;
            const char* val = HSF_AUTO_SWITCH_POLICY_DEF;
            v = XBsonDocument_value_keyUtf8(doc, keys[i]);
            if (v && XBsonValue_type((XBsonValue*)v) == XBSON_TYPE_STRING) {
                const XString* sv = XBsonValue_toString((XBsonValue*)v);
                const char* raw = sv ? XString_toUtf8(sv) : "";
                if (amSwitchPolicyValid(raw)) val = raw;
            }
            snprintf(dst, 16, "%s", val);
        }
    }
    XClassDelete((XClass*)doc);
}

/** @brief 把循环配置写成 JSON 串并落 INI（auto/cycle）。 */
static void amSaveCycleCfg(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    if (!doc) return;
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_CFGK_AUTO_CYCLE_ENABLED,
                                      s_cycleCfg.m_enabled);
    XBsonDocument_insert_keyUtf8_double(doc, HSF_CFGK_AUTO_CYCLE_PRESS_THRESHOLD,
                                        s_cycleCfg.m_pressThreshold);
    XBsonDocument_insert_keyUtf8_utf8(doc, HSF_CFGK_AUTO_CYCLE_PRESS_COMPARE,
                                      s_cycleCfg.m_pressCompare);
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_CFGK_AUTO_CYCLE_TIMER_FALLBACK,
                                      s_cycleCfg.m_timerFallback);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_AUTO_CYCLE_TIMER_MINUTES,
                                       s_cycleCfg.m_timerMinutes);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_AUTO_CYCLE_BACKWASH_MINUTES,
                                       s_cycleCfg.m_backwashMinutes);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_AUTO_CYCLE_RINSE_MINUTES,
                                       s_cycleCfg.m_rinseMinutes);
    {
        XString* json = XBsonDocument_toJson_string(doc, XJsonDocument_Compact);
        if (json) {
            HsfServerSettings_setString("auto/cycle", XString_toUtf8(json));
            XClassDelete((XClass*)json);
        }
    }
    XClassDelete((XClass*)doc);
}

bool HsfServerAutoMode_applyCycleDoc(const struct XBsonDocument* cycleDoc,
                                     char* outErr, int cap)
{
    const XBsonDocument* doc = (const XBsonDocument*)cycleDoc;
    const XBsonValue* v;
    char why[128];
    HsfServerAutoModeCycleCfg next = s_cycleCfg;

    why[0] = '\0';
    if (!doc) {
        snprintf(why, sizeof(why), "cycle 必须是对象");
    }
    if (!why[0]) {
        v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc,
                                        HSF_CFGK_AUTO_CYCLE_ENABLED);
        /* [严格布尔 2026-11-16 审计·对齐 Python strict_bool] 原来
         * toBool(v, 缺省) 对字符串静默回缺省 ⇒ 客户端发 "true"（字符串）
         * 被当"没改"，发 "false" 想关却可能保持开 ⇒ 同一坑的两面。
         * 布尔开关 fail-closed：不是真布尔就明确拒。 */
        if (v) {
            if (!XBsonValue_isBool((XBsonValue*)v)) {
                snprintf(why, sizeof(why),
                         "%s 需要布尔值（true/false）",
                         HSF_CFGK_AUTO_CYCLE_ENABLED);
            } else {
                next.m_enabled = XBsonValue_toBool((XBsonValue*)v,
                                                   next.m_enabled);
            }
        }
    }
    if (!why[0]) {
        v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc,
                                        HSF_CFGK_AUTO_CYCLE_PRESS_THRESHOLD);
        if (v) {
            const XBsonType t = XBsonValue_type((XBsonValue*)v);
            double val = 0.0;
            bool numeric = true;
            if (t == XBSON_TYPE_DOUBLE) {
                val = XBsonValue_toDouble((XBsonValue*)v, 0.0);
            } else if (t == XBSON_TYPE_INT32) {
                val = (double)XBsonValue_toInt32((XBsonValue*)v, 0);
            } else if (t == XBSON_TYPE_INT64) {
                val = (double)XBsonValue_toInt64((XBsonValue*)v, 0);
            } else {
                numeric = false;
            }
            if (!numeric) {
                snprintf(why, sizeof(why), "pressThreshold 需要数字（MPa）");
            } else {
                /* [夹取口径与 Python 同] float() 能转化即收：负数夹到 0、
                 * 大于 10 夹到 10（Python max(0.0, min(MAX, val))）。
                 * 只有「不是数字」才拒。 */
                if (val < 0.0) val = 0.0;
                if (val > HSF_AUTO_CYCLE_PRESS_THRESHOLD_MAX)
                    val = HSF_AUTO_CYCLE_PRESS_THRESHOLD_MAX;
                next.m_pressThreshold = val;
            }
        }
    }
    if (!why[0]) {
        v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc,
                                        HSF_CFGK_AUTO_CYCLE_PRESS_COMPARE);
        if (v) {
            const XString* s = XBsonValue_toString((XBsonValue*)v);
            const char* cmp = s ? XString_toUtf8(s) : "";
            /* [大小写/空白宽容 2026-11-12·对齐 Python .strip().lower()]：
             * “ GT ” 与 “gt” 是同一个语义，不该因为多一个空格就拒。 */
            char norm[8];
            size_t n = 0;
            const char* p = cmp;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
            while (*p && n + 1 < sizeof(norm)) {
                char ch = *p++;
                if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
                norm[n++] = ch;
            }
            while (n > 0 && (norm[n - 1] == ' ' || norm[n - 1] == '\t' ||
                             norm[n - 1] == '\r' || norm[n - 1] == '\n'))
                --n;
            norm[n] = '\0';
            if (strcmp(norm, HSF_AUTO_PRESS_COMPARE_GT) != 0 &&
                strcmp(norm, HSF_AUTO_PRESS_COMPARE_LT) != 0) {
                snprintf(why, sizeof(why), "pressCompare 只能是 gt/lt");
            } else {
                snprintf(next.m_pressCompare, sizeof(next.m_pressCompare), "%s",
                         norm);
            }
        }
    }
    if (!why[0]) {
        v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc,
                                        HSF_CFGK_AUTO_CYCLE_TIMER_FALLBACK);
        /* [严格布尔 2026-11-16 审计·对齐 Python] 同 enabled。 */
        if (v) {
            if (!XBsonValue_isBool((XBsonValue*)v)) {
                snprintf(why, sizeof(why),
                         "%s 需要布尔值（true/false）",
                         HSF_CFGK_AUTO_CYCLE_TIMER_FALLBACK);
            } else {
                next.m_timerFallback = XBsonValue_toBool((XBsonValue*)v,
                                                         false);
            }
        }
    }
    if (!why[0]) {
        static const char* const kMinKeys[] = {
            HSF_CFGK_AUTO_CYCLE_TIMER_MINUTES,
            HSF_CFGK_AUTO_CYCLE_BACKWASH_MINUTES,
            HSF_CFGK_AUTO_CYCLE_RINSE_MINUTES
        };
        int* const kTargets[] = {
            &next.m_timerMinutes, &next.m_backwashMinutes, &next.m_rinseMinutes
        };
        int k;
        for (k = 0; k < 3 && !why[0]; ++k) {
            v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, kMinKeys[k]);
            if (!v) continue;
            {
                const XBsonType t = XBsonValue_type((XBsonValue*)v);
                int val;
                if (t == XBSON_TYPE_INT32) {
                    val = (int)XBsonValue_toInt32((XBsonValue*)v, -1);
                } else if (t == XBSON_TYPE_INT64) {
                    val = (int)XBsonValue_toInt64((XBsonValue*)v, -1);
                } else if (t == XBSON_TYPE_DOUBLE) {
                    val = (int)XBsonValue_toDouble((XBsonValue*)v, -1.0);
                } else {
                    val = -1;
                }
                if (val < 0) {
                    snprintf(why, sizeof(why), "%s 需要整数分钟", kMinKeys[k]);
                } else {
                    if (val < HSF_AUTO_CYCLE_MINUTES_MIN)
                        val = HSF_AUTO_CYCLE_MINUTES_MIN;
                    if (val > HSF_AUTO_CYCLE_MINUTES_MAX)
                        val = HSF_AUTO_CYCLE_MINUTES_MAX;
                    *kTargets[k] = val;
                }
            }
        }
    }
    if (!why[0]) {
        s_cycleCfg = next;
        amSaveCycleCfg();         /* 立即生效 + 落盘。 */
    }
    if (why[0] && outErr && cap > 0) snprintf(outErr, (size_t)cap, "%s", why);
    return !why[0];
}

const HsfServerAutoModeCycleCfg* HsfServerAutoMode_cycleConfig(void)
{
    return &s_cycleCfg;
}

/** @brief 把按键接线配置写成 JSON 串并落 INI（auto/buttons）。 */
static void amSaveButtonsCfg(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonArray* nc;
    if (!doc) return;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_AUTO_BUTTONS_DEBOUNCE,
                                       (int32_t)s_btnDebounceMs);
    nc = XBsonArray_create();
    if (nc) {
        static const char* const kIds[] = {
            "start_di", "stop_di", "reset_di"
        };
        const bool flags[3] = { s_btnNcStart, s_btnNcStop, s_btnNcReset };
        int i;
        for (i = 0; i < 3; ++i) {
            XBsonValue* val;
            if (!flags[i]) continue;
            val = XBsonValue_create(XBSON_TYPE_STRING);
            if (!val) continue;
            XBsonValue_setString_utf8(val, kIds[i]);
            XBsonArray_append_move_base(nc, val);
            XBsonValue_delete(val);      /* move 后释放源壳。 */
        }
        XBsonDocument_insert_keyUtf8_array_move(
            doc, HSF_CFGK_AUTO_BUTTONS_NC, nc);
        XClassDelete((XClass*)nc);       /* move 后源数组壳由调用方释放。 */
    }
    {
        XString* json = XBsonDocument_toJson_string(doc, XJsonDocument_Compact);
        if (json) {
            HsfServerSettings_setString("auto/buttons", XString_toUtf8(json));
            XClassDelete((XClass*)json);
        }
    }
    XClassDelete((XClass*)doc);
}

bool HsfServerAutoMode_applyButtonsDoc(const struct XBsonDocument* buttonsDoc,
                                       char* outErr, int cap)
{
    const XBsonDocument* doc = (const XBsonDocument*)buttonsDoc;
    const XBsonValue* v;
    char why[128];
    int debounce = s_btnDebounceMs;
    bool ncStart = s_btnNcStart;
    bool ncStop = s_btnNcStop;
    bool ncReset = s_btnNcReset;

    why[0] = '\0';
    if (!doc) {
        snprintf(why, sizeof(why), "buttons 必须是对象");
    }
    if (!why[0]) {
        v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc,
                                        HSF_CFGK_AUTO_BUTTONS_DEBOUNCE);
        if (v) {
            const XBsonType t = XBsonValue_type((XBsonValue*)v);
            int val = -1;
            if (t == XBSON_TYPE_INT32) {
                val = (int)XBsonValue_toInt32((XBsonValue*)v, -1);
            } else if (t == XBSON_TYPE_INT64) {
                val = (int)XBsonValue_toInt64((XBsonValue*)v, -1);
            } else if (t == XBSON_TYPE_DOUBLE) {
                const double d = XBsonValue_toDouble((XBsonValue*)v, -1.0);
                val = (d >= 0.0) ? (int)d : -1;
            }
            /* [fail-closed] 非法值明确拒（键形错了要让人看见）。 */
            if (val < 0) {
                snprintf(why, sizeof(why), "debounceMs 需要非负整数");
            } else {
                debounce = val;
            }
        }
    }
    if (!why[0]) {
        v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc,
                                        HSF_CFGK_AUTO_BUTTONS_NC);
        if (v) {
            const XBsonArray* arr = XBsonValue_toArray((XBsonValue*)v);
            if (!arr) {
                snprintf(why, sizeof(why), "normallyClosed 必须是数组");
            } else {
                /* 键名与 Python buttons.py KEY_* 同字面；未知 id 丢弃
                 * （前向兼容）。空数组 = 三键全常开（显式选择，合法）。 */
                static const char* const kIds[] = {
                    "start_di", "stop_di", "reset_di"
                };
                bool flags[3] = { false, false, false };
                const size_t n = XBsonArray_size_base((XBsonArray*)arr);
                size_t k;
                for (k = 0; k < n; ++k) {
                    const XBsonValue* el = XBsonArray_at_base((XBsonArray*)arr, k);
                    const XString* s;
                    int i;
                    if (!el) continue;
                    if (XBsonValue_type((XBsonValue*)el) != XBSON_TYPE_STRING)
                        continue;
                    s = XBsonValue_toString((XBsonValue*)el);
                    if (!s) continue;
                    for (i = 0; i < 3; ++i) {
                        if (strcmp(kIds[i], XString_toUtf8(s)) == 0)
                            flags[i] = true;
                    }
                }
                ncStart = flags[0];
                ncStop = flags[1];
                ncReset = flags[2];
            }
        }
    }
    if (!why[0]) {
        s_btnDebounceMs = debounce;
        s_btnNcStart = ncStart;
        s_btnNcStop = ncStop;
        s_btnNcReset = ncReset;
        amSaveButtonsCfg();              /* 立即生效 + 落盘。 */
    }
    if (why[0] && outErr && cap > 0) snprintf(outErr, (size_t)cap, "%s", why);
    return !why[0];
}

/** @brief 从 JSON 串恢复循环配置（坏串/缺键保持缺省，宽松口径）。 */
static void amLoadCycleCfg(const char* json)
{
    XByteArray* bytes;
    XJsonDocument* jsonDoc;
    XJsonObject* obj;
    XBsonDocument* doc;
    char why[64];
    if (!json || json[0] != '{') return;   /* 缺键/非对象值：无需恢复。 */
    bytes = XByteArray_create();
    if (!bytes) return;
    if (!XByteArray_push_back_2(bytes, json, strlen(json))) {
        XClassDelete((XClass*)bytes);
        return;
    }
    jsonDoc = XJsonDocument_fromJson(bytes);
    XClassDelete((XClass*)bytes);
    obj = jsonDoc ? XJsonDocument_object(jsonDoc) : NULL;
    doc = obj ? XBsonDocument_fromJsonObject(obj) : NULL;
    XClassDelete((XClass*)jsonDoc);
    if (!doc) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "自动循环配置恢复失败：auto/cycle 不是 JSON 对象（以「%.16s」开头，"
                 "保持缺省）", json);
        amLog("auto", msg);
        return;
    }
    /* 宽松恢复：非法值回缺省而不是拒（启动不能因为一个脏值起不来）。 */
    if (!HsfServerAutoMode_applyCycleDoc(doc, why, (int)sizeof(why))) {
        char msg[160];
        snprintf(msg, sizeof(msg), "自动循环配置恢复失败：%s（保持缺省）", why);
        amLog("auto", msg);
    }
    XClassDelete((XClass*)doc);
}

/** @brief 从 JSON 串恢复按键接线配置（坏串/缺键保持缺省，宽松口径）。 */
static void amLoadButtonsCfg(const char* json)
{
    XByteArray* bytes;
    XJsonDocument* jsonDoc;
    XJsonObject* obj;
    XBsonDocument* doc;
    char why[64];
    if (!json || json[0] != '{') return;   /* 缺键/非对象值：无需恢复。 */
    bytes = XByteArray_create();
    if (!bytes) return;
    if (!XByteArray_push_back_2(bytes, json, strlen(json))) {
        XClassDelete((XClass*)bytes);
        return;
    }
    jsonDoc = XJsonDocument_fromJson(bytes);
    XClassDelete((XClass*)bytes);
    obj = jsonDoc ? XJsonDocument_object(jsonDoc) : NULL;
    doc = obj ? XBsonDocument_fromJsonObject(obj) : NULL;
    XClassDelete((XClass*)jsonDoc);
    if (!doc) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "按键接线配置恢复失败：auto/buttons 不是 JSON 对象（保持缺省）");
        amLog("auto", msg);
        return;
    }
    /* 宽松恢复：非法值回缺省而不是拒（启动不能因为一个脏值起不来）。 */
    if (!HsfServerAutoMode_applyButtonsDoc(doc, why, (int)sizeof(why))) {
        char msg[160];
        snprintf(msg, sizeof(msg), "按键接线配置恢复失败：%s（保持缺省）", why);
        amLog("auto", msg);
    }
    XClassDelete((XClass*)doc);
}

void HsfServerAutoMode_loadConfig(void)
{
    int m;
    char key[32];
    char json[AM_CFG_JSON_MAX];
    for (m = 0; m < HSF_AUTO_MODE_COUNT; ++m) {
        snprintf(key, sizeof(key), "auto/%s", kModeIds[m]);
        /* [键与输出不能共用缓冲] getString 先读键再写 buf，同一缓冲是
         * 未定义顺序——键名进独立小缓冲。 */
        json[0] = '\0';
        HsfServerSettings_getString(key, "", json, (int)sizeof(json));
        amLoadModeCfg(m, json);
    }
    /* [自动循环配置 2026-11-12] auto/cycle 独立一键（与三模式平级）。
     * [缓冲必须先清] getString 在键不存在且缺省为空串时**不写 buf** ⇒
     * 复用同一个 json 缓冲会把上一个模式的 JSON 当循环配置解析（启动日志
     * 报“auto/cycle 不是合法 JSON”的实锅）。 */
    json[0] = '\0';
    HsfServerSettings_getString("auto/cycle", "", json, (int)sizeof(json));
    amLoadCycleCfg(json);
    /* [按键接线配置 2026-11-13] auto/buttons 独立一键（与 cycle 同套路）。
     * 缓冲同样先清（同上实锅口径）。 */
    json[0] = '\0';
    HsfServerSettings_getString("auto/buttons", "", json, (int)sizeof(json));
    amLoadButtonsCfg(json);
}

/* ==================== 装配 / 拆卸 / 查询 ==================== */

void HsfServerAutoMode_setLog(HsfServerAutoModeLogFn fn)
{
    s_log = fn;
}

bool HsfServerAutoMode_ready(void)
{
    return s_ready;
}

struct XAutoController* HsfServerAutoMode_controller(void)
{
    return s_ready ? &s_ctl : NULL;
}

bool HsfServerAutoMode_running(void)
{
    return s_ready && XAutoController_running(&s_ctl);
}

void HsfServerAutoMode_modeId(char* buf, int cap)
{
    if (buf && cap > 0) snprintf(buf, (size_t)cap, "%s", s_ready ? s_mode : "");
}

void HsfServerAutoMode_reportedMode(char* buf, int cap)
{
    if (!buf || cap <= 0) return;
    if (s_ready && s_cycleRoot && s_cycleStage[0]) {
        snprintf(buf, (size_t)cap, "%s", s_cycleStage);
    } else if (s_ready && strcmp(s_mode, HSF_AUTO_MODE_ID_AUTO) == 0 &&
               s_cycleResumeStage[0]) {
        /* 停止/急停后未复位：如实报「下次将从哪一步继续」（Python 同口径）。 */
        snprintf(buf, (size_t)cap, "%s", s_cycleResumeStage);
    } else if (s_ready && XAutoController_running(&s_ctl) && s_runMode[0]) {
        /* [粘性分账 2026-11-20] 运行中报**本轮运行的模式**，不是粘性
         * ——用户运行中改了下拉（op=5），界面仍要显示正在跑的那个模式
         * （Python 运行中报 ctl.mode 同款）。 */
        snprintf(buf, (size_t)cap, "%s", s_runMode);
    } else {
        snprintf(buf, (size_t)cap, "%s", s_ready ? s_mode : "");
    }
}

void HsfServerAutoMode_phase(char* buf, int cap)
{
    if (buf && cap > 0) {
        snprintf(buf, (size_t)cap, "%s",
                 s_ready ? XAutoController_phase(&s_ctl) : kPhaseIdle);
    }
}

bool HsfServerAutoMode_faultLatched(void)
{
    return s_ready && s_faultLatched;
}

bool HsfServerAutoMode_estopLatched(void)
{
    return s_ready && XAutoController_estopped(&s_ctl);
}

uint64_t HsfServerAutoMode_runMs(void)
{
    uint64_t total;

    if (!s_ready) return 0;
    total = s_runAccumMs;
    /* [正在跑的这一段算到当前] 停止/急停/中止后这一段已结算进累计，起算
     * 时刻清零 ⇒ 这里只补"还没结算"的那一段（Python run_ms 同款）。 */
    if (XAutoController_running(&s_ctl) && s_runStartMs) {
        const uint64_t now = (uint64_t)XDateTime_currentMSecsSinceEpoch();
        if (now > s_runStartMs) {
            total += (now - s_runStartMs);
        }
    }
    return total;
}

void HsfServerAutoMode_setPressureFn(HsfServerAutoModePressFn fn)
{
    s_pressFn = fn;
}

void HsfServerAutoMode_stageTiming(int64_t* outElapsed, int64_t* outTotal)
{
    int64_t elapsed = -1;
    int64_t total = 0;

    if (s_ready && XAutoController_running(&s_ctl) && s_cycleRoot &&
        s_cycleStage[0]) {
        const uint64_t now = (uint64_t)XDateTime_currentMSecsSinceEpoch();
        const uint64_t start = s_cycleStageAt ? s_cycleStageAt : now;
        elapsed = (now > start) ? (int64_t)(now - start) : 0;
        if (strcmp(s_cycleStage, HSF_AUTO_MODE_ID_BACKWASH) == 0) {
            total = (int64_t)s_runCycle.m_backwashMinutes * 60000;
        } else if (strcmp(s_cycleStage, HSF_AUTO_MODE_ID_RINSE) == 0) {
            total = (int64_t)s_runCycle.m_rinseMinutes * 60000;
        } else if (strcmp(s_cycleStage, HSF_AUTO_MODE_ID_PRODUCE) == 0) {
            /* 产水：只有定时兜底开启时才有总时长（否则按压差切换）。 */
            total = s_runCycle.m_timerFallback
                        ? (int64_t)s_runCycle.m_timerMinutes * 60000 : 0;
        }
    }
    if (outElapsed) *outElapsed = elapsed;
    if (outTotal) *outTotal = total;
}

bool HsfServerAutoMode_setup(void)
{
    int m;
    if (s_ready) return true;
    /* [控制器只 init 一次] XAutoController_init 是 memset 全清——对已 init
     * 的对象再来一次会把报警队列缓冲的账本抹掉（缓冲本体泄漏）。同进程
     * 内 stop→start 重启只撤回调、不重新 init（与旧装配面同口径）。 */
    if (!s_ctlInit) {
        XAutoController_init(&s_ctl);
        s_ctlInit = true;
    }
    s_faultLatched = false;
    s_mode[0] = '\0';
    /* [粘性分账 2026-11-20] 运行中模式也清空：重启后没有"正在跑的工序"
     * ⇒ 恢复出来的只是**粘性**（下次启动用）。 */
    s_runMode[0] = '\0';
    /* [恢复粘性模式 2026-11-13·对齐 Python load_run_mode] 落盘的「上次
     * 选中模式」在重启后恢复 ⇒ 现场物理启动按钮开箱即用（不清空时按键
     * 会被拒「未选择自动模式」）。恢复出来的 id 仍要过合法性校验——
     * 配置文件被手改成野值时按未选处理（fail-closed）。 */
    {
        char buf[AM_MODE_ID_MAX];
        buf[0] = '\0';
        HsfServerSettings_getString(AM_KEY_STICKY_MODE, "", buf,
                                    (int)sizeof(buf));
        if (HsfServerAutoMode_isKnownMode(buf))
            snprintf(s_mode, sizeof(s_mode), "%s", buf);
    }
    for (m = 0; m < HSF_AUTO_MODE_COUNT; ++m) {
        memset(&s_cfg[m], 0, sizeof(s_cfg[m]));
        s_cfg[m].m_stirRpm = HSF_AUTO_STIR_RPM_DEF;
        s_cfg[m].m_stirReverse = false;
        /* [阀复位缺省 2026-11-12] 停止=关回（既有口径）、急停=保持现状。 */
        s_cfg[m].m_resetValvesOnStop =
            (bool)HSF_AUTO_MODE_RESET_VALVES_ON_STOP_DEF;
        s_cfg[m].m_resetValvesOnEstop =
            (bool)HSF_AUTO_MODE_RESET_VALVES_ON_ESTOP_DEF;
        /* [换相策略缺省 2026-11-14] 两个泵都"先停再启"（保持既有逻辑）。 */
        snprintf(s_cfg[m].m_stirSwitch, sizeof(s_cfg[m].m_stirSwitch), "%s",
                 HSF_AUTO_SWITCH_POLICY_DEF);
        snprintf(s_cfg[m].m_inletSwitch, sizeof(s_cfg[m].m_inletSwitch), "%s",
                 HSF_AUTO_SWITCH_POLICY_DEF);
    }
    /* [循环配置回缺省 2026-11-12] setup 可重复调用（测试重启/换 INI）——
     * s_cycleCfg 是静态变量，残留上次运行期的值会被 loadConfig 的"缺键=
     * 保持现值"当有效配置沿用（Python 是新建对象 ⇒ 天然回缺省）。与上面
     * 三模式的 memset+DEF 同款：先回宏缺省，再交给 loadConfig 按文件覆盖。 */
    memset(&s_cycleCfg, 0, sizeof(s_cycleCfg));
    s_cycleCfg.m_enabled = (bool)HSF_AUTO_CYCLE_ENABLED_DEF;
    s_cycleCfg.m_pressThreshold = HSF_AUTO_CYCLE_PRESS_THRESHOLD_DEF;
    snprintf(s_cycleCfg.m_pressCompare, sizeof(s_cycleCfg.m_pressCompare),
             "%s", HSF_AUTO_CYCLE_PRESS_COMPARE_DEF);
    s_cycleCfg.m_timerFallback = (bool)HSF_AUTO_CYCLE_TIMER_FALLBACK_DEF;
    s_cycleCfg.m_timerMinutes = HSF_AUTO_CYCLE_TIMER_MINUTES_DEF;
    s_cycleCfg.m_backwashMinutes = HSF_AUTO_CYCLE_BACKWASH_MINUTES_DEF;
    s_cycleCfg.m_rinseMinutes = HSF_AUTO_CYCLE_RINSE_MINUTES_DEF;
    /* [按键接线回缺省 2026-11-13] 与 s_cycleCfg 同口径：setup 可重复调用
     * （测试重启/换 INI）——静态变量残留会被 loadConfig 的「缺键=保持现值」
     * 当有效配置沿用。缺省=300ms + 启动/复位常开、停止常闭（Python 同）。 */
    s_btnDebounceMs = (int)AM_BTN_DEBOUNCE_MS;
    s_btnNcStart = false;
    s_btnNcStop = true;
    s_btnNcReset = false;
    amClearPhaseState();
    s_estopEverOk = false;
    s_estopBootWarned = false;
    /* [累计运行时长启动恢复 2026-11-12·对齐 Python] AutoController 构造时
     * _run_accum_ms = ctx.load_run_ms()（autocontrol.py:578）——服务端重启
     * 不归零（用户定案「这个时间要服务端持久化」）。设置未打开（测试裸装配）
     * 时 getString 回缺省空串 ⇒ 0，行为与旧口径一致。 */
    amLoadRunMs();
    s_runStartMs = 0;
    s_runPersistAtMs = 0;
    s_prevStart = s_prevStop = s_prevReset = -1;
    memset(s_lastEdgeMs, 0, sizeof(s_lastEdgeMs));
    s_stopAllSeq = 0;
    if (!XAutoController_setStepHandler(&s_ctl, amStepEntry, NULL)) {
        XPrintf("HsfServerAutoMode: 步进回调注册失败（安全默认：不动作）\n");
        return false;
    }
    s_ready = true;
    HsfServerAutoMode_loadConfig();
    return true;
}

void HsfServerAutoMode_shutdown(void)
{
    if (!s_ready) return;
    /* [优雅退出把"正在跑的这一段"结算落盘 2026-11-12] Python 侧进程直接退
     * 出时最多丢一个 60s 周期窗口；C 端 shutdown 是显式收尾路径，把最后
     * 一段结算进累计再落盘（≤60s 的丢失窗口只留给断电/崩溃）。 */
    if (s_runStartMs) {
        amSettleRunTime((uint64_t)XDateTime_currentMSecsSinceEpoch());
    }
    /* 停控制器 + 撤回调（进程关闭不替人做设备收尾，与框架 stop 契约一致）。 */
    XAutoController_stop(&s_ctl, (uint64_t)XDateTime_currentMSecsSinceEpoch());
    XAutoController_setStepHandler(&s_ctl, NULL, NULL);
    s_ready = false;
}
