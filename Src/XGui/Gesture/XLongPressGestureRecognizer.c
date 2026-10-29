/**
 * @file       XLongPressGestureRecognizer.c
 * @brief      长按手势识别器实现（对标 Qt 6.8 QTapAndHoldGesture）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XLongPressGestureRecognizer.h"
#include "XGestureRecognizer_Protected.h"
#include "XMemory.h"
#include "XWidget.h"
#include "XGuiConfig.h"

#if XGESTURE_ON

/** @brief 判定：按下计时，超时触发；位移超容差即取消。 */
static int VLongPressGestureRecognizer_recognize(XGestureRecognizer* base,
                                                  const XTouchEvent* event,
                                                  const XTouchPoint* points,
                                                  int pointCount, int64_t nowMs)
{
    XLongPressGestureRecognizer* self = (XLongPressGestureRecognizer*)base;
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
        self->m_fired = false;
        XGestureRecognizer_setOutputPosition(base, &pos, &pos);
        XGestureRecognizer_setOutputFingerCount(base, pointCount);
        return (int)XGestureRecognizerResult_MayBeGesture;
    }
    if (type == XEVENT_TYPE_TOUCH_CANCEL) {
        self->m_fired = false;
        return (int)XGestureRecognizerResult_CancelGesture;
    }
    if (type == XEVENT_TYPE_TOUCH_END) {
        self->m_fired = false;
        /* 未达时长即抬起=点按（长按不成立），放行给点按手势。 */
        return (int)XGestureRecognizerResult_Ignore;
    }
    /* UPDATE：位移超容差即取消长按（用户已拖走）。 */
    {
        int dx = pos.x - self->m_pressX;
        int dy = pos.y - self->m_pressY;
        int dist = (dx >= 0 ? dx : -dx) + (dy >= 0 ? dy : -dy);
        if (dist > self->m_maxDistance) {
            self->m_fired = false;
            return (int)XGestureRecognizerResult_CancelGesture;
        }
    }
    if (!self->m_fired &&
        nowMs - self->m_pressMs >= (int64_t)self->m_duration) {
        self->m_fired = true;
        XGestureRecognizer_setOutputPosition(base, &pos, &pos);
        XGestureRecognizer_setOutputFingerCount(base, pointCount);
        return (int)XGestureRecognizerResult_TriggerGesture;
    }
    XGestureRecognizer_setOutputPosition(base, &pos, &pos);
    XGestureRecognizer_setOutputFingerCount(base, pointCount);
    return (int)XGestureRecognizerResult_MayBeGesture;
}

/* ==================== 虚函数表与生命周期 ==================== */

XVtable* XLongPressGestureRecognizer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLongPressGestureRecognizer)
    XVTABLE_INHERIT_XCLASS(XGestureRecognizer);
    XVTABLE_OVERLOAD_DEFAULT(EXGestureRecognizer_Recognize,
                             VLongPressGestureRecognizer_recognize);
    return XVTABLE_DEFAULT;
}

void XLongPressGestureRecognizer_init(XLongPressGestureRecognizer* self,
                                      XObject* parent)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XGestureRecognizer_init(&self->m_base, parent,
                            (int)XGestureEventType_TapAndHold);
    XClassSetVtable(self, XLongPressGestureRecognizer);
    /* 判定回调登记（函数指针登记：框架内 XContainer 的
       m_dataCopyMethod 族同款先例，识别器族多层派生不依赖
       虚表槽位继承）。 */
    self->m_base.m_recognize = VLongPressGestureRecognizer_recognize;
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_duration = 700;
    self->m_maxDistance = 40;
}

XLongPressGestureRecognizer* XLongPressGestureRecognizer_create_ex(
    XMemoryType memory, XObject* parent)
{
    XLongPressGestureRecognizer* self =
        (XLongPressGestureRecognizer*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XLongPressGestureRecognizer_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XLongPressGestureRecognizer_setDuration(
    XLongPressGestureRecognizer* self, int duration)
{
    if (!self) return;
    if (duration < 0) duration = 0;
    self->m_duration = duration;
}

void XLongPressGestureRecognizer_setMaxDistance(
    XLongPressGestureRecognizer* self, int maxDistance)
{
    if (!self) return;
    if (maxDistance < 0) maxDistance = 0;
    self->m_maxDistance = maxDistance;
}

#endif /* XGESTURE_ON */

#endif /* XGUI_ON */
