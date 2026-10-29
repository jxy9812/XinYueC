/**
 * @file       XPanGestureRecognizer.c
 * @brief      平移手势识别器实现（对标 Qt 6.8 QPanGesture）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XPanGestureRecognizer.h"
#include "XGestureRecognizer_Protected.h"
#include "XMemory.h"
#include "XWidget.h"
#include "XGuiConfig.h"

#if XGESTURE_ON

/** @brief 判定：按压→越阈触发→逐帧更新→抬手收口。 */
static int VPanGestureRecognizer_recognize(XGestureRecognizer* base,
                                            const XTouchEvent* event,
                                            const XTouchPoint* points,
                                            int pointCount, int64_t nowMs)
{
    XPanGestureRecognizer* self = (XPanGestureRecognizer*)base;
    XEventType type;
    XPoint pos;
    XPoint delta;
    XPoint start;
    int dx;
    int dy;
    if (!self || !event || !points || pointCount <= 0)
        return (int)XGestureRecognizerResult_Ignore;
    type = (XEventType)event->m_class.m_type;
    pos = points[0].m_position;
    if (type == XEVENT_TYPE_TOUCH_BEGIN) {
        /* 按压：记起点与基准（位移量按主点；多指模式取首指）。 */
        self->m_pressX = pos.x;
        self->m_pressY = pos.y;
        self->m_lastX = pos.x;
        self->m_lastY = pos.y;
        self->m_totalX = 0;
        self->m_totalY = 0;
        self->m_pressMs = nowMs;
        self->m_started = false;
        XGestureRecognizer_setOutputPosition(base, &pos, &pos);
        XGestureRecognizer_setOutputFingerCount(base, pointCount);
        return (int)XGestureRecognizerResult_MayBeGesture;
    }
    if (type == XEVENT_TYPE_TOUCH_CANCEL) {
        self->m_started = false;
        return (int)XGestureRecognizerResult_CancelGesture;
    }
    if (type == XEVENT_TYPE_TOUCH_END) {
        if (!self->m_started) {
            /* 未越阈即抬起=点按而非平移：放行（不争抢点按手势）。 */
            self->m_started = false;
            return (int)XGestureRecognizerResult_Ignore;
        }
        self->m_started = false;
        dx = pos.x - self->m_lastX;
        dy = pos.y - self->m_lastY;
        delta.x = (short)dx;
        delta.y = (short)dy;
        start.x = (short)self->m_pressX;
        start.y = (short)self->m_pressY;
        XGestureRecognizer_setOutputPosition(base, &pos, &start);
        XGestureRecognizer_setOutputDelta(base, &delta, &pos);
        XGestureRecognizer_setOutputFingerCount(base, pointCount);
        {
            int64_t dt = nowMs - self->m_pressMs;
            if (dt > 0) {
                XGestureRecognizer_setOutputVelocity(
                    base, (float)((self->m_totalX + dx) * 1000.0 / (double)dt),
                    (float)((self->m_totalY + dy) * 1000.0 / (double)dt));
            }
        }
        return (int)XGestureRecognizerResult_FinishGesture;
    }
    /* UPDATE：位移越阈即触发（Qt 口径：offset 任一轴绝对值 > 10px）。 */
    dx = pos.x - self->m_pressX;
    dy = pos.y - self->m_pressY;
    if (!self->m_started) {
        int adx = dx >= 0 ? dx : -dx;
        int ady = dy >= 0 ? dy : -dy;
        if (adx <= self->m_threshold && ady <= self->m_threshold) {
            XGestureRecognizer_setOutputPosition(base, &pos, &pos);
            XGestureRecognizer_setOutputFingerCount(base, pointCount);
            return (int)XGestureRecognizerResult_MayBeGesture;
        }
        self->m_started = true;
    }
    delta.x = (short)(pos.x - self->m_lastX);
    delta.y = (short)(pos.y - self->m_lastY);
    self->m_lastX = pos.x;
    self->m_lastY = pos.y;
    self->m_totalX = pos.x - self->m_pressX;
    self->m_totalY = pos.y - self->m_pressY;
    start.x = (short)self->m_pressX;
    start.y = (short)self->m_pressY;
    XGestureRecognizer_setOutputPosition(base, &pos, &start);
    XGestureRecognizer_setOutputDelta(base, &delta, &pos);
    XGestureRecognizer_setOutputFingerCount(base, pointCount);
    {
        int64_t dt = nowMs - self->m_pressMs;
        if (dt > 0)
            XGestureRecognizer_setOutputVelocity(
                base, (float)(self->m_totalX * 1000.0 / (double)dt),
                (float)(self->m_totalY * 1000.0 / (double)dt));
    }
    return (int)XGestureRecognizerResult_TriggerGesture;
}

/* ==================== 虚函数表与生命周期 ==================== */

XVtable* XPanGestureRecognizer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPanGestureRecognizer)
    XVTABLE_INHERIT_XCLASS(XGestureRecognizer);
    XVTABLE_OVERLOAD_DEFAULT(EXGestureRecognizer_Recognize,
                             VPanGestureRecognizer_recognize);
    return XVTABLE_DEFAULT;
}

void XPanGestureRecognizer_init(XPanGestureRecognizer* self, XObject* parent)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XGestureRecognizer_init(&self->m_base, parent,
                            (int)XGestureEventType_Pan);
    XClassSetVtable(self, XPanGestureRecognizer);
    /* 判定回调登记（函数指针登记：框架内 XContainer 的
       m_dataCopyMethod 族同款先例，识别器族多层派生不依赖
       虚表槽位继承）。 */
    self->m_base.m_recognize = VPanGestureRecognizer_recognize;
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_threshold = 10;
    self->m_pointCount = 1;
}

XPanGestureRecognizer* XPanGestureRecognizer_create_ex(XMemoryType memory,
                                                        XObject* parent)
{
    XPanGestureRecognizer* self =
        (XPanGestureRecognizer*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XPanGestureRecognizer_init(self, parent);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XPanGestureRecognizer_setThreshold(XPanGestureRecognizer* self,
                                        int threshold)
{
    if (!self) return;
    if (threshold < 0) threshold = 0;
    self->m_threshold = threshold;
}

void XPanGestureRecognizer_setPointCount(XPanGestureRecognizer* self,
                                         int count)
{
    if (!self) return;
    if (count < 1) count = 1;
    if (count > 8) count = 8;
    self->m_pointCount = count;
}

#endif /* XGESTURE_ON */

#endif /* XGUI_ON */
