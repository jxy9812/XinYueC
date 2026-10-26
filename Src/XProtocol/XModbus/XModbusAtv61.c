/**
 * @file XModbusAtv61.c
 * @brief 施耐德 ATV61 变频器 Modbus 驱动实现。
 * @details 全部动作都是异步的：调用接口立即返回，结果经信号通知。
 *          内部同时只允许一个请求在途（RTU 单事务语义），后续动作排队由调用方
 *          通过 XModbusAtv61_isBusy() 判断。
 */
#include "XModbus_config.h"
#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_ATV61_ON

#include "XModbusAtv61.h"
#include "XModbusReply.h"
#include "XModbusDataUnit.h"
#include "XVarList.h"
#include "XTimer.h"
#include <string.h>
#include <math.h>

/* =============== 内部阶段机 =============== */
#define STAGE_NONE        0   /**< 空闲 */
#define STAGE_READ_MAIN   1   /**< 读 3201..3209（ETA/频率/电流/转矩/ETI/电压/热态） */
#define STAGE_READ_LFT    2   /**< 读 7121（故障代码） */
#define STAGE_READ_THR    3   /**< 读 9630（电机热状态） */
#define STAGE_READ_ETA    4   /**< 只读 3201（启动序列判状态） */
#define STAGE_WRITE_CMD   5   /**< 写 8501（命令字） */
#define STAGE_WRITE_REF   6   /**< 写频率/转速给定字 */

/** @brief 启动序列每步之间的等待时间（毫秒） */
#define ATV61_STEP_DELAY_MS 120
/** @brief 启动序列最大步数 */
#define ATV61_START_MAX_STEP 6
/** @brief 故障复位后归位等待（毫秒） */
#define ATV61_RESET_DELAY_MS 200

/* =============== 前置声明 =============== */
static void XModbusAtv61_onReplyFinished(XObject* receiver, XVarList* args);
static void VXModbusAtv61_timerEvent(XObject* obj, XTimerEvent* event);
static bool atv61SendRead(XModbusAtv61* driver, uint16_t address, uint16_t count, int stage);
static bool atv61SendWrite(XModbusAtv61* driver, uint16_t address, uint16_t value, int stage);
static void atv61StepTimerStart(XModbusAtv61* driver, int delayMs);
static void atv61StepTimerStop(XModbusAtv61* driver);
static void atv61EmitStatus(XModbusAtv61* driver);
static void atv61EmitDone(XModbusAtv61* driver, XModbusAtv61_Operation op, bool ok);
static void atv61EmitError(XModbusAtv61* driver, const char* message);
static void atv61ApplyStatusWords(XModbusAtv61* driver, const XModbusDataUnit* unit);
static void atv61Finish(XModbusAtv61* driver, bool ok);

/* =============== 故障码表 ===============
 * 数值与字母码取自施耐德官方《ATV610 通信参数表》LFT 枚举（EAV64394），
 * 与 ATV61/ATV71 同属一个逻辑地址家族，故障字母码一致。
 * ATV61 编程手册（第 242~246 页）给出的字母码与本表一致；
 * 现场若遇到不一致，以 HMI [1.10 诊断] 当前故障列表为准。 */
typedef struct XModbusAtv61FaultEntry {
    uint16_t m_code;      /**< LFT 寄存器数值 */
    const char* m_name;   /**< ATV61 故障缩写 */
    const char* m_text;   /**< 英文说明 */
} XModbusAtv61FaultEntry;

static const XModbusAtv61FaultEntry s_atv61Faults[] = {
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

/* =============== 虚函数表 =============== */
XVtable* XModbusAtv61_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XModbusAtv61)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXModbusAtv61_timerEvent);
    XCLASS_SET_CLASS_NAME_DEFAULT("XModbusAtv61");
    XCLASS_SHOW_SIZE_DEFAULT(XModbusAtv61);
    return XVTABLE_DEFAULT;
}

/* =============== 创建 / 初始化 =============== */
XModbusAtv61* XModbusAtv61_create_ex(XMemoryType memory)
{
    XModbusAtv61* driver = (XModbusAtv61*)XMemory_malloc(sizeof(XModbusAtv61), memory);
    if (!driver) return NULL;
    XModbusAtv61_init(driver);
    Set_Class_Memory(driver, memory);
    Set_Class_IsHeap(driver, true);
    return driver;
}

void XModbusAtv61_init(XModbusAtv61* driver)
{
    if (!driver) return;
    XObject_init(&driver->m_class);
    XClassGetVtable(driver) = XModbusAtv61_class_init();
    driver->m_client = NULL;
    driver->m_unitId = 1;
    driver->m_refMode = XModbusAtv61_RefMode_ModbusFreq;
    driver->m_referenceAddress = XModbusAtv61_AddrLfr1;
    memset(&driver->m_status, 0, sizeof(driver->m_status));
    driver->m_lastCmd = 0;
    driver->m_busy = false;
    driver->m_haveStatus = false;
    driver->m_op = XModbusAtv61_Op_None;
    driver->m_stage = STAGE_NONE;
    driver->m_step = 0;
    driver->m_reverse = false;
    driver->m_pendingReset = false;
    driver->m_pendingSpeedValue = 0;
    driver->m_pendingFreq = 0.0;
    driver->m_pendingCmd = 0;
    driver->m_pendingReply = NULL;
    driver->m_stepTimer = XTIMER_INVALID_ID;
}

/* =============== 绑定与配置 =============== */
bool XModbusAtv61_attach(XModbusAtv61* driver, XModbusClient* client, int unitId)
{
    if (!driver || !client) return false;
    if (unitId < 0 || unitId > 247) return false;
    driver->m_client = client;
    driver->m_unitId = unitId;
    return true;
}

void XModbusAtv61_detach(XModbusAtv61* driver)
{
    if (!driver) return;
    atv61StepTimerStop(driver);
    driver->m_client = NULL;
    driver->m_busy = false;
}

int XModbusAtv61_unitId(const XModbusAtv61* driver)
{
    return driver ? driver->m_unitId : 0;
}

void XModbusAtv61_setUnitId(XModbusAtv61* driver, int unitId)
{
    if (!driver) return;
    if (unitId < 0 || unitId > 247) return;
    driver->m_unitId = unitId;
}

void XModbusAtv61_setReferenceMode(XModbusAtv61* driver, XModbusAtv61_ReferenceMode mode)
{
    if (!driver) return;
    driver->m_refMode = mode;
    switch (mode) {
    case XModbusAtv61_RefMode_SpeedRpm:
        driver->m_referenceAddress = XModbusAtv61_AddrLfrd;
        break;
    case XModbusAtv61_RefMode_LfrFreq:
        driver->m_referenceAddress = XModbusAtv61_AddrLfr;
        break;
    case XModbusAtv61_RefMode_ModbusFreq:
    default:
        driver->m_referenceAddress = XModbusAtv61_AddrLfr1;
        break;
    }
}

XModbusAtv61_ReferenceMode XModbusAtv61_referenceMode(const XModbusAtv61* driver)
{
    return driver ? driver->m_refMode : XModbusAtv61_RefMode_ModbusFreq;
}

void XModbusAtv61_setReferenceAddress(XModbusAtv61* driver, uint16_t address)
{
    if (!driver) return;
    driver->m_referenceAddress = address;
}

uint16_t XModbusAtv61_referenceAddress(const XModbusAtv61* driver)
{
    return driver ? driver->m_referenceAddress : 0;
}

const XModbusAtv61Status* XModbusAtv61_status_const(const XModbusAtv61* driver)
{
    return driver ? &driver->m_status : NULL;
}

bool XModbusAtv61_isBusy(const XModbusAtv61* driver)
{
    return driver ? driver->m_busy : false;
}

/* =============== 内部工具 =============== */
static void atv61StepTimerStop(XModbusAtv61* driver)
{
    if (!driver) return;
    if (driver->m_stepTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)driver, driver->m_stepTimer);
        driver->m_stepTimer = XTIMER_INVALID_ID;
    }
}

static void atv61StepTimerStart(XModbusAtv61* driver, int delayMs)
{
    if (!driver) return;
    atv61StepTimerStop(driver);
    if (delayMs <= 0) delayMs = 1;
    driver->m_stepTimer = XObject_startTimer_ms((XObject*)driver,
        (uint64_t)delayMs, XTimerType_CoarseTimer);
}

static void atv61EmitStatus(XModbusAtv61* driver)
{
    if (!driver) return;
    XModbusAtv61_statusUpdated_signal(driver);
}

static void atv61EmitDone(XModbusAtv61* driver, XModbusAtv61_Operation op, bool ok)
{
    if (!driver) return;
    XModbusAtv61_commandCompleted_signal(driver, op, ok);
}

static void atv61EmitError(XModbusAtv61* driver, const char* message)
{
    if (!driver) return;
    XModbusAtv61_errorOccurred_signal(driver, message);
}

/** @brief 收尾：清忙、结算当前动作、发完成信号 */
static void atv61Finish(XModbusAtv61* driver, bool ok)
{
    if (!driver) return;
    XModbusAtv61_Operation op = driver->m_op;
    driver->m_busy = false;
    driver->m_stage = STAGE_NONE;
    driver->m_op = XModbusAtv61_Op_None;
    atv61StepTimerStop(driver);
    atv61EmitDone(driver, op, ok);
}

/* =============== 请求发送 =============== */
static bool atv61SendRead(XModbusAtv61* driver, uint16_t address, uint16_t count, int stage)
{
    if (!driver || !driver->m_client) return false;
    XModbusDataUnit* unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
        XModbusHoldingRegisters, address, (size_t)count);
    if (!unit) return false;
    XModbusReply* reply = XModbusClient_sendReadRequest(driver->m_client, unit, driver->m_unitId);
    XClassDelete((XClass*)unit);
    if (!reply) return false;
    driver->m_pendingReply = reply;
    driver->m_stage = stage;
    driver->m_busy = true;
    XObject_connect_1((XObject*)reply,
                      (size_t)XModbusReply_finished_signal(reply),
                      (XObject*)driver,
                      XModbusAtv61_onReplyFinished,
                      XConnectionType_Direct);
    return true;
}

static bool atv61SendWrite(XModbusAtv61* driver, uint16_t address, uint16_t value, int stage)
{
    if (!driver || !driver->m_client) return false;
    XModbusDataUnit* unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
        XModbusHoldingRegisters, address, 1);
    if (!unit) return false;
    XModbusDataUnit_setValue(unit, 0, value);
    XModbusReply* reply = XModbusClient_sendWriteRequest(driver->m_client, unit, driver->m_unitId);
    XClassDelete((XClass*)unit);
    if (!reply) return false;
    driver->m_pendingReply = reply;
    driver->m_stage = stage;
    driver->m_busy = true;
    XObject_connect_1((XObject*)reply,
                      (size_t)XModbusReply_finished_signal(reply),
                      (XObject*)driver,
                      XModbusAtv61_onReplyFinished,
                      XConnectionType_Direct);
    return true;
}

/* =============== 状态解析 =============== */
/** @brief 由 ETA 原值刷新全部派生标志（状态判定、运行/故障/方向等） */
static void atv61ApplyEta(XModbusAtv61* driver, uint16_t eta)
{
    if (!driver) return;
    XModbusAtv61Status* st = &driver->m_status;
    st->eta = eta;
    st->driveState = (uint8_t)(eta & XModbusAtv61_EtaMask);
    st->running = (eta & XModbusAtv61_EtaOperationEnabled) != 0;
    st->faulted = (eta & XModbusAtv61_EtaFault) != 0;
    st->warning = (eta & XModbusAtv61_EtaWarning) != 0;
    st->reverse = (eta & XModbusAtv61_EtaReverse) != 0;
    st->targetReached = (eta & XModbusAtv61_EtaTargetReached) != 0;
    st->remoteControl = (eta & XModbusAtv61_EtaRemote) != 0;
    st->quickStopActive = (eta & XModbusAtv61_EtaQuickStopActive) == 0;
    st->referenceOutOfRange = (eta & XModbusAtv61_EtaRefOutOfLimits) != 0;
    st->valid = true;
    driver->m_haveStatus = true;
}

static void atv61ApplyStatusWords(XModbusAtv61* driver, const XModbusDataUnit* unit)
{
    if (!driver || !unit) return;
    XModbusAtv61Status* st = &driver->m_status;
    size_t n = XModbusDataUnit_valueCount(unit);
    /* 3201..3209：ETA RFR FRH LCR OTR ETI ULN UOP THD */
    if (n >= 9) {
        st->eta = XModbusDataUnit_value(unit, 0);
        st->outputFreqHz = (int16_t)XModbusDataUnit_value(unit, 1) / 10.0;
        st->referenceHz = (int16_t)XModbusDataUnit_value(unit, 2) / 10.0;
        st->motorCurrent = XModbusDataUnit_value(unit, 3);
        st->motorTorquePercent = (int16_t)XModbusDataUnit_value(unit, 4) / 10.0;
        st->eti = XModbusDataUnit_value(unit, 5);
        st->mainsVoltage = XModbusDataUnit_value(unit, 6) / 10.0;
        st->motorVoltage = XModbusDataUnit_value(unit, 7);
        st->driveThermalPercent = XModbusDataUnit_value(unit, 8);
    }
    else if (n >= 1) {
        st->eta = XModbusDataUnit_value(unit, 0);
    }
    atv61ApplyEta(driver, st->eta);
}

/** @brief 由当前 ETA 推导下一步命令字（CiA402 状态机推进） */
static uint16_t atv61NextCmd(uint16_t eta, bool reverse)
{
    uint16_t cmd;
    uint8_t state = (uint8_t)(eta & XModbusAtv61_EtaMask);
    switch (state) {
    case XModbusAtv61_EtaStateDisabled:  /* 2：未上电 → Shutdown */
        cmd = XModbusAtv61_CmdShutdown;
        break;
    case XModbusAtv61_EtaStateReady:     /* 3：允许上电 → Switch on */
        cmd = XModbusAtv61_CmdSwitchOnState;
        break;
    case XModbusAtv61_EtaStateSwitched:  /* 4：已上电 → Enable operation */
        cmd = XModbusAtv61_CmdRun;
        break;
    case XModbusAtv61_EtaStateRunning:   /* 5：已在运行 */
        cmd = XModbusAtv61_CmdRun;
        break;
    default:
        cmd = XModbusAtv61_CmdRun;
        break;
    }
    if (reverse) cmd |= XModbusAtv61_CmdReverse;
    return cmd;
}

/* =============== 应答处理 =============== */
static void XModbusAtv61_onReplyFinished(XObject* receiver, XVarList* args)
{
    (void)args;
    XModbusAtv61* driver = (XModbusAtv61*)receiver;
    if (!driver) return;
    XModbusReply* reply = driver->m_pendingReply;
    if (!reply) return;
    driver->m_pendingReply = NULL;

    bool ok = (XModbusReply_error(reply) == XModbusDevice_NoError);
    int stage = driver->m_stage;
    const XModbusDataUnit* result = ok ? XModbusReply_result_const(reply) : NULL;

    switch (stage) {
    case STAGE_READ_MAIN:
        if (ok && result) {
            atv61ApplyStatusWords(driver, result);
            /* 继续读故障码与电机热状态 */
            if (!atv61SendRead(driver, XModbusAtv61_AddrLft, 1, STAGE_READ_LFT)) {
                atv61EmitStatus(driver);
                atv61Finish(driver, false);
                break;
            }
        } else {
            atv61Finish(driver, false);
        }
        break;

    case STAGE_READ_LFT:
        if (ok && result) {
            driver->m_status.lastFault = XModbusDataUnit_value(result, 0);
            if (!atv61SendRead(driver, XModbusAtv61_AddrThr, 1, STAGE_READ_THR)) {
                atv61EmitStatus(driver);
                atv61Finish(driver, false);
                break;
            }
        } else {
            atv61Finish(driver, false);
        }
        break;

    case STAGE_READ_THR:
        if (ok && result) {
            driver->m_status.motorThermalPercent = XModbusDataUnit_value(result, 0);
            atv61EmitStatus(driver);
            atv61Finish(driver, true);
        } else {
            atv61Finish(driver, false);
        }
        break;

    case STAGE_READ_ETA: {
        /* 启动序列：读状态 → 决定下一步命令字 */
        if (!ok || !result) {
            atv61Finish(driver, false);
            break;
        }
        uint16_t eta = XModbusDataUnit_value(result, 0);
        atv61ApplyEta(driver, eta);
        if (driver->m_status.faulted) {
            atv61EmitError(driver, "变频器处于故障态，请先复位故障");
            atv61Finish(driver, false);
            break;
        }
        if (driver->m_status.running) {
            atv61EmitStatus(driver);
            atv61Finish(driver, true);
            break;
        }
        if (driver->m_step >= ATV61_START_MAX_STEP) {
            atv61EmitError(driver, "启动超时：变频器未进入运行状态");
            atv61Finish(driver, false);
            break;
        }
        driver->m_pendingCmd = atv61NextCmd(eta, driver->m_reverse);
        ++driver->m_step;
        if (!atv61SendWrite(driver, XModbusAtv61_AddrCmd, driver->m_pendingCmd, STAGE_WRITE_CMD)) {
            atv61Finish(driver, false);
        }
        break;
    }

    case STAGE_WRITE_CMD:
        if (!ok) {
            atv61Finish(driver, false);
            break;
        }
        driver->m_lastCmd = driver->m_pendingCmd;
        if (driver->m_op == XModbusAtv61_Op_ResetFault && driver->m_pendingReset) {
            /* 故障复位：写 Disable voltage 归位 */
            driver->m_pendingReset = false;
            if (!atv61SendWrite(driver, XModbusAtv61_AddrCmd,
                                 XModbusAtv61_CmdDisableVoltage, STAGE_WRITE_CMD)) {
                atv61Finish(driver, false);
            }
            break;
        }
        if (driver->m_op == XModbusAtv61_Op_Start) {
            /* 等待变频器状态迁移后再读 ETA */
            atv61StepTimerStart(driver, ATV61_STEP_DELAY_MS);
            break;
        }
        atv61Finish(driver, true);
        break;

    case STAGE_WRITE_REF:
        if (ok) atv61Finish(driver, true);
        else atv61Finish(driver, false);
        break;

    default:
        atv61Finish(driver, ok);
        break;
    }

    XObject_deleteLater((XObject*)reply);
}

/* =============== 定时器事件（步进延时） =============== */
static void VXModbusAtv61_timerEvent(XObject* obj, XTimerEvent* event)
{
    if (!obj || !event) goto parent_call;
    XEvent_accept(event);
    XModbusAtv61* driver = (XModbusAtv61*)obj;
    if (XTimerEvent_timerId(event) != driver->m_stepTimer) goto parent_call;
    driver->m_stepTimer = XTIMER_INVALID_ID;

    if (driver->m_op == XModbusAtv61_Op_Start) {
        if (!atv61SendRead(driver, XModbusAtv61_AddrEta, 1, STAGE_READ_ETA)) {
            atv61Finish(driver, false);
        }
        return;
    }
    if (driver->m_op == XModbusAtv61_Op_ResetFault) {
        atv61Finish(driver, true);
        return;
    }
    atv61Finish(driver, true);

parent_call:
    XClass_Parent(XObject, EXObject_TimerEvent, void (*)(XObject*, XTimerEvent*))(obj, event);
}

/* =============== 公开动作接口 =============== */
bool XModbusAtv61_readStatus(XModbusAtv61* driver)
{
    if (!driver || !driver->m_client || driver->m_busy) return false;
    driver->m_op = XModbusAtv61_Op_ReadStatus;
    driver->m_step = 0;
    /* 3201..3209 是连续有效地址块；3210 非有效参数，单独读 3211。 */
    if (!atv61SendRead(driver, XModbusAtv61_AddrEta, 9, STAGE_READ_MAIN)) {
        driver->m_op = XModbusAtv61_Op_None;
        return false;
    }
    return true;
}

bool XModbusAtv61_setFrequency(XModbusAtv61* driver, double hz)
{
    if (!driver || !driver->m_client || driver->m_busy) return false;
    if (driver->m_refMode == XModbusAtv61_RefMode_SpeedRpm) return false;
    double scaled = hz * 10.0;                 /* 0.1 Hz 定点 */
    if (scaled > 32767.0) scaled = 32767.0;
    if (scaled < -32768.0) scaled = -32768.0;
    driver->m_pendingFreq = hz;
    driver->m_op = XModbusAtv61_Op_SetFrequency;
    if (!atv61SendWrite(driver, driver->m_referenceAddress,
                        (uint16_t)(int16_t)(int32_t)scaled, STAGE_WRITE_REF)) {
        driver->m_op = XModbusAtv61_Op_None;
        return false;
    }
    return true;
}

bool XModbusAtv61_setSpeedRpm(XModbusAtv61* driver, int rpm)
{
    if (!driver || !driver->m_client || driver->m_busy) return false;
    if (rpm > 32767) rpm = 32767;
    if (rpm < -32768) rpm = -32768;
    driver->m_pendingSpeedValue = rpm;
    driver->m_op = XModbusAtv61_Op_SetSpeed;
    if (!atv61SendWrite(driver, XModbusAtv61_AddrLfrd,
                        (uint16_t)(int16_t)rpm, STAGE_WRITE_REF)) {
        driver->m_op = XModbusAtv61_Op_None;
        return false;
    }
    return true;
}

bool XModbusAtv61_start(XModbusAtv61* driver, bool reverse)
{
    if (!driver || !driver->m_client || driver->m_busy) return false;
    driver->m_op = XModbusAtv61_Op_Start;
    driver->m_step = 0;
    driver->m_reverse = reverse;
    if (!atv61SendRead(driver, XModbusAtv61_AddrEta, 1, STAGE_READ_ETA)) {
        driver->m_op = XModbusAtv61_Op_None;
        return false;
    }
    return true;
}

bool XModbusAtv61_stop(XModbusAtv61* driver)
{
    if (!driver || !driver->m_client || driver->m_busy) return false;
    driver->m_op = XModbusAtv61_Op_Stop;
    driver->m_pendingCmd = XModbusAtv61_CmdDisableOperation;
    if (!atv61SendWrite(driver, XModbusAtv61_AddrCmd,
                        driver->m_pendingCmd, STAGE_WRITE_CMD)) {
        driver->m_op = XModbusAtv61_Op_None;
        return false;
    }
    return true;
}

bool XModbusAtv61_freeStop(XModbusAtv61* driver)
{
    if (!driver || !driver->m_client || driver->m_busy) return false;
    driver->m_op = XModbusAtv61_Op_FreeStop;
    driver->m_pendingCmd = XModbusAtv61_CmdDisableVoltage;
    if (!atv61SendWrite(driver, XModbusAtv61_AddrCmd,
                        driver->m_pendingCmd, STAGE_WRITE_CMD)) {
        driver->m_op = XModbusAtv61_Op_None;
        return false;
    }
    return true;
}

bool XModbusAtv61_quickStop(XModbusAtv61* driver)
{
    if (!driver || !driver->m_client || driver->m_busy) return false;
    driver->m_op = XModbusAtv61_Op_QuickStop;
    driver->m_pendingCmd = XModbusAtv61_CmdQuickStopCmd;
    if (!atv61SendWrite(driver, XModbusAtv61_AddrCmd,
                        driver->m_pendingCmd, STAGE_WRITE_CMD)) {
        driver->m_op = XModbusAtv61_Op_None;
        return false;
    }
    return true;
}

bool XModbusAtv61_resetFault(XModbusAtv61* driver)
{
    if (!driver || !driver->m_client || driver->m_busy) return false;
    driver->m_op = XModbusAtv61_Op_ResetFault;
    driver->m_pendingReset = true;
    driver->m_pendingCmd = XModbusAtv61_CmdFaultResetCmd;
    if (!atv61SendWrite(driver, XModbusAtv61_AddrCmd,
                        driver->m_pendingCmd, STAGE_WRITE_CMD)) {
        driver->m_op = XModbusAtv61_Op_None;
        driver->m_pendingReset = false;
        return false;
    }
    return true;
}

/* =============== 故障码查询 =============== */
const char* XModbusAtv61_faultName(uint16_t code)
{
    size_t i;
    for (i = 0; i < ATV61_FAULT_COUNT; ++i) {
        if (s_atv61Faults[i].m_code == code) return s_atv61Faults[i].m_name;
    }
    return NULL;
}

const char* XModbusAtv61_faultText(uint16_t code)
{
    size_t i;
    for (i = 0; i < ATV61_FAULT_COUNT; ++i) {
        if (s_atv61Faults[i].m_code == code) return s_atv61Faults[i].m_text;
    }
    return NULL;
}

/* =============== 信号 ===============
 * 约定（对齐 XModbusDevice_stateChanged_signal / XObject_signalIdOf）：
 * 信号函数被"取标识"时以 NULL 调用，只返回标识符、不分配参数；
 * 被"发射"时以真实对象调用，发射并带参数。 */
void* XModbusAtv61_statusUpdated_signal(XModbusAtv61* driver)
{
    if (driver) {
        const XModbusAtv61Status* status = &driver->m_status;
        XVarList* args = XVarList_Create(XVar(const XModbusAtv61Status*, status));
        XObject_emitSignal((XObject*)driver,
                           (size_t)XModbusAtv61_statusUpdated_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XModbusAtv61_statusUpdated_signal;
}

void* XModbusAtv61_commandCompleted_signal(XModbusAtv61* driver, XModbusAtv61_Operation op, bool ok)
{
    if (driver) {
        XModbusAtv61_Operation opCopy = op;
        bool okCopy = ok;
        XVarList* args = XVarList_Create(XVar(XModbusAtv61_Operation, opCopy),
                                         XVar(bool, okCopy));
        XObject_emitSignal((XObject*)driver,
                           (size_t)XModbusAtv61_commandCompleted_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XModbusAtv61_commandCompleted_signal;
}

void* XModbusAtv61_errorOccurred_signal(XModbusAtv61* driver, const char* message)
{
    if (driver) {
        const char* msg = message ? message : "未知错误";
        XVarList* args = XVarList_Create(XVar(const char*, msg));
        XObject_emitSignal((XObject*)driver,
                           (size_t)XModbusAtv61_errorOccurred_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XModbusAtv61_errorOccurred_signal;
}

#endif /* XMODBUS_ATV61_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */
