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

typedef struct {
    bool     m_screenOff;      /**< 影子状态：当前是否已熄屏。 */
    bool     m_hasBaseline;    /**< 空闲计时基线是否已建立。 */
    bool     m_defaultsApplied;/**< 缺省策略是否已建立。 */
    int64_t  m_lastActivityMs; /**< 最近一次用户活动时刻（单调毫秒）。 */
    XPlatformPowerPolicy m_policy; /**< 策略影子（应用设定值）。 */
    unsigned m_caps;           /**< 能力位缓存（首次查询时探测）。 */
    bool     m_capsProbed;     /**< 能力位是否已探测。 */
} XPlatformPowerState;

static XPlatformPowerState g_xpow;

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
}

/** @brief 建立/刷新空闲计时基线。 */
static void xpow_markActivity(void)
{
    g_xpow.m_lastActivityMs = xpow_nowMs();
    g_xpow.m_hasBaseline = true;
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
