#include "XCodeTest.h"

#include "XAbstractAnimation.h"
#include "XAnimationDriver.h"
#include "XAnimationGroup.h"
#include "XCoreApplication.h"
#include "XObject.h"
#include "XPrintf.h"
#include "XSync_config.h"
#include "XThread.h"

/**
 * @file XAnimationTest.c
 * @brief XAnimation 模块（XAbstractAnimation/XAnimationGroup/XAnimationDriver）
 *        全量行为测试，对标 Qt 6.8 QAbstractAnimation 语义。
 *
 * @details 时间推进语义用 setCurrentTime 做确定性断言；驱动器按帧推进只做
 *          真实时序的冒烟断言（毫秒级时差依赖单调钟，不做等值断言）。
 */

// ==================== 测试用固定时长动画 ====================

XCLASS_DEFINE_BEGING(XAnimationTest_FixedAnimation)
XCLASS_DEFINE_EXTEND_END(XAnimationTest_FixedAnimation, XAbstractAnimation);

typedef struct XAnimationTest_FixedAnimation {
    XAbstractAnimation m_class; ///< 继承 XAbstractAnimation，必须位于首成员。
    int m_duration;             ///< duration() 返回值（-1 表示未定义）。
    int m_updateCurrentTimeCount; ///< updateCurrentTime 回调计数。
    int m_updateStateCount;     ///< updateState 回调计数。
    int m_updateDirectionCount; ///< updateDirection 回调计数。
    int m_lastCurrentTime;      ///< 最近一次 updateCurrentTime 的实参。
} XAnimationTest_FixedAnimation;

static int VXAnimationTest_FixedAnimation_duration(
    const XAbstractAnimation* animation)
{
    return ((const XAnimationTest_FixedAnimation*)animation)->m_duration;
}

static void VXAnimationTest_FixedAnimation_updateCurrentTime(
    XAbstractAnimation* animation, int currentTime)
{
    XAnimationTest_FixedAnimation* self = (XAnimationTest_FixedAnimation*)animation;
    ++self->m_updateCurrentTimeCount;
    self->m_lastCurrentTime = currentTime;
}

static void VXAnimationTest_FixedAnimation_updateState(
    XAbstractAnimation* animation, XAbstractAnimation_State newState,
    XAbstractAnimation_State oldState)
{
    (void)newState;
    (void)oldState;
    ++((XAnimationTest_FixedAnimation*)animation)->m_updateStateCount;
}

static void VXAnimationTest_FixedAnimation_updateDirection(
    XAbstractAnimation* animation, XAbstractAnimation_Direction direction)
{
    (void)direction;
    ++((XAnimationTest_FixedAnimation*)animation)->m_updateDirectionCount;
}

static XVtable* VXAnimationTest_FixedAnimation_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAnimationTest_FixedAnimation)
    XVTABLE_INHERIT_XCLASS(XAbstractAnimation);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractAnimation_Duration,
                             VXAnimationTest_FixedAnimation_duration);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractAnimation_UpdateCurrentTime,
                             VXAnimationTest_FixedAnimation_updateCurrentTime);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractAnimation_UpdateState,
                             VXAnimationTest_FixedAnimation_updateState);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractAnimation_UpdateDirection,
                             VXAnimationTest_FixedAnimation_updateDirection);
    return XVTABLE_DEFAULT;
}

static XAnimationTest_FixedAnimation* XAnimationTest_FixedAnimation_create(int duration)
{
    XAnimationTest_FixedAnimation* animation = XClass_Malloc(XAnimationTest_FixedAnimation);
    if (!animation)
        return NULL;
    XAbstractAnimation_init((XAbstractAnimation*)animation, NULL);
    XClassGetVtable(animation) = VXAnimationTest_FixedAnimation_class_init();
    Set_Class_Memory(animation, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(animation, true);
    animation->m_duration = duration;
    animation->m_updateCurrentTimeCount = 0;
    animation->m_updateStateCount = 0;
    animation->m_updateDirectionCount = 0;
    animation->m_lastCurrentTime = -1;
    return animation;
}

// ==================== 公共测试工具 ====================

static int g_checkCount = 0;
static int g_failureCount = 0;
static int g_finishedCount = 0;
static int g_stateChangedCount = 0;
static int g_loopChangedCount = 0;
static int g_directionChangedCount = 0;
static int g_destroyedCount = 0;
static XAbstractAnimation_State g_lastNewState = XAbstractAnimation_Stopped;
static int g_lastLoop = 0;

static void XAnimationTest_begin(const char* title)
{
    g_checkCount = 0;
    g_failureCount = 0;
    XPrintf("\n%s\n", title);
}

static bool XAnimationTest_expect(bool condition, const char* message)
{
    ++g_checkCount;
    if (!condition)
        ++g_failureCount;
    XPrintf("  [%s] %s\n", condition ? "通过" : "失败", message);
    return condition;
}

static void XAnimationTest_end(void)
{
    XPrintf("  本组检查：%d，失败：%d\n", g_checkCount, g_failureCount);
}

static void XAnimationTest_resetCounters(void)
{
    g_finishedCount = 0;
    g_stateChangedCount = 0;
    g_loopChangedCount = 0;
    g_directionChangedCount = 0;
    g_destroyedCount = 0;
    g_lastNewState = XAbstractAnimation_Stopped;
    g_lastLoop = 0;
}

static void XAnimationTest_finishedSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    (void)arguments;
    ++g_finishedCount;
}

static void XAnimationTest_stateChangedSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    XVarList_args_2(arguments, int, newState, int, oldState);
    (void)oldState;
    g_lastNewState = (XAbstractAnimation_State)newState;
    ++g_stateChangedCount;
}

static void XAnimationTest_loopChangedSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    XVarList_args_1(arguments, int, currentLoop);
    g_lastLoop = currentLoop;
    ++g_loopChangedCount;
}

static void XAnimationTest_directionChangedSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    (void)arguments;
    ++g_directionChangedCount;
}

static void XAnimationTest_destroyedSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    (void)arguments;
    ++g_destroyedCount;
}

static void XAnimationTest_connectAll(XAbstractAnimation* animation)
{
    XObject_connect((XObject*)animation, (size_t)XAbstractAnimation_finished_signal,
                      NULL, XAnimationTest_finishedSlot, XConnectionType_Direct);
    XObject_connect((XObject*)animation, (size_t)XAbstractAnimation_stateChanged_signal,
                      NULL, XAnimationTest_stateChangedSlot, XConnectionType_Direct);
    XObject_connect((XObject*)animation, (size_t)XAbstractAnimation_currentLoopChanged_signal,
                      NULL, XAnimationTest_loopChangedSlot, XConnectionType_Direct);
    XObject_connect((XObject*)animation, (size_t)XAbstractAnimation_directionChanged_signal,
                      NULL, XAnimationTest_directionChangedSlot, XConnectionType_Direct);
    XObject_connect((XObject*)animation, (size_t)XObject_destroyed_signal,
                      NULL, XAnimationTest_destroyedSlot, XConnectionType_Direct);
}

// ==================== 状态机与时间语义 ====================

static void XAnimationTest_stateBasics(void)
{
    XAnimationTest_begin("XAbstractAnimation 状态机与时间语义（对标 QAbstractAnimation）");

    /* 默认属性面。 */
    XAnimationTest_FixedAnimation* animation = XAnimationTest_FixedAnimation_create(100);
    XAnimationTest_expect(animation != NULL, "创建固定时长动画");
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Stopped,
                          "初始状态为 Stopped");
    XAnimationTest_expect(XAbstractAnimation_loopCount((XAbstractAnimation*)animation) == 1,
                          "默认循环次数为 1");
    XAnimationTest_expect(XAbstractAnimation_direction((XAbstractAnimation*)animation) == XAbstractAnimation_Forward,
                          "默认方向为 Forward");
    XAnimationTest_expect(XAbstractAnimation_currentTime((XAbstractAnimation*)animation) == 0
                              && XAbstractAnimation_currentLoopTime((XAbstractAnimation*)animation) == 0,
                          "初始时间为 0");
    XAnimationTest_expect(XAbstractAnimation_duration_base((XAbstractAnimation*)animation) == 100,
                          "duration 虚函数调度到派生类");
    XAnimationTest_expect(XAbstractAnimation_totalDuration((XAbstractAnimation*)animation) == 100,
                          "totalDuration = duration × loopCount");
    XAnimationTest_expect(XAbstractAnimation_group((XAbstractAnimation*)animation) == NULL,
                          "未加入组时 group 为空");
    XAnimationTest_connectAll((XAbstractAnimation*)animation);

    /* 端点自停 + finished。 */
    XAbstractAnimation_start((XAbstractAnimation*)animation);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Running,
                          "start 后进入 Running");
    XAnimationDriver* defaultDriver = XAnimationDriver_defaultInstance();
    int guard = 0;
    while (XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Running
           && guard < 24) {
        XThread_msleep(16);
        XAnimationDriver_advance(defaultDriver);
        ++guard;
    }
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Stopped,
                          "duration 100 的动画被驱动器推进到端点自停");
    XAnimationTest_expect(g_finishedCount >= 1, "自然停止发出 finished");

    /* start 幂等与 rewind。 */
    XAnimationTest_resetCounters();
    XAbstractAnimation_setCurrentTime((XAbstractAnimation*)animation, 40);
    XAnimationTest_expect(XAbstractAnimation_currentTime((XAbstractAnimation*)animation) == 40,
                          "手动 setCurrentTime 生效");
    XAbstractAnimation_start((XAbstractAnimation*)animation);
    XAnimationTest_expect(XAbstractAnimation_currentTime((XAbstractAnimation*)animation) == 0,
                          "从非零时间重新 start 按方向回卷到 0");
    XAbstractAnimation_start((XAbstractAnimation*)animation);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Running
                              && g_stateChangedCount == 1,
                          "Running 状态重复 start 为空操作");
    XAbstractAnimation_stop((XAbstractAnimation*)animation);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Stopped
                              && g_finishedCount == 0,
                          "中途 stop 不发 finished（对标 Qt 端点条件）");
    XAnimationTest_expect(g_lastNewState == XAbstractAnimation_Stopped,
                          "stateChanged 报告新状态 Stopped");

    /* 暂停/恢复。 */
    XAnimationTest_resetCounters();
    XAbstractAnimation_pause((XAbstractAnimation*)animation);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Stopped,
                          "Stopped 状态下的 pause 请求被拒绝");
    XAbstractAnimation_resume((XAbstractAnimation*)animation);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Stopped,
                          "非 Paused 状态下的 resume 请求被拒绝");
    XAbstractAnimation_start((XAbstractAnimation*)animation);
    XAbstractAnimation_setPaused((XAbstractAnimation*)animation, true);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Paused,
                          "setPaused(true) 暂停");
    XAbstractAnimation_setPaused((XAbstractAnimation*)animation, false);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Running,
                          "setPaused(false) 恢复");
    XAbstractAnimation_stop((XAbstractAnimation*)animation);

    /* 循环推进：loopCount=3 跨循环与 currentLoopChanged。 */
    XAnimationTest_resetCounters();
    XAbstractAnimation_setLoopCount((XAbstractAnimation*)animation, 3);
    XAnimationTest_expect(XAbstractAnimation_totalDuration((XAbstractAnimation*)animation) == 300,
                          "loopCount=3 时 totalDuration=300");
    XAbstractAnimation_setCurrentTime((XAbstractAnimation*)animation, 250);
    XAnimationTest_expect(XAbstractAnimation_currentLoop((XAbstractAnimation*)animation) == 2,
                          "250ms 落在第 3 圈（currentLoop=2）");
    XAnimationTest_expect(XAbstractAnimation_currentLoopTime((XAbstractAnimation*)animation) == 50,
                          "循环内时间 = 250 % 100");
    XAnimationTest_expect(g_loopChangedCount >= 1 && g_lastLoop == 2,
                          "循环序号变化发出 currentLoopChanged");
    XAnimationTest_expect(g_finishedCount == 0, "未到端点不发 finished");

    XAbstractAnimation_setLoopCount((XAbstractAnimation*)animation, 1);
    XAbstractAnimation_setCurrentTime((XAbstractAnimation*)animation, 100);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Stopped,
                          "Forward 到达端点保持停止态");
    XAnimationTest_expect(g_finishedCount == 0,
                          "已停止动画的端点 setCurrentTime 不重复发 finished（Qt stop 幂等）");
    XAbstractAnimation_start((XAbstractAnimation*)animation);
    XAnimationTest_resetCounters();
    XAbstractAnimation_setCurrentTime((XAbstractAnimation*)animation, 100);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Stopped
                              && g_finishedCount == 1,
                          "Running 中到达 Forward 端点自停并发 finished");

    /* loopCount=0：不运行。 */
    XAnimationTest_resetCounters();
    XAbstractAnimation_setLoopCount((XAbstractAnimation*)animation, 0);
    XAbstractAnimation_start((XAbstractAnimation*)animation);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Stopped,
                          "loopCount=0 时 start 不改变状态");
    XAbstractAnimation_setLoopCount((XAbstractAnimation*)animation, 1);

    /* 无限循环：手动 stop 也发 finished。 */
    XAnimationTest_resetCounters();
    XAbstractAnimation_setLoopCount((XAbstractAnimation*)animation, -1);
    XAnimationTest_expect(XAbstractAnimation_totalDuration((XAbstractAnimation*)animation) == -1,
                          "无限循环 totalDuration=-1");
    XAbstractAnimation_start((XAbstractAnimation*)animation);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Running,
                          "无限循环不由端点自停");
    XAbstractAnimation_stop((XAbstractAnimation*)animation);
    XAnimationTest_expect(g_finishedCount == 1, "无限循环手动停止发 finished");
    XAbstractAnimation_setLoopCount((XAbstractAnimation*)animation, 1);

    /* Backward 方向：Stopped 下改向重置 + 反向时间计算 + 0 端点。
     * 与 Qt 一致：loopCount 有限时 setCurrentTime 先被钳到 totalDura，
     * 反向 (t-1)%dura+1 的跨圈计算只在无限循环（totalDura=-1）下可达。 */
    XAnimationTest_resetCounters();
    XAbstractAnimation_setDirection((XAbstractAnimation*)animation, XAbstractAnimation_Backward);
    XAnimationTest_expect(XAbstractAnimation_currentLoopTime((XAbstractAnimation*)animation) == 100
                              && XAbstractAnimation_currentLoop((XAbstractAnimation*)animation) == 0,
                          "Stopped 改向 Backward 重置到 duration/末圈");
    XAnimationTest_expect(g_directionChangedCount == 1, "改向发出 directionChanged");
    XAbstractAnimation_setCurrentTime((XAbstractAnimation*)animation, 130);
    XAnimationTest_expect(XAbstractAnimation_currentTime((XAbstractAnimation*)animation) == 100,
                          "有限循环下 setCurrentTime(130) 被钳到 totalDura=100（对标 Qt）");
    XAbstractAnimation_setLoopCount((XAbstractAnimation*)animation, -1);
    XAnimationTest_resetCounters();
    XAbstractAnimation_setCurrentTime((XAbstractAnimation*)animation, 130);
    XAnimationTest_expect(XAbstractAnimation_currentLoopTime((XAbstractAnimation*)animation) == 30,
                          "Backward 的循环内时间 = (t-1) % dura + 1（无限循环）");
    XAnimationTest_expect(g_loopChangedCount >= 1, "Backward 跨圈报告 currentLoopChanged");
    XAbstractAnimation_setCurrentTime((XAbstractAnimation*)animation, 0);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Stopped,
                          "Backward 到达 0 端点自动停止");
    XAnimationTest_expect(g_finishedCount == 0,
                          "已停止动画的 0 端点 setCurrentTime 不重复发 finished");
    XAbstractAnimation_start((XAbstractAnimation*)animation);
    XAnimationTest_resetCounters();
    XAbstractAnimation_setCurrentTime((XAbstractAnimation*)animation, 0);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Stopped
                              && g_finishedCount == 1,
                          "Running 中到达 Backward 0 端点自停并发 finished");
    XAbstractAnimation_setDirection((XAbstractAnimation*)animation, XAbstractAnimation_Forward);
    XAbstractAnimation_setLoopCount((XAbstractAnimation*)animation, 1);

    /* 未定义时长：只跑一次。 */
    XAnimationTest_FixedAnimation* endless = XAnimationTest_FixedAnimation_create(-1);
    XAnimationTest_connectAll((XAbstractAnimation*)endless);
    XAnimationTest_resetCounters();
    XAnimationTest_expect(XAbstractAnimation_totalDuration((XAbstractAnimation*)endless) == -1,
                          "duration=-1 时 totalDuration=-1");
    XAbstractAnimation_start((XAbstractAnimation*)endless);
    XAbstractAnimation_stop((XAbstractAnimation*)endless);
    XAnimationTest_expect(g_finishedCount == 1, "未定义时长停止时发 finished");
    XClassDelete((XClass*)endless);

    /* duration 0：启动即到端点。 */
    XAnimationTest_FixedAnimation* zero = XAnimationTest_FixedAnimation_create(0);
    XAnimationTest_connectAll((XAbstractAnimation*)zero);
    XAnimationTest_resetCounters();
    XAbstractAnimation_start((XAbstractAnimation*)zero);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)zero) == XAbstractAnimation_Stopped,
                          "duration=0 启动即自停");
    XAnimationTest_expect(g_finishedCount == 1, "duration=0 启动发 finished");
    XClassDelete((XClass*)zero);

    XClassDelete((XClass*)animation);

    /* deleteWhenStopped：自然结束走 deleteLater。 */
    XAnimationTest_FixedAnimation* doomed = XAnimationTest_FixedAnimation_create(0);
    XAnimationTest_connectAll((XAbstractAnimation*)doomed);
    XAnimationTest_resetCounters();
    XAbstractAnimation_start_ex((XAbstractAnimation*)doomed, XAbstractAnimation_DeleteWhenStopped);
    /* deleteLater 的兑现需要真实事件循环层（loopLevel>0 时按"外层循环前
     * 投递"语义兑现），processEvents 的裸泵不带循环层。 */
    XCoreApplication_execBounded(50);
    XAnimationTest_expect(g_destroyedCount == 1, "DeleteWhenStopped 结束后对象被延迟销毁");

    XAnimationTest_end();
}

// ==================== 动画组 ====================

/** @brief 测试用固定时长动画组（派生组基类，验证运行中取空自停路径）。 */
XCLASS_DEFINE_BEGING(XAnimationTest_TimedGroup)
XCLASS_DEFINE_EXTEND_END(XAnimationTest_TimedGroup, XAnimationGroup);

typedef struct XAnimationTest_TimedGroup {
    XAnimationGroup m_class; ///< 继承 XAnimationGroup，必须位于首成员。
} XAnimationTest_TimedGroup;

static int VXAnimationTest_TimedGroup_duration(const XAbstractAnimation* animation)
{
    (void)animation;
    return 60;
}

static XVtable* XAnimationTest_TimedGroup_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XAnimationTest_TimedGroup)
    XVTABLE_INHERIT_XCLASS(XAnimationGroup);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractAnimation_Duration,
                             VXAnimationTest_TimedGroup_duration);
    return XVTABLE_DEFAULT;
}

static XAnimationTest_TimedGroup* XAnimationTest_TimedGroup_create(void)
{
    XAnimationTest_TimedGroup* group = XClass_Malloc(XAnimationTest_TimedGroup);
    if (!group)
        return NULL;
    XAnimationGroup_init((XAnimationGroup*)group);
    XClassGetVtable(group) = XAnimationTest_TimedGroup_class_init();
    Set_Class_Memory(group, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(group, true);
    return group;
}

static void XAnimationTest_groupSemantics(void)
{
    XAnimationTest_begin("XAnimationGroup 容器语义（对标 QAnimationGroup）");

    XAnimationGroup* group = XAnimationGroup_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XAnimationTest_expect(group != NULL, "创建动画组");
    XAnimationTest_expect(XAnimationGroup_animationCount(group) == 0, "初始组为空");

    XAnimationTest_FixedAnimation* a = XAnimationTest_FixedAnimation_create(10);
    XAnimationTest_FixedAnimation* b = XAnimationTest_FixedAnimation_create(20);
    XAnimationTest_expect(XAnimationGroup_addAnimation(group, (XAbstractAnimation*)a),
                          "加入动画 a");
    XAnimationTest_expect(XAnimationGroup_addAnimation(group, (XAbstractAnimation*)b),
                          "加入动画 b");
    XAnimationTest_expect(XAbstractAnimation_group((XAbstractAnimation*)a) == group,
                          "a 的 group 回指本组");
    XAnimationTest_expect(XAnimationGroup_animationCount(group) == 2, "组内数量为 2");
    XAnimationTest_expect(XAnimationGroup_animationAt(group, 0) == (XAbstractAnimation*)a
                              && XAnimationGroup_indexOfAnimation(group, (XAbstractAnimation*)b) == 1,
                          "animationAt/indexOfAnimation 按插入序");
    XAnimationTest_expect(!XAnimationGroup_addAnimation(group, NULL), "NULL 动画被拒绝");

    XAnimationGroup* other = XAnimationGroup_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XAnimationTest_expect(XAnimationGroup_addAnimation(other, (XAbstractAnimation*)a),
                          "跨组 addAnimation 自动从旧组搬运（对标 insertAnimation）");
    XAnimationTest_expect(XAbstractAnimation_group((XAbstractAnimation*)a) == other
                              && XAnimationGroup_animationCount(group) == 1
                              && XAnimationGroup_animationCount(other) == 1,
                          "搬运后旧组不再持有、新组接管的 group 回指正确");

    XAnimationTest_expect(XAnimationGroup_takeAnimation(other, 0) == (XAbstractAnimation*)a,
                          "takeAnimation 从 other 移出并返回 a");
    XAnimationTest_expect(XAbstractAnimation_group((XAbstractAnimation*)a) == NULL,
                          "移出后 group 回指清空");
    XClassDelete((XClass*)a);
    XClassDelete((XClass*)other);

    XAnimationTest_expect(XAnimationGroup_takeAnimation(group, 0) == (XAbstractAnimation*)b,
                          "takeAnimation 移出并返回 b");
    XAnimationTest_expect(XAbstractAnimation_group((XAbstractAnimation*)b) == NULL
                              && XAnimationGroup_animationCount(group) == 0,
                          "b 移出后组为空");
    XClassDelete((XClass*)b);

    /* 对标 animationRemoved：非空运行中的组被取空 → 自停并把时间归零。
     * 用固定时长组派生类（基类 duration=0 启动即到端点，无法停留 Running）。 */
    XAnimationTest_FixedAnimation* c = XAnimationTest_FixedAnimation_create(30);
    XAnimationTest_TimedGroup* g2 = XAnimationTest_TimedGroup_create();
    XAnimationGroup_addAnimation((XAnimationGroup*)g2, (XAbstractAnimation*)c);
    XAbstractAnimation_start((XAbstractAnimation*)g2);
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)g2) == XAbstractAnimation_Running,
                          "固定时长组可独立启动");
    XAnimationTest_expect(XAnimationGroup_takeAnimation((XAnimationGroup*)g2, 0) == (XAbstractAnimation*)c,
                          "取空运行中的组返回子动画");
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)g2) == XAbstractAnimation_Stopped
                              && XAbstractAnimation_currentTime((XAbstractAnimation*)g2) == 0,
                          "组被取空后自停并把当前时间归零（对标 animationRemoved）");
    XClassDelete((XClass*)c);
    XClassDelete((XClass*)g2);

    /* clear 随组所有权销毁子动画。 */
    XAnimationTest_FixedAnimation* d = XAnimationTest_FixedAnimation_create(15);
    XAnimationGroup_addAnimation(group, (XAbstractAnimation*)d);
    XObject_connect((XObject*)d, (size_t)XObject_destroyed_signal,
                      NULL, XAnimationTest_destroyedSlot, XConnectionType_Direct);
    XAnimationTest_resetCounters();
    XAnimationGroup_clear(group);
    XAnimationTest_expect(g_destroyedCount == 1 && XAnimationGroup_animationCount(group) == 0,
                          "clear 销毁全部子动画");
    XClassDelete((XClass*)group);

    XAnimationTest_end();
}

// ==================== 驱动器 ====================

static void XAnimationTest_driverLifecycle(void)
{
    XAnimationTest_begin("XAnimationDriver 安装与自动驱动（对标 QAnimationDriver）");

    XAnimationDriver* driver = XAnimationDriver_defaultInstance();
    XAnimationTest_expect(driver != NULL, "默认驱动器单例可获取");
    bool wasRunning = XAnimationDriver_isRunning(driver);

    XAnimationTest_FixedAnimation* animation = XAnimationTest_FixedAnimation_create(1000);
    XAbstractAnimation_start((XAbstractAnimation*)animation);
    XAnimationTest_expect(XAnimationDriver_isRunning(driver),
                          "首个顶层动画启动自动安装驱动器");
    XAnimationTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation) == XAbstractAnimation_Running,
                          "动画处于 Running");

    XThread_msleep(40);
    XAnimationDriver_advance(driver);
    const int progressed = XAbstractAnimation_currentTime((XAbstractAnimation*)animation);
    XAnimationTest_expect(progressed > 0, "手动 advance 按单调时差推进时间");

    XAbstractAnimation_pause((XAbstractAnimation*)animation);
    XThread_msleep(30);
    XAnimationDriver_advance(driver);
    XAnimationTest_expect(XAbstractAnimation_currentTime((XAbstractAnimation*)animation) == progressed,
                          "暂停中的动画不被推进");

    XAbstractAnimation_stop((XAbstractAnimation*)animation);
    if (!wasRunning)
        XAnimationTest_expect(!XAnimationDriver_isRunning(driver),
                              "最后一个顶层动画注销后驱动器自动卸载");
    XAnimationTest_expect(XAnimationDriver_isRunning(driver) == wasRunning,
                          "驱动器回到安装前状态");

    XAnimationTest_end();
}

// ==================== 入口 ====================

int XAnimationTest_runAll(void)
{
    int totalFailures = 0;
    XAnimationTest_stateBasics();
    totalFailures += g_failureCount;
    XAnimationTest_groupSemantics();
    totalFailures += g_failureCount;
    XAnimationTest_driverLifecycle();
    totalFailures += g_failureCount;
    XPrintf("\nXAnimation 全量测试：失败计数 = %d\n", totalFailures);
    return totalFailures;
}
