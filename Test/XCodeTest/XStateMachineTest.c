#include"XCodeTest.h"
#include"XMemory.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XStateMachine.h"
#include"XState.h"
#include"XFinalState.h"
#include"XEventTransition.h"
#include"XPrintf.h"
static void StateAEnteredCallback(XAbstractState* state, XStateMachine* machine) {
    XPrintf("A进入状态 %p\n", state);
}
static void StateAExitedCallback(XAbstractState* state, XStateMachine* machine) {
    XPrintf("A退出状态 %p\n", state);
}
static void StateBEnteredCallback(XAbstractState* state, XStateMachine* machine) {
    XPrintf("B进入状态 %p \n", state);
}
static void StateBExitedCallback(XAbstractState* state, XStateMachine* machine) {
    XPrintf("B退出状态 %p\n", state);
}
// 修正条件函数名称，明确是B到最终状态的转换
static bool BtoFinal(const XAbstractTransition* transition, const XEvent* event) {
    XPrintf("B转换到Final条件\n");
    return true;
}

void XStateMachineTest() {
    XStateMachine* machine = XStateMachine_create();
    XObject_addEventFilter(machine, XEVENT_TRANSITION,XStateMachine_handleEventCB, machine);
    // 创建状态
    XState* stateA = XState_create();
    XState* stateB = XState_create();
    XFinalState* finalState = XFinalState_create();  // 最终状态

    // 设置回调
    XAbstractState_setEnteredCallback((XAbstractState*)stateA, StateAEnteredCallback);
    XAbstractState_setExitedCallback((XAbstractState*)stateA, StateAExitedCallback);
    XAbstractState_setEnteredCallback((XAbstractState*)stateB, StateBEnteredCallback);
    XAbstractState_setExitedCallback((XAbstractState*)stateB, StateBExitedCallback);

    // 配置父子关系：stateA包含stateB和finalState，初始子状态为stateB
    XAbstractState_addState(stateA, (XAbstractState*)stateB);
    XAbstractState_addState(stateA, (XAbstractState*)finalState);  // 添加最终状态作为子状态
    XState_setInitialState(stateA, (XAbstractState*)stateB);

    // 修正1：转换源状态为stateB，目标状态为finalState
    XEventTransition* transition = XEventTransition_create(XEVENT_TRANSITION);
    XAbstractTransition_setTargetState(transition, (XAbstractState*)finalState);  // 目标改为最终状态
    XAbstractTransition_setCondition(transition, BtoFinal);
    XState_addTransition(stateB, (XAbstractTransition*)transition);  // 源状态改为stateB

    // 配置状态机
    XStateMachine_addState(machine, (XAbstractState*)stateA);
    XStateMachine_setInitialState(machine, (XAbstractState*)stateA);

    // 启动状态机
    XStateMachine_start(machine);  // 预期：A进入 → B进入

    // 触发事件
    //XEvent event = { .event.code = XEVENT_TRANSITION };
    //XStateMachine_handleEvent(machine, &event);  // 触发B→Final转换
    XObject_postEvent(machine,XEventMin_create(machine, XEVENT_TRANSITION,0),XEVENT_PRIORITY_NORMAL);
    // 清理资源
    //XState_destroy(stateA);  // 自动销毁子状态和转换
    //XFinalState_destroy(finalState);
    //XStateMachine_destroy(machine);
    //XCoreApplication_requestQuit();
}
void XMenu_XStateMachineTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XStateMachine(状态机)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XStateMachineTest);
	}
}