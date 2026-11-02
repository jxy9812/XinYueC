/**
 * @file       XVfdAtv61.c
 * @brief      施耐德 Altivar 61 (ATV61) 变频器子类实现（通道点驱动）。
 * @details    本文件只含 ATV61 的协议知识：点规格表、CiA402/Drivecom 状态字
 *             解码、命令编解码、启动序列推进、109 条故障表。
 *             数据来源与写通道一律经基类（XVfdDevice_*Role）下沉到通道点，
 *             本文件不出现任何传输/编帧符号。
 */
#include "XVfdAtv61.h"
#if XVFD_ON
#if XVFD_ATV61_ON

#include "XVfdDevice_Protected.h"
#include <string.h>

/* ==================== 点规格表（语义角色 → ATV61 逻辑地址 + 原生单位） ==================== */

static const XVfdPointSpec s_atv61Specs[] = {
    /* role                    type                   div  addr                          tag     caps */
    { XVfdRole_StatusWord,     XIoPoint_AnalogInput,   1, XVfdAtv61_AddrEta,  "ETA",  0 },
    { XVfdRole_AuxStateWord,   XIoPoint_AnalogInput,   1, XVfdAtv61_AddrEti,  "ETI",  0 },
    { XVfdRole_FaultCode,      XIoPoint_AnalogInput,   1, XVfdAtv61_AddrLft,  "LFT",  0 },
    { XVfdRole_OutputFreq,     XIoPoint_AnalogInput,  10, XVfdAtv61_AddrRfr,  "RFR",  0 },
    { XVfdRole_SetFreq,        XIoPoint_AnalogInput,  10, XVfdAtv61_AddrFrh,  "FRH",  0 },
    { XVfdRole_OutputCurrent,  XIoPoint_AnalogInput,   1, XVfdAtv61_AddrLcr,  "LCR",  0 },
    { XVfdRole_OutputVoltage,  XIoPoint_AnalogInput,   1, XVfdAtv61_AddrUop,  "UOP",  0 },
    { XVfdRole_MainsVoltage,   XIoPoint_AnalogInput,  10, XVfdAtv61_AddrUln,  "ULN",  0 },
    { XVfdRole_BusVoltage,     XIoPoint_AnalogInput,  10, XVfdAtv61_AddrVbus, "VBUS", 0 },
    { XVfdRole_Torque,         XIoPoint_AnalogInput,  10, XVfdAtv61_AddrOtr,  "OTR",  XVfdCap_TorqueReport },
    { XVfdRole_SpeedRpm,       XIoPoint_AnalogInput,   1, XVfdAtv61_AddrRfrd, "RFRD", 0 },
    { XVfdRole_DriveThermal,   XIoPoint_AnalogInput,   1, XVfdAtv61_AddrThd,  "THD",  XVfdCap_ThermalReport },
    { XVfdRole_MotorThermal,   XIoPoint_AnalogInput,   1, XVfdAtv61_AddrThr,  "THR",  0 },
    { XVfdRole_CommandWord,    XIoPoint_AnalogOutput,  1, XVfdAtv61_AddrCmd,  "CMD",  0 },
    { XVfdRole_FreqSet,        XIoPoint_AnalogOutput, 10, XVfdAtv61_AddrLfr1, "LFR1", 0 },
    { XVfdRole_SpeedSet,       XIoPoint_AnalogOutput,  1, XVfdAtv61_AddrLfrd, "LFRD", XVfdCap_SpeedRpmGiven },
    { XVfdRole_AccelTime,      XIoPoint_AnalogOutput, 100, XVfdAtv61_AddrAcc, "ACC",  0 },
    { XVfdRole_DecelTime,      XIoPoint_AnalogOutput, 100, XVfdAtv61_AddrDec, "DEC",  0 }
};

#define ATV61_SPEC_COUNT ((int)(sizeof(s_atv61Specs) / sizeof(s_atv61Specs[0])))

/* ==================== 故障码表 ====================
 * 数值与字母码取自施耐德官方《ATV610 通信参数表》LFT 枚举（EAV64394），
 * 与 ATV61/ATV71 同属一个逻辑地址家族，故障字母码一致。
 * 现场若遇不一致，以 HMI [1.10 诊断] 当前故障列表为准并修订本表。 */
static const XVfdFaultEntry s_atv61Faults[] = {
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
    { 155, "INF,", "内部故障 20（选件接口 PCBA）" },
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

#define ATV61_FAULT_COUNT ((int)(sizeof(s_atv61Faults) / sizeof(s_atv61Faults[0])))

/* ==================== 前置声明 ==================== */
static bool atv61Step(XVfdAtv61* dev);

/* ==================== 状态字解码 ==================== */

/**
 * @brief 由 ETA 原始值刷新快照运行态字段。
 * @param dev 实例；不能为 NULL。
 * @param eta ETA 原始值。
 * @return 无。
 */
static void atv61ApplyEta(XVfdAtv61* dev, uint16_t eta)
{
    XVfdStatus* st;
    uint8_t state;
    if (!dev) return;
    st = XVfdDevice_status(&dev->m_base);
    state = (uint8_t)(eta & XVfdAtv61_EtaMask);
    dev->m_etaCache = eta;
    st->m_rawStateWord = eta;
    st->m_validMask |= XVfdField_RawStateWord;
    st->m_running = (eta & XVfdAtv61_EtaOperationEnabled) != 0;
    st->m_faulted = (eta & XVfdAtv61_EtaFault) != 0;
    st->m_warning = (eta & XVfdAtv61_EtaWarning) != 0;
    st->m_reverse = (eta & XVfdAtv61_EtaReverse) != 0;
    st->m_targetReached = (eta & XVfdAtv61_EtaTargetReached) != 0;
    st->m_remote = (eta & XVfdAtv61_EtaRemote) != 0;
    st->m_dcBraking = (dev->m_etiCache & XVfdAtv61_EtiDcBrake) != 0;
    st->m_validMask |= XVfdField_Running | XVfdField_Faulted | XVfdField_Warning
                     | XVfdField_Reverse | XVfdField_TargetReached | XVfdField_Remote
                     | XVfdField_DcBraking;
    switch (state) {
    case XVfdAtv61_EtaStateDisabled: st->m_runState = XVfdRunState_Stopped; break;
    case XVfdAtv61_EtaStateReady:
    case XVfdAtv61_EtaStateSwitched: st->m_runState = XVfdRunState_Ready; break;
    case XVfdAtv61_EtaStateRunning:
        if (dev->m_etiCache & XVfdAtv61_EtiAccel) st->m_runState = XVfdRunState_Accelerating;
        else if (dev->m_etiCache & XVfdAtv61_EtiDecel) st->m_runState = XVfdRunState_Decelerating;
        else st->m_runState = st->m_reverse ? XVfdRunState_RunningReverse
                                            : XVfdRunState_RunningForward;
        break;
    case XVfdAtv61_EtaStateQuickStop: st->m_runState = XVfdRunState_QuickStopping; break;
    default:
        st->m_runState = st->m_faulted ? XVfdRunState_Fault : XVfdRunState_Unknown;
        break;
    }
    st->m_validMask |= XVfdField_RunState;
    XVfdDevice_markDirty(&dev->m_base);
}

/**
 * @brief 由 ETI 原始值刷新（加速/减速/直流制动位）；随后按缓存 ETA 重解码。
 * @param dev 实例；不能为 NULL。
 * @param eti ETI 原始值。
 * @return 无。
 */
static void atv61ApplyEti(XVfdAtv61* dev, uint16_t eti)
{
    if (!dev) return;
    dev->m_etiCache = eti;
    atv61ApplyEta(dev, dev->m_etaCache);
}

/**
 * @brief DecodePoint 重载：状态字/辅助字/故障码走 ATV61 语义，其余交基类。
 * @param d 设备实例。
 * @param spec 点规格。
 * @param raw 原生值。
 * @return 无。
 */
static void VXVfdAtv61_DecodePoint(XVfdDevice* d, const XVfdPointSpec* spec, int32_t raw)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
    if (!dev || !spec) return;
    switch (spec->m_role) {
    case XVfdRole_StatusWord:
        atv61ApplyEta(dev, (uint16_t)raw);
        return;
    case XVfdRole_AuxStateWord:
        atv61ApplyEti(dev, (uint16_t)raw);
        return;
    case XVfdRole_FaultCode: {
        XVfdStatus* st = XVfdDevice_status(d);
        st->m_faultCode = (uint16_t)raw;
        st->m_validMask |= XVfdField_FaultCode;
        XVfdDevice_markDirty(d);
        return;
    }
    default:
        break;
    }
    XVfdDevice_decodePoint_base(d, spec, raw);
}

/* ==================== CiA402 启动序列 ==================== */

/**
 * @brief 由当前 ETA 推导下一步命令字与期望状态。
 * @param eta ETA 原始值。
 * @param reverse true=反转。
 * @param outExpect 输出该步写完后应到达的 ETA 状态码（CiA402 掩码域）。
 * @return 命令字（含方向位）。
 * @note 期望状态用于步进等待：命令字写完 ≠ 从站状态已迁移，序列按
 *       “写 → 轮询缓存 ETA 到期望态 → 下一步”推进（每步有等待上限）。
 */
static uint16_t atv61NextCmd(uint16_t eta, bool reverse, uint16_t* outExpect)
{
    uint16_t cmd;
    switch ((uint8_t)(eta & XVfdAtv61_EtaMask)) {
    case XVfdAtv61_EtaStateDisabled:
        cmd = XVfdAtv61_CmdShutdown;      *outExpect = XVfdAtv61_EtaStateReady;    break;
    case XVfdAtv61_EtaStateReady:
        cmd = XVfdAtv61_CmdSwitchOnState; *outExpect = XVfdAtv61_EtaStateSwitched; break;
    case XVfdAtv61_EtaStateSwitched:
        cmd = XVfdAtv61_CmdRun;           *outExpect = XVfdAtv61_EtaStateRunning; break;
    default:
        cmd = XVfdAtv61_CmdRun;           *outExpect = XVfdAtv61_EtaStateRunning; break;
    }
    if (reverse) cmd |= XVfdAtv61_CmdReverse;
    return cmd;
}

/**
 * @brief 启动序列单步：读状态字 → 判定 → 写下一命令字（写落定后由定时器继续）。
 * @param dev 实例；不能为 NULL。
 * @return 序列仍在推进返回 true；已判失败返回 false（动作已收口）。
 */
static bool atv61Step(XVfdAtv61* dev)
{
    XVfdDevice* d;
    XVfdStatus* st;
    int32_t eta = 0;
    if (!dev) return false;
    d = &dev->m_base;
    st = XVfdDevice_status(d);
    if (!XVfdDevice_readRole(d, XVfdRole_StatusWord, &eta)) {
        XVfdDevice_emitError(d, "启动序列读取状态字失败（状态点无数据）");
        XVfdDevice_finishAction(d, false);
        return false;
    }
    atv61ApplyEta(dev, (uint16_t)eta);
    if (st->m_faulted) {
        XVfdDevice_setLastError(d, XVfdError_WriteRejected);
        XVfdDevice_emitError(d, "变频器处于故障态，请先复位故障");
        XVfdDevice_finishAction(d, false);
        return false;
    }
    if (st->m_running) {
        XVfdDevice_finishAction(d, true);
        return false;
    }
    if (dev->m_step >= XVfdAtv61_StartMaxStep) {
        XVfdDevice_setLastError(d, XVfdError_Timeout);
        XVfdDevice_emitError(d, "启动超时：变频器未进入运行状态");
        XVfdDevice_finishAction(d, false);
        return false;
    }
    dev->m_pendingCmd = atv61NextCmd((uint16_t)eta, dev->m_reverse, &dev->m_expectEta);
    dev->m_waitPolls = 0;
    ++dev->m_step;
    dev->m_waitingWrite = true;
    if (!XVfdDevice_writeRole(d, XVfdRole_CommandWord, (int32_t)dev->m_pendingCmd)) {
        dev->m_waitingWrite = false;
        XVfdDevice_finishAction(d, false);
        return false;
    }
    XVfdDevice_armActionTimer(d, XVfdDevice_actionTimeout(d));
    return true;
}

/* ==================== 动作实现 ==================== */

/**
 * @brief 启动：CiA402 状态机推进（读 ETA → 逐级写命令字）。
 * @param d 设备实例。
 * @param reverse true=反转。
 * @return 受理返回 true。
 */
static bool VXVfdAtv61_Start(XVfdDevice* d, bool reverse)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
    bool progressing;
    if (!XVfdDevice_beginAction(d, XVfdOperation_Start)) return false;
    dev->m_reverse = reverse;
    dev->m_step = 0;
    dev->m_waitingWrite = false;
    progressing = atv61Step(dev);
    if (!progressing && XVfdDevice_lastError(d) != XVfdError_OK) return false;
    return true;
}

/**
 * @brief 受控停机：写 Disable operation(0x0007)。
 * @param d 设备实例。
 * @return 受理返回 true。
 */
static bool VXVfdAtv61_Stop(XVfdDevice* d)
{
    return XVfdDevice_actionWrite(d, XVfdOperation_Stop, XVfdRole_CommandWord,
                                  (int32_t)XVfdAtv61_CmdDisableOperation);
}

/**
 * @brief 自由停车：写 Disable voltage(0x0000)。
 * @param d 设备实例。
 * @return 受理返回 true。
 */
static bool VXVfdAtv61_FreeStop(XVfdDevice* d)
{
    return XVfdDevice_actionWrite(d, XVfdOperation_FreeStop, XVfdRole_CommandWord,
                                  (int32_t)XVfdAtv61_CmdDisableVoltage);
}

/**
 * @brief 快速停机：写 Quick stop(0x0002)。
 * @param d 设备实例。
 * @return 受理返回 true。
 */
static bool VXVfdAtv61_QuickStop(XVfdDevice* d)
{
    return XVfdDevice_actionWrite(d, XVfdOperation_QuickStop, XVfdRole_CommandWord,
                                  (int32_t)XVfdAtv61_CmdQuickStopCmd);
}

/**
 * @brief 写频率给定（Hz → 0.1Hz 定点）。
 * @param d 设备实例。
 * @param hz 目标频率（Hz）。
 * @return 受理返回 true。
 */
static bool VXVfdAtv61_SetFrequency(XVfdDevice* d, double hz)
{
    int32_t native = XVfdDevice_engineeringToRaw(hz, XVfdAtv61_FreqDiv, -32768, 32767);
    return XVfdDevice_actionWrite(d, XVfdOperation_SetFrequency, XVfdRole_FreqSet, native);
}

/**
 * @brief 写转速给定（rpm）。
 * @param d 设备实例。
 * @param rpm 目标转速。
 * @return 受理返回 true。
 */
static bool VXVfdAtv61_SetSpeedRpm(XVfdDevice* d, int rpm)
{
    int32_t native = rpm;
    if (native > 32767) native = 32767;
    if (native < -32768) native = -32768;
    return XVfdDevice_actionWrite(d, XVfdOperation_SetSpeed, XVfdRole_SpeedSet, native);
}

/**
 * @brief 写加减速时间（秒 → 0.01s 定点；ACC 与 DEC 两点两步写）。
 * @param d 设备实例。
 * @param index 组号（ATV61 仅 0 组支持经本接口写）。
 * @param accelS 加速时间（秒）。
 * @param decelS 减速时间（秒）。
 * @return 受理返回 true（两步写由 ActionWriteDone 收口第一跳）。
 */
static bool VXVfdAtv61_SetAccelDecel(XVfdDevice* d, int index, double accelS, double decelS)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
    int32_t acc;
    int32_t dec;
    if (index != 0) {
        XVfdDevice_setLastError(d, XVfdError_InvalidParam);
        return false;
    }
    acc = XVfdDevice_engineeringToRaw(accelS, XVfdAtv61_TimeDiv, -32768, 32767);
    dec = XVfdDevice_engineeringToRaw(decelS, XVfdAtv61_TimeDiv, -32768, 32767);
    if (!XVfdDevice_beginAction(d, XVfdOperation_SetAccelDecel)) return false;
    dev->m_pendingCmd = (uint16_t)dec;   /* 复用字段暂存待写减速值 */
    if (!XVfdDevice_writeRole(d, XVfdRole_AccelTime, acc)) {
        XVfdDevice_finishAction(d, false);
        return false;
    }
    XVfdDevice_armActionTimer(d, XVfdDevice_actionTimeout(d));
    return true;
}

/**
 * @brief 故障复位：0x0080 上升沿 → 归位 0x0000（两步序列）。
 * @param d 设备实例。
 * @return 受理返回 true。
 */
static bool VXVfdAtv61_ResetFault(XVfdDevice* d)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
    if (!XVfdDevice_beginAction(d, XVfdOperation_ResetFault)) return false;
    dev->m_pendingReset = true;
    if (!XVfdDevice_writeRole(d, XVfdRole_CommandWord, (int32_t)XVfdAtv61_CmdFaultResetCmd)) {
        dev->m_pendingReset = false;
        XVfdDevice_finishAction(d, false);
        return false;
    }
    XVfdDevice_armActionTimer(d, XVfdDevice_actionTimeout(d));
    return true;
}

/**
 * @brief ActionWriteDone 重载：启动序列/复位序列的多步推进，其余默认收口。
 * @param d 设备实例。
 * @param point 落定的点（未用）。
 * @param ok 写落定结果。
 * @return 无。
 */
static void VXVfdAtv61_ActionWriteDone(XVfdDevice* d, XIoPoint* point, bool ok)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
    XVfdOperation op = XVfdDevice_currentOp(d);
    (void)point;
    if (!dev) return;
    if (op == XVfdOperation_Start) {
        if (!ok) {
            dev->m_waitingWrite = false;
            XVfdDevice_finishAction(d, false);
            return;
        }
        dev->m_waitingWrite = false;
        XVfdDevice_armActionTimer(d, dev->m_stepDelayMs); /* 等状态迁移后继续推进 */
        return;
    }
    if (op == XVfdOperation_SetAccelDecel) {
        if (!ok) {
            XVfdDevice_finishAction(d, false);
            return;
        }
        if (!XVfdDevice_writeRole(d, XVfdRole_DecelTime, (int32_t)(int16_t)dev->m_pendingCmd)) {
            XVfdDevice_finishAction(d, false);
            return;
        }
        XVfdDevice_armActionTimer(d, XVfdDevice_actionTimeout(d));
        return;
    }
    if (op == XVfdOperation_ResetFault && dev->m_pendingReset) {
        dev->m_pendingReset = false;
        if (!ok) {
            XVfdDevice_finishAction(d, false);
            return;
        }
        if (!XVfdDevice_writeRole(d, XVfdRole_CommandWord,
                                  (int32_t)XVfdAtv61_CmdDisableVoltage)) {
            XVfdDevice_finishAction(d, false);
            return;
        }
        XVfdDevice_armActionTimer(d, XVfdDevice_actionTimeout(d));
        return;
    }
    XVfdDevice_finishAction(d, ok);
}

/**
 * @brief ActionTimerExpired 重载：启动序列按“期望状态轮询”推进 /
 *        写落定超时判定，其余交基类。
 * @param d 设备实例。
 * @return 无。
 * @note 步进节拍不再盲等固定毫秒：写落定后周期性读缓存 ETA，直到状态
 *       迁移到期望态（总线上读/写排队延迟不影响序列正确性）；等待超过
 *       XVfdAtv61_StartWaitPolls 拍判超时。
 */
static void VXVfdAtv61_ActionTimerExpired(XVfdDevice* d)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
    if (!dev) return;
    if (XVfdDevice_currentOp(d) == XVfdOperation_Start) {
        if (dev->m_waitingWrite) {
            dev->m_waitingWrite = false;
            XVfdDevice_setLastError(d, XVfdError_Timeout);
            XVfdDevice_emitError(d, "命令字写落定超时（启动序列中止）");
            XVfdDevice_finishAction(d, false);
            return;
        }
        {
            int32_t eta = 0;
            if (!XVfdDevice_readRole(d, XVfdRole_StatusWord, &eta)) {
                XVfdDevice_emitError(d, "启动序列读取状态字失败（状态点无数据）");
                XVfdDevice_finishAction(d, false);
                return;
            }
            atv61ApplyEta(dev, (uint16_t)eta);
            if ((dev->m_etaCache & XVfdAtv61_EtaMask) == dev->m_expectEta) {
                atv61Step(dev);          /* 状态已迁移：推进下一步 */
                return;
            }
            if (++dev->m_waitPolls > XVfdAtv61_StartWaitPolls) {
                XVfdDevice_setLastError(d, XVfdError_Timeout);
                XVfdDevice_emitError(d, "启动超时：状态未按序列迁移");
                XVfdDevice_finishAction(d, false);
                return;
            }
            XVfdDevice_armActionTimer(d, 60);   /* 未迁移：继续等 */
            return;
        }
    }
    XClass_Parent(XVfdDevice, EXVfdDevice_ActionTimerExpired,
                  void (*)(XVfdDevice*))(d);
}

/* ==================== 虚函数表 / 生命周期 ==================== */

XVtable* XVfdAtv61_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVfdAtv61)
    XVTABLE_INHERIT_XCLASS(XVfdDevice);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_DecodePoint, VXVfdAtv61_DecodePoint);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_ActionWriteDone, VXVfdAtv61_ActionWriteDone);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_ActionTimerExpired, VXVfdAtv61_ActionTimerExpired);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_Start, VXVfdAtv61_Start);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_Stop, VXVfdAtv61_Stop);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_FreeStop, VXVfdAtv61_FreeStop);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_QuickStop, VXVfdAtv61_QuickStop);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_SetFrequency, VXVfdAtv61_SetFrequency);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_SetSpeedRpm, VXVfdAtv61_SetSpeedRpm);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_SetAccelDecel, VXVfdAtv61_SetAccelDecel);
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
    XVfdDevice_init(&dev->m_base);
    XClassGetVtable(dev) = XVfdAtv61_class_init();
    dev->m_stepDelayMs = XVfdAtv61_DefaultStepDelayMs;
    XVfdDevice_setCapability(&dev->m_base, XVfdCap_Reverse);
    XVfdDevice_setCapability(&dev->m_base, XVfdCap_FreeStop);
    XVfdDevice_setCapability(&dev->m_base, XVfdCap_QuickStop);
}

/* ==================== 点规格表 / 配置 ==================== */

const XVfdPointSpec* XVfdAtv61_pointSpecs(int* count)
{
    if (count) *count = ATV61_SPEC_COUNT;
    return s_atv61Specs;
}

void XVfdAtv61_setStepDelay(XVfdAtv61* dev, uint32_t delayMs)
{
    if (!dev) return;
    dev->m_stepDelayMs = (delayMs == 0) ? XVfdAtv61_DefaultStepDelayMs : delayMs;
}

uint32_t XVfdAtv61_stepDelay(const XVfdAtv61* dev)
{
    return dev ? dev->m_stepDelayMs : 0;
}

/* ==================== 故障码查询 ==================== */

const char* XVfdAtv61_faultName(uint16_t code)
{
    const XVfdFaultEntry* e = XVfdDevice_faultEntry_const(s_atv61Faults, ATV61_FAULT_COUNT, code);
    return e ? e->m_name : NULL;
}

const char* XVfdAtv61_faultText(uint16_t code)
{
    const XVfdFaultEntry* e = XVfdDevice_faultEntry_const(s_atv61Faults, ATV61_FAULT_COUNT, code);
    return e ? e->m_text : NULL;
}

#endif /* XVFD_ATV61_ON */
#endif /* XVFD_ON */
