
/**
 * @file       XDiffPressureGauge.c
 * @brief      压差计设备抽象实现。
 * @details    与 XTempHumTransmitter.c 同族（单通道版）：组信号槽经
 *             connect_1 订阅（receiver=设备，槽内按 role 过滤）、XObject
 *             自带定时器做质量巡检（周期型，挂接期间常驻）、解挂与 deinit
 *             共用内部路径。数据流：组 inputChanged(role, chEng) → 第二级
 *             标度 → 缓存/信号；质量翻转沿发 qualityChanged。
 */
#include "XDiffPressureGauge.h"

#if XINDUSTRY_ON && XPRESS_DIFF_ON

#include <string.h>
#include "XMemory.h"
#include "XDateTime.h"

/* ==================== 内部辅助 ==================== */

#define XDIFFPRESSURE_DEFAULT_STALE_MS 5000u
#define XDIFFPRESSURE_QUALITY_PERIOD_MS 1000u   /**< 质量巡检周期（毫秒）。 */

/** @brief 订阅槽前向声明：解挂的断开坐标需要引用。 */
static void VXDiffPressureGauge_onInputChanged(XObject* receiver, XVarList* args);

/**
 * @brief 第二级标度换算：eng = chEng * num / den + offset（int64 中间量）。
 */
static int32_t diffApplyScale(const XDiffPressureGauge* self, int32_t chEng)
{
    int64_t v = (int64_t)chEng * (int64_t)self->m_scaleNum;
    if (self->m_scaleDen != 0)
        v /= (int64_t)self->m_scaleDen;
    v += (int64_t)self->m_scaleOffset;
    return (int32_t)v;
}

/**
 * @brief 质量评估：已收到 && 未陈旧。
 */
static bool diffEvaluateQuality(const XDiffPressureGauge* self)
{
    uint64_t now;
    if (!self->m_pt || !self->m_received) return false;
    if (self->m_staleMs == 0u) return true;
    now = XDateTime_currentMSecsSinceEpoch();
    return (now - self->m_lastChangeMs) <= (uint64_t)self->m_staleMs;
}

/**
 * @brief 质量发布：翻转沿发 qualityChanged（数据拍与巡检共用）。
 */
static void diffPublishQuality(XDiffPressureGauge* self)
{
    bool q = diffEvaluateQuality(self);
    if (self->m_quality == q) return;
    self->m_quality = q;
    XDiffPressureGauge_qualityChanged_signal(self, q);
}

/**
 * @brief 播种/更新路径：取点缓存值 → 标度 → 变化才发 valueChanged。
 * @return 点有有效数据返回 true。
 */
static bool diffAbsorbPoint(XDiffPressureGauge* self)
{
    int32_t chEng = 0;
    int32_t eng;
    if (!self->m_group || !self->m_pt) return false;
    if (!XIoChannelGroup_readAnalogInput(self->m_group,
                                         XIoPoint_role(self->m_pt), &chEng))
        return false;
    eng = diffApplyScale(self, chEng);
    self->m_lastChangeMs = XDateTime_currentMSecsSinceEpoch();
    if (!self->m_received || self->m_value != eng) {
        self->m_received = true;
        self->m_value = eng;
        XDiffPressureGauge_valueChanged_signal(self, eng);
    } else {
        self->m_received = true;
    }
    diffPublishQuality(self);
    return true;
}

/* ==================== 定时器与生命周期 ==================== */

static void diffStopQualityTimer(XDiffPressureGauge* self)
{
    if (self->m_qualityTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_qualityTimer);
        self->m_qualityTimer = XTIMER_INVALID_ID;
    }
}

static void diffStartQualityTimer(XDiffPressureGauge* self)
{
    if (self->m_qualityTimer != XTIMER_INVALID_ID) return;
    self->m_qualityTimer = XObject_startTimer_ms((XObject*)self,
                                                 XDIFFPRESSURE_QUALITY_PERIOD_MS,
                                                 XTimerType_CoarseTimer);
    if (self->m_qualityTimer == XTIMER_INVALID_ID) {
        /* 巡检资源启动失败：陈旧翻转将无法主动发布，按可见性侧报错 */
        XDiffPressureGauge_errorOccurred_signal(self, XDiffPressureError_Internal);
    }
}

/* ==================== 内部解挂（detach 与 deinit 共用） ==================== */

static void diffDetachInternal(XDiffPressureGauge* self)
{
    if (!self->m_group) return;
    if (self->m_signalAttached) {
        XObject_disconnect_1((XObject*)self->m_group,
                             XSignal(XIoChannelGroup_inputChanged_signal),
                             (XObject*)self, VXDiffPressureGauge_onInputChanged);
        self->m_signalAttached = false;
    }
    if (self->m_pt) {
        self->m_pt = NULL;
        if (self->m_quality) {
            self->m_quality = false;
            XDiffPressureGauge_qualityChanged_signal(self, false);
        }
    }
    diffStopQualityTimer(self);
    self->m_group = NULL;
}

/* ==================== 组信号槽（经 connect_1 连接，receiver=设备） ==================== */

/**
 * @brief 组输入变化槽：按输入点 role 过滤本设备，标度后更新缓存。
 * @param receiver 连接的接收者（即本设备）；**不是**组，禁止直接强转。
 * @param args 信号参数（uint16_t role, int32_t chEng）。
 * @return 无。
 */
static void VXDiffPressureGauge_onInputChanged(XObject* receiver, XVarList* args)
{
    XDiffPressureGauge* self;

    if (!receiver || !args) return;
    self = (XDiffPressureGauge*)receiver;
    XVarList_args_2(args, uint16_t, role, int32_t, chEng);
    if (!self->m_pt || role != XIoPoint_role(self->m_pt)) return;
    {
        int32_t eng = diffApplyScale(self, chEng);
        self->m_lastChangeMs = XDateTime_currentMSecsSinceEpoch();
        if (!self->m_received || self->m_value != eng) {
            self->m_received = true;
            self->m_value = eng;
            XDiffPressureGauge_valueChanged_signal(self, eng);
        } else {
            self->m_received = true;
        }
        diffPublishQuality(self);
    }
}

/**
 * @brief 质量巡检拍（周期型；挂接期间常驻）：陈旧翻转沿发布。
 */
static void VXDiffPressureGauge_timerEvent(XObject* obj, XTimerEvent* event)
{
    XDiffPressureGauge* self = (XDiffPressureGauge*)obj;
    XTimerId id;

    if (!self || !event) return;
    id = XTimerEvent_timerId(event);
    XEvent_accept(event);
    if (id == self->m_qualityTimer) {
        if (self->m_pt)
            diffPublishQuality(self);
        return;
    }
    /* 未知 id 交父类，不吞 */
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))((XObject*)self, event);
}

static void VXDiffPressureGauge_deinit(XObject* obj)
{
    XDiffPressureGauge* self = (XDiffPressureGauge*)obj;

    if (!self) return;
    diffStopQualityTimer(self);
    diffDetachInternal(self);
    XClass_Deinit_Parent(XObject, obj);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XDiffPressureGauge_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDiffPressureGauge)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXDiffPressureGauge_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXDiffPressureGauge_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XDiffPressureGauge_init(XDiffPressureGauge* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XDiffPressureGauge));
    XObject_init((XObject*)obj);
    XClassSetVtable(obj, XDiffPressureGauge);
    obj->m_group = NULL;
    obj->m_pt = NULL;
    obj->m_scaleNum = 1;
    obj->m_scaleDen = 1;
    obj->m_scaleOffset = 0;
    obj->m_value = 0;
    obj->m_received = false;
    obj->m_lastChangeMs = 0;
    obj->m_staleMs = XDIFFPRESSURE_DEFAULT_STALE_MS;
    obj->m_signalAttached = false;
    obj->m_qualityTimer = XTIMER_INVALID_ID;
    obj->m_quality = false;
}

XDiffPressureGauge* XDiffPressureGauge_create(void)
{
    XDiffPressureGauge* obj = (XDiffPressureGauge*)XMemory_calloc(
        1, sizeof(XDiffPressureGauge), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XDiffPressureGauge_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 装配 ==================== */

bool XDiffPressureGauge_attach(XDiffPressureGauge* self, XIoChannelGroup* group,
                               XIoPoint* pressurePt)
{
    XConnection* conn;

    if (ISNULL(self, "XDiffPressureGauge")) return false;
    if (!group || !pressurePt) return false;
    if (self->m_group == group && self->m_pt == pressurePt) return true;
    diffDetachInternal(self);
    self->m_group = group;
    self->m_pt = pressurePt;
    conn = XObject_connect_1((XObject*)group,
                             XSignal(XIoChannelGroup_inputChanged_signal),
                             (XObject*)self, VXDiffPressureGauge_onInputChanged,
                             XConnectionType_Auto);
    if (!conn) {
        self->m_group = NULL;
        self->m_pt = NULL;
        return false;
    }
    self->m_signalAttached = true;
    diffAbsorbPoint(self);          /* 播种：同步读当前缓存值落初始快照。 */
    diffStartQualityTimer(self);    /* 巡检常驻；启动失败仅报错不回滚。 */
    return true;
}

bool XDiffPressureGauge_detach(XDiffPressureGauge* self)
{
    bool had;
    if (ISNULL(self, "XDiffPressureGauge")) return false;
    if (!self->m_group) return false;
    had = (self->m_pt != NULL);
    diffDetachInternal(self);
    return had;
}

/* ==================== 配置 ==================== */

bool XDiffPressureGauge_setScale(XDiffPressureGauge* self,
                                 int32_t num, int32_t den, int32_t offset)
{
    int32_t old;
    if (ISNULL(self, "XDiffPressureGauge")) return false;
    if (den == 0) return false;
    self->m_scaleNum = num;
    self->m_scaleDen = den;
    self->m_scaleOffset = offset;
    /* 立即生效：重算缓存值，变化则重发（调用方可感知标度变更）。 */
    old = self->m_value;
    if (self->m_pt && self->m_received) {
        int32_t chEng = 0;
        if (self->m_group &&
            XIoChannelGroup_readAnalogInput(self->m_group,
                                            XIoPoint_role(self->m_pt), &chEng)) {
            self->m_value = diffApplyScale(self, chEng);
            if (self->m_value != old)
                XDiffPressureGauge_valueChanged_signal(self, self->m_value);
        }
    }
    return true;
}

void XDiffPressureGauge_scale(const XDiffPressureGauge* self,
                              int32_t* outNum, int32_t* outDen, int32_t* outOffset)
{
    if (outNum) *outNum = 0;
    if (outDen) *outDen = 0;
    if (outOffset) *outOffset = 0;
    if (!self) return;
    if (outNum) *outNum = self->m_scaleNum;
    if (outDen) *outDen = self->m_scaleDen;
    if (outOffset) *outOffset = self->m_scaleOffset;
}

bool XDiffPressureGauge_setStaleMs(XDiffPressureGauge* self, uint32_t staleMs)
{
    if (ISNULL(self, "XDiffPressureGauge")) return false;
    self->m_staleMs = staleMs;
    return true;
}

uint32_t XDiffPressureGauge_staleMs(const XDiffPressureGauge* self)
{
    if (!self) return 0u;
    return self->m_staleMs;
}

/* ==================== 查询 ==================== */

bool XDiffPressureGauge_value(const XDiffPressureGauge* self, int32_t* outEng)
{
    bool valid;
    if (outEng) *outEng = 0;
    if (!self) return false;
    valid = diffEvaluateQuality(self);
    if (valid && outEng)
        *outEng = self->m_value;
    return valid;
}

bool XDiffPressureGauge_isValid(const XDiffPressureGauge* self)
{
    if (!self) return false;
    return diffEvaluateQuality(self);
}

bool XDiffPressureGauge_lastUpdateMs(const XDiffPressureGauge* self, uint64_t* outMs)
{
    if (outMs) *outMs = 0;
    if (!self || !self->m_received) return false;
    if (outMs) *outMs = self->m_lastChangeMs;
    return true;
}

/* ==================== 信号 ==================== */

void* XDiffPressureGauge_valueChanged_signal(XDiffPressureGauge* self, int32_t engValue)
{
    XEmitSignal(self, XDiffPressureGauge_valueChanged_signal,
                XVarList_Create(XVar(int32_t, engValue)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XDiffPressureGauge_qualityChanged_signal(XDiffPressureGauge* self, bool valid)
{
    XEmitSignal(self, XDiffPressureGauge_qualityChanged_signal,
                XVarList_Create(XVar(bool, valid)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XDiffPressureGauge_errorOccurred_signal(XDiffPressureGauge* self, int error)
{
    XEmitSignal(self, XDiffPressureGauge_errorOccurred_signal,
                XVarList_Create(XVar(int, error)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

#endif /* XINDUSTRY_ON && XPRESS_DIFF_ON */
