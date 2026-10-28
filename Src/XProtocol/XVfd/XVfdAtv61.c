/**
 * @file XVfdAtv61.c
 * @brief 施耐德 ATV61 变频器子类实现。
 * @details 资产平移自原 XModbusAtv61.c（已删除）：CiA402 启动序列、状态解析、
 *          109 条故障表；轮询/参数通道/单飞收口由 XVfdModbusBase 引擎承担。
 */
#include "XVfdAtv61.h"
#if XVFD_ON
#if XVFD_ATV61_ON

#include "XVfdModbusBase_Protected.h"
#include "XVfdDevice_Protected.h"
#include "XVfdLink.h"
#include "XTimer.h"
#include <string.h>
#include <math.h>

/* =============== 内部阶段机 =============== */
#define STAGE_NONE        0   /**< 空闲 */
#define STAGE_READ_ETA    1   /**< 只读 3201（启动序列判状态） */
#define STAGE_WRITE_CMD   2   /**< 写 8501（命令字） */

/** @brief 启动序列每步等待（毫秒） */
#define ATV61_STEP_DELAY_MS 120
/** @brief 启动序列最大步数 */
#define ATV61_START_MAX_STEP 6

/* =============== 故障码表 ===============
 * 数值与字母码取自施耐德官方《ATV610 通信参数表》LFT 枚举（EAV64394），
 * 与 ATV61/ATV71 同属一个逻辑地址家族，故障字母码一致。
 * 现场若遇不一致，以 HMI [1.10 诊断] 当前故障列表为准并修订本表。 */
typedef struct XVfdAtv61FaultEntry {
    uint16_t m_code;      /**< LFT 寄存器数值 */
    const char* m_name;   /**< ATV61 故障缩写 */
    const char* m_text;   /**< 中文说明 */
} XVfdAtv61FaultEntry;

static const XVfdAtv61FaultEntry s_atv61Faults[] = {
    { 0, "NOF", "无故障" },
    { 2, "EEF1", "控制存储器故障" },
    { 3, "CFF", "配置不正确" },
    { 4, "CFI", "配置无效" },
    { 5, "SLF1", "Modbus 通信中断" },
    { 6, "ILF", "内部通信中断（选件卡）" },
    { 7, "CNF", "现场总线通信中断" },
    { 8, "EPF1", "外部故障" },
    { 9, "OCF", "过流" },
    { 10, "CRF1", "预充电故障" },
    { 13, "LFF2", "AI2 4-20mA 信号丢失" },
    { 15, "IHF", "输入过热" },
    { 16, "OHF", "变频器过热" },
    { 17, "OLF", "电机过载" },
    { 18, "OBF", "直流母线过压" },
    { 19, "OSF", "电源过压" },
    { 20, "OPF1", "输出缺 1 相" },
    { 21, "PHF", "输入主电源故障" },
    { 22, "USF", "电源欠压" },
    { 23, "SCF1", "电机短路" },
    { 24, "SOF", "超速" },
    { 25, "TNF", "自整定故障" },
    { 26, "INF1", "内部故障 1（额定值）" },
    { 27, "INF2", "内部故障 2（软件）" },
    { 28, "INF3", "内部故障 3（内部通信）" },
    { 29, "INF4", "内部故障 4（生产）" },
    { 30, "EEF2", "功率存储器故障" },
    { 32, "SCF3", "接地短路" },
    { 33, "OPF2", "电机缺 3 相" },
    { 34, "COF", "CANopen 通信中断" },
    { 37, "INF7", "内部故障 7（初始化）" },
    { 38, "EPF2", "现场总线检测到的外部故障" },
    { 40, "INF8", "内部故障 8（开关电源）" },
    { 42, "SLF2", "PowerSuite 通信中断" },
    { 45, "SLF3", "图形终端通信中断" },
    { 51, "INF9", "内部故障 9（电流测量）" },
    { 52, "INFA", "内部故障 10（主电源）" },
    { 53, "INFB", "内部故障 11（温度）" },
    { 54, "TJF", "IGBT 过热" },
    { 55, "SCF4", "IGBT 短路" },
    { 56, "SCF5", "电机短路" },
    { 60, "INFC", "内部故障 12（内部电流源）" },
    { 64, "LCF", "输入接触器故障" },
    { 68, "INF6", "内部故障 6（选件）" },
    { 69, "INFE", "内部故障 14（CPU）" },
    { 71, "LFF3", "AI3 4-20mA 信号丢失" },
    { 72, "LFF4", "AI4 4-20mA 信号丢失" },
    { 73, "HCF", "板卡不兼容" },
    { 77, "CFI2", "配置传送故障" },
    { 79, "LFF5", "AI5 4-20mA 信号丢失" },
    { 99, "CSF", "通道切换故障" },
    { 100, "ULF", "过程欠载" },
    { 101, "OLC", "过程过载" },
    { 105, "ASF", "角度故障" },
    { 106, "LFF1", "AI1 4-20mA 信号丢失" },
    { 107, "SAFF", "安全功能故障" },
    { 110, "TH2F", "AI2 热保护等级故障" },
    { 111, "T2CF", "AI2 热敏电阻故障" },
    { 112, "TH3F", "AI3 热保护等级故障" },
    { 113, "T3CF", "AI3 热敏电阻故障" },
    { 114, "PCPF", "泵循环启动故障" },
    { 115, "OPLF", "出口压力低" },
    { 116, "HFPF", "高流量故障" },
    { 117, "IPPF", "入口压力故障" },
    { 119, "PLFF", "泵低流量故障" },
    { 120, "TH4F", "AI4 热保护等级故障" },
    { 121, "T4CF", "AI4 热敏电阻故障" },
    { 122, "TH5F", "AI5 热保护等级故障" },
    { 123, "T5CF", "AI5 热敏电阻故障" },
    { 124, "JAMF", "防堵故障" },
    { 125, "OPHF", "出口压力高" },
    { 126, "DRYF", "干运转故障" },
    { 127, "PFMF", "PID 反馈故障" },
    { 128, "PGLF", "程序加载故障" },
    { 129, "PGRF", "程序运行故障" },
    { 130, "MPLF", "主泵不可用" },
    { 131, "LCLF", "低液位故障" },
    { 132, "LCHF", "高液位故障" },
    { 142, "INFG", "内部故障 16（IO 模块-继电器）" },
    { 143, "INFH", "内部故障 17（IO 模块-标准）" },
    { 144, "INF0", "内部故障 0（IPC）" },
    { 146, "INFD", "内部故障 13（差动电流）" },
    { 148, "STF", "电机堵转故障" },
    { 149, "INFL", "内部故障 21（RTC）" },
    { 150, "ETHF", "以太网通信中断" },
    { 151, "INFF", "内部故障 15（Flash）" },
    { 152, "FWER", "固件升级故障" },
    { 153, "INFM", "内部故障 22（内置以太网）" },
    { 154, "INFP", "内部故障 25（控制板不兼容）" },
    { 155, "INFK", "内部故障 20（选件接口 PCBA）" },
    { 157, "INFR", "内部故障 27（诊断 CPLD）" },
    { 158, "INFN", "内部故障 23（模块链路）" },
    { 159, "SCF6", "AFE 短路" },
    { 160, "OBF2", "AFE 母线不平衡" },
    { 161, "INFS", "内部故障 28（AFE）" },
    { 173, "CHF", "柜内过热" },
    { 174, "URF", "AFE 电源欠压" },
    { 175, "INFV", "内部故障 31（缺功率砖）" },
    { 176, "INFT", "内部故障 29（逆变器）" },
    { 177, "INFU", "内部故障 30（整流器）" },
    { 179, "TJF2", "AFE IGBT 过热" },
    { 180, "CRF3", "AFE 接触器反馈故障" },
    { 181, "CFI3", "预设值传送故障" },
    { 182, "CBF", "断路器故障" },
    { 200, "FDR1", "FDR 内置以太网故障" },
    { 201, "FDR2", "FDR 以太网模块故障" },
    { 206, "DCRE", "直流母线纹波故障" },
    { 208, "IDLF", "空闲模式退出故障" },
    { 220, "AFLF", "高级功能被锁定" }
};

#define ATV61_FAULT_COUNT (sizeof(s_atv61Faults) / sizeof(s_atv61Faults[0]))

/* =============== 前置声明 =============== */
static void atv61StartTimerStart(XVfdAtv61* dev, int delayMs);
static void atv61StartTimerStop(XVfdAtv61* dev);
static void atv61OnWriteDone(XObject* owner, void* userData, bool ok, const XModbusDataUnit* result);
static void atv61OnEtaDone(XObject* owner, void* userData, bool ok, const XModbusDataUnit* result);

/* =============== 状态解析 =============== */

/** @brief 由 ETA/ETI 刷新快照运行态字段 */
static void atv61ApplyEta(XVfdAtv61* dev, uint16_t eta, uint16_t eti)
{
    XVfdStatus* st = XVfdDevice_status(&dev->m_base.m_base);
    uint8_t state = (uint8_t)(eta & XVfdAtv61_EtaMask);
    st->m_rawStateWord = eta;
    st->m_validMask |= XVfdField_RawStateWord;
    st->m_running = (eta & XVfdAtv61_EtaOperationEnabled) != 0;
    st->m_faulted = (eta & XVfdAtv61_EtaFault) != 0;
    st->m_warning = (eta & XVfdAtv61_EtaWarning) != 0;
    st->m_reverse = (eta & XVfdAtv61_EtaReverse) != 0;
    st->m_targetReached = (eta & XVfdAtv61_EtaTargetReached) != 0;
    st->m_remote = (eta & XVfdAtv61_EtaRemote) != 0;
    st->m_dcBraking = (eti & XVfdAtv61_EtiDcBrake) != 0;
    st->m_validMask |= XVfdField_Running | XVfdField_Faulted | XVfdField_Warning
                     | XVfdField_Reverse | XVfdField_TargetReached | XVfdField_Remote
                     | XVfdField_DcBraking;
    switch (state) {
    case XVfdAtv61_EtaStateDisabled: st->m_runState = XVfdRunState_Stopped; break;
    case XVfdAtv61_EtaStateReady:
    case XVfdAtv61_EtaStateSwitched: st->m_runState = XVfdRunState_Ready; break;
    case XVfdAtv61_EtaStateRunning:
        if (eti & XVfdAtv61_EtiAccel) st->m_runState = XVfdRunState_Accelerating;
        else if (eti & XVfdAtv61_EtiDecel) st->m_runState = XVfdRunState_Decelerating;
        else st->m_runState = st->m_reverse ? XVfdRunState_RunningReverse : XVfdRunState_RunningForward;
        break;
    case XVfdAtv61_EtaStateQuickStop: st->m_runState = XVfdRunState_QuickStopping; break;
    default: st->m_runState = st->m_faulted ? XVfdRunState_Fault : XVfdRunState_Unknown; break;
    }
    st->m_validMask |= XVfdField_RunState;
    st->m_faultCode = st->m_faulted ? st->m_faultCode : 0;
}

/** @brief 轮询组 1 解析：3201..3209（ETA RFR FRH LCR OTR ETI ULN UOP THD） */
static void atv61ParseMain(XObject* owner, const XModbusDataUnit* result)
{
    XVfdAtv61* dev = (XVfdAtv61*)owner;
    XVfdStatus* st;
    size_t n;
    if (!dev || !result) return;
    st = XVfdDevice_status(&dev->m_base.m_base);
    n = XModbusDataUnit_valueCount(result);
    if (n < 9) return;
    st->m_outputFreqHz = (int16_t)XModbusDataUnit_value(result, 1) / 10.0;
    st->m_setFreqHz = (int16_t)XModbusDataUnit_value(result, 2) / 10.0;
    st->m_outputCurrentA = XModbusDataUnit_value(result, 3);
    st->m_torquePercent = (int16_t)XModbusDataUnit_value(result, 4) / 10.0;
    st->m_mainsVoltageV = XModbusDataUnit_value(result, 6) / 10.0;
    st->m_outputVoltageV = XModbusDataUnit_value(result, 7);
    st->m_driveThermalPercent = XModbusDataUnit_value(result, 8);
    st->m_validMask |= XVfdField_OutputFreqHz | XVfdField_SetFreqHz | XVfdField_OutputCurrent
                     | XVfdField_TorquePercent | XVfdField_MainsVoltage | XVfdField_OutputVoltage
                     | XVfdField_DriveThermalPercent;
    atv61ApplyEta(dev, XModbusDataUnit_value(result, 0), XModbusDataUnit_value(result, 5));
}

/** @brief 轮询组 2 解析：7121（最近故障代码） */
static void atv61ParseFault(XObject* owner, const XModbusDataUnit* result)
{
    XVfdAtv61* dev = (XVfdAtv61*)owner;
    XVfdStatus* st;
    if (!dev || !result) return;
    st = XVfdDevice_status(&dev->m_base.m_base);
    st->m_faultCode = XModbusDataUnit_value(result, 0);
    st->m_validMask |= XVfdField_FaultCode;
}

/** @brief 轮询组 3 解析：9630（电机热状态） */
static void atv61ParseThermal(XObject* owner, const XModbusDataUnit* result)
{
    XVfdAtv61* dev = (XVfdAtv61*)owner;
    XVfdStatus* st;
    if (!dev || !result) return;
    st = XVfdDevice_status(&dev->m_base.m_base);
    st->m_motorThermalPercent = XModbusDataUnit_value(result, 0);
    st->m_validMask |= XVfdField_MotorThermalPercent;
}

/* =============== CiA402 启动序列 =============== */

/** @brief 由当前 ETA 推导下一步命令字 */
static uint16_t atv61NextCmd(uint16_t eta, bool reverse)
{
    uint16_t cmd;
    uint8_t state = (uint8_t)(eta & XVfdAtv61_EtaMask);
    switch (state) {
    case XVfdAtv61_EtaStateDisabled: cmd = XVfdAtv61_CmdShutdown; break;
    case XVfdAtv61_EtaStateReady:    cmd = XVfdAtv61_CmdSwitchOnState; break;
    case XVfdAtv61_EtaStateSwitched: cmd = XVfdAtv61_CmdRun; break;
    default:                         cmd = XVfdAtv61_CmdRun; break;
    }
    if (reverse) cmd |= XVfdAtv61_CmdReverse;
    return cmd;
}

static void atv61StartTimerStop(XVfdAtv61* dev)
{
    if (!dev) return;
    if (dev->m_stepTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)dev, dev->m_stepTimer);
        dev->m_stepTimer = XTIMER_INVALID_ID;
    }
}

static void atv61StartTimerStart(XVfdAtv61* dev, int delayMs)
{
    atv61StartTimerStop(dev);
    if (delayMs <= 0) delayMs = 1;
    dev->m_stepTimer = XObject_startTimer_ms((XObject*)dev,
        (uint64_t)delayMs, XTimerType_CoarseTimer);
}

/** @brief 启动序列：读 ETA → 推进命令字 → 等待 → 再读，至运行或超步数 */
static void atv61OnEtaDone(XObject* owner, void* userData, bool ok, const XModbusDataUnit* result)
{
    XVfdAtv61* dev = (XVfdAtv61*)owner;
    uint16_t eta;
    (void)userData;
    if (!dev) return;
    if (!ok || !result) {
        XVfdDevice_emitError(&dev->m_base.m_base, "启动序列读取状态失败");
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return;
    }
    eta = XModbusDataUnit_value(result, 0);
    /* 启动序列只读 ETA 单点，ETI 未知按 0 处理（不影响状态机推进判定） */
    atv61ApplyEta(dev, eta, 0);
    if (XVfdDevice_status(&dev->m_base.m_base)->m_faulted) {
        XVfdDevice_emitError(&dev->m_base.m_base, "变频器处于故障态，请先复位故障");
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return;
    }
    if (XVfdDevice_status(&dev->m_base.m_base)->m_running) {
        XVfdModbusBase_finishAction(&dev->m_base, true);
        return;
    }
    if (dev->m_step >= ATV61_START_MAX_STEP) {
        XVfdDevice_emitError(&dev->m_base.m_base, "启动超时：变频器未进入运行状态");
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return;
    }
    dev->m_pendingCmd = atv61NextCmd(eta, dev->m_reverse);
    ++dev->m_step;
    dev->m_stage = STAGE_WRITE_CMD;
    if (!XVfdModbusBase_submitWriteSingle(&dev->m_base, XVfdAtv61_AddrCmd,
                                          dev->m_pendingCmd, atv61OnWriteDone, NULL, XModbusRequestPriority_High)) {
        XVfdModbusBase_finishAction(&dev->m_base, false);
    }
}

static void atv61OnWriteDone(XObject* owner, void* userData, bool ok, const XModbusDataUnit* result)
{
    XVfdAtv61* dev = (XVfdAtv61*)owner;
    (void)userData;
    (void)result;
    if (!dev) return;
    if (!ok) {
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return;
    }
    if (XVfdModbusBase_currentOp(&dev->m_base) == XVfdOperation_ResetFault && dev->m_pendingReset) {
        /* 故障复位第二步：写 Disable voltage 归位 */
        dev->m_pendingReset = false;
        dev->m_stage = STAGE_WRITE_CMD;
        if (!XVfdModbusBase_submitWriteSingle(&dev->m_base, XVfdAtv61_AddrCmd,
                                              XVfdAtv61_CmdDisableVoltage,
                                              atv61OnWriteDone, NULL, XModbusRequestPriority_High)) {
            XVfdModbusBase_finishAction(&dev->m_base, false);
        }
        return;
    }
    if (dev->m_stage == STAGE_WRITE_CMD && XVfdModbusBase_currentOp(&dev->m_base) == XVfdOperation_Start) {
        /* 等待状态迁移后继续读 ETA 推进 */
        atv61StartTimerStart(dev, ATV61_STEP_DELAY_MS);
        return;
    }
    XVfdModbusBase_finishAction(&dev->m_base, true);
}

/** @brief 步进定时器到期：继续读 ETA（启动序列）或收口（复位归位） */
static void VXVfdAtv61_timerEvent(XObject* obj, XTimerEvent* event)
{
    XVfdAtv61* dev = (XVfdAtv61*)obj;
    if (!obj || !event) goto parent_call;
    if (XTimerEvent_timerId(event) != dev->m_stepTimer) goto parent_call;
    XEvent_accept(event);
    dev->m_stepTimer = XTIMER_INVALID_ID;
    if (XVfdModbusBase_currentOp(&dev->m_base) == XVfdOperation_Start) {
        dev->m_stage = STAGE_READ_ETA;
        if (!XVfdModbusBase_submitRead(&dev->m_base, XVfdAtv61_AddrEta, 1,
                                       atv61OnEtaDone, NULL, XModbusRequestPriority_High)) {
            XVfdModbusBase_finishAction(&dev->m_base, false);
        }
        return;
    }
    XVfdModbusBase_finishAction(&dev->m_base, true);
    return;

parent_call:
    /* 未知定时器 id 交父类链处理：第一参=直接父类（XClass_Parent 语义） */
    XClass_Parent(XVfdModbusBase, EXObject_TimerEvent, void (*)(XObject*, XTimerEvent*))(obj, event);
}

/* =============== 单写动作收口（简单动作共用） =============== */
static void atv61SimpleWriteDone(XObject* owner, void* userData, bool ok, const XModbusDataUnit* result)
{
    (void)userData;
    (void)result;
    if (!owner) return;
    XVfdModbusBase_finishAction((XVfdModbusBase*)owner, ok);
}

/* =============== 业务虚函数实现（重载 XVfdDevice 槽） =============== */
static bool VXVfdAtv61_Start(XVfdDevice* d, bool reverse)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
{
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_Start,
                                            reverse, 0, 0.0, 0.0);
        if (en != 0) return en > 0;
    }
    if (!XVfdModbusBase_busyStart(&dev->m_base, XVfdOperation_Start)) return false;
    dev->m_step = 0;
    dev->m_reverse = reverse;
    dev->m_stage = STAGE_READ_ETA;
    if (!XVfdModbusBase_submitRead(&dev->m_base, XVfdAtv61_AddrEta, 1, atv61OnEtaDone, NULL, XModbusRequestPriority_High)) {
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return false;
    }
    return true;
}

static bool VXVfdAtv61_Stop(XVfdDevice* d)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
{
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_Stop,
                                            false, 0, 0.0, 0.0);
        if (en != 0) return en > 0;
    }
    if (!XVfdModbusBase_busyStart(&dev->m_base, XVfdOperation_Stop)) return false;
    if (!XVfdModbusBase_submitWriteSingle(&dev->m_base, XVfdAtv61_AddrCmd,
                                          XVfdAtv61_CmdDisableOperation, atv61SimpleWriteDone, NULL, XModbusRequestPriority_High)) {
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return false;
    }
    return true;
}

static bool VXVfdAtv61_FreeStop(XVfdDevice* d)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
{
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_FreeStop,
                                            false, 0, 0.0, 0.0);
        if (en != 0) return en > 0;
    }
    if (!XVfdModbusBase_busyStart(&dev->m_base, XVfdOperation_FreeStop)) return false;
    if (!XVfdModbusBase_submitWriteSingle(&dev->m_base, XVfdAtv61_AddrCmd,
                                          XVfdAtv61_CmdDisableVoltage, atv61SimpleWriteDone, NULL, XModbusRequestPriority_High)) {
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return false;
    }
    return true;
}

static bool VXVfdAtv61_QuickStop(XVfdDevice* d)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
{
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_QuickStop,
                                            false, 0, 0.0, 0.0);
        if (en != 0) return en > 0;
    }
    if (!XVfdModbusBase_busyStart(&dev->m_base, XVfdOperation_QuickStop)) return false;
    if (!XVfdModbusBase_submitWriteSingle(&dev->m_base, XVfdAtv61_AddrCmd,
                                          XVfdAtv61_CmdQuickStopCmd, atv61SimpleWriteDone, NULL, XModbusRequestPriority_High)) {
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return false;
    }
    return true;
}

static bool VXVfdAtv61_SetFrequency(XVfdDevice* d, double hz)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
    double scaled;
    {
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_SetFrequency,
                                            false, 0, hz, 0.0);
        if (en != 0) return en > 0;
    }
    if (dev->m_refMode == XVfdAtv61_RefMode_SpeedRpm) {
        d->m_lastError = XVfdError_InvalidParam;
        return false;
    }
    if (!XVfdModbusBase_busyStart(&dev->m_base, XVfdOperation_SetFrequency)) return false;
    scaled = hz * 10.0; /* 0.1 Hz 定点 */
    if (scaled > 32767.0) scaled = 32767.0;
    if (scaled < -32768.0) scaled = -32768.0;
    if (!XVfdModbusBase_submitWriteSingle(&dev->m_base, dev->m_referenceAddress,
                                          (uint16_t)(int16_t)(int32_t)scaled,
                                          atv61SimpleWriteDone, NULL, XModbusRequestPriority_High)) {
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return false;
    }
    return true;
}

static bool VXVfdAtv61_SetSpeedRpm(XVfdDevice* d, int rpm)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
    {
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_SetSpeed,
                                            false, rpm, 0.0, 0.0);
        if (en != 0) return en > 0;
    }
    if (!XVfdModbusBase_busyStart(&dev->m_base, XVfdOperation_SetSpeed)) return false;
    if (rpm > 32767) rpm = 32767;
    if (rpm < -32768) rpm = -32768;
    if (!XVfdModbusBase_submitWriteSingle(&dev->m_base, XVfdAtv61_AddrLfrd,
                                          (uint16_t)(int16_t)rpm, atv61SimpleWriteDone, NULL, XModbusRequestPriority_High)) {
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return false;
    }
    return true;
}

/** @brief 故障复位：0x0080 上升沿 → 归位 0x0000（两步序列） */
static bool VXVfdAtv61_ResetFault(XVfdDevice* d)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
    {
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_ResetFault,
                                            false, 0, 0.0, 0.0);
        if (en != 0) return en > 0;
    }
    if (!XVfdModbusBase_busyStart(&dev->m_base, XVfdOperation_ResetFault)) return false;
    dev->m_pendingReset = true;
    if (!XVfdModbusBase_submitWriteSingle(&dev->m_base, XVfdAtv61_AddrCmd,
                                          XVfdAtv61_CmdFaultResetCmd, atv61OnWriteDone, NULL, XModbusRequestPriority_High)) {
        dev->m_pendingReset = false;
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return false;
    }
    return true;
}

/* =============== 虚函数表 / 生命周期 =============== */
XVtable* XVfdAtv61_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVfdAtv61)
    XVTABLE_INHERIT_XCLASS(XVfdModbusBase);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXVfdAtv61_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_Start, VXVfdAtv61_Start);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_Stop, VXVfdAtv61_Stop);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_FreeStop, VXVfdAtv61_FreeStop);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_QuickStop, VXVfdAtv61_QuickStop);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_SetFrequency, VXVfdAtv61_SetFrequency);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_SetSpeedRpm, VXVfdAtv61_SetSpeedRpm);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_ResetFault, VXVfdAtv61_ResetFault);
    XCLASS_SET_CLASS_NAME_DEFAULT("XVfdAtv61");
    XCLASS_SHOW_SIZE_DEFAULT(XVfdAtv61);
    return XVTABLE_DEFAULT;
}

XVfdAtv61* XVfdAtv61_create_ex(XMemoryType memory)
{
    XVfdAtv61* dev = (XVfdAtv61*)XMemory_malloc(sizeof(XVfdAtv61), memory);
    if (!dev) return NULL;
    XVfdAtv61_init(dev);
    Set_Class_Memory(dev, memory);
    Set_Class_IsHeap(dev, true);
    return dev;
}

void XVfdAtv61_init(XVfdAtv61* dev)
{
    if (!dev) return;
    memset(dev, 0, sizeof(XVfdAtv61));
    XVfdModbusBase_init(&dev->m_base);
    XClassGetVtable(dev) = XVfdAtv61_class_init();
    dev->m_refMode = XVfdAtv61_RefMode_ModbusFreq;
    dev->m_referenceAddress = XVfdAtv61_AddrLfr1;
    dev->m_stepTimer = XTIMER_INVALID_ID;
    /* 轮询组：3201..3209 主状态块 / 7121 故障码 / 9630 电机热状态 */
    XVfdModbusBase_addPollGroup(&dev->m_base, XVfdAtv61_AddrEta, 9,
                                XVfdPollItem_Status | XVfdPollItem_Telemetry, atv61ParseMain);
    XVfdModbusBase_addPollGroup(&dev->m_base, XVfdAtv61_AddrLft, 1,
                                XVfdPollItem_Fault, atv61ParseFault);
    XVfdModbusBase_addPollGroup(&dev->m_base, XVfdAtv61_AddrThr, 1,
                                XVfdPollItem_Telemetry, atv61ParseThermal);
    /* 能力位图 */
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_Reverse);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_FreeStop);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_QuickStop);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_SpeedRpmGiven);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_TorqueReport);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_PowerReport);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_ThermalReport);
}

/* =============== 配置 =============== */
void XVfdAtv61_setReferenceMode(XVfdAtv61* dev, XVfdAtv61_ReferenceMode mode)
{
    if (!dev) return;
    dev->m_refMode = mode;
    switch (mode) {
    case XVfdAtv61_RefMode_SpeedRpm: dev->m_referenceAddress = XVfdAtv61_AddrLfrd; break;
    case XVfdAtv61_RefMode_LfrFreq:  dev->m_referenceAddress = XVfdAtv61_AddrLfr; break;
    default:                          dev->m_referenceAddress = XVfdAtv61_AddrLfr1; break;
    }
}

XVfdAtv61_ReferenceMode XVfdAtv61_referenceMode(const XVfdAtv61* dev)
{
    return dev ? dev->m_refMode : XVfdAtv61_RefMode_ModbusFreq;
}

void XVfdAtv61_setReferenceAddress(XVfdAtv61* dev, uint16_t address)
{
    if (dev) dev->m_referenceAddress = address;
}

/* =============== 故障码查询 =============== */
const char* XVfdAtv61_faultName(uint16_t code)
{
    size_t i;
    for (i = 0; i < ATV61_FAULT_COUNT; ++i) {
        if (s_atv61Faults[i].m_code == code) return s_atv61Faults[i].m_name;
    }
    return NULL;
}

const char* XVfdAtv61_faultText(uint16_t code)
{
    size_t i;
    for (i = 0; i < ATV61_FAULT_COUNT; ++i) {
        if (s_atv61Faults[i].m_code == code) return s_atv61Faults[i].m_text;
    }
    return NULL;
}

#endif /* XVFD_ATV61_ON */
#endif /* XVFD_ON */
