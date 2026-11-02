/**
 * @file       XVfdDevice.c
 * @brief      变频器抽象父类实现（通道点驱动）。
 * @details    [2026-11-01 通道化重构] 本文件实现：
 *               1) 装配：挂接通道组 + 绑定语义槽点/参数点（全部借用）；
 *               2) 数据进：组 inputChanged → 语义槽命中 → DecodePoint 虚槽分派
 *                  （基类做通用遥测解码，品牌重写消化状态字等特有语义）；
 *               3) 控制出：组 writeDone → ActionWriteDone 虚槽分派（缺省收口）；
 *               4) 节拍：快照发布定时器（脏标记消费）+ 动作监督定时器；
 *               5) 动作单飞、参数通道、信号发射、标度换算与故障表查找。
 *             本文件**只依赖 XChannel**：不出现任何 XModbus/链路/编帧符号，
 *             协议细节全部在品牌子类与点实现中。
 */
#include "XVfdDevice.h"
#if XVFD_ON
#if XVFD_DEVICE_ON

#include "XVfdDevice_Protected.h"
#include "XVarList.h"
#include "XEvent.h"
#include "XDateTime.h"
#include "XTimer.h"
#include <string.h>

/** @brief 快照发布周期缺省值（毫秒）。 */
#define XVFD_DEFAULT_STATUS_INTERVAL_MS 200u
/** @brief 写落定监督超时缺省值（毫秒）。 */
#define XVFD_DEFAULT_ACTION_TIMEOUT_MS 2000u

/* 定时器事件槽（定义在文件后半，类初始化处需取其地址）。 */
static void VXVfdDevice_timerEvent(XObject* obj, XTimerEvent* event);

/* ==================== 内部工具 ==================== */

/**
 * @brief 角色是否合法。
 * @param role 角色值。
 * @return 合法返回 true。
 */
static bool xvfdRoleValid(int role)
{
    return (role > (int)XVfdRole_None && role < (int)XVfdRole_Count);
}

/**
 * @brief 在语义槽表中按点指针反查槽（数据进路径）。
 * @param dev 设备实例；不能为 NULL。
 * @param point 触发信号的点；不能为 NULL。
 * @return 命中槽指针；未命中返回 NULL。
 */
static XVfdSlot* xvfdSlotByPoint(XVfdDevice* dev, const XIoPoint* point)
{
    int i;
    if (!dev || !point) return NULL;
    for (i = 0; i < XVFD_ROLE_CAP; ++i) {
        if (dev->m_slots[i].m_point == point) return &dev->m_slots[i];
    }
    return NULL;
}

/**
 * @brief 取语义槽（角色 → 槽）。
 * @param dev 设备实例；不能为 NULL。
 * @param role 角色。
 * @return 槽指针；未绑定返回 NULL。
 */
static XVfdSlot* xvfdSlot(XVfdDevice* dev, int role)
{
    if (!dev || !xvfdRoleValid(role)) return NULL;
    if (!dev->m_slots[role].m_point) return NULL;
    return &dev->m_slots[role];
}

/**
 * @brief 参数点反查登记项。
 * @param dev 设备实例；不能为 NULL。
 * @param paramId 参数下标。
 * @return 登记项指针；未绑定返回 NULL。
 */
static XVfdParamBinding* xvfdParamBinding(XVfdDevice* dev, uint16_t paramId)
{
    int i;
    if (!dev) return NULL;
    for (i = 0; i < dev->m_paramBindCount; ++i) {
        if (dev->m_paramBinds[i].m_id == paramId) return &dev->m_paramBinds[i];
    }
    return NULL;
}

/**
 * @brief 写一个点（按点类型选组写 API）。
 * @param dev 设备实例；不能为 NULL。
 * @param point 目标点；不能为 NULL。
 * @param value 点单位值。
 * @return 通道受理返回 true。
 */
static bool xvfdWritePoint(XVfdDevice* dev, XIoPoint* point, int32_t value)
{
    if (!dev || !dev->m_group || !point) return false;
    if (XIoPoint_type(point) == XIoPoint_AnalogOutput) {
        return XIoChannelGroup_writeAnalogOutput(dev->m_group, point, value);
    }
    if (XIoPoint_type(point) == XIoPoint_DigitalOutput) {
        return XIoChannelGroup_writeDigitalOutput(dev->m_group, point, value != 0);
    }
    return false;
}

/**
 * @brief 读一个点（经 ReadRaw 虚槽，不发起传输）。
 * @param point 目标点；不能为 NULL。
 * @param outValue 输出点单位值；不能为 NULL。
 * @return 取到有效值返回 true。
 */
static bool xvfdReadPoint(XIoPoint* point, int32_t* outValue)
{
    if (!point || !outValue) return false;
    return XIoPoint_readRaw_base(point, outValue);
}

/**
 * @brief 启动/重启快照发布定时器。
 * @param dev 设备实例；不能为 NULL。
 * @return 无。
 */
static void xvfdStatusTimerStart(XVfdDevice* dev)
{
    if (!dev) return;
    if (dev->m_statusTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)dev, dev->m_statusTimer);
        dev->m_statusTimer = XTIMER_INVALID_ID;
    }
    dev->m_statusTimer = XObject_startTimer_ms((XObject*)dev,
        (uint64_t)dev->m_statusIntervalMs, XTimerType_CoarseTimer);
}

/* ==================== 数据进：组信号槽 ==================== */

/**
 * @brief 组 inputChanged 槽：命中语义槽 → DecodePoint 虚槽分派。
 * @param receiver 连接的接收者（即本设备）；**不是**组，禁止按组强转。
 * @param args 信号参数（XIoPoint* point, int32_t value）。
 * @return 无。
 */
static void xvfdOnInputChanged(XObject* receiver, XVarList* args)
{
    XVfdDevice* dev = (XVfdDevice*)receiver;
    XVfdSlot* slot;
    XIoPoint* point;
    int32_t value;
    if (!dev || !args) return;
    XVarList_args_2(args, XIoPoint*, pointArg, int32_t, valueArg);
    point = (XIoPoint*)pointArg;
    value = valueArg;
    slot = xvfdSlotByPoint(dev, point);
    if (!slot || !slot->m_spec) return;
    XClassGetVirtualFunc(dev, EXVfdDevice_DecodePoint,
        void (*)(XVfdDevice*, const XVfdPointSpec*, int32_t))(dev, slot->m_spec, value);
}

/**
 * @brief 组 writeDone 槽：动作在途时交 ActionWriteDone 虚槽分派。
 * @param receiver 连接的接收者（即本设备）；**不是**组。
 * @param args 信号参数（XIoPoint* point, bool ok）。
 * @return 无。
 * @note 无动作在途的 writeDone（例如业务层直接写点）静默丢弃。
 */
static void xvfdOnWriteDone(XObject* receiver, XVarList* args)
{
    XVfdDevice* dev = (XVfdDevice*)receiver;
    XIoPoint* point;
    bool ok;
    if (!dev || !args || !dev->m_busy) return;
    XVarList_args_2(args, XIoPoint*, pointArg, bool, okArg);
    point = (XIoPoint*)pointArg;
    ok = okArg;
    XClassGetVirtualFunc(dev, EXVfdDevice_ActionWriteDone,
        void (*)(XVfdDevice*, XIoPoint*, bool))(dev, point, ok);
}

/* ==================== 缺省虚函数实现 ==================== */

/**
 * @brief DecodePoint 缺省实现：通用遥测解码。
 * @param dev 设备实例。
 * @param spec 点规格。
 * @param raw 原生值。
 * @return 无。
 */
static void VXVfdDevice_DecodePoint(XVfdDevice* dev, const XVfdPointSpec* spec, int32_t raw)
{
    XVfdDevice_decodePoint_base(dev, spec, raw);
}

/**
 * @brief ActionWriteDone 缺省实现：单写动作收口。
 * @param dev 设备实例。
 * @param point 落定的点（未用）。
 * @param ok 是否成功。
 * @return 无。
 */
static void VXVfdDevice_ActionWriteDone(XVfdDevice* dev, XIoPoint* point, bool ok)
{
    (void)point;
    if (!dev) return;
    XVfdDevice_finishAction(dev, ok);
}

/**
 * @brief ActionTimerExpired 缺省实现：动作监督超时 → 失败收口。
 * @param dev 设备实例。
 * @return 无。
 */
static void VXVfdDevice_ActionTimerExpired(XVfdDevice* dev)
{
    if (!dev) return;
    if (!dev->m_busy) return;
    dev->m_lastError = XVfdError_Timeout;
    XVfdDevice_emitError(dev, "动作监督超时：写落定未在超时内返回");
    XVfdDevice_finishAction(dev, false);
}

/**
 * @brief 置 NotSupported 的公共实现。
 * @param dev 设备实例。
 * @return 恒 false。
 */
static bool xvfdNotSupported(XVfdDevice* dev)
{
    if (dev) dev->m_lastError = XVfdError_NotSupported;
    return false;
}

static bool VXVfdDevice_Start(XVfdDevice* dev, bool reverse) { (void)reverse; return xvfdNotSupported(dev); }
static bool VXVfdDevice_Stop(XVfdDevice* dev) { return xvfdNotSupported(dev); }
static bool VXVfdDevice_FreeStop(XVfdDevice* dev) { return xvfdNotSupported(dev); }
static bool VXVfdDevice_QuickStop(XVfdDevice* dev) { return xvfdNotSupported(dev); }
static bool VXVfdDevice_Jog(XVfdDevice* dev, bool reverse) { (void)reverse; return xvfdNotSupported(dev); }
static bool VXVfdDevice_SetFrequency(XVfdDevice* dev, double hz) { (void)hz; return xvfdNotSupported(dev); }
static bool VXVfdDevice_SetSpeedRpm(XVfdDevice* dev, int rpm) { (void)rpm; return xvfdNotSupported(dev); }
static bool VXVfdDevice_SetAccelDecel(XVfdDevice* dev, int index, double accelS, double decelS)
{ (void)index; (void)accelS; (void)decelS; return xvfdNotSupported(dev); }
static bool VXVfdDevice_SelectPresetSpeed(XVfdDevice* dev, int index)
{ (void)index; return xvfdNotSupported(dev); }
static bool VXVfdDevice_SetPresetSpeedHz(XVfdDevice* dev, int index, double hz)
{ (void)index; (void)hz; return xvfdNotSupported(dev); }
static bool VXVfdDevice_SetPidTarget(XVfdDevice* dev, double value)
{ (void)value; return xvfdNotSupported(dev); }
static bool VXVfdDevice_SetCommandSource(XVfdDevice* dev, int source)
{ (void)source; return xvfdNotSupported(dev); }
static bool VXVfdDevice_SetFrequencySource(XVfdDevice* dev, int source)
{ (void)source; return xvfdNotSupported(dev); }
static bool VXVfdDevice_ResetFault(XVfdDevice* dev) { return xvfdNotSupported(dev); }
static bool VXVfdDevice_SetDigitalOutput(XVfdDevice* dev, int index, bool on)
{ (void)index; (void)on; return xvfdNotSupported(dev); }
static bool VXVfdDevice_StartMotorTuning(XVfdDevice* dev, int mode)
{ (void)mode; return xvfdNotSupported(dev); }
static bool VXVfdDevice_FactoryReset(XVfdDevice* dev, int scope)
{ (void)scope; return xvfdNotSupported(dev); }
static bool VXVfdDevice_UnlockPassword(XVfdDevice* dev, uint32_t password)
{ (void)password; return xvfdNotSupported(dev); }
static bool VXVfdDevice_SaveParameters(XVfdDevice* dev) { return xvfdNotSupported(dev); }
static bool VXVfdDevice_ReadNameplate(XVfdDevice* dev) { return xvfdNotSupported(dev); }
static bool VXVfdDevice_ReadFaultHistory(XVfdDevice* dev) { return xvfdNotSupported(dev); }

/* ==================== 虚函数表 ==================== */

XVtable* XVfdDevice_class_init(void)
{
    void* table[] = {
        VXVfdDevice_DecodePoint,
        VXVfdDevice_ActionWriteDone,
        VXVfdDevice_ActionTimerExpired,
        VXVfdDevice_Start, VXVfdDevice_Stop, VXVfdDevice_FreeStop, VXVfdDevice_QuickStop,
        VXVfdDevice_Jog, VXVfdDevice_SetFrequency, VXVfdDevice_SetSpeedRpm,
        VXVfdDevice_SetAccelDecel, VXVfdDevice_SelectPresetSpeed, VXVfdDevice_SetPresetSpeedHz,
        VXVfdDevice_SetPidTarget, VXVfdDevice_SetCommandSource, VXVfdDevice_SetFrequencySource,
        VXVfdDevice_ResetFault, VXVfdDevice_SetDigitalOutput, VXVfdDevice_StartMotorTuning,
        VXVfdDevice_FactoryReset, VXVfdDevice_UnlockPassword, VXVfdDevice_SaveParameters,
        XVfdDevice_readParam_base, XVfdDevice_writeParam_base,
        VXVfdDevice_ReadNameplate, VXVfdDevice_ReadFaultHistory
    };
    XVTABLE_INIT_DEFAULT(XVfdDevice)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXVfdDevice_timerEvent);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XCLASS_SET_CLASS_NAME_DEFAULT("XVfdDevice");
    XCLASS_SHOW_SIZE_DEFAULT(XVfdDevice);
    return XVTABLE_DEFAULT;
}

/* ==================== 创建 / 初始化 ==================== */

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
    dev->m_statusIntervalMs = XVFD_DEFAULT_STATUS_INTERVAL_MS;
    dev->m_actionTimeoutMs = XVFD_DEFAULT_ACTION_TIMEOUT_MS;
    dev->m_statusTimer = XTIMER_INVALID_ID;
    dev->m_actionTimer = XTIMER_INVALID_ID;
    dev->m_lastError = XVfdError_OK;
    dev->m_op = XVfdOperation_None;
}

/* ==================== 装配 ==================== */

bool XVfdDevice_attach(XVfdDevice* dev, XIoChannelGroup* group)
{
    XConnection* c1;
    XConnection* c2;
    if (!dev || !group) return false;
    if (dev->m_group) XVfdDevice_detach(dev);
    dev->m_group = group;
    c1 = XObject_connect_1((XObject*)group, XSignal(XIoChannelGroup_inputChanged_signal),
                           (XObject*)dev, xvfdOnInputChanged, XConnectionType_Direct);
    c2 = XObject_connect_1((XObject*)group, XSignal(XIoChannelGroup_writeDone_signal),
                           (XObject*)dev, xvfdOnWriteDone, XConnectionType_Direct);
    if (!c1 || !c2) {
        if (c1) {
            XObject_disconnect_1((XObject*)group, XSignal(XIoChannelGroup_inputChanged_signal),
                                 (XObject*)dev, xvfdOnInputChanged);
        }
        if (c2) {
            XObject_disconnect_1((XObject*)group, XSignal(XIoChannelGroup_writeDone_signal),
                                 (XObject*)dev, xvfdOnWriteDone);
        }
        dev->m_group = NULL;
        return false;
    }
    xvfdStatusTimerStart(dev);
    return true;
}

void XVfdDevice_detach(XVfdDevice* dev)
{
    if (!dev) return;
    if (dev->m_group) {
        XObject_disconnect_1((XObject*)dev->m_group, XSignal(XIoChannelGroup_inputChanged_signal),
                             (XObject*)dev, xvfdOnInputChanged);
        XObject_disconnect_1((XObject*)dev->m_group, XSignal(XIoChannelGroup_writeDone_signal),
                             (XObject*)dev, xvfdOnWriteDone);
        dev->m_group = NULL;
    }
    if (dev->m_statusTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)dev, dev->m_statusTimer);
        dev->m_statusTimer = XTIMER_INVALID_ID;
    }
    XVfdDevice_stopActionTimer(dev);
    memset(dev->m_slots, 0, sizeof(dev->m_slots));
    memset(dev->m_paramBinds, 0, sizeof(dev->m_paramBinds));
    dev->m_paramBindCount = 0;
    dev->m_busy = false;
    dev->m_op = XVfdOperation_None;
}

bool XVfdDevice_isAttached(const XVfdDevice* dev)
{
    return dev ? (dev->m_group != NULL) : false;
}

XIoChannelGroup* XVfdDevice_channelGroup(const XVfdDevice* dev)
{
    return dev ? dev->m_group : NULL;
}

bool XVfdDevice_bindPoint(XVfdDevice* dev, const XVfdPointSpec* spec, XIoPoint* point)
{
    int32_t seed = 0;
    if (!dev || !spec || !point) return false;
    if (!dev->m_group) {
        dev->m_lastError = XVfdError_NotAttached;
        return false;
    }
    if (!xvfdRoleValid((int)spec->m_role)) return false;
    if (!XIoChannelGroup_contains(dev->m_group, point)) {
        dev->m_lastError = XVfdError_NotAttached;
        return false;
    }
    dev->m_slots[spec->m_role].m_spec = spec;
    dev->m_slots[spec->m_role].m_point = point;
    dev->m_caps |= spec->m_caps;
    /* 播种：点若已有数据（数据源已首拍）立即入快照，避免"绑定后等下一拍"。 */
    if (xvfdReadPoint(point, &seed)) {
        XClassGetVirtualFunc(dev, EXVfdDevice_DecodePoint,
            void (*)(XVfdDevice*, const XVfdPointSpec*, int32_t))(dev, spec, seed);
    }
    return true;
}

bool XVfdDevice_unbindPoint(XVfdDevice* dev, XVfdPointRole role)
{
    if (!dev || !xvfdRoleValid((int)role)) return false;
    if (!dev->m_slots[role].m_point) return false;
    dev->m_slots[role].m_spec = NULL;
    dev->m_slots[role].m_point = NULL;
    return true;
}

bool XVfdDevice_hasPoint(const XVfdDevice* dev, XVfdPointRole role)
{
    if (!dev || !xvfdRoleValid((int)role)) return false;
    return dev->m_slots[role].m_point != NULL;
}

XIoPoint* XVfdDevice_point(const XVfdDevice* dev, XVfdPointRole role)
{
    if (!dev || !xvfdRoleValid((int)role)) return NULL;
    return dev->m_slots[role].m_point;
}

int XVfdDevice_boundCount(const XVfdDevice* dev)
{
    int i;
    int n = 0;
    if (!dev) return 0;
    for (i = 0; i < XVFD_ROLE_CAP; ++i) {
        if (dev->m_slots[i].m_point) ++n;
    }
    return n;
}

bool XVfdDevice_bindParamPoint(XVfdDevice* dev, uint16_t paramId, XIoPoint* point)
{
    XVfdParamBinding* b;
    if (!dev || !point) return false;
    if (!dev->m_group || !XIoChannelGroup_contains(dev->m_group, point)) {
        dev->m_lastError = XVfdError_NotAttached;
        return false;
    }
    b = xvfdParamBinding(dev, paramId);
    if (b) {
        b->m_point = point;
        return true;
    }
    if (dev->m_paramBindCount >= XVFD_PARAM_BIND_CAP) {
        dev->m_lastError = XVfdError_ParamRejected;
        return false;
    }
    dev->m_paramBinds[dev->m_paramBindCount].m_id = paramId;
    dev->m_paramBinds[dev->m_paramBindCount].m_point = point;
    ++dev->m_paramBindCount;
    return true;
}

XIoPoint* XVfdDevice_paramPoint(const XVfdDevice* dev, uint16_t paramId)
{
    XVfdParamBinding* b = xvfdParamBinding((XVfdDevice*)dev, paramId);
    return b ? b->m_point : NULL;
}

/* ==================== 状态与配置 ==================== */

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

void XVfdDevice_setStatusInterval(XVfdDevice* dev, uint32_t intervalMs)
{
    if (!dev) return;
    dev->m_statusIntervalMs = (intervalMs == 0) ? XVFD_DEFAULT_STATUS_INTERVAL_MS : intervalMs;
    if (dev->m_group) xvfdStatusTimerStart(dev);
}

uint32_t XVfdDevice_statusInterval(const XVfdDevice* dev)
{
    return dev ? dev->m_statusIntervalMs : 0;
}

void XVfdDevice_setActionTimeout(XVfdDevice* dev, uint32_t timeoutMs)
{
    if (!dev) return;
    dev->m_actionTimeoutMs = (timeoutMs == 0) ? XVFD_DEFAULT_ACTION_TIMEOUT_MS : timeoutMs;
}

uint32_t XVfdDevice_actionTimeout(const XVfdDevice* dev)
{
    return dev ? dev->m_actionTimeoutMs : 0;
}

/* ==================== 参数通道（表 + 读写基类实现） ==================== */

void XVfdDevice_setParamTable(XVfdDevice* dev, const XVfdParamDesc* table, int count)
{
    if (!dev) return;
    dev->m_params = table;
    dev->m_paramCount = (table && count > 0) ? count : 0;
}

int XVfdDevice_paramCount(const XVfdDevice* dev)
{
    return dev ? dev->m_paramCount : 0;
}

const XVfdParamDesc* XVfdDevice_paramDesc_const(const XVfdDevice* dev, uint16_t paramId)
{
    if (!dev || !dev->m_params || (int)paramId >= dev->m_paramCount) return NULL;
    return &dev->m_params[paramId];
}

bool XVfdDevice_readParam_base(XVfdDevice* dev, uint16_t paramId)
{
    XVfdParamBinding* b;
    int32_t raw = 0;
    if (!dev) return false;
    b = xvfdParamBinding(dev, paramId);
    if (!b) {
        dev->m_lastError = XVfdError_ParamRejected;
        XVfdDevice_emitError(dev, "参数点未绑定：请先 bindParamPoint");
        return false;
    }
    if (!xvfdReadPoint(b->m_point, &raw)) {
        dev->m_lastError = XVfdError_NoData;
        XVfdDevice_paramReadDone_signal(dev, paramId, 0, false);
        return true;
    }
    dev->m_lastError = XVfdError_OK;
    XVfdDevice_paramReadDone_signal(dev, paramId, raw, true);
    return true;
}

bool XVfdDevice_writeParam_base(XVfdDevice* dev, uint16_t paramId, int32_t raw)
{
    XVfdParamBinding* b;
    const XVfdParamDesc* desc;
    if (!dev) return false;
    b = xvfdParamBinding(dev, paramId);
    if (!b) {
        dev->m_lastError = XVfdError_ParamRejected;
        XVfdDevice_emitError(dev, "参数点未绑定：请先 bindParamPoint");
        return false;
    }
    desc = XVfdDevice_paramDesc_const(dev, paramId);
    if (desc) {
        if (desc->m_access == (uint8_t)XVfdParamAccess_Factory ||
            desc->m_access == (uint8_t)XVfdParamAccess_ReadOnly) {
            dev->m_lastError = XVfdError_ParamRunLocked;
            XVfdDevice_emitError(dev, "参数只读或厂家参数：拒绝写入");
            return false;
        }
        if (desc->m_access == (uint8_t)XVfdParamAccess_RunLocked && dev->m_status.m_running) {
            dev->m_lastError = XVfdError_ParamRunLocked;
            XVfdDevice_emitError(dev, "参数运行禁改（★）：请停机后再写");
            return false;
        }
    }
    if (!XVfdDevice_beginAction(dev, XVfdOperation_WriteParam)) return false;
    dev->m_opParamId = paramId;
    if (!xvfdWritePoint(dev, b->m_point, raw)) {
        dev->m_lastError = XVfdError_WriteRejected;
        XVfdDevice_finishAction(dev, false);
        return false;
    }
    XVfdDevice_armActionTimer(dev, dev->m_actionTimeoutMs);
    return true;
}

/* ==================== 动作 *_base 分派入口 ==================== */

/* 说明：各 *_base 入口只做虚表分派，业务实现全在品牌子类的 VXxx 槽中。 */

bool XVfdDevice_start(XVfdDevice* dev, bool reverse)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_Start,
        bool (*)(XVfdDevice*, bool))(dev, reverse);
}

bool XVfdDevice_stop(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_Stop, bool (*)(XVfdDevice*))(dev);
}

bool XVfdDevice_freeStop(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_FreeStop, bool (*)(XVfdDevice*))(dev);
}

bool XVfdDevice_quickStop(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_QuickStop, bool (*)(XVfdDevice*))(dev);
}

bool XVfdDevice_jog(XVfdDevice* dev, bool reverse)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_Jog,
        bool (*)(XVfdDevice*, bool))(dev, reverse);
}

bool XVfdDevice_setFrequency(XVfdDevice* dev, double hz)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetFrequency,
        bool (*)(XVfdDevice*, double))(dev, hz);
}

bool XVfdDevice_setSpeedRpm(XVfdDevice* dev, int rpm)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetSpeedRpm,
        bool (*)(XVfdDevice*, int))(dev, rpm);
}

bool XVfdDevice_setAccelDecel(XVfdDevice* dev, int index, double accelS, double decelS)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetAccelDecel,
        bool (*)(XVfdDevice*, int, double, double))(dev, index, accelS, decelS);
}

bool XVfdDevice_selectPresetSpeed(XVfdDevice* dev, int index)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SelectPresetSpeed,
        bool (*)(XVfdDevice*, int))(dev, index);
}

bool XVfdDevice_setPresetSpeedHz(XVfdDevice* dev, int index, double hz)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetPresetSpeedHz,
        bool (*)(XVfdDevice*, int, double))(dev, index, hz);
}

bool XVfdDevice_setPidTarget(XVfdDevice* dev, double value)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetPidTarget,
        bool (*)(XVfdDevice*, double))(dev, value);
}

bool XVfdDevice_setCommandSource(XVfdDevice* dev, int source)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetCommandSource,
        bool (*)(XVfdDevice*, int))(dev, source);
}

bool XVfdDevice_setFrequencySource(XVfdDevice* dev, int source)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetFrequencySource,
        bool (*)(XVfdDevice*, int))(dev, source);
}

bool XVfdDevice_resetFault(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_ResetFault, bool (*)(XVfdDevice*))(dev);
}

bool XVfdDevice_setDigitalOutput(XVfdDevice* dev, int index, bool on)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetDigitalOutput,
        bool (*)(XVfdDevice*, int, bool))(dev, index, on);
}

bool XVfdDevice_startMotorTuning(XVfdDevice* dev, int mode)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_StartMotorTuning,
        bool (*)(XVfdDevice*, int))(dev, mode);
}

bool XVfdDevice_factoryReset(XVfdDevice* dev, int scope)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_FactoryReset,
        bool (*)(XVfdDevice*, int))(dev, scope);
}

bool XVfdDevice_unlockPassword(XVfdDevice* dev, uint32_t password)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_UnlockPassword,
        bool (*)(XVfdDevice*, uint32_t))(dev, password);
}

bool XVfdDevice_saveParameters(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_SaveParameters, bool (*)(XVfdDevice*))(dev);
}

bool XVfdDevice_readParam(XVfdDevice* dev, uint16_t paramId)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_ReadParam,
        bool (*)(XVfdDevice*, uint16_t))(dev, paramId);
}

bool XVfdDevice_writeParam(XVfdDevice* dev, uint16_t paramId, int32_t raw)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_WriteParam,
        bool (*)(XVfdDevice*, uint16_t, int32_t))(dev, paramId, raw);
}

bool XVfdDevice_readNameplate(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_ReadNameplate, bool (*)(XVfdDevice*))(dev);
}

bool XVfdDevice_readFaultHistory(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_ReadFaultHistory, bool (*)(XVfdDevice*))(dev);
}

/* ==================== 子类内部辅助（Protected） ==================== */

XVfdStatus* XVfdDevice_status(XVfdDevice* dev)
{
    return dev ? &dev->m_status : NULL;
}

void XVfdDevice_markDirty(XVfdDevice* dev)
{
    if (dev) dev->m_dirty = true;
}

void XVfdDevice_emitStatus(XVfdDevice* dev)
{
    if (!dev) return;
    dev->m_status.m_valid = true;
    dev->m_haveStatus = true;
    dev->m_status.m_timestampMs = XDateTime_currentMSecsSinceEpoch();
    dev->m_dirty = false;
    XVfdDevice_statusUpdated_signal(dev);
}

void XVfdDevice_emitDone(XVfdDevice* dev, XVfdOperation op, bool ok)
{
    uint16_t paramId;
    if (!dev) return;
    paramId = dev->m_opParamId;
    dev->m_busy = false;
    dev->m_op = XVfdOperation_None;
    if (op == XVfdOperation_WriteParam) {
        XVfdDevice_paramWriteDone_signal(dev, paramId, ok);
        return;
    }
    if (op == XVfdOperation_ReadParam) {
        XVfdDevice_paramReadDone_signal(dev, paramId, 0, ok);
        return;
    }
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

bool XVfdDevice_readRole(XVfdDevice* dev, XVfdPointRole role, int32_t* outValue)
{
    XVfdSlot* slot = xvfdSlot(dev, (int)role);
    if (!slot) {
        if (dev) dev->m_lastError = XVfdError_NotAttached;
        return false;
    }
    if (!xvfdReadPoint(slot->m_point, outValue)) {
        if (dev) dev->m_lastError = XVfdError_NoData;
        return false;
    }
    return true;
}

bool XVfdDevice_writeRole(XVfdDevice* dev, XVfdPointRole role, int32_t nativeValue)
{
    XVfdSlot* slot = xvfdSlot(dev, (int)role);
    if (!slot) {
        if (dev) dev->m_lastError = XVfdError_NotAttached;
        return false;
    }
    if (!xvfdWritePoint(dev, slot->m_point, nativeValue)) {
        if (dev) dev->m_lastError = XVfdError_WriteRejected;
        return false;
    }
    return true;
}

bool XVfdDevice_beginAction(XVfdDevice* dev, XVfdOperation op)
{
    if (!dev) return false;
    if (!dev->m_group) {
        dev->m_lastError = XVfdError_NotAttached;
        return false;
    }
    if (dev->m_busy) {
        dev->m_lastError = XVfdError_Busy;
        return false;
    }
    dev->m_busy = true;
    dev->m_op = op;
    dev->m_opParamId = 0;
    dev->m_lastError = XVfdError_OK;
    return true;
}

void XVfdDevice_finishAction(XVfdDevice* dev, bool ok)
{
    XVfdOperation op;
    if (!dev || !dev->m_busy) return;
    op = dev->m_op;
    XVfdDevice_stopActionTimer(dev);
    if (ok) dev->m_lastError = XVfdError_OK;
    else if (dev->m_lastError == XVfdError_OK) dev->m_lastError = XVfdError_WriteFailed;
    XVfdDevice_emitDone(dev, op, ok);
}

XVfdOperation XVfdDevice_currentOp(const XVfdDevice* dev)
{
    return dev ? dev->m_op : XVfdOperation_None;
}

uint16_t XVfdDevice_currentParamId(const XVfdDevice* dev)
{
    return dev ? dev->m_opParamId : 0;
}

void XVfdDevice_armActionTimer(XVfdDevice* dev, uint32_t delayMs)
{
    if (!dev) return;
    XVfdDevice_stopActionTimer(dev);
    if (delayMs == 0) delayMs = 1;
    dev->m_actionTimer = XObject_startTimer_ms((XObject*)dev, (uint64_t)delayMs,
                                               XTimerType_CoarseTimer);
}

void XVfdDevice_stopActionTimer(XVfdDevice* dev)
{
    if (!dev) return;
    if (dev->m_actionTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)dev, dev->m_actionTimer);
        dev->m_actionTimer = XTIMER_INVALID_ID;
    }
}

bool XVfdDevice_actionWrite(XVfdDevice* dev, XVfdOperation op,
                            XVfdPointRole role, int32_t nativeValue)
{
    if (!dev) return false;
    if (!XVfdDevice_beginAction(dev, op)) return false;
    if (!XVfdDevice_writeRole(dev, role, nativeValue)) {
        XVfdDevice_finishAction(dev, false);
        return false;
    }
    XVfdDevice_armActionTimer(dev, dev->m_actionTimeoutMs);
    return true;
}

double XVfdDevice_rawToEngineering(int32_t raw, int32_t div)
{
    int32_t d = (div == 0) ? 1 : div;
    return (double)raw / (double)d;
}

int32_t XVfdDevice_engineeringToRaw(double value, int32_t div, int32_t minRaw, int32_t maxRaw)
{
    double d = (double)((div == 0) ? 1 : div);
    double v = value * d;
    int32_t raw;
    if (!(v == v)) return minRaw; /* NaN 防护 */
    if (v >= (double)maxRaw) return maxRaw;
    if (v <= (double)minRaw) return minRaw;
    raw = (int32_t)((v >= 0.0) ? (v + 0.5) : (v - 0.5));
    if (raw > maxRaw) raw = maxRaw;
    if (raw < minRaw) raw = minRaw;
    return raw;
}

const XVfdFaultEntry* XVfdDevice_faultEntry_const(const XVfdFaultEntry* table, int count,
                                                  uint16_t code)
{
    int i;
    if (!table || count <= 0) return NULL;
    for (i = 0; i < count; ++i) {
        if (table[i].m_code == code) return &table[i];
    }
    return NULL;
}

void XVfdDevice_decodePoint_base(XVfdDevice* dev, const XVfdPointSpec* spec, int32_t raw)
{
    XVfdStatus* st;
    double eng;
    if (!dev || !spec) return;
    st = &dev->m_status;
    eng = XVfdDevice_rawToEngineering(raw, spec->m_div);
    switch (spec->m_role) {
    case XVfdRole_StatusWord:
        st->m_rawStateWord = (uint16_t)raw;
        st->m_validMask |= XVfdField_RawStateWord;
        break;
    case XVfdRole_FaultCode:
        st->m_faultCode = (uint16_t)raw;
        st->m_validMask |= XVfdField_FaultCode;
        break;
    case XVfdRole_OutputFreq:
        st->m_outputFreqHz = eng;
        st->m_validMask |= XVfdField_OutputFreqHz;
        break;
    case XVfdRole_SetFreq:
        st->m_setFreqHz = eng;
        st->m_validMask |= XVfdField_SetFreqHz;
        break;
    case XVfdRole_OutputCurrent:
        st->m_outputCurrentA = eng;
        st->m_validMask |= XVfdField_OutputCurrent;
        break;
    case XVfdRole_OutputVoltage:
        st->m_outputVoltageV = eng;
        st->m_validMask |= XVfdField_OutputVoltage;
        break;
    case XVfdRole_BusVoltage:
        st->m_busVoltageV = eng;
        st->m_validMask |= XVfdField_BusVoltage;
        break;
    case XVfdRole_MainsVoltage:
        st->m_mainsVoltageV = eng;
        st->m_validMask |= XVfdField_MainsVoltage;
        break;
    case XVfdRole_OutputPower:
        st->m_outputPowerKw = eng;
        st->m_validMask |= XVfdField_OutputPowerKw;
        break;
    case XVfdRole_OutputPowerPercent:
        st->m_outputPowerPercent = eng;
        st->m_validMask |= XVfdField_OutputPowerPercent;
        break;
    case XVfdRole_Torque:
        st->m_torquePercent = eng;
        st->m_validMask |= XVfdField_TorquePercent;
        break;
    case XVfdRole_SpeedRpm:
        st->m_speedRpm = eng;
        st->m_validMask |= XVfdField_SpeedRpm;
        break;
    case XVfdRole_DriveTemp:
        st->m_driveTempC = eng;
        st->m_validMask |= XVfdField_DriveTempC;
        break;
    case XVfdRole_MotorTemp:
        st->m_motorTempC = eng;
        st->m_validMask |= XVfdField_MotorTempC;
        break;
    case XVfdRole_DriveThermal:
        st->m_driveThermalPercent = eng;
        st->m_validMask |= XVfdField_DriveThermalPercent;
        break;
    case XVfdRole_MotorThermal:
        st->m_motorThermalPercent = eng;
        st->m_validMask |= XVfdField_MotorThermalPercent;
        break;
    case XVfdRole_DiBits:
        st->m_diBits = (uint16_t)raw;
        st->m_validMask |= XVfdField_DiBits;
        break;
    case XVfdRole_DoBits:
        st->m_doBits = (uint16_t)raw;
        st->m_validMask |= XVfdField_DoBits;
        break;
    case XVfdRole_Ai1:
        st->m_ai1 = eng;
        st->m_validMask |= XVfdField_Ai1;
        break;
    case XVfdRole_Ai2:
        st->m_ai2 = eng;
        st->m_validMask |= XVfdField_Ai2;
        break;
    case XVfdRole_Ai3:
        st->m_ai3 = eng;
        st->m_validMask |= XVfdField_Ai3;
        break;
    case XVfdRole_PidSetpoint:
        st->m_pidSetpoint = eng;
        st->m_validMask |= XVfdField_PidSetpoint;
        break;
    case XVfdRole_PidFeedback:
        st->m_pidFeedback = eng;
        st->m_validMask |= XVfdField_PidFeedback;
        break;
    case XVfdRole_Counter:
        st->m_counterValue = raw;
        st->m_validMask |= XVfdField_CounterValue;
        break;
    case XVfdRole_Length:
        st->m_lengthValue = eng;
        st->m_validMask |= XVfdField_LengthValue;
        break;
    case XVfdRole_RunHours:
        st->m_runHoursTotal = eng;
        st->m_validMask |= XVfdField_RunHours;
        break;
    case XVfdRole_PowerOnHours:
        st->m_powerOnHoursTotal = eng;
        st->m_validMask |= XVfdField_PowerOnHours;
        break;
    case XVfdRole_Energy:
        st->m_energyKwhTotal = eng;
        st->m_validMask |= XVfdField_EnergyKwh;
        break;
    default:
        return; /* 写角色/辅助字/换算基准：不进快照，由品牌自行读取。 */
    }
    dev->m_dirty = true;
}

/* ==================== 定时器事件 ==================== */

/**
 * @brief 定时器事件：快照发布到期 / 动作监督到期分派。
 * @param obj 本设备对象。
 * @param event 定时器事件。
 * @return 无。
 */
static void VXVfdDevice_timerEvent(XObject* obj, XTimerEvent* event)
{
    XVfdDevice* dev = (XVfdDevice*)obj;
    XTimerId id;
    if (!obj || !event) return;
    id = XTimerEvent_timerId(event);
    if (dev->m_statusTimer != XTIMER_INVALID_ID && id == dev->m_statusTimer) {
        XEvent_accept((XEvent*)event);
        dev->m_statusTimer = XTIMER_INVALID_ID;
        if (dev->m_dirty) XVfdDevice_emitStatus(dev);
        if (dev->m_group) xvfdStatusTimerStart(dev);
        return;
    }
    if (dev->m_actionTimer != XTIMER_INVALID_ID && id == dev->m_actionTimer) {
        XEvent_accept((XEvent*)event);
        dev->m_actionTimer = XTIMER_INVALID_ID;
        XClassGetVirtualFunc(dev, EXVfdDevice_ActionTimerExpired,
            void (*)(XVfdDevice*))(dev);
        return;
    }
    /* 未知定时器 id 交父类链处理：第一参=直接父类（XClass_Parent 语义）。 */
    XClass_Parent(XObject, EXObject_TimerEvent, void (*)(XObject*, XTimerEvent*))(obj, event);
}

/* ==================== 信号 ==================== */

void* XVfdDevice_statusUpdated_signal(XVfdDevice* dev)
{
    if (dev) {
        const XVfdStatus* snap = &dev->m_status;
        XVarList* args = XVarList_Create(XVar(const XVfdStatus*, snap));
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
        XObject_emitSignal((XObject*)dev, (size_t)XVfdDevice_nameplateUpdated_signal,
                           NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
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

#endif /* XVFD_DEVICE_ON */
#endif /* XVFD_ON */
