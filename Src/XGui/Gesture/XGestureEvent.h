/**
 * @file       XGestureEvent.h
 * @brief      触摸手势事件对象公开 API（对标 Qt 6.8 QGestureEvent/QGesture）。
 * @details    框架手势识别器（XGestureRecognizer 族）识别成功后，经事件
 *             循环把本事件投递到目标控件，由控件的 GestureEvent 虚槽消费
 *             （对标 Qt 的 QEvent::Gesture 派发路径）。本类只承载数据，
 *             不含识别逻辑。
 *             语义对标：
 *             - XGestureEventType 数值对齐 Qt::GestureType（Tap=1、
 *               TapAndHold=2、Pan=3、Pinch=4、Swipe=5、Custom=0x0100）；
 *               框架在此基础上扩展 Qt 无独立类的三种手势（Rotate/
 *               DoubleTap/Fling），取值 6~8 接在 Qt 自定义段之前，不与
 *               Qt 数值冲突；
 *             - XGestureState 数值对齐 Qt::GestureState（NoGesture=0、
 *               Started=1、Updated=2、Finished=3、Canceled=4）；
 *             - 位置为控件局部坐标（对标 QPanGesture::offset 等的
 *               widget-local 语义）。
 * @note       本头**不受 XGESTURE_ON 裁剪**（枚举 + 事件对象都是框架触摸
 *             仿真的基础载体）；受该开关裁剪的是识别器族
 *             （XGestureRecognizer.h 及其派生识别器）。
 * @author     XinYueC 团队
 */
#ifndef XGESTUREEVENT_H
#define XGESTUREEVENT_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XEvent.h"


/** @brief 手势类型（对标 Qt::GestureType，数值一致；框架扩展段见下）。 */
typedef enum XGestureEventType
{
    XGestureEventType_Tap = 1,       /**< 点按（Qt TapGesture）。 */
    XGestureEventType_TapAndHold = 2,/**< 长按（Qt TapAndHoldGesture）。 */
    XGestureEventType_Pan = 3,       /**< 平移/拖动（Qt PanGesture）。 */
    XGestureEventType_Pinch = 4,     /**< 双指缩放（Qt PinchGesture）。 */
    XGestureEventType_Swipe = 5,     /**< 轻扫（Qt SwipeGesture）。 */
    XGestureEventType_Rotate = 6,    /**< 双指旋转（框架扩展：Qt 无独立旋转
                                          手势类，Qt 在 Pinch 内以
                                          rotationAngle 附带）。 */
    XGestureEventType_DoubleTap = 7, /**< 双击（框架扩展：Qt 无双击手势类，
                                          Qt 走鼠标 DBL_CLICK 合成）。 */
    XGestureEventType_Fling = 8,     /**< 甩动/惯性（框架扩展：Qt 无独立
                                          手势类，Qt 由滚动体系自持）。 */
    XGestureEventType_Custom = 0x0100/**< 自定义手势（Qt CustomGesture）。 */
} XGestureEventType;

/** @brief 手势状态（对标 Qt::GestureState，数值一致）。 */
typedef enum XGestureState
{
    XGestureState_NoGesture = 0,   /**< 无手势。 */
    XGestureState_Started = 1,     /**< 手势开始（本次手势的首个事件）。 */
    XGestureState_Updated = 2,     /**< 手势进行中（数据有变化）。 */
    XGestureState_Finished = 3,    /**< 手势正常结束。 */
    XGestureState_Canceled = 4     /**< 手势被取消（异常收口）。 */
} XGestureState;

/** @brief 手势方向（对标 QSwipeGesture 的方向取值）。 */
typedef enum XGestureDirection
{
    XGestureDirection_None = 0, /**< 无方向。 */
    XGestureDirection_Left = 1, /**< 向左。 */
    XGestureDirection_Right = 2,/**< 向右。 */
    XGestureDirection_Up = 3,   /**< 向上。 */
    XGestureDirection_Down = 4  /**< 向下。 */
} XGestureDirection;

/** @brief 拖动认领模式（XGui 扩展；DragBegin 判定事件上由接受控件标记，
 *         框架手势状态机回读后分流）。
 * @details Qt 无对标（Qt 的手势识别器由应用侧自行决定消费策略）；本框
 *          架的触摸拖动存在「左键按住拖动仿真」与「滚动拖动原始位移
 *          流」两条语义分支，故在事件上留一位标记。
 */
typedef enum XGestureDragMode
{
    XGestureDragMode_SimulateMouse = 0, /**< 默认：左键按住拖动仿真（历史
                                            语义：合成 press 续持 + MOUSE_MOVE
                                            随行 + RELEASE 收口，等效真实
                                            鼠标左键拖拽）。 */
    XGestureDragMode_Track = 1          /**< 滚动拖动：挂着的合成 press 远
                                            偏移释放关闭，本序列改发平移
                                            手势的原始位移流（跟手滚动、
                                            惯性滑行由此承载）。 */
} XGestureDragMode;
/* [裁剪安全 2026-10-30] 上面的手势类型/状态/方向/拖动模式枚举是
 * **轻量值类型**（无虚表、无堆成员），且被框架的触摸→鼠标仿真
 * 路径使用（tap/dblclick/longpress/drag 的判定结果）——那属框架基础
 * 交互能力，不应随手势识别器模块裁掉。故枚举留在 XGESTURE_ON 门
 * **之外**，只裁手势事件对象与其函数（裁剪无收益却会连带炸
 * 触摸仿真）。 */


XCLASS_DEFINE_BEGING(XGestureEvent)
XCLASS_DEFINE_ENUM(XGestureEvent, Clone) = XCLASS_VTABLE_GET_SIZE(XEvent),
XCLASS_DEFINE_END(XGestureEvent)

/**
 * @brief      手势事件对象；m_class 必须是第一个成员。
 * @details    字段语义对齐 Qt 对应手势类：位置为控件局部坐标，速度单位
 *             px/s，缩放系数为无量纲比例（1.0=未缩放），旋转角单位度。
 */
typedef struct XGestureEvent
{
    XEvent m_class;              /**< 继承 XEvent；必须为第一个成员。 */
    uint8_t m_gestureType;       /**< 手势类型（存 XGestureEventType）。 */
    uint8_t m_state;             /**< 手势状态（存 XGestureState）。 */
    uint8_t m_direction;         /**< 手势方向（存 XGestureDirection；仅轻扫
                                     手势有意义，其余为 None）。 */
    uint8_t m_fingerCount;       /**< 参与手势的触点数量（1=单指）。 */
    XPoint m_position;           /**< 当前主触点位置（控件局部坐标）。 */
    XPoint m_globalPosition;     /**< 当前主触点位置（屏幕全局坐标）。 */
    XPoint m_lastPosition;       /**< 上一帧主触点位置（控件局部坐标）。 */
    XPoint m_startPosition;      /**< 手势起始位置（控件局部坐标）。 */
    XPoint m_delta;              /**< 本帧增量位移（本次事件相对上一帧）。 */
    float  m_velocityX;          /**< 水平速度（px/s，正=向右）。 */
    float  m_velocityY;          /**< 纵向速度（px/s，正=向下）。 */
    float  m_scaleFactor;        /**< 本帧缩放比例（无量纲，1.0=未缩放；
                                     仅缩放手势有意义）。 */
    float  m_totalScaleFactor;   /**< 累计缩放比例（自手势起始累乘）。 */
    float  m_rotationAngle;      /**< 本帧旋转角增量（度，正=顺时针）。 */
    float  m_totalRotationAngle; /**< 累计旋转角（度，自手势起始累加）。 */
    float  m_angle;              /**< 手势方向角（度，相对 x 轴，顺时针为
                                     正；仅轻扫手势有意义）。 */
    uint8_t m_dragMode;          /**< 拖动认领模式（存 XGestureDragMode；
                                     仅平移手势的 Started 判定事件有意义，
                                     默认 SimulateMouse）。 */
} XGestureEvent;

/* ==================== 生命周期 ==================== */

/** @brief 手势事件类虚函数表初始化。 */
XVtable* XGestureEvent_class_init(void);
/**
 * @brief 初始化调用者提供的手势事件存储。
 * @param event  事件存储；不可为 NULL。
 * @param type   事件类型；固定为 XEVENT_TYPE_GESTURE。
 */
void XGestureEvent_init(XGestureEvent* event, XEventType type);
/**
 * @brief 创建手势事件对象。
 * @param memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param type   事件类型；固定为 XEVENT_TYPE_GESTURE。
 * @return 新事件对象；内存分配失败返回 NULL。
 */
XGestureEvent* XGestureEvent_create_ex(XMemoryType memory, XEventType type);
/** @brief 默认内存池快速创建（宏形式）。 */
#define XGestureEvent_create(...) \
    XGestureEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

/* ==================== 数据访问 ==================== */

/** @brief 读取手势类型（返回 XGestureEventType）。 */
XGestureEventType XGestureEvent_gestureType(const XGestureEvent* event);
/** @brief 读取手势状态（返回 XGestureState）。 */
XGestureState XGestureEvent_state(const XGestureEvent* event);
/** @brief 读取手势方向（返回 XGestureDirection）。 */
XGestureDirection XGestureEvent_direction(const XGestureEvent* event);
/** @brief 读取参与手势的触点数量。 */
int XGestureEvent_fingerCount(const XGestureEvent* event);
/** @brief 读取当前主触点位置（控件局部坐标）。 */
XPoint XGestureEvent_position(const XGestureEvent* event);
/** @brief 读取当前主触点全局位置。 */
XPoint XGestureEvent_globalPosition(const XGestureEvent* event);
/** @brief 读取上一帧主触点位置（控件局部坐标）。 */
XPoint XGestureEvent_lastPosition(const XGestureEvent* event);
/** @brief 读取手势起始位置（控件局部坐标）。 */
XPoint XGestureEvent_startPosition(const XGestureEvent* event);
/** @brief 读取本帧增量位移。 */
XPoint XGestureEvent_delta(const XGestureEvent* event);
/** @brief 读取水平速度（px/s）。 */
float XGestureEvent_velocityX(const XGestureEvent* event);
/** @brief 读取纵向速度（px/s）。 */
float XGestureEvent_velocityY(const XGestureEvent* event);
/** @brief 读取本帧缩放比例（1.0=未缩放）。 */
float XGestureEvent_scaleFactor(const XGestureEvent* event);
/** @brief 读取累计缩放比例。 */
float XGestureEvent_totalScaleFactor(const XGestureEvent* event);
/** @brief 读取本帧旋转角增量（度）。 */
float XGestureEvent_rotationAngle(const XGestureEvent* event);
/** @brief 读取累计旋转角（度）。 */
float XGestureEvent_totalRotationAngle(const XGestureEvent* event);
/** @brief 读取手势方向角（度）。 */
float XGestureEvent_angle(const XGestureEvent* event);
/**
 * @brief 读取拖动认领模式（存 XGestureDragMode）。
 * @param event 手势事件；可为 NULL。
 * @return 拖动认领模式；未标记时返回 SimulateMouse。
 */
int XGestureEvent_dragMode(const XGestureEvent* event);
/**
 * @brief 设置拖动认领模式（XGui 扩展；仅平移手势的 Started 判定事件
 *        由接受控件在 accept 前标记，框架手势状态机回读后分流）。
 * @param event 手势事件；不可为 NULL。
 * @param mode 拖动认领模式（存 XGestureDragMode）。
 * @return 无返回值。
 */
void XGestureEvent_setDragMode(XGestureEvent* event, int mode);

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XGESTUREEVENT_H */
