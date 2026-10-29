/**
 * @file XVfdM512.c
 * @brief 511M/512M/513M 系列变频器子类实现。
 * @details 命令字/状态/故障走特殊地址（2000H/3000H/8000H），遥测走 1001H 快捷块，
 *          参数走品牌地址编排（P 组 EEPROM=F0xxH…）；频率给定经 P0.10 百分比换算。
 *          故障表键值=8000H 通讯值（与面板 Err 编号相差 1，见说明书第五章/附录1）。
 */
#include "XVfdM512.h"
#if XVFD_ON
#if XVFD_M512_ON

#include "XVfdModbusBase_Protected.h"
#include "XVfdDevice_Protected.h"
#include "XVfdLink.h"
#include <string.h>

/* =============== 参数核心描述表（核心集先行，地址=品牌 EEPROM 编码） =============== */
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
#define M512_PARAM_COUNT (sizeof(s_m512Params) / sizeof(s_m512Params[0]))

/* =============== 故障码表（8000H 通讯值；面板 Err 编号=值-1，见第五章） ===============
 * 条目布局为公共 XVfdFaultEntry（XVfdModbusBase_Protected.h），查找走
 * XVfdModbusBase_faultEntry_const。 */
static const XVfdFaultEntry s_m512Faults[] = {
    { 0x0000, NULL,    "无故障" },
    { 0x0002, "E.tIGbt", "加速过电流" },
    { 0x0003, "E.oCdF",  "减速过电流" },
    { 0x0004, "E.oCAC",  "恒速过电流" },
    { 0x0005, "E.oUAC",  "加速过电压" },
    { 0x0006, "E.oUdE",  "减速过电压" },
    { 0x0007, "E.oUCo",  "恒速过电压" },
    { 0x0008, NULL,    "瞬停过压" },
    { 0x0009, NULL,    "缓冲电阻过载故障" },
    { 0x000A, "E.iLF",  "输入缺相" },
    { 0x000B, "E.oLF",  "输出缺相" },
    { 0x000C, "E.oH1",  "模块过热" },
    { 0x000F, "E.EIOF", "外部设备故障" },
    { 0x0010, "E.CoF1", "通讯故障" },
    { 0x0011, "E.HALL", "电流检测故障" },
    { 0x0012, "E.oLt",  "电机过载" },
    { 0x0014, "E.PG1",  "编码器/PG卡故障" },
    { 0x0015, NULL,    "参数故障异常" },
    { 0x0016, NULL,    "参数拷贝操作故障" },
    { 0x0017, "E.SHot", "电机对地短路故障" },
    { 0x0019, "E.ARA",  "累计运行时间到达故障" },
    { 0x001A, "E.US1",  "用户自定义故障1" },
    { 0x001B, "E.US2",  "用户自定义故障2" },
    { 0x001C, "E.EPA",  "累计上电时间到达故障" },
    { 0x001D, "E.PID",  "运行时PID反馈丢失故障" },
    { 0x001E, "E.SFr",  "运行时切换电机故障" },
    { 0x0028, "E.Sdl",  "速度偏差过大故障" },
    { 0x0029, NULL,    "电机超速" },
    { 0x002A, "E.oHt",  "电机过温故障" },
    { 0x005A, NULL,    "编码器通讯错误" },
    { 0x005B, NULL,    "传感器故障" },
    { 0x005E, "E.PoSF", "初始位置错误" }
};
#define M512_FAULT_COUNT (sizeof(s_m512Faults) / sizeof(s_m512Faults[0]))

/* =============== 轮询组解析 =============== */

/** @brief 3000H：运行状态（1正转 2反转 3停机） */
static void m512ParseState(XObject* owner, const XModbusDataUnit* result)
{
    XVfdM512* dev = (XVfdM512*)owner;
    XVfdStatus* st;
    uint16_t v;
    if (!dev || !result) return;
    st = XVfdDevice_status(&dev->m_base.m_base);
    v = XModbusDataUnit_value(result, 0);
    st->m_rawStateWord = v;
    st->m_validMask |= XVfdField_RawStateWord | XVfdField_RunState;
    switch (v) {
    case 1: st->m_runState = XVfdRunState_RunningForward; st->m_running = true; st->m_reverse = false; break;
    case 2: st->m_runState = XVfdRunState_RunningReverse; st->m_running = true; st->m_reverse = true; break;
    case 3: st->m_runState = XVfdRunState_Stopped; st->m_running = false; st->m_reverse = false; break;
    default: st->m_runState = XVfdRunState_Unknown; break;
    }
    st->m_validMask |= XVfdField_Running | XVfdField_Reverse;
}

/** @brief 8000H：故障码 */
static void m512ParseFault(XObject* owner, const XModbusDataUnit* result)
{
    XVfdM512* dev = (XVfdM512*)owner;
    XVfdStatus* st;
    uint16_t v;
    if (!dev || !result) return;
    st = XVfdDevice_status(&dev->m_base.m_base);
    v = XModbusDataUnit_value(result, 0);
    st->m_faultCode = v;
    st->m_faulted = (v != 0);
    st->m_runState = v ? XVfdRunState_Fault : st->m_runState;
    st->m_validMask |= XVfdField_FaultCode | XVfdField_Faulted;
}

/** @brief 1001H..1006H：运行频率/母线电压/输出电压/输出电流/输出功率/输出转矩 */
static void m512ParseTelemetry(XObject* owner, const XModbusDataUnit* result)
{
    XVfdM512* dev = (XVfdM512*)owner;
    XVfdStatus* st;
    if (!dev || !result) return;
    if (XModbusDataUnit_valueCount(result) < XVfdM512_TelemetryCount) return;
    st = XVfdDevice_status(&dev->m_base.m_base);
    st->m_outputFreqHz = (int16_t)XModbusDataUnit_value(result, 0) / 100.0;   /* 0.01Hz */
    st->m_busVoltageV = XModbusDataUnit_value(result, 1) / 10.0;              /* 0.1V */
    st->m_outputVoltageV = XModbusDataUnit_value(result, 2) / 10.0;           /* 0.1V */
    st->m_outputCurrentA = XModbusDataUnit_value(result, 3) / 100.0;          /* 0.01A */
    st->m_outputPowerKw = XModbusDataUnit_value(result, 4) / 10.0;            /* 0.1kW */
    st->m_torquePercent = (int16_t)XModbusDataUnit_value(result, 5) / 10.0;   /* 0.1% */
    st->m_setFreqPercent = (int16_t)XModbusDataUnit_value(result, 0) / 100.0; /* 近似：以运行频率%口径，现场标定 */
    st->m_validMask |= XVfdField_OutputFreqHz | XVfdField_BusVoltage | XVfdField_OutputVoltage
                     | XVfdField_OutputCurrent | XVfdField_OutputPowerKw | XVfdField_TorquePercent;
}

/* =============== 动作收口 =============== */

/** @brief 设定频率两阶段：阶段1 读 P0.10（最大频率）→ 阶段2 写 1000H 百分比 */
static void m512OnMaxFreqDone(XObject* owner, void* userData, bool ok, const XModbusDataUnit* result)
{
    XVfdM512* dev = (XVfdM512*)owner;
    double percent;
    (void)userData;
    if (!dev) return;
    if (ok && result && XModbusDataUnit_valueCount(result) > 0) {
        dev->m_maxFreqHz = (double)(int16_t)XModbusDataUnit_value(result, 0) / 100.0;
        dev->m_maxFreqKnown = true;
    } else {
        dev->m_maxFreqHz = 50.0; /* 读取失败按缺省 50Hz 换算并上报错误 */
        XVfdDevice_emitError(&dev->m_base.m_base, "读取最大频率失败，按 50Hz 换算给定");
    }
    if (dev->m_maxFreqHz <= 0.0) dev->m_maxFreqHz = 50.0;
    percent = dev->m_pendingFreqHz / dev->m_maxFreqHz * 10000.0;
    if (percent > 10000.0) percent = 10000.0;
    if (percent < -10000.0) percent = -10000.0;
    if (!XVfdModbusBase_submitWriteSingle(&dev->m_base, XVfdM512_AddrFreqSet,
                                          (uint16_t)(int16_t)(int32_t)percent,
                                          XVfdModbusBase_simpleWriteDone, NULL, XModbusRequestPriority_High)) {
        XVfdModbusBase_finishAction(&dev->m_base, false);
    }
}

/** @brief 加减速两步：写 P0.17（加速）→ 写 P0.18（减速）→ 收口 */
static void m512OnAccelDone(XObject* owner, void* userData, bool ok, const XModbusDataUnit* result)
{
    XVfdM512* dev = (XVfdM512*)owner;
    uint16_t raw;
    (void)userData;
    (void)result;
    if (!dev) return;
    if (!ok) {
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return;
    }
    /* P0.19 单位缺省 1=0.1s：秒 → 0.1s 原始码 */
    raw = (uint16_t)(dev->m_pendingDecelS * 10.0);
    if (!XVfdModbusBase_submitWriteSingle(&dev->m_base, XVfdM512_AddrDecelTime,
                                          raw, XVfdModbusBase_simpleWriteDone, NULL, XModbusRequestPriority_High)) {
        XVfdModbusBase_finishAction(&dev->m_base, false);
    }
}

/* =============== 业务虚函数实现（重载 XVfdDevice 槽） =============== */

static bool VXVfdM512_Start(XVfdDevice* d, bool reverse)
{
    XVfdM512* dev = (XVfdM512*)d;
    int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_Start,
                                        reverse, 0, 0.0, 0.0);
    if (en != 0) return en > 0; /* 忙则挂起排队，收口后经虚槽补发 */
    return XVfdModbusBase_submitActionWrite(&dev->m_base, XVfdOperation_Start,
                                            XVfdM512_AddrCmdWord,
                                            reverse ? XVfdM512_CmdRunReverse : XVfdM512_CmdRunForward);
}

static bool VXVfdM512_Stop(XVfdDevice* d)
{
    XVfdM512* dev = (XVfdM512*)d;
    int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_Stop,
                                        false, 0, 0.0, 0.0);
    if (en != 0) return en > 0;
    return XVfdModbusBase_submitActionWrite(&dev->m_base, XVfdOperation_Stop,
                                            XVfdM512_AddrCmdWord, XVfdM512_CmdDecelStop);
}

static bool VXVfdM512_FreeStop(XVfdDevice* d)
{
    XVfdM512* dev = (XVfdM512*)d;
    int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_FreeStop,
                                        false, 0, 0.0, 0.0);
    if (en != 0) return en > 0;
    return XVfdModbusBase_submitActionWrite(&dev->m_base, XVfdOperation_FreeStop,
                                            XVfdM512_AddrCmdWord, XVfdM512_CmdFreeStop);
}

static bool VXVfdM512_Jog(XVfdDevice* d, bool reverse)
{
    XVfdM512* dev = (XVfdM512*)d;
    int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_Jog,
                                        reverse, 0, 0.0, 0.0);
    if (en != 0) return en > 0;
    return XVfdModbusBase_submitActionWrite(&dev->m_base, XVfdOperation_Jog,
                                            XVfdM512_AddrCmdWord,
                                            reverse ? XVfdM512_CmdJogReverse : XVfdM512_CmdJogForward);
}

static bool VXVfdM512_SetFrequency(XVfdDevice* d, double hz)
{
    XVfdM512* dev = (XVfdM512*)d;
    int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_SetFrequency,
                                        false, 0, hz, 0.0);
    if (en != 0) return en > 0; /* 未绑定在此返回 false（NotAttached） */
    if (!XVfdModbusBase_busyStart(&dev->m_base, XVfdOperation_SetFrequency)) return false;
    dev->m_pendingFreqHz = hz;
    if (dev->m_maxFreqKnown) {
        return m512OnMaxFreqDone((XObject*)dev, NULL, true, NULL), true;
    }
    if (!XVfdModbusBase_submitRead(&dev->m_base, XVfdM512_AddrMaxFreq, 1,
                                   m512OnMaxFreqDone, NULL, XModbusRequestPriority_High)) {
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return false;
    }
    return true;
}

static bool VXVfdM512_SetAccelDecel(XVfdDevice* d, int index, double accelS, double decelS)
{
    XVfdM512* dev = (XVfdM512*)d;
    uint16_t raw;
    if (index != 0) { d->m_lastError = XVfdError_InvalidParam; return false; }
    {
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_SetAccelDecel,
                                            false, index, accelS, decelS);
        if (en != 0) return en > 0;
    }
    if (!XVfdModbusBase_busyStart(&dev->m_base, XVfdOperation_SetAccelDecel)) return false;
    dev->m_pendingDecelS = decelS;
    raw = (uint16_t)(accelS * 10.0); /* P0.19 单位缺省 1=0.1s */
    if (!XVfdModbusBase_submitWriteSingle(&dev->m_base, XVfdM512_AddrAccelTime,
                                          raw, m512OnAccelDone, NULL, XModbusRequestPriority_High)) {
        XVfdModbusBase_finishAction(&dev->m_base, false);
        return false;
    }
    return true;
}

static bool VXVfdM512_SetPresetSpeedHz(XVfdDevice* d, int index, double hz)
{
    XVfdM512* dev = (XVfdM512*)d;
    double percent;
    if (index < 0 || index > 15) { d->m_lastError = XVfdError_InvalidParam; return false; }
    {
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_SetPresetSpeedHz,
                                            false, index, hz, 0.0);
        if (en != 0) return en > 0;
    }
    /* 多段指令以最大频率为 100%：与通讯设定值同口径（现场标定） */
    percent = hz / (dev->m_maxFreqKnown ? dev->m_maxFreqHz : 50.0) * 10000.0;
    if (percent > 10000.0) percent = 10000.0;
    if (percent < -10000.0) percent = -10000.0;
    return XVfdModbusBase_submitActionWrite(&dev->m_base, XVfdOperation_SetPresetSpeedHz,
                                            (uint16_t)(XVfdM512_AddrPresetBase + index),
                                            (uint16_t)(int16_t)(int32_t)percent);
}

static bool VXVfdM512_SetPidTarget(XVfdDevice* d, double value)
{
    XVfdM512* dev = (XVfdM512*)d;
    {
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_SetPidTarget,
                                            false, 0, value, 0.0);
        if (en != 0) return en > 0;
    }
    return XVfdModbusBase_submitActionWrite(&dev->m_base, XVfdOperation_SetPidTarget,
                                            0xFA01,
                                            (uint16_t)(int16_t)(int32_t)(value * 100.0));
}

static bool VXVfdM512_ResetFault(XVfdDevice* d)
{
    XVfdM512* dev = (XVfdM512*)d;
    {
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_ResetFault,
                                            false, 0, 0.0, 0.0);
        if (en != 0) return en > 0;
    }
    return XVfdModbusBase_submitActionWrite(&dev->m_base, XVfdOperation_ResetFault,
                                            XVfdM512_AddrCmdWord, XVfdM512_CmdFaultReset);
}

static bool VXVfdM512_SetDigitalOutput(XVfdDevice* d, int index, bool on)
{
    XVfdM512* dev = (XVfdM512*)d;
    /* 2001H 为位图整体写：以当前 DO 位图为基础置/清位（简化：仅置位） */
    uint16_t bits;
    if (index < 0 || index > 9) { d->m_lastError = XVfdError_InvalidParam; return false; }
    {
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_SetDigitalOutput,
                                            on, index, 0.0, 0.0);
        if (en != 0) return en > 0;
    }
    bits = on ? (uint16_t)(1u << index) : 0;
    return XVfdModbusBase_submitActionWrite(&dev->m_base, XVfdOperation_SetDigitalOutput,
                                            XVfdM512_AddrDoForce, bits);
}

static bool VXVfdM512_FactoryReset(XVfdDevice* d, int scope)
{
    XVfdM512* dev = (XVfdM512*)d;
    uint16_t cmd;
    if (scope != 0 && scope != 1) { d->m_lastError = XVfdError_InvalidParam; return false; }
    {
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_FactoryReset,
                                            false, scope, 0.0, 0.0);
        if (en != 0) return en > 0;
    }
    cmd = (scope == 0) ? 1 : 2; /* 1=恢复出厂不含电机 2=含电机参数 */
    return XVfdModbusBase_submitActionWrite(&dev->m_base, XVfdOperation_FactoryReset,
                                            XVfdM512_AddrParamInit, cmd);
}

static bool VXVfdM512_UnlockPassword(XVfdDevice* d, uint32_t password)
{
    XVfdM512* dev = (XVfdM512*)d;
    {
        int en = XVfdModbusBase_actionEntry(&dev->m_base, XVfdOperation_UnlockPassword,
                                            false, (int)(password & 0xFFFF), 0.0, 0.0);
        if (en != 0) return en > 0;
    }
    return XVfdModbusBase_submitActionWrite(&dev->m_base, XVfdOperation_UnlockPassword,
                                            XVfdM512_AddrPassword,
                                            (uint16_t)(password & 0xFFFF));
}

/* =============== 虚函数表 / 生命周期 =============== */
XVtable* XVfdM512_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVfdM512)
    XVTABLE_INHERIT_XCLASS(XVfdModbusBase);
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
    XVfdModbusBase_init(&dev->m_base);
    XClassGetVtable(dev) = XVfdM512_class_init();
    dev->m_maxFreqHz = 50.0;
    XVfdModbusBase_addPollGroup(&dev->m_base, XVfdM512_AddrRunState, 1,
                                XVfdPollItem_Status, m512ParseState);
    XVfdModbusBase_addPollGroup(&dev->m_base, XVfdM512_AddrFault, 1,
                                XVfdPollItem_Fault, m512ParseFault);
    XVfdModbusBase_addPollGroup(&dev->m_base, XVfdM512_AddrTelemetry, XVfdM512_TelemetryCount,
                                XVfdPollItem_Telemetry, m512ParseTelemetry);
    XVfdModbusBase_setParamTable(&dev->m_base, s_m512Params, (int)M512_PARAM_COUNT);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_Reverse);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_Jog);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_FreeStop);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_MultiSpeed);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_DoForce);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_Password);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_FactoryReset);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_TorqueReport);
    XVfdDevice_setCapability(&dev->m_base.m_base, XVfdCap_PowerReport);
}

/* =============== 配置 =============== */
void XVfdM512_setNonStdReply(XVfdM512* dev, bool nonStd)
{
    if (dev) dev->m_nonStdReply = nonStd;
}

bool XVfdM512_nonStdReply(const XVfdM512* dev)
{
    return dev ? dev->m_nonStdReply : false;
}

/* =============== 故障码查询 =============== */
const char* XVfdM512_faultName(uint16_t code)
{
    size_t i;
    for (i = 0; i < M512_FAULT_COUNT; ++i) {
        if (s_m512Faults[i].m_code == code) return s_m512Faults[i].m_name;
    }
    return NULL;
}

const char* XVfdM512_faultText(uint16_t code)
{
    size_t i;
    for (i = 0; i < M512_FAULT_COUNT; ++i) {
        if (s_m512Faults[i].m_code == code) return s_m512Faults[i].m_text;
    }
    return NULL;
}

#endif /* XVFD_M512_ON */
#endif /* XVFD_ON */
