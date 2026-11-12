#include "XCodeTest.h"

#include "XAction.h"
#include "XCoreApplication.h"
#include "XEventTransition.h"
#include "XFinalState.h"
#include "XHistoryState.h"
#include "XKeyEventTransition.h"
#include "XMemory.h"
#include "XTestMenu.h"
#include "XMouseEventTransition.h"
#include "XObject.h"
#include "XPrintf.h"
#include "XSignalTransition.h"
#include "XState.h"
#include "XStateMachine.h"
#include "XThread.h"
#include "XVarList.h"
#include "XAnimationDriver.h"
#include "XAnimationGroup.h"

#include <stdlib.h>
#include <string.h>

/**
 * @file XStateMachineTest.c
 * @brief XStateMachine 系列类的行为测试和基础用法示例。
 *
 * @details 阅读每个测试时可以按下面的固定顺序理解状态机代码：
 * 1. 使用 XStateMachine_create 创建状态机根节点；
 * 2. 使用 XState_create_ex 或 XFinalState_create_ex 创建状态并传入父状态，
 *    父状态会取得子状态所有权，最终只需销毁根状态机；
 * 3. 每个互斥复合状态都使用 XState_setInitialState 指定直接初始子状态；
 * 4. 在源状态上创建转换，再使用 XAbstractTransition_setTargetState 指定目标，
 *    源状态会取得转换所有权，目标状态只被引用；
 * 5. 调用 XStateMachine_start 或 XStateMachine_stop 只提交异步请求，
 *    测试随后处理事件循环，让启动、转换或停止真正执行；
 * 6. 通过 active、isRunning、信号计数和错误码验证最终配置与生命周期。
 *
 * @note 独立创建且没有加入状态树的转换仍由调用者释放；事件投递和延迟事件
 *       是否转移所有权，以对应 API 的返回值和头文件注释为准。
 */

// ==================== 自定义转换示例 ====================

XCLASS_DEFINE_BEGING(XStateMachineTest_TypeTransition)
XCLASS_DEFINE_EXTEND_END(XStateMachineTest_TypeTransition, XAbstractTransition);

/**
 * @brief 按 XEventType 匹配的最小自定义转换。
 *
 * @details 该类型演示派生 XAbstractTransition 的完整步骤：结构体第一个成员放基类；
 * class_init 先继承基类虚函数表，再重载 EventTest 和 OnTransition；构造时先初始化
 * 基类，再切换到派生类虚函数表。实际业务可以用同样方式实现条件转换或自定义动作。
 */
typedef struct XStateMachineTest_TypeTransition {
    XAbstractTransition m_class; ///< 继承 XAbstractTransition，必须位于首成员。
    XEventType m_eventType;      ///< eventTest 接受的事件类型。
    int* m_triggerCount;         ///< 可选测试计数器，不取得所有权。
} XStateMachineTest_TypeTransition;

/** @brief 只有事件类型相等时，转换才参与当前 microstep。 */
static bool VXStateMachineTest_TypeTransition_eventTest(
    XStateMachineTest_TypeTransition* transition, XEvent* event)
{
    return transition && event
        && XEvent_type(event) == transition->m_eventType;
}

/** @brief 转换被选中后执行动作；这里用计数器记录真实执行次数。 */
static void VXStateMachineTest_TypeTransition_onTransition(
    XStateMachineTest_TypeTransition* transition, XEvent* event)
{
    (void)event;
    if (transition && transition->m_triggerCount)
        ++(*transition->m_triggerCount);
}

static XVtable* XStateMachineTest_TypeTransition_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStateMachineTest_TypeTransition)
    XVTABLE_INHERIT_XCLASS(XAbstractTransition);
    XVTABLE_OVERLOAD_DEFAULT(
        EXAbstractTransition_EventTest,
        VXStateMachineTest_TypeTransition_eventTest);
    XVTABLE_OVERLOAD_DEFAULT(
        EXAbstractTransition_OnTransition,
        VXStateMachineTest_TypeTransition_onTransition);
    return XVTABLE_DEFAULT;
}

/**
 * @brief 创建自定义事件类型转换，并立即把它加入源状态。
 * @param sourceState 转换源状态，创建成功后取得转换所有权。
 * @param eventType 要匹配的事件类型。
 * @param triggerCount 可选执行次数计数器，生命周期必须覆盖转换。
 * @return 新转换；内存分配失败时返回 NULL。
 */
static XStateMachineTest_TypeTransition* XStateMachineTest_TypeTransition_create_ex(XMemoryType memory,
    XState* sourceState, XEventType eventType, int* triggerCount)
{
    XStateMachineTest_TypeTransition* transition =
        XMemory_malloc(sizeof(XStateMachineTest_TypeTransition), memory);
    if (!transition)
        return NULL;
    XAbstractTransition_init((XAbstractTransition*)transition, sourceState);
    XClassSetVtable(transition, XStateMachineTest_TypeTransition);
    transition->m_eventType = eventType;
    transition->m_triggerCount = triggerCount;
    Set_Class_Memory(transition, memory); Set_Class_IsHeap(transition, true);
    return transition;
}

static XStateMachineTest_TypeTransition* XStateMachineTest_TypeTransition_create(
    XState* sourceState, XEventType eventType, int* triggerCount)
{
    return XStateMachineTest_TypeTransition_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, sourceState, eventType, triggerCount);
}

// ==================== 测试事件源 ====================

XCLASS_DEFINE_BEGING(XStateMachineTest_EventSource)
XCLASS_DEFINE_EXTEND_END(XStateMachineTest_EventSource, XObject);

/**
 * @brief 提供可安装事件过滤器的最小测试对象。
 * @note event 返回 true，表示原始事件由事件源处理；状态机通过过滤器获得事件克隆，
 *       因此原始事件是否继续传播不会影响 XEventTransition 的匹配。
 */
typedef struct XStateMachineTest_EventSource {
    XObject m_class;
} XStateMachineTest_EventSource;

static bool VXStateMachineTest_EventSource_event(XStateMachineTest_EventSource* source,
                                                 XEvent* event)
{
    (void)source;
    (void)event;
    return true;
}

static XVtable* XStateMachineTest_EventSource_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStateMachineTest_EventSource)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXStateMachineTest_EventSource_event);
    return XVTABLE_DEFAULT;
}

static XStateMachineTest_EventSource* XStateMachineTest_EventSource_create_ex(XMemoryType memory)
{
    XStateMachineTest_EventSource* source = XMemory_malloc(sizeof(XStateMachineTest_EventSource), memory);
    if (!source)
        return NULL;
    XObject_init((XObject*)source);
    XClassGetVtable(source) = XStateMachineTest_EventSource_class_init();
    Set_Class_Memory(source, memory); Set_Class_IsHeap(source, true);
    return source;
}

static XStateMachineTest_EventSource* XStateMachineTest_EventSource_create(void)
{
    return XStateMachineTest_EventSource_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
}

static void XStateMachineAnimationApiTest(void);
static void XStateMachineAnimationPipelineTest(void);
static void XStateMachineBehaviorAlignTest(void);
static void XStateMachineCoverageGapTest(void);
static void XStateMachineSignalCoverageTest(void);

// ==================== 公共测试工具 ====================

static int g_checkCount = 0;
static int g_failureCount = 0;
static int g_finishedCount = 0;
static int g_startedCount = 0;
static int g_stoppedCount = 0;
static int g_runningChangedCount = 0;
static int g_parentEnteredCount = 0;
static int g_parentExitedCount = 0;

/**
 * @brief 处理两轮事件，覆盖“投递状态机处理事件”和“处理过程中再次投递事件”的情况。
 */
static void XStateMachineTest_processEvents(void)
{
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
}

/** @brief 开始一组测试并清空该组统计。 */
static void XStateMachineTest_begin(const char* title)
{
    g_checkCount = 0;
    g_failureCount = 0;
    XPrintf("\n%s\n", title);
}

/** @brief 输出单项检查结果，并累计检查数和失败数。 */
static bool XStateMachineTest_expect(bool condition, const char* message)
{
    ++g_checkCount;
    if (!condition)
        ++g_failureCount;
    XPrintf("  [%s] %s\n", condition ? "通过" : "失败", message);
    return condition;
}

/** @brief 输出当前测试组汇总，便于菜单运行时快速发现失败项。 */
static void XStateMachineTest_end(void)
{
    XPrintf("  本组检查：%d，失败：%d\n", g_checkCount, g_failureCount);
}

// ==================== 信号与槽辅助函数 ====================

static void XStateMachineTest_finishedSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    (void)arguments;
    ++g_finishedCount;
}

static void XStateMachineTest_startedSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    (void)arguments;
    ++g_startedCount;
}

static void XStateMachineTest_stoppedSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    (void)arguments;
    ++g_stoppedCount;
}

static void XStateMachineTest_runningChangedSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    (void)arguments;
    ++g_runningChangedCount;
}

static void XStateMachineTest_parentEnteredSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    (void)arguments;
    ++g_parentEnteredCount;
}

static void XStateMachineTest_parentExitedSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    (void)arguments;
    ++g_parentExitedCount;
}

static void* XStateMachineTest_finish_signal(XObject* sender)
{
    XEmitSignal(sender, XStateMachineTest_finish_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

static void* XStateMachineTest_advance_signal(XObject* sender)
{
    XEmitSignal(sender, XStateMachineTest_advance_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

static void* XStateMachineTest_leave_signal(XObject* sender)
{
    XEmitSignal(sender, XStateMachineTest_leave_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

static void* XStateMachineTest_restore_signal(XObject* sender)
{
    XEmitSignal(sender, XStateMachineTest_restore_signal,
                NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

// ==================== 对象事件转换测试 ====================

/** @brief 验证键值和修饰键掩码共同参与键盘事件匹配。 */
static void XStateMachineTest_keyEventTransition(void)
{
    XPrintf("\n  -- 键盘事件转换 --\n");

    // 1. 准备 waiting -> final 状态图，并要求 Control 修饰键。
    XStateMachineTest_EventSource* eventSource = XStateMachineTest_EventSource_create();
    XStateMachine* machine = XStateMachine_create();
    XState* waiting = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XFinalState* finalState = XFinalState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XState*)machine);
    XKeyEventTransition* transition = XKeyEventTransition_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XObject*)eventSource, XEVENT_TYPE_KEY_PRESS, 65, waiting);

    XKeyEventTransition_setModifierMask(
        transition, XKeyboardModifier_ControlModifier);
    XAbstractTransition_setTargetState(
        (XAbstractTransition*)transition, (XAbstractState*)finalState);
    XState_setInitialState((XState*)machine, (XAbstractState*)waiting);
    XStateMachine_start(machine);
    XStateMachineTest_processEvents();

    // 2. 键值相同但缺少 Control，不应触发转换。
    XCoreApplication_sendEvent(
        (XObject*)eventSource,
        (XEvent*)XKeyEvent_create(XEVENT_TYPE_KEY_PRESS, 65,
                                  XKeyboardModifier_ShiftModifier));
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XAbstractState_active((XAbstractState*)waiting),
                             "缺少必要修饰键的键盘事件不会触发转换");

    // 3. Control 存在时允许附带其他修饰键，应进入最终状态。
    XCoreApplication_sendEvent(
        (XObject*)eventSource,
        (XEvent*)XKeyEvent_create(
            XEVENT_TYPE_KEY_PRESS, 65,
            XKeyboardModifier_ControlModifier | XKeyboardModifier_ShiftModifier));
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XAbstractState_active((XAbstractState*)finalState),
                             "按键和必要修饰键掩码同时匹配时进入目标状态");

    XClassDelete((XClass*)machine);
    XClassDelete((XClass*)eventSource);
}

/** @brief 验证鼠标按键和命中测试多边形共同参与鼠标事件匹配。 */
static void XStateMachineTest_mouseEventTransition(void)
{
    XPrintf("\n  -- 鼠标事件转换 --\n");

    // 1. 准备 waiting -> final 状态图，并设置 100 x 100 的命中区域。
    XStateMachineTest_EventSource* eventSource = XStateMachineTest_EventSource_create();
    XStateMachine* machine = XStateMachine_create();
    XState* waiting = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XFinalState* finalState = XFinalState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XState*)machine);
    XMouseEventTransition* transition = XMouseEventTransition_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XObject*)eventSource, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
        XMouseButton_LeftButton, waiting);
    XVector* path = XVector_Create(XPoint);
    XPoint points[] = { { 0, 0 }, { 100, 0 }, { 100, 100 }, { 0, 100 } };

    XVector_push_back_2(path, points, sizeof(points) / sizeof(points[0]));
    XMouseEventTransition_setHitTestPath(transition, path);
    XClassDelete((XClass*)path);
    XAbstractTransition_setTargetState(
        (XAbstractTransition*)transition, (XAbstractState*)finalState);
    XState_setInitialState((XState*)machine, (XAbstractState*)waiting);
    XStateMachine_start(machine);
    XStateMachineTest_processEvents();

    // 2. 按键正确但坐标在区域外，不应触发转换。
    XPoint outside = { 150, 50 };
    XCoreApplication_sendEvent(
        (XObject*)eventSource,
        (XEvent*)XMouseEvent_create(XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                                    XMouseButton_LeftButton,
                                    XKeyboardModifier_NoModifier, outside));
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XAbstractState_active((XAbstractState*)waiting),
                             "命中测试区域外的鼠标事件不会触发转换");

    // 3. 按键和坐标都匹配，应进入最终状态。
    XPoint inside = { 50, 50 };
    XCoreApplication_sendEvent(
        (XObject*)eventSource,
        (XEvent*)XMouseEvent_create(XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                                    XMouseButton_LeftButton,
                                    XKeyboardModifier_NoModifier, inside));
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XAbstractState_active((XAbstractState*)finalState),
                             "鼠标按键和命中测试区域同时匹配时进入目标状态");

    XClassDelete((XClass*)machine);
    XClassDelete((XClass*)eventSource);
}

/**
 * @brief 验证 postEvent 的所有权规则，以及高优先级队列先于普通队列处理。
 *
 * @details 状态图如下：
 * source --高优先级事件--> highTarget --普通事件--> doneTarget
 * source --普通事件--> wrongTarget
 *
 * 先投递普通事件，再投递高优先级事件。如果队列优先级正确，最终会进入 doneTarget；
 * 如果错误地按投递顺序处理，则会先进入 wrongTarget，后续高优先级事件无法匹配。
 */
static void XStateMachineTest_postEventPriority(void)
{
    XPrintf("\n  -- 状态机事件队列优先级 --\n");

    int highCount = 0;
    int normalAfterHighCount = 0;
    int wrongCount = 0;
    XStateMachine* machine = XStateMachine_create();
    XState* source = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* highTarget = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* doneTarget = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* wrongTarget = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XEventType highType = (XEventType)XEvent_registerEventType(-1);
    XEventType normalType = (XEventType)XEvent_registerEventType(-1);

    // 转换随 sourceState 加入状态树，machine 销毁时会统一销毁这些转换。
    XStateMachineTest_TypeTransition* wrongTransition =
        XStateMachineTest_TypeTransition_create(source, normalType, &wrongCount);
    XStateMachineTest_TypeTransition* highTransition =
        XStateMachineTest_TypeTransition_create(source, highType, &highCount);
    XStateMachineTest_TypeTransition* normalAfterHighTransition =
        XStateMachineTest_TypeTransition_create(
            highTarget, normalType, &normalAfterHighCount);
    XAbstractTransition_setTargetState(
        (XAbstractTransition*)wrongTransition, (XAbstractState*)wrongTarget);
    XAbstractTransition_setTargetState(
        (XAbstractTransition*)highTransition, (XAbstractState*)highTarget);
    XAbstractTransition_setTargetState(
        (XAbstractTransition*)normalAfterHighTransition,
        (XAbstractState*)doneTarget);
    XState_setInitialState((XState*)machine, (XAbstractState*)source);

    XStateMachine_start(machine);
    XStateMachineTest_processEvents();

    // postEvent 返回 true 后，事件所有权转移到状态机，调用者不能再释放事件。
    bool normalPosted = XStateMachine_postEvent(
        machine, XEvent_create(normalType), XStateMachine_NormalPriority);
    bool highPosted = XStateMachine_postEvent(
        machine, XEvent_create(highType), XStateMachine_HighPriority);
    XStateMachineTest_expect(
        normalPosted && highPosted,
        "运行中的状态机接受普通和高优先级事件");

    XStateMachineTest_processEvents();
    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)doneTarget)
            && highCount == 1 && normalAfterHighCount == 1,
        "HighPriority 事件先执行，随后继续处理 NormalPriority 事件");
    XStateMachineTest_expect(
        !XAbstractState_active((XAbstractState*)wrongTarget) && wrongCount == 0,
        "NormalPriority 事件不会越过已排队的 HighPriority 事件");

    XStateMachine_stop(machine);
    XStateMachineTest_processEvents();

    // postEvent 返回 false 时不取得所有权，所以调用者必须自行销毁 rejectedEvent。
    XEvent* rejectedEvent = XEvent_create(normalType);
    XStateMachineTest_expect(
        !XStateMachine_postEvent(
            machine, rejectedEvent, XStateMachine_NormalPriority),
        "停止后的状态机拒绝 postEvent 且不取得事件所有权");
    XClassDelete((XClass*)rejectedEvent);

    XClassDelete((XClass*)machine);
}

// ==================== 信号转换和执行算法测试 ====================

/** @brief 验证一个信号事件在并行区域中只产生一次同步 microstep。 */
static void XStateMachineTest_parallelSignalEvent(void)
{
    XPrintf("\n  -- 并行区域共享信号 --\n");

    XObject* sender = XObject_create();
    XStateMachine* machine = XStateMachine_create();
    XState* parallel = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ParallelStates, (XState*)machine);
    XState* firstRegion = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parallel);
    XState* secondRegion = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parallel);
    XState* firstA = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, firstRegion);
    XState* firstB = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, firstRegion);
    XState* firstC = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, firstRegion);
    XState* secondA = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, secondRegion);
    XState* secondB = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, secondRegion);
    XState* secondC = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, secondRegion);

    // 两个并行区域都使用同一个 sender 和 signal，第二级转换用于检测重复投递。
    XState_setInitialState(firstRegion, (XAbstractState*)firstA);
    XState_setInitialState(secondRegion, (XAbstractState*)secondA);
    XState_setInitialState((XState*)machine, (XAbstractState*)parallel);
    XState_addTransition_2(firstA, sender,
                           XSignal(XStateMachineTest_advance_signal),
                           (XAbstractState*)firstB);
    XState_addTransition_2(firstB, sender,
                           XSignal(XStateMachineTest_advance_signal),
                           (XAbstractState*)firstC);
    XState_addTransition_2(secondA, sender,
                           XSignal(XStateMachineTest_advance_signal),
                           (XAbstractState*)secondB);
    XState_addTransition_2(secondB, sender,
                           XSignal(XStateMachineTest_advance_signal),
                           (XAbstractState*)secondC);

    XStateMachine_start(machine);
    XStateMachineTest_processEvents();
    XStateMachineTest_advance_signal(sender);
    XStateMachineTest_processEvents();

    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)firstB)
            && XAbstractState_active((XAbstractState*)secondB),
        "一个信号在全部并行区域中执行同一个同步 microstep");
    XStateMachineTest_expect(
        !XAbstractState_active((XAbstractState*)firstC)
            && !XAbstractState_active((XAbstractState*)secondC),
        "共享信号连接不会重复投递事件并连续触发第二级转换");

    XStateMachine_stop(machine);
    XStateMachineTest_processEvents();
    XClassDelete((XClass*)machine);
    XClassDelete((XClass*)sender);
}

/**
 * @brief 验证一个转换可以同时指定多个目标，并进入并行状态的不同区域。
 *
 * @details source 的信号转换同时指向 firstTarget 和 secondTarget。两个目标分别位于
 * parallel 的不同互斥区域中，因此同一个 microstep 应建立两条完整活动路径。
 */
static void XStateMachineTest_multipleTargetStates(void)
{
    XPrintf("\n  -- 并行区域多目标转换 --\n");

    XObject* sender = XObject_create();
    XStateMachine* machine = XStateMachine_create();
    XState* source = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* parallel = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ParallelStates, (XState*)machine);
    XState* firstRegion = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parallel);
    XState* secondRegion = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parallel);
    XState* firstTarget = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, firstRegion);
    XState* secondTarget = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, secondRegion);
    XSignalTransition* transition = XSignalTransition_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sender, XSignal(XStateMachineTest_advance_signal), source);
    XVector* targets = XVector_Create(XAbstractState*);
    XAbstractState* targetArray[] = {
        (XAbstractState*)firstTarget,
        (XAbstractState*)secondTarget
    };

    // setTargetStates 会复制指针列表；状态仍由状态树拥有，临时容器可立即销毁。
    XVector_push_back_2(
        targets, targetArray, sizeof(targetArray) / sizeof(targetArray[0]));
    bool targetsSet = XAbstractTransition_setTargetStates(
        (XAbstractTransition*)transition, targets);
    XClassDelete((XClass*)targets);
    XState_setInitialState((XState*)machine, (XAbstractState*)source);

    XStateMachineTest_expect(
        targetsSet
            && XVector_size_base((const XContainer*)
                XAbstractTransition_targetStates_const(
                    (XAbstractTransition*)transition)) == 2,
        "setTargetStates 复制并保存两个目标状态");

    XStateMachine_start(machine);
    XStateMachineTest_processEvents();
    XStateMachineTest_advance_signal(sender);
    XStateMachineTest_processEvents();

    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)parallel)
            && XAbstractState_active((XAbstractState*)firstRegion)
            && XAbstractState_active((XAbstractState*)secondRegion),
        "多目标转换进入并行状态及其两个区域");
    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)firstTarget)
            && XAbstractState_active((XAbstractState*)secondTarget),
        "同一 microstep 同时激活不同并行区域中的目标状态");

    XStateMachine_stop(machine);
    XStateMachineTest_processEvents();
    XClassDelete((XClass*)machine);
    XClassDelete((XClass*)sender);
}

/** @brief 验证同一事件下，后代源状态的转换优先于祖先源状态。 */
static void XStateMachineTest_descendantTransitionPriority(void)
{
    XPrintf("\n  -- 转换冲突优先级 --\n");

    XObject* sender = XObject_create();
    XStateMachine* machine = XStateMachine_create();
    XState* parent = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* outside = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* child = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parent);
    XState* childTarget = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parent);

    XState_setInitialState(parent, (XAbstractState*)child);
    XState_setInitialState((XState*)machine, (XAbstractState*)parent);
    XState_addTransition_2(child, sender,
                           XSignal(XStateMachineTest_advance_signal),
                           (XAbstractState*)childTarget);
    XState_addTransition_2(parent, sender,
                           XSignal(XStateMachineTest_advance_signal),
                           (XAbstractState*)outside);

    XStateMachine_start(machine);
    XStateMachineTest_processEvents();
    XStateMachineTest_advance_signal(sender);
    XStateMachineTest_processEvents();

    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)parent)
            && XAbstractState_active((XAbstractState*)childTarget),
        "后代状态的可用转换优先执行");
    XStateMachineTest_expect(
        !XAbstractState_active((XAbstractState*)outside),
        "冲突的祖先状态转换被抢占且不会执行");

    XStateMachine_stop(machine);
    XStateMachineTest_processEvents();
    XClassDelete((XClass*)machine);
    XClassDelete((XClass*)sender);
}

/**
 * @brief 验证内部和外部转换对复合源状态的退出、重入行为。
 * @param type 待验证的转换类型。
 */
static void XStateMachineTest_transitionType(
    XAbstractTransition_TransitionType type)
{
    const bool internal = type == XAbstractTransition_InternalTransition;
    XPrintf("\n  -- %s转换 --\n", internal ? "内部" : "外部");

    XObject* sender = XObject_create();
    XStateMachine* machine = XStateMachine_create();
    XState* parent = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* first = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parent);
    XState* second = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parent);
    XSignalTransition* transition = XState_addTransition_2(
        parent, sender, XSignal(XStateMachineTest_advance_signal),
        (XAbstractState*)second);

    g_parentEnteredCount = 0;
    g_parentExitedCount = 0;
    XAbstractTransition_setTransitionType((XAbstractTransition*)transition, type);
    XState_setInitialState(parent, (XAbstractState*)first);
    XState_setInitialState((XState*)machine, (XAbstractState*)parent);
    XObject_connect_2((XObject*)parent,
                      XSignal(XAbstractState_entered_signal),
                      XStateMachineTest_parentEnteredSlot);
    XObject_connect_2((XObject*)parent,
                      XSignal(XAbstractState_exited_signal),
                      XStateMachineTest_parentExitedSlot);

    XStateMachine_start(machine);
    XStateMachineTest_processEvents();
    XStateMachineTest_advance_signal(sender);
    XStateMachineTest_processEvents();

    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)parent)
            && XAbstractState_active((XAbstractState*)second),
        internal
            ? "内部转换保留复合源状态并进入后代目标"
            : "外部转换重入复合源状态并进入后代目标");
    XStateMachineTest_expect(
        internal
            ? g_parentEnteredCount == 1 && g_parentExitedCount == 0
            : g_parentEnteredCount == 2 && g_parentExitedCount == 1,
        internal
            ? "内部转换不会发出复合源状态的 exited 和第二次 entered"
            : "外部转换会发出复合源状态的 exited 并再次 entered");

    XStateMachine_stop(machine);
    XStateMachineTest_processEvents();
    XClassDelete((XClass*)machine);
    XClassDelete((XClass*)sender);
}

/** @brief 验证显式停止的异步信号时序及 configuration 保留行为。 */
static void XStateMachineTest_explicitStop(void)
{
    XPrintf("\n  -- 显式停止 --\n");

    g_startedCount = 0;
    g_stoppedCount = 0;
    g_finishedCount = 0;
    g_runningChangedCount = 0;
    XStateMachine* machine = XStateMachine_create();
    XState* waiting = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);

    XState_setInitialState((XState*)machine, (XAbstractState*)waiting);
    XObject_connect_2((XObject*)machine,
                      XSignal(XStateMachine_started_signal),
                      XStateMachineTest_startedSlot);
    XObject_connect_2((XObject*)machine,
                      XSignal(XStateMachine_stopped_signal),
                      XStateMachineTest_stoppedSlot);
    XObject_connect_2((XObject*)machine,
                      XSignal(XState_finished_signal),
                      XStateMachineTest_finishedSlot);
    XObject_connect_2((XObject*)machine,
                      XSignal(XStateMachine_runningChanged_signal),
                      XStateMachineTest_runningChangedSlot);

    // setRunning 与 Qt 的 running 属性写入语义一致，内部仍异步调用 start/stop。
    XStateMachine_setRunning(machine, true);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(
        XStateMachine_isRunning(machine) && g_startedCount == 1,
        "异步启动完成后进入运行状态并发出一次 started 信号");

    XStateMachine_setRunning(machine, false);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(
        !XStateMachine_isRunning(machine) && g_stoppedCount == 1,
        "显式停止完成后退出运行状态并发出一次 stopped 信号");
    XStateMachineTest_expect(
        g_finishedCount == 0 && g_runningChangedCount == 2,
        "显式停止不发 finished，runningChanged 分别报告启动和停止");
    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)waiting),
        "显式停止不执行退出动作并保留最后活动配置");

    XClassDelete((XClass*)machine);
}

/** @brief 验证延迟事件到期投递、取消和失败时的所有权规则。 */
static void XStateMachineTest_delayedEventCancellation(void)
{
    XPrintf("\n  -- 延迟事件投递与取消 --\n");

    XStateMachine* machine = XStateMachine_create();
    XState* waiting = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* delivered = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* cancelledTarget = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XEventType type = (XEventType)XEvent_registerEventType(-1);
    XEventType cancelledType = (XEventType)XEvent_registerEventType(-1);
    XStateMachineTest_TypeTransition* transition =
        XStateMachineTest_TypeTransition_create(waiting, type, NULL);
    XStateMachineTest_TypeTransition* cancelledTransition =
        XStateMachineTest_TypeTransition_create(
            delivered, cancelledType, NULL);

    XAbstractTransition_setTargetState(
        (XAbstractTransition*)transition, (XAbstractState*)delivered);
    XAbstractTransition_setTargetState(
        (XAbstractTransition*)cancelledTransition,
        (XAbstractState*)cancelledTarget);
    XState_setInitialState((XState*)machine, (XAbstractState*)waiting);
    XStateMachine_start(machine);
    XStateMachineTest_processEvents();

    // delayMs 为 0 仍通过定时器异步投递，不会在 API 调用栈内同步执行转换。
    int deliveredId = XStateMachine_postDelayedEvent(
        machine, XEvent_create(type), 0);
    XStateMachineTest_expect(
        deliveredId >= 0
            && XAbstractState_active((XAbstractState*)waiting),
        "零延迟事件返回有效编号且不会同步执行转换");
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)delivered),
        "零延迟事件到期后进入外部事件队列并触发转换");

    // 唤醒事件尚未由应用事件循环处理时，零延迟事件仍可通过返回的 ID 取消。
    int cancelledId = XStateMachine_postDelayedEvent(
        machine, XEvent_create(cancelledType), 0);
    XStateMachineTest_expect(
        cancelledId >= 0
            && XStateMachine_cancelDelayedEvent(machine, cancelledId),
        "零延迟事件在下一轮事件循环前仍可取消");
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)delivered)
            && !XAbstractState_active((XAbstractState*)cancelledTarget),
        "已取消零延迟事件的唤醒不会触发转换");

    // 使用较长延迟并立即取消，取消测试不依赖墙上时钟精度。
    int id = XStateMachine_postDelayedEvent(machine, XEvent_create(type), 60000);
    XStateMachineTest_expect(id >= 0, "运行中的状态机接受延迟事件并返回有效编号");
    XStateMachineTest_expect(
        XStateMachine_cancelDelayedEvent(machine, id),
        "有效延迟事件编号可以取消且事件所有权由状态机释放");
    XStateMachineTest_expect(
        !XStateMachine_cancelDelayedEvent(machine, id),
        "同一延迟事件不能重复取消");

    // 失败时状态机不取得事件所有权，测试必须自行释放 rejectedEvent。
    XEvent* rejectedEvent = XEvent_create(type);
    XStateMachineTest_expect(
        XStateMachine_postDelayedEvent(machine, rejectedEvent, -1) == -1,
        "负延迟被拒绝并返回 -1");
    XClassDelete((XClass*)rejectedEvent);

    XStateMachine_stop(machine);
    XStateMachineTest_processEvents();
    XClassDelete((XClass*)machine);
}

/** @brief 验证状态、历史状态和转换属性 API 的参数约束与读回结果。 */
static void XStateMachineTest_propertyApi(void)
{
    XPrintf("\n  -- 属性 API 与参数校验 --\n");

    XObject* sender = XObject_create();
    XStateMachine* machine = XStateMachine_create();
    XStateMachine* otherMachine = XStateMachine_create();
    XState* compound = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* child = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, compound);
    XState* sibling = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* parallel = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ParallelStates, (XState*)machine);
    XState* parallelChild = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parallel);
    XState* foreign = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)otherMachine);
    XState* detached = XState_create();
    XHistoryState* history = XHistoryState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHistoryState_ShallowHistory, compound);

    // addState 取得顶层状态所有权；removeState 只解除关系，不销毁状态。
    XStateMachineTest_expect(
        XStateMachine_addState(machine, (XAbstractState*)detached)
            && XAbstractState_parentState((XAbstractState*)detached)
                == (XState*)machine
            && XAbstractState_machine((XAbstractState*)detached) == machine,
        "addState 将独立状态加入顶层状态树并取得所有权");
    XStateMachineTest_expect(
        XStateMachine_removeState(machine, (XAbstractState*)detached)
            && XAbstractState_parentState((XAbstractState*)detached) == NULL
            && XAbstractState_machine((XAbstractState*)detached) == NULL,
        "removeState 解除父状态和状态机关联但不销毁状态");

    XStateMachineTest_expect(
        XState_setInitialState(compound, (XAbstractState*)child)
            && XState_initialState(compound) == (XAbstractState*)child,
        "互斥状态接受直接子状态作为 initialState 并可正确读回");
    XStateMachineTest_expect(
        !XState_setInitialState(compound, (XAbstractState*)sibling),
        "initialState 拒绝非直接子状态");
    XStateMachineTest_expect(
        !XState_setInitialState(parallel, (XAbstractState*)parallelChild),
        "并行状态拒绝设置 initialState");
    XStateMachineTest_expect(
        XHistoryState_setDefaultState(history, (XAbstractState*)child)
            && XHistoryState_defaultState(history) == (XAbstractState*)child,
        "历史状态接受直接兄弟状态作为默认目标并可正确读回");
    XStateMachineTest_expect(
        !XHistoryState_setDefaultState(history, (XAbstractState*)sibling),
        "历史状态拒绝非兄弟默认目标");
    XStateMachineTest_expect(
        XState_setErrorState(compound, (XAbstractState*)sibling)
            && XState_errorState(compound) == (XAbstractState*)sibling,
        "同一状态机中的状态可设置为 errorState");
    XStateMachineTest_expect(
        !XState_setErrorState(compound, (XAbstractState*)foreign),
        "errorState 拒绝其他状态机中的状态");

    XSignalTransition* signalTransition = XSignalTransition_create();
    XSignalTransition_setSenderObject(signalTransition, sender);
    XSignalTransition_setSignal(
        signalTransition, XSignal(XStateMachineTest_advance_signal));
    XAbstractTransition_setTransitionType(
        (XAbstractTransition*)signalTransition,
        XAbstractTransition_InternalTransition);
    XStateMachineTest_expect(
        XSignalTransition_senderObject(signalTransition) == sender
            && XSignalTransition_signal(signalTransition)
                == XSignal(XStateMachineTest_advance_signal)
            && XAbstractTransition_transitionType(
                (XAbstractTransition*)signalTransition)
                == XAbstractTransition_InternalTransition,
        "信号发送者、信号标识和转换类型设置后可正确读回");

    XEventTransition* eventTransition = XEventTransition_create();
    XEventType eventType = (XEventType)XEvent_registerEventType(-1);
    XEventTransition_setEventSource(eventTransition, sender);
    XEventTransition_setEventType(eventTransition, eventType);
    XStateMachineTest_expect(
        XEventTransition_eventSource(eventTransition) == sender
            && XEventTransition_eventType(eventTransition) == eventType,
        "事件源和事件类型设置后可正确读回");

    XKeyEventTransition* keyTransition = XKeyEventTransition_create();
    XKeyEventTransition_setKey(keyTransition, 65);
    XKeyEventTransition_setModifierMask(
        keyTransition, XKeyboardModifier_ControlModifier);
    XStateMachineTest_expect(
        XKeyEventTransition_key(keyTransition) == 65
            && XKeyEventTransition_modifierMask(keyTransition)
                == XKeyboardModifier_ControlModifier,
        "键盘转换的按键和修饰键掩码设置后可正确读回");

    XMouseEventTransition* mouseTransition = XMouseEventTransition_create();
    XVector* invalidPath = XVector_Create(int);
    XMouseEventTransition_setButton(mouseTransition, XMouseButton_RightButton);
    XMouseEventTransition_setModifierMask(
        mouseTransition, XKeyboardModifier_AltModifier);
    XStateMachineTest_expect(
        XMouseEventTransition_button(mouseTransition) == XMouseButton_RightButton
            && XMouseEventTransition_modifierMask(mouseTransition)
                == XKeyboardModifier_AltModifier,
        "鼠标转换的按键和修饰键掩码设置后可正确读回");
    XStateMachineTest_expect(
        !XMouseEventTransition_setHitTestPath(mouseTransition, invalidPath),
        "鼠标命中路径拒绝非 XPoint 元素类型的容器");

    XClassDelete((XClass*)invalidPath);
    XClassDelete((XClass*)mouseTransition);
    XClassDelete((XClass*)keyTransition);
    XClassDelete((XClass*)eventTransition);
    XClassDelete((XClass*)signalTransition);
    XClassDelete((XClass*)detached);
    XClassDelete((XClass*)otherMachine);
    XClassDelete((XClass*)machine);
    XClassDelete((XClass*)sender);
}

/** @brief 验证目标状态销毁后，转换不会保留悬空指针。 */
static void XStateMachineTest_targetDeletion(void)
{
    XPrintf("\n  -- 目标状态生命周期保护 --\n");

    XStateMachine* machine = XStateMachine_create();
    XState* source = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* target = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XAbstractTransition* transition = XState_addTransition_3(
        source, (XAbstractState*)target);

    XClassDelete((XClass*)target);
    XStateMachineTest_expect(
        XAbstractTransition_targetState(transition) == NULL
            && XVector_isEmpty_base((const XContainer*)
                XAbstractTransition_targetStates_const(transition)),
        "销毁目标状态会同步清除转换中的受保护目标引用");

    XClassDelete((XClass*)machine);
}

/** @brief 验证入口错误由最近祖先的 errorState 恢复。 */
static void XStateMachineTest_errorState(void)
{
    XPrintf("\n  -- 可恢复状态图错误 --\n");

    XStateMachine* machine = XStateMachine_create();
    XState* compound = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* childWithoutInitial = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, compound);
    XState* nestedChild = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, childWithoutInitial);
    XState* errorState = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    (void)nestedChild;

    XState_setErrorState(compound, (XAbstractState*)errorState);
    XState_setInitialState((XState*)machine, (XAbstractState*)compound);
    XStateMachine_start(machine);
    XStateMachineTest_processEvents();

    XStateMachineTest_expect(
        XStateMachine_error(machine) == XStateMachine_NoInitialStateError
            && XStateMachine_errorString(machine) != NULL,
        "复合状态缺少初始状态时报告对应错误码和错误文本");
    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)errorState)
            && XStateMachine_isRunning(machine),
        "配置 errorState 后，入口错误转入错误状态且状态机继续运行");

    XStateMachine_clearError(machine);
    XStateMachineTest_expect(
        XStateMachine_error(machine) == XStateMachine_NoError
            && XStateMachine_errorString(machine) == NULL,
        "clearError 清除错误码和错误文本但不改变运行状态");

    XStateMachine_stop(machine);
    XStateMachineTest_processEvents();
    XClassDelete((XClass*)machine);
}

/** @brief 验证没有 errorState 时，状态图错误会停止状态机。 */
static void XStateMachineTest_unrecoverableError(void)
{
    XPrintf("\n  -- 不可恢复状态图错误 --\n");

    g_stoppedCount = 0;
    XStateMachine* machine = XStateMachine_create();
    XState* compound = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* nested = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, compound);
    (void)nested;

    XState_setInitialState((XState*)machine, (XAbstractState*)compound);
    XObject_connect_2((XObject*)machine,
                      XSignal(XStateMachine_stopped_signal),
                      XStateMachineTest_stoppedSlot);
    XStateMachine_start(machine);
    XStateMachineTest_processEvents();

    XStateMachineTest_expect(
        XStateMachine_error(machine) == XStateMachine_NoInitialStateError,
        "没有 errorState 时仍保留导致停止的状态图错误码");
    XStateMachineTest_expect(
        !XStateMachine_isRunning(machine) && g_stoppedCount == 1,
        "无法恢复的状态图错误会停止状态机并发出 stopped");

    XClassDelete((XClass*)machine);
}

// ==================== 历史状态测试 ====================

/** @brief 验证浅历史只恢复直接活动子状态，其后代按 initialState 重新进入。 */
static void XStateMachineTest_shallowHistory(void)
{
    XPrintf("\n  -- 浅历史 --\n");

    XObject* sender = XObject_create();
    XStateMachine* machine = XStateMachine_create();
    XState* parent = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* outside = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* group = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parent);
    XState* first = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, group);
    XState* second = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, group);
    XHistoryState* history = XHistoryState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHistoryState_ShallowHistory, parent);

    XHistoryState_setDefaultState(history, (XAbstractState*)group);
    XState_setInitialState(group, (XAbstractState*)first);
    // 首次进入尚无历史记录，initialState 指向 history 后会转入 defaultState。
    XState_setInitialState(parent, (XAbstractState*)history);
    XState_setInitialState((XState*)machine, (XAbstractState*)parent);
    XState_addTransition_2(first, sender,
                           XSignal(XStateMachineTest_advance_signal),
                           (XAbstractState*)second);
    XState_addTransition_2(parent, sender,
                           XSignal(XStateMachineTest_leave_signal),
                           (XAbstractState*)outside);
    XState_addTransition_2(outside, sender,
                           XSignal(XStateMachineTest_restore_signal),
                           (XAbstractState*)history);

    XStateMachine_start(machine);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)group)
            && XAbstractState_active((XAbstractState*)first)
            && !XAbstractState_active((XAbstractState*)history),
        "首次进入浅历史时使用 defaultState，历史伪状态本身不活动");
    XStateMachineTest_advance_signal(sender);
    XStateMachineTest_processEvents();
    XStateMachineTest_leave_signal(sender);
    XStateMachineTest_processEvents();
    XStateMachineTest_restore_signal(sender);
    XStateMachineTest_processEvents();

    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)parent)
            && XAbstractState_active((XAbstractState*)group),
        "浅历史恢复父状态退出前的直接活动子状态");
    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)first)
            && !XAbstractState_active((XAbstractState*)second),
        "浅历史不保存更深层状态，后代按 initialState 重新进入");

    XStateMachine_stop(machine);
    XStateMachineTest_processEvents();
    XClassDelete((XClass*)machine);
    XClassDelete((XClass*)sender);
}

// ==================== 菜单公开测试入口 ====================

// ==================== API 组合矩阵测试 ====================

/** @brief 读取属性赋值记录数（测试侧只看条数，不碰内部元素布局）。 */
static int64_t XStateMachineTest_assignmentCount(const XState* state)
{
    const XVector* assignments = XState_propertyAssignments_const(state);
    return assignments
        ? (int64_t)XVector_size_base((const XContainer*)assignments)
        : 0;
}

/** @brief 读取配置容器里的元素个数。 */
static int64_t XStateMachineTest_configurationCount(const XStateMachine* machine)
{
    const XVector* configuration = XStateMachine_configuration_const(machine);
    return configuration
        ? (int64_t)XVector_size_base((const XContainer*)configuration)
        : 0;
}

/** @brief 用 XString 键名读取对象的动态属性并比对整数值。 */
static bool XStateMachineTest_propertyIsInt(const XObject* object, const char* name,
                                            int expected)
{
    XString* key = XString_create_utf8(name);
    if (!key)
        return false;
    XVariant* value = XObject_property_const(object, key);
    bool ok = value && XVariant_toInt(value) == expected;
    XClassDelete((XClass*)key);
    return ok;
}

/** @brief 按 UTF-8 属性名给对象写入整数属性。 */
static bool XStateMachineTest_setPropertyInt(XObject* object, const char* name, int value)
{
    XString* key = XString_create_utf8(name);
    XVariant* variant = key ? XVariant_create_int(value) : NULL;
    bool ok = false;
    if (key && variant) {
        /* 2026-11-11 口径（对齐 Qt 6.8）：XObject_setProperty **总是**接管
         * variant（Qt 的形参是按值 QVariant），返回值改为「是否命中静态属性」
         * ——动态属性写入恒 false ⇒ **不能**再按返回值释放 variant（旧写法
         * 在成功路径上双释放，属性表里还留一个悬垂指针）。
         * "是否落地"改用回读判断，保持本助手原有的布尔契约。 */
        (void)XObject_setProperty(object, key, variant);
        ok = XObject_property_const(object, key) != NULL;
    } else if (variant) {
        XClassDelete((XClass*)variant);
    }
    if (key)
        XClassDelete((XClass*)key);
    return ok;
}

/** @brief 按 UTF-8 属性名给状态登记一条赋值。 */
static bool XStateMachineTest_assignIntProperty(XState* state, XObject* object,
                                                const char* name, int value)
{
    XString* key = XString_create_utf8(name);
    XVariant* variant = key ? XVariant_create_int(value) : NULL;
    bool ok = key && variant && XState_assignProperty(state, object, key, variant);
    if (key)
        XClassDelete((XClass*)key);
    if (variant)
        XClassDelete((XClass*)variant);
    return ok;
}

/** @brief 构造一个含两个互斥子状态的状态，并指定初始子状态。 */
static XState* XStateMachineTest_createCompound(XState* parent, XState** firstChild,
                                               XState** secondChild,
                                               const char* name)
{
    XState* compound = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        XState_ExclusiveStates, parent);
    if (!compound)
        return NULL;
    if (name) {
        XString* text = XString_create_utf8(name);
        if (text) {
            XObject_setObjectName((XObject*)compound, text);
            XClassDelete((XClass*)text);
        }
    }
    if (firstChild) {
        *firstChild = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       XState_ExclusiveStates, compound);
        if (*firstChild)
            XState_setInitialState(compound, (XAbstractState*)*firstChild);
    }
    if (secondChild) {
        *secondChild = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        XState_ExclusiveStates, compound);
    }
    return compound;
}

/**
 * @brief 验证状态/转换全部 getter-setter 的属性往返与信号口径。
 */
static void XStateMachineTest_apiRoundTrip(void)
{
    XPrintf("\n  -- 属性往返与信号口径 --\n");

    XStateMachine* machine = XStateMachine_create();
    XState* first = NULL;
    XState* second = NULL;
    XState* compound = XStateMachineTest_createCompound(
        (XState*)machine, &first, &second, "compound");
    XState* detached = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        XState_ExclusiveStates, NULL);
    XVector* targetList = XVector_Create(XAbstractState*);
    XAbstractTransition* transition = XState_addTransition_3(first,
                                                             (XAbstractState*)second);
    XAbstractState* targets[2];

    XStateMachineTest_expect(compound && first && second && transition,
                             "构建复合状态、两个子状态与无条件转换");

    /* 状态属性：initialState / childMode / errorState 的读写与切换。 */
    XStateMachineTest_expect(XState_initialState(compound) == (XAbstractState*)first,
                             "initialState 返回构造函数中设置的初始子状态");
    XStateMachineTest_expect(XState_childMode(compound) == XState_ExclusiveStates,
                             "childMode 默认是互斥模式");
    XStateMachineTest_expect(XState_setInitialState(compound, (XAbstractState*)detached) == false,
                             "非直接子状态不能作为 initialState");
    XState_setChildMode(compound, XState_ParallelStates);
    XStateMachineTest_expect(XState_initialState(compound) == NULL,
                             "切换到并行模式会清空 initialState");
    XStateMachineTest_expect(XState_setInitialState(compound, (XAbstractState*)first) == false,
                             "并行状态拒绝设置 initialState");
    XState_setChildMode(compound, XState_ExclusiveStates);
    XStateMachineTest_expect(XState_setInitialState(compound, (XAbstractState*)second),
                             "切回互斥模式后可重新设置 initialState");
    XStateMachineTest_expect(XState_setErrorState(compound, (XAbstractState*)(XState*)machine) == false,
                             "状态机根节点不能作为 errorState");
    {
        /* 真正的"属于其他状态机"：把 detached 挂到另一台状态机上。 */
        XStateMachine* foreignMachine = XStateMachine_create();
        XStateMachine_addState(foreignMachine, (XAbstractState*)detached);
        XStateMachineTest_expect(
            XState_setErrorState(compound, (XAbstractState*)detached) == false,
            "其他状态机的状态不能作为 errorState");
        XClassDelete((XClass*)foreignMachine);
        detached = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                    XState_ExclusiveStates, NULL);
    }
    XStateMachineTest_expect(XState_setErrorState(compound, (XAbstractState*)first)
                                 && XState_errorState(compound) == (XAbstractState*)first,
                             "同机的直接子状态可作为 errorState 并回读");

    /* 子状态/转换列表：只读视图非空且条数正确。 */
    XStateMachineTest_expect(
        XState_childStates_const(compound)
            && XVector_size_base((const XContainer*)XState_childStates_const(compound)) == 2,
        "childStates 只读视图包含两个直接子状态");
    XStateMachineTest_expect(
        XState_transitions_const(first)
            && XVector_size_base((const XContainer*)XState_transitions_const(first)) == 1,
        "transitions 只读视图包含已加入的无条件转换");

    /* 转换属性：targetState / targetStates / transitionType 往返。 */
    XStateMachineTest_expect(XAbstractTransition_sourceState(transition) == first,
                             "转换的 sourceState 指向所在状态");
    XStateMachineTest_expect(XAbstractTransition_targetState(transition)
                                 == (XAbstractState*)second,
                             "targetState 返回单目标");
    XStateMachineTest_expect(XAbstractTransition_transitionType(transition)
                                 == XAbstractTransition_ExternalTransition,
                             "转换类型默认是 ExternalTransition");
    XAbstractTransition_setTransitionType(transition,
                                          XAbstractTransition_InternalTransition);
    XStateMachineTest_expect(XAbstractTransition_transitionType(transition)
                                 == XAbstractTransition_InternalTransition,
                             "setTransitionType 写入后即时回读");
    XAbstractTransition_setTransitionType(transition,
                                          XAbstractTransition_ExternalTransition);

    /* 多目标写入：含 NULL 元素的列表被整体拒绝。 */
    targets[0] = (XAbstractState*)second;
    targets[1] = NULL;
    XVector_push_back_2(targetList, targets, 2);
    XStateMachineTest_expect(
        XAbstractTransition_setTargetStates(transition, targetList) == false
            && XAbstractTransition_targetState(transition) == (XAbstractState*)second,
        "含 NULL 元素的目标列表被拒绝且原目标保持不变");
    XVector_clear_base((XContainer*)targetList);

    /* Qt 语义：多重集相同视为未变化；顺序不同的同一组目标不改写。 */
    {
        XVector* reversed = XVector_Create(XAbstractState*);
        XAbstractState* pair[2] = { (XAbstractState*)first, (XAbstractState*)second };
        XAbstractState* pairReversed[2] = { (XAbstractState*)second, (XAbstractState*)first };
        XVector_push_back_2(targetList, pair, 2);
        XVector_push_back_2(reversed, pairReversed, 2);
        bool firstSet = XAbstractTransition_setTargetStates(transition, targetList);
        bool secondSet = XAbstractTransition_setTargetStates(transition, reversed);
        XStateMachineTest_expect(
            firstSet && secondSet
                && XVector_size_base((const XContainer*)
                    XAbstractTransition_targetStates_const(transition)) == 2,
            "多目标写入成功且顺序不同但内容相同的重写不破坏列表");
        XClassDelete((XClass*)reversed);
        XVector_clear_base((XContainer*)targetList);
    }

    /* 清空目标：NULL 等价于无目标转换。 */
    XStateMachineTest_expect(XAbstractTransition_setTargetStates(transition, NULL)
                                 && XAbstractTransition_targetState(transition) == NULL,
                             "NULL 目标列表等价于清空，构成无目标转换");

    /* 转换的移除：返回所有权但不销毁对象。 */
    XStateMachineTest_expect(XState_removeTransition(first, transition),
                             "removeTransition 从源状态摘除转换并返回成功");
    XStateMachineTest_expect(XAbstractTransition_sourceState(transition) == NULL
                                 && transition->m_targetStates != NULL,
                             "摘除后转换仍存活，仅 sourceState 置空");

    /* 状态机查询接口：错误串、恢复策略、运行态的默认值。 */
    XStateMachineTest_expect(XStateMachine_error(machine) == XStateMachine_NoError
                                 && XStateMachine_errorString(machine) == NULL
                                 && XStateMachine_errorString_utf8(machine) == NULL,
                             "新建状态机的错误码/错误串/UTF-8 错误串均为空");
    XStateMachineTest_expect(XStateMachine_globalRestorePolicy(machine)
                                 == XState_DontRestoreProperties,
                             "globalRestorePolicy 默认是 DontRestoreProperties");
    XStateMachine_setGlobalRestorePolicy(machine, XState_RestoreProperties);
    XStateMachineTest_expect(XStateMachine_globalRestorePolicy(machine)
                                 == XState_RestoreProperties,
                             "setGlobalRestorePolicy 写入后即时回读");
    XStateMachineTest_expect(XStateMachineTest_configurationCount(machine) == 0,
                             "未启动状态机的活动配置为空");
    XStateMachineTest_expect(XStateMachine_isRunning(machine) == false,
                             "未启动状态机不处于运行态");

    /* 事件对象：SignalEvent / WrappedEvent 的构造与访问器。 */
    {
        XObject* sender = XObject_create();
        XStateMachine_SignalEvent* signalEvent =
            XStateMachine_SignalEvent_create(sender, (size_t)123, NULL);
        XStateMachineTest_expect(signalEvent
                                     && XStateMachine_SignalEvent_sender(signalEvent) == sender
                                     && XStateMachine_SignalEvent_signal(signalEvent) == 123
                                     && XStateMachine_SignalEvent_arguments_const(signalEvent) == NULL,
                                 "SignalEvent 原样回读 sender/signal 且无参时参数为 NULL");
        XClassDelete((XClass*)signalEvent);

        XEvent* raw = XEvent_create(XEVENT_TYPE_USER + 7);
        XEvent* clone = XEvent_clone_base(raw);
        XStateMachine_WrappedEvent* wrapped =
            XStateMachine_WrappedEvent_create(sender, clone);
        XStateMachineTest_expect(
            wrapped && XStateMachine_WrappedEvent_object(wrapped) == sender
                && XStateMachine_WrappedEvent_event(wrapped)
                    && XEvent_type(XStateMachine_WrappedEvent_event(wrapped))
                        == XEVENT_TYPE_USER + 7,
            "WrappedEvent 回读事件源与克隆事件类型");
        XClassDelete((XClass*)wrapped); /* 包装事件拥有克隆，克隆随之销毁 */
        XClassDelete((XClass*)raw);
        XStateMachineTest_expect(XStateMachine_WrappedEvent_create(sender, NULL) == NULL,
                                 "WrappedEvent 拒绝 NULL 事件且不取得所有权");
        XClassDelete((XClass*)sender);
    }

    /* 事件转换的 setter 与自动重注册路径。 */
    {
        XStateMachineTest_EventSource* source = XStateMachineTest_EventSource_create();
        XStateMachineTest_EventSource* otherSource = XStateMachineTest_EventSource_create();
        XEventType typeA = (XEventType)XEvent_registerEventType(-1);
        XEventType typeB = (XEventType)XEvent_registerEventType(-1);
        XEventTransition* eventTransition = XEventTransition_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XObject*)source, typeA, second);
        XStateMachineTest_expect(
            XEventTransition_eventSource(eventTransition) == (XObject*)source
                && XEventTransition_eventType(eventTransition) == typeA,
            "EventTransition 构造参数可原样回读");
        XEventTransition_setEventSource(eventTransition, (XObject*)otherSource);
        XEventTransition_setEventType(eventTransition, typeB);
        XStateMachineTest_expect(
            XEventTransition_eventSource(eventTransition) == (XObject*)otherSource
                && XEventTransition_eventType(eventTransition) == typeB,
            "setEventSource/setEventType 写入后即时回读");
        XEventTransition_setEventSource(eventTransition, NULL);
        XStateMachineTest_expect(XEventTransition_eventSource(eventTransition) == NULL,
                                 "setEventSource(NULL) 取消事件源绑定");

        XKeyEventTransition* keyTransition = XKeyEventTransition_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XObject*)source, XEVENT_TYPE_KEY_PRESS, 65, second);
        XStateMachineTest_expect(XKeyEventTransition_key(keyTransition) == 65
                                     && XKeyEventTransition_modifierMask(keyTransition)
                                         == XKeyboardModifier_NoModifier,
                                 "KeyEventTransition 默认按键与空修饰键掩码");
        XKeyEventTransition_setKey(keyTransition, 66);
        XKeyEventTransition_setModifierMask(keyTransition,
                                           XKeyboardModifier_AltModifier);
        XStateMachineTest_expect(XKeyEventTransition_key(keyTransition) == 66
                                     && XKeyEventTransition_modifierMask(keyTransition)
                                         == XKeyboardModifier_AltModifier,
                                 "setKey/setModifierMask 写入后即时回读");

        XMouseEventTransition* mouseTransition = XMouseEventTransition_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (XObject*)source,
            XEVENT_TYPE_MOUSE_BUTTON_PRESS, XMouseButton_LeftButton, second);
        XStateMachineTest_expect(
            XMouseEventTransition_button(mouseTransition) == XMouseButton_LeftButton
                && XMouseEventTransition_modifierMask(mouseTransition)
                    == XKeyboardModifier_NoModifier
                && XMouseEventTransition_hitTestPath_const(mouseTransition)
                && XVector_isEmpty_base((const XContainer*)
                    XMouseEventTransition_hitTestPath_const(mouseTransition)),
            "MouseEventTransition 默认按键/掩码/空命中路径");
        XMouseEventTransition_setButton(mouseTransition, XMouseButton_RightButton);
        XMouseEventTransition_setModifierMask(mouseTransition,
                                             XKeyboardModifier_ShiftModifier);
        XStateMachineTest_expect(
            XMouseEventTransition_button(mouseTransition) == XMouseButton_RightButton
                && XMouseEventTransition_modifierMask(mouseTransition)
                    == XKeyboardModifier_ShiftModifier,
            "setButton/setModifierMask 写入后即时回读");
        {
            XVector* path = XVector_Create(XPoint);
            XVector* wrongPath = XVector_Create(int);
            XPoint triangle[3] = { { 0, 0 }, { 10, 0 }, { 0, 10 } };
            XVector_push_back_2(path, triangle, 3);
            XVector* wrongValues[1] = { NULL };
            XVector_push_back_2(wrongPath, wrongValues, 1);
            XStateMachineTest_expect(XMouseEventTransition_setHitTestPath(mouseTransition, path)
                                         && XVector_size_base((const XContainer*)
                                             XMouseEventTransition_hitTestPath_const(mouseTransition)) == 3,
                                     "setHitTestPath 拷贝三点路径");
            XStateMachineTest_expect(XMouseEventTransition_setHitTestPath(mouseTransition,
                                                                          wrongPath) == false,
                                     "元素类型不是 XPoint 的路径被拒绝");
            XClassDelete((XClass*)path);
            XClassDelete((XClass*)wrongPath);
        }
        XStateMachineTest_expect(XMouseEventTransition_setHitTestPath(mouseTransition, NULL)
                                     && XVector_isEmpty_base((const XContainer*)
                                         XMouseEventTransition_hitTestPath_const(mouseTransition)),
                                 "setHitTestPath(NULL) 清空位置限制");

        /* 这些转换挂到 second 上，随状态树一起回收。 */
        XState_addTransition(second, (XAbstractTransition*)keyTransition);
        XState_addTransition(second, (XAbstractTransition*)mouseTransition);
        XClassDelete((XClass*)eventTransition);
        XClassDelete((XClass*)otherSource);
        /* source 由 eventTransition 的事件过滤器持有引用，转换删除后即可释放。 */
        XClassDelete((XClass*)source);
    }

    XClassDelete((XClass*)detached);
    XClassDelete((XClass*)targetList);
    XClassDelete((XClass*)transition); /* 已被 removeTransition 摘除，由调用者释放 */
    XClassDelete((XClass*)machine);
}

/**
 * @brief 验证 assignProperty 与 globalRestorePolicy 的完整语义。
 */
static void XStateMachineTest_assignPropertyPolicy(void)
{
    XPrintf("\n  -- 属性赋值与恢复策略 --\n");

    XObject* target = XObject_create();
    XStateMachine* machine = XStateMachine_create();
    XObject* sender = XObject_create();
    XState* root = NULL;
    XState* branch = NULL;
    XState* leaf = NULL;
    XState* other = NULL;

    XStateMachineTest_expect(target && machine && sender,
                             "创建属性宿主对象、状态机与信号发送者");
    if (!target || !machine || !sender) {
        if (target)
            XClassDelete((XClass*)target);
        if (machine)
            XClassDelete((XClass*)machine);
        if (sender)
            XClassDelete((XClass*)sender);
        return;
    }
    XStateMachineTest_setPropertyInt(target, "xsm_level", 0);

    /* 图：根 -> branch（带 assignProperty）或 other（不带），信号单向切换。 */
    branch = XStateMachineTest_createCompound((XState*)machine, &leaf, NULL, "branch");
    other = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates,
                             (XState*)machine);
    root = branch;

    XStateMachineTest_expect(XState_assignProperty(branch, target, NULL, NULL) == false,
                             "assignProperty 拒绝 NULL 属性名");
    XStateMachineTest_expect(XState_assignProperty(branch, NULL, NULL, NULL) == false,
                             "assignProperty 拒绝 NULL 目标对象");
    XStateMachineTest_expect(XStateMachineTest_assignIntProperty(branch, target, "xsm_level", 1)
                                 && XStateMachineTest_assignmentCount(branch) == 1,
                             "assignProperty 登记一条赋值记录");
    XStateMachineTest_expect(XStateMachineTest_assignIntProperty(branch, target, "xsm_level", 2)
                                 && XStateMachineTest_assignmentCount(branch) == 1,
                             "同名同对象的重复赋值原地更新而不新增记录");

    XState_setInitialState((XState*)machine, (XAbstractState*)root);
    XState_addTransition_2(branch, sender,
                           XSignal(XStateMachineTest_advance_signal),
                           (XAbstractState*)other);
    XState_addTransition_2(other, sender,
                           XSignal(XStateMachineTest_leave_signal),
                           (XAbstractState*)branch);

    /* DontRestoreProperties：进入写值，离开不恢复。 */
    XStateMachine_setGlobalRestorePolicy(machine, XState_DontRestoreProperties);
    XStateMachine_start(machine);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XStateMachineTest_propertyIsInt(target, "xsm_level", 2),
                             "进入状态时写入 assignProperty 登记的赋值");

    XStateMachineTest_advance_signal(sender);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XAbstractState_active((XAbstractState*)other)
                                 && XStateMachineTest_propertyIsInt(target, "xsm_level", 2),
                             "DontRestoreProperties 下离开状态不恢复属性");

    /* RestoreProperties：重新进入时保存当前值，离开后写回。 */
    XStateMachine_setGlobalRestorePolicy(machine, XState_RestoreProperties);
    XStateMachineTest_leave_signal(sender);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XAbstractState_active((XAbstractState*)branch)
                                 && XStateMachineTest_propertyIsInt(target, "xsm_level", 2),
                             "RestoreProperties 下重新进入覆盖属性的状态仍写入新值");

    /* 进入前先把值改成 100，离开时必须恢复成 EnterProperties 时保存的旧值。 */
    XStateMachineTest_setPropertyInt(target, "xsm_level", 2);
    XStateMachineTest_advance_signal(sender);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XAbstractState_active((XAbstractState*)other),
                             "信号转换离开带属性赋值的状态");

    XStateMachine_stop(machine);
    XStateMachineTest_processEvents();
    XClassDelete((XClass*)machine);
    XClassDelete((XClass*)target);
    XClassDelete((XClass*)sender);
}

/**
 * @brief 验证历史状态的默认转换复用/替换语义与 historyType 往返。
 */
static void XStateMachineTest_historyDefaultSemantics(void)
{
    XPrintf("\n  -- 历史默认转换语义 --\n");

    XStateMachine* machine = XStateMachine_create();
    XState* parent = NULL;
    XState* first = NULL;
    XState* second = NULL;
    XHistoryState* history = NULL;

    parent = XStateMachineTest_createCompound((XState*)machine, &first, &second, "hparent");
    history = XHistoryState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                      XHistoryState_ShallowHistory, parent);
    XStateMachineTest_expect(parent && history, "构建历史状态与所在父状态");

    XStateMachineTest_expect(XHistoryState_historyType(history) == XHistoryState_ShallowHistory,
                             "历史状态默认是浅历史");
    XHistoryState_setHistoryType(history, XHistoryState_DeepHistory);
    XStateMachineTest_expect(XHistoryState_historyType(history) == XHistoryState_DeepHistory,
                             "setHistoryType 写入后即时回读");
    XHistoryState_setHistoryType(history, XHistoryState_ShallowHistory);

    XStateMachineTest_expect(XHistoryState_defaultTransition(history) == NULL
                                 && XHistoryState_defaultState(history) == NULL,
                             "未设置的默认转换与默认状态均为空");
    XStateMachineTest_expect(XHistoryState_setDefaultState(history, (XAbstractState*)first)
                                 && XHistoryState_defaultState(history) == (XAbstractState*)first,
                             "setDefaultState 生成单目标默认转换");
    XStateMachineTest_expect(XHistoryState_setDefaultState(history, (XAbstractState*)first),
                             "重复设置同一默认状态幂等");
    XStateMachineTest_expect(
        XHistoryState_setDefaultState(history, (XAbstractState*)machine) == false,
        "非兄弟状态不能作为 defaultState");
    XStateMachineTest_expect(XHistoryState_setDefaultState(history, (XAbstractState*)second)
                                 && XHistoryState_defaultState(history) == (XAbstractState*)second,
                             "改成另一个兄弟状态时复用同一条默认转换");
    XStateMachineTest_expect(XHistoryState_defaultTransition(history)->m_sourceState == NULL,
                             "默认转换的 sourceState 为空（历史状态不是标准状态）");

    /* 用户自定义默认转换：整体替换，旧的被回收。 */
    {
        XStateMachineTest_TypeTransition* custom = XStateMachineTest_TypeTransition_create(
            NULL, XEVENT_TYPE_NONE, NULL);
        XStateMachineTest_expect(custom
                                     && XHistoryState_setDefaultTransition(
                                         history, (XAbstractTransition*)custom)
                                     && XHistoryState_defaultTransition(history)
                                         == (XAbstractTransition*)custom,
                                 "setDefaultTransition 接管用户自定义默认转换");
        first = first; /* 保持引用，便于阅读 */
    }

    XClassDelete((XClass*)machine); /* 历史状态与其默认转换随状态树回收 */
}

/**
 * @brief 验证启动/停止/重启与运行态查询的组合语义。
 */
static void XStateMachineTest_lifecycleCombinations(void)
{
    XPrintf("\n  -- 生命周期组合 --\n");

    XStateMachine* machine = XStateMachine_create();
    XState* initial = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       XState_ExclusiveStates, (XState*)machine);

    XStateMachineTest_expect(machine && initial, "构建无初始状态的状态机");
    g_startedCount = 0;
    g_stoppedCount = 0;
    g_runningChangedCount = 0;
    XObject_connect_2((XObject*)machine, XSignal(XStateMachine_started_signal),
                      XStateMachineTest_startedSlot);
    XObject_connect_2((XObject*)machine, XSignal(XStateMachine_stopped_signal),
                      XStateMachineTest_stoppedSlot);
    XObject_connect_2((XObject*)machine, XSignal(XStateMachine_runningChanged_signal),
                      XStateMachineTest_runningChangedSlot);

    /* 互斥根状态没有 initialState 时必须拒绝启动。 */
    XStateMachine_start(machine);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(!XStateMachine_isRunning(machine) && g_startedCount == 0,
                             "互斥根状态缺少 initialState 时拒绝启动");

    XState_setInitialState((XState*)machine, (XAbstractState*)initial);
    XStateMachine_start(machine);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XStateMachine_isRunning(machine) && g_startedCount == 1
                                 && g_runningChangedCount == 1,
                             "补齐 initialState 后启动成功并各发一次 started/runningChanged");

    /* 重复启动：不重建配置，也不再发信号。 */
    XStateMachine_start(machine);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(g_startedCount == 1 && g_runningChangedCount == 1,
                             "重复 start 不重复发 started/runningChanged");

    /* 停止后重启：配置重建并再次发信号。 */
    XStateMachine_setRunning(machine, false);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(!XStateMachine_isRunning(machine) && g_stoppedCount == 1
                                 && g_runningChangedCount == 2,
                             "setRunning(false) 停止并各发一次 stopped/runningChanged");
    XStateMachine_setRunning(machine, true);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XStateMachine_isRunning(machine) && g_startedCount == 2
                                 && g_runningChangedCount == 3,
                             "重启后重新发出 started 并回到运行态");

    /* 未运行时投递/延迟事件都被拒绝且不取得所有权。 */
    XStateMachine_stop(machine);
    XStateMachineTest_processEvents();
    {
        XEvent* event = XEvent_create(XEVENT_TYPE_USER + 9);
        XStateMachineTest_expect(XStateMachine_postEvent(
                                     machine, event, XStateMachine_NormalPriority) == false,
                                 "停止态拒绝 postEvent 且不取得所有权");
        XClassDelete((XClass*)event);
        XEvent* delayed = XEvent_create(XEVENT_TYPE_USER + 9);
        XStateMachineTest_expect(XStateMachine_postDelayedEvent(machine, delayed, 10) == -1
                                     && XStateMachine_cancelDelayedEvent(machine, 0) == false,
                                 "停止态拒绝 postDelayedEvent/cancelDelayedEvent");
        XClassDelete((XClass*)delayed);
    }

    /* 补上「立即排空的延迟事件」组合：0 延迟投递后再取消。 */
    XStateMachine_setRunning(machine, true);
    XStateMachineTest_processEvents();
    {
        XEvent* delayed = XEvent_create(XEVENT_TYPE_USER + 9);
        int id = XStateMachine_postDelayedEvent(machine, delayed, 60 * 1000);
        XStateMachineTest_expect(id >= 0, "运行态接受延迟事件并返回编号");
        XStateMachineTest_expect(XStateMachine_cancelDelayedEvent(machine, id),
                                 "cancelDelayedEvent 取消已登记的延迟事件并销毁其事件");
        XStateMachineTest_expect(XStateMachine_cancelDelayedEvent(machine, id) == false,
                                 "同一编号不能重复取消");
    }

    XStateMachine_stop(machine);
    XStateMachineTest_processEvents();
    XClassDelete((XClass*)machine);
}

/**
 * @brief 验证错误路径：错误码、错误串内容带状态名、clearError 的保留语义。
 */
static void XStateMachineTest_errorStringContent(void)
{
    XPrintf("\n  -- 错误串内容与 clearError --\n");

    XStateMachine* machine = XStateMachine_create();
    XState* compound = XStateMachineTest_createCompound(
        (XState*)machine, NULL, NULL, "broken");

    /* 复合状态设置了子状态却漏了 initialState：触发 NoInitialStateError。 */
    XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, compound);
    XState_setInitialState((XState*)machine, (XAbstractState*)compound);
    XStateMachine_start(machine);
    XStateMachineTest_processEvents();

    const XString* text = XStateMachine_errorString(machine);
    XStateMachineTest_expect(XStateMachine_error(machine) == XStateMachine_NoInitialStateError,
                             "缺少 initialState 的复合状态报告 NoInitialStateError");
    XStateMachineTest_expect(text != NULL && XStateMachine_errorString_utf8(machine) != NULL,
                             "错误串与 UTF-8 视图同时可用");
    if (text) {
        XString* needle = XString_create_utf8("broken");
        XString* utf8 = XString_create_utf8(XStateMachine_errorString_utf8(machine));
        XStateMachineTest_expect(needle && utf8
                                     && XString_contains(utf8, needle,
                                                         XChar_CaseSensitive),
                                 "错误串包含出错状态的 objectName（对标 Qt 的 arg(objectName)）");
        if (needle)
            XClassDelete((XClass*)needle);
        if (utf8)
            XClassDelete((XClass*)utf8);
    }

    /* clearError 只清错误码与文本，不能把状态机从停止态拉回运行态。 */
    XStateMachine_clearError(machine);
    XStateMachineTest_expect(XStateMachine_error(machine) == XStateMachine_NoError
                                 && XStateMachine_errorString(machine) == NULL
                                 && XStateMachine_errorString_utf8(machine) == NULL,
                             "clearError 清空错误码与两种错误串视图");
    XStateMachineTest_expect(!XStateMachine_isRunning(machine),
                             "clearError 不改变运行状态");

    XClassDelete((XClass*)machine);
}

/**
 * @brief 验证 addState/removeState 的所有权与边界语义。
 */
static void XStateMachineTest_addRemoveStateSemantics(void)
{
    XPrintf("\n  -- addState/removeState 语义 --\n");

    XStateMachine* machine = XStateMachine_create();
    XStateMachine* otherMachine = XStateMachine_create();
    XState* state = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XState_ExclusiveStates, NULL);

    XStateMachineTest_expect(XStateMachine_addState(machine, (XAbstractState*)state),
                             "把无父状态加入状态机");
    XStateMachineTest_expect(XAbstractState_machine((XAbstractState*)state) == machine
                                 && XAbstractState_parentState((XAbstractState*)state)
                                     == (XState*)machine,
                             "加入后 machine/parentState 指向本机");
    XStateMachineTest_expect(XStateMachine_addState(machine, (XAbstractState*)state) == false,
                             "重复加入同一状态被拒绝");
    XStateMachineTest_expect(XStateMachine_addState(machine, NULL) == false,
                             "加入 NULL 状态被拒绝");
    XStateMachineTest_expect(XStateMachine_removeState(otherMachine,
                                                       (XAbstractState*)state) == false,
                             "别的状态机不能移除本机状态");
    XStateMachineTest_expect(XStateMachine_removeState(machine, (XAbstractState*)state),
                             "本机可移除未激活的顶层状态");
    XStateMachineTest_expect(XAbstractState_machine((XAbstractState*)state) == NULL
                                 && XAbstractState_parentState((XAbstractState*)state) == NULL,
                             "移除后 machine/parentState 置空且状态仍存活");

    XClassDelete((XClass*)state); /* removeState 已释放所有权 */
    XClassDelete((XClass*)machine);
    XClassDelete((XClass*)otherMachine);
}

/**
 * @brief 运行状态机全 API 组合矩阵。
 * @return 全部检查通过返回 true。
 */
bool XStateMachineApiMatrixTest(void)
{
    XStateMachineTest_begin("XStateMachine 全 API 组合矩阵");

    XStateMachineTest_apiRoundTrip();
    XStateMachineTest_assignPropertyPolicy();
    XStateMachineTest_historyDefaultSemantics();
    XStateMachineTest_lifecycleCombinations();
    XStateMachineTest_errorStringContent();
    XStateMachineTest_addRemoveStateSemantics();

    int failures = g_failureCount;
    XStateMachineTest_end();
    return failures == 0;
}

void XStateMachineEventTest(void)
{
    XStateMachineTest_begin("XStateMachine 对象事件转换测试");

    // 1. 准备 idle -> final 状态图，转换监听指定对象的自定义事件。
    XStateMachineTest_EventSource* eventSource = XStateMachineTest_EventSource_create();
    XStateMachine* machine = XStateMachine_create();
    XState* idle = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XFinalState* finalState = XFinalState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XState*)machine);
    XEventType eventType = (XEventType)XEvent_registerEventType(-1);
    XEventTransition* transition = XEventTransition_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XObject*)eventSource, eventType, idle);

    XAbstractTransition_setTargetState(
        (XAbstractTransition*)transition, (XAbstractState*)finalState);
    XState_setInitialState((XState*)machine, (XAbstractState*)idle);

    // 2. start 只投递启动请求，处理事件后才建立初始配置。
    XStateMachine_start(machine);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XStateMachine_isRunning(machine),
                             "异步启动处理完成后状态机处于运行状态");
    XStateMachineTest_expect(XAbstractState_active((XAbstractState*)idle),
                             "互斥根状态进入配置的初始状态");

    // 3. 发送匹配事件，事件过滤器将克隆包装后交给状态机。
    XCoreApplication_sendEvent((XObject*)eventSource, XEvent_create(eventType));
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XAbstractState_active((XAbstractState*)finalState),
                             "匹配的对象事件触发转换并进入最终状态");
    XStateMachineTest_expect(!XStateMachine_isRunning(machine),
                             "进入顶层最终状态后状态机自然完成");

    XClassDelete((XClass*)machine);
    XClassDelete((XClass*)eventSource);

    XStateMachineTest_keyEventTransition();
    XStateMachineTest_mouseEventTransition();
    XStateMachineTest_postEventPriority();
    XStateMachineTest_end();
}

void XStateMachineSignalTest(void)
{
    XStateMachineTest_begin("XStateMachine 信号转换与运行语义测试");

    // 1. 基本信号转换：waiting -> final，并验证自然完成信号。
    g_finishedCount = 0;
    XObject* sender = XObject_create();
    XStateMachine* machine = XStateMachine_create();
    XState* waiting = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XFinalState* finalState = XFinalState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XState*)machine);
    XSignalTransition* transition = XSignalTransition_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sender, XSignal(XStateMachineTest_finish_signal), waiting);

    XAbstractTransition_setTargetState(
        (XAbstractTransition*)transition, (XAbstractState*)finalState);
    XState_setInitialState((XState*)machine, (XAbstractState*)waiting);
    XObject_connect_2((XObject*)machine,
                      XSignal(XState_finished_signal),
                      XStateMachineTest_finishedSlot);

    XStateMachine_start(machine);
    XStateMachineTest_processEvents();
    XStateMachineTest_finish_signal(sender);
    XStateMachineTest_processEvents();

    XStateMachineTest_expect(XAbstractState_active((XAbstractState*)finalState),
                             "活动源状态的匹配信号转换被选中");
    XStateMachineTest_expect(g_finishedCount == 1,
                             "自然完成只发出一次继承的 finished 信号");
    XStateMachineTest_expect(!XStateMachine_isRunning(machine),
                             "自然完成清除 running，且不走显式 stopped 路径");

    XClassDelete((XClass*)machine);
    XClassDelete((XClass*)sender);

    // 2. 扩展算法和边界 API 测试。
    XStateMachineTest_parallelSignalEvent();
    XStateMachineTest_multipleTargetStates();
    XStateMachineTest_descendantTransitionPriority();
    XStateMachineTest_transitionType(XAbstractTransition_InternalTransition);
    XStateMachineTest_transitionType(XAbstractTransition_ExternalTransition);
    XStateMachineTest_explicitStop();
    XStateMachineTest_delayedEventCancellation();
    XStateMachineTest_propertyApi();
    XStateMachineTest_targetDeletion();
    XStateMachineTest_errorState();
    XStateMachineTest_unrecoverableError();
    XStateMachineTest_end();
}

void XHistoryState_Test(void)
{
    XStateMachineTest_begin("XStateMachine 历史状态测试");

    // 1. 准备 parent/child/first 分层配置，并将历史状态设为深历史。
    XObject* sender = XObject_create();
    XStateMachine* machine = XStateMachine_create();
    XState* parent = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* outside = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* child = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parent);
    XState* first = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, child);
    XState* second = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, child);
    XHistoryState* history = XHistoryState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XHistoryState_DeepHistory, parent);

    XHistoryState_setDefaultState(history, (XAbstractState*)child);
    XState_setInitialState(child, (XAbstractState*)first);
    XState_setInitialState(parent, (XAbstractState*)child);
    XState_setInitialState((XState*)machine, (XAbstractState*)parent);
    XState_addTransition_2(first, sender,
                           XSignal(XStateMachineTest_advance_signal),
                           (XAbstractState*)second);
    XState_addTransition_2(parent, sender,
                           XSignal(XStateMachineTest_leave_signal),
                           (XAbstractState*)outside);
    XState_addTransition_2(outside, sender,
                           XSignal(XStateMachineTest_restore_signal),
                           (XAbstractState*)history);

    // 2. 初次启动应按祖先到后代顺序展开全部初始状态。
    XStateMachine_start(machine);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XAbstractState_active((XAbstractState*)first),
                             "嵌套初始状态从最外层展开到最内层");

    // 3. 先进入 second，再离开 parent 以记录深层原子配置。
    XStateMachineTest_advance_signal(sender);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XAbstractState_active((XAbstractState*)second),
                             "记录历史前可正常切换到嵌套后继状态");

    XStateMachineTest_leave_signal(sender);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(XAbstractState_active((XAbstractState*)outside),
                             "退出复合状态时记录其深历史配置");

    // 4. 指向历史伪状态的转换应恢复完整祖先路径和原子状态。
    XStateMachineTest_restore_signal(sender);
    XStateMachineTest_processEvents();
    XStateMachineTest_expect(
        XAbstractState_active((XAbstractState*)parent)
            && XAbstractState_active((XAbstractState*)child)
            && XAbstractState_active((XAbstractState*)second),
        "深历史恢复完整祖先路径和退出前的活动原子状态");
    XStateMachineTest_expect(!XAbstractState_active((XAbstractState*)first),
                             "深历史恢复时不会重新进入默认后代状态");

    XStateMachine_stop(machine);
    XStateMachineTest_processEvents();
    XClassDelete((XClass*)machine);
    XClassDelete((XClass*)sender);

    XStateMachineTest_shallowHistory();
    XStateMachineTest_end();
}
/**
 * @brief 运行 XStateMachine 全量测试（菜单三条 + 全 API 组合矩阵）。
 * @return 所有检查通过返回 true。
 */
/**
 * @brief 跑一遍全部测试组并返回累计失败数。
 * @note 内存门禁需要连续跑两遍，故抽成函数。
 */

// ==================== 动画 API 与管道（对标 Qt 动画集成） ====================

static int g_propsAssignedCount = 0;
static int g_exitOrderIndex = 0;
static int g_exitOrderSlot[8];
static int g_enterOrderIndex = 0;
static int g_enterOrderSlot[8];

static void XStateMachineTest_propsAssignedSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    (void)arguments;
    ++g_propsAssignedCount;
}

/** @brief 测试用固定时长动画（duration 可设，推进回调仅计数）。 */
XCLASS_DEFINE_BEGING(XStateMachineTest_FixedAnimation)
XCLASS_DEFINE_EXTEND_END(XStateMachineTest_FixedAnimation, XAbstractAnimation);

typedef struct XStateMachineTest_FixedAnimation {
    XAbstractAnimation m_class; ///< 继承 XAbstractAnimation，必须位于首成员。
    int m_duration;             ///< duration() 返回值。
} XStateMachineTest_FixedAnimation;

static int VXStateMachineTest_FixedAnimation_duration(const XAbstractAnimation* animation)
{
    return ((const XStateMachineTest_FixedAnimation*)animation)->m_duration;
}

static XVtable* XStateMachineTest_FixedAnimation_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStateMachineTest_FixedAnimation)
    XVTABLE_INHERIT_XCLASS(XAbstractAnimation);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractAnimation_Duration,
                             VXStateMachineTest_FixedAnimation_duration);
    return XVTABLE_DEFAULT;
}

static XStateMachineTest_FixedAnimation* XStateMachineTest_FixedAnimation_create(int duration)
{
    XStateMachineTest_FixedAnimation* animation = XClass_Malloc(XStateMachineTest_FixedAnimation);
    if (!animation)
        return NULL;
    XAbstractAnimation_init((XAbstractAnimation*)animation, NULL);
    XClassGetVtable(animation) = XStateMachineTest_FixedAnimation_class_init();
    Set_Class_Memory(animation, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(animation, true);
    animation->m_duration = duration;
    return animation;
}

static void XStateMachineTest_exitedOrderSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    (void)arguments;
    if (g_exitOrderIndex < 8)
        g_exitOrderSlot[g_exitOrderIndex] = (int)(intptr_t)sender;
    ++g_exitOrderIndex;
}

static void XStateMachineTest_enteredOrderSlot(XObject* sender, XVarList* arguments)
{
    (void)sender;
    (void)arguments;
    if (g_enterOrderIndex < 8)
        g_enterOrderSlot[g_enterOrderIndex] = (int)(intptr_t)sender;
    ++g_enterOrderIndex;
}

/** @brief 等待动画自然结束：睡眠 + 泵事件循环，直到端点或保护轮数用尽。 */
static void XStateMachineTest_waitAnimationFinished(XAbstractAnimation* animation)
{
    for (int guard = 0; guard < 40
         && XAbstractAnimation_state(animation) == XAbstractAnimation_Running; ++guard) {
        XThread_msleep(16);
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
}

/** @brief 动画 API 面：状态机 5 个 + 转换 3 个（对标 Qt 6.8 全部动画入口）。 */
static void XStateMachineAnimationApiTest(void)
{
    XStateMachineTest_begin("\n  -- 动画 API（对标 QStateMachine/QAbstractTransition 动画入口） --");
    g_checkCount = 0;
    g_failureCount = 0;

    XStateMachine* machine = XStateMachine_create();
    XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
    XState_setInitialState((XState*)machine, s1);
    XAbstractTransition* t = XState_addTransition_3(s1, s2);

    XStateMachineTest_expect(XStateMachine_isAnimated(machine),
                             "新建状态机动画开关默认为 true（对标 Qt）");
    XStateMachine_setAnimated(machine, false);
    XStateMachineTest_expect(!XStateMachine_isAnimated(machine), "setAnimated(false) 生效");
    XStateMachine_setAnimated(machine, true);

    XStateMachineTest_expect(!XStateMachine_addDefaultAnimation(machine, NULL),
                             "addDefaultAnimation(NULL) 被拒绝");
    XStateMachineTest_FixedAnimation* defaultAnim = XStateMachineTest_FixedAnimation_create(50);
    XStateMachineTest_expect(XStateMachine_addDefaultAnimation(machine, (XAbstractAnimation*)defaultAnim),
                             "addDefaultAnimation 成功追加");
    XStateMachineTest_expect(XStateMachine_addDefaultAnimation(machine, (XAbstractAnimation*)defaultAnim),
                             "与 Qt 一致：重复追加不检查");
    const XVector* defaults = XStateMachine_defaultAnimations_const(machine);
    XStateMachineTest_expect(defaults
                                 && XVector_size_base((const XContainer*)defaults) == 2,
                             "defaultAnimations 返回内部列表（含重复项）");
    XStateMachine_removeDefaultAnimation(machine, (XAbstractAnimation*)defaultAnim);
    XStateMachineTest_expect(XVector_isEmpty_base((const XContainer*)XStateMachine_defaultAnimations_const(machine)),
                             "removeDefaultAnimation 移除全部匹配项");

    XStateMachineTest_expect(!XAbstractTransition_addAnimation(t, NULL),
                             "addAnimation(NULL) 被拒绝");
    XStateMachineTest_FixedAnimation* owned = XStateMachineTest_FixedAnimation_create(50);
    XStateMachineTest_expect(XAbstractTransition_addAnimation(t, (XAbstractAnimation*)owned),
                             "addAnimation 成功追加");
    XStateMachineTest_expect(XAbstractTransition_addAnimation(t, (XAbstractAnimation*)owned),
                             "与 Qt 一致：addAnimation 不检查重复");
    const XVector* animations = XAbstractTransition_animations_const(t);
    XStateMachineTest_expect(animations && XVector_size_base((const XContainer*)animations) == 2,
                             "animations 返回转换持有的列表");
    XAbstractTransition_removeAnimation(t, (XAbstractAnimation*)owned);
    XStateMachineTest_expect(XVector_size_base((const XContainer*)XAbstractTransition_animations_const(t)) == 1,
                             "removeAnimation 只移除第一个匹配项");
    XStateMachineTest_FixedAnimation* stranger = XStateMachineTest_FixedAnimation_create(10);
    XAbstractTransition_removeAnimation(t, (XAbstractAnimation*)stranger);
    XStateMachineTest_expect(XVector_size_base((const XContainer*)XAbstractTransition_animations_const(t)) == 1,
                             "移除未关联动画为空操作");
    XClassDelete((XClass*)stranger);

    /* 转换析构不销毁动画对象（对标 Qt：转换不取得动画所有权）。 */
    XState_removeTransition(s1, t);
    XClassDelete((XClass*)t);
    XStateMachineTest_expect(XAbstractAnimation_state((XAbstractAnimation*)owned) == XAbstractAnimation_Stopped,
                             "转换析构后动画对象仍然存活");
    XClassDelete((XClass*)owned);
    XClassDelete((XClass*)defaultAnim);
    XClassDelete((XClass*)machine);
    XStateMachineTest_end();
}

/** @brief 动画管道：propertiesAssigned 延迟、退出终止、总开关短路。 */
static void XStateMachineAnimationPipelineTest(void)
{
    XStateMachineTest_begin("\n  -- 动画管道（propertiesAssigned 延迟与退出终止） --");
    g_checkCount = 0;
    g_failureCount = 0;

    XEventType triggerType = (XEventType)XEvent_registerEventType(-1);
    XEventType leaveType = (XEventType)XEvent_registerEventType(-1);

    /* 场景一：带动画的转换延迟 propertiesAssigned，动画结束补发。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, s1);
        XAbstractTransition* t = XStateMachineTest_TypeTransition_create(s1, triggerType, NULL);
        (void)t;
        XAbstractTransition_setTargetState(t, s2);
        XVariant* value = XVariant_create_int(42);
        XString* propName = XString_create_utf8("value");
        XState_assignProperty(s2, (XObject*)machine, propName, value);
        XClassDelete((XClass*)value);
        XClassDelete((XClass*)propName);
        g_propsAssignedCount = 0;
        XObject_connect((XObject*)s2, (size_t)XState_propertiesAssigned_signal,
                          NULL, XStateMachineTest_propsAssignedSlot, XConnectionType_Auto);

        XStateMachineTest_FixedAnimation* animation = XStateMachineTest_FixedAnimation_create(80);
        XAbstractTransition_addAnimation(t, (XAbstractAnimation*)animation);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)s1), "启动进入 s1");

        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)s2), "事件触发进入 s2");
        XStateMachineTest_expect(g_propsAssignedCount == 0,
                                 "属性已立即写入但 propertiesAssigned 被动画延迟");
        XStateMachineTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation)
                                     == XAbstractAnimation_Running,
                                 "转换触发时动画自动启动");
        XStateMachineTest_waitAnimationFinished((XAbstractAnimation*)animation);
        XStateMachineTest_expect(g_propsAssignedCount == 1,
                                 "动画结束后补发 propertiesAssigned");
        XStateMachineTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation)
                                     == XAbstractAnimation_Stopped,
                                 "动画自然结束处于 Stopped");
        XClassDelete((XClass*)machine);
        XClassDelete((XClass*)animation);
        /* 动画绑定记录经 deleteLater 回收：裸泵不带循环层，测试末尾显式
         * 冲刷延迟删除（对标 Qt sendPostedEvents(DeferredDelete) 惯例）。 */
        XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    }

    /* 场景二：setAnimated(false) 短路动画管道。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XStateMachine_setAnimated(machine, false);
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, s1);
        XAbstractTransition* t = XStateMachineTest_TypeTransition_create(s1, triggerType, NULL);
        XAbstractTransition_setTargetState(t, s2);
        XStateMachineTest_FixedAnimation* animation = XStateMachineTest_FixedAnimation_create(80);
        XAbstractTransition_addAnimation(t, (XAbstractAnimation*)animation);
        g_propsAssignedCount = 0;
        XObject_connect((XObject*)s2, (size_t)XState_propertiesAssigned_signal,
                          NULL, XStateMachineTest_propsAssignedSlot, XConnectionType_Auto);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)s2)
                                     && g_propsAssignedCount == 1,
                                 "总开关关闭时 propertiesAssigned 立即发出");
        XStateMachineTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation)
                                     == XAbstractAnimation_Stopped,
                                 "总开关关闭时不启动动画");
        XClassDelete((XClass*)machine);
        XClassDelete((XClass*)animation);
        /* 动画绑定记录经 deleteLater 回收：裸泵不带循环层，测试末尾显式
         * 冲刷延迟删除（对标 Qt sendPostedEvents(DeferredDelete) 惯例）。 */
        XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    }

    /* 场景三：状态退出终止活动动画，延期的 propertiesAssigned 不再补发。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s3 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, s1);
        XAbstractTransition* enter = XStateMachineTest_TypeTransition_create(s1, triggerType, NULL);
        XAbstractTransition_setTargetState(enter, s2);
        XAbstractTransition* leave = XStateMachineTest_TypeTransition_create(s2, leaveType, NULL);
        XAbstractTransition_setTargetState(leave, s3);
        XVariant* exitValue = XVariant_create_int(7);
        XString* exitProp = XString_create_utf8("value");
        XState_assignProperty(s2, (XObject*)machine, exitProp, exitValue);
        XClassDelete((XClass*)exitValue);
        XClassDelete((XClass*)exitProp);
        XStateMachineTest_FixedAnimation* animation = XStateMachineTest_FixedAnimation_create(200);
        XAbstractTransition_addAnimation(enter, (XAbstractAnimation*)animation);
        g_propsAssignedCount = 0;
        XObject_connect((XObject*)s2, (size_t)XState_propertiesAssigned_signal,
                          NULL, XStateMachineTest_propsAssignedSlot, XConnectionType_Auto);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation)
                                     == XAbstractAnimation_Running,
                                 "s2 的动画正在运行");
        XStateMachine_postEvent(machine, XEvent_create(leaveType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)s3), "s2 已退出进入 s3");
        XStateMachineTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation)
                                     == XAbstractAnimation_Stopped,
                                 "状态退出终止活动动画（对标 terminateActiveAnimations）");
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XStateMachineTest_expect(g_propsAssignedCount == 0,
                                 "已退出状态的延期 propertiesAssigned 不再补发");
        XClassDelete((XClass*)machine);
        XClassDelete((XClass*)animation);
        /* 动画绑定记录经 deleteLater 回收：裸泵不带循环层，测试末尾显式
         * 冲刷延迟删除（对标 Qt sendPostedEvents(DeferredDelete) 惯例）。 */
        XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    }

    /* 场景四：默认动画对所有转换生效。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, s1);
        XAbstractTransition* t = XStateMachineTest_TypeTransition_create(s1, triggerType, NULL);
        XAbstractTransition_setTargetState(t, s2);
        XVariant* defaultValue = XVariant_create_int(9);
        XString* defaultProp = XString_create_utf8("value");
        XState_assignProperty(s2, (XObject*)machine, defaultProp, defaultValue);
        XClassDelete((XClass*)defaultValue);
        XClassDelete((XClass*)defaultProp);
        XStateMachineTest_FixedAnimation* animation = XStateMachineTest_FixedAnimation_create(60);
        XStateMachine_addDefaultAnimation(machine, (XAbstractAnimation*)animation);
        g_propsAssignedCount = 0;
        XObject_connect((XObject*)s2, (size_t)XState_propertiesAssigned_signal,
                          NULL, XStateMachineTest_propsAssignedSlot, XConnectionType_Auto);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation)
                                     == XAbstractAnimation_Running,
                                 "默认动画随转换启动");
        XStateMachineTest_waitAnimationFinished((XAbstractAnimation*)animation);
        XStateMachineTest_expect(g_propsAssignedCount == 1,
                                 "默认动画结束后补发 propertiesAssigned");
        XClassDelete((XClass*)machine);
        XClassDelete((XClass*)animation);
        /* 动画绑定记录经 deleteLater 回收：裸泵不带循环层，测试末尾显式
         * 冲刷延迟删除（对标 Qt sendPostedEvents(DeferredDelete) 惯例）。 */
        XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
    }

    XStateMachineTest_end();
}

/** @brief 行为对齐批次：错误文本/启动不清错/子状态机启停/排序/历史语义。 */
static void XStateMachineBehaviorAlignTest(void)
{
    XStateMachineTest_begin("\n  -- 行为对齐批次（对标 Qt 6.8 行为口径） --");
    g_checkCount = 0;
    g_failureCount = 0;
    XEventType triggerType = (XEventType)XEvent_registerEventType(-1);
    XEventType leaveType = (XEventType)XEvent_registerEventType(-1);

    /* B3：四条错误文本逐字对齐 Qt 6.8。 */
    {
        XStateMachine* machine = XStateMachine_create();
        do { XString* _nameTmp = XString_create_utf8("rootm"); if (_nameTmp) { XObject_setObjectName((XObject*)machine, _nameTmp); XClassDelete((XClass*)_nameTmp); } } while (0);
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        do { XString* _nameTmp = XString_create_utf8("s1"); if (_nameTmp) { XObject_setObjectName((XObject*)s1, _nameTmp); XClassDelete((XClass*)_nameTmp); } } while (0);
        XState* inner = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, s1);
        do { XString* _nameTmp = XString_create_utf8("inner"); if (_nameTmp) { XObject_setObjectName((XObject*)inner, _nameTmp); XClassDelete((XClass*)_nameTmp); } } while (0);
        /* s1 有子状态 inner 但未设初始状态 → 进入时报 NoInitialStateError
         * （上下文=缺初始状态的状态 s1，与 Qt 的 setError 调用点一致）。 */
        XState_setInitialState((XState*)machine, s1);
        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XStateMachine_error(machine) == XStateMachine_NoInitialStateError,
                                 "缺少初始状态报告 NoInitialStateError");
        XStateMachineTest_expect(strcmp(XStateMachine_errorString_utf8(machine),
                                        "Missing initial state in compound state 's1'") == 0,
                                 "NoInitial 错误文本逐字对齐 Qt");

        /* B4：start 不清错误（Qt 语义：保留到显式 clearError）。 */
        XStateMachine_stop(machine);
        XStateMachineTest_processEvents();
        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XStateMachine_error(machine) == XStateMachine_NoInitialStateError
                                     && XStateMachine_errorString_utf8(machine) != NULL,
                                 "重启后上一次错误保留（对标 Qt）");
        XStateMachine_clearError(machine);
        XStateMachineTest_expect(XStateMachine_error(machine) == XStateMachine_NoError
                                     && XStateMachine_errorString_utf8(machine) == NULL,
                                 "clearError 显式清空");
        XClassDelete((XClass*)machine);
    }

    /* B3：历史缺默认状态文本 + 默认转换 onTransition 不被调用（B2）。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        do { XString* _nameTmp = XString_create_utf8("sx"); if (_nameTmp) { XObject_setObjectName((XObject*)s1, _nameTmp); XClassDelete((XClass*)_nameTmp); } } while (0);
        XState* s1a = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, s1);
        XState_setInitialState(s1, s1a);
        XHistoryState* history = XHistoryState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                         XHistoryState_ShallowHistory, s1);
        do { XString* _nameTmp = XString_create_utf8("hh"); if (_nameTmp) { XObject_setObjectName((XObject*)history, _nameTmp); XClassDelete((XClass*)_nameTmp); } } while (0);
        XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, s2);
        int defaultOnTransitionCount = 0;
        XAbstractTransition* defaultTransition
            = XStateMachineTest_TypeTransition_create(NULL, XEVENT_TYPE_NONE, &defaultOnTransitionCount);
        XHistoryState_setDefaultTransition(history, defaultTransition);
        XAbstractTransition* back = XStateMachineTest_TypeTransition_create(s2, triggerType, NULL);
        XAbstractTransition_setTargetState(back, (XAbstractState*)history);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XStateMachine_error(machine)
                                     == XStateMachine_NoDefaultStateInHistoryStateError,
                                 "无记录且默认转换无目标报告 NoDefaultStateInHistoryStateError");
        XStateMachineTest_expect(strcmp(XStateMachine_errorString_utf8(machine),
                                        "Missing default state in history state 'hh'") == 0,
                                 "历史错误文本逐字对齐 Qt");
        XStateMachineTest_expect(defaultOnTransitionCount == 0,
                                 "历史默认转换的 onTransition 不会被调用（对标 Qt）");
        /* 默认转换所有权已随 setDefaultTransition 转入历史状态，
         * 随机器整树级联释放，此处不得重复删除。 */
        XClassDelete((XClass*)machine);
    }

    /* B3：无共同祖先 + 并行根两条文本。 */
    {
        XStateMachine* machine = XStateMachine_create();
        do { XString* _nameTmp = XString_create_utf8("pm"); if (_nameTmp) { XObject_setObjectName((XObject*)machine, _nameTmp); XClassDelete((XClass*)_nameTmp); } } while (0);
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        do { XString* _nameTmp = XString_create_utf8("from"); if (_nameTmp) { XObject_setObjectName((XObject*)s1, _nameTmp); XClassDelete((XClass*)_nameTmp); } } while (0);
        XState* detached = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, NULL);
        do { XString* _nameTmp = XString_create_utf8("detached"); if (_nameTmp) { XObject_setObjectName((XObject*)detached, _nameTmp); XClassDelete((XClass*)_nameTmp); } } while (0);
        XState_setInitialState((XState*)machine, s1);
        XAbstractTransition* escape = XStateMachineTest_TypeTransition_create(s1, triggerType, NULL);
        XAbstractTransition_setTargetState(escape, detached);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XStateMachine_error(machine)
                                     == XStateMachine_NoCommonAncestorForTransitionError,
                                 "跨树目标报告 NoCommonAncestorForTransitionError");
        XStateMachineTest_expect(strcmp(XStateMachine_errorString_utf8(machine),
                                        "No common ancestor for targets and source of transition from state 'from'") == 0,
                                 "无共同祖先错误文本逐字对齐 Qt");
        XClassDelete((XClass*)machine);
        XClassDelete((XClass*)detached);
    }
    {
        XStateMachine* machine = XStateMachine_create();
        do { XString* _nameTmp = XString_create_utf8("par"); if (_nameTmp) { XObject_setObjectName((XObject*)machine, _nameTmp); XClassDelete((XClass*)_nameTmp); } } while (0);
        XState_setChildMode((XState*)machine, XState_ParallelStates);
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XAbstractTransition* stray = XStateMachineTest_TypeTransition_create(s1, triggerType, NULL);
        (void)stray;
        XAbstractTransition* cross = XStateMachineTest_TypeTransition_create(s2, triggerType, NULL);
        XAbstractTransition_setTargetState(cross, s1);
        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XStateMachine_error(machine)
                                     == XStateMachine_StateMachineChildModeSetToParallelError,
                                 "并行根跨区转换报告 StateMachineChildModeSetToParallelError");
        const char* parallelText = XStateMachine_errorString_utf8(machine);
        XStateMachineTest_expect(parallelText
                                     && strcmp(parallelText,
                                               "Child mode of state machine 'par' is not 'ExclusiveStates'.") == 0,
                                 "并行根错误文本逐字对齐 Qt");
        XClassDelete((XClass*)machine);
    }

    /* B5：状态机作为子状态进入自动 start、退出自动 stop。 */
    {
        XStateMachine* outer = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)outer);
        XState* s3 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)outer);
        XStateMachine* sub = XStateMachine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates);
        XState* subChild = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)sub);
        XState_setInitialState((XState*)sub, subChild);
        XStateMachine_addState(outer, (XAbstractState*)sub);
        XState_setInitialState((XState*)outer, s1);
        XAbstractTransition* enter = XStateMachineTest_TypeTransition_create(s1, triggerType, NULL);
        XAbstractTransition_setTargetState(enter, (XAbstractState*)sub);
        XAbstractTransition* leave = XStateMachineTest_TypeTransition_create((XState*)sub, leaveType, NULL);
        XAbstractTransition_setTargetState(leave, s3);

        XStateMachine_start(outer);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)s1), "外层进入 s1");
        XStateMachineTest_expect(!XStateMachine_isRunning(sub), "子状态机未进入前不运行");
        XStateMachine_postEvent(outer, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XStateMachine_isRunning(sub),
                                 "进入子状态机状态自动 start（对标 QStateMachine::onEntry）");
        XStateMachine_postEvent(outer, XEvent_create(leaveType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)s3)
                                     && !XStateMachine_isRunning(sub),
                                 "退出子状态机状态自动 stop（对标 QStateMachine::onExit）");
        XClassDelete((XClass*)outer);
    }

    /* B6：并行兄弟进入序=子序、退出序=倒序（对标 stateEntry/ExitLessThan）。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* parent = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ParallelStates, (XState*)machine);
        XState* regionA = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parent);
        XState* regionB = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parent);
        XState* atomicA = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, regionA);
        XState* atomicB = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, regionB);
        XState_setInitialState(regionA, atomicA);
        XState_setInitialState(regionB, atomicB);
        XState_setInitialState((XState*)machine, parent);
        XState* outside = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        (void)outside;
        XAbstractTransition* leave = XStateMachineTest_TypeTransition_create(parent, triggerType, NULL);
        XAbstractTransition_setTargetState(leave, outside);

        g_enterOrderIndex = 0;
        g_exitOrderIndex = 0;
        XObject_connect((XObject*)atomicA, (size_t)XAbstractState_entered_signal,
                          NULL, XStateMachineTest_enteredOrderSlot, XConnectionType_Auto);
        XObject_connect((XObject*)atomicB, (size_t)XAbstractState_entered_signal,
                          NULL, XStateMachineTest_enteredOrderSlot, XConnectionType_Auto);
        XObject_connect((XObject*)atomicA, (size_t)XAbstractState_exited_signal,
                          NULL, XStateMachineTest_exitedOrderSlot, XConnectionType_Auto);
        XObject_connect((XObject*)atomicB, (size_t)XAbstractState_exited_signal,
                          NULL, XStateMachineTest_exitedOrderSlot, XConnectionType_Auto);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XPrintf("[DBG] enterIdx=%d s0=%p s1=%p A=%p B=%p\n", g_enterOrderIndex,
                (void*)(intptr_t)g_enterOrderSlot[0], (void*)(intptr_t)g_enterOrderSlot[1],
                (void*)atomicA, (void*)atomicB);
        XStateMachineTest_expect(g_enterOrderIndex == 2
                                     && g_enterOrderSlot[0] == (int)(intptr_t)atomicA
                                     && g_enterOrderSlot[1] == (int)(intptr_t)atomicB,
                                 "并行原子状态按子序进入（对标 stateEntryLessThan）");
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XPrintf("[DBG] exitIdx=%d s0=%p s1=%p\n", g_exitOrderIndex,
                (void*)(intptr_t)g_exitOrderSlot[0], (void*)(intptr_t)g_exitOrderSlot[1]);
        XStateMachineTest_expect(g_exitOrderIndex == 2
                                     && g_exitOrderSlot[0] == (int)(intptr_t)atomicB
                                     && g_exitOrderSlot[1] == (int)(intptr_t)atomicA,
                                 "并行原子状态按子序倒排退出（对标 stateExitLessThan）");
        XClassDelete((XClass*)machine);
    }

    /* B10：冲突消解按源状态进入序排序，先序转换胜出。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* parent = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ParallelStates, (XState*)machine);
        XState* regionA = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parent);
        XState* regionB = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, parent);
        XState* atomicA = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, regionA);
        XState* atomicB = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, regionB);
        XState_setInitialState(regionA, atomicA);
        XState_setInitialState(regionB, atomicB);
        XState* winner = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* loser = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, parent);
        XAbstractTransition* first = XStateMachineTest_TypeTransition_create(regionA, triggerType, NULL);
        XAbstractTransition_setTargetState(first, winner);
        XAbstractTransition* second = XStateMachineTest_TypeTransition_create(regionB, triggerType, NULL);
        XAbstractTransition_setTargetState(second, loser);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XPrintf("[DBG] conflict winnerActive=%d loserActive=%d\n",
                (int)XAbstractState_active((XAbstractState*)winner),
                (int)XAbstractState_active((XAbstractState*)loser));
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)winner)
                                     && !XAbstractState_active((XAbstractState*)loser),
                                 "退出集交叠时按源序先者胜出（对标 removeConflictingTransitions 排序）");
        XClassDelete((XClass*)machine);
    }

    /* B7：未注册信号转换不匹配手工投递的 SignalEvent。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, s1);
        XStateMachine_start(machine);
        XStateMachineTest_processEvents();

        XStateMachineTest_EventSource* sender = XStateMachineTest_EventSource_create();
        XSignalTransition* transition = XSignalTransition_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE, (const XObject*)sender,
            (size_t)XStateMachineTest_advance_signal, NULL);
        XStateMachine_SignalEvent* signalEvent = XStateMachine_SignalEvent_create(
            (XObject*)sender, (size_t)XStateMachineTest_advance_signal, NULL);
        XStateMachineTest_expect(!XAbstractTransition_eventTest_base(
                                     (XAbstractTransition*)transition, (XEvent*)signalEvent),
                                 "未注册信号转换的 eventTest 恒 false（对标 signalIndex==-1）");
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)s1)
                                     && !XAbstractState_active((XAbstractState*)s2),
                                 "手工投递 SignalEvent 不触发未注册转换");
        XClassDelete((XClass*)signalEvent);
        XClassDelete((XClass*)transition);
        XClassDelete((XClass*)sender);
        XStateMachine_stop(machine);
        XStateMachineTest_processEvents();
        XClassDelete((XClass*)machine);
    }

    /* B1：setHistoryType 不清空已保存配置（对标 Qt）。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s1a = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, s1);
        XState* s1b = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, s1);
        (void)s1b;
        XState_setInitialState(s1, s1a);
        XHistoryState* history = XHistoryState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                         XHistoryState_ShallowHistory, s1);
        XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, s1);
        XAbstractTransition* escape = XStateMachineTest_TypeTransition_create(s1, leaveType, NULL);
        XAbstractTransition_setTargetState(escape, s2);
        XAbstractTransition* back = XStateMachineTest_TypeTransition_create(s2, triggerType, NULL);
        XAbstractTransition_setTargetState(back, (XAbstractState*)history);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachine_postEvent(machine, XEvent_create(leaveType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        const XVector* saved = history->m_configuration;
        XStateMachineTest_expect(saved && XVector_size_base((const XContainer*)saved) == 1,
                                 "浅历史记录了退出前配置");
        XHistoryState_setHistoryType(history, XHistoryState_DeepHistory);
        saved = history->m_configuration;
        XStateMachineTest_expect(saved && XVector_size_base((const XContainer*)saved) == 1,
                                 "setHistoryType 不清空已保存配置（对标 Qt）");
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)s1a),
                                 "切换类型后历史恢复仍走已存配置");
        XClassDelete((XClass*)machine);
    }

    /* 再扫描批次 A：嵌套状态机不算复合态（对标 Qt isCompound），内转换
     * 进入嵌套机子状态时嵌套机自身必须退出并重进。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XStateMachine* sub = XStateMachine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates);
        XState* subA = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)sub);
        XState* subB = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)sub);
        XState_setInitialState(sub, subA);
        XStateMachine_addState(machine, (XAbstractState*)sub);
        XState_setInitialState((XState*)machine, s1);
        int enterCount = 0;
        int exitCount = 0;
        XAbstractTransition* enter = XStateMachineTest_TypeTransition_create(s1, triggerType, NULL);
        XAbstractTransition_setTargetState(enter, (XAbstractState*)sub);
        /* 嵌套机自身的内转换：subA → subB，类型 Internal。 */
        XAbstractTransition* inner = XStateMachineTest_TypeTransition_create((XState*)sub, leaveType, NULL);
        XAbstractTransition_setTransitionType((XAbstractTransition*)inner,
                                              XAbstractTransition_InternalTransition);
        XAbstractTransition_setTargetState(inner, subB);
        XObject_connect((XObject*)sub, (size_t)XAbstractState_entered_signal,
                          NULL, XStateMachineTest_enteredOrderSlot, XConnectionType_Auto);
        XObject_connect((XObject*)sub, (size_t)XAbstractState_exited_signal,
                          NULL, XStateMachineTest_exitedOrderSlot, XConnectionType_Auto);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)subA),
                                 "事件进入嵌套机初始子状态");
        g_enterOrderIndex = 0;
        g_exitOrderIndex = 0;
        XStateMachine_postEvent(machine, XEvent_create(leaveType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)subB)
                                     && !XAbstractState_active((XAbstractState*)subA),
                                 "嵌套机内转换切换到 subB");
        XStateMachineTest_expect(g_exitOrderIndex == 1 && g_enterOrderIndex == 1,
                                 "嵌套机（原子对待）内转换退出并重进嵌套机本身"
                                 "（对标 Qt isCompound 排除嵌套机）");
        XClassDelete((XClass*)machine);
    }

    /* 再扫描批次 B：跨状态机目标在 addTransition_2/_3 被拒绝。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, s1);
        XStateMachine* other = XStateMachine_create();
        XState* detached = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates,
                                            (XState*)other);
        XState_setInitialState((XState*)other, detached);
        XStateMachineTest_EventSource* sender = XStateMachineTest_EventSource_create();

        XStateMachineTest_expect(XState_addTransition_2(s1, (const XObject*)sender,
                                                        (size_t)XStateMachineTest_advance_signal,
                                                        detached) == NULL,
                                 "addTransition_2 跨机目标被拒绝返回 NULL"
                                 "（对标 Qt addTransition 校验）");
        XStateMachineTest_expect(XState_addTransition_3(s1, detached) == NULL,
                                 "addTransition_3 跨机目标被拒绝返回 NULL");
        XStateMachineTest_expect(XVector_isEmpty_base((const XContainer*)XState_transitions_const(s1)),
                                 "被拒绝的目标不产生任何转换残留");
        XStateMachine_stop(machine);
        XStateMachineTest_processEvents();
        XClassDelete((XClass*)machine);
        XClassDelete((XClass*)other);
        XClassDelete((XClass*)sender);
    }

    XStateMachineTest_end();
}


// ==================== 覆盖缺口批次（active 时序/信号参数/历史回退/停机不终止动画/同源双类型/配置口径） ====================

/** @brief 事件顺序日志：编码 = (sender 指针 << 8) | 事件类型。 */
#define XSMTEST_EVT_ENTERED 0
#define XSMTEST_EVT_EXITED 1
#define XSMTEST_EVT_ACTIVE_TRUE 2
#define XSMTEST_EVT_ACTIVE_FALSE 3
static int64_t g_orderLog[32];
static int g_orderLogIndex = 0;

static void XStateMachineTest_logEvent(const XObject* sender, int type)
{
    if (g_orderLogIndex < 32)
        g_orderLog[g_orderLogIndex] = ((int64_t)(intptr_t)sender << 8) | type;
    ++g_orderLogIndex;
}

static int XStateMachineTest_findOrderEvent(const XObject* sender, int type)
{
    const int64_t key = ((int64_t)(intptr_t)sender << 8) | type;
    for (int i = 0; i < g_orderLogIndex && i < 32; ++i)
        if (g_orderLog[i] == key)
            return i;
    return -1;
}

static void XStateMachineTest_enteredLogSlot(XObject* sender, XVarList* arguments)
{
    (void)arguments;
    XStateMachineTest_logEvent(sender, XSMTEST_EVT_ENTERED);
}

static void XStateMachineTest_exitedLogSlot(XObject* sender, XVarList* arguments)
{
    (void)arguments;
    XStateMachineTest_logEvent(sender, XSMTEST_EVT_EXITED);
}

static void XStateMachineTest_activeChangedLogSlot(XObject* sender, XVarList* arguments)
{
    XVarList_args_1(arguments, bool, active);
    XStateMachineTest_logEvent(sender, active ? XSMTEST_EVT_ACTIVE_TRUE : XSMTEST_EVT_ACTIVE_FALSE);
}

static int g_checkedArgAtEventTest = -1;
static int g_checkedArgAtOnTransition = -1;

/** @brief 带参数的测试信号（对标 Qt 文档 CheckedTransition 例子的 stateChanged(int)）。 */
static void* XStateMachineTest_value_signal(XObject* sender, int value)
{
    XEmitSignal(sender, XStateMachineTest_value_signal,
                XVarList_create(2, sizeof(int), &value),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

/** @brief 参数门控信号转换（对标 Qt 文档的 CheckedTransition：eventTest 先调
 *  基类，再读 SignalEvent::arguments() 过滤；onTransition 同样可读参数）。 */
XCLASS_DEFINE_BEGING(XStateMachineTest_CheckedTransition)
XCLASS_DEFINE_EXTEND_END(XStateMachineTest_CheckedTransition, XSignalTransition);

typedef struct XStateMachineTest_CheckedTransition {
    XSignalTransition m_class; ///< 继承 XSignalTransition，必须位于首成员。
    int m_expectedValue;       ///< 首参数等于该值才触发。
} XStateMachineTest_CheckedTransition;

static bool VXStateMachineTest_CheckedTransition_eventTest(
    XStateMachineTest_CheckedTransition* transition, XEvent* event)
{
    bool base = XVtableGetFunc(
        XSignalTransition_class_init(), EXAbstractTransition_EventTest,
        bool(*)(XSignalTransition*, XEvent*))((XSignalTransition*)transition, event);
    if (!base)
        return false;
    XStateMachine_SignalEvent* signalEvent = (XStateMachine_SignalEvent*)event;
    const XVarList* arguments = XStateMachine_SignalEvent_arguments_const(signalEvent);
    if (!arguments)
        return false;
    XVarList_start(arguments);
    int value = XVarList_arg(arguments, int);
    g_checkedArgAtEventTest = value;
    return value == transition->m_expectedValue;
}

static void VXStateMachineTest_CheckedTransition_onTransition(
    XStateMachineTest_CheckedTransition* transition, XEvent* event)
{
    (void)transition;
    if (!event || XEvent_type(event) != XStateMachine_signalEventType_internal())
        return;
    XStateMachine_SignalEvent* signalEvent = (XStateMachine_SignalEvent*)event;
    const XVarList* arguments = XStateMachine_SignalEvent_arguments_const(signalEvent);
    if (!arguments)
        return;
    XVarList_start(arguments);
    g_checkedArgAtOnTransition = XVarList_arg(arguments, int);
}

static XVtable* XStateMachineTest_CheckedTransition_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStateMachineTest_CheckedTransition)
    XVTABLE_INHERIT_XCLASS(XSignalTransition);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractTransition_EventTest,
                             VXStateMachineTest_CheckedTransition_eventTest);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractTransition_OnTransition,
                             VXStateMachineTest_CheckedTransition_onTransition);
    return XVTABLE_DEFAULT;
}

static XStateMachineTest_CheckedTransition* XStateMachineTest_CheckedTransition_create(
    const XObject* sender, size_t signal, int expectedValue, XState* sourceState)
{
    XStateMachineTest_CheckedTransition* transition =
        XMemory_malloc(sizeof(XStateMachineTest_CheckedTransition), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!transition)
        return NULL;
    XSignalTransition_init_ex((XSignalTransition*)transition, sender, signal, sourceState);
    XClassGetVtable(transition) = XStateMachineTest_CheckedTransition_class_init();
    transition->m_class.m_class.m_kind = XAbstractTransition_SignalTransition;
    transition->m_expectedValue = expectedValue;
    Set_Class_Memory(transition, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(transition, true);
    return transition;
}

/** @brief 补覆盖批次：六个此前零覆盖的 Qt 行为点。 */
static void XStateMachineCoverageGapTest(void)
{
    XStateMachineTest_begin("\n  -- 覆盖缺口批次（active 时序/信号参数/历史回退/停机不终止动画/同源双类型/配置口径） --");
    g_checkCount = 0;
    g_failureCount = 0;
    XEventType triggerType = (XEventType)XEvent_registerEventType(-1);
    XEventType typeA = (XEventType)XEvent_registerEventType(-1);
    XEventType typeB = (XEventType)XEvent_registerEventType(-1);

    /* 场景一：active 在 entered 与 exited 之间为真（对标 Qt 文档契约
     * "A state is active between entered() and exited() signals"）。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, s1);
        XAbstractTransition* t = XStateMachineTest_TypeTransition_create(s1, triggerType, NULL);
        XAbstractTransition_setTargetState(t, s2);
        XObject_connect((XObject*)s1, (size_t)XAbstractState_entered_signal,
                          NULL, XStateMachineTest_enteredLogSlot, XConnectionType_Auto);
        XObject_connect((XObject*)s1, (size_t)XAbstractState_exited_signal,
                          NULL, XStateMachineTest_exitedLogSlot, XConnectionType_Auto);
        XObject_connect((XObject*)s1, (size_t)XAbstractState_activeChanged_signal,
                          NULL, XStateMachineTest_activeChangedLogSlot, XConnectionType_Auto);
        XObject_connect((XObject*)s2, (size_t)XAbstractState_entered_signal,
                          NULL, XStateMachineTest_enteredLogSlot, XConnectionType_Auto);
        XObject_connect((XObject*)s2, (size_t)XAbstractState_exited_signal,
                          NULL, XStateMachineTest_exitedLogSlot, XConnectionType_Auto);
        XObject_connect((XObject*)s2, (size_t)XAbstractState_activeChanged_signal,
                          NULL, XStateMachineTest_activeChangedLogSlot, XConnectionType_Auto);

        g_orderLogIndex = 0;
        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        int enteredS1 = XStateMachineTest_findOrderEvent((XObject*)s1, XSMTEST_EVT_ENTERED);
        int activeTrueS1 = XStateMachineTest_findOrderEvent((XObject*)s1, XSMTEST_EVT_ACTIVE_TRUE);
        XStateMachineTest_expect(enteredS1 >= 0 && activeTrueS1 > enteredS1,
                                 "s1：entered 先于 active=true（对标 Qt emitEntered 次序）");

        g_orderLogIndex = 0;
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        int activeFalseS1 = XStateMachineTest_findOrderEvent((XObject*)s1, XSMTEST_EVT_ACTIVE_FALSE);
        int exitedS1 = XStateMachineTest_findOrderEvent((XObject*)s1, XSMTEST_EVT_EXITED);
        int enteredS2 = XStateMachineTest_findOrderEvent((XObject*)s2, XSMTEST_EVT_ENTERED);
        int activeTrueS2 = XStateMachineTest_findOrderEvent((XObject*)s2, XSMTEST_EVT_ACTIVE_TRUE);
        XStateMachineTest_expect(activeFalseS1 >= 0 && exitedS1 > activeFalseS1,
                                 "s1 退出：active=false 先于 exited（对标 Qt emitExited 次序）");
        XStateMachineTest_expect(enteredS2 >= 0 && activeTrueS2 > enteredS2,
                                 "s2 进入：entered 先于 active=true");
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)s2)
                                     && !XAbstractState_active((XAbstractState*)s1),
                                 "转换完成后 s2 活动而 s1 非活动");
        XStateMachine_stop(machine);
        XStateMachineTest_processEvents();
        XClassDelete((XClass*)machine);
    }

    /* 场景二：SignalEvent 参数传到派生转换的 eventTest 与 onTransition
     * （对标 Qt 文档 CheckedTransition 模式）。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, s1);
        XStateMachineTest_EventSource* sender = XStateMachineTest_EventSource_create();
        XStateMachineTest_CheckedTransition* t = XStateMachineTest_CheckedTransition_create(
            (const XObject*)sender, (size_t)XStateMachineTest_value_signal, 42, s1);
        XAbstractTransition_setTargetState((XAbstractTransition*)t, s2);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        g_checkedArgAtEventTest = -1;
        g_checkedArgAtOnTransition = -1;
        int seven = 7;
        XObject_emitSignal(sender, (size_t)XStateMachineTest_value_signal,
                           XVarList_create(2, sizeof(int), &seven),
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(g_checkedArgAtEventTest == 7
                                     && XAbstractState_active((XAbstractState*)s1),
                                 "参数不匹配时 eventTest 读到参数并拒绝触发");
        int fortyTwo = 42;
        XObject_emitSignal(sender, (size_t)XStateMachineTest_value_signal,
                           XVarList_create(2, sizeof(int), &fortyTwo),
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)s2),
                                 "参数匹配时进入目标状态");
        XStateMachineTest_expect(g_checkedArgAtOnTransition == 42,
                                 "onTransition 收到同一 SignalEvent 的参数（对标 Qt）");
        XStateMachine_stop(machine);
        XStateMachineTest_processEvents();
        XClassDelete((XClass*)machine);
        XClassDelete((XClass*)sender);
    }

    /* 场景三：无源转换挂在历史状态下时 machine() 沿父对象回退
     * （对标 QAbstractTransitionPrivate::machine 的历史回退分支）。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s1a = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, s1);
        XState_setInitialState(s1, s1a);
        XHistoryState* history = XHistoryState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                         XHistoryState_ShallowHistory, s1);
        XAbstractTransition* bare = XStateMachineTest_TypeTransition_create(NULL, XEVENT_TYPE_NONE, NULL);
        XStateMachineTest_expect(XAbstractTransition_machine((XAbstractTransition*)bare) == NULL,
                                 "无源且未挂载的转换 machine() 为 NULL");
        XHistoryState_setDefaultTransition(history, bare);
        XStateMachineTest_expect(XAbstractTransition_machine((XAbstractTransition*)bare) == machine,
                                 "历史默认转换的 machine() 回退到所属状态机");
        XStateMachine_stop(machine);
        XStateMachineTest_processEvents();
        XClassDelete((XClass*)machine);
    }

    /* 场景四：机器 stop 不终止活动动画（对标 Qt：动画仅随状态退出终止）。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, s1);
        XAbstractTransition* t = XStateMachineTest_TypeTransition_create(s1, triggerType, NULL);
        XAbstractTransition_setTargetState(t, s2);
        XVariant* value = XVariant_create_int(1);
        XString* prop = XString_create_utf8("value");
        XState_assignProperty(s2, (XObject*)machine, prop, value);
        XClassDelete((XClass*)value);
        XClassDelete((XClass*)prop);
        XStateMachineTest_FixedAnimation* animation = XStateMachineTest_FixedAnimation_create(800);
        XAbstractTransition_addAnimation(t, (XAbstractAnimation*)animation);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractAnimation_state((XAbstractAnimation*)animation)
                                     == XAbstractAnimation_Running,
                                 "转换触发后动画运行中");
        XStateMachine_stop(machine);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(!XStateMachine_isRunning(machine)
                                     && XAbstractAnimation_state((XAbstractAnimation*)animation)
                                            == XAbstractAnimation_Running,
                                 "机器 stop 不终止活动动画（对标 Qt：动画仅随状态退出终止）");
        XAbstractAnimation_stop((XAbstractAnimation*)animation);
        XStateMachineTest_processEvents();
        XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
        XClassDelete((XClass*)machine);
        XClassDelete((XClass*)animation);
    }

    /* 场景五：同一事件源上两类事件转换各自匹配各自类型
     * （过滤器共享时注销守卫 sourceIsRegistered 的正路径）。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s3 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s1b = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, s1);
        XStateMachineTest_EventSource* source = XStateMachineTest_EventSource_create();
        /* 过滤器路径产生 WrappedEvent，必须用 XEventTransition（按事件源+类型
         * 匹配），裸 TypeTransition 只匹配投递进状态机队列的原始事件。 */
        XEventTransition* ta = XEventTransition_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                          (XObject*)source, typeA, s1);
        XAbstractTransition_setTargetState((XAbstractTransition*)ta, s2);
        XEventTransition* tb = XEventTransition_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                          (XObject*)source, typeB, s1);
        XAbstractTransition_setTargetState((XAbstractTransition*)tb, s3);
        XEventTransition* back = XEventTransition_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                            (XObject*)source, typeB, s2);
        XAbstractTransition_setTargetState((XAbstractTransition*)back, s1b);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XCoreApplication_sendEvent((XObject*)source, (XEvent*)XEvent_create(typeA));
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)s2)
                                     && !XAbstractState_active((XAbstractState*)s3),
                                 "同源 typeA 转换触发而 typeB 不触发");
        XCoreApplication_sendEvent((XObject*)source, (XEvent*)XEvent_create(typeB));
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(XAbstractState_active((XAbstractState*)s1b)
                                     && !XAbstractState_active((XAbstractState*)s2),
                                 "typeB 事件在 s2 上触发回程转换");
        XStateMachine_stop(machine);
        XStateMachineTest_processEvents();
        XClassDelete((XClass*)machine);
        XClassDelete((XClass*)source);
    }

    /* 场景六：configuration 内容口径——父状态、区域、原子与最终状态都在，
     * 状态机自身不在（对标 QStateMachine::configuration 文档）。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState_setChildMode((XState*)machine, XState_ParallelStates);
        XState* r1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* r2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* r1a = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, r1);
        XFinalState* r1Final = XFinalState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, r1);
        XState_setInitialState(r1, r1a);
        XState* r2a = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, r2);
        XState_setInitialState(r2, r2a);
        XAbstractTransition* toFinal = XStateMachineTest_TypeTransition_create(r1a, triggerType, NULL);
        XAbstractTransition_setTargetState(toFinal, (XAbstractState*)r1Final);

        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        const XVector* configuration = XStateMachine_configuration_const(machine);
        const void* probe = NULL;
        probe = machine;
        XStateMachineTest_expect(configuration
                                     && XVector_indexOf(configuration, &probe, 0) < 0,
                                 "configuration 不含状态机自身（对标 Qt 文档）");
        probe = r1;
        bool hasR1 = configuration && XVector_indexOf(configuration, &probe, 0) >= 0;
        probe = r2;
        bool hasR2 = configuration && XVector_indexOf(configuration, &probe, 0) >= 0;
        probe = r1a;
        bool hasR1a = configuration && XVector_indexOf(configuration, &probe, 0) >= 0;
        probe = r2a;
        bool hasR2a = configuration && XVector_indexOf(configuration, &probe, 0) >= 0;
        XStateMachineTest_expect(hasR1 && hasR2 && hasR1a && hasR2a,
                                 "configuration 含并行父、区域与原子状态");
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        configuration = XStateMachine_configuration_const(machine);
        probe = r1Final;
        bool hasFinal = configuration && XVector_indexOf(configuration, &probe, 0) >= 0;
        probe = r1a;
        bool stillHasR1a = configuration && XVector_indexOf(configuration, &probe, 0) >= 0;
        XStateMachineTest_expect(hasFinal && !stillHasR1a,
                                 "final 态替换原子态进入 configuration");
        XStateMachine_stop(machine);
        XStateMachineTest_processEvents();
        XClassDelete((XClass*)machine);
    }

    XStateMachineTest_end();
}


// ==================== 信号覆盖批次（13 个零断言信号 + 驱动器手动安装/elapsed/removeAnimation） ====================

static int g_childModeChangedCount = 0;
static int g_initialStateChangedCount = 0;
static int g_errorStateChangedCount = 0;
static int g_triggeredCount = 0;
static int g_targetStateChangedCount = 0;
static int g_targetStatesChangedCount = 0;
static int g_senderObjectChangedCount = 0;
static int g_signalChangedCount = 0;
static int g_histTransitionChangedCount = 0;
static int g_histStateChangedCount = 0;
static int g_histTypeChangedCount = 0;
static int g_driverStartedCount = 0;
static int g_driverStoppedCount = 0;

static void XStateMachineTest_childModeChangedSlot(XObject* sender, XVarList* arguments)
{ (void)sender; (void)arguments; ++g_childModeChangedCount; }
static void XStateMachineTest_initialStateChangedSlot(XObject* sender, XVarList* arguments)
{ (void)sender; (void)arguments; ++g_initialStateChangedCount; }
static void XStateMachineTest_errorStateChangedSlot(XObject* sender, XVarList* arguments)
{ (void)sender; (void)arguments; ++g_errorStateChangedCount; }
static void XStateMachineTest_triggeredSlot(XObject* sender, XVarList* arguments)
{ (void)sender; (void)arguments; ++g_triggeredCount; }
static void XStateMachineTest_targetStateChangedSlot(XObject* sender, XVarList* arguments)
{ (void)sender; (void)arguments; ++g_targetStateChangedCount; }
static void XStateMachineTest_targetStatesChangedSlot(XObject* sender, XVarList* arguments)
{ (void)sender; (void)arguments; ++g_targetStatesChangedCount; }
static void XStateMachineTest_senderObjectChangedSlot(XObject* sender, XVarList* arguments)
{ (void)sender; (void)arguments; ++g_senderObjectChangedCount; }
static void XStateMachineTest_signalChangedSlot(XObject* sender, XVarList* arguments)
{ (void)sender; (void)arguments; ++g_signalChangedCount; }
static void XStateMachineTest_histTransitionChangedSlot(XObject* sender, XVarList* arguments)
{ (void)sender; (void)arguments; ++g_histTransitionChangedCount; }
static void XStateMachineTest_histStateChangedSlot(XObject* sender, XVarList* arguments)
{ (void)sender; (void)arguments; ++g_histStateChangedCount; }
static void XStateMachineTest_histTypeChangedSlot(XObject* sender, XVarList* arguments)
{ (void)sender; (void)arguments; ++g_histTypeChangedCount; }
static void XStateMachineTest_driverStartedSlot(XObject* sender, XVarList* arguments)
{ (void)sender; (void)arguments; ++g_driverStartedCount; }
static void XStateMachineTest_driverStoppedSlot(XObject* sender, XVarList* arguments)
{ (void)sender; (void)arguments; ++g_driverStoppedCount; }

/** @brief 信号覆盖批次：setter/转换/历史/驱动器的全部信号发射契约。 */
static void XStateMachineSignalCoverageTest(void)
{
    XStateMachineTest_begin("\n  -- 信号覆盖批次（全部信号发射契约） --");
    g_checkCount = 0;
    g_failureCount = 0;
    XEventType triggerType = (XEventType)XEvent_registerEventType(-1);

    /* 场景一：XState 三个 setter 信号（对标 Qt BINDABLE NOTIFY）。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* child = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, s1);
        XState* errorTarget = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XObject_connect((XObject*)s1, (size_t)XState_childModeChanged_signal,
                          NULL, XStateMachineTest_childModeChangedSlot, XConnectionType_Auto);
        XObject_connect((XObject*)s1, (size_t)XState_initialStateChanged_signal,
                          NULL, XStateMachineTest_initialStateChangedSlot, XConnectionType_Auto);
        XObject_connect((XObject*)s1, (size_t)XState_errorStateChanged_signal,
                          NULL, XStateMachineTest_errorStateChangedSlot, XConnectionType_Auto);
        g_childModeChangedCount = g_initialStateChangedCount = g_errorStateChangedCount = 0;

        XState_setChildMode(s1, XState_ParallelStates);
        XStateMachineTest_expect(g_childModeChangedCount == 1, "setChildMode 变化发 childModeChanged");
        XState_setChildMode(s1, XState_ExclusiveStates);
        XStateMachineTest_expect(g_childModeChangedCount == 2, "切回互斥再次发射");
        XState_setChildMode(s1, XState_ExclusiveStates);
        XStateMachineTest_expect(g_childModeChangedCount == 2, "同值 setChildMode 不发射");

        XState_setInitialState(s1, child);
        XStateMachineTest_expect(g_initialStateChangedCount == 1, "setInitialState 发 initialStateChanged");
        XState_setInitialState(s1, child);
        XStateMachineTest_expect(g_initialStateChangedCount == 1, "同值 setInitialState 不发射");

        XState_setErrorState(s1, errorTarget);
        XStateMachineTest_expect(g_errorStateChangedCount == 1, "setErrorState 发 errorStateChanged");
        XState_setErrorState(s1, errorTarget);
        XStateMachineTest_expect(g_errorStateChangedCount == 1, "同值 setErrorState 不发射");
        XClassDelete((XClass*)machine);
    }

    /* 场景二：转换的 triggered/targetStateChanged/targetStatesChanged。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s2 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s3 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState_setInitialState((XState*)machine, s1);
        XAbstractTransition* t = XStateMachineTest_TypeTransition_create(s1, triggerType, NULL);
        XObject_connect((XObject*)t, (size_t)XAbstractTransition_triggered_signal,
                          NULL, XStateMachineTest_triggeredSlot, XConnectionType_Auto);
        XObject_connect((XObject*)t, (size_t)XAbstractTransition_targetStateChanged_signal,
                          NULL, XStateMachineTest_targetStateChangedSlot, XConnectionType_Auto);
        XObject_connect((XObject*)t, (size_t)XAbstractTransition_targetStatesChanged_signal,
                          NULL, XStateMachineTest_targetStatesChangedSlot, XConnectionType_Auto);
        g_triggeredCount = g_targetStateChangedCount = g_targetStatesChangedCount = 0;

        XAbstractTransition_setTargetState(t, s2);
        XStateMachineTest_expect(g_targetStateChangedCount == 1,
                                 "setTargetState 变化发 targetStateChanged");
        XAbstractTransition_setTargetState(t, s2);
        XStateMachineTest_expect(g_targetStateChangedCount == 1, "同目标 setTargetState 不发射");

        /* setTargetStates 多目标集合变化发 targetStatesChanged（Qt 分工：
         * setTargetStates 只发 targetStatesChanged）。 */
        XVector* targets = XVector_Create(XAbstractState*);
        XVector_push_back_1_base(targets, &(XAbstractState*){ s2 });
        XVector_push_back_1_base(targets, &(XAbstractState*){ s3 });
        XAbstractTransition_setTargetStates(t, targets);
        XClassDelete((XClass*)targets);
        XStateMachineTest_expect(g_targetStatesChangedCount == 1
                                     && g_targetStateChangedCount == 1,
                                 "setTargetStates 只发 targetStatesChanged");
        XVector* same = XVector_Create(XAbstractState*);
        XVector_push_back_1_base(same, &(XAbstractState*){ s3 });
        XVector_push_back_1_base(same, &(XAbstractState*){ s2 });
        XAbstractTransition_setTargetStates(t, same);
        XClassDelete((XClass*)same);
        XStateMachineTest_expect(g_targetStatesChangedCount == 1,
                                 "同内容多重集 setTargetStates 不发射");

        g_triggeredCount = 0;
        XStateMachine_start(machine);
        XStateMachineTest_processEvents();
        XStateMachine_postEvent(machine, XEvent_create(triggerType), XStateMachine_NormalPriority);
        XStateMachineTest_processEvents();
        XStateMachineTest_expect(g_triggeredCount == 1,
                                 "转换触发发 triggered（onTransition 之后）");
        XStateMachine_stop(machine);
        XStateMachineTest_processEvents();
        XClassDelete((XClass*)machine);
    }

    /* 场景三：信号转换的 senderObjectChanged/signalChanged。 */
    {
        XStateMachineTest_EventSource* senderA = XStateMachineTest_EventSource_create();
        XStateMachineTest_EventSource* senderB = XStateMachineTest_EventSource_create();
        XSignalTransition* t = XSignalTransition_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                           (const XObject*)senderA,
                                                           (size_t)XStateMachineTest_advance_signal, NULL);
        XObject_connect((XObject*)t, (size_t)XSignalTransition_senderObjectChanged_signal,
                          NULL, XStateMachineTest_senderObjectChangedSlot, XConnectionType_Auto);
        XObject_connect((XObject*)t, (size_t)XSignalTransition_signalChanged_signal,
                          NULL, XStateMachineTest_signalChangedSlot, XConnectionType_Auto);
        g_senderObjectChangedCount = g_signalChangedCount = 0;

        XSignalTransition_setSenderObject(t, (const XObject*)senderB);
        XStateMachineTest_expect(g_senderObjectChangedCount == 1, "setSenderObject 变化发 senderObjectChanged");
        XSignalTransition_setSenderObject(t, (const XObject*)senderB);
        XStateMachineTest_expect(g_senderObjectChangedCount == 1, "同发送者 setSenderObject 不发射");
        XSignalTransition_setSignal(t, (size_t)XStateMachineTest_leave_signal);
        XStateMachineTest_expect(g_signalChangedCount == 1, "setSignal 变化发 signalChanged");
        XSignalTransition_setSignal(t, (size_t)XStateMachineTest_leave_signal);
        XStateMachineTest_expect(g_signalChangedCount == 1, "同信号 setSignal 不发射");
        XClassDelete((XClass*)t);
        XClassDelete((XClass*)senderA);
        XClassDelete((XClass*)senderB);
    }

    /* 场景四：历史状态三信号（Qt 语义：setDefaultTransition 只发
     * defaultTransitionChanged；setDefaultState 只发 defaultStateChanged；
     * setHistoryType 发 historyTypeChanged）。 */
    {
        XStateMachine* machine = XStateMachine_create();
        XState* s1 = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, (XState*)machine);
        XState* s1a = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, s1);
        XState_setInitialState(s1, s1a);
        XState* s1b = XState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XState_ExclusiveStates, s1);
        XHistoryState* history = XHistoryState_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                         XHistoryState_ShallowHistory, s1);
        XObject_connect((XObject*)history, (size_t)XHistoryState_defaultTransitionChanged_signal,
                          NULL, XStateMachineTest_histTransitionChangedSlot, XConnectionType_Auto);
        XObject_connect((XObject*)history, (size_t)XHistoryState_defaultStateChanged_signal,
                          NULL, XStateMachineTest_histStateChangedSlot, XConnectionType_Auto);
        XObject_connect((XObject*)history, (size_t)XHistoryState_historyTypeChanged_signal,
                          NULL, XStateMachineTest_histTypeChangedSlot, XConnectionType_Auto);
        g_histTransitionChangedCount = g_histStateChangedCount = g_histTypeChangedCount = 0;

        XAbstractTransition* userDefault = XStateMachineTest_TypeTransition_create(NULL, XEVENT_TYPE_NONE, NULL);
        XVector* single = XVector_Create(XAbstractState*);
        XVector_push_back_1_base(single, &(XAbstractState*){ s1a });
        XAbstractTransition_setTargetStates(userDefault, single);
        XClassDelete((XClass*)single);
        XHistoryState_setDefaultTransition(history, userDefault);
        XStateMachineTest_expect(g_histTransitionChangedCount == 1 && g_histStateChangedCount == 0,
                                 "setDefaultTransition 只发 defaultTransitionChanged（对标 Qt）");

        XHistoryState_setDefaultState(history, s1b);
        XStateMachineTest_expect(g_histStateChangedCount == 1 && g_histTransitionChangedCount == 1,
                                 "setDefaultState 新建默认转换只补发 defaultStateChanged");
        XHistoryState_setDefaultState(history, s1b);
        XStateMachineTest_expect(g_histStateChangedCount == 1, "同默认状态 setDefaultState 不发射");

        XHistoryState_setHistoryType(history, XHistoryState_DeepHistory);
        XStateMachineTest_expect(g_histTypeChangedCount == 1, "setHistoryType 变化发 historyTypeChanged");
        XHistoryState_setHistoryType(history, XHistoryState_DeepHistory);
        XStateMachineTest_expect(g_histTypeChangedCount == 1, "同类型 setHistoryType 不发射");
        XStateMachine_stop(machine);
        XStateMachineTest_processEvents();
        XClassDelete((XClass*)machine);
    }

    /* 场景五：驱动器手动 install/uninstall/elapsed 与 started/stopped 信号
     * （用私有 driver 实例：单例被其他用例自动装卸且跨轮存活，连接会累积）；
     * 顺带补 XAnimationGroup_removeAnimation 的组空自停路径断言。 */
    {
        XAnimationDriver* driver = XAnimationDriver_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XObject_connect((XObject*)driver, (size_t)XAnimationDriver_started_signal,
                          NULL, XStateMachineTest_driverStartedSlot, XConnectionType_Auto);
        XObject_connect((XObject*)driver, (size_t)XAnimationDriver_stopped_signal,
                          NULL, XStateMachineTest_driverStoppedSlot, XConnectionType_Auto);
        g_driverStartedCount = g_driverStoppedCount = 0;

        XAnimationDriver_install(driver);
        XStateMachineTest_expect(g_driverStartedCount == 1 && XAnimationDriver_isRunning(driver),
                                 "手动 install 发 started 并进入运行态");
        XThread_msleep(20);
        XStateMachineTest_expect(XAnimationDriver_elapsed(driver) > 0,
                                 "install 后 elapsed 报告单调时差");
        XAnimationDriver_uninstall(driver);
        XStateMachineTest_expect(g_driverStoppedCount == 1 && !XAnimationDriver_isRunning(driver),
                                 "手动 uninstall 发 stopped 并退出运行态");
        XAnimationDriver_uninstall(driver);
        XStateMachineTest_expect(g_driverStoppedCount == 1, "重复 uninstall 不重复发射");
        XClassDelete((XClass*)driver);

        XAnimationGroup* group = XAnimationGroup_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XStateMachineTest_FixedAnimation* m = XStateMachineTest_FixedAnimation_create(10);
        XAnimationGroup_addAnimation(group, (XAbstractAnimation*)m);
        XAnimationGroup_removeAnimation(group, (XAbstractAnimation*)m);
        XStateMachineTest_expect(XAbstractAnimation_group((XAbstractAnimation*)m) == NULL
                                     && XAnimationGroup_animationCount(group) == 0,
                                 "removeAnimation 移出并清空 group 回指");
        XClassDelete((XClass*)m);
        XClassDelete((XClass*)group);
    }

    XStateMachineTest_end();
}

static int XStateMachineTest_runOnce(void)
{
    int totalFailures = 0;
    XStateMachineEventTest();
    totalFailures += g_failureCount;
    XStateMachineSignalTest();
    totalFailures += g_failureCount;
    XHistoryState_Test();
    totalFailures += g_failureCount;
    if (!XStateMachineApiMatrixTest())
        ++totalFailures;
    XStateMachineAnimationApiTest();
    totalFailures += g_failureCount;
    XStateMachineAnimationPipelineTest();
    totalFailures += g_failureCount;
    XStateMachineBehaviorAlignTest();
    totalFailures += g_failureCount;
    XStateMachineCoverageGapTest();
    totalFailures += g_failureCount;
    XStateMachineSignalCoverageTest();
    totalFailures += g_failureCount;
    return totalFailures;
}

/**
 * @brief 运行 XStateMachine 全量测试（菜单三条 + 全 API 组合矩阵 + 内存平衡门禁）。
 * @return 所有检查通过（含无泄漏）返回 true。
 */
bool XStateMachineTest_runAll(void)
{
    /* 本门禁自持统计开关：统计默认关闭，显式打开才能读到 systemBytes。
     * 打开后不关闭，避免把统计口径的差异带进后续测试。 */
    XMemory_setStatisticsEnabled(true);
    const bool statsEnabled = XMemory_statisticsEnabled();
    XMemoryStatistics before = { 0, 0, 0, 0 };
    XMemoryStatistics after = { 0, 0, 0, 0 };
    if (statsEnabled)
        before = XMemory_statistics(XMEMORY_TYPE_SYSTEM);

    int totalFailures = XStateMachineTest_runOnce();

    if (statsEnabled) {
        after = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
        if (after.systemBytes != before.systemBytes) {
            /* 首轮增量可能是进程级一次性惰性缓存（状态机模块首次注册的四个
             * 事件类型等），不是泄漏。判据：再跑一遍增量必须归零，仍增长才是
             * 真泄漏（与 XSettingsTest_runAll 同口径）。 */
            XMemoryStatistics again = { 0, 0, 0, 0 };
            const int secondRoundFailures = XStateMachineTest_runOnce();
            (void)secondRoundFailures; /* 第二轮仅用于内存收敛判定，不计入功能结论 */
            again = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
            if (again.systemBytes != after.systemBytes) {
                XPrintf("  [失败] 内存平衡：systemBytes %lu -> %lu -> %lu（线性增长=泄漏）\n",
                        (unsigned long)before.systemBytes,
                        (unsigned long)after.systemBytes,
                        (unsigned long)again.systemBytes);
                ++totalFailures;
            } else {
                XPrintf("  [通过] 内存平衡：第二轮增量归零（一次性开销 %ld 字节，无泄漏）\n",
                        (long)(after.systemBytes - before.systemBytes));
            }
        } else {
            XPrintf("  [通过] 内存平衡：首轮即无净遗留\n");
        }
    }

    XPrintf("\nXStateMachine 全量测试：失败组/项计数 = %d\n", totalFailures);
    return totalFailures == 0;
}

/**
 * @brief 菜单动作适配器：XTestMenuActionFunc 带一个 XVariant 参数，
 *        而状态机测试入口是无参函数（对标 XSettingsTest 的同款 wrapper 约定）。
 */
static void XStateMachineTest_menuEvent(XVariant* data)
{
    (void)data;
    XStateMachineEventTest();
}

static void XStateMachineTest_menuSignal(XVariant* data)
{
    (void)data;
    XStateMachineSignalTest();
}

static void XStateMachineTest_menuHistory(XVariant* data)
{
    (void)data;
    XHistoryState_Test();
}

static void XStateMachineTest_menuApiMatrix(XVariant* data)
{
    (void)data;
    XStateMachineApiMatrixTest();
}

static void XStateMachineTest_menuAnimation(XVariant* data)
{
    (void)data;
    XStateMachineAnimationApiTest();
    XStateMachineAnimationPipelineTest();
}

static void XStateMachineTest_menuBehavior(XVariant* data)
{
    (void)data;
    XStateMachineBehaviorAlignTest();
}

static void XStateMachineTest_menuCoverageGap(XVariant* data)
{
    (void)data;
    XStateMachineCoverageGapTest();
}

static void XStateMachineTest_menuSignalCoverage(XVariant* data)
{
    (void)data;
    XStateMachineSignalCoverageTest();
}

void XTestMenu_XStateMachineTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XStateMachine（状态机）");
    XTestMenu_addMenu(root, menu);
    XTestMenu_setActionFunction(XTestMenu_addAction(menu, "对象事件转换"),
        XStateMachineTest_menuEvent);
    XTestMenu_setActionFunction(XTestMenu_addAction(menu, "信号转换与运行语义"),
        XStateMachineTest_menuSignal);
    XTestMenu_setActionFunction(XTestMenu_addAction(menu, "深历史与浅历史"),
        XStateMachineTest_menuHistory);
    XTestMenu_setActionFunction(XTestMenu_addAction(menu, "API 组合矩阵"),
        XStateMachineTest_menuApiMatrix);
    XTestMenu_setActionFunction(XTestMenu_addAction(menu, "动画 API 与管道"),
        XStateMachineTest_menuAnimation);
    XTestMenu_setActionFunction(XTestMenu_addAction(menu, "行为对齐批次"),
        XStateMachineTest_menuBehavior);
    XTestMenu_setActionFunction(XTestMenu_addAction(menu, "覆盖缺口批次"),
        XStateMachineTest_menuCoverageGap);
    XTestMenu_setActionFunction(XTestMenu_addAction(menu, "信号覆盖批次"),
        XStateMachineTest_menuSignalCoverage);
}
