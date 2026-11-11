/**
 * @file       XAutoControl.c
 * @brief      自动运行控制器实现：运行闸门/急停锁存/步进节流/记账/报警环形队列。
 * @details    纯框架件：不持有任何设备、通道、传输对象——本文件不 include
 *             任何设备/通道/协议头文件。工艺逻辑由使用者经
 *             XAutoController_setStepHandler 注册（其 ud 上下文自带设备
 *             适配层，见头文件「职责边界」与「设计约束」）。
 */
#include "XAutoControl.h"

#if XINDUSTRY_ON && XAUTO_CONTROL_ON

/* 裸名解析（同头文件）：include 清单含 XContainer/XQueue/XCircularQueue 目录，
 * 不依赖只有静态链才传播的 -I Src/XCore。 */
#include "XCircularQueue.h"
#include <string.h>
#include <stdio.h>

/* ==================== 内部辅助 ==================== */

#define XAUTO_CONTROL_DEFAULT_STEP_MS 50u

/**
 * @brief 状态迁移（带 stateChanged 信号；同值静默）。
 * @details 唯一改 m_state 的入口——started/stopped/eStopTriggered/resetDone
 *          都只是状态迁移的语义投影，时序一律"先语义信号、后 stateChanged"
 *          的镜像顺序由各迁移点保证。
 */
static void autoCtlSetState(XAutoController* ctl, XAutoControlState state)
{
    if (ctl->m_state == (int)state) return;
    ctl->m_state = (int)state;
    XAutoController_stateChanged_signal(ctl, (int)state);
}

/* ==================== 生命周期 ==================== */

static void VXAutoController_deinit(XObject* obj)
{
    XAutoController* ctl = (XAutoController*)obj;
    if (!ctl) return;
    /* clear 只清元素、不释放队列缓冲区；必须走 deinit（XCircularQueue 的
       析构虚函数）才能回收 init 时分配的 typeSize×(count+1) 缓冲。 */
    XClassDeinit(&ctl->m_alarms);
    XClass_Deinit_Parent(XObject, obj);
}

XVtable* XAutoController_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAutoController)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAutoController_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XAutoController);
    return XVTABLE_DEFAULT;
}

void XAutoController_init(XAutoController* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(*obj));
    XObject_init((XObject*)obj);
    XClassGetVtable(obj) = XAutoController_class_init();
    obj->m_state = (int)XAutoControlState_Stopped;
    obj->m_minStepMs = XAUTO_CONTROL_DEFAULT_STEP_MS;
    XCircularQueue_init(&obj->m_alarms, sizeof(XAutoControlAlarm),
                        XAUTO_CONTROL_ALARM_CAPACITY);
    XAutoController_setPhase(obj, "idle");
}

XAutoController* XAutoController_create(void)
{
    XAutoController* obj = (XAutoController*)XMemory_malloc(
        sizeof(XAutoController), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XAutoController_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 用户逻辑装配 ==================== */

bool XAutoController_setStepHandler(XAutoController* ctl,
                                    XAutoControlStepFn stepFn, void* ud)
{
    if (!ctl) return false;
    ctl->m_stepFn = stepFn;
    ctl->m_stepUd = stepFn ? ud : NULL;
    return true;
}

/* ==================== 配置 ==================== */

bool XAutoController_setMinStepIntervalMs(XAutoController* ctl, uint32_t intervalMs)
{
    if (!ctl) return false;
    ctl->m_minStepMs = intervalMs;
    return true;
}

uint32_t XAutoController_minStepIntervalMs(const XAutoController* ctl)
{
    return ctl ? ctl->m_minStepMs : 0u;
}

/* ==================== 生命周期（运行闸门：启动/停止/急停/复位） ==================== */

bool XAutoController_start(XAutoController* ctl, uint64_t nowMs)
{
    if (!ctl) return false;
    /* 急停锁存中拒绝启动：解除必须先复位（人显式确认现场安全）。 */
    if (ctl->m_state == (int)XAutoControlState_EmergencyStop) return false;
    if (ctl->m_state == (int)XAutoControlState_Running) return false;
    ctl->m_lastStepMs = 0;
    XAutoController_setPhase(ctl, "idle");
    autoCtlSetState(ctl, XAutoControlState_Running);
    XAutoController_started_signal(ctl);
    (void)nowMs;
    return true;
}

bool XAutoController_stop(XAutoController* ctl, uint64_t nowMs)
{
    if (!ctl) return false;
    /* 急停是"更强的停止"：普通停止不解除锁存、也不算一次新停止。 */
    if (ctl->m_state != (int)XAutoControlState_Running) return false;
    ctl->m_lastStepMs = 0;
    autoCtlSetState(ctl, XAutoControlState_Stopped);
    XAutoController_stopped_signal(ctl);
    (void)nowMs;
    return true;
}

bool XAutoController_eStop(XAutoController* ctl, uint64_t nowMs)
{
    if (!ctl) return false;
    if (ctl->m_state == (int)XAutoControlState_EmergencyStop) return false;
    ctl->m_lastStepMs = 0;
    autoCtlSetState(ctl, XAutoControlState_EmergencyStop);
    XAutoController_eStopTriggered_signal(ctl);
    /* 报警在信号之后：槽方（推送/日志）看到的在队快照已含本条。 */
    XAutoController_raiseAlarm(ctl, (int)XAutoControlAlarm_EmergencyStop,
                               "自动运行急停触发（等待人工复位）", nowMs);
    return true;
}

bool XAutoController_reset(XAutoController* ctl, uint64_t nowMs)
{
    if (!ctl) return false;
    if (ctl->m_state != (int)XAutoControlState_EmergencyStop) return false;
    ctl->m_lastStepMs = 0;
    autoCtlSetState(ctl, XAutoControlState_Stopped);
    XAutoController_resetDone_signal(ctl);
    (void)nowMs;
    return true;
}

bool XAutoController_estopped(const XAutoController* ctl)
{
    return ctl && ctl->m_state == (int)XAutoControlState_EmergencyStop;
}

XAutoControlState XAutoController_state(const XAutoController* ctl)
{
    return ctl ? (XAutoControlState)ctl->m_state : XAutoControlState_Stopped;
}

bool XAutoController_running(const XAutoController* ctl)
{
    return ctl && ctl->m_state == (int)XAutoControlState_Running;
}

void XAutoController_step(XAutoController* ctl, uint64_t nowMs)
{
    if (!ctl) return;
    if (ctl->m_state != (int)XAutoControlState_Running) return;
    if (!ctl->m_stepFn) return;
    if (ctl->m_lastStepMs != 0 && ctl->m_minStepMs != 0 &&
        (nowMs - ctl->m_lastStepMs) < (uint64_t)ctl->m_minStepMs)
        return;
    ctl->m_lastStepMs = nowMs;
    ctl->m_stepFn(ctl->m_stepUd, ctl, nowMs);
}

/* ==================== 相位 ==================== */

void XAutoController_setPhase(XAutoController* ctl, const char* phase)
{
    if (!ctl) return;
    snprintf(ctl->m_phase, sizeof(ctl->m_phase), "%s", phase ? phase : "");
}

const char* XAutoController_phase(const XAutoController* ctl)
{
    return ctl ? ctl->m_phase : "";
}

/* ==================== 节流 ==================== */

bool XAutoController_throttle(XAutoController* ctl, const char* key,
                              uint32_t intervalMs, uint64_t nowMs)
{
    int i;
    int freeSlot = -1;

    if (!ctl) return false;
    if (intervalMs == 0) return true;
    for (i = 0; i < XAUTO_CONTROL_THROTTLE_SLOTS; ++i) {
        if (ctl->m_throttle[i].m_key[0] == '\0') {
            if (freeSlot < 0) freeSlot = i;
            continue;
        }
        if (strncmp(ctl->m_throttle[i].m_key, key,
                    sizeof(ctl->m_throttle[i].m_key)) == 0) {
            uint64_t last = ctl->m_throttle[i].m_lastMs;
            if (last != 0 && (nowMs - last) < (uint64_t)intervalMs)
                return false;
            ctl->m_throttle[i].m_lastMs = nowMs;
            return true;
        }
    }
    /* 新 key：占空槽；表满直接放行（安全侧，见头文件说明）。 */
    if (freeSlot < 0) {
        snprintf(ctl->m_lastError, sizeof(ctl->m_lastError),
                 "节流表已满（%d 槽），动作未经节流放行",
                 XAUTO_CONTROL_THROTTLE_SLOTS);
        return true;
    }
    snprintf(ctl->m_throttle[freeSlot].m_key,
             sizeof(ctl->m_throttle[freeSlot].m_key), "%s", key ? key : "");
    ctl->m_throttle[freeSlot].m_lastMs = nowMs;
    return true;
}

/* ==================== 动作记账 ==================== */

void XAutoController_noteAction(XAutoController* ctl, bool ok, const char* errUtf8)
{
    if (!ctl) return;
    if (ok) {
        ++ctl->m_actions;
        ctl->m_lastError[0] = '\0';
        return;
    }
    snprintf(ctl->m_lastError, sizeof(ctl->m_lastError), "%s",
             errUtf8 ? errUtf8 : "");
}

/* ==================== 报警面 ==================== */

bool XAutoController_raiseAlarm(XAutoController* ctl, int kind,
                                const char* text, uint64_t nowMs)
{
    XAutoControlAlarm item;
    XAutoControlAlarm dropped;

    if (!ctl) return false;
    /* 满挤最旧：先出队丢弃最老一条再入队（远端只丢最老历史，不丢最新告警）。 */
    if (XCircularQueue_count_base(&ctl->m_alarms) >= XAUTO_CONTROL_ALARM_CAPACITY)
        XCircularQueue_receive_base(&ctl->m_alarms, &dropped);
    memset(&item, 0, sizeof(item));
    item.m_kind = kind;
    item.m_atMs = nowMs;
    snprintf(item.m_text, sizeof(item.m_text), "%s", text ? text : "");
    if (!XCircularQueue_push_base(&ctl->m_alarms, &item))
        return false;
    XAutoController_alarmRaised_signal(ctl, kind, (int64_t)nowMs, item.m_text);
    return true;
}


bool XAutoController_takeAlarm(XAutoController* ctl, XAutoControlAlarm* outAlarm)
{
    if (!ctl) return false;
    return XCircularQueue_receive_base(&ctl->m_alarms, outAlarm);
}

int XAutoController_pendingAlarmCount(const XAutoController* ctl)
{
    return ctl ? (int)XCircularQueue_count_base(&ctl->m_alarms) : 0;
}


/* ==================== 查询 ==================== */

int64_t XAutoController_actionCount(const XAutoController* ctl)
{
    return ctl ? ctl->m_actions : 0;
}

const char* XAutoController_lastError(const XAutoController* ctl)
{
    return ctl ? ctl->m_lastError : "";
}

void XAutoController_status(const XAutoController* ctl, XAutoControlStatus* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!ctl) return;
    out->m_running = XAutoController_running(ctl);
    out->m_state = (int)XAutoController_state(ctl);
    snprintf(out->m_phase, sizeof(out->m_phase), "%s", ctl->m_phase);
    out->m_actions = ctl->m_actions;
    snprintf(out->m_lastError, sizeof(out->m_lastError), "%s", ctl->m_lastError);
    out->m_stepIntervalMs = ctl->m_minStepMs;
    out->m_pendingAlarms = (int)XCircularQueue_count_base(&ctl->m_alarms);
}

/* ==================== 信号 ==================== */

void* XAutoController_started_signal(XAutoController* ctl)
{
    XEmitSignal(ctl, XAutoController_started_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XAutoController_stopped_signal(XAutoController* ctl)
{
    XEmitSignal(ctl, XAutoController_stopped_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XAutoController_eStopTriggered_signal(XAutoController* ctl)
{
    XEmitSignal(ctl, XAutoController_eStopTriggered_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XAutoController_resetDone_signal(XAutoController* ctl)
{
    XEmitSignal(ctl, XAutoController_resetDone_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XAutoController_stateChanged_signal(XAutoController* ctl, int state)
{
    XEmitSignal(ctl, XAutoController_stateChanged_signal,
                XVarList_Create(XVar(int, state)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XAutoController_alarmRaised_signal(XAutoController* ctl, int kind,
                                         int64_t atMs, const char* text)
{
    XEmitSignal(ctl, XAutoController_alarmRaised_signal,
                XVarList_Create(XVar(int, kind), XVar(int64_t, atMs),
                                XVar(const char*, text)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return NULL;
}

#endif /* XINDUSTRY_ON && XAUTO_CONTROL_ON */
