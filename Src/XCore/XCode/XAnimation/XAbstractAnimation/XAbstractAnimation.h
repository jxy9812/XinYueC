/**
 * @file       XAbstractAnimation.h
 * @brief      动画抽象基类公开 API。
 * @details    对齐 Qt 6.8 QAbstractAnimation（QtCore 动画模块）；实现只依赖
 *             XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或其他平台 API。
 *             本类是 XStateMachine 动画 API（XStateMachine_setAnimated 等）
 *             与 XAbstractTransition 动画 API 的参数类型依赖。
 */
#ifndef XABSTRACTANIMATION_H
#define XABSTRACTANIMATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XObject.h"

typedef struct XAnimationGroup XAnimationGroup;

/** @brief 动画播放方向，对应 QAbstractAnimation::Direction。 */
typedef enum XAbstractAnimation_Direction {
    XAbstractAnimation_Forward,  ///< 时间从 0 递增到时长终点。
    XAbstractAnimation_Backward  ///< 时间从时长终点递减到 0。
} XAbstractAnimation_Direction;

/** @brief 动画生命周期状态，对应 QAbstractAnimation::State。 */
typedef enum XAbstractAnimation_State {
    XAbstractAnimation_Stopped, ///< 未运行（初始状态，也是结束后的状态）。
    XAbstractAnimation_Paused,  ///< 暂停；currentTime 保持不变。
    XAbstractAnimation_Running  ///< 运行中；事件循环周期性推进 currentTime。
} XAbstractAnimation_State;

/** @brief 动画结束后的处置策略，对应 QAbstractAnimation::DeletionPolicy。 */
typedef enum XAbstractAnimation_DeletionPolicy {
    XAbstractAnimation_KeepWhenStopped = 0, ///< 停止后保留对象。
    XAbstractAnimation_DeleteWhenStopped    ///< 停止后延迟销毁对象。
} XAbstractAnimation_DeletionPolicy;

XCLASS_DEFINE_BEGING(XAbstractAnimation)
XCLASS_DEFINE_ENUM(XAbstractAnimation, UpdateCurrentTime) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XAbstractAnimation, UpdateState),
XCLASS_DEFINE_ENUM(XAbstractAnimation, UpdateDirection),
XCLASS_DEFINE_ENUM(XAbstractAnimation, Duration),
XCLASS_DEFINE_END(XAbstractAnimation)

/**
 * @brief 所有动画的抽象基类，对应 Qt 6.8 的 QAbstractAnimation。
 * @note duration 与 updateCurrentTime 在 Qt 中是纯虚函数；C 侧由虚表槽
 *       EXAbstractAnimation_Duration / EXAbstractAnimation_UpdateCurrentTime
 *       表达，基类默认实现分别返回 0 与空操作，派生类必须重载才有时长。
 */
typedef struct XAbstractAnimation {
    XObject m_class;                          ///< 继承 XObject，必须位于结构体第一位。
    XAbstractAnimation_State m_state;         ///< 当前状态。
    XAbstractAnimation_Direction m_direction; ///< 播放方向（默认 Forward）。
    int m_loopCount;                          ///< 循环次数：0=不运行，-1=无限，n=循环 n 次（默认 1）。
    int m_currentLoop;                        ///< 当前循环序号（从 0 起）。
    int m_currentLoopTime;                    ///< 当前循环内的时间（0..duration）。
    int m_totalCurrentTime;                   ///< 跨循环累计时间（对标 Qt 的 currentTime 属性）。
    bool m_deleteWhenStopped;                 ///< 停止后是否延迟销毁（start 的 DeletionPolicy）。
    bool m_hasRegisteredTimer;                ///< 是否已登记到全局动画驱动器。
    XAnimationGroup* m_group;                 ///< 所属动画组，未加入组时为 NULL。
} XAbstractAnimation;

/**
 * @brief 初始化 XAbstractAnimation 虚函数表。
 * @return XAbstractAnimation 的共享虚函数表。
 */
XVtable* XAbstractAnimation_class_init(void);

/**
 * @brief 创建无父对象的动画（对标 QAbstractAnimation(QObject*)）。
 * @param memory 内存类型。
 * @param parent 父对象，可为 NULL；非 NULL 时取得所有权（XObject 级联）。
 * @return 新动画；内存分配失败返回 NULL。
 * @note 本类为抽象基类：直接创建的实例 duration 恒为 0，start 后立即到达
 *       端点自停；业务请派生并重载 EXAbstractAnimation_Duration 与
 *       EXAbstractAnimation_UpdateCurrentTime。
 */
XAbstractAnimation* XAbstractAnimation_create_ex(XMemoryType memory, XObject* parent);
/**
 * @brief 初始化动画基类部分（栈对象或嵌入结构体场景）。
 * @param animation 调用者提供的未初始化存储。
 * @param parent 父对象，可为 NULL。
 */
void XAbstractAnimation_init(XAbstractAnimation* animation, XObject* parent);

/**
 * @brief 获取动画状态（对标 QAbstractAnimation::state）。
 * @param animation 动画实例。
 * @return 当前状态；animation 为 NULL 时返回 Stopped。
 */
XAbstractAnimation_State XAbstractAnimation_state(const XAbstractAnimation* animation);

/**
 * @brief 获取所属动画组（对标 QAbstractAnimation::group）。
 * @param animation 动画实例。
 * @return 所属组；未加入组或 animation 为 NULL 时返回 NULL。
 * @warning 借用语义：返回值归动画树所有，调用者不得释放。
 */
XAnimationGroup* XAbstractAnimation_group(const XAbstractAnimation* animation);

/**
 * @brief 获取播放方向（对标 QAbstractAnimation::direction）。
 * @param animation 动画实例。
 * @return 当前方向；animation 为 NULL 时返回 Forward。
 */
XAbstractAnimation_Direction XAbstractAnimation_direction(const XAbstractAnimation* animation);
/**
 * @brief 设置播放方向（对标 QAbstractAnimation::setDirection）。
 * @param animation 动画实例。
 * @param direction 新方向。
 * @note 与 Qt 一致：Stopped 状态下改向 Backward 会把时间重置到
 *       duration/currentLoop=loopCount-1，改向 Forward 重置到 0/0；
 *       运行中改向立即调 updateDirection 并发 directionChanged。
 */
void XAbstractAnimation_setDirection(XAbstractAnimation* animation,
                                     XAbstractAnimation_Direction direction);

/**
 * @brief 获取跨循环累计时间（对标 QAbstractAnimation::currentTime）。
 * @param animation 动画实例。
 * @return 累计时间（毫秒）；animation 为 NULL 时返回 0。
 */
int XAbstractAnimation_currentTime(const XAbstractAnimation* animation);
/**
 * @brief 获取当前循环内的时间（对标 QAbstractAnimation::currentLoopTime）。
 * @param animation 动画实例。
 * @return 循环内时间（0..duration）；animation 为 NULL 时返回 0。
 */
int XAbstractAnimation_currentLoopTime(const XAbstractAnimation* animation);

/**
 * @brief 获取循环次数（对标 QAbstractAnimation::loopCount）。
 * @param animation 动画实例。
 * @return 循环次数；animation 为 NULL 时返回 1。
 */
int XAbstractAnimation_loopCount(const XAbstractAnimation* animation);
/**
 * @brief 设置循环次数（对标 QAbstractAnimation::setLoopCount）。
 * @param animation 动画实例。
 * @param loopCount 0=不运行，-1=无限循环，正数为循环次数。
 */
void XAbstractAnimation_setLoopCount(XAbstractAnimation* animation, int loopCount);

/**
 * @brief 获取当前循环序号（对标 QAbstractAnimation::currentLoop）。
 * @param animation 动画实例。
 * @return 当前循环序号（从 0 起）；animation 为 NULL 时返回 0。
 */
int XAbstractAnimation_currentLoop(const XAbstractAnimation* animation);

/**
 * @brief 调用派生类的时长虚函数（对标 QAbstractAnimation::duration）。
 * @param animation 动画实例。
 * @return 单循环时长（毫秒）；-1 表示时长未定义（只跑一次）。
 * @note 通过 EXAbstractAnimation_Duration 调度到派生类实现。
 */
int XAbstractAnimation_duration_base(const XAbstractAnimation* animation);
/**
 * @brief 获取含循环次数的总时长（对标 QAbstractAnimation::totalDuration）。
 * @param animation 动画实例。
 * @return duration×loopCount；duration<=0 时返回 duration；无限循环返回 -1。
 */
int XAbstractAnimation_totalDuration(const XAbstractAnimation* animation);

/**
 * @brief 启动动画并指定结束处置策略（对标 QAbstractAnimation::start(DeletionPolicy)）。
 * @param animation 动画实例。
 * @param policy 结束后保留还是延迟销毁。
 * @note 已处于 Running 时本函数为空操作；从 Stopped 启动会按方向重置时间。
 */
void XAbstractAnimation_start_ex(XAbstractAnimation* animation,
                                 XAbstractAnimation_DeletionPolicy policy);
/**
 * @brief 停止动画（对标 QAbstractAnimation::stop）。
 * @param animation 动画实例。
 * @note 已停止时空操作；因到达端点而自然停止时先发 finished。
 */
void XAbstractAnimation_stop(XAbstractAnimation* animation);
/**
 * @brief 暂停动画（对标 QAbstractAnimation::pause）。
 * @param animation 动画实例。
 * @note 与 Qt 一致：Stopped 状态下的暂停请求被拒绝。
 */
void XAbstractAnimation_pause(XAbstractAnimation* animation);
/**
 * @brief 恢复暂停的动画（对标 QAbstractAnimation::resume）。
 * @param animation 动画实例。
 * @note 与 Qt 一致：非 Paused 状态下的恢复请求被拒绝。
 */
void XAbstractAnimation_resume(XAbstractAnimation* animation);
/**
 * @brief 按布尔值暂停或恢复（对标 QAbstractAnimation::setPaused）。
 * @param animation 动画实例。
 * @param paused true 表示暂停，false 表示恢复。
 */
void XAbstractAnimation_setPaused(XAbstractAnimation* animation, bool paused);
/**
 * @brief 手动设置累计时间并更新循环（对标 QAbstractAnimation::setCurrentTime）。
 * @param animation 动画实例。
 * @param msecs 新的累计时间（毫秒）；负值按 0 处理，超出总时长按总时长钳位。
 * @note 到达端点（Forward 到总时长或 Backward 到 0）时自动停止。
 */
void XAbstractAnimation_setCurrentTime(XAbstractAnimation* animation, int msecs);

/**
 * @brief 调用派生类的时间推进虚函数。
 * @param animation 动画实例。
 * @param currentTime 当前循环内的时间（毫秒）。
 * @note 通过 EXAbstractAnimation_UpdateCurrentTime 调度到派生类实现。
 */
void XAbstractAnimation_updateCurrentTime_base(XAbstractAnimation* animation, int currentTime);
/**
 * @brief 调用派生类的状态变化虚函数。
 * @param animation 动画实例。
 * @param newState 新状态。
 * @param oldState 旧状态。
 * @note 通过 EXAbstractAnimation_UpdateState 调度到派生类实现；派生类在
 *       本回调里再改自身状态时，后续通知按 Qt 口径截断（不再发信号）。
 */
void XAbstractAnimation_updateState_base(XAbstractAnimation* animation,
                                         XAbstractAnimation_State newState,
                                         XAbstractAnimation_State oldState);
/**
 * @brief 调用派生类的方向变化虚函数。
 * @param animation 动画实例。
 * @param direction 新方向。
 * @note 通过 EXAbstractAnimation_UpdateDirection 调度到派生类实现。
 */
void XAbstractAnimation_updateDirection_base(XAbstractAnimation* animation,
                                             XAbstractAnimation_Direction direction);

/**
 * @brief 动画自然停止或手动停止到达端点后发出的 finished 信号（对标 finished）。
 * @param animation 已经结束的动画。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XAbstractAnimation_finished_signal(XAbstractAnimation* animation);
/**
 * @brief 状态变化信号（对标 stateChanged）。
 * @param animation 状态已变化的动画。
 * @param newState 新状态。
 * @param oldState 旧状态。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XAbstractAnimation_stateChanged_signal(XAbstractAnimation* animation,
                                             XAbstractAnimation_State newState,
                                             XAbstractAnimation_State oldState);
/**
 * @brief 循环序号变化信号（对标 currentLoopChanged）。
 * @param animation 循环序号已变化的动画。
 * @param currentLoop 新循环序号。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XAbstractAnimation_currentLoopChanged_signal(XAbstractAnimation* animation,
                                                   int currentLoop);
/**
 * @brief 方向变化信号（对标 directionChanged）。
 * @param animation 方向已变化的动画。
 * @param direction 新方向。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XAbstractAnimation_directionChanged_signal(XAbstractAnimation* animation,
                                                 XAbstractAnimation_Direction direction);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XAbstractAnimation_create
#define XAbstractAnimation_create() \
	XAbstractAnimation_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL)
#undef XAbstractAnimation_start
#define XAbstractAnimation_start(animation) \
	XAbstractAnimation_start_ex((animation), XAbstractAnimation_KeepWhenStopped)

#endif // XABSTRACTANIMATION_H
