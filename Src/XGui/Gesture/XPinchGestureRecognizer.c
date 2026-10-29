/**
 * @file       XPinchGestureRecognizer.c
 * @brief      双指缩放手势识别器实现（对标 Qt 6.8 QPinchGesture）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XPinchGestureRecognizer.h"
#include "XGestureRecognizer_Protected.h"
#include "XMemory.h"
#include "XWidget.h"
#include "XGuiConfig.h"
#include <math.h>

#if XGESTURE_ON

/** @brief 两指间距（px）。 */
static float xpinch_distance(const XTouchPoint* a, const XTouchPoint* b)
{
    double dx = (double)b->m_position.x - (double)a->m_position.x;
    double dy = (double)b->m_position.y - (double)a->m_position.y;
    return (float)sqrt(dx * dx + dy * dy);
}

/** @brief 判定：恰好两指；单步比例=当前间距/上一帧间距。 */
static int VPinchGestureRecognizer_recognize(XGestureRecognizer* base,
                                              const XTouchEvent* event,
                                              const XTouchPoint* points,
                                              int pointCount, int64_t nowMs)
{
    XPinchGestureRecognizer* self = (XPinchGestureRecognizer*)base;
    XEventType type;
    float distance;
    XPoint center;
    if (!self || !event || !points)
        return (int)XGestureRecognizerResult_Ignore;
    (void)nowMs;
    type = (XEventType)event->m_class.m_type;
    if (pointCount < 2) {
        /* 不足两指：缩放终止（Qt：点数变化即收口）。 */
        if (type == XEVENT_TYPE_TOUCH_END ||
            type == XEVENT_TYPE_TOUCH_CANCEL) {
            if (self->m_started) {
                self->m_started = false;
                return (int)XGestureRecognizerResult_CancelGesture;
            }
        }
        return (int)XGestureRecognizerResult_Ignore;
    }
    distance = xpinch_distance(&points[0], &points[1]);
    center.x = (short)(((int)points[0].m_position.x +
                         (int)points[1].m_position.x) / 2);
    center.y = (short)(((int)points[0].m_position.y +
                         (int)points[1].m_position.y) / 2);
    if (type == XEVENT_TYPE_TOUCH_BEGIN) {
        self->m_prevDistance = distance;
        self->m_totalScale = 1.0f;
        self->m_started = false;
        XGestureRecognizer_setOutputPosition(base, &center, &center);
        XGestureRecognizer_setOutputFingerCount(base, pointCount);
        return (int)XGestureRecognizerResult_MayBeGesture;
    }
    if (type == XEVENT_TYPE_TOUCH_CANCEL) {
        if (self->m_started) {
            self->m_started = false;
            return (int)XGestureRecognizerResult_CancelGesture;
        }
        return (int)XGestureRecognizerResult_Ignore;
    }
    if (type == XEVENT_TYPE_TOUCH_END) {
        if (self->m_started) {
            self->m_started = false;
            XGestureRecognizer_setOutputPosition(base, &center, &center);
            XGestureRecognizer_setOutputFingerCount(base, pointCount);
            return (int)XGestureRecognizerResult_FinishGesture;
        }
        return (int)XGestureRecognizerResult_Ignore;
    }
    /* UPDATE：单步比例（Qt 口径：超 [0.1,2.0] 视为抖动丢弃该步）。 */
    if (self->m_prevDistance <= 0.0f) {
        self->m_prevDistance = distance;
        return (int)XGestureRecognizerResult_MayBeGesture;
    }
    {
        float scale = distance / self->m_prevDistance;
        if (scale < self->m_minScale || scale > self->m_maxScale) {
            self->m_prevDistance = distance;
            return (int)XGestureRecognizerResult_Ignore; /* 抖动步丢弃。 */
        }
        self->m_prevDistance = distance;
        self->m_totalScale *= scale;
        self->m_started = true;
        XGestureRecognizer_setOutputPosition(base, &center, &center);
        XGestureRecognizer_setOutputScale(base, scale, self->m_totalScale);
        XGestureRecognizer_setOutputFingerCount(base, pointCount);
        return (int)XGestureRecognizerResult_TriggerGesture;
    }
}

/* ==================== 虚函数表与生命周期 ==================== */

XVtable* XPinchGestureRecognizer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPinchGestureRecognizer)
    XVTABLE_INHERIT_XCLASS(XGestureRecognizer);
    XVTABLE_OVERLOAD_DEFAULT(EXGestureRecognizer_Recognize,
                             VPinchGestureRecognizer_recognize);
    return XVTABLE_DEFAULT;
}

void XPinchGestureRecognizer_init(XPinchGestureRecognizer* self,
                                  XObject* parent)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XGestureRecognizer_init(&self->m_base, parent,
                            (int)XGestureEventType_Pinch);
    XClassSetVtable(self, XPinchGestureRecognizer);
    /* 判定回调登记（函数指针登记：框架内 XContainer 的
       m_dataCopyMethod 族同款先例，识别器族多层派生不依赖
       虚表槽位继承）。 */
    self->m_base.m_recognize = VPinchGestureRecognizer_recognize;
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_minScale = 0.1f;
    self->m_maxScale = 2.0f;
    self->m_totalScale = 1.0f;
}

XPinchGestureRecognizer* XPinchGestureRecognizer_create_ex(
    XMemoryType memory, XObject* parent)
{
    XPinchGestureRecognizer* self =
        (XPinchGestureRecognizer*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XPinchGestureRecognizer_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XPinchGestureRecognizer_setScaleRange(XPinchGestureRecognizer* self,
                                            float minScale, float maxScale)
{
    if (!self) return;
    if (minScale <= 0.0f) minScale = 0.01f;
    if (maxScale <= minScale) maxScale = minScale;
    self->m_minScale = minScale;
    self->m_maxScale = maxScale;
}

#endif /* XGESTURE_ON */

#endif /* XGUI_ON */
