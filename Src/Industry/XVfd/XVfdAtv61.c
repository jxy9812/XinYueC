/**
 * @file       XVfdAtv61.c
 * @brief      施耐德 Altivar 61 (ATV61) 变频器子类实现（通道点驱动）。
 * @details    本文件只含 ATV61 的协议知识：点规格表、CiA402/Drivecom 画像表
 *             （ETA/CMD 常量 + 时序常量）、ETI 品牌专有位解码、给定量标度
 *             换算、故障表。
 *             CiA402 状态机骨架（启动序列逐级推进、期望态轮询、停机族命令、
 *             故障复位两步序列、状态字解码）已在通用基类 XVfdCiA402，本文件
 *             只给一张 XVfdCiA402Profile 与品牌专有辅助位语义。
 *             数据来源与写通道一律经基类（XVfdDevice_*Role）下沉到通道点，
 *             本文件不出现任何传输/编帧符号。
 */
#include "XVfdAtv61.h"
#if XVFD_ON
#if XVFD_ATV61_ON

#include "XVfdCiA402_Protected.h"
#include "XVfdDevice_Protected.h"
#include <string.h>

/* ==================== 点规格表（语义角色 → ATV61 逻辑地址 + 原生单位） ==================== */

static const XVfdPointSpec s_atv61Specs[] = {
    /* role                    type                   div  tag     caps */
    { XVfdRole_StatusWord,     XIoPoint_AnalogInput,   1, "ETA",   0 },
    { XVfdRole_AuxStateWord,   XIoPoint_AnalogInput,   1, "ETI",   0 },
    { XVfdRole_FaultCode,      XIoPoint_AnalogInput,   1, "LFT",   0 },
    { XVfdRole_OutputFreq,     XIoPoint_AnalogInput,  10, "RFR",   0 },
    { XVfdRole_SetFreq,        XIoPoint_AnalogInput,  10, "FRH",   0 },
    { XVfdRole_OutputCurrent,  XIoPoint_AnalogInput,   1, "LCR",   0 },
    { XVfdRole_OutputVoltage,  XIoPoint_AnalogInput,   1, "UOP",   0 },
    { XVfdRole_MainsVoltage,   XIoPoint_AnalogInput,  10, "ULN",   0 },
    { XVfdRole_BusVoltage,     XIoPoint_AnalogInput,  10, "VBUS",  0 },
    { XVfdRole_Torque,         XIoPoint_AnalogInput,  10, "OTR",   XVfdCap_TorqueReport },
    { XVfdRole_SpeedRpm,       XIoPoint_AnalogInput,   1, "RFRD",  0 },
    { XVfdRole_DriveThermal,   XIoPoint_AnalogInput,   1, "THD",   XVfdCap_ThermalReport },
    { XVfdRole_MotorThermal,   XIoPoint_AnalogInput,   1, "THR",   0 },
    { XVfdRole_CommandWord,    XIoPoint_AnalogOutput,  1, "CMD",   0 },
    { XVfdRole_FreqSet,        XIoPoint_AnalogOutput, 10, "LFR1",  0 },
    { XVfdRole_SpeedSet,       XIoPoint_AnalogOutput,  1, "LFRD",  XVfdCap_SpeedRpmGiven },
    { XVfdRole_AccelTime,      XIoPoint_AnalogOutput, 100, "ACC",   0 },
    { XVfdRole_DecelTime,      XIoPoint_AnalogOutput, 100, "DEC",   0 },
};

#define ATV61_SPEC_COUNT ((int)(sizeof(s_atv61Specs) / sizeof(s_atv61Specs[0])))

/* ==================== CiA402 / Drivecom 画像表 ====================
 * 把 ATV61 的 ETA/CMD 常量与时序常量交给通用骨架 XVfdCiA402：
 * 状态域掩码、六个状态码、命令字、状态位、序列时序全部由本表驱动。
 * 命令字位定义与状态字位定义见 XVfdAtv61.h（公开常量，外部在用）。 */
static const XVfdCiA402Profile s_atv61Profile = {
    /* m_stateMask      */ XVfdAtv61_EtaMask,
    /* m_states         */ { XVfdAtv61_EtaStateDisabled,
                             XVfdAtv61_EtaStateReady,
                             XVfdAtv61_EtaStateSwitched,
                             XVfdAtv61_EtaStateRunning,
                             XVfdAtv61_EtaStateQuickStop,
                             XVfdAtv61_EtaStateFault },
    /* m_cmdShutdown    */ XVfdAtv61_CmdShutdown,
    /* m_cmdSwitchOn    */ XVfdAtv61_CmdSwitchOnState,
    /* m_cmdRun         */ XVfdAtv61_CmdRun,
    /* m_cmdDisableOp   */ XVfdAtv61_CmdDisableOperation,
    /* m_cmdDisableVoltage */ XVfdAtv61_CmdDisableVoltage,
    /* m_cmdQuickStop   */ XVfdAtv61_CmdQuickStopCmd,
    /* m_cmdFaultReset  */ XVfdAtv61_CmdFaultResetCmd,
    /* m_cmdReverse     */ XVfdAtv61_CmdReverse,
    /* m_bitRunning     */ XVfdAtv61_EtaOperationEnabled,
    /* m_bitFault       */ XVfdAtv61_EtaFault,
    /* m_bitWarning     */ XVfdAtv61_EtaWarning,
    /* m_bitRemote      */ XVfdAtv61_EtaRemote,
    /* m_bitTargetReached */ XVfdAtv61_EtaTargetReached,
    /* m_bitReverse     */ XVfdAtv61_EtaReverse,
    /* m_maxStep        */ XVfdAtv61_StartMaxStep,
    /* m_stepDelayMs    */ XVfdAtv61_DefaultStepDelayMs,
    /* m_pollLimit      */ XVfdAtv61_StartWaitPolls,
    /* m_pollIntervalMs */ 60
};

/* ==================== 故障码表 ====================
 *
 * 【字母码口径】《ATV61 编程手册》V5.8 p.242–247 的故障字母码全表（手册面板
 * 显示的就是这个混合大小写写法，如 CnF / InFb / ObF / tnF）。本表 A 组按手册
 * 写法逐条给出字母码与中文含义。
 *
 * 【数值码口径】手册**只给字母码，不给数值码**（ATV61 的 LFT 寄存器无公开的
 * 数值枚举）。故数值码沿用旧表（其出处为《ATV610 通信参数表》EAV64394 的 LFT
 * 枚举，与 ATV61 同属一个逻辑地址家族）；旧表没有的手册条目在旧表未占用的
 * 保留号段内新分配（新增条目见行尾 “（手册新增）” 标注）。
 *
 * 【分组】A 组 = ATV61 编程手册列出的故障（参与 XVfdAtv61_faultName/faultText
 * 的显示）；B 组 = 其它产品族（ATV610 / AFE / 泵控）条目，**ATV61 手册未列**，
 * 不是 ATV61 的故障，只保留供现场对照与后续移植查阅，不参与 ATV61 故障显示。
 *
 * 【复位分档】A = 不能自动复位（须先断电再上电清除原因）｜B = 原因消失后可用
 * 自动重起动复位｜C = 原因一消失即可复位（手册分档）。 */

static const XVfdFaultEntry s_atv61Faults[] = {
    { 0,   "NOF",  "无故障" },
    { 2,   "EEF1", "控制卡存储器故障（A 档）" },
    { 3,   "CFF",  "错误的设置（卡被更换/拆掉）（C 档）" },
    { 4,   "CFI",  "无效设置（总线加载的设置不一致）（C 档）" },
    { 5,   "SLF1", "Modbus 通信中断（B 档）" },
    { 6,   "ILF",  "选件卡内部连接故障（A 档）" },
    { 7,   "CnF",  "网络故障（通信卡）（B 档）" },
    { 8,   "EPF1", "LI/位输入的外部故障（B 档）" },
    { 9,   "OCF",  "过流（A 档）" },
    { 10,  "CrF1", "预充电故障（负载继电器控制故障/充电电阻损坏）（A 档）" },
    { 11,  "AI2F", "AI2 输入信号不一致（模拟输入 AI2）（A 档，手册新增）" },
    { 12,  "bOF",  "制动电阻过载（A 档，手册新增）" },
    { 13,  "LFF2", "AI2 4-20mA 信号损失（B 档）" },
    { 14,  "bUF",  "制动单元短路（A 档，手册新增）" },
    { 16,  "OHF",  "变频器过热（B 档）" },
    { 17,  "OLF",  "电机过载（B 档）" },
    { 18,  "ObF",  "制动过速（B 档）；注：ATV61 手册无「直流母线过压」故障，本数值码沿用旧表" },
    { 19,  "OSF",  "输入过电压（B 档）" },
    { 20,  "OPF1", "输出缺 1 相（B 档）" },
    { 21,  "PHF",  "输入缺相（C 档）" },
    { 22,  "USF",  "欠压（C 档）" },
    { 23,  "SCF1", "电机短路（A 档）" },
    { 24,  "SOF",  "超速（A 档）" },
    { 25,  "tnF",  "自整定（A 档）" },
    { 26,  "InF1", "额定功率错误（功率卡与存储的卡不同）（A 档）" },
    { 27,  "InF2", "不兼容的电源板（A 档）" },
    { 28,  "InF3", "内部串行连接（A 档）" },
    { 29,  "InF4", "生产专用区域（A 档）" },
    { 30,  "EEF2", "功率卡存储器故障（A 档）" },
    { 31,  "CrF2", "晶闸管充电回路故障（直流母线充电故障）（A 档，手册新增）" },
    { 32,  "SCF3", "接地短路（A 档）" },
    { 33,  "OPF2", "电机缺 3 相（B 档）" },
    { 34,  "COF",  "CANopen 通信中断（B 档）" },
    { 35,  "EnF",  "编码器反馈故障（A 档，手册新增）" },
    { 36,  "FCF1", "输出接触器未打开（A 档，手册新增）" },
    { 37,  "InF7", "硬件初始化（A 档）" },
    { 38,  "EPF2", "网络输入的外部故障（B 档）" },
    { 39,  "FCF2", "输出接触器未关闭（B 档，手册新增）" },
    { 40,  "InF8", "内部控制电源故障（A 档）" },
    { 41,  "Fd1",  "阻尼器卡住（B 档，手册新增）" },
    { 42,  "SLF2", "PowerSuite 通信中断（B 档）" },
    { 43,  "Fd2",  "阻尼器开启（A 档，手册新增）" },
    { 44,  "HdF",  "IGBT 去饱和（A 档，手册新增）" },
    { 45,  "SLF3", "控制面板通信中断（图形显示终端）（B 档）" },
    { 46,  "PrF",  "电源切除失效（「断电」安全功能故障）（A 档，手册新增）" },
    { 47,  "PrtF", "电源确认故障（C 档，手册新增）" },
    { 48,  "SCF2", "有阻抗短路（A 档，手册新增）" },
    { 49,  "SPF",  "速度反馈丢失（B 档，手册新增）" },
    { 50,  "APF",  "程序故障（Controller Inside 卡）（B 档，手册新增）" },
    { 51,  "InF9", "内部电流测量故障（A 档）" },
    { 52,  "InFA", "内部输入电源故障（A 档）" },
    { 53,  "InFb", "内部温度传感器（A 档）" },
    { 54,  "tJF",  "IGBT 过热（B 档）" },
    { 55,  "SCF4", "IGBT 短路（B 档）" },
    { 56,  "SCF5", "电机短路（B 档）" },
    { 57,  "nFF",  "无流体故障（B 档，手册新增）" },
    { 58,  "OtF1", "PTC1 过热（B 档，手册新增）" },
    { 59,  "OtF2", "PTC2 过热（B 档，手册新增）" },
    { 60,  "InFC", "内部时钟故障（A 档）" },
    { 61,  "OtFL", "LI6=PTC 过热（B 档，手册新增）" },
    { 62,  "PtF1", "PTC1 探头（B 档，手册新增）" },
    { 63,  "PtF2", "PTC2 探头（B 档，手册新增）" },
    { 64,  "LCF",  "输入接触器（B 档）" },
    { 65,  "PtFL", "LI6=PTC 探头（B 档，手册新增）" },
    { 66,  "SPIF", "PI 反馈故障（B 档，手册新增）" },
    { 67,  "SSF",  "转矩/电流限幅（B 档，手册新增）" },
    { 68,  "InF6", "选项卡故障（A 档）" },
    { 69,  "InFE", "内部 CPU 故障（A 档）" },
    { 71,  "LFF3", "AI3 4-20mA 信号损失（B 档）" },
    { 72,  "LFF4", "AI4 4-20mA 信号损失（B 档）" },
    { 73,  "HCF",  "卡匹配（C 档）" },
    { 100, "ULF",  "过程欠载故障（B 档）" },
    { 101, "OLC",  "过载故障（过程过载）（B 档）" }
};

#define ATV61_FAULT_COUNT ((int)(sizeof(s_atv61Faults) / sizeof(s_atv61Faults[0])))

/* ---- 其它产品族（ATV610 / AFE / 泵控）：ATV61 编程手册未列 ----
 * 这些条目出自《ATV610 通信参数表》EAV64394 的 LFT 枚举，带 AFE 前端、泵控、
 * 以太网特征，**ATV61 编程手册 p.242–247 的故障全表里根本没有它们**，
 * 因此它们不是 ATV61 的故障：单列一组保留（现场对照/后续移植用），
 * 不参与 XVfdAtv61_faultName/faultText 的 ATV61 故障显示。 */
static const XVfdFaultEntry s_atv61OtherFaults[] = {
    { 15,  "IHF",  "输入过热" },
    { 77,  "CFI2", "配置传送故障" },
    { 79,  "LFF5", "AI5 4-20mA 信号丢失" },
    { 99,  "CSF",  "通道切换故障" },
    { 105, "ASF",  "角度故障" },
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
    { 148, "STF",  "电机堵转故障" },
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
    { 173, "CHF",  "柜内过热" },
    { 174, "URF",  "AFE 电源欠压" },
    { 175, "INFV", "内部故障 31（缺功率砖）" },
    { 176, "INFT", "内部故障 29（逆变器）" },
    { 177, "INFU", "内部故障 30（整流器）" },
    { 179, "TJF2", "AFE IGBT 过热" },
    { 180, "CRF3", "AFE 接触器反馈故障" },
    { 181, "CFI3", "预设值传送故障" },
    { 182, "CBF",  "断路器故障" },
    { 200, "FDR1", "FDR 内置以太网故障" },
    { 201, "FDR2", "FDR 以太网模块故障" },
    { 206, "DCRE", "直流母线纹波故障" },
    { 208, "IDLF", "空闲模式退出故障" },
    { 220, "AFLF", "高级功能被锁定" }
};

#define ATV61_OTHER_FAULT_COUNT ((int)(sizeof(s_atv61OtherFaults) / sizeof(s_atv61OtherFaults[0])))

/* ==================== 品牌专有解码（ETI 辅助位） ==================== */

/**
 * @brief 品牌重载解码：ETI 的加速/减速/直流制动位与运行态细分。
 * @param base 通用骨架实例。
 * @param status 状态字原始值（通用部分已解完；此处不用）。
 * @param aux 辅助状态字 ETI 原始值。
 * @param st 可变状态快照（就地改写）。
 * @return 无。
 */
static void VXVfdAtv61_DecodeBrand(XVfdCiA402* base, uint16_t status, uint16_t aux,
                                   XVfdStatus* st)
{
    (void)status;
    if (!base || !st) return;
    st->m_dcBraking = (aux & XVfdAtv61_EtiDcBrake) != 0;
    st->m_validMask |= XVfdField_DcBraking;
    if (st->m_runState == XVfdRunState_RunningForward ||
        st->m_runState == XVfdRunState_RunningReverse) {
        if (aux & XVfdAtv61_EtiAccel) st->m_runState = XVfdRunState_Accelerating;
        else if (aux & XVfdAtv61_EtiDecel) st->m_runState = XVfdRunState_Decelerating;
    }
}

/* ==================== 动作实现（品牌专有标度与两步写） ==================== */

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
    dev->m_pendingDecel = (int16_t)dec;   /* 暂存待写减速值 */
    if (!XVfdDevice_writeRole(d, XVfdRole_AccelTime, acc)) {
        XVfdDevice_finishAction(d, false);
        return false;
    }
    XVfdDevice_armActionTimer(d, XVfdDevice_actionTimeout(d));
    return true;
}

/**
 * @brief ActionWriteDone 重载：加减速两步写的第二跳；其余交 CiA402 基类。
 * @param d 设备实例。
 * @param point 落定的点（未用）。
 * @param ok 写落定结果。
 * @return 无。
 */
static void VXVfdAtv61_ActionWriteDone(XVfdDevice* d, XIoPoint* point, bool ok)
{
    XVfdAtv61* dev = (XVfdAtv61*)d;
    XVfdOperation op = XVfdDevice_currentOp(d);
    if (!dev) return;
    if (op == XVfdOperation_SetAccelDecel) {
        if (!ok) {
            XVfdDevice_finishAction(d, false);
            return;
        }
        if (!XVfdDevice_writeRole(d, XVfdRole_DecelTime, (int32_t)dev->m_pendingDecel)) {
            XVfdDevice_finishAction(d, false);
            return;
        }
        XVfdDevice_armActionTimer(d, XVfdDevice_actionTimeout(d));
        return;
    }
    /* 启动序列 / 故障复位序列等由通用 CiA402 骨架推进。
     * [签名根修] 此前按 `void (*)(XVfdDevice*)` 单参调用三参虚槽
     * （XVfdDevice* , XIoPoint* , bool）：x86-64 SysV 下 point/ok 从寄存器
     * 读出的实为 R7 之后的残留值，`ok` 恒为垃圾（实测 ok=189/230，非 0/1）。
     * CiA402 序列因此把「写失败」当成功继续推进，ATV61 启动/复位序列永不
     * 收口（xvfd-atv61 的 6 条失败用例）。按虚槽真实原型转发，三个参数原样
     * 透传，品牌层与会话层语义不变。 */
    XClass_Parent(XVfdCiA402, EXVfdDevice_ActionWriteDone,
                  void (*)(XVfdDevice*, XIoPoint*, bool))(d, point, ok);
}

/* ==================== 虚函数表 / 生命周期 ==================== */

XVtable* XVfdAtv61_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVfdAtv61)
    XVTABLE_INHERIT_XCLASS(XVfdCiA402);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdCiA402_DecodeBrand, VXVfdAtv61_DecodeBrand);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_ActionWriteDone, VXVfdAtv61_ActionWriteDone);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_SetFrequency, VXVfdAtv61_SetFrequency);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_SetSpeedRpm, VXVfdAtv61_SetSpeedRpm);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_SetAccelDecel, VXVfdAtv61_SetAccelDecel);
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
    XVfdCiA402_init(&dev->m_base);
    XClassGetVtable(dev) = XVfdAtv61_class_init();
    XVfdCiA402_setProfile(&dev->m_base, &s_atv61Profile);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_Reverse);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_FreeStop);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_QuickStop);
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
    XVfdCiA402_setStepDelay(&dev->m_base, delayMs);
}

uint32_t XVfdAtv61_stepDelay(const XVfdAtv61* dev)
{
    return dev ? XVfdCiA402_stepDelay(&dev->m_base) : 0;
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

const XVfdFaultEntry* XVfdAtv61_otherFaultEntry(uint16_t code)
{
    return XVfdDevice_faultEntry_const(s_atv61OtherFaults, ATV61_OTHER_FAULT_COUNT, code);
}

#endif /* XVFD_ATV61_ON */
#endif /* XVFD_ON */
