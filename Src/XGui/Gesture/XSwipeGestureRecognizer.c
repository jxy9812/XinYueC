/**
 * @file       XSwipeGestureRecognizer.c
 * @brief      轻扫手势识别器实现（对标 Qt 6.8 QSwipeGesture）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XSwipeGestureRecognizer.h"
#include "XGestureRecognizer_Protected.h"
#include "XMemory.h"
#include "XWidget.h"
#include "XGuiConfig.h"
#include <math.h>

#if XGESTURE_ON

/** @brief 判定：位移越阈进入候选，抬手时按速度与时长定案。 */
static int VSwipeGestureRecognizer_recognize(XGestureRecognizer* base,
                                             const XTouchEvent* event,
                                             const XTouchPoint* points,
                                             int pointCount, int64_t nowMs)
{
    XSwipeGestureRecognizer* self = (XSwipeGestureRecognizer*)base;
    XEventType type;
    XPoint pos;
    if (!self || !event || !points || pointCount <= 0)
        return (int)XGestureRecognizerResult_Ignore;
    type = (XEventType)event->m_class.m_type;
    pos = points[0].m_position;
    if (type == XEVENT_TYPE_TOUCH_BEGIN) {
        self->m_pressX = pos.x;
        self->m_pressY = pos.y;
        self->m_lastX = pos.x;
        self->m_lastY = pos.y;
        self->m_pressMs = nowMs;
        self->m_moved = false;
        self->m_fired = false;
        XGestureRecognizer_setOutputPosition(base, &pos, &pos);
        XGestureRecognizer_setOutputFingerCount(base, pointCount);
        return (int)XGestureRecognizerResult_MayBeGesture;
    }
    if (type == XEVENT_TYPE_TOUCH_CANCEL) {
        self->m_moved = false;
        self->m_fired = false;
        return (int)XGestureRecognizerResult_CancelGesture;
    }
    if (type == XEVENT_TYPE_TOUCH_END) {
        int dx;
        int dy;
        int64_t dt;
        int velocity;
        int direction;
        float angle;
        if (!self->m_moved || self->m_fired) {
            self->m_moved = false;
            return (int)XGestureRecognizerResult_Ignore;
        }
        dx = pos.x - self->m_pressX;
        dy = pos.y - self->m_pressY;
        dt = nowMs - self->m_pressMs;
        if (dt <= 0 || dt > (int64_t)self->m_maxDuration) {
            /* 按压过久=长按/拖动，不是轻扫。 */
            self->m_moved = false;
            return (int)XGestureRecognizerResult_Ignore;
        }
        velocity = (int)(((dx >= 0 ? dx : -dx) +
                           (dy >= 0 ? dy : -dy)) * 1000 / dt);
        if (velocity < self->m_minVelocity) {
            self->m_moved = false;
            return (int)XGestureRecognizerResult_Ignore;
        }
        /* 主导轴方向 + 方向角（相对 x 轴，顺时针为正）。 */
        if ((dx >= 0 ? dx : -dx) >= (dy >= 0 ? dy : -dy)) {
            direction = (dx >= 0) ? (int)XGestureDirection_Right
                                  : (int)XGestureDirection_Left;
        } else {
            direction = (dy >= 0) ? (int)XGestureDirection_Down
                                  : (int)XGestureDirection_Up;
        }
        angle = 0.0f;
        if (dx != 0 || dy != 0)
            angle = (float)(atan2((double)dy, (double)dx) * 180.0 /
                            3.14159265358979);
        self->m_fired = true;
        self->m_moved = false;
        XGestureRecognizer_setOutputPosition(base, &pos, &pos);
        XGestureRecognizer_setOutputDirection(base, direction, angle);
        XGestureRecognizer_setOutputFingerCount(base, pointCount);
        XGestureRecognizer_setOutputVelocity(
            base, (float)(dx * 1000.0 / (double)dt),
            (float)(dy * 1000.0 / (double)dt));
        return (int)XGestureRecognizerResult_FinishGesture;
    }
    /* UPDATE：位移越阈进入候选（对标 Qt 的位移阈值口径）。 */
    {
        int dx = pos.x - self->m_pressX;
        int dy = pos.y - self->m_pressY;
        int adx = dx >= 0 ? dx : -dx;
        int ady = dy >= 0 ? dy : -dy;
        if (!self->m_moved) {
            if (adx < self->m_minDistance && ady < self->m_minDistance)
                return (int)XGestureRecognizerResult_MayBeGesture;
            self->m_moved = true;
        }
    }
    self->m_lastX = pos.x;
    self->m_lastY = pos.y;
    XGestureRecognizer_setOutputPosition(base, &pos, &pos);
    XGestureRecognizer_setOutputFingerCount(base, pointCount);
    return (int)XGestureRecognizerResult_MayBeGesture;
}

/* ==================== 虚函数表与生命周期 ==================== */

XVtable* XSwipeGestureRecognizer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSwipeGestureRecognizer)
    XVTABLE_INHERIT_XCLASS(XGestureRecognizer);
    XVTABLE_OVERLOAD_DEFAULT(EXGestureRecognizer_Recognize,
                             VSwipeGestureRecognizer_recognize);
    return XVTABLE_DEFAULT;
}

void XSwipeGestureRecognizer_init(XSwipeGestureRecognizer* self,
                                  XObject* parent)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XGestureRecognizer_init(&self->m_base, parent,
                            (int)XGestureEventType_Swipe);
    XClassSetVtable(self, XSwipeGestureRecognizer);
    /* 判定回调登记（函数指针登记：框架内 XContainer 的
       m_dataCopyMethod 族同款先例，识别器族多层派生不依赖
       虚表槽位继承）。 */
    self->m_base.m_recognize = VSwipeGestureRecognizer_recognize;
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_minDistance = 50;
    self->m_minVelocity = 300;
    self->m_maxDuration = 300;
}

XSwipeGestureRecognizer* XSwipeGestureRecognizer_create_ex(
    XMemoryType memory, XObject* parent)
{
    XSwipeGestureRecognizer* self =
        (XSwipeGestureRecognizer*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XSwipeGestureRecognizer_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XSwipeGestureRecognizer_setMinDistance(XSwipeGestureRecognizer* self,
                                            int minDistance)
{
    if (!self) return;
    if (minDistance < 0) minDistance = 0;
    self->m_minDistance = minDistance;
}

void XSwipeGestureRecognizer_setMinVelocity(XSwipeGestureRecognizer* self,
                                            int minVelocity)
{
    if (!self) return;
    if (minVelocity < 0) minVelocity = 0;
    self->m_minVelocity = minVelocity;
}

void XSwipeGestureRecognizer_setMaxDuration(XSwipeGestureRecognizer* self,
                                            int maxDuration)
{
    if (!self) return;
    if (maxDuration < 0) maxDuration = 0;
    self->m_maxDuration = maxDuration;
}

#endif /* XGESTURE_ON */

#endif /* XGUI_ON */
