/**
 * @file XVfdModbusBase.c
 * @brief Modbus 语义中间基类实现（轮询组引擎 + 通用参数通道 + 动作收口）。
 */
#include "XVfdModbusBase.h"
#if XVFD_ON
#if XVFD_DEVICE_ON

#include "XVfdModbusBase_Protected.h"
#include "XVfdDevice_Protected.h"
#include "XVfdLink.h"
#include "XDateTime.h"
#include <string.h>

/* =============== 内部工具 =============== */

/** @brief 计划表到期蹦床：转设备虚槽 PollDue（在 setLink 登记前声明） */
static void mbPollDueTrampoline(XObject* owner, void* userData);

/** @brief 未绑定链路的统一拒绝 */
static bool mbNoLink(XVfdDevice* dev)
{
    dev->m_lastError = XVfdError_NotAttached;
    return false;
}

/* =============== 内部完成回调 =============== */

/** @brief 忙碌收口后的统一续程：设备空闲且有挂起动作时补发（动作优先机制） */
static void mbAfterIdle(XVfdModbusBase* base);

/** @brief 补发挂起动作：经设备虚槽重新分派（此时设备级单飞已空闲） */
static void mbDispatchPending(XVfdModbusBase* base, const XVfdModbusPendingAction* a)
{
    XVfdDevice* dev = &base->m_base;
    switch (a->m_op) {
    case XVfdOperation_Start: XVfdDevice_start(dev, a->m_b1); break;
    case XVfdOperation_Stop: XVfdDevice_stop(dev); break;
    case XVfdOperation_FreeStop: XVfdDevice_freeStop(dev); break;
    case XVfdOperation_QuickStop: XVfdDevice_quickStop(dev); break;
    case XVfdOperation_Jog: XVfdDevice_jog(dev, a->m_b1); break;
    case XVfdOperation_ResetFault: XVfdDevice_resetFault(dev); break;
    case XVfdOperation_SetFrequency: XVfdDevice_setFrequency(dev, a->m_d1); break;
    case XVfdOperation_SetSpeed: XVfdDevice_setSpeedRpm(dev, a->m_i1); break;
    case XVfdOperation_SetAccelDecel: XVfdDevice_setAccelDecel(dev, a->m_i1, a->m_d1, a->m_d2); break;
    case XVfdOperation_SetPresetSpeedHz: XVfdDevice_setPresetSpeedHz(dev, a->m_i1, a->m_d1); break;
    case XVfdOperation_SetPidTarget: XVfdDevice_setPidTarget(dev, a->m_d1); break;
    default: break; /* 不支持挂起的动作类型：丢弃 */
    }
}

static void mbAfterIdle(XVfdModbusBase* base)
{
    if (!base || base->m_base.m_busy) return;
    while (!base->m_base.m_busy && base->m_pendingCount > 0) {
        XVfdModbusPendingAction pending = base->m_pending[base->m_pendingHead];
        base->m_pending[base->m_pendingHead].m_op = XVfdOperation_None;
        base->m_pendingHead = (base->m_pendingHead + 1) % XVFD_MODBUS_PENDING_CAP;
        --base->m_pendingCount;
        mbDispatchPending(base, &pending); /* 补发：可能重新置忙（轮询/动作），循环自动收敛 */
    }
}

/** @brief 轮询读收口：解析回填 → 发状态快照 → 清设备级忙 → 补发挂起动作 */
static void mbPollDone(XObject* owner, void* userData, bool ok, const XModbusDataUnit* result)
{
    XVfdModbusBase* base = (XVfdModbusBase*)owner;
    int groupIdx = (int)(intptr_t)userData;
    XVfdModbusParseFunc parse;
    if (!base) return;
    base->m_base.m_busy = false;
    if (ok && result && groupIdx >= 0 && groupIdx < XVFD_MODBUS_POLL_GROUP_CAP
        && base->m_groupUsed[groupIdx]) {
        parse = (XVfdModbusParseFunc)base->m_groupParse[groupIdx];
        if (parse) parse((XObject*)base, result);
        XVfdDevice_emitStatus(&base->m_base);
    }
    mbAfterIdle(base);
}

/** @brief 参数读收口：发 paramReadDone → 清忙 */
static void mbParamReadDone(XObject* owner, void* userData, bool ok, const XModbusDataUnit* result)
{
    XVfdModbusBase* base = (XVfdModbusBase*)owner;
    uint16_t paramId = (uint16_t)(intptr_t)userData;
    int32_t raw = 0;
    if (!base) return;
    if (ok && result && XModbusDataUnit_valueCount(result) > 0) {
        raw = (int32_t)(int16_t)XModbusDataUnit_value(result, 0);
    }
    base->m_base.m_busy = false;
    base->m_paramReadPending = false;
    XVfdDevice_paramReadDone_signal(&base->m_base, paramId, raw, ok);
    mbAfterIdle(base);
}

/** @brief 参数写收口：发 paramWriteDone → 清忙 */
static void mbParamWriteDone(XObject* owner, void* userData, bool ok, const XModbusDataUnit* result)
{
    XVfdModbusBase* base = (XVfdModbusBase*)owner;
    uint16_t paramId = (uint16_t)(intptr_t)userData;
    (void)result;
    if (!base) return;
    base->m_base.m_busy = false;
    base->m_paramWritePending = false;
    XVfdDevice_paramWriteDone_signal(&base->m_base, paramId, ok);
    mbAfterIdle(base);
}

/** @brief 单写动作收口：发 commandCompleted → 清忙 */
static void mbActionDone(XObject* owner, void* userData, bool ok, const XModbusDataUnit* result)
{
    XVfdModbusBase* base = (XVfdModbusBase*)owner;
    XVfdOperation op = (XVfdOperation)(intptr_t)userData;
    (void)result;
    if (!base) return;
    XVfdDevice_emitDone(&base->m_base, op, ok);
}

/* =============== 虚函数实现（XVfdDevice 槽） =============== */

/** @brief PollDue：设备级单飞空闲时按掩码轮转提交下一组轮询读 */
static bool VXVfdModbusBase_PollDue(XVfdDevice* dev)
{
    XVfdModbusBase* base = (XVfdModbusBase*)dev;
    int scanned;
    if (!base || !base->m_mblink) return false;
    if (base->m_base.m_busy) return false; /* 忙：本拍放弃，计划表下拍重试 */
    for (scanned = 0; scanned < XVFD_MODBUS_POLL_GROUP_CAP; ++scanned) {
        int idx = base->m_pollCursor;
        base->m_pollCursor = (base->m_pollCursor + 1) % XVFD_MODBUS_POLL_GROUP_CAP;
        if (!base->m_groupUsed[idx]) continue;
        if ((base->m_base.m_pollItems & base->m_groupMask[idx]) == 0) continue;
        if (XVfdModbusBase_submitRead(base, base->m_groupAddr[idx], base->m_groupCount[idx],
                                      mbPollDone, (void*)(intptr_t)idx, XModbusRequestPriority_Normal)) {
            base->m_base.m_busy = true;
            XVfdLink_markPollSent(XVfdDevice_link(&base->m_base), (XObject*)base);
            return true;
        }
        return false; /* 链路拒收（忙/表满）：下拍重试 */
    }
    return false;
}

/** @brief ReadParam：描述表寻址 + 设备级单飞 + FC03 读单点 */
static bool VXVfdModbusBase_ReadParam(XVfdDevice* dev, uint16_t paramId)
{
    XVfdModbusBase* base = (XVfdModbusBase*)dev;
    if (!base || !base->m_mblink) return mbNoLink(dev);
    if (paramId >= (uint16_t)base->m_paramCount) {
        dev->m_lastError = XVfdError_InvalidParam;
        return false;
    }
    if (dev->m_busy) {
        dev->m_lastError = XVfdError_Busy;
        return false;
    }
    if (!XVfdModbusBase_submitRead(base, base->m_params[paramId].m_addr, 1,
                                   mbParamReadDone, (void*)(intptr_t)paramId, XModbusRequestPriority_Normal)) {
        dev->m_lastError = XVfdError_Busy;
        return false;
    }
    dev->m_busy = true;
    base->m_paramReadPending = true;
    base->m_pendingParamId = paramId;
    return true;
}

/** @brief WriteParam：描述表寻址 + 存取约束（运行禁改/厂家参数）+ FC06 写单点 */
static bool VXVfdModbusBase_WriteParam(XVfdDevice* dev, uint16_t paramId, int32_t raw)
{
    XVfdModbusBase* base = (XVfdModbusBase*)dev;
    const XVfdParamDesc* desc;
    if (!base || !base->m_mblink) return mbNoLink(dev);
    if (paramId >= (uint16_t)base->m_paramCount) {
        dev->m_lastError = XVfdError_InvalidParam;
        return false;
    }
    desc = &base->m_params[paramId];
    if (desc->m_access == XVfdParamAccess_Factory) {
        dev->m_lastError = XVfdError_NotSupported;
        return false;
    }
    if (desc->m_access == XVfdParamAccess_RunLocked && dev->m_status.m_running) {
        dev->m_lastError = XVfdError_ParamRunLocked;
        XVfdDevice_emitError(dev, "参数运行状态禁改");
        return false;
    }
    if (dev->m_busy) {
        dev->m_lastError = XVfdError_Busy;
        return false;
    }
    if (raw < desc->m_min || raw > desc->m_max) {
        dev->m_lastError = XVfdError_ParamRejected;
        return false;
    }
    if (!XVfdModbusBase_submitWriteSingle(base, desc->m_addr, (uint16_t)(uint32_t)raw,
                                          mbParamWriteDone, (void*)(intptr_t)paramId, XModbusRequestPriority_Normal)) {
        dev->m_lastError = XVfdError_Busy;
        return false;
    }
    dev->m_busy = true;
    base->m_paramWritePending = true;
    base->m_pendingParamId = paramId;
    return true;
}

/* =============== 虚函数表 / 生命周期 =============== */
XVtable* XVfdModbusBase_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVfdModbusBase)
    XVTABLE_INHERIT_XCLASS(XVfdDevice);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_PollDue, VXVfdModbusBase_PollDue);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_ReadParam, VXVfdModbusBase_ReadParam);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_WriteParam, VXVfdModbusBase_WriteParam);
    XCLASS_SET_CLASS_NAME_DEFAULT("XVfdModbusBase");
    XCLASS_SHOW_SIZE_DEFAULT(XVfdModbusBase);
    return XVTABLE_DEFAULT;
}

XVfdModbusBase* XVfdModbusBase_create_ex(XMemoryType memory)
{
    XVfdModbusBase* base = (XVfdModbusBase*)XMemory_malloc(sizeof(XVfdModbusBase), memory);
    if (!base) return NULL;
    XVfdModbusBase_init(base);
    Set_Class_Memory(base, memory);
    Set_Class_IsHeap(base, true);
    return base;
}

void XVfdModbusBase_init(XVfdModbusBase* base)
{
    if (!base) return;
    memset(base, 0, sizeof(XVfdModbusBase));
    XVfdDevice_init(&base->m_base);
    XClassGetVtable(base) = XVfdModbusBase_class_init();
}

/* =============== 绑定 =============== */
bool XVfdModbusBase_setLink(XVfdModbusBase* base, XVfdModbusLink* link, int unitId)
{
    if (!base || !link) return false;
    if (!XVfdDevice_attach(&base->m_base, &link->m_base, unitId)) return false;
    base->m_mblink = link;
    if (unitId >= 1 && base->m_base.m_pollIntervalMs > 0) {
        XVfdLink_registerPoll(&link->m_base, (XObject*)base, NULL,
                              mbPollDueTrampoline, unitId, base->m_base.m_pollIntervalMs);
        XVfdLink_startPolling(&link->m_base);
    }
    return true;
}

void XVfdModbusBase_detach(XVfdModbusBase* base)
{
    if (!base) return;
    XVfdDevice_detach(&base->m_base); /* 基类内注销链路计划 */
    base->m_mblink = NULL;
    base->m_base.m_busy = false;
}

int XVfdModbusBase_paramCount(const XVfdModbusBase* base)
{
    return base ? base->m_paramCount : 0;
}

const XVfdParamDesc* XVfdModbusBase_paramDesc_const(const XVfdModbusBase* base, uint16_t paramId)
{
    if (!base || paramId >= (uint16_t)base->m_paramCount) return NULL;
    return &base->m_params[paramId];
}

/* =============== 子类装配辅助（Protected） =============== */

/** @brief 计划表到期蹦床：转设备虚槽 PollDue */
static void mbPollDueTrampoline(XObject* owner, void* userData)
{
    (void)userData;
    XVfdDevice_pollDue_base((XVfdDevice*)owner);
}


int XVfdModbusBase_addPollGroup(XVfdModbusBase* base, uint16_t addr, uint16_t count,
                                uint32_t areaMask, XVfdModbusParseFunc parse)
{
    int i;
    if (!base || !parse || count == 0) return -1;
    for (i = 0; i < XVFD_MODBUS_POLL_GROUP_CAP; ++i) {
        if (base->m_groupUsed[i]) continue;
        base->m_groupAddr[i] = addr;
        base->m_groupCount[i] = count;
        base->m_groupMask[i] = areaMask;
        base->m_groupParse[i] = (void*)parse;
        base->m_groupUsed[i] = true;
        return i;
    }
    return -1;
}

void XVfdModbusBase_setParamTable(XVfdModbusBase* base, const XVfdParamDesc* table, int count)
{
    if (!base) return;
    base->m_params = table;
    base->m_paramCount = count > 0 ? count : 0;
}

bool XVfdModbusBase_submitRead(XVfdModbusBase* base, uint16_t addr, uint16_t count,
                               XVfdModbusLinkDoneFunc done, void* userData,
                               XModbusRequestPriority priority)
{
    if (!base || !base->m_mblink) return false;
    return XVfdModbusLink_submitRead(base->m_mblink, (XObject*)base,
                                     base->m_base.m_unitId, addr, count, done, userData,
                                     priority);
}

bool XVfdModbusBase_submitWriteSingle(XVfdModbusBase* base, uint16_t addr, uint16_t value,
                                      XVfdModbusLinkDoneFunc done, void* userData,
                                      XModbusRequestPriority priority)
{
    bool ok;
    if (!base || !base->m_mblink) return false;
    ok = XVfdModbusLink_submitWriteSingle(base->m_mblink, (XObject*)base,
                                          base->m_base.m_unitId, addr, value, done, userData,
                                          priority);
    return ok;
}

bool XVfdModbusBase_busyStart(XVfdModbusBase* base, XVfdOperation op)
{
    if (!base) return false;
    if (base->m_base.m_busy) {
        base->m_base.m_lastError = XVfdError_Busy;
        return false;
    }
    base->m_base.m_busy = true;
    base->m_op = op;
    return true;
}

void XVfdModbusBase_finishAction(XVfdModbusBase* base, bool ok)
{
    if (!base) return;
    XVfdDevice_emitDone(&base->m_base, base->m_op, ok);
    mbAfterIdle(base);
}

int XVfdModbusBase_actionEntry(XVfdModbusBase* base, XVfdOperation op,
                               bool b1, int i1, double d1, double d2)
{
    XVfdDevice* dev;
    if (!base) return -1;
    dev = &base->m_base;
    if (!dev->m_link) {
        dev->m_lastError = XVfdError_NotAttached;
        return -1;
    }
    if (!dev->m_busy) return 0; /* 空闲：调用方继续 busyStart+提交 */
    if (base->m_pendingCount < XVFD_MODBUS_PENDING_CAP) {
        int tail = (base->m_pendingHead + base->m_pendingCount) % XVFD_MODBUS_PENDING_CAP;
        base->m_pending[tail].m_op = op;        /* 挂起：收口后补发（动作优先） */
        base->m_pending[tail].m_b1 = b1;
        base->m_pending[tail].m_i1 = i1;
        base->m_pending[tail].m_d1 = d1;
        base->m_pending[tail].m_d2 = d2;
        ++base->m_pendingCount;
        return 1;
    }
    dev->m_lastError = XVfdError_Busy;
    return -1;
}

XVfdOperation XVfdModbusBase_currentOp(XVfdModbusBase* base)
{
    return base ? base->m_op : XVfdOperation_None;
}

#endif /* XVFD_DEVICE_ON */
#endif /* XVFD_ON */
