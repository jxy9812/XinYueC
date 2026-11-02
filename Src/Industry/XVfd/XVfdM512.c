/**
 * @file       XVfdM512.c
 * @brief      511M/512M/513M 系列变频器子类实现（通道点驱动）。
 * @details    本文件只含品牌协议知识：点规格表、参数描述表、状态/故障解码、
 *             命令字编码、频率百分比换算（P0.10 基准）。数据进/控制出一律经
 *             基类（XVfdDevice_readRole/writeRole/actionWrite）下沉到通道点。
 */
#include "XVfdM512.h"
#if XVFD_ON
#if XVFD_M512_ON

#include "XVfdDevice_Protected.h"
#include <string.h>

/* ==================== 点规格表 ==================== */

static const XVfdPointSpec s_m512Specs[] = {
    /* role                    type                   div  addr                     tag        caps */
    { XVfdRole_StatusWord,     XIoPoint_AnalogInput,   1, XVfdM512_AddrRunState, "STATE",   0 },
    { XVfdRole_FaultCode,      XIoPoint_AnalogInput,   1, XVfdM512_AddrFault,    "FAULT",   0 },
    { XVfdRole_OutputFreq,     XIoPoint_AnalogInput, 100, XVfdM512_AddrTelemetry + 0, "RFR", 0 },
    { XVfdRole_BusVoltage,     XIoPoint_AnalogInput,  10, XVfdM512_AddrTelemetry + 1, "VBUS", 0 },
    { XVfdRole_OutputVoltage,  XIoPoint_AnalogInput,  10, XVfdM512_AddrTelemetry + 2, "UOP", 0 },
    { XVfdRole_OutputCurrent,  XIoPoint_AnalogInput, 100, XVfdM512_AddrTelemetry + 3, "LCR", 0 },
    { XVfdRole_OutputPower,    XIoPoint_AnalogInput,  10, XVfdM512_AddrTelemetry + 4, "OPR", XVfdCap_PowerReport },
    { XVfdRole_Torque,         XIoPoint_AnalogInput,  10, XVfdM512_AddrTelemetry + 5, "OTR", XVfdCap_TorqueReport },
    { XVfdRole_MaxFreq,        XIoPoint_AnalogInput, 100, XVfdM512_AddrMaxFreq,   "MAXF",    0 },
    { XVfdRole_CommandWord,    XIoPoint_AnalogOutput,  1, XVfdM512_AddrCmdWord,  "CMD",     0 },
    { XVfdRole_FreqSet,        XIoPoint_AnalogOutput, 100, XVfdM512_AddrFreqSet, "FREQ%",   0 },
    { XVfdRole_AccelTime,      XIoPoint_AnalogOutput, 10, XVfdM512_AddrAccelTime, "ACC",    0 },
    { XVfdRole_DecelTime,      XIoPoint_AnalogOutput, 10, XVfdM512_AddrDecelTime, "DEC",    0 },
    { XVfdRole_JogCommand,     XIoPoint_AnalogOutput, 100, XVfdM512_AddrJogFreq, "JOGF",    0 },
    { XVfdRole_PresetSpeed,    XIoPoint_AnalogOutput, 100, XVfdM512_AddrPresetBase, "PC00", XVfdCap_MultiSpeed },
    { XVfdRole_PidTarget,      XIoPoint_AnalogOutput, 100, XVfdM512_AddrPidTarget, "PA01",   0 },
    { XVfdRole_DoForce,        XIoPoint_AnalogOutput,  1, XVfdM512_AddrDoForce,  "DOF",     XVfdCap_DoForce },
    { XVfdRole_Password,       XIoPoint_AnalogOutput,  1, XVfdM512_AddrPassword, "PWD",     XVfdCap_Password },
    { XVfdRole_ParamInit,      XIoPoint_AnalogOutput,  1, XVfdM512_AddrParamInit, "PINIT",  XVfdCap_FactoryReset }
};

#define M512_SPEC_COUNT ((int)(sizeof(s_m512Specs) / sizeof(s_m512Specs[0])))

/* ==================== 参数核心描述表（地址=品牌 EEPROM 编码） ==================== */

static const XVfdParamDesc s_m512Params[] = {
    /* addr    name               unit     dec  min     max     def     access            group */
    { 0xF001, "P0.01 控制方式",        NULL,  0,  0,      2,      2,      XVfdParamAccess_RunLocked, 0 },
    { 0xF002, "P0.02 命令源选择",      NULL,  0,  0,      2,      0,      XVfdParamAccess_RW,        0 },
    { 0xF003, "P0.03 主频率源X选择",   NULL,  0,  0,      9,      1,      XVfdParamAccess_RunLocked, 0 },
    { 0xF00A, "P0.10 最大频率",       "Hz",   2,  5000,   500000, 5000,   XVfdParamAccess_RunLocked, 0 },
    { 0xF00C, "P0.12 上限频率",       "Hz",   2,  0,      500000, 5000,   XVfdParamAccess_RunLocked, 0 },
    { 0xF00E, "P0.14 下限频率",       "Hz",   2,  0,      500000, 0,      XVfdParamAccess_RunLocked, 0 },
    { 0xF011, "P0.17 加速时间1",      "s",    1,  0,      650000, 0,      XVfdParamAccess_RW,        0 },
    { 0xF012, "P0.18 减速时间1",      "s",    1,  0,      650000, 0,      XVfdParamAccess_RW,        0 },
    { 0xF013, "P0.19 加减速时间单位",  NULL,  0,  0,      2,      1,      XVfdParamAccess_RunLocked, 0 },
    { 0xF01B, "P0.27 命令源捆绑频率源", NULL, 0,  0,      999,    0,      XVfdParamAccess_RW,        0 },
    { 0xF01C, "P0.28 串口通讯协议选择", NULL, 0,  0,      3,      0,      XVfdParamAccess_RunLocked, 0 },
    { 0xF800, "P8.00 点动运行频率",    "Hz",  2,  0,      500000, 200,    XVfdParamAccess_RW,        8 },
    { 0xFC00, "PC.00 多段指令0",       "%",   2, -10000, 10000,  0,      XVfdParamAccess_RW,        12 },
    { 0xFD00, "Pd.00 通讯波特率",      NULL,  0,  0,      11,     5,      XVfdParamAccess_RunLocked, 13 },
    { 0xFD02, "Pd.02 本机地址",        NULL,  0,  0,      247,    1,      XVfdParamAccess_RunLocked, 13 },
    { 0xFD04, "Pd.04 串口通讯超时时间", "s",  1,  0,      6000,   0,      XVfdParamAccess_RW,        13 },
    { 0xFA01, "PA.01 PID数字给定",     "%",   2,  0,      10000,  0,      XVfdParamAccess_RW,        10 }
};

#define M512_PARAM_COUNT ((int)(sizeof(s_m512Params) / sizeof(s_m512Params[0])))

/* ==================== 故障码表（8000H 通讯值；面板 Err 编号=值-1，见第五章） ==================== */

static const XVfdFaultEntry s_m512Faults[] = {
    { 0x0000, NULL,      "无故障" },
    { 0x0002, "E.tIGbt", "加速过电流" },
    { 0x0003, "E.oCdF",  "减速过电流" },
    { 0x0004, "E.oCAC",  "恒速过电流" },
    { 0x0005, "E.oUAC",  "加速过电压" },
    { 0x0006, "E.oUdE",  "减速过电压" },
    { 0x0007, "E.oUCo",  "恒速过电压" },
    { 0x0008, NULL,      "瞬停过压" },
    { 0x0009, NULL,      "缓冲电阻过载故障" },
    { 0x000A, "E.iLF",   "输入缺相" },
    { 0x000B, "E.oLF",   "输出缺相" },
    { 0x000C, "E.oH1",   "模块过热" },
    { 0x000F, "E.EIOF",  "外部设备故障" },
    { 0x0010, "E.CoF1",  "通讯故障" },
    { 0x0011, "E.HALL",  "电流检测故障" },
    { 0x0012, "E.oLt",   "电机过载" },
    { 0x0014, "E.PG1",   "编码器/PG卡故障" },
    { 0x0015, NULL,      "参数故障异常" },
    { 0x0016, NULL,      "参数拷贝操作故障" },
    { 0x0017, "E.SHot",  "电机对地短路故障" },
    { 0x0019, "E.ARA",   "累计运行时间到达故障" },
    { 0x001A, "E.US1",   "用户自定义故障1" },
    { 0x001B, "E.US2",   "用户自定义故障2" },
    { 0x001C, "E.EPA",   "累计上电时间到达故障" },
    { 0x001D, "E.PID",   "运行时PID反馈丢失故障" },
    { 0x001E, "E.SFr",   "运行时切换电机故障" },
    { 0x0028, "E.Sdl",   "速度偏差过大故障" },
    { 0x0029, NULL,      "电机超速" },
    { 0x002A, "E.oHt",   "电机过温故障" },
    { 0x005A, NULL,      "编码器通讯错误" },
    { 0x005B, NULL,      "传感器故障" },
    { 0x005E, "E.PoSF",  "初始位置错误" }
};

#define M512_FAULT_COUNT ((int)(sizeof(s_m512Faults) / sizeof(s_m512Faults[0])))

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
 * @brief 取频率换算基准（最大频率）：优先设备读取，其次缓存，最后 50Hz 兜底。
 * @param dev 实例；不能为 NULL。
 * @return 最大频率（Hz，>0）。
 */
static double m512MaxFreq(XVfdM512* dev)
{
    int32_t raw = 0;
    if (!dev) return 50.0;
    if (XVfdDevice_readRole(&dev->m_base, XVfdRole_MaxFreq, &raw) && raw > 0) {
        dev->m_maxFreqHz = XVfdDevice_rawToEngineering(raw, XVfdM512_MaxFreqDiv);
        dev->m_maxFreqKnown = true;
    }
    if (dev->m_maxFreqHz > 0.0) return dev->m_maxFreqHz;
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

/* ==================== 动作实现 ==================== */

/**
 * @brief 命令字单写动作。
 * @param dev 实例。
 * @param op 动作类型。
 * @param cmd 命令字（XVfdM512_Command）。
 * @return 受理返回 true。
 */
static bool m512Command(XVfdM512* dev, XVfdOperation op, int cmd)
{
    return XVfdDevice_actionWrite(&dev->m_base, op, XVfdRole_CommandWord, (int32_t)cmd);
}

static bool VXVfdM512_Start(XVfdDevice* d, bool reverse)
{
    return m512Command((XVfdM512*)d, XVfdOperation_Start,
                       reverse ? XVfdM512_CmdRunReverse : XVfdM512_CmdRunForward);
}

static bool VXVfdM512_Stop(XVfdDevice* d)
{
    return m512Command((XVfdM512*)d, XVfdOperation_Stop, XVfdM512_CmdDecelStop);
}

static bool VXVfdM512_FreeStop(XVfdDevice* d)
{
    return m512Command((XVfdM512*)d, XVfdOperation_FreeStop, XVfdM512_CmdFreeStop);
}

static bool VXVfdM512_Jog(XVfdDevice* d, bool reverse)
{
    return m512Command((XVfdM512*)d, XVfdOperation_Jog,
                       reverse ? XVfdM512_CmdJogReverse : XVfdM512_CmdJogForward);
}

static bool VXVfdM512_ResetFault(XVfdDevice* d)
{
    return m512Command((XVfdM512*)d, XVfdOperation_ResetFault, XVfdM512_CmdFaultReset);
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
    int32_t raw = m512FreqToRaw(dev, hz);
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
    if (index != 0) {
        XVfdDevice_setLastError(d, XVfdError_InvalidParam);
        return false;
    }
    acc = XVfdDevice_engineeringToRaw(accelS, XVfdM512_TimeDiv, 0, 650000);
    dev->m_pendingRaw = (uint16_t)XVfdDevice_engineeringToRaw(decelS, XVfdM512_TimeDiv, 0, 650000);
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
 * @brief 写多段速值（PC.00~15；本接口仅 0 段经点表，其余段经参数通道）。
 * @param d 设备实例。
 * @param index 段号（0 基；非 0 段返回 NotSupported）。
 * @param hz 段速（Hz）。
 * @return 受理返回 true。
 */
static bool VXVfdM512_SetPresetSpeedHz(XVfdDevice* d, int index, double hz)
{
    XVfdM512* dev = (XVfdM512*)d;
    int32_t raw;
    if (index < 0 || index > 15) {
        XVfdDevice_setLastError(d, XVfdError_InvalidParam);
        return false;
    }
    if (index != 0) {
        XVfdDevice_setLastError(d, XVfdError_NotSupported);
        XVfdDevice_emitError(d, "多段速 1~15 段请经参数通道写（PC.01~PC.15）");
        return false;
    }
    raw = m512FreqToRaw(dev, hz);
    return XVfdDevice_actionWrite(d, XVfdOperation_SetPresetSpeedHz, XVfdRole_PresetSpeed, raw);
}

/**
 * @brief 写 PID 数字给定（PA.01，0.01%）。
 * @param d 设备实例。
 * @param value 目标值（%）。
 * @return 受理返回 true。
 */
static bool VXVfdM512_SetPidTarget(XVfdDevice* d, double value)
{
    int32_t raw = XVfdDevice_engineeringToRaw(value, XVfdM512_FreqPercentDiv, 0, 10000);
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
    XVTABLE_INHERIT_XCLASS(XVfdDevice);
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
    XVfdDevice_init(&dev->m_base);
    XClassGetVtable(dev) = XVfdM512_class_init();
    XVfdDevice_setParamTable(&dev->m_base, s_m512Params, M512_PARAM_COUNT);
    XVfdDevice_setCapability(&dev->m_base, XVfdCap_Reverse);
    XVfdDevice_setCapability(&dev->m_base, XVfdCap_Jog);
    XVfdDevice_setCapability(&dev->m_base, XVfdCap_FreeStop);
    XVfdDevice_setCapability(&dev->m_base, XVfdCap_ParamAccess);
}

/* ==================== 点表 / 参数表 / 配置 ==================== */

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
    if (!dev || hz <= 0.0) return;
    dev->m_maxFreqHz = hz;
    dev->m_maxFreqKnown = true;
}

double XVfdM512_maxFrequency(const XVfdM512* dev)
{
    if (!dev) return 0.0;
    return dev->m_maxFreqKnown ? dev->m_maxFreqHz : 0.0;
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
