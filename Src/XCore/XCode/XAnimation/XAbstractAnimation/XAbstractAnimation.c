#include "XAbstractAnimation.h"

#include "XAnimation_p.h"
#include "XAnimationDriver.h"
#include "XMemory.h"

static void VXAbstractAnimation_deinit(XAbstractAnimation* animation);

/* ==================== 虚函数默认实现 ==================== */

static int VXAbstractAnimation_duration(const XAbstractAnimation* animation)
{
    (void)animation;
    /* Qt 的 duration() 是纯虚；C 侧默认 0 = 启动后立即到达端点自停。 */
    return 0;
}

static void VXAbstractAnimation_updateCurrentTime(XAbstractAnimation* animation, int currentTime)
{
    (void)animation;
    (void)currentTime;
}

static void VXAbstractAnimation_updateState(XAbstractAnimation* animation,
                                            XAbstractAnimation_State newState,
                                            XAbstractAnimation_State oldState)
{
    (void)animation;
    (void)newState;
    (void)oldState;
}

static void VXAbstractAnimation_updateDirection(XAbstractAnimation* animation,
                                                XAbstractAnimation_Direction direction)
{
    (void)animation;
    (void)direction;
}

/* ==================== 内部：状态机（对标 QAbstractAnimationPrivate::setState） ==================== */

/**
 * @brief 驱动器登记辅助：按 Qt 的 isTopLevel 语义登记/注销。
 * @details 顶层 = 无所属组或所属组处于 Stopped；组内运行中的动画由组子类驱动。
 */
static void XAbstractAnimation_updateDriverRegistration(XAbstractAnimation* animation,
                                                        bool running)
{
    bool topLevel = !animation->m_group
        || XAbstractAnimation_state((const XAbstractAnimation*)animation->m_group)
               == XAbstractAnimation_Stopped;
    if (running && topLevel)
        XAbstractAnimation_registerDriver_internal(animation);
    else
        XAbstractAnimation_unregisterDriver_internal(animation);
}

void XAbstractAnimation_setState_internal(XAbstractAnimation* animation,
                                          XAbstractAnimation_State newState)
{
    if (!animation)
        return;
    const XAbstractAnimation_State oldState = animation->m_state;
    if (oldState == newState)
        return;
    if (animation->m_loopCount == 0)
        return;

    const int oldCurrentTime = animation->m_totalCurrentTime;
    const int oldCurrentLoop = animation->m_currentLoop;
    const XAbstractAnimation_Direction oldDirection = animation->m_direction;

    /* 从 Stopped 启动时按方向重置时间（对标 Qt 的 rewind 段）。 */
    if ((newState == XAbstractAnimation_Paused || newState == XAbstractAnimation_Running)
        && oldState == XAbstractAnimation_Stopped) {
        int startAt = 0;
        if (animation->m_direction == XAbstractAnimation_Backward) {
            startAt = animation->m_loopCount < 0
                ? XAbstractAnimation_duration_base(animation)
                : XAbstractAnimation_totalDuration(animation);
        }
        animation->m_totalCurrentTime = startAt;
        animation->m_currentLoopTime = startAt;
    }

    animation->m_state = newState;

    /* (un)登记必须先于虚调用，保证驱动器状态正确（对标 Qt 次序注释）。 */
    XAbstractAnimation_updateDriverRegistration(animation,
                                                newState == XAbstractAnimation_Running);

    XAbstractAnimation_updateState_base(animation, newState, oldState);
    /* 派生类在 updateState 里改状态时按 Qt 口径截断后续通知。 */
    if (animation->m_state != newState)
        return;

    XAbstractAnimation_stateChanged_signal(animation, newState, oldState);
    if (animation->m_state != newState)
        return;

    switch (newState) {
    case XAbstractAnimation_Paused:
        break;
    case XAbstractAnimation_Running:
        /* 启动即按重置后的时间发一次推进（对标 Qt 的 setCurrentTime 调用）。 */
        if (oldState == XAbstractAnimation_Stopped)
            XAbstractAnimation_setCurrentTime(animation, animation->m_totalCurrentTime);
        break;
    case XAbstractAnimation_Stopped:
        if (animation->m_deleteWhenStopped)
            XObject_deleteLater((XObject*)animation);
        {
            const int dura = XAbstractAnimation_duration_base(animation);
            const bool reachedEnd = dura == -1
                || animation->m_loopCount < 0
                || (oldDirection == XAbstractAnimation_Forward
                    && (oldCurrentTime * (oldCurrentLoop + 1)) == (dura * animation->m_loopCount))
                || (oldDirection == XAbstractAnimation_Backward && oldCurrentTime == 0);
            if (reachedEnd)
                XAbstractAnimation_finished_signal(animation);
        }
        break;
    }
}

void XAbstractAnimation_setGroup_internal(XAbstractAnimation* animation, XAnimationGroup* group)
{
    if (!animation || animation->m_group == group)
        return;
    const bool wasRunning = animation->m_state == XAbstractAnimation_Running;
    animation->m_group = group;
    if (wasRunning)
        XAbstractAnimation_updateDriverRegistration(animation, true);
}

/* ==================== 生命周期 ==================== */

static void VXAbstractAnimation_deinit(XAbstractAnimation* animation)
{
    if (!animation)
        return;
    /* Qt ~QAbstractAnimation：不走 stop()（避免纯虚调用），直接切换状态
     * 并发 stateChanged；注销驱动器登记；组移除由组侧析构路径处理。 */
    if (animation->m_state != XAbstractAnimation_Stopped) {
        const XAbstractAnimation_State oldState = animation->m_state;
        animation->m_state = XAbstractAnimation_Stopped;
        XAbstractAnimation_unregisterDriver_internal(animation);
        XAbstractAnimation_stateChanged_signal(animation, XAbstractAnimation_Stopped, oldState);
    }
    XVtableGetFunc(XObject_class_init(), EXClass_Deinit,
                   void(*)(XObject*))((XObject*)animation);
}

XVtable* XAbstractAnimation_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAbstractAnimation)
    XVTABLE_INHERIT_XCLASS(XObject);
    /* 槽位顺序必须与 XCLASS_DEFINE_ENUM 一致：
     * UpdateCurrentTime / UpdateState / UpdateDirection / Duration。 */
    void* funcs[] = {
        VXAbstractAnimation_updateCurrentTime,
        VXAbstractAnimation_updateState,
        VXAbstractAnimation_updateDirection,
        VXAbstractAnimation_duration,
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(funcs);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractAnimation_deinit);
    return XVTABLE_DEFAULT;
}

XAbstractAnimation* XAbstractAnimation_create_ex(XMemoryType memory, XObject* parent)
{
    XAbstractAnimation* animation = XMemory_malloc(sizeof(XAbstractAnimation), memory);
    if (!animation)
        return NULL;
    XAbstractAnimation_init(animation, parent);
    Set_Class_Memory(animation, memory);
    Set_Class_IsHeap(animation, true);
    return animation;
}

void XAbstractAnimation_init(XAbstractAnimation* animation, XObject* parent)
{
    if (!animation)
        return;
    XObject_init((XObject*)animation);
    XClassSetVtable(animation, XAbstractAnimation);
    animation->m_state = XAbstractAnimation_Stopped;
    animation->m_direction = XAbstractAnimation_Forward;
    animation->m_loopCount = 1;
    animation->m_currentLoop = 0;
    animation->m_currentLoopTime = 0;
    animation->m_totalCurrentTime = 0;
    animation->m_deleteWhenStopped = false;
    animation->m_hasRegisteredTimer = false;
    animation->m_group = NULL;
    if (parent)
        XObject_setParent((XObject*)animation, parent);
}

/* ==================== 属性访问 ==================== */

XAbstractAnimation_State XAbstractAnimation_state(const XAbstractAnimation* animation)
{
    return animation ? animation->m_state : XAbstractAnimation_Stopped;
}

XAnimationGroup* XAbstractAnimation_group(const XAbstractAnimation* animation)
{
    return animation ? animation->m_group : NULL;
}

XAbstractAnimation_Direction XAbstractAnimation_direction(const XAbstractAnimation* animation)
{
    return animation ? animation->m_direction : XAbstractAnimation_Forward;
}

void XAbstractAnimation_setDirection(XAbstractAnimation* animation,
                                     XAbstractAnimation_Direction direction)
{
    if (!animation || animation->m_direction == direction)
        return;

    const int oldCurrentLoop = animation->m_currentLoop;
    if (animation->m_state == XAbstractAnimation_Stopped) {
        if (direction == XAbstractAnimation_Backward) {
            animation->m_currentLoopTime = XAbstractAnimation_duration_base(animation);
            animation->m_currentLoop = animation->m_loopCount - 1;
        } else {
            animation->m_currentLoopTime = 0;
            animation->m_currentLoop = 0;
        }
    }

    animation->m_direction = direction;
    XAbstractAnimation_updateDirection_base(animation, direction);

    if (animation->m_currentLoop != oldCurrentLoop)
        XAbstractAnimation_currentLoopChanged_signal(animation, animation->m_currentLoop);
    XAbstractAnimation_directionChanged_signal(animation, direction);
}

int XAbstractAnimation_currentTime(const XAbstractAnimation* animation)
{
    return animation ? animation->m_totalCurrentTime : 0;
}

int XAbstractAnimation_currentLoopTime(const XAbstractAnimation* animation)
{
    return animation ? animation->m_currentLoopTime : 0;
}

int XAbstractAnimation_loopCount(const XAbstractAnimation* animation)
{
    return animation ? animation->m_loopCount : 1;
}

void XAbstractAnimation_setLoopCount(XAbstractAnimation* animation, int loopCount)
{
    if (animation)
        animation->m_loopCount = loopCount;
}

int XAbstractAnimation_currentLoop(const XAbstractAnimation* animation)
{
    return animation ? animation->m_currentLoop : 0;
}

int XAbstractAnimation_duration_base(const XAbstractAnimation* animation)
{
    if (!animation)
        return 0;
    return XVtableGetFunc(XClassGetVtable(animation), EXAbstractAnimation_Duration,
                          int(*)(const XAbstractAnimation*))(animation);
}

int XAbstractAnimation_totalDuration(const XAbstractAnimation* animation)
{
    if (!animation)
        return 0;
    const int dura = XAbstractAnimation_duration_base(animation);
    if (dura <= 0)
        return dura;
    if (animation->m_loopCount < 0)
        return -1;
    return dura * animation->m_loopCount;
}

/* ==================== 时间推进（对标 QAbstractAnimation::setCurrentTime） ==================== */

void XAbstractAnimation_setCurrentTime(XAbstractAnimation* animation, int msecs)
{
    if (!animation)
        return;
    if (msecs < 0)
        msecs = 0;

    const int dura = XAbstractAnimation_duration_base(animation);
    const int totalLoopCount = animation->m_loopCount;
    const int totalDura = dura <= 0 ? dura : ((totalLoopCount < 0) ? -1 : dura * totalLoopCount);
    if (totalDura != -1 && msecs > totalDura)
        msecs = totalDura;

    const int oldTotalCurrentTime = animation->m_totalCurrentTime;
    animation->m_totalCurrentTime = msecs;

    XAbstractAnimation_Direction currentDirection = animation->m_direction;

    const int oldLoop = animation->m_currentLoop;
    int newCurrentLoop = (dura <= 0) ? 0 : (msecs / dura);
    if (totalLoopCount >= 0 && newCurrentLoop == totalLoopCount) {
        /* 已在末端：时间钳到单循环时长、循环号回退一格（对标 Qt）。 */
        animation->m_currentLoopTime = dura > 0 ? dura : 0;
        newCurrentLoop = totalLoopCount > 0 ? totalLoopCount - 1 : 0;
    } else {
        if (currentDirection == XAbstractAnimation_Forward) {
            animation->m_currentLoopTime = (dura <= 0) ? msecs : (msecs % dura);
        } else {
            animation->m_currentLoopTime = (dura <= 0) ? msecs : ((msecs - 1) % dura) + 1;
            if (animation->m_currentLoopTime == dura)
                newCurrentLoop = newCurrentLoop - 1;
        }
    }
    animation->m_currentLoop = newCurrentLoop;

    /* 虚函数可能改属性，读回后再发通知（对标 Qt 次序）。 */
    XAbstractAnimation_updateCurrentTime_base(animation, animation->m_currentLoopTime);
    newCurrentLoop = animation->m_currentLoop;
    currentDirection = animation->m_direction;
    const int newTotalCurrentTime = animation->m_totalCurrentTime;

    if (newCurrentLoop != oldLoop)
        XAbstractAnimation_currentLoopChanged_signal(animation, newCurrentLoop);
    if (oldTotalCurrentTime != newTotalCurrentTime) {
        /* Qt 的 currentTime 是 BINDABLE 无 NOTIFY 信号，此处只对齐状态面。 */
    }

    /* 到达端点自动停止（对标 Qt 的 stop 分支）。 */
    if ((currentDirection == XAbstractAnimation_Forward && totalDura != -1
         && newTotalCurrentTime == totalDura)
        || (currentDirection == XAbstractAnimation_Backward && newTotalCurrentTime == 0)) {
        XAbstractAnimation_stop(animation);
    }
}

/* ==================== 生命周期控制（对标 start/stop/pause/resume） ==================== */

void XAbstractAnimation_start_ex(XAbstractAnimation* animation,
                                 XAbstractAnimation_DeletionPolicy policy)
{
    if (!animation || animation->m_state == XAbstractAnimation_Running)
        return;
    animation->m_deleteWhenStopped = policy == XAbstractAnimation_DeleteWhenStopped;
    XAbstractAnimation_setState_internal(animation, XAbstractAnimation_Running);
}

void XAbstractAnimation_stop(XAbstractAnimation* animation)
{
    if (!animation || animation->m_state == XAbstractAnimation_Stopped)
        return;
    XAbstractAnimation_setState_internal(animation, XAbstractAnimation_Stopped);
}

void XAbstractAnimation_pause(XAbstractAnimation* animation)
{
    if (!animation || animation->m_state == XAbstractAnimation_Stopped)
        return;
    XAbstractAnimation_setState_internal(animation, XAbstractAnimation_Paused);
}

void XAbstractAnimation_resume(XAbstractAnimation* animation)
{
    if (!animation || animation->m_state != XAbstractAnimation_Paused)
        return;
    XAbstractAnimation_setState_internal(animation, XAbstractAnimation_Running);
}

void XAbstractAnimation_setPaused(XAbstractAnimation* animation, bool paused)
{
    if (paused)
        XAbstractAnimation_pause(animation);
    else
        XAbstractAnimation_resume(animation);
}

/* ==================== 虚调度入口 ==================== */

void XAbstractAnimation_updateCurrentTime_base(XAbstractAnimation* animation, int currentTime)
{
    if (ISNULL(animation, "XAbstractAnimation") || ISNULL(XClassGetVtable(animation), "Vtable"))
        return;
    XClassGetVirtualFunc(animation, EXAbstractAnimation_UpdateCurrentTime,
                         void(*)(XAbstractAnimation*, int))(animation, currentTime);
}

void XAbstractAnimation_updateState_base(XAbstractAnimation* animation,
                                         XAbstractAnimation_State newState,
                                         XAbstractAnimation_State oldState)
{
    if (ISNULL(animation, "XAbstractAnimation") || ISNULL(XClassGetVtable(animation), "Vtable"))
        return;
    XClassGetVirtualFunc(animation, EXAbstractAnimation_UpdateState,
                         void(*)(XAbstractAnimation*, XAbstractAnimation_State,
                                 XAbstractAnimation_State))(animation, newState, oldState);
}

void XAbstractAnimation_updateDirection_base(XAbstractAnimation* animation,
                                             XAbstractAnimation_Direction direction)
{
    if (ISNULL(animation, "XAbstractAnimation") || ISNULL(XClassGetVtable(animation), "Vtable"))
        return;
    XClassGetVirtualFunc(animation, EXAbstractAnimation_UpdateDirection,
                         void(*)(XAbstractAnimation*, XAbstractAnimation_Direction))(animation,
                                                                                     direction);
}

/* ==================== 信号 ==================== */

void* XAbstractAnimation_finished_signal(XAbstractAnimation* animation)
{
    XEmitSignal((XObject*)animation, XAbstractAnimation_finished_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XAbstractAnimation_stateChanged_signal(XAbstractAnimation* animation,
                                             XAbstractAnimation_State newState,
                                             XAbstractAnimation_State oldState)
{
    XEmitSignal((XObject*)animation, XAbstractAnimation_stateChanged_signal,
                XVarList_create(4, sizeof(int), &newState, sizeof(int), &oldState),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XAbstractAnimation_currentLoopChanged_signal(XAbstractAnimation* animation,
                                                   int currentLoop)
{
    XEmitSignal((XObject*)animation, XAbstractAnimation_currentLoopChanged_signal,
                XVarList_create(2, sizeof(int), &currentLoop),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XAbstractAnimation_directionChanged_signal(XAbstractAnimation* animation,
                                                 XAbstractAnimation_Direction direction)
{
    XEmitSignal((XObject*)animation, XAbstractAnimation_directionChanged_signal,
                XVarList_create(2, sizeof(int), &direction),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
