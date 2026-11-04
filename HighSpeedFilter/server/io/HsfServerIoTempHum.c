
/**
 * @file       HsfServerIoTempHum.c
 * @brief      服务端温湿度站实现。
 * @details    数据流（本站自建，不经 XModbusLink 的直通通路）：
 *               Hub 轮询 FC04 → 缓存块（输入寄存器 vbase..vbase+1）
 *               → valueChanged(type=IR, vStart, count)
 *               → 本站槽：逐地址取值 → 负温折返解码 → 组 feedRole(role, raw)
 *               → XTempHumTransmitter（第二级标度）→ 缓存/信号
 *             负温折返必须在这一层做：XIoAnalogPoint 的线性标度表达不了
 *             「raw>=10000 表示负数」的分段映射。
 */
#include "HsfServerIoTempHum.h"

#include "HsfServerConfig.h"
#include "HsfServerBus.h"

#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XIoChannelGroup.h"
#include "XIoPoint.h"
#include "XIoAnalogPoint.h"
#include "XTempHumTransmitter.h"
#include "XVarList.h"
#include "XPrintf.h"
#include <string.h>

/* ==================== 内部常量 ==================== */

/** @brief 从站寄存器 0000H/0001H 的远端地址（固定：手册输入寄存器起始）。 */
#define HSF_TEMPHUM_REMOTE_START   0u

/** @brief 从站单帧读寄存器上限（实测 20，非通用 125）。 */
#define HSF_TEMPHUM_MAX_READ_REGS  20u

/** @brief 负温编码阈值（手册 2.1：raw >= 10000 表示负温度）。 */
#define HSF_TEMPHUM_NEG_BASE       10000u

/** @brief 温度/湿度寄存器在站内的偏移（= 组内 role）。 */
#define HSF_TEMPHUM_OFF_TEMP       0
#define HSF_TEMPHUM_OFF_HUM        1

/* ==================== 纯解码助手 ==================== */

int32_t HsfServerIoTempHum_decodeTemp(uint16_t raw)
{
    if (raw >= (uint16_t)HSF_TEMPHUM_NEG_BASE)
        return -(int32_t)((uint32_t)raw - HSF_TEMPHUM_NEG_BASE);
    return (int32_t)raw;
}

int32_t HsfServerIoTempHum_decodeHum(uint16_t raw)
{
    return (int32_t)raw;
}

/**
 * @brief 本站数据的质量判定（真实数据源口径）。
 * @param st 目标站。
 * @param outTempValid/outHumValid 输出两通道质量（可为 NULL）。
 * @return 至少一路有效返回 true。
 * @details 质量**不能靠设备层的陈旧计时**：房间温湿度长期稳定，组层的
 *          feedRole「值未变则静默」，设备的最近一拍时刻因此停在很久以前，
 *          稳定量会被误判陈旧（2026-11-04 实锅：温度恒读 0.0）。
 *          真正的数据源新鲜度只有 Hub 知道（块级 lastOkMs）——用
 *          XModbusHub_pointValid 按本站两个寄存器判，才是正确口径。
 */
static bool temphumEvaluateQuality(const HsfServerIoTempHum* st,
                                   bool* outTempValid, bool* outHumValid)
{
    XModbusHub* hub;
    bool tv = false;
    bool hv = false;

    if (outTempValid) *outTempValid = false;
    if (outHumValid) *outHumValid = false;
    if (!st || !st->m_bus) return false;
    hub = HsfServerBus_hub(st->m_bus);
    if (!hub) return false;
    tv = XModbusHub_pointValid(hub, XModbusInputRegisters, st->m_vbase,
                               HSF_TEMPHUM_STALE_MS);
    hv = XModbusHub_pointValid(hub, XModbusInputRegisters,
                               (uint16_t)(st->m_vbase + HSF_TEMPHUM_OFF_HUM),
                               HSF_TEMPHUM_STALE_MS);
    if (outTempValid) *outTempValid = tv;
    if (outHumValid) *outHumValid = hv;
    return tv || hv;
}

/* ==================== 采样拍 ==================== */

/**
 * @brief 从 Hub 缓存取本站两个字并喂进通道组（一次采样拍）。
 * @param st 目标站；未装配返回 false。
 * @return 喂进了至少一个字返回 true。
 * @note 组内 feedRole 自带「值未变则静默」，故重复调用无副作用。
 */
static bool temphumSampleFromHub(HsfServerIoTempHum* st)
{
    XModbusHub* hub;
    uint16_t raw = 0;
    bool fed = false;

    if (!st || !st->m_bus || !st->m_group) return false;
    hub = HsfServerBus_hub(st->m_bus);
    if (!hub) return false;
    if (XModbusHub_value(hub, XModbusInputRegisters, st->m_vbase, &raw)) {
        XIoChannelGroup_feedRole(st->m_group, HSF_TEMPHUM_ROLE_TEMP,
                                 HsfServerIoTempHum_decodeTemp(raw));
        fed = true;
    }
    if (XModbusHub_value(hub, XModbusInputRegisters,
                         (uint16_t)(st->m_vbase + HSF_TEMPHUM_OFF_HUM), &raw)) {
        XIoChannelGroup_feedRole(st->m_group, HSF_TEMPHUM_ROLE_HUM,
                                 HsfServerIoTempHum_decodeHum(raw));
        fed = true;
    }
    return fed;
}

/**
 * @brief 周期采样拍（定时器槽）。
 * @details 房间温湿度是**长期稳定量**：Hub 的 valueChanged 只在值变化时
 *          发射，稳定下来之后一帧信号都不发。若只靠信号驱动，两点会因
 *          「太久没有新拍」被判陈旧（valid=0），前端看到的就是 0.0。
 *          故本站自带一个采样定时器，按固定节拍从 Hub 缓存重取当前值——
 *          值不变的拍被组内静默吞掉（不产生无谓信号），但设备的「最近
 *          一拍时刻」会被刷新，陈旧判定因此保持有效。
 *          （2026-11-04 实锅：纯信号驱动时温度/湿度随机一个读成 0。）
 */
static void VHsfServerIoTempHum_timerEvent(XObject* obj, XTimerEvent* event)
{
    HsfServerIoTempHum* st = (HsfServerIoTempHum*)obj;
    if (!st || !event) return;
    XEvent_accept(event);
    if (XTimerEvent_timerId(event) == st->m_sampleTimer) {
        (void)temphumSampleFromHub(st);
        return;
    }
    /* 未知 id 交父类，不吞。 */
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))((XObject*)st, event);
}

/* ==================== Hub 信号槽 ==================== */

/**
 * @brief Hub 值变化槽：只认本站那两个字，解码后喂通道组。
 * @param receiver 连接的接收者（即本站对象）；**不是** Hub，禁止直接强转。
 * @param args 信号参数（int type, int virtualStart, int count）。
 * @return 无。
 * @note 逐地址从 Hub 缓存重取（信号只给区间，不给值），与 XModbusLink
 *       同款口径；区间里落在本站之外的地址直接跳过。
 */
static void VHsfServerIoTempHum_onHubValueChanged(XObject* receiver, XVarList* args)
{
    HsfServerIoTempHum* st;
    int typeInt;
    int vStart;
    int count;
    int i;

    if (!receiver || !args) return;
    st = (HsfServerIoTempHum*)receiver;
    if (!st->m_bus || !st->m_group) return;

    /* 宏展开为三条独立声明，变量名不能与形参（type）撞名。 */
    XVarList_args_3(args, int, regType, int, start, int, cnt);
    typeInt = regType;
    vStart = start;
    count = cnt;
    if (typeInt != (int)XModbusInputRegisters) return;
    if (count <= 0) return;

    for (i = 0; i < count; ++i) {
        const uint32_t addr = (uint32_t)vStart + (uint32_t)i;
        uint16_t raw = 0;
        if (addr > 0xFFFFu) break;
        if (addr < (uint32_t)st->m_vbase) continue;
        if (addr >= (uint32_t)st->m_vbase + HSF_TEMPHUM_REG_COUNT) continue;
        if (!XModbusHub_value(HsfServerBus_hub(st->m_bus),
                              XModbusInputRegisters, (uint16_t)addr, &raw))
            continue;
        if (addr == (uint32_t)st->m_vbase + HSF_TEMPHUM_OFF_TEMP) {
            XIoChannelGroup_feedRole(st->m_group, HSF_TEMPHUM_ROLE_TEMP,
                                     HsfServerIoTempHum_decodeTemp(raw));
        } else {
            XIoChannelGroup_feedRole(st->m_group, HSF_TEMPHUM_ROLE_HUM,
                                     HsfServerIoTempHum_decodeHum(raw));
        }
    }
}

/* ==================== 生命周期 ==================== */

/**
 * @brief 建一个模拟量输入点并登记进组（失败自行清理）。
 * @return 成功 true。
 */
static bool temphumMakePoint(XIoChannelGroup* group, uint16_t role,
                             const char* tag)
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
    XIoPoint_setSigned(pt, true);             /* 负温为负值。 */
    /* 通道层标度直通：保留原生 0.1 单位（219 = 21.9℃）。 */
    XIoAnalogPoint_setScale(apt, 1, 1, 0, 0);
    if (!XIoChannelGroup_addPoint(group, pt, true)) {
        XClassDelete((XClass*)apt);
        return false;
    }
    return true;
}

static void VHsfServerIoTempHum_deinit(XObject* obj);

XVtable* HsfServerIoTempHum_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfServerIoTempHum)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VHsfServerIoTempHum_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VHsfServerIoTempHum_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(HsfServerIoTempHum);
    return XVTABLE_DEFAULT;
}

void HsfServerIoTempHum_init(HsfServerIoTempHum* st)
{
    if (!st) return;
    memset(st, 0, sizeof(*st));
    XObject_init((XObject*)st);
    XClassGetVtable(st) = HsfServerIoTempHum_class_init();
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

HsfServerIoTempHum* HsfServerIoTempHum_create(void)
{
    HsfServerIoTempHum* st = (HsfServerIoTempHum*)XMemory_malloc(
        sizeof(HsfServerIoTempHum), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!st) return NULL;
    HsfServerIoTempHum_init(st);
    Set_Class_Memory(st, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(st, true);
    return st;
}

/* ==================== 装配 ==================== */

bool HsfServerIoTempHum_mount(HsfServerIoTempHum* st, HsfServerBus* bus,
                              XModbusMapping* map, int unitId,
                              uint16_t vbase, uint32_t staleMs)
{
    XModbusHub* hub;
    XModbusHubUnitLimits limits;
    int devId;

    if (!st || st->m_mounted) return false;
    if (!bus || !map) return false;
    hub = HsfServerBus_hub(bus);
    devId = HsfServerBus_deviceId(bus);
    if (!hub || devId < 0) {
        XPrintf("HsfServerIoTempHum: 总线未就绪\n");
        return false;
    }

    st->m_bus = bus;
    st->m_map = map;
    st->m_unitId = unitId;
    st->m_vbase = vbase;

    /* ① 映射段：输入寄存器 vbase..vbase+1 ↔ 远端 0x0000..0x0001。 */
    if (!XModbusMapping_addRangeEx(map, XModbusInputRegisters, vbase,
                                   HSF_TEMPHUM_REG_COUNT, devId, unitId,
                                   HSF_TEMPHUM_REMOTE_START, 0, 0)) {
        XPrintf("HsfServerIoTempHum: 映射段登记失败（站 %d v=0x%04X）\n",
                unitId, (unsigned)vbase);
        goto fail;
    }
    /* ② 从站协议约束（作用域=从站）：块读上限 20 + 只 FC06 写 + 分级节拍。 */
    memset(&limits, 0, sizeof(limits));
    limits.m_maxReadRegisters = (uint16_t)HSF_TEMPHUM_MAX_READ_REGS;
    limits.m_rangeBarrier = true;      /* 一帧不跨通信组边界。 */
    limits.m_singleWriteOnly = true;   /* 不支持 FC10。 */
    limits.m_minPollIntervalMs = HSF_TEMPHUM_MIN_POLL_MS;   /* 本从站真实节拍。 */
    if (!XModbusHub_setUnitLimits(hub, devId, unitId, &limits)) {
        XPrintf("HsfServerIoTempHum: 从站约束登记失败（站 %d）\n", unitId);
        goto fail;
    }
    /* ③ 通道组 + 两个模拟量输入点。 */
    st->m_group = XIoChannelGroup_create();
    if (!st->m_group) goto fail;
    if (!temphumMakePoint(st->m_group, HSF_TEMPHUM_ROLE_TEMP, "TEMP") ||
        !temphumMakePoint(st->m_group, HSF_TEMPHUM_ROLE_HUM, "HUM")) {
        XPrintf("HsfServerIoTempHum: 输入点装配失败\n");
        goto fail;
    }
    /* ④ 设备抽象（第二级标度直通：工程值即 0.1 单位定点）。 */
    st->m_dev = XTempHumTransmitter_create();
    if (!st->m_dev) goto fail;
    XTempHumTransmitter_setStaleMs(st->m_dev, staleMs);
    if (!XTempHumTransmitter_attach(st->m_dev, st->m_group,
                                    XIoChannelGroup_pointAt(st->m_group,
                                                            HSF_TEMPHUM_ROLE_TEMP),
                                    XIoChannelGroup_pointAt(st->m_group,
                                                            HSF_TEMPHUM_ROLE_HUM))) {
        XPrintf("HsfServerIoTempHum: 设备挂组失败\n");
        goto fail;
    }
    /* ⑤ 订阅 Hub 值变化（唯一一条；负温解码在槽内完成）。
     *    **必须在 HsfServerBus_start 之前订阅**：Hub 的 valueChanged 是
     *    「变化才发」，温湿度在房间里长期稳定（219/645 不变），首拍之后就
     *    再也不发——订阅晚于首拍就永远拿不到初值（2026-11-04 实锅：
     *    订阅移到 start 后温湿度恒 valid=0，而变频器因为状态字每拍都变
     *    而侥幸正常）。 */
    st->m_conn = XObject_connect_1((XObject*)hub,
                                   XSignal(XModbusHub_valueChanged_signal),
                                   (XObject*)st,
                                   VHsfServerIoTempHum_onHubValueChanged,
                                   XConnectionType_Auto);
    if (!st->m_conn) {
        XPrintf("HsfServerIoTempHum: Hub 信号订阅失败（站 %d）\n", unitId);
        goto fail;
    }
    /* ⑥ 采样定时器：稳定量的「拍」靠它刷新（见槽函数说明）。 */
    st->m_sampleTimer = XObject_startTimer_ms((XObject*)st,
                                              (int)HSF_TEMPHUM_SAMPLE_MS,
                                              XTimerType_CoarseTimer);
    if (st->m_sampleTimer == XTIMER_INVALID_ID) {
        XPrintf("HsfServerIoTempHum: 采样定时器启动失败（站 %d）\n", unitId);
        goto fail;
    }
    st->m_mounted = true;
    XPrintf("HsfServerIoTempHum: 站 %d 装配完成（IR v=0x%04X × %u，"
            "陈旧 %ums）\n", unitId, (unsigned)vbase,
            (unsigned)HSF_TEMPHUM_REG_COUNT, (unsigned)staleMs);
    return true;

fail:
    HsfServerIoTempHum_unmount(st);
    return false;
}

void HsfServerIoTempHum_unmount(HsfServerIoTempHum* st)
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

static void VHsfServerIoTempHum_deinit(XObject* obj)
{
    HsfServerIoTempHum* st = (HsfServerIoTempHum*)obj;
    if (!st) return;
    HsfServerIoTempHum_unmount(st);
    XClass_Deinit_Parent(XObject, obj);
}

/* ==================== 查询 ==================== */

XTempHumTransmitter* HsfServerIoTempHum_device(const HsfServerIoTempHum* st)
{
    return st ? st->m_dev : NULL;
}

bool HsfServerIoTempHum_values(const HsfServerIoTempHum* st,
                               int32_t* outTempC10, int32_t* outHumPct10,
                               bool* outTempValid, bool* outHumValid)
{
    int32_t t = 0;
    int32_t h = 0;
    bool tv = false;
    bool hv = false;
    XModbusHub* hub;

    if (outTempC10) *outTempC10 = 0;
    if (outHumPct10) *outHumPct10 = 0;
    if (outTempValid) *outTempValid = false;
    if (outHumValid) *outHumValid = false;
    if (!st || !st->m_mounted || !st->m_bus) return false;

    /* 质量：Hub 数据源新鲜度（见 temphumEvaluateQuality 说明）。 */
    (void)temphumEvaluateQuality(st, &tv, &hv);
    /* 值：直接取 Hub 缓存并解码（与采样拍同口径，避免经过设备层的
     * 「陈旧判 0」——稳定量在设备层会被判陈旧而返回 0）。 */
    hub = HsfServerBus_hub(st->m_bus);
    if (hub && tv) {
        uint16_t raw = 0;
        if (XModbusHub_value(hub, XModbusInputRegisters, st->m_vbase, &raw))
            t = HsfServerIoTempHum_decodeTemp(raw);
        else
            tv = false;
    }
    if (hub && hv) {
        uint16_t raw = 0;
        if (XModbusHub_value(hub, XModbusInputRegisters,
                             (uint16_t)(st->m_vbase + HSF_TEMPHUM_OFF_HUM), &raw))
            h = HsfServerIoTempHum_decodeHum(raw);
        else
            hv = false;
    }
    if (outTempC10) *outTempC10 = tv ? t : 0;
    if (outHumPct10) *outHumPct10 = hv ? h : 0;
    if (outTempValid) *outTempValid = tv;
    if (outHumValid) *outHumValid = hv;
    return tv || hv;
}

bool HsfServerIoTempHum_isMounted(const HsfServerIoTempHum* st)
{
    return st && st->m_mounted;
}
