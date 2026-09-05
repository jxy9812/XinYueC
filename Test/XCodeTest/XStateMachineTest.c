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

    XStateMachine_delete_base((XClass*)machine);
    XClass_delete_base((XClass*)eventSource);
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
    XVector_delete_base((XClass*)path);
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

    XStateMachine_delete_base((XClass*)machine);
    XClass_delete_base((XClass*)eventSource);
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
    XEvent_delete_base((XClass*)rejectedEvent);

    XStateMachine_delete_base((XClass*)machine);
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
    XStateMachine_delete_base((XClass*)machine);
    XClass_delete_base((XClass*)sender);
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
    XVector_delete_base((XClass*)targets);
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
    XStateMachine_delete_base((XClass*)machine);
    XClass_delete_base((XClass*)sender);
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
    XStateMachine_delete_base((XClass*)machine);
    XClass_delete_base((XClass*)sender);
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
    XStateMachine_delete_base((XClass*)machine);
    XClass_delete_base((XClass*)sender);
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

    XStateMachine_delete_base((XClass*)machine);
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
    XEvent_delete_base((XClass*)rejectedEvent);

    XStateMachine_stop(machine);
    XStateMachineTest_processEvents();
    XStateMachine_delete_base((XClass*)machine);
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

    XVector_delete_base((XClass*)invalidPath);
    XMouseEventTransition_delete_base((XClass*)mouseTransition);
    XKeyEventTransition_delete_base((XClass*)keyTransition);
    XEventTransition_delete_base((XClass*)eventTransition);
    XSignalTransition_delete_base((XClass*)signalTransition);
    XState_delete_base((XClass*)detached);
    XStateMachine_delete_base((XClass*)otherMachine);
    XStateMachine_delete_base((XClass*)machine);
    XClass_delete_base((XClass*)sender);
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

    XState_delete_base((XClass*)target);
    XStateMachineTest_expect(
        XAbstractTransition_targetState(transition) == NULL
            && XVector_isEmpty_base((const XContainer*)
                XAbstractTransition_targetStates_const(transition)),
        "销毁目标状态会同步清除转换中的受保护目标引用");

    XStateMachine_delete_base((XClass*)machine);
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
    XStateMachine_delete_base((XClass*)machine);
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

    XStateMachine_delete_base((XClass*)machine);
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
    XStateMachine_delete_base((XClass*)machine);
    XClass_delete_base((XClass*)sender);
}

// ==================== 菜单公开测试入口 ====================

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

    XStateMachine_delete_base((XClass*)machine);
    XClass_delete_base((XClass*)eventSource);

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

    XStateMachine_delete_base((XClass*)machine);
    XClass_delete_base((XClass*)sender);

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
    XStateMachine_delete_base((XClass*)machine);
    XClass_delete_base((XClass*)sender);

    XStateMachineTest_shallowHistory();
    XStateMachineTest_end();
}
void XTestMenu_XStateMachineTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XStateMachine（状态机）");
    XTestMenu_addMenu(root, menu);
    XTestMenu_setActionFunction(XTestMenu_addAction(menu, "对象事件转换"),
        XStateMachineEventTest);
    XTestMenu_setActionFunction(XTestMenu_addAction(menu, "信号转换与运行语义"),
        XStateMachineSignalTest);
    XTestMenu_setActionFunction(XTestMenu_addAction(menu, "深历史与浅历史"),
        XHistoryState_Test);
}
