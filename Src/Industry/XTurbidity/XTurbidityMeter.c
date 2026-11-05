/**
 * @file       XTurbidityMeter.c
 * @brief      浊度仪设备抽象实现。
 * @details    与 XFlowMeter.c 同族：组信号槽经 connect_1 订阅（receiver=设备，
 *             槽内按 role 过滤）、XObject 自带定时器做质量巡检（周期型，
 *             挂接期间常驻）、解挂与 deinit 共用内部路径。数据流：
 *             组 inputChanged(role, chEng) → 整拍重算 → 变化才发 valueChanged；
 *             质量翻转沿发 qualityChanged。
 *
 *             [Words 模式为什么读原始字而不是读点工程值]
 *             32 位量由相邻两字拼成。若各读一次点缓存的**工程值**，会经过
 *             两次点标度换算——点标度是给单个 16 位物理量设计的（如 mV→μA），
 *             对"位模式的一半"没有任何意义，任何非恒等标度都会毁掉 IEEE754。
 *             故本模式经 XIoLink_readRole_base 读**原始字**（读 Hub 缓存，
 *             一帧整块回填 ⇒ 两次读同源），点标度在这里被刻意绕过。
 *             链路不可用时（纯 mock 场景）回落点缓存的工程值，此时要求该点
 *             标度恒等（1/1/0）——装配约定，由测试与装配层保证。
 *
 *             [Analog 模式为什么读工程值]
 *             模拟量通道只有一路，点内标度（原始 ADC 码 → 工程量）正是这一级
 *             该做的事，绕过它反而错。本模式经 XIoChannelGroup_readAnalogInput
 *             读工程值，再做第二级标度（工程量 → 0.01NTU 定点）。
 *
 *             [字序/格式/除数为何是运行期配置]
 *             实测真机 WZT-701A 是 CDAB + Float32（见头文件真机帧），而同一
 *             现场的另一台表完全可能是 ABCD + UInt32/100。写死任一种都会让
 *             另一种读数变成错误值且**不报错**（float 解释整数只是天文数字，
 *             整数解释 float 只是莫名其妙的大数）。故三个口径都做成配置项，
 *             缺省值取实测口径；换表只改配置。
 */
#include "XTurbidityMeter.h"

#if XINDUSTRY_ON && XTURBIDITY_ON

#include <string.h>
#include "XMemory.h"
#include "XDateTime.h"
#include "XChannel/XIoLink.h"

/* ==================== 内部常量 ==================== */

#define XTURBIDITY_DEFAULT_STALE_MS     5000u
#define XTURBIDITY_QUALITY_PERIOD_MS    1000u  /**< 质量巡检周期（毫秒）。 */
#define XTURBIDITY_DEFAULT_WORD_DIVISOR 100.0  /**< 整数格式缺省除数（两位小数）。 */

/** @brief 订阅槽前向声明：解挂的断开坐标需要引用。 */
static void VXTurbidityMeter_onInputChanged(XObject* receiver, XVarList* args);

/* ==================== 原始值读取 ==================== */

/**
 * @brief 读一个 role 的**原始值**（链路优先，回落点缓存）。
 * @param group 通道组；不能为 NULL。
 * @param point 该 role 的点；不能为 NULL。
 * @param outRaw 输出原始值。
 * @return 取到返回 true。
 * @note 链路可用时走 readRole——读的是 Hub 缓存快照，与同一帧内其它次读一致。
 *       链路不可用时回落点缓存的工程值（要求恒等标度，见文件头说明）。
 */
static bool turbidityReadRaw(XIoChannelGroup* group, XIoPoint* point, int32_t* outRaw)
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

/* ==================== 32 位量解码 ==================== */

/**
 * @brief 按配置把两个 16 位字拼成 32 位位模式。
 * @param self 浊度仪（提供字序）。
 * @param w0 首字原始值；w1 次字原始值。
 * @return 32 位位模式。
 * @note CDAB（缺省）= 低字在前：w0 是低 16 位、w1 是高 16 位。
 *       ABCD = 高字在前：w0 是高 16 位。
 */
static uint32_t turbidityPackBits(const XTurbidityMeter* self, uint16_t w0, uint16_t w1)
{
    if (self->m_wordOrder == XTurbidityMeterWordOrder_HighLow)
        return ((uint32_t)w0 << 16) | (uint32_t)w1;
    return ((uint32_t)w1 << 16) | (uint32_t)w0;
}

/**
 * @brief 按配置把 32 位位模式解释成 NTU。
 * @param self 浊度仪（提供格式与除数）。
 * @param bits 32 位位模式。
 * @return NTU。
 * @note Float32 用 memcpy 取位模式（严格别名安全）；整数格式按除数换算。
 */
static double turbidityDecodeNtu(const XTurbidityMeter* self, uint32_t bits)
{
    switch (self->m_wordFormat) {
    case XTurbidityMeterWordFormat_Float32: {
        float f;
        memcpy(&f, &bits, sizeof(f));
        return (double)f;
    }
    case XTurbidityMeterWordFormat_Int32Scaled: {
        int32_t v;
        double d;
        memcpy(&v, &bits, sizeof(v));
        d = self->m_wordDivisor;
        if (d == 0.0) return 0.0;
        return (double)v / d;
    }
    case XTurbidityMeterWordFormat_UInt32Scaled:
    default: {
        double d = self->m_wordDivisor;
        if (d == 0.0) return 0.0;
        return (double)bits / d;
    }
    }
}

/**
 * @brief 解码一次 Words 模式的浊度。
 * @param self 浊度仪。
 * @param outNtu 输出 NTU。
 * @return 两个字都可读返回 true。
 * @note 两个字**经 turbidityReadRaw 读原始值**，而不是各读一次点缓存：
 *       Hub 是一帧整块回填、整块发信号，故同一帧内两次读同源。若改成
 *       「收到首字事件时读次字缓存」，块回填按地址升序，此刻次字可能仍是
 *       上一帧的值（新首字 + 旧次字），拼出来是错的中间态。
 */
static bool turbidityDecodeWords(XTurbidityMeter* self, double* outNtu)
{
    int32_t raw0 = 0;
    int32_t raw1 = 0;
    uint32_t bits;

    if (!self->m_group) return false;
    if (!self->m_points[XTurbidityMeterRole_Word0] ||
        !self->m_points[XTurbidityMeterRole_Word1])
        return false;
    if (!turbidityReadRaw(self->m_group, self->m_points[XTurbidityMeterRole_Word0], &raw0))
        return false;
    if (!turbidityReadRaw(self->m_group, self->m_points[XTurbidityMeterRole_Word1], &raw1))
        return false;

    bits = turbidityPackBits(self, (uint16_t)raw0, (uint16_t)raw1);
    self->m_rawBits = bits;
    *outNtu = turbidityDecodeNtu(self, bits);
    return true;
}

/**
 * @brief 二级标度换算（Analog 模式）：fixed01 = chEng * num / den + offset。
 * @param self 浊度仪（提供标度）。
 * @param chEng 通道工程值。
 * @return 0.01NTU 定点值。
 * @note int64 中间量防溢出（与 XDiffPressureGauge 同款）。
 */
static int32_t turbidityApplyScale(const XTurbidityMeter* self, int32_t chEng)
{
    int64_t v = (int64_t)chEng * (int64_t)self->m_scaleNum;
    if (self->m_scaleDen != 0)
        v /= (int64_t)self->m_scaleDen;
    v += (int64_t)self->m_scaleOffset;
    return (int32_t)v;
}

/**
 * @brief 解码一次 Analog 模式的浊度。
 * @param self 浊度仪。
 * @param outNtu 输出 NTU。
 * @return 通道有有效数据返回 true。
 */
static bool turbidityDecodeAnalog(XTurbidityMeter* self, double* outNtu)
{
    int32_t chEng = 0;

    if (!self->m_group || !self->m_points[XTurbidityMeterRole_Word0]) return false;
    if (!XIoChannelGroup_readAnalogInput(
            self->m_group, XIoPoint_role(self->m_points[XTurbidityMeterRole_Word0]), &chEng))
        return false;
    self->m_analogEng = chEng;
    *outNtu = (double)turbidityApplyScale(self, chEng) / 100.0;
    return true;
}

/* ==================== 质量 ==================== */

/**
 * @brief 质量评估：已收到 && 未陈旧。
 */
static bool turbidityEvaluateQuality(const XTurbidityMeter* self)
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
static void turbidityPublishQuality(XTurbidityMeter* self)
{
    bool q = turbidityEvaluateQuality(self);
    if (self->m_quality == q) return;
    self->m_quality = q;
    XTurbidityMeter_qualityChanged_signal(self, q);
}

/**
 * @brief 整拍重算：按当前形态解一次浊度，变化才发 valueChanged。
 * @param self 浊度仪。
 * @note 任一 role 事件到达即触发整拍重算——一帧应答整块回填，任一字的
 *       变化都意味着"新的一帧已就位"，此时读到的全部字同源。
 */
static void turbidityRefresh(XTurbidityMeter* self)
{
    double ntu = 0.0;
    bool got;

    if (!self->m_group) return;
    if (self->m_source == XTurbidityMeterSource_Analog)
        got = turbidityDecodeAnalog(self, &ntu);
    else
        got = turbidityDecodeWords(self, &ntu);
    if (!got) return;

    self->m_lastChangeMs = XDateTime_currentMSecsSinceEpoch();
    self->m_received = true;
    if (self->m_ntu != ntu) {
        self->m_ntu = ntu;
        XTurbidityMeter_valueChanged_signal(self, ntu);
    }
    turbidityPublishQuality(self);
}

/* ==================== 生命周期 ==================== */

/**
 * @brief 内部解挂（detach 与 deinit 共用）。
 */
static void turbidityDetachInternal(XTurbidityMeter* self)
{
    int i;
    if (!self->m_group) return;
    if (self->m_signalAttached) {
        XObject_disconnect_1((XObject*)self->m_group,
                             XSignal(XIoChannelGroup_inputChanged_signal),
                             (XObject*)self, VXTurbidityMeter_onInputChanged);
        self->m_signalAttached = false;
    }
    for (i = 0; i < (int)XTurbidityMeterRole_Count; ++i) self->m_points[i] = NULL;

    if (self->m_qualityTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_qualityTimer);
        self->m_qualityTimer = XTIMER_INVALID_ID;
    }
    /* 失去数据沿：质量转无效（调用方可见的可见性收敛）。 */
    if (self->m_quality) {
        self->m_quality = false;
        XTurbidityMeter_qualityChanged_signal(self, false);
    }
    self->m_group = NULL;
}

/**
 * @brief 质量巡检定时器启动（失败仅报错，不回滚装配）。
 */
static void turbidityStartQualityTimer(XTurbidityMeter* self)
{
    if (self->m_qualityTimer != XTIMER_INVALID_ID) return;
    self->m_qualityTimer = XObject_startTimer_ms((XObject*)self,
                                                 XTURBIDITY_QUALITY_PERIOD_MS,
                                                 XTimerType_CoarseTimer);
    if (self->m_qualityTimer == XTIMER_INVALID_ID) {
        /* 巡检资源启动失败：陈旧翻转将无法主动发布，按可见性侧报错 */
        XTurbidityMeter_errorOccurred_signal(self, XTurbidityMeterError_Internal);
    }
}

/* ==================== 组信号槽 ==================== */

/**
 * @brief 组输入变化槽：role 属于本设备才重算整拍。
 * @param receiver 连接的接收者（即本设备）；**不是**组，禁止直接强转。
 * @param args 信号参数（uint16_t role, int32_t chEng）。
 * @return 无。
 */
static void VXTurbidityMeter_onInputChanged(XObject* receiver, XVarList* args)
{
    XTurbidityMeter* self;
    bool relevant;

    if (!receiver || !args) return;
    self = (XTurbidityMeter*)receiver;
    XVarList_args_2(args, uint16_t, role, int32_t, chEng);
    (void)chEng;    /* 值本身不用：整拍重算会按形态重新取值。 */
    if (role >= (uint16_t)XTurbidityMeterRole_Count) return;

    /* 只看本形态需要的 role：Analog 只关心 role 0；Words 关心 0 和 1。
     * [为什么要过滤] 同一组里可能还有别的设备登记的点（多设备共用一组），
     * 不过滤会让他人的数据拍触发本设备的整拍重算——功能上无害但会把
     * "最近一拍"刷新成不属于本设备的时刻，掩盖真实的陈旧。 */
    if (self->m_source == XTurbidityMeterSource_Analog)
        relevant = (role == (uint16_t)XTurbidityMeterRole_Word0);
    else
        relevant = (role == (uint16_t)XTurbidityMeterRole_Word0 ||
                    role == (uint16_t)XTurbidityMeterRole_Word1);
    if (!relevant) return;
    if (!self->m_points[role]) return;
    turbidityRefresh(self);
}

/**
 * @brief 质量巡检拍（周期型；挂接期间常驻）：陈旧翻转沿发布。
 */
static void VXTurbidityMeter_timerEvent(XObject* obj, XTimerEvent* event)
{
    XTurbidityMeter* self = (XTurbidityMeter*)obj;
    XTimerId id;

    if (!self || !event) return;
    id = XTimerEvent_timerId(event);
    XEvent_accept(event);
    if (id == self->m_qualityTimer) {
        if (self->m_group) turbidityPublishQuality(self);
        return;
    }
    /* 未知 id 交父类，不吞 */
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))((XObject*)self, event);
}

static void VXTurbidityMeter_deinit(XObject* obj)
{
    XTurbidityMeter* self = (XTurbidityMeter*)obj;

    if (!self) return;
    turbidityDetachInternal(self);
    XClass_Deinit_Parent(XObject, obj);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XTurbidityMeter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTurbidityMeter)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXTurbidityMeter_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTurbidityMeter_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XTurbidityMeter_init(XTurbidityMeter* obj)
{
    int i;
    if (!obj) return;
    memset(obj, 0, sizeof(XTurbidityMeter));
    XObject_init((XObject*)obj);
    XClassSetVtable(obj, XTurbidityMeter);
    obj->m_group = NULL;
    for (i = 0; i < (int)XTurbidityMeterRole_Count; ++i) obj->m_points[i] = NULL;
    /* 缺省口径 = 实测真机（WZT-701A：CDAB + Float32）。 */
    obj->m_source = XTurbidityMeterSource_Words;
    obj->m_wordOrder = XTurbidityMeterWordOrder_LowHigh;
    obj->m_wordFormat = XTurbidityMeterWordFormat_Float32;
    obj->m_wordDivisor = XTURBIDITY_DEFAULT_WORD_DIVISOR;
    obj->m_scaleNum = 1;
    obj->m_scaleDen = 1;
    obj->m_scaleOffset = 0;
    obj->m_ntu = 0.0;
    obj->m_rawBits = 0u;
    obj->m_analogEng = 0;
    obj->m_received = false;
    obj->m_lastChangeMs = 0;
    obj->m_staleMs = XTURBIDITY_DEFAULT_STALE_MS;
    obj->m_signalAttached = false;
    obj->m_qualityTimer = XTIMER_INVALID_ID;
    obj->m_quality = false;
}

XTurbidityMeter* XTurbidityMeter_create(void)
{
    XTurbidityMeter* obj = (XTurbidityMeter*)XMemory_calloc(
        1, sizeof(XTurbidityMeter), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XTurbidityMeter_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 装配 ==================== */

bool XTurbidityMeter_attach(XTurbidityMeter* self, XIoChannelGroup* group)
{
    XConnection* conn;
    int i;

    if (ISNULL(self, "XTurbidityMeter")) return false;
    if (!group) return false;
    if (self->m_group == group) return true;
    turbidityDetachInternal(self);
    self->m_group = group;

    /* 按 role 取点：两种形态都从 role 0 起（Words 还要 role 1）。 */
    for (i = 0; i < (int)XTurbidityMeterRole_Count; ++i)
        self->m_points[i] = XIoChannelGroup_pointAt(group, (uint16_t)i);

    /* 所需 role 必须齐全——缺一个就解不出量，装配错误必须尽早暴露，
     * 不能静默退化成"读到 0 且质量有效"。 */
    if (!self->m_points[XTurbidityMeterRole_Word0]) {
        self->m_group = NULL;
        return false;
    }
    if (self->m_source != XTurbidityMeterSource_Analog &&
        !self->m_points[XTurbidityMeterRole_Word1]) {
        self->m_points[XTurbidityMeterRole_Word0] = NULL;
        self->m_group = NULL;
        return false;
    }

    conn = XObject_connect_1((XObject*)group,
                             XSignal(XIoChannelGroup_inputChanged_signal),
                             (XObject*)self, VXTurbidityMeter_onInputChanged,
                             XConnectionType_Auto);
    if (!conn) {
        turbidityDetachInternal(self);
        return false;
    }
    self->m_signalAttached = true;
    turbidityRefresh(self);             /* 播种：同步读一次落初始快照。 */
    turbidityStartQualityTimer(self);   /* 巡检常驻；启动失败仅报错不回滚。 */
    return true;
}

bool XTurbidityMeter_detach(XTurbidityMeter* self)
{
    if (ISNULL(self, "XTurbidityMeter")) return false;
    if (!self->m_group) return false;
    turbidityDetachInternal(self);
    return true;
}

/* ==================== 配置 ==================== */

bool XTurbidityMeter_setSourceMode(XTurbidityMeter* self, XTurbidityMeterSourceMode mode)
{
    if (ISNULL(self, "XTurbidityMeter")) return false;
    if (mode != XTurbidityMeterSource_Words && mode != XTurbidityMeterSource_Analog)
        return false;
    /* 挂接期间换形态会让点表与新模式不匹配（查出来恒 0 且不报错），
     * 故要求先 detach——宁可让调用方多做一步，也不留静默读错的配置口。 */
    if (self->m_group) {
        XTurbidityMeter_errorOccurred_signal(self, XTurbidityMeterError_BadConfig);
        return false;
    }
    self->m_source = mode;
    return true;
}

XTurbidityMeterSourceMode XTurbidityMeter_sourceMode(const XTurbidityMeter* self)
{
    if (!self) return XTurbidityMeterSource_Words;
    return self->m_source;
}

bool XTurbidityMeter_setWordOrder(XTurbidityMeter* self, XTurbidityMeterWordOrder order)
{
    if (ISNULL(self, "XTurbidityMeter")) return false;
    if (order != XTurbidityMeterWordOrder_LowHigh &&
        order != XTurbidityMeterWordOrder_HighLow)
        return false;
    self->m_wordOrder = order;
    if (self->m_group) turbidityRefresh(self);   /* 重算并（值变时）发信号。 */
    return true;
}

XTurbidityMeterWordOrder XTurbidityMeter_wordOrder(const XTurbidityMeter* self)
{
    if (!self) return XTurbidityMeterWordOrder_LowHigh;
    return self->m_wordOrder;
}

bool XTurbidityMeter_setWordFormat(XTurbidityMeter* self, XTurbidityMeterWordFormat format)
{
    if (ISNULL(self, "XTurbidityMeter")) return false;
    if (format != XTurbidityMeterWordFormat_Float32 &&
        format != XTurbidityMeterWordFormat_UInt32Scaled &&
        format != XTurbidityMeterWordFormat_Int32Scaled)
        return false;
    self->m_wordFormat = format;
    if (self->m_group) turbidityRefresh(self);
    return true;
}

XTurbidityMeterWordFormat XTurbidityMeter_wordFormat(const XTurbidityMeter* self)
{
    if (!self) return XTurbidityMeterWordFormat_Float32;
    return self->m_wordFormat;
}

bool XTurbidityMeter_setWordDivisor(XTurbidityMeter* self, double divisor)
{
    if (ISNULL(self, "XTurbidityMeter")) return false;
    if (!(divisor > 0.0)) {     /* 同时挡住 0、负数与 NaN。 */
        XTurbidityMeter_errorOccurred_signal(self, XTurbidityMeterError_BadConfig);
        return false;
    }
    self->m_wordDivisor = divisor;
    if (self->m_group) turbidityRefresh(self);
    return true;
}

double XTurbidityMeter_wordDivisor(const XTurbidityMeter* self)
{
    if (!self) return 0.0;
    return self->m_wordDivisor;
}

bool XTurbidityMeter_setScale(XTurbidityMeter* self, int32_t num, int32_t den,
                              int32_t offset)
{
    if (ISNULL(self, "XTurbidityMeter")) return false;
    if (den == 0) {
        XTurbidityMeter_errorOccurred_signal(self, XTurbidityMeterError_BadConfig);
        return false;
    }
    self->m_scaleNum = num;
    self->m_scaleDen = den;
    self->m_scaleOffset = offset;
    if (self->m_group) turbidityRefresh(self);
    return true;
}

void XTurbidityMeter_scale(const XTurbidityMeter* self, int32_t* outNum,
                           int32_t* outDen, int32_t* outOffset)
{
    if (outNum) *outNum = 0;
    if (outDen) *outDen = 0;
    if (outOffset) *outOffset = 0;
    if (!self) return;
    if (outNum) *outNum = self->m_scaleNum;
    if (outDen) *outDen = self->m_scaleDen;
    if (outOffset) *outOffset = self->m_scaleOffset;
}

bool XTurbidityMeter_setStaleMs(XTurbidityMeter* self, uint32_t staleMs)
{
    if (ISNULL(self, "XTurbidityMeter")) return false;
    self->m_staleMs = staleMs;
    return true;
}

uint32_t XTurbidityMeter_staleMs(const XTurbidityMeter* self)
{
    if (!self) return 0u;
    return self->m_staleMs;
}

/* ==================== 查询 ==================== */

bool XTurbidityMeter_value(const XTurbidityMeter* self, double* outNtu)
{
    bool valid;
    if (outNtu) *outNtu = 0.0;
    if (!self || !self->m_group) return false;
    valid = turbidityEvaluateQuality(self);
    if (valid && outNtu) *outNtu = self->m_ntu;
    return valid;
}

bool XTurbidityMeter_isValid(const XTurbidityMeter* self)
{
    if (!self) return false;
    return turbidityEvaluateQuality(self);
}

bool XTurbidityMeter_lastUpdateMs(const XTurbidityMeter* self, uint64_t* outMs)
{
    if (outMs) *outMs = 0;
    if (!self || !self->m_received) return false;
    if (outMs) *outMs = self->m_lastChangeMs;
    return true;
}

bool XTurbidityMeter_rawWords(const XTurbidityMeter* self, uint32_t* outBits)
{
    if (outBits) *outBits = 0;
    if (!self || self->m_source != XTurbidityMeterSource_Words) return false;
    if (!self->m_received) return false;
    if (outBits) *outBits = self->m_rawBits;
    return true;
}

bool XTurbidityMeter_rawAnalog(const XTurbidityMeter* self, int32_t* outEng)
{
    if (outEng) *outEng = 0;
    if (!self || self->m_source != XTurbidityMeterSource_Analog) return false;
    if (!self->m_received) return false;
    if (outEng) *outEng = self->m_analogEng;
    return true;
}

/* ==================== 信号 ==================== */

/**
 * @brief 发射一个已建参数列表的信号（统一所有权收口）。
 * @details 与 XFlowMeter 同款：不能照抄 XEmitSignal 宏的写法（宏把
 *          XVarList_Create 作为实参传给有 m_signalSlot 守卫的 if，未连接时
 *          根本不去求值、不分配）；本类若先显式建列表再传，无接收者时列表
 *          就无人释放。故此处统一：无信号槽则就地释放，有则交给 emit。
 */
static void turbidityEmit(XTurbidityMeter* self, size_t signal, XVarList* args)
{
    if (!args) return;
    if (!self || !((XObject*)self)->m_signalSlot) {
        XVarList_delete(args);
        return;
    }
    XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                       XEVENT_PRIORITY_NORMAL);
}

void* XTurbidityMeter_valueChanged_signal(XTurbidityMeter* self, double ntu)
{
    double v = ntu;
    turbidityEmit(self, (size_t)XTurbidityMeter_valueChanged_signal,
                  XVarList_Create(XVar(double, v)));
    return (void*)(size_t)XTurbidityMeter_valueChanged_signal;
}

void* XTurbidityMeter_qualityChanged_signal(XTurbidityMeter* self, bool valid)
{
    bool v = valid;
    turbidityEmit(self, (size_t)XTurbidityMeter_qualityChanged_signal,
                  XVarList_Create(XVar(bool, v)));
    return (void*)(size_t)XTurbidityMeter_qualityChanged_signal;
}

void* XTurbidityMeter_errorOccurred_signal(XTurbidityMeter* self, int error)
{
    int e = error;
    turbidityEmit(self, (size_t)XTurbidityMeter_errorOccurred_signal,
                  XVarList_Create(XVar(int, e)));
    return (void*)(size_t)XTurbidityMeter_errorOccurred_signal;
}

#endif /* XINDUSTRY_ON && XTURBIDITY_ON */
