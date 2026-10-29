/**
 * @file       XGestureRecognizer.h
 * @brief      触摸手势识别器基类公开 API（对标 Qt 6.8 QGestureRecognizer）。
 * @details    框架手势识别体系：识别器对象消费原始触摸帧流，判定命中后
 *             经事件循环把 XGestureEvent 投递到目标控件的 GestureEvent
 *             虚槽（对标 Qt 的 QGestureRecognizer → QEvent::Gesture 链路）。
 *
 *             状态机数值对齐 Qt::GestureState（NoGesture=0/Started=1/
 *             Updated=2/Finished=3/Canceled=4）；判定结果位对齐
 *             QGestureRecognizer::Result（Ignore=0x1/MayBeGesture=0x2/
 *             TriggerGesture=0x4/FinishGesture=0x8/CancelGesture=0x10/
 *             ConsumeEventHint=0x100）。
 *
 *             与 Qt 的差异：
 *             - 识别器实例与单个目标控件绑定（Qt 按「手势类型」全局注册
 *               识别器、控件 grab 订阅）；框架按实例订阅，便于同一应用
 *               同时存在多套阈值配置；
 *             - 识别器为 XObject 派生（Qt 同款），内部定时器走对象自带
 *               定时器机制（长按/双击计时）；
 *             - 手势事件默认**不**消费原始触摸事件（对齐 Qt 默认语义）。
 *
 * @note       模块总开关 XGESTURE_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XGESTURERECOGNIZER_H
#define XGESTURERECOGNIZER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XWindowEvent.h"
#include "XGestureEvent.h"

#if XGESTURE_ON

/** @brief 识别器判定结果位（对标 Qt QGestureRecognizer::Result）。 */
typedef enum XGestureRecognizerResult
{
    XGestureRecognizerResult_Ignore = 0x0001,      /**< 忽略本次帧，状态不变。 */
    XGestureRecognizerResult_MayBeGesture = 0x0002,/**< 可能是手势，继续观察。 */
    XGestureRecognizerResult_TriggerGesture = 0x0004,/**< 触发手势（进入 Started）。 */
    XGestureRecognizerResult_FinishGesture = 0x0008,/**< 结束手势（进入 Finished）。 */
    XGestureRecognizerResult_CancelGesture = 0x0010,/**< 取消手势（进入 Canceled）。 */
    XGestureRecognizerResult_ConsumeEventHint = 0x0100 /**< 要求消费原始触摸事件。 */
} XGestureRecognizerResult;

/** @brief 识别器判定结果位掩码工具。 */
#define XGESTURE_RESULT_STATE_MASK 0x00ff
#define XGESTURE_RESULT_HINT_MASK  0xff00

/**
 * @brief 识别器单帧可处理的最大触点数（框架内部快照容量上限）。
 * @details 触点超过此数的帧在识别阶段被截断（其余触点不参与判定）；
 *          框架侧投递与前帧快照均以此为硬上限（栈上数组容量）。
 */
#define XGESTURE_MAX_POINTS 8

/** @brief 识别器判定回调（派生类实现，构造时登记到基类）。
 * @details 对标 Qt QGestureRecognizer::recognize 的纯虚判定入口；本框架
 *          以函数指针登记（框架内同类先例：XContainer 的 m_dataCopyMethod
 *          族），识别器族多层派生下不依赖虚表槽位继承。
 */
struct XGestureRecognizer;
typedef int (*XGestureRecognizeFunc)(struct XGestureRecognizer* self,
                                     const XTouchEvent* event,
                                     const XTouchPoint* points,
                                     int pointCount, int64_t nowMs);

XCLASS_DEFINE_BEGING(XGestureRecognizer)
XCLASS_DEFINE_ENUM(XGestureRecognizer, Recognize) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XGestureRecognizer)

/**
 * @brief 手势输出数据暂存区（识别器派生类填写，基类据此构造事件）。
 * @details 对标 Qt 各手势类的数据面（QPanGesture::offset/delta、
 *          QPinchGesture::scaleFactor/totalScaleFactor/rotationAngle、
 *          QSwipeGesture::swipeAngle）。位置字段一律控件局部坐标。
 */
typedef struct XGestureRecognizerOutput
{
    XPoint m_position;           /**< 当前主触点位置（控件局部）。 */
    XPoint m_globalPosition;     /**< 当前主触点全局位置。 */
    XPoint m_lastPosition;       /**< 上一帧主触点位置（控件局部）。 */
    XPoint m_startPosition;      /**< 手势起始位置（控件局部）。 */
    XPoint m_delta;              /**< 本帧增量位移。 */
    float  m_velocityX;          /**< 水平速度（px/s，正=向右）。 */
    float  m_velocityY;          /**< 纵向速度（px/s，正=向下）。 */
    float  m_scaleFactor;        /**< 本帧缩放比例（1.0=未缩放）。 */
    float  m_totalScaleFactor;   /**< 累计缩放比例。 */
    float  m_rotationAngle;      /**< 本帧旋转角增量（度）。 */
    float  m_totalRotationAngle; /**< 累计旋转角（度）。 */
    float  m_angle;              /**< 手势方向角（度，相对 x 轴）。 */
    uint8_t m_direction;         /**< 手势方向（存 XGestureDirection）。 */
    uint8_t m_fingerCount;       /**< 参与手势的触点数量。 */
} XGestureRecognizerOutput;

/**
 * @brief      手势识别器基类；m_class 必须是第一个成员。
 * @details    派生类只需重载 Recognize 虚槽给出判定结果；帧驱动、状态
 *             迁移、事件构造与投递由基类统一完成（模板方法模式）。
 */
typedef struct XGestureRecognizer
{
    XObject m_class;          /**< 继承 XObject；必须为第一个成员。 */
    XObject* m_target;        /**< 目标对象（借用；对标 Qt
                                   QGestureRecognizer::create(QObject*)：目标
                                   通常是控件，识别成功后手势事件投递给它。
                                   识别器不拥有目标，控件亦不拥有识别器——
                                   订阅关系由双方各自持有引用，见
                                   XWidget_addGestureRecognizer）。 */
    uint8_t m_gestureType;    /**< 本识别器产出的手势类型
                                   （存 XGestureEventType）。 */
    uint8_t m_state;          /**< 当前手势状态（存 XGestureState）。 */
    bool    m_active;         /**< 手势进行中（Started/Updated 期间）。 */
    int     m_pointCount;     /**< 本帧参与判定的触点数量。 */
    int64_t m_startMs;        /**< 手势起始时刻（手势时钟域毫秒）。 */
    int64_t m_lastMs;         /**< 最近一帧时刻（手势时钟域毫秒）。 */
    /* [子类使用] 输出暂存与前帧快照：派生类在 Recognize 槽内经
       XGestureRecognizer_Protected.h 的 setter 填写输出数据，并可读取
       上一帧触点快照（缩放/旋转类手势需要帧间距离与角度差）。 */
    XGestureRecognizerOutput m_output; /**< 输出暂存区（内部使用）。 */
    XTouchPoint m_prevPoints[XGESTURE_MAX_POINTS]; /**< 上一帧触点快照（内部使用）。 */
    int m_prevPointCount;               /**< 上一帧触点数量（内部使用）。 */
    XGestureRecognizeFunc m_recognize; /**< 判定回调（派生类构造时登记；
                                          NULL=不识别任何手势）。 */
} XGestureRecognizer;

/* ==================== 生命周期 ==================== */

/** @brief 识别器类虚函数表初始化。 */
XVtable* XGestureRecognizer_class_init(void);
/**
 * @brief 初始化调用者提供的识别器存储（栈对象）。
 * @param self   识别器存储；不可为 NULL。
 * @param parent 父对象；可为 NULL（识别器独立存活）。
 * @param type   本识别器产出的手势类型（存 XGestureEventType）。
 */
void XGestureRecognizer_init(XGestureRecognizer* self, XObject* parent,
                              int type);
/**
 * @brief 创建识别器对象（堆）。
 * @param memory 内存类型；通常传 XCLASS_DEFAULT_MEMORY_TYPE。
 * @param parent 父对象；可为 NULL。
 * @param type   本识别器产出的手势类型（存 XGestureEventType）。
 * @return 新识别器；内存分配失败返回 NULL。
 */
XGestureRecognizer* XGestureRecognizer_create_ex(XMemoryType memory,
                                                 XObject* parent, int type);
/** @brief 默认内存池快速创建（宏形式）。 */
#define XGestureRecognizer_create(...) \
    XGestureRecognizer_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

/* ==================== 配置 ==================== */

/**
 * @brief 绑定目标控件（识别器识别成功后把手势事件投递给该控件）。
 * @param self 识别器；NULL 无操作。
 * @param target 目标对象（通常为控件）；可为 NULL（解除绑定，此后不
 *                产出事件）。
 * @return 无返回值。
 */
void XGestureRecognizer_setTarget(XGestureRecognizer* self,
                                   XObject* target);
/**
 * @brief 读取目标控件（借用指针）。
 * @param self 识别器；可为 NULL。
 * @return 目标对象；未绑定或 self 为 NULL 时返回 NULL。
 */
XObject* XGestureRecognizer_target(const XGestureRecognizer* self);
/**
 * @brief 读取本识别器产出的手势类型。
 * @param self 识别器；可为 NULL。
 * @return 手势类型（存 XGestureEventType）；self 为 NULL 时返回 Tap。
 */
XGestureEventType XGestureRecognizer_gestureType(
    const XGestureRecognizer* self);
/**
 * @brief 读取当前手势状态。
 * @param self 识别器；可为 NULL。
 * @return 手势状态（存 XGestureState）；self 为 NULL 时返回 NoGesture。
 */
XGestureState XGestureRecognizer_state(const XGestureRecognizer* self);
/**
 * @brief 重置识别器状态（取消进行中的手势，回到 NoGesture）。
 * @param self 识别器；NULL 无操作。
 * @return 无返回值。
 */
void XGestureRecognizer_reset(XGestureRecognizer* self);

/* ==================== 帧驱动（框架内部调用） ==================== */

/**
 * @brief 识别器判定虚槽（派生类重载点）。
 * @details 派生类依据本帧触点集合与自身阈值给出判定结果位；基类负责
 *          按结果迁移状态、构造并投递 XGestureEvent。
 * @param self 识别器；不可为 NULL。
 * @param event 本帧触摸事件（借用；位置为识别器目标控件局部坐标）。
 * @param points 本帧触点数组（借用；长度为 pointCount）。
 * @param pointCount 本帧触点数量。
 * @param nowMs 本帧时刻（手势时钟域毫秒）。
 * @return 判定结果位（XGestureRecognizerResult 组合）。
 */
int XGestureRecognizer_recognize_base(XGestureRecognizer* self,
                                       const XTouchEvent* event,
                                       const XTouchPoint* points,
                                       int pointCount, int64_t nowMs);

/**
 * @brief 投递一帧原始触摸给识别器（框架内部入口）。
 * @details 识别器判定 → 状态迁移 → 构造 XGestureEvent → 经事件循环
 *          投递到目标控件（XCoreApplication_postEvent）。手势事件默认
 *          不消费原始触摸事件（对齐 Qt 默认语义）。
 * @param self 识别器；NULL 无操作。
 * @param event 本帧触摸事件（借用）。
 * @param points 本帧触点数组（借用；坐标为目标控件局部坐标）。
 * @param pointCount 本帧触点数量。
 * @param nowMs 本帧时刻（手势时钟域毫秒）。
 * @return 无返回值。
 */
void XGestureRecognizer_processTouch(XGestureRecognizer* self,
                                      const XTouchEvent* event,
                                      const XTouchPoint* points,
                                      int pointCount, int64_t nowMs);

#ifdef __cplusplus
}
#endif
#endif /* XGESTURE_ON */

#endif /* XGUI_ON */

#endif /* XGESTURERECOGNIZER_H */
