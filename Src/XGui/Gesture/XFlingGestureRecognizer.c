/**
 * @file       XFlingGestureRecognizer.c
 * @brief      甩动手势识别器实现（框架扩展手势）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XFlingGestureRecognizer.h"
#include "XGestureRecognizer_Protected.h"
#include "XMemory.h"
#include "XWidget.h"
#include "XGuiConfig.h"

#if XGESTURE_ON

/** @brief 判定：抬手瞬间按位移与速度定案。 */
static int VFlingGestureRecognizer_recognize(XGestureRecognizer* base,
                                              const XTouchEvent* event,
                                              const XTouchPoint* points,
                                              int pointCount, int64_t nowMs)
{
    XFlingGestureRecognizer* self = (XFlingGestureRecognizer*)base;
    XEventType type;
    XPoint pos;
    if (!self || !event || !points || pointCount <= 0)
        return (int)XGestureRecognizerResult_Ignore;
    type = (XEventType)event->m_class.m_type;
    pos = points[0].m_position;
    if (type == XEVENT_TYPE_TOUCH_BEGIN) {
        self->m_pressX = pos.x;
        self->m_pressY = pos.y;
        self->m_pressMs = nowMs;
        XGestureRecognizer_setOutputPosition(base, &pos, &pos);
        XGestureRecognizer_setOutputFingerCount(base, pointCount);
        return (int)XGestureRecognizerResult_MayBeGesture;
    }
    if (type == XEVENT_TYPE_TOUCH_CANCEL)
        return (int)XGestureRecognizerResult_CancelGesture;
    if (type != XEVENT_TYPE_TOUCH_END)
        return (int)XGestureRecognizerResult_Ignore;
    {
        int dx = pos.x - self->m_pressX;
        int dy = pos.y - self->m_pressY;
        int64_t dt = nowMs - self->m_pressMs;
        int dist = (dx >= 0 ? dx : -dx) + (dy >= 0 ? dy : -dy);
        int velocity;
        int direction;
        if (dt <= 0) return (int)XGestureRecognizerResult_Ignore;
        if (dist < self->m_minDistance)
            return (int)XGestureRecognizerResult_Ignore;
        velocity = (int)(dist * 1000 / dt);
        if (velocity < self->m_minVelocity)
            return (int)XGestureRecognizerResult_Ignore;
        if ((dx >= 0 ? dx : -dx) >= (dy >= 0 ? dy : -dy))
            direction = (dx >= 0) ? (int)XGestureDirection_Right
                                  : (int)XGestureDirection_Left;
        else
            direction = (dy >= 0) ? (int)XGestureDirection_Down
                                  : (int)XGestureDirection_Up;
        XGestureRecognizer_setOutputPosition(base, &pos, &pos);
        XGestureRecognizer_setOutputVelocity(
            base, (float)(dx * 1000.0 / (double)dt),
            (float)(dy * 1000.0 / (double)dt));
        XGestureRecognizer_setOutputDirection(base, direction, 0.0f);
        XGestureRecognizer_setOutputFingerCount(base, pointCount);
        return (int)XGestureRecognizerResult_FinishGesture;
    }
}

/* ==================== 虚函数表与生命周期 ==================== */

XVtable* XFlingGestureRecognizer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFlingGestureRecognizer)
    XVTABLE_INHERIT_XCLASS(XGestureRecognizer);
    XVTABLE_OVERLOAD_DEFAULT(EXGestureRecognizer_Recognize,
                             VFlingGestureRecognizer_recognize);
    return XVTABLE_DEFAULT;
}

void XFlingGestureRecognizer_init(XFlingGestureRecognizer* self,
                                  XObject* parent)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XGestureRecognizer_init(&self->m_base, parent,
                            (int)XGestureEventType_Fling);
    XClassSetVtable(self, XFlingGestureRecognizer);
    /* 判定回调登记（函数指针登记：框架内 XContainer 的
       m_dataCopyMethod 族同款先例，识别器族多层派生不依赖
       虚表槽位继承）。 */
    self->m_base.m_recognize = VFlingGestureRecognizer_recognize;
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_minDistance = 20;
    self->m_minVelocity = 500;
}

XFlingGestureRecognizer* XFlingGestureRecognizer_create_ex(
    XMemoryType memory, XObject* parent)
{
    XFlingGestureRecognizer* self =
        (XFlingGestureRecognizer*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XFlingGestureRecognizer_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XFlingGestureRecognizer_setMinDistance(XFlingGestureRecognizer* self,
                                            int minDistance)
{
    if (!self) return;
    if (minDistance < 0) minDistance = 0;
    self->m_minDistance = minDistance;
}

void XFlingGestureRecognizer_setMinVelocity(XFlingGestureRecognizer* self,
                                            int minVelocity)
{
    if (!self) return;
    if (minVelocity < 0) minVelocity = 0;
    self->m_minVelocity = minVelocity;
}

#endif /* XGESTURE_ON */

#endif /* XGUI_ON */
