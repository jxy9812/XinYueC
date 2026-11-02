/******************************************************************************
 * @file       XPlatformPower.c
 * @brief      平台电源管理公共中转（影子状态 + 空闲打点 + 策略巡检）。
 * @details    契约声明见 XPlatformPower.h。本文件是公共层与平台层之间
 *             的唯一中转点，承担三件事：
 *
 *             1. 影子状态：平台层多有「动作下发」而无「状态查询」面
 *                （fbdev 背光 PWM 写入后读不回熄屏态、X11 DPMS 无查询
 *                接口），故公共层维护一份影子状态作为权威来源。
 *             2. 空闲计时：noteActivity 打点，计时基准取单调时钟
 *                （XDateTime_currentMSecsSinceEpoch），系统校时不会让
 *                「多久没动」跳变。
 *             3. 策略巡检：tick() 由应用层的周期定时器调用，按策略结构
 *                里的两个阈值推进——达到熄屏阈值则熄屏，达到挂起阈值
 *                则先熄屏再整机挂起。阈值由应用层注入（setPolicy），
 *                框架不内置任何默认超时（默认「从不」）。
 * @note       本文件不引用任何平台 API（平台调用一律经 Driver_ 钩子），
 *             嵌入式可用。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XPlatformPower.h"

#include "XDateTime.h"
#include "XCoreApplication.h" /* XCoreApplication_instance / postEvent（框架事件上行口）。 */
#include "XEvent.h"           /* XEvent_create_ex（基础框架事件）。 */

typedef struct {
    /* —— 1-bit 旗标组（位域内存态表示：本结构体为 .c 内静态单例，
     *    不落盘/跨进程，无序列化面；不用 bool:1——实现定义行为，
     *    挂 unsigned int 底型）—— */
    unsigned int m_screenOff : 1;       /**< 影子状态：当前是否已熄屏。 */
    unsigned int m_hasBaseline : 1;     /**< 空闲计时基线是否已建立。 */
    unsigned int m_defaultsApplied : 1; /**< 缺省策略是否已建立。 */
    unsigned int m_reconcileArmed : 1;  /**< 对账armed：首次 tick 前的启动同步待执行。 */
    unsigned int m_capsProbed : 1;      /**< 能力位是否已探测。 */
    unsigned m_caps;           /**< 能力位缓存（首次查询时探测）。 */
    int64_t  m_lastActivityMs; /**< 最近一次用户活动时刻（单调毫秒）。 */
    int64_t  m_lastReprobeMs;  /**< 上次能力重探时刻（None 冷却节流用）。 */
    XPlatformPowerPolicy m_policy; /**< 策略影子（应用设定值）。 */
} XPlatformPowerState;

/* 布局锁：56→48B（旗标+能力 8B + 时间戳 16B + 策略 24B）。
 * 静态断言分编译器收敛（与 XAtomic.h 的 XATOMIC_THREAD_LOCAL/XALIGNAS 同款）：
 * MSVC 默认 C 模式不认 C11 的 _Static_assert（需 /std:c11+），裸用会被当
 * 隐式函数声明 ⇒ sizeof 前语法错误（2026-11-01 远端合并回归）；GNU/Clang
 * 及声明 C11 的编译器用真断言，MSVC 兼容路径降级为空（布局约束以注释锁）。 */
#if defined(__GNUC__) || defined(__clang__) || \
    (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L)
#define XPOW_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define XPOW_STATIC_ASSERT(cond, msg)
#endif
XPOW_STATIC_ASSERT(sizeof(XPlatformPowerState) == 48,
                   "XPlatformPowerState 布局变化：旗标组/能力/时间戳/策略四段排布被改动");

static XPlatformPowerState g_xpow;

/* 前置声明：影子同步与事件广播（实现体在 xpow_markActivity 之后）。 */
static bool xpow_shadowSyncToHardware(XPlatformPowerScreenActual actual);
static void xpow_postScreenStateEvent(void);

/** @brief 取当前单调毫秒。 */
static int64_t xpow_nowMs(void)
{
    return XDateTime_currentMSecsSinceEpoch();
}

/** @brief 首次使用时建立缺省策略。
 *  @details 静态结构体是**零初始化**，而「零」对本契约不是缺省语义：
 *           两个阈值的 0 = 「从不」是对的，但唤醒来源的 0 = 「无输入可
 *           唤醒」是危险值（熄屏后叫不醒）。故缺省必须是「两个阈值
 *           从不 + 唤醒来源全部有效」，由本函数在首次使用前建立。
 *           少了这一步，界面回读到的唤醒来源是 0 → 三个开关全不勾，
 *           与「默认全部有效」的口径相悖（自测因调过 setPolicy 而
 *           恰好掩盖了它——这类「自测路径与应用启动路径不同」的坑
 *           值得记一笔）。
 */
static void xpow_ensureDefaults(void)
{
    if (g_xpow.m_defaultsApplied) return;
    g_xpow.m_defaultsApplied = true;
    g_xpow.m_policy.m_screenOffMs = 0;      /* 从不自动熄屏。 */
    g_xpow.m_policy.m_systemSuspendMs = 0;  /* 从不自动挂起。 */
    g_xpow.m_policy.m_wakeInputs = (unsigned)XPlatformPowerInputAll;
    /* 启动同步armed：首次 tick（应用首个 1Hz 巡检）时做一次影子/硬件
     * 对账——把「前任遗留熄屏态」（旧程序熄屏后退出/被替换、验证脚本
     * 写黑、面板电源管理）拉回亮屏。此处只置位不动作：capabilities()
     * 在驱动层可能触发文件系统探测（fbdev 背光节点扫描），放在 init
     * 路径有启动竞态（sysfs 尚未挂载全）；1Hz tick 已是事件循环稳态。 */
    g_xpow.m_reconcileArmed = true;
}

/** @brief 能力位重探冷却（毫秒）：探测失败后按此间隔重试。
 *  @details fbdev 熄屏手段探测在设备早期可能全部落空（sysfs/设备节点
 *           尚未就绪），但静态缓存会让「不支持」在本次进程生命周期内
 *           永久成立——开机自启场景下熄屏能力永远失效。失败结果按
 *           冷却间隔重试（10s 量级：覆盖慢挂载，又不会高频扫盘）。 */
#define XPOW_REPROBE_COOLDOWN_MS 10000

/** @brief 影子/硬件对账（含能力重探）。
 *  @details 数据源是平台实况查询（fbdev 读背光/blank 节点；X11 读服务端
 *           DPMS 档位；无读回原语的平台恒 Unknown）。三件事按序执行：
 *           1. 能力位为 0 且距上次重探超过冷却 → 重探一次（开机竞态
 *              自愈：节点就绪后能力在 10s 内生效）；
 *           2. 实况 Unknown → 平台读不回，什么都不做（不得把「读不到」
 *              当「熄屏」误点亮）；
 *           3. 影子与硬件失配 → 采信硬件（xpow_shadowSyncToHardware）：
 *              影子说亮、硬件已熄 → 立即点亮自愈；影子说熄、硬件已亮
 *              （**外部唤醒**：VNC/x11vnc、`xset dpms force on`、面板
 *              电源管理）→ 影子归位为亮并记一次活动。
 *           失配收口后把变化转成框架事件（powerStateChanged 信号）。 */
static void xpow_reconcileLocked(void)
{
    XPlatformPowerScreenActual actual;
    if ((g_xpow.m_caps == 0u || !g_xpow.m_capsProbed) &&
        xpow_nowMs() - g_xpow.m_lastReprobeMs >= XPOW_REPROBE_COOLDOWN_MS) {
        g_xpow.m_lastReprobeMs = xpow_nowMs();
        g_xpow.m_capsProbed = false;
        g_xpow.m_caps = XPlatformPower_capabilities();
    }
    actual = XPlatformPowerDriver_queryScreenState();
    if (xpow_shadowSyncToHardware(actual))
        xpow_postScreenStateEvent();
}

/** @brief 对账入口（含启动 armed 消费）。 */
static void xpow_reconcile(void)
{
    if (g_xpow.m_reconcileArmed) {
        g_xpow.m_reconcileArmed = false;
        /* 首次对账前先把能力探一次（armed 场景下 capabilities 可能
         * 还没被任何人问过）。 */
        g_xpow.m_lastReprobeMs = xpow_nowMs();
        g_xpow.m_caps = XPlatformPower_capabilities();
        g_xpow.m_capsProbed = true;
    }
    xpow_reconcileLocked();
}

/** @brief 建立/刷新空闲计时基线。 */
static void xpow_markActivity(void)
{
    g_xpow.m_lastActivityMs = xpow_nowMs();
    g_xpow.m_hasBaseline = true;
}

/* ==================== 影子跟随硬件（外部唤醒的唯一收口点） ==================== */

/** @brief 把影子态拉齐到硬件真值。
 *  @param actual 硬件实况（Unknown=平台读不到 ⇒ 不动作）。
 *  @return true=影子态发生了变化（外部可见的跃迁 ⇒ 需广播框架事件）。
 *  @details 两个方向，缺一不可：
 *           - **硬件=熄、影子=亮** ⇒ 外部把屏写黑（前任遗留/验证脚本/
 *             面板电源管理）⇒ 点亮自愈。触摸唤醒闸判「未熄屏」会放行不
 *             唤醒 ⇒ 屏幕永远黑着（刷入新程序后唤不醒的根因）。
 *           - **硬件=亮、影子=熄** ⇒ **外部唤醒**（VNC/x11vnc、
 *             `xset dpms force on`、面板电源管理；且不经应用输入通道）
 *             ⇒ 采信硬件：影子归位为亮并**记一次活动**（空闲基线重置）。
 *             这一支是「被 VNC 唤醒后再也无法休眠」的根修：旧口径对
 *             这一支刻意不动作（理由是「外部点亮不改变应用意图，等应用
 *             自己的唤醒路径收口」），但唤醒不一定经过应用输入通道
 *             ⇒ 影子永久卡在「熄」⇒ 自动熄屏分支被 !m_screenOff 永久
 *             门死 ⇒ 应用之后再也发不出熄屏指令，且此后每个输入事件都
 *             会重复触发一次无谓的「点亮」往返。 */
static bool xpow_shadowSyncToHardware(XPlatformPowerScreenActual actual)
{
    if (actual == XPlatformPowerScreenActual_Unknown) return false;
    if (actual == XPlatformPowerScreenActual_Off && !g_xpow.m_screenOff) {
        /* 点亮自愈：只有真点亮成功才改影子（失败保持原状，下轮重试）。 */
        return XPlatformPower_setScreenState(XPlatformPowerScreenState_On);
    }
    if (actual == XPlatformPowerScreenActual_On && g_xpow.m_screenOff) {
        g_xpow.m_screenOff = false;
        /* 外部唤醒计为一次活动：有人把屏点亮了 ⇒ 此刻「屏亮着」是合理
           状态，空闲计时重新起算。若不重置，下一拍巡检（空闲时长早已
           超阈值）会立刻再熄 ⇒ VNC 用户刚唤醒就被灭掉。 */
        xpow_markActivity();
        return true;
    }
    return false;
}

/** @brief 屏幕电源态变化 → 框架事件（投递到应用对象）。
 *  @details 平台上报与 1Hz 对账共用本出口：状态跃迁一律转成框架事件
 *           （XEVENT_TYPE_SCREEN_POWER_STATE_CHANGED），由应用层转发为
 *           屏幕对象的 powerStateChanged 信号 ⇒ 任何继承自 XObject 的
 *           类都可以自主选择是否监听。 */
static void xpow_postScreenStateEvent(void)
{
    XCoreApplication* app;
    XEvent* event;
    app = XCoreApplication_instance();
    if (!app) return;
    event = XEvent_create_ex(XMEMORY_TYPE_MULTIPOOL,
                             XEVENT_TYPE_SCREEN_POWER_STATE_CHANGED);
    if (!event) return;
    /* 所有权移交事件队列：投递失败由 postEvent 内部删除事件。 */
    XCoreApplication_postEvent(app, event, XEVENT_PRIORITY_NORMAL);
}

/** @brief 平台是否支持「委托平台自己计时」（安卓：系统「屏幕超时」）。 */
static bool xpow_delegatesTimeout(void)
{
    return XPlatformPower_supports(XPlatformPowerCap_ScreenTimeout);
}

/* ==================== 公共契约 ==================== */

unsigned XPlatformPower_capabilities(void)
{
    if (!g_xpow.m_capsProbed) {
        /* 能力探测结果缓存：平台能力是静态属性（fbdev 背光节点、sysfs
         * 挂载点等在设备生命周期内不变），不随每次调用反复探测。 */
        g_xpow.m_caps = XPlatformPowerDriver_capabilities();
        g_xpow.m_capsProbed = true;
    }
    return g_xpow.m_caps;
}

bool XPlatformPower_supports(XPlatformPowerCapability cap)
{
    return (XPlatformPower_capabilities() & (unsigned)cap) != 0u;
}

bool XPlatformPower_setScreenState(XPlatformPowerScreenState state)
{
    bool ok = XPlatformPowerDriver_setScreenState(state);
    if (ok) {
        g_xpow.m_screenOff = (state == XPlatformPowerScreenState_Off);
        /* 点亮时重置空闲基线：否则用户手动唤醒后会因「空闲时长早已超
         * 阈值」而被下一轮巡检立刻再次熄屏。熄屏时**不**重置——熄屏
         * 后空闲时长需继续累加，才能到达系统挂起阈值。 */
        if (state == XPlatformPowerScreenState_On)
            xpow_markActivity();
    }
    return ok;
}

bool XPlatformPower_isScreenOff(void)
{
    return g_xpow.m_screenOff;
}

bool XPlatformPower_reconcile(void)
{
    XPlatformPowerScreenActual actual;
    xpow_ensureDefaults();
    xpow_reconcile();
    actual = XPlatformPowerDriver_queryScreenState();
    if (actual == XPlatformPowerScreenActual_Off && g_xpow.m_screenOff)
        return false; /* 对账后仍黑：点亮失败（或不支持）。 */
    return actual == XPlatformPowerScreenActual_On;
}

void XPlatformPower_reportScreenStateChanged(XPlatformPowerScreenActual actual)
{
    bool changed;
    xpow_ensureDefaults();
    /* 影子拉齐硬件真值（外部唤醒 ⇒ 影子=亮 + 记一次活动）。 */
    changed = xpow_shadowSyncToHardware(actual);
    /* 状态真的变了才广播：平台轮询会以固定节拍重复上报同一实况，
       无变化时不得打扰订阅者。 */
    if (changed)
        xpow_postScreenStateEvent();
}

bool XPlatformPower_setPolicy(const XPlatformPowerPolicy* policy)
{
    int64_t screenOffMs;
    int64_t suspendMs;
    unsigned wakeInputs;
    xpow_ensureDefaults();
    screenOffMs = policy ? policy->m_screenOffMs : 0;
    suspendMs = policy ? policy->m_systemSuspendMs : 0;
    wakeInputs = policy ? policy->m_wakeInputs : 0u;
    if (screenOffMs < 0) screenOffMs = 0;
    if (suspendMs < 0) suspendMs = 0;

    if (xpow_delegatesTimeout()) {
        /* 平台自己计时（安卓写系统「屏幕超时」）：熄屏阈值交平台设置，
         * 本地不再参与关屏判定。**失败即失败**，如实返回 false——安卓
         * 无主动关屏原语，本地计时到点也无处可关，回落等于把失效静默
         * 吞掉（界面显示「已设置 5 分钟」而系统从不熄屏）。 */
        if (!XPlatformPowerDriver_setScreenTimeout(screenOffMs))
            return false;
    }
    /* 影子始终记录用户设定值：供 UI 回显（委托计时时系统侧可能被别的
     * 途径改过，本应用仍以自己的设定为准回显）。 */
    g_xpow.m_policy.m_screenOffMs = screenOffMs;
    g_xpow.m_policy.m_systemSuspendMs = suspendMs;
    /* 唤醒来源缺省为「全部有效」：策略里没显式给（0）时按全开处理，
     * 避免调用方漏填就把唤醒路径全关死。 */
    g_xpow.m_policy.m_wakeInputs = wakeInputs ? wakeInputs
                                               : (unsigned)XPlatformPowerInputAll;
    return true;
}

void XPlatformPower_policy(XPlatformPowerPolicy* out)
{
    if (!out) return;
    xpow_ensureDefaults();
    *out = g_xpow.m_policy;
}

/* ==================== 空闲原语与策略巡检 ==================== */

void XPlatformPower_noteActivity(unsigned inputMask)
{
    /* 熄屏状态下：**只有被允许的输入源**能唤醒（屏幕已灭，界面按钮不
     * 可达，唤醒路径只能是输入设备）。未被允许的输入不唤醒，但仍算
     * 「用户有活动」——重置空闲计时，避免「鼠标一直被禁用却仍在动」
     * 时仍被判为无人操作。 */
    if (g_xpow.m_screenOff &&
        (inputMask & g_xpow.m_policy.m_wakeInputs) != 0u)
        XPlatformPower_setScreenState(XPlatformPowerScreenState_On);
    xpow_markActivity();
}

void XPlatformPower_tick(void)
{
    int64_t idle;
    bool delegated = xpow_delegatesTimeout();

    xpow_ensureDefaults();
    /* 影子/硬件对账先行（含启动同步与能力重探）：1Hz 一次，读回路径
     * 是 sysfs 节点小文本读，开销可忽略。 */
    xpow_reconcile();

    if (!g_xpow.m_hasBaseline) {
        /* 首次推进：建立基线而非立即判定超时（否则启动即熄屏）。 */
        xpow_markActivity();
        return;
    }
    idle = xpow_nowMs() - g_xpow.m_lastActivityMs;
    if (idle < 0) return;

    /* 挂起阈值优先判定：达到即先熄屏再整机挂起（挂起阈值可能小于
     * 熄屏阈值，此时「先熄屏」由本分支保证）。 */
    if (g_xpow.m_policy.m_systemSuspendMs > 0 &&
        idle >= g_xpow.m_policy.m_systemSuspendMs) {
        /* 委托计时时由系统自己熄屏，应用不重复关屏（也无从关屏）。 */
        if (!delegated && !g_xpow.m_screenOff)
            XPlatformPower_setScreenState(XPlatformPowerScreenState_Off);
        if (XPlatformPower_supports(XPlatformPowerCap_SystemSuspend))
            XPlatformPowerDriver_setSystemState(XPlatformPowerSystemState_Suspend);
        /* 挂起成功后本进程被冻结；系统恢复后重新建立基线，避免恢复
         * 瞬间因「空闲时长早已超阈值」而连环挂起。 */
        xpow_markActivity();
        return;
    }

    /* 熄屏阈值：委托计时的平台由系统执行，本地不参与。 */
    if (!delegated && g_xpow.m_policy.m_screenOffMs > 0 &&
        idle >= g_xpow.m_policy.m_screenOffMs && !g_xpow.m_screenOff)
        XPlatformPower_setScreenState(XPlatformPowerScreenState_Off);
}

bool XPlatformPower_requestScreenTimeoutPermission(void)
{
    if (!xpow_delegatesTimeout()) return false;
    return XPlatformPowerDriver_requestScreenTimeoutPermission();
}

#endif /* XGUI_ON */
