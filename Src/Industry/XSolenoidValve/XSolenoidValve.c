/**
 * @file       XSolenoidValve.c
 * @brief      电磁阀设备抽象实现。
 * @details    [2026-11-01 通道组重构] 只依赖通道组（XIoChannelGroup）+ 点
 *             id：动作出口=组内 DigitalOutput 点（写经组受理，落定经组
 *             writeDone(id, ok) 通知）；到位反馈=组内 DigitalInput 点（变化
 *             经组 inputChanged(id, v) 通知，重读经组 readDigitalInput）。
 *             阀位状态机 + 在途动作监督定时器 + 双限位矛盾检测全部在本文件。
 *             信号槽遵守框架红线：槽经 connect_1 订阅组信号，槽内按
 *             channelId 过滤本阀关注点。
 */
#include "XSolenoidValve.h"

#if XINDUSTRY_ON && XSOLENOID_VALVE_ON

#include <string.h>
#include "XMemory.h"

/* ==================== 内部辅助 ==================== */

#define XSOLENOIDVALVE_DEFAULT_TIMEOUT_MS 5000u

/* 订阅槽前向声明：内部解挂函数的断开坐标需要引用 */
static void VXSolenoidValve_onWriteDone(XObject* receiver, XVarList* args);
static void VXSolenoidValve_onLimitChanged(XObject* receiver, XVarList* args);

static void valve_setState(XSolenoidValve* valve, XSolenoidValveState state)
{
    if (valve->m_state == state) return;
    valve->m_state = state;
    XSolenoidValve_stateChanged_signal(valve, state);
}

static void valve_stopGuard(XSolenoidValve* valve)
{
    if (valve->m_guardTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)valve, valve->m_guardTimer);
        valve->m_guardTimer = XTIMER_INVALID_ID;
    }
}

static void valve_startGuard(XSolenoidValve* valve)
{
    if (valve->m_timeoutMs == 0u) return;
    valve->m_guardTimer = XObject_startTimer_ms((XObject*)valve,
                                                valve->m_timeoutMs,
                                                XTimerType_CoarseTimer);
    if (valve->m_guardTimer == XTIMER_INVALID_ID) {
        /* 监督资源启动失败：无法保证在途动作可收敛，按工业安全侧判故障 */
        XSolenoidValve_errorOccurred_signal(valve, XSolenoidValveError_Internal);
        valve_setState(valve, XSolenoidValveState_Fault);
    }
}

/* 到位收敛：停监督、落稳态并按方向发 opened/closed */
static void valve_arrive(XSolenoidValve* valve, XSolenoidValveState state)
{
    valve_stopGuard(valve);
    if (valve->m_state == state) return;
    valve->m_state = state;
    XSolenoidValve_stateChanged_signal(valve, state);
    if (state == XSolenoidValveState_Open)
        XSolenoidValve_opened_signal(valve);
    else if (state == XSolenoidValveState_Closed)
        XSolenoidValve_closed_signal(valve);
}

static void valve_fault(XSolenoidValve* valve, XSolenoidValveError error)
{
    valve_stopGuard(valve);
    XSolenoidValve_errorOccurred_signal(valve, (int)error);
    valve_setState(valve, XSolenoidValveState_Fault);
}

/* 重读双限位并重定位阀位：反馈变化/挂接播种共用的唯一判定路径 */
static void valve_reevaluate(XSolenoidValve* valve)
{
    bool openActive = false;
    bool closeActive = false;
    bool openOk = false;
    bool closeOk = false;

    if (valve->m_feedbackMode != XSolenoidValveFeedbackMode_DualLimit)
        return;
    if (!valve->m_group) return;
    if (valve->m_openLimitPt)
        openOk = XIoChannelGroup_readDigitalInput(valve->m_group,
                                                  XIoPoint_role(valve->m_openLimitPt),
                                                  &openActive);
    if (valve->m_closeLimitPt)
        closeOk = XIoChannelGroup_readDigitalInput(valve->m_group,
                                                   XIoPoint_role(valve->m_closeLimitPt),
                                                   &closeActive);

    if (openOk && closeOk && openActive && closeActive) {
        valve_fault(valve, XSolenoidValveError_LimitContradiction);
        return;
    }
    if (openOk && openActive) {
        valve_arrive(valve, XSolenoidValveState_Open);
        return;
    }
    if (closeOk && closeActive) {
        valve_arrive(valve, XSolenoidValveState_Closed);
        return;
    }
    /* 双限位均无效：在途动作交监督定时器收敛；稳态位置视为丢失 */
    if (valve->m_state == XSolenoidValveState_Open ||
        valve->m_state == XSolenoidValveState_Closed)
        valve_setState(valve, XSolenoidValveState_Unknown);
}

/* ==================== 内部解挂（deinit 与公开 detach 共用） ==================== */

static void valve_detachOutputInternal(XSolenoidValve* valve)
{
    if (!valve->m_group || !valve->m_outputPt) return;
    XObject_disconnect_1((XObject*)valve->m_group,
                         XSignal(XIoChannelGroup_writeDone_signal),
                         (XObject*)valve, VXSolenoidValve_onWriteDone);
    valve->m_outputPt = NULL;
    valve->m_group = NULL;
}

static bool valve_detachFeedbackInternal(XSolenoidValve* valve)
{
    bool detached = false;
    if (!valve->m_group) return false;
    if (valve->m_openLimitPt || valve->m_closeLimitPt) {
        XObject_disconnect_1((XObject*)valve->m_group,
                             XSignal(XIoChannelGroup_inputChanged_signal),
                             (XObject*)valve, VXSolenoidValve_onLimitChanged);
        valve->m_openLimitPt = NULL;
        valve->m_closeLimitPt = NULL;
        detached = true;
    }
    if (!valve->m_outputPt) valve->m_group = NULL; /* 出口也未挂：彻底解组。 */
    return detached;
}

/* ==================== 组信号槽（经 connect_1 连接，receiver=阀） ==================== */

/**
 * @brief 组写落定槽：按出口点 role 过滤本阀，None 口径下推进到位。
 * @param receiver 连接的接收者（即本阀）；**不是** 组，禁止直接强转。
 * @param args 信号参数（uint16_t role, bool ok）。
 * @return 无。
 */
static void VXSolenoidValve_onWriteDone(XObject* receiver, XVarList* args)
{
    XSolenoidValve* valve;

    if (!receiver || !args) return;
    valve = (XSolenoidValve*)receiver;
    XVarList_args_2(args, uint16_t, role, bool, ok);
    if (!valve->m_outputPt) return;
    if (role != XIoPoint_role(valve->m_outputPt)) return;

    if (!ok) {
        /* 异步写入失败只在在途动作上判故障；稳态下仅上报不动状态 */
        if (valve->m_state == XSolenoidValveState_Opening ||
            valve->m_state == XSolenoidValveState_Closing)
            valve_fault(valve, XSolenoidValveError_WriteFailed);
        else
            XSolenoidValve_errorOccurred_signal(valve, XSolenoidValveError_WriteFailed);
        return;
    }
    /* None 口径：出口确认即推断到位；DualLimit 口径等待限位 */
    if (valve->m_feedbackMode == XSolenoidValveFeedbackMode_None) {
        if (valve->m_state == XSolenoidValveState_Opening)
            valve_arrive(valve, XSolenoidValveState_Open);
        else if (valve->m_state == XSolenoidValveState_Closing)
            valve_arrive(valve, XSolenoidValveState_Closed);
    }
}

/**
 * @brief 组输入变化槽：按限位点 role 过滤本阀，统一重读双限位。
 * @param receiver 连接的接收者（即本阀）；**不是** 组，禁止直接强转。
 * @param args 信号参数（uint16_t role, int32_t value）。
 * @return 无。
 */
static void VXSolenoidValve_onLimitChanged(XObject* receiver, XVarList* args)
{
    XSolenoidValve* valve;

    if (!args) return;
    valve = (XSolenoidValve*)receiver;
    XVarList_args_2(args, uint16_t, role, int32_t, value);
    (void)value;
    if ((!valve->m_openLimitPt || role != XIoPoint_role(valve->m_openLimitPt)) &&
        (!valve->m_closeLimitPt || role != XIoPoint_role(valve->m_closeLimitPt)))
        return;
    /* 统一重读双限位：播种、到位、矛盾、位置丢失一条判定路径 */
    valve_reevaluate(valve);
}

static void VXSolenoidValve_timerEvent(XObject* obj, XTimerEvent* event)
{
    XSolenoidValve* valve = (XSolenoidValve*)obj;
    XTimerId id;

    if (!valve || !event) return;
    id = XTimerEvent_timerId(event);
    XEvent_accept(event);
    if (id == valve->m_guardTimer) {
        valve->m_guardTimer = XTIMER_INVALID_ID;
        XSolenoidValve_commandTimeout_signal(valve);
        valve_setState(valve, XSolenoidValveState_Fault);
        XSolenoidValve_errorOccurred_signal(valve, XSolenoidValveError_Timeout);
        return;
    }
    /* 未知 id 交父类，不吞 */
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))((XObject*)valve, event);
}

static void VXSolenoidValve_deinit(XObject* obj)
{
    XSolenoidValve* valve = (XSolenoidValve*)obj;

    if (!valve) return;
    valve_stopGuard(valve);
    valve_detachOutputInternal(valve);
    valve_detachFeedbackInternal(valve);
    XClass_Deinit_Parent(XObject, obj);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XSolenoidValve_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSolenoidValve)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXSolenoidValve_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSolenoidValve_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XSolenoidValve_init(XSolenoidValve* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XSolenoidValve));
    XObject_init((XObject*)obj);
    XClassSetVtable(obj, XSolenoidValve);
    obj->m_group = NULL;
    obj->m_outputPt = NULL;
    obj->m_openLimitPt = NULL;
    obj->m_closeLimitPt = NULL;
    obj->m_feedbackMode = XSolenoidValveFeedbackMode_None;
    obj->m_state = XSolenoidValveState_Unknown;
    obj->m_timeoutMs = XSOLENOIDVALVE_DEFAULT_TIMEOUT_MS;
    obj->m_guardTimer = XTIMER_INVALID_ID;
}

XSolenoidValve* XSolenoidValve_create(void)
{
    XSolenoidValve* obj = (XSolenoidValve*)XMemory_calloc(1, sizeof(XSolenoidValve), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XSolenoidValve_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 装配 ==================== */

bool XSolenoidValve_attachOutput(XSolenoidValve* valve, XIoChannelGroup* group,
                                 XIoPoint* outputPt)
{
    XConnection* conn;

    if (ISNULL(valve, "XSolenoidValve")) return false;
    if (!group || !outputPt) return false;
    if (valve->m_group == group && valve->m_outputPt == outputPt) return true;
    valve_detachOutputInternal(valve);
    valve->m_group = group;
    valve->m_outputPt = outputPt;
    conn = XObject_connect_1((XObject*)group,
                             XSignal(XIoChannelGroup_writeDone_signal),
                             (XObject*)valve, VXSolenoidValve_onWriteDone,
                             XConnectionType_Direct);
    if (!conn) {
        valve->m_group = NULL;
        valve->m_outputPt = NULL;
        return false;
    }
    valve_setState(valve, XSolenoidValveState_Unknown);
    return true;
}

bool XSolenoidValve_attachFeedback_2(XSolenoidValve* valve, XIoChannelGroup* group,
                                     XIoPoint* openLimitPt, XIoPoint* closeLimitPt)
{
    bool attached = false;

    if (ISNULL(valve, "XSolenoidValve")) return false;
    if (!group || (!openLimitPt && !closeLimitPt)) return false;
    valve_detachFeedbackInternal(valve);
    valve->m_group = group;
    if (openLimitPt) {
        if (!XObject_connect_1((XObject*)group,
                               XSignal(XIoChannelGroup_inputChanged_signal),
                               (XObject*)valve, VXSolenoidValve_onLimitChanged,
                               XConnectionType_Auto))
            return false;
        valve->m_openLimitPt = openLimitPt;
        attached = true;
    }
    if (closeLimitPt) {
        if (!XObject_connect_1((XObject*)group,
                               XSignal(XIoChannelGroup_inputChanged_signal),
                               (XObject*)valve, VXSolenoidValve_onLimitChanged,
            XConnectionType_Auto))
            return false;
        valve->m_closeLimitPt = closeLimitPt;
        attached = true;
    }
    if (attached)
        valve_reevaluate(valve);   /* 播种：同步读限位落初始阀位 */
    return attached;
}

bool XSolenoidValve_detachOutput(XSolenoidValve* valve)
{
    if (ISNULL(valve, "XSolenoidValve")) return false;
    if (!valve->m_group || !valve->m_outputPt) return false;
    valve_detachOutputInternal(valve);
    return true;
}

bool XSolenoidValve_detachFeedback(XSolenoidValve* valve)
{
    if (ISNULL(valve, "XSolenoidValve")) return false;
    return valve_detachFeedbackInternal(valve);
}

/* ==================== 配置 ==================== */

bool XSolenoidValve_setFeedbackMode(XSolenoidValve* valve,
                                    XSolenoidValveFeedbackMode mode)
{
    if (ISNULL(valve, "XSolenoidValve")) return false;
    if (mode != XSolenoidValveFeedbackMode_None &&
        mode != XSolenoidValveFeedbackMode_DualLimit)
        return false;
    valve->m_feedbackMode = mode;
    return true;
}

XSolenoidValveFeedbackMode XSolenoidValve_feedbackMode(const XSolenoidValve* valve)
{
    if (!valve) return XSolenoidValveFeedbackMode_None;
    return valve->m_feedbackMode;
}

bool XSolenoidValve_setTimeoutMs(XSolenoidValve* valve, uint32_t timeoutMs)
{
    if (ISNULL(valve, "XSolenoidValve")) return false;
    valve->m_timeoutMs = timeoutMs;
    return true;
}

uint32_t XSolenoidValve_timeoutMs(const XSolenoidValve* valve)
{
    if (!valve) return 0u;
    return valve->m_timeoutMs;
}

/* ==================== 动作 ==================== */

bool XSolenoidValve_open(XSolenoidValve* valve)
{
    if (ISNULL(valve, "XSolenoidValve")) return false;
    if (!valve->m_group || !valve->m_outputPt) {
        XSolenoidValve_errorOccurred_signal(valve, XSolenoidValveError_NotAttached);
        return false;
    }
    if (valve->m_state == XSolenoidValveState_Open) return true;
    /* 先落在途态、先挂监督，再驱动出口：同步通道在 write 调用栈内即完成
     * 到位确认（writeDone -> arrive），状态口径必须先于动作就绪 */
    valve_stopGuard(valve);
    valve_setState(valve, XSolenoidValveState_Opening);
    valve_startGuard(valve);
    if (valve->m_state == XSolenoidValveState_Fault)
        return false;   /* startGuard 失败已判故障 */
    if (!XIoChannelGroup_writeDigitalOutput(valve->m_group,
                                            XIoPoint_role(valve->m_outputPt), true)) {
        valve_stopGuard(valve);
        XSolenoidValve_errorOccurred_signal(valve, XSolenoidValveError_WriteRejected);
        valve_setState(valve, XSolenoidValveState_Fault);
        return false;
    }
    return true;
}

bool XSolenoidValve_close(XSolenoidValve* valve)
{
    if (ISNULL(valve, "XSolenoidValve")) return false;
    if (!valve->m_group || !valve->m_outputPt) {
        XSolenoidValve_errorOccurred_signal(valve, XSolenoidValveError_NotAttached);
        return false;
    }
    if (valve->m_state == XSolenoidValveState_Closed) return true;
    valve_stopGuard(valve);
    valve_setState(valve, XSolenoidValveState_Closing);
    valve_startGuard(valve);
    if (valve->m_state == XSolenoidValveState_Fault)
        return false;
    if (!XIoChannelGroup_writeDigitalOutput(valve->m_group,
                                            XIoPoint_role(valve->m_outputPt), false)) {
        valve_stopGuard(valve);
        XSolenoidValve_errorOccurred_signal(valve, XSolenoidValveError_WriteRejected);
        valve_setState(valve, XSolenoidValveState_Fault);
        return false;
    }
    return true;
}

/* ==================== 查询 ==================== */

XSolenoidValveState XSolenoidValve_state(const XSolenoidValve* valve)
{
    if (!valve) return XSolenoidValveState_Unknown;
    return valve->m_state;
}

bool XSolenoidValve_isFullyOpen(const XSolenoidValve* valve)
{
    return XSolenoidValve_state(valve) == XSolenoidValveState_Open;
}

bool XSolenoidValve_isFullyClosed(const XSolenoidValve* valve)
{
    return XSolenoidValve_state(valve) == XSolenoidValveState_Closed;
}

/* ==================== 信号 ==================== */

void* XSolenoidValve_stateChanged_signal(XSolenoidValve* valve,
                                         XSolenoidValveState state)
{
    XEmitSignal(valve, XSolenoidValve_stateChanged_signal,
                XVarList_Create(XVar(XSolenoidValveState, state)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XSolenoidValve_opened_signal(XSolenoidValve* valve)
{
    XEmitSignal(valve, XSolenoidValve_opened_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XSolenoidValve_closed_signal(XSolenoidValve* valve)
{
    XEmitSignal(valve, XSolenoidValve_closed_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XSolenoidValve_commandTimeout_signal(XSolenoidValve* valve)
{
    XEmitSignal(valve, XSolenoidValve_commandTimeout_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XSolenoidValve_errorOccurred_signal(XSolenoidValve* valve, int error)
{
    XEmitSignal(valve, XSolenoidValve_errorOccurred_signal,
                XVarList_Create(XVar(int, error)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

#endif /* XINDUSTRY_ON && XSOLENOID_VALVE_ON */
