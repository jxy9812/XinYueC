/**
 * @file       XSolenoidValve.c
 * @brief      电磁阀设备抽象实现。
 * @details    只依赖通道接口族（XDigitalOutput/XDigitalInput），不含任何
 *             具体传输逻辑；阀位状态机 + 在途动作监督定时器 + 双限位矛盾
 *             检测全部在本文件。信号槽遵守框架红线：槽经 connect_1 订阅，
 *             槽内以 XObject_sender(receiver) 取回真正的信号发送者比对，
 *             禁止把槽第 1 参强转成通道类型。
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
    if (valve->m_openLimit)
        openOk = XDigitalInput_read_base(valve->m_openLimit, &openActive);
    if (valve->m_closeLimit)
        closeOk = XDigitalInput_read_base(valve->m_closeLimit, &closeActive);

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
    if (!valve->m_output) return;
    XObject_disconnect_1((XObject*)valve->m_output,
                         XSignal(XDigitalOutput_writeDone_signal),
                         (XObject*)valve, VXSolenoidValve_onWriteDone);
    valve->m_output = NULL;
}

static bool valve_detachFeedbackInternal(XSolenoidValve* valve)
{
    bool detached = false;
    if (valve->m_openLimit) {
        XObject_disconnect_1((XObject*)valve->m_openLimit,
                             XSignal(XDigitalInput_inputChanged_signal),
                             (XObject*)valve, VXSolenoidValve_onLimitChanged);
        valve->m_openLimit = NULL;
        detached = true;
    }
    if (valve->m_closeLimit) {
        XObject_disconnect_1((XObject*)valve->m_closeLimit,
                             XSignal(XDigitalInput_inputChanged_signal),
                             (XObject*)valve, VXSolenoidValve_onLimitChanged);
        valve->m_closeLimit = NULL;
        detached = true;
    }
    return detached;
}

/* ==================== 虚函数实现（static，仅供 class_init 注册） ==================== */

/* 动作出口 writeDone 订阅槽：第 1 参是 receiver（本阀），sender 经
 * XObject_sender 取回——框架信号槽红线（2026-10-25 实证陷阱）。 */
static void VXSolenoidValve_onWriteDone(XObject* receiver, XVarList* args)
{
    XSolenoidValve* valve;
    XObject* sender;
    bool ok = false;

    if (!receiver || !args) return;
    valve = (XSolenoidValve*)receiver;
    sender = XObject_sender(receiver);
    if (!sender || sender != (XObject*)valve->m_output) return;

    XVarList_start(args);
    ok = XVarList_arg(args, bool);

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

/* 到位反馈 inputChanged 订阅槽：sender 比对过滤非本阀反馈 */
static void VXSolenoidValve_onLimitChanged(XObject* receiver, XVarList* args)
{
    XSolenoidValve* valve;
    XObject* sender;

    (void)args;
    if (!receiver) return;
    valve = (XSolenoidValve*)receiver;
    sender = XObject_sender(receiver);
    if (!sender) return;
    if (sender != (XObject*)valve->m_openLimit &&
        sender != (XObject*)valve->m_closeLimit)
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
    obj->m_output = NULL;
    obj->m_openLimit = NULL;
    obj->m_closeLimit = NULL;
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

bool XSolenoidValve_attachOutput(XSolenoidValve* valve, XDigitalOutput* output)
{
    XConnection* conn;

    if (ISNULL(valve, "XSolenoidValve")) return false;
    if (!output) return false;
    if (valve->m_output) valve_detachOutputInternal(valve);
    valve->m_output = output;
    conn = XObject_connect_1((XObject*)output,
                             XSignal(XDigitalOutput_writeDone_signal),
                             (XObject*)valve, VXSolenoidValve_onWriteDone,
                             XConnectionType_Direct);
    if (!conn) {
        valve->m_output = NULL;
        return false;
    }
    valve_setState(valve, XSolenoidValveState_Unknown);
    return true;
}

bool XSolenoidValve_attachFeedback_2(XSolenoidValve* valve,
                                     XDigitalInput* openLimit,
                                     XDigitalInput* closeLimit)
{
    bool attached = false;

    if (ISNULL(valve, "XSolenoidValve")) return false;
    if (!openLimit && !closeLimit) return false;
    valve_detachFeedbackInternal(valve);
    if (openLimit) {
        if (!XObject_connect_1((XObject*)openLimit,
                               XSignal(XDigitalInput_inputChanged_signal),
                               (XObject*)valve, VXSolenoidValve_onLimitChanged,
                               XConnectionType_Direct))
            return false;
        valve->m_openLimit = openLimit;
        attached = true;
    }
    if (closeLimit) {
        if (!XObject_connect_1((XObject*)closeLimit,
                               XSignal(XDigitalInput_inputChanged_signal),
                               (XObject*)valve, VXSolenoidValve_onLimitChanged,
                               XConnectionType_Direct))
            return false;
        valve->m_closeLimit = closeLimit;
        attached = true;
    }
    if (attached)
        valve_reevaluate(valve);   /* 播种：同步读限位落初始阀位 */
    return attached;
}

bool XSolenoidValve_detachOutput(XSolenoidValve* valve)
{
    if (ISNULL(valve, "XSolenoidValve")) return false;
    if (!valve->m_output) return false;
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
    if (!valve->m_output) {
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
    if (!XDigitalOutput_write_base(valve->m_output, true)) {
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
    if (!valve->m_output) {
        XSolenoidValve_errorOccurred_signal(valve, XSolenoidValveError_NotAttached);
        return false;
    }
    if (valve->m_state == XSolenoidValveState_Closed) return true;
    valve_stopGuard(valve);
    valve_setState(valve, XSolenoidValveState_Closing);
    valve_startGuard(valve);
    if (valve->m_state == XSolenoidValveState_Fault)
        return false;
    if (!XDigitalOutput_write_base(valve->m_output, false)) {
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
