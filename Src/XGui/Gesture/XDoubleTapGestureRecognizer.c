/**
 * @file       XDoubleTapGestureRecognizer.c
 * @brief      双击手势识别器实现（框架扩展手势）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XDoubleTapGestureRecognizer.h"
#include "XGestureRecognizer_Protected.h"
#include "XMemory.h"
#include "XWidget.h"
#include "XGuiConfig.h"

#if XGESTURE_ON

/** @brief 判定：第一次点按收口记账，第二次在间隔与距离内即判双击。 */
static int VDoubleTapGestureRecognizer_recognize(XGestureRecognizer* base,
                                                  const XTouchEvent* event,
                                                  const XTouchPoint* points,
                                                  int pointCount, int64_t nowMs)
{
    XDoubleTapGestureRecognizer* self = (XDoubleTapGestureRecognizer*)base;
    XEventType type;
    XPoint pos;
    if (!self || !event || !points || pointCount <= 0)
        return (int)XGestureRecognizerResult_Ignore;
    type = (XEventType)event->m_class.m_type;
    pos = points[0].m_position;
    if (type == XEVENT_TYPE_TOUCH_BEGIN) {
        self->m_pressX = pos.x;
        self->m_pressY = pos.y;
        self->m_pressed = true;
        XGestureRecognizer_setOutputPosition(base, &pos, &pos);
        XGestureRecognizer_setOutputFingerCount(base, pointCount);
        return (int)XGestureRecognizerResult_MayBeGesture;
    }
    if (type == XEVENT_TYPE_TOUCH_CANCEL) {
        self->m_pressed = false;
        return (int)XGestureRecognizerResult_CancelGesture;
    }
    if (type != XEVENT_TYPE_TOUCH_END)
        return (int)XGestureRecognizerResult_Ignore;
    if (!self->m_pressed)
        return (int)XGestureRecognizerResult_Ignore;
    self->m_pressed = false;
    /* 与上次点按比对：间隔与距离均在阈值内 → 双击。 */
    if (self->m_firstMs > 0 &&
        nowMs - self->m_firstMs <= (int64_t)self->m_interval) {
        int dx = pos.x - self->m_firstX;
        int dy = pos.y - self->m_firstY;
        int dist = (dx >= 0 ? dx : -dx) + (dy >= 0 ? dy : -dy);
        if (dist <= self->m_maxDistance) {
            self->m_firstMs = 0;
            XGestureRecognizer_setOutputPosition(base, &pos, &pos);
            XGestureRecognizer_setOutputFingerCount(base, pointCount);
            return (int)XGestureRecognizerResult_FinishGesture;
        }
    }
    /* 记为首次点按，等待下一次。 */
    self->m_firstX = pos.x;
    self->m_firstY = pos.y;
    self->m_firstMs = nowMs;
    XGestureRecognizer_setOutputPosition(base, &pos, &pos);
    XGestureRecognizer_setOutputFingerCount(base, pointCount);
    return (int)XGestureRecognizerResult_Ignore;
}

/* ==================== 虚函数表与生命周期 ==================== */

XVtable* XDoubleTapGestureRecognizer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDoubleTapGestureRecognizer)
    XVTABLE_INHERIT_XCLASS(XGestureRecognizer);
    XVTABLE_OVERLOAD_DEFAULT(EXGestureRecognizer_Recognize,
                             VDoubleTapGestureRecognizer_recognize);
    return XVTABLE_DEFAULT;
}

void XDoubleTapGestureRecognizer_init(XDoubleTapGestureRecognizer* self,
                                      XObject* parent)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XGestureRecognizer_init(&self->m_base, parent,
                            (int)XGestureEventType_DoubleTap);
    XClassSetVtable(self, XDoubleTapGestureRecognizer);
    /* 判定回调登记（函数指针登记：框架内 XContainer 的
       m_dataCopyMethod 族同款先例，识别器族多层派生不依赖
       虚表槽位继承）。 */
    self->m_base.m_recognize = VDoubleTapGestureRecognizer_recognize;
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_interval = 400;
    self->m_maxDistance = 40;
}

XDoubleTapGestureRecognizer* XDoubleTapGestureRecognizer_create_ex(
    XMemoryType memory, XObject* parent)
{
    XDoubleTapGestureRecognizer* self =
        (XDoubleTapGestureRecognizer*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XDoubleTapGestureRecognizer_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XDoubleTapGestureRecognizer_setInterval(
    XDoubleTapGestureRecognizer* self, int interval)
{
    if (!self) return;
    if (interval < 0) interval = 0;
    self->m_interval = interval;
}

void XDoubleTapGestureRecognizer_setMaxDistance(
    XDoubleTapGestureRecognizer* self, int maxDistance)
{
    if (!self) return;
    if (maxDistance < 0) maxDistance = 0;
    self->m_maxDistance = maxDistance;
}

#endif /* XGESTURE_ON */

#endif /* XGUI_ON */
