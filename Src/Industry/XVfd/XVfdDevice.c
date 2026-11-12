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
#include <stdlib.h>   /* getenv：XHSF_VFD_TRACE 诊断门控 */

/** @brief 快照发布周期缺省值（毫秒）。 */
#define XVFD_DEFAULT_STATUS_INTERVAL_MS 200u
/** @brief 写落定监督超时缺省值（毫秒）。 */
#define XVFD_DEFAULT_ACTION_TIMEOUT_MS 2000u
/**
 * @brief 电机额定转速缺省值（rpm；最大频率处的转速）。
 * @details 1440rpm@50Hz 是 4 极异步电机的额定点（同步 1500rpm 减去额定转差），
 *          现场绝大多数风机/泵都落在这个点附近。品牌 init 可覆盖（如从
 *          P1.05 读得后 setRatedRpm）。0 = 未配置（换算时回落本值）。
 */
#define XVFD_DEFAULT_RATED_RPM 1440.0

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
 * @brief 取语义槽（角色 → 槽；O(1) 数组下标）。
 * @param dev 设备实例；不能为 NULL。
 * @param role 角色。
 * @return 槽指针；未绑定返回 NULL。
 * @note [2026-11-02 零遍历] 旧实现另有 xvfdSlotByPoint 按点指针线性扫 68 槽
 *       （每次数据到达都扫一遍）。现在组信号直接携带 role，本函数一步到位，
 *       那个扫描函数已删除。
 */
static XVfdSlot* xvfdSlot(XVfdDevice* dev, int role)
{
    if (!dev || !xvfdRoleValid(role)) return NULL;
    if (!dev->m_slots[role].m_point) return NULL;
    return &dev->m_slots[role];
}

/**
 * @brief 写一个点（按点类型选组写 API）。
 * @param dev 设备实例；不能为 NULL。
 * @param point 目标点；不能为 NULL。
 * @param value 点单位值。
 * @return 通道受理返回 true。
 * @note [2026-11-02] 组写入口已改 role 口径：点指针转 role 后下发。
 */
static bool xvfdWritePoint(XVfdDevice* dev, XIoPoint* point, int32_t value)
{
    uint16_t role;
    if (!dev || !dev->m_group || !point) return false;
    role = XIoPoint_role(point);
    if (XIoPoint_type(point) == XIoPoint_AnalogOutput) {
        return XIoChannelGroup_writeAnalogOutput(dev->m_group, role, value);
    }
    if (XIoPoint_type(point) == XIoPoint_DigitalOutput) {
        return XIoChannelGroup_writeDigitalOutput(dev->m_group, role, value != 0);
    }
    return false;
}

/**
 * @brief 读一个点（读本地缓存，不发起传输）。
 * @param point 目标点；不能为 NULL。
 * @param outValue 输出点单位值；不能为 NULL。
 * @return 取到有效值返回 true。
 * @note [2026-11-02] 点不再有 ReadRaw 虚槽，读的是组喂进来的缓存值。
 */
static bool xvfdReadPoint(XIoChannelGroup* group, XIoPoint* point, int32_t* outValue)
{
    /* 优先读组内缓存（链路推过来的已标度工程值）。
     * 缓存无值时（首拍前/刚绑定）回落到链路当前值，与旧实现
     * 「直接读 Hub 缓存」一致：部分业务判定（如命令源未配置时拒绝 start）
     * 依赖「首拍已回、值为默认 0」而非「完全无数据」。 */
    if (XIoPoint_value(point, outValue)) return true;
    if (!point || !group) return false;
    {
        XIoLink* link = XIoChannelGroup_link(group);
        int32_t raw = 0;
        if (!link) return false;
        if (!XIoLink_readRole_base(link, XIoPoint_role(point), &raw)) return false;
        *outValue = XIoPoint_toEngineering(point, raw);
        return true;
    }
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
    dev->m_statusTimer = XObject_startTimer((XObject*)dev,
        (uint64_t)dev->m_statusIntervalMs, XTimerType_CoarseTimer);
}

/* ==================== 数据进：组信号槽 ==================== */

/**
 * @brief 组 inputChanged 槽：role 直达语义槽 → DecodePoint 虚槽分派。
 * @param receiver 连接的接收者（即本设备）；**不是**组，禁止按组强转。
 * @param args 信号参数（uint16_t role, int32_t value）。
 * @return 无。
 * @note [2026-11-02 零遍历] 信号带 role，直接 m_slots[role] 取址；
 *       旧实现带点指针再线性扫 68 槽匹配，已删除。
 */
static void xvfdOnInputChanged(XObject* receiver, XVarList* args)
{
    XVfdDevice* dev = (XVfdDevice*)receiver;
    XVfdSlot* slot;
    uint16_t role;
    int32_t value;
    if (!dev || !args) return;
    XVarList_args_2(args, uint16_t, roleArg, int32_t, valueArg);
    role = roleArg;
    value = valueArg;
    if (!xvfdRoleValid((int)role)) return;
    slot = &dev->m_slots[role];
    if (!slot->m_point) return;
    XClassGetVirtualFunc(dev, EXVfdDevice_DecodePoint,
        void (*)(XVfdDevice*, const XVfdPointSpec*, int32_t))(dev, slot->m_spec, value);
}

/* 前向声明：参数写落定信号定义在本文件后段（信号区）。 */
void* XVfdDevice_paramWriteDone_signal(XVfdDevice* dev, uint16_t paramId, bool ok);

/**
 * @brief 组 writeDone 槽：动作在途时交 ActionWriteDone 虚槽分派。
 * @param receiver 连接的接收者（即本设备）；**不是**组。
 * @param args 信号参数（uint16_t role, bool ok）。
 * @return 无。
 * @note 无动作在途的 writeDone（例如业务层直接写点）静默丢弃。
 */
static void xvfdOnWriteDone(XObject* receiver, XVarList* args)
{
    XVfdDevice* dev = (XVfdDevice*)receiver;
    XIoPoint* point;
    uint16_t role;
    bool ok;
    if (!dev || !args) return;
    XVarList_args_2(args, uint16_t, roleArg, bool, okArg);
    /* [诊断 XHSF_VFD_TRACE] 写落定到达（动作在途与否都打）：现场"每个动作
     * 都要等满 8s 动作超时才放开"的唯一分水岭就是这条有没有到。 */
    {
        const char* tr = getenv("XHSF_VFD_TRACE");
        if (tr && tr[0] && tr[0] != '0')
            XPrintf("[vfd] writeDone role=%u ok=%d busy=%d\n",
                    (unsigned)roleArg, (int)okArg, (int)dev->m_busy);
    }
    if (!dev->m_busy) return;
    role = roleArg;
    ok = okArg;
    /* [2026-11-02 参数点收口] 参数点的 role 是 128+paramId，不属于语义角色域
     * （<XVfdRole_Count）。若在在途动作是参数写，直接走参数写落定收口，不经语义槽。 */
    if (!xvfdRoleValid((int)role)) {
        if (dev->m_op == XVfdOperation_WriteParam) {
            XVfdDevice_finishAction(dev, ok);
            XVfdDevice_paramWriteDone_signal(dev, dev->m_opParamId, ok);
        }
        return;
    }
    point = dev->m_slots[role].m_point;
    if (!point) return;
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
/**
 * @brief 缺省「带给定启动/点动」：先写给定、再发命令（尽力而为）。
 * @details 仅适用于**没有设备级单飞闸冲突**的传输场景。M512 已重载为多阶段
 *          单动作（见 XVfdM512.c），因为分两次调用会被单飞闸自拒。
 */
static bool VXVfdDevice_StartWithGiven(XVfdDevice* dev, XVfdOperation op,
                                       bool reverse, double hz)
{
    if (!dev) return false;
    if (op != XVfdOperation_Start && op != XVfdOperation_Jog) {
        dev->m_lastError = XVfdError_InvalidParam;
        return false;
    }
    if (hz > 0.0 && !XVfdDevice_setFrequency(dev, hz)) return false;
    return (op == XVfdOperation_Start) ? XVfdDevice_start(dev, reverse)
                                       : XVfdDevice_jog(dev, reverse);
}

bool XVfdDevice_startWithGiven(XVfdDevice* dev, XVfdOperation op, bool reverse,
                               double hz)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable"))
        return false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_StartWithGiven,
        bool (*)(XVfdDevice*, XVfdOperation, bool, double))(dev, op, reverse, hz);
}

/**
 * @brief 缺省「按转速调速」：rpm → Hz 后转调本类的 SetFrequency。
 * @param dev 设备。
 * @param rpm 目标转速（负值=反转方向给定，与频率给定负值同语义）。
 * @return 受理返回 true；无频率通道（SetFrequency 未实现）返回 false。
 * @details [2026-11-05 转速给定通用化] 多数品牌只有一个频率给定寄存器
 *          （M512 的 1000H 百分比），没有独立转速给定。与其每个品牌各写
 *          一遍乘法，不如在基类按
 *              hz = rpm / ratedRpm × maxFreqHz
 *          换算后交给子类的 SetFrequency——子类的源校验（如"主频率源必须
 *          是通讯给定"）、标度、写序全部复用，不会被绕过。
 *          有原生转速通道的品牌（ATV61）自行覆盖本虚槽。
 */
static bool VXVfdDevice_SetSpeedRpm(XVfdDevice* dev, int rpm)
{
    double rated;
    double base;
    double hz;
    if (!dev) return false;
    rated = (dev->m_ratedRpm > 0.0) ? dev->m_ratedRpm : XVFD_DEFAULT_RATED_RPM;
    base = (dev->m_maxFreqHz > 0.0) ? dev->m_maxFreqHz : 50.0;
    hz = (double)rpm / rated * base;
    /* 转调子类频率通道：未实现时它自己会报 NotSupported（不静默吞掉）。 */
    return XClassGetVirtualFunc(dev, EXVfdDevice_SetFrequency,
        bool (*)(XVfdDevice*, double))(dev, hz);
}
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

/**
 * @brief 析构：释放参数点表（唯一堆成员）后走父类链。
 * @param dev 设备实例；可为 NULL。
 * @return 无。
 * @note [2026-11-02] 参数点表由 setParamTable 按 paramCount 分配，
 *       必须在这里回收，否则每次换参数表都泄漏一块。
 */
static void VXVfdDevice_deinit(XVfdDevice* dev)
{
    if (!dev) return;
    if (dev->m_paramPoints) {
        XMemory_free(dev->m_paramPoints, XCLASS_DEFAULT_MEMORY_TYPE);
        dev->m_paramPoints = NULL;
    }
    dev->m_paramCount = 0;
    dev->m_params = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)dev);
}

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
        VXVfdDevice_ReadNameplate, VXVfdDevice_ReadFaultHistory,
        VXVfdDevice_StartWithGiven
    };
    XVTABLE_INIT_DEFAULT(XVfdDevice)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXVfdDevice_deinit);
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
    dev->m_ratedRpm = XVFD_DEFAULT_RATED_RPM;   /* 转速给定换算基准 */
    dev->m_statusTimer = XTIMER_INVALID_ID;
    dev->m_actionTimer = XTIMER_INVALID_ID;
    dev->m_lastError = XVfdError_OK;
    dev->m_op = XVfdOperation_None;
    dev->m_controlEnabled = true;   /* 控制闸缺省使能（历史行为不变） */
}

/* ==================== 装配 ==================== */

bool XVfdDevice_attach(XVfdDevice* dev, XIoChannelGroup* group)
{
    XConnection* c1;
    XConnection* c2;
    if (!dev || !group) return false;
    if (dev->m_group) XVfdDevice_detach(dev);
    dev->m_group = group;
    c1 = XObject_connect((XObject*)group, XSignal(XIoChannelGroup_inputChanged_signal),
                           (XObject*)dev, xvfdOnInputChanged, XConnectionType_Direct);
    c2 = XObject_connect((XObject*)group, XSignal(XIoChannelGroup_writeDone_signal),
                           (XObject*)dev, xvfdOnWriteDone, XConnectionType_Direct);
    if (!c1 || !c2) {
        if (c1) {
            XObject_disconnect((XObject*)group, XSignal(XIoChannelGroup_inputChanged_signal),
                                 (XObject*)dev, xvfdOnInputChanged);
        }
        if (c2) {
            XObject_disconnect((XObject*)group, XSignal(XIoChannelGroup_writeDone_signal),
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
        XObject_disconnect((XObject*)dev->m_group, XSignal(XIoChannelGroup_inputChanged_signal),
                             (XObject*)dev, xvfdOnInputChanged);
        XObject_disconnect((XObject*)dev->m_group, XSignal(XIoChannelGroup_writeDone_signal),
                             (XObject*)dev, xvfdOnWriteDone);
        dev->m_group = NULL;
    }
    if (dev->m_statusTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)dev, dev->m_statusTimer);
        dev->m_statusTimer = XTIMER_INVALID_ID;
    }
    XVfdDevice_stopActionTimer(dev);
    memset(dev->m_slots, 0, sizeof(dev->m_slots));
    if (dev->m_paramPoints) {
        memset(dev->m_paramPoints, 0, sizeof(XIoPoint*) * (size_t)dev->m_paramCount);
    }
    dev->m_boundCount = 0;
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
    /* [2026-11-02] 保留旧签名：内部委托给 bindSpec（规格字段内联进槽）。
     * 新代码应直接用 XVfdDevice_bindSpec / XVfdDevice_bindRole。 */
    return XVfdDevice_bindSpec(dev, spec, point);
}

bool XVfdDevice_bindRole(XVfdDevice* dev, uint16_t role, XIoPoint* point)
{
    int32_t seed = 0;
    if (!dev || !point) return false;
    if (!dev->m_group) {
        dev->m_lastError = XVfdError_NotAttached;
        return false;
    }
    if (!xvfdRoleValid((int)role)) return false;
    if (!XIoChannelGroup_contains(dev->m_group, point)) {
        dev->m_lastError = XVfdError_NotAttached;
        return false;
    }
    if (!dev->m_slots[role].m_point) ++dev->m_boundCount;  /* 计数器维护。 */
    dev->m_slots[role].m_point = point;
    /* [2026-11-02 零遍历] 高频字段内联进槽：解码路径不再经 spec 指针取值。 */
    dev->m_slots[role].m_div = 0;
    dev->m_slots[role].m_signed = XIoPoint_isSigned(point) ? 1u : 0u;
    /* 播种：点若已有数据（数据源已首拍）立即入快照，避免"绑定后等下一拍"。 */
    if (xvfdReadPoint(dev->m_group, point, &seed)) {
        XClassGetVirtualFunc(dev, EXVfdDevice_DecodePoint,
            void (*)(XVfdDevice*, const XVfdPointSpec*, int32_t))(
                dev, dev->m_slots[role].m_spec, seed);
    }
    return true;
}

bool XVfdDevice_bindSpec(XVfdDevice* dev, const XVfdPointSpec* spec, XIoPoint* point)
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
    if (!dev->m_slots[spec->m_role].m_point) ++dev->m_boundCount;
    dev->m_slots[spec->m_role].m_spec = spec;
    dev->m_slots[spec->m_role].m_point = point;
    /* 规格高频字段内联：解码路径零间接。 */
    dev->m_slots[spec->m_role].m_div = spec->m_div;
    dev->m_slots[spec->m_role].m_signed = spec->m_signed;
    dev->m_caps |= spec->m_caps;
    if (xvfdReadPoint(dev->m_group, point, &seed)) {
        XClassGetVirtualFunc(dev, EXVfdDevice_DecodePoint,
            void (*)(XVfdDevice*, const XVfdPointSpec*, int32_t))(dev, spec, seed);
    }
    return true;
}

const XVfdPointSpec* XVfdDevice_spec(const XVfdDevice* dev, XVfdPointRole role)
{
    if (!dev || !xvfdRoleValid((int)role)) return NULL;
    return dev->m_slots[role].m_spec;
}

bool XVfdDevice_unbindPoint(XVfdDevice* dev, XVfdPointRole role)
{
    if (!dev || !xvfdRoleValid((int)role)) return false;
    if (!dev->m_slots[role].m_point) return false;
    dev->m_slots[role].m_spec = NULL;
    dev->m_slots[role].m_point = NULL;
    dev->m_slots[role].m_div = 0;
    dev->m_slots[role].m_signed = 0;
    --dev->m_boundCount;
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
    /* [2026-11-02 零遍历] 改读 bind/unbind 维护的计数器，不再扫 96 槽。 */
    return dev ? dev->m_boundCount : 0;
}

bool XVfdDevice_bindParamPoint(XVfdDevice* dev, uint16_t paramId, XIoPoint* point)
{
    if (!dev || !point) return false;
    if (!dev->m_group || !XIoChannelGroup_contains(dev->m_group, point)) {
        dev->m_lastError = XVfdError_NotAttached;
        return false;
    }
    /* [2026-11-02 零遍历] paramId 是参数表连续下标，直接作数组下标；
     * 旧实现是「扫 m_paramBinds 找 id」。 */
    if (paramId >= (uint16_t)dev->m_paramCount) {
        dev->m_lastError = XVfdError_ParamRejected;
        return false;
    }
    /* [惰性分配] 首次绑定参数点时才分配下标表（见 setParamTable 注）。 */
    if (!dev->m_paramPoints) {
        size_t bytes = sizeof(XIoPoint*) * (size_t)dev->m_paramCount;
        dev->m_paramPoints = (XIoPoint**)XMemory_malloc(bytes,
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (!dev->m_paramPoints) {
            dev->m_lastError = XVfdError_ParamRejected;
            return false;
        }
        memset(dev->m_paramPoints, 0, bytes);
    }
    dev->m_paramPoints[paramId] = point;
    return true;
}

XIoPoint* XVfdDevice_paramPoint(const XVfdDevice* dev, uint16_t paramId)
{
    if (!dev || !dev->m_paramPoints) return NULL;
    if (paramId >= (uint16_t)dev->m_paramCount) return NULL;
    return dev->m_paramPoints[paramId];
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
    /* [2026-11-05 陈旧快照根修] m_valid / m_validMask 一旦置位就再没人清：
     * 链路断开（485 没接 / 变频器断电）后 m_status 仍保留最后一拍的字段与
     * 掩码，服务端据此持续发布"看起来有值"的遥测——实测客户端显示母线
     * 电压 553.2V、输出功率 0.0kW，而变频器根本没应答。
     *
     * 正确语义：快照只在"足够新"时有效。阈值取发布周期的 5 倍（最慢
     * 100ms 发布 → 500ms），既覆盖单拍抖动，又能在链路死掉后约 0.5s 内
     * 把 valid 翻成 false。**清掩码是必须的**：消费方（客户端页、服务端
     * 编码器）都按 mask 判"这个字段有没有值"，只翻 valid 不够。
     * 就地清理、不额外分配，返回值仍是对内部存储的借用（既有契约不变）。 */
    if (dev) {
        XVfdStatus* st = (XVfdStatus*)&dev->m_status;
        uint32_t interval = dev->m_statusIntervalMs ? dev->m_statusIntervalMs
                                                    : 200u;
        /* [阈值可由装配层覆盖 2026-11-10] 缺省「发布周期 × 5」假定数据刷新
         * 节拍 = 发布节拍；Modbus 总线是**轮转发读组**（一圈 = 组数 × 拍长，
         * 现场变频器一圈 2.8s），阈值短于一圈就会每隔几拍误判陈旧 ⇒ 客户端
         * 遥测在「---」与真值之间闪。装配层按真实轮转周期算好后经
         * XVfdDevice_setStaleMs 传进来（m_staleMs=0 表示沿用缺省）。 */
        uint64_t limitMs = dev->m_staleMs ? (uint64_t)dev->m_staleMs
                                          : (uint64_t)interval * 5u;
        uint64_t nowMs = XDateTime_currentMSecsSinceEpoch();
        if (st->m_valid && st->m_timestampMs != 0 &&
            nowMs > st->m_timestampMs &&
            (nowMs - st->m_timestampMs) > limitMs) {
            st->m_valid = false;
            st->m_validMask = 0;
        }
    }
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

void XVfdDevice_setControlEnabled(XVfdDevice* dev, bool enabled)
{
    if (!dev) return;
    dev->m_controlEnabled = enabled;
}

bool XVfdDevice_controlEnabled(const XVfdDevice* dev)
{
    /* 未初始化/野指针按「未使能」保守处理；正常 init 的实例恒有值。 */
    return dev ? dev->m_controlEnabled : false;
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

/* ==================== 快照陈旧阈值（随轮转周期联动） ==================== */

void XVfdDevice_setStaleMs(XVfdDevice* dev, uint32_t staleMs)
{
    if (!dev) return;
    dev->m_staleMs = staleMs;
    /* 阈值变了要立刻重判一次：否则旧阈值下已判失效的快照要等下一拍
     * 才恢复（现场：改采集周期后界面仍闪几秒「---」）。 */
    {
        XVfdStatus* st = (XVfdStatus*)&dev->m_status;
        if (st->m_valid && st->m_timestampMs != 0) {
            const uint64_t nowMs = XDateTime_currentMSecsSinceEpoch();
            const uint32_t interval = dev->m_statusIntervalMs
                                          ? dev->m_statusIntervalMs : 200u;
            const uint64_t limitMs = staleMs ? (uint64_t)staleMs
                                             : (uint64_t)interval * 5u;
            if (nowMs > st->m_timestampMs &&
                (nowMs - st->m_timestampMs) > limitMs) {
                st->m_valid = false;
                st->m_validMask = 0;
            }
        }
    }
}

uint32_t XVfdDevice_staleMs(const XVfdDevice* dev)
{
    if (!dev) return 0;
    if (dev->m_staleMs) return dev->m_staleMs;
    {
        const uint32_t interval = dev->m_statusIntervalMs
                                      ? dev->m_statusIntervalMs : 200u;
        return interval * 5u;
    }
}

/** @brief 阶段判定死区（Hz；与 Python VfdDevice.PHASE_DEADBAND_HZ 同值）。 */
#define XVFD_PHASE_DEADBAND_HZ  0.15

void XVfdDevice_setPhaseDerivation(XVfdDevice* dev, bool enable)
{
    if (!dev) return;
    dev->m_phaseDerive = enable;
}

void XVfdDevice_deriveRunPhase(XVfdDevice* dev)
{
    XVfdStatus* st;
    bool hasOut;
    bool hasSet;

    if (!dev) return;
    st = &dev->m_status;
    hasOut = (st->m_validMask & XVfdField_OutputFreqHz) != 0;
    hasSet = (st->m_validMask & XVfdField_SetFreqHz) != 0;

    if (st->m_faulted) {
        st->m_runState = XVfdRunState_Fault;
        return;
    }
    /* [已发停止命令 + 输出还没到 0 ⇒ 正在停机] 最可靠的判据，并补上
     * "首拍还没有历史值可比"的空档。真机停机时设定频率仍留旧给定，靠
     * 差值会判成"加速中"（实测停机全程显示加速中，完全反了）。 */
    if (dev->m_stopRequested) {
        if (hasOut && st->m_outputFreqHz > XVFD_PHASE_DEADBAND_HZ) {
            st->m_runState = XVfdRunState_Decelerating;
            /* [停机斜坡 ≠ 运行] 对齐 Python VfdDevice.field_value(bit2)：
               停机意图下即便状态字仍报"运行"（M512 虚拟模型在降到 0 前
               一直报运行），对外运行位必须报 False——客户端据此把
               「减速中」显示成「正在停机」，点动钮在停机斜坡一拍内恢复
               （停稳后点动恢复可用的时序判据就靠它），停机禁给定/运行
               禁点动两个服务端闸也同此口径。状态字每拍重解码，这里的
               覆盖不会泄漏到下一拍。 */
            st->m_running = false;
            return;
        }
        dev->m_stopRequested = false;   /* 停到位，退出停机窗口。 */
    }
    if (!st->m_running) {
        st->m_runState = (hasOut && st->m_outputFreqHz > XVFD_PHASE_DEADBAND_HZ)
                       ? XVfdRunState_Decelerating : XVfdRunState_Stopped;
        return;
    }
    /* [运行中换向减速] 命令方向 ≠ 状态字方向（状态字过零才翻）：输出在按
     * 减速时间降向 0，但设定频率镜像仍是给定——下面的差值判据会把这段
     * 误判成"加速中"（2026-11-06 换向口径实测，与 Python _derive_phase
     * 同款补丁）。过零翻向后两者一致，交回差值判据。 */
    if (dev->m_hasCmdReverse && dev->m_cmdReverse != st->m_reverse) {
        st->m_runState = XVfdRunState_Decelerating;
        return;
    }
    if (!hasOut || !hasSet) return;      /* 遥测不全：保留状态字结论。 */
    {
        const double delta = st->m_setFreqHz - st->m_outputFreqHz;
        if (delta > XVFD_PHASE_DEADBAND_HZ) {
            st->m_runState = XVfdRunState_Accelerating;
            return;
        }
        if (delta < -XVFD_PHASE_DEADBAND_HZ) {
            st->m_runState = XVfdRunState_Decelerating;
            return;
        }
    }
    st->m_runState = st->m_reverse ? XVfdRunState_RunningReverse
                                   : XVfdRunState_RunningForward;
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

/* ==================== 频率换算（绝对口径 / 最大频率百分比口径） ==================== */

double XVfdDevice_maxFrequency(const XVfdDevice* dev)
{
    return dev ? dev->m_maxFreqHz : 0.0;
}

void XVfdDevice_setMaxFrequency(XVfdDevice* dev, double hz)
{
    if (!dev || hz <= 0.0) return;
    dev->m_maxFreqHz = hz;
    dev->m_maxFreqKnown = true;
}

double XVfdDevice_ratedRpm(const XVfdDevice* dev)
{
    if (!dev) return 0.0;
    return (dev->m_ratedRpm > 0.0) ? dev->m_ratedRpm : XVFD_DEFAULT_RATED_RPM;
}

void XVfdDevice_setRatedRpm(XVfdDevice* dev, double rpm)
{
    if (!dev || !(rpm > 0.0)) return;
    dev->m_ratedRpm = rpm;
}

double XVfdDevice_rpmToHz(const XVfdDevice* dev, double rpm)
{
    const double base = (dev && dev->m_maxFreqHz > 0.0) ? dev->m_maxFreqHz : 50.0;
    const double rated = XVfdDevice_ratedRpm(dev);
    if (!(rated > 0.0)) return 0.0;
    return rpm / rated * base;
}

double XVfdDevice_hzToRpm(const XVfdDevice* dev, double hz)
{
    const double base = (dev && dev->m_maxFreqHz > 0.0) ? dev->m_maxFreqHz : 50.0;
    if (!(base > 0.0)) return 0.0;
    return hz / base * XVfdDevice_ratedRpm(dev);
}

int32_t XVfdDevice_frequencyToRaw(XVfdDevice* dev, XVfdFreqBasis basis, double hz,
                                  int32_t div, int32_t minRaw, int32_t maxRaw)
{
    double base;
    double percent;
    if (!dev) return 0;
    if (basis == XVfdFreqBasis_Absolute) {
        /* 绝对口径：原生码 = Hz ÷ 除率（除率由品牌点规格给出：0.1Hz 的品牌填 10）。 */
        return XVfdDevice_engineeringToRaw(hz, div, minRaw, maxRaw);
    }
    /* 相对口径：原生码表示「最大频率的百分数 × 100」。基准未知时 50Hz 兜底
     * （与 M512 历史行为一致：未读到 P0.10 时按 50Hz 算）。 */
    base = dev->m_maxFreqHz;
    if (!(base > 0.0)) base = 50.0;
    percent = hz / base * 100.0;
    return XVfdDevice_engineeringToRaw(percent, 100, minRaw, maxRaw);
}

double XVfdDevice_rawToFrequency(const XVfdDevice* dev, XVfdFreqBasis basis,
                                  int32_t raw, int32_t div)
{
    double base;
    if (!dev) return 0.0;
    if (basis == XVfdFreqBasis_Absolute) return XVfdDevice_rawToEngineering(raw, div);
    base = dev->m_maxFreqHz;
    if (!(base > 0.0)) base = 50.0;
    /* 原生码 = 百分数 × 100 → % = raw/100 → Hz = % × 基准/100 */
    return XVfdDevice_rawToEngineering(raw, div) / 100.0 * base;
}

/* ==================== 参数通道（表 + 读写基类实现） ==================== */

void XVfdDevice_setParamTable(XVfdDevice* dev, const XVfdParamDesc* table, int count)
{
    if (!dev) return;
    /* [2026-11-02 零遍历] 参数点表按 paramId 直接下标，随参数表规模一次性分配。
     * 旧实现是定长 m_paramBinds[64] + 线性查 id。
     * [惰性分配] 分配推迟到首次 bindParamPoint：部分品牌在 init 期就设参数表
     * （此时设备尚未挂接通道组），提前分配只是徒增一次堆交互，且掩盖了
     * 「表设了但没有点」这层语义。 */
    if (dev->m_paramPoints) {
        XMemory_free(dev->m_paramPoints, XCLASS_DEFAULT_MEMORY_TYPE);
        dev->m_paramPoints = NULL;
    }
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
    XIoPoint* pt;
    int32_t raw = 0;
    if (!dev) return false;
    pt = XVfdDevice_paramPoint(dev, paramId);   /* O(1) 下标直达 */
    if (!pt) {
        dev->m_lastError = XVfdError_ParamRejected;
        XVfdDevice_emitError(dev, "参数点未绑定：请先 bindParamPoint");
        return false;
    }
    if (!xvfdReadPoint(dev->m_group, pt, &raw)) {
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
    XIoPoint* pt;
    const XVfdParamDesc* desc;
    if (!dev) return false;
    pt = XVfdDevice_paramPoint(dev, paramId);   /* O(1) 下标直达 */
    if (!pt) {
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
    if (!xvfdWritePoint(dev, pt, raw)) {
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
    /* [重新启动 = 退出停机窗口] 对齐 Python VfdDevice.apply_control 的
     * OP_START 分支（self._stop_requested = False）。停机斜坡期用户又按启动
     * 时，输出频率会被 START 重新拉向给定——若不清 m_stopRequested，
     * deriveRunPhase 每拍都判"输出 > 死区 ⇒ 正在停机、强制 m_running=false"，
     * 而输出已升到给定、永远降不回 0.15Hz 以下，标志也就永远清不掉，
     * 运行位被死锁在 False（runState 恒 Decelerating、点动钮不置灰）。
     * START 是明确的启动意图，之前的停止意图到此作废。 */
    dev->m_stopRequested = false;
    /* [命令方向记账] 换向减速判据用：状态字过零才翻向，这段"命令方向 ≠
     * 状态字方向"的窗口就是换向减速（见 deriveRunPhase）。 */
    dev->m_hasCmdReverse = true;
    dev->m_cmdReverse = reverse;
    return XClassGetVirtualFunc(dev, EXVfdDevice_Start,
        bool (*)(XVfdDevice*, bool))(dev, reverse);
}

bool XVfdDevice_stop(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    /* [停止窗口标记] 停机后输出频率不会瞬间归零（真机斜坡下降，现场好几
     * 秒）。这段时间状态字可能还报"运行"、输出在往下掉——正是要看的
     * "正在停机"。这个窗口靠差值判不出来（真机 0x1000 留旧给定），只能
     * 靠"已发停止命令"标记。 */
    dev->m_stopRequested = true;
    dev->m_hasCmdReverse = false;   /* 停止覆盖换向：方向命令作废。 */
    return XClassGetVirtualFunc(dev, EXVfdDevice_Stop, bool (*)(XVfdDevice*))(dev);
}

bool XVfdDevice_freeStop(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    dev->m_hasCmdReverse = false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_FreeStop, bool (*)(XVfdDevice*))(dev);
}

bool XVfdDevice_quickStop(XVfdDevice* dev)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    dev->m_stopRequested = true;
    dev->m_hasCmdReverse = false;
    return XClassGetVirtualFunc(dev, EXVfdDevice_QuickStop, bool (*)(XVfdDevice*))(dev);
}

bool XVfdDevice_jog(XVfdDevice* dev, bool reverse)
{
    if (ISNULL(dev, "XVfdDevice") || ISNULL(XClassGetVtable(dev), "Vtable")) return false;
    dev->m_hasCmdReverse = true;
    dev->m_cmdReverse = reverse;
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
    /* [2026-11-05 空快照误判在线根修] 原先无条件置 m_valid=true。
     * 后果：从站根本不应答时快照仍被当成"有效" → 健康列表报
     * "变频器正常"、客户端显示一片 0.0 而非"--"（实测：vm=0 但
     * online=True，自相矛盾）。
     *
     * 判据取**遥测字段**的有效位，不能直接用 m_validMask 整体非零：
     * 掩码里还包含 RawStateWord 这类"非遥测"位（情上报上来的原始
     * 状态字），它们使掩码非零但遥测字段全空 → 快照编码为
     * 只剩 "vm":0 ，客户端一无所获。 */
    if ((dev->m_status.m_validMask & XVfdField_TelemetryMask) != 0) {
        dev->m_status.m_valid = true;
        dev->m_haveStatus = true;
        dev->m_status.m_timestampMs = XDateTime_currentMSecsSinceEpoch();
    } else {
        dev->m_status.m_valid = false;
    }
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

/**
 * @brief 只留存失败文本（不发射 errorOccurred 信号）。
 * @param dev 设备实例。
 * @param text 中文原因。
 * @note  给「不发射信号也要留痕」的失败闸用（如控制闸未使能、设备忙）——
 *        这些是**预期内**的拒绝，不必惊动日志监听者，但操作员仍要看到
 *        为什么被拒。
 */
static void xvfdKeepErrorText(XVfdDevice* dev, const char* text)
{
    if (!dev || !text || !text[0]) return;
    size_t n = strlen(text);
    if (n >= sizeof(dev->m_lastErrorText)) n = sizeof(dev->m_lastErrorText) - 1;
    memcpy(dev->m_lastErrorText, text, n);
    dev->m_lastErrorText[n] = '\0';
    dev->m_hasErrorText = true;
}

void XVfdDevice_emitError(XVfdDevice* dev, const char* message)
{
    if (!dev) return;
    /* [留住最后失败原因 2026-11-10] errorOccurred 信号是**瞬时**的：没挂
     *   监听者就丢了。上层（HSF 服务端）要把「为什么拒绝」回给操作员，
     *   靠的就是这份留存文本——此前设备层只留错误**枚举**（XVfdError_*），
     *   具体原因（如「命令源未切到通讯」）随信号消失 ⇒ 服务端只能回笼统
     *   文案（设备拒绝（在途动作/控制闸/能力不符）），现场无从判断。 */
    if (message && message[0]) {
        size_t n = strlen(message);
        if (n >= sizeof(dev->m_lastErrorText)) n = sizeof(dev->m_lastErrorText) - 1;
        memcpy(dev->m_lastErrorText, message, n);
        dev->m_lastErrorText[n] = '\0';
        dev->m_hasErrorText = true;
    }
    XVfdDevice_errorOccurred_signal(dev, message);
}

void XVfdDevice_setLastError(XVfdDevice* dev, XVfdError error)
{
    if (!dev) return;
    dev->m_lastError = error;
    /* 错误枚举变了 ⇒ 旧的文本不再对应（除非本次也带了具体原因）。 */
    if (error == XVfdError_OK) dev->m_hasErrorText = false;
}

const char* XVfdDevice_lastErrorText(const XVfdDevice* dev)
{
    if (!dev || !dev->m_hasErrorText) return NULL;
    return dev->m_lastErrorText;
}

const char* XVfdDevice_errorText(XVfdError error)
{
    switch (error) {
    case XVfdError_OK:                return "无错误";
    case XVfdError_NotAttached:       return "设备未挂接通道组或语义槽未绑定";
    case XVfdError_Busy:              return "设备忙（上一动作在途）";
    case XVfdError_WriteRejected:     return "写入被拒（点未实现/未登记/类型不符）";
    case XVfdError_WriteFailed:       return "写落定失败";
    case XVfdError_Timeout:           return "写落定监督超时";
    case XVfdError_NoData:            return "所需点尚无数据（未首拍/数据源离线）";
    case XVfdError_ParamRejected:     return "参数被拒绝（无绑定/越界）";
    case XVfdError_ParamRunLocked:    return "参数运行禁改";
    case XVfdError_NotSupported:      return "该品牌不支持此操作";
    case XVfdError_InvalidParam:      return "调用参数非法";
    case XVfdError_ControlDisabled:   return "控制闸未使能（设备被锁在只读）";
    default:                          return "未知错误";
    }
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
    if (!xvfdReadPoint(dev->m_group, slot->m_point, outValue)) {
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
    /* [有符号原生量 2026-11-02 归口] 补码归一已下沉到 XIoAnalogPoint_toRaw
     *   （2026-11-03 拆分后标度换算在模拟量子类）：
     *   有符号点（点上的 m_signed=1）负值输出 16 位补码原码，无符号点钳 0。
     *   本层不再做符号处理——同一语义只在通道层定义一次（旧实现两处各有一套，
     *   走设备层与走组层结果不同）。 */
    if (!xvfdWritePoint(dev, slot->m_point, nativeValue)) {
        if (dev) dev->m_lastError = XVfdError_WriteRejected;
        return false;
    }
    return true;
}

bool XVfdDevice_beginAction(XVfdDevice* dev, XVfdOperation op)
{
    if (!dev) return false;
    if (!dev->m_controlEnabled) {
        /* 控制闸未使能：全部修改动作在咽喉处拒绝（读路径不经过本函数）。 */
        dev->m_lastError = XVfdError_ControlDisabled;
        xvfdKeepErrorText(dev, "控制闸未使能（设备被锁在只读，或运行模式是自动）");
        return false;
    }
    if (!dev->m_group) {
        dev->m_lastError = XVfdError_NotAttached;
        xvfdKeepErrorText(dev, "设备未挂接通道组或语义槽未绑定");
        return false;
    }
    if (dev->m_busy) {
        dev->m_lastError = XVfdError_Busy;
        xvfdKeepErrorText(dev, "设备忙（上一动作在途）");
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
    dev->m_actionTimer = XObject_startTimer((XObject*)dev, (uint64_t)delayMs,
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
    const char* tr;
    if (!dev) return false;
    if (!XVfdDevice_beginAction(dev, op)) return false;
    tr = getenv("XHSF_VFD_TRACE");
    if (tr && tr[0] && tr[0] != '0')
        XPrintf("[vfd] actionWrite op=%d role=%d raw=%ld -> writeRole\n",
                (int)op, (int)role, (long)nativeValue);
    if (!XVfdDevice_writeRole(dev, role, nativeValue)) {
        xvfdKeepErrorText(dev, XVfdDevice_errorText(dev->m_lastError));
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

/**
 * @brief 转速是**换算值**：由输出频率按铭牌额定点线性换算（50Hz ↔ 1440rpm）。
 * @details [为什么设备层算] 变频器本体没有转速寄存器（M512 只有频率），
 *          转速只能由频率按额定点比例换算——滑差随负载变化，故这是**估算**，
 *          界面上必须标明"换算值"，不能当实测转速。
 *          与 Python 侧 VfdDevice.rpm_of 同一口径（50Hz → 1440rpm）。
 *          [谁优先] 设备**真带**转速寄存器时（如 ATV61 的 RFRD）以寄存器
 *          为准，本函数不覆盖——寄存器值是实测，比换算可信。
 * @param dev 设备实例。
 * @return 无。
 */
static void xvfdDeriveSpeedRpm(XVfdDevice* dev)
{
    XVfdStatus* st;

    if (!dev) return;
    st = &dev->m_status;
    /* 设备自带转速寄存器 ⇒ 实测值优先，不覆盖。 */
    if (dev->m_slots[XVfdRole_SpeedRpm].m_spec) return;
    if (!(st->m_validMask & XVfdField_OutputFreqHz)) return;
    st->m_speedRpm = XVfdDevice_hzToRpm(dev, st->m_outputFreqHz);
    st->m_validMask |= XVfdField_SpeedRpm;
}

void XVfdDevice_decodePoint_base(XVfdDevice* dev, const XVfdPointSpec* spec, int32_t raw)
{
    XVfdStatus* st;
    double eng;
    if (!dev || !spec) return;
    st = &dev->m_status;
    /* [2026-11-02 标度归属] 语义点在通道层不标度（点标度恒为 1/1），故这里
     * 拿到的就是原生量，标度换算在本层用 double 做（保留小数，如 69/10=6.9）。
     * 通道层的 XIoPoint_toEngineering 返回 int32_t，做不了小数标度。 */
    eng = XVfdDevice_rawToEngineering(raw, spec->m_div);
    switch (spec->m_role) {    case XVfdRole_StatusWord:
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
        /* 频率一变，换算出来的转速也跟着变（见 xvfdDeriveSpeedRpm）。 */
        xvfdDeriveSpeedRpm(dev);
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
    case XVfdRole_MaxFreq:
        /* [换算基准] 最大频率不进快照，但缓存下来供相对口径频率换算。 */
        dev->m_maxFreqHz = eng;
        dev->m_maxFreqKnown = true;
        break;
    default:
        return; /* 写角色/辅助字/铭牌与故障历史角色：由品牌读编排消费。 */
    }
    /* [阶段推导] 输出频率/设定频率/状态字任一变化都重算一次运行阶段
     * （加速中/减速中/正在停机）。只有开了推导的品牌才做（ATV61 的状态字
     * 自带加/减速位，基类推导会覆盖它更准的结论）。 */
    if (dev->m_phaseDerive) XVfdDevice_deriveRunPhase(dev);
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
    /* [触发即注销 2026-11-03] XObject_startTimer_ms 注册的是**重复型**定时器：
     * 触发 ≠ 注销 —— fd 槽位与调度节点都存续到 killTimer 才释放
     * （XAbstractEventDispatcher_registerTimer 分配 XFd 槽，
     *  unregisterTimer 才 XFd_free）。旧实现只把存储 id 置 INVALID：
     *   ① 下一拍 xvfdStatusTimerStart 因 id 已失效**杀不掉**旧定时器，
     *      而每拍又 start 一个新定时器 ⇒ **每拍永久泄漏一个 fd**；
     *   ② fd 表只有 128 槽，占满后全应用再无定时器可用 ⇒
     *      RTU 客户端的帧间定时器注册失败（id 变 -1）⇒ 完整应答帧滞留
     *      接收缓冲永不解析 ⇒ **所有请求超时**（真机实测 200ms 状态间隔
     *      约 25s、1000ms 约 36s 耗尽 128 槽）。
     * 这与 XModbusRtuSerialServer「485 从站 timerEvent 触发不注销泄漏
     * 定时器占满 128 槽 XFD 表」是同一类缺陷的另一处复发，故此处同样
     * 「触发即注销」。 */
    if (dev->m_statusTimer != XTIMER_INVALID_ID && id == dev->m_statusTimer) {
        XEvent_accept((XEvent*)event);
        XObject_killTimer(obj, id);          /* 触发即注销：释放 fd 槽 */
        dev->m_statusTimer = XTIMER_INVALID_ID;
        if (dev->m_dirty) XVfdDevice_emitStatus(dev);
        if (dev->m_group) xvfdStatusTimerStart(dev);
        return;
    }
    if (dev->m_actionTimer != XTIMER_INVALID_ID && id == dev->m_actionTimer) {
        XEvent_accept((XEvent*)event);
        XObject_killTimer(obj, id);          /* 同上：动作监督定时器也是重复型 */
        dev->m_actionTimer = XTIMER_INVALID_ID;
        XClassGetVirtualFunc(dev, EXVfdDevice_ActionTimerExpired,
            void (*)(XVfdDevice*))(dev);
        return;
    }
    /* 未知定时器 id 交父类链处理：第一参=直接父类（XClass_Parent 语义）。
     * **这里不 killTimer**：本类的两个定时器在各自分支里已「触发即注销」，
     * 走到这里的只可能是已注销的迟到事件（id 槽位已被复用时误杀活定时器
     * 会把动作监督掐掉，比泄漏一个已死定时器更糟）。 */
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
