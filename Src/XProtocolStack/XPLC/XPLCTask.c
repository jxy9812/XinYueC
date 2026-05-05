#include "XPLCTask.h"
#include "XMemory.h"
#include <limits.h>
#include <string.h>
XPLCTask* XPLCTask_create()
{
	XPLCTask* plc = XMalloc_System(sizeof(XPLCTask));
	XPLCTask_init(plc);
	return plc;
}
void XPLCTask_setStartFunc(XPLCTask* task, XPLCTaskStartFunc func)
{
	if (task)
		task->m_startFunc = func;
}
void XPLCTask_setFinishFunc(XPLCTask* task, XPLCTaskFinishFunc func)
{
	if (task)
		task->m_finishFunc = func;
}

void XPLCTask_setTaskData(XPLCTask* task, void* taskData)
{
	if (task)
		task->m_taskData = taskData;
}

int XPLCTask_getState(XPLCTask* task)
{
	if (task)
		return task->m_runTaskState;
	return INT_MIN;
}
bool XPLCTask_addState_base(XPLCTask*task, uint16_t state, XPLCTaskStateFunc func, void* stateData)
{
	if (ISNULL(task, "") || ISNULL(XClassGetVtable(task), ""))
		return false ;
	return XClassGetVirtualFunc(task, EXPLCTask_AddTaskState, bool(*)(XPLCTask*, uint16_t, XPLCTaskStateFunc,void*))(task, state,func,stateData);
}

bool XPLCTask_removeState_base(XPLCTask* task, uint16_t state)
{
	if (ISNULL(task, "") || ISNULL(XClassGetVtable(task), ""))
		return false;
	return XClassGetVirtualFunc(task, EXPLCTask_RemoveTaskState, bool(*)(XPLCTask*, uint16_t))(task, state);
}

void XPLCTask_clearState_base(XPLCTask* task)
{
	if (ISNULL(task, "") || ISNULL(XClassGetVtable(task), ""))
		return;
	XClassGetVirtualFunc(task, EXPLCTask_ClearTaskState, void(*)(XPLCTask*))(task);
}

void XPLCTask_setState_base(XPLCTask* task, uint16_t state)
{
	if (ISNULL(task, "") || ISNULL(XClassGetVtable(task), ""))
		return;
	XClassGetVirtualFunc(task, EXPLCTask_SetState, void(*)(XPLCTask*, uint16_t))(task,state);
}
void XPLCTask_start_base(XPLCTask* task)
{
	if (ISNULL(task, "") || ISNULL(XClassGetVtable(task), ""))
		return;
	XClassGetVirtualFunc(task, EXPLCTask_Start, void(*)(XPLCTask*))(task);
}

void XPLCTask_finish_base(XPLCTask* task)
{
	if (ISNULL(task, "") || ISNULL(XClassGetVtable(task), ""))
		return;
	XClassGetVirtualFunc(task, EXPLCTask_Finish, void(*)(XPLCTask*))(task);
}
