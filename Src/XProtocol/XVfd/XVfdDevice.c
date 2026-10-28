/**
 * @file XVfdDevice.c
 * @brief 变频器抽象父类实现。
 * @details 本文件实现：24 个业务虚槽的缺省实现（一律 NotSupported）、绑定与
 *          轮询配置、全部 *_base 虚表分派入口、信号发射与子类回填辅助。
 *          业务逻辑在子类（XVfdModbusBase → XVfdAtv61 / XVfdM512）的
 *          VXxx 虚函数实现中，本文件不含任何协议细节。
 */
#include "XVfdDevice.h"
#if XVFD_ON
#if XVFD_DEVICE_ON

#include "XVfdDevice_Protected.h"
#include "XVfdLink.h"
#include "XVarList.h"
#include <string.h>

/* =============== 缺省虚函数实现（一律 NotSupported） =============== */
static bool VXVfdDevice_PollDue(XVfdDevice* dev) { (void)dev; return false; }

static bool notSupported(XVfdDevice* dev)
{
    if (dev) dev->m_lastError = XVfdError_NotSupported;
    return false;
}

static bool VXVfdDevice_Start(XVfdDevice* dev, bool reverse) { (void)reverse; return notSupported(dev); }
static bool VXVfdDevice_Stop(XVfdDevice* dev) { return notSupported(dev); }
static bool VXVfdDevice_FreeStop(XVfdDevice* dev) { return notSupported(dev); }
static bool VXVfdDevice_QuickStop(XVfdDevice* dev) { return notSupported(dev); }
static bool VXVfdDevice_Jog(XVfdDevice* dev, bool reverse) { (void)reverse; return notSupported(dev); }
static bool VXVfdDevice_SetFrequency(XVfdDevice* dev, double hz) { (void)hz; return notSupported(dev); }
static bool VXVfdDevice_SetSpeedRpm(XVfdDevice* dev, int rpm) { (void)rpm; return notSupported(dev); }
static bool VXVfdDevice_SetAccelDecel(XVfdDevice* dev, int index, double accelS, double decelS)
{ (void)index; (void)accelS; (void)decelS; return notSupported(dev); }
static bool VXVfdDevice_SelectPresetSpeed(XVfdDevice* dev, int index) { (void)index; return notSupported(dev); }
static bool VXVfdDevice_SetPresetSpeedHz(XVfdDevice* dev, int index, double hz)
{ (void)index; (void)hz; return notSupported(dev); }
static bool VXVfdDevice_SetPidTarget(XVfdDevice* dev, double value) { (void)value; return notSupported(dev); }
static bool VXVfdDevice_SetCommandSource(XVfdDevice* dev, int source) { (void)source; return notSupported(dev); }
static bool VXVfdDevice_SetFrequencySource(XVfdDevice* dev, int source) { (void)source; return notSupported(dev); }
static bool VXVfdDevice_ResetFault(XVfdDevice* dev) { return notSupported(dev); }
static bool VXVfdDevice_SetDigitalOutput(XVfdDevice* dev, int index, bool on)
{ (void)index; (void)on; return notSupported(dev); }
static bool VXVfdDevice_StartMotorTuning(XVfdDevice* dev, int mode) { (void)mode; return notSupported(dev); }
static bool VXVfdDevice_FactoryReset(XVfdDevice* dev, int scope) { (void)scope; return notSupported(dev); }
static bool VXVfdDevice_UnlockPassword(XVfdDevice* dev, uint32_t password) { (void)password; return notSupported(dev); }
static bool VXVfdDevice_SaveParameters(XVfdDevice* dev) { return notSupported(dev); }
static bool VXVfdDevice_ReadParam(XVfdDevice* dev, uint16_t paramId) { (void)paramId; return notSupported(dev); }
static bool VXVfdDevice_WriteParam(XVfdDevice* dev, uint16_t paramId, int32_t raw)
{ (void)paramId; (void)raw; return notSupported(dev); }
static bool VXVfdDevice_ReadNameplate(XVfdDevice* dev) { return notSupported(dev); }
static bool VXVfdDevice_ReadFaultHistory(XVfdDevice* dev) { return notSupported(dev); }

/* =============== 虚函数表 =============== */
XVtable* XVfdDevice_class_init(void)
{
    void* table[] = {
        VXVfdDevice_PollDue,
        VXVfdDevice_Start, VXVfdDevice_Stop, VXVfdDevice_FreeStop, VXVfdDevice_QuickStop,
        VXVfdDevice_Jog, VXVfdDevice_SetFrequency, VXVfdDevice_SetSpeedRpm,
        VXVfdDevice_SetAccelDecel, VXVfdDevice_SelectPresetSpeed, VXVfdDevice_SetPresetSpeedHz,
        VXVfdDevice_SetPidTarget, VXVfdDevice_SetCommandSource, VXVfdDevice_SetFrequencySource,
        VXVfdDevice_ResetFault, VXVfdDevice_SetDigitalOutput, VXVfdDevice_StartMotorTuning,
        VXVfdDevice_FactoryReset, VXVfdDevice_UnlockPassword, VXVfdDevice_SaveParameters,
        VXVfdDevice_ReadParam, VXVfdDevice_WriteParam, VXVfdDevice_ReadNameplate,
        VXVfdDevice_ReadFaultHistory
    };
    XVTABLE_INIT_DEFAULT(XVfdDevice)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XCLASS_SET_CLASS_NAME_DEFAULT("XVfdDevice");
    XCLASS_SHOW_SIZE_DEFAULT(XVfdDevice);
    return XVTABLE_DEFAULT;
}

/* =============== 创建 / 初始化 =============== */
XVfdDevice* XVfdDevice_create_ex(XMemoryType memory)
{
    XVfdDevice* dev = (XVfdDevice*)XMemory_malloc(sizeof(XVfdDevice), memory);
    if (!dev) return NULL;
    XVfdDevice_init(dev);
    Set_Class_Memory(dev, memory);
    Set_Class_IsHeap(dev, true);
    return dev;
}

void XVfdDevice_init(XVfdDevice* dev)
{
    if (!dev) return;
    memset(dev, 0, sizeof(XVfdDevice));
    XObject_init(&dev->m_class);
    XClassGetVtable(dev) = XVfdDevice_class_init();
    dev->m_unitId = 1;
    dev->m_pollItems = XVfdPollItem_Status | XVfdPollItem_Telemetry | XVfdPollItem_Fault;
    dev->m_pollIntervalMs = 500;
}

/* =============== 绑定与配置 =============== */
bool XVfdDevice_attach(XVfdDevice* dev, XVfdLink* link, int unitId)
{
    if (!dev || !link) return false;
    if (unitId < 0 || unitId > 247) return false;
    dev->m_link = link;
    dev->m_unitId = unitId;
    return true;
}

void XVfdDevice_detach(XVfdDevice* dev)
{
    if (!dev) return;
    if (dev->m_link) XVfdLink_unregisterPollOwner(dev->m_link, (XObject*)dev);
    dev->m_link = NULL;
    dev->m_busy = false;
}

int XVfdDevice_unitId(const XVfdDevice* dev)
{
    return dev ? dev->m_unitId : 0;
}

void XVfdDevice_setUnitId(XVfdDevice* dev, int unitId)
{
    if (!dev) return;
    if (unitId < 0 || unitId > 247) return;
    dev->m_unitId = unitId;
}

uint32_t XVfdDevice_caps(const XVfdDevice* dev)
{
    return dev ? dev->m_caps : 0;
}

bool XVfdDevice_hasCapability(const XVfdDevice* dev, uint32_t cap)
{
    return dev ? (dev->m_caps & cap) != 0 : false;
}

const XVfdStatus* XVfdDevice_status_const(const XVfdDevice* dev)
{
    return dev ? &dev->m_status : NULL;
}

bool XVfdDevice_isBusy(const XVfdDevice* dev)
{
    return dev ? dev->m_busy : false;
}

XVfdError XVfdDevice_lastError(const XVfdDevice* dev)
{
    return dev ? dev->m_lastError : XVfdError_OK;
}

void XVfdDevice_setPollItems(XVfdDevice* dev, uint32_t items)
{
    if (!dev) return;
    dev->m_pollItems = items;
}

uint32_t XVfdDevice_pollItems(const XVfdDevice* dev)
{
    return dev ? dev->m_pollItems : 0;
}

void XVfdDevice_setPollInterval(XVfdDevice* dev, uint32_t intervalMs)
{
    if (!dev) return;
    dev->m_pollIntervalMs = intervalMs;
    if (dev->m_link) XVfdLink_setPollInterval(dev->m_link, (XObject*)dev, intervalMs);
}

uint32_t XVfdDevice_pollInterval(const XVfdDevice* dev)
{
    return dev ? dev->m_pollIntervalMs : 0;
}

/* =============== 虚函数分派入口（只查表分派，不写业务） =============== */
bool XVfdDevice_start(XVfdDevice* dev, bool reverse)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_Start,
                                bool(*)(XVfdDevice*, bool))(dev, reverse);
}

bool XVfdDevice_stop(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_Stop, bool(*)(XVfdDevice*))(dev);
}

bool XVfdDevice_freeStop(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_FreeStop, bool(*)(XVfdDevice*))(dev);
}

bool XVfdDevice_quickStop(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_QuickStop, bool(*)(XVfdDevice*))(dev);
}

bool XVfdDevice_jog(XVfdDevice* dev, bool reverse)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_Jog,
                                bool(*)(XVfdDevice*, bool))(dev, reverse);
}

bool XVfdDevice_setFrequency(XVfdDevice* dev, double hz)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetFrequency,
                                bool(*)(XVfdDevice*, double))(dev, hz);
}

bool XVfdDevice_setSpeedRpm(XVfdDevice* dev, int rpm)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetSpeedRpm,
                                bool(*)(XVfdDevice*, int))(dev, rpm);
}

bool XVfdDevice_setAccelDecel(XVfdDevice* dev, int index, double accelS, double decelS)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetAccelDecel,
                                bool(*)(XVfdDevice*, int, double, double))(dev, index, accelS, decelS);
}

bool XVfdDevice_selectPresetSpeed(XVfdDevice* dev, int index)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SelectPresetSpeed,
                                bool(*)(XVfdDevice*, int))(dev, index);
}

bool XVfdDevice_setPresetSpeedHz(XVfdDevice* dev, int index, double hz)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetPresetSpeedHz,
                                bool(*)(XVfdDevice*, int, double))(dev, index, hz);
}

bool XVfdDevice_setPidTarget(XVfdDevice* dev, double value)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetPidTarget,
                                bool(*)(XVfdDevice*, double))(dev, value);
}

bool XVfdDevice_setCommandSource(XVfdDevice* dev, int source)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetCommandSource,
                                bool(*)(XVfdDevice*, int))(dev, source);
}

bool XVfdDevice_setFrequencySource(XVfdDevice* dev, int source)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetFrequencySource,
                                bool(*)(XVfdDevice*, int))(dev, source);
}

bool XVfdDevice_resetFault(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_ResetFault, bool(*)(XVfdDevice*))(dev);
}

bool XVfdDevice_setDigitalOutput(XVfdDevice* dev, int index, bool on)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetDigitalOutput,
                                bool(*)(XVfdDevice*, int, bool))(dev, index, on);
}

bool XVfdDevice_startMotorTuning(XVfdDevice* dev, int mode)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_StartMotorTuning,
                                bool(*)(XVfdDevice*, int))(dev, mode);
}

bool XVfdDevice_factoryReset(XVfdDevice* dev, int scope)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_FactoryReset,
                                bool(*)(XVfdDevice*, int))(dev, scope);
}

bool XVfdDevice_unlockPassword(XVfdDevice* dev, uint32_t password)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_UnlockPassword,
                                bool(*)(XVfdDevice*, uint32_t))(dev, password);
}

bool XVfdDevice_saveParameters(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SaveParameters, bool(*)(XVfdDevice*))(dev);
}

bool XVfdDevice_readParam(XVfdDevice* dev, uint16_t paramId)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_ReadParam,
                                bool(*)(XVfdDevice*, uint16_t))(dev, paramId);
}

bool XVfdDevice_writeParam(XVfdDevice* dev, uint16_t paramId, int32_t raw)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_WriteParam,
                                bool(*)(XVfdDevice*, uint16_t, int32_t))(dev, paramId, raw);
}

bool XVfdDevice_readNameplate(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_ReadNameplate, bool(*)(XVfdDevice*))(dev);
}

bool XVfdDevice_readFaultHistory(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_ReadFaultHistory, bool(*)(XVfdDevice*))(dev);
}

/** @brief 链路计划表到期分派入口：由链路适配器经登记回调调用。 */
bool XVfdDevice_pollDue_base(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_PollDue, bool(*)(XVfdDevice*))(dev);
}

/* =============== 信号 =============== */
void* XVfdDevice_statusUpdated_signal(XVfdDevice* dev)
{
    if (dev) {
        const XVfdStatus* status = &dev->m_status;
        XVarList* args = XVarList_Create(XVar(const XVfdStatus*, status));
        XObject_emitSignal((XObject*)dev, (size_t)XVfdDevice_statusUpdated_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XVfdDevice_statusUpdated_signal;
}

void* XVfdDevice_commandCompleted_signal(XVfdDevice* dev, XVfdOperation op, bool ok)
{
    if (dev) {
        XVfdOperation opCopy = op;
        bool okCopy = ok;
        XVarList* args = XVarList_Create(XVar(XVfdOperation, opCopy), XVar(bool, okCopy));
        XObject_emitSignal((XObject*)dev, (size_t)XVfdDevice_commandCompleted_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XVfdDevice_commandCompleted_signal;
}

void* XVfdDevice_paramReadDone_signal(XVfdDevice* dev, uint16_t paramId, int32_t raw, bool ok)
{
    if (dev) {
        uint16_t idCopy = paramId;
        int32_t rawCopy = raw;
        bool okCopy = ok;
        XVarList* args = XVarList_Create(XVar(uint16_t, idCopy), XVar(int32_t, rawCopy),
                                         XVar(bool, okCopy));
        XObject_emitSignal((XObject*)dev, (size_t)XVfdDevice_paramReadDone_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XVfdDevice_paramReadDone_signal;
}

void* XVfdDevice_paramWriteDone_signal(XVfdDevice* dev, uint16_t paramId, bool ok)
{
    if (dev) {
        uint16_t idCopy = paramId;
        bool okCopy = ok;
        XVarList* args = XVarList_Create(XVar(uint16_t, idCopy), XVar(bool, okCopy));
        XObject_emitSignal((XObject*)dev, (size_t)XVfdDevice_paramWriteDone_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XVfdDevice_paramWriteDone_signal;
}

void* XVfdDevice_nameplateUpdated_signal(XVfdDevice* dev)
{
    if (dev) {
        XVarList* args = NULL; /* v1 铭牌经 paramReadDone 通道时无独立参数 */
        XObject_emitSignal((XObject*)dev, (size_t)XVfdDevice_nameplateUpdated_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XVfdDevice_nameplateUpdated_signal;
}

void* XVfdDevice_faultHistoryUpdated_signal(XVfdDevice* dev)
{
    if (dev) {
        XObject_emitSignal((XObject*)dev, (size_t)XVfdDevice_faultHistoryUpdated_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XVfdDevice_faultHistoryUpdated_signal;
}

void* XVfdDevice_errorOccurred_signal(XVfdDevice* dev, const char* message)
{
    if (dev) {
        const char* msg = message ? message : "未知错误";
        XVarList* args = XVarList_Create(XVar(const char*, msg));
        XObject_emitSignal((XObject*)dev, (size_t)XVfdDevice_errorOccurred_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    }
    return (void*)(size_t)XVfdDevice_errorOccurred_signal;
}

/* =============== 子类回填辅助（Protected） =============== */
XVfdStatus* XVfdDevice_status(XVfdDevice* dev)
{
    return dev ? &dev->m_status : NULL;
}

void XVfdDevice_emitStatus(XVfdDevice* dev)
{
    if (!dev) return;
    dev->m_status.m_valid = true;
    dev->m_haveStatus = true;
    dev->m_status.m_timestampMs = XDateTime_currentMSecsSinceEpoch();
    XVfdDevice_statusUpdated_signal(dev);
}

void XVfdDevice_emitDone(XVfdDevice* dev, XVfdOperation op, bool ok)
{
    if (!dev) return;
    dev->m_busy = false;
    XVfdDevice_commandCompleted_signal(dev, op, ok);
}

void XVfdDevice_emitError(XVfdDevice* dev, const char* message)
{
    if (!dev) return;
    XVfdDevice_errorOccurred_signal(dev, message);
}

void XVfdDevice_setLastError(XVfdDevice* dev, XVfdError error)
{
    if (dev) dev->m_lastError = error;
}

void XVfdDevice_setCapability(XVfdDevice* dev, uint32_t cap)
{
    if (dev) dev->m_caps |= cap;
}

XVfdLink* XVfdDevice_link(XVfdDevice* dev)
{
    return dev ? dev->m_link : NULL;
}

#endif /* XVFD_DEVICE_ON */
#endif /* XVFD_ON */
