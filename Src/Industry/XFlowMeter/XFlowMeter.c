
/**
 * @file       XFlowMeter.c
 * @brief      电磁流量计设备抽象实现。
 * @details    与 XTempHumTransmitter.c / XDiffPressureGauge.c 同族：组信号槽经
 *             connect_1 订阅（receiver=设备，槽内按 role 过滤）、XObject 自带
 *             定时器做质量巡检（周期型，挂接期间常驻）、解挂与 deinit 共用内部
 *             路径。数据流：组 inputChanged(role, chEng) → 按帧重算快照 → 变化
 *             才发 valueChanged / alarmChanged；质量翻转沿发 qualityChanged。
 *
 *             [32 位拼装与「同帧一致」]
 *             瞬时流量/流速/百分比/电导比/正反向累积量都是 32 位量，占连续两个
 *             寄存器（高字在前）。拼装：
 *                 bits = ((uint32_t)hiRaw << 16) | loRaw;
 *             Float 用 memcpy 到 float（避免严格别名 UB），Long 直接当 uint32。
 *
 *             两个字**经 XIoLink_readRole_base 读原始值**，而不是各读一次点缓存：
 *             Hub 是一帧整块回填、整块发信号，故同一帧内所有寄存器是同时刻快照，
 *             两次 readRole 拿到的是同一帧；若改成「收到高字事件时读低字缓存」，
 *             块回填按地址升序，此刻低字可能还是上一帧的值（新高位+旧低位）。
 *             链路不可用时（如纯 mock 场景）回落到点缓存的工程值，此时要求该点
 *             标度为恒等（1/1/0）——这是装配约定，由测试与装配层保证。
 */
#include "XFlowMeter.h"

#if XINDUSTRY_ON && XFLOW_METER_ON

#include <string.h>
#include "XMemory.h"
#include "XDateTime.h"
#include "XChannel/XIoLink.h"

/* ==================== 内部辅助 ==================== */

#define XFLOWMETER_DEFAULT_STALE_MS 5000u
#define XFLOWMETER_QUALITY_PERIOD_MS 1000u   /**< 质量巡检周期（毫秒）。 */

/** @brief 订阅槽前向声明：解挂的断开坐标需要引用。 */
static void VXFlowMeter_onInputChanged(XObject* receiver, XVarList* args);

/**
 * @brief 量与「高字 role / 低字 role」的对应表（表驱动，避免散落魔数）。
 */
typedef struct XFlowMeterQuantityRole {
    XFlowMeterRole m_hiRole;   /**< 高字 role。 */
    XFlowMeterRole m_loRole;   /**< 低字 role（必须 = m_hiRole + 1）。 */
    bool m_isFloat;            /**< true=IEEE754 单精度；false=32 位无符号整数。 */
} XFlowMeterQuantityRole;

static const XFlowMeterQuantityRole s_quantityRoles[XFlowMeterQuantity_Count] = {
    /* 瞬时流量      */ { XFlowMeterRole_InstantFlowHi,  XFlowMeterRole_InstantFlowLo,  true  },
    /* 瞬时流速      */ { XFlowMeterRole_InstantSpeedHi, XFlowMeterRole_InstantSpeedLo, true  },
    /* 流量百分比    */ { XFlowMeterRole_FlowPercentHi,  XFlowMeterRole_FlowPercentLo,  true  },
    /* 流体电导比    */ { XFlowMeterRole_ConductivityHi, XFlowMeterRole_ConductivityLo, true  },
    /* 正向累积量    */ { XFlowMeterRole_ForwardIntHi,   XFlowMeterRole_ForwardIntLo,   false },
    /* 反向累积量    */ { XFlowMeterRole_ReverseIntHi,   XFlowMeterRole_ReverseIntLo,   false }
};

/**
 * @brief 读一个 role 的**原始值**（链路优先，回落点缓存）。
 * @param group 通道组；不能为 NULL。
 * @param point 该 role 的点；不能为 NULL。
 * @param outRaw 输出原始值。
 * @return 取到返回 true。
 * @note 链路可用时走 readRole——读的是 Hub 缓存，与同一帧内其它次读一致。
 *       链路不可用时回落点缓存的工程值（要求恒等标度，见文件头说明）。
 */
static bool flowMeterReadRaw(XIoChannelGroup* group, XIoPoint* point, int32_t* outRaw)
{
    XIoLink* link;
    int32_t raw = 0;

    if (!group || !point || !outRaw) return false;
    link = XIoChannelGroup_link(group);
    if (link && XIoLink_readRole_base(link, XIoPoint_role(point), &raw)) {
        *outRaw = raw;
        return true;
    }
    return XIoPoint_value(point, outRaw);
}

/**
 * @brief 从两个 16 位原始字拼出一个量的物理值。
 * @param self 流量计（提供点表）。
 * @param q 量号。
 * @param outValue 输出物理值。
 * @return 该量两个字都可读返回 true。
 * @note 高字在前、大端拼接（协议口径）。
 */
static bool flowMeterAssemble(const XFlowMeter* self, int q, double* outValue)
{
    const XFlowMeterQuantityRole* qr = &s_quantityRoles[q];
    int32_t hiRaw = 0;
    int32_t loRaw = 0;
    uint32_t bits;

    if (!self->m_group || !self->m_pairBound[q]) return false;
    if (!flowMeterReadRaw(self->m_group, self->m_points[qr->m_hiRole], &hiRaw))
        return false;
    if (!flowMeterReadRaw(self->m_group, self->m_points[qr->m_loRole], &loRaw))
        return false;

    bits = ((uint32_t)(uint16_t)hiRaw << 16) | (uint32_t)(uint16_t)loRaw;
    if (qr->m_isFloat) {
        float f;
        memcpy(&f, &bits, sizeof(f));
        *outValue = (double)f;
    } else {
        *outValue = (double)bits;
    }
    return true;
}

/**
 * @brief 读单位代码（0x1020 / 0x1021 单字 U16）。
 * @return 读到返回 true。
 */
static bool flowMeterReadUnit(XIoChannelGroup* group, XIoPoint* point, uint16_t* outCode)
{
    int32_t raw = 0;
    if (!flowMeterReadRaw(group, point, &raw)) return false;
    *outCode = (uint16_t)raw;
    return true;
}

/**
 * @brief 质量评估：已收到 && 未陈旧。
 */
static bool flowMeterEvaluateQuality(const XFlowMeter* self)
{
    uint64_t now;
    if (!self->m_group || !self->m_received) return false;
    if (self->m_staleMs == 0u) return true;
    now = XDateTime_currentMSecsSinceEpoch();
    return (now - self->m_lastChangeMs) <= (uint64_t)self->m_staleMs;
}

/**
 * @brief 质量发布：翻转沿发 qualityChanged（数据拍与巡检共用）。
 */
static void flowMeterPublishQuality(XFlowMeter* self)
{
    bool q = flowMeterEvaluateQuality(self);
    if (self->m_quality == q) return;
    self->m_quality = q;
    XFlowMeter_qualityChanged_signal(self, q);
}

/**
 * @brief 报警发布：位变化才发 alarmChanged。
 */
static void flowMeterPublishAlarms(XFlowMeter* self)
{
    if (self->m_publishedAlarms == self->m_alarmBits) return;
    self->m_publishedAlarms = self->m_alarmBits;
    XFlowMeter_alarmChanged_signal(self, (int)self->m_alarmBits);
}

/**
 * @brief 整拍重算：按「同帧快照」重算全部量/单位/报警，变化才发信号。
 * @note 任一 role 事件到达即触发整拍重算——因为一帧应答整块回填，
 *       任一寄存器的变化都意味着「新的一帧已就位」，此时读到的全部字同源。
 * @note [2026-11-04 性能] 值的比较与缓存更新在一趟里完成，**信号只在全部
 *       算完之后按「有变化才发」发射**：一次刷新最多 1 个 valueChanged
 *       （量号=-1 表示整帧快照，见 XFlowMeter_valueChanged_signal 说明），
 *       不再每量一条。原先每量一条在本设备的场景（一帧 22 个寄存器、
 *       一次块回填触发 22 次 role 事件 ⇒ 22×6 条信号）会把固定池的
 *       XVarList 打满，压测下连 XMultiPool 都耗尽。
 */
static void flowMeterRefresh(XFlowMeter* self)
{
    int q;
    int i;
    bool any = false;
    bool changed = false;

    if (!self->m_group) return;

    /* 物理量（32 位，成对拼装）。 */
    for (q = 0; q < (int)XFlowMeterQuantity_Count; ++q) {
        double v = 0.0;
        if (!flowMeterAssemble(self, q, &v)) continue;
        any = true;
        if (self->m_values[q] != v) {
            self->m_values[q] = v;
            changed = true;
        }
    }
    if (!any) return;

    /* 单字字段（单位/报警）与多字量共用同一张点表，按 role 直接取。 */
    {
        uint16_t code = 0;
        if (self->m_points[XFlowMeterRole_InstantFlowUnit] &&
            flowMeterReadUnit(self->m_group,
                              self->m_points[XFlowMeterRole_InstantFlowUnit], &code))
            self->m_flowUnitCode = code;
        if (self->m_points[XFlowMeterRole_TotalUnit] &&
            flowMeterReadUnit(self->m_group,
                              self->m_points[XFlowMeterRole_TotalUnit], &code))
            self->m_totalUnitCode = code;
    }

    /* 报警（四字 → 位掩码；1=报警）。位序与 XFlowMeterAlarm 一致。 */
    {
        static const XFlowMeterRole alarmRoles[4] = {
            XFlowMeterRole_UpperAlarm, XFlowMeterRole_LowerAlarm,
            XFlowMeterRole_EmptyAlarm, XFlowMeterRole_SystemAlarm
        };
        uint8_t bits = 0;
        for (i = 0; i < 4; ++i) {
            int32_t raw = 0;
            if (self->m_points[alarmRoles[i]] &&
                flowMeterReadRaw(self->m_group, self->m_points[alarmRoles[i]], &raw)) {
                if (raw != 0) bits |= (uint8_t)(1u << i);
            }
        }
        self->m_alarmBits = bits;
    }

    self->m_lastChangeMs = XDateTime_currentMSecsSinceEpoch();
    self->m_received = true;
    if (changed)
        XFlowMeter_valueChanged_signal(self, (int)XFlowMeterQuantity_Count, 0.0);
    flowMeterPublishAlarms(self);
    flowMeterPublishQuality(self);
}

/**
 * @brief 内部解挂（detach 与 deinit 共用）。
 */
static void flowMeterDetachInternal(XFlowMeter* self)
{
    int i;
    if (!self->m_group) return;
    if (self->m_signalAttached) {
        XObject_disconnect((XObject*)self->m_group,
                             XSignal(XIoChannelGroup_inputChanged_signal),
                             (XObject*)self, VXFlowMeter_onInputChanged);
        self->m_signalAttached = false;
    }
    for (i = 0; i < (int)XFlowMeterRole_Count; ++i) self->m_points[i] = NULL;
    for (i = 0; i < (int)XFlowMeterQuantity_Count; ++i) self->m_pairBound[i] = false;

    if (self->m_qualityTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_qualityTimer);
        self->m_qualityTimer = XTIMER_INVALID_ID;
    }
    /* 失去数据沿：质量转无效、报警清零（调用方可见的可见性收敛）。 */
    if (self->m_quality) {
        self->m_quality = false;
        XFlowMeter_qualityChanged_signal(self, false);
    }
    if (self->m_publishedAlarms != 0u) {
        self->m_publishedAlarms = 0u;
        self->m_alarmBits = 0u;
        XFlowMeter_alarmChanged_signal(self, 0);
    } else {
        self->m_alarmBits = 0u;
    }
    self->m_group = NULL;
}

/**
 * @brief 质量巡检定时器启动（失败仅报错，不回滚装配）。
 */
static void flowMeterStartQualityTimer(XFlowMeter* self)
{
    if (self->m_qualityTimer != XTIMER_INVALID_ID) return;
    self->m_qualityTimer = XObject_startTimer((XObject*)self,
                                                 XFLOWMETER_QUALITY_PERIOD_MS,
                                                 XTimerType_CoarseTimer);
    if (self->m_qualityTimer == XTIMER_INVALID_ID) {
        /* 巡检资源启动失败：陈旧翻转将无法主动发布，按可见性侧报错 */
        XFlowMeter_errorOccurred_signal(self, XFlowMeterError_Internal);
    }
}

/* ==================== 组信号槽（经 connect_1 连接，receiver=设备） ==================== */

/**
 * @brief 组输入变化槽：role 属于本设备才重算整拍。
 * @param receiver 连接的接收者（即本设备）；**不是**组，禁止直接强转。
 * @param args 信号参数（uint16_t role, int32_t chEng）。
 * @return 无。
 */
static void VXFlowMeter_onInputChanged(XObject* receiver, XVarList* args)
{
    XFlowMeter* self;

    if (!receiver || !args) return;
    self = (XFlowMeter*)receiver;
    XVarList_args_2(args, uint16_t, role, int32_t, chEng);
    (void)chEng;    /* 值本身不用：整拍重算会经链路读同帧全部字。 */
    if (role >= (uint16_t)XFlowMeterRole_Count) return;
    if (!self->m_points[role]) return;
    flowMeterRefresh(self);
}

/**
 * @brief 质量巡检拍（周期型；挂接期间常驻）：陈旧翻转沿发布。
 */
static void VXFlowMeter_timerEvent(XObject* obj, XTimerEvent* event)
{
    XFlowMeter* self = (XFlowMeter*)obj;
    XTimerId id;

    if (!self || !event) return;
    id = XTimerEvent_timerId(event);
    XEvent_accept(event);
    if (id == self->m_qualityTimer) {
        if (self->m_group) flowMeterPublishQuality(self);
        return;
    }
    /* 未知 id 交父类，不吞 */
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))((XObject*)self, event);
}

static void VXFlowMeter_deinit(XObject* obj)
{
    XFlowMeter* self = (XFlowMeter*)obj;

    if (!self) return;
    flowMeterDetachInternal(self);
    XClass_Deinit_Parent(XObject, obj);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XFlowMeter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFlowMeter)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXFlowMeter_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFlowMeter_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XFlowMeter_init(XFlowMeter* obj)
{
    int i;
    if (!obj) return;
    memset(obj, 0, sizeof(XFlowMeter));
    XObject_init((XObject*)obj);
    XClassSetVtable(obj, XFlowMeter);
    obj->m_group = NULL;
    for (i = 0; i < (int)XFlowMeterRole_Count; ++i) obj->m_points[i] = NULL;
    for (i = 0; i < (int)XFlowMeterQuantity_Count; ++i) {
        obj->m_pairBound[i] = false;
        obj->m_values[i] = 0.0;
    }
    obj->m_flowUnitCode = (uint16_t)XFlowMeterFlowUnit_Unknown;
    obj->m_totalUnitCode = (uint16_t)XFlowMeterTotalUnit_Unknown;
    obj->m_alarmBits = 0u;
    obj->m_received = false;
    obj->m_lastChangeMs = 0;
    obj->m_staleMs = XFLOWMETER_DEFAULT_STALE_MS;
    obj->m_signalAttached = false;
    obj->m_qualityTimer = XTIMER_INVALID_ID;
    obj->m_quality = false;
    obj->m_publishedAlarms = 0u;
}

XFlowMeter* XFlowMeter_create(void)
{
    XFlowMeter* obj = (XFlowMeter*)XMemory_calloc(
        1, sizeof(XFlowMeter), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XFlowMeter_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 装配 ==================== */

bool XFlowMeter_attach(XFlowMeter* self, XIoChannelGroup* group)
{
    XConnection* conn;
    int q;
    int i;
    bool anyPair = false;

    if (ISNULL(self, "XFlowMeter")) return false;
    if (!group) return false;
    if (self->m_group == group) return true;
    flowMeterDetachInternal(self);
    self->m_group = group;

    /* 按 role 取点：22 个量槽全部落在 role 表内（含单位与报警）。 */
    for (i = 0; i < (int)XFlowMeterRole_Count; ++i)
        self->m_points[i] = XIoChannelGroup_pointAt(group, (uint16_t)i);

    /* 多字量：两个 role 都在组内才算可解。 */
    for (q = 0; q < (int)XFlowMeterQuantity_Count; ++q) {
        const XFlowMeterQuantityRole* qr = &s_quantityRoles[q];
        self->m_pairBound[q] = (self->m_points[qr->m_hiRole] != NULL) &&
                               (self->m_points[qr->m_loRole] != NULL);
        if (self->m_pairBound[q]) anyPair = true;
    }
    if (!anyPair) {
        /* 一个完整的量都没有：这是装配错误，必须暴露而不是装作挂上了。 */
        flowMeterDetachInternal(self);
        return false;
    }

    conn = XObject_connect((XObject*)group,
                             XSignal(XIoChannelGroup_inputChanged_signal),
                             (XObject*)self, VXFlowMeter_onInputChanged,
                             XConnectionType_Auto);
    if (!conn) {
        flowMeterDetachInternal(self);
        return false;
    }
    self->m_signalAttached = true;
    flowMeterRefresh(self);             /* 播种：同步读一次落初始快照。 */
    flowMeterStartQualityTimer(self);   /* 巡检常驻；启动失败仅报错不回滚。 */
    return true;
}

bool XFlowMeter_detach(XFlowMeter* self)
{
    if (ISNULL(self, "XFlowMeter")) return false;
    if (!self->m_group) return false;
    flowMeterDetachInternal(self);
    return true;
}

/* ==================== 配置 ==================== */

bool XFlowMeter_setStaleMs(XFlowMeter* self, uint32_t staleMs)
{
    if (ISNULL(self, "XFlowMeter")) return false;
    self->m_staleMs = staleMs;
    return true;
}

uint32_t XFlowMeter_staleMs(const XFlowMeter* self)
{
    if (!self) return 0u;
    return self->m_staleMs;
}

/* ==================== 查询 ==================== */

bool XFlowMeter_value(const XFlowMeter* self, XFlowMeterQuantity quantity,
                      double* outValue)
{
    int q = (int)quantity;
    bool valid;
    if (outValue) *outValue = 0.0;
    if (!self || q < 0 || q >= (int)XFlowMeterQuantity_Count) return false;
    if (!self->m_pairBound[q]) return false;
    valid = flowMeterEvaluateQuality(self);
    if (valid && outValue) *outValue = self->m_values[q];
    return valid;
}

bool XFlowMeter_isValid(const XFlowMeter* self)
{
    if (!self) return false;
    return flowMeterEvaluateQuality(self);
}

bool XFlowMeter_lastUpdateMs(const XFlowMeter* self, uint64_t* outMs)
{
    if (outMs) *outMs = 0;
    if (!self || !self->m_received) return false;
    if (outMs) *outMs = self->m_lastChangeMs;
    return true;
}

XFlowMeterFlowUnit XFlowMeter_flowUnit(const XFlowMeter* self)
{
    if (!self || !self->m_points[XFlowMeterRole_InstantFlowUnit])
        return XFlowMeterFlowUnit_Unknown;
    if (self->m_flowUnitCode > (uint16_t)XFlowMeterFlowUnit_GPH)
        return XFlowMeterFlowUnit_Unknown;
    return (XFlowMeterFlowUnit)self->m_flowUnitCode;
}

XFlowMeterTotalUnit XFlowMeter_totalUnit(const XFlowMeter* self)
{
    if (!self || !self->m_points[XFlowMeterRole_TotalUnit])
        return XFlowMeterTotalUnit_Unknown;
    /* B 型/511 型为 0~3；C 型 0~11 用高两位表精度档，基础单位仍按低两位解释。 */
    if (self->m_totalUnitCode > (uint16_t)XFlowMeterTotalUnit_USG) {
        uint16_t base = (uint16_t)(self->m_totalUnitCode % 4u);
        return (XFlowMeterTotalUnit)base;
    }
    return (XFlowMeterTotalUnit)self->m_totalUnitCode;
}

void XFlowMeter_unitCodes(const XFlowMeter* self, uint16_t* outFlowCode,
                          uint16_t* outTotalCode)
{
    if (outFlowCode) *outFlowCode = 0;
    if (outTotalCode) *outTotalCode = 0;
    if (!self) return;
    if (outFlowCode) *outFlowCode = self->m_flowUnitCode;
    if (outTotalCode) *outTotalCode = self->m_totalUnitCode;
}

uint8_t XFlowMeter_alarms(const XFlowMeter* self)
{
    if (!self) return 0u;
    return self->m_alarmBits;
}

/* ==================== 信号 ==================== */

/**
 * @brief 发射一个已建参数列表的信号（统一所有权收口）。
 * @details [2026-11-04 实锅] 不能照抄 XEmitSignal 宏的写法：宏把
 *          XVarList_Create(...) 作为**实参**传给有 `m_signalSlot` 守卫的
 *          if，未连接时实参根本不求值、不分配；本类若先显式建列表再传，
 *          无接收者时列表就无人释放（泄漏压测 300 轮即回不了基线）。
 *          故此处统一：无信号槽则就地释放，有则交给 emit。
 */
static void flowMeterEmit(XFlowMeter* self, size_t signal, XVarList* args)
{
    if (!args) return;
    if (!self || !((XObject*)self)->m_signalSlot) {
        XVarList_delete(args);
        return;
    }
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

void* XFlowMeter_valueChanged_signal(XFlowMeter* self, int quantity, double value)
{
    int q = quantity;
    double v = value;
    flowMeterEmit(self, (size_t)XFlowMeter_valueChanged_signal,
                  XVarList_Create(XVar(int, q), XVar(double, v)));
    return (void*)(size_t)XFlowMeter_valueChanged_signal;
}

void* XFlowMeter_qualityChanged_signal(XFlowMeter* self, bool valid)
{
    bool v = valid;
    flowMeterEmit(self, (size_t)XFlowMeter_qualityChanged_signal,
                  XVarList_Create(XVar(bool, v)));
    return (void*)(size_t)XFlowMeter_qualityChanged_signal;
}

void* XFlowMeter_alarmChanged_signal(XFlowMeter* self, int bits)
{
    int b = bits;
    flowMeterEmit(self, (size_t)XFlowMeter_alarmChanged_signal,
                  XVarList_Create(XVar(int, b)));
    return (void*)(size_t)XFlowMeter_alarmChanged_signal;
}

void* XFlowMeter_errorOccurred_signal(XFlowMeter* self, int error)
{
    int e = error;
    flowMeterEmit(self, (size_t)XFlowMeter_errorOccurred_signal,
                  XVarList_Create(XVar(int, e)));
    return (void*)(size_t)XFlowMeter_errorOccurred_signal;
}

#endif /* XINDUSTRY_ON && XFLOW_METER_ON */
