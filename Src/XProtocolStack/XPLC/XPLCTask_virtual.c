#include "XPLCTask.h"
#include "XMemory.h"
#include "XHash.h"
#include "XHashFunc.h"
#include "XEquality.h"
#include <string.h>
#include <assert.h>
#include <limits.h>
typedef struct TaskStateNode
{
	XPLCTaskStateFunc func;
	void* stateData;
}TaskStateNode;
static bool VXPLCTask_addState(XPLCTask* task, uint16_t state, XPLCTaskStateFunc func, void* stateData);
static bool VXPLCTask_removeState(XPLCTask* task, uint16_t state);
static void VXPLCTask_clearState(XPLCTask* task);
static void VXPLCTask_setState(XPLCTask* task, uint16_t state);
static void VXPLCTask_poll(XPLCTask* task);
static void VXPLCTask_start(XPLCTask* task);//开始任务
static void VXPLCTask_finish(XPLCTask* task);
static void VXIODevice_delete(XPLCTask* task);

static void StartTask(XPLCTask* task);
static void RunTask(XPLCTask* task);
static void FinishTask(XPLCTask* task);
XVtable* XPLCTask_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XPLCTask))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_DEFAULT(XClass_class_init());
	void* table[] = {
		VXPLCTask_addState,VXPLCTask_removeState,
		VXPLCTask_clearState,VXPLCTask_setState,
		VXPLCTask_start,VXPLCTask_finish
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXIODevice_delete);
	XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXPLCTask_poll);
#if SHOWCONTAINERSIZE
	printf("XPLCTask size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
void XPLCTask_init(XPLCTask* task)
{
	if (task == NULL)
		return;
	memset(((XClass*)task) + 1, 0, sizeof(XPLCTask) - sizeof(XClass));
	XObject_init(task);
	XClassGetVtable(task) = XPLCTask_class_init();
	task->m_taskStateMap = XHash_Create(int32_t, TaskStateNode,XHash_murmur3_32,XEquality_int);
	task->m_runTaskState = INT_MIN;
	task->m_lastTaskState = XPLCT_State_ExitTask;//默认退出任务状态
}

bool VXPLCTask_addState(XPLCTask* task, uint16_t state, XPLCTaskStateFunc func, void* stateData)
{
	if(task->m_taskStateMap==NULL)
		return false;
	TaskStateNode node = {func,stateData};
	int32_t s = state;
	XMapBase_insert_base(task->m_taskStateMap,&s,&node);
	task->m_taskNode = NULL;
	return true;
}
bool VXPLCTask_removeState(XPLCTask* task, uint16_t state)
{
	if (task->m_taskStateMap == NULL)
		return false;
	int32_t s = state;
	XMapBase_remove_base(task->m_taskStateMap,&s);
	task->m_taskNode = NULL;
	return true;
}
void VXPLCTask_clearState(XPLCTask* task)
{
	if (task->m_taskStateMap == NULL)
		return ;
	XMapBase_clear_base(task->m_taskStateMap);
	task->m_taskNode = NULL;
}
void VXPLCTask_setState(XPLCTask* task, uint16_t state)
{
	task->m_runTaskState = state;
}
void VXPLCTask_poll(XPLCTask* task)
{
	switch (task->m_lastTaskState)
	{
	case XPLCT_State_Start:StartTask(task); break;
	case XPLCT_State_ExitTask:break;//任务已经退出了
	case XPLCT_State_Finish:FinishTask(task); break;
	default:RunTask(task); break;
	}
}
void VXPLCTask_start(XPLCTask* task)
{
	task->m_lastTaskState = XPLCT_State_Start;
}
void VXPLCTask_finish(XPLCTask* task)
{
	task->m_lastTaskState = XPLCT_State_Finish;
}
void VXIODevice_delete(XPLCTask* task)
{
	if (task->m_lastTaskState != XPLCT_State_ExitTask)
	{
		FinishTask(task);//先结束任务清理资源
	}
	if (task->m_taskStateMap)
		XMapBase_delete_base(task->m_taskStateMap);

	XMemory_free(task);
}

void StartTask(XPLCTask* task)
{
	if (task->m_startFunc&& task->m_startFunc(task,task->m_taskData)&&!XMapBase_isEmpty_base(task->m_taskStateMap))
	{
		//task->m_lastTaskState = XPLCTaskState_RunState;
	}
	else
	{
		task->m_lastTaskState = XPLCT_State_Finish;
	}
}

void RunTask(XPLCTask* task)
{
	TaskStateNode* node = NULL;
	if (task->m_taskNode == NULL|| task->m_lastTaskState != task->m_runTaskState)
	{
		task->m_taskNode =XMapBase_value_base(task->m_taskStateMap, task->m_runTaskState);
		task->m_lastTaskState = task->m_runTaskState;
	}
	node = task->m_taskNode;
	if (node == NULL)
	{//任务状态出错
		task->m_lastTaskState = XPLCT_State_Finish;
		return;
	}
	node->func(task,task->m_taskData,node->stateData);
}

void FinishTask(XPLCTask* task)
{
	if (task->m_finishFunc)
		task->m_finishFunc(task, task->m_taskData);
	task->m_lastTaskState = XPLCT_State_ExitTask;
}
