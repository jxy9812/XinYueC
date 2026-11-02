/**
 * @file       XVfdCiA402.c
 * @brief      通用 CiA402 / Drivecom 状态机骨架基类实现。
 * @details    本类只含「CiA402 状态迁移」的通用骨架：状态字解码、启动序列
 *             逐级推进与期望态轮询、停机族命令、故障复位两步序列。
 *             品牌知识（状态域掩码/状态码/命令字/状态位/时序）全在
 *             XVfdCiA402Profile 里；品牌专有辅助位语义走
 *             EXVfdCiA402_DecodeBrand 虚槽由子类补齐。
 */
#include "XVfdCiA402.h"
#if XVFD_ON
#if XVFD_CIA402_ON

#include "XVfdCiA402_Protected.h"
#include "XVfdDevice_Protected.h"
#include <string.h>

/* ==================== 前置声明 ==================== */

/**
 * @brief 启动序列单步：读状态字 → 判定 → 写下一命令字（写落定后由定时器继续）。
 * @param dev 实例；不能为 NULL。
 * @return 序列仍在推进返回 true；已判失败返回 false（动作已收口）。
 */
static bool cia402Step(XVfdCiA402* dev);

/* ==================== 画像表与配置 ==================== */

void XVfdCiA402_setProfile(XVfdCiA402* dev, const XVfdCiA402Profile* profile)
{
    if (!dev) return;
    dev->m_profile = profile;
    dev->m_stepDelayMs = profile ? (uint32_t)profile->m_stepDelayMs : 0;
}

const XVfdCiA402Profile* XVfdCiA402_profile(const XVfdCiA402* dev)
{
    return dev ? dev->m_profile : NULL;
}

const XVfdCiA402Profile* XVfdCiA402_profileOf(const XVfdCiA402* dev)
{
    return dev ? dev->m_profile : NULL;
}

void XVfdCiA402_setStepDelay(XVfdCiA402* dev, uint32_t delayMs)
{
    if (!dev) return;
    if (delayMs == 0) {
        dev->m_stepDelayMs = dev->m_profile ? (uint32_t)dev->m_profile->m_stepDelayMs : 0;
    } else {
        dev->m_stepDelayMs = delayMs;
    }
}

uint32_t XVfdCiA402_stepDelay(const XVfdCiA402* dev)
{
    return dev ? dev->m_stepDelayMs : 0;
}

/* ==================== 状态字解码 ==================== */

void XVfdCiA402_applyStatus(XVfdCiA402* dev, uint16_t status)
{
    const XVfdCiA402Profile* p;
    XVfdStatus* st;
    uint16_t state;
    if (!dev) return;
    p = dev->m_profile;
    st = XVfdDevice_status(&dev->m_base);
    dev->m_etaCache = status;
    st->m_rawStateWord = status;
    st->m_validMask |= XVfdField_RawStateWord;
    if (p) {
        st->m_running = (status & p->m_bitRunning) != 0;
        st->m_faulted = (status & p->m_bitFault) != 0;
        st->m_warning = (status & p->m_bitWarning) != 0;
        st->m_reverse = (status & p->m_bitReverse) != 0;
        st->m_targetReached = (status & p->m_bitTargetReached) != 0;
        st->m_remote = (status & p->m_bitRemote) != 0;
        st->m_validMask |= XVfdField_Running | XVfdField_Faulted | XVfdField_Warning
                         | XVfdField_Reverse | XVfdField_TargetReached | XVfdField_Remote;
        state = (uint16_t)(status & p->m_stateMask);
        if (state == p->m_states[XVFD_CIA402_STATE_DISABLED]) {
            st->m_runState = XVfdRunState_Stopped;
        } else if (state == p->m_states[XVFD_CIA402_STATE_READY] ||
                   state == p->m_states[XVFD_CIA402_STATE_SWITCHED]) {
            st->m_runState = XVfdRunState_Ready;
        } else if (state == p->m_states[XVFD_CIA402_STATE_RUNNING]) {
            st->m_runState = st->m_reverse ? XVfdRunState_RunningReverse
                                           : XVfdRunState_RunningForward;
        } else if (state == p->m_states[XVFD_CIA402_STATE_QUICKSTOP]) {
            st->m_runState = XVfdRunState_QuickStopping;
        } else {
            st->m_runState = st->m_faulted ? XVfdRunState_Fault : XVfdRunState_Unknown;
        }
        st->m_validMask |= XVfdField_RunState;
    }
    /* 品牌专有位（加速/减速/直流制动等）：交给品牌重载解码虚槽。 */
    XClassGetVirtualFunc(dev, EXVfdCiA402_DecodeBrand,
                         void (*)(XVfdCiA402*, uint16_t, uint16_t, XVfdStatus*))
        (dev, status, dev->m_auxCache, st);
    XVfdDevice_markDirty(&dev->m_base);
}

void XVfdCiA402_applyAux(XVfdCiA402* dev, uint16_t aux)
{
    if (!dev) return;
    dev->m_auxCache = aux;
    XVfdCiA402_applyStatus(dev, dev->m_etaCache);
}

/**
 * @brief 品牌重载解码虚槽的缺省实现：通用部分已解完，品牌无附加语义。
 * @param dev 实例；不能为 NULL。
 * @param status 状态字原始值（未用）。
 * @param aux 辅助状态字原始值（未用）。
 * @param st 状态快照（未用）。
 * @return 无。
 */
static void VXVfdCiA402_DecodeBrand(XVfdCiA402* dev, uint16_t status, uint16_t aux,
                                    XVfdStatus* st)
{
    (void)dev;
    (void)status;
    (void)aux;
    (void)st;
}

/**
 * @brief DecodePoint 重载：状态字/辅助字/故障码走 CiA402 语义，其余交基类。
 * @param d 设备实例。
 * @param spec 点规格。
 * @param raw 原生值。
 * @return 无。
 */
static void VXVfdCiA402_DecodePoint(XVfdDevice* d, const XVfdPointSpec* spec, int32_t raw)
{
    XVfdCiA402* dev = (XVfdCiA402*)d;
    if (!dev || !spec) return;
    switch (spec->m_role) {
    case XVfdRole_StatusWord:
        XVfdCiA402_applyStatus(dev, (uint16_t)raw);
        return;
    case XVfdRole_AuxStateWord:
        XVfdCiA402_applyAux(dev, (uint16_t)raw);
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

uint16_t XVfdCiA402_nextCommand(const XVfdCiA402* dev, uint16_t status, bool reverse,
                                 uint16_t* outExpect)
{
    const XVfdCiA402Profile* p;
    uint16_t cmd;
    uint16_t state;
    if (!dev || !outExpect) return 0;
    p = dev->m_profile;
    if (!p) { *outExpect = 0; return 0; }
    state = (uint16_t)(status & p->m_stateMask);
    if (state == p->m_states[XVFD_CIA402_STATE_DISABLED]) {
        cmd = p->m_cmdShutdown;
        *outExpect = p->m_states[XVFD_CIA402_STATE_READY];
    } else if (state == p->m_states[XVFD_CIA402_STATE_READY]) {
        cmd = p->m_cmdSwitchOn;
        *outExpect = p->m_states[XVFD_CIA402_STATE_SWITCHED];
    } else {
        cmd = p->m_cmdRun;
        *outExpect = p->m_states[XVFD_CIA402_STATE_RUNNING];
    }
    if (reverse) cmd |= p->m_cmdReverse;
    return cmd;
}

static bool cia402Step(XVfdCiA402* dev)
{
    const XVfdCiA402Profile* p;
    XVfdDevice* d;
    XVfdStatus* st;
    int32_t eta = 0;
    if (!dev) return false;
    d = &dev->m_base;
    p = dev->m_profile;
    if (!p) {
        XVfdDevice_setLastError(d, XVfdError_NotSupported);
        XVfdDevice_emitError(d, "品牌画像表未挂接，无法推进启动序列");
        XVfdDevice_finishAction(d, false);
        return false;
    }
    st = XVfdDevice_status(d);
    if (!XVfdDevice_readRole(d, XVfdRole_StatusWord, &eta)) {
        XVfdDevice_emitError(d, "启动序列读取状态字失败（状态点无数据）");
        XVfdDevice_finishAction(d, false);
        return false;
    }
    XVfdCiA402_applyStatus(dev, (uint16_t)eta);
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
    if (dev->m_step >= (int)p->m_maxStep) {
        XVfdDevice_setLastError(d, XVfdError_Timeout);
        XVfdDevice_emitError(d, "启动超时：变频器未进入运行状态");
        XVfdDevice_finishAction(d, false);
        return false;
    }
    dev->m_pendingCmd = XVfdCiA402_nextCommand(dev, (uint16_t)eta, dev->m_reverse,
                                               &dev->m_expectEta);
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
 * @brief 启动：CiA402 状态机推进（读状态字 → 逐级写命令字）。
 * @param d 设备实例。
 * @param reverse true=反转。
 * @return 受理返回 true。
 */
static bool VXVfdCiA402_Start(XVfdDevice* d, bool reverse)
{
    XVfdCiA402* dev = (XVfdCiA402*)d;
    bool progressing;
    if (!XVfdDevice_beginAction(d, XVfdOperation_Start)) return false;
    dev->m_reverse = reverse;
    dev->m_step = 0;
    dev->m_waitingWrite = false;
    progressing = cia402Step(dev);
    if (!progressing && XVfdDevice_lastError(d) != XVfdError_OK) return false;
    return true;
}

/**
 * @brief 受控停机：写 Disable operation。
 * @param d 设备实例。
 * @return 受理返回 true。
 */
static bool VXVfdCiA402_Stop(XVfdDevice* d)
{
    const XVfdCiA402Profile* p = XVfdCiA402_profileOf((const XVfdCiA402*)d);
    return XVfdDevice_actionWrite(d, XVfdOperation_Stop, XVfdRole_CommandWord,
                                  (int32_t)(p ? p->m_cmdDisableOp : 0));
}

/**
 * @brief 自由停车：写 Disable voltage。
 * @param d 设备实例。
 * @return 受理返回 true。
 */
static bool VXVfdCiA402_FreeStop(XVfdDevice* d)
{
    const XVfdCiA402Profile* p = XVfdCiA402_profileOf((const XVfdCiA402*)d);
    return XVfdDevice_actionWrite(d, XVfdOperation_FreeStop, XVfdRole_CommandWord,
                                  (int32_t)(p ? p->m_cmdDisableVoltage : 0));
}

/**
 * @brief 快速停机：写 Quick stop。
 * @param d 设备实例。
 * @return 受理返回 true。
 */
static bool VXVfdCiA402_QuickStop(XVfdDevice* d)
{
    const XVfdCiA402Profile* p = XVfdCiA402_profileOf((const XVfdCiA402*)d);
    return XVfdDevice_actionWrite(d, XVfdOperation_QuickStop, XVfdRole_CommandWord,
                                  (int32_t)(p ? p->m_cmdQuickStop : 0));
}

/**
 * @brief 故障复位：上升沿命令字 → 归位命令字（两步序列）。
 * @param d 设备实例。
 * @return 受理返回 true。
 */
static bool VXVfdCiA402_ResetFault(XVfdDevice* d)
{
    XVfdCiA402* dev = (XVfdCiA402*)d;
    const XVfdCiA402Profile* p = XVfdCiA402_profileOf(dev);
    if (!XVfdDevice_beginAction(d, XVfdOperation_ResetFault)) return false;
    dev->m_pendingReset = true;
    if (!XVfdDevice_writeRole(d, XVfdRole_CommandWord,
                              (int32_t)(p ? p->m_cmdFaultReset : 0))) {
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
static void VXVfdCiA402_ActionWriteDone(XVfdDevice* d, XIoPoint* point, bool ok)
{
    XVfdCiA402* dev = (XVfdCiA402*)d;
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
    if (op == XVfdOperation_ResetFault && dev->m_pendingReset) {
        dev->m_pendingReset = false;
        if (!ok) {
            XVfdDevice_finishAction(d, false);
            return;
        }
        if (!XVfdDevice_writeRole(d, XVfdRole_CommandWord,
                                  (int32_t)(dev->m_profile
                                            ? dev->m_profile->m_cmdDisableVoltage : 0))) {
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
 * @note 步进节拍不再盲等固定毫秒：写落定后周期性读缓存状态字，直到状态
 *       迁移到期望态（总线上读/写排队延迟不影响序列正确性）；等待超过
 *       画像表的轮询上限拍判超时。
 */
static void VXVfdCiA402_ActionTimerExpired(XVfdDevice* d)
{
    XVfdCiA402* dev = (XVfdCiA402*)d;
    const XVfdCiA402Profile* p;
    if (!dev) return;
    p = dev->m_profile;
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
            XVfdCiA402_applyStatus(dev, (uint16_t)eta);
            if (p && ((uint16_t)(dev->m_etaCache & p->m_stateMask) == dev->m_expectEta)) {
                cia402Step(dev);          /* 状态已迁移：推进下一步 */
                return;
            }
            if (p && ++dev->m_waitPolls > (int)p->m_pollLimit) {
                XVfdDevice_setLastError(d, XVfdError_Timeout);
                XVfdDevice_emitError(d, "启动超时：状态未按序列迁移");
                XVfdDevice_finishAction(d, false);
                return;
            }
            XVfdDevice_armActionTimer(d, p ? p->m_pollIntervalMs : 60); /* 未迁移：继续等 */
            return;
        }
    }
    XClass_Parent(XVfdDevice, EXVfdDevice_ActionTimerExpired,
                  void (*)(XVfdDevice*))(d);
}

/* ==================== 虚函数表 / 生命周期 ==================== */

XVtable* XVfdCiA402_class_init(void)
{
    void* table[] = { VXVfdCiA402_DecodeBrand };
    XVTABLE_INIT_DEFAULT(XVfdCiA402)
    XVTABLE_INHERIT_XCLASS(XVfdDevice);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_DecodePoint, VXVfdCiA402_DecodePoint);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_ActionWriteDone, VXVfdCiA402_ActionWriteDone);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_ActionTimerExpired, VXVfdCiA402_ActionTimerExpired);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_Start, VXVfdCiA402_Start);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_Stop, VXVfdCiA402_Stop);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_FreeStop, VXVfdCiA402_FreeStop);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_QuickStop, VXVfdCiA402_QuickStop);
    XVTABLE_OVERLOAD_DEFAULT(EXVfdDevice_ResetFault, VXVfdCiA402_ResetFault);
    XCLASS_SET_CLASS_NAME_DEFAULT("XVfdCiA402");
    XCLASS_SHOW_SIZE_DEFAULT(XVfdCiA402);
    return XVTABLE_DEFAULT;
}

void XVfdCiA402_init(XVfdCiA402* dev)
{
    if (!dev) return;
    memset(dev, 0, sizeof(XVfdCiA402));
    XVfdDevice_init(&dev->m_base);
    XClassGetVtable(dev) = XVfdCiA402_class_init();
}

#endif /* XVFD_CIA402_ON */
#endif /* XVFD_ON */
