
/**
 * @file       XTempHumTransmitter.c
 * @brief      温湿度变送器设备抽象实现。
 * @details    完全对齐 XSolenoidValve.c 的形态：组信号槽经 connect_1 订阅
 *             （receiver=设备，槽内按 role 过滤本设备关注点）、XObject
 *             自带定时器做质量巡检（**周期型**：与电磁阀在途监督的单发型
 *             不同，挂接期间常驻，detach/deinit 才 kill——「触发≠注销」
 *             教训在此形态下天然规避）、解挂与 deinit 共用内部路径。
 *             数据流：组 inputChanged(role, chEng) → 第二级标度 → 缓存/
 *             信号；质量 = 已收拍 && 未陈旧，翻转沿发 qualityChanged。
 */
#include "XTempHumTransmitter.h"

#if XINDUSTRY_ON && XTEMP_HUM_ON

#include <string.h>
#include "XMemory.h"
#include "XDateTime.h"

/* ==================== 内部辅助 ==================== */

#define XTEMPHUM_DEFAULT_STALE_MS 5000u
#define XTEMPHUM_QUALITY_PERIOD_MS 1000u   /**< 质量巡检周期（毫秒）。 */

/** @brief 订阅槽前向声明：解挂的断开坐标需要引用。 */
static void VXTempHumTransmitter_onInputChanged(XObject* receiver, XVarList* args);

/** @brief 通道号合法化（越界一律按温度处理并返回 false）。 */
static bool temphumChannelOk(int channel)
{
    return channel == (int)XTempHumChannel_Temperature ||
           channel == (int)XTempHumChannel_Humidity;
}

/**
 * @brief 第二级标度换算：eng = chEng * num / den + offset（int64 中间量）。
 */
static int32_t temphumApplyScale(const XTempHumTransmitter* self, int ch, int32_t chEng)
{
    int64_t v = (int64_t)chEng * (int64_t)self->m_scaleNum[ch];
    if (self->m_scaleDen[ch] != 0)
        v /= (int64_t)self->m_scaleDen[ch];
    v += (int64_t)self->m_scaleOffset[ch];
    return (int32_t)v;
}

/**
 * @brief 通道质量评估：已收到 && 未陈旧。
 */
static bool temphumEvaluateQuality(const XTempHumTransmitter* self, int ch)
{
    uint64_t now;
    if (!self->m_pts[ch] || !self->m_received[ch]) return false;
    if (self->m_staleMs == 0u) return true;
    now = XDateTime_currentMSecsSinceEpoch();
    return (now - self->m_lastChangeMs[ch]) <= (uint64_t)self->m_staleMs;
}

/**
 * @brief 质量发布：翻转沿发 qualityChanged（数据拍与巡检共用）。
 */
static void temphumPublishQuality(XTempHumTransmitter* self, int ch)
{
    bool q = temphumEvaluateQuality(self, ch);
    if (self->m_quality[ch] == q) return;
    self->m_quality[ch] = q;
    XTempHumTransmitter_qualityChanged_signal(self, ch, q);
}

/**
 * @brief 单点播种/更新路径：取点缓存值 → 标度 → 变化才发 valueChanged。
 * @return 点有有效数据返回 true。
 */
static bool temphumAbsorbPoint(XTempHumTransmitter* self, int ch)
{
    int32_t chEng = 0;
    int32_t eng;
    if (!self->m_group || !self->m_pts[ch]) return false;
    if (!XIoChannelGroup_readAnalogInput(self->m_group,
                                         XIoPoint_role(self->m_pts[ch]), &chEng))
        return false;
    eng = temphumApplyScale(self, ch, chEng);
    self->m_lastChangeMs[ch] = XDateTime_currentMSecsSinceEpoch();
    if (!self->m_received[ch] || self->m_value[ch] != eng) {
        self->m_received[ch] = true;
        self->m_value[ch] = eng;
        XTempHumTransmitter_valueChanged_signal(self, ch, eng);
    } else {
        self->m_received[ch] = true;
    }
    temphumPublishQuality(self, ch);
    return true;
}

/* ==================== 定时器与生命周期 ==================== */

static void temphumStopQualityTimer(XTempHumTransmitter* self)
{
    if (self->m_qualityTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_qualityTimer);
        self->m_qualityTimer = XTIMER_INVALID_ID;
    }
}

static void temphumStartQualityTimer(XTempHumTransmitter* self)
{
    if (self->m_qualityTimer != XTIMER_INVALID_ID) return;
    self->m_qualityTimer = XObject_startTimer_ms((XObject*)self,
                                                 XTEMPHUM_QUALITY_PERIOD_MS,
                                                 XTimerType_CoarseTimer);
    if (self->m_qualityTimer == XTIMER_INVALID_ID) {
        /* 巡检资源启动失败：陈旧翻转将无法主动发布，按工业可见性侧报错 */
        XTempHumTransmitter_errorOccurred_signal(self, XTempHumError_Internal);
    }
}

/* ==================== 内部解挂（detach 与 deinit 共用） ==================== */

static void temphumDetachInternal(XTempHumTransmitter* self)
{
    int ch;
    bool hadAny = false;
    if (!self->m_group) return;
    for (ch = 0; ch < 2; ++ch)
        if (self->m_pts[ch]) hadAny = true;
    if (!hadAny && !self->m_signalAttached) {
        self->m_group = NULL;
        return;
    }
    if (self->m_signalAttached) {
        XObject_disconnect_1((XObject*)self->m_group,
                             XSignal(XIoChannelGroup_inputChanged_signal),
                             (XObject*)self, VXTempHumTransmitter_onInputChanged);
        self->m_signalAttached = false;
    }
    for (ch = 0; ch < 2; ++ch) {
        if (self->m_pts[ch]) {
            self->m_pts[ch] = NULL;
            /* 通道质量转无效（调用方可见的失去数据沿）。 */
            if (self->m_quality[ch]) {
                self->m_quality[ch] = false;
                XTempHumTransmitter_qualityChanged_signal(self, ch, false);
            }
        }
    }
    temphumStopQualityTimer(self);
    self->m_group = NULL;
}

/* ==================== 组信号槽（经 connect_1 连接，receiver=设备） ==================== */

/**
 * @brief 组输入变化槽：按输入点 role 过滤本设备，标度后更新缓存。
 * @param receiver 连接的接收者（即本设备）；**不是**组，禁止直接强转。
 * @param args 信号参数（uint16_t role, int32_t chEng）。
 * @return 无。
 */
static void VXTempHumTransmitter_onInputChanged(XObject* receiver, XVarList* args)
{
    XTempHumTransmitter* self;
    int ch;

    if (!receiver || !args) return;
    self = (XTempHumTransmitter*)receiver;
    XVarList_args_2(args, uint16_t, role, int32_t, chEng);
    for (ch = 0; ch < 2; ++ch) {
        if (!self->m_pts[ch] || role != XIoPoint_role(self->m_pts[ch])) continue;
        {
            int32_t eng = temphumApplyScale(self, ch, chEng);
            self->m_lastChangeMs[ch] = XDateTime_currentMSecsSinceEpoch();
            if (!self->m_received[ch] || self->m_value[ch] != eng) {
                self->m_received[ch] = true;
                self->m_value[ch] = eng;
                XTempHumTransmitter_valueChanged_signal(self, ch, eng);
            } else {
                self->m_received[ch] = true;
            }
            temphumPublishQuality(self, ch);
        }
    }
}

/**
 * @brief 质量巡检拍（周期型；挂接期间常驻）：陈旧翻转沿发布。
 */
static void VXTempHumTransmitter_timerEvent(XObject* obj, XTimerEvent* event)
{
    XTempHumTransmitter* self = (XTempHumTransmitter*)obj;
    XTimerId id;

    if (!self || !event) return;
    id = XTimerEvent_timerId(event);
    XEvent_accept(event);
    if (id == self->m_qualityTimer) {
        int ch;
        for (ch = 0; ch < 2; ++ch)
            if (self->m_pts[ch])
                temphumPublishQuality(self, ch);
        return;
    }
    /* 未知 id 交父类，不吞 */
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))((XObject*)self, event);
}

static void VXTempHumTransmitter_deinit(XObject* obj)
{
    XTempHumTransmitter* self = (XTempHumTransmitter*)obj;

    if (!self) return;
    temphumStopQualityTimer(self);
    temphumDetachInternal(self);
    XClass_Deinit_Parent(XObject, obj);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XTempHumTransmitter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTempHumTransmitter)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXTempHumTransmitter_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTempHumTransmitter_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XTempHumTransmitter_init(XTempHumTransmitter* obj)
{
    int ch;
    if (!obj) return;
    memset(obj, 0, sizeof(XTempHumTransmitter));
    XObject_init((XObject*)obj);
    XClassSetVtable(obj, XTempHumTransmitter);
    obj->m_group = NULL;
    for (ch = 0; ch < 2; ++ch) {
        obj->m_pts[ch] = NULL;
        obj->m_scaleNum[ch] = 1;
        obj->m_scaleDen[ch] = 1;
        obj->m_scaleOffset[ch] = 0;
        obj->m_value[ch] = 0;
        obj->m_received[ch] = false;
        obj->m_lastChangeMs[ch] = 0;
        obj->m_quality[ch] = false;
    }
    obj->m_staleMs = XTEMPHUM_DEFAULT_STALE_MS;
    obj->m_signalAttached = false;
    obj->m_qualityTimer = XTIMER_INVALID_ID;
}

XTempHumTransmitter* XTempHumTransmitter_create(void)
{
    XTempHumTransmitter* obj = (XTempHumTransmitter*)XMemory_calloc(
        1, sizeof(XTempHumTransmitter), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XTempHumTransmitter_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 装配 ==================== */

bool XTempHumTransmitter_attach(XTempHumTransmitter* self, XIoChannelGroup* group,
                                XIoPoint* tempPt, XIoPoint* humPt)
{
    XConnection* conn;
    bool planted = false;

    if (ISNULL(self, "XTempHumTransmitter")) return false;
    if (!group || (!tempPt && !humPt)) return false;
    if (self->m_group == group && self->m_pts[0] == tempPt &&
        self->m_pts[1] == humPt)
        return true;
    temphumDetachInternal(self);
    self->m_group = group;
    self->m_pts[0] = tempPt;
    self->m_pts[1] = humPt;
    conn = XObject_connect_1((XObject*)group,
                             XSignal(XIoChannelGroup_inputChanged_signal),
                             (XObject*)self, VXTempHumTransmitter_onInputChanged,
                             XConnectionType_Auto);
    if (!conn) {
        self->m_group = NULL;
        self->m_pts[0] = NULL;
        self->m_pts[1] = NULL;
        return false;
    }
    self->m_signalAttached = true;
    /* 播种：同步读各点当前缓存值落初始快照（电磁阀 attachFeedback 同款）。 */
    {
        int ch;
        for (ch = 0; ch < 2; ++ch)
            if (self->m_pts[ch])
                planted = temphumAbsorbPoint(self, ch) || planted;
    }
    /* 质量巡检常驻（挂接期间）；启动失败仅报错不回滚装配。 */
    temphumStartQualityTimer(self);
    (void)planted;
    return true;
}

bool XTempHumTransmitter_detach(XTempHumTransmitter* self)
{
    bool had;
    int ch;
    if (ISNULL(self, "XTempHumTransmitter")) return false;
    if (!self->m_group) return false;
    had = false;
    for (ch = 0; ch < 2; ++ch)
        if (self->m_pts[ch]) had = true;
    temphumDetachInternal(self);
    return had;
}

/* ==================== 配置 ==================== */

bool XTempHumTransmitter_setScale(XTempHumTransmitter* self, XTempHumChannel channel,
                                  int32_t num, int32_t den, int32_t offset)
{
    int ch = (int)channel;
    int32_t old;
    if (ISNULL(self, "XTempHumTransmitter")) return false;
    if (!temphumChannelOk(ch)) return false;
    if (den == 0) return false;
    self->m_scaleNum[ch] = num;
    self->m_scaleDen[ch] = den;
    self->m_scaleOffset[ch] = offset;
    /* 立即生效：重算缓存值，变化则重发（调用方可感知标度变更）。 */
    old = self->m_value[ch];
    if (self->m_pts[ch] && self->m_received[ch]) {
        int32_t chEng = 0;
        if (self->m_group &&
            XIoChannelGroup_readAnalogInput(self->m_group,
                                            XIoPoint_role(self->m_pts[ch]), &chEng)) {
            self->m_value[ch] = temphumApplyScale(self, ch, chEng);
            if (self->m_value[ch] != old)
                XTempHumTransmitter_valueChanged_signal(self, ch, self->m_value[ch]);
        }
    }
    return true;
}

void XTempHumTransmitter_scale(const XTempHumTransmitter* self, XTempHumChannel channel,
                               int32_t* outNum, int32_t* outDen, int32_t* outOffset)
{
    int ch = (int)channel;
    if (outNum) *outNum = 0;
    if (outDen) *outDen = 0;
    if (outOffset) *outOffset = 0;
    if (!self || !temphumChannelOk(ch)) return;
    if (outNum) *outNum = self->m_scaleNum[ch];
    if (outDen) *outDen = self->m_scaleDen[ch];
    if (outOffset) *outOffset = self->m_scaleOffset[ch];
}

bool XTempHumTransmitter_setStaleMs(XTempHumTransmitter* self, uint32_t staleMs)
{
    if (ISNULL(self, "XTempHumTransmitter")) return false;
    self->m_staleMs = staleMs;
    return true;
}

uint32_t XTempHumTransmitter_staleMs(const XTempHumTransmitter* self)
{
    if (!self) return 0u;
    return self->m_staleMs;
}

/* ==================== 查询 ==================== */

bool XTempHumTransmitter_value(const XTempHumTransmitter* self, XTempHumChannel channel,
                               int32_t* outEng)
{
    int ch = (int)channel;
    bool valid;
    if (outEng) *outEng = 0;
    if (!self || !temphumChannelOk(ch)) return false;
    valid = temphumEvaluateQuality(self, ch);
    if (valid && outEng)
        *outEng = self->m_value[ch];
    return valid;
}

bool XTempHumTransmitter_isValid(const XTempHumTransmitter* self, XTempHumChannel channel)
{
    int ch = (int)channel;
    if (!self || !temphumChannelOk(ch)) return false;
    return temphumEvaluateQuality(self, ch);
}

bool XTempHumTransmitter_lastUpdateMs(const XTempHumTransmitter* self,
                                      XTempHumChannel channel, uint64_t* outMs)
{
    int ch = (int)channel;
    if (outMs) *outMs = 0;
    if (!self || !temphumChannelOk(ch) || !self->m_received[ch]) return false;
    if (outMs) *outMs = self->m_lastChangeMs[ch];
    return true;
}

/* ==================== 信号 ==================== */

void* XTempHumTransmitter_valueChanged_signal(XTempHumTransmitter* self,
                                              int channel, int32_t engValue)
{
    XEmitSignal(self, XTempHumTransmitter_valueChanged_signal,
                XVarList_Create(XVar(int, channel), XVar(int32_t, engValue)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XTempHumTransmitter_qualityChanged_signal(XTempHumTransmitter* self,
                                                int channel, bool valid)
{
    XEmitSignal(self, XTempHumTransmitter_qualityChanged_signal,
                XVarList_Create(XVar(int, channel), XVar(bool, valid)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XTempHumTransmitter_errorOccurred_signal(XTempHumTransmitter* self, int error)
{
    XEmitSignal(self, XTempHumTransmitter_errorOccurred_signal,
                XVarList_Create(XVar(int, error)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

#endif /* XINDUSTRY_ON && XTEMP_HUM_ON */
