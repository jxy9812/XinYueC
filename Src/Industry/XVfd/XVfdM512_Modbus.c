/**
 * @file       XVfdM512_Modbus.c
 * @brief      511M/512M/513M 系列的 **Modbus 绑定表**（地址知识）。
 * @details    [2026-11-02 分层收口] 本文件承载 M512 在 Modbus 上的全部地址知识：
 *             点绑定（role → 寄存器）、参数绑定（paramId → 寄存器）、
 *             通信组（功能码组边界）、从站约束。
 *
 *             品牌主文件 XVfdM512.c 只剩语义（role/type/div/caps/signed/tag），
 *             不含任何地址——这正是"换总线不换语义"的落点：将来接 EtherCAT，
 *             新增 XVfdM512_EtherCAT.c 提供另一份绑定表即可，主文件不动。
 *
 *             [通信组的取舍] 每条只覆盖「本实现实际用到的那一段连续地址」，
 *             不整段铺开：手册条目内含大量未用地址（U 组 0x7000~0x7074 有 75 字，
 *             本实现只用电机温度一处），整段映射会让读帧数暴涨。手册未印的
 *             空洞地址一律不映射、不读。
 */
#include "XVfdM512.h"
#include "XVfdBind.h"

#if XVFD_ON && XVFD_M512_ON

#include "XIoPoint.h"

/* ==================== 通信组表（功能码组边界） ==================== */
static const XVfdCommGroup s_m512Groups[] = {
    /* remoteStart count  area(区)                 手册条目 */
    { 0x1000,  1,  XIoPoint_AnalogOutput,  }, /* 1000H 通讯设定值 */
    { 0x1001,  8,  XIoPoint_AnalogInput,   }, /* 1001H~1008H 频率/母线/电压/电流/功率/转矩/DI/DO */
    { 0x1009,  3,  XIoPoint_AnalogInput,   }, /* 1009H~100BH AI1~AI3 */
    { 0x1010,  4,  XIoPoint_AnalogInput,   }, /* 1010H~1013H PID设定/反馈/PLC阶段/脉冲频率 */
    { 0x1019,  3,  XIoPoint_AnalogInput,   }, /* 1019H~101BH 线速度/上电时间/运行时间 */
    { 0x101F,  2,  XIoPoint_AnalogInput,   }, /* 101FH~1020H 主频率X/辅频率Y 显示 */
    { 0x1F00,  1,  XIoPoint_AnalogOutput,  }, /* 1F00H 用户密码校验 */
    { 0x1F01,  1,  XIoPoint_AnalogOutput,  }, /* 1F01H 参数初始化 */
    { 0x2000,  5,  XIoPoint_AnalogOutput,  }, /* 2000H~2004H 命令/DO强制/AO1/AO2/FMP强制 */
    { 0x3000,  1,  XIoPoint_AnalogInput,   }, /* 3000H 变频器运行状态 */
    { 0x7022,  1,  XIoPoint_AnalogInput,   }, /* U0.34 电机温度（U 组只取用到的这一址） */
    { 0x8000,  1,  XIoPoint_AnalogInput,   }, /* 8000H 变频器故障描述 */
    { 0xF001,  4,  XIoPoint_AnalogOutput,  }, /* P0.01~P0.04 控制方式/命令源/频率源/辅频源 */
    { 0xF008,  8,  XIoPoint_AnalogOutput,  }, /* P0.08~P0.15 预置频率/最大频率/上下限/载波 */
    { 0xF011,  3,  XIoPoint_AnalogOutput,  }, /* P0.17~P0.19 加减速时间与时间单位 */
    { 0xF016,  1,  XIoPoint_AnalogOutput,  }, /* P0.22 频率指令分辨率 */
    { 0xF019,  1,  XIoPoint_AnalogOutput,  }, /* P0.25 加减速时间基准频率 */
    { 0xF01B,  2,  XIoPoint_AnalogOutput,  }, /* P0.27~P0.28 命令源捆绑与通讯协议 */
    { 0xF100,  6,  XIoPoint_AnalogOutput,  }, /* P1.00~P1.05 电机参数 */
    { 0xF800,  3,  XIoPoint_AnalogOutput,  }, /* P8.00~P8.02 点动频率与点动时间 */
    { 0xF900,  2,  XIoPoint_AnalogOutput,  }, /* P9.00~P9.01 电机过载保护 */
    { 0xF909,  1,  XIoPoint_AnalogOutput,  }, /* P9.09 故障自动复位次数 */
    { 0xFA00,  5,  XIoPoint_AnalogOutput,  }, /* PA.00~PA.04 PID */
    { 0xFC00, 16,  XIoPoint_AnalogOutput,  }, /* PC.00~PC.15 多段指令 0~15 */
    { 0xFD00,  6,  XIoPoint_AnalogOutput,  }, /* Pd.00~Pd.05 通信参数 */
    { 0xFF00,  2,  XIoPoint_AnalogOutput,  }, /* PP.00~PP.01 密码与参数初始化 */
    { 0xA001,  1,  XIoPoint_AnalogOutput,  }, /* A0.01 转矩设定源 */
    { 0xA003,  1,  XIoPoint_AnalogOutput,  }  /* A0.03 转矩数字设定 */
};
#define M512_GROUP_COUNT ((int)(sizeof(s_m512Groups) / sizeof(s_m512Groups[0])))

/* ==================== 点绑定表（role → 寄存器） ==================== */

static const XVfdPointBinding s_m512PointBinds[] = {
    /* role                     addr    bitOff bitW */
    { XVfdRole_StatusWord,     XVfdM512_AddrRunState, 0, 0 },
    { XVfdRole_FaultCode,      XVfdM512_AddrFault, 0, 0 },
    { XVfdRole_OutputFreq,     XVfdM512_AddrTelemetry + 0, 0, 0 },
    { XVfdRole_BusVoltage,     XVfdM512_AddrTelemetry + 1, 0, 0 },
    { XVfdRole_OutputVoltage,  XVfdM512_AddrTelemetry + 2, 0, 0 },
    { XVfdRole_OutputCurrent,  XVfdM512_AddrTelemetry + 3, 0, 0 },
    { XVfdRole_OutputPower,    XVfdM512_AddrTelemetry + 4, 0, 0 },
    { XVfdRole_Torque,         XVfdM512_AddrTelemetry + 5, 0, 0 },
    { XVfdRole_DiBits,         XVfdM512_AddrTelemetry + 6, 0, 0 },
    { XVfdRole_DoBits,         XVfdM512_AddrTelemetry + 7, 0, 0 },
    { XVfdRole_Ai1,            XVfdM512_AddrTelemetry + 8, 0, 0 },
    { XVfdRole_Ai2,            XVfdM512_AddrTelemetry + 9, 0, 0 },
    { XVfdRole_Ai3,            XVfdM512_AddrTelemetry + 10, 0, 0 },
    { XVfdRole_PidSetpoint,    XVfdM512_AddrTelemetry + 15, 0, 0 },
    { XVfdRole_PidFeedback,    XVfdM512_AddrTelemetry + 16, 0, 0 },
    { XVfdRole_PlcStep,        XVfdM512_AddrTelemetry + 17, 0, 0 },
    { XVfdRole_PulseFreq,      XVfdM512_AddrTelemetry + 18, 0, 0 },
    { XVfdRole_LoadSpeed,      XVfdM512_AddrTelemetry + 24, 0, 0 },
    { XVfdRole_PowerOnHours,   XVfdM512_AddrTelemetry + 25, 0, 0 },
    { XVfdRole_RunHours,       XVfdM512_AddrTelemetry + 26, 0, 0 },
    { XVfdRole_SetFreq,        XVfdM512_AddrTelemetry + 30, 0, 0 },
    { XVfdRole_MotorTemp,      XVfdM512_AddrMonitorBase + 34, 0, 0 },
    { XVfdRole_MaxFreq,        XVfdM512_AddrMaxFreq, 0, 0 },
    { XVfdRole_CommandWord,    XVfdM512_AddrCmdWord, 0, 0 },
    { XVfdRole_FreqSet,        XVfdM512_AddrFreqSet, 0, 0 },
    { XVfdRole_DoForce,        XVfdM512_AddrDoForce, 0, 0 },
    { XVfdRole_AoForce,        XVfdM512_AddrAoForce, 0, 0 },
    { XVfdRole_AoForce2,       XVfdM512_AddrAoForce2, 0, 0 },
    { XVfdRole_PulseForce,     XVfdM512_AddrPulseForce, 0, 0 },
    { XVfdRole_PresetSpeed,    XVfdM512_AddrPresetBase, 0, 0 },
    { XVfdRole_PidTarget,      XVfdM512_AddrPidTarget, 0, 0 },
    { XVfdRole_Password,       XVfdM512_AddrPassword, 0, 0 },
    { XVfdRole_ParamInit,      XVfdM512_AddrParamInit, 0, 0 },
    { XVfdRole_CommandSource,  XVfdM512_AddrCmdSource, 0, 0 },
    { XVfdRole_FreqSource,     XVfdM512_AddrFreqSource, 0, 0 },
    { XVfdRole_PidSource,      XVfdM512_AddrPidSource, 0, 0 },
    { XVfdRole_AccelTime,      XVfdM512_AddrAccelTime, 0, 0 },
    { XVfdRole_DecelTime,      XVfdM512_AddrDecelTime, 0, 0 },
};

#define M512_POINT_BIND_COUNT ((int)(sizeof(s_m512PointBinds) / sizeof(s_m512PointBinds[0])))

/* ==================== 参数绑定表（paramId → 寄存器） ==================== */

static const XVfdParamBinding s_m512ParamBinds[] = {
    /* paramId  addr   addrRam */
    { 0, 0xF001, 0 },
    { 1, 0xF002, 0 },
    { 2, 0xF003, 0 },
    { 3, 0xF004, 0 },
    { 4, 0xF008, 0 },
    { 5, 0xF00A, 0 },
    { 6, 0xF00C, 0 },
    { 7, 0xF00E, 0 },
    { 8, 0xF00F, 0 },
    { 9, 0xF011, 0 },
    { 10, 0xF012, 0 },
    { 11, 0xF013, 0 },
    { 12, 0xF016, 0 },
    { 13, 0xF019, 0 },
    { 14, 0xF01B, 0 },
    { 15, 0xF01C, 0 },
    { 16, 0xF100, 0 },
    { 17, 0xF101, 0 },
    { 18, 0xF102, 0 },
    { 19, 0xF103, 0 },
    { 20, 0xF104, 0 },
    { 21, 0xF105, 0 },
    { 22, 0xF900, 0 },
    { 23, 0xF901, 0 },
    { 24, 0xF909, 0 },
    { 25, 0xF800, 0 },
    { 26, 0xF801, 0 },
    { 27, 0xF802, 0 },
    { 28, 0xFA00, 0 },
    { 29, 0xFA01, 0 },
    { 30, 0xFA02, 0 },
    { 31, 0xFA03, 0 },
    { 32, 0xFA04, 0 },
    { 33, 0xFC00, 0 },
    { 34, 0xFC01, 0 },
    { 35, 0xFC02, 0 },
    { 36, 0xFC03, 0 },
    { 37, 0xFC04, 0 },
    { 38, 0xFC05, 0 },
    { 39, 0xFC06, 0 },
    { 40, 0xFC07, 0 },
    { 41, 0xFC08, 0 },
    { 42, 0xFC09, 0 },
    { 43, 0xFC0A, 0 },
    { 44, 0xFC0B, 0 },
    { 45, 0xFC0C, 0 },
    { 46, 0xFC0D, 0 },
    { 47, 0xFC0E, 0 },
    { 48, 0xFC0F, 0 },
    { 49, 0xFD00, 0 },
    { 50, 0xFD01, 0 },
    { 51, 0xFD02, 0 },
    { 52, 0xFD03, 0 },
    { 53, 0xFD04, 0 },
    { 54, 0xFD05, 0 },
    { 55, 0xFF00, 0 },
    { 56, 0xFF01, 0 },
    { 57, 0xA001, 0 },
    { 58, 0xA003, 0 },
};

#define M512_PARAM_BIND_COUNT ((int)(sizeof(s_m512ParamBinds) / sizeof(s_m512ParamBinds[0])))

/* ==================== 从站协议约束 ==================== */

static const XVfdUnitProtocol s_m512Protocol = {
    XVfdM512_MaxReadRegisters,   /* 单帧读上限 12 字 */
    true,                        /* 组边界为读帧硬边界 */
    true                         /* 只允许单寄存器写（FC06） */
};

/* ==================== 导出 ==================== */

/**
 * @brief 取 M512 的 Modbus 绑定表。
 * @return 静态常量绑定表指针；不可释放。
 */
const XVfdBindTable* XVfdM512_modbusBinding(void)
{
    static XVfdBindTable table;
    static bool inited = false;
    if (!inited) {
        table.m_points = s_m512PointBinds;
        table.m_pointCount = M512_POINT_BIND_COUNT;
        table.m_params = s_m512ParamBinds;
        table.m_paramCount = M512_PARAM_BIND_COUNT;
        table.m_groups = s_m512Groups;
        table.m_groupCount = M512_GROUP_COUNT;
        table.m_protocol = &s_m512Protocol;
        inited = true;
    }
    return &table;
}

#endif /* XVFD_ON && XVFD_M512_ON */
