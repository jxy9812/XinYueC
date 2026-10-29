/**
 * @file       XTapGestureRecognizer.c
 * @brief      点按手势识别器实现（对标 Qt 6.8 QTapGesture）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XTapGestureRecognizer.h"
#include "XGestureRecognizer_Protected.h"
#include "XMemory.h"
#include "XWidget.h"
#include "XGuiConfig.h"

#if XGESTURE_ON

/** @brief 判定：按下记起点，抬起时位移在容差内即判点按。 */
static int VTapGestureRecognizer_recognize(XGestureRecognizer* base,
                                            const XTouchEvent* event,
                                            const XTouchPoint* points,
                                            int pointCount, int64_t nowMs)
{
    XTapGestureRecognizer* self = (XTapGestureRecognizer*)base;
    XEventType type;
    XPoint pos;
    if (!self || !event || !points || pointCount <= 0)
        return (int)XGestureRecognizerResult_Ignore;
    (void)nowMs;
    type = (XEventType)event->m_class.m_type;
    pos = points[0].m_position;
    if (type == XEVENT_TYPE_TOUCH_BEGIN) {
        self->m_pressX = pos.x;
        self->m_pressY = pos.y;
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
        int dist = (dx >= 0 ? dx : -dx) + (dy >= 0 ? dy : -dy);
        if (dist > self->m_maxDistance) {
            /* 位移超容差=拖动而非点按：放弃（不与平移手势争抢）。 */
            return (int)XGestureRecognizerResult_Ignore;
        }
    }
    XGestureRecognizer_setOutputPosition(base, &pos, &pos);
    XGestureRecognizer_setOutputFingerCount(base, pointCount);
    return (int)XGestureRecognizerResult_FinishGesture;
}

/* ==================== 虚函数表与生命周期 ==================== */

XVtable* XTapGestureRecognizer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTapGestureRecognizer)
    XVTABLE_INHERIT_XCLASS(XGestureRecognizer);
    XVTABLE_OVERLOAD_DEFAULT(EXGestureRecognizer_Recognize,
                             VTapGestureRecognizer_recognize);
    return XVTABLE_DEFAULT;
}

void XTapGestureRecognizer_init(XTapGestureRecognizer* self, XObject* parent)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XGestureRecognizer_init(&self->m_base, parent,
                            (int)XGestureEventType_Tap);
    XClassSetVtable(self, XTapGestureRecognizer);
    /* 判定回调登记（函数指针登记：框架内 XContainer 的
       m_dataCopyMethod 族同款先例，识别器族多层派生不依赖
       虚表槽位继承）。 */
    self->m_base.m_recognize = VTapGestureRecognizer_recognize;
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_maxDistance = 40;
}

XTapGestureRecognizer* XTapGestureRecognizer_create_ex(XMemoryType memory,
                                                        XObject* parent)
{
    XTapGestureRecognizer* self =
        (XTapGestureRecognizer*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTapGestureRecognizer_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XTapGestureRecognizer_setMaxDistance(XTapGestureRecognizer* self,
                                          int maxDistance)
{
    if (!self) return;
    if (maxDistance < 0) maxDistance = 0;
    self->m_maxDistance = maxDistance;
}

#endif /* XGESTURE_ON */

#endif /* XGUI_ON */
