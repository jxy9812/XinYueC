/**
 * @file       XVirtualKeyboardTrace.c
 * @brief      XVirtualKeyboardTrace 手写轨迹容器实现（容器与引擎分派
 *             面；识别引擎本轮 N-A）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"

#if XVIRTUALKEYBOARD_ON

#include "XVirtualKeyboardTrace.h"
#include "XStringUtils.h"
#include "XMemory.h"

/** @brief 通道数据（名字 + double 数组，按需扩展）。 */
typedef struct XVirtualKeyboardTraceChannel
{
    char* m_name;     /**< 通道名（UTF-8，堆分配）。 */
    double* m_data;   /**< 数据数组（堆分配）。 */
    int m_count;      /**< 已写数据条数。 */
    int m_capacity;   /**< 数组容量。 */
} XVirtualKeyboardTraceChannel;

/** @brief 私有数据块。 */
typedef struct XVirtualKeyboardTracePrivate
{
    int m_traceId;                    /**< 轨迹 id（默认 -1，Qt 缺省）。 */
    XVirtualKeyboardTraceChannel* m_channels; /**< 通道数组。 */
    int m_channelCount;               /**< 通道数。 */
    XVirtualKeyboardTracePoint* m_points; /**< 点数组。 */
    int m_pointCount;                 /**< 点数。 */
    int m_pointCapacity;              /**< 点数组容量。 */
    bool m_final;                     /**< 一笔结束。 */
    bool m_canceled;                  /**< 轨迹作废。 */
    float m_opacity;                  /**< 透明度 [0,1]，默认 1。 */
    XTimerId m_hideTimer;             /**< 延时隐藏定时器。 */
    bool m_hideExpired;               /**< 延时隐藏已到期。 */
} XVirtualKeyboardTracePrivate;

/** @brief 内部取私有块。 */
static XVirtualKeyboardTracePrivate* xvkt_priv(
        const XVirtualKeyboardTrace* self)
{
    return (self && self->m_data)
               ? (XVirtualKeyboardTracePrivate*)self->m_data : NULL;
}

/** @brief 发射信号并管理参数表生命周期。 */
static void xvkt_emit(XVirtualKeyboardTrace* self, size_t signal,
                      XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args)
        XVarList_delete(args);
}

/** @brief 通道查找（名字精确匹配；未找到返回 NULL）。 */
static XVirtualKeyboardTraceChannel* xvkt_findChannel(
        XVirtualKeyboardTracePrivate* priv, const char* channel)
{
    int i;
    if (!priv || !channel) return NULL;
    for (i = 0; i < priv->m_channelCount; ++i) {
        if (priv->m_channels[i].m_name &&
            XStrcmp(priv->m_channels[i].m_name, channel) == 0)
            return &priv->m_channels[i];
    }
    return NULL;
}

/** @brief 释放全部通道。 */
static void xvkt_clearChannels(XVirtualKeyboardTracePrivate* priv)
{
    int i;
    if (!priv) return;
    for (i = 0; i < priv->m_channelCount; ++i) {
        if (priv->m_channels[i].m_name)
            XFree_System(priv->m_channels[i].m_name);
        if (priv->m_channels[i].m_data)
            XFree_System(priv->m_channels[i].m_data);
    }
    if (priv->m_channels) {
        XFree_System(priv->m_channels);
        priv->m_channels = NULL;
    }
    priv->m_channelCount = 0;
}

/** @brief 停延时隐藏定时器（幂等）。 */
static void xvkt_stopHide(XVirtualKeyboardTrace* self)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    if (!priv || priv->m_hideTimer == XTIMER_INVALID_ID) return;
    XObject_killTimer((XObject*)self, priv->m_hideTimer);
    priv->m_hideTimer = XTIMER_INVALID_ID;
}

/* 前向声明（class_init 注册用；init 由引擎保护头声明、本 TU 定义）。 */
static void XVkt_deinit(XVirtualKeyboardTrace* self);
static void XVkt_timerEvent(XObject* object, XTimerEvent* event);
void XVirtualKeyboardTrace_init(XVirtualKeyboardTrace* self);
XVirtualKeyboardTrace* XVirtualKeyboardTrace_create_engine(void);

XVtable* XVirtualKeyboardTrace_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVirtualKeyboardTrace)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, XVkt_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XVkt_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 定时器分派：隐藏到期；其余链回基类。 */
static void XVkt_timerEvent(XObject* object, XTimerEvent* event)
{
    XVirtualKeyboardTrace* self = (XVirtualKeyboardTrace*)object;
    XVirtualKeyboardTracePrivate* priv;
    XTimerId id;
    if (!self || !event) return;
    priv = xvkt_priv(self);
    id = (XTimerId)XTimerEvent_timerId(event);
    if (priv && id == priv->m_hideTimer) {
        priv->m_hideTimer = XTIMER_INVALID_ID;
        priv->m_hideExpired = true;
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(object, event);
}

/** @brief 反初始化：释放点/通道/私有块后调父类。 */
static void XVkt_deinit(XVirtualKeyboardTrace* self)
{
    XVirtualKeyboardTracePrivate* priv;
    if (!self) return;
    priv = xvkt_priv(self);
    if (priv) {
        xvkt_stopHide(self);
        if (priv->m_points) XFree_System(priv->m_points);
        xvkt_clearChannels(priv);
        XFree_System(priv);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

/**
 * @brief      引擎 TU 专用创建入口（traceBegin 契约：只能经引擎获得）。
 * @return     新对象指针；失败返回 NULL。调用方用 *_delete_base 释放。
 */
XVirtualKeyboardTrace* XVirtualKeyboardTrace_create_engine(void)
{
    XVirtualKeyboardTrace* self =
        (XVirtualKeyboardTrace*)XMalloc_System(sizeof(XVirtualKeyboardTrace));
    if (!self) return NULL;
    XVirtualKeyboardTrace_init(self);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    return self;
}

void XVirtualKeyboardTrace_init(XVirtualKeyboardTrace* self)
{
    XVirtualKeyboardTracePrivate* priv;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XVirtualKeyboardTrace);
    self->m_data = XMalloc_System(sizeof(XVirtualKeyboardTracePrivate));
    if (!self->m_data) return;
    priv = (XVirtualKeyboardTracePrivate*)self->m_data;
    XMemset(priv, 0, sizeof(*priv));
    priv->m_traceId = -1;
    priv->m_opacity = 1.0f;
    priv->m_hideTimer = XTIMER_INVALID_ID;
}

int XVirtualKeyboardTrace_traceId(const XVirtualKeyboardTrace* self)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    return priv ? priv->m_traceId : -1;
}

void XVirtualKeyboardTrace_setTraceId(XVirtualKeyboardTrace* self,
                                      int traceId)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    if (!priv || priv->m_traceId == traceId) return;
    priv->m_traceId = traceId;
    xvkt_emit(self, (size_t)XVirtualKeyboardTrace_traceIdChanged_signal(
                        NULL, 0),
              XVarList_Create(XVar(int, traceId)));
}

int XVirtualKeyboardTrace_channelCount(const XVirtualKeyboardTrace* self)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    return priv ? priv->m_channelCount : 0;
}

const char* XVirtualKeyboardTrace_channelAt(
        const XVirtualKeyboardTrace* self, int index)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    if (!priv || index < 0 || index >= priv->m_channelCount) return NULL;
    return priv->m_channels[index].m_name;
}

void XVirtualKeyboardTrace_setChannels(XVirtualKeyboardTrace* self,
                                       const char* const* channels,
                                       int count)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    int i;
    int n = 0;
    if (!priv) return;
    xvkt_clearChannels(priv);
    if (channels && count > 0) {
        priv->m_channels = (XVirtualKeyboardTraceChannel*)XMalloc_System(
            (size_t)count * sizeof(XVirtualKeyboardTraceChannel));
        if (priv->m_channels) {
            XMemset(priv->m_channels,
                    0, (size_t)count * sizeof(XVirtualKeyboardTraceChannel));
            /* 紧凑下标 n 写入：失败槽不占坑，计数与已填充槽严格一致，
               clearChannels 按 m_channelCount 释放才不漏（对齐
               XVirtualKeyboardSettings/XVirtualKeyboardDictionary 写法）。 */
            for (i = 0; i < count && channels[i]; ++i) {
                size_t len = XStrlen(channels[i]) + 1;
                char* copy = (char*)XMalloc_System(len);
                if (!copy) continue;
                XMemcpy(copy, channels[i], len);
                priv->m_channels[n].m_name = copy;
                ++n;
            }
            priv->m_channelCount = n;
        }
    }
    xvkt_emit(self, (size_t)XVirtualKeyboardTrace_channelsChanged_signal(
                        NULL),
              NULL);
}

int XVirtualKeyboardTrace_length(const XVirtualKeyboardTrace* self)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    return priv ? priv->m_pointCount : 0;
}

int XVirtualKeyboardTrace_points(const XVirtualKeyboardTrace* self, int pos,
                                 int count,
                                 XVirtualKeyboardTracePoint* outPoints,
                                 int maxCount)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    int start;
    int end;
    int written = 0;
    int i;
    if (!priv) return 0;
    if (pos < 0) pos = 0;
    if (pos > priv->m_pointCount) return 0;
    start = pos;
    end = (count < 0 || start + count > priv->m_pointCount)
              ? priv->m_pointCount
              : start + count;
    for (i = start; i < end; ++i) {
        if (outPoints && written < maxCount)
            outPoints[written] = priv->m_points[i];
        ++written;
    }
    return written;
}

int XVirtualKeyboardTrace_addPoint(XVirtualKeyboardTrace* self,
                                   const XVirtualKeyboardTracePoint* point)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    if (!priv || !point) return -1;
    if (priv->m_pointCount >= priv->m_pointCapacity) {
        int newCap = priv->m_pointCapacity > 0 ? priv->m_pointCapacity * 2
                                               : 16;
        XVirtualKeyboardTracePoint* grown =
            (XVirtualKeyboardTracePoint*)XMalloc_System(
                (size_t)newCap * sizeof(XVirtualKeyboardTracePoint));
        if (!grown) return -1;
        if (priv->m_points) {
            XMemcpy(grown, priv->m_points,
                    (size_t)priv->m_pointCount *
                        sizeof(XVirtualKeyboardTracePoint));
            XFree_System(priv->m_points);
        }
        priv->m_points = grown;
        priv->m_pointCapacity = newCap;
    }
    priv->m_points[priv->m_pointCount] = *point;
    ++priv->m_pointCount;
    xvkt_emit(self, (size_t)XVirtualKeyboardTrace_lengthChanged_signal(
                        NULL, 0),
              XVarList_Create(XVar(int, priv->m_pointCount)));
    return priv->m_pointCount - 1;
}

bool XVirtualKeyboardTrace_setChannelData(XVirtualKeyboardTrace* self,
                                          const char* channel, int index,
                                          double data)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    XVirtualKeyboardTraceChannel* ch;
    if (!priv || index < 0) return false;
    ch = xvkt_findChannel(priv, channel);
    if (!ch) return false;
    if (index >= ch->m_capacity) {
        int newCap = ch->m_capacity > 0 ? ch->m_capacity : 16;
        double* grown;
        while (newCap <= index) newCap *= 2;
        grown = (double*)XMalloc_System((size_t)newCap * sizeof(double));
        if (!grown) return false;
        if (ch->m_data) {
            XMemcpy(grown, ch->m_data,
                    (size_t)ch->m_count * sizeof(double));
            XFree_System(ch->m_data);
        }
        /* 扩容区间补 0（稀疏通道按需扩展语义）。 */
        XMemset(grown + ch->m_count, 0,
                (size_t)(newCap - ch->m_count) * sizeof(double));
        ch->m_data = grown;
        ch->m_capacity = newCap;
    }
    ch->m_data[index] = data;
    if (index >= ch->m_count) ch->m_count = index + 1;
    return true;
}

int XVirtualKeyboardTrace_channelData(const XVirtualKeyboardTrace* self,
                                      const char* channel, int pos,
                                      int count, double* outData,
                                      int maxCount)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    XVirtualKeyboardTraceChannel* ch;
    int start;
    int end;
    int written = 0;
    int i;
    if (!priv) return 0;
    ch = xvkt_findChannel(priv, channel);
    if (!ch) return 0;
    if (pos < 0) pos = 0;
    if (pos > ch->m_count) return 0;
    start = pos;
    end = (count < 0 || start + count > ch->m_count) ? ch->m_count
                                                     : start + count;
    for (i = start; i < end; ++i) {
        if (outData && written < maxCount) outData[written] = ch->m_data[i];
        ++written;
    }
    return written;
}

bool XVirtualKeyboardTrace_isFinal(const XVirtualKeyboardTrace* self)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    return priv ? priv->m_final : false;
}

void XVirtualKeyboardTrace_setFinal(XVirtualKeyboardTrace* self,
                                    bool isFinal)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    if (!priv || priv->m_final == isFinal) return;
    priv->m_final = isFinal;
    xvkt_emit(self, (size_t)XVirtualKeyboardTrace_finalChanged_signal(
                        NULL, false),
              XVarList_Create(XVar(bool, isFinal)));
}

bool XVirtualKeyboardTrace_isCanceled(const XVirtualKeyboardTrace* self)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    return priv ? priv->m_canceled : false;
}

void XVirtualKeyboardTrace_setCanceled(XVirtualKeyboardTrace* self,
                                       bool isCanceled)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    if (!priv || priv->m_canceled == isCanceled) return;
    priv->m_canceled = isCanceled;
    xvkt_emit(self, (size_t)XVirtualKeyboardTrace_canceledChanged_signal(
                        NULL, false),
              XVarList_Create(XVar(bool, isCanceled)));
}

float XVirtualKeyboardTrace_opacity(const XVirtualKeyboardTrace* self)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    return priv ? priv->m_opacity : 1.0f;
}

void XVirtualKeyboardTrace_setOpacity(XVirtualKeyboardTrace* self,
                                      float opacity)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    if (!priv || priv->m_opacity == opacity) return;
    priv->m_opacity = opacity;
    xvkt_emit(self, (size_t)XVirtualKeyboardTrace_opacityChanged_signal(
                        NULL, 0.0f),
              XVarList_Create(XVar(float, opacity)));
}

void XVirtualKeyboardTrace_startHideTimer(XVirtualKeyboardTrace* self,
                                          int delayMs)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    if (!priv) return;
    xvkt_stopHide(self);
    priv->m_hideExpired = false;
    if (delayMs < 0) delayMs = 0;
    priv->m_hideTimer = XObject_startTimer_ms((XObject*)self,
                                              (uint64_t)delayMs,
                                              XTimerType_CoarseTimer);
}

bool XVirtualKeyboardTrace_isHideExpired(const XVirtualKeyboardTrace* self)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    return priv ? priv->m_hideExpired : false;
}

/* ==================== 信号（纯 ID getter） ==================== */

void* XVirtualKeyboardTrace_traceIdChanged_signal(
        XVirtualKeyboardTrace* self, int traceId)
{
    (void)self; (void)traceId;
    return (void*)(size_t)XVirtualKeyboardTrace_traceIdChanged_signal;
}

void* XVirtualKeyboardTrace_channelsChanged_signal(
        XVirtualKeyboardTrace* self)
{
    (void)self;
    return (void*)(size_t)XVirtualKeyboardTrace_channelsChanged_signal;
}

void* XVirtualKeyboardTrace_lengthChanged_signal(
        XVirtualKeyboardTrace* self, int length)
{
    (void)self; (void)length;
    return (void*)(size_t)XVirtualKeyboardTrace_lengthChanged_signal;
}

void* XVirtualKeyboardTrace_finalChanged_signal(
        XVirtualKeyboardTrace* self, bool isFinal)
{
    (void)self; (void)isFinal;
    return (void*)(size_t)XVirtualKeyboardTrace_finalChanged_signal;
}

void* XVirtualKeyboardTrace_canceledChanged_signal(
        XVirtualKeyboardTrace* self, bool isCanceled)
{
    (void)self; (void)isCanceled;
    return (void*)(size_t)XVirtualKeyboardTrace_canceledChanged_signal;
}

void* XVirtualKeyboardTrace_opacityChanged_signal(
        XVirtualKeyboardTrace* self, float opacity)
{
    (void)self; (void)opacity;
    return (void*)(size_t)XVirtualKeyboardTrace_opacityChanged_signal;
}

#endif /* XVIRTUALKEYBOARD_ON */
