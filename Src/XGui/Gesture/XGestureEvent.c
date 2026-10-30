/**
 * @file       XGestureEvent.c
 * @brief      触摸手势事件对象实现（对标 Qt 6.8 QGestureEvent 数据面）。
 * @details    与同名头文件的公开 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XGestureEvent.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XGuiConfig.h"

/* [裁剪安全 2026-10-30] 本文件**不受 XGESTURE_ON 裁剪**：XGestureEvent 是框架
 * 触摸仿真的事件载体（TOUCH_DRAG 载荷），属基础交互能力；被裁的只有识别器族
 * （XGestureRecognizer.c 及其派生识别器）。头文件与本实现保持同一裁剪口径。 */


/* ==================== 虚函数实现 ==================== */

/** @brief deinit：无动态成员，仅链回父类。 */
static void VXGestureEvent_deinit(XGestureEvent* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XEvent, (XEvent*)self);
}

/** @brief clone：深拷贝全部手势数据字段（无动态成员，逐字段复制）。 */
static XEvent* VXGestureEvent_clone(const XGestureEvent* event)
{
    XGestureEvent* copy;
    if (!event) return NULL;
    copy = (XGestureEvent*)Class_Memory(event)->malloc(sizeof(XGestureEvent));
    if (!copy) return NULL;
    /* 基类字段随 init 重置（XEvent_init 清零 XClass 之后的全部基类位），
     * 故显式回填与源一致的标志位（accepted/spontaneous/input 等）。 */
    XEvent_init((XEvent*)copy, ((const XEvent*)event)->m_type);
    XClassGetVtable(copy) = XGestureEvent_class_init();
    copy->m_class.m_accepted = event->m_class.m_accepted;
    copy->m_class.m_spontaneous = event->m_class.m_spontaneous;
    copy->m_class.m_posted = event->m_class.m_posted;
    copy->m_class.m_input_event = event->m_class.m_input_event;
    copy->m_class.m_pointer_event = event->m_class.m_pointer_event;
    Class_Memory(copy) = Class_Memory(event);
    copy->m_gestureType = event->m_gestureType;
    copy->m_state = event->m_state;
    copy->m_direction = event->m_direction;
    copy->m_fingerCount = event->m_fingerCount;
    copy->m_position = event->m_position;
    copy->m_globalPosition = event->m_globalPosition;
    copy->m_lastPosition = event->m_lastPosition;
    copy->m_startPosition = event->m_startPosition;
    copy->m_delta = event->m_delta;
    copy->m_velocityX = event->m_velocityX;
    copy->m_velocityY = event->m_velocityY;
    copy->m_scaleFactor = event->m_scaleFactor;
    copy->m_totalScaleFactor = event->m_totalScaleFactor;
    copy->m_rotationAngle = event->m_rotationAngle;
    copy->m_totalRotationAngle = event->m_totalRotationAngle;
    copy->m_angle = event->m_angle;
    copy->m_dragMode = event->m_dragMode;
    Set_Class_IsHeap(copy, true);
    return (XEvent*)copy;
}

/* ==================== 虚函数表与生命周期 ==================== */

XVtable* XGestureEvent_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGestureEvent)
    XVTABLE_INHERIT_XCLASS(XEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXGestureEvent_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXEvent_Clone, VXGestureEvent_clone);
    return XVTABLE_DEFAULT;
}

void XGestureEvent_init(XGestureEvent* event, XEventType type)
{
    if (!event) return;
    XEvent_init((XEvent*)event, type);
    XClassGetVtable(event) = XGestureEvent_class_init();
    event->m_class.m_input_event = true;
    event->m_class.m_pointer_event = true;
    event->m_gestureType = (uint8_t)XGestureEventType_Tap;
    event->m_state = (uint8_t)XGestureState_NoGesture;
    event->m_direction = (uint8_t)XGestureDirection_None;
    event->m_fingerCount = 0;
    XPoint_init(&event->m_position, 0, 0);
    XPoint_init(&event->m_globalPosition, 0, 0);
    XPoint_init(&event->m_lastPosition, 0, 0);
    XPoint_init(&event->m_startPosition, 0, 0);
    XPoint_init(&event->m_delta, 0, 0);
    event->m_velocityX = 0.0f;
    event->m_velocityY = 0.0f;
    event->m_scaleFactor = 1.0f;
    event->m_totalScaleFactor = 1.0f;
    event->m_rotationAngle = 0.0f;
    event->m_totalRotationAngle = 0.0f;
    event->m_angle = 0.0f;
}

XGestureEvent* XGestureEvent_create_ex(XMemoryType memory, XEventType type)
{
    XGestureEvent* event =
        (XGestureEvent*)XMemory_malloc(sizeof(XGestureEvent), memory);
    if (!event) return NULL;
    XGestureEvent_init(event, type);
    Set_Class_Memory(event, memory);
    Set_Class_IsHeap(event, true);
    return event;
}

/* ==================== 数据访问 ==================== */

XGestureEventType XGestureEvent_gestureType(const XGestureEvent* event)
{
    return event ? (XGestureEventType)event->m_gestureType
                 : XGestureEventType_Tap;
}

XGestureState XGestureEvent_state(const XGestureEvent* event)
{
    return event ? (XGestureState)event->m_state : XGestureState_NoGesture;
}

XGestureDirection XGestureEvent_direction(const XGestureEvent* event)
{
    return event ? (XGestureDirection)event->m_direction
                 : XGestureDirection_None;
}

int XGestureEvent_fingerCount(const XGestureEvent* event)
{
    return event ? (int)event->m_fingerCount : 0;
}

XPoint XGestureEvent_position(const XGestureEvent* event)
{
    XPoint out;
    XPoint_init(&out, 0, 0);
    if (event) out = event->m_position;
    return out;
}

XPoint XGestureEvent_globalPosition(const XGestureEvent* event)
{
    XPoint out;
    XPoint_init(&out, 0, 0);
    if (event) out = event->m_globalPosition;
    return out;
}

XPoint XGestureEvent_lastPosition(const XGestureEvent* event)
{
    XPoint out;
    XPoint_init(&out, 0, 0);
    if (event) out = event->m_lastPosition;
    return out;
}

XPoint XGestureEvent_startPosition(const XGestureEvent* event)
{
    XPoint out;
    XPoint_init(&out, 0, 0);
    if (event) out = event->m_startPosition;
    return out;
}

XPoint XGestureEvent_delta(const XGestureEvent* event)
{
    XPoint out;
    XPoint_init(&out, 0, 0);
    if (event) out = event->m_delta;
    return out;
}

float XGestureEvent_velocityX(const XGestureEvent* event)
{
    return event ? event->m_velocityX : 0.0f;
}

float XGestureEvent_velocityY(const XGestureEvent* event)
{
    return event ? event->m_velocityY : 0.0f;
}

float XGestureEvent_scaleFactor(const XGestureEvent* event)
{
    return event ? event->m_scaleFactor : 1.0f;
}

float XGestureEvent_totalScaleFactor(const XGestureEvent* event)
{
    return event ? event->m_totalScaleFactor : 1.0f;
}

float XGestureEvent_rotationAngle(const XGestureEvent* event)
{
    return event ? event->m_rotationAngle : 0.0f;
}

float XGestureEvent_totalRotationAngle(const XGestureEvent* event)
{
    return event ? event->m_totalRotationAngle : 0.0f;
}

float XGestureEvent_angle(const XGestureEvent* event)
{
    return event ? event->m_angle : 0.0f;
}

int XGestureEvent_dragMode(const XGestureEvent* event)
{
    return event ? (int)event->m_dragMode
                 : (int)XGestureDragMode_SimulateMouse;
}

void XGestureEvent_setDragMode(XGestureEvent* event, int mode)
{
    if (event) event->m_dragMode = (uint8_t)mode;
}


#endif /* XGUI_ON */
