/**
 * @file       XRotateGestureRecognizer.c
 * @brief      双指旋转手势识别器实现（框架扩展手势）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XRotateGestureRecognizer.h"
#include "XGestureRecognizer_Protected.h"
#include "XMemory.h"
#include "XWidget.h"
#include "XGuiConfig.h"
#include <math.h>

#if XGESTURE_ON

/** @brief 两指连线角（度，-180~180，顺时针为正）。 */
static float xrotate_angle(const XTouchPoint* a, const XTouchPoint* b)
{
    double dx = (double)b->m_position.x - (double)a->m_position.x;
    double dy = (double)b->m_position.y - (double)a->m_position.y;
    float deg = (float)(atan2(dy, dx) * 180.0 / 3.14159265358979);
    if (deg > 180.0f) deg -= 360.0f;
    if (deg <= -180.0f) deg += 360.0f;
    return deg;
}

/** @brief 判定：恰好两指；本帧角度差=当前角−上一帧角。 */
static int VRotateGestureRecognizer_recognize(XGestureRecognizer* base,
                                               const XTouchEvent* event,
                                               const XTouchPoint* points,
                                               int pointCount, int64_t nowMs)
{
    XRotateGestureRecognizer* self = (XRotateGestureRecognizer*)base;
    XEventType type;
    float angle;
    XPoint center;
    if (!self || !event || !points)
        return (int)XGestureRecognizerResult_Ignore;
    (void)nowMs;
    type = (XEventType)event->m_class.m_type;
    if (pointCount < 2) {
        if ((type == XEVENT_TYPE_TOUCH_END ||
             type == XEVENT_TYPE_TOUCH_CANCEL) && self->m_started) {
            self->m_started = false;
            return (int)XGestureRecognizerResult_CancelGesture;
        }
        return (int)XGestureRecognizerResult_Ignore;
    }
    angle = xrotate_angle(&points[0], &points[1]);
    center.x = (short)(((int)points[0].m_position.x +
                         (int)points[1].m_position.x) / 2);
    center.y = (short)(((int)points[0].m_position.y +
                         (int)points[1].m_position.y) / 2);
    if (type == XEVENT_TYPE_TOUCH_BEGIN) {
        self->m_prevAngle = angle;
        self->m_totalAngle = 0.0f;
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
    /* UPDATE：角度差（归一到 (-180,180]）。 */
    {
        float delta = angle - self->m_prevAngle;
        if (delta > 180.0f) delta -= 360.0f;
        if (delta <= -180.0f) delta += 360.0f;
        self->m_prevAngle = angle;
        if (delta > -self->m_minAngle && delta < self->m_minAngle)
            return (int)XGestureRecognizerResult_Ignore; /* 抖动步丢弃。 */
        self->m_totalAngle += delta;
        self->m_started = true;
        XGestureRecognizer_setOutputPosition(base, &center, &center);
        XGestureRecognizer_setOutputRotation(base, delta, self->m_totalAngle);
        XGestureRecognizer_setOutputFingerCount(base, pointCount);
        return (int)XGestureRecognizerResult_TriggerGesture;
    }
}

/* ==================== 虚函数表与生命周期 ==================== */

XVtable* XRotateGestureRecognizer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XRotateGestureRecognizer)
    XVTABLE_INHERIT_XCLASS(XGestureRecognizer);
    XVTABLE_OVERLOAD_DEFAULT(EXGestureRecognizer_Recognize,
                             VRotateGestureRecognizer_recognize);
    return XVTABLE_DEFAULT;
}

void XRotateGestureRecognizer_init(XRotateGestureRecognizer* self,
                                   XObject* parent)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XGestureRecognizer_init(&self->m_base, parent,
                            (int)XGestureEventType_Rotate);
    XClassSetVtable(self, XRotateGestureRecognizer);
    /* 判定回调登记（函数指针登记：框架内 XContainer 的
       m_dataCopyMethod 族同款先例，识别器族多层派生不依赖
       虚表槽位继承）。 */
    self->m_base.m_recognize = VRotateGestureRecognizer_recognize;
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_minAngle = 1.0f;
}

XRotateGestureRecognizer* XRotateGestureRecognizer_create_ex(
    XMemoryType memory, XObject* parent)
{
    XRotateGestureRecognizer* self =
        (XRotateGestureRecognizer*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XRotateGestureRecognizer_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XRotateGestureRecognizer_setMinAngle(XRotateGestureRecognizer* self,
                                           float minAngle)
{
    if (!self) return;
    if (minAngle < 0.0f) minAngle = 0.0f;
    self->m_minAngle = minAngle;
}

#endif /* XGESTURE_ON */

#endif /* XGUI_ON */
