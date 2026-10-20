/**
 * @file       XVirtualKeyboardTrace.c
 * @brief      XVirtualKeyboardTrace 手写轨迹容器实现（容器与引擎分派
 *             面；识别引擎本轮 N-A）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "CXinYueConfig.h"

#if XVIRTUALKEYBOARD_ON

#include "XVirtualKeyboardTrace.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XString.h"
#include "XVector.h"

/** @brief 通道数据（名字 + double 向量，元素对象均由通道拥有）。 */
typedef struct XVirtualKeyboardTraceChannel
{
    XString m_name; /**< 通道名（对象拥有）。 */
    XVector m_data; /**< 数据向量（对象拥有，元素 double，按需扩展）。 */
} XVirtualKeyboardTraceChannel;

/** @brief 私有数据块。 */
typedef struct XVirtualKeyboardTracePrivate
{
    int m_traceId;          /**< 轨迹 id（默认 -1，Qt 缺省）。 */
    XVector m_channels;     /**< 通道向量（元素 XVirtualKeyboardTraceChannel，
                                 带元素拷贝/移动/释放回调）。 */
    XVector m_points;       /**< 点向量（元素 XVirtualKeyboardTracePoint）。 */
    bool m_final;           /**< 一笔结束。 */
    bool m_canceled;        /**< 轨迹作废。 */
    float m_opacity;        /**< 透明度 [0,1]，默认 1。 */
    XTimerId m_hideTimer;   /**< 延时隐藏定时器。 */
    bool m_hideExpired;     /**< 延时隐藏已到期。 */
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

/** @brief 通道元素拷贝（通道 XVector 的元素复制回调）。 */
static void xvkt_channelCopy(void* data, const void* sData)
{
    XVirtualKeyboardTraceChannel* dest = (XVirtualKeyboardTraceChannel*)data;
    const XVirtualKeyboardTraceChannel* src =
        (const XVirtualKeyboardTraceChannel*)sData;
    XClassCopy(&dest->m_name, &src->m_name);
    XClassCopy(&dest->m_data, &src->m_data);
}

/** @brief 通道元素移动（通道 XVector 扩容/插入搬移的元素回调）。 */
static void xvkt_channelMove(void* data, const void* sData)
{
    XVirtualKeyboardTraceChannel* dest = (XVirtualKeyboardTraceChannel*)data;
    XVirtualKeyboardTraceChannel* src = (XVirtualKeyboardTraceChannel*)sData;
    XClassMove(&dest->m_name, &src->m_name);
    XClassMove(&dest->m_data, &src->m_data);
}

/** @brief 通道元素释放（通道 XVector 清理/析构的元素回调）。 */
static void xvkt_channelDeinit(void* data)
{
    XVirtualKeyboardTraceChannel* ch = (XVirtualKeyboardTraceChannel*)data;
    XClassDeinit(&ch->m_name);
    XClassDeinit(&ch->m_data);
}

/** @brief 通道查找（名字精确匹配；未找到返回 NULL）。 */
static XVirtualKeyboardTraceChannel* xvkt_findChannel(
        XVirtualKeyboardTracePrivate* priv, const char* channel)
{
    size_t i;
    size_t n;
    if (!priv || !channel) return NULL;
    n = XVector_size_base(&priv->m_channels);
    for (i = 0; i < n; ++i) {
        XVirtualKeyboardTraceChannel* ch =
            (XVirtualKeyboardTraceChannel*)XVector_at_base(&priv->m_channels,
                                                           (int64_t)i);
        if (XStrcmp(XString_toUtf8(&ch->m_name), channel) == 0)
            return ch;
    }
    return NULL;
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
        XClassDeinit(&priv->m_points);
        XClassDeinit(&priv->m_channels);
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
    XVector_init(&priv->m_channels, sizeof(XVirtualKeyboardTraceChannel),
                 false);
    XContainerSetDataCopyMethod(&priv->m_channels, xvkt_channelCopy);
    XContainerSetDataMoveMethod(&priv->m_channels, xvkt_channelMove);
    XContainerSetDataDeinitMethod(&priv->m_channels, xvkt_channelDeinit);
    XVector_init(&priv->m_points, sizeof(XVirtualKeyboardTracePoint), false);
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
    return priv ? (int)XVector_size_base(&priv->m_channels) : 0;
}

const char* XVirtualKeyboardTrace_channelAt(
        const XVirtualKeyboardTrace* self, int index)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    XVirtualKeyboardTraceChannel* ch;
    if (!priv || index < 0 ||
        index >= (int)XVector_size_base(&priv->m_channels))
        return NULL;
    ch = (XVirtualKeyboardTraceChannel*)XVector_at_base(&priv->m_channels,
                                                        index);
    return ch ? XString_toUtf8(&ch->m_name) : NULL;
}

void XVirtualKeyboardTrace_setChannels(XVirtualKeyboardTrace* self,
                                       const char* const* channels,
                                       int count)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    int i;
    if (!priv) return;
    XVector_clear_base(&priv->m_channels);
    for (i = 0; channels && i < count && channels[i]; ++i) {
        XVirtualKeyboardTraceChannel channel;
        bool ok;
        XString_init(&channel.m_name);
        XVector_init(&channel.m_data, sizeof(double), false);
        ok = XString_assign_utf8(&channel.m_name, channels[i]) &&
             XVector_push_back_move_1_base(&priv->m_channels, &channel);
        if (!ok) {
            /* 失败条目不占坑：就地释放本条后跳过（对齐原紧凑写入语义）。 */
            XClassDeinit(&channel.m_name);
            XClassDeinit(&channel.m_data);
        }
        /* 成功时成员已 move 入表，channel 留空，无需再反初始化。 */
    }
    xvkt_emit(self, (size_t)XVirtualKeyboardTrace_channelsChanged_signal(
                        NULL),
              NULL);
}

int XVirtualKeyboardTrace_length(const XVirtualKeyboardTrace* self)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    return priv ? (int)XVector_size_base(&priv->m_points) : 0;
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
    end = (int)XVector_size_base(&priv->m_points);
    if (pos > end) return 0;
    start = pos;
    end = (count < 0 || start + count > end) ? end : start + count;
    for (i = start; i < end; ++i) {
        if (outPoints && written < maxCount) {
            outPoints[written] =
                *(const XVirtualKeyboardTracePoint*)XVector_at_base(
                    &priv->m_points, i);
        }
        ++written;
    }
    return written;
}

int XVirtualKeyboardTrace_addPoint(XVirtualKeyboardTrace* self,
                                   const XVirtualKeyboardTracePoint* point)
{
    XVirtualKeyboardTracePrivate* priv = xvkt_priv(self);
    int count;
    if (!priv || !point) return -1;
    if (!XVector_push_back_1_base(&priv->m_points, point)) return -1;
    count = (int)XVector_size_base(&priv->m_points);
    xvkt_emit(self, (size_t)XVirtualKeyboardTrace_lengthChanged_signal(
                        NULL, 0),
              XVarList_Create(XVar(int, count)));
    return count - 1;
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
    if ((size_t)index >= XVector_size_base(&ch->m_data)) {
        /* 稀疏通道按需扩展：resize 将新增区间补 0（原语义）。 */
        if (!XVector_resize_base(&ch->m_data, (size_t)index + 1))
            return false;
    }
    *(double*)XVector_at_base(&ch->m_data, index) = data;
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
    end = (int)XVector_size_base(&ch->m_data);
    if (pos > end) return 0;
    start = pos;
    end = (count < 0 || start + count > end) ? end : start + count;
    for (i = start; i < end; ++i) {
        if (outData && written < maxCount) {
            outData[written] = *(const double*)XVector_at_base(&ch->m_data,
                                                               i);
        }
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

#endif /* XGUI_ON */
