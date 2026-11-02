/**
 * @file       XVfdM512.c
 * @brief      511M/512M/513M 系列变频器子类实现（通道点驱动）。
 * @details    [2026-11-02 分层收口] 本文件只含**语义知识**：点规格表（role/type/
 *             div/caps/signed/tag）、参数描述表、故障表、命令码表、状态/故障解码、
 *             命令字编码、频率百分比换算（P0.10 基准）与通讯通道前置检查。
 *             **不含任何总线地址**——地址、通信组、从站约束都在绑定层
 *             XVfdM512_Modbus.c（XVfdM512_modbusBinding），换总线只换那份表。
 *             数据进/控制出一律经基类
 *             （XVfdDevice_readRole/writeRole/actionWrite）下沉到通道点。
 *             手册依据：《511M/512M/513M 高性能矢量变频器使用说明书》
 *             2020-05-03 附录1（通信）+ 第三/四/五章（参数与故障）。
 */
#include "XVfdM512.h"
#if XVFD_ON
#if XVFD_M512_ON

#include "XVfdDevice_Protected.h"
#include "XVfdCommandEnum_Protected.h"
#include <string.h>

/* ==================== 通信组表 ====================
 * 手册条目级的远端地址区间。**组边界不是地址连续性**：1000H「通讯设定值」
 * 与 1001H~1020H「运行/停机参数」地址相邻却分属两条条目，一帧不得同时读。
 * 装配层按组切映射段（每组一段），读帧因此天然不跨组。
 * 组内只列「本实现用到的连续子区间」：手册若干组内部有未印的空洞
 * （P0.20 / U0.48 前 / U0.50~U0.57），空洞处不映射即不读。 */

/* ==================== 通信组表 ====================
 * 手册条目级的远端地址区间。**组边界不是地址连续性**：1000H「通讯设定值」
 * 与 1001H~1020H「运行/停机参数」地址相邻却分属两条条目，一帧不得同时读。
 * 装配层按组切映射段（每组一段），读帧因此天然不跨组。
 *
 * 每条只覆盖「本实现实际用到的那一段连续地址」——**不整段铺开**：手册条目
 * 内含大量未用地址（U 组 0x7000~0x7074 有 75 字，本实现只用电机温度一处），
 * 整段映射会让读帧数暴涨（本型号地址天然分散：严格按功能码组边界读，
 * 读帧数 ≈ 用到的连续子区间数）。手册未印的空洞地址一律不映射、不读。
 */


/* ==================== 点规格表 ==================== */

static const XVfdPointSpec s_m512Specs[] = {
    /* role                    type                   div  tag                                caps                 signed */
    /* ---- 读点（遥测） ---- */
    { XVfdRole_StatusWord,     XIoPoint_AnalogInput,   1, "STATE", 0,                   0 },
    { XVfdRole_FaultCode,      XIoPoint_AnalogInput,   1, "FAULT", 0,                   0 },
    { XVfdRole_OutputFreq,     XIoPoint_AnalogInput,  100, "RFR",   0,                   0 },
    { XVfdRole_BusVoltage,     XIoPoint_AnalogInput,  10, "VBUS",  0,                   0 },
    { XVfdRole_OutputVoltage,  XIoPoint_AnalogInput,   1, "UOP",   0,                   0 },
    { XVfdRole_OutputCurrent,  XIoPoint_AnalogInput,  100, "LCR",   0,                   0 },
    { XVfdRole_OutputPower,    XIoPoint_AnalogInput,  10, "OPR",   XVfdCap_PowerReport, 0 },
    { XVfdRole_Torque,         XIoPoint_AnalogInput,  10, "OTR",   XVfdCap_TorqueReport, 0 },
    { XVfdRole_DiBits,         XIoPoint_AnalogInput,   1, "DI",    XVfdCap_DiDoStatus,  0 },
    { XVfdRole_DoBits,         XIoPoint_AnalogInput,   1, "DO",    XVfdCap_DiDoStatus,  0 },
    { XVfdRole_Ai1,            XIoPoint_AnalogInput,  100, "AI1",   XVfdCap_AiMonitor,   0 },
    { XVfdRole_Ai2,            XIoPoint_AnalogInput,  100, "AI2",   XVfdCap_AiMonitor,   0 },
    { XVfdRole_Ai3,            XIoPoint_AnalogInput,  100, "AI3",   XVfdCap_AiMonitor,   0 },
    { XVfdRole_PidSetpoint,    XIoPoint_AnalogInput,   1, "PIDS",  XVfdCap_PidMonitor,  0 },
    { XVfdRole_PidFeedback,    XIoPoint_AnalogInput,   1, "PIDF",  XVfdCap_PidMonitor,  0 },
    { XVfdRole_PlcStep,        XIoPoint_AnalogInput,   1, "PLC",   0,                   0 },
    { XVfdRole_PulseFreq,      XIoPoint_AnalogInput,  100, "PULSE", 0,                   0 },
    { XVfdRole_LoadSpeed,      XIoPoint_AnalogInput,   1, "LOAD",  0,                   0 },
    { XVfdRole_PowerOnHours,   XIoPoint_AnalogInput,  60, "PON",   XVfdCap_RuntimeStats, 0 },
    { XVfdRole_RunHours,       XIoPoint_AnalogInput,  600, "RUN",   XVfdCap_RuntimeStats, 0 },
    { XVfdRole_SetFreq,        XIoPoint_AnalogInput,  100, "FRQ",   0,                   0 },
    { XVfdRole_MotorTemp,      XIoPoint_AnalogInput,   1, "MTMP",  XVfdCap_MotorTemp,   0 },
    /* 最大频率既是遥测又是可调参数（P0.10）：用 AO 承载——AO 的 ReadRaw 同样
     * 读 Hub 缓存，故一点即可读可写（AI 点不可写，会挡住参数写入）。 */
    { XVfdRole_MaxFreq,        XIoPoint_AnalogOutput, 100, "MAXF",  0,                   0 },
    /* ---- 写点（控制） ---- */
    { XVfdRole_CommandWord,    XIoPoint_AnalogOutput,  1, "CMD",   0,                   0 },
    /* 1000H 通讯设定值是 16 位补码（-10000~+10000 = ±100.00%）：必须标有符号，
     * 否则通道层的无符号钳位会把反转给负频率写成 0。 */
    { XVfdRole_FreqSet,        XIoPoint_AnalogOutput, 100, "FREQ%", 0,                   1 },
    { XVfdRole_DoForce,        XIoPoint_AnalogOutput,  1, "DOF",   XVfdCap_DoForce,     0 },
    { XVfdRole_AoForce,        XIoPoint_AnalogOutput,  1, "AOF1",  XVfdCap_AoForce,     0 },
    { XVfdRole_AoForce2,       XIoPoint_AnalogOutput,  1, "AOF2",  XVfdCap_AoForce,     0 },
    { XVfdRole_PulseForce,     XIoPoint_AnalogOutput,  1, "PULOF", XVfdCap_PulseForce,  0 },
    /* PC.00~PC.15 多段指令同样是 ±100% 的补码量。 */
    { XVfdRole_PresetSpeed,    XIoPoint_AnalogOutput, 100, "PC00",  XVfdCap_MultiSpeed | XVfdCap_PresetSpeed16, 1 },
    { XVfdRole_PidTarget,      XIoPoint_AnalogOutput,  1, "PA01",  0,                   0 },
    { XVfdRole_Password,       XIoPoint_AnalogOutput,  1, "PWD",   XVfdCap_Password,    0 },
    { XVfdRole_ParamInit,      XIoPoint_AnalogOutput,  1, "PINIT", XVfdCap_FactoryReset, 0 },
    { XVfdRole_CommandSource,  XIoPoint_AnalogOutput,  1, "CMDSRC", 0,                   0 },
    { XVfdRole_FreqSource,     XIoPoint_AnalogOutput,  1, "FRQSRC", 0,                   0 },
    { XVfdRole_PidSource,      XIoPoint_AnalogOutput,  1, "PIDSRC", 0,                   0 },
    { XVfdRole_AccelTime,      XIoPoint_AnalogOutput, 10, "ACC",   0,                   0 },
    { XVfdRole_DecelTime,      XIoPoint_AnalogOutput, 10, "DEC",   0,                   0 },
};

#define M512_SPEC_COUNT ((int)(sizeof(s_m512Specs) / sizeof(s_m512Specs[0])))

/* ==================== 命令码表（离散命令枚举骨架用） ==================== */

static const XVfdCommandEnumItem s_m512Commands[] = {
    { XVfdM512_CmdRunForward, XVfdOperation_Start,        false },
    { XVfdM512_CmdRunReverse, XVfdOperation_Start,        true  },
    { XVfdM512_CmdJogForward, XVfdOperation_Jog,          false },
    { XVfdM512_CmdJogReverse, XVfdOperation_Jog,          true  },
    { XVfdM512_CmdFreeStop,   XVfdOperation_FreeStop,     false },
    { XVfdM512_CmdDecelStop,  XVfdOperation_Stop,         false },
    { XVfdM512_CmdFaultReset, XVfdOperation_ResetFault,   false }
};

#define M512_COMMAND_COUNT ((int)(sizeof(s_m512Commands) / sizeof(s_m512Commands[0])))

/* ==================== 参数描述表 ====================
 * m_addr = 手册原始地址；m_addrRam = 「只改 RAM 不落 EEPROM」地址变体
 * （附录1：P 组 FxxxH→0xxxH、A 组 AxxxH→4xxxH；PP/其它组手册未给变体）。 */

static const XVfdParamDesc s_m512Params[] = {
    /* name                     unit   dec  min     max     def     access            group */
    { "P0.01 控制方式",         NULL,   0,  0,      2,      2,      XVfdParamAccess_RunLocked, 0 },
    { "P0.02 命令源选择",       NULL,   0,  0,      2,      0,      XVfdParamAccess_RW, 0 },
    { "P0.03 主频率源X选择",    NULL,   0,  0,      9,      1,      XVfdParamAccess_RunLocked, 0 },
    { "P0.04 辅助频率源Y选择",  NULL,   0,  0,      9,      0,      XVfdParamAccess_RunLocked, 0 },
    { "P0.08 预置频率",         "Hz",   2,  0,      500000, 5000,   XVfdParamAccess_RW, 0 },
    { "P0.10 最大频率",         "Hz",   2,  5000,   500000, 5000,   XVfdParamAccess_RunLocked, 0 },
    { "P0.12 上限频率",         "Hz",   2,  0,      500000, 5000,   XVfdParamAccess_RunLocked, 0 },
    { "P0.14 下限频率",         "Hz",   2,  0,      500000, 0,      XVfdParamAccess_RunLocked, 0 },
    { "P0.15 载波频率",         "kHz",  1,  5,      160,    0,      XVfdParamAccess_RW, 0 },
    { "P0.17 加速时间1",        "s",    1,  0,      650000, 0,      XVfdParamAccess_RW, 0 },
    { "P0.18 减速时间1",        "s",    1,  0,      650000, 0,      XVfdParamAccess_RW, 0 },
    { "P0.19 加减速时间单位",   NULL,   0,  0,      2,      1,      XVfdParamAccess_RunLocked, 0 },
    { "P0.22 频率指令分辨率",   NULL,   0,  1,      2,      1,      XVfdParamAccess_RunLocked, 0 },
    { "P0.25 加减速时间基准频率", NULL, 0,  0,      2,      0,      XVfdParamAccess_RunLocked, 0 },
    { "P0.27 命令源捆绑频率源",  NULL,   0,  0,      999,    0,      XVfdParamAccess_RW, 0 },
    { "P0.28 串口通讯协议选择",  NULL,   0,  0,      3,      0,      XVfdParamAccess_RunLocked, 0 },
    { "P1.00 电机类型选择",     NULL,   0,  0,      2,      0,      XVfdParamAccess_RunLocked, 1 },
    { "P1.01 电机额定功率",     "kW",   1,  10,     10000,  0,      XVfdParamAccess_RunLocked, 1 },
    { "P1.02 电机额定电压",     "V",    0,  1,      2000,   0,      XVfdParamAccess_RunLocked, 1 },
    { "P1.03 电机额定电流",     "A",    2,  1,      65535,  0,      XVfdParamAccess_RunLocked, 1 },
    { "P1.04 电机额定频率",     "Hz",   2,  1,      500000, 0,      XVfdParamAccess_RunLocked, 1 },
    { "P1.05 电机额定转速",     "rpm",  0,  1,      65535,  0,      XVfdParamAccess_RunLocked, 1 },
    { "P9.00 电机过载保护选择", NULL,   0,  0,      1,      1,      XVfdParamAccess_RW,        9 },
    { "P9.01 电机过载保护增益", NULL,   2,  20,     1000,   100,    XVfdParamAccess_RW,        9 },
    { "P9.09 故障自动复位次数", NULL,   0,  0,      20,     0,      XVfdParamAccess_RW,        9 },
    { "P8.00 点动运行频率",     "Hz",   2,  0,      500000, 200,    XVfdParamAccess_RW,        8 },
    { "P8.01 点动加速时间",     "s",    1,  0,      65000,  200,    XVfdParamAccess_RW,        8 },
    { "P8.02 点动减速时间",     "s",    1,  0,      65000,  200,    XVfdParamAccess_RW,        8 },
    { "PA.00 PID 给定源",       NULL,   0,  0,      7,      7,      XVfdParamAccess_RunLocked, 10 },
    { "PA.01 PID 数字给定",     NULL,   0,  0,      100,    3,      XVfdParamAccess_RW,        10 },
    { "PA.02 PID 反馈源",       NULL,   0,  0,      8,      0,      XVfdParamAccess_RW,        10 },
    { "PA.03 PID 作用方向",     NULL,   0,  0,      1,      0,      XVfdParamAccess_RW,        10 },
    { "PA.04 PID 给定反馈量程", NULL,   0,  0,      100,    10,     XVfdParamAccess_RW,        10 },
    { "PC.00 多段指令0",        "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.01 多段指令1",        "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.02 多段指令2",        "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.03 多段指令3",        "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.04 多段指令4",        "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.05 多段指令5",        "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.06 多段指令6",        "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.07 多段指令7",        "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.08 多段指令8",        "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.09 多段指令9",        "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.10 多段指令10",       "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.11 多段指令11",       "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.12 多段指令12",       "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.13 多段指令13",       "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.14 多段指令14",       "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "PC.15 多段指令15",       "%",    2,  -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { "Pd.00 通讯波特率",       NULL,   0,  0,      65535,  6005,   XVfdParamAccess_RunLocked, 13 },
    { "Pd.01 MODBUS数据格式",   NULL,   0,  0,      3,      0,      XVfdParamAccess_RunLocked, 13 },
    { "Pd.02 本机地址",         NULL,   0,  0,      247,    1,      XVfdParamAccess_RunLocked, 13 },
    { "Pd.03 MODBUS应答延时",   "ms",   0,  0,      20,     2,      XVfdParamAccess_RW,        13 },
    { "Pd.04 串口通讯超时时间", "s",    1,  0,      600,    0,      XVfdParamAccess_RW,        13 },
    { "Pd.05 通讯数据格式",     NULL,   0,  0,      31,     0,      XVfdParamAccess_RunLocked, 13 },
    { "PP.00 用户密码",         NULL,   0,  0,      65535,  0,      XVfdParamAccess_RW,        14 },
    { "PP.01 参数初始化",       NULL,   0,  0,      501,    0,      XVfdParamAccess_RunLocked, 14 },
    { "A0.01 转矩设定源",       NULL,   0,  0,      7,      0,      XVfdParamAccess_RunLocked, 14 },
    { "A0.03 转矩数字设定",     "%",    1,  -2000,  2000,   1500,   XVfdParamAccess_RW,        14 }
};

#define M512_PARAM_COUNT ((int)(sizeof(s_m512Params) / sizeof(s_m512Params[0])))

/* ==================== 故障码表 ====================
 * 附录1「8000H 变频器故障描述」原文 41 条（权威）。m_name 是**人工对应**的
 * 面板显示码：手册第五章的 Err nn 与 8000H 是**两套编号**（例：面板
 * Err01=E.Igbt 在 8000H 表里根本没有对应项；面板 Err08=E.CPF 在 8000H 里
 * 是 0x0008=缓冲电阻过载），因此不可按数值互推，本表的字母码仅供现场
 * 人工核对；无确定对应的条目留 NULL。 */

static const XVfdFaultEntry s_m512Faults[] = {
    { 0x0000, NULL,      "无故障" },
    { 0x0001, NULL,      "保留" },
    { 0x0002, "E.oCAC",  "加速过电流" },
    { 0x0003, "E.oCdE",  "减速过电流" },
    { 0x0004, "E.oCCo",  "恒速过电流" },
    { 0x0005, "E.oUAC",  "加速过电压" },
    { 0x0006, "E.oUdE",  "减速过电压" },
    { 0x0007, "E.oUCo",  "恒速过电压" },
    { 0x0008, NULL,      "缓冲电阻过载故障" },
    { 0x0009, "E.LU",    "欠压故障" },
    { 0x000A, "E.oL1",   "变频器过载" },
    { 0x000B, "E.oLt",   "电机过载" },
    { 0x000C, "E.ILF",   "输入缺相" },
    { 0x000D, "E.oLF",   "输出缺相" },
    { 0x000E, "E.oH1",   "模块过热" },
    { 0x000F, "E.EIOF",  "外部故障" },
    { 0x0010, "E.CoF1",  "通讯异常" },
    { 0x0011, "E.rECF",  "接触器异常" },
    { 0x0012, "E.HALL",  "电流检测故障" },
    { 0x0013, "E.tUnE",  "电机调谐故障" },
    { 0x0014, "E.PG1",   "编码器/PG卡故障" },
    { 0x0015, "E.EEP",   "参数读写异常" },
    { 0x0016, "E.HArd",  "变频器硬件故障" },
    { 0x0017, "E.SHot",  "电机对地短路故障" },
    { 0x0018, NULL,      "保留" },
    { 0x0019, NULL,      "保留" },
    { 0x001A, "E.ArA",   "运行时间到达" },
    { 0x001B, "E.USt1",  "用户自定义故障1" },
    { 0x001C, "E.USt2",  "用户自定义故障2" },
    { 0x001D, "E.APA",   "上电时间到达" },
    { 0x001E, "E.ULF",   "堵载" },
    { 0x001F, "E.PID",   "运行时PID反馈丢失" },
    { 0x0028, "E.CbC",   "快速限流超时故障" },
    { 0x0029, "E.tSr",   "运行时切换电机故障" },
    { 0x002A, "E.SdL",   "速度偏差过大" },
    { 0x002B, "E.oSF",   "电机超速度" },
    { 0x002D, "E.oHt",   "电机过温" },
    { 0x005A, NULL,      "编码器线数设定错误" },
    { 0x005B, NULL,      "未接编码器" },
    { 0x005C, "E.PoSF",  "初始位置错误" },
    { 0x005E, NULL,      "速度反馈错误" }
};

#define M512_FAULT_COUNT ((int)(sizeof(s_m512Faults) / sizeof(s_m512Faults[0])))

/* ==================== 从站协议约束 ====================
 * 附录1：只支持 FC03（读）与 FC06（写）；单次读 n 最大 12；一帧读不得跨过
 * 本功能码组的最后一个功能码。 */


/* ==================== 解码 ==================== */

/**
 * @brief DecodePoint 重载：3000H 运行状态与 8000H 故障派生，其余交基类。
 * @param d 设备实例。
 * @param spec 点规格。
 * @param raw 原生值。
 * @return 无。
 */
static void VXVfdM512_DecodePoint(XVfdDevice* d, const XVfdPointSpec* spec, int32_t raw)
{
    XVfdStatus* st;
    if (!d || !spec) return;
    st = XVfdDevice_status(d);
    switch (spec->m_role) {
    case XVfdRole_StatusWord:
        st->m_rawStateWord = (uint16_t)raw;
        st->m_validMask |= XVfdField_RawStateWord | XVfdField_RunState
                         | XVfdField_Running | XVfdField_Reverse;
        switch ((uint16_t)raw) {
        case 1:
            st->m_runState = XVfdRunState_RunningForward;
            st->m_running = true;
            st->m_reverse = false;
            break;
        case 2:
            st->m_runState = XVfdRunState_RunningReverse;
            st->m_running = true;
            st->m_reverse = true;
            break;
        case 3:
            st->m_runState = XVfdRunState_Stopped;
            st->m_running = false;
            st->m_reverse = false;
            break;
        default:
            st->m_runState = XVfdRunState_Unknown;
            st->m_running = false;
            break;
        }
        if (st->m_faulted) st->m_runState = XVfdRunState_Fault;
        XVfdDevice_markDirty(d);
        return;
    case XVfdRole_FaultCode:
        st->m_faultCode = (uint16_t)raw;
        st->m_faulted = (raw != 0);
        if (st->m_faulted) st->m_runState = XVfdRunState_Fault;
        st->m_validMask |= XVfdField_FaultCode | XVfdField_Faulted;
        XVfdDevice_markDirty(d);
        return;
    default:
        break;
    }
    XVfdDevice_decodePoint_base(d, spec, raw);
}

/* ==================== 频率换算 ==================== */

/**
 * @brief 取频率换算基准（最大频率）：先读点刷新缓存，缺失按 50Hz 兜底。
 * @param dev 实例；不能为 NULL。
 * @return 最大频率（Hz，>0）。
 */
static double m512MaxFreq(XVfdM512* dev)
{
    int32_t raw = 0;
    if (!dev) return 50.0;
    if (XVfdDevice_readRole(&dev->m_base.m_base, XVfdRole_MaxFreq, &raw) && raw > 0) {
        XVfdDevice_setMaxFrequency(&dev->m_base.m_base,
                                    XVfdDevice_rawToEngineering(raw, XVfdM512_MaxFreqDiv));
    }
    if (XVfdDevice_maxFrequency(&dev->m_base.m_base) > 0.0) {
        return XVfdDevice_maxFrequency(&dev->m_base.m_base);
    }
    return 50.0;
}

/**
 * @brief Hz → 通讯设定值原始码（±10000 = ±100% 最大频率）。
 * @param dev 实例；不能为 NULL。
 * @param hz 目标频率（Hz）。
 * @return 原始码（已钳位 ±10000）。
 */
static int32_t m512FreqToRaw(XVfdM512* dev, double hz)
{
    double base = m512MaxFreq(dev);
    double percent = hz / base * 100.0;
    return XVfdDevice_engineeringToRaw(percent, XVfdM512_FreqPercentDiv, -10000, 10000);
}

/* ==================== 通讯通道前置检查 ==================== */

/**
 * @brief 校验「源选择」是否已指向通讯通道（附录1 的功能可用性前提）。
 * @param d 设备实例。
 * @param role 源选择语义槽。
 * @param required 通讯通道枚举值。
 * @param message 未配置时的提示（静态常量）。
 * @return 已配置（或无法校验）返回 true；明确未配置返回 false。
 * @note 槽未绑定或点尚无数据时**放行**：无法校验不等于未配置，交由现场与
 *       手册兜底；一旦读到且不是通讯通道，立即报错——否则命令会被从站静默
 *       忽略（2000H 仅在命令源=通讯时生效，1000H 仅在主频率源=通讯给定时生效）。
 */
static bool m512CheckSource(XVfdDevice* d, XVfdPointRole role, int32_t required,
                            const char* message)
{
    int32_t v = 0;
    if (!XVfdDevice_readRole(d, role, &v)) return true;
    if (v == required) return true;
    XVfdDevice_setLastError(d, XVfdError_WriteRejected);
    XVfdDevice_emitError(d, message);
    return false;
}

/* ==================== 动作实现 ==================== */

/**
 * @brief 命令字单写动作（走通用离散命令枚举骨架）。
 * @param dev 实例。
 * @param op 动作类型。
 * @param reverse 是否反转方向。
 * @return 受理返回 true。
 */
static bool m512Command(XVfdM512* dev, XVfdOperation op, bool reverse)
{
    return XVfdCommandEnum_write(&dev->m_base, op, reverse, XVfdRole_CommandWord);
}

static bool VXVfdM512_Start(XVfdDevice* d, bool reverse)
{
    XVfdM512* dev = (XVfdM512*)d;
    if (!dev) return false;
    /* [前置] 2000H 命令字仅在「命令源=通讯」时生效；未配置时从站会静默忽略。 */
    if (!m512CheckSource(d, XVfdRole_CommandSource, XVfdM512_CmdSourceComm,
                         "命令源未切到通讯（P0.02=2）：从站会忽略命令字，"
                         "请先调用 XVfdM512_setCommandSourceComm")) {
        return false;
    }
    return m512Command(dev, XVfdOperation_Start, reverse);
}

static bool VXVfdM512_Stop(XVfdDevice* d)
{
    XVfdM512* dev = (XVfdM512*)d;
    if (!dev) return false;
    return m512Command(dev, XVfdOperation_Stop, false);
}

static bool VXVfdM512_FreeStop(XVfdDevice* d)
{
    XVfdM512* dev = (XVfdM512*)d;
    if (!dev) return false;
    return m512Command(dev, XVfdOperation_FreeStop, false);
}

static bool VXVfdM512_Jog(XVfdDevice* d, bool reverse)
{
    XVfdM512* dev = (XVfdM512*)d;
    if (!dev) return false;
    return m512Command(dev, XVfdOperation_Jog, reverse);
}

static bool VXVfdM512_ResetFault(XVfdDevice* d)
{
    XVfdM512* dev = (XVfdM512*)d;
    if (!dev) return false;
    return m512Command(dev, XVfdOperation_ResetFault, false);
}

/**
 * @brief 写频率给定：Hz → 最大频率百分比 → 1000H（±10000）。
 * @param d 设备实例。
 * @param hz 目标频率（Hz）。
 * @return 受理返回 true。
 */
static bool VXVfdM512_SetFrequency(XVfdDevice* d, double hz)
{
    XVfdM512* dev = (XVfdM512*)d;
    int32_t raw;
    if (!dev) return false;
    /* [前置] 1000H 通讯设定值仅在「主频率源=通讯给定」时生效。 */
    if (!m512CheckSource(d, XVfdRole_FreqSource, XVfdM512_FreqSourceComm,
                         "主频率源未切到通讯给定（P0.03=9）：从站会忽略频率给定，"
                         "请先调用 XVfdM512_setFrequencySourceComm")) {
        return false;
    }
    raw = m512FreqToRaw(dev, hz);
    return XVfdDevice_actionWrite(d, XVfdOperation_SetFrequency, XVfdRole_FreqSet, raw);
}

/**
 * @brief 写加减速时间（两步：P0.17 → P0.18）。
 * @param d 设备实例。
 * @param index 组号（仅 0 组支持）。
 * @param accelS 加速时间（秒）。
 * @param decelS 减速时间（秒）。
 * @return 受理返回 true。
 */
static bool VXVfdM512_SetAccelDecel(XVfdDevice* d, int index, double accelS, double decelS)
{
    XVfdM512* dev = (XVfdM512*)d;
    int32_t acc;
    if (!dev) return false;
    if (index != 0) {
        XVfdDevice_setLastError(d, XVfdError_InvalidParam);
        return false;
    }
    acc = XVfdDevice_engineeringToRaw(accelS, XVfdM512_TimeDiv, 0, 650000);
    dev->m_pendingRaw = (uint16_t)XVfdDevice_engineeringToRaw(decelS, XVfdM512_TimeDiv,
                                                                0, 650000);
    dev->m_stage = 0;
    if (!XVfdDevice_beginAction(d, XVfdOperation_SetAccelDecel)) return false;
    if (!XVfdDevice_writeRole(d, XVfdRole_AccelTime, acc)) {
        XVfdDevice_finishAction(d, false);
        return false;
    }
    XVfdDevice_armActionTimer(d, XVfdDevice_actionTimeout(d));
    return true;
}

/**
 * @brief 写多段速值（PC.00~PC.15；原生是「最大频率百分数」）。
 * @param d 设备实例。
 * @param index 段号（0~15）。
 * @param hz 段速（Hz）。
 * @return 受理返回 true。
 */
static bool VXVfdM512_SetPresetSpeedHz(XVfdDevice* d, int index, double hz)
{
    XVfdM512* dev = (XVfdM512*)d;
    int32_t raw;
    if (!dev) return false;
    if (index < 0 || index >= XVfdM512_AddrPresetCount) {
        XVfdDevice_setLastError(d, XVfdError_InvalidParam);
        return false;
    }
    raw = m512FreqToRaw(dev, hz);
    /* 16 段速各占一个 PC.xx 地址：点表只绑了 0 段，其余段经参数通道写。 */
    if (index == 0) {
        return XVfdDevice_actionWrite(d, XVfdOperation_SetPresetSpeedHz,
                                      XVfdRole_PresetSpeed, raw);
    }
    XVfdDevice_setLastError(d, XVfdError_NotSupported);
    XVfdDevice_emitError(d, "多段速 1~15 段请经参数通道写（PC.01~PC.15）");
    return false;
}

/**
 * @brief 写 PID 数字给定（PA.01；单位是「量程」不是 %）。
 * @param d 设备实例。
 * @param value 目标值（量程单位）。
 * @return 受理返回 true。
 */
static bool VXVfdM512_SetPidTarget(XVfdDevice* d, double value)
{
    XVfdM512* dev = (XVfdM512*)d;
    int32_t raw;
    if (!dev) return false;
    if (!m512CheckSource(d, XVfdRole_PidSource, XVfdM512_PidSourceComm,
                         "PID 给定源未切到通讯给定（PA.00=5）：从站会忽略 PID 给定，"
                         "请先调用 XVfdM512_setPidSourceComm")) {
        return false;
    }
    raw = XVfdDevice_engineeringToRaw(value, 1, 0, 100);
    return XVfdDevice_actionWrite(d, XVfdOperation_SetPidTarget, XVfdRole_PidTarget, raw);
}

/**
 * @brief 输出端子通讯强制（2001H 位图整体写）。
 * @param d 设备实例。
 * @param index 端子序号（0~9）。
 * @param on true=置位。
 * @return 受理返回 true。
 */
static bool VXVfdM512_SetDigitalOutput(XVfdDevice* d, int index, bool on)
{
    int32_t bits;
    if (index < 0 || index > 9) {
        XVfdDevice_setLastError(d, XVfdError_InvalidParam);
        return false;
    }
    bits = on ? (int32_t)(1u << index) : 0;
    return XVfdDevice_actionWrite(d, XVfdOperation_SetDigitalOutput, XVfdRole_DoForce, bits);
}

/**
 * @brief 模拟输出强制（2002H/2003H；0~0x7FFF 对应 0~100%）。
 * @param dev 实例。
 * @param which 0=AO1 1=AO2。
 * @param percent 百分比（0~100）。
 * @return 受理返回 true。
 */
static bool m512SetAoForce(XVfdM512* dev, int which, double percent)
{
    int32_t raw;
    XVfdPointRole role = (which == 0) ? XVfdRole_AoForce : XVfdRole_AoForce2;
    if (!dev) return false;
    raw = XVfdDevice_engineeringToRaw(percent, 1, 0, 0x7FFF);
    return XVfdDevice_actionWrite(&dev->m_base.m_base, XVfdOperation_SetDigitalOutput,
                                  role, raw);
}

/**
 * @brief 恢复出厂（1F01H：1=不含电机参数 / 2=含电机参数）。
 * @param d 设备实例。
 * @param scope 范围（0/1）。
 * @return 受理返回 true。
 */
static bool VXVfdM512_FactoryReset(XVfdDevice* d, int scope)
{
    int32_t cmd;
    if (scope != 0 && scope != 1) {
        XVfdDevice_setLastError(d, XVfdError_InvalidParam);
        return false;
    }
    cmd = (scope == 0) ? 1 : 2;
    return XVfdDevice_actionWrite(d, XVfdOperation_FactoryReset, XVfdRole_ParamInit, cmd);
}

/**
 * @brief 密码解锁（1F00H，从站回 8888H 表示通过）。
 * @param d 设备实例。
 * @param password 密码原值。
 * @return 受理返回 true。
 */
static bool VXVfdM512_UnlockPassword(XVfdDevice* d, uint32_t password)
{
    return XVfdDevice_actionWrite(d, XVfdOperation_UnlockPassword, XVfdRole_Password,
                                  (int32_t)(password & 0xFFFFu));
}

/**
 * @brief ActionWriteDone 重载：加减速两步写推进，其余默认收口。
 * @param d 设备实例。
 * @param point 落定的点（未用）。
 * @param ok 写落定结果。
 * @return 无。
 */
static void VXVfdM512_ActionWriteDone(XVfdDevice* d, XIoPoint* point, bool ok)
{
    XVfdM512* dev = (XVfdM512*)d;
    (void)point;
    if (!dev) return;
    if (XVfdDevice_currentOp(d) == XVfdOperation_SetAccelDecel) {
        if (!ok) {
            XVfdDevice_finishAction(d, false);
            return;
        }
        if (dev->m_stage == 0) {
            dev->m_stage = 1;
            if (!XVfdDevice_writeRole(d, XVfdRole_DecelTime, (int32_t)dev->m_pendingRaw)) {
                XVfdDevice_finishAction(d, false);
                return;
            }
            XVfdDevice_armActionTimer(d, XVfdDevice_actionTimeout(d));
            return;
        }
    }
    XVfdDevice_finishAction(d, ok);
}

/* ==================== 虚函数表 / 生命周期 ==================== */

XVtable* XVfdM512_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVfdM512)
    XVTABLE_INHERIT_XCLASS(XVfdCommandEnum);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_DecodePoint, VXVfdM512_DecodePoint);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_ActionWriteDone, VXVfdM512_ActionWriteDone);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_Start, VXVfdM512_Start);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_Stop, VXVfdM512_Stop);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_FreeStop, VXVfdM512_FreeStop);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_Jog, VXVfdM512_Jog);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_SetFrequency, VXVfdM512_SetFrequency);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_SetAccelDecel, VXVfdM512_SetAccelDecel);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_SetPresetSpeedHz, VXVfdM512_SetPresetSpeedHz);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_SetPidTarget, VXVfdM512_SetPidTarget);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_ResetFault, VXVfdM512_ResetFault);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_SetDigitalOutput, VXVfdM512_SetDigitalOutput);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_FactoryReset, VXVfdM512_FactoryReset);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_UnlockPassword, VXVfdM512_UnlockPassword);
    XCLASS_SET_CLASS_NAME_DEFAULT("XVfdM512");
    XCLASS_SHOW_SIZE_DEFAULT(XVfdM512);
    return XVTABLE_DEFAULT;
}

XVfdM512* XVfdM512_create_ex(XMemoryType memory)
{
    XVfdM512* dev = (XVfdM512*)XMemory_malloc(sizeof(XVfdM512), memory);
    if (!dev) return NULL;
    XVfdM512_init(dev);
    Set_Class_Memory(dev, memory);
    Set_Class_IsHeap(dev, true);
    return dev;
}

void XVfdM512_init(XVfdM512* dev)
{
    if (!dev) return;
    memset(dev, 0, sizeof(XVfdM512));
    XVfdCommandEnum_init(&dev->m_base);
    XClassGetVtable(dev) = XVfdM512_class_init();
    XVfdCommandEnum_setCommands(&dev->m_base, s_m512Commands, M512_COMMAND_COUNT);
    XVfdDevice_setParamTable(&dev->m_base.m_base, s_m512Params, M512_PARAM_COUNT);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_Reverse);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_Jog);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_FreeStop);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_ParamAccess);
    /* 手册缺省：最大频率 50Hz（P0.10 出厂值），先预设可让首次调频即正确换算。 */
    XVfdDevice_setMaxFrequency(&dev->m_base.m_base, 50.0);
}

/* ==================== 点表 / 组表 / 参数表 / 配置 ==================== */

const XVfdPointSpec* XVfdM512_pointSpecs(int* count)
{
    if (count) *count = M512_SPEC_COUNT;
    return s_m512Specs;
}

const XVfdParamDesc* XVfdM512_paramDescs(int* count)
{
    if (count) *count = M512_PARAM_COUNT;
    return s_m512Params;
}

void XVfdM512_setMaxFrequency(XVfdM512* dev, double hz)
{
    if (!dev) return;
    XVfdDevice_setMaxFrequency(&dev->m_base.m_base, hz);
}

double XVfdM512_maxFrequency(const XVfdM512* dev)
{
    if (!dev) return 0.0;
    return XVfdDevice_maxFrequency(&dev->m_base.m_base);
}

/* ==================== 通讯通道前置（附录1 的功能可用性前提） ==================== */

bool XVfdM512_setCommandSourceComm(XVfdM512* dev)
{
    if (!dev) return false;
    return XVfdDevice_actionWrite(&dev->m_base.m_base, XVfdOperation_SetCommandSource,
                                  XVfdRole_CommandSource, XVfdM512_CmdSourceComm);
}

bool XVfdM512_setFrequencySourceComm(XVfdM512* dev)
{
    if (!dev) return false;
    return XVfdDevice_actionWrite(&dev->m_base.m_base, XVfdOperation_SetFrequencySource,
                                  XVfdRole_FreqSource, XVfdM512_FreqSourceComm);
}

bool XVfdM512_setPidSourceComm(XVfdM512* dev)
{
    if (!dev) return false;
    return XVfdDevice_actionWrite(&dev->m_base.m_base, XVfdOperation_SetPidSource,
                                  XVfdRole_PidSource, XVfdM512_PidSourceComm);
}

/**
 * @brief 模拟输出通讯强制（附录1：2002H=AO1、2003H=AO2）。
 * @param dev 实例（不能为 NULL）。
 * @param which 0=AO1 1=AO2。
 * @param percent 百分比（0~100）。
 * @return 受理返回 true；参数非法返回 false。
 * @note 对应输出端子的功能选择须为「通讯控制」时才生效。
 */
bool XVfdM512_setAnalogOutputForce(XVfdM512* dev, int which, double percent)
{
    if (!dev) return false;
    if (which < 0 || which > 1) {
        XVfdDevice_setLastError(&dev->m_base.m_base, XVfdError_InvalidParam);
        return false;
    }
    return m512SetAoForce(dev, which, percent);
}

/* ==================== 故障码查询 ==================== */

const char* XVfdM512_faultName(uint16_t code)
{
    const XVfdFaultEntry* e = XVfdDevice_faultEntry_const(s_m512Faults, M512_FAULT_COUNT, code);
    return e ? e->m_name : NULL;
}

const char* XVfdM512_faultText(uint16_t code)
{
    const XVfdFaultEntry* e = XVfdDevice_faultEntry_const(s_m512Faults, M512_FAULT_COUNT, code);
    return e ? e->m_text : NULL;
}

#endif /* XVFD_M512_ON */
#endif /* XVFD_ON */
