/**
 * @file       HsfServerIoValve.c
 * @brief      服务端电磁阀组实现（状态判读 + 动作下发）。
 * @details    纯业务层：不持有任何传输对象，DO/DI 由调用方（HsfServerIo）
 *             经数字量站翻译后喂进来。这样阀组与"挂在哪个模块上"完全解耦，
 *             现场换模块/换口只改配置。
 *
 *             状态判读口径与 Python 侧 valves.py 逐条对齐（见头文件注释）。
 */
#include "HsfServerIoValve.h"

#include "XPrintf.h"
#include <string.h>
#include <stdio.h>

#define HSF_VALVE_MIN_TIMEOUT_MS  500

/* ==================== 生命周期 ==================== */

static void VHsfServerIoValve_deinit(XObject* obj)
{
    HsfServerIoValve* st = (HsfServerIoValve*)obj;
    if (!st) return;
    HsfServerIoValve_clear(st);
    XClass_Deinit_Parent(XObject, obj);
}

XVtable* HsfServerIoValve_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfServerIoValve)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VHsfServerIoValve_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(HsfServerIoValve);
    return XVTABLE_DEFAULT;
}

void HsfServerIoValve_init(HsfServerIoValve* st)
{
    int i;
    if (!st) return;
    memset(st, 0, sizeof(*st));
    XObject_init((XObject*)st);
    XClassGetVtable(st) = HsfServerIoValve_class_init();
    for (i = 0; i < HSF_VALVE_COUNT; ++i) {
        st->m_items[i].m_do = -1;
        st->m_items[i].m_openDi = -1;
        st->m_items[i].m_closeDi = -1;
        st->m_items[i].m_state = HsfValveState_Unknown;
    }
    st->m_moveTimeoutMs = 8000;
}

void HsfServerIoValve_clear(HsfServerIoValve* st)
{
    if (!st) return;
    HsfServerIoValve_init(st);
}

/* ==================== 装配 ==================== */

bool HsfServerIoValve_configure(HsfServerIoValve* st,
                                const HsfServerIoValveCfg* items, int count,
                                int moveTimeoutMs, bool enabled)
{
    int i;
    int readyCount = 0;

    if (!st) return false;
    HsfServerIoValve_init(st);

    st->m_enabled = enabled;
    st->m_moveTimeoutMs = moveTimeoutMs < HSF_VALVE_MIN_TIMEOUT_MS
                        ? HSF_VALVE_MIN_TIMEOUT_MS : moveTimeoutMs;
    if (count < 0) count = 0;
    if (count > HSF_VALVE_COUNT) count = HSF_VALVE_COUNT;
    st->m_count = count;

    for (i = 0; i < count; ++i) {
        HsfServerIoValveItem* it = &st->m_items[i];
        if (items) {
            if (items[i].m_name && items[i].m_name[0])
                snprintf(it->m_name, sizeof(it->m_name), "%s", items[i].m_name);
            it->m_do = items[i].m_do;
            it->m_openDi = items[i].m_openDi;
            it->m_closeDi = items[i].m_closeDi;
        } else {
            it->m_do = -1;
            it->m_openDi = -1;
            it->m_closeDi = -1;
        }
        if (!it->m_name[0])
            snprintf(it->m_name, sizeof(it->m_name),
                     "\xE7\x94\xB5\xE7\xA3\x81\xE9\x98\x80%d", i + 1);
        it->m_state = HsfValveState_Unknown;
        it->m_valid = false;
        /* [缺省乐观] 首拍 poll 之前没有在线信息，不能凭空判离线——否则装配
         * 完成到第一次反馈之间（最多一个轮询周期）用户点开关会被莫名拒绝。
         * 待首拍 poll 用真实新鲜度覆写。 */
        it->m_online = true;
        it->m_everFeedback = false;
        it->m_cmdAtMs = 0;
        it->m_pendingCmd = false;
        it->m_timeoutReported = false;
        it->m_conflictReported = false;
        it->m_pendingAlarmKind = 0;
        it->m_pendingAlarmText[0] = '\0';
        it->m_cmdResult[0] = '\0';
        it->m_knownDo = false;
        it->m_knownDoValid = false;
        if (it->m_do >= 0 && it->m_openDi >= 0 && it->m_closeDi >= 0) {
            ++readyCount;
        } else {
            snprintf(it->m_text, sizeof(it->m_text),
                     "\xE7\x82\xB9\xE4\xBD\x8D\xE6\x9C\xAA\xE9\x85\x8D\xE7\xBD\xAE"); /* 点位未配置 */
        }
    }
    st->m_ready = (readyCount > 0);
    XPrintf("HsfServerIoValve: 装配 %d 路，其中 %d 路点位齐全（超时 %dms，启用=%s）\n",
            st->m_count, readyCount, st->m_moveTimeoutMs,
            st->m_enabled ? "是" : "否");
    return true;
}

/* ==================== 单路是否可操作 ==================== */

static bool valveItemReady(const HsfServerIoValveItem* it)
{
    return it && it->m_do >= 0 && it->m_openDi >= 0 && it->m_closeDi >= 0;
}

/** @brief 判"已经是目标位置"（用**到位反馈**判，不用命令回显——回显在
 *         阀卡住时同样为真，会把"卡住的阀"误认为到位）。 */
static bool valveSameTargetAlreadyMet(const HsfServerIoValveItem* it, bool on)
{
    if (on) return it->m_state == HsfValveState_Open;
    return it->m_state == HsfValveState_Closed;
}

/** @brief 判"同一目标正在动作中"（在途重复请求：不重置计时器——卡阀
 *         被反复点击掩盖的直接成因就是无条件重置）。 */
static bool valveSameTargetInFlight(const HsfServerIoValveItem* it, bool on)
{
    return it->m_cmdAtMs > 0 &&
           it->m_openCmd == on &&
           it->m_state == HsfValveState_Moving;
}

/* ==================== 动作 ==================== */

bool HsfServerIoValve_setOutput(HsfServerIoValve* st, int index, bool wantOpen,
                                char* outErr, int cap)
{
    HsfServerIoValveItem* it;

    if (outErr && cap > 0) outErr[0] = '\0';
    if (!st || !st->m_enabled) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s",
                             "\xE7\x94\xB5\xE7\xA3\x81\xE9\x98\x80\xE7\xBB\x84\xE6\x9C\xAA\xE5\x90\xAF\xE7\x94\xA8"); /* 电磁阀组未启用 */
        return false;
    }
    if (index < 0 || index >= st->m_count) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s",
                             "\xE9\x98\x80\xE5\x8F\xB7\xE8\xB6\x8A\xE7\x95\x8C"); /* 阀号越界 */
        return false;
    }
    it = &st->m_items[index];
    if (!valveItemReady(it)) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s",
                             "\xE7\x82\xB9\xE4\xBD\x8D\xE6\x9C\xAA\xE9\x85\x8D\xE7\xBD\xAE\xEF\xBC\x8C\xE6\x97\xA0\xE6\xB3\x95\xE6\x8E\xA7\xE5\x88\xB6"); /* 点位未配置，无法控制 */
        return false;
    }
    /* [模块离线 ⇒ 明确拒绝，且不记命令意图] 若这里放行去写，写请求会因为
     * 通道超出在线模块范围而失败，但 pendingCmd 已经置位 ⇒ 下一拍 poll 记下
     * 命令时刻 ⇒ 界面先显示"动作中"、超时后显示"动作超时未到位"。
     * 那是在**编造**一个"阀卡住了"的假象，而真相是模块不在线，根本发不出去。
     * 故这里必须早退，把"离线"如实告诉调用方。 */
    if (!it->m_online) {
        if (outErr) snprintf(outErr, (size_t)cap, "%s",
                             "\xE5\x8F\x8D\xE9\xA6\x88\xE6\xA8\xA1\xE5\x9D\x97\xE7\xA6\xBB\xE7\xBA\xBF"
                             "\xEF\xBC\x8C\xE6\x97\xA0\xE6\xB3\x95\xE6\x8E\xA7\xE5\x88\xB6"
                             "\xEF\xBC\x9A%s"); /* 反馈模块离线，无法控制： */
        if (cap > 0) {
            size_t used = strlen(outErr);
            snprintf(outErr + used, (size_t)cap - used, "%s", it->m_text);
        }
        return false;
    }

    /* ---- ① 已到位且同目标：幂等忽略（不写 DO、不重置计时器）。
     * [为什么必须在服务端拦] 客户端把到位侧按钮置灰只是界面便利；网络
     * 重发、脚本直连都能绕过。照单全收的后果是白写一次 DO + 重置 20s
     * 计时器 ⇒ 卡阀被点击掩盖。 */
    if (valveSameTargetAlreadyMet(it, wantOpen)) {
        snprintf(it->m_cmdResult, sizeof(it->m_cmdResult), "ignored");
        return true;
    }
    /* ---- ② 同目标在途：重复请求——DO 仍要重发（万一上次写丢了），
     * 但**不重置计时器**（超时从首次命令起算，卡阀无法靠连点掩盖）。 */
    if (valveSameTargetInFlight(it, wantOpen)) {
        snprintf(it->m_cmdResult, sizeof(it->m_cmdResult), "repeated");
        return true;
    }
    /* ---- ③ 反向/首次：新动作——记意图 + 从现在计时。 */
    {
        const bool reversing = (it->m_cmdAtMs > 0 && it->m_openCmd != wantOpen);
        snprintf(it->m_cmdResult, sizeof(it->m_cmdResult), "%s",
                 reversing ? "reversed" : "issued");
    }
    it->m_openCmd = wantOpen;
    it->m_pendingCmd = true;
    it->m_cmdAtMs = 0;

    /* 真正出帧由调用方（HsfServerIo_*）经数字量站单点写完成——本层只负责
     * 校验与意图记账，返回 true 表示"允许下发"。 */
    /* [新动作 ⇒ 上一轮告警标志清零] 否则第二次动作永远不会再报超时
     * （Python 侧同款 timeout_reported/conflict_reported 复位）。 */
    it->m_timeoutReported = false;
    it->m_conflictReported = false;
    return true;
}

void HsfServerIoValve_noteOwnWrite(HsfServerIoValve* st, int index, bool on)
{
    HsfServerIoValveItem* it;
    if (!st || index < 0 || index >= st->m_count) return;
    it = &st->m_items[index];
    it->m_knownDo = on;
    it->m_knownDoValid = true;
}

/* ==================== 反馈更新 / 状态判读 ==================== */

/**
 * @brief 登记一条待推送告警（本拍至多一条）。
 * @param it 目标阀条目。
 * @param kind 告警类型（HsfValveAlarmKind）。
 * @param text 告警文字。
 * @note 与 Python 侧 ValveItem._make_alarm 同口径：告警是**事件**，
 *       由 poll 产生、由上层 takeAlarm 取走推送。
 */
static void valveQueueAlarm(HsfServerIoValveItem* it, int kind, const char* text)
{
    it->m_pendingAlarmKind = kind;
    snprintf(it->m_pendingAlarmText, sizeof(it->m_pendingAlarmText), "%s",
             text ? text : "");
}

/**
 * @brief 单路状态判读（对齐 Python ValveItem.refresh_state）。
 * @param sourceFresh 本路两个到位 DI 通道**现在是否仍可信**（链路未陈旧）。
 */
static void valveRefreshItem(HsfServerIoValveItem* it, uint64_t nowMs,
                             int moveTimeoutMs, bool diValid, bool sourceFresh)
{
    int state;

    if (!valveItemReady(it)) {
        it->m_state = HsfValveState_Unknown;
        it->m_valid = false;
        it->m_online = false;
        snprintf(it->m_text, sizeof(it->m_text),
                 "\xE7\x82\xB9\xE4\xBD\x8D\xE6\x9C\xAA\xE9\x85\x8D\xE7\xBD\xAE"); /* 点位未配置 */
        return;
    }
    it->m_online = sourceFresh && diValid;

    /* [反馈源不可用 ⇒ 明确报"无法控制"] 反馈来自的模块不在线时，任何状态
     * 推演都没有依据：缓存里的旧值不可信，读到的 0 也不能当作"现在关着"。
     * 此时必须显式宣告不可用，且**不能**退化成"无到位反馈（检查接线）"
     * ——那句话会把排查方向从"模块掉线"带偏到"行程开关没接"。
     * 状态保持 Unknown（不伪造 Open/Closed），文本给出可执行的结论。 */
    if (!sourceFresh) {
        it->m_state = HsfValveState_Unknown;
        it->m_valid = false;
        snprintf(it->m_text, sizeof(it->m_text),
                 "\xE5\x8F\x8D\xE9\xA6\x88\xE6\xA8\xA1\xE5\x9D\x97\xE7\xA6\xBB\xE7\xBA\xBF"
                 "\xEF\xBC\x8C\xE6\x97\xA0\xE6\xB3\x95\xE6\x8E\xA7\xE5\x88\xB6"
                 "\xEF\xBC\x88\xE6\xA3\x80\xE6\x9F\xA5\xE6\x89\xA9\xE5\xB1\x95"
                 "\xE6\xA8\xA1\xE5\x9D\x97\xE5\x9C\xA8\xE7\xBA\xBF\xE4\xB8\x8E\xE6\x8E\xA5"
                 "\xE7\xBA\xBF\xEF\xBC\x89");
        /* 反馈模块离线，无法控制（检查扩展模块在线与接线） */
        it->m_cmdAtMs = 0;      /* 动作计时作废：没有反馈就无法判"到位/超时"。 */
        return;
    }

    it->m_valid = diValid;
    if (!diValid) {
        /* 从没读到过反馈（模块在但本拍无有效值）：保留上一状态，
         * 只标 valid=false，界面按"数据无效"呈现。绝不在这里改状态
         * ——否则模块一掉线六个阀全变"未知"，看着像全坏了。 */
        return;
    }

    if (it->m_openFb && it->m_closeFb) {
        state = HsfValveState_Fault;
        snprintf(it->m_text, sizeof(it->m_text),
                 "\xE5\xBC\x80/\xE5\x85\xB3\xE5\x88\xB0\xE4\xBD\x8D\xE4\xBF\xA1\xE5\x8F\xB7"
                 "\xE5\x90\x8C\xE6\x97\xB6\xE6\x9C\x89\xE6\x95\x88\xEF\xBC\x88\xE6\x8E\xA5\xE7\xBA\xBF"
                 "\xE6\x88\x96\xE9\x98\x80\xE4\xBD\x93\xE6\x95\x85\xE9\x9A\x9C\xEF\xBC\x89");
        /* 开/关到位信号同时有效（接线或阀体故障） */
        /* [一次持续只报一次] 判据每拍都成立；没有这个位会每 200ms 推一条，
         * 客户端被刷屏（Python 侧同款 conflict_reported）。 */
        if (!it->m_conflictReported) {
            it->m_conflictReported = true;
            valveQueueAlarm(it, HsfValveAlarmKind_Conflict, it->m_text);
        }
        it->m_state = state;
        it->m_cmdAtMs = 0;
        return;
    }
    if (it->m_openFb) {
        /* [停在旧位不算到位] 有在途命令且命令目标是**关**时，这个"开到位"
         * 只是命令下发前一拍的旧反馈（DI 缓存滞后站点轮询 ≤200ms）——
         * 状态必须保持 Moving，绝不能把它当"已到位"把 20s 计时基清零。
         * 与 Python valves.py 的承载性修复（arrived_at_target 判定）同口径：
         * 旧实现无条件清 cmd_at_ms ⇒ 刚发的命令被旧反馈瞬间"到位"，
         * 卡阀被掩盖、超时永远不触发。 */
        if (it->m_cmdAtMs != 0 && !it->m_openCmd) {
            it->m_state = HsfValveState_Moving;
            snprintf(it->m_text, sizeof(it->m_text),
                     "\xE5\x8A\xA8\xE4\xBD\x9C\xE4\xB8\xAD");  /* 动作中 */
            return;
        }
        /* 到位了（且与命令目标一致，或本就没有在途命令）。
         * 若此前报过超时，发一条"恢复"消警（一次动作一条）。 */
        if (it->m_timeoutReported) {
            it->m_timeoutReported = false;
            snprintf(it->m_pendingAlarmText, sizeof(it->m_pendingAlarmText),
                     "\xE5\xB7\xB2\xE5\x88\xB0\xE4\xBD\x8D\xEF\xBC\x88\xE5\xBC\x80\xEF\xBC\x89"); /* 已到位（开） */
            it->m_pendingAlarmKind = HsfValveAlarmKind_Cleared;
        }
        it->m_conflictReported = false;
        it->m_state = HsfValveState_Open;
        it->m_text[0] = '\0';
        it->m_cmdAtMs = 0;
        return;
    }
    if (it->m_closeFb) {
        /* [停在旧位不算到位] 对称分支：在途命令目标是"开"时，"关到位"
         * 是旧反馈，保持 Moving（见上一分支注记）。 */
        if (it->m_cmdAtMs != 0 && it->m_openCmd) {
            it->m_state = HsfValveState_Moving;
            snprintf(it->m_text, sizeof(it->m_text),
                     "\xE5\x8A\xA8\xE4\xBD\x9C\xE4\xB8\xAD");  /* 动作中 */
            return;
        }
        if (it->m_timeoutReported) {
            it->m_timeoutReported = false;
            snprintf(it->m_pendingAlarmText, sizeof(it->m_pendingAlarmText),
                     "\xE5\xB7\xB2\xE5\x88\xB0\xE4\xBD\x8D\xEF\xBC\x88\xE5\x85\xB3\xEF\xBC\x89"); /* 已到位（关） */
            it->m_pendingAlarmKind = HsfValveAlarmKind_Cleared;
        }
        it->m_conflictReported = false;
        it->m_state = HsfValveState_Closed;
        it->m_text[0] = '\0';
        it->m_cmdAtMs = 0;
        return;
    }

    /* 两个反馈都为 0：动作中 / 无反馈 / 超时。 */
    if (it->m_cmdAtMs == 0) {
        /* 从未下过命令，且读到过反馈 ⇒ 位置未知但阀是好的（阀体可能停在
         * 中间行程）；从未收到过任何反馈 ⇒ 行程开关没接。 */
        it->m_state = HsfValveState_Unknown;
        if (!it->m_everFeedback)
            snprintf(it->m_text, sizeof(it->m_text),
                     "\xE6\x97\xA0\xE5\x88\xB0\xE4\xBD\x8D\xE5\x8F\x8D\xE9\xA6\x88"
                     "\xEF\xBC\x88\xE6\xA3\x80\xE6\x9F\xA5\xE8\xA1\x8C\xE7\xA8\x8B"
                     "\xE5\xBC\x80\xE5\x85\xB3\xE6\x8E\xA5\xE7\xBA\xBF\xEF\xBC\x89");
        /* 无到位反馈（检查行程开关接线） */
        else
            it->m_text[0] = '\0';
        return;
    }
    if (nowMs - it->m_cmdAtMs > (uint64_t)moveTimeoutMs) {
        /* [不误报故障] 超时了但历史上从未出现过任何到位信号 ⇒ 更可能是
         * 行程开关没接，而不是阀卡住。报"故障"会天天误报 ⇒ 报未知+提示。 */
        if (!it->m_everFeedback) {
            it->m_state = HsfValveState_Unknown;
            snprintf(it->m_text, sizeof(it->m_text),
                     "\xE5\x91\xBD\xE4\xBB\xA4\xE5\xB7\xB2\xE4\xB8\x8B\xE5\x8F\x91"
                     "\xE4\xBD\x86\xE6\x97\xA0\xE5\x88\xB0\xE4\xBD\x8D\xE5\x8F\x8D"
                     "\xE9\xA6\x88\xEF\xBC\x88\xE8\xA1\x8C\xE7\xA8\x8B\xE5\xBC\x80"
                     "\xE5\x85\xB3\xE5\x8F\xAF\xE8\x83\xBD\xE6\x9C\xAA\xE6\x8E\xA5"
                     "\xE7\xBA\xBF\xEF\xBC\x89");
            /* 命令已下发但无到位反馈（行程开关可能未接线） */
        } else {
            it->m_state = HsfValveState_Fault;
            snprintf(it->m_text, sizeof(it->m_text),
                     "\xE5\x8A\xA8\xE4\xBD\x9C\xE8\xB6\x85\xE6\x97\xB6\xE6\x9C\xAA"
                     "\xE5\x88\xB0\xE4\xBD\x8D");  /* 动作超时未到位 */
        }
        /* [一次动作只告警一次] 判据每拍都成立；没有这个位会每 200ms 一条。 */
        if (!it->m_timeoutReported) {
            it->m_timeoutReported = true;
            valveQueueAlarm(it, HsfValveAlarmKind_Timeout, it->m_text);
        }
        return;
    }
    it->m_state = HsfValveState_Moving;
    snprintf(it->m_text, sizeof(it->m_text),
             "\xE5\x8A\xA8\xE4\xBD\x9C\xE4\xB8\xAD");  /* 动作中 */
}

void HsfServerIoValve_poll(HsfServerIoValve* st, uint32_t diMask, bool diValid,
                           uint32_t diFreshMask, uint32_t doMask, bool doValid,
                           uint64_t nowMs)
{
    int i;

    if (!st) return;
    for (i = 0; i < st->m_count; ++i) {
        HsfServerIoValveItem* it = &st->m_items[i];
        bool openFb = false;
        bool closeFb = false;
        bool fresh = false;

        if (valveItemReady(it)) {
            /* 本路反馈源是否可信 = 它的两个到位通道**都**新鲜。
             * 两者缺一就无法判阀位（"只有开到位可信"等于不知道到底开没开）。 */
            fresh = ((diFreshMask & (1u << it->m_openDi)) != 0) &&
                    ((diFreshMask & (1u << it->m_closeDi)) != 0);
        }
        if (fresh && diValid) {
            openFb = (diMask & (1u << it->m_openDi)) != 0;
            closeFb = (diMask & (1u << it->m_closeDi)) != 0;
        }
        it->m_openFb = openFb;
        it->m_closeFb = closeFb;
        if (openFb || closeFb) it->m_everFeedback = true;

        /* [外部改动接管] 与 Python _adopt_external_do 同口径：模块真实 DO
         * 电平与我方最后已知值不一致 ⇒ 有人绕过本服务动了线圈，接管为一次
         * 新动作（起算超时）。首次播种只认真实 DO 帧（doValid）——拿"还没
         * 读过的 0"当基准会凭空编造一条 external（Python 侧同款
         * last_ok_ms>0 门）。 */
        if (valveItemReady(it) && doValid && it->m_online) {
            const bool actual = ((doMask & (1u << it->m_do)) != 0);
            if (!it->m_knownDoValid) {
                it->m_knownDo = actual;
                it->m_knownDoValid = true;
            } else if (actual != it->m_knownDo) {
                it->m_openCmd = actual;
                /* [超时起点当场落定] 与 Python _adopt_external_do 同口径
                 * （cmd_at_ms = now 直写，不经 pendingCmd 下拍补写）——本拍
                 * 反馈缓存可能还是改动前一拍（显示已到位会把 cmdAt 再清 0），
                 * 补写路径会被它冲掉。 */
                it->m_cmdAtMs = nowMs;
                it->m_timeoutReported = false;
                it->m_conflictReported = false;
                snprintf(it->m_cmdResult, sizeof(it->m_cmdResult), "external");
                it->m_knownDo = actual;
                XPrintf("HsfServerIoValve: 电磁阀 %s 检测到 DO 被外部改动 →%s，"
                        "接管为新动作（起算 %dms 超时）\n",
                        it->m_name, actual ? "开" : "关", st->m_moveTimeoutMs);
            }
        }

        /* [命令时刻的补写] setOutput 时拿不到 nowMs（纯业务层不查时钟），
         * 故在下一拍用调用方给的 nowMs 落定。 */
        if (it->m_pendingCmd) {
            it->m_cmdAtMs = nowMs;
            it->m_pendingCmd = false;
        }

        valveRefreshItem(it, nowMs, st->m_moveTimeoutMs, diValid, fresh);
        /* 状态更新后清除命令意图（已到位无需继续"动作中"）。
         *
         * [Fault **不得**在此清零] 超时判故障后若清掉 m_cmdAtMs，下一拍就会
         * 落进 valveRefreshItem 的 m_cmdAtMs==0 分支（"从未下过命令"）⇒
         * 状态从「故障」退回「未知」。现场表现是**告警闪一下就变未知**，
         * 运维看不到持续故障——而这恰恰是最需要持续显示的情形。
         * 超时判据自身幂等（nowMs 只增不减，下一拍照样判超时），
         * 且 m_timeoutReported 保证一次动作只告警一条，故无需清零。 */
        if (it->m_state == HsfValveState_Open ||
            it->m_state == HsfValveState_Closed) {
            it->m_cmdAtMs = 0;
        }
    }
}

/* ==================== 查询 ==================== */

int HsfServerIoValve_count(const HsfServerIoValve* st)
{
    return st ? st->m_count : 0;
}

bool HsfServerIoValve_ready(const HsfServerIoValve* st)
{
    return st && st->m_ready;
}

int HsfServerIoValve_readyCount(const HsfServerIoValve* st)
{
    int i;
    int n = 0;
    if (!st) return 0;
    for (i = 0; i < st->m_count; ++i)
        if (valveItemReady(&st->m_items[i])) ++n;
    return n;
}

const HsfServerIoValveItem* HsfServerIoValve_item(const HsfServerIoValve* st,
                                                  int index)
{
    if (!st || index < 0 || index >= st->m_count) return NULL;
    return &st->m_items[index];
}

bool HsfServerIoValve_takeAlarm(HsfServerIoValve* st, int index, int* outKind,
                               char* outText, int cap)
{
    HsfServerIoValveItem* it;

    if (outText && cap > 0) outText[0] = '\0';
    if (!st || index < 0 || index >= st->m_count) return false;
    it = &st->m_items[index];
    if (it->m_pendingAlarmKind == 0) return false;

    if (outKind) *outKind = it->m_pendingAlarmKind;
    if (outText && cap > 0)
        snprintf(outText, (size_t)cap, "%s", it->m_pendingAlarmText);
    /* 取走即消费：一条告警只推一次（见头文件说明）。 */
    it->m_pendingAlarmKind = 0;
    it->m_pendingAlarmText[0] = '\0';
    return true;
}

int HsfServerIoValve_faultCount(const HsfServerIoValve* st)
{
    int i;
    int n = 0;
    if (!st) return 0;
    for (i = 0; i < st->m_count; ++i)
        if (st->m_items[i].m_state == HsfValveState_Fault) ++n;
    return n;
}

int HsfServerIoValve_moveTimeoutMs(const HsfServerIoValve* st)
{
    return st ? st->m_moveTimeoutMs : 0;
}
