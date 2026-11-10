
/**
 * @file       HsfServerIoFlowMeter.c
 * @brief      服务端电磁流量计站实现。
 * @details    数据流（本站自建，不经 XModbusLink 的直通通路）：
 *               Hub 轮询 FC04（22 个寄存器一段）
 *               → 缓存块（输入寄存器 vbase..vbase+21）
 *               → valueChanged(type=IR, vStart, count)
 *               → 本站槽：按区间逐地址重取 → 组 feedRole(role, raw)
 *               → XFlowMeter（32 位拼装 + 单位/报警解析）→ 缓存/信号
 *
 *             [为什么一次喂全 22 个] Hub 一帧回填 22 个字、发一次区间信号。
 *             本站据此把 22 个 role 全部 feed 一遍（值未变的被组内静默吞掉），
 *             设备层随后整拍重算——32 位量读到的高低字必然同源。若改成
 *             「只喂变化的那个字」，高字到达时低字仍是上一帧值，会解出错值。
 */
#include "HsfServerIoFlowMeter.h"

#include "HsfServerConfig.h"
#include "HsfServerBus.h"

#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XIoChannelGroup.h"
#include "XIoPoint.h"
#include "XIoAnalogPoint.h"
#include "XFlowMeter.h"
#include "XVarList.h"
#include "XPrintf.h"
#include <string.h>

/* ==================== 内部常量 ==================== */

/** @brief 从站变量区远端起始地址（协议表 2-1：0x1010）。 */
#define HSF_FLOWMETER_REMOTE_START 0x1010u

/** @brief 本站寄存器数量（0x1010~0x1025 共 22 个）。 */
#define HSF_FLOWMETER_REG_COUNT    ((uint32_t)XFlowMeterRole_Count)

/**
 * @brief 从站单帧读寄存器上限。
 * @note  实测 125（协议规范上限）也能一次读回，但本站只需要 22 个；
 *        这里按实际需要声明，避免 Hub 规划出无意义的大帧。
 */
#define HSF_FLOWMETER_MAX_READ_REGS HSF_FLOWMETER_REG_COUNT

/* ==================== 质量判定 ==================== */

/**
 * @brief 本站数据的质量判定（真实数据源口径）。
 * @param st 目标站。
 * @param staleMs 陈旧阈值（毫秒）。
 * @return 至少一个寄存器新鲜返回 true。
 * @details 与温湿度站同款：管道静止时流量恒定，组层 feedRole「值未变则
 *          静默」，设备的最近一拍时刻会停在很久以前而被设备层判陈旧。
 *          真正的数据源新鲜度只有 Hub 知道（块级 lastOkMs）——用
 *          XModbusHub_pointValid 按本站寄存器判，才是正确口径。
 */
static bool flowMeterEvaluateQuality(const HsfServerIoFlowMeter* st, uint32_t staleMs)
{
    XModbusHub* hub;
    int i;

    if (!st || !st->m_bus) return false;
    hub = HsfServerBus_hub(st->m_bus);
    if (!hub) return false;
    /* 取首尾各一个点：同一映射段同一次回填，判一个即可代表整段；
     * 多判几个只为容错（个别地址绑定失败时不至于整体误判）。 */
    for (i = 0; i < (int)HSF_FLOWMETER_REG_COUNT; ++i) {
        if (XModbusHub_pointValid(hub, XModbusInputRegisters,
                                  (uint16_t)(st->m_vbase + i), staleMs))
            return true;
    }
    return false;
}

/* ==================== 采样拍 ==================== */

/**
 * @brief 从 Hub 缓存取本站全部寄存器并喂进通道组（一次采样拍）。
 * @param st 目标站；未装配返回 false。
 * @return 喂进了至少一个字返回 true。
 * @note [必须用 feedRoleRefresh 而不是 feedRole] 本函数是**心跳拍**：静止
 *       时值恒定，feedRole 会因"值未变"静默、一帧信号都不发，设备层
 *       XFlowMeter 的「最近一拍时刻」就停住不动，超过 staleMs 被判陈旧 ⇒
 *       valid=false（实测：973/1031 次取值为 0，页面流量恒 0，而 Hub 里的
 *       原始字完全正确）。feedRoleRefresh 值未变也发射一次，正好表达
 *       "这一拍的数据确实是从链路读回来的"。变频器侧踩过同一个坑
 *       （见 XIoChannelGroup_feedRoleRefresh 的注记）。
 */
static bool flowMeterSampleFromHub(HsfServerIoFlowMeter* st)
{
    XModbusHub* hub;
    bool fed = false;
    int i;

    if (!st || !st->m_bus || !st->m_group) return false;
    hub = HsfServerBus_hub(st->m_bus);
    if (!hub) return false;
    for (i = 0; i < (int)HSF_FLOWMETER_REG_COUNT; ++i) {
        uint16_t raw = 0;
        if (!XModbusHub_value(hub, XModbusInputRegisters,
                              (uint16_t)(st->m_vbase + i), &raw))
            continue;
        XIoChannelGroup_feedRoleRefresh(st->m_group, (uint16_t)i, (int32_t)raw);
        fed = true;
    }
    return fed;
}

/**
 * @brief 周期采样拍（定时器槽）。
 * @details 管道静止/流量恒定时 Hub「变化才发」，一帧信号都不发。若只靠
 *          信号驱动，设备层会因「太久没有新拍」而判陈旧。本拍按固定节拍
 *          从 Hub 缓存重取当前值——值不变的拍被组内静默吞掉（不产生无谓
 *          信号），但设备层的「最近一拍时刻」被刷新。
 */
static void VHsfServerIoFlowMeter_timerEvent(XObject* obj, XTimerEvent* event)
{
    HsfServerIoFlowMeter* st = (HsfServerIoFlowMeter*)obj;
    if (!st || !event) return;
    XEvent_accept(event);
    if (XTimerEvent_timerId(event) == st->m_sampleTimer) {
        (void)flowMeterSampleFromHub(st);
        return;
    }
    /* 未知 id 交父类，不吞。 */
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))((XObject*)st, event);
}

/* ==================== Hub 信号槽 ==================== */

/**
 * @brief Hub 值变化槽：区间与本站相交就整段重喂。
 * @param receiver 连接的接收者（即本站对象）；**不是** Hub，禁止直接强转。
 * @param args 信号参数（int type, int virtualStart, int count）。
 * @return 无。
 * @note 整段重喂（而不是只喂变化的那几个字）是本模块的正确性关键：
 *       32 位量由相邻两字拼成，只喂高字会让设备层读到上一帧的低字。
 */
static void VHsfServerIoFlowMeter_onHubValueChanged(XObject* receiver, XVarList* args)
{
    HsfServerIoFlowMeter* st;
    int typeInt;
    int vStart;
    int count;

    if (!receiver || !args) return;
    st = (HsfServerIoFlowMeter*)receiver;
    if (!st->m_bus || !st->m_group) return;

    /* 宏展开为三条独立声明，变量名不能与形参（type）撞名。 */
    XVarList_args_3(args, int, regType, int, start, int, cnt);
    typeInt = regType;
    vStart = start;
    count = cnt;
    if (typeInt != (int)XModbusInputRegisters) return;
    if (count <= 0) return;

    /* 区间与 [vbase, vbase+22) 不相交则忽略。 */
    if (vStart < 0) return;
    if ((uint32_t)vStart >= (uint32_t)st->m_vbase + HSF_FLOWMETER_REG_COUNT) return;
    if ((uint32_t)vStart + (uint32_t)count <= (uint32_t)st->m_vbase) return;

    (void)flowMeterSampleFromHub(st);
}

/* ==================== 生命周期 ==================== */

/**
 * @brief 建一个模拟量输入点并登记进组（失败自行清理）。
 * @param group 目标通道组。
 * @param role 语义槽号 = 寄存器在站内的偏移。
 * @param tag 点名（静态常量借用）。
 * @return 成功 true。
 * @note **标度必须恒等（1/1/0）且无符号**：这些字是 32 位量的原始位模式，
 *       任何标度运算都会破坏 IEEE754。标度换算由设备层在拼成 float 之后做。
 */
static bool flowMeterMakePoint(XIoChannelGroup* group, uint16_t role, const char* tag)
{
    XIoAnalogPoint* apt = XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XIoPoint* pt;
    if (!apt) return false;
    pt = &apt->m_base;
    if (!XIoPoint_setRole(pt, role)) {
        XClassDelete((XClass*)apt);
        return false;
    }
    XIoPoint_setType(pt, XIoPoint_AnalogInput);
    XIoPoint_setTag(pt, tag);
    XIoPoint_setUseHolding(pt, false);        /* 输入寄存器（FC04）。 */
    XIoPoint_setSigned(pt, false);            /* 32 位量的字按无符号位模式处理。 */
    XIoAnalogPoint_setScale(apt, 1, 1, 0, 0); /* 恒等：不碰位模式。 */
    if (!XIoChannelGroup_addPoint(group, pt, true)) {
        XClassDelete((XClass*)apt);
        return false;
    }
    return true;
}

static void VHsfServerIoFlowMeter_deinit(XObject* obj);

XVtable* HsfServerIoFlowMeter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfServerIoFlowMeter)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VHsfServerIoFlowMeter_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VHsfServerIoFlowMeter_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(HsfServerIoFlowMeter);
    return XVTABLE_DEFAULT;
}

void HsfServerIoFlowMeter_init(HsfServerIoFlowMeter* st)
{
    if (!st) return;
    memset(st, 0, sizeof(*st));
    XObject_init((XObject*)st);
    XClassGetVtable(st) = HsfServerIoFlowMeter_class_init();
    st->m_bus = NULL;
    st->m_group = NULL;
    st->m_dev = NULL;
    st->m_map = NULL;
    st->m_conn = NULL;
    st->m_sampleTimer = XTIMER_INVALID_ID;
    st->m_unitId = -1;
    st->m_vbase = 0;
    st->m_mounted = false;
}

HsfServerIoFlowMeter* HsfServerIoFlowMeter_create(void)
{
    HsfServerIoFlowMeter* st = (HsfServerIoFlowMeter*)XMemory_malloc(
        sizeof(HsfServerIoFlowMeter), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!st) return NULL;
    HsfServerIoFlowMeter_init(st);
    Set_Class_Memory(st, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(st, true);
    return st;
}

/* ==================== 装配 ==================== */

bool HsfServerIoFlowMeter_mount(HsfServerIoFlowMeter* st, HsfServerBus* bus,
                                XModbusMapping* map, int unitId,
                                uint16_t vbase, uint32_t staleMs)
{
    XModbusHub* hub;
    XModbusHubUnitLimits limits;
    int devId;
    int i;

    if (!st || st->m_mounted) return false;
    if (!bus || !map) return false;
    hub = HsfServerBus_hub(bus);
    devId = HsfServerBus_deviceId(bus);
    if (!hub || devId < 0) {
        XPrintf("HsfServerIoFlowMeter: 总线未就绪\n");
        return false;
    }

    st->m_bus = bus;
    st->m_map = map;
    st->m_unitId = unitId;
    st->m_vbase = vbase;

    /* ① 映射段：输入寄存器 vbase..vbase+21 ↔ 远端 0x1010..0x1025。 */
    if (!XModbusMapping_addRangeEx(map, XModbusInputRegisters, vbase,
                                   HSF_FLOWMETER_REG_COUNT, devId, unitId,
                                   HSF_FLOWMETER_REMOTE_START, 0, 0)) {
        XPrintf("HsfServerIoFlowMeter: 映射段登记失败（站 %d v=0x%04X）\n",
                unitId, (unsigned)vbase);
        goto fail;
    }
    /* ② 从站协议约束（作用域=从站）：单帧读 22 个 + 分级节拍。
     *    本机实测异常应答是标准格式（功能码置 0x80），但本站只读不写，
     *    故不设 singleWriteOnly（没有写路径需要约束）。 */
    memset(&limits, 0, sizeof(limits));
    limits.m_maxReadRegisters = (uint16_t)HSF_FLOWMETER_MAX_READ_REGS;
    limits.m_rangeBarrier = true;      /* 一帧不跨通信组边界。 */
    limits.m_singleWriteOnly = true;   /* 本站无写路径；保持与其它站一致。 */
    limits.m_minPollIntervalMs = HSF_FLOWMETER_MIN_POLL_MS;
    if (!XModbusHub_setUnitLimits(hub, devId, unitId, &limits)) {
        XPrintf("HsfServerIoFlowMeter: 从站约束登记失败（站 %d）\n", unitId);
        goto fail;
    }
    /* ③ 通道组 + 22 个模拟量输入点（role 0..21 ↔ 寄存器偏移）。 */
    st->m_group = XIoChannelGroup_create();
    if (!st->m_group) goto fail;
    for (i = 0; i < (int)HSF_FLOWMETER_REG_COUNT; ++i) {
        if (!flowMeterMakePoint(st->m_group, (uint16_t)i, "FLOW")) {
            XPrintf("HsfServerIoFlowMeter: 输入点装配失败（role %d）\n", i);
            goto fail;
        }
    }
    /* ④ 设备抽象（32 位拼装 + 单位/报警解析）。 */
    st->m_dev = XFlowMeter_create();
    if (!st->m_dev) goto fail;
    XFlowMeter_setStaleMs(st->m_dev, staleMs);
    /* [站点层也记住阈值 2026-11-10] 质量判定走 Hub 块级新鲜度，必须比
     * **采集周期**而不是编译期基线（与温湿度/浊度站同款）。 */
    st->m_staleMs = staleMs;
    if (!XFlowMeter_attach(st->m_dev, st->m_group)) {
        XPrintf("HsfServerIoFlowMeter: 设备挂组失败\n");
        goto fail;
    }
    /* ⑤ 订阅 Hub 值变化（唯一一条；整段重喂在槽内完成）。
     *    **必须在 HsfServerBus_start 之前订阅**：Hub 的 valueChanged 是
     *    「变化才发」，管道静止时首拍之后不再发——订阅晚于首拍就拿不到初值。 */
    st->m_conn = XObject_connect_1((XObject*)hub,
                                   XSignal(XModbusHub_valueChanged_signal),
                                   (XObject*)st,
                                   VHsfServerIoFlowMeter_onHubValueChanged,
                                   XConnectionType_Auto);
    if (!st->m_conn) {
        XPrintf("HsfServerIoFlowMeter: Hub 信号订阅失败（站 %d）\n", unitId);
        goto fail;
    }
    /* ⑥ 采样定时器：静止量的「拍」靠它刷新（见槽函数说明）。 */
    st->m_sampleTimer = XObject_startTimer_ms((XObject*)st,
                                              (int)HSF_FLOWMETER_SAMPLE_MS,
                                              XTimerType_CoarseTimer);
    if (st->m_sampleTimer == XTIMER_INVALID_ID) {
        XPrintf("HsfServerIoFlowMeter: 采样定时器启动失败（站 %d）\n", unitId);
        goto fail;
    }
    st->m_mounted = true;
    XPrintf("HsfServerIoFlowMeter: 站 %d 装配完成（IR v=0x%04X × %u，"
            "远端 0x%04X，陈旧 %ums）\n", unitId, (unsigned)vbase,
            (unsigned)HSF_FLOWMETER_REG_COUNT,
            (unsigned)HSF_FLOWMETER_REMOTE_START, (unsigned)staleMs);
    return true;

fail:
    HsfServerIoFlowMeter_unmount(st);
    return false;
}

void HsfServerIoFlowMeter_unmount(HsfServerIoFlowMeter* st)
{
    if (!st) return;
    /* ① 先停采样定时器（「触发≠注销」纪律：触发点不注销就泄漏槽位）。 */
    if (st->m_sampleTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)st, st->m_sampleTimer);
        st->m_sampleTimer = XTIMER_INVALID_ID;
    }
    /* ② 退订 Hub 信号（订阅挂在 Hub 上；Hub 须仍存活）。 */
    if (st->m_conn) {
        if (st->m_bus && HsfServerBus_hub(st->m_bus))
            XObject_disconnect_2(st->m_conn);
        st->m_conn = NULL;
    }
    /* ③ 删设备：析构里停质量巡检定时器并退订组信号。 */
    if (st->m_dev) {
        XClassDelete((XClass*)st->m_dev);
        st->m_dev = NULL;
    }
    /* ④ 删组：点随 autoDelete 一并释放。 */
    if (st->m_group) {
        XClassDelete((XClass*)st->m_group);
        st->m_group = NULL;
    }
    st->m_bus = NULL;
    st->m_map = NULL;
    st->m_unitId = -1;
    st->m_mounted = false;
}

static void VHsfServerIoFlowMeter_deinit(XObject* obj)
{
    HsfServerIoFlowMeter* st = (HsfServerIoFlowMeter*)obj;
    if (!st) return;
    HsfServerIoFlowMeter_unmount(st);
    XClass_Deinit_Parent(XObject, obj);
}

/* ==================== 查询 ==================== */

XFlowMeter* HsfServerIoFlowMeter_device(const HsfServerIoFlowMeter* st)
{
    return st ? st->m_dev : NULL;
}

bool HsfServerIoFlowMeter_values(const HsfServerIoFlowMeter* st,
                                 double* outFlow, double* outSpeed,
                                 double* outForwardTotal, double* outReverseTotal,
                                 int* outFlowUnit, int* outTotalUnit,
                                 int* outAlarms, bool* outValid)
{
    bool valid;
    double v = 0.0;
    if (outFlow) *outFlow = 0.0;
    if (outSpeed) *outSpeed = 0.0;
    if (outForwardTotal) *outForwardTotal = 0.0;
    if (outReverseTotal) *outReverseTotal = 0.0;
    if (outFlowUnit) *outFlowUnit = (int)XFlowMeterFlowUnit_Unknown;
    if (outTotalUnit) *outTotalUnit = (int)XFlowMeterTotalUnit_Unknown;
    if (outAlarms) *outAlarms = 0;
    if (outValid) *outValid = false;
    if (!st || !st->m_mounted || !st->m_dev || !st->m_bus) return false;

    /* 质量：Hub 数据源新鲜度（见文件头说明），不用设备层陈旧计时。 */
    valid = flowMeterEvaluateQuality(st, st->m_staleMs);
    if (outValid) *outValid = valid;
    if (!valid) return false;

    /* 值：直接取设备层缓存（32 位拼装已由 XFlowMeter 完成）。
     * 用 XFlowMeter_value 而非绕过设备层：拼装与解码逻辑只应有一处。 */
    if (XFlowMeter_value(st->m_dev, XFlowMeterQuantity_InstantFlow, &v)) {
        if (outFlow) *outFlow = v;
    }
    if (XFlowMeter_value(st->m_dev, XFlowMeterQuantity_InstantSpeed, &v)) {
        if (outSpeed) *outSpeed = v;
    }
    if (XFlowMeter_value(st->m_dev, XFlowMeterQuantity_ForwardTotal, &v)) {
        if (outForwardTotal) *outForwardTotal = v;
    }
    if (XFlowMeter_value(st->m_dev, XFlowMeterQuantity_ReverseTotal, &v)) {
        if (outReverseTotal) *outReverseTotal = v;
    }
    if (outFlowUnit) *outFlowUnit = (int)XFlowMeter_flowUnit(st->m_dev);
    if (outTotalUnit) *outTotalUnit = (int)XFlowMeter_totalUnit(st->m_dev);
    if (outAlarms) *outAlarms = (int)XFlowMeter_alarms(st->m_dev);
    return true;
}

bool HsfServerIoFlowMeter_isMounted(const HsfServerIoFlowMeter* st)
{
    return st && st->m_mounted;
}

bool HsfServerIoFlowMeter_sampleNow(HsfServerIoFlowMeter* st)
{
    if (!st || !st->m_mounted) return false;
    return flowMeterSampleFromHub(st);
}

int HsfServerIoFlowMeter_health(int alarms, bool valid)
{
    /* 只有**系统报警**算设备告警；「空管」在泵停/无流时必然置位，
     * 判成故障会让首页误报「电磁流量计 故障」（Python 侧同口径）。 */
    if (alarms & (int)XFlowMeterAlarm_System) return HsfDeviceHealth_Warning;
    return valid ? HsfDeviceHealth_Ok : HsfDeviceHealth_Stale;
}
