#include"XCodeTest.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XStateMachine.h"
#include"XState.h"
#include"XFinalState.h"
#include"XHistoryState.h"
#include"XEventTransition.h"
#include"XSignalTransition.h"
#include"XPrintf.h"

// -------------------------- 1. 定义状态回调（用于观察状态切换） --------------------------
static void ParentEntered(XAbstractState* state, XStateMachine* machine) {
    XPrintf("[Parent] 进入状态 (地址: %p)\n", state);
}
static void ParentExited(XAbstractState* state, XStateMachine* machine) {
    XPrintf("[Parent] 退出状态 (地址: %p)\n", state);
    XCoreApplication_quit();
}

static void Child1Entered(XAbstractState* state, XStateMachine* machine) {
    XPrintf("  [Child1] 进入状态 (地址: %p)\n", state);
}
static void Child1Exited(XAbstractState* state, XStateMachine* machine) {
    XPrintf("  [Child1] 退出状态 (地址: %p)\n", state);
}

static void Grandchild1Entered(XAbstractState* state, XStateMachine* machine) {
    XPrintf("    [Grandchild1] 进入状态 (地址: %p) → 设备高速运行\n", state);
    XCoreApplication_quit();
}
static void Grandchild1Exited(XAbstractState* state, XStateMachine* machine) {
    XPrintf("    [Grandchild1] 退出状态 (地址: %p) → 停止高速运行\n", state);
}

static void Child2Entered(XAbstractState* state, XStateMachine* machine) {
    XPrintf("  [Child2] 进入状态 (地址: %p) → 设备待机\n", state);
    XCoreApplication_quit();
}
static void Child2Exited(XAbstractState* state, XStateMachine* machine) {
    XPrintf("  [Child2] 退出状态 (地址: %p) → 退出待机\n", state);
}

// -------------------------- 2. 定义转换条件（始终返回true，简化测试） --------------------------
static bool TransitionCondition(const XEventTransition* transition) {
    XPrintf("\n触发转换 (事件码: %d)\n", transition->m_event->type);
    return true; // 条件满足，允许转换
}

// -------------------------- 3. 主测试函数 --------------------------
void XHistoryState_Test() {
    // -------------------------- 步骤1：创建状态机和所有状态 --------------------------
    XStateMachine* machine = XStateMachine_create();
    if (!machine) {
        XPrintf("状态机创建失败！\n");
        return;
    }
    /*XObject_addEventFilter(machine, 1001, XStateMachine_handleEventCB, machine);
    XObject_addEventFilter(machine, 1002, XStateMachine_handleEventCB, machine);
    XObject_addEventFilter(machine, 1003, XStateMachine_handleEventCB, machine);
    XObject_addEventFilter(machine, 1004, XStateMachine_handleEventCB, machine);*/
    // 父状态（设备总控制）
    XState* parent = XState_create();
    XAbstractState_setEnteredCallback(parent, ParentEntered);
    XAbstractState_setExitedCallback(parent, ParentExited);

    // 子状态1（运行模式）+ 嵌套子状态（高速模式）
    XState* child1 = XState_create();
    XState* grandchild1 = XState_create();
    XAbstractState_setEnteredCallback(child1, Child1Entered);
    XAbstractState_setExitedCallback(child1, Child1Exited);
    XAbstractState_setEnteredCallback(grandchild1, Grandchild1Entered);
    XAbstractState_setExitedCallback(grandchild1, Grandchild1Exited);

    // 子状态2（待机模式）
    XState* child2 = XState_create();
    XAbstractState_setEnteredCallback(child2, Child2Entered);
    XAbstractState_setExitedCallback(child2, Child2Exited);

    // -------------------------- 步骤2：创建历史状态（浅层+深层） --------------------------
    // 浅层历史：仅恢复parent的直接子状态（如child1/child2）
    XHistoryState* shallowHist = XHistoryState_create(XHistoryStateType_Shallow);
    XHistoryState_setDefaultState(shallowHist, (XAbstractState*)child1); // 默认恢复到child1
    XAbstractState_setParentState_base(shallowHist, parent); // 绑定父状态为parent

    // 深层历史：恢复完整嵌套子状态（如child1→grandchild1）
    XHistoryState* deepHist = XHistoryState_create(XHistoryStateType_Deep);
    XHistoryState_setDefaultState(deepHist, (XAbstractState*)child1); // 默认恢复到child1
    XAbstractState_setParentState_base(deepHist, parent); // 绑定父状态为parent

    // -------------------------- 步骤3：配置状态层级关系 --------------------------
    // 1. child1添加嵌套子状态grandchild1，并设置初始子状态为grandchild1
    XState_addState(child1, (XAbstractState*)grandchild1);
    XState_setInitialState(child1, (XAbstractState*)grandchild1);

    // 2. parent添加子状态（child1、child2、shallowHist、deepHist）
    XState_addState(parent, (XAbstractState*)child1);
    XState_addState(parent, (XAbstractState*)child2);
    XState_addState(parent, (XAbstractState*)shallowHist);
    XState_addState(parent, (XAbstractState*)deepHist);
    XState_setInitialState(parent, (XAbstractState*)child1);

    // 3. 状态机添加顶层状态parent，并设置初始状态为parent
    XStateMachine_addState(machine, (XAbstractState*)parent);
    XStateMachine_setInitialState(machine, (XAbstractState*)parent);

    // -------------------------- 步骤4：添加状态转换（用于切换模式） --------------------------
    // 转换1：child1 → child2（事件码：1001）
    XEventTransition* trans1 = XEventTransition_create(1001);
    XAbstractTransition_setSourceState(trans1, (XAbstractState*)child1);
    XAbstractTransition_setTargetState(trans1, (XAbstractState*)child2);
    XAbstractTransition_setCondition(trans1, TransitionCondition);
    XState_addTransition(child1, (XAbstractTransition*)trans1);

    // 转换2：child2 → child1（事件码：1002）
    XEventTransition* trans2 = XEventTransition_create(1002);
    XAbstractTransition_setSourceState(trans2, (XAbstractState*)child2);
    XAbstractTransition_setTargetState(trans2, (XAbstractState*)child1);
    XAbstractTransition_setCondition(trans2, TransitionCondition);
    XState_addTransition(child2, (XAbstractTransition*)trans2);

    // 转换3：parent → shallowHist（通过历史恢复，事件码：1003）
    XEventTransition* trans3 = XEventTransition_create(1003);
    XAbstractTransition_setSourceState(trans3, (XAbstractState*)parent);
    XAbstractTransition_setTargetState(trans3, (XAbstractState*)shallowHist);
    XAbstractTransition_setCondition(trans3, TransitionCondition);
    XState_addTransition(parent, (XAbstractTransition*)trans3);

    // 转换4：parent → deepHist（通过深层历史恢复，事件码：1004）
    XEventTransition* trans4 = XEventTransition_create(1004);
    XAbstractTransition_setSourceState(trans4, (XAbstractState*)parent);
    XAbstractTransition_setTargetState(trans4, (XAbstractState*)deepHist);
    XAbstractTransition_setCondition(trans4, TransitionCondition);
    XState_addTransition(parent, (XAbstractTransition*)trans4);

    // -------------------------- 步骤5：启动状态机并模拟流程 --------------------------
    XPrintf("===================== 1. 启动状态机（初始状态） =====================\n");
    XStateMachine_start(machine); // 自动激活：parent→child1→grandchild1

    // -------------------------- 步骤6：切换到child2（待机模式） --------------------------
    XPrintf("\n===================== 2. 发送事件1001（child1→child2） =====================\n");
    //XObject_postEvent(machine, XEvent_create(1001), XEVENT_PRIORITY_NORMAL);
    //XCoreApplication_processEvents(XEventLoop_AllEvents);
    XCoreApplication_exec();
    // -------------------------- 步骤7：退出parent（触发历史存储） --------------------------
    XPrintf("\n===================== 3. 退出parent（触发历史存储） =====================\n");
    XState_deactivate_base((XAbstractState*)parent); // 退出时自动存储当前状态child2
    //XCoreApplication_exec();
    // -------------------------- 步骤8：重新激活parent，通过浅层历史恢复 --------------------------
    XPrintf("\n===================== 4. 发送事件1003（浅层历史恢复） =====================\n");
    XState_setInitialState(parent, (XAbstractState*)NULL);
    XState_activate_base((XAbstractState*)parent); // 重新进入parent
    //XObject_postEvent(machine, XEvent_create(machine, 1003, 0), XEVENT_PRIORITY_NORMAL);
    XCoreApplication_exec();
    // -------------------------- 步骤9：切换回child1→grandchild1 --------------------------
    XPrintf("\n===================== 5. 发送事件1002（child2→child1） =====================\n");
    //XObject_postEvent(machine, XEvent_create(machine, 1002, 0), XEVENT_PRIORITY_NORMAL); // 进入child1→grandchild1
    XCoreApplication_exec();
    // -------------------------- 步骤10：退出parent（再次触发历史存储） --------------------------
    XPrintf("\n===================== 6. 退出parent（再次触发历史存储） =====================\n");
    XState_deactivate_base((XAbstractState*)parent); // 深层历史存储child1→grandchild1

    // -------------------------- 步骤11：重新激活parent，通过深层历史恢复 --------------------------
    XPrintf("\n===================== 7. 发送事件1004（深层历史恢复） =====================\n");
    XState_activate_base((XAbstractState*)parent); // 重新进入parent
    //XObject_postEvent(machine, XEvent_create(machine, 1004, 0), XEVENT_PRIORITY_NORMAL); // 触发深层历史，恢复到grandchild1

    //// -------------------------- 步骤12：清理资源 --------------------------
    //printf("\n===================== 8. 销毁资源 =====================\n");
    //XState_delete_base(parent);       // 自动销毁所有子状态（child1/child2/grandchild1/历史状态）
    //XEventTransition_destroy(trans1);
    //XEventTransition_destroy(trans2);
    //XEventTransition_destroy(trans3);
    //XEventTransition_destroy(trans4);
    //XStateMachine_destroy(machine);
}

static void StateAEnteredCallback(XAbstractState* state) {
    XPrintf("A进入状态 %p\n", state);
}
static void StateAExitedCallback(XAbstractState* state) {
    XPrintf("A退出状态 %p\n", state);
}
static void StateBEnteredCallback(XAbstractState* state) {
    XPrintf("B进入状态 %p \n", state);
}
static void StateBExitedCallback(XAbstractState* state) {
    XPrintf("B退出状态 %p\n", state);
}
static void FinalStateEnteredCallback(XAbstractState* state) {
    XPrintf("final进入状态 %p\n", state);
}
static void FinalStatExitedCallback(XAbstractState* state) {
    XPrintf("final退出状态 %p\n", state);
}

// 修正条件函数名称，明确是B到最终状态的转换
static bool BtoFinal(const XAbstractTransition* transition) 
{
    XPrintf("B转换到Final条件\n");
    return true;
}
//释放槽函数
static void deleteSlot(XObject* sender, XObject* receiver, void* args)
{
    XPrintf("释放资源\n");
    XStateMachine* machine = sender;
    XState_delete_base(machine->m_initialState);
    XStateMachine_delete_base(sender);
    
    XCoreApplication_quit();
}
void XStateMachineEventTest() 
{
    bool isWhile = false;
    do
    {
        XPrintf("XStateMachine 事件测试\n");
        XStateMachine* machine = XStateMachine_create();
        XObject_connect(machine, XSignal(XStateMachine_stop_signal), machine, deleteSlot, XConnectionType_Queued);
        //XObject_addEventFilter(machine, XEVENT_TRANSITION, XStateMachine_handleEventCB, machine);
        // 创建状态
        XState* stateA = XState_create();
        XState* stateB = XState_create();
        XFinalState* finalState = XFinalState_create();  // 最终状态

        // 设置回调
        XAbstractState_setEnteredCallback(finalState, FinalStateEnteredCallback);
        XAbstractState_setExitedCallback(finalState, FinalStatExitedCallback);
        XAbstractState_setEnteredCallback((XAbstractState*)stateA, StateAEnteredCallback);
        XAbstractState_setExitedCallback((XAbstractState*)stateA, StateAExitedCallback);
        XAbstractState_setEnteredCallback((XAbstractState*)stateB, StateBEnteredCallback);
        XAbstractState_setExitedCallback((XAbstractState*)stateB, StateBExitedCallback);

        // 配置父子关系：stateA包含stateB和finalState，初始子状态为stateB
        XState_addState(stateA, (XAbstractState*)stateB);
        XState_addState(stateA, (XAbstractState*)finalState);  // 添加最终状态作为子状态
        XState_setInitialState(stateA, (XAbstractState*)stateB);

        // 修正1：转换源状态为stateB，目标状态为finalState
        //XEventTransition* transition = XEventTransition_create(XEVENT_TRANSITION);
        //XAbstractTransition_setTargetState(transition, (XAbstractState*)finalState);  // 目标改为最终状态
        //XAbstractTransition_setCondition(transition, BtoFinal);
        //XState_addTransition(stateB, (XAbstractTransition*)transition);  // 源状态改为stateB

        //// 配置状态机
        //XStateMachine_addState(machine, (XAbstractState*)stateA);
        //XStateMachine_setInitialState(machine, (XAbstractState*)stateA);

        //// 启动状态机
        //XStateMachine_start(machine);  // 预期：A进入 → B进入

        //// 触发事件
        //XObject_postEvent(machine, XEvent_create(machine, XEVENT_TRANSITION, 0), XEVENT_PRIORITY_NORMAL);
        
        // 清理资源
        /*现在无法清理资源，deleteSlot 用信号和槽的方式异步清理*/
        //XState_delete_base(stateA);  // 自动销毁子状态和转换
        //XFinalState_destroy(finalState);
        //XStateMachine_delete_base(m_machine);
        //XCoreApplication_quit();
        if(isWhile)XCoreApplication_exec();
    } while (isWhile);
}

void XStateMachineSignalTest()
{
    bool isWhile = false;
    do
    {
        XPrintf("XStateMachine 信号测试\n");
        XStateMachine* machine = XStateMachine_create();
        XObject_connect(machine, XSignal(XStateMachine_stop_signal), machine, deleteSlot, XConnectionType_Queued);
        // 创建状态
        XState* stateA = XState_create();
        XState* stateB = XState_create();
        XFinalState* finalState = XFinalState_create();  // 最终状态

        // 设置回调
        XAbstractState_setEnteredCallback(finalState, FinalStateEnteredCallback);
        XAbstractState_setExitedCallback(finalState, FinalStatExitedCallback);
        XAbstractState_setEnteredCallback((XAbstractState*)stateA, StateAEnteredCallback);
        XAbstractState_setExitedCallback((XAbstractState*)stateA, StateAExitedCallback);
        XAbstractState_setEnteredCallback((XAbstractState*)stateB, StateBEnteredCallback);
        XAbstractState_setExitedCallback((XAbstractState*)stateB, StateBExitedCallback);

        // 配置父子关系：stateA包含stateB和finalState，初始子状态为stateB
        XState_addState(stateA, (XAbstractState*)stateB);
        XState_addState(stateA, (XAbstractState*)finalState);  // 添加最终状态作为子状态
        XState_setInitialState(stateA, (XAbstractState*)stateB);

        // 修正1：转换源状态为stateB，目标状态为finalState
        XSignalTransition* transition = XSignalTransition_create_signal(machine,XSignal(XStateMachine_start_signal));
        XAbstractTransition_setTargetState(transition, (XAbstractState*)finalState);  // 目标改为最终状态
        XAbstractTransition_setCondition(transition, BtoFinal);
        XState_addTransition(stateB, (XAbstractTransition*)transition);  // 源状态改为stateB

        // 配置状态机
        XStateMachine_addState(machine, (XAbstractState*)stateA);
        XStateMachine_setInitialState(machine, (XAbstractState*)stateA);

        // 启动状态机
        XStateMachine_start(machine);  // 预期：A进入 → B进入

        // 清理资源
        /*现在无法清理资源，deleteSlot 用信号和槽的方式异步清理*/
        //XState_delete_base(stateA);  // 自动销毁子状态和转换
        //XFinalState_destroy(finalState);
        //XStateMachine_delete_base(m_machine);
        //XCoreApplication_quit();
        if (isWhile)
            XCoreApplication_exec();
    } while (isWhile);
}
void XMenu_XStateMachineTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XStateMachine(状态机)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "Event测试");
		XAction_setAction(action, XStateMachineEventTest);
	}
    {
        XAction* action = XMenu_addAction(menu, "Signal测试");
        XAction_setAction(action, XStateMachineSignalTest);
    }
}