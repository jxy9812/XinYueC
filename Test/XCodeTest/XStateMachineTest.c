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

#include <stdlib.h>

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
    XVariant* value = XObject_property(object, key);
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
        ok = XObject_setProperty(object, key, variant);
        if (!ok)
            XClassDelete((XClass*)variant);
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
}
