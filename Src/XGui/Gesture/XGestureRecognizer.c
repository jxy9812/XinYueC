/**
 * @file       XGestureRecognizer.c
 * @brief      触摸手势识别器基类实现（对标 Qt 6.8 QGestureRecognizer）。
 * @details    基类承担模板方法职责：帧驱动 → 调用派生类判定 → 按结果
 *             迁移状态机 → 构造 XGestureEvent → 经事件循环投递到目标
 *             控件。派生类只需在 Recognize 虚槽给出判定结果并填写输出
 *             数据（经 XGestureRecognizer_Protected.h 的 setter）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XGestureRecognizer.h"
#include "XGestureRecognizer_Protected.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
#include "XWindowSystemInterface.h"
#include "XWidget.h"
#include "XGuiConfig.h"

#if XGESTURE_ON

/* ==================== 内部工具 ==================== */

/** @brief 手势时钟域当前毫秒（触摸时间戳域优先，同序列各帧同域可比）。 */
int64_t XGestureRecognizer_nowMs(void)
{
#if XWINDOWSYSTEMINTERFACE_ON && XWINDOW_ON && XWINDOWEVENT_ON
    {
        uint32_t ts = XWindowSystemInterface_touchTimestamp();
        if (ts != 0) return (int64_t)ts;
    }
#endif
    return XDateTime_currentMSecsSinceEpoch();
}

/* ==================== 虚函数实现 ==================== */

/**
 * @brief 默认判定：恒忽略（基类不识别任何手势；派生类必须重载）。
 */
static int VXGestureRecognizer_recognize(XGestureRecognizer* self,
                                          const XTouchEvent* event,
                                          const XTouchPoint* points,
                                          int pointCount, int64_t nowMs)
{
    (void)self; (void)event; (void)points; (void)pointCount; (void)nowMs;
    return (int)XGestureRecognizerResult_Ignore;
}

/* ==================== 虚函数表与生命周期 ==================== */

XVtable* XGestureRecognizer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGestureRecognizer)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXGestureRecognizer_Recognize,
                             VXGestureRecognizer_recognize);
    return XVTABLE_DEFAULT;
}

void XGestureRecognizer_init(XGestureRecognizer* self, XObject* parent,
                              int type)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init(&self->m_class);
    if (parent) XObject_setParent(&self->m_class, parent);
    XClassSetVtable(self, XGestureRecognizer);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_gestureType = (uint8_t)type;
    self->m_state = (uint8_t)XGestureState_NoGesture;
    self->m_active = false;
    self->m_pointCount = 0;
    self->m_startMs = 0;
    self->m_lastMs = 0;
    self->m_output.m_scaleFactor = 1.0f;
    self->m_output.m_totalScaleFactor = 1.0f;
    self->m_prevPointCount = 0;
}

XGestureRecognizer* XGestureRecognizer_create_ex(XMemoryType memory,
                                                 XObject* parent, int type)
{
    XGestureRecognizer* self =
        (XGestureRecognizer*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XGestureRecognizer_init(self, parent, type);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 配置 ==================== */

void XGestureRecognizer_setTarget(XGestureRecognizer* self, XObject* target)
{
    if (!self) return;
    self->m_target = target;
}

XObject* XGestureRecognizer_target(const XGestureRecognizer* self)
{
    return self ? self->m_target : NULL;
}

XGestureEventType XGestureRecognizer_gestureType(
    const XGestureRecognizer* self)
{
    return self ? (XGestureEventType)self->m_gestureType
                : XGestureEventType_Tap;
}

XGestureState XGestureRecognizer_state(const XGestureRecognizer* self)
{
    return self ? (XGestureState)self->m_state : XGestureState_NoGesture;
}

void XGestureRecognizer_reset(XGestureRecognizer* self)
{
    if (!self) return;
    self->m_state = (uint8_t)XGestureState_NoGesture;
    self->m_active = false;
    self->m_startMs = 0;
    self->m_pointCount = 0;
    self->m_prevPointCount = 0;
}

/* ==================== 事件投递 ==================== */

/** @brief 构造并投递手势事件（经事件循环；Qt 同款 postEvent 语义）。 */
static void xgesture_deliver(XGestureRecognizer* self, int state)
{
    XGestureEvent* event;
    XObject* target = self->m_target;
    if (!target) return;
    event = XGestureEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                    XEVENT_TYPE_GESTURE);
    if (!event) return;
    event->m_gestureType = self->m_gestureType;
    event->m_state = (uint8_t)state;
    event->m_position = self->m_output.m_position;
    event->m_globalPosition = self->m_output.m_globalPosition;
    event->m_lastPosition = self->m_output.m_lastPosition;
    event->m_startPosition = self->m_output.m_startPosition;
    event->m_delta = self->m_output.m_delta;
    event->m_velocityX = self->m_output.m_velocityX;
    event->m_velocityY = self->m_output.m_velocityY;
    event->m_scaleFactor = self->m_output.m_scaleFactor;
    event->m_totalScaleFactor = self->m_output.m_totalScaleFactor;
    event->m_rotationAngle = self->m_output.m_rotationAngle;
    event->m_totalRotationAngle = self->m_output.m_totalRotationAngle;
    event->m_angle = self->m_output.m_angle;
    event->m_direction = self->m_output.m_direction;
    event->m_fingerCount = self->m_output.m_fingerCount;
    /* 手势事件默认接受（对齐 Qt QGestureEvent：Gesture 事件默认接受，
       未单独 ignore 的手势可继续沿父控件链上抛）。 */
    XEvent_accept((XEvent*)event);
    XCoreApplication_postEvent(target, (XEvent*)event,
                               XEVENT_PRIORITY_NORMAL);
}

/* ==================== 帧驱动 ==================== */

void XGestureRecognizer_processTouch(XGestureRecognizer* self,
                                      const XTouchEvent* event,
                                      const XTouchPoint* points,
                                      int pointCount, int64_t nowMs)
{
    int result;
    int stateMask;
    int i;
    if (!self) return;
    /* 触点数硬上限截断：前帧快照容量为 XGESTURE_MAX_POINTS，超出部分
       不参与判定（此前仅拷贝时截断而计数未截断，越界读栈上数组）。 */
    if (pointCount > XGESTURE_MAX_POINTS) pointCount = XGESTURE_MAX_POINTS;
    self->m_lastMs = nowMs;
    self->m_pointCount = pointCount;
    /* 输出暂存复位（派生类按本帧重填；缩放/速度默认中性值）。 */
    self->m_output.m_scaleFactor = 1.0f;
    self->m_output.m_totalScaleFactor = 1.0f;
    self->m_output.m_rotationAngle = 0.0f;
    self->m_output.m_totalRotationAngle = 0.0f;
    self->m_output.m_velocityX = 0.0f;
    self->m_output.m_velocityY = 0.0f;
    self->m_output.m_direction = (uint8_t)XGestureDirection_None;
    self->m_output.m_angle = 0.0f;
    /* 派生类判定（判定期间 m_prevPoints 仍是真正的上一帧快照）。 */
    result = XGestureRecognizer_recognize_base(self, event, points,
                                                pointCount, nowMs);
    /* 前帧快照滚动：判定完成后才备份本帧，供下一帧的帧间差分（缩放/
       旋转）使用——此前判定前覆盖，识别器在槽内读到的是本帧而非前帧。 */
    for (i = 0; points && i < pointCount; ++i)
        self->m_prevPoints[i] = points[i];
    self->m_prevPointCount = pointCount;
    stateMask = result & XGESTURE_RESULT_STATE_MASK;
    /* 状态迁移（对齐 Qt QGestureManager 的状态归并）：
       - Trigger：未激活→Started（投递 Started 事件）；已激活→Updated；
       - Finish：未激活（单次型，如点按）→先补一次 Started 再 Finished；
         已激活→Finished；
       - Cancel：已激活→Canceled；仅"可能"态→静默丢弃；
       - MayBe：已激活→视为 Finished（Qt：active 回退到 MayBe 即收口）；
       - Ignore：状态不变。 */
    if (stateMask == (int)XGestureRecognizerResult_TriggerGesture) {
        if (!self->m_active) {
            self->m_active = true;
            self->m_state = (uint8_t)XGestureState_Started;
            self->m_startMs = nowMs;
            xgesture_deliver(self, (int)XGestureState_Started);
        } else {
            self->m_state = (uint8_t)XGestureState_Updated;
            xgesture_deliver(self, (int)XGestureState_Updated);
        }
    } else if (stateMask == (int)XGestureRecognizerResult_FinishGesture) {
        if (!self->m_active) {
            /* 单次型手势（点按/双击/长按）：Qt 口径为伪造一次 Started
               再投 Finished，消费方据此能收到完整的状态序列。 */
            self->m_active = true;
            self->m_startMs = nowMs;
            self->m_state = (uint8_t)XGestureState_Started;
            xgesture_deliver(self, (int)XGestureState_Started);
        }
        self->m_active = false;
        self->m_state = (uint8_t)XGestureState_Finished;
        xgesture_deliver(self, (int)XGestureState_Finished);
    } else if (stateMask == (int)XGestureRecognizerResult_CancelGesture) {
        if (self->m_active) {
            self->m_active = false;
            self->m_state = (uint8_t)XGestureState_Canceled;
            xgesture_deliver(self, (int)XGestureState_Canceled);
        }
    } else if (stateMask == (int)XGestureRecognizerResult_MayBeGesture) {
        if (self->m_active) {
            self->m_active = false;
            self->m_state = (uint8_t)XGestureState_Finished;
            xgesture_deliver(self, (int)XGestureState_Finished);
        }
    }
}

int XGestureRecognizer_recognize_base(XGestureRecognizer* self,
                                       const XTouchEvent* event,
                                       const XTouchPoint* points,
                                       int pointCount, int64_t nowMs)
{
    /* 判定回调由派生类构造时登记（函数指针登记，框架内 XContainer
     * m_dataCopyMethod 同款先例）：识别器族多层派生不依赖虚表槽位继承。 */
    if (!self || !self->m_recognize)
        return (int)XGestureRecognizerResult_Ignore;
    return self->m_recognize(self, event, points, pointCount, nowMs);
}

/* ==================== 子类输出数据填写（Protected） ==================== */

void XGestureRecognizer_setOutputPosition(XGestureRecognizer* self,
                                          const XPoint* position,
                                          const XPoint* startPosition)
{
    if (!self) return;
    if (position) self->m_output.m_position = *position;
    if (startPosition) self->m_output.m_startPosition = *startPosition;
}

void XGestureRecognizer_setOutputDelta(XGestureRecognizer* self,
                                        const XPoint* delta,
                                        const XPoint* lastPosition)
{
    if (!self) return;
    if (delta) self->m_output.m_delta = *delta;
    if (lastPosition) self->m_output.m_lastPosition = *lastPosition;
}

void XGestureRecognizer_setOutputVelocity(XGestureRecognizer* self,
                                           float velocityX, float velocityY)
{
    if (!self) return;
    self->m_output.m_velocityX = velocityX;
    self->m_output.m_velocityY = velocityY;
}

void XGestureRecognizer_setOutputScale(XGestureRecognizer* self,
                                        float scaleFactor,
                                        float totalScaleFactor)
{
    if (!self) return;
    self->m_output.m_scaleFactor = scaleFactor;
    self->m_output.m_totalScaleFactor = totalScaleFactor;
}

void XGestureRecognizer_setOutputRotation(XGestureRecognizer* self,
                                           float rotationAngle,
                                           float totalRotationAngle)
{
    if (!self) return;
    self->m_output.m_rotationAngle = rotationAngle;
    self->m_output.m_totalRotationAngle = totalRotationAngle;
}

void XGestureRecognizer_setOutputDirection(XGestureRecognizer* self,
                                            int direction, float angle)
{
    if (!self) return;
    self->m_output.m_direction = (uint8_t)direction;
    self->m_output.m_angle = angle;
}

void XGestureRecognizer_setOutputFingerCount(XGestureRecognizer* self,
                                             int count)
{
    if (!self) return;
    if (count < 0) count = 0;
    if (count > 255) count = 255;
    self->m_output.m_fingerCount = (uint8_t)count;
}

const XTouchPoint* XGestureRecognizer_prevPoints(
    const XGestureRecognizer* self, int* outCount)
{
    if (outCount) *outCount = 0;
    if (!self || self->m_prevPointCount <= 0) return NULL;
    if (outCount) *outCount = self->m_prevPointCount;
    return self->m_prevPoints;
}

int64_t XGestureRecognizer_startMs(const XGestureRecognizer* self)
{
    return self ? self->m_startMs : 0;
}

int64_t XGestureRecognizer_lastMs(const XGestureRecognizer* self)
{
    return self ? self->m_lastMs : 0;
}

#endif /* XGESTURE_ON */

#endif /* XGUI_ON */
