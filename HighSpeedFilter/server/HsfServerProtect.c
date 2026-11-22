/**
 * @file       HsfServerProtect.c
 * @brief      服务端运行保护 + 流量计告警事件（2026-11-15；口径见同名 .h）。
 * @details    Python 事实源：server_py/hsfsrv/app.py 的 `_protect_step` /
 *             `_flowmeter_alarm_step`（逐条同口径，两端同键同文案）。
 */
#include "HsfServerProtect.h"

#include "HsfServerIo.h"        /* 流量计/进水泵门面。 */
#include "HsfServerSettings.h"  /* protect/<键> 落盘读。 */
#include "HsfServerAutoMode.h"  /* 停设备（与现场停止按钮同款）。 */
#include "XhsfLinkProto.h"      /* 配置键 + 告警类型 + 设备类别。 */
#include "XFlowMeter.h"         /* XFlowMeterAlarm_* 报警位。 */
#include "XBsonDocument.h"      /* 配置载荷（applyConfigDoc 用）。 */
#include "XBsonValue.h"         /* 取值与类型判断。 */
#include "XPrintf.h"

#include <stdio.h>
#include <stdlib.h>   /* strtod（压差上限解析）。 */
#include <string.h>

/** @brief 流量计报警位图（上次值；边沿判定用）。
 *  @details -1 = 还没取过基线（首帧只记基线、不报：服务端启动时流量计
 *           可能已经挂着空管（现场如此）⇒ 那不是"新发生的报警"，不推不
 *           报；否则每次重启都多一条噪音，把真事件淹掉）。 */
static int s_fmAlarmMask = -1;

/** @brief 「进水泵开着且流量计报空管」成立的时刻（0 = 当前不成立）。 */
static uint64_t s_protectSinceMs = 0;
/** @brief 压差**连续**超过上限的起始时刻（0 = 当前未超；去抖用）。 */
static uint64_t s_pressHighSinceMs = 0;
/** @brief 压差取数回调（App 注入；NULL = 无压差数据 ⇒ 不判定）。 */
static HsfServerProtectPressFn s_pressFn = NULL;

void HsfServerProtect_reset(void)
{
    s_fmAlarmMask = -1;
    s_protectSinceMs = 0;
    s_pressHighSinceMs = 0;
}

void HsfServerProtect_setPressureFn(HsfServerProtectPressFn fn)
{
    s_pressFn = fn;
}

/**
 * @brief 运行保护配置读（INI；缺键=缺省；越界夹到合法区间）。
 * @details 键路径 protect/<键>，与协议键同名（便于回读对照）。
 */
int HsfServerProtect_timeoutMs(void)
{
    const int v = HsfServerSettings_getInt(
        "protect/inletNoFlowTimeoutMs",
        HSF_PROTECT_INLET_NOFLOW_TIMEOUT_MS_DEF);
    if (v < HSF_PROTECT_INLET_NOFLOW_TIMEOUT_MS_MIN) {
        return HSF_PROTECT_INLET_NOFLOW_TIMEOUT_MS_MIN;
    }
    if (v > HSF_PROTECT_INLET_NOFLOW_TIMEOUT_MS_MAX) {
        return HSF_PROTECT_INLET_NOFLOW_TIMEOUT_MS_MAX;
    }
    return v;
}

/**
 * @brief 压差过大保护是否启用（缺省 false = 不启用）。
 * @details 压差上限与现场工艺强相关（滤芯阻力/清洗周期都会改）⇒ 写死
 *          一个值必然误动作，由现场自己定阈值并决定开不开。
 */
bool HsfServerProtect_pressHighEnabled(void)
{
    return HsfServerSettings_getInt("protect/pressHighEnabled",
                                    HSF_PROTECT_PRESS_HIGH_ENABLED_DEF
                                        ? 1 : 0) != 0;
}

/**
 * @brief 压差上限（MPa；缺省 0.8 —— 正常运行约 0.5 MPa）。
 * @details [为什么 INI 存字符串] C 端设置只有 int/string 两种；压差是
 *          小数（0.8 MPa），存 int 会丢精度 ⇒ 存**字符串**（"0.800"）
 *          用 strtod 解析（手改 INI 时也可读懂）。
 */
double HsfServerProtect_pressHighLimitMpa(void)
{
    char buf[32];
    double mpa = HSF_PROTECT_PRESS_HIGH_LIMIT_MPA_DEF;
    char* end = NULL;
    HsfServerSettings_getString("protect/pressHighLimitMpa", "0.800", buf,
                                (int)sizeof(buf));
    /* [2026-11-15 审计·对齐 Python] strtod 不校验 ⇒ INI 里写错一个字符
     * （"0.8x"）时 mpa=0 ⇒ 保护**静默失效**（limit<=0 ⇒ 不判定）。
     * Python 侧走 _float（非法回缺省）⇒ 这边也校验：没解析出数字就用缺省。 */
    mpa = strtod(buf, &end);
    if (end == buf) {
        mpa = HSF_PROTECT_PRESS_HIGH_LIMIT_MPA_DEF;
    }
    if (mpa < HSF_PROTECT_PRESS_HIGH_LIMIT_MPA_MIN) {
        mpa = HSF_PROTECT_PRESS_HIGH_LIMIT_MPA_MIN;
    } else if (mpa > HSF_PROTECT_PRESS_HIGH_LIMIT_MPA_MAX) {
        mpa = HSF_PROTECT_PRESS_HIGH_LIMIT_MPA_MAX;
    }
    return mpa;
}

/**
 * @brief 压差过大触发后是否**停止设备**（缺省 true = 停）。
 * @details 与空管保护的「停止设备」开关是**两个独立的键**
 *      （protect/inletNoFlowStop vs protect/pressHighStop）：两种保护的
 *      处置力度可能不同（空管多是仪表/水路问题，压差过大是滤芯堵塞）。
 */
bool HsfServerProtect_pressHighStop(void)
{
    return HsfServerSettings_getInt("protect/pressHighStop",
                                    HSF_PROTECT_PRESS_HIGH_STOP_DEF
                                        ? 1 : 0) != 0;
}

/** @brief 运行保护动作是否**停止设备**（缺省 true = 停）。 */
bool HsfServerProtect_stopDevices(void)
{
    return HsfServerSettings_getInt("protect/inletNoFlowStop",
                                    HSF_PROTECT_INLET_NOFLOW_STOP_DEF
                                        ? 1 : 0) != 0;
}

/**
 * @brief 应用 protect 节的一整包配置（fail-closed；见 .h 的说明）。
 * @details [为什么独立成函数] App 的命令处理函数是 static ⇒ 回归锁够不着；
 *          把「校验 + 写入」收在这里 ⇒ App 与测试**共用同一份纪律**
 *          （两端口径一致，且回归锁真能抓到回潮）。
 */
bool HsfServerProtect_applyConfigDoc(const struct XBsonDocument* doc,
                                     char* err, int cap)
{
    bool hasTimeout = false, hasNoFlowStop = false, hasPressEnabled = false;
    bool hasPressLimit = false, hasPressStop = false;
    int newTimeout = 0;
    bool newNoFlowStop = true, newPressEnabled = false, newPressStop = true;
    double newPressLimit = HSF_PROTECT_PRESS_HIGH_LIMIT_MPA_DEF;
    char buf[192];
    size_t i;

    if (err && cap > 0) err[0] = '\0';
    if (!doc) {
        if (err && cap > 0) snprintf(err, (size_t)cap, "载荷里没有配置键");
        return false;
    }
    for (i = 0; i < XBsonDocument_size_base(doc); ++i) {
        const XBsonElement* el = XBsonDocument_at_const(doc, (int64_t)i);
        const char* key;
        const XBsonValue* v;
        if (!el) continue;
        key = XString_toUtf8(&el->m_key);
        if (!key || strcmp(key, "section") == 0) continue;
        v = &el->m_value;
        if (strcmp(key, HSF_CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS) == 0) {
            if (!XBsonValue_isInt32(v) && !XBsonValue_isInt64(v)) {
                snprintf(buf, sizeof(buf), "%s 需要整数毫秒", key);
                goto fail;
            }
            newTimeout = (int)XBsonValue_toInt64(v, 0);
            if (newTimeout < HSF_PROTECT_INLET_NOFLOW_TIMEOUT_MS_MIN) {
                newTimeout = HSF_PROTECT_INLET_NOFLOW_TIMEOUT_MS_MIN;
            } else if (newTimeout > HSF_PROTECT_INLET_NOFLOW_TIMEOUT_MS_MAX) {
                newTimeout = HSF_PROTECT_INLET_NOFLOW_TIMEOUT_MS_MAX;
            }
            hasTimeout = true;
        } else if (strcmp(key, HSF_CFGK_PROTECT_INLET_NOFLOW_STOP) == 0) {
            if (!XBsonValue_isBool(v)) {
                snprintf(buf, sizeof(buf), "%s 需要布尔值（true/false）", key);
                goto fail;
            }
            newNoFlowStop = XBsonValue_toBool(v, true);
            hasNoFlowStop = true;
        } else if (strcmp(key, HSF_CFGK_PROTECT_PRESS_HIGH_ENABLED) == 0) {
            if (!XBsonValue_isBool(v)) {
                snprintf(buf, sizeof(buf), "%s 需要布尔值（true/false）", key);
                goto fail;
            }
            newPressEnabled = XBsonValue_toBool(v, false);
            hasPressEnabled = true;
        } else if (strcmp(key, HSF_CFGK_PROTECT_PRESS_HIGH_LIMIT_MPA) == 0) {
            if (!XBsonValue_isDouble(v) && !XBsonValue_isInt32(v) &&
                !XBsonValue_isInt64(v)) {
                snprintf(buf, sizeof(buf), "%s 需要数字（MPa）", key);
                goto fail;
            }
            newPressLimit = XBsonValue_toDouble(
                v, HSF_PROTECT_PRESS_HIGH_LIMIT_MPA_DEF);
            if (newPressLimit < HSF_PROTECT_PRESS_HIGH_LIMIT_MPA_MIN) {
                newPressLimit = HSF_PROTECT_PRESS_HIGH_LIMIT_MPA_MIN;
            } else if (newPressLimit > HSF_PROTECT_PRESS_HIGH_LIMIT_MPA_MAX) {
                newPressLimit = HSF_PROTECT_PRESS_HIGH_LIMIT_MPA_MAX;
            }
            hasPressLimit = true;
        } else if (strcmp(key, HSF_CFGK_PROTECT_PRESS_HIGH_STOP) == 0) {
            if (!XBsonValue_isBool(v)) {
                snprintf(buf, sizeof(buf), "%s 需要布尔值（true/false）", key);
                goto fail;
            }
            newPressStop = XBsonValue_toBool(v, true);
            hasPressStop = true;
        } else {
            snprintf(buf, sizeof(buf), "未知的运行保护配置键：%s", key);
            goto fail;
        }
    }
    if (!hasTimeout && !hasNoFlowStop && !hasPressEnabled &&
        !hasPressLimit && !hasPressStop) {
        if (err && cap > 0) snprintf(err, (size_t)cap, "载荷里没有配置键");
        return false;
    }
    /* ---- 全部校验通过 ⇒ 一次性写入（不留半截配置）---- */
    if (hasTimeout) {
        HsfServerSettings_setInt("protect/inletNoFlowTimeoutMs", newTimeout);
    }
    if (hasNoFlowStop) {
        HsfServerSettings_setInt("protect/inletNoFlowStop",
                                 newNoFlowStop ? 1 : 0);
    }
    if (hasPressEnabled) {
        HsfServerSettings_setInt("protect/pressHighEnabled",
                                 newPressEnabled ? 1 : 0);
    }
    if (hasPressLimit) {
        char lim[32];
        snprintf(lim, sizeof(lim), "%.3f", newPressLimit);
        HsfServerSettings_setString("protect/pressHighLimitMpa", lim);
    }
    if (hasPressStop) {
        HsfServerSettings_setInt("protect/pressHighStop", newPressStop ? 1 : 0);
    }
    return true;
fail:
    if (err && cap > 0) snprintf(err, (size_t)cap, "%s", buf);
    return false;
}

/**
 * @brief 流量计报警位（**全部侧合并**）与数据有效性。
 * @details 现场只有一台流量计（站5，路 B），但框架按侧建模 ⇒ 取两侧并集
 *          （任一侧在线且有效即参与判定）。
 */
static int hsfProtectFlowMeterMask(bool* outValid)
{
    int si;
    int mask = 0;
    bool valid = false;
    for (si = 0; si < (int)HsfSide_Count; ++si) {
        const HsfServerIoFlowMeter* fm = HsfServerIo_flowMeter((HsfSide)si);
        double flow = 0.0, speed = 0.0, fwd = 0.0, rev = 0.0;
        int unit = 0, totalUnit = 0, alarms = 0;
        bool v = false;
        if (!fm) continue;
        if (!HsfServerIoFlowMeter_values(fm, &flow, &speed, &fwd, &rev, &unit,
                                         &totalUnit, &alarms, &v)) {
            continue;
        }
        if (!v) continue;
        valid = true;
        mask |= alarms;
    }
    if (outValid) *outValid = valid;
    return mask;
}

/**
 * @brief 进水泵是否在开（DO 回显；泵无反馈点，输出即状态）。
 * @details 数字量模块不在线/数据不新鲜 ⇒ 判"没开"（泵的实际状态此时
 *          **未知**，不能拿猜出来的状态去停设备）。
 */
static bool hsfProtectInletPumpOn(void)
{
    const HsfServerIoPump* bank = HsfServerIo_pumps();
    const int count = bank ? HsfServerIoPump_count(bank) : 0;
    int i;
    for (i = 0; i < count; ++i) {
        const HsfServerIoPumpItem* it = HsfServerIoPump_item(bank, i);
        if (it && it->m_valid && it->m_on) return true;
    }
    return false;
}

/**
 * @brief 流量计是否**有效**地报空管（离线/数据陈旧 ⇒ false）。
 * @details 读不到 ≠ 空管：拿"没数据"当"空管"会把整条线停掉。
 */
static bool hsfProtectFlowMeterEmpty(void)
{
    bool valid = false;
    const int mask = hsfProtectFlowMeterMask(&valid);
    return valid && ((mask & XFlowMeterAlarm_EmptyPipe) != 0);
}

/** @brief 往定长缓冲尾部追加一段 UTF-8 文本（越界截断）；返回新长度。 */
static size_t hsfProtectAppend(char* buf, size_t cap, size_t n, const char* s)
{
    size_t len;
    if (!buf || cap == 0 || !s) return n;
    if (n >= cap) return n;
    len = strlen(s);
    if (n + len >= cap) len = (cap > n + 1) ? (cap - n - 1) : 0;
    memcpy(buf + n, s, len);
    n += len;
    buf[n] = '\0';
    return n;
}

/**
 * @brief 流量计报警位图 → 中文（顺序与客户端 fmAlarmText 同款：
 *        空管 → 上限 → 下限 → 系统）。
 * @details [分隔符 2026-11-15·对齐 Python] 用「、」（顿号）——Python 侧
 *          `_flowmeter_alarm_text` 就是 "、".join(...)；两端文案必须逐字
 *          相同（报警页/日志页是同一批运维在看）。
 */
static void hsfProtectAlarmText(int mask, char* buf, size_t cap)
{
    size_t n = 0;
    if (!buf || cap == 0) return;
    buf[0] = '\0';
#define FM_APPEND(txt)                                                    \
    do { if (n) n = hsfProtectAppend(buf, cap, n, "、");                  \
         n = hsfProtectAppend(buf, cap, n, (txt)); } while (0)
    if (mask & XFlowMeterAlarm_EmptyPipe) FM_APPEND("空管");
    if (mask & XFlowMeterAlarm_Upper)      FM_APPEND("上限");
    if (mask & XFlowMeterAlarm_Lower)      FM_APPEND("下限");
    if (mask & XFlowMeterAlarm_System)     FM_APPEND("系统");
#undef FM_APPEND
    if (n == 0) {
        snprintf(buf, cap, "%s", "流量计报警已恢复");
    } else {
        char tmp[128];
        snprintf(tmp, sizeof(tmp), "流量计报警：%s", buf);
        snprintf(buf, cap, "%s", tmp);
    }
}

/** @brief 填一条事件（类型/设备类别/文字）。 */
static void hsfProtectFill(HsfServerProtectEvent* out, int kind, int devKind,
                           const char* text)
{
    if (!out) return;
    out->m_hasAlarm = true;
    out->m_kind = kind;
    out->m_devKind = devKind;
    snprintf(out->m_text, sizeof(out->m_text), "%s", text ? text : "");
}

/**
 * @brief 空管保护：进水泵开着、流量计仍报空管，持续超时限 ⇒ 报警 + 停机。
 * @details 判据与 Python 侧 app._protect_step 同口径：
 *          ① 进水泵在开（DO 回显）；② 流量计**在线且新鲜**并报空管；
 *          ③ ①∧② **连续**成立 ≥ 时限（0 = 关闭该保护）。
 *          [为什么算"连续成立"的时长] 泵开着很久、流量计此刻才闪一次空管
 *          位 ⇒ 按"泵开时长"判会立刻停线 ⇒ 必须**连续**空管满一个时限。
 */
static bool hsfProtectInletNoFlowStep(uint64_t nowMs,
                                      HsfServerProtectEvent* out,
                                      int timeoutMs)
{
    if (timeoutMs <= 0) {
        s_protectSinceMs = 0;
        return false;
    }
    /* [锁存已在 ⇒ 不再重复动作] 同压差保护：条件还成立时每超时周期报一次
     * 会把报警表刷爆 ⇒ 锁存未解（人没确认）就不重复动作。 */
    if (HsfServerAutoMode_faultLatched()) {
        s_protectSinceMs = 0;
        return false;
    }
    if (!hsfProtectInletPumpOn() || !hsfProtectFlowMeterEmpty()) {
        s_protectSinceMs = 0;
        return false;
    }
    if (s_protectSinceMs == 0) {
        s_protectSinceMs = nowMs;
        return false;
    }
    /* [时钟回拨保护 2026-11-15] C 端的服务端节拍用**墙钟**
     * （XDateTime_currentMSecsSinceEpoch）⇒ NTP 校时可能让墙钟**往回跳**。
     * 无符号相减会下溢成一个巨大值 ⇒ 保护**迟迟不动作**（泵空管着却不
     * 停线）。⇒ 墙钟回拨时**重新起算**（Python 侧用单调钟，无此问题；
     * 但 C 端全服务端都用墙钟，这里只做本模块的防御）。
     */
    if (nowMs < s_protectSinceMs) {
        s_protectSinceMs = nowMs;
        return false;
    }
    if ((nowMs - s_protectSinceMs) < (uint64_t)timeoutMs) {
        return false;
    }
    s_protectSinceMs = 0;
    {
        const bool stopDevices = HsfServerProtect_stopDevices();
        char text[160];
        snprintf(text, sizeof(text),
                 "进水泵开启期间流量计持续报空管 %.0f 秒%s",
                 (double)timeoutMs / 1000.0,
                 stopDevices ? "，已停止设备"
                             : "（只报警，未停设备；报警已锁存，需人工复位）");
        XPrintf("HsfServerApp: 运行保护：%s\n", text);
        hsfProtectFill(out, HSF_AUTO_ALARM_FLOWMETER,
                       (int)HsfDeviceKind_FlowMeter, text);
        /* [锁存 2026-11-15] 保护动作进了报警表 ⇒ 锁存：必须人工复位才能
         * 再启动设备（停机与否由配置决定，但"要不要人工确认"由锁存保证）。
         * enqueueAlarm=false —— 本模块**自己**发了这条告警（带 dev 键）。 */
        (void)HsfServerAutoMode_raiseFault(text, nowMs, stopDevices, false);
        return true;
    }
}

/**
 * @brief 压差过大保护：压差连续超上限达去抖时长 ⇒ 报警 +（按配置）停机。
 * @details [读不到 ≠ 压差过大] 模拟量通道离线/未标定 ⇒ **不判定**
 *          （拿"没数据"当"压差过大"会把正在生产的线停掉）。
 *          [为什么要去抖 2 秒] 滤芯堵塞是分钟级过程，但启泵水锤等瞬时
 *          尖峰会误触发 ⇒ 要求**连续**超过上限 2 秒才动作。
 *          [动作后锁存] 告警进报警表 ⇒ 走**故障锁存**（与设备故障同款）：
 *          压力回落后**不自动恢复**，必须人工复位才能再启动设备。
 */
static bool hsfProtectPressHighStep(uint64_t nowMs,
                                    HsfServerProtectEvent* out)
{
    const bool enabled = HsfServerProtect_pressHighEnabled();
    double limit = HsfServerProtect_pressHighLimitMpa();
    double mpa = 0.0;
    bool have = false;

    if (!enabled || limit <= 0.0 || !s_pressFn) {
        s_pressHighSinceMs = 0;
        return false;
    }
    /* [锁存已在 ⇒ 不再重复动作] 压差还高着时若每 2 秒报一次，报警表会被
     * 刷爆（实测：一次超压 ⇒ 连续两条告警 + 两次全停）。锁存未解说明
     * "人还没确认过" ⇒ 不重复动作，等人工复位后才有下一次机会。 */
    if (HsfServerAutoMode_faultLatched()) {
        s_pressHighSinceMs = 0;
        return false;
    }
    have = s_pressFn(&mpa);
    if (!have || mpa <= limit) {
        s_pressHighSinceMs = 0;
        return false;
    }
    if (s_pressHighSinceMs == 0) {
        s_pressHighSinceMs = nowMs;
        return false;
    }
    /* [时钟回拨保护 2026-11-15] 同上：墙钟往回跳时重新起算（无符号相减
     * 下溢会让去抖迟迟不满足 ⇒ 超压却不动作）。 */
    if (nowMs < s_pressHighSinceMs) {
        s_pressHighSinceMs = nowMs;
        return false;
    }
    if ((nowMs - s_pressHighSinceMs) < (uint64_t)HSF_PROTECT_PRESS_HIGH_DEBOUNCE_MS) {
        return false;
    }
    /* ---- 到时限：动作一次 ---- */
    s_pressHighSinceMs = 0;
    {
        const bool stopDevices = HsfServerProtect_pressHighStop();
        char text[192];
        snprintf(text, sizeof(text),
                 "压差过高：%.3f MPa（超过上限 %.3f）%s",
                 mpa, limit,
                 stopDevices ? "，已停止设备"
                             : "（只报警，未停设备；报警已锁存，需人工复位）");
        XPrintf("HsfServerApp: 运行保护：%s\n", text);
        if (out) {
            out->m_hasAlarm = true;
            out->m_kind = HSF_AUTO_ALARM_PRESS_HIGH;
            out->m_devKind = (int)HsfDeviceKind_AnalogIo;
            snprintf(out->m_text, sizeof(out->m_text), "%s", text);
        }
        /* [锁存 2026-11-15] 压差过大进了报警表 ⇒ 锁存：必须人工复位才能
         * 再启动设备（停机与否由配置决定）。enqueueAlarm=false —— 本模块
         * **自己**发了这条告警（带 dev 键）。 */
        (void)HsfServerAutoMode_raiseFault(text, nowMs, stopDevices, false);
        return true;
    }
}

bool HsfServerProtect_tick(uint64_t nowMs, HsfServerProtectEvent* out)
{
    const int timeoutMs = HsfServerProtect_timeoutMs();
    bool fmValid = false;
    const int fmMask = hsfProtectFlowMeterMask(&fmValid);

    if (out) {
        out->m_hasAlarm = false;
        out->m_kind = 0;
        out->m_devKind = -1;
        out->m_text[0] = '\0';
    }

    /* ---- 流量计告警**边沿** → 一条事件 ---- */
    if (fmValid) {
        if (s_fmAlarmMask < 0) {
            /* [首帧只记基线] 见 s_fmAlarmMask 的注记。 */
            s_fmAlarmMask = fmMask;
        } else if (fmMask != s_fmAlarmMask) {
            char text[128];
            s_fmAlarmMask = fmMask;
            hsfProtectAlarmText(fmMask, text, sizeof(text));
            XPrintf("HsfServerApp: 流量计报警 %s\n", text);
            hsfProtectFill(out, HSF_AUTO_ALARM_FLOWMETER,
                           (int)HsfDeviceKind_FlowMeter, text);
            return true;
        }
    }

    /* ---- ② 空管保护（进水泵开着仍报空管）---- */
    if (hsfProtectInletNoFlowStep(nowMs, out, timeoutMs)) {
        return true;
    }
    /* ---- ③ 压差过大保护（2026-11-15）---- */
    return hsfProtectPressHighStep(nowMs, out);
}
