/**
 * @file       HsfServerIoTurbidity.c
 * @brief      服务端浊度仪站实现。
 * @details    数据流（本站自建，不经 XModbusLink 的直通通路）：
 *               Hub 轮询 FC03（每台各自的寄存器数一段：站 1=2 仅浊度、
 *               站 2=4 浊度+温度同刻）
 *               → 缓存块（保持寄存器 vbase..vbase+regCount-1）
 *               → valueChanged(type=HR, vStart, count)
 *               → 本站槽：按区间逐地址重取 → 组 feedRole(role, raw)
 *               → XTurbidityMeter（逐台字序拼装：站 1=CDAB、站 2=DCBA
 *                 + float32 解释）→ 缓存/信号
 *
 *             [为什么一次喂全 regCount 个] 一帧回填 regCount 个字、发一次区间
 *             信号。本站据此把全部 role feed 一遍，设备层随后整拍重算——
 *             32 位量读到的高低两字（以及温度两字）必然同源。若改成"只喂
 *             变化的那个字"，高字到达时低字仍是上一帧值，会解出错值
 *             （流量计站同一个教训）。
 *
 *             [为什么读保持寄存器（HR）而流量计读输入寄存器（IR）]
 *             手册 4.3.10.2 的测试命令是 `01 03 00 00 00 02`——**功能码 03**
 *             即读保持寄存器。流量计/温湿度是 FC04（输入寄存器）。这两种区
 *             在 Hub 里是**两个独立的虚拟地址空间**，映射时写错区号会表现为
 *             "点永远没有数据"，且不报任何错误。故本站把区号做成显式常量，
 *             并在装配日志里打出来。
 */
#include "HsfServerIoTurbidity.h"

#include "HsfServerConfig.h"
#include "HsfServerBus.h"

#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XIoChannelGroup.h"
#include "XIoPoint.h"
#include "XIoAnalogPoint.h"
#include "XTurbidityMeter.h"
#include "XVarList.h"
#include "XPrintf.h"
#include <string.h>

/* ==================== 内部常量 ==================== */

/**
 * @brief 浊度仪寄存器所在的 Modbus 区。
 * @note 手册 4.3.10.2：`01 03 ...` = FC03 = 读保持寄存器。**不是输入寄存器**。
 */
#define HSF_TURB_AREA   XModbusHoldingRegisters

/**
 * @brief 字序枚举值 → 装配日志短名（诊断用；与 XTurbidityMeterWordOrder 对齐）。
 */
static const char* wordOrderString(int order)
{
    switch (order) {
    case (int)XTurbidityMeterWordOrder_LowHigh: return "CDAB";
    case (int)XTurbidityMeterWordOrder_HighLow: return "ABCD";
    case (int)XTurbidityMeterWordOrder_Dcba:    return "DCBA";
    default: return "?";
    }
}

/* ==================== 质量判定 ==================== */

/**
 * @brief 本站数据的质量判定（真实数据源口径）。
 * @param st 目标站。
 * @param staleMs 陈旧阈值（毫秒）。
 * @return 至少一个寄存器新鲜返回 true。
 * @details 与流量计/温湿度站同款：水样浊度长期稳定，组层 feedRole「值未变则
 *          静默」，设备的最近一拍时刻会停在很久以前而被设备层判陈旧。真正的
 *          数据源新鲜度只有 Hub 知道（块级 lastOkMs）——用
 *          XModbusHub_pointValid 按本站寄存器判，才是正确口径。
 */
static bool turbStationQuality(const HsfServerIoTurbidity* st, uint32_t staleMs)
{
    XModbusHub* hub;
    int i;

    if (!st || !st->m_bus) return false;
    hub = HsfServerBus_hub(st->m_bus);
    if (!hub) return false;
    for (i = 0; i < st->m_regCount; ++i) {
        if (XModbusHub_pointValid(hub, HSF_TURB_AREA,
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
 * @note [必须用 feedRoleRefresh 而不是 feedRole] 本函数是**心跳拍**：浊度稳定
 *       时值恒定，feedRole 会因"值未变"静默、一帧信号都不发，设备层
 *       XTurbidityMeter 的「最近一拍时刻」就停住不动，超过 staleMs 被判陈旧
 *       ⇒ 页面显示"无数据"而 Hub 里原始字完全正确（流量计站踩过同一个坑：
 *       实测 973/1031 次取值被判陈旧）。feedRoleRefresh 值未变也发射一次，
 *       正好表达"这一拍的数据确实是从链路读回来的"。
 */
static bool turbSampleFromHub(HsfServerIoTurbidity* st)
{
    XModbusHub* hub;
    bool fed = false;
    int i;

    if (!st || !st->m_bus || !st->m_group) return false;
    hub = HsfServerBus_hub(st->m_bus);
    if (!hub) return false;
    for (i = 0; i < st->m_regCount; ++i) {
        uint16_t raw = 0;
        if (!XModbusHub_value(hub, HSF_TURB_AREA,
                              (uint16_t)(st->m_vbase + i), &raw))
            continue;
        XIoChannelGroup_feedRoleRefresh(st->m_group, (uint16_t)i, (int32_t)raw);
        fed = true;
    }
    return fed;
}

/**
 * @brief 周期采样拍（定时器槽）。
 * @details 浊度恒定时 Hub「变化才发」，一帧信号都不发。若只靠信号驱动，设备层
 *          会因「太久没有新拍」而判陈旧。本拍按固定节拍从 Hub 缓存重取当前值
 *          ——值不变的拍被组内静默吞掉（不产生无谓信号），但设备层的「最近
 *          一拍时刻」被刷新。
 */
static void VHsfServerIoTurbidity_timerEvent(XObject* obj, XTimerEvent* event)
{
    HsfServerIoTurbidity* st = (HsfServerIoTurbidity*)obj;
    if (!st || !event) return;
    XEvent_accept(event);
    if (XTimerEvent_timerId(event) == st->m_sampleTimer) {
        (void)turbSampleFromHub(st);
        /* [温度首拍一次性日志] 带温度的机型（站 2）解出第一拍温度时记一条，
         * 供现场核对"浊度+温度同刻"确实在工作；只记一次（m_tempLogged），
         * 周期性遥测值不进日志——日志页要能按页翻，不能被采样刷屏。 */
        if (!st->m_tempLogged && st->m_dev) {
            double c = 0.0;
            if (XTurbidityMeter_temperature(st->m_dev, &c)) {
                st->m_tempLogged = true;
                XPrintf("HsfServerIoTurbidity: 浊度仪%d 温度首拍 %.2f℃"
                        "（0x0000×4 同刻快照）\n", st->m_index + 1, c);
            }
        }
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
 */
static void VHsfServerIoTurbidity_onHubValueChanged(XObject* receiver,
                                                    XVarList* args)
{
    HsfServerIoTurbidity* st;
    int typeInt;
    int vStart;
    int count;

    if (!receiver || !args) return;
    st = (HsfServerIoTurbidity*)receiver;
    if (!st->m_bus || !st->m_group) return;

    /* 宏展开为三条独立声明，变量名不能与形参撞名。 */
    XVarList_args_3(args, int, regType, int, start, int, cnt);
    typeInt = regType;
    vStart = start;
    count = cnt;
    if (typeInt != (int)HSF_TURB_AREA) return;
    if (count <= 0) return;

    /* 区间与 [vbase, vbase+regCount) 不相交则忽略。 */
    if (vStart < 0) return;
    if ((uint32_t)vStart >= (uint32_t)st->m_vbase + (uint32_t)st->m_regCount) return;
    if ((uint32_t)vStart + (uint32_t)count <= (uint32_t)st->m_vbase) return;

    (void)turbSampleFromHub(st);
}

/* ==================== 生命周期 ==================== */

/**
 * @brief 建一个模拟量输入点并登记进组（失败自行清理）。
 * @param group 目标通道组。
 * @param role 语义槽号 = 寄存器在站内的偏移。
 * @return 成功 true。
 * @note **标度必须恒等（1/1/0）且无符号**：这些字是 32 位量的原始位模式，
 *       任何标度运算都会破坏 IEEE754。标度换算由设备层在拼成 float 之后做。
 */
static bool turbMakePoint(XIoChannelGroup* group, uint16_t role)
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
    XIoPoint_setTag(pt, "TURB");
    XIoPoint_setUseHolding(pt, true);         /* 保持寄存器（FC03）。 */
    XIoPoint_setSigned(pt, false);            /* 32 位量的字按无符号位模式处理。 */
    XIoAnalogPoint_setScale(apt, 1, 1, 0, 0); /* 恒等：不碰位模式。 */
    if (!XIoChannelGroup_addPoint(group, pt, true)) {
        XClassDelete((XClass*)apt);
        return false;
    }
    return true;
}

static void VHsfServerIoTurbidity_deinit(XObject* obj);

XVtable* HsfServerIoTurbidity_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfServerIoTurbidity)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VHsfServerIoTurbidity_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VHsfServerIoTurbidity_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(HsfServerIoTurbidity);
    return XVTABLE_DEFAULT;
}

void HsfServerIoTurbidity_init(HsfServerIoTurbidity* st)
{
    if (!st) return;
    memset(st, 0, sizeof(*st));
    XObject_init((XObject*)st);
    XClassGetVtable(st) = HsfServerIoTurbidity_class_init();
    st->m_bus = NULL;
    st->m_group = NULL;
    st->m_dev = NULL;
    st->m_map = NULL;
    st->m_conn = NULL;
    st->m_sampleTimer = XTIMER_INVALID_ID;
    st->m_unitId = -1;
    st->m_index = -1;
    st->m_vbase = 0;
    st->m_regCount = 0;
    st->m_tempLogged = false;
    st->m_mounted = false;
}

HsfServerIoTurbidity* HsfServerIoTurbidity_create(void)
{
    HsfServerIoTurbidity* st = (HsfServerIoTurbidity*)XMemory_malloc(
        sizeof(HsfServerIoTurbidity), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!st) return NULL;
    HsfServerIoTurbidity_init(st);
    Set_Class_Memory(st, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(st, true);
    return st;
}

/* ==================== 装配 ==================== */

bool HsfServerIoTurbidity_mount(HsfServerIoTurbidity* st, HsfServerBus* bus,
                                XModbusMapping* map, int unitId,
                                int index, uint16_t vbase, uint32_t staleMs,
                                int regCount, int wordOrder, uint32_t txQuietMs)
{
    XModbusHub* hub;
    XModbusHubUnitLimits limits;
    int devId;
    int i;

    if (!st || st->m_mounted) return false;
    if (!bus || !map) return false;
    /* 寄存器数合法域 [2,4]：2=仅浊度（站 1）、4=浊度+温度同刻（站 2）。
     * 映射段/从站约束/点表全按它登记，乱值等于静默改写协议，早失败。 */
    if (regCount < 2 || regCount > 4) return false;
    hub = HsfServerBus_hub(bus);
    devId = HsfServerBus_deviceId(bus);
    if (!hub || devId < 0) {
        XPrintf("HsfServerIoTurbidity: 总线未就绪\n");
        return false;
    }

    st->m_bus = bus;
    st->m_map = map;
    st->m_unitId = unitId;
    st->m_index = index;
    st->m_vbase = vbase;
    st->m_regCount = regCount;
    st->m_tempLogged = false;

    /* ① 映射段：保持寄存器 vbase..vbase+regCount-1 ↔ 远端 0x0000 起同宽
     *    （FC03；站 2 的 ×4 是"浊度+温度同刻"口径，实测文档 §5.2）。 */
    if (!XModbusMapping_addRangeEx(map, HSF_TURB_AREA, vbase,
                                   regCount, devId, unitId,
                                   HSF_TURBIDITY_REMOTE_ADDR, 0, 0)) {
        XPrintf("HsfServerIoTurbidity: 映射段登记失败（站 %d v=0x%04X）\n",
                unitId, (unsigned)vbase);
        goto fail;
    }
    /* ② 从站协议约束（作用域=从站）：单帧读本站寄存器数 + 分级节拍 +
     *    发帧前总线静默期（站 1=200ms：流量计 49 字节长帧后的恢复期，
     *    ttyS4 实测 §8；站 2 免疫=0）。本站只读不写，无 singleWriteOnly
     *    需求之外的写约束。 */
    memset(&limits, 0, sizeof(limits));
    limits.m_maxReadRegisters = (uint16_t)regCount;
    limits.m_rangeBarrier = true;      /* 一帧不跨通信组边界。 */
    limits.m_singleWriteOnly = true;   /* 本站无写路径；保持与其它站一致。 */
    limits.m_minPollIntervalMs = HSF_TURBIDITY_MIN_POLL_MS;
    limits.m_preTxQuietMs = txQuietMs;
    if (!XModbusHub_setUnitLimits(hub, devId, unitId, &limits)) {
        XPrintf("HsfServerIoTurbidity: 从站约束登记失败（站 %d）\n", unitId);
        goto fail;
    }
    /* ③ 通道组 + regCount 个模拟量输入点（role 0..regCount-1 ↔ 寄存器偏移；
     *    role 0/1=浊度、role 2/3=温度（仅 regCount==4 的机型）。 */
    st->m_group = XIoChannelGroup_create();
    if (!st->m_group) goto fail;
    for (i = 0; i < regCount; ++i) {
        if (!turbMakePoint(st->m_group, (uint16_t)i)) {
            XPrintf("HsfServerIoTurbidity: 输入点装配失败（role %d）\n", i);
            goto fail;
        }
    }
    /* ④ 设备抽象（字序按台配置 + float32 解释）。
     *    [字序必须逐台配] 2026-11-09 实测：站 1=CDAB（手册）、站 2=DCBA
     *    （整字节反序）。字序错了不报错、只出假数（1.1e-5 这类），故在
     *    attach **之前**设置——attach 里的播种重算就会用对口径。 */
    st->m_dev = XTurbidityMeter_create();
    if (!st->m_dev) goto fail;
    XTurbidityMeter_setStaleMs(st->m_dev, staleMs);
    /* [站点层也记住阈值 2026-11-10] 质量判定走 Hub 块级新鲜度，必须比
     * **采集周期**而不是编译期基线（与温湿度/流量计站同款）。 */
    st->m_staleMs = staleMs;
    if (!XTurbidityMeter_setWordOrder(st->m_dev,
                                      (XTurbidityMeterWordOrder)wordOrder)) {
        XPrintf("HsfServerIoTurbidity: 字序配置非法（站 %d，order=%d）\n",
                unitId, wordOrder);
        goto fail;
    }
    if (!XTurbidityMeter_attach(st->m_dev, st->m_group)) {
        XPrintf("HsfServerIoTurbidity: 设备挂组失败（站 %d）\n", unitId);
        goto fail;
    }
    /* ⑤ 订阅 Hub 值变化（唯一一条；整段重喂在槽内完成）。
     *    **必须在 HsfServerBus_start 之前订阅**：Hub 的 valueChanged 是
     *    「变化才发」，浊度稳定时首拍之后不再发——订阅晚于首拍就拿不到初值。 */
    st->m_conn = XObject_connect((XObject*)hub,
                                   XSignal(XModbusHub_valueChanged_signal),
                                   (XObject*)st,
                                   VHsfServerIoTurbidity_onHubValueChanged,
                                   XConnectionType_Auto);
    if (!st->m_conn) {
        XPrintf("HsfServerIoTurbidity: Hub 信号订阅失败（站 %d）\n", unitId);
        goto fail;
    }
    /* ⑥ 采样定时器：稳定量的「拍」靠它刷新（见槽函数说明）。 */
    st->m_sampleTimer = XObject_startTimer((XObject*)st,
                                              (int)HSF_TURBIDITY_SAMPLE_MS,
                                              XTimerType_CoarseTimer);
    if (st->m_sampleTimer == XTIMER_INVALID_ID) {
        XPrintf("HsfServerIoTurbidity: 采样定时器启动失败（站 %d）\n", unitId);
        goto fail;
    }
    st->m_mounted = true;
    XPrintf("HsfServerIoTurbidity: 浊度仪%d 装配完成（站 %d，HR v=0x%04X × %d，"
            "远端 0x%04X，FC%02d，字序 %s，静默 %ums，陈旧 %ums）\n",
            index + 1, unitId, (unsigned)vbase,
            regCount,
            (unsigned)HSF_TURBIDITY_REMOTE_ADDR, HSF_TURBIDITY_FUNCTION,
            wordOrderString(wordOrder),
            (unsigned)txQuietMs,
            (unsigned)staleMs);
    return true;

fail:
    HsfServerIoTurbidity_unmount(st);
    return false;
}

void HsfServerIoTurbidity_unmount(HsfServerIoTurbidity* st)
{
    if (!st) return;
    /* ① 先停采样定时器（「触发≠注销」纪律：触发点不注销就泄漏槽位）。 */
    if (st->m_sampleTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)st, st->m_sampleTimer);
        st->m_sampleTimer = XTIMER_INVALID_ID;
    }
    /* ② 退订 Hub 信号（订阅挂在 Hub 上；Hub 须仍存活）。
     *    用 XObject_disconnect_2(连接句柄) 而不是按四元组反查——连接对象
     *    自己知道两端，Hub 若不在了也不会解引用悬垂指针（流量计站同款）。 */
    if (st->m_conn) {
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
    st->m_mounted = false;
}

static void VHsfServerIoTurbidity_deinit(XObject* obj)
{
    HsfServerIoTurbidity* st = (HsfServerIoTurbidity*)obj;
    if (!st) return;
    HsfServerIoTurbidity_unmount(st);
    XClass_Deinit_Parent(XObject, obj);
}

/* ==================== 查询 ==================== */

bool HsfServerIoTurbidity_ntu(const HsfServerIoTurbidity* st, double* outNtu)
{
    if (outNtu) *outNtu = 0.0;
    if (!st || !st->m_dev) return false;
    /* 质量口径取 Hub 块级新鲜度（见文件头说明），而不是设备层自算的陈旧。 */
    if (!turbStationQuality(st, st->m_staleMs)) {
        double v = 0.0;
        (void)XTurbidityMeter_value(st->m_dev, &v);
        if (outNtu) *outNtu = v;    /* 值仍带出（可能是最后一次有效值）。 */
        return false;
    }
    return XTurbidityMeter_value(st->m_dev, outNtu);
}

bool HsfServerIoTurbidity_tempC(const HsfServerIoTurbidity* st, double* outC)
{
    if (outC) *outC = 0.0;
    if (!st || !st->m_dev) return false;
    /* 与 ntu 同一道闸：Hub 块级新鲜度。机型无温度（role 2/3 未登记）时
     * 设备层恒 false，这里自然返回 false。 */
    if (!turbStationQuality(st, st->m_staleMs)) return false;
    return XTurbidityMeter_temperature(st->m_dev, outC);
}

bool HsfServerIoTurbidity_raw(const HsfServerIoTurbidity* st, uint32_t* outBits)
{
    if (outBits) *outBits = 0;
    if (!st || !st->m_dev) return false;
    return XTurbidityMeter_rawWords(st->m_dev, outBits);
}

bool HsfServerIoTurbidity_isOnline(const HsfServerIoTurbidity* st)
{
    return turbStationQuality(st, st->m_staleMs);
}

bool HsfServerIoTurbidity_isMounted(const HsfServerIoTurbidity* st)
{
    if (!st) return false;
    return st->m_mounted;
}

int HsfServerIoTurbidity_unitId(const HsfServerIoTurbidity* st)
{
    if (!st) return -1;
    return st->m_unitId;
}

int HsfServerIoTurbidity_index(const HsfServerIoTurbidity* st)
{
    if (!st) return -1;
    return st->m_index;
}

XIoChannelGroup* HsfServerIoTurbidity_group(const HsfServerIoTurbidity* st)
{
    if (!st) return NULL;
    return st->m_group;
}

XTurbidityMeter* HsfServerIoTurbidity_device(const HsfServerIoTurbidity* st)
{
    if (!st) return NULL;
    return st->m_dev;
}
